#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QVariantMap>

struct LogEntry
{
    QDateTime timestamp;
    QString level;
    QString levelName;
    QString message;
};

class LogListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY totalCountChanged)
    Q_PROPERTY(QString filterLevel READ filterLevel WRITE setFilterLevel NOTIFY filterLevelChanged)

public:
    enum LogRoles {
        TimestampRole = Qt::UserRole + 1,
        TimeTextRole,
        LevelRole,
        LevelNameRole,
        MessageRole
    };
    Q_ENUM(LogRoles)

    explicit LogListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void append(const LogEntry &entry);
    void clear();
    int totalCount() const;
    QString filterLevel() const;
    void setFilterLevel(const QString &filterLevel);

    Q_INVOKABLE QVariantMap get(int row) const;

signals:
    void countChanged();
    void totalCountChanged();
    void filterLevelChanged();

private:
    bool accepts(const LogEntry &entry) const;
    QList<LogEntry> visibleEntries() const;

    QList<LogEntry> m_allEntries;
    QString m_filterLevel = QStringLiteral("all");
};
