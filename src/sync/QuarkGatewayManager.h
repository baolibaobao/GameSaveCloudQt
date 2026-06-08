#pragma once

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QProcess>
#include <QByteArray>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include "sync/WebDavSettings.h"

class QNetworkReply;

class QuarkGatewayManager : public QObject
{
    Q_OBJECT

public:
    explicit QuarkGatewayManager(QObject *parent = nullptr);

    void startAndConfigure(const QString &cookie);
    void uploadFileIfMissing(const QString &localFilePath, const QString &remoteDirectoryPath);
    void uploadFileOverwrite(const QString &localFilePath, const QString &remoteDirectoryPath);
    void uploadDataFile(const QString &remoteFilePath, const QByteArray &data, const QString &operationId);
    void downloadDataFile(const QString &remoteFilePath, const QString &operationId);
    void downloadFile(const QString &remoteFilePath, const QString &localFilePath);
    QString adminPassword() const;
    QString gatewayBaseUrl() const;
    QString executablePath() const;
    QString dataDirectoryPath() const;

signals:
    void statusChanged(const QString &status);
    void gatewayReady(const WebDavConfig &config, const QString &message);
    void gatewayFailed(const QString &message);
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
    void dataFileUploadFinished(
        bool success,
        const QString &remoteFilePath,
        const QString &operationId,
        const QString &message);
    void dataFileDownloadFinished(
        bool success,
        const QString &remoteFilePath,
        const QString &operationId,
        const QByteArray &data,
        const QString &message);

private:
    QString findExecutablePath() const;
    QString engineDirectoryPath() const;
    QString configFilePath() const;
    QString loadOrCreateAdminPassword();
    bool setAdminPassword(const QString &password);
    void startProcess();
    void pollLogin();
    void login();
    void configureStorage(const QString &token);
    int storageIdFromListPayload(const QByteArray &payload) const;
    void createOrUpdateStorage(const QString &token, int existingStorageId);
    void ensureSyncDirectory(const QString &token);
    void emitGatewayReady();
    void checkRemoteFileBeforeUpload(const QString &localFilePath, const QString &remoteFilePath);
    void checkRemoteFileAfterDirectoryRefresh(const QString &localFilePath, const QString &remoteFilePath);
    void ensureDirectoryBeforeUpload(
        const QString &localFilePath,
        const QString &remoteDirectoryPath,
        const QString &remoteFilePath,
        const QString &uploadState = QStringLiteral("uploaded"),
        const QString &successMessage = QStringLiteral("快照已通过 OpenList API 上传到夸克网盘"));
    void putFile(
        const QString &localFilePath,
        const QString &remoteFilePath,
        const QString &uploadState = QStringLiteral("uploaded"),
        const QString &successMessage = QStringLiteral("快照已通过 OpenList API 上传到夸克网盘"));
    void ensureDirectoryBeforeDataUpload(
        const QString &remoteFilePath,
        const QByteArray &data,
        const QString &operationId);
    void putDataFile(
        const QString &remoteFilePath,
        const QByteArray &data,
        const QString &operationId);
    void downloadFromUrl(
        const QString &remoteFilePath,
        const QString &localFilePath,
        const QUrl &url,
        const QVariantMap &headers = {});
    void downloadDataFromUrl(
        const QString &remoteFilePath,
        const QString &operationId,
        const QUrl &url,
        const QVariantMap &headers = {});
    QUrl proxiedDownloadUrl(const QString &remoteFilePath, const QString &sign = {}) const;
    QNetworkRequest apiRequest(const QString &path, const QString &token = {}) const;
    QByteArray storagePayload(int existingStorageId = 0) const;
    QByteArray quarkAdditionJson() const;
    QString normalizedRemotePath(const QString &remotePath) const;
    QString apiMessageFromPayload(const QByteArray &payload, const QString &fallback) const;
    void finishFailure(const QString &message);

    QNetworkAccessManager m_network;
    QProcess m_process;
    QTimer m_loginPollTimer;
    QString m_cookie;
    QString m_adminPassword;
    QString m_apiToken;
    int m_loginAttempts = 0;
    int m_mountConflictRetries = 0;
    bool m_configuring = false;
};
