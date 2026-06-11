#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "models/GameInfo.h"

struct ConfigCloudSyncImportResult
{
    bool valid = false;
    QString error;
    QList<GameInfo> games;
};

class ConfigCloudSync
{
public:
    QJsonObject buildDocument(
        const QList<GameInfo> &localGames,
        const QHash<QString, QString> &cloudDirectoriesByAppId,
        const QJsonObject &existingDocument) const;

    ConfigCloudSyncImportResult parseDocument(const QByteArray &data) const;
    bool mergeImportedGame(const GameInfo &importedGame, GameInfo &localGame) const;

private:
    QJsonObject gameToJson(const GameInfo &game, const QString &cloudDirectory) const;
    GameInfo gameFromJson(const QJsonObject &object) const;
    QStringList jsonArrayToStringList(const QJsonArray &array) const;
    QJsonArray stringListToJsonArray(const QStringList &values) const;
};
