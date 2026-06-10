#include "SteamManager.h"

#include <QDir>
#include <QDirIterator>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <memory>

#include "storage/AppSettings.h"

SteamManager::SteamManager(QObject *parent)
    : QObject(parent)
{
    m_webDavConfig = m_webDavSettings.loadConfig();
    m_webDavConnectionStatus = m_webDavConfig.isValid()
                                   ? QStringLiteral("已保存 WebDAV 配置，等待测试连接")
                                   : QStringLiteral("未配置 WebDAV，请填写云同步连接信息");
    loadQuarkGatewaySettings();
    loadAutoSyncSettings();
    m_webDavConnectionStatus = m_quarkCookie.trimmed().isEmpty()
                                   ? QStringLiteral("未连接夸克网盘，请粘贴 Cookie 后点击连接")
                                   : QStringLiteral("已保存夸克 Cookie，等待自动连接夸克网盘");

    ensureDefaultStorageRootInitialized();
    alignLogDirectoryWithSnapshotRoot();
    m_logger.info(QStringLiteral("程序启动完成，日志系统已初始化，当前日志文件：%1").arg(m_logger.logFilePath()));

    connect(&m_metadataClient, &SteamMetadataClient::metadataReady, this,
            [this](const QString &appId, const QVariantMap &metadata) {
                if (m_gameModel.updateMetadata(appId, metadata)) {
                    persistManualGameIfPresent(appId);
                    rebuildInstalledGamesFromModel();
                    saveGameMetadataCacheForApp(appId);
                }
            });

    connect(&m_metadataClient, &SteamMetadataClient::metadataFailed, this,
            [this](const QString &appId, const QString &reason) {
                m_logger.warning(QStringLiteral("Steam 资料获取失败：AppID %1，原因：%2").arg(appId, reason));
                if (m_gameModel.updateMetadataStatus(appId, QStringLiteral("资料获取失败"))) {
                    persistManualGameIfPresent(appId);
                    rebuildInstalledGamesFromModel();
                }
            });

    connect(&m_savePathResolver, &SavePathResolver::savePathResolved, this,
            [this](const QString &appId,
                   const QString &path,
                   const QString &source,
                   const QString &availability,
                   const QString &detail,
                   bool canSync) {
                const QString status = source == QStringLiteral("用户指定")
                                           ? QStringLiteral("已手动指定位置")
                                           : QStringLiteral("已自动识别位置");
                m_logger.info(QStringLiteral("存档路径识别完成：AppID %1，来源：%2，状态：%3，路径：%4")
                                  .arg(appId, source, availability, path));
                if (m_gameModel.updateSavePath(appId, path, status, source, availability, detail, canSync)) {
                    if (canSync) {
                        m_gameModel.updateSnapshotStatus(
                            appId,
                            QStringLiteral("未检查本地快照"),
                            QStringLiteral("存档目录可同步，等待执行本地快照预处理检查"),
                            QString(),
                            QString(),
                            QString(),
                            0,
                            false);
                    }
                    persistManualGameIfPresent(appId);
                    rebuildInstalledGamesFromModel();
                    saveGameMetadataCacheForApp(appId);
                }
            });

    connect(&m_savePathResolver, &SavePathResolver::savePathNeedsManual, this,
            [this](const QString &appId, const QString &reason) {
                const GameInfo currentGame = gameByAppId(appId);
                if (!currentGame.savePath.trimmed().isEmpty()) {
                    m_logger.info(QStringLiteral("保留已有游戏数据缓存中的存档路径：AppID %1，路径：%2")
                                      .arg(appId, currentGame.savePath));
                    return;
                }

                m_logger.warning(QStringLiteral("数据库未获取到存档位置，需要用户手动指定：AppID %1，原因：%2").arg(appId, reason));
                if (m_gameModel.updateSavePath(
                        appId,
                        {},
                        QStringLiteral("需用户手动指定"),
                        QStringLiteral("PCGamingWiki"),
                        QStringLiteral("不可同步"),
                        reason,
                        false)) {
                    persistManualGameIfPresent(appId);
                    rebuildInstalledGamesFromModel();
                }
            });

    connect(&m_pcGamingWikiSearchClient, &PCGamingWikiGameSearchClient::gameSearchResolved, this,
            [this](const QString &requestId,
                   const QString &pageName,
                   const QString &pageId,
                   const QString &steamAppId) {
                GameInfo game = m_pendingManualGames.take(requestId);
                if (!game.isValid()) {
                    return;
                }

                const QString oldAppId = game.appId;
                game.pcGamingWikiPageName = pageName;
                game.pcGamingWikiPageId = pageId;

                if (isNumericAppId(steamAppId)) {
                    game.appId = steamAppId;
                    game.name = pageName.isEmpty() ? game.name : pageName;
                    game.displayName = game.name;
                    game.headerImageUrl = QStringLiteral("https://cdn.cloudflare.steamstatic.com/steam/apps/%1/header.jpg").arg(game.appId);
                    game.metadataStatus = QStringLiteral("PCGamingWiki 已匹配 Steam AppID，正在获取 Steam 中文资料");
                    game.savePathStatus = QStringLiteral("正在识别存档路径");
                    game.savePathSource = QStringLiteral("PCGamingWiki");
                    game.savePathAvailability = QStringLiteral("等待检测");
                    game.savePathDetail = QStringLiteral("已通过 PCGamingWiki 匹配 Steam AppID，等待存档路径识别");
                } else {
                    game.name = pageName.isEmpty() ? game.name : pageName;
                    game.displayName = game.name;
                    game.metadataStatus = QStringLiteral("PCGamingWiki 已匹配页面，但该页面没有 Steam AppID");
                    game.savePathStatus = QStringLiteral("需用户手动指定");
                    game.savePathSource = QStringLiteral("PCGamingWiki");
                    game.savePathAvailability = QStringLiteral("不可同步");
                    game.savePathDetail = QStringLiteral("已找到 PCGamingWiki 页面，但无法获得 Steam AppID，因此不能自动拼接图片或按 AppID 查询存档位置");
                    game.savePathCanSync = false;
                }

                m_manualGameManager.saveManualGame(game);
                upsertGameAndRebuild(game, oldAppId);
                m_logger.info(QStringLiteral("手动添加游戏已匹配 PCGamingWiki：%1，Steam AppID：%2")
                                  .arg(game.displayName, isNumericAppId(game.appId) ? game.appId : QStringLiteral("未匹配")));

                const QList<GameInfo> oneGame = {game};
                if (isNumericAppId(game.appId)) {
                    requestMetadataForGames(oneGame);
                    resolveSavePathsForGames(oneGame);
                }
            });

    connect(&m_pcGamingWikiSearchClient, &PCGamingWikiGameSearchClient::gameSearchFailed, this,
            [this](const QString &requestId, const QString &reason) {
                GameInfo game = m_pendingManualGames.take(requestId);
                if (!game.isValid()) {
                    return;
                }

                game.metadataStatus = QStringLiteral("PCGamingWiki 未匹配到游戏资料");
                game.savePathStatus = QStringLiteral("需用户手动指定");
                game.savePathSource = QStringLiteral("PCGamingWiki");
                game.savePathAvailability = QStringLiteral("不可同步");
                game.savePathDetail = reason;
                game.savePathCanSync = false;

                m_manualGameManager.saveManualGame(game);
                upsertGameAndRebuild(game);
                m_logger.warning(QStringLiteral("手动添加游戏未匹配到 PCGamingWiki：%1，原因：%2").arg(game.displayName, reason));
            });

    connect(&m_processMonitor, &GameProcessMonitor::processStatusChanged, this,
            [this](const QString &appId, const QString &runningStatus) {
                if (m_gameModel.updateRunningStatus(appId, runningStatus)) {
                    persistManualGameIfPresent(appId);
                    rebuildInstalledGamesFromModel();
                }
            });

    connect(&m_processMonitor, &GameProcessMonitor::gameProcessStarted, this,
            [this](const QString &appId, const QString &processName) {
                const GameInfo game = gameByAppId(appId);
                const QString displayName = game.isValid() ? game.displayName : appId;
                m_logger.info(QStringLiteral("游戏进程监控：检测到游戏正在运行，游戏：%1，进程：%2")
                                  .arg(displayName, processName));
            });

    connect(&m_processMonitor, &GameProcessMonitor::gameProcessClosed, this,
            [this](const QString &appId, const QString &processName) {
                const GameInfo game = gameByAppId(appId);
                const QString displayName = game.isValid() ? game.displayName : appId;
                m_logger.info(QStringLiteral("游戏进程监控：检测到游戏已关闭，游戏：%1，进程：%2，后续阶段将触发云同步检查")
                                  .arg(displayName, processName));
                handleGameClosedForAutoSync(appId, processName);
            });

    connect(&m_webDavClient, &WebDavClient::connectionTestFinished, this,
            [this](bool success, const QString &message) {
                setWebDavTesting(false);
                setWebDavConnectionStatus(message);
                if (success) {
                    m_logger.info(QStringLiteral("WebDAV 连接测试成功：%1").arg(message));
                } else {
                    m_logger.warning(QStringLiteral("WebDAV 连接测试失败：%1").arg(message));
                }
            });

    connect(&m_webDavClient, &WebDavClient::fileUploadFinished, this,
            [this](bool success,
                   const QString &localFilePath,
                   const QString &remoteFilePath,
                   const QString &uploadState,
                   const QString &message) {
                handleSnapshotUploadFinished(success, localFilePath, remoteFilePath, uploadState, message);
            });

    connect(&m_webDavClient, &WebDavClient::fileDownloadFinished, this,
            [this](bool success,
                   const QString &remoteFilePath,
                   const QString &localFilePath,
                   const QString &message) {
                handleSnapshotDownloadFinished(success, remoteFilePath, localFilePath, message);
            });

    connect(&m_quarkGatewayManager, &QuarkGatewayManager::statusChanged, this,
            [this](const QString &status) {
                setQuarkGatewayStatus(status);
            });

    connect(&m_quarkGatewayManager, &QuarkGatewayManager::fileUploadFinished, this,
            [this](bool success,
                   const QString &localFilePath,
                   const QString &remoteFilePath,
                   const QString &uploadState,
                   const QString &message) {
                handleSnapshotUploadFinished(success, localFilePath, remoteFilePath, uploadState, message);
            });

    connect(&m_quarkGatewayManager, &QuarkGatewayManager::fileDownloadFinished, this,
            [this](bool success,
                   const QString &remoteFilePath,
                   const QString &localFilePath,
                   const QString &message) {
                handleSnapshotDownloadFinished(success, remoteFilePath, localFilePath, message);
            });

    connect(&m_quarkGatewayManager, &QuarkGatewayManager::dataFileUploadFinished, this,
            [this](bool success,
                   const QString &remoteFilePath,
                   const QString &operationId,
                   const QString &message) {
                handleCloudDataUploadFinished(success, remoteFilePath, operationId, message);
            });

    connect(&m_quarkGatewayManager, &QuarkGatewayManager::dataFileDownloadFinished, this,
            [this](bool success,
                   const QString &remoteFilePath,
                   const QString &operationId,
                   const QByteArray &data,
                   const QString &message) {
                handleCloudDataDownloadFinished(success, remoteFilePath, operationId, data, message);
            });

    connect(&m_quarkGatewayManager, &QuarkGatewayManager::gatewayReady, this,
            [this](const WebDavConfig &config, const QString &message) {
                if (m_webDavSettings.saveConfig(config)) {
                    m_webDavConfig = m_webDavSettings.loadConfig();
                    emit webDavSettingsChanged();
                } else {
                    m_logger.warning(QStringLiteral("夸克网关已就绪，但 WebDAV 配置保存失败"));
                }

                setQuarkGatewayStatus(message);
                setWebDavTesting(false);
                setWebDavConnectionStatus(QStringLiteral("夸克网盘连接成功，可以上传和下载快照"));
                m_logger.info(QStringLiteral("夸克本机网关已就绪：%1，远程目录 %2")
                                  .arg(m_webDavConfig.serverUrl, m_webDavConfig.remoteRootPath));
                m_cloudManifestLoaded = false;
                m_cloudManifestRefreshInFlight = false;
                m_cloudDirectoryByAppId.clear();
                refreshCloudManifestFromRemote();
                startQuarkCookieHealthCheck();
            });

    connect(&m_quarkGatewayManager, &QuarkGatewayManager::gatewayFailed, this,
            [this](const QString &message) {
                setWebDavTesting(false);
                setQuarkGatewayStatus(message);
                setWebDavConnectionStatus(QStringLiteral("夸克本机网关未就绪，暂不能上传或下载"));
                m_logger.warning(QStringLiteral("夸克本机网关配置失败：%1").arg(message));
            });

    if (!m_quarkCookie.trimmed().isEmpty()) {
        QTimer::singleShot(0, this, [this]() {
            setWebDavTesting(true);
            setWebDavConnectionStatus(QStringLiteral("正在自动连接夸克网盘"));
            setQuarkGatewayStatus(QStringLiteral("已读取本地保存的夸克 Cookie，正在自动启动 OpenList 本机网关"));
            m_logger.info(QStringLiteral("已读取本地保存的夸克 Cookie，开始自动连接夸克网盘"));
            m_quarkGatewayManager.startAndConfigure(m_quarkCookie);
        });
    }
}

QString SteamManager::steamPath() const
{
    return m_steamPath;
}

QString SteamManager::snapshotRootPath() const
{
    return m_snapshotPreprocessor.snapshotRootPath();
}

QString SteamManager::logDirectory() const
{
    return m_logger.logDirectory();
}

QString SteamManager::logFilePath() const
{
    return m_logger.logFilePath();
}

QString SteamManager::webDavServerUrl() const
{
    return m_webDavConfig.serverUrl;
}

QString SteamManager::webDavUsername() const
{
    return m_webDavConfig.username;
}

QString SteamManager::webDavPassword() const
{
    return m_webDavConfig.password;
}

QString SteamManager::webDavRemoteRootPath() const
{
    return m_webDavConfig.remoteRootPath;
}

QString SteamManager::webDavConnectionStatus() const
{
    return m_webDavConnectionStatus;
}

bool SteamManager::webDavTesting() const
{
    return m_webDavTesting;
}

QString SteamManager::quarkCookie() const
{
    return m_quarkCookie;
}

QString SteamManager::quarkGatewayStatus() const
{
    return m_quarkGatewayStatus;
}

bool SteamManager::autoSyncEnabled() const
{
    return m_autoSyncEnabled;
}

bool SteamManager::launchAtStartup() const
{
    return m_startupManager.isEnabled();
}

QVariantList SteamManager::installedGames() const
{
    return m_installedGames;
}

GameListModel *SteamManager::gameModel()
{
    return &m_gameModel;
}

LogListModel *SteamManager::logModel()
{
    return m_logger.logModel();
}

QString SteamManager::findSteamPath()
{
    QString foundPath;

#ifdef Q_OS_WIN
    /*
     * Steam 在 Windows 上会把安装目录写入注册表。
     *
     * HKEY_CURRENT_USER\Software\Valve\Steam\SteamPath
     *   - 当前用户安装 Steam 时最常见的位置。
     *   - SteamPath 通常使用正斜杠，例如 C:/Program Files (x86)/Steam。
     *
     * HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Valve\Steam\InstallPath
     *   - 64 位 Windows 上 32 位 Steam 客户端常见的机器级注册表位置。
     *
     * QSettings::NativeFormat 可以直接读取 Windows 注册表，不需要手写
     * RegOpenKeyEx / RegQueryValueEx。后续如果要读取更复杂的 Windows API
     * 数据，可以再替换为原生 WinAPI 封装。
     */
    const QStringList registryKeys = {
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"),
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam"),
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Valve\\Steam")
    };

    for (const QString &registryKey : registryKeys) {
        QSettings settings(registryKey, QSettings::NativeFormat);

        const QString steamPathValue = settings.value(QStringLiteral("SteamPath")).toString();
        foundPath = normalizedExistingSteamPath(steamPathValue);
        if (!foundPath.isEmpty()) {
            break;
        }

        const QString installPathValue = settings.value(QStringLiteral("InstallPath")).toString();
        foundPath = normalizedExistingSteamPath(installPathValue);
        if (!foundPath.isEmpty()) {
            break;
        }
    }
#endif

    /*
     * 注册表读取失败时做一个保守兜底：
     * 很多机器仍然把 Steam 安装在 Program Files (x86)\Steam。
     * 这里不直接假设路径有效，而是继续检查该目录下是否存在 steamapps。
     */
    if (foundPath.isEmpty()) {
        const QStringList fallbackRoots = {
            qEnvironmentVariable("ProgramFiles(x86)") + QStringLiteral("/Steam"),
            qEnvironmentVariable("ProgramFiles") + QStringLiteral("/Steam")
        };

        for (const QString &candidate : fallbackRoots) {
            foundPath = normalizedExistingSteamPath(candidate);
            if (!foundPath.isEmpty()) {
                break;
            }
        }
    }

    if (m_steamPath != foundPath) {
        m_steamPath = foundPath;
        emit steamPathChanged();
    }

    if (m_steamPath.isEmpty()) {
        m_logger.warning(QStringLiteral("Steam 路径识别失败：注册表和默认安装目录都没有找到可用的 steamapps 目录"));
    } else {
        m_logger.info(QStringLiteral("Steam 路径识别成功：%1").arg(m_steamPath));
    }

    return m_steamPath;
}

QVariantList SteamManager::scanInstalledGames(bool forceNetworkRefresh)
{
    const QString steamRootPath = m_steamPath.isEmpty() ? findSteamPath() : m_steamPath;
    QList<GameInfo> scannedGames;
    m_logger.info(QStringLiteral("开始扫描本地 Steam 游戏与手动添加游戏"));

    QSet<QString> seenAppIds;

    /*
     * Steam 的每个已安装游戏都会在某个 steamapps 目录下生成：
     *
     *   appmanifest_<AppID>.acf
     *
     * ACF 是 Valve 的 KeyValues 文本格式。第一阶段只需要 appid、name、
     * installdir 这几个字段，用正则读取足够稳定；后续如果要解析更多嵌套字段，
     * 可以升级为完整 KeyValues 解析器。
     */
    for (const QString &steamAppsPath : steamRootPath.isEmpty() ? QStringList() : steamAppsDirectories(steamRootPath)) {
        const QDir steamAppsDir(steamAppsPath);
        const QStringList manifestFiles = steamAppsDir.entryList(
            {QStringLiteral("appmanifest_*.acf")},
            QDir::Files | QDir::Readable,
            QDir::Name
        );

        for (const QString &manifestFile : manifestFiles) {
            GameInfo game = parseAcfFile(steamAppsDir.absoluteFilePath(manifestFile));
            const QString appId = game.appId;

            if (appId.isEmpty() || seenAppIds.contains(appId) || shouldHideSteamApp(game)) {
                continue;
            }

            seenAppIds.insert(appId);
            game.libraryPath = QDir::cleanPath(steamAppsDir.absolutePath() + QStringLiteral("/.."));
            game.processName = guessProcessNameForSteamGame(game);
            game.runningStatus = game.processName.isEmpty()
                                     ? QStringLiteral("未识别到游戏主程序，暂不能监控运行状态")
                                     : QStringLiteral("等待进程监控");
            scannedGames.append(game);
        }
    }

    appendManualGames(scannedGames, seenAppIds);

    std::sort(scannedGames.begin(), scannedGames.end(), [](const GameInfo &left, const GameInfo &right) {
        return QString::localeAwareCompare(left.name, right.name) < 0;
    });

    m_gameMetadataCache.reload();
    const int cachedCount = m_gameMetadataCache.applyToGames(scannedGames);
    applyManualSavePaths(scannedGames);

    m_gameModel.setGames(scannedGames);
    applyCloudSnapshotRecordsToModel();
    rebuildInstalledGamesFromModel();
    m_processMonitor.setGames(scannedGames);
    m_processMonitor.start();
    if (!m_hasLoggedProcessMonitorIdle
        && m_processMonitor.trackedGameCount() > 0
        && !m_processMonitor.hasRunningGames()) {
        m_logger.info(QStringLiteral("游戏进程监控正常运行，当前未检测到正在运行的游戏"));
        m_hasLoggedProcessMonitorIdle = true;
    }
    if (forceNetworkRefresh) {
        requestMetadataForGames(scannedGames);
        resolveSavePathsForGames(scannedGames);
        m_logger.info(QStringLiteral("已触发重新扫描：正在刷新 Steam 元数据和 PCGamingWiki 存档路径"));
    } else {
        m_logger.info(QStringLiteral("启动扫描已使用本地游戏数据缓存：命中 %1 个游戏，需要联网更新时请点击重新扫描")
                          .arg(cachedCount));
    }
    m_logger.info(QStringLiteral("游戏扫描完成：共发现 %1 个游戏，其中包含 Steam 库游戏和已保存的手动添加游戏").arg(scannedGames.count()));

    return m_installedGames;
}

