#include "QuarkGatewayManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QAuthenticator>
#include <QBuffer>
#include <QDirIterator>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <memory>
#include <utility>

#include "storage/AppSettings.h"

QuarkGatewayManager::QuarkGatewayManager(QObject *parent)
    : QObject(parent)
{
    m_loginPollTimer.setInterval(800);
    connect(&m_loginPollTimer, &QTimer::timeout, this, &QuarkGatewayManager::pollLogin);
    connect(&m_network, &QNetworkAccessManager::authenticationRequired, this,
            [this](QNetworkReply *reply, QAuthenticator *authenticator) {
                const QUrl url = reply ? reply->url() : QUrl();
                const bool isLocalOpenList = url.host() == QStringLiteral("127.0.0.1")
                                             || url.host() == QStringLiteral("localhost");
                if (!isLocalOpenList || m_adminPassword.trimmed().isEmpty()) {
                    return;
                }

                authenticator->setUser(QStringLiteral("admin"));
                authenticator->setPassword(m_adminPassword);
            });
}

void QuarkGatewayManager::startAndConfigure(const QString &cookie, bool forceStorageUpdate)
{
    if (m_configuring) {
        emit statusChanged(QStringLiteral("夸克网关正在启动和配置，请等待当前操作完成"));
        return;
    }

    m_cookie = cookie.trimmed();
    if (m_cookie.isEmpty()) {
        finishFailure(QStringLiteral("夸克 Cookie 为空，无法配置 OpenList 夸克驱动"));
        return;
    }

    if (!QFileInfo::exists(executablePath())) {
        finishFailure(QStringLiteral("未找到 OpenList 引擎：%1，请把 openlist.exe 放到该位置").arg(executablePath()));
        return;
    }

    m_configuring = true;
    m_forceStorageUpdate = forceStorageUpdate;
    m_loginAttempts = 0;
    m_mountConflictRetries = 0;
    m_adminPassword = loadOrCreateAdminPassword();

    QDir dataDir(dataDirectoryPath());
    if (!dataDir.exists() && !dataDir.mkpath(QStringLiteral("."))) {
        finishFailure(QStringLiteral("无法创建 OpenList 数据目录：%1").arg(dataDirectoryPath()));
        return;
    }

    migrateLegacyDataDirectory();
    cleanupOpenListLogs();

    emit statusChanged(QStringLiteral("正在设置 OpenList 管理员密码"));
    if (!setAdminPassword(m_adminPassword)) {
        finishFailure(QStringLiteral("OpenList 管理员密码设置失败，请检查引擎版本或文件权限"));
        return;
    }

    emit statusChanged(QStringLiteral("正在启动 OpenList 本机网关"));
    startProcess();
    m_loginPollTimer.start();
}

void QuarkGatewayManager::uploadFileIfMissing(const QString &localFilePath, const QString &remoteDirectoryPath)
{
    const QFileInfo fileInfo(localFilePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        emit fileUploadFinished(false,
                                localFilePath,
                                QString(),
                                QStringLiteral("local_file_missing"),
                                QStringLiteral("本地快照文件不存在，无法上传到夸克网盘"));
        return;
    }

    if (m_apiToken.trimmed().isEmpty()) {
        emit fileUploadFinished(false,
                                localFilePath,
                                QString(),
                                QStringLiteral("quark_gateway_not_ready"),
                                QStringLiteral("夸克本机网关尚未连接成功，请先在云同步设置中点击“连接”"));
        return;
    }

    const QString remoteDirectory = normalizedRemotePath(remoteDirectoryPath);
    UploadRequest request;
    request.localFilePath = localFilePath;
    request.remoteDirectoryPath = remoteDirectory;
    request.overwrite = false;
    request.uploadState = QStringLiteral("uploaded");
    request.successMessage = QStringLiteral("快照已通过 OpenList API 上传到夸克网盘");

    m_uploadQueue.enqueue(request);
    startNextUpload();
}

void QuarkGatewayManager::uploadFileOverwrite(const QString &localFilePath, const QString &remoteDirectoryPath)
{
    const QFileInfo fileInfo(localFilePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        emit fileUploadFinished(false,
                                localFilePath,
                                QString(),
                                QStringLiteral("local_file_missing"),
                                QStringLiteral("本地快照文件不存在，无法覆盖上传到夸克网盘"));
        return;
    }

    if (m_apiToken.trimmed().isEmpty()) {
        emit fileUploadFinished(false,
                                localFilePath,
                                QString(),
                                QStringLiteral("quark_gateway_not_ready"),
                                QStringLiteral("夸克本机网关尚未连接成功，请先在云同步设置中点击“连接”"));
        return;
    }

    const QString remoteDirectory = normalizedRemotePath(remoteDirectoryPath);
    UploadRequest request;
    request.localFilePath = localFilePath;
    request.remoteDirectoryPath = remoteDirectory;
    request.overwrite = true;
    request.uploadState = QStringLiteral("overwritten");
    request.successMessage = QStringLiteral("快照已通过 OpenList API 覆盖上传到夸克网盘");

    m_uploadQueue.enqueue(request);
    startNextUpload();
}

void QuarkGatewayManager::uploadDataFile(
    const QString &remoteFilePath,
    const QByteArray &data,
    const QString &operationId)
{
    const QString cleanRemotePath = normalizedRemotePath(remoteFilePath);
    if (cleanRemotePath.isEmpty()) {
        emit dataFileUploadFinished(false,
                                    remoteFilePath,
                                    operationId,
                                    QStringLiteral("云端 JSON 路径为空，无法上传云端索引"));
        return;
    }

    if (m_apiToken.trimmed().isEmpty()) {
        emit dataFileUploadFinished(false,
                                    cleanRemotePath,
                                    operationId,
                                    QStringLiteral("夸克本机网关尚未连接成功，无法上传云端索引"));
        return;
    }

    ensureDirectoryBeforeDataUpload(cleanRemotePath, data, operationId);
}

void QuarkGatewayManager::uploadDataFileOverwrite(
    const QString &remoteFilePath,
    const QByteArray &data,
    const QString &operationId)
{
    const QString cleanRemotePath = normalizedRemotePath(remoteFilePath);
    if (cleanRemotePath.isEmpty()) {
        emit dataFileUploadFinished(false,
                                    remoteFilePath,
                                    operationId,
                                    QStringLiteral("云端 JSON 路径为空，无法覆盖写入云端配置"));
        return;
    }

    if (m_apiToken.trimmed().isEmpty()) {
        emit dataFileUploadFinished(false,
                                    cleanRemotePath,
                                    operationId,
                                    QStringLiteral("夸克本机网关尚未连接成功，无法覆盖写入云端配置"));
        return;
    }

    ensureDirectoryBeforeDataUpload(cleanRemotePath, data, operationId, true);
}

void QuarkGatewayManager::downloadFile(const QString &remoteFilePath, const QString &localFilePath)
{
    const QString cleanRemotePath = normalizedRemotePath(remoteFilePath);
    if (cleanRemotePath.isEmpty() || localFilePath.trimmed().isEmpty()) {
        emit fileDownloadFinished(false,
                                  remoteFilePath,
                                  localFilePath,
                                  QStringLiteral("云端路径或本地保存路径为空，无法下载快照"));
        return;
    }

    if (m_apiToken.trimmed().isEmpty()) {
        emit fileDownloadFinished(false,
                                  cleanRemotePath,
                                  localFilePath,
                                  QStringLiteral("夸克本机网关尚未连接成功，请先在云同步设置中点击“连接”"));
        return;
    }

    m_downloadQueue.enqueue(QPair<QString, QString>(cleanRemotePath, localFilePath));
    startNextDownload();
}

void QuarkGatewayManager::performDownloadFile(const QString &remoteFilePath, const QString &localFilePath)
{
    const QString cleanRemotePath = normalizedRemotePath(remoteFilePath);
    QNetworkRequest request = apiRequest(QStringLiteral("/api/fs/get"), m_apiToken);
    QJsonObject body;
    body.insert(QStringLiteral("path"), cleanRemotePath);
    body.insert(QStringLiteral("password"), QString());
    body.insert(QStringLiteral("refresh"), false);

    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, cleanRemotePath, localFilePath]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        if (error != QNetworkReply::NoError || root.value(QStringLiteral("code")).toInt() != 200) {
            finishFileDownload(false,
                               cleanRemotePath,
                               localFilePath,
                               apiMessageFromPayload(payload, errorString));
            return;
        }

        downloadFromUrlCandidates(
            cleanRemotePath,
            localFilePath,
            buildDownloadCandidates(cleanRemotePath, root.value(QStringLiteral("data")).toObject()));
    });
}

