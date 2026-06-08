#include "QuarkGatewayManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QAuthenticator>
#include <QBuffer>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>

#include <memory>

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

void QuarkGatewayManager::startAndConfigure(const QString &cookie)
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
    m_loginAttempts = 0;
    m_mountConflictRetries = 0;
    m_adminPassword = loadOrCreateAdminPassword();

    QDir dataDir(dataDirectoryPath());
    if (!dataDir.exists() && !dataDir.mkpath(QStringLiteral("."))) {
        finishFailure(QStringLiteral("无法创建 OpenList 数据目录：%1").arg(dataDirectoryPath()));
        return;
    }

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
    const QString remoteFilePath = remoteDirectory + QLatin1Char('/') + fileInfo.fileName();
    checkRemoteFileBeforeUpload(localFilePath, remoteFilePath);
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
    const QString remoteFilePath = remoteDirectory + QLatin1Char('/') + fileInfo.fileName();
    ensureDirectoryBeforeUpload(
        localFilePath,
        remoteDirectory,
        remoteFilePath,
        QStringLiteral("overwritten"),
        QStringLiteral("快照已通过 OpenList API 覆盖上传到夸克网盘"));
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

    QNetworkRequest request = apiRequest(QStringLiteral("/api/fs/get"), m_apiToken);
    QJsonObject body;
    body.insert(QStringLiteral("path"), cleanRemotePath);
    body.insert(QStringLiteral("password"), QString());
    body.insert(QStringLiteral("refresh"), true);

    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, cleanRemotePath, localFilePath]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        if (error != QNetworkReply::NoError || root.value(QStringLiteral("code")).toInt() != 200) {
            emit fileDownloadFinished(false,
                                      cleanRemotePath,
                                      localFilePath,
                                      apiMessageFromPayload(payload, errorString));
            return;
        }

        const QJsonObject data = root.value(QStringLiteral("data")).toObject();
        QVariantMap headers = data.value(QStringLiteral("header")).toObject().toVariantMap();
        if (headers.isEmpty()) {
            headers = data.value(QStringLiteral("headers")).toObject().toVariantMap();
        }

        const QString sign = data.value(QStringLiteral("sign")).toString();
        QUrl downloadUrl = proxiedDownloadUrl(cleanRemotePath, sign);
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

        downloadFromUrl(cleanRemotePath, localFilePath, downloadUrl, headers);
    });
}

void QuarkGatewayManager::downloadDataFile(const QString &remoteFilePath, const QString &operationId)
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
    body.insert(QStringLiteral("refresh"), true);

    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, cleanRemotePath, operationId]() {
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
        QUrl downloadUrl = proxiedDownloadUrl(cleanRemotePath, sign);
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

        downloadDataFromUrl(cleanRemotePath, operationId, downloadUrl, headers);
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
    return QDir::toNativeSeparators(QDir(engineDirectoryPath()).absoluteFilePath(QStringLiteral("data")));
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

QString QuarkGatewayManager::configFilePath() const
{
    return QDir::toNativeSeparators(QDir(dataDirectoryPath()).absoluteFilePath(QStringLiteral("config.json")));
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
        QStringLiteral("--config"),
        configFilePath()
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
        QStringLiteral("--config"),
        configFilePath()
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

        int existingStorageId = 0;
        if (error == QNetworkReply::NoError) {
            existingStorageId = storageIdFromListPayload(payload);
        }

        createOrUpdateStorage(token, existingStorageId);
    });
}