QVariantList SteamManager::getInstalledGames()
{
    return scanInstalledGames(false);
}

void SteamManager::loadInstalledGames()
{
    scanInstalledGames(false);
}

void SteamManager::refreshInstalledGames()
{
    scanInstalledGames(true);
}

bool SteamManager::setManualSavePath(const QString &appId, const QUrl &folderUrl)
{
    const QString localPath = folderUrl.isLocalFile() ? folderUrl.toLocalFile() : folderUrl.toString();
    const bool success = m_savePathResolver.setManualSavePath(appId, localPath);
    if (success) {
        m_logger.info(QStringLiteral("用户手动指定存档目录：AppID %1，路径：%2").arg(appId, QDir::toNativeSeparators(localPath)));
    } else {
        m_logger.warning(QStringLiteral("用户手动指定存档目录失败：AppID %1，路径：%2").arg(appId, QDir::toNativeSeparators(localPath)));
    }
    return success;
}

bool SteamManager::addManualGameFromExecutable(const QUrl &executableUrl)
{
    const QString localPath = executableUrl.isLocalFile() ? executableUrl.toLocalFile() : executableUrl.toString();
    const QFileInfo executableInfo(localPath);

    if (!executableInfo.exists() || !executableInfo.isFile() || executableInfo.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) != 0) {
        m_logger.warning(QStringLiteral("手动添加游戏失败：选择的文件不是有效的 Windows exe：%1").arg(QDir::toNativeSeparators(localPath)));
        return false;
    }

    GameInfo game = m_gameIdentityResolver.createPendingManualGame(executableInfo.absoluteFilePath());
    m_pendingManualGames.insert(game.appId, game);
    m_manualGameManager.saveManualGame(game);
    upsertGameAndRebuild(game);

    m_pcGamingWikiSearchClient.requestGameSearch(
        game.appId,
        m_gameIdentityResolver.candidateNamesForExecutable(game.executablePath));
    m_logger.info(QStringLiteral("已手动添加游戏并开始匹配数据库：%1，exe：%2")
                      .arg(game.displayName, QDir::toNativeSeparators(game.executablePath)));
    return true;
}

bool SteamManager::setSnapshotRootPath(const QUrl &folderUrl)
{
    const QString selectedPath = folderUrl.isLocalFile() ? folderUrl.toLocalFile() : folderUrl.toString();
    const QString newStorageRootPath = storageRootFromSnapshotRoot(selectedPath);
    const QString newSnapshotRootPath = snapshotDirectoryForStorageRoot(newStorageRootPath);
    const QString newLogDirectoryPath = logDirectoryForStorageRoot(newStorageRootPath);
    const QString previousSnapshotRootPath = snapshotRootPath();
    const QString previousLogDirectoryPath = m_logger.logDirectory();

    if (newStorageRootPath.isEmpty()) {
        m_logger.warning(QStringLiteral("游戏存档根目录设置失败：选择的目录为空"));
        return false;
    }

    QDir storageRootDir(newStorageRootPath);
    if (!storageRootDir.exists() && !storageRootDir.mkpath(QStringLiteral("."))) {
        m_logger.warning(QStringLiteral("游戏存档根目录设置失败：无法创建目录 %1，请检查磁盘权限").arg(QDir::toNativeSeparators(newStorageRootPath)));
        return false;
    }

    if (!pathsEqual(previousSnapshotRootPath, newSnapshotRootPath)
        && !migrateDirectoryContents(previousSnapshotRootPath, newSnapshotRootPath, {
            newSnapshotRootPath,
            newLogDirectoryPath,
            previousLogDirectoryPath
        })) {
        m_logger.warning(QStringLiteral("游戏存档快照迁移失败，已保留原目录：%1").arg(previousSnapshotRootPath));
        return false;
    }

    if (!pathsEqual(previousLogDirectoryPath, newLogDirectoryPath)
        && !migrateDirectoryContents(previousLogDirectoryPath, newLogDirectoryPath)) {
        m_logger.warning(QStringLiteral("日志目录迁移失败，已保留原目录：%1").arg(previousLogDirectoryPath));
        return false;
    }

    if (!m_snapshotPreprocessor.setSnapshotRootPath(newSnapshotRootPath)) {
        m_logger.warning(QStringLiteral("本地快照目录设置失败：%1").arg(QDir::toNativeSeparators(newSnapshotRootPath)));
        return false;
    }

    const QString previousLogDirectory = m_logger.logDirectory();
    if (!m_logger.setLogDirectoryPath(newLogDirectoryPath)) {
        m_logger.warning(QStringLiteral("日志目录设置失败：%1").arg(QDir::toNativeSeparators(newLogDirectoryPath)));
        return false;
    }

    if (!pathsEqual(previousSnapshotRootPath, snapshotRootPath())) {
        emit snapshotRootPathChanged();
    }

    if (!pathsEqual(previousLogDirectory, m_logger.logDirectory())) {
        emit logDirectoryChanged();
        emit logFilePathChanged();
    }

    m_logger.info(QStringLiteral("游戏存档根目录已设置为：%1，快照目录：%2，日志目录：%3")
                      .arg(QDir::toNativeSeparators(newStorageRootPath),
                           snapshotRootPath(),
                           m_logger.logDirectory()));
    return true;
}

bool SteamManager::analyzeSnapshotForGame(const QString &appId)
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return false;
    }

    const QVariantMap result = m_snapshotPreprocessor.analyzeGame(game);
    applySnapshotResult(appId, result);
    const QString levelText = result.value(QStringLiteral("success")).toBool() ? QStringLiteral("完成") : QStringLiteral("失败");
    m_logger.info(QStringLiteral("本地快照检查%1：%2，结果：%3，详情：%4")
                      .arg(levelText, game.displayName, result.value(QStringLiteral("status")).toString(), result.value(QStringLiteral("detail")).toString()));
    return result.value(QStringLiteral("success")).toBool();
}

bool SteamManager::createSnapshotForGame(const QString &appId)
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return false;
    }

    const QVariantMap result = m_snapshotPreprocessor.createSnapshotForGame(game);
    applySnapshotResult(appId, result);
    if (result.value(QStringLiteral("success")).toBool()) {
        m_logger.info(QStringLiteral("本地快照创建完成：%1，结果：%2，详情：%3")
                          .arg(game.displayName, result.value(QStringLiteral("status")).toString(), result.value(QStringLiteral("detail")).toString()));
    } else {
        m_logger.error(QStringLiteral("本地快照创建失败：%1，结果：%2，详情：%3")
                           .arg(game.displayName, result.value(QStringLiteral("status")).toString(), result.value(QStringLiteral("detail")).toString()));
    }
    return result.value(QStringLiteral("success")).toBool();
}

bool SteamManager::deleteLocalSnapshotsForGame(const QString &appId)
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return false;
    }

    const QVariantMap result = m_snapshotPreprocessor.deleteSnapshotsForGame(game);
    applySnapshotResult(appId, result);

    const QString status = result.value(QStringLiteral("status")).toString();
    const QString detail = result.value(QStringLiteral("detail")).toString();
    if (result.value(QStringLiteral("success")).toBool()) {
        m_logger.info(QStringLiteral("本地存档快照删除完成：%1，结果：%2，详情：%3")
                          .arg(game.displayName, status, detail));
    } else {
        m_logger.warning(QStringLiteral("本地存档快照删除失败：%1，结果：%2，详情：%3")
                             .arg(game.displayName, status, detail));
    }

    /*
     * 删除动作只清理软件生成的本地快照目录，不修改云端快照索引。
     * 这里再按磁盘实际 zip 文件刷新一次，保证 UI 的本地快照数量不会停留在旧值。
     */
    refreshSnapshotRecordsForGame(appId, status, detail, result.value(QStringLiteral("needsSnapshot")).toBool());
    return result.value(QStringLiteral("success")).toBool();
}

bool SteamManager::restoreLocalSnapshotForGame(const QString &appId, const QString &snapshotPathOrFileName)
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return false;
    }

    if (!game.savePathCanSync || game.savePath.trimmed().isEmpty()) {
        m_logger.warning(QStringLiteral("快照恢复失败：%1 当前没有可恢复的本地存档目录").arg(game.displayName));
        return false;
    }

    if (m_processMonitor.isGameRunning(appId)) {
        m_logger.warning(QStringLiteral("快照恢复已阻止：%1 正在运行，请先关闭游戏后再恢复存档").arg(game.displayName));
        emit userAlertRequested(
            QStringLiteral("快照恢复已阻止"),
            QStringLiteral("%1 正在运行，请先关闭游戏后再恢复存档").arg(game.displayName),
            QStringLiteral("为了避免存档被游戏进程重新写入或损坏，软件已经停止本次恢复操作。关闭游戏后，可以再次点击恢复。"),
            false);
        refreshSnapshotRecordsForGame(
            appId,
            QStringLiteral("快照恢复已阻止"),
            QStringLiteral("检测到游戏进程仍在运行。为了避免存档被游戏重新写入或损坏，请先关闭游戏后再恢复快照"),
            false);
        return false;
    }

    const QString selector = snapshotPathOrFileName.trimmed();
    if (selector.isEmpty()) {
        m_logger.warning(QStringLiteral("快照恢复失败：%1 未选择本地 zip 快照").arg(game.displayName));
        return false;
    }

    QString selectedZipPath;
    QString selectedFileName;
    const QVariantList snapshots = m_snapshotPreprocessor.snapshotsForGame(game);
    for (const QVariant &item : snapshots) {
        const QVariantMap snapshot = item.toMap();
        const QString zipPath = snapshot.value(QStringLiteral("zipPath")).toString();
        const QString fileName = snapshot.value(QStringLiteral("fileName")).toString();
        if (zipPath == selector || fileName == selector) {
            selectedZipPath = zipPath;
            selectedFileName = fileName;
            break;
        }
    }

    if (selectedZipPath.trimmed().isEmpty()) {
        m_logger.warning(QStringLiteral("快照恢复失败：%1 的本地快照列表中未找到 %2").arg(game.displayName, selector));
        refreshLocalSnapshotsForGame(appId);
        return false;
    }

    const QString storageRootPath = storageRootFromSnapshotRoot(snapshotRootPath());
    const QString backupRootPath = QDir(storageRootPath).absoluteFilePath(QStringLiteral("恢复前备份"));
    m_logger.info(QStringLiteral("开始恢复本地快照：%1，快照：%2，目标存档目录：%3")
                      .arg(game.displayName,
                           QDir::toNativeSeparators(selectedZipPath),
                           QDir::toNativeSeparators(game.savePath)));

    const QVariantMap result = m_snapshotRestoreManager.restoreSnapshot(game, selectedZipPath, backupRootPath);
    const QString status = result.value(QStringLiteral("status")).toString();
    const QString detail = result.value(QStringLiteral("detail")).toString();
    const QString backupPath = result.value(QStringLiteral("backupPath")).toString();
    const bool success = result.value(QStringLiteral("success")).toBool();

    refreshSnapshotRecordsForGame(
        appId,
        status,
        backupPath.trimmed().isEmpty()
            ? detail
            : QStringLiteral("%1；恢复前备份：%2").arg(detail, QDir::toNativeSeparators(backupPath)),
        false);

    if (success) {
        m_logger.info(QStringLiteral("本地快照恢复完成：%1，快照：%2，恢复前备份：%3")
                          .arg(game.displayName,
                               selectedFileName.isEmpty() ? QFileInfo(selectedZipPath).fileName() : selectedFileName,
                               QDir::toNativeSeparators(backupPath)));
    } else {
        m_logger.error(QStringLiteral("本地快照恢复失败：%1，快照：%2，原因：%3")
                           .arg(game.displayName,
                                selectedFileName.isEmpty() ? selector : selectedFileName,
                                detail));
    }

    return success;
}

bool SteamManager::restoreCloudSnapshotForGame(const QString &appId, const QString &snapshotFileNameOrRemotePath)
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return false;
    }

    if (!m_webDavConfig.isValid()) {
        m_logger.warning(QStringLiteral("云端快照恢复失败：夸克网盘尚未连接成功"));
        return false;
    }

    if (!game.savePathCanSync || game.savePath.trimmed().isEmpty()) {
        m_logger.warning(QStringLiteral("云端快照恢复失败：%1 当前没有可恢复的本地存档目录").arg(game.displayName));
        return false;
    }

    if (m_processMonitor.isGameRunning(appId)) {
        m_logger.warning(QStringLiteral("云端快照恢复已阻止：%1 正在运行，请先关闭游戏后再恢复存档").arg(game.displayName));
        emit userAlertRequested(
            QStringLiteral("云端快照恢复已阻止"),
            QStringLiteral("%1 正在运行，请先关闭游戏后再恢复云端快照").arg(game.displayName),
            QStringLiteral("云端快照会覆盖游戏真实存档目录。为了避免存档被游戏进程重新写入或损坏，请先完全关闭游戏后再恢复。"),
            false);
        refreshSnapshotRecordsForGame(
            appId,
            QStringLiteral("云端快照恢复已阻止"),
            QStringLiteral("检测到游戏进程仍在运行。为了避免存档被游戏重新写入或损坏，请先关闭游戏后再恢复云端快照"),
            false);
        return false;
    }

    const QString selector = snapshotFileNameOrRemotePath.trimmed();
    if (selector.isEmpty()) {
        m_logger.warning(QStringLiteral("云端快照恢复失败：%1 未选择云端快照").arg(game.displayName));
        return false;
    }

    const QVariantList snapshots = downloadableSnapshotRecordsForGame(game);
    for (const QVariant &item : snapshots) {
        QVariantMap snapshot = item.toMap();
        const QString fileName = snapshot.value(QStringLiteral("fileName")).toString().trimmed();
        QString remotePath = snapshot.value(QStringLiteral("remotePath")).toString().trimmed();
        if (fileName != selector && remotePath != selector) {
            continue;
        }

        if (remotePath.isEmpty()) {
            remotePath = cloudDirectoryForGame(game) + QLatin1Char('/') + fileName;
        }
        const QString localPath = localDownloadPathForSnapshot(game, fileName);
        if (fileName.isEmpty() || remotePath.isEmpty() || localPath.trimmed().isEmpty()) {
            m_logger.warning(QStringLiteral("云端快照恢复失败：%1 的云端快照记录不完整").arg(game.displayName));
            return false;
        }

        snapshot.insert(QStringLiteral("remotePath"), remotePath);

        if (QFileInfo::exists(localPath)) {
            m_snapshotPreprocessor.importCloudSnapshotRecord(game, snapshot, localPath);
            m_logger.info(QStringLiteral("云端快照恢复：%1 的快照本地已存在，将直接恢复，文件：%2")
                              .arg(game.displayName, QDir::toNativeSeparators(localPath)));
            return restoreLocalSnapshotForGame(appId, localPath);
        }

        if (m_pendingSnapshotDownloadAppIds.contains(localPath)) {
            m_pendingSnapshotRestoreDownloadPaths.insert(localPath);
            m_logger.info(QStringLiteral("云端快照恢复等待下载完成：%1，快照：%2，本地路径：%3")
                              .arg(game.displayName, fileName, QDir::toNativeSeparators(localPath)));
            refreshSnapshotRecordsForGame(
                appId,
                QStringLiteral("云端快照正在下载，完成后将自动恢复"),
                QStringLiteral("所选云端快照已在下载队列中，下载成功后会先备份当前存档，再自动恢复该版本"),
                false);
            return true;
        }

        m_pendingSnapshotDownloadAppIds.insert(localPath, appId);
        m_pendingSnapshotDownloadRecords.insert(localPath, snapshot);
        m_pendingSnapshotRestoreDownloadPaths.insert(localPath);
        m_logger.info(QStringLiteral("开始下载并恢复云端快照：%1，文件：%2，云端路径：%3，本地路径：%4")
                          .arg(game.displayName, fileName, remotePath, QDir::toNativeSeparators(localPath)));
        refreshSnapshotRecordsForGame(
            appId,
            QStringLiteral("正在下载云端快照，完成后将自动恢复"),
            QStringLiteral("所选云端快照本地尚不存在，软件会先下载 zip，再备份当前存档并恢复该版本"),
            false);

        if (remotePath.startsWith(QStringLiteral("/Quark"), Qt::CaseInsensitive)) {
            m_quarkGatewayManager.downloadFile(remotePath, localPath);
        } else {
            m_webDavClient.downloadFile(m_webDavConfig, remotePath, localPath);
        }
        return true;
    }

    m_logger.warning(QStringLiteral("云端快照恢复失败：%1 未找到所选云端快照 %2").arg(game.displayName, selector));
    return false;
}

QVariantList SteamManager::restoreBackupsForGame(const QString &appId) const
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return {};
    }

    return m_snapshotRestoreManager.backupsForGame(restoreBackupRootPath(), game);
}

