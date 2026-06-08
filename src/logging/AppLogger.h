#pragma once

#include <QObject>
#include <QDateTime>
#include <QString>
#include <QUrl>

#include "models/LogListModel.h"

class AppLogger : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString logDirectory READ logDirectory NOTIFY logDirectoryChanged)
    Q_PROPERTY(QString logFilePath READ logFilePath NOTIFY logFilePathChanged)
    Q_PROPERTY(LogListModel *logModel READ logModel CONSTANT)

public:
    explicit AppLogger(QObject *parent = nullptr);

    QString logDirectory() const;
    QString logFilePath() const;
    LogListModel *logModel();

    Q_INVOKABLE bool setLogDirectory(const QUrl &folderUrl);
    Q_INVOKABLE void clearVisibleLogs();
    Q_INVOKABLE bool openLogDirectory() const;

    bool setLogDirectoryPath(const QString &path);
    void debug(const QString &message);
    void info(const QString &message);
    void warning(const QString &message);
    void error(const QString &message);

signals:
    void logDirectoryChanged();
    void logFilePathChanged();

private:
    QString defaultLogDirectory() const;
    QString loadLogDirectory() const;
    bool saveLogDirectory(const QString &path) const;
    QString currentLogFilePath() const;
    QString dailyLogFilePath(const QDateTime &timestamp) const;
    QString levelName(const QString &level) const;
    void write(const QString &level, const QString &message);
    bool appendLineToFile(const QString &filePath, const QString &line) const;

    QString m_logDirectory;
    LogListModel m_logModel;
};
