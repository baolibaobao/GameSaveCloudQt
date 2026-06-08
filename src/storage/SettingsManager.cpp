#include "SettingsManager.h"

#include <QDir>

#include "AppSettings.h"

SettingsManager::SettingsManager() = default;

QString SettingsManager::manualSavePathForApp(const QString &appId) const
{
    const QString cleanAppId = appId.trimmed();
    if (cleanAppId.isEmpty()) {
        return {};
    }

    const std::unique_ptr<QSettings> settings = AppSettings::create();
    const QString path = settings->value(manualSavePathKey(cleanAppId)).toString().trimmed();
    if (path.isEmpty()) {
        return {};
    }

    return QDir::toNativeSeparators(QDir::cleanPath(path));
}

bool SettingsManager::setManualSavePathForApp(const QString &appId, const QString &path)
{
    const QString cleanAppId = appId.trimmed();
    const QString cleanPath = QDir::cleanPath(path.trimmed());

    if (cleanAppId.isEmpty() || cleanPath.isEmpty()) {
        return false;
    }

    const std::unique_ptr<QSettings> settings = AppSettings::create();
    settings->setValue(manualSavePathKey(cleanAppId), QDir::toNativeSeparators(cleanPath));
    settings->sync();
    return settings->status() == QSettings::NoError;
}

QString SettingsManager::manualSavePathKey(const QString &appId) const
{
    return QStringLiteral("manualSavePaths/%1").arg(appId);
}