bool SteamManager::restoreBackupForGame(const QString &appId, const QString &backupPathOrFileName)
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return false;
    }

    if (!game.savePathCanSync || game.savePath.trimmed().isEmpty()) {
        m_logger.warning(QStringLiteral("恢复前备份回滚失败：%1 当前没有可恢复的本地存档目录").arg(game.displayName));
        return false;
    }

    if (m_processMonitor.isGameRunning(appId)) {
        m_logger.warning(QStringLiteral("恢复前备份回滚已阻止：%1 正在运行，请先关闭游戏").arg(game.displayName));
        emit userAlertRequested(
            QStringLiteral("恢复前备份回滚已阻止"),
            QStringLiteral("%1 正在运行，请先关闭游戏后再回滚恢复前备份").arg(game.displayName),
            QStringLiteral("回滚恢复前备份会覆盖游戏真实存档目录。为了避免存档被游戏进程重新写入或损坏，请先完全关闭游戏后再操作。"),
            false);
        refreshSnapshotRecordsForGame(
            appId,
            QStringLiteral("恢复前备份回滚已阻止"),
            QStringLiteral("检测到游戏进程仍在运行。为了避免存档被游戏重新写入或损坏，请先关闭游戏后再回滚恢复前备份"),
            false);
        return false;
    }

    const QString selector = backupPathOrFileName.trimmed();
    if (selector.isEmpty()) {
        m_logger.warning(QStringLiteral("恢复前备份回滚失败：%1 未选择备份 zip").arg(game.displayName));
        return false;
    }

    QString selectedZipPath;
    const QVariantList backups = restoreBackupsForGame(appId);
    for (const QVariant &item : backups) {
        const QVariantMap backup = item.toMap();
        const QString zipPath = backup.value(QStringLiteral("zipPath")).toString();
        const QString fileName = backup.value(QStringLiteral("fileName")).toString();
        if (zipPath == selector || fileName == selector) {
            selectedZipPath = zipPath;
            break;
        }
    }

    if (selectedZipPath.trimmed().isEmpty()) {
        m_logger.warning(QStringLiteral("恢复前备份回滚失败：%1 的备份列表中未找到 %2").arg(game.displayName, selector));
        return false;
    }

    m_logger.info(QStringLiteral("开始从恢复前备份回滚：%1，备份：%2，目标存档目录：%3")
                      .arg(game.displayName,
                           QDir::toNativeSeparators(selectedZipPath),
                           QDir::toNativeSeparators(game.savePath)));
    const QVariantMap result = m_snapshotRestoreManager.restoreSnapshot(game, selectedZipPath, restoreBackupRootPath());
    const QString status = result.value(QStringLiteral("status")).toString();
    const QString detail = result.value(QStringLiteral("detail")).toString();
    const QString backupPath = result.value(QStringLiteral("backupPath")).toString();
    const bool success = result.value(QStringLiteral("success")).toBool();

    refreshSnapshotRecordsForGame(
        appId,
        success ? QStringLiteral("恢复前备份回滚完成") : status,
        backupPath.trimmed().isEmpty()
            ? detail
            : QStringLiteral("%1；回滚前也已再次备份当前存档：%2").arg(detail, QDir::toNativeSeparators(backupPath)),
        false);

    if (success) {
        m_logger.info(QStringLiteral("恢复前备份回滚完成：%1，备份：%2").arg(game.displayName, QDir::toNativeSeparators(selectedZipPath)));
    } else {
        m_logger.error(QStringLiteral("恢复前备份回滚失败：%1，备份：%2，原因：%3").arg(game.displayName, QDir::toNativeSeparators(selectedZipPath), detail));
    }

    return success;
}

bool SteamManager::deleteRestoreBackupsForGame(const QString &appId)
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return false;
    }

    const QVariantMap result = m_snapshotRestoreManager.deleteBackupsForGame(restoreBackupRootPath(), game);
    const QString status = result.value(QStringLiteral("status")).toString();
    const QString detail = result.value(QStringLiteral("detail")).toString();
    if (result.value(QStringLiteral("success")).toBool()) {
        m_logger.info(QStringLiteral("恢复前备份清理完成：%1，结果：%2，详情：%3").arg(game.displayName, status, detail));
    } else {
        m_logger.warning(QStringLiteral("恢复前备份清理失败：%1，结果：%2，详情：%3").arg(game.displayName, status, detail));
    }
    return result.value(QStringLiteral("success")).toBool();
}

bool SteamManager::openRestoreBackupDirectoryForGame(const QString &appId)
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return false;
    }

    const QString directoryPath = m_snapshotRestoreManager.backupDirectoryForGame(restoreBackupRootPath(), game);
    QDir directory(directoryPath);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        m_logger.warning(QStringLiteral("打开恢复前备份目录失败：%1，无法创建目录：%2")
                             .arg(game.displayName, QDir::toNativeSeparators(directoryPath)));
        return false;
    }

    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(directory.absolutePath()));
    if (opened) {
        m_logger.info(QStringLiteral("已打开恢复前备份目录：%1，路径：%2")
                          .arg(game.displayName, QDir::toNativeSeparators(directory.absolutePath())));
    } else {
        m_logger.warning(QStringLiteral("打开恢复前备份目录失败：%1，路径：%2")
                             .arg(game.displayName, QDir::toNativeSeparators(directory.absolutePath())));
    }
    return opened;
}

bool SteamManager::uploadLatestSnapshotForGame(const QString &appId)
{
    return uploadAllSnapshotsForGame(appId);
}

bool SteamManager::uploadAllSnapshotsForGame(const QString &appId)
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return false;
    }

    if (!m_webDavConfig.isValid()) {
        m_logger.warning(QStringLiteral("快照上传失败：WebDAV 未配置或配置不完整"));
        return false;
    }

    const QVariantList snapshots = m_snapshotPreprocessor.snapshotsForGame(game);
    if (snapshots.isEmpty()) {
        m_logger.warning(QStringLiteral("快照上传失败：%1 还没有本地 zip 快照").arg(game.displayName));
        refreshSnapshotRecordsForGame(
            appId,
            QStringLiteral("本地没有可上传的快照文件"),
            QStringLiteral("本地快照列表已按磁盘实际存在的 zip 文件重新刷新；如果快照被手动删除，请重新创建快照或先从云端下载"),
            false);
        return false;
    }

    if (!uploadSnapshotRecordsForGame(game, snapshots)) {
        m_logger.warning(QStringLiteral("快照上传失败：%1 没有可进入上传检查队列的本地 zip 快照").arg(game.displayName));
        return false;
    }
    return true;
}

bool SteamManager::downloadLatestSnapshotForGame(const QString &appId)
{
    return downloadAllSnapshotsForGame(appId);
}

bool SteamManager::downloadAllSnapshotsForGame(const QString &appId)
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return false;
    }

    if (!m_webDavConfig.isValid()) {
        m_logger.warning(QStringLiteral("快照下载失败：WebDAV 未配置或配置不完整"));
        return false;
    }

    const QString gameManifestPath = cloudDirectoryForGame(game) + QStringLiteral("/snapshot-manifest.json");
    if (gameManifestPath.startsWith(QStringLiteral("/Quark"), Qt::CaseInsensitive)) {
        m_logger.info(QStringLiteral("开始读取游戏完整云端快照索引：%1，路径：%2")
                          .arg(game.displayName, gameManifestPath));
        m_quarkGatewayManager.downloadDataFile(
            gameManifestPath,
            QStringLiteral("cloud-game-download-all:%1").arg(appId));
        return true;
    }

    if (!downloadSnapshotRecordsForGame(game, downloadableSnapshotRecordsForGame(game))) {
        m_logger.warning(QStringLiteral("快照下载失败：%1 没有可用于下载的快照记录").arg(game.displayName));
        return false;
    }

    return true;
}

bool SteamManager::uploadSelectedSnapshotForGame(const QString &appId, const QString &snapshotPathOrFileName)
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return false;
    }

    if (!m_webDavConfig.isValid()) {
        m_logger.warning(QStringLiteral("覆盖上传快照失败：WebDAV 未配置或配置不完整"));
        return false;
    }

    const QString selector = snapshotPathOrFileName.trimmed();
    if (selector.isEmpty()) {
        m_logger.warning(QStringLiteral("覆盖上传快照失败：未选择快照"));
        return false;
    }

    const QVariantList snapshots = m_snapshotPreprocessor.snapshotsForGame(game);
    for (const QVariant &item : snapshots) {
        const QVariantMap snapshot = item.toMap();
        const QString zipPath = snapshot.value(QStringLiteral("zipPath")).toString();
        const QString fileName = snapshot.value(QStringLiteral("fileName")).toString();
        if (zipPath != selector && fileName != selector) {
            continue;
        }

        const QFileInfo zipInfo(zipPath);
        if (!zipInfo.exists() || !zipInfo.isFile()) {
            m_logger.warning(QStringLiteral("覆盖上传快照失败：本地文件不存在，游戏：%1，快照：%2")
                                 .arg(game.displayName, selector));
            refreshLocalSnapshotsForGame(appId);
            return false;
        }

        const QString uploadPath = zipInfo.absoluteFilePath();
        const QString uploadFileName = fileName.trimmed().isEmpty() ? zipInfo.fileName() : fileName;
        const QString remoteDirectory = cloudDirectoryForGame(game);
        m_pendingSnapshotUploadAppIds.insert(uploadPath, appId);
        m_pendingSnapshotUploadFileNames.insert(uploadPath, uploadFileName);
        m_logger.info(QStringLiteral("开始覆盖上传选中快照：%1，本地文件：%2，云端目录：%3")
                          .arg(game.displayName, QDir::toNativeSeparators(uploadPath), remoteDirectory));
        if (remoteDirectory.startsWith(QStringLiteral("/Quark"), Qt::CaseInsensitive)) {
            m_quarkGatewayManager.uploadFileOverwrite(uploadPath, remoteDirectory);
        } else {
            m_webDavClient.uploadFileOverwrite(m_webDavConfig, uploadPath, remoteDirectory);
        }
        return true;
    }

    m_logger.warning(QStringLiteral("覆盖上传快照失败：%1 未找到所选快照 %2").arg(game.displayName, selector));
    return false;
}

bool SteamManager::downloadSelectedSnapshotForGame(const QString &appId, const QString &snapshotFileName)
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return false;
    }

    if (!m_webDavConfig.isValid()) {
        m_logger.warning(QStringLiteral("覆盖下载快照失败：夸克网盘尚未连接成功"));
        return false;
    }

    const QString selector = snapshotFileName.trimmed();
    if (selector.isEmpty()) {
        m_logger.warning(QStringLiteral("覆盖下载快照失败：未选择云端快照"));
        return false;
    }

    const QVariantList snapshots = downloadableSnapshotRecordsForGame(game);
    for (const QVariant &item : snapshots) {
        QVariantMap snapshot = item.toMap();
        const QString fileName = snapshot.value(QStringLiteral("fileName")).toString().trimmed();
        QString remotePath = snapshot.value(QStringLiteral("remotePath")).toString().trimmed();
        if (fileName != selector && remotePath != selector) {
            continue;
        }

        if (remotePath.isEmpty()) {
            remotePath = cloudDirectoryForGame(game) + QLatin1Char('/') + fileName;
        }
        if (remotePath.isEmpty() || fileName.isEmpty()) {
            m_logger.warning(QStringLiteral("覆盖下载快照失败：%1 的云端路径或文件名为空").arg(game.displayName));
            return false;
        }

        const QString localPath = localDownloadPathForSnapshot(game, fileName);
        if (localPath.trimmed().isEmpty()) {
            m_logger.warning(QStringLiteral("覆盖下载快照失败：无法生成本地保存路径"));
            return false;
        }

        snapshot.insert(QStringLiteral("remotePath"), remotePath);
        m_pendingSnapshotDownloadAppIds.insert(localPath, appId);
        m_pendingSnapshotDownloadRecords.insert(localPath, snapshot);
        m_logger.info(QStringLiteral("开始覆盖下载选中快照：%1，文件：%2，云端路径：%3，本地路径：%4")
                          .arg(game.displayName, fileName, remotePath, QDir::toNativeSeparators(localPath)));
        if (remotePath.startsWith(QStringLiteral("/Quark"), Qt::CaseInsensitive)) {
            m_quarkGatewayManager.downloadFile(remotePath, localPath);
        } else {
            m_webDavClient.downloadFile(m_webDavConfig, remotePath, localPath);
        }
        return true;
    }

    m_logger.warning(QStringLiteral("覆盖下载快照失败：%1 未找到所选云端快照 %2").arg(game.displayName, selector));
    return false;
}

bool SteamManager::hasDownloadableSnapshot(const QString &appId) const
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return false;
    }

    return !downloadableSnapshotRecordsForGame(game).isEmpty();
}

bool SteamManager::refreshLocalSnapshotsForGame(const QString &appId)
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return false;
    }

    const int localSnapshotCount = m_snapshotPreprocessor.snapshotsForGame(game).count();
    QString status = game.snapshotStatus.trimmed().isEmpty()
                         ? QStringLiteral("本地快照列表已刷新")
                         : game.snapshotStatus;
    QString detail = QStringLiteral("已按磁盘实际存在的 zip 文件重新刷新，本地快照 %1 个")
                         .arg(localSnapshotCount);

    if (localSnapshotCount <= 0) {
        if (hasDownloadableSnapshot(appId)) {
            status = QStringLiteral("本地快照 0 个，可从云端下载已上传快照");
            detail = QStringLiteral("本地快照目录当前没有 zip 文件，但历史记录中存在已上传到云端的快照，可以点击“下载全部快照”取回");
        } else {
            status = QStringLiteral("本地快照 0 个");
            detail = QStringLiteral("本地快照目录当前没有可用 zip 文件，请重新创建快照后再上传");
        }
    }

    refreshSnapshotRecordsForGame(appId, status, detail, game.snapshotNeedsCreate);
    return true;
}

void SteamManager::refreshCloudManifestFromRemote()
{
    if (!m_webDavConfig.isValid()) {
        m_logger.warning(QStringLiteral("云端索引读取失败：夸克网盘尚未连接成功"));
        return;
    }

    if (m_cloudManifestRefreshInFlight) {
        return;
    }

    m_cloudManifestRefreshInFlight = true;
    m_logger.info(QStringLiteral("开始读取云端快照索引：%1").arg(cloudRootManifestPath()));
    m_quarkGatewayManager.downloadDataFile(
        cloudRootManifestPath(),
        QStringLiteral("cloud-root-refresh"));
}

void SteamManager::refreshCloudSnapshotsForGame(const QString &appId)
{
    refreshCloudSnapshotsForGameInternal(appId, false);
}

void SteamManager::refreshCloudSnapshotsForGameQuietly(const QString &appId)
{
    refreshCloudSnapshotsForGameInternal(appId, true);
}

void SteamManager::refreshCloudSnapshotsForGameInternal(const QString &appId, bool quiet)
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return;
    }

    if (!m_webDavConfig.isValid()) {
        m_logger.warning(QStringLiteral("云端快照列表读取失败：夸克网盘尚未连接成功"));
        emit cloudSnapshotsChanged(appId);
        return;
    }

    if (!m_cloudManifestLoaded) {
        const bool existingQuiet = m_pendingCloudSnapshotRefreshQuietByAppId.value(appId, true);
        m_pendingCloudSnapshotRefreshQuietByAppId.insert(appId, existingQuiet && quiet);
        if (!quiet) {
            m_logger.info(QStringLiteral("云端根索引尚未读取完成，已暂缓读取游戏快照列表：%1").arg(game.displayName));
        }
        refreshCloudManifestFromRemote();
        return;
    }

    const QString gameManifestPath = cloudDirectoryForGame(game) + QStringLiteral("/snapshot-manifest.json");
    if (gameManifestPath.startsWith(QStringLiteral("/Quark"), Qt::CaseInsensitive)) {
        const bool shouldLog = !quiet || shouldLogAutomaticCloudRefresh(appId);
        if (shouldLog) {
            m_logger.info(QStringLiteral("开始读取可选下载快照列表：%1，路径：%2")
                              .arg(game.displayName, gameManifestPath));
        }
        m_quarkGatewayManager.downloadDataFile(
            gameManifestPath,
            QStringLiteral("%1:%2")
                .arg(shouldLog
                         ? QStringLiteral("cloud-game-list-ui")
                         : QStringLiteral("cloud-game-list-ui-quiet"),
                     appId));
        return;
    }

    emit cloudSnapshotsChanged(appId);
}

QVariantList SteamManager::snapshotsForGame(const QString &appId) const
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return {};
    }

    return m_snapshotPreprocessor.snapshotsForGame(game);
}

QVariantList SteamManager::cloudSnapshotsForGame(const QString &appId) const
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return {};
    }

    const QVariantList cloudRecords = m_cloudSnapshotRecordsByAppId.value(appId);
    if (m_cloudSnapshotRecordsByAppId.contains(appId)) {
        return m_cloudSnapshotRecordsByAppId.value(appId);
    }

    return downloadableSnapshotRecordsForGame(game);
}

void SteamManager::clearVisibleLogs()
{
    m_logger.clearVisibleLogs();
}

bool SteamManager::openLogDirectory() const
{
    return m_logger.openLogDirectory();
}

bool SteamManager::openGameDataCacheDirectory()
{
    QDir cacheDir(m_gameMetadataCache.cacheDirectoryPath());
    if (!cacheDir.exists() && !cacheDir.mkpath(QStringLiteral("."))) {
        m_logger.warning(QStringLiteral("打开游戏数据缓存目录失败：无法创建目录 %1")
                             .arg(QDir::toNativeSeparators(cacheDir.absolutePath())));
        return false;
    }

    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(cacheDir.absolutePath()));
    if (opened) {
        m_logger.info(QStringLiteral("已打开游戏数据缓存目录：%1")
                          .arg(QDir::toNativeSeparators(cacheDir.absolutePath())));
    } else {
        m_logger.warning(QStringLiteral("打开游戏数据缓存目录失败：%1")
                             .arg(QDir::toNativeSeparators(cacheDir.absolutePath())));
    }

    return opened;
}

bool SteamManager::openSavePathForGame(const QString &appId)
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return false;
    }

    const QString savePath = game.savePath.trimmed();
    if (savePath.isEmpty()) {
        m_logger.warning(QStringLiteral("打开真实存档目录失败：%1 尚未识别或指定存档目录").arg(game.displayName));
        return false;
    }

    QDir directory(savePath);
    if (!directory.exists()) {
        m_logger.warning(QStringLiteral("打开真实存档目录失败：目录不存在，游戏：%1，路径：%2")
                             .arg(game.displayName, QDir::toNativeSeparators(savePath)));
        return false;
    }

    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(directory.absolutePath()));
    if (opened) {
        m_logger.info(QStringLiteral("已打开真实存档目录：%1，路径：%2")
                          .arg(game.displayName, QDir::toNativeSeparators(directory.absolutePath())));
    } else {
        m_logger.warning(QStringLiteral("打开真实存档目录失败：%1，路径：%2")
                             .arg(game.displayName, QDir::toNativeSeparators(directory.absolutePath())));
    }
    return opened;
}

bool SteamManager::openSnapshotDirectoryForGame(const QString &appId)
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return false;
    }

    const QString directoryPath = m_snapshotPreprocessor.snapshotDirectoryForGame(game);
    if (directoryPath.trimmed().isEmpty()) {
        m_logger.warning(QStringLiteral("打开存档快照目录失败：%1 的快照目录尚未生成").arg(game.displayName));
        return false;
    }

    QDir directory(directoryPath);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        m_logger.warning(QStringLiteral("打开存档快照目录失败：无法创建目录 %1")
                             .arg(QDir::toNativeSeparators(directoryPath)));
        return false;
    }

    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(directory.absolutePath()));
    if (opened) {
        m_logger.info(QStringLiteral("已打开存档快照目录：%1，路径：%2")
                          .arg(game.displayName, QDir::toNativeSeparators(directory.absolutePath())));
    } else {
        m_logger.warning(QStringLiteral("打开存档快照目录失败：%1，路径：%2")
                             .arg(game.displayName, QDir::toNativeSeparators(directory.absolutePath())));
    }
    return opened;
}