void QuarkGatewayManager::downloadDataFile(const QString &remoteFilePath, const QString &operationId, bool forceRefresh)
{
    const QString cleanRemotePath = normalizedRemotePath(remoteFilePath);
    if (cleanRemotePath.isEmpty()) {
        emit dataFileDownloadFinished(false,
                                      remoteFilePath,
                                      operationId,
                                      {},
                                      QStringLiteral("云端 JSON 路径为空，无法下载云端索引"));
        return;
    }

    if (m_apiToken.trimmed().isEmpty()) {
        emit dataFileDownloadFinished(false,
                                      cleanRemotePath,
                                      operationId,
                                      {},
                                      QStringLiteral("夸克本机网关尚未连接成功，无法下载云端索引"));
        return;
    }

    QNetworkRequest request = apiRequest(QStringLiteral("/api/fs/get"), m_apiToken);
    QJsonObject body;
    body.insert(QStringLiteral("path"), cleanRemotePath);
    body.insert(QStringLiteral("password"), QString());
    body.insert(QStringLiteral("refresh"), forceRefresh);

    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, cleanRemotePath, operationId, forceRefresh]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        if (error != QNetworkReply::NoError || root.value(QStringLiteral("code")).toInt() != 200) {
            emit dataFileDownloadFinished(false,
                                          cleanRemotePath,
                                          operationId,
                                          {},
                                          apiMessageFromPayload(payload, errorString));
            return;
        }

        const QJsonObject data = root.value(QStringLiteral("data")).toObject();
        QVariantMap headers = data.value(QStringLiteral("header")).toObject().toVariantMap();
        if (headers.isEmpty()) {
            headers = data.value(QStringLiteral("headers")).toObject().toVariantMap();
        }

        const QString sign = data.value(QStringLiteral("sign")).toString();
        QUrl downloadUrl = proxiedDownloadUrl(cleanRemotePath, sign, true);
        if (sign.trimmed().isEmpty()) {
            QString rawUrl = data.value(QStringLiteral("raw_url")).toString();
            if (rawUrl.trimmed().isEmpty()) {
                rawUrl = data.value(QStringLiteral("url")).toString();
            }

            QUrl rawDownloadUrl(rawUrl);
            if (rawDownloadUrl.isRelative() && rawUrl.startsWith(QLatin1Char('/'))) {
                rawDownloadUrl = QUrl(gatewayBaseUrl() + rawUrl);
            }

            const bool isLocalOpenListUrl = rawDownloadUrl.host() == QStringLiteral("127.0.0.1")
                                            || rawDownloadUrl.host() == QStringLiteral("localhost");
            if (rawDownloadUrl.isValid() && isLocalOpenListUrl) {
                downloadUrl = rawDownloadUrl;
            }
        }

        downloadDataFromUrl(cleanRemotePath, operationId, downloadUrl, headers, forceRefresh);
    });
}

void QuarkGatewayManager::checkStorageHealth(const QString &operationId)
{
    if (m_apiToken.trimmed().isEmpty()) {
        emit storageHealthCheckFinished(false,
                                        operationId,
                                        QStringLiteral("OpenList 本机网关尚未完成登录，无法检查夸克挂载状态"));
        return;
    }

    QNetworkRequest request = apiRequest(QStringLiteral("/api/admin/storage/list?page=1&per_page=200"), m_apiToken);
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, operationId]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        if (error != QNetworkReply::NoError || root.value(QStringLiteral("code")).toInt() != 200) {
            emit storageHealthCheckFinished(false,
                                            operationId,
                                            apiMessageFromPayload(payload, errorString));
            return;
        }

        const QJsonObject storage = quarkStorageFromListPayload(payload);
        const int storageId = storage.value(QStringLiteral("id")).toInt();
        if (storageId <= 0) {
            emit storageHealthCheckFinished(false,
                                            operationId,
                                            QStringLiteral("OpenList 未找到 /Quark 夸克挂载，请重新连接 Cookie"));
            return;
        }

        const QString status = storage.value(QStringLiteral("status")).toString().trimmed();
        if (!status.isEmpty() && status.compare(QStringLiteral("work"), Qt::CaseInsensitive) != 0) {
            emit storageHealthCheckFinished(false,
                                            operationId,
                                            QStringLiteral("OpenList /Quark 挂载状态异常：%1")
                                                .arg(status.isEmpty() ? QStringLiteral("unknown") : status));
            return;
        }

        emit storageHealthCheckFinished(true,
                                        operationId,
                                        status.isEmpty()
                                            ? QStringLiteral("OpenList 已找到 /Quark 挂载，但未返回 storage status，将继续检查 WebDAV")
                                            : QStringLiteral("OpenList /Quark 挂载状态正常：status=work"));
    });
}

void QuarkGatewayManager::checkWebDavDirectory(const QString &remoteDirectoryPath, const QString &operationId)
{
    const QString cleanRemotePath = normalizedRemotePath(remoteDirectoryPath);
    QUrl url(gatewayBaseUrl() + QStringLiteral("/dav") + cleanRemotePath);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GameSaveCloudQt/0.1"));
    request.setRawHeader("Depth", "0");
    request.setTransferTimeout(12000);

    if (!m_adminPassword.isEmpty()) {
        const QByteArray credential = QStringLiteral("admin:%1").arg(m_adminPassword).toUtf8().toBase64();
        request.setRawHeader("Authorization", QByteArrayLiteral("Basic ") + credential);
    }

    const QByteArray body = QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<propfind xmlns=\"DAV:\"><prop><resourcetype/></prop></propfind>");
    QNetworkReply *reply = m_network.sendCustomRequest(request, QByteArrayLiteral("PROPFIND"), body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cleanRemotePath, operationId]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();

        if (error == QNetworkReply::NoError && (httpStatus == 200 || httpStatus == 207)) {
            emit remoteDirectoryCheckFinished(true,
                                             cleanRemotePath,
                                             operationId,
                                             QStringLiteral("OpenList WebDAV 目录可访问"));
            return;
        }

        const QString statusText = httpStatus > 0
                                       ? QStringLiteral("HTTP %1，%2").arg(httpStatus).arg(errorString)
                                       : errorString;
        emit remoteDirectoryCheckFinished(false,
                                         cleanRemotePath,
                                         operationId,
                                         QStringLiteral("OpenList WebDAV 目录不可访问：%1").arg(statusText));
    });
}

QString QuarkGatewayManager::adminPassword() const
{
    return m_adminPassword;
}

QString QuarkGatewayManager::gatewayBaseUrl() const
{
    return QStringLiteral("http://127.0.0.1:5244");
}

QString QuarkGatewayManager::executablePath() const
{
    return findExecutablePath();
}

QString QuarkGatewayManager::dataDirectoryPath() const
{
    return QDir::toNativeSeparators(QDir(appDataRootPath()).absoluteFilePath(QStringLiteral("openlist")));
}

QString QuarkGatewayManager::findExecutablePath() const
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QStringList candidates = {
        appDir.absoluteFilePath(QStringLiteral("engines/openlist/openlist.exe")),
        appDir.absoluteFilePath(QStringLiteral("openlist.exe")),
        appDir.absoluteFilePath(QStringLiteral("engines/alist/alist.exe")),
        appDir.absoluteFilePath(QStringLiteral("alist.exe"))
    };

    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::toNativeSeparators(candidate);
        }
    }

    return QDir::toNativeSeparators(candidates.first());
}

QString QuarkGatewayManager::engineDirectoryPath() const
{
    return QDir::toNativeSeparators(QFileInfo(executablePath()).absolutePath());
}

QString QuarkGatewayManager::legacyDataDirectoryPath() const
{
    return QDir::toNativeSeparators(QDir(engineDirectoryPath()).absoluteFilePath(QStringLiteral("data")));
}

QString QuarkGatewayManager::appDataRootPath() const
{
#ifdef Q_OS_WIN
    const QString roamingPath = qEnvironmentVariable("APPDATA").trimmed();
    if (!roamingPath.isEmpty()) {
        return QDir::toNativeSeparators(QDir(roamingPath).absoluteFilePath(QStringLiteral("GameSaveCloudQt")));
    }
#endif

    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.trimmed().isEmpty()) {
        appDataPath = QDir::home().absoluteFilePath(QStringLiteral(".GameSaveCloudQt"));
    }

    return QDir::toNativeSeparators(appDataPath);
}

