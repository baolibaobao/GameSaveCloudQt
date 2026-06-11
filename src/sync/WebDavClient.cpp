#include "WebDavClient.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>

WebDavClient::WebDavClient(QObject *parent)
    : QObject(parent)
{
}

void WebDavClient::testConnection(const WebDavConfig &config)
{
    if (m_isTesting) {
        emit connectionTestFinished(false, QStringLiteral("正在测试 WebDAV 连接，请等待当前请求完成"));
        return;
    }

    if (!config.isValid()) {
        emit connectionTestFinished(false, QStringLiteral("WebDAV 配置不完整，请填写服务器地址和远程目录"));
        return;
    }

    m_isTesting = true;

    /*
     * WebDAV 的目录检测通常使用 PROPFIND：
     * - Depth: 0 表示只询问当前路径本身，不列出子文件。
     * - 207 Multi-Status 是最常见的成功状态，部分服务器也会返回 200。
     * - 404 表示远程根目录不存在，随后用 MKCOL 创建目录。
     */
    const QUrl url = remoteUrl(config, config.remoteRootPath);
    QNetworkRequest request = makeRequest(config, url);
    request.setRawHeader("Depth", "0");

    const QByteArray body = QByteArrayLiteral(
        R"(<?xml version="1.0" encoding="utf-8"?>)"
        R"(<d:propfind xmlns:d="DAV:"><d:prop><d:resourcetype/></d:prop></d:propfind>)");
    QNetworkReply *reply = m_network.sendCustomRequest(request, QByteArrayLiteral("PROPFIND"), body);

    connect(reply, &QNetworkReply::finished, this, [this, reply, config]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError error = reply->error();
        const QString statusText = httpStatusText(reply);
        reply->deleteLater();

        if (error == QNetworkReply::NoError && (statusCode == 200 || statusCode == 207)) {
            m_isTesting = false;
            emit connectionTestFinished(true, QStringLiteral("WebDAV 连接成功，远程目录可访问"));
            return;
        }

        if (statusCode == 404) {
            ensureRemoteRootDirectory(config);
            return;
        }

        m_isTesting = false;
        emit connectionTestFinished(false, QStringLiteral("WebDAV 连接失败：%1").arg(statusText));
    });
}

void WebDavClient::uploadFileIfMissing(
    const WebDavConfig &config,
    const QString &localFilePath,
    const QString &remoteDirectoryPath)
{
    const QFileInfo fileInfo(localFilePath);
    if (!config.isValid()) {
        emit fileUploadFinished(false, localFilePath, QString(), QStringLiteral("config_invalid"),
                                QStringLiteral("WebDAV 配置不完整，无法上传快照"));
        return;
    }

    if (!fileInfo.exists() || !fileInfo.isFile()) {
        emit fileUploadFinished(false, localFilePath, QString(), QStringLiteral("local_file_missing"),
                                QStringLiteral("本地快照文件不存在，无法上传"));
        return;
    }

    const QString remoteDirectory = normalizedRemotePath(remoteDirectoryPath);
    const QString remoteFilePath = remoteDirectory + QLatin1Char('/') + fileInfo.fileName();
    QNetworkRequest request = makeRequest(config, remoteUrl(config, remoteFilePath));
    request.setRawHeader("Depth", "0");

    /*
     * 上传前必须先访问云端确认同名 zip 是否存在。
     * 不能只相信本地 manifest，因为用户可能在网盘里手动删除了文件。
     */
    const QByteArray body = QByteArrayLiteral(
        R"(<?xml version="1.0" encoding="utf-8"?>)"
        R"(<d:propfind xmlns:d="DAV:"><d:prop><d:getcontentlength/></d:prop></d:propfind>)");
    QNetworkReply *reply = m_network.sendCustomRequest(request, QByteArrayLiteral("PROPFIND"), body);

    connect(reply, &QNetworkReply::finished, this, [this, reply, config, localFilePath, remoteDirectory, remoteFilePath]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError error = reply->error();
        const QString statusText = httpStatusText(reply);
        reply->deleteLater();

        if (error == QNetworkReply::NoError && (statusCode == 200 || statusCode == 207)) {
            emit fileUploadFinished(true,
                                    localFilePath,
                                    remoteFilePath,
                                    QStringLiteral("remote_exists"),
                                    QStringLiteral("云端已存在同名快照，已跳过上传"));
            return;
        }

        if (statusCode == 404) {
            createRemoteDirectoriesForUpload(
                config,
                directoryHierarchy(remoteDirectory),
                0,
                localFilePath,
                remoteFilePath);
            return;
        }

        emit fileUploadFinished(false,
                                localFilePath,
                                remoteFilePath,
                                QStringLiteral("remote_check_failed"),
                                QStringLiteral("检查云端快照是否存在失败：%1").arg(statusText));
    });
}

