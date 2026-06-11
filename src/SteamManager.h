#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "cache/GameMetadataCache.h"
#include "logging/AppLogger.h"
#include "manual/GameIdentityResolver.h"
#include "manual/ManualGameManager.h"
#include "manual/PCGamingWikiGameSearchClient.h"
#include "models/GameInfo.h"
#include "models/GameListModel.h"
#include "process/GameProcessMonitor.h"
#include "saves/SavePathResolver.h"
#include "snapshots/SnapshotPreprocessor.h"
#include "snapshots/SnapshotRestoreManager.h"
#include "steam/SteamMetadataClient.h"
#include "sync/ConfigCloudSync.h"
#include "sync/QuarkGatewayManager.h"
#include "sync/WebDavClient.h"
#include "sync/WebDavSettings.h"
#include "system/StartupManager.h"

class SteamManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString steamPath READ steamPath NOTIFY steamPathChanged)
    Q_PROPERTY(QString snapshotRootPath READ snapshotRootPath NOTIFY snapshotRootPathChanged)
    Q_PROPERTY(QString logDirectory READ logDirectory NOTIFY logDirectoryChanged)
    Q_PROPERTY(QString logFilePath READ logFilePath NOTIFY logFilePathChanged)
    Q_PROPERTY(QString webDavServerUrl READ webDavServerUrl NOTIFY webDavSettingsChanged)
    Q_PROPERTY(QString webDavUsername READ webDavUsername NOTIFY webDavSettingsChanged)
    Q_PROPERTY(QString webDavPassword READ webDavPassword NOTIFY webDavSettingsChanged)
    Q_PROPERTY(QString webDavRemoteRootPath READ webDavRemoteRootPath NOTIFY webDavSettingsChanged)
    Q_PROPERTY(QString webDavConnectionStatus READ webDavConnectionStatus NOTIFY webDavConnectionStatusChanged)
    Q_PROPERTY(bool webDavTesting READ webDavTesting NOTIFY webDavTestingChanged)
    Q_PROPERTY(QString quarkCookie READ quarkCookie NOTIFY quarkGatewaySettingsChanged)
    Q_PROPERTY(QString quarkGatewayStatus READ quarkGatewayStatus NOTIFY quarkGatewayStatusChanged)
    Q_PROPERTY(bool autoSyncEnabled READ autoSyncEnabled NOTIFY autoSyncSettingsChanged)
    Q_PROPERTY(bool launchAtStartup READ launchAtStartup NOTIFY startupSettingsChanged)
    Q_PROPERTY(bool snapshotUploadInProgress READ snapshotUploadInProgress NOTIFY snapshotTransferProgressChanged)
    Q_PROPERTY(double snapshotUploadProgress READ snapshotUploadProgress NOTIFY snapshotTransferProgressChanged)
    Q_PROPERTY(QString snapshotUploadStatus READ snapshotUploadStatus NOTIFY snapshotTransferProgressChanged)
    Q_PROPERTY(bool snapshotDownloadInProgress READ snapshotDownloadInProgress NOTIFY snapshotTransferProgressChanged)
    Q_PROPERTY(double snapshotDownloadProgress READ snapshotDownloadProgress NOTIFY snapshotTransferProgressChanged)
    Q_PROPERTY(QString snapshotDownloadStatus READ snapshotDownloadStatus NOTIFY snapshotTransferProgressChanged)
    Q_PROPERTY(int runningGameCount READ runningGameCount NOTIFY installedGamesChanged)
    Q_PROPERTY(int syncableGameCount READ syncableGameCount NOTIFY installedGamesChanged)
    Q_PROPERTY(int manualSavePathGameCount READ manualSavePathGameCount NOTIFY installedGamesChanged)
    Q_PROPERTY(QVariantList installedGames READ installedGames NOTIFY installedGamesChanged)
    Q_PROPERTY(GameListModel *gameModel READ gameModel CONSTANT)
    Q_PROPERTY(LogListModel *logModel READ logModel CONSTANT)