void QuarkGatewayManager::cleanupOpenListLogs(int daysToKeep) const
{
    const QDir dataDir(dataDirectoryPath());
    if (!dataDir.exists()) {
        return;
    }

    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-std::max(1, daysToKeep));
    const QStringList logRoots = {
        dataDir.absolutePath(),
        dataDir.absoluteFilePath(QStringLiteral("log")),
        dataDir.absoluteFilePath(QStringLiteral("logs"))
    };

    QSet<QString> visitedRoots;
    for (const QString &rootPath : logRoots) {
        const QString cleanRootPath = QDir::cleanPath(rootPath);
        if (visitedRoots.contains(cleanRootPath)) {
            continue;
        }
        visitedRoots.insert(cleanRootPath);

        QDir rootDir(cleanRootPath);
        if (!rootDir.exists()) {
            continue;
        }

        QDirIterator iterator(
            rootDir.absolutePath(),
            {QStringLiteral("*.log"), QStringLiteral("*.log.*")},
            QDir::Files | QDir::Readable | QDir::Writable,
            QDirIterator::Subdirectories);

        while (iterator.hasNext()) {
            iterator.next();
            const QFileInfo fileInfo(iterator.filePath());
            if (!fileInfo.lastModified().isValid() || fileInfo.lastModified() >= cutoff) {
                continue;
            }

            QFile::remove(fileInfo.absoluteFilePath());
        }
    }
}

bool QuarkGatewayManager::migrateLegacyDataDirectory()
{
    const QString sourcePath = legacyDataDirectoryPath();
    const QString targetPath = dataDirectoryPath();
    if (QDir::cleanPath(sourcePath).compare(QDir::cleanPath(targetPath), Qt::CaseInsensitive) == 0) {
        return false;
    }

    const QDir sourceDir(sourcePath);
    if (!sourceDir.exists()) {
        return false;
    }

    QDir targetDir(targetPath);
    const bool targetHasOpenListData = QFileInfo::exists(targetDir.absoluteFilePath(QStringLiteral("data.db")))
                                       || QFileInfo::exists(targetDir.absoluteFilePath(QStringLiteral("config.json")));
    if (targetHasOpenListData) {
        return false;
    }

    if (!targetDir.exists() && !targetDir.mkpath(QStringLiteral("."))) {
        emit statusChanged(QStringLiteral("OpenList 固定数据目录创建失败，无法迁移旧数据"));
        return false;
    }

    const bool copied = copyDirectoryContents(sourcePath, targetPath);
    if (copied) {
        emit statusChanged(QStringLiteral("已迁移旧 OpenList 数据到固定目录"));
    }
    return copied;
}

bool QuarkGatewayManager::copyDirectoryContents(const QString &sourcePath, const QString &targetPath) const
{
    const QDir sourceDir(sourcePath);
    if (!sourceDir.exists()) {
        return false;
    }

    QDir targetDir(targetPath);
    if (!targetDir.exists() && !targetDir.mkpath(QStringLiteral("."))) {
        return false;
    }

    QDirIterator iterator(sourcePath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString sourceItemPath = iterator.next();
        const QString relativePath = sourceDir.relativeFilePath(sourceItemPath);
        const QString targetItemPath = targetDir.absoluteFilePath(relativePath);
        const QFileInfo sourceInfo(sourceItemPath);

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

        if (QFileInfo::exists(targetItemPath)) {
            continue;
        }

        if (!QFile::copy(sourceItemPath, targetItemPath)) {
            return false;
        }
    }

    return true;
}

QString QuarkGatewayManager::loadOrCreateAdminPassword()
{
    const std::unique_ptr<QSettings> settings = AppSettings::create();
    QString password = settings->value(QStringLiteral("quark/openListAdminPassword")).toString();
    if (!password.trimmed().isEmpty()) {
        return password;
    }

    const QString alphabet = QStringLiteral("ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789");
    for (int i = 0; i < 28; ++i) {
        password.append(alphabet.at(QRandomGenerator::global()->bounded(alphabet.length())));
    }

    settings->setValue(QStringLiteral("quark/openListAdminPassword"), password);
    settings->sync();
    return password;
}

bool QuarkGatewayManager::setAdminPassword(const QString &password)
{
    /*
     * OpenList/AList 提供 admin set 命令修改管理员密码。
     * WebDAV 认证会使用这个 admin 账号，所以密码由软件生成并保存到本地 settings.ini。
     */
    QProcess process;
    process.setProgram(executablePath());
    process.setArguments({
        QStringLiteral("admin"),
        QStringLiteral("set"),
        password,
        QStringLiteral("--data"),
        dataDirectoryPath()
    });
    process.setWorkingDirectory(engineDirectoryPath());
    process.start();
    if (!process.waitForStarted(5000) || !process.waitForFinished(15000)) {
        return false;
    }

    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

void QuarkGatewayManager::startProcess()
{
    if (m_process.state() != QProcess::NotRunning) {
        return;
    }

    m_process.setProgram(executablePath());
    m_process.setArguments({
        QStringLiteral("server"),
        QStringLiteral("--data"),
        dataDirectoryPath(),
        QStringLiteral("--log-std")
    });
    m_process.setWorkingDirectory(engineDirectoryPath());
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_process.start();
}

void QuarkGatewayManager::pollLogin()
{
    ++m_loginAttempts;
    if (m_loginAttempts > 30) {
        m_loginPollTimer.stop();
        finishFailure(QStringLiteral("OpenList 本机网关启动超时，请检查 5244 端口是否被占用"));
        return;
    }

    login();
}

void QuarkGatewayManager::login()
{
    QNetworkRequest request = apiRequest(QStringLiteral("/api/auth/login"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject body;
    body.insert(QStringLiteral("username"), QStringLiteral("admin"));
    body.insert(QStringLiteral("password"), m_adminPassword);

    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (error != QNetworkReply::NoError) {
            return;
        }

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        if (root.value(QStringLiteral("code")).toInt() != 200) {
            return;
        }

        const QString token = root.value(QStringLiteral("data")).toObject().value(QStringLiteral("token")).toString();
        if (token.isEmpty()) {
            return;
        }

        m_apiToken = token;
        m_loginPollTimer.stop();
        emit statusChanged(QStringLiteral("OpenList 已启动，正在配置夸克存储"));
        configureStorage(token);
    });
}

void QuarkGatewayManager::configureStorage(const QString &token)
{
    QNetworkRequest request = apiRequest(QStringLiteral("/api/admin/storage/list?page=1&per_page=200"), token);
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, token]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        QJsonObject existingStorage;
        if (error == QNetworkReply::NoError) {
            existingStorage = quarkStorageFromListPayload(payload);
        }

        const int existingStorageId = existingStorage.value(QStringLiteral("id")).toInt();
        const QString status = existingStorage.value(QStringLiteral("status")).toString().trimmed();
        const bool storageLooksHealthy = status.isEmpty()
                                         || status.compare(QStringLiteral("work"), Qt::CaseInsensitive) == 0;
        const int cacheExpiration = existingStorage.value(QStringLiteral("cache_expiration")).toInt(-1);
        const bool storageNeedsCachePolicyUpdate = cacheExpiration == 0;
        if (existingStorageId > 0 && !m_forceStorageUpdate && storageLooksHealthy && !storageNeedsCachePolicyUpdate) {
            emit statusChanged(QStringLiteral("已复用本地 OpenList 夸克挂载"));
            ensureSyncDirectory(token);
            return;
        }

        if (existingStorageId > 0 && storageNeedsCachePolicyUpdate) {
            emit statusChanged(QStringLiteral("正在更新 OpenList 夸克挂载缓存策略"));
        }

        createOrUpdateStorage(token, existingStorageId);
    });
}

QJsonObject QuarkGatewayManager::quarkStorageFromListPayload(const QByteArray &payload) const
{
    const QJsonObject root = QJsonDocument::fromJson(payload).object();
    const QJsonObject data = root.value(QStringLiteral("data")).toObject();

    const QList<QJsonArray> candidateArrays = {
        data.value(QStringLiteral("content")).toArray(),
        data.value(QStringLiteral("items")).toArray(),
        data.value(QStringLiteral("storages")).toArray(),
        root.value(QStringLiteral("content")).toArray(),
        root.value(QStringLiteral("items")).toArray()
    };

    for (const QJsonArray &array : candidateArrays) {
        for (const QJsonValue &value : array) {
            const QJsonObject item = value.toObject();
            const QString driver = item.value(QStringLiteral("driver")).toString().trimmed();
            if (!driver.isEmpty() && driver.compare(QStringLiteral("Quark"), Qt::CaseInsensitive) != 0) {
                continue;
            }

            QString mountPath = item.value(QStringLiteral("mount_path")).toString().trimmed();
            if (!mountPath.startsWith(QLatin1Char('/'))) {
                mountPath.prepend(QLatin1Char('/'));
            }

            if (mountPath.compare(QStringLiteral("/Quark"), Qt::CaseInsensitive) == 0) {
                return item;
            }
        }
    }

    return {};
}

