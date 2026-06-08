#pragma once

#include <QString>

class SettingsManager
{
public:
    SettingsManager();

    QString manualSavePathForApp(const QString &appId) const;
    bool setManualSavePathForApp(const QString &appId, const QString &path);

private:
    QString manualSavePathKey(const QString &appId) const;
};
