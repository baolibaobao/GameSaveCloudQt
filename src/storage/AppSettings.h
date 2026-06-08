#pragma once

#include <memory>

#include <QSettings>
#include <QString>

class AppSettings
{
public:
    static QString configDirectoryPath();
    static QString settingsFilePath();
    static std::unique_ptr<QSettings> create();
};