bool SteamManager::saveWebDavSettings(
    const QString &serverUrl,
    const QString &username,
    const QString &password,
    const QString &remoteRootPath)
{
    const WebDavConfig config = webDavConfigFromInput(serverUrl, username, password, remoteRootPath);
    if (!config.isValid()) {
        setWebDavConnectionStatus(QStringLiteral("WebDAV 配置不完整：请填写有效的服务器地址和远程目录"));
        m_logger.warning(QStringLiteral("WebDAV 配置保存失败：服务器地址或远程目录无效"));
        return false;
    }

    if (!m_webDavSettings.saveConfig(config)) {
        setWebDavConnectionStatus(QStringLiteral("WebDAV 配置保存失败：无法写入本地配置文件"));
        m_logger.warning(QStringLiteral("WebDAV 配置保存失败：无法写入本地配置文件"));
        return false;
    }

    m_webDavConfig = m_webDavSettings.loadConfig();
    emit webDavSettingsChanged();
    setWebDavConnectionStatus(QStringLiteral("WebDAV 配置已保存，尚未完成连接测试"));
    m_logger.info(QStringLiteral("WebDAV 配置已保存：服务器 %1，远程目录 %2")
                      .arg(m_webDavConfig.serverUrl, m_webDavConfig.remoteRootPath));
    return true;
}

void SteamManager::testWebDavConnection(
    const QString &serverUrl,
    const QString &username,
    const QString &password,
    const QString &remoteRootPath)
{
    if (!saveWebDavSettings(serverUrl, username, password, remoteRootPath)) {
        return;
    }

    setWebDavTesting(true);
    setWebDavConnectionStatus(QStringLiteral("正在测试 WebDAV 连接并确认远程目录"));
    m_logger.info(QStringLiteral("开始测试 WebDAV 连接：服务器 %1，远程目录 %2")
                      .arg(m_webDavConfig.serverUrl, m_webDavConfig.remoteRootPath));
    m_webDavClient.testConnection(m_webDavConfig);
}

bool SteamManager::saveQuarkCookieGateway(const QString &cookie)
{
    const QString cleanCookie = cookie.trimmed();
    if (cleanCookie.isEmpty()) {
        setQuarkGatewayStatus(QStringLiteral("夸克 Cookie 为空，无法启用夸克网关模式"));
        m_logger.warning(QStringLiteral("夸克网关配置失败：Cookie 为空"));
        return false;
    }

    const std::unique_ptr<QSettings> settings = AppSettings::create();
    settings->setValue(QStringLiteral("quark/cookie"), cleanCookie);
    settings->sync();
    if (settings->status() != QSettings::NoError) {
        setQuarkGatewayStatus(QStringLiteral("夸克 Cookie 保存失败：无法写入本地配置文件"));
        m_logger.warning(QStringLiteral("夸克网关配置失败：无法写入本地配置文件"));
        return false;
    }

    m_quarkCookie = cleanCookie;
    emit quarkGatewaySettingsChanged();

    setWebDavTesting(true);
    setWebDavConnectionStatus(QStringLiteral("正在启动和配置夸克本机网关"));
    setQuarkGatewayStatus(QStringLiteral("夸克 Cookie 已保存，正在启动 OpenList 本机网关"));
    m_logger.info(QStringLiteral("夸克 Cookie 已保存，开始启动 OpenList 本机网关"));
    m_quarkGatewayManager.startAndConfigure(cleanCookie);
    return true;
}

bool SteamManager::setAutoSyncEnabled(bool enabled)
{
    const std::unique_ptr<QSettings> settings = AppSettings::create();
    settings->setValue(QStringLiteral("autoSync/enabled"), enabled);
    settings->sync();
    if (settings->status() != QSettings::NoError) {
        m_logger.warning(QStringLiteral("自动同步设置保存失败：无法写入全局开关"));
        return false;
    }

    if (m_autoSyncEnabled == enabled) {
        return true;
    }

    m_autoSyncEnabled = enabled;
    emit autoSyncSettingsChanged();
    m_logger.info(enabled
                      ? QStringLiteral("自动同步已启用：游戏关闭后会自动检查并创建本地存档快照")
                      : QStringLiteral("自动同步已关闭：游戏关闭后不会自动创建快照"));
    return true;
}

bool SteamManager::autoSyncEnabledForGame(const QString &appId) const
{
    const QString cleanAppId = appId.trimmed();
    if (cleanAppId.isEmpty()) {
        return false;
    }

    const std::unique_ptr<QSettings> settings = AppSettings::create();
    return settings->value(autoSyncGameSettingsKey(cleanAppId), false).toBool();
}

bool SteamManager::setAutoSyncEnabledForGame(const QString &appId, bool enabled)
{
    const QString cleanAppId = appId.trimmed();
    const GameInfo game = gameByAppId(cleanAppId);
    if (cleanAppId.isEmpty() || !game.isValid()) {
        return false;
    }

    const std::unique_ptr<QSettings> settings = AppSettings::create();
    settings->setValue(autoSyncGameSettingsKey(cleanAppId), enabled);
    settings->sync();
    if (settings->status() != QSettings::NoError) {
        m_logger.warning(QStringLiteral("自动同步设置保存失败：游戏 %1 的单独开关无法写入").arg(game.displayName));
        return false;
    }

    emit autoSyncSettingsChanged();
    updateSyncStatusForGame(cleanAppId, enabled ? QStringLiteral("自动同步已启用，等待游戏关闭后检查") : QStringLiteral("自动同步已对该游戏关闭"));
    m_logger.info(QStringLiteral("游戏自动同步开关已更新：%1，状态：%2")
                      .arg(game.displayName, enabled ? QStringLiteral("启用") : QStringLiteral("关闭")));
    return true;
}

bool SteamManager::setAutoSyncEnabledForAllGames(bool enabled)
{
    const QList<GameInfo> games = m_gameModel.games();
    const std::unique_ptr<QSettings> settings = AppSettings::create();
    int updatedCount = 0;
    for (const GameInfo &game : games) {
        if (!game.isValid()) {
            continue;
        }
        settings->setValue(autoSyncGameSettingsKey(game.appId), enabled);
        ++updatedCount;
    }
    settings->sync();
    if (settings->status() != QSettings::NoError) {
        m_logger.warning(QStringLiteral("自动同步批量设置保存失败：无法写入游戏开关"));
        return false;
    }

    for (const GameInfo &game : games) {
        if (!game.isValid()) {
            continue;
        }
        updateSyncStatusForGame(game.appId, enabled ? QStringLiteral("自动同步已启用，等待游戏关闭后检查") : QStringLiteral("自动同步已对该游戏关闭"));
    }

    emit autoSyncSettingsChanged();
    m_logger.info(QStringLiteral("自动同步批量设置完成：%1，影响游戏数量：%2")
                      .arg(enabled ? QStringLiteral("全开") : QStringLiteral("全关"))
                      .arg(updatedCount));
    return true;
}

bool SteamManager::setLaunchAtStartup(bool enabled)
{
    const bool wasEnabled = m_startupManager.isEnabled();
    QString errorMessage;
    if (!m_startupManager.setEnabled(enabled, &errorMessage)) {
        m_logger.warning(QStringLiteral("开机自启设置失败：%1").arg(errorMessage));
        emit userAlertRequested(
            QStringLiteral("开机自启设置失败"),
            enabled
                ? QStringLiteral("未能开启开机自启。")
                : QStringLiteral("未能关闭开机自启。"),
            errorMessage,
            false);
        emit startupSettingsChanged();
        return false;
    }

    const bool isEnabled = m_startupManager.isEnabled();
    if (wasEnabled != isEnabled) {
        emit startupSettingsChanged();
    }

    m_logger.info(isEnabled
                      ? QStringLiteral("开机自启已开启：软件会随 Windows 登录自动启动并进入后台监控")
                      : QStringLiteral("开机自启已关闭：软件不会随 Windows 登录自动启动"));
    return true;
}