void QuarkGatewayManager::createOrUpdateStorage(const QString &token, int existingStorageId)
{
    const QString path = existingStorageId > 0
                             ? QStringLiteral("/api/admin/storage/update")
                             : QStringLiteral("/api/admin/storage/create");
    QNetworkRequest request = apiRequest(path, token);
    QNetworkReply *reply = m_network.post(request, storagePayload(existingStorageId));

    connect(reply, &QNetworkReply::finished, this, [this, reply, token, existingStorageId]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (error != QNetworkReply::NoError) {
            finishFailure(QStringLiteral("OpenList 夸克存储配置失败：网络请求失败"));
            return;
        }

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        if (root.value(QStringLiteral("code")).toInt() != 200) {
            const QString message = root.value(QStringLiteral("message")).toString();
            if (existingStorageId <= 0
                && m_mountConflictRetries < 1
                && (message.contains(QStringLiteral("UNIQUE constraint failed"), Qt::CaseInsensitive)
                    || message.contains(QStringLiteral("mount_path"), Qt::CaseInsensitive))) {
                ++m_mountConflictRetries;
                emit statusChanged(QStringLiteral("OpenList 已存在 /Quark 挂载，正在改为更新配置"));
                configureStorage(token);
                return;
            }

            finishFailure(QStringLiteral("OpenList 夸克存储配置失败：%1").arg(message));
            return;
        }

        ensureSyncDirectory(token);
    });
}

void QuarkGatewayManager::ensureSyncDirectory(const QString &token)
{
    /*
     * 某些网盘驱动会拒绝 WebDAV MKCOL，但允许通过 OpenList 自身 API 创建目录。
     * 先用 /api/fs/mkdir 确保同步根目录存在，后续 WebDAV 连接测试只需 PROPFIND。
     */
    QNetworkRequest request = apiRequest(QStringLiteral("/api/fs/mkdir"), token);
    QJsonObject body;
    body.insert(QStringLiteral("path"), QStringLiteral("/Quark/GameSaveCloudQt"));

    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (error != QNetworkReply::NoError) {
            finishFailure(QStringLiteral("OpenList 夸克同步目录创建失败：网络请求失败"));
            return;
        }

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        const int code = root.value(QStringLiteral("code")).toInt();
        const QString message = root.value(QStringLiteral("message")).toString();
        if (code == 200
            || message.contains(QStringLiteral("exist"), Qt::CaseInsensitive)
            || message.contains(QStringLiteral("exists"), Qt::CaseInsensitive)
            || message.contains(QStringLiteral("已存在"))) {
            emitGatewayReady();
            return;
        }

        finishFailure(QStringLiteral("OpenList 夸克同步目录创建失败：%1").arg(message));
    });
}

void QuarkGatewayManager::emitGatewayReady()
{
    m_configuring = false;
    WebDavConfig config;
    config.serverUrl = gatewayBaseUrl() + QStringLiteral("/dav");
    config.username = QStringLiteral("admin");
    config.password = m_adminPassword;
    config.remoteRootPath = QStringLiteral("/Quark/GameSaveCloudQt");
    emit gatewayReady(config, QStringLiteral("OpenList 夸克网关已就绪，可以上传和下载快照"));
}

void QuarkGatewayManager::startNextUpload()
{
    if (m_uploadInProgress || m_uploadQueue.isEmpty()) {
        return;
    }

    const UploadRequest request = m_uploadQueue.dequeue();
    m_uploadInProgress = true;
    performUploadFile(request);
}

void QuarkGatewayManager::performUploadFile(const UploadRequest &request)
{
    const QFileInfo fileInfo(request.localFilePath);
    const QString remoteDirectory = normalizedRemotePath(request.remoteDirectoryPath);
    const QString remoteFilePath = remoteDirectory + QLatin1Char('/') + fileInfo.fileName();

    if (request.overwrite) {
        ensureDirectoryBeforeUpload(
            request.localFilePath,
            remoteDirectory,
            remoteFilePath,
            request.uploadState,
            request.successMessage);
        return;
    }

    checkRemoteFileBeforeUpload(request.localFilePath, remoteFilePath);
}

void QuarkGatewayManager::finishFileUpload(
    bool success,
    const QString &localFilePath,
    const QString &remoteFilePath,
    const QString &uploadState,
    const QString &message)
{
    emit fileUploadFinished(success, localFilePath, remoteFilePath, uploadState, message);

    if (m_uploadInProgress) {
        m_uploadInProgress = false;
    }
    QTimer::singleShot(250, this, &QuarkGatewayManager::startNextUpload);
}

void QuarkGatewayManager::checkRemoteFileBeforeUpload(const QString &localFilePath, const QString &remoteFilePath)
{
    const int slashIndex = remoteFilePath.lastIndexOf(QLatin1Char('/'));
    const QString remoteDirectory = slashIndex > 0
                                        ? remoteFilePath.left(slashIndex)
                                        : QStringLiteral("/Quark/GameSaveCloudQt");

    QNetworkRequest request = apiRequest(QStringLiteral("/api/fs/list"), m_apiToken);
    QJsonObject body;
    body.insert(QStringLiteral("path"), normalizedRemotePath(remoteDirectory));
    body.insert(QStringLiteral("password"), QString());
    body.insert(QStringLiteral("page"), 1);
    body.insert(QStringLiteral("per_page"), 1);
    /*
     * 先刷新父目录，再检查具体文件。
     * 夸克网页端手动删除文件后，旧目录缓存最容易让 OpenList 继续返回已删除文件。
     */
    body.insert(QStringLiteral("refresh"), true);

    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, localFilePath, remoteFilePath, remoteDirectory]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        if (error == QNetworkReply::NoError && root.value(QStringLiteral("code")).toInt() == 200) {
            checkRemoteFileAfterDirectoryRefresh(localFilePath, remoteFilePath);
            return;
        }

        const QString message = root.value(QStringLiteral("message")).toString();
        if (message.contains(QStringLiteral("not exist"), Qt::CaseInsensitive)
            || message.contains(QStringLiteral("not found"), Qt::CaseInsensitive)
            || message.contains(QStringLiteral("不存在"))) {
            ensureDirectoryBeforeUpload(localFilePath, remoteDirectory, remoteFilePath);
            return;
        }

        finishFileUpload(false,
                         localFilePath,
                         remoteFilePath,
                         QStringLiteral("remote_check_failed"),
                         QStringLiteral("刷新云端快照目录失败，无法确认是否需要重新上传：%1")
                             .arg(apiMessageFromPayload(payload, errorString)));
    });
}

void QuarkGatewayManager::checkRemoteFileAfterDirectoryRefresh(const QString &localFilePath, const QString &remoteFilePath)
{
    QNetworkRequest request = apiRequest(QStringLiteral("/api/fs/get"), m_apiToken);
    QJsonObject body;
    body.insert(QStringLiteral("path"), remoteFilePath);
    body.insert(QStringLiteral("password"), QString());
    /*
     * 上传前的存在性检查必须强制刷新 OpenList/夸克侧缓存。
     * 用户可能在网盘网页端手动删除了旧快照，如果这里只读缓存，
     * 软件会误以为云端仍有同名 zip，从而跳过本该重新上传的文件。
     */
    body.insert(QStringLiteral("refresh"), true);

    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, localFilePath, remoteFilePath]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        if (error == QNetworkReply::NoError && root.value(QStringLiteral("code")).toInt() == 200) {
            finishFileUpload(true,
                             localFilePath,
                             remoteFilePath,
                             QStringLiteral("remote_exists"),
                             QStringLiteral("夸克网盘中已存在同名快照，已跳过上传"));
            return;
        }

        const QString remoteDirectory = remoteFilePath.left(remoteFilePath.lastIndexOf(QLatin1Char('/')));
        ensureDirectoryBeforeUpload(localFilePath, remoteDirectory, remoteFilePath);
    });
}