public:
    explicit SteamManager(QObject *parent = nullptr);

    QString steamPath() const;
    QString snapshotRootPath() const;
    QString logDirectory() const;
    QString logFilePath() const;
    QString webDavServerUrl() const;
    QString webDavUsername() const;
    QString webDavPassword() const;
    QString webDavRemoteRootPath() const;
    QString webDavConnectionStatus() const;
    bool webDavTesting() const;
    QString quarkCookie() const;
    QString quarkGatewayStatus() const;
    bool autoSyncEnabled() const;
    bool launchAtStartup() const;
    bool snapshotUploadInProgress() const;
    double snapshotUploadProgress() const;
    QString snapshotUploadStatus() const;
    bool snapshotDownloadInProgress() const;
    double snapshotDownloadProgress() const;
    QString snapshotDownloadStatus() const;
    int runningGameCount() const;
    int syncableGameCount() const;
    int manualSavePathGameCount() const;
    QVariantList installedGames() const;
    GameListModel *gameModel();
    LogListModel *logModel();

    Q_INVOKABLE QString findSteamPath();
    Q_INVOKABLE QVariantList getInstalledGames();
    Q_INVOKABLE void loadInstalledGames();
    Q_INVOKABLE void refreshInstalledGames();
    Q_INVOKABLE bool setManualSavePath(const QString &appId, const QUrl &folderUrl);
    Q_INVOKABLE bool addManualGameFromExecutable(const QUrl &executableUrl);
    Q_INVOKABLE bool setSnapshotRootPath(const QUrl &folderUrl);
    Q_INVOKABLE bool analyzeSnapshotForGame(const QString &appId);
    Q_INVOKABLE bool createSnapshotForGame(const QString &appId);
    Q_INVOKABLE bool deleteLocalSnapshotsForGame(const QString &appId);
    Q_INVOKABLE bool restoreLocalSnapshotForGame(const QString &appId, const QString &snapshotPathOrFileName);
    Q_INVOKABLE bool restoreCloudSnapshotForGame(const QString &appId, const QString &snapshotFileNameOrRemotePath);
    Q_INVOKABLE QVariantList restoreBackupsForGame(const QString &appId) const;
    Q_INVOKABLE bool restoreBackupForGame(const QString &appId, const QString &backupPathOrFileName);
    Q_INVOKABLE bool deleteRestoreBackupsForGame(const QString &appId);
    Q_INVOKABLE bool openRestoreBackupDirectoryForGame(const QString &appId);
    Q_INVOKABLE bool uploadAllSnapshotsForGame(const QString &appId);
    Q_INVOKABLE bool uploadLatestSnapshotForGame(const QString &appId);
    Q_INVOKABLE bool downloadAllSnapshotsForGame(const QString &appId);
    Q_INVOKABLE bool downloadLatestSnapshotForGame(const QString &appId);
    Q_INVOKABLE bool uploadSelectedSnapshotForGame(const QString &appId, const QString &snapshotPathOrFileName);
    Q_INVOKABLE bool downloadSelectedSnapshotForGame(const QString &appId, const QString &snapshotFileName);
    Q_INVOKABLE bool hasDownloadableSnapshot(const QString &appId) const;
    Q_INVOKABLE bool refreshLocalSnapshotsForGame(const QString &appId);
    Q_INVOKABLE void refreshCloudManifestFromRemote();
    Q_INVOKABLE void refreshCloudSnapshotsForGame(const QString &appId);
    Q_INVOKABLE void refreshCloudSnapshotsForGameQuietly(const QString &appId);
    Q_INVOKABLE QVariantList snapshotsForGame(const QString &appId) const;
    Q_INVOKABLE QVariantList cloudSnapshotsForGame(const QString &appId) const;
    Q_INVOKABLE void clearVisibleLogs();
    Q_INVOKABLE bool openLogDirectory() const;
    Q_INVOKABLE bool openGameDataCacheDirectory();
    Q_INVOKABLE bool openSavePathForGame(const QString &appId);
    Q_INVOKABLE bool openSnapshotDirectoryForGame(const QString &appId);
    Q_INVOKABLE bool saveWebDavSettings(
        const QString &serverUrl,
        const QString &username,
        const QString &password,
        const QString &remoteRootPath);
    Q_INVOKABLE void testWebDavConnection(
        const QString &serverUrl,
        const QString &username,
        const QString &password,
        const QString &remoteRootPath);
    Q_INVOKABLE bool saveQuarkCookieGateway(const QString &cookie);
    Q_INVOKABLE bool recheckQuarkGatewayHealth();
    Q_INVOKABLE bool setAutoSyncEnabled(bool enabled);
    Q_INVOKABLE bool autoSyncEnabledForGame(const QString &appId) const;
    Q_INVOKABLE bool setAutoSyncEnabledForGame(const QString &appId, bool enabled);
    Q_INVOKABLE bool setAutoSyncEnabledForAllGames(bool enabled);
    Q_INVOKABLE bool setLaunchAtStartup(bool enabled);