QStringList SteamManager::steamAppsDirectories(const QString &steamRootPath) const
{
    QStringList directories;
    QSet<QString> seenDirectories;

    auto addSteamAppsDirectory = [&](const QString &path) {
        const QString cleanPath = QDir::cleanPath(path);
        if (cleanPath.isEmpty() || seenDirectories.contains(cleanPath)) {
            return;
        }

        const QDir dir(cleanPath);
        if (!dir.exists()) {
            return;
        }

        seenDirectories.insert(cleanPath);
        directories.append(cleanPath);
    };

    /*
     * 主 Steam 目录下通常一定有 steamapps。即使用户没有额外库，
     * 所有 appmanifest_*.acf 也会在这里。
     */
    addSteamAppsDirectory(QDir(steamRootPath).absoluteFilePath(QStringLiteral("steamapps")));

    /*
     * 如果用户把游戏装在 D 盘、E 盘等额外库，Steam 会在
     * steamapps/libraryfolders.vdf 中记录这些库路径。
     * 这里读取 "path" 字段，把每个库根目录转换为 <library>/steamapps。
     */
    const QString libraryFoldersPath = QDir(steamRootPath).absoluteFilePath(QStringLiteral("steamapps/libraryfolders.vdf"));
    QFile libraryFoldersFile(libraryFoldersPath);
    if (!libraryFoldersFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return directories;
    }

    const QString content = QString::fromUtf8(libraryFoldersFile.readAll());
    const QRegularExpression pathRegex(QStringLiteral(R"REGEX("path"\s+"([^"]+)")REGEX"));
    QRegularExpressionMatchIterator matches = pathRegex.globalMatch(content);

    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        QString libraryPath = match.captured(1);

        /*
         * VDF 文件中的 Windows 路径经常写成 D:\\SteamLibrary。
         * 先把双反斜杠还原，再交给 QDir 统一清理分隔符。
         */
        libraryPath.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
        addSteamAppsDirectory(QDir(libraryPath).absoluteFilePath(QStringLiteral("steamapps")));
    }

    return directories;
}

GameInfo SteamManager::parseAcfFile(const QString &acfFilePath) const
{
    GameInfo game;
    QFile file(acfFilePath);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return game;
    }

    /*
     * appmanifest_*.acf 是纯文本 KeyValues 文件，常见编码是 UTF-8。
     * QString::fromUtf8 可以保留中文游戏名；如果遇到极老文件编码异常，
     * 后续可以在这里增加本地编码兜底。
     */
    const QString content = QString::fromUtf8(file.readAll());

    const QRegularExpression appIdRegex(QStringLiteral(R"REGEX("appid"\s+"([^"]+)")REGEX"));
    const QRegularExpression nameRegex(QStringLiteral(R"REGEX("name"\s+"([^"]+)")REGEX"));
    const QRegularExpression installDirRegex(QStringLiteral(R"REGEX("installdir"\s+"([^"]+)")REGEX"));

    const QString appId = appIdRegex.match(content).captured(1).trimmed();
    const QString name = nameRegex.match(content).captured(1).trimmed();
    const QString installDir = installDirRegex.match(content).captured(1).trimmed();

    if (appId.isEmpty() || name.isEmpty()) {
        return {};
    }

    const QFileInfo manifestInfo(acfFilePath);
    game.appId = appId;
    game.name = name;
    game.installDir = installDir;
    game.displayName = name;
    game.manifestPath = manifestInfo.absoluteFilePath();
    game.headerImageUrl = QStringLiteral("https://cdn.cloudflare.steamstatic.com/steam/apps/%1/header.jpg").arg(appId);
    game.metadataStatus = QStringLiteral("正在获取资料");
    game.savePathStatus = QStringLiteral("正在识别存档路径");
    game.savePathSource = QStringLiteral("PCGamingWiki");
    game.savePathAvailability = QStringLiteral("等待检测");
    game.savePathDetail = QStringLiteral("等待存档路径识别完成");
    game.savePathCanSync = false;
    game.runningStatus = QStringLiteral("等待进程监控");
    game.syncStatus = QStringLiteral("未同步");
    game.snapshotStatus = QStringLiteral("未检查本地快照");
    game.snapshotDetail = QStringLiteral("等待用户设置快照目录并执行快照预处理检查");
    game.snapshotNeedsCreate = false;

    return game;
}

QString SteamManager::guessProcessNameForSteamGame(const GameInfo &game) const
{
    const QString installPath = installPathForSteamGame(game);
    if (installPath.isEmpty()) {
        return {};
    }

    const QDir installDir(installPath);
    if (!installDir.exists()) {
        return {};
    }

    /*
     * Steam 的 appmanifest 只告诉我们安装目录，不稳定记录启动 exe。
     * 这里做一个保守启发式扫描：
     * - 遍历游戏安装目录下的 exe。
     * - 排除卸载器、崩溃处理器、运行库安装器、反作弊安装器等明显不是游戏主体的文件。
     * - 优先选择文件名和游戏名、安装目录名最接近的 exe。
     *
     * 后续阶段如果要做到更准，可以解析 Steam appinfo.vdf 的 launch 配置；
     * 这一版先让进程监控有可用基础。
     */
    const QString normalizedGameName = game.name.toLower().remove(QRegularExpression(QStringLiteral(R"([^a-z0-9])")));
    const QString normalizedInstallDir = game.installDir.toLower().remove(QRegularExpression(QStringLiteral(R"([^a-z0-9])")));

    QString bestProcessName;
    int bestScore = -1;
    int scannedCount = 0;

    QDirIterator iterator(
        installPath,
        {QStringLiteral("*.exe")},
        QDir::Files | QDir::Readable,
        QDirIterator::Subdirectories);

    while (iterator.hasNext() && scannedCount < 200) {
        iterator.next();
        ++scannedCount;

        const QFileInfo executableInfo(iterator.filePath());
        const QString fileName = executableInfo.fileName();
        const QString baseName = executableInfo.completeBaseName();
        const QString normalizedBaseName = baseName.toLower().remove(QRegularExpression(QStringLiteral(R"([^a-z0-9])")));
        const QString lowerPath = QDir::toNativeSeparators(executableInfo.absoluteFilePath()).toLower();

        const QStringList ignoredFragments = {
            QStringLiteral("unins"),
            QStringLiteral("uninstall"),
            QStringLiteral("crashhandler"),
            QStringLiteral("unitycrashhandler"),
            QStringLiteral("vc_redist"),
            QStringLiteral("vcredist"),
            QStringLiteral("dxsetup"),
            QStringLiteral("dotnet"),
            QStringLiteral("redistributable"),
            QStringLiteral("eacsetup"),
            QStringLiteral("easyanticheat")
        };

        bool ignored = false;
        for (const QString &fragment : ignoredFragments) {
            if (lowerPath.contains(fragment)) {
                ignored = true;
                break;
            }
        }

        if (ignored || normalizedBaseName.isEmpty()) {
            continue;
        }

        int score = 10;
        if (!normalizedInstallDir.isEmpty() && normalizedBaseName == normalizedInstallDir) {
            score += 80;
        }

        if (!normalizedGameName.isEmpty() && normalizedBaseName == normalizedGameName) {
            score += 80;
        }

        if (!normalizedInstallDir.isEmpty() && normalizedBaseName.contains(normalizedInstallDir)) {
            score += 40;
        }

        if (!normalizedGameName.isEmpty() && normalizedGameName.contains(normalizedBaseName)) {
            score += 30;
        }

        if (lowerPath.contains(QStringLiteral("\\binaries\\")) || lowerPath.contains(QStringLiteral("\\win64\\"))) {
            score += 8;
        }

        if (baseName.contains(QStringLiteral("launcher"), Qt::CaseInsensitive)) {
            score -= 15;
        }

        if (score > bestScore) {
            bestScore = score;
            bestProcessName = fileName;
        }
    }

    return bestProcessName;
}

QString SteamManager::installPathForSteamGame(const GameInfo &game) const
{
    if (game.libraryPath.isEmpty() || game.installDir.isEmpty()) {
        return {};
    }

    return QDir::cleanPath(QDir(game.libraryPath).absoluteFilePath(
        QStringLiteral("steamapps/common/%1").arg(game.installDir)));
}

void SteamManager::appendManualGames(QList<GameInfo> &games, QSet<QString> &seenAppIds) const
{
    for (GameInfo game : m_manualGameManager.loadManualGames()) {
        if (game.appId.trimmed().isEmpty() || seenAppIds.contains(game.appId)) {
            continue;
        }

        if (game.displayName.trimmed().isEmpty()) {
            game.displayName = game.name;
        }

        if (game.runningStatus.trimmed().isEmpty()) {
            game.runningStatus = game.processName.trimmed().isEmpty()
                                     ? QStringLiteral("未识别到游戏主程序，暂不能监控运行状态")
                                     : QStringLiteral("等待进程监控");
        }

        if (game.syncStatus.trimmed().isEmpty()) {
            game.syncStatus = QStringLiteral("未同步");
        }

        if (game.snapshotStatus.trimmed().isEmpty()) {
            game.snapshotStatus = QStringLiteral("未检查本地快照");
        }

        if (game.snapshotDetail.trimmed().isEmpty()) {
            game.snapshotDetail = QStringLiteral("等待用户设置快照目录并执行快照预处理检查");
        }

        seenAppIds.insert(game.appId);
        games.append(game);
    }
}

void SteamManager::upsertGameAndRebuild(const GameInfo &game, const QString &oldAppId)
{
    m_gameModel.upsertGame(game, oldAppId);
    m_processMonitor.setGames(m_gameModel.games());
    m_processMonitor.start();
    rebuildInstalledGamesFromModel();
}

void SteamManager::persistManualGameIfPresent(const QString &appId)
{
    for (const GameInfo &game : m_gameModel.games()) {
        if (game.appId == appId && game.isManualGame) {
            m_manualGameManager.saveManualGame(game);
            return;
        }
    }
}

bool SteamManager::shouldHideSteamApp(const GameInfo &game) const
{
    return game.appId == QStringLiteral("228980")
        || game.name.compare(QStringLiteral("Steamworks Common Redistributables"), Qt::CaseInsensitive) == 0
        || game.displayName.compare(QStringLiteral("Steamworks Common Redistributables"), Qt::CaseInsensitive) == 0;
}

void SteamManager::applyManualSavePaths(QList<GameInfo> &games)
{
    m_savePathResolver.setSteamPath(m_steamPath);
    for (GameInfo &game : games) {
        m_savePathResolver.applyManualSavePath(game);
    }
}

bool SteamManager::saveGameMetadataCacheForApp(const QString &appId)
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return false;
    }

    return m_gameMetadataCache.saveGame(game);
}

void SteamManager::requestMetadataForGames(const QList<GameInfo> &games)
{
    for (const GameInfo &game : games) {
        if (!isNumericAppId(game.appId)) {
            continue;
        }

        m_metadataClient.requestAppDetails(game.appId);
    }
}

void SteamManager::resolveSavePathsForGames(const QList<GameInfo> &games)
{
    m_savePathResolver.setSteamPath(m_steamPath);
    for (const GameInfo &game : games) {
        m_savePathResolver.resolveGameSavePath(game);
    }
}

void SteamManager::rebuildInstalledGamesFromModel()
{
    QVariantList games;
    for (const GameInfo &game : m_gameModel.games()) {
        games.append(game.toVariantMap());
    }

    if (m_installedGames != games) {
        m_installedGames = games;
        emit installedGamesChanged();
    }
}

GameInfo SteamManager::gameByAppId(const QString &appId) const
{
    for (const GameInfo &game : m_gameModel.games()) {
        if (game.appId == appId) {
            return game;
        }
    }

    return {};
}

void SteamManager::applySnapshotResult(const QString &appId, const QVariantMap &result)
{
    const QString status = result.value(QStringLiteral("status")).toString();
    const QString detail = result.value(QStringLiteral("detail")).toString();
    const QString directory = result.value(QStringLiteral("snapshotDirectory")).toString();
    const QString latestSnapshotFileName = result.value(QStringLiteral("snapshotFileName")).toString().isEmpty()
                                               ? result.value(QStringLiteral("latestSnapshotFileName")).toString()
                                               : result.value(QStringLiteral("snapshotFileName")).toString();
    const QString latestSnapshotPath = result.value(QStringLiteral("snapshotPath")).toString().isEmpty()
                                           ? result.value(QStringLiteral("latestSnapshotPath")).toString()
                                           : result.value(QStringLiteral("snapshotPath")).toString();
    const int snapshotCount = result.value(QStringLiteral("snapshotCount")).toInt();
    const bool needsCreate = result.value(QStringLiteral("needsSnapshot")).toBool();

    if (m_gameModel.updateSnapshotStatus(
            appId,
            status,
            detail,
            directory,
            latestSnapshotFileName,
            latestSnapshotPath,
            snapshotCount,
            needsCreate)) {
        persistManualGameIfPresent(appId);
        rebuildInstalledGamesFromModel();
    }
}

void SteamManager::refreshSnapshotRecordsForGame(
    const QString &appId,
    const QString &status,
    const QString &detail,
    bool needsCreate)
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return;
    }

    const QVariantList localSnapshots = m_snapshotPreprocessor.snapshotsForGame(game);
    const QVariantMap latestSnapshot = localSnapshots.isEmpty()
                                           ? QVariantMap()
                                           : localSnapshots.first().toMap();
    QString snapshotDirectory = m_snapshotPreprocessor.snapshotDirectoryForGame(game);
    if (snapshotDirectory.trimmed().isEmpty()) {
        snapshotDirectory = game.snapshotDirectory;
    }

    const QVariantMap refreshed = {
        {QStringLiteral("success"), true},
        {QStringLiteral("status"), status},
        {QStringLiteral("detail"), detail},
        {QStringLiteral("snapshotDirectory"), snapshotDirectory},
        {QStringLiteral("latestSnapshotFileName"), latestSnapshot.value(QStringLiteral("fileName")).toString()},
        {QStringLiteral("latestSnapshotPath"), latestSnapshot.value(QStringLiteral("zipPath")).toString()},
        {QStringLiteral("snapshotCount"), localSnapshots.count()},
        {QStringLiteral("needsSnapshot"), needsCreate}
    };
    applySnapshotResult(appId, refreshed);
}

void SteamManager::handleSnapshotUploadFinished(
    bool success,
    const QString &localFilePath,
    const QString &remoteFilePath,
    const QString &uploadState,
    const QString &message)
{
    const QString appId = m_pendingSnapshotUploadAppIds.take(localFilePath);
    const QString fileName = m_pendingSnapshotUploadFileNames.take(localFilePath);
    const bool isBatchUpload = m_pendingBatchSnapshotUploadPaths.remove(localFilePath) > 0;
    const bool isAutoSyncUpload = m_pendingAutoSyncUploadPaths.remove(localFilePath) > 0;
    const GameInfo game = gameByAppId(appId);

    if (!game.isValid()) {
        m_logger.warning(QStringLiteral("快照上传结果无法匹配游戏：%1，结果：%2").arg(localFilePath, message));
        return;
    }

    const QString currentSnapshotDirectory = m_snapshotPreprocessor.snapshotDirectoryForGame(game);

    if (success) {
        m_snapshotPreprocessor.markSnapshotUploadState(
            game,
            localFilePath,
            fileName,
            uploadState,
            remoteFilePath);
        const QVariantMap refreshed = {
            {QStringLiteral("success"), true},
            {QStringLiteral("status"), uploadState == QStringLiteral("remote_exists")
                                           ? QStringLiteral("云端已存在同名快照，已跳过上传")
                                           : (uploadState == QStringLiteral("overwritten")
                                                  ? QStringLiteral("选中快照已覆盖上传云端")
                                                  : QStringLiteral("本地快照已上传云端"))},
            {QStringLiteral("detail"), message},
            {QStringLiteral("snapshotDirectory"), currentSnapshotDirectory},
            {QStringLiteral("latestSnapshotFileName"), fileName},
            {QStringLiteral("latestSnapshotPath"), localFilePath},
            {QStringLiteral("snapshotCount"), m_snapshotPreprocessor.snapshotsForGame(game).count()},
            {QStringLiteral("needsSnapshot"), false}
        };
        applySnapshotResult(appId, refreshed);
        m_logger.info(QStringLiteral("快照云端同步完成：%1，状态：%2，云端路径：%3")
                          .arg(game.displayName, uploadState, remoteFilePath));
        if (isAutoSyncUpload) {
            updateSyncStatusForGame(
                appId,
                uploadState == QStringLiteral("remote_exists")
                    ? QStringLiteral("自动同步完成：已创建本地快照，云端已有同名快照并已跳过上传")
                    : QStringLiteral("自动同步完成：已创建本地快照并上传云端"));
            m_logger.info(QStringLiteral("自动同步云端上传完成：%1，状态：%2，云端路径：%3")
                              .arg(game.displayName, uploadState, remoteFilePath));
        }
        if (isBatchUpload) {
            ++m_pendingSnapshotUploadSuccessByAppId[appId];
        }
    } else {
        if (isBatchUpload) {
            ++m_pendingSnapshotUploadFailureByAppId[appId];
        }
        const QVariantMap failed = {
            {QStringLiteral("success"), false},
            {QStringLiteral("status"), QStringLiteral("快照上传失败")},
            {QStringLiteral("detail"), message},
            {QStringLiteral("snapshotDirectory"), currentSnapshotDirectory},
            {QStringLiteral("latestSnapshotFileName"), fileName},
            {QStringLiteral("latestSnapshotPath"), localFilePath},
            {QStringLiteral("snapshotCount"), m_snapshotPreprocessor.snapshotsForGame(game).count()},
            {QStringLiteral("needsSnapshot"), false}
        };
        applySnapshotResult(appId, failed);
        m_logger.error(QStringLiteral("快照云端同步失败：%1，原因：%2").arg(game.displayName, message));
        if (isAutoSyncUpload) {
            updateSyncStatusForGame(appId, QStringLiteral("自动同步失败：已保留本地快照，云端上传失败，可稍后手动上传"));
            m_logger.error(QStringLiteral("自动同步云端上传失败：%1，原因：%2").arg(game.displayName, message));
            if (remoteFilePath.startsWith(QStringLiteral("/Quark"), Qt::CaseInsensitive)
                && isQuarkAuthFailureMessage(message)) {
                setQuarkGatewayStatus(QStringLiteral("夸克 Cookie 可能已过期或上传鉴权异常：自动同步已创建本地快照，但上传云端失败。请在云同步设置中更新 Cookie 后重新连接。"));
            } else if (message.contains(QStringLiteral("quark_gateway_not_ready"), Qt::CaseInsensitive)
                       || message.contains(QStringLiteral("未就绪"), Qt::CaseInsensitive)
                       || message.contains(QStringLiteral("未连接"), Qt::CaseInsensitive)) {
                emit userAlertRequested(
                    QStringLiteral("自动同步未上传云端"),
                    QStringLiteral("%1 的本地快照已创建，但云端连接尚未就绪。").arg(game.displayName),
                    QStringLiteral("本地 zip 快照已经保留。请进入“云同步设置”确认夸克网关连接成功后，再手动上传该快照。失败原因：%1").arg(message),
                    false);
            } else {
                emit userAlertRequested(
                    QStringLiteral("自动同步上传失败"),
                    QStringLiteral("%1 的本地快照已创建，但没有上传到云端。").arg(game.displayName),
                    QStringLiteral("本地 zip 快照已经保留，可以稍后在游戏详情页点击“上传全部”手动补传。失败原因：%1").arg(message),
                    false);
            }
        }
    }

    int remaining = isBatchUpload ? m_pendingSnapshotUploadRemainingByAppId.value(appId, 0) : 0;
    if (remaining <= 0) {
        if (success && remoteFilePath.startsWith(QStringLiteral("/Quark"), Qt::CaseInsensitive)) {
            requestCloudManifestMergeForGame(appId);
        }
        return;
    }

    --remaining;
    if (remaining > 0) {
        m_pendingSnapshotUploadRemainingByAppId.insert(appId, remaining);
        refreshSnapshotRecordsForGame(
            appId,
            QStringLiteral("正在上传并补齐云端缺失快照"),
            QStringLiteral("本轮快照上传检查仍有 %1 个文件等待完成").arg(remaining),
            false);
        return;
    }

    m_pendingSnapshotUploadRemainingByAppId.remove(appId);
    const int successCount = m_pendingSnapshotUploadSuccessByAppId.take(appId);
    const int failureCount = m_pendingSnapshotUploadFailureByAppId.take(appId);
    refreshSnapshotRecordsForGame(
        appId,
        failureCount <= 0
            ? QStringLiteral("本地快照已全部完成云端同步检查")
            : QStringLiteral("部分本地快照上传或检查失败"),
        failureCount <= 0
            ? QStringLiteral("本轮已成功确认或上传 %1 个本地快照，接下来会重建云端快照索引")
                  .arg(successCount)
            : QStringLiteral("本轮成功确认或上传 %1 个快照，失败 %2 个；失败文件不会写入新的云端索引")
                  .arg(successCount)
                  .arg(failureCount),
        false);

    if (failureCount <= 0 && successCount > 0
        && cloudDirectoryForGame(game).startsWith(QStringLiteral("/Quark"), Qt::CaseInsensitive)) {
        requestCloudManifestMergeForGame(appId);
    }
}

void SteamManager::handleSnapshotDownloadFinished(
    bool success,
    const QString &remoteFilePath,
    const QString &localFilePath,
    const QString &message)
{
    const QString appId = m_pendingSnapshotDownloadAppIds.take(localFilePath);
    const QVariantMap cloudRecord = m_pendingSnapshotDownloadRecords.take(localFilePath);
    const bool shouldRestoreAfterDownload = m_pendingSnapshotRestoreDownloadPaths.remove(localFilePath) > 0;
    const GameInfo game = gameByAppId(appId);
    const bool isBatchDownload = m_pendingSnapshotDownloadRemainingByAppId.value(appId, 0) > 0;

    if (!game.isValid()) {
        m_logger.warning(QStringLiteral("快照下载结果无法匹配游戏：云端路径 %1，本地路径 %2，结果：%3")
                             .arg(remoteFilePath, QDir::toNativeSeparators(localFilePath), message));
        return;
    }

    if (success) {
        if (!cloudRecord.isEmpty()
            && !m_snapshotPreprocessor.importCloudSnapshotRecord(game, cloudRecord, localFilePath)) {
            m_logger.warning(QStringLiteral("快照已下载，但写入本地快照索引失败：%1，本地路径 %2")
                                 .arg(game.displayName, QDir::toNativeSeparators(localFilePath)));
        }
        if (!shouldRestoreAfterDownload || isBatchDownload) {
            ++m_pendingSnapshotDownloadSuccessByAppId[appId];
        }
        if (shouldRestoreAfterDownload) {
            m_logger.info(QStringLiteral("云端快照下载完成，开始自动恢复：%1，文件：%2")
                              .arg(game.displayName, QDir::toNativeSeparators(localFilePath)));
            restoreLocalSnapshotForGame(appId, localFilePath);
            if (!isBatchDownload) {
                return;
            }
        }
        m_logger.info(QStringLiteral("快照下载完成：%1，云端路径 %2，本地路径 %3")
                          .arg(game.displayName, remoteFilePath, QDir::toNativeSeparators(localFilePath)));
    } else {
        if (!shouldRestoreAfterDownload || isBatchDownload) {
            ++m_pendingSnapshotDownloadFailureByAppId[appId];
        }
        if (shouldRestoreAfterDownload) {
            refreshSnapshotRecordsForGame(
                appId,
                QStringLiteral("云端快照恢复失败"),
                QStringLiteral("所选云端快照下载失败，因此未执行恢复。原因：%1").arg(message),
                false);
            m_logger.warning(QStringLiteral("云端快照恢复失败：%1，云端快照下载失败，原因：%2")
                                 .arg(game.displayName, message));
            if (!isBatchDownload) {
                return;
            }
        }
        m_logger.warning(QStringLiteral("快照下载失败：%1，云端路径 %2，原因：%3")
                             .arg(game.displayName, remoteFilePath, message));
        if (remoteFilePath.startsWith(QStringLiteral("/Quark"), Qt::CaseInsensitive)
            && isQuarkAuthFailureMessage(message)) {
            setQuarkGatewayStatus(QStringLiteral("夸克 Cookie 可能已过期或下载鉴权异常：目录读取可能正常，但文件下载会失败。请在云同步设置中更新 Cookie 后重新连接。"));
        }
    }

    int remaining = m_pendingSnapshotDownloadRemainingByAppId.value(appId, 0);
    if (remaining > 0) {
        --remaining;
        if (remaining <= 0) {
            m_pendingSnapshotDownloadRemainingByAppId.remove(appId);
            const int successCount = m_pendingSnapshotDownloadSuccessByAppId.take(appId);
            const int failureCount = m_pendingSnapshotDownloadFailureByAppId.take(appId);
            refreshSnapshotRecordsForGame(
                appId,
                failureCount <= 0
                    ? QStringLiteral("缺失的云端快照已下载完成")
                    : QStringLiteral("部分云端快照下载失败"),
                failureCount <= 0
                    ? QStringLiteral("本轮已成功下载 %1 个缺失快照，并已刷新本地快照列表")
                          .arg(successCount)
                    : QStringLiteral("本轮成功下载 %1 个快照，失败 %2 个；失败原因可查看上方日志")
                          .arg(successCount)
                          .arg(failureCount),
                false);
            return;
        }

        m_pendingSnapshotDownloadRemainingByAppId.insert(appId, remaining);
        refreshSnapshotRecordsForGame(
            appId,
            QStringLiteral("正在下载缺失的云端快照"),
            QStringLiteral("本轮快照下载仍有 %1 个文件等待完成").arg(remaining),
            false);
        return;
    }

    refreshSnapshotRecordsForGame(
        appId,
        success ? QStringLiteral("快照已下载到本地快照目录") : QStringLiteral("快照下载失败"),
        message,
        false);
}

void SteamManager::handleCloudDataUploadFinished(
    bool success,
    const QString &remoteFilePath,
    const QString &operationId,
    const QString &message)
{
    if (operationId == QStringLiteral("quark-cookie-health-upload")) {
        if (success) {
            m_logger.info(QStringLiteral("夸克 Cookie 健康检查：探针文件已写入，开始验证读取能力"));
            m_quarkGatewayManager.downloadDataFile(
                quarkCookieHealthCheckPath(),
                QStringLiteral("quark-cookie-health-download"));
        } else {
            const QString status = isQuarkAuthFailureMessage(message)
                                       ? QStringLiteral("夸克 Cookie 健康检查失败：Cookie 可能已过期或鉴权异常，请更新 Cookie 后重新连接")
                                       : QStringLiteral("夸克 Cookie 健康检查失败：无法写入探针文件，请查看日志确认 OpenList/夸克网关状态");
            setQuarkGatewayStatus(status);
            m_logger.warning(QStringLiteral("夸克 Cookie 健康检查写入失败：路径：%1，原因：%2").arg(remoteFilePath, message));
        }
        return;
    }

    if (operationId.startsWith(QStringLiteral("cloud-game-upload:"))) {
        const QString appId = operationId.mid(QStringLiteral("cloud-game-upload:").length());
        const GameInfo game = gameByAppId(appId);
        const QString displayName = game.isValid() ? game.displayName : appId;
        if (success) {
            m_logger.info(QStringLiteral("游戏云端快照索引已更新：%1，路径：%2").arg(displayName, remoteFilePath));
            refreshCloudSnapshotsForGame(appId);
        } else {
            m_logger.warning(QStringLiteral("游戏云端快照索引更新失败：%1，路径：%2，原因：%3").arg(displayName, remoteFilePath, message));
        }
        return;
    }

    if (operationId.startsWith(QStringLiteral("cloud-root-upload:"))) {
        const QString appId = operationId.mid(QStringLiteral("cloud-root-upload:").length());
        const GameInfo game = gameByAppId(appId);
        const QString displayName = game.isValid() ? game.displayName : appId;
        if (success) {
            m_cloudManifestLoaded = true;
            m_logger.info(QStringLiteral("云端根索引已更新：%1，路径：%2").arg(displayName, remoteFilePath));
        } else {
            m_logger.warning(QStringLiteral("云端根索引更新失败：%1，路径：%2，原因：%3").arg(displayName, remoteFilePath, message));
        }
        return;
    }

    if (!success) {
        m_logger.warning(QStringLiteral("云端索引上传失败：路径：%1，操作：%2，原因：%3").arg(remoteFilePath, operationId, message));
    }
}

void SteamManager::handleCloudDataDownloadFinished(
    bool success,
    const QString &remoteFilePath,
    const QString &operationId,
    const QByteArray &data,
    const QString &message)
{
    if (operationId == QStringLiteral("quark-cookie-health-download")) {
        if (success) {
            const QJsonObject payload = QJsonDocument::fromJson(data).object();
            const bool ok = payload.value(QStringLiteral("type")).toString() == QStringLiteral("quark-cookie-health-check");
            if (ok) {
                setQuarkGatewayStatus(QStringLiteral("夸克 Cookie 健康检查通过：OpenList 本机网关可写入并读取文件，上传和下载快照可用"));
                m_logger.info(QStringLiteral("夸克 Cookie 健康检查通过：探针文件读取成功，路径：%1").arg(remoteFilePath));
            } else {
                setQuarkGatewayStatus(QStringLiteral("夸克 Cookie 健康检查异常：已读取探针文件，但内容不完整，请重新连接后再试"));
                m_logger.warning(QStringLiteral("夸克 Cookie 健康检查异常：探针文件内容不符合预期，路径：%1").arg(remoteFilePath));
            }
        } else {
            const QString status = isQuarkAuthFailureMessage(message)
                                       ? QStringLiteral("夸克 Cookie 健康检查失败：Cookie 可能已过期或下载鉴权异常。若目录能打开但 zip 下载失败，请更新 Cookie 后重新连接")
                                       : QStringLiteral("夸克 Cookie 健康检查失败：探针文件读取失败，请查看日志确认 OpenList/夸克网关状态");
            setQuarkGatewayStatus(status);
            m_logger.warning(QStringLiteral("夸克 Cookie 健康检查读取失败：路径：%1，原因：%2").arg(remoteFilePath, message));
        }
        return;
    }

    if (operationId == QStringLiteral("cloud-root-refresh")) {
        m_cloudManifestRefreshInFlight = false;
        if (success) {
            applyCloudRootManifest(data);
            m_logger.info(QStringLiteral("云端快照索引读取完成：%1").arg(remoteFilePath));
            const QHash<QString, bool> pendingRefreshes = m_pendingCloudSnapshotRefreshQuietByAppId;
            m_pendingCloudSnapshotRefreshQuietByAppId.clear();
            for (auto it = pendingRefreshes.constBegin(); it != pendingRefreshes.constEnd(); ++it) {
                refreshCloudSnapshotsForGameInternal(it.key(), it.value());
            }
        } else {
            m_cloudManifestLoaded = true;
            m_cloudSnapshotRecordsByAppId.clear();
            m_cloudDirectoryByAppId.clear();
            m_pendingCloudSnapshotRefreshQuietByAppId.clear();
            applyCloudSnapshotRecordsToModel();
            if (isQuarkAuthFailureMessage(message)) {
                setQuarkGatewayStatus(QStringLiteral("夸克 Cookie 可能已过期或云端读取鉴权异常，请在云同步设置中更新 Cookie 后重新连接"));
            }
            m_logger.warning(QStringLiteral("云端快照索引读取失败：%1；如果这是首次使用，上传快照后会自动创建。原因：%2")
                                 .arg(remoteFilePath, message));
        }
        return;
    }

    if (operationId.startsWith(QStringLiteral("cloud-game-download-all:"))) {
        const QString appId = operationId.mid(QStringLiteral("cloud-game-download-all:").length());
        const GameInfo game = gameByAppId(appId);
        if (!game.isValid()) {
            return;
        }

        if (success) {
            const QJsonObject gameManifest = QJsonDocument::fromJson(data).object();
            const QVariantList cloudRecords = cloudSnapshotRecordsFromGameManifest(gameManifest);
            if (cloudRecords.isEmpty()) {
                m_logger.warning(QStringLiteral("游戏完整云端快照索引没有可下载记录：%1，路径：%2")
                                     .arg(game.displayName, remoteFilePath));
                updateCloudSnapshotStatusForGame(
                    appId,
                    {},
                    QStringLiteral("云端快照索引没有可下载记录"),
                    QStringLiteral("已读取该游戏的 snapshot-manifest.json，但其中没有包含有效的 zip 文件名和云端路径"));
                return;
            }

            m_cloudSnapshotRecordsByAppId.insert(appId, cloudRecords);
            updateCloudSnapshotStatusForGame(
                appId,
                cloudRecords,
                QStringLiteral("已读取完整云端快照索引"),
                QStringLiteral("已从该游戏的 snapshot-manifest.json 读取到完整快照列表，下载时会补齐本地缺失的 zip 文件"));
            m_logger.info(QStringLiteral("游戏完整云端快照索引读取完成：%1，共 %2 个快照，路径：%3，方式：%4")
                              .arg(game.displayName)
                              .arg(cloudRecords.count())
                              .arg(remoteFilePath, message));
            downloadSnapshotRecordsForGame(game, cloudRecords);
        } else {
            m_logger.warning(QStringLiteral("游戏完整云端快照索引读取失败：%1，路径：%2，原因：%3；已停止使用可能过期的云端摘要记录")
                                 .arg(game.displayName, remoteFilePath, message));
            if (isQuarkAuthFailureMessage(message)) {
                setQuarkGatewayStatus(QStringLiteral("夸克 Cookie 可能已过期或云端读取鉴权异常，请在云同步设置中更新 Cookie 后重新连接"));
            }
            m_cloudSnapshotRecordsByAppId.insert(appId, QVariantList{});
            updateCloudSnapshotStatusForGame(
                appId,
                {},
                QStringLiteral("云端快照索引读取失败"),
                QStringLiteral("无法读取该游戏的 snapshot-manifest.json，软件不会使用可能过期的历史索引继续下载；原因：%1")
                    .arg(message));
            refreshSnapshotRecordsForGame(
                appId,
                QStringLiteral("下载全部快照失败"),
                QStringLiteral("无法读取游戏完整云端索引，已停止使用过期云端记录"),
                false);
            emit cloudSnapshotsChanged(appId);
        }
        return;
    }

    if (operationId.startsWith(QStringLiteral("cloud-game-merge-read:"))) {
        const QString appId = operationId.mid(QStringLiteral("cloud-game-merge-read:").length());
        const GameInfo game = gameByAppId(appId);
        if (!game.isValid()) {
            return;
        }

        if (success) {
            const QJsonObject gameManifest = QJsonDocument::fromJson(data).object();
            const QVariantList cloudRecords = cloudSnapshotRecordsFromGameManifest(gameManifest);
            if (!cloudRecords.isEmpty()) {
                m_cloudSnapshotRecordsByAppId.insert(appId, cloudRecords);
                updateCloudSnapshotStatusForGame(
                    appId,
                    cloudRecords,
                    QStringLiteral("已合并云端现有快照索引"),
                    QStringLiteral("写入云端索引前，已先读取并合并云端目录中真实存在的 zip 快照"));
            }
            m_logger.info(QStringLiteral("云端索引合并准备完成：%1，现有云端快照 %2 个，路径：%3，方式：%4")
                              .arg(game.displayName)
                              .arg(cloudRecords.count())
                              .arg(remoteFilePath, message));
        } else {
            m_logger.warning(QStringLiteral("云端索引合并读取失败：%1，路径：%2，原因：%3；将仅用本地已确认快照继续写入")
                                 .arg(game.displayName, remoteFilePath, message));
        }

        uploadCloudManifestsForGame(game);
        return;
    }

    if (operationId.startsWith(QStringLiteral("cloud-game-list-ui:"))
        || operationId.startsWith(QStringLiteral("cloud-game-list-ui-quiet:"))) {
        const bool quiet = operationId.startsWith(QStringLiteral("cloud-game-list-ui-quiet:"));
        const QString prefix = quiet
                                   ? QStringLiteral("cloud-game-list-ui-quiet:")
                                   : QStringLiteral("cloud-game-list-ui:");
        const QString appId = operationId.mid(prefix.length());
        const GameInfo game = gameByAppId(appId);
        if (!game.isValid()) {
            return;
        }

        if (success) {
            const QJsonObject gameManifest = QJsonDocument::fromJson(data).object();
            const QVariantList cloudRecords = cloudSnapshotRecordsFromGameManifest(gameManifest);
            m_cloudSnapshotRecordsByAppId.insert(appId, cloudRecords);
            updateCloudSnapshotStatusForGame(
                appId,
                cloudRecords,
                cloudRecords.isEmpty()
                    ? QStringLiteral("云端快照索引没有可下载记录")
                    : QStringLiteral("已读取完整云端快照索引"),
                cloudRecords.isEmpty()
                    ? QStringLiteral("已读取该游戏的 snapshot-manifest.json，但其中没有包含有效的 zip 文件名和云端路径")
                    : QStringLiteral("已从该游戏的 snapshot-manifest.json 读取到完整快照列表，可在弹窗中选择单个快照覆盖下载"));
            if (!quiet) {
                m_logger.info(QStringLiteral("可选下载快照列表读取完成：%1，共 %2 个快照，路径：%3，方式：%4")
                                  .arg(game.displayName)
                                  .arg(cloudRecords.count())
                                  .arg(remoteFilePath, message));
            }
        } else {
            m_cloudSnapshotRecordsByAppId.insert(appId, QVariantList{});
            if (isQuarkAuthFailureMessage(message)) {
                setQuarkGatewayStatus(QStringLiteral("夸克 Cookie 可能已过期或云端读取鉴权异常，请在云同步设置中更新 Cookie 后重新连接"));
            }
            updateCloudSnapshotStatusForGame(
                appId,
                {},
                QStringLiteral("云端快照索引读取失败"),
                QStringLiteral("无法读取该游戏的 snapshot-manifest.json，已清空当前内存中的可选下载列表；原因：%1")
                    .arg(message));
            m_logger.warning(QStringLiteral("可选下载快照列表读取失败：%1，路径：%2，原因：%3")
                                 .arg(game.displayName, remoteFilePath, message));
        }

        emit cloudSnapshotsChanged(appId);
        return;
    }

    if (operationId.startsWith(QStringLiteral("cloud-root-read:"))) {
        const QString appId = operationId.mid(QStringLiteral("cloud-root-read:").length());
        const GameInfo game = gameByAppId(appId);
        if (!game.isValid()) {
            return;
        }

        const QVariantList currentGameCloudRecords = m_cloudSnapshotRecordsByAppId.value(appId);
        if (success) {
            applyCloudRootManifest(data);
            if (!currentGameCloudRecords.isEmpty()) {
                m_cloudSnapshotRecordsByAppId.insert(appId, currentGameCloudRecords);
            }
        }

        const QJsonObject gameManifest = buildGameCloudManifest(game);
        const QJsonObject gameSummary = cloudGameSummaryFromManifest(gameManifest);
        const QJsonObject rootManifest = buildCloudRootManifest(success ? data : QByteArray(), gameSummary);
        m_quarkGatewayManager.uploadDataFile(
            cloudRootManifestPath(),
            QJsonDocument(rootManifest).toJson(QJsonDocument::Indented),
            QStringLiteral("cloud-root-upload:%1").arg(appId));

        if (!success) {
            m_logger.info(QStringLiteral("云端根索引尚未存在或读取失败，将创建新的根索引：%1，原因：%2")
                              .arg(remoteFilePath, message));
        }
        return;
    }
}

void SteamManager::requestCloudManifestMergeForGame(const QString &appId)
{
    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return;
    }

    const QString gameManifestPath = cloudDirectoryForGame(game) + QStringLiteral("/snapshot-manifest.json");
    if (gameManifestPath.startsWith(QStringLiteral("/Quark"), Qt::CaseInsensitive)) {
        m_quarkGatewayManager.downloadDataFile(
            gameManifestPath,
            QStringLiteral("cloud-game-merge-read:%1").arg(appId));
        return;
    }

    uploadCloudManifestsForGame(game);
}

void SteamManager::uploadCloudManifestsForGame(const GameInfo &game)
{
    const QString appId = game.appId;
    const QJsonObject gameManifest = buildGameCloudManifest(game);
    const QVariantList cloudRecords = cloudSnapshotRecordsFromGameManifest(gameManifest);
    if (cloudRecords.isEmpty()) {
        m_logger.warning(QStringLiteral("云端索引更新跳过：%1 暂无已确认在云端存在的快照记录").arg(game.displayName));
        return;
    }

    updateCloudSnapshotStatusForGame(
        appId,
        cloudRecords,
        QStringLiteral("云端索引已更新"),
        QStringLiteral("上传成功后已刷新本地云端索引状态，并开始写入云端 snapshot-manifest.json 和 cloud-manifest.json"));
    m_cloudSnapshotRecordsByAppId.insert(appId, cloudRecords);
    emit cloudSnapshotsChanged(appId);

    const QString gameManifestPath = cloudDirectoryForGame(game) + QStringLiteral("/snapshot-manifest.json");
    m_quarkGatewayManager.uploadDataFile(
        gameManifestPath,
        QJsonDocument(gameManifest).toJson(QJsonDocument::Indented),
        QStringLiteral("cloud-game-upload:%1").arg(appId));

    m_quarkGatewayManager.downloadDataFile(
        cloudRootManifestPath(),
        QStringLiteral("cloud-root-read:%1").arg(appId));
}

QJsonObject SteamManager::buildGameCloudManifest(const GameInfo &game) const
{
    /*
     * 游戏目录下的 snapshot-manifest.json 是“云端索引”：
     * - 只记录已经确认在云端存在的 zip 快照；
     * - 保存 remotePath，方便换电脑后不依赖本地 manifest 也能下载；
     * - 不记录具体存档文件列表，恢复解压会在阶段 9 单独处理。
     */
    QVariantList records;
    QSet<QString> seenFileNames;

    auto appendRecords = [&](const QVariantList &items) {
        for (const QVariant &item : items) {
            QVariantMap record = item.toMap();
            QString fileName = record.value(QStringLiteral("fileName")).toString().trimmed();
            QString remotePath = record.value(QStringLiteral("remotePath")).toString().trimmed();
            if (fileName.isEmpty() && !remotePath.isEmpty()) {
                fileName = QFileInfo(remotePath).fileName();
                record.insert(QStringLiteral("fileName"), fileName);
            }
            if (remotePath.isEmpty() && !fileName.isEmpty()) {
                remotePath = cloudDirectoryForGame(game) + QLatin1Char('/') + fileName;
                record.insert(QStringLiteral("remotePath"), remotePath);
            }
            if (fileName.isEmpty() || remotePath.isEmpty()) {
                continue;
            }

            const QString fileKey = fileName.toCaseFolded();
            if (seenFileNames.contains(fileKey)) {
                continue;
            }

            seenFileNames.insert(fileKey);
            records.append(record);
        }
    };

    appendRecords(cloudSnapshotRecordsFromExistingLocalSnapshots(game));
    appendRecords(m_cloudSnapshotRecordsByAppId.value(game.appId));

    std::sort(records.begin(), records.end(), [](const QVariant &left, const QVariant &right) {
        return left.toMap().value(QStringLiteral("fileName")).toString()
            > right.toMap().value(QStringLiteral("fileName")).toString();
    });

    QJsonArray snapshots;
    for (const QVariant &item : records) {
        const QVariantMap record = item.toMap();
        QJsonObject snapshot;
        snapshot.insert(QStringLiteral("fileName"), record.value(QStringLiteral("fileName")).toString());
        snapshot.insert(QStringLiteral("remotePath"), record.value(QStringLiteral("remotePath")).toString());
        snapshot.insert(QStringLiteral("uploadState"), record.value(QStringLiteral("uploadState")).toString());
        snapshot.insert(QStringLiteral("remoteVerifiedAt"), record.value(QStringLiteral("remoteVerifiedAt")).toString());
        snapshot.insert(QStringLiteral("createdAtUtc"), record.value(QStringLiteral("createdAtUtc")).toString());
        snapshot.insert(QStringLiteral("zipSize"), record.value(QStringLiteral("zipSize")).toString());
        snapshot.insert(QStringLiteral("contentDigest"), record.value(QStringLiteral("contentDigest")).toString());
        snapshot.insert(QStringLiteral("fileCount"), record.value(QStringLiteral("fileCount")).toInt());
        snapshot.insert(QStringLiteral("totalBytes"), record.value(QStringLiteral("totalBytes")).toString());
        snapshots.append(snapshot);
    }

    const QVariantMap latest = records.isEmpty() ? QVariantMap() : records.first().toMap();
    QJsonObject manifest;
    manifest.insert(QStringLiteral("schemaVersion"), 1);
    manifest.insert(QStringLiteral("appId"), game.appId);
    manifest.insert(QStringLiteral("gameName"), game.displayName.isEmpty() ? game.name : game.displayName);
    manifest.insert(QStringLiteral("remoteDirectory"), cloudDirectoryForGame(game));
    manifest.insert(QStringLiteral("updatedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    manifest.insert(QStringLiteral("snapshotCount"), records.count());
    manifest.insert(QStringLiteral("latestSnapshotFileName"), latest.value(QStringLiteral("fileName")).toString());
    manifest.insert(QStringLiteral("latestRemotePath"), latest.value(QStringLiteral("remotePath")).toString());
    manifest.insert(QStringLiteral("snapshots"), snapshots);
    return manifest;
}

QJsonObject SteamManager::buildCloudRootManifest(
    const QByteArray &existingRootData,
    const QJsonObject &gameSummary) const
{
    /*
     * 根目录 cloud-manifest.json 只保存每个游戏的摘要。
     * 软件启动或夸克连接成功后先读取这个小文件，就能快速知道：
     * 哪些游戏云端有快照、最新快照叫什么、下载路径在哪里。
     */
    QJsonObject root = QJsonDocument::fromJson(existingRootData).object();
    QJsonArray mergedGames;
    const QString appId = gameSummary.value(QStringLiteral("appId")).toString();

    const QJsonArray oldGames = root.value(QStringLiteral("games")).toArray();
    for (const QJsonValue &value : oldGames) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("appId")).toString() == appId) {
            continue;
        }
        mergedGames.append(item);
    }
    mergedGames.append(gameSummary);

    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("remoteRootPath"), m_webDavConfig.remoteRootPath);
    root.insert(QStringLiteral("updatedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("games"), mergedGames);
    return root;
}

QJsonObject SteamManager::cloudGameSummaryFromManifest(const QJsonObject &gameManifest) const
{
    QJsonObject summary;
    summary.insert(QStringLiteral("appId"), gameManifest.value(QStringLiteral("appId")).toString());
    summary.insert(QStringLiteral("gameName"), gameManifest.value(QStringLiteral("gameName")).toString());
    summary.insert(QStringLiteral("remoteDirectory"), gameManifest.value(QStringLiteral("remoteDirectory")).toString());
    summary.insert(QStringLiteral("snapshotCount"), gameManifest.value(QStringLiteral("snapshotCount")).toInt());
    summary.insert(QStringLiteral("latestSnapshotFileName"), gameManifest.value(QStringLiteral("latestSnapshotFileName")).toString());
    summary.insert(QStringLiteral("latestRemotePath"), gameManifest.value(QStringLiteral("latestRemotePath")).toString());
    summary.insert(QStringLiteral("updatedAtUtc"), gameManifest.value(QStringLiteral("updatedAtUtc")).toString());
    return summary;
}

QVariantList SteamManager::cloudSnapshotRecordsFromLocalManifest(const GameInfo &game) const
{
    /*
     * 本地 manifest 会保留所有历史快照记录，但并不是每条都已经上传。
     * 阶段 8 只把 remotePath 存在，或 uploadState 表示云端已确认的记录写入云端索引。
     */
    QVariantList records;
    const QVariantList localRecords = m_snapshotPreprocessor.allSnapshotRecordsForGame(game);
    for (const QVariant &item : localRecords) {
        QVariantMap record = item.toMap();
        const QString fileName = record.value(QStringLiteral("fileName")).toString();
        QString remotePath = record.value(QStringLiteral("remotePath")).toString();
        const QString uploadState = record.value(QStringLiteral("uploadState")).toString();

        if (fileName.trimmed().isEmpty()) {
            continue;
        }

        if (remotePath.trimmed().isEmpty()
            && (uploadState == QStringLiteral("uploaded")
                || uploadState == QStringLiteral("remote_exists")
                || uploadState == QStringLiteral("cloud_manifest"))) {
            remotePath = cloudDirectoryForGame(game) + QLatin1Char('/') + fileName;
        }

        if (remotePath.trimmed().isEmpty()) {
            continue;
        }

        record.insert(QStringLiteral("remotePath"), remotePath);
        if (uploadState.trimmed().isEmpty()) {
            record.insert(QStringLiteral("uploadState"), QStringLiteral("uploaded"));
        }
        records.append(record);
    }

    return records;
}

QVariantList SteamManager::cloudSnapshotRecordsFromExistingLocalSnapshots(const GameInfo &game) const
{
    /*
     * 写云端 snapshot-manifest.json 时，只能使用本地磁盘真实存在的 zip。
     * 本地历史 manifest 可能还保存“曾经上传过”的旧记录，但用户可能已经在云盘
     * 或本地删除了对应文件；这些旧记录不能继续被写回云端索引。
     */
    QVariantList records;
    const QVariantList localSnapshots = m_snapshotPreprocessor.snapshotsForGame(game);
    for (const QVariant &item : localSnapshots) {
        QVariantMap record = item.toMap();
        const QString fileName = record.value(QStringLiteral("fileName")).toString();
        QString remotePath = record.value(QStringLiteral("remotePath")).toString();
        const QString uploadState = record.value(QStringLiteral("uploadState")).toString();

        if (fileName.trimmed().isEmpty()) {
            continue;
        }

        const QFileInfo zipFile(record.value(QStringLiteral("zipPath")).toString());
        if (!zipFile.exists() || !zipFile.isFile()) {
            continue;
        }

        if (remotePath.trimmed().isEmpty()
            && (uploadState == QStringLiteral("uploaded")
                || uploadState == QStringLiteral("remote_exists")
                || uploadState == QStringLiteral("cloud_manifest"))) {
            remotePath = cloudDirectoryForGame(game) + QLatin1Char('/') + fileName;
        }

        if (remotePath.trimmed().isEmpty()) {
            continue;
        }

        record.insert(QStringLiteral("remotePath"), remotePath);
        records.append(record);
    }

    return records;
}

QVariantList SteamManager::cloudSnapshotRecordsFromGameManifest(const QJsonObject &gameManifest) const
{
    QVariantList records;
    const int manifestCount = gameManifest.value(QStringLiteral("snapshotCount")).toInt();
    const QJsonArray snapshots = gameManifest.value(QStringLiteral("snapshots")).toArray();
    for (const QJsonValue &value : snapshots) {
        QVariantMap record = value.toObject().toVariantMap();
        if (record.value(QStringLiteral("fileName")).toString().trimmed().isEmpty()
            || record.value(QStringLiteral("remotePath")).toString().trimmed().isEmpty()) {
            continue;
        }
        if (records.isEmpty() && manifestCount > 0) {
            record.insert(QStringLiteral("cloudSnapshotCount"), manifestCount);
        }
        records.append(record);
    }
    return records;
}

QVariantList SteamManager::downloadableSnapshotRecordsForGame(const GameInfo &game) const
{
    if (m_cloudSnapshotRecordsByAppId.contains(game.appId)) {
        return m_cloudSnapshotRecordsByAppId.value(game.appId);
    }

    return cloudSnapshotRecordsFromLocalManifest(game);
}

bool SteamManager::uploadSnapshotRecordsForGame(const GameInfo &game, const QVariantList &snapshots)
{
    if (!game.isValid() || snapshots.isEmpty()) {
        return false;
    }

    const QString remoteDirectory = cloudDirectoryForGame(game);
    QVariantList pendingUploads;
    QSet<QString> seenLocalPaths;
    int invalidCount = 0;
    int alreadyUploadingCount = 0;

    for (const QVariant &item : snapshots) {
        const QVariantMap snapshot = item.toMap();
        const QString zipPath = snapshot.value(QStringLiteral("zipPath")).toString();
        const QString fileName = snapshot.value(QStringLiteral("fileName")).toString();
        if (zipPath.trimmed().isEmpty() || seenLocalPaths.contains(zipPath)) {
            ++invalidCount;
            continue;
        }
        seenLocalPaths.insert(zipPath);

        const QFileInfo zipInfo(zipPath);
        if (!zipInfo.exists() || !zipInfo.isFile()) {
            ++invalidCount;
            continue;
        }

        if (m_pendingSnapshotUploadAppIds.contains(zipPath)) {
            ++alreadyUploadingCount;
            continue;
        }

        QVariantMap pending;
        pending.insert(QStringLiteral("zipPath"), zipInfo.absoluteFilePath());
        pending.insert(QStringLiteral("fileName"), fileName.trimmed().isEmpty() ? zipInfo.fileName() : fileName);
        pendingUploads.append(pending);
    }

    if (invalidCount > 0) {
        m_logger.warning(QStringLiteral("快照上传检查：%1 有 %2 条本地快照记录无效或文件已不存在，已按磁盘实际文件跳过")
                             .arg(game.displayName)
                             .arg(invalidCount));
    }
    if (alreadyUploadingCount > 0) {
        m_logger.info(QStringLiteral("快照上传检查：%1 有 %2 个快照已在上传检查队列中，已跳过重复请求")
                          .arg(game.displayName)
                          .arg(alreadyUploadingCount));
    }

    const int uploadCheckCount = pendingUploads.count();
    if (uploadCheckCount <= 0) {
        refreshSnapshotRecordsForGame(
            game.appId,
            alreadyUploadingCount > 0
                ? QStringLiteral("本地快照正在上传检查中")
                : QStringLiteral("没有可上传的本地快照文件"),
            alreadyUploadingCount > 0
                ? QStringLiteral("已有 %1 个本地快照正在进行云端存在性检查，请等待完成")
                      .arg(alreadyUploadingCount)
                : QStringLiteral("本地快照列表中没有找到实际存在的 zip 文件，请先重新检查或创建快照"),
            false);
        return alreadyUploadingCount > 0;
    }

    m_pendingSnapshotUploadRemainingByAppId[game.appId] += uploadCheckCount;
    refreshSnapshotRecordsForGame(
        game.appId,
        QStringLiteral("正在上传并补齐云端缺失快照"),
        QStringLiteral("本地共有 %1 个可用快照，将逐个检查云端是否存在；云端缺失的会重新上传，已存在的会跳过")
            .arg(uploadCheckCount),
        false);

    for (const QVariant &item : pendingUploads) {
        const QVariantMap pending = item.toMap();
        const QString zipPath = pending.value(QStringLiteral("zipPath")).toString();
        const QString fileName = pending.value(QStringLiteral("fileName")).toString();

        m_pendingSnapshotUploadAppIds.insert(zipPath, game.appId);
        m_pendingSnapshotUploadFileNames.insert(zipPath, fileName);
        m_pendingBatchSnapshotUploadPaths.insert(zipPath);
        m_logger.info(QStringLiteral("开始上传检查本地快照：%1，本地文件：%2，云端目录：%3")
                          .arg(game.displayName, QDir::toNativeSeparators(zipPath), remoteDirectory));
        if (remoteDirectory.startsWith(QStringLiteral("/Quark"), Qt::CaseInsensitive)) {
            m_quarkGatewayManager.uploadFileIfMissing(zipPath, remoteDirectory);
        } else {
            m_webDavClient.uploadFileIfMissing(m_webDavConfig, zipPath, remoteDirectory);
        }
    }

    return true;
}

bool SteamManager::uploadAutoSyncSnapshotForGame(const GameInfo &game, const QVariantMap &snapshotResult)
{
    if (!game.isValid()) {
        return false;
    }

    const QString zipPath = snapshotResult.value(QStringLiteral("snapshotPath")).toString().trimmed();
    QString fileName = snapshotResult.value(QStringLiteral("snapshotFileName")).toString().trimmed();
    if (zipPath.isEmpty()) {
        updateSyncStatusForGame(game.appId, QStringLiteral("自动同步失败：快照已创建，但没有获得 zip 文件路径"));
        m_logger.warning(QStringLiteral("自动同步上传失败：%1 的快照创建结果没有返回 zip 文件路径").arg(game.displayName));
        return false;
    }

    const QFileInfo zipInfo(zipPath);
    if (!zipInfo.exists() || !zipInfo.isFile()) {
        updateSyncStatusForGame(game.appId, QStringLiteral("自动同步失败：快照文件不存在，未上传云端"));
        m_logger.warning(QStringLiteral("自动同步上传失败：%1 的快照文件不存在，路径：%2")
                             .arg(game.displayName, QDir::toNativeSeparators(zipPath)));
        refreshLocalSnapshotsForGame(game.appId);
        return false;
    }

    if (!m_webDavConfig.isValid()) {
        updateSyncStatusForGame(game.appId, QStringLiteral("自动同步失败：已保留本地快照，云端未连接，可稍后手动上传"));
        refreshSnapshotRecordsForGame(
            game.appId,
            QStringLiteral("自动同步已创建本地快照，但云端未连接"),
            QStringLiteral("快照已经保存到本地目录，但夸克/OpenList 网关或 WebDAV 配置尚未就绪，因此没有上传云端"),
            false);
        m_logger.warning(QStringLiteral("自动同步上传跳过：%1 已创建本地快照，但云端未连接或 WebDAV 配置不完整")
                             .arg(game.displayName));
        emit userAlertRequested(
            QStringLiteral("自动同步未上传云端"),
            QStringLiteral("%1 的本地快照已创建，但云端尚未连接。").arg(game.displayName),
            QStringLiteral("请进入“云同步设置”确认夸克 Cookie/OpenList 网关连接成功后，再手动上传该快照，或等待下一次自动同步。"),
            false);
        return false;
    }

    const QString uploadPath = zipInfo.absoluteFilePath();
    if (fileName.isEmpty()) {
        fileName = zipInfo.fileName();
    }

    if (m_pendingSnapshotUploadAppIds.contains(uploadPath)) {
        updateSyncStatusForGame(game.appId, QStringLiteral("自动同步上传已在队列中"));
        m_logger.info(QStringLiteral("自动同步上传跳过重复请求：%1，快照：%2")
                          .arg(game.displayName, QDir::toNativeSeparators(uploadPath)));
        return true;
    }

    const QString remoteDirectory = cloudDirectoryForGame(game);
    m_pendingSnapshotUploadAppIds.insert(uploadPath, game.appId);
    m_pendingSnapshotUploadFileNames.insert(uploadPath, fileName);
    m_pendingAutoSyncUploadPaths.insert(uploadPath);

    refreshSnapshotRecordsForGame(
        game.appId,
        QStringLiteral("自动同步正在上传新快照"),
        QStringLiteral("本次游戏关闭后创建的新快照正在上传云端；如果云端已有同名文件，会自动跳过"),
        false);
    m_logger.info(QStringLiteral("自动同步开始上传新快照：%1，本地文件：%2，云端目录：%3")
                      .arg(game.displayName, QDir::toNativeSeparators(uploadPath), remoteDirectory));

    if (remoteDirectory.startsWith(QStringLiteral("/Quark"), Qt::CaseInsensitive)) {
        m_quarkGatewayManager.uploadFileIfMissing(uploadPath, remoteDirectory);
    } else {
        m_webDavClient.uploadFileIfMissing(m_webDavConfig, uploadPath, remoteDirectory);
    }
    return true;
}

bool SteamManager::downloadSnapshotRecordsForGame(const GameInfo &game, const QVariantList &snapshots)
{
    if (!game.isValid() || snapshots.isEmpty()) {
        return false;
    }

    QVariantList pendingDownloads;
    QSet<QString> seenFileNames;
    int validRecordCount = 0;
    int skippedExistingCount = 0;
    int importedExistingCount = 0;
    int alreadyDownloadingCount = 0;

    for (const QVariant &item : snapshots) {
        QVariantMap snapshot = item.toMap();
        const QString fileName = snapshot.value(QStringLiteral("fileName")).toString().trimmed();
        if (fileName.isEmpty() || seenFileNames.contains(fileName)) {
            continue;
        }
        seenFileNames.insert(fileName);

        QString remotePath = snapshot.value(QStringLiteral("remotePath")).toString().trimmed();
        if (remotePath.isEmpty()) {
            remotePath = cloudDirectoryForGame(game) + QLatin1Char('/') + fileName;
        }
        if (remotePath.isEmpty()) {
            continue;
        }

        const QString localPath = localDownloadPathForSnapshot(game, fileName);
        if (localPath.trimmed().isEmpty()) {
            m_logger.warning(QStringLiteral("快照下载跳过：%1 的快照 %2 无法生成本地保存路径")
                                 .arg(game.displayName, fileName));
            continue;
        }

        ++validRecordCount;
        snapshot.insert(QStringLiteral("remotePath"), remotePath);
        if (QFileInfo::exists(localPath)) {
            ++skippedExistingCount;
            if (m_snapshotPreprocessor.importCloudSnapshotRecord(game, snapshot, localPath)) {
                ++importedExistingCount;
            }
            continue;
        }
        if (m_pendingSnapshotDownloadAppIds.contains(localPath)) {
            ++alreadyDownloadingCount;
            continue;
        }

        QVariantMap pending;
        pending.insert(QStringLiteral("fileName"), fileName);
        pending.insert(QStringLiteral("remotePath"), remotePath);
        pending.insert(QStringLiteral("localPath"), localPath);
        pending.insert(QStringLiteral("record"), snapshot);
        pendingDownloads.append(pending);
    }

    if (validRecordCount <= 0) {
        return false;
    }

    if (skippedExistingCount > 0) {
        m_logger.info(QStringLiteral("快照下载已跳过：%1 有 %2 个云端快照本地已存在同名文件，其中 %3 个已刷新到本地索引")
                          .arg(game.displayName)
                          .arg(skippedExistingCount)
                          .arg(importedExistingCount));
    }
    if (alreadyDownloadingCount > 0) {
        m_logger.info(QStringLiteral("快照下载已跳过重复请求：%1 有 %2 个云端快照正在下载中")
                          .arg(game.displayName)
                          .arg(alreadyDownloadingCount));
    }

    const int downloadCount = pendingDownloads.count();
    if (downloadCount <= 0) {
        refreshSnapshotRecordsForGame(
            game.appId,
            alreadyDownloadingCount > 0
                ? QStringLiteral("缺失的云端快照正在下载中")
                : QStringLiteral("云端快照已全部存在本地，无需下载"),
            alreadyDownloadingCount > 0
                ? QStringLiteral("云端索引中共有 %1 个有效快照，其中 %2 个文件已经在下载队列中，请等待下载完成")
                      .arg(validRecordCount)
                      .arg(alreadyDownloadingCount)
                : QStringLiteral("云端索引中共有 %1 个有效快照，本地快照目录已存在全部同名 zip 文件；软件已刷新本地快照列表")
                      .arg(validRecordCount),
            false);
        return true;
    }

    m_pendingSnapshotDownloadRemainingByAppId[game.appId] += downloadCount;
    refreshSnapshotRecordsForGame(
        game.appId,
        QStringLiteral("正在下载缺失的云端快照"),
        QStringLiteral("云端索引中共有 %1 个有效快照，本地已存在 %2 个，正在下载中 %3 个，将下载缺失的 %4 个")
            .arg(validRecordCount)
            .arg(skippedExistingCount)
            .arg(alreadyDownloadingCount)
            .arg(downloadCount),
        false);

    for (const QVariant &item : pendingDownloads) {
        const QVariantMap pending = item.toMap();
        const QString fileName = pending.value(QStringLiteral("fileName")).toString();
        const QString remotePath = pending.value(QStringLiteral("remotePath")).toString();
        const QString localPath = pending.value(QStringLiteral("localPath")).toString();
        const QVariantMap record = pending.value(QStringLiteral("record")).toMap();

        m_pendingSnapshotDownloadAppIds.insert(localPath, game.appId);
        m_pendingSnapshotDownloadRecords.insert(localPath, record);
        m_logger.info(QStringLiteral("开始下载云端快照：%1，文件：%2，云端路径：%3，本地路径：%4")
                          .arg(game.displayName, fileName, remotePath, QDir::toNativeSeparators(localPath)));
        if (remotePath.startsWith(QStringLiteral("/Quark"), Qt::CaseInsensitive)) {
            m_quarkGatewayManager.downloadFile(remotePath, localPath);
        } else {
            m_webDavClient.downloadFile(m_webDavConfig, remotePath, localPath);
        }
    }

    return true;
}

void SteamManager::applyCloudRootManifest(const QByteArray &data)
{
    const QJsonObject root = QJsonDocument::fromJson(data).object();
    const QJsonArray games = root.value(QStringLiteral("games")).toArray();

    m_cloudSnapshotRecordsByAppId.clear();
    m_cloudDirectoryByAppId.clear();
    for (const QJsonValue &value : games) {
        const QJsonObject item = value.toObject();
        const QString appId = item.value(QStringLiteral("appId")).toString();
        const QString fileName = item.value(QStringLiteral("latestSnapshotFileName")).toString();
        const QString remotePath = item.value(QStringLiteral("latestRemotePath")).toString();
        QString remoteDirectory = item.value(QStringLiteral("remoteDirectory")).toString().trimmed();
        const int count = item.value(QStringLiteral("snapshotCount")).toInt();
        if (appId.trimmed().isEmpty()) {
            continue;
        }

        if (remoteDirectory.isEmpty()) {
            const int slashIndex = remotePath.lastIndexOf(QLatin1Char('/'));
            if (slashIndex > 0) {
                remoteDirectory = remotePath.left(slashIndex);
            }
        }
        remoteDirectory.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (!remoteDirectory.isEmpty() && !remoteDirectory.startsWith(QLatin1Char('/'))) {
            remoteDirectory.prepend(QLatin1Char('/'));
        }
        while (remoteDirectory.length() > 1 && remoteDirectory.endsWith(QLatin1Char('/'))) {
            remoteDirectory.chop(1);
        }
        if (!remoteDirectory.isEmpty()) {
            m_cloudDirectoryByAppId.insert(appId, remoteDirectory);
        }

        QVariantList records;
        if (!fileName.trimmed().isEmpty() && !remotePath.trimmed().isEmpty()) {
            QVariantMap record;
            record.insert(QStringLiteral("fileName"), fileName);
            record.insert(QStringLiteral("remotePath"), remotePath);
            record.insert(QStringLiteral("uploadState"), QStringLiteral("cloud_manifest"));
            record.insert(QStringLiteral("cloudSnapshotCount"), count);
            records.append(record);
        }
        m_cloudSnapshotRecordsByAppId.insert(appId, records);
    }

    m_cloudManifestLoaded = true;
    applyCloudSnapshotRecordsToModel();
}

void SteamManager::applyCloudSnapshotRecordsToModel()
{
    for (const GameInfo &game : m_gameModel.games()) {
        const QVariantList records = m_cloudSnapshotRecordsByAppId.value(game.appId);
        if (!records.isEmpty()) {
            updateCloudSnapshotStatusForGame(
                game.appId,
                records,
                QStringLiteral("云端索引已读取"),
                QStringLiteral("已从 cloud-manifest.json 读取该游戏的云端快照记录"));
            continue;
        }

        if (m_cloudManifestLoaded) {
            updateCloudSnapshotStatusForGame(
                game.appId,
                {},
                QStringLiteral("云端暂无快照记录"),
                QStringLiteral("cloud-manifest.json 中暂未记录该游戏的已上传快照"));
        }
    }
}

void SteamManager::updateCloudSnapshotStatusForGame(
    const QString &appId,
    const QVariantList &records,
    const QString &status,
    const QString &detail)
{
    const QVariantMap latest = records.isEmpty() ? QVariantMap() : records.first().toMap();
    const int countFromManifest = latest.value(QStringLiteral("cloudSnapshotCount")).toInt();
    const int count = countFromManifest > records.count() ? countFromManifest : records.count();

    if (m_gameModel.updateCloudSnapshotStatus(
            appId,
            status,
            detail,
            latest.value(QStringLiteral("fileName")).toString(),
            latest.value(QStringLiteral("remotePath")).toString(),
            count)) {
        persistManualGameIfPresent(appId);
        rebuildInstalledGamesFromModel();
    }
}

QString SteamManager::cloudRootPath() const
{
    QString rootPath = m_webDavConfig.remoteRootPath.trimmed();
    rootPath.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (rootPath.isEmpty()) {
        rootPath = QStringLiteral("/Quark/GameSaveCloudQt");
    }
    if (!rootPath.startsWith(QLatin1Char('/'))) {
        rootPath.prepend(QLatin1Char('/'));
    }
    while (rootPath.length() > 1 && rootPath.endsWith(QLatin1Char('/'))) {
        rootPath.chop(1);
    }

    return rootPath;
}

QString SteamManager::cloudRootManifestPath() const
{
    return cloudRootPath() + QStringLiteral("/cloud-manifest.json");
}

QString SteamManager::restoreBackupRootPath() const
{
    return QDir(storageRootFromSnapshotRoot(snapshotRootPath())).absoluteFilePath(QStringLiteral("恢复前备份"));
}

QString SteamManager::logDirectoryForSnapshotRoot(const QString &snapshotRootPath) const
{
    return logDirectoryForStorageRoot(storageRootFromSnapshotRoot(snapshotRootPath));
}

QString SteamManager::defaultStorageRootPath() const
{
    const QString dDriveRoot = QStringLiteral("D:/");
    const QString preferredRoot = QDir(dDriveRoot).exists()
                                      ? QDir(dDriveRoot).absoluteFilePath(QStringLiteral("游戏存档"))
                                      : QStringLiteral("C:/游戏存档");
    return QDir::toNativeSeparators(QDir::cleanPath(preferredRoot));
}

QString SteamManager::storageRootFromSnapshotRoot(const QString &snapshotRootPath) const
{
    const QString cleanPath = QDir::cleanPath(snapshotRootPath.trimmed());
    if (cleanPath.isEmpty() || cleanPath == QStringLiteral(".")) {
        return {};
    }

    const QFileInfo rootInfo(cleanPath);
    const QString folderName = rootInfo.fileName();
    if (folderName == QStringLiteral("存档") || folderName == QStringLiteral("日志")) {
        return QDir::toNativeSeparators(rootInfo.absolutePath());
    }

    return QDir::toNativeSeparators(cleanPath);
}

QString SteamManager::snapshotDirectoryForStorageRoot(const QString &storageRootPath) const
{
    const QString cleanPath = QDir::cleanPath(storageRootPath.trimmed());
    if (cleanPath.isEmpty() || cleanPath == QStringLiteral(".")) {
        return {};
    }

    return QDir::toNativeSeparators(QDir(cleanPath).absoluteFilePath(QStringLiteral("存档")));
}

QString SteamManager::logDirectoryForStorageRoot(const QString &storageRootPath) const
{
    const QString cleanPath = QDir::cleanPath(storageRootPath.trimmed());
    if (cleanPath.isEmpty() || cleanPath == QStringLiteral(".")) {
        return {};
    }

    return QDir::toNativeSeparators(QDir(cleanPath).absoluteFilePath(QStringLiteral("日志")));
}

bool SteamManager::ensureDefaultStorageRootInitialized()
{
    if (!snapshotRootPath().trimmed().isEmpty()) {
        return true;
    }

    const QString storageRootPath = defaultStorageRootPath();
    const QString snapshotRootPath = snapshotDirectoryForStorageRoot(storageRootPath);
    const QString logDirectoryPath = logDirectoryForStorageRoot(storageRootPath);

    QDir storageRootDir(storageRootPath);
    if (!storageRootDir.exists() && !storageRootDir.mkpath(QStringLiteral("."))) {
        return false;
    }

    if (!QDir().mkpath(snapshotRootPath) || !QDir().mkpath(logDirectoryPath)) {
        return false;
    }

    if (!m_snapshotPreprocessor.setSnapshotRootPath(snapshotRootPath)) {
        return false;
    }

    if (!m_logger.setLogDirectoryPath(logDirectoryPath)) {
        return false;
    }

    emit snapshotRootPathChanged();
    emit logDirectoryChanged();
    emit logFilePathChanged();
    m_logger.info(QStringLiteral("首次运行已自动创建游戏存档目录：%1，快照目录：%2，日志目录：%3")
                      .arg(storageRootPath, snapshotRootPath, logDirectoryPath));
    return true;
}

void SteamManager::alignLogDirectoryWithSnapshotRoot()
{
    const QString expectedLogDirectory = logDirectoryForSnapshotRoot(snapshotRootPath());
    if (expectedLogDirectory.isEmpty() || m_logger.logDirectory() == expectedLogDirectory) {
        return;
    }

    const QString previousLogDirectory = m_logger.logDirectory();
    if (!m_logger.setLogDirectoryPath(expectedLogDirectory)) {
        return;
    }

    if (previousLogDirectory != m_logger.logDirectory()) {
        emit logDirectoryChanged();
        emit logFilePathChanged();
    }

    m_logger.info(QStringLiteral("日志目录已自动放入游戏存档目录内：%1").arg(m_logger.logDirectory()));
}

bool SteamManager::migrateDirectoryContents(
    const QString &sourcePath,
    const QString &targetPath,
    const QStringList &excludedPaths)
{
    const QString cleanSourcePath = QDir::cleanPath(sourcePath.trimmed());
    const QString cleanTargetPath = QDir::cleanPath(targetPath.trimmed());

    if (cleanSourcePath.isEmpty() || cleanTargetPath.isEmpty() || pathsEqual(cleanSourcePath, cleanTargetPath)) {
        return true;
    }

    QDir sourceDir(cleanSourcePath);
    if (!sourceDir.exists()) {
        return QDir().mkpath(cleanTargetPath);
    }

    const QFileInfo targetInfo(cleanTargetPath);
    if (!QDir().mkpath(targetInfo.absolutePath())) {
        return false;
    }

    if (!pathIsInside(cleanTargetPath, cleanSourcePath) && QDir().rename(cleanSourcePath, cleanTargetPath)) {
        return true;
    }

    if (pathIsInside(cleanTargetPath, cleanSourcePath)) {
        QStringList exclusions = excludedPaths;
        exclusions.append(cleanTargetPath);
        return moveDirectoryChildren(cleanSourcePath, cleanTargetPath, exclusions);
    }

    if (!copyDirectoryContents(cleanSourcePath, cleanTargetPath, excludedPaths)) {
        return false;
    }

    return sourceDir.removeRecursively();
}

bool SteamManager::copyDirectoryContents(
    const QString &sourcePath,
    const QString &targetPath,
    const QStringList &excludedPaths) const
{
    QDir sourceDir(sourcePath);
    if (!sourceDir.exists()) {
        return true;
    }

    if (!QDir().mkpath(targetPath)) {
        return false;
    }

    QDirIterator iterator(
        sourcePath,
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);

    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo sourceInfo(iterator.filePath());
        if (pathsEqual(sourceInfo.absoluteFilePath(), targetPath)
            || pathIsInside(sourceInfo.absoluteFilePath(), targetPath)) {
            continue;
        }

        bool excluded = false;
        for (const QString &excludedPath : excludedPaths) {
            if (pathsEqual(sourceInfo.absoluteFilePath(), excludedPath)
                || pathIsInside(sourceInfo.absoluteFilePath(), excludedPath)) {
                excluded = true;
                break;
            }
        }

        if (excluded) {
            continue;
        }

        const QString relativePath = sourceDir.relativeFilePath(sourceInfo.absoluteFilePath());
        QString targetItemPath = QDir(targetPath).absoluteFilePath(relativePath);

        if (sourceInfo.isDir()) {
            if (!QDir().mkpath(targetItemPath)) {
                return false;
            }
            continue;
        }

        const QFileInfo targetInfo(targetItemPath);
        if (!QDir().mkpath(targetInfo.absolutePath())) {
            return false;
        }

        if (QFile::exists(targetItemPath)) {
            const QString suffix = targetInfo.suffix().isEmpty()
                                       ? QString()
                                       : QStringLiteral(".%1").arg(targetInfo.suffix());
            const QString basePath = QDir(targetInfo.absolutePath()).absoluteFilePath(targetInfo.completeBaseName());
            int copyIndex = 1;
            do {
                targetItemPath = QStringLiteral("%1_migrated_%2%3").arg(basePath).arg(copyIndex).arg(suffix);
                ++copyIndex;
            } while (QFile::exists(targetItemPath));
        }

        if (!QFile::copy(sourceInfo.absoluteFilePath(), targetItemPath)) {
            return false;
        }
    }

    return true;
}

bool SteamManager::moveDirectoryChildren(
    const QString &sourcePath,
    const QString &targetPath,
    const QStringList &excludedPaths) const
{
    QDir sourceDir(sourcePath);
    if (!sourceDir.exists()) {
        return true;
    }

    if (!QDir().mkpath(targetPath)) {
        return false;
    }

    const QFileInfoList entries = sourceDir.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
        QDir::Name);

    for (const QFileInfo &entryInfo : entries) {
        bool excluded = false;
        for (const QString &excludedPath : excludedPaths) {
            if (pathsEqual(entryInfo.absoluteFilePath(), excludedPath)
                || pathIsInside(entryInfo.absoluteFilePath(), excludedPath)) {
                excluded = true;
                break;
            }
        }

        if (excluded) {
            continue;
        }

        QString targetItemPath = QDir(targetPath).absoluteFilePath(entryInfo.fileName());
        if (QFile::exists(targetItemPath)) {
            const QFileInfo targetInfo(targetItemPath);
            const QString suffix = targetInfo.suffix().isEmpty()
                                       ? QString()
                                       : QStringLiteral(".%1").arg(targetInfo.suffix());
            const QString basePath = QDir(targetInfo.absolutePath()).absoluteFilePath(targetInfo.completeBaseName());
            int moveIndex = 1;
            do {
                targetItemPath = QStringLiteral("%1_migrated_%2%3").arg(basePath).arg(moveIndex).arg(suffix);
                ++moveIndex;
            } while (QFile::exists(targetItemPath));
        }

        if (QDir().rename(entryInfo.absoluteFilePath(), targetItemPath)) {
            continue;
        }

        if (entryInfo.isDir()) {
            if (!copyDirectoryContents(entryInfo.absoluteFilePath(), targetItemPath)) {
                return false;
            }
            QDir copiedSourceDir(entryInfo.absoluteFilePath());
            if (!copiedSourceDir.removeRecursively()) {
                return false;
            }
            continue;
        }

        if (!QFile::copy(entryInfo.absoluteFilePath(), targetItemPath)) {
            return false;
        }

        if (!QFile::remove(entryInfo.absoluteFilePath())) {
            return false;
        }
    }

    return true;
}

bool SteamManager::pathsEqual(const QString &leftPath, const QString &rightPath) const
{
    const QString left = QDir::cleanPath(QDir::fromNativeSeparators(leftPath.trimmed())).toLower();
    const QString right = QDir::cleanPath(QDir::fromNativeSeparators(rightPath.trimmed())).toLower();
    return left == right;
}

bool SteamManager::pathIsInside(const QString &childPath, const QString &parentPath) const
{
    QString child = QDir::cleanPath(QDir::fromNativeSeparators(childPath.trimmed())).toLower();
    QString parent = QDir::cleanPath(QDir::fromNativeSeparators(parentPath.trimmed())).toLower();

    if (child.isEmpty() || parent.isEmpty() || child == parent) {
        return false;
    }

    if (!parent.endsWith(QLatin1Char('/'))) {
        parent += QLatin1Char('/');
    }

    return child.startsWith(parent);
}

QString SteamManager::normalizedExistingSteamPath(const QString &path) const
{
    if (path.trimmed().isEmpty()) {
        return {};
    }

    const QString cleanPath = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
    const QDir steamDir(cleanPath);

    if (!steamDir.exists()) {
        return {};
    }

    if (!steamDir.exists(QStringLiteral("steamapps"))) {
        return {};
    }

    return steamDir.absolutePath();
}

bool SteamManager::isNumericAppId(const QString &appId) const
{
    static const QRegularExpression numericRegex(QStringLiteral(R"(^\d+$)"));
    return numericRegex.match(appId.trimmed()).hasMatch();
}

WebDavConfig SteamManager::webDavConfigFromInput(
    const QString &serverUrl,
    const QString &username,
    const QString &password,
    const QString &remoteRootPath) const
{
    WebDavConfig config;
    config.serverUrl = serverUrl.trimmed();
    config.username = username.trimmed();
    config.password = password;
    config.remoteRootPath = remoteRootPath.trimmed().isEmpty()
                                ? QStringLiteral("/GameSaveCloudQt")
                                : remoteRootPath.trimmed();
    return config;
}

void SteamManager::setWebDavConnectionStatus(const QString &status)
{
    if (m_webDavConnectionStatus == status) {
        return;
    }

    m_webDavConnectionStatus = status;
    emit webDavConnectionStatusChanged();
}

void SteamManager::setWebDavTesting(bool testing)
{
    if (m_webDavTesting == testing) {
        return;
    }

    m_webDavTesting = testing;
    emit webDavTestingChanged();
}

QString SteamManager::cloudDirectoryForGame(const GameInfo &game) const
{
    const QString rootPath = cloudRootPath();
    const QString cachedDirectory = m_cloudDirectoryByAppId.value(game.appId).trimmed();
    if (!cachedDirectory.isEmpty()
        && (cachedDirectory == rootPath
            || cachedDirectory.startsWith(rootPath + QLatin1Char('/'), Qt::CaseInsensitive))) {
        return cachedDirectory;
    }

    QString name = game.appId.trimmed();
    if (name.isEmpty()) {
        name = game.displayName.isEmpty() ? game.name : game.displayName;
    }

    name.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*])")), QStringLiteral("_"));
    name.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral("_"));
    name = name.trimmed().left(80);
    if (name.isEmpty()) {
        name = QStringLiteral("Unknown_Game");
    }

    return QStringLiteral("%1/apps/%2").arg(rootPath, name);
}

