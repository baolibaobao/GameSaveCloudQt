#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>

#include "models/GameInfo.h"

class ManualGameManager
{
public:
    QList<GameInfo> loadManualGames() const;
    bool saveManualGame(const GameInfo &game) const;

private:
    QVariantMap gameToMap(const GameInfo &game) const;
    GameInfo mapToGame(const QVariantMap &map) const;
};
