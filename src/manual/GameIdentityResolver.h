#pragma once

#include <QString>
#include <QStringList>

#include "models/GameInfo.h"

class GameIdentityResolver
{
public:
    QString manualAppIdForExecutable(const QString &executablePath) const;
    QStringList candidateNamesForExecutable(const QString &executablePath) const;
    GameInfo createPendingManualGame(const QString &executablePath) const;

private:
    QString cleanedName(QString value) const;
};