QString SteamManager::localDownloadPathForSnapshot(const GameInfo &game, const QString &fileName) const
{
    QString directoryPath = m_snapshotPreprocessor.snapshotDirectoryForGame(game);
    if (directoryPath.trimmed().isEmpty()) {
        directoryPath = game.snapshotDirectory;
    }

    if (directoryPath.trimmed().isEmpty() || fileName.trimmed().isEmpty()) {
        return {};
    }

    QDir dir(directoryPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return {};
    }

    return QDir::toNativeSeparators(dir.absoluteFilePath(fileName));
}

void SteamManager::loadQuarkGatewaySettings()
{
    const std::unique_ptr<QSettings> settings = AppSettings::create();
    m_quarkCookie = settings->value(QStringLiteral("quark/cookie")).toString();
    m_quarkGatewayStatus = m_quarkCookie.trimmed().isEmpty()
                               ? QStringLiteral("未配置夸克 Cookie")
                               : QStringLiteral("已保存夸克 Cookie，等待本机网关连接");
}

void SteamManager::loadAutoSyncSettings()
{
    const std::unique_ptr<QSettings> settings = AppSettings::create();
    m_autoSyncEnabled = settings->value(QStringLiteral("autoSync/enabled"), false).toBool();
}

QString SteamManager::autoSyncGameSettingsKey(const QString &appId) const
{
    const QByteArray encodedAppId = QUrl::toPercentEncoding(appId.trimmed());
    return QStringLiteral("autoSync/games/%1/enabled").arg(QString::fromLatin1(encodedAppId));
}

