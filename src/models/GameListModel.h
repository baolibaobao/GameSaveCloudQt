#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QVariantMap>

#include "GameInfo.h"

class GameListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum GameRoles {
        AppIdRole = Qt::UserRole + 1,
        NameRole,
        LocalizedNameRole,
        DisplayNameRole,
        DevelopersRole,
        PublishersRole,
        ShortDescriptionRole,
        InstallDirRole,
        LibraryPathRole,
        ManifestPathRole,
        HeaderImageUrlRole,
        SavePathRole,
        SavePathStatusRole,
        SavePathSourceRole,
        SavePathAvailabilityRole,
        SavePathDetailRole,
        SavePathCanSyncRole,
        IsManualGameRole,
        ExecutablePathRole,
        PCGamingWikiPageNameRole,
        PCGamingWikiPageIdRole,
        ProcessNameRole,
        RunningStatusRole,
        SyncStatusRole,
        MetadataStatusRole,
        SnapshotStatusRole,
        SnapshotDetailRole,
        SnapshotDirectoryRole,
        LatestSnapshotFileNameRole,
        LatestSnapshotPathRole,
        SnapshotCountRole,
        SnapshotNeedsCreateRole,
        CloudSnapshotStatusRole,
        CloudSnapshotDetailRole,
        LatestCloudSnapshotFileNameRole,
        LatestCloudSnapshotPathRole,
        CloudSnapshotCountRole
    };
    Q_ENUM(GameRoles)

    explicit GameListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setGames(const QList<GameInfo> &games);
    const QList<GameInfo> &games() const;
    void upsertGame(const GameInfo &game, const QString &oldAppId = {});
    bool updateMetadata(const QString &appId, const QVariantMap &metadata);
    bool updateMetadataStatus(const QString &appId, const QString &status);
    bool updateSavePath(
        const QString &appId,
        const QString &path,
        const QString &status,
        const QString &source,
        const QString &availability,
        const QString &detail,
        bool canSync);
    bool updateRunningStatus(const QString &appId, const QString &runningStatus);
    bool updateSnapshotStatus(
        const QString &appId,
        const QString &status,
        const QString &detail,
        const QString &directory,
        const QString &latestSnapshotFileName,
        const QString &latestSnapshotPath,
        int snapshotCount,
        bool needsCreate);
    bool updateCloudSnapshotStatus(
        const QString &appId,
        const QString &status,
        const QString &detail,
        const QString &latestCloudSnapshotFileName,
        const QString &latestCloudSnapshotPath,
        int cloudSnapshotCount);

    Q_INVOKABLE QVariantMap get(int row) const;

signals:
    void countChanged();

private:
    QList<GameInfo> m_games;
};
