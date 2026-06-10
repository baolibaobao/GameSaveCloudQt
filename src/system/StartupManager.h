#pragma once

#include <QString>

class StartupManager
{
public:
    bool isSupported() const;
    bool isEnabled() const;
    bool setEnabled(bool enabled, QString *errorMessage = nullptr) const;

private:
    QString startupDirectoryPath() const;
    QString shortcutPath() const;
    bool createShortcut(QString *errorMessage) const;
    bool removeShortcut(QString *errorMessage) const;
};
