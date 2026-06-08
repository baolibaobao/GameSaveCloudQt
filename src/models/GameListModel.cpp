#include "GameListModel.h"

#include <utility>

GameListModel::GameListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int GameListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }

    return m_games.count();
}

QVariant GameListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_games.count()) {
        return {};
    }

    const GameInfo &game = m_games.at(index.row());

    switch (role) {
    case AppIdRole:
        return game.appId;
    case NameRole:
        return game.name;
    case LocalizedNameRole:
        return game.localizedName;
    case DisplayNameRole:
    case Qt::DisplayRole:
        return game.displayName.isEmpty() ? game.name : game.displayName;
    case DevelopersRole:
        return game.developers;
    case PublishersRole:
        return game.publishers;
    case ShortDescriptionRole:
        return game.shortDescription;
    case InstallDirRole:
        return game.installDir;
    case LibraryPathRole:
        return game.libraryPath;
    case ManifestPathRole:
        return game.manifestPath;
    case HeaderImageUrlRole:
        return game.headerImageUrl;
    case SavePathRole:
        return game.savePath;
    case SavePathStatusRole:
        return game.savePathStatus;
    case SavePathSourceRole:
        return game.savePathSource;
    case SavePathAvailabilityRole:
        return game.savePathAvailability;
    case SavePathDetailRole:
        return game.savePathDetail;
    case SavePathCanSyncRole:
        return game.savePathCanSync;
    case IsManualGameRole:
        return game.isManualGame;
    case ExecutablePathRole:
        return game.executablePath;
    case PCGamingWikiPageNameRole:
        return game.pcGamingWikiPageName;
    case PCGamingWikiPageIdRole:
        return game.pcGamingWikiPageId;
    case ProcessNameRole:
        return game.processName;
    case RunningStatusRole:
        return game.runningStatus;
    case SyncStatusRole:
        return game.syncStatus;
    case MetadataStatusRole:
        return game.metadataStatus;
    case SnapshotStatusRole:
        return game.snapshotStatus;
    case SnapshotDetailRole:
        return game.snapshotDetail;
    case SnapshotDirectoryRole:
        return game.snapshotDirectory;
    case LatestSnapshotFileNameRole:
        return game.latestSnapshotFileName;
    case LatestSnapshotPathRole:
        return game.latestSnapshotPath;
    case SnapshotCountRole:
        return game.snapshotCount;
    case SnapshotNeedsCreateRole:
        return game.snapshotNeedsCreate;
    case CloudSnapshotStatusRole:
        return game.cloudSnapshotStatus;
    case CloudSnapshotDetailRole:
        return game.cloudSnapshotDetail;
    case LatestCloudSnapshotFileNameRole:
        return game.latestCloudSnapshotFileName;
    case LatestCloudSnapshotPathRole:
        return game.latestCloudSnapshotPath;
    case CloudSnapshotCountRole:
        return game.cloudSnapshotCount;
    default:
        return {};
    }
}