void QuarkGatewayManager::ensureDirectoryBeforeUpload(
    const QString &localFilePath,
    const QString &remoteDirectoryPath,
    const QString &remoteFilePath,
    const QString &uploadState,
    const QString &successMessage)
{
    QNetworkRequest request = apiRequest(QStringLiteral("/api/fs/mkdir"), m_apiToken);
    QJsonObject body;
    body.insert(QStringLiteral("path"), normalizedRemotePath(remoteDirectoryPath));

    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, localFilePath, remoteFilePath, uploadState, successMessage]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        const int code = root.value(QStringLiteral("code")).toInt();
        const QString message = root.value(QStringLiteral("message")).toString();
        if (error == QNetworkReply::NoError
            && (code == 200
                || message.contains(QStringLiteral("exist"), Qt::CaseInsensitive)
                || message.contains(QStringLiteral("exists"), Qt::CaseInsensitive)
                || message.contains(QStringLiteral("已存在")))) {
            putFile(localFilePath, remoteFilePath, uploadState, successMessage);
            return;
        }

        finishFileUpload(false,
                         localFilePath,
                         remoteFilePath,
                         QStringLiteral("remote_directory_failed"),
                         apiMessageFromPayload(payload, errorString));
    });
}

void QuarkGatewayManager::putFile(
    const QString &localFilePath,
    const QString &remoteFilePath,
    const QString &uploadState,
    const QString &successMessage)
{
    QFile *file = new QFile(localFilePath, this);
    if (!file->open(QIODevice::ReadOnly)) {
        const QString error = file->errorString();
        file->deleteLater();
        finishFileUpload(false,
                         localFilePath,
                         remoteFilePath,
                         QStringLiteral("local_file_open_failed"),
                         QStringLiteral("无法读取本地快照文件：%1").arg(error));
        return;
    }

    QNetworkRequest request = apiRequest(QStringLiteral("/api/fs/put"), m_apiToken);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    request.setRawHeader("File-Path", QUrl::toPercentEncoding(remoteFilePath));
    request.setTransferTimeout(300000);

    QNetworkReply *reply = m_network.put(request, file);
    file->setParent(reply);
    connect(reply, &QNetworkReply::uploadProgress, this,
            [this, localFilePath, remoteFilePath](qint64 bytesSent, qint64 bytesTotal) {
                emit fileUploadProgress(localFilePath, remoteFilePath, bytesSent, bytesTotal);
            });

    connect(reply, &QNetworkReply::finished, this, [this, reply, file, localFilePath, remoteFilePath, uploadState, successMessage]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        const QByteArray payload = reply->readAll();
        file->close();
        reply->deleteLater();

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        if (error == QNetworkReply::NoError && root.value(QStringLiteral("code")).toInt() == 200) {
            finishFileUpload(true,
                             localFilePath,
                             remoteFilePath,
                             uploadState,
                             successMessage);
            return;
        }

        finishFileUpload(false,
                         localFilePath,
                         remoteFilePath,
                         QStringLiteral("upload_failed"),
                         apiMessageFromPayload(payload, errorString));
    });
}

void QuarkGatewayManager::ensureDirectoryBeforeDataUpload(
    const QString &remoteFilePath,
    const QByteArray &data,
    const QString &operationId,
    bool overwriteExistingFile)
{
    const int slashIndex = remoteFilePath.lastIndexOf(QLatin1Char('/'));
    const QString remoteDirectory = slashIndex > 0
                                        ? remoteFilePath.left(slashIndex)
                                        : QStringLiteral("/Quark/GameSaveCloudQt");

    QNetworkRequest request = apiRequest(QStringLiteral("/api/fs/mkdir"), m_apiToken);
    QJsonObject body;
    body.insert(QStringLiteral("path"), normalizedRemotePath(remoteDirectory));

    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, remoteFilePath, data, operationId, overwriteExistingFile]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        const int code = root.value(QStringLiteral("code")).toInt();
        const QString message = root.value(QStringLiteral("message")).toString();
        if (error == QNetworkReply::NoError
            && (code == 200
                || message.contains(QStringLiteral("exist"), Qt::CaseInsensitive)
                || message.contains(QStringLiteral("exists"), Qt::CaseInsensitive)
                || message.contains(QStringLiteral("已存在")))) {
            if (overwriteExistingFile) {
                removeDataFileBeforeOverwrite(remoteFilePath, data, operationId);
            } else {
                putDataFile(remoteFilePath, data, operationId);
            }
            return;
        }

        emit dataFileUploadFinished(false,
                                    remoteFilePath,
                                    operationId,
                                    apiMessageFromPayload(payload, errorString));
    });
}

void QuarkGatewayManager::putDataFile(
    const QString &remoteFilePath,
    const QByteArray &data,
    const QString &operationId)
{
    QBuffer *buffer = new QBuffer(this);
    buffer->setData(data);
    if (!buffer->open(QIODevice::ReadOnly)) {
        buffer->deleteLater();
        emit dataFileUploadFinished(false,
                                    remoteFilePath,
                                    operationId,
                                    QStringLiteral("无法打开内存中的云端索引内容"));
        return;
    }

    QNetworkRequest request = apiRequest(QStringLiteral("/api/fs/put"), m_apiToken);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("File-Path", QUrl::toPercentEncoding(remoteFilePath));
    request.setTransferTimeout(300000);

    QNetworkReply *reply = m_network.put(request, buffer);
    buffer->setParent(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, buffer, remoteFilePath, operationId]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        const QByteArray payload = reply->readAll();
        buffer->close();
        reply->deleteLater();

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        if (error == QNetworkReply::NoError && root.value(QStringLiteral("code")).toInt() == 200) {
            emit dataFileUploadFinished(true,
                                        remoteFilePath,
                                        operationId,
                                        QStringLiteral("云端索引已写入夸克网盘"));
            return;
        }

        emit dataFileUploadFinished(false,
                                    remoteFilePath,
                                    operationId,
                                    apiMessageFromPayload(payload, errorString));
    });
}

void QuarkGatewayManager::removeDataFileBeforeOverwrite(
    const QString &remoteFilePath,
    const QByteArray &data,
    const QString &operationId)
{
    const QString cleanRemotePath = normalizedRemotePath(remoteFilePath);
    const int slashIndex = cleanRemotePath.lastIndexOf(QLatin1Char('/'));
    const QString remoteDirectory = slashIndex > 0
                                        ? cleanRemotePath.left(slashIndex)
                                        : QStringLiteral("/Quark/GameSaveCloudQt");
    const QString fileName = QFileInfo(cleanRemotePath).fileName();

    if (fileName.trimmed().isEmpty()) {
        emit dataFileUploadFinished(false,
                                    cleanRemotePath,
                                    operationId,
                                    QStringLiteral("云端配置文件名为空，无法覆盖写入"));
        return;
    }

    QNetworkRequest request = apiRequest(QStringLiteral("/api/fs/remove"), m_apiToken);
    QJsonObject body;
    body.insert(QStringLiteral("dir"), normalizedRemotePath(remoteDirectory));
    body.insert(QStringLiteral("names"), QJsonArray{fileName});

    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, cleanRemotePath, data, operationId]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        const int code = root.value(QStringLiteral("code")).toInt();
        const QString message = root.value(QStringLiteral("message")).toString();
        const bool missingIsOk = message.contains(QStringLiteral("not exist"), Qt::CaseInsensitive)
                                 || message.contains(QStringLiteral("not found"), Qt::CaseInsensitive)
                                 || message.contains(QStringLiteral("不存在"));
        if (error == QNetworkReply::NoError && (code == 200 || missingIsOk)) {
            putDataFile(cleanRemotePath, data, operationId);
            return;
        }

        emit dataFileUploadFinished(false,
                                    cleanRemotePath,
                                    operationId,
                                    QStringLiteral("云端旧配置删除失败：%1")
                                        .arg(apiMessageFromPayload(payload, errorString)));
    });
}

void QuarkGatewayManager::downloadFromUrl(
    const QString &remoteFilePath,
    const QString &localFilePath,
    const QUrl &url,
    const QVariantMap &headers)
{
    downloadFromUrlCandidates(
        remoteFilePath,
        localFilePath,
        QList<QPair<QUrl, QVariantMap>>{QPair<QUrl, QVariantMap>(url, headers)});
}

void QuarkGatewayManager::startNextDownload()
{
    if (m_downloadInProgress || m_downloadQueue.isEmpty()) {
        return;
    }

    const QPair<QString, QString> next = m_downloadQueue.dequeue();
    m_downloadInProgress = true;
    performDownloadFile(next.first, next.second);
}

void QuarkGatewayManager::finishFileDownload(
    bool success,
    const QString &remoteFilePath,
    const QString &localFilePath,
    const QString &message)
{
    emit fileDownloadFinished(success, remoteFilePath, localFilePath, message);

    if (m_downloadInProgress) {
        m_downloadInProgress = false;
    }
    QTimer::singleShot(250, this, &QuarkGatewayManager::startNextDownload);
}