void SteamManager::updateSyncStatusForGame(const QString &appId, const QString &status)
{
    if (m_gameModel.updateSyncStatus(appId, status)) {
        persistManualGameIfPresent(appId);
        rebuildInstalledGamesFromModel();
    }
}

void SteamManager::handleGameClosedForAutoSync(const QString &appId, const QString &processName)
{
    Q_UNUSED(processName)

    const GameInfo game = gameByAppId(appId);
    if (!game.isValid()) {
        return;
    }

    if (!m_autoSyncEnabled) {
        updateSyncStatusForGame(appId, QStringLiteral("自动同步已关闭"));
        return;
    }

    if (!autoSyncEnabledForGame(appId)) {
        updateSyncStatusForGame(appId, QStringLiteral("自动同步已对该游戏关闭"));
        return;
    }

    if (!game.savePathCanSync || game.savePath.trimmed().isEmpty()) {
        updateSyncStatusForGame(appId, QStringLiteral("自动同步跳过：当前没有可同步的本地存档目录"));
        m_logger.warning(QStringLiteral("自动同步跳过：%1 当前没有可同步的本地存档目录").arg(game.displayName));
        return;
    }

    m_logger.info(QStringLiteral("自动同步开始：检测到游戏关闭，开始检查本地存档变化，游戏：%1").arg(game.displayName));
    updateSyncStatusForGame(appId, QStringLiteral("自动同步检查中"));

    const QVariantMap analysis = m_snapshotPreprocessor.analyzeGame(game);
    applySnapshotResult(appId, analysis);

    if (!analysis.value(QStringLiteral("success")).toBool()) {
        const QString detail = analysis.value(QStringLiteral("detail")).toString();
        updateSyncStatusForGame(appId, QStringLiteral("自动同步检查失败"));
        m_logger.warning(QStringLiteral("自动同步检查失败：%1，原因：%2").arg(game.displayName, detail));
        return;
    }

    if (!analysis.value(QStringLiteral("needsSnapshot")).toBool()) {
        updateSyncStatusForGame(appId, QStringLiteral("自动同步完成：存档无变化，未创建新快照"));
        m_logger.info(QStringLiteral("自动同步完成：%1 存档无变化，未创建新快照").arg(game.displayName));
        return;
    }

    const QVariantMap snapshotResult = m_snapshotPreprocessor.createSnapshotForGame(game);
    applySnapshotResult(appId, snapshotResult);

    if (snapshotResult.value(QStringLiteral("success")).toBool()) {
        const QString fileName = snapshotResult.value(QStringLiteral("snapshotFileName")).toString();
        updateSyncStatusForGame(appId, QStringLiteral("自动同步已创建本地快照，正在上传云端"));
        m_logger.info(QStringLiteral("自动同步已创建本地快照：%1，快照：%2，开始上传云端")
                          .arg(game.displayName, fileName));
        uploadAutoSyncSnapshotForGame(game, snapshotResult);
    } else {
        const QString detail = snapshotResult.value(QStringLiteral("detail")).toString();
        updateSyncStatusForGame(appId, QStringLiteral("自动同步失败：本地快照创建失败"));
        m_logger.error(QStringLiteral("自动同步失败：%1 本地快照创建失败，原因：%2").arg(game.displayName, detail));
    }
}