void WebDavClient::uploadFileOverwrite(
    const WebDavConfig &config,
    const QString &localFilePath,
    const QString &remoteDirectoryPath)
{
    const QFileInfo fileInfo(localFilePath);
    if (!config.isValid()) {
        emit fileUploadFinished(false, localFilePath, QString(), QStringLiteral("config_invalid"),
                                QStringLiteral("WebDAV 配置不完整，无法覆盖上传快照"));
        return;
    }

    if (!fileInfo.exists() || !fileInfo.isFile()) {
        emit fileUploadFinished(false, localFilePath, QString(), QStringLiteral("local_file_missing"),
                                QStringLiteral("本地快照文件不存在，无法覆盖上传"));
        return;
    }

    const QString remoteDirectory = normalizedRemotePath(remoteDirectoryPath);
    const QString remoteFilePath = remoteDirectory + QLatin1Char('/') + fileInfo.fileName();
    createRemoteDirectoriesForUpload(
        config,
        directoryHierarchy(remoteDirectory),
        0,
        localFilePath,
        remoteFilePath,
        QStringLiteral("overwritten"),
        QStringLiteral("快照已覆盖上传到 WebDAV 云端"));
}

void WebDavClient::downloadFile(
    const WebDavConfig &config,
    const QString &remoteFilePath,
    const QString &localFilePath)
{
    if (!config.isValid()) {
        emit fileDownloadFinished(false, remoteFilePath, localFilePath,
                                  QStringLiteral("WebDAV 配置不完整，无法下载快照"));
        return;
    }

    if (remoteFilePath.trimmed().isEmpty() || localFilePath.trimmed().isEmpty()) {
        emit fileDownloadFinished(false, remoteFilePath, localFilePath,
                                  QStringLiteral("云端路径或本地保存路径为空，无法下载快照"));
        return;
    }

    QNetworkReply *reply = m_network.get(makeRequest(config, remoteUrl(config, remoteFilePath)));
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, remoteFilePath, localFilePath](qint64 bytesReceived, qint64 bytesTotal) {
                emit fileDownloadProgress(remoteFilePath, localFilePath, bytesReceived, bytesTotal);
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply, remoteFilePath, localFilePath]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QString statusText = httpStatusText(reply);
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (error != QNetworkReply::NoError) {
            emit fileDownloadFinished(false, remoteFilePath, localFilePath,
                                      QStringLiteral("快照下载失败：%1").arg(statusText));
            return;
        }

        const QFileInfo fileInfo(localFilePath);
        QDir dir(fileInfo.absolutePath());
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
            emit fileDownloadFinished(false, remoteFilePath, localFilePath,
                                      QStringLiteral("无法创建本地下载目录"));
            return;
        }

        QFile file(localFilePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit fileDownloadFinished(false, remoteFilePath, localFilePath,
                                      QStringLiteral("无法写入本地快照文件：%1").arg(file.errorString()));
            return;
        }

        const qint64 writtenBytes = file.write(payload);
        if (writtenBytes != payload.size() || file.error() != QFile::NoError) {
            emit fileDownloadFinished(false, remoteFilePath, localFilePath,
                                      QStringLiteral("本地快照文件写入失败：%1").arg(file.errorString()));
            return;
        }

        if (!file.flush()) {
            const QString errorText = file.errorString();
            file.close();
            emit fileDownloadFinished(false, remoteFilePath, localFilePath,
                                      QStringLiteral("本地快照文件刷新到磁盘失败：%1").arg(errorText));
            return;
        }
        file.close();

        emit fileDownloadFinished(true, remoteFilePath, localFilePath,
                                  QStringLiteral("快照已下载到本地快照目录"));
    });
}

QNetworkRequest WebDavClient::makeRequest(const WebDavConfig &config, const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GameSaveCloudQt/0.1"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/xml; charset=utf-8"));
    request.setTransferTimeout(15000);

    /*
     * WebDAV 常见认证方式是 HTTP Basic。
     * 日志系统只记录连接结果，不输出 Authorization 或密码，避免泄露账号信息。
     */
    if (!config.username.isEmpty() || !config.password.isEmpty()) {
        const QByteArray credential = QStringLiteral("%1:%2")
                                          .arg(config.username, config.password)
                                          .toUtf8()
                                          .toBase64();
        request.setRawHeader("Authorization", QByteArrayLiteral("Basic ") + credential);
    }

    return request;
}

QUrl WebDavClient::remoteUrl(const WebDavConfig &config, const QString &remotePath) const
{
    QUrl url(sanitizedBaseUrl(config.serverUrl));
    QString basePath = url.path();
    while (basePath.endsWith(QLatin1Char('/'))) {
        basePath.chop(1);
    }

    url.setPath(basePath + normalizedRemotePath(remotePath));
    return url;
}