void QuarkGatewayManager::downloadFromUrlCandidates(
    const QString &remoteFilePath,
    const QString &localFilePath,
    const QList<QPair<QUrl, QVariantMap>> &candidates,
    int candidateIndex,
    const QString &previousError)
{
    if (candidateIndex < 0 || candidateIndex >= candidates.count()) {
        finishFileDownload(false,
                           remoteFilePath,
                           localFilePath,
                           previousError.trimmed().isEmpty()
                               ? QStringLiteral("快照下载失败：没有可用下载地址")
                               : previousError);
        return;
    }

    const QPair<QUrl, QVariantMap> candidate = candidates.at(candidateIndex);
    const QUrl url = candidate.first;
    const QVariantMap headers = candidate.second;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GameSaveCloudQt/0.1"));
    request.setRawHeader("Cache-Control", "no-cache");
    request.setRawHeader("Pragma", "no-cache");
    request.setRawHeader("Accept", "application/octet-stream,*/*");
    request.setTransferTimeout(300000);

    const bool isLocalOpenListUrl = url.host() == QStringLiteral("127.0.0.1")
                                    || url.host() == QStringLiteral("localhost");
    if (isLocalOpenListUrl && !m_adminPassword.isEmpty()) {
        const QByteArray credential = QStringLiteral("admin:%1").arg(m_adminPassword).toUtf8().toBase64();
        request.setRawHeader("Authorization", QByteArrayLiteral("Basic ") + credential);
    }
    applySafeDownloadHeaders(request, headers);

    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, remoteFilePath, localFilePath](qint64 bytesReceived, qint64 bytesTotal) {
                emit fileDownloadProgress(remoteFilePath, localFilePath, bytesReceived, bytesTotal);
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply, remoteFilePath, localFilePath, candidates, candidateIndex, previousError]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (error != QNetworkReply::NoError) {
            const QString scheme = reply->url().scheme();
            const QString host = reply->url().host();
            const QString path = reply->url().path();
            const QString currentError = QStringLiteral("候选%1失败[%2://%3%4]：%5")
                                             .arg(candidateIndex + 1)
                                             .arg(scheme, host, path, errorString);
            const QString combinedError = previousError.trimmed().isEmpty()
                                              ? QStringLiteral("快照下载失败：%1").arg(currentError)
                                              : QStringLiteral("%1；%2").arg(previousError, currentError);
            downloadFromUrlCandidates(remoteFilePath, localFilePath, candidates, candidateIndex + 1, combinedError);
            return;
        }

        const QFileInfo fileInfo(localFilePath);
        QDir dir(fileInfo.absolutePath());
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
            finishFileDownload(false,
                               remoteFilePath,
                               localFilePath,
                               QStringLiteral("无法创建本地下载目录"));
            return;
        }

        QFile file(localFilePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            finishFileDownload(false,
                               remoteFilePath,
                               localFilePath,
                               QStringLiteral("无法写入本地快照文件：%1").arg(file.errorString()));
            return;
        }

        const qint64 writtenBytes = file.write(payload);
        if (writtenBytes != payload.size() || file.error() != QFile::NoError) {
            finishFileDownload(false,
                               remoteFilePath,
                               localFilePath,
                               QStringLiteral("本地快照文件写入失败：%1").arg(file.errorString()));
            return;
        }

        if (!file.flush()) {
            const QString errorText = file.errorString();
            file.close();
            finishFileDownload(false,
                               remoteFilePath,
                               localFilePath,
                               QStringLiteral("本地快照文件刷新到磁盘失败：%1").arg(errorText));
            return;
        }
        file.close();

        finishFileDownload(true,
                           remoteFilePath,
                           localFilePath,
                           QStringLiteral("快照已通过 OpenList API 下载到本地快照目录"));
    });
}

QList<QPair<QUrl, QVariantMap>> QuarkGatewayManager::buildDownloadCandidates(
    const QString &remoteFilePath,
    const QJsonObject &fileData) const
{
    QVariantMap headers = fileData.value(QStringLiteral("header")).toObject().toVariantMap();
    if (headers.isEmpty()) {
        headers = fileData.value(QStringLiteral("headers")).toObject().toVariantMap();
    }

    QList<QPair<QUrl, QVariantMap>> candidates;
    const QString sign = fileData.value(QStringLiteral("sign")).toString();
    /*
     * 优先使用 OpenList 本机代理和 WebDAV，减少直接触碰夸克临时直链。
     * raw_url/url 只作为最后兜底，避免请求头或行为模式更像脚本下载器。
     */
    candidates.append(QPair<QUrl, QVariantMap>(proxiedDownloadUrl(remoteFilePath, sign, true), QVariantMap{}));
    candidates.append(QPair<QUrl, QVariantMap>(
        QUrl(gatewayBaseUrl() + QStringLiteral("/dav") + normalizedRemotePath(remoteFilePath)),
        QVariantMap{}));
    candidates.append(QPair<QUrl, QVariantMap>(proxiedDownloadUrl(remoteFilePath, sign), QVariantMap{}));

    QString rawUrl = fileData.value(QStringLiteral("raw_url")).toString();
    if (rawUrl.trimmed().isEmpty()) {
        rawUrl = fileData.value(QStringLiteral("url")).toString();
    }

    QUrl rawDownloadUrl(rawUrl);
    if (rawDownloadUrl.isRelative() && rawUrl.startsWith(QLatin1Char('/'))) {
        rawDownloadUrl = QUrl(gatewayBaseUrl() + rawUrl);
    }
    if (rawDownloadUrl.isValid() && !rawDownloadUrl.isEmpty()) {
        candidates.append(QPair<QUrl, QVariantMap>(rawDownloadUrl, headers));
    }

    return candidates;
}

void QuarkGatewayManager::applySafeDownloadHeaders(QNetworkRequest &request, const QVariantMap &headers) const
{
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        const QString key = it.key().trimmed();
        const QString value = it.value().toString();
        /*
         * OpenList 的本机代理会执行 HTTP 条件请求检查。某些上游直链 header
         * 如果携带 If-Match / If-None-Match / If-Modified-Since / If-Range 等条件语义，
         * 可能让 OpenList 在文件实际存在时返回 412 Precondition Failed。
         * 下载快照只需要完整读取 zip，因此这里跳过这些条件头，避免缓存或旧直链元数据干扰。
         */
        const QString lowerKey = key.toLower();
        const bool isConditionalHeader = lowerKey == QStringLiteral("if-match")
                                         || lowerKey == QStringLiteral("if-none-match")
                                         || lowerKey == QStringLiteral("if-modified-since")
                                         || lowerKey == QStringLiteral("if-unmodified-since")
                                         || lowerKey == QStringLiteral("if-range")
                                         || lowerKey == QStringLiteral("range");
        if (!key.isEmpty() && !value.isEmpty() && !isConditionalHeader) {
            request.setRawHeader(key.toUtf8(), value.toUtf8());
        }
    }
}

void QuarkGatewayManager::downloadDataFromUrl(
    const QString &remoteFilePath,
    const QString &operationId,
    const QUrl &url,
    const QVariantMap &headers,
    bool forceRefresh)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GameSaveCloudQt/0.1"));
    request.setTransferTimeout(300000);
    if (!m_adminPassword.isEmpty()) {
        const QByteArray credential = QStringLiteral("admin:%1").arg(m_adminPassword).toUtf8().toBase64();
        request.setRawHeader("Authorization", QByteArrayLiteral("Basic ") + credential);
    }
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        const QString key = it.key().trimmed();
        const QString value = it.value().toString();
        if (!key.isEmpty() && !value.isEmpty()) {
            request.setRawHeader(key.toUtf8(), value.toUtf8());
        }
    }

    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, remoteFilePath, operationId, forceRefresh]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (error != QNetworkReply::NoError) {
            downloadDataFromWebDav(
                remoteFilePath,
                operationId,
                QStringLiteral("云端索引下载失败：%1").arg(errorString),
                forceRefresh);
            return;
        }

        if (remoteFilePath.endsWith(QStringLiteral("/snapshot-manifest.json"))) {
            mergeSnapshotManifestWithDirectoryListing(
                remoteFilePath,
                operationId,
                payload,
                QStringLiteral("云端索引已读取"),
                forceRefresh);
            return;
        }

        emit dataFileDownloadFinished(true,
                                      remoteFilePath,
                                      operationId,
                                      payload,
                                      QStringLiteral("云端索引已读取"));
    });
}

