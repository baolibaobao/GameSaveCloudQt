#include "AppLogger.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QStringConverter>

#include "storage/AppSettings.h"

AppLogger::AppLogger(QObject *parent)
    : QObject(parent),
      m_logDirectory(loadLogDirectory())
{
}

QString AppLogger::logDirectory() const
{
    return m_logDirectory;
}

QString AppLogger::logFilePath() const
{
    return currentLogFilePath();
}

LogListModel *AppLogger::logModel()
{
    return &m_logModel;
}

bool AppLogger::setLogDirectory(const QUrl &folderUrl)
{
    const QString localPath = folderUrl.isLocalFile() ? folderUrl.toLocalFile() : folderUrl.toString();
    return setLogDirectoryPath(localPath);
}

bool AppLogger::setLogDirectoryPath(const QString &path)
{
    const QString localPath = path;
    const QString cleanPath = QDir::cleanPath(localPath.trimmed());
    if (cleanPath.isEmpty()) {
        warning(QStringLiteral("日志目录设置失败：选择的目录为空"));
        return false;
    }

    /*
     * 日志目录由用户指定后立即创建。这样后续 WebDAV、快照或扫描模块写日志时，
     * 不需要每个模块重复判断目录是否存在，也能尽早暴露权限问题。
     */
    QDir dir(cleanPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        error(QStringLiteral("日志目录设置失败：无法创建目录 %1，请检查磁盘权限").arg(QDir::toNativeSeparators(cleanPath)));
        return false;
    }

    const QString nativePath = QDir::toNativeSeparators(dir.absolutePath());
    if (!saveLogDirectory(nativePath)) {
        error(QStringLiteral("日志目录设置失败：无法写入软件设置"));
        return false;
    }

    if (m_logDirectory != nativePath) {
        m_logDirectory = nativePath;
        emit logDirectoryChanged();
        emit logFilePathChanged();
    }

    info(QStringLiteral("日志输出目录已设置为：%1").arg(m_logDirectory));
    return true;
}

void AppLogger::clearVisibleLogs()
{
    /*
     * 这里只清空软件界面里的最近日志，不删除本地 .log 文件。
     * 本地日志是排查问题的重要依据，避免用户误点后丢失历史记录。
     */
    m_logModel.clear();
}

bool AppLogger::openLogDirectory() const
{
    QDir dir(m_logDirectory);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return false;
    }

    return QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()));
}

void AppLogger::debug(const QString &message)
{
    write(QStringLiteral("debug"), message);
}

void AppLogger::info(const QString &message)
{
    write(QStringLiteral("info"), message);
}

void AppLogger::warning(const QString &message)
{
    write(QStringLiteral("warning"), message);
}

void AppLogger::error(const QString &message)
{
    write(QStringLiteral("error"), message);
}

QString AppLogger::defaultLogDirectory() const
{
    QString documentsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documentsPath.trimmed().isEmpty()) {
        documentsPath = QDir::homePath();
    }

    return QDir::toNativeSeparators(QDir(documentsPath).absoluteFilePath(QStringLiteral("GameSaveCloudQt/logs")));
}

QString AppLogger::loadLogDirectory() const
{
    const std::unique_ptr<QSettings> settings = AppSettings::create();
    const QString configuredPath = settings->value(QStringLiteral("logging/logDirectory")).toString().trimmed();
    if (!configuredPath.isEmpty()) {
        return QDir::toNativeSeparators(QDir::cleanPath(configuredPath));
    }

    return defaultLogDirectory();
}

bool AppLogger::saveLogDirectory(const QString &path) const
{
    const std::unique_ptr<QSettings> settings = AppSettings::create();
    settings->setValue(QStringLiteral("logging/logDirectory"), QDir::toNativeSeparators(QDir::cleanPath(path)));
    settings->sync();
    return settings->status() == QSettings::NoError;
}

QString AppLogger::currentLogFilePath() const
{
    return QDir::toNativeSeparators(QDir(m_logDirectory).absoluteFilePath(QStringLiteral("game-save-operations.log")));
}

QString AppLogger::dailyLogFilePath(const QDateTime &timestamp) const
{
    const QString fileName = QStringLiteral("game-save-operations_%1.log")
                                 .arg(timestamp.toLocalTime().date().toString(QStringLiteral("yyyy-MM-dd")));
    return QDir::toNativeSeparators(QDir(m_logDirectory).absoluteFilePath(fileName));
}

QString AppLogger::levelName(const QString &level) const
{
    if (level == QStringLiteral("debug")) {
        return QStringLiteral("调试");
    }
    if (level == QStringLiteral("warning")) {
        return QStringLiteral("警告");
    }
    if (level == QStringLiteral("error")) {
        return QStringLiteral("错误");
    }

    return QStringLiteral("信息");
}

void AppLogger::write(const QString &level, const QString &message)
{
    const QDateTime now = QDateTime::currentDateTime();
    const QString cleanMessage = message.trimmed().isEmpty()
                                     ? QStringLiteral("无详细内容")
                                     : message.trimmed();

    LogEntry entry;
    entry.timestamp = now;
    entry.level = level;
    entry.levelName = levelName(level);
    entry.message = cleanMessage;
    m_logModel.append(entry);

    const QString line = QStringLiteral("[%1] [%2] %3\n")
                             .arg(now.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                             .arg(entry.levelName)
                             .arg(cleanMessage);

    /*
     * 同一条日志写入两个文件：
     * 1. game-save-operations.log：当前滚动日志，方便用户立刻打开查看。
     * 2. game-save-operations_yyyy-MM-dd.log：按日期归档，方便长期排查。
     *
     * 日志只记录操作过程、路径、HTTP 状态码等信息；后续 WebDAV 阶段接入时，
     * 绝不能把密码或 Authorization 请求头写入这里。
     */
    appendLineToFile(currentLogFilePath(), line);
    appendLineToFile(dailyLogFilePath(now), line);
}

bool AppLogger::appendLineToFile(const QString &filePath, const QString &line) const
{
    const QFileInfo fileInfo(filePath);
    QDir dir(fileInfo.absolutePath());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << line;
    return true;
}
