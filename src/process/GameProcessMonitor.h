#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include "models/GameInfo.h"

class GameProcessMonitor : public QObject
{
    Q_OBJECT

public:
    explicit GameProcessMonitor(QObject *parent = nullptr);

    void setGames(const QList<GameInfo> &games);
    void start();
    void stop();
    bool hasRunningGames() const;
    bool isGameRunning(const QString &appId) const;
    int trackedGameCount() const;

signals:
    void processStatusChanged(const QString &appId, const QString &runningStatus);
    void gameProcessStarted(const QString &appId, const QString &processName);
    void gameProcessClosed(const QString &appId, const QString &processName);

private:
    struct TrackedGame
    {
        QString appId;
        QString processName;
    };

    void pollProcesses();
    QSet<QString> runningProcessNames() const;

    QTimer m_timer;
    QHash<QString, TrackedGame> m_games;
    QHash<QString, bool> m_lastRunningState;
    QSet<QString> m_observedAppIds;
};
