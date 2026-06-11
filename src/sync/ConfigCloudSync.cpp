#include "ConfigCloudSync.h"

#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>
#include <QSysInfo>

namespace {

QString cleanText(const QString &value)
{
    return value.trimmed();
}

bool setIfDifferent(QString &target, const QString &value)
{
    const QString cleanValue = cleanText(value);
    if (cleanValue.isEmpty() || target == cleanValue) {
        return false;
    }

    target = cleanValue;
    return true;
}

bool setListIfDifferent(QStringList &target, const QStringList &value)
{
    if (value.isEmpty() || target == value) {
        return false;
    }

    target = value;
    return true;
}

} // namespace

QJsonObject ConfigCloudSync::buildDocument(
    const QList<GameInfo> &localGames,
    const QHash<QString, QString> &cloudDirectoriesByAppId,
    const QJsonObject &existingDocument) const
{
    QSet<QString> localAppIds;
    QJsonArray mergedGames;

    for (const GameInfo &game : localGames) {
        const QString appId = cleanText(game.appId);
        if (!appId.isEmpty()) {
            localAppIds.insert(appId);
        }
    }

    /*
     * 配置云同步必须保留其他设备写入、但本机当前未安装的游戏条目。
     * 否则一台只装了少量游戏的电脑启动后，会把云端配置“瘦身”成自己的本地列表。
     */
    const QJsonArray existingGames = existingDocument.value(QStringLiteral("games")).toArray();
    for (const QJsonValue &value : existingGames) {
        const QJsonObject object = value.toObject();
        const QString appId = cleanText(object.value(QStringLiteral("appId")).toString());
        if (appId.isEmpty() || localAppIds.contains(appId)) {
            continue;
        }
        const GameInfo importedGame = gameFromJson(object);
        if (importedGame.isValid()) {
            mergedGames.append(gameToJson(importedGame, object.value(QStringLiteral("cloudDirectory")).toString()));
        }
    }

    for (const GameInfo &game : localGames) {
        const QString appId = cleanText(game.appId);
        if (appId.isEmpty()) {
            continue;
        }
        mergedGames.append(gameToJson(game, cloudDirectoriesByAppId.value(appId)));
    }

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("app"), QStringLiteral("GameSaveCloudQt"));
    root.insert(QStringLiteral("kind"), QStringLiteral("application-config-sync"));
    root.insert(QStringLiteral("updatedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("deviceName"), QSysInfo::machineHostName());
    root.insert(QStringLiteral("games"), mergedGames);
    return root;
}

ConfigCloudSyncImportResult ConfigCloudSync::parseDocument(const QByteArray &data) const
{
    ConfigCloudSyncImportResult result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("配置云同步文件不是有效 JSON：%1").arg(parseError.errorString());
        return result;
    }

    const QJsonObject root = document.object();
    const QString app = root.value(QStringLiteral("app")).toString();
    const QString kind = root.value(QStringLiteral("kind")).toString();
    if (app != QStringLiteral("GameSaveCloudQt")
        || kind != QStringLiteral("application-config-sync")) {
        result.error = QStringLiteral("配置云同步文件标识不匹配");
        return result;
    }

    const QJsonArray games = root.value(QStringLiteral("games")).toArray();
    for (const QJsonValue &value : games) {
        const GameInfo game = gameFromJson(value.toObject());
        if (game.isValid()) {
            result.games.append(game);
        }
    }

    result.valid = true;
    return result;
}

bool ConfigCloudSync::mergeImportedGame(const GameInfo &importedGame, GameInfo &localGame) const
{
    if (cleanText(importedGame.appId).isEmpty()
        || cleanText(localGame.appId) != cleanText(importedGame.appId)) {
        return false;
    }

    bool changed = false;
    changed |= setIfDifferent(localGame.name, importedGame.name);
    changed |= setIfDifferent(localGame.localizedName, importedGame.localizedName);
    changed |= setIfDifferent(localGame.displayName, importedGame.displayName);
    changed |= setListIfDifferent(localGame.developers, importedGame.developers);
    changed |= setListIfDifferent(localGame.publishers, importedGame.publishers);
    changed |= setIfDifferent(localGame.shortDescription, importedGame.shortDescription);
    changed |= setIfDifferent(localGame.headerImageUrl, importedGame.headerImageUrl);
    changed |= setIfDifferent(localGame.metadataStatus, importedGame.metadataStatus);
    changed |= setIfDifferent(localGame.pcGamingWikiPageName, importedGame.pcGamingWikiPageName);
    changed |= setIfDifferent(localGame.pcGamingWikiPageId, importedGame.pcGamingWikiPageId);

    /*
     * 云端路径只能作为候选线索：换电脑后真实存档目录很可能不同。
     * 只有本机当前没有存档路径时才展示候选，并且强制保持不可同步，等待用户确认。
     */
    if (cleanText(localGame.savePath).isEmpty() && !cleanText(importedGame.savePath).isEmpty()) {
        localGame.savePath = importedGame.savePath;
        localGame.savePathStatus = QStringLiteral("需确认存档目录");
        localGame.savePathSource = cleanText(importedGame.savePathSource).isEmpty()
                                       ? QStringLiteral("配置云同步")
                                       : importedGame.savePathSource;
        localGame.savePathAvailability = QStringLiteral("待确认");
        localGame.savePathDetail = QStringLiteral("来自配置云同步的历史存档路径，换设备后请重新确认目录后再同步");
        localGame.savePathCanSync = false;
        changed = true;
    }

    return changed;
}