QHash<int, QByteArray> GameListModel::roleNames() const
{
    return {
        {AppIdRole, "appId"},
        {NameRole, "name"},
        {LocalizedNameRole, "localizedName"},
        {DisplayNameRole, "displayName"},
        {DevelopersRole, "developers"},
        {PublishersRole, "publishers"},
        {ShortDescriptionRole, "shortDescription"},
        {InstallDirRole, "installDir"},
        {LibraryPathRole, "libraryPath"},
        {ManifestPathRole, "manifestPath"},
        {HeaderImageUrlRole, "headerImageUrl"},
        {SavePathRole, "savePath"},
        {SavePathStatusRole, "savePathStatus"},
        {SavePathSourceRole, "savePathSource"},
        {SavePathAvailabilityRole, "savePathAvailability"},
        {SavePathDetailRole, "savePathDetail"},
        {SavePathCanSyncRole, "savePathCanSync"},
        {IsManualGameRole, "isManualGame"},
        {ExecutablePathRole, "executablePath"},
        {PCGamingWikiPageNameRole, "pcGamingWikiPageName"},
        {PCGamingWikiPageIdRole, "pcGamingWikiPageId"},
        {ProcessNameRole, "processName"},
        {RunningStatusRole, "runningStatus"},
        {SyncStatusRole, "syncStatus"},
        {MetadataStatusRole, "metadataStatus"},
        {SnapshotStatusRole, "snapshotStatus"},
        {SnapshotDetailRole, "snapshotDetail"},
        {SnapshotDirectoryRole, "snapshotDirectory"},
        {LatestSnapshotFileNameRole, "latestSnapshotFileName"},
        {LatestSnapshotPathRole, "latestSnapshotPath"},
        {SnapshotCountRole, "snapshotCount"},
        {SnapshotNeedsCreateRole, "snapshotNeedsCreate"},
        {CloudSnapshotStatusRole, "cloudSnapshotStatus"},
        {CloudSnapshotDetailRole, "cloudSnapshotDetail"},
        {LatestCloudSnapshotFileNameRole, "latestCloudSnapshotFileName"},
        {LatestCloudSnapshotPathRole, "latestCloudSnapshotPath"},
        {CloudSnapshotCountRole, "cloudSnapshotCount"}
    };
}

void GameListModel::setGames(const QList<GameInfo> &games)
{
    beginResetModel();
    m_games = games;
    endResetModel();
    emit countChanged();
}

const QList<GameInfo> &GameListModel::games() const
{
    return m_games;
}

void GameListModel::upsertGame(const GameInfo &game, const QString &oldAppId)
{
    QList<GameInfo> mergedGames;
    bool inserted = false;

    for (const GameInfo &existingGame : std::as_const(m_games)) {
        const bool sameCurrentId = existingGame.appId == game.appId;
        const bool sameOldId = !oldAppId.isEmpty() && existingGame.appId == oldAppId;

        if (sameCurrentId && !inserted) {
            mergedGames.append(game);
            inserted = true;
            continue;
        }

        if (sameOldId) {
            if (!inserted) {
                mergedGames.append(game);
                inserted = true;
            }
            continue;
        }

        mergedGames.append(existingGame);
    }

    if (!inserted) {
        mergedGames.append(game);
    }

    beginResetModel();
    m_games = mergedGames;
    endResetModel();
    emit countChanged();
}

bool GameListModel::updateMetadata(const QString &appId, const QVariantMap &metadata)
{
    for (int row = 0; row < m_games.count(); ++row) {
        GameInfo &game = m_games[row];
        if (game.appId != appId) {
            continue;
        }

        const QString localizedName = metadata.value(QStringLiteral("localizedName")).toString().trimmed();
        const QString displayName = metadata.value(QStringLiteral("displayName")).toString().trimmed();
        const QString headerImageUrl = metadata.value(QStringLiteral("headerImageUrl")).toString().trimmed();

        if (!localizedName.isEmpty()) {
            game.localizedName = localizedName;
        }

        game.displayName = displayName.isEmpty() ? game.name : displayName;
        game.developers = metadata.value(QStringLiteral("developers")).toStringList();
        game.publishers = metadata.value(QStringLiteral("publishers")).toStringList();
        game.shortDescription = metadata.value(QStringLiteral("shortDescription")).toString().trimmed();
        game.metadataStatus = metadata.value(QStringLiteral("metadataStatus")).toString().trimmed();

        if (!headerImageUrl.isEmpty()) {
            game.headerImageUrl = headerImageUrl;
        }

        const QModelIndex changedIndex = index(row);
        emit dataChanged(changedIndex, changedIndex, {
            LocalizedNameRole,
            DisplayNameRole,
            DevelopersRole,
            PublishersRole,
            ShortDescriptionRole,
            HeaderImageUrlRole,
            MetadataStatusRole
        });
        return true;
    }

    return false;
}

bool GameListModel::updateMetadataStatus(const QString &appId, const QString &status)
{
    for (int row = 0; row < m_games.count(); ++row) {
        GameInfo &game = m_games[row];
        if (game.appId != appId) {
            continue;
        }

        game.metadataStatus = status;
        const QModelIndex changedIndex = index(row);
        emit dataChanged(changedIndex, changedIndex, {MetadataStatusRole});
        return true;
    }

    return false;
}