QString WebDavClient::sanitizedBaseUrl(const QString &serverUrl) const
{
    QString cleanUrl = serverUrl.trimmed();
    while (cleanUrl.endsWith(QLatin1Char('/'))) {
        cleanUrl.chop(1);
    }

    return cleanUrl;
}

QString WebDavClient::normalizedRemotePath(const QString &remotePath) const
{
    QString cleanPath = remotePath.trimmed();
    cleanPath.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (cleanPath.isEmpty()) {
        cleanPath = QStringLiteral("/GameSaveCloudQt");
    }

    if (!cleanPath.startsWith(QLatin1Char('/'))) {
        cleanPath.prepend(QLatin1Char('/'));
    }

    while (cleanPath.length() > 1 && cleanPath.endsWith(QLatin1Char('/'))) {
        cleanPath.chop(1);
    }

    return cleanPath;
}

QString WebDavClient::httpStatusText(QNetworkReply *reply) const
{
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    if (statusCode > 0 && !reason.isEmpty()) {
        return QStringLiteral("HTTP %1 %2").arg(statusCode).arg(reason);
    }

    if (statusCode > 0) {
        return QStringLiteral("HTTP %1").arg(statusCode);
    }

    return reply->errorString();
}

void WebDavClient::ensureRemoteRootDirectory(const WebDavConfig &config)
{
    /*
     * MKCOL 用于创建 WebDAV 目录。很多服务器在目录已存在时会返回 405，
     * 但这里是在 PROPFIND 返回 404 后才调用，因此 201 是最标准的成功结果。
     * 仍然兼容 200/204/405，避免不同 WebDAV 实现差异导致误判。
     */
    const QStringList directoryPaths = directoryHierarchy(config.remoteRootPath);

    if (directoryPaths.isEmpty()) {
        m_isTesting = false;
        emit connectionTestFinished(true, QStringLiteral("WebDAV 连接成功，远程根目录可访问"));
        return;
    }

    createRemoteDirectories(config, directoryPaths, 0);
}

void WebDavClient::createRemoteDirectories(const WebDavConfig &config, const QStringList &directoryPaths, int index)
{
    if (index >= directoryPaths.count()) {
        m_isTesting = false;
        emit connectionTestFinished(true, QStringLiteral("WebDAV 连接成功，远程目录已确认可用"));
        return;
    }

    QNetworkRequest probeRequest = makeRequest(config, remoteUrl(config, directoryPaths.at(index)));
    probeRequest.setRawHeader("Depth", "0");
    const QByteArray body = QByteArrayLiteral(
        R"(<?xml version="1.0" encoding="utf-8"?>)"
        R"(<d:propfind xmlns:d="DAV:"><d:prop><d:resourcetype/></d:prop></d:propfind>)");
    QNetworkReply *probeReply = m_network.sendCustomRequest(probeRequest, QByteArrayLiteral("PROPFIND"), body);

    connect(probeReply, &QNetworkReply::finished, this, [this, probeReply, config, directoryPaths, index]() {
        const int statusCode = probeReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError error = probeReply->error();
        const QString statusText = httpStatusText(probeReply);
        probeReply->deleteLater();

        if (error == QNetworkReply::NoError && (statusCode == 200 || statusCode == 207)) {
            createRemoteDirectories(config, directoryPaths, index + 1);
            return;
        }

        if (statusCode == 404) {
            makeDirectoryForConnection(config, directoryPaths, index);
            return;
        }

        m_isTesting = false;
        emit connectionTestFinished(false, QStringLiteral("WebDAV 连接成功但检查远程目录失败：%1").arg(statusText));
    });
}

void WebDavClient::makeDirectoryForConnection(const WebDavConfig &config, const QStringList &directoryPaths, int index)
{
    const QUrl url = remoteUrl(config, directoryPaths.at(index));
    QNetworkRequest request = makeRequest(config, url);
    QNetworkReply *reply = m_network.sendCustomRequest(request, QByteArrayLiteral("MKCOL"));

    connect(reply, &QNetworkReply::finished, this, [this, reply, config, directoryPaths, index]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError error = reply->error();
        const QString statusText = httpStatusText(reply);
        reply->deleteLater();

        if ((error == QNetworkReply::NoError && (statusCode == 200 || statusCode == 201 || statusCode == 204))
            || statusCode == 405) {
            createRemoteDirectories(config, directoryPaths, index + 1);
            return;
        }

        m_isTesting = false;
        emit connectionTestFinished(false, QStringLiteral("WebDAV 连接成功但创建远程目录失败：%1").arg(statusText));
    });
}