QJsonObject ConfigCloudSync::gameToJson(const GameInfo &game, const QString &cloudDirectory) const
{
    QJsonObject object;
    object.insert(QStringLiteral("appId"), cleanText(game.appId));
    object.insert(QStringLiteral("name"), cleanText(game.name));
    object.insert(QStringLiteral("localizedName"), cleanText(game.localizedName));
    object.insert(QStringLiteral("displayName"), cleanText(game.displayName));
    object.insert(QStringLiteral("developers"), stringListToJsonArray(game.developers));
    object.insert(QStringLiteral("publishers"), stringListToJsonArray(game.publishers));
    object.insert(QStringLiteral("shortDescription"), cleanText(game.shortDescription));
    object.insert(QStringLiteral("headerImageUrl"), cleanText(game.headerImageUrl));
    object.insert(QStringLiteral("metadataStatus"), cleanText(game.metadataStatus));
    object.insert(QStringLiteral("pcGamingWikiPageName"), cleanText(game.pcGamingWikiPageName));
    object.insert(QStringLiteral("pcGamingWikiPageId"), cleanText(game.pcGamingWikiPageId));
    object.insert(QStringLiteral("isManualGame"), game.isManualGame);
    object.insert(QStringLiteral("savePathCandidate"), cleanText(game.savePath));
    object.insert(QStringLiteral("savePathSource"), cleanText(game.savePathSource));
    object.insert(QStringLiteral("savePathAvailability"), cleanText(game.savePathAvailability));
    object.insert(QStringLiteral("savePathDetail"), cleanText(game.savePathDetail));
    object.insert(QStringLiteral("cloudDirectory"), cleanText(cloudDirectory));
    object.insert(QStringLiteral("updatedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return object;
}

GameInfo ConfigCloudSync::gameFromJson(const QJsonObject &object) const
{
    GameInfo game;
    game.appId = cleanText(object.value(QStringLiteral("appId")).toString());
    game.name = cleanText(object.value(QStringLiteral("name")).toString());
    game.localizedName = cleanText(object.value(QStringLiteral("localizedName")).toString());
    game.displayName = cleanText(object.value(QStringLiteral("displayName")).toString());
    game.developers = jsonArrayToStringList(object.value(QStringLiteral("developers")).toArray());
    game.publishers = jsonArrayToStringList(object.value(QStringLiteral("publishers")).toArray());
    game.shortDescription = cleanText(object.value(QStringLiteral("shortDescription")).toString());
    game.headerImageUrl = cleanText(object.value(QStringLiteral("headerImageUrl")).toString());
    game.metadataStatus = cleanText(object.value(QStringLiteral("metadataStatus")).toString());
    game.pcGamingWikiPageName = cleanText(object.value(QStringLiteral("pcGamingWikiPageName")).toString());
    game.pcGamingWikiPageId = cleanText(object.value(QStringLiteral("pcGamingWikiPageId")).toString());
    game.isManualGame = object.value(QStringLiteral("isManualGame")).toBool(false);

    const QString savePathCandidate = cleanText(object.value(QStringLiteral("savePathCandidate")).toString());
    if (!savePathCandidate.isEmpty()) {
        game.savePath = QDir::toNativeSeparators(QDir::cleanPath(savePathCandidate));
        game.savePathStatus = QStringLiteral("需确认存档目录");
        game.savePathSource = cleanText(object.value(QStringLiteral("savePathSource")).toString());
        game.savePathAvailability = QStringLiteral("待确认");
        game.savePathDetail = QStringLiteral("来自配置云同步的历史存档路径，换设备后请重新确认目录后再同步");
        game.savePathCanSync = false;
    }

    return game;
}

QStringList ConfigCloudSync::jsonArrayToStringList(const QJsonArray &array) const
{
    QStringList values;
    for (const QJsonValue &value : array) {
        const QString text = cleanText(value.toString());
        if (!text.isEmpty()) {
            values.append(text);
        }
    }
    return values;
}

QJsonArray ConfigCloudSync::stringListToJsonArray(const QStringList &values) const
{
    QJsonArray array;
    for (const QString &value : values) {
        const QString text = cleanText(value);
        if (!text.isEmpty()) {
            array.append(text);
        }
    }
    return array;
}
