#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include "PCGamingWikiSaveProvider.h"
#include "models/GameInfo.h"
#include "SavePathValidator.h"
#include "SteamAutoCloudSaveProvider.h"
#include "storage/SettingsManager.h"

class SavePathResolver : public QObject
{
    Q_OBJECT

public:
    explicit SavePathResolver(QObject *parent = nullptr);

    void setSteamPath(const QString &steamPath);
    bool applyManualSavePath(GameInfo &game) const;
    void resolveGameSavePath(const GameInfo &game);
    bool setManualSavePath(const QString &appId, const QString &path);

signals:
    void savePathResolved(
        const QString &appId,
        const QString &path,
        const QString &source,
        const QString &availability,
        const QString &detail,
        bool canSync);
    void savePathNeedsManual(const QString &appId, const QString &reason);

private:
    QString normalizePcGamingWikiPath(QString rawPath, const GameInfo &game) const;
    QString resolvePathTemplateValue(const QString &value, const GameInfo &game) const;
    QString installPathForGame(const GameInfo &game) const;
    QString userProfilePath() const;
    QString savedGamesPath() const;
    void rememberGame(const GameInfo &game);
    bool isNumericAppId(const QString &appId) const;

    QString m_steamPath;
    SteamAutoCloudSaveProvider m_steamAutoCloudSaveProvider;
    PCGamingWikiSaveProvider m_pcGamingWikiProvider;
    SavePathValidator m_savePathValidator;
    SettingsManager m_settingsManager;
    QHash<QString, GameInfo> m_pendingGames;
};