int QuarkGatewayManager::storageIdFromListPayload(const QByteArray &payload) const
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
            QString mountPath = item.value(QStringLiteral("mount_path")).toString().trimmed();
            if (!mountPath.startsWith(QLatin1Char('/'))) {
                mountPath.prepend(QLatin1Char('/'));
            }

            if (mountPath.compare(QStringLiteral("/Quark"), Qt::CaseInsensitive) == 0) {
                return item.value(QStringLiteral("id")).toInt();
            }
        }
    }

    return 0;
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

        emit fileUploadFinished(false,
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
            emit fileUploadFinished(true,
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

        emit fileUploadFinished(false,
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
        emit fileUploadFinished(false,
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

    connect(reply, &QNetworkReply::finished, this, [this, reply, file, localFilePath, remoteFilePath, uploadState, successMessage]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        const QByteArray payload = reply->readAll();
        file->close();
        reply->deleteLater();

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        if (error == QNetworkReply::NoError && root.value(QStringLiteral("code")).toInt() == 200) {
            emit fileUploadFinished(true,
                                    localFilePath,
                                    remoteFilePath,
                                    uploadState,
                                    successMessage);
            return;
        }

        emit fileUploadFinished(false,
                                localFilePath,
                                remoteFilePath,
                                QStringLiteral("upload_failed"),
                                apiMessageFromPayload(payload, errorString));
    });
}

void QuarkGatewayManager::ensureDirectoryBeforeDataUpload(
    const QString &remoteFilePath,
    const QByteArray &data,
    const QString &operationId)
{
    const int slashIndex = remoteFilePath.lastIndexOf(QLatin1Char('/'));
    const QString remoteDirectory = slashIndex > 0
                                        ? remoteFilePath.left(slashIndex)
                                        : QStringLiteral("/Quark/GameSaveCloudQt");

    QNetworkRequest request = apiRequest(QStringLiteral("/api/fs/mkdir"), m_apiToken);
    QJsonObject body;
    body.insert(QStringLiteral("path"), normalizedRemotePath(remoteDirectory));

    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, remoteFilePath, data, operationId]() {
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
            putDataFile(remoteFilePath, data, operationId);
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

void QuarkGatewayManager::downloadFromUrl(
    const QString &remoteFilePath,
    const QString &localFilePath,
    const QUrl &url,
    const QVariantMap &headers)
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
    connect(reply, &QNetworkReply::finished, this, [this, reply, remoteFilePath, localFilePath]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (error != QNetworkReply::NoError) {
            emit fileDownloadFinished(false,
                                      remoteFilePath,
                                      localFilePath,
                                      QStringLiteral("快照下载失败：%1").arg(errorString));
            return;
        }

        const QFileInfo fileInfo(localFilePath);
        QDir dir(fileInfo.absolutePath());
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
            emit fileDownloadFinished(false,
                                      remoteFilePath,
                                      localFilePath,
                                      QStringLiteral("无法创建本地下载目录"));
            return;
        }

        QFile file(localFilePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit fileDownloadFinished(false,
                                      remoteFilePath,
                                      localFilePath,
                                      QStringLiteral("无法写入本地快照文件：%1").arg(file.errorString()));
            return;
        }

        file.write(payload);
        if (file.error() != QFile::NoError) {
            emit fileDownloadFinished(false,
                                      remoteFilePath,
                                      localFilePath,
                                      QStringLiteral("本地快照文件写入失败：%1").arg(file.errorString()));
            return;
        }

        emit fileDownloadFinished(true,
                                  remoteFilePath,
                                  localFilePath,
                                  QStringLiteral("快照已通过 OpenList API 下载到本地快照目录"));
    });
}

void QuarkGatewayManager::downloadDataFromUrl(
    const QString &remoteFilePath,
    const QString &operationId,
    const QUrl &url,
    const QVariantMap &headers)
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
    connect(reply, &QNetworkReply::finished, this, [this, reply, remoteFilePath, operationId]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (error != QNetworkReply::NoError) {
            emit dataFileDownloadFinished(false,
                                          remoteFilePath,
                                          operationId,
                                          {},
                                          QStringLiteral("云端索引下载失败：%1").arg(errorString));
            return;
        }

        emit dataFileDownloadFinished(true,
                                      remoteFilePath,
                                      operationId,
                                      payload,
                                      QStringLiteral("云端索引已读取"));
    });
}

QUrl QuarkGatewayManager::proxiedDownloadUrl(const QString &remoteFilePath, const QString &sign) const
{
    QUrl url(gatewayBaseUrl());
    url.setPath(QStringLiteral("/d") + normalizedRemotePath(remoteFilePath));
    if (!sign.trimmed().isEmpty()) {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("sign"), sign.trimmed());
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
     * 这里偏向同步正确性而不是目录浏览性能。
     * 存档快照可能被用户在夸克网页端手动删除，缓存时间过长会导致
     * OpenList 仍返回旧文件，进而让上传逻辑误判“云端已存在”。
     */
    body.insert(QStringLiteral("cache_expiration"), 0);
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
