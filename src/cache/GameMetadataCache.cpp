#include "GameMetadataCache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>

#include "storage/AppSettings.h"

GameMetadataCache::GameMetadataCache() = default;

QString GameMetadataCache::cacheDirectoryPath() const
{
    return QDir::toNativeSeparators(
        QDir(AppSettings::configDirectoryPath()).absoluteFilePath(QStringLiteral("game-data-cache")));
}

QString GameMetadataCache::cacheFilePath() const
{
    QDir cacheDir(cacheDirectoryPath());
    if (!cacheDir.exists()) {
        cacheDir.mkpath(QStringLiteral("."));
    }

    return QDir::toNativeSeparators(cacheDir.absoluteFilePath(QStringLiteral("game-metadata-cache.json")));
}

void GameMetadataCache::reload()
{
    m_loaded = false;
    m_games.clear();
    ensureLoaded();
}

bool GameMetadataCache::applyToGame(GameInfo &game) const
{
    ensureLoaded();

    const QString cleanAppId = game.appId.trimmed();
    if (cleanAppId.isEmpty() || !m_games.contains(cleanAppId)) {
        return false;
    }

    applyJsonToGame(m_games.value(cleanAppId), game);
    return true;
}

int GameMetadataCache::applyToGames(QList<GameInfo> &games) const
{
    int appliedCount = 0;
    for (GameInfo &game : games) {
        if (applyToGame(game)) {
            ++appliedCount;
        }
    }

    return appliedCount;
}

bool GameMetadataCache::saveGame(const GameInfo &game)
{
    const QString cleanAppId = game.appId.trimmed();
    if (cleanAppId.isEmpty()) {
        return false;
    }

    ensureLoaded();
    m_games.insert(cleanAppId, gameToJson(game));
    return write();
}

void GameMetadataCache::ensureLoaded() const
{
    if (m_loaded) {
        return;
    }

    m_loaded = true;
    m_games.clear();

    QFile file(cacheFilePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return;
    }

    const QJsonObject gamesObject = document.object().value(QStringLiteral("games")).toObject();
    for (auto it = gamesObject.constBegin(); it != gamesObject.constEnd(); ++it) {
        const QString appId = it.key().trimmed();
        const QJsonObject gameObject = it.value().toObject();
        if (!appId.isEmpty() && !gameObject.isEmpty()) {
            m_games.insert(appId, gameObject);
        }
    }
}

bool GameMetadataCache::write() const
{
    QDir cacheDir(cacheDirectoryPath());
    if (!cacheDir.exists() && !cacheDir.mkpath(QStringLiteral("."))) {
        return false;
    }

    QJsonObject gamesObject;
    for (auto it = m_games.constBegin(); it != m_games.constEnd(); ++it) {
        gamesObject.insert(it.key(), it.value());
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("games"), gamesObject);

    QSaveFile file(cacheFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

QJsonObject GameMetadataCache::gameToJson(const GameInfo &game) const
{
    QJsonObject object;
    object.insert(QStringLiteral("appId"), game.appId);
    object.insert(QStringLiteral("name"), game.name);
    object.insert(QStringLiteral("localizedName"), game.localizedName);
    object.insert(QStringLiteral("displayName"), game.displayName);
    object.insert(QStringLiteral("developers"), stringListToJsonArray(game.developers));
    object.insert(QStringLiteral("publishers"), stringListToJsonArray(game.publishers));
    object.insert(QStringLiteral("shortDescription"), game.shortDescription);
    object.insert(QStringLiteral("headerImageUrl"), game.headerImageUrl);
    object.insert(QStringLiteral("savePath"), game.savePath);
    object.insert(QStringLiteral("savePathStatus"), game.savePathStatus);
    object.insert(QStringLiteral("savePathSource"), game.savePathSource);
    object.insert(QStringLiteral("savePathAvailability"), game.savePathAvailability);
    object.insert(QStringLiteral("savePathDetail"), game.savePathDetail);
    object.insert(QStringLiteral("savePathCanSync"), game.savePathCanSync);
    object.insert(QStringLiteral("pcGamingWikiPageName"), game.pcGamingWikiPageName);
    object.insert(QStringLiteral("pcGamingWikiPageId"), game.pcGamingWikiPageId);
    object.insert(QStringLiteral("metadataStatus"), game.metadataStatus);
    object.insert(QStringLiteral("lastUpdated"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return object;
}

void GameMetadataCache::applyJsonToGame(const QJsonObject &object, GameInfo &game) const
{
    // 缓存只覆盖网络元数据和存档识别结果；安装目录、manifest、进程名仍以本次本地扫描为准。
    const QString localizedName = object.value(QStringLiteral("localizedName")).toString().trimmed();
    const QString displayName = object.value(QStringLiteral("displayName")).toString().trimmed();
    const QString headerImageUrl = object.value(QStringLiteral("headerImageUrl")).toString().trimmed();
    const QString savePath = object.value(QStringLiteral("savePath")).toString().trimmed();
    const QString savePathStatus = object.value(QStringLiteral("savePathStatus")).toString().trimmed();

    if (!localizedName.isEmpty()) {
        game.localizedName = localizedName;
    }

    if (!displayName.isEmpty()) {
        game.displayName = displayName;
    }

    if (!headerImageUrl.isEmpty()) {
        game.headerImageUrl = headerImageUrl;
    }

    game.developers = jsonArrayToStringList(object.value(QStringLiteral("developers")).toArray());
    game.publishers = jsonArrayToStringList(object.value(QStringLiteral("publishers")).toArray());
    game.shortDescription = object.value(QStringLiteral("shortDescription")).toString().trimmed();

    const QString metadataStatus = object.value(QStringLiteral("metadataStatus")).toString().trimmed();
    if (!metadataStatus.isEmpty()) {
        game.metadataStatus = metadataStatus;
    }

    if (!savePath.isEmpty() || !savePathStatus.isEmpty()) {
        game.savePath = savePath.isEmpty() ? QString() : QDir::toNativeSeparators(QDir::cleanPath(savePath));
        game.savePathStatus = savePathStatus;
        game.savePathSource = object.value(QStringLiteral("savePathSource")).toString().trimmed();
        game.savePathAvailability = object.value(QStringLiteral("savePathAvailability")).toString().trimmed();
        game.savePathDetail = object.value(QStringLiteral("savePathDetail")).toString().trimmed();
        game.savePathCanSync = object.value(QStringLiteral("savePathCanSync")).toBool(false);
    }

    const QString pageName = object.value(QStringLiteral("pcGamingWikiPageName")).toString().trimmed();
    if (!pageName.isEmpty()) {
        game.pcGamingWikiPageName = pageName;
    }

    const QString pageId = object.value(QStringLiteral("pcGamingWikiPageId")).toString().trimmed();
    if (!pageId.isEmpty()) {
        game.pcGamingWikiPageId = pageId;
    }
}

QStringList GameMetadataCache::jsonArrayToStringList(const QJsonArray &array) const
{
    QStringList values;
    for (const QJsonValue &value : array) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) {
            values.append(text);
        }
    }

    return values;
}

QJsonArray GameMetadataCache::stringListToJsonArray(const QStringList &values) const
{
    QJsonArray array;
    for (const QString &value : values) {
        const QString text = value.trimmed();
        if (!text.isEmpty()) {
            array.append(text);
        }
    }

    return array;
}