void QuarkGatewayManager::downloadDataFromWebDav(
    const QString &remoteFilePath,
    const QString &operationId,
    const QString &firstErrorMessage,
    bool forceRefresh)
{
    QUrl url(gatewayBaseUrl() + QStringLiteral("/dav") + normalizedRemotePath(remoteFilePath));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GameSaveCloudQt/0.1"));
    request.setTransferTimeout(300000);
    if (!m_adminPassword.isEmpty()) {
        const QByteArray credential = QStringLiteral("admin:%1").arg(m_adminPassword).toUtf8().toBase64();
        request.setRawHeader("Authorization", QByteArrayLiteral("Basic ") + credential);
    }

    /*
     * OpenList/夸克对 /d/<path>?sign=... 直链偶尔会返回 Forbidden 或 Precondition Failed。
     * 索引文件是软件自己写入的小 JSON，使用本机 WebDAV 读取同一路径更稳定；
     * 只有 WebDAV 兜底也失败时，才把两个错误一起返回，方便日志定位。
     */
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, remoteFilePath, operationId, firstErrorMessage, forceRefresh]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (error != QNetworkReply::NoError) {
            if (remoteFilePath.endsWith(QStringLiteral("/snapshot-manifest.json"))) {
                downloadSnapshotManifestFromDirectoryListing(
                    remoteFilePath,
                    operationId,
                    QStringLiteral("%1；WebDAV 兜底读取也失败：%2")
                        .arg(firstErrorMessage, errorString),
                    forceRefresh);
                return;
            }

            emit dataFileDownloadFinished(false,
                                          remoteFilePath,
                                          operationId,
                                          {},
                                          QStringLiteral("%1；WebDAV 兜底读取也失败：%2")
                                              .arg(firstErrorMessage, errorString));
            return;
        }

        if (remoteFilePath.endsWith(QStringLiteral("/snapshot-manifest.json"))) {
            mergeSnapshotManifestWithDirectoryListing(
                remoteFilePath,
                operationId,
                payload,
                QStringLiteral("云端索引已通过 WebDAV 兜底读取"),
                forceRefresh);
            return;
        }

        emit dataFileDownloadFinished(true,
                                      remoteFilePath,
                                      operationId,
                                      payload,
                                      QStringLiteral("云端索引已通过 WebDAV 兜底读取"));
    });
}

