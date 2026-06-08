#include "AppSettings.h"

#include <QCoreApplication>
#include <QDir>

QString AppSettings::configDirectoryPath()
{
    /*
     * 软件自身配置采用便携式保存方式，不再写入 Windows 注册表。
     * 生成位置固定在 exe 同目录下：
     *
     *   <软件目录>/config/settings.ini
     *
     * 用户如果想彻底清空软件配置，只需要删除 config 文件夹。
     */
    const QString applicationDir = QCoreApplication::applicationDirPath();
    return QDir::toNativeSeparators(QDir(applicationDir).absoluteFilePath(QStringLiteral("config")));
}

QString AppSettings::settingsFilePath()
{
    QDir configDir(configDirectoryPath());
    if (!configDir.exists()) {
        configDir.mkpath(QStringLiteral("."));
    }

    return QDir::toNativeSeparators(configDir.absoluteFilePath(QStringLiteral("settings.ini")));
}

std::unique_ptr<QSettings> AppSettings::create()
{
    return std::make_unique<QSettings>(settingsFilePath(), QSettings::IniFormat);
}
