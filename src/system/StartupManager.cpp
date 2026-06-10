#include "system/StartupManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windows.h>
#endif

#include <string>

bool StartupManager::isSupported() const
{
#ifdef Q_OS_WIN
    return !startupDirectoryPath().isEmpty();
#else
    return false;
#endif
}

bool StartupManager::isEnabled() const
{
    return QFileInfo::exists(shortcutPath());
}

bool StartupManager::setEnabled(bool enabled, QString *errorMessage) const
{
    if (!isSupported()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("当前系统不支持自动创建开机自启项");
        }
        return false;
    }

    return enabled ? createShortcut(errorMessage) : removeShortcut(errorMessage);
}

QString StartupManager::startupDirectoryPath() const
{
#ifdef Q_OS_WIN
    PWSTR rawPath = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_Startup, 0, nullptr, &rawPath);
    if (FAILED(result) || !rawPath) {
        return {};
    }

    const QString path = QString::fromWCharArray(rawPath);
    CoTaskMemFree(rawPath);
    return QDir::toNativeSeparators(path);
#else
    return {};
#endif
}

QString StartupManager::shortcutPath() const
{
    const QString directoryPath = startupDirectoryPath();
    if (directoryPath.isEmpty()) {
        return {};
    }

    return QDir(directoryPath).absoluteFilePath(QStringLiteral("GameSaveCloud-Qt.lnk"));
}

bool StartupManager::createShortcut(QString *errorMessage) const
{
#ifdef Q_OS_WIN
    const QString directoryPath = startupDirectoryPath();
    if (directoryPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法定位 Windows 启动文件夹");
        }
        return false;
    }

    if (!QDir().mkpath(directoryPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建 Windows 启动文件夹：%1").arg(directoryPath);
        }
        return false;
    }

    const QString executablePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString workingDirectory = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    const QString linkPath = QDir::toNativeSeparators(shortcutPath());

    /*
     * 开机自启使用用户启动文件夹中的 .lnk 快捷方式实现：
     * - 不把软件配置写进注册表；
     * - 用户可以在 Windows 启动文件夹里直观看到并删除；
     * - 快捷方式目标始终指向当前运行的 exe。
     */
    const HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitializedHere = SUCCEEDED(initResult);
    if (FAILED(initResult) && initResult != RPC_E_CHANGED_MODE) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("初始化 Windows 快捷方式组件失败：0x%1")
                                .arg(static_cast<qulonglong>(initResult), 0, 16);
        }
        return false;
    }

    IShellLinkW *shellLink = nullptr;
    HRESULT result = CoCreateInstance(
        CLSID_ShellLink,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IShellLinkW,
        reinterpret_cast<void **>(&shellLink));
    if (FAILED(result) || !shellLink) {
        if (comInitializedHere) {
            CoUninitialize();
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("创建 Windows 快捷方式对象失败：0x%1")
                                .arg(static_cast<qulonglong>(result), 0, 16);
        }
        return false;
    }

    const std::wstring executableWide = executablePath.toStdWString();
    const std::wstring workingWide = workingDirectory.toStdWString();
    shellLink->SetPath(executableWide.c_str());
    shellLink->SetWorkingDirectory(workingWide.c_str());
    shellLink->SetDescription(L"GameSaveCloud-Qt");

    IPersistFile *persistFile = nullptr;
    result = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&persistFile));
    if (SUCCEEDED(result) && persistFile) {
        const std::wstring linkWide = linkPath.toStdWString();
        result = persistFile->Save(linkWide.c_str(), TRUE);
        persistFile->Release();
    }

    shellLink->Release();
    if (comInitializedHere) {
        CoUninitialize();
    }

    if (FAILED(result)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("保存开机自启快捷方式失败：0x%1")
                                .arg(static_cast<qulonglong>(result), 0, 16);
        }
        return false;
    }

    return true;
#else
    if (errorMessage) {
        *errorMessage = QStringLiteral("当前系统不支持 Windows 启动文件夹快捷方式");
    }
    return false;
#endif
}

bool StartupManager::removeShortcut(QString *errorMessage) const
{
    const QString linkPath = shortcutPath();
    if (linkPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法定位开机自启快捷方式路径");
        }
        return false;
    }

    if (!QFileInfo::exists(linkPath)) {
        return true;
    }

    if (!QFile::remove(linkPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("删除开机自启快捷方式失败：%1").arg(linkPath);
        }
        return false;
    }

    return true;
}