QStringList WebDavClient::directoryHierarchy(const QString &remoteDirectoryPath) const
{
    const QStringList segments = normalizedRemotePath(remoteDirectoryPath).split(
        QLatin1Char('/'),
        Qt::SkipEmptyParts);

    QStringList directoryPaths;
    QString currentPath;
    for (const QString &segment : segments) {
        currentPath += QLatin1Char('/');
        currentPath += segment;
        directoryPaths.append(currentPath);
    }

    return directoryPaths;
}

void WebDavClient::createRemoteDirectoriesForUpload(
    const WebDavConfig &config,
    const QStringList &directoryPaths,
    int index,
    const QString &localFilePath,
    const QString &remoteFilePath,
    const QString &uploadState,
    const QString &successMessage)
{
    if (index >= directoryPaths.count()) {
        putFile(config, localFilePath, remoteFilePath, uploadState, successMessage);
        return;
    }

    QNetworkRequest probeRequest = makeRequest(config, remoteUrl(config, directoryPaths.at(index)));
    probeRequest.setRawHeader("Depth", "0");
    const QByteArray body = QByteArrayLiteral(
        R"(<?xml version="1.0" encoding="utf-8"?>)"
        R"(<d:propfind xmlns:d="DAV:"><d:prop><d:resourcetype/></d:prop></d:propfind>)");
    QNetworkReply *probeReply = m_network.sendCustomRequest(probeRequest, QByteArrayLiteral("PROPFIND"), body);

    connect(probeReply, &QNetworkReply::finished, this, [this, probeReply, config, directoryPaths, index, localFilePath, remoteFilePath, uploadState, successMessage]() {
        const int statusCode = probeReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError error = probeReply->error();
        const QString statusText = httpStatusText(probeReply);
        probeReply->deleteLater();

        if (error == QNetworkReply::NoError && (statusCode == 200 || statusCode == 207)) {
            createRemoteDirectoriesForUpload(config, directoryPaths, index + 1, localFilePath, remoteFilePath, uploadState, successMessage);
            return;
        }

        if (statusCode == 404) {
            makeDirectoryForUpload(config, directoryPaths, index, localFilePath, remoteFilePath, uploadState, successMessage);
            return;
        }

        emit fileUploadFinished(false,
                                localFilePath,
                                remoteFilePath,
                                QStringLiteral("remote_directory_check_failed"),
                                QStringLiteral("检查云端快照目录失败：%1").arg(statusText));
    });
}

void WebDavClient::makeDirectoryForUpload(
    const WebDavConfig &config,
    const QStringList &directoryPaths,
    int index,
    const QString &localFilePath,
    const QString &remoteFilePath,
    const QString &uploadState,
    const QString &successMessage)
{
    QNetworkRequest request = makeRequest(config, remoteUrl(config, directoryPaths.at(index)));
    QNetworkReply *reply = m_network.sendCustomRequest(request, QByteArrayLiteral("MKCOL"));

    connect(reply, &QNetworkReply::finished, this, [this, reply, config, directoryPaths, index, localFilePath, remoteFilePath, uploadState, successMessage]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError error = reply->error();
        const QString statusText = httpStatusText(reply);
        reply->deleteLater();

        if ((error == QNetworkReply::NoError && (statusCode == 200 || statusCode == 201 || statusCode == 204))
            || statusCode == 405) {
            createRemoteDirectoriesForUpload(config, directoryPaths, index + 1, localFilePath, remoteFilePath, uploadState, successMessage);
            return;
        }

        emit fileUploadFinished(false,
                                localFilePath,
                                remoteFilePath,
                                QStringLiteral("remote_directory_failed"),
                                QStringLiteral("创建云端快照目录失败：%1").arg(statusText));
    });
}

void WebDavClient::putFile(
    const WebDavConfig &config,
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

    QNetworkRequest request = makeRequest(config, remoteUrl(config, remoteFilePath));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/zip"));
    QNetworkReply *reply = m_network.put(request, file);
    file->setParent(reply);
    connect(reply, &QNetworkReply::uploadProgress, this,
            [this, localFilePath, remoteFilePath](qint64 bytesSent, qint64 bytesTotal) {
                emit fileUploadProgress(localFilePath, remoteFilePath, bytesSent, bytesTotal);
            });

    connect(reply, &QNetworkReply::finished, this, [this, reply, file, localFilePath, remoteFilePath, uploadState, successMessage]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError error = reply->error();
        const QString statusText = httpStatusText(reply);
        file->close();
        reply->deleteLater();

        if (error == QNetworkReply::NoError && (statusCode == 200 || statusCode == 201 || statusCode == 204)) {
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
                                QStringLiteral("快照上传失败：%1").arg(statusText));
    });
}