void QuarkGatewayManager::mergeSnapshotManifestWithDirectoryListing(
    const QString &remoteFilePath,
    const QString &operationId,
    const QByteArray &manifestData,
    const QString &readMessage,
    bool forceRefresh)
{
    const QString cleanManifestPath = normalizedRemotePath(remoteFilePath);
    const int slashIndex = cleanManifestPath.lastIndexOf(QLatin1Char('/'));
    const QString remoteDirectory = slashIndex > 0
                                        ? cleanManifestPath.left(slashIndex)
                                        : QStringLiteral("/Quark/GameSaveCloudQt");

    QNetworkRequest request = apiRequest(QStringLiteral("/api/fs/list"), m_apiToken);
    QJsonObject body;
    body.insert(QStringLiteral("path"), remoteDirectory);
    body.insert(QStringLiteral("password"), QString());
    body.insert(QStringLiteral("page"), 1);
    body.insert(QStringLiteral("per_page"), 500);
    body.insert(QStringLiteral("refresh"), forceRefresh);

    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, cleanManifestPath, remoteDirectory, operationId, manifestData, readMessage]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        if (error != QNetworkReply::NoError || root.value(QStringLiteral("code")).toInt() != 200) {
            emit dataFileDownloadFinished(true,
                                          cleanManifestPath,
                                          operationId,
                                          manifestData,
                                          QStringLiteral("%1；目录刷新合并失败，暂按索引文件显示：%2")
                                              .arg(readMessage, apiMessageFromPayload(payload, errorString)));
            return;
        }

        QJsonObject manifest = QJsonDocument::fromJson(manifestData).object();
        if (manifest.isEmpty()) {
            manifest.insert(QStringLiteral("schemaVersion"), 1);
        }

        const QJsonObject data = root.value(QStringLiteral("data")).toObject();
        QJsonArray content = data.value(QStringLiteral("content")).toArray();
        if (content.isEmpty()) {
            content = data.value(QStringLiteral("items")).toArray();
        }

        QSet<QString> directoryFileKeys;
        QHash<QString, QJsonObject> directoryItemsByFileKey;
        for (const QJsonValue &value : content) {
            const QJsonObject item = value.toObject();
            QString fileName = item.value(QStringLiteral("name")).toString().trimmed();
            if (fileName.isEmpty()) {
                fileName = item.value(QStringLiteral("filename")).toString().trimmed();
            }
            if (!fileName.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
                continue;
            }

            const QString fileKey = fileName.toCaseFolded();
            if (directoryFileKeys.contains(fileKey)) {
                continue;
            }
            directoryFileKeys.insert(fileKey);
            directoryItemsByFileKey.insert(fileKey, item);
        }

        QSet<QString> seenFileNames;
        QList<QJsonObject> mergedSnapshots;
        const QJsonArray manifestSnapshots = manifest.value(QStringLiteral("snapshots")).toArray();
        for (const QJsonValue &value : manifestSnapshots) {
            QJsonObject snapshot = value.toObject();
            QString fileName = snapshot.value(QStringLiteral("fileName")).toString().trimmed();
            QString remotePath = snapshot.value(QStringLiteral("remotePath")).toString().trimmed();
            if (fileName.isEmpty() && !remotePath.isEmpty()) {
                fileName = QFileInfo(remotePath).fileName();
                snapshot.insert(QStringLiteral("fileName"), fileName);
            }
            if (remotePath.isEmpty() && !fileName.isEmpty()) {
                remotePath = remoteDirectory + QLatin1Char('/') + fileName;
                snapshot.insert(QStringLiteral("remotePath"), remotePath);
            }
            if (fileName.isEmpty() || !fileName.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
                continue;
            }

            const QString fileKey = fileName.toCaseFolded();
            if (!directoryFileKeys.contains(fileKey) || seenFileNames.contains(fileKey)) {
                continue;
            }
            seenFileNames.insert(fileKey);
            snapshot.insert(QStringLiteral("remotePath"), remoteDirectory + QLatin1Char('/') + fileName);
            mergedSnapshots.append(snapshot);
        }

        for (const QString &fileKey : std::as_const(directoryFileKeys)) {
            if (seenFileNames.contains(fileKey)) {
                continue;
            }
            seenFileNames.insert(fileKey);

            const QJsonObject item = directoryItemsByFileKey.value(fileKey);
            QString fileName = item.value(QStringLiteral("name")).toString().trimmed();
            if (fileName.isEmpty()) {
                fileName = item.value(QStringLiteral("filename")).toString().trimmed();
            }
            QJsonObject snapshot;
            snapshot.insert(QStringLiteral("fileName"), fileName);
            snapshot.insert(QStringLiteral("remotePath"), remoteDirectory + QLatin1Char('/') + fileName);
            snapshot.insert(QStringLiteral("uploadState"), QStringLiteral("cloud_directory_listing"));
            snapshot.insert(QStringLiteral("remoteVerifiedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

            const QJsonValue sizeValue = item.value(QStringLiteral("size"));
            if (sizeValue.isDouble()) {
                snapshot.insert(QStringLiteral("zipSize"), QString::number(static_cast<qint64>(sizeValue.toDouble())));
            }
            mergedSnapshots.append(snapshot);
        }

        std::sort(mergedSnapshots.begin(), mergedSnapshots.end(), [](const QJsonObject &left, const QJsonObject &right) {
            return left.value(QStringLiteral("fileName")).toString()
                > right.value(QStringLiteral("fileName")).toString();
        });

        QJsonArray snapshots;
        for (const QJsonObject &snapshot : mergedSnapshots) {
            snapshots.append(snapshot);
        }

        manifest.insert(QStringLiteral("remoteDirectory"), remoteDirectory);
        manifest.insert(QStringLiteral("updatedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        manifest.insert(QStringLiteral("snapshotCount"), snapshots.count());
        manifest.insert(QStringLiteral("snapshots"), snapshots);
        if (!mergedSnapshots.isEmpty()) {
            const QJsonObject latest = mergedSnapshots.first();
            manifest.insert(QStringLiteral("latestSnapshotFileName"), latest.value(QStringLiteral("fileName")).toString());
            manifest.insert(QStringLiteral("latestRemotePath"), latest.value(QStringLiteral("remotePath")).toString());
        }

        emit dataFileDownloadFinished(true,
                                      cleanManifestPath,
                                      operationId,
                                      QJsonDocument(manifest).toJson(QJsonDocument::Compact),
                                      QStringLiteral("%1，并已合并云端目录中的 %2 个 zip 快照")
                                          .arg(readMessage)
                                          .arg(snapshots.count()));
    });
}

void QuarkGatewayManager::downloadSnapshotManifestFromDirectoryListing(
    const QString &remoteFilePath,
    const QString &operationId,
    const QString &firstErrorMessage,
    bool forceRefresh)
{
    const QString cleanManifestPath = normalizedRemotePath(remoteFilePath);
    const int slashIndex = cleanManifestPath.lastIndexOf(QLatin1Char('/'));
    const QString remoteDirectory = slashIndex > 0
                                        ? cleanManifestPath.left(slashIndex)
                                        : QStringLiteral("/Quark/GameSaveCloudQt");

    QNetworkRequest request = apiRequest(QStringLiteral("/api/fs/list"), m_apiToken);
    QJsonObject body;
    body.insert(QStringLiteral("path"), remoteDirectory);
    body.insert(QStringLiteral("password"), QString());
    body.insert(QStringLiteral("page"), 1);
    body.insert(QStringLiteral("per_page"), 500);
    body.insert(QStringLiteral("refresh"), forceRefresh);

    /*
     * 如果夸克/OpenList 拒绝读取 snapshot-manifest.json 的文件内容，
     * 仍然可以通过目录列表看到云端 zip。这里临时拼出同结构 manifest，
     * 让上层下载逻辑继续工作，不因为索引文件 403 而卡死。
     */
    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, remoteFilePath, operationId, remoteDirectory, firstErrorMessage]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        if (error != QNetworkReply::NoError || root.value(QStringLiteral("code")).toInt() != 200) {
            emit dataFileDownloadFinished(false,
                                          remoteFilePath,
                                          operationId,
                                          {},
                                          QStringLiteral("%1；目录兜底读取也失败：%2")
                                              .arg(firstErrorMessage, apiMessageFromPayload(payload, errorString)));
            return;
        }

        const QJsonObject data = root.value(QStringLiteral("data")).toObject();
        QJsonArray content = data.value(QStringLiteral("content")).toArray();
        if (content.isEmpty()) {
            content = data.value(QStringLiteral("items")).toArray();
        }

        QJsonArray snapshots;
        for (const QJsonValue &value : content) {
            const QJsonObject item = value.toObject();
            QString fileName = item.value(QStringLiteral("name")).toString();
            if (fileName.trimmed().isEmpty()) {
                fileName = item.value(QStringLiteral("filename")).toString();
            }
            if (!fileName.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
                continue;
            }

            QJsonObject snapshot;
            snapshot.insert(QStringLiteral("fileName"), fileName);
            snapshot.insert(QStringLiteral("remotePath"), remoteDirectory + QLatin1Char('/') + fileName);
            snapshot.insert(QStringLiteral("uploadState"), QStringLiteral("cloud_directory_listing"));
            snapshot.insert(QStringLiteral("remoteVerifiedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

            const QJsonValue sizeValue = item.value(QStringLiteral("size"));
            if (sizeValue.isDouble()) {
                snapshot.insert(QStringLiteral("zipSize"), QString::number(static_cast<qint64>(sizeValue.toDouble())));
            }
            snapshots.append(snapshot);
        }

        QJsonObject manifest;
        manifest.insert(QStringLiteral("schemaVersion"), 1);
        manifest.insert(QStringLiteral("remoteDirectory"), remoteDirectory);
        manifest.insert(QStringLiteral("updatedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        manifest.insert(QStringLiteral("snapshotCount"), snapshots.count());
        manifest.insert(QStringLiteral("snapshots"), snapshots);

        emit dataFileDownloadFinished(true,
                                      remoteFilePath,
                                      operationId,
                                      QJsonDocument(manifest).toJson(QJsonDocument::Compact),
                                      QStringLiteral("云端索引文件无法读取，已通过目录列表生成临时快照索引"));
    });
}

QUrl QuarkGatewayManager::proxiedDownloadUrl(
    const QString &remoteFilePath,
    const QString &sign,
    bool forceLocalProxy) const
{
    QUrl url(gatewayBaseUrl());
    url.setPath((forceLocalProxy ? QStringLiteral("/p") : QStringLiteral("/d"))
                + normalizedRemotePath(remoteFilePath));
    if (forceLocalProxy || !sign.trimmed().isEmpty()) {
        QUrlQuery query;
        if (forceLocalProxy) {
            /*
             * OpenList 的 /p 端点默认可能继续跳转到下游代理地址；
             * 加 d 参数可以强制由本机 OpenList 代理输出文件内容。
             * 对夸克驱动来说，这比把用户暴露到下游临时直链更稳定。
             */
            query.addQueryItem(QStringLiteral("d"), QString());
        }
        if (!sign.trimmed().isEmpty()) {
            query.addQueryItem(QStringLiteral("sign"), sign.trimmed());
        }
        url.setQuery(query);
    }
    return url;
}

QNetworkRequest QuarkGatewayManager::apiRequest(const QString &path, const QString &token) const
{
    QNetworkRequest request(QUrl(gatewayBaseUrl() + path));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GameSaveCloudQt/0.1"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Cache-Control", "no-cache");
    request.setRawHeader("Pragma", "no-cache");
    request.setTransferTimeout(12000);
    if (!token.isEmpty()) {
        request.setRawHeader("Authorization", token.toUtf8());
    }
    return request;
}

QString QuarkGatewayManager::normalizedRemotePath(const QString &remotePath) const
{
    QString cleanPath = remotePath.trimmed();
    cleanPath.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (cleanPath.isEmpty()) {
        cleanPath = QStringLiteral("/Quark/GameSaveCloudQt");
    }
    if (!cleanPath.startsWith(QLatin1Char('/'))) {
        cleanPath.prepend(QLatin1Char('/'));
    }
    while (cleanPath.length() > 1 && cleanPath.endsWith(QLatin1Char('/'))) {
        cleanPath.chop(1);
    }
    return cleanPath;
}

QString QuarkGatewayManager::apiMessageFromPayload(const QByteArray &payload, const QString &fallback) const
{
    const QJsonObject root = QJsonDocument::fromJson(payload).object();
    const QString message = root.value(QStringLiteral("message")).toString();
    if (!message.trimmed().isEmpty()) {
        return QStringLiteral("OpenList API 请求失败：%1").arg(message);
    }

    if (!fallback.trimmed().isEmpty()) {
        return QStringLiteral("OpenList API 请求失败：%1").arg(fallback);
    }

    return QStringLiteral("OpenList API 请求失败：未返回明确错误信息");
}

QByteArray QuarkGatewayManager::storagePayload(int existingStorageId) const
{
    QJsonObject body;
    if (existingStorageId > 0) {
        body.insert(QStringLiteral("id"), existingStorageId);
    }
    body.insert(QStringLiteral("mount_path"), QStringLiteral("/Quark"));
    body.insert(QStringLiteral("order"), 0);
    body.insert(QStringLiteral("remark"), QStringLiteral("GameSaveCloud Quark"));
    /*
     * 普通读取允许使用 OpenList 短缓存，减少对夸克接口的强制刷新。
     * 上传前存在性检查、用户手动刷新等需要强一致的路径会单独传 refresh=true。
     */
    body.insert(QStringLiteral("cache_expiration"), 300);
    body.insert(QStringLiteral("web_proxy"), true);
    body.insert(QStringLiteral("webdav_policy"), QStringLiteral("native_proxy"));
    body.insert(QStringLiteral("down_proxy_url"), QString());
    body.insert(QStringLiteral("extract_folder"), QStringLiteral("front"));
    body.insert(QStringLiteral("enable_sign"), true);
    body.insert(QStringLiteral("driver"), QStringLiteral("Quark"));
    body.insert(QStringLiteral("order_by"), QStringLiteral("name"));
    body.insert(QStringLiteral("order_direction"), QStringLiteral("asc"));
    body.insert(QStringLiteral("addition"), QString::fromUtf8(quarkAdditionJson()));
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QByteArray QuarkGatewayManager::quarkAdditionJson() const
{
    QJsonObject addition;
    addition.insert(QStringLiteral("cookie"), m_cookie);
    addition.insert(QStringLiteral("root_folder_id"), QStringLiteral("0"));
    addition.insert(QStringLiteral("order_by"), QStringLiteral("name"));
    addition.insert(QStringLiteral("order_direction"), QStringLiteral("asc"));
    addition.insert(QStringLiteral("use_transcoding_address"), false);
    return QJsonDocument(addition).toJson(QJsonDocument::Compact);
}

void QuarkGatewayManager::finishFailure(const QString &message)
{
    m_loginPollTimer.stop();
    m_configuring = false;
    emit gatewayFailed(message);
}