void SteamManager::setQuarkGatewayStatus(const QString &status)
{
    if (m_quarkGatewayStatus == status) {
        return;
    }

    m_quarkGatewayStatus = status;
    emit quarkGatewayStatusChanged();
}

void SteamManager::startQuarkCookieHealthCheck()
{
    if (!m_webDavConfig.isValid()) {
        return;
    }

    /*
     * 夸克 Cookie 有一种比较迷惑的“半失效”状态：
     * OpenList 仍然可以列目录，但真正下载文件时会返回 403 Forbidden
     * 或 412 Precondition Failed。这里写入并读取一个很小的 JSON 探针，
     * 用真实文件读写验证 Cookie、OpenList 网关、夸克下载鉴权是否仍然可用。
     */
    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("quark-cookie-health-check"));
    payload.insert(QStringLiteral("checkedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    payload.insert(QStringLiteral("app"), QStringLiteral("GameSaveCloudQt"));

    m_logger.info(QStringLiteral("开始夸克 Cookie 健康检查：将写入并读取探针文件 %1")
                      .arg(quarkCookieHealthCheckPath()));
    m_quarkGatewayManager.uploadDataFile(
        quarkCookieHealthCheckPath(),
        QJsonDocument(payload).toJson(QJsonDocument::Compact),
        QStringLiteral("quark-cookie-health-upload"));
}

QString SteamManager::quarkCookieHealthCheckPath() const
{
    QString rootPath = m_webDavConfig.remoteRootPath.trimmed();
    rootPath.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (rootPath.isEmpty()) {
        rootPath = QStringLiteral("/Quark/GameSaveCloudQt");
    }
    if (!rootPath.startsWith(QLatin1Char('/'))) {
        rootPath.prepend(QLatin1Char('/'));
    }
    while (rootPath.length() > 1 && rootPath.endsWith(QLatin1Char('/'))) {
        rootPath.chop(1);
    }

    return rootPath + QStringLiteral("/.healthcheck.json");
}

bool SteamManager::isQuarkAuthFailureMessage(const QString &message) const
{
    const QString text = message.trimmed();
    return text.contains(QStringLiteral("403"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("412"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("Forbidden"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("Precondition Failed"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("Unauthorized"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("Authentication"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("主机需要验证"))
        || text.contains(QStringLiteral("需要验证"))
        || text.contains(QStringLiteral("鉴权"));
}

bool SteamManager::shouldLogAutomaticCloudRefresh(const QString &appId)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime lastLoggedAt = m_lastAutomaticCloudRefreshLogByAppId.value(appId);
    if (lastLoggedAt.isValid() && lastLoggedAt.secsTo(now) < 10) {
        return false;
    }

    m_lastAutomaticCloudRefreshLogByAppId.insert(appId, now);
    return true;
}
