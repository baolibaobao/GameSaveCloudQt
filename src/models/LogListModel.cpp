#include "LogListModel.h"

#include <algorithm>

LogListModel::LogListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int LogListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }

    return visibleEntries().count();
}

QVariant LogListModel::data(const QModelIndex &index, int role) const
{
    const QList<LogEntry> entries = visibleEntries();
    if (!index.isValid() || index.row() < 0 || index.row() >= entries.count()) {
        return {};
    }

    const LogEntry &entry = entries.at(index.row());
    switch (role) {
    case TimestampRole:
        return entry.timestamp;
    case TimeTextRole:
        return entry.timestamp.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    case LevelRole:
        return entry.level;
    case LevelNameRole:
        return entry.levelName;
    case MessageRole:
        return entry.message;
    default:
        return {};
    }
}

QHash<int, QByteArray> LogListModel::roleNames() const
{
    return {
        {TimestampRole, "timestamp"},
        {TimeTextRole, "timeText"},
        {LevelRole, "level"},
        {LevelNameRole, "levelName"},
        {MessageRole, "message"}
    };
}

void LogListModel::append(const LogEntry &entry)
{
    constexpr int maximumVisibleEntries = 1000;
    const bool wasVisible = accepts(entry);
    const int oldTotalCount = m_allEntries.count();

    if (wasVisible) {
        beginInsertRows({}, 0, 0);
        m_allEntries.append(entry);
        endInsertRows();
        emit countChanged();
    } else {
        m_allEntries.append(entry);
    }

    if (m_allEntries.count() != oldTotalCount) {
        emit totalCountChanged();
    }

    if (m_allEntries.count() <= maximumVisibleEntries) {
        return;
    }

    beginResetModel();
    const int removeCount = m_allEntries.count() - maximumVisibleEntries;
    m_allEntries.erase(m_allEntries.begin(), m_allEntries.begin() + removeCount);
    endResetModel();
    emit countChanged();
    emit totalCountChanged();
}

void LogListModel::clear()
{
    if (m_allEntries.isEmpty()) {
        return;
    }

    beginResetModel();
    m_allEntries.clear();
    endResetModel();
    emit countChanged();
    emit totalCountChanged();
}

int LogListModel::totalCount() const
{
    return m_allEntries.count();
}

QString LogListModel::filterLevel() const
{
    return m_filterLevel;
}

void LogListModel::setFilterLevel(const QString &filterLevel)
{
    const QString cleanFilter = filterLevel.trimmed().isEmpty() ? QStringLiteral("all") : filterLevel.trimmed();
    if (m_filterLevel == cleanFilter) {
        return;
    }

    beginResetModel();
    m_filterLevel = cleanFilter;
    endResetModel();
    emit filterLevelChanged();
    emit countChanged();
}

QVariantMap LogListModel::get(int row) const
{
    QVariantMap map;
    const QList<LogEntry> entries = visibleEntries();
    if (row < 0 || row >= entries.count()) {
        return map;
    }

    const LogEntry &entry = entries.at(row);
    map.insert(QStringLiteral("timestamp"), entry.timestamp);
    map.insert(QStringLiteral("timeText"), entry.timestamp.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    map.insert(QStringLiteral("level"), entry.level);
    map.insert(QStringLiteral("levelName"), entry.levelName);
    map.insert(QStringLiteral("message"), entry.message);
    return map;
}

bool LogListModel::accepts(const LogEntry &entry) const
{
    return m_filterLevel == QStringLiteral("all") || entry.level == m_filterLevel;
}

QList<LogEntry> LogListModel::visibleEntries() const
{
    if (m_filterLevel == QStringLiteral("all")) {
        QList<LogEntry> entries = m_allEntries;
        std::reverse(entries.begin(), entries.end());
        return entries;
    }

    QList<LogEntry> entries;
    for (auto iterator = m_allEntries.crbegin(); iterator != m_allEntries.crend(); ++iterator) {
        const LogEntry &entry = *iterator;
        if (accepts(entry)) {
            entries.append(entry);
        }
    }

    return entries;
}
