#include "SnapshotSettings.h"

#include <QDir>

#include "storage/AppSettings.h"

SnapshotSettings::SnapshotSettings() = default;

QString SnapshotSettings::snapshotRootPath() const
{
    const std::unique_ptr<QSettings> settings = AppSettings::create();
    return normalizedPath(settings->value(QStringLiteral("snapshots/rootPath")).toString());
}

bool SnapshotSettings::setSnapshotRootPath(const QString &path) const
{
    const QString cleanPath = normalizedPath(path);
    if (cleanPath.isEmpty()) {
        return false;
    }

    QDir dir(cleanPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return false;
    }

    const std::unique_ptr<QSettings> settings = AppSettings::create();
    settings->setValue(QStringLiteral("snapshots/rootPath"), QDir::toNativeSeparators(dir.absolutePath()));
    settings->sync();
    return settings->status() == QSettings::NoError;
}

QString SnapshotSettings::normalizedPath(const QString &path) const
{
    const QString cleanPath = QDir::cleanPath(path.trimmed());
    if (cleanPath.isEmpty() || cleanPath == QStringLiteral(".")) {
        return {};
    }

    return QDir::toNativeSeparators(cleanPath);
}
