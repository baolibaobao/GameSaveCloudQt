#include "GameProcessMonitor.h"

#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

GameProcessMonitor::GameProcessMonitor(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &GameProcessMonitor::pollProcesses);
}

void GameProcessMonitor::setGames(const QList<GameInfo> &games)
{
    m_games.clear();
    QSet<QString> activeAppIds;

    for (const GameInfo &game : games) {
        const QString processName = QFileInfo(game.processName).fileName().trimmed();
        if (game.appId.trimmed().isEmpty() || processName.isEmpty()) {
            continue;
        }

        TrackedGame trackedGame;
        trackedGame.appId = game.appId;
        trackedGame.processName = processName;
        m_games.insert(game.appId, trackedGame);
        activeAppIds.insert(game.appId);
    }

    for (auto iterator = m_lastRunningState.begin(); iterator != m_lastRunningState.end();) {
        if (!activeAppIds.contains(iterator.key())) {
            iterator = m_lastRunningState.erase(iterator);
        } else {
            ++iterator;
        }
    }

    for (auto iterator = m_observedAppIds.begin(); iterator != m_observedAppIds.end();) {
        if (!activeAppIds.contains(*iterator)) {
            iterator = m_observedAppIds.erase(iterator);
        } else {
            ++iterator;
        }
    }

    /*
     * 游戏列表刷新后马上轮询一次，避免 UI 必须等 2 秒才显示当前运行状态。
     * m_lastRunningState 不清空：这样后续如果某个游戏刚从运行变为关闭，
     * 仍然可以识别到“关闭”这个边沿事件，为后面的自动同步触发器做准备。
     */
    pollProcesses();
}

void GameProcessMonitor::start()
{
    if (!m_timer.isActive()) {
        m_timer.start();
    }

    pollProcesses();
}

void GameProcessMonitor::stop()
{
    m_timer.stop();
}

bool GameProcessMonitor::hasRunningGames() const
{
    for (auto iterator = m_lastRunningState.cbegin(); iterator != m_lastRunningState.cend(); ++iterator) {
        if (iterator.value()) {
            return true;
        }
    }

    return false;
}

int GameProcessMonitor::trackedGameCount() const
{
    return m_games.count();
}

void GameProcessMonitor::pollProcesses()
{
    if (m_games.isEmpty()) {
        return;
    }

    const QSet<QString> runningNames = runningProcessNames();

    for (const TrackedGame &game : m_games) {
        const QString normalizedProcessName = game.processName.toLower();
        const bool isRunning = runningNames.contains(normalizedProcessName);
        const bool wasRunning = m_lastRunningState.value(game.appId, false);
        const bool hasObservedBefore = m_observedAppIds.contains(game.appId);

        m_lastRunningState.insert(game.appId, isRunning);
        m_observedAppIds.insert(game.appId);

        if (isRunning) {
            if (!wasRunning) {
                emit processStatusChanged(game.appId, QStringLiteral("游戏正在运行，等待关闭后再进行云同步"));
                emit gameProcessStarted(game.appId, game.processName);
            }
            continue;
        }

        if (wasRunning) {
            emit processStatusChanged(game.appId, QStringLiteral("游戏已关闭，后续阶段将触发云同步"));
            emit gameProcessClosed(game.appId, game.processName);
        } else if (!hasObservedBefore) {
            emit processStatusChanged(game.appId, QStringLiteral("游戏未运行，等待启动"));
        }
    }
}

QSet<QString> GameProcessMonitor::runningProcessNames() const
{
    QSet<QString> processNames;

#ifdef Q_OS_WIN
    /*
     * Toolhelp API 是 Windows 上枚举进程的经典方式：
     * 1. CreateToolhelp32Snapshot 创建当前进程快照。
     * 2. Process32FirstW 读取第一个进程。
     * 3. Process32NextW 循环读取后续进程。
     *
     * 这里我们只需要 exe 文件名，不需要打开进程句柄，也不读取进程内存，
     * 因此权限要求比较低，普通用户运行软件即可完成监控。
     */
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return processNames;
    }

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(snapshot, &entry)) {
        do {
            const QString processName = QString::fromWCharArray(entry.szExeFile).trimmed().toLower();
            if (!processName.isEmpty()) {
                processNames.insert(processName);
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
#endif

    return processNames;
}
