#include "SteamCloudDetector.h"

#include <QDir>
#include <QDirIterator>

bool SteamCloudDetector::hasSteamCloudCacheFiles(const QString &steamPath, const QString &appId) const
{
    const QString cleanSteamPath = QDir::cleanPath(steamPath.trimmed());
    const QString cleanAppId = appId.trimmed();

    if (cleanSteamPath.isEmpty() || cleanAppId.isEmpty()) {
        return false;
    }

    const QDir userdataDir(QDir(cleanSteamPath).absoluteFilePath(QStringLiteral("userdata")));
    if (!userdataDir.exists()) {
        return false;
    }

    const QStringList steamUserIds = userdataDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &steamUserId : steamUserIds) {
        const QString appRootPath = userdataDir.absoluteFilePath(QStringLiteral("%1/%2").arg(steamUserId, cleanAppId));
        const QString remotePath = QDir(appRootPath).absoluteFilePath(QStringLiteral("remote"));

        /*
         * Steam Cloud 的本地缓存通常在 userdata/<SteamID>/<AppID>/remote。
         * AppID 根目录下可能只有 remotecache.vdf 等元数据；单独出现元数据时
         * 不能证明有真实云存档文件，所以这里只把 remote 目录里的文件当作强信号。
         * 我们只用这里判断“Steam 已经有云存档体系在工作”，不把这些文件纳入同步。
         */
        if (directoryHasFiles(remotePath)) {
            return true;
        }
    }

    return false;
}

bool SteamCloudDetector::directoryHasFiles(const QString &path) const
{
    const QDir dir(path);
    if (!dir.exists()) {
        return false;
    }

    QDirIterator iterator(path, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    return iterator.hasNext();
}
