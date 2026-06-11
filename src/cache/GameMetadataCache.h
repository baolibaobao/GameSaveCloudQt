#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "models/GameInfo.h"

class GameMetadataCache
{
public:
    GameMetadataCache();

    QString cacheDirectoryPath() const;
    QString cacheFilePath() const;

    void reload();
    QList<GameInfo> cachedGames() const;
    bool applyToGame(GameInfo &game) const;
    int applyToGames(QList<GameInfo> &games) const;
    bool saveGame(const GameInfo &game);
    bool saveGames(const QList<GameInfo> &games);

private:
    void ensureLoaded() const;
    bool write() const;
    QJsonObject gameToJson(const GameInfo &game) const;
    void applyJsonToGame(const QJsonObject &object, GameInfo &game) const;
    GameInfo jsonToGame(const QJsonObject &object) const;
    QStringList jsonArrayToStringList(const QJsonArray &array) const;
    QJsonArray stringListToJsonArray(const QStringList &values) const;

    mutable bool m_loaded = false;
    mutable QHash<QString, QJsonObject> m_games;
};