signals:
    void steamPathChanged();
    void snapshotRootPathChanged();
    void logDirectoryChanged();
    void logFilePathChanged();
    void webDavSettingsChanged();
    void webDavConnectionStatusChanged();
    void webDavTestingChanged();
    void quarkGatewaySettingsChanged();
    void quarkGatewayStatusChanged();
    void autoSyncSettingsChanged();
    void startupSettingsChanged();
    void snapshotTransferProgressChanged();
    void installedGamesChanged();
    void cloudSnapshotsChanged(const QString &appId);
    void userAlertRequested(
        const QString &title,
        const QString &message,
        const QString &detail,
        bool danger);

private:
    QStringList steamAppsDirectories(const QString &steamRootPath) const;
    GameInfo parseAcfFile(const QString &acfFilePath) const;
    QString normalizedExistingSteamPath(const QString &path) const;
    QString guessProcessNameForSteamGame(const GameInfo &game) const;
    QString installPathForSteamGame(const GameInfo &game) const;
    void appendManualGames(QList<GameInfo> &games, QSet<QString> &seenAppIds) const;
    void upsertGameAndRebuild(const GameInfo &game, const QString &oldAppId = {});
    void persistManualGameIfPresent(const QString &appId);
    bool shouldHideSteamApp(const GameInfo &game) const;
    QVariantList scanInstalledGames(bool forceNetworkRefresh);
    bool showCachedGamesBeforeScan();
    void scheduleInstalledGamesScan(bool forceNetworkRefresh);
    void beginInstalledGamesScan(bool forceNetworkRefresh);
    void processNextInstalledGamesScanBatch();
    void finishInstalledGamesScan();
    void scheduleProcessNameRefresh(const QStringList &appIds);
    void processNextProcessNameRefresh();
    void applyManualSavePaths(QList<GameInfo> &games);
    bool saveGameMetadataCacheForApp(const QString &appId);
    void requestMetadataForGames(const QList<GameInfo> &games);
    void resolveSavePathsForGames(const QList<GameInfo> &games);
    void requestMissingStartupDataForGames(const QList<GameInfo> &games);
    void rebuildInstalledGamesFromModel();
    GameInfo gameByAppId(const QString &appId) const;
    void applySnapshotResult(const QString &appId, const QVariantMap &result);
    void refreshSnapshotRecordsForGame(
        const QString &appId,
        const QString &status,
        const QString &detail,
        bool needsCreate);
    void scheduleLocalSnapshotRecordsRefresh();
    void processNextLocalSnapshotRecordsRefresh();
    void saveGameMetadataCacheDeferred(const QString &appId);
    void flushDeferredGameMetadataCache();
    void handleSnapshotUploadFinished(
        bool success,
        const QString &localFilePath,
        const QString &remoteFilePath,
        const QString &uploadState,
        const QString &message);
    void handleSnapshotDownloadFinished(
        bool success,
        const QString &remoteFilePath,
        const QString &localFilePath,
        const QString &message);
    void handleSnapshotUploadProgress(
        const QString &localFilePath,
        const QString &remoteFilePath,
        qint64 bytesSent,
        qint64 bytesTotal);
    void handleSnapshotDownloadProgress(
        const QString &remoteFilePath,
        const QString &localFilePath,
        qint64 bytesReceived,
        qint64 bytesTotal);
    void handleCloudDataUploadFinished(
        bool success,
        const QString &remoteFilePath,
        const QString &operationId,
        const QString &message);
    void handleCloudDataDownloadFinished(
        bool success,
        const QString &remoteFilePath,
        const QString &operationId,
        const QByteArray &data,
        const QString &message);
    void requestCloudManifestMergeForGame(const QString &appId);
    void uploadCloudManifestsForGame(const GameInfo &game);
    QJsonObject buildGameCloudManifest(const GameInfo &game) const;
    QJsonObject buildCloudRootManifest(
        const QByteArray &existingRootData,
        const QJsonObject &gameSummary) const;
    QJsonObject cloudGameSummaryFromManifest(const QJsonObject &gameManifest) const;
    QVariantList cloudSnapshotRecordsFromLocalManifest(const GameInfo &game) const;
    QVariantList cloudSnapshotRecordsFromExistingLocalSnapshots(const GameInfo &game) const;
    QVariantList cloudSnapshotRecordsFromGameManifest(const QJsonObject &gameManifest) const;
    QVariantList downloadableSnapshotRecordsForGame(const GameInfo &game) const;
    bool uploadSnapshotRecordsForGame(const GameInfo &game, const QVariantList &snapshots);
    bool uploadAutoSyncSnapshotForGame(const GameInfo &game, const QVariantMap &snapshotResult);
    bool downloadSnapshotRecordsForGame(const GameInfo &game, const QVariantList &snapshots);
    void applyCloudRootManifest(const QByteArray &data);
    void applyCloudSnapshotRecordsToModel();
    void requestConfigCloudSyncDownload();
    void scheduleConfigCloudSyncUpload();
    void uploadConfigCloudSync();
    void applyConfigCloudSync(const QByteArray &data);
    void updateCloudSnapshotStatusForGame(
        const QString &appId,
        const QVariantList &records,
        const QString &status,
        const QString &detail);
    QString cloudRootPath() const;
    QString cloudRootManifestPath() const;
    QString configCloudSyncPath() const;
    QString restoreBackupRootPath() const;
    QString defaultStorageRootPath() const;
    QString storageRootFromSnapshotRoot(const QString &snapshotRootPath) const;
    QString snapshotDirectoryForStorageRoot(const QString &storageRootPath) const;
    QString logDirectoryForStorageRoot(const QString &storageRootPath) const;
    QString logDirectoryForSnapshotRoot(const QString &snapshotRootPath) const;
    bool ensureDefaultStorageRootInitialized();
    void alignLogDirectoryWithSnapshotRoot();
    bool migrateDirectoryContents(
        const QString &sourcePath,
        const QString &targetPath,
        const QStringList &excludedPaths = {});
    bool copyDirectoryContents(
        const QString &sourcePath,
        const QString &targetPath,
        const QStringList &excludedPaths = {}) const;
    bool moveDirectoryChildren(
        const QString &sourcePath,
        const QString &targetPath,
        const QStringList &excludedPaths = {}) const;
    bool pathsEqual(const QString &leftPath, const QString &rightPath) const;
    bool pathIsInside(const QString &childPath, const QString &parentPath) const;
    bool isNumericAppId(const QString &appId) const;
    WebDavConfig webDavConfigFromInput(
        const QString &serverUrl,
        const QString &username,
        const QString &password,
        const QString &remoteRootPath) const;
    void setWebDavConnectionStatus(const QString &status);
    void setWebDavTesting(bool testing);
    QString cloudDirectoryForGame(const GameInfo &game) const;
    QString localDownloadPathForSnapshot(const GameInfo &game, const QString &fileName) const;
    void loadQuarkGatewaySettings();
    void setQuarkGatewayStatus(const QString &status);
    void refreshCloudManifestFromRemoteInternal(bool forceRefresh);
    void setSnapshotUploadProgress(bool inProgress, double progress, const QString &status);
    void setSnapshotDownloadProgress(bool inProgress, double progress, const QString &status);
    QString transferProgressText(
        const QString &action,
        const GameInfo &game,
        const QString &fileName,
        int completedCount,
        int remainingCount,
        int totalCount,
        qint64 bytesDone,
        qint64 bytesTotal) const;
    void startQuarkCookieHealthCheck();
    void startQuarkCookieProbeUpload();
    QString quarkCookieHealthCheckPath() const;
    bool isQuarkAuthFailureMessage(const QString &message) const;
    void loadAutoSyncSettings();
    QString autoSyncGameSettingsKey(const QString &appId) const;
    void handleGameClosedForAutoSync(const QString &appId, const QString &processName);
    void updateSyncStatusForGame(const QString &appId, const QString &status);
    void refreshCloudSnapshotsForGameInternal(const QString &appId, bool quiet);
    bool shouldLogAutomaticCloudRefresh(const QString &appId);

    QString m_steamPath;
    GameListModel m_gameModel;
    GameMetadataCache m_gameMetadataCache;
    SteamMetadataClient m_metadataClient;
    SavePathResolver m_savePathResolver;
    GameProcessMonitor m_processMonitor;
    SnapshotPreprocessor m_snapshotPreprocessor;
    SnapshotRestoreManager m_snapshotRestoreManager;
    AppLogger m_logger;
    WebDavSettings m_webDavSettings;
    WebDavClient m_webDavClient;
    QuarkGatewayManager m_quarkGatewayManager;
    ConfigCloudSync m_configCloudSync;
    WebDavConfig m_webDavConfig;
    QString m_webDavConnectionStatus;
    bool m_webDavTesting = false;
    QString m_quarkCookie;
    QString m_quarkGatewayStatus;
    bool m_autoSyncEnabled = false;
    StartupManager m_startupManager;
    QHash<QString, QString> m_pendingSnapshotUploadAppIds;
    QHash<QString, QString> m_pendingSnapshotUploadFileNames;
    QSet<QString> m_pendingBatchSnapshotUploadPaths;
    QSet<QString> m_pendingAutoSyncUploadPaths;
    QHash<QString, int> m_pendingSnapshotUploadRemainingByAppId;
    QHash<QString, int> m_pendingSnapshotUploadTotalByAppId;
    QHash<QString, int> m_pendingSnapshotUploadSuccessByAppId;
    QHash<QString, int> m_pendingSnapshotUploadFailureByAppId;
    QHash<QString, QString> m_pendingSnapshotDownloadAppIds;
    QHash<QString, QVariantMap> m_pendingSnapshotDownloadRecords;
    QSet<QString> m_pendingSnapshotRestoreDownloadPaths;
    QHash<QString, int> m_pendingSnapshotDownloadRemainingByAppId;
    QHash<QString, int> m_pendingSnapshotDownloadTotalByAppId;
    QHash<QString, int> m_pendingSnapshotDownloadSuccessByAppId;
    QHash<QString, int> m_pendingSnapshotDownloadFailureByAppId;
    QHash<QString, QVariantList> m_cloudSnapshotRecordsByAppId;
    QHash<QString, QString> m_cloudDirectoryByAppId;
    QTimer m_configCloudSyncUploadTimer;
    QTimer m_installedGamesScanTimer;
    QTimer m_localSnapshotRefreshTimer;
    QTimer m_processNameRefreshTimer;
    QJsonObject m_configCloudSyncDocument;
    QJsonObject m_pendingConfigCloudSyncDocument;
    QStringList m_pendingLocalSnapshotRefreshAppIds;
    QStringList m_pendingProcessNameRefreshAppIds;
    QStringList m_pendingSteamManifestPaths;
    QList<GameInfo> m_pendingInstalledGameScanGames;
    QStringList m_pendingInstalledGameProcessNameRefreshAppIds;
    QSet<QString> m_pendingInstalledGameScanSeenAppIds;
    QHash<QString, bool> m_pendingCloudSnapshotRefreshQuietByAppId;
    QHash<QString, QDateTime> m_lastAutomaticCloudRefreshLogByAppId;
    GameIdentityResolver m_gameIdentityResolver;
    ManualGameManager m_manualGameManager;
    PCGamingWikiGameSearchClient m_pcGamingWikiSearchClient;
    QHash<QString, GameInfo> m_pendingManualGames;
    bool m_hasLoggedProcessMonitorIdle = false;
    bool m_cloudManifestLoaded = false;
    bool m_cloudManifestRefreshInFlight = false;
    int m_installedGamesScanRequestId = 0;
    bool m_quarkGatewayStartRequestedThisSession = false;
    bool m_installedGamesScanForceNetworkRefresh = false;
    bool m_applyingConfigCloudSync = false;
    bool m_deferGameMetadataCacheWrites = false;
    QSet<QString> m_deferredGameMetadataCacheAppIds;
    bool m_snapshotUploadInProgress = false;
    double m_snapshotUploadProgress = 0.0;
    QString m_snapshotUploadStatus;
    bool m_snapshotDownloadInProgress = false;
    double m_snapshotDownloadProgress = 0.0;
    QString m_snapshotDownloadStatus;
};
