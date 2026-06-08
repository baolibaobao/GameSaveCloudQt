#pragma once

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "sync/WebDavSettings.h"

class QNetworkReply;

class WebDavClient : public QObject
{
    Q_OBJECT

public:
    explicit WebDavClient(QObject *parent = nullptr);

    void testConnection(const WebDavConfig &config);
    void uploadFileIfMissing(
        const WebDavConfig &config,
        const QString &localFilePath,
        const QString &remoteDirectoryPath);
    void uploadFileOverwrite(
        const WebDavConfig &config,
        const QString &localFilePath,
        const QString &remoteDirectoryPath);
    void downloadFile(
        const WebDavConfig &config,
        const QString &remoteFilePath,
        const QString &localFilePath);

signals:
    void connectionTestFinished(bool success, const QString &message);
    void fileUploadFinished(
        bool success,
        const QString &localFilePath,
        const QString &remoteFilePath,
        const QString &uploadState,
        const QString &message);
    void fileDownloadFinished(
        bool success,
        const QString &remoteFilePath,
        const QString &localFilePath,
        const QString &message);

private:
    QNetworkRequest makeRequest(const WebDavConfig &config, const QUrl &url) const;
    QUrl remoteUrl(const WebDavConfig &config, const QString &remotePath) const;
    QString sanitizedBaseUrl(const QString &serverUrl) const;
    QString normalizedRemotePath(const QString &remotePath) const;
    QString httpStatusText(QNetworkReply *reply) const;
    void ensureRemoteRootDirectory(const WebDavConfig &config);
    void createRemoteDirectories(const WebDavConfig &config, const QStringList &directoryPaths, int index);
    void makeDirectoryForConnection(const WebDavConfig &config, const QStringList &directoryPaths, int index);
    QStringList directoryHierarchy(const QString &remoteDirectoryPath) const;
    void createRemoteDirectoriesForUpload(
        const WebDavConfig &config,
        const QStringList &directoryPaths,
        int index,
        const QString &localFilePath,
        const QString &remoteFilePath,
        const QString &uploadState = QStringLiteral("uploaded"),
        const QString &successMessage = QStringLiteral("快照已上传到 WebDAV 云端"));
    void makeDirectoryForUpload(
        const WebDavConfig &config,
        const QStringList &directoryPaths,
        int index,
        const QString &localFilePath,
        const QString &remoteFilePath,
        const QString &uploadState = QStringLiteral("uploaded"),
        const QString &successMessage = QStringLiteral("快照已上传到 WebDAV 云端"));
    void putFile(
        const WebDavConfig &config,
        const QString &localFilePath,
        const QString &remoteFilePath,
        const QString &uploadState = QStringLiteral("uploaded"),
        const QString &successMessage = QStringLiteral("快照已上传到 WebDAV 云端"));

    QNetworkAccessManager m_network;
    bool m_isTesting = false;
};