bool GameListModel::updateSavePath(
    const QString &appId,
    const QString &path,
    const QString &status,
    const QString &source,
    const QString &availability,
    const QString &detail,
    bool canSync)
{
    for (int row = 0; row < m_games.count(); ++row) {
        GameInfo &game = m_games[row];
        if (game.appId != appId) {
            continue;
        }

        game.savePath = path;
        game.savePathStatus = status;
        game.savePathSource = source;
        game.savePathAvailability = availability;
        game.savePathDetail = detail;
        game.savePathCanSync = canSync;

        const QModelIndex changedIndex = index(row);
        emit dataChanged(changedIndex, changedIndex, {
            SavePathRole,
            SavePathStatusRole,
            SavePathSourceRole,
            SavePathAvailabilityRole,
            SavePathDetailRole,
            SavePathCanSyncRole
        });
        return true;
    }

    return false;
}

bool GameListModel::updateRunningStatus(const QString &appId, const QString &runningStatus)
{
    for (int row = 0; row < m_games.count(); ++row) {
        GameInfo &game = m_games[row];
        if (game.appId != appId) {
            continue;
        }

        if (game.runningStatus == runningStatus) {
            return false;
        }

        game.runningStatus = runningStatus;
        const QModelIndex changedIndex = index(row);
        emit dataChanged(changedIndex, changedIndex, {RunningStatusRole});
        return true;
    }

    return false;
}

bool GameListModel::updateSnapshotStatus(
    const QString &appId,
    const QString &status,
    const QString &detail,
    const QString &directory,
    const QString &latestSnapshotFileName,
    const QString &latestSnapshotPath,
    int snapshotCount,
    bool needsCreate)
{
    for (int row = 0; row < m_games.count(); ++row) {
        GameInfo &game = m_games[row];
        if (game.appId != appId) {
            continue;
        }

        game.snapshotStatus = status;
        game.snapshotDetail = detail;
        game.snapshotDirectory = directory;
        game.latestSnapshotFileName = latestSnapshotFileName;
        game.latestSnapshotPath = latestSnapshotPath;
        game.snapshotCount = snapshotCount;
        game.snapshotNeedsCreate = needsCreate;

        const QModelIndex changedIndex = index(row);
        emit dataChanged(changedIndex, changedIndex, {
            SnapshotStatusRole,
            SnapshotDetailRole,
            SnapshotDirectoryRole,
            LatestSnapshotFileNameRole,
            LatestSnapshotPathRole,
            SnapshotCountRole,
            SnapshotNeedsCreateRole
        });
        return true;
    }

    return false;
}

bool GameListModel::updateCloudSnapshotStatus(
    const QString &appId,
    const QString &status,
    const QString &detail,
    const QString &latestCloudSnapshotFileName,
    const QString &latestCloudSnapshotPath,
    int cloudSnapshotCount)
{
    for (int row = 0; row < m_games.count(); ++row) {
        GameInfo &game = m_games[row];
        if (game.appId != appId) {
            continue;
        }

        game.cloudSnapshotStatus = status;
        game.cloudSnapshotDetail = detail;
        game.latestCloudSnapshotFileName = latestCloudSnapshotFileName;
        game.latestCloudSnapshotPath = latestCloudSnapshotPath;
        game.cloudSnapshotCount = cloudSnapshotCount;

        const QModelIndex changedIndex = index(row);
        emit dataChanged(changedIndex, changedIndex, {
            CloudSnapshotStatusRole,
            CloudSnapshotDetailRole,
            LatestCloudSnapshotFileNameRole,
            LatestCloudSnapshotPathRole,
            CloudSnapshotCountRole
        });
        return true;
    }

    return false;
}

QVariantMap GameListModel::get(int row) const
{
    if (row < 0 || row >= m_games.count()) {
        return {};
    }

    return m_games.at(row).toVariantMap();
}
