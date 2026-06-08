#include "ManualGameManager.h"

#include <QDir>
#include <QVariantList>
#include <QVariantMap>

#include "storage/AppSettings.h"

QList<GameInfo> ManualGameManager::loadManualGames() const
{
    const std::unique_ptr<QSettings> settings = AppSettings::create();
    const QVariantList items = settings->value(QStringLiteral("manualGames/items")).toList();

    QList<GameInfo> games;
    for (const QVariant &item : items) {
        const GameInfo game = mapToGame(item.toMap());
        if (game.isValid()) {
            games.append(game);
        }
    }

    return games;
}

bool ManualGameManager::saveManualGame(const GameInfo &game) const
{
    if (!game.isValid() || game.executablePath.trimmed().isEmpty()) {
        return false;
    }

    const std::unique_ptr<QSettings> settings = AppSettings::create();
    QVariantList items = settings->value(QStringLiteral("manualGames/items")).toList();

    const QString cleanExecutablePath = QDir::cleanPath(game.executablePath).toLower();
    bool replaced = false;

    for (QVariant &item : items) {
        QVariantMap existing = item.toMap();
        const QString existingAppId = existing.value(QStringLiteral("appId")).toString();
        const QString existingExecutablePath = QDir::cleanPath(
            existing.value(QStringLiteral("executablePath")).toString()).toLower();

        if (existingAppId == game.appId || existingExecutablePath == cleanExecutablePath) {
            existing = gameToMap(game);
            item = existing;
            replaced = true;
            break;
        }
    }

    if (!replaced) {
        items.append(gameToMap(game));
    }

    settings->setValue(QStringLiteral("manualGames/items"), items);
    settings->sync();
    return settings->status() == QSettings::NoError;
}

QVariantMap ManualGameManager::gameToMap(const GameInfo &game) const
{
    QVariantMap map;
    map.insert(QStringLiteral("appId"), game.appId);
    map.insert(QStringLiteral("name"), game.name);
    map.insert(QStringLiteral("localizedName"), game.localizedName);
    map.insert(QStringLiteral("displayName"), game.displayName);
    map.insert(QStringLiteral("installDir"), game.installDir);
    map.insert(QStringLiteral("headerImageUrl"), game.headerImageUrl);
    map.insert(QStringLiteral("savePath"), game.savePath);
    map.insert(QStringLiteral("savePathStatus"), game.savePathStatus);
    map.insert(QStringLiteral("savePathSource"), game.savePathSource);
    map.insert(QStringLiteral("savePathAvailability"), game.savePathAvailability);
    map.insert(QStringLiteral("savePathDetail"), game.savePathDetail);
    map.insert(QStringLiteral("savePathCanSync"), game.savePathCanSync);
    map.insert(QStringLiteral("isManualGame"), true);
    map.insert(QStringLiteral("executablePath"), game.executablePath);
    map.insert(QStringLiteral("pcGamingWikiPageName"), game.pcGamingWikiPageName);
    map.insert(QStringLiteral("pcGamingWikiPageId"), game.pcGamingWikiPageId);
    map.insert(QStringLiteral("processName"), game.processName);
    map.insert(QStringLiteral("runningStatus"), game.runningStatus);
    map.insert(QStringLiteral("syncStatus"), game.syncStatus);
    map.insert(QStringLiteral("metadataStatus"), game.metadataStatus);
    map.insert(QStringLiteral("snapshotStatus"), game.snapshotStatus);
    map.insert(QStringLiteral("snapshotDetail"), game.snapshotDetail);
    map.insert(QStringLiteral("snapshotDirectory"), game.snapshotDirectory);
    map.insert(QStringLiteral("latestSnapshotFileName"), game.latestSnapshotFileName);
    map.insert(QStringLiteral("latestSnapshotPath"), game.latestSnapshotPath);
    map.insert(QStringLiteral("snapshotCount"), game.snapshotCount);
    map.insert(QStringLiteral("snapshotNeedsCreate"), game.snapshotNeedsCreate);
    map.insert(QStringLiteral("cloudSnapshotStatus"), game.cloudSnapshotStatus);
    map.insert(QStringLiteral("cloudSnapshotDetail"), game.cloudSnapshotDetail);
    map.insert(QStringLiteral("latestCloudSnapshotFileName"), game.latestCloudSnapshotFileName);
    map.insert(QStringLiteral("latestCloudSnapshotPath"), game.latestCloudSnapshotPath);
    map.insert(QStringLiteral("cloudSnapshotCount"), game.cloudSnapshotCount);
    return map;
}

GameInfo ManualGameManager::mapToGame(const QVariantMap &map) const
{
    GameInfo game;
    game.appId = map.value(QStringLiteral("appId")).toString();
    game.name = map.value(QStringLiteral("name")).toString();
    game.localizedName = map.value(QStringLiteral("localizedName")).toString();
    game.displayName = map.value(QStringLiteral("displayName")).toString();
    game.installDir = map.value(QStringLiteral("installDir")).toString();
    game.headerImageUrl = map.value(QStringLiteral("headerImageUrl")).toString();
    game.savePath = map.value(QStringLiteral("savePath")).toString();
    game.savePathStatus = map.value(QStringLiteral("savePathStatus")).toString();
    game.savePathSource = map.value(QStringLiteral("savePathSource")).toString();
    game.savePathAvailability = map.value(QStringLiteral("savePathAvailability")).toString();
    game.savePathDetail = map.value(QStringLiteral("savePathDetail")).toString();
    game.savePathCanSync = map.value(QStringLiteral("savePathCanSync")).toBool();
    game.isManualGame = true;
    game.executablePath = QDir::toNativeSeparators(QDir::cleanPath(map.value(QStringLiteral("executablePath")).toString()));
    game.pcGamingWikiPageName = map.value(QStringLiteral("pcGamingWikiPageName")).toString();
    game.pcGamingWikiPageId = map.value(QStringLiteral("pcGamingWikiPageId")).toString();
    game.processName = map.value(QStringLiteral("processName")).toString();
    game.runningStatus = map.value(QStringLiteral("runningStatus")).toString();
    game.syncStatus = map.value(QStringLiteral("syncStatus")).toString();
    game.metadataStatus = map.value(QStringLiteral("metadataStatus")).toString();
    game.snapshotStatus = map.value(QStringLiteral("snapshotStatus")).toString();
    game.snapshotDetail = map.value(QStringLiteral("snapshotDetail")).toString();
    game.snapshotDirectory = map.value(QStringLiteral("snapshotDirectory")).toString();
    game.latestSnapshotFileName = map.value(QStringLiteral("latestSnapshotFileName")).toString();
    game.latestSnapshotPath = map.value(QStringLiteral("latestSnapshotPath")).toString();
    game.snapshotCount = map.value(QStringLiteral("snapshotCount")).toInt();
    game.snapshotNeedsCreate = map.value(QStringLiteral("snapshotNeedsCreate")).toBool();
    game.cloudSnapshotStatus = map.value(QStringLiteral("cloudSnapshotStatus")).toString();
    game.cloudSnapshotDetail = map.value(QStringLiteral("cloudSnapshotDetail")).toString();
    game.latestCloudSnapshotFileName = map.value(QStringLiteral("latestCloudSnapshotFileName")).toString();
    game.latestCloudSnapshotPath = map.value(QStringLiteral("latestCloudSnapshotPath")).toString();
    game.cloudSnapshotCount = map.value(QStringLiteral("cloudSnapshotCount")).toInt();
    return game;
}
