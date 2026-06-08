#include "SavePathValidator.h"

#include <QDir>
#include <QDirIterator>
#include <QRegularExpression>

SavePathValidator::Result SavePathValidator::validate(
    const QString &path,
    const QString &appId,
    const QString &steamPath) const
{
    const QString cleanPath = QDir::cleanPath(path.trimmed());
    if (cleanPath.isEmpty()) {
        return {QStringLiteral("需用户手动指定"), QStringLiteral("路径为空"), false};
    }

    /*
     * PCGamingWiki 有些路径会包含 <SteamID>、<uid> 这类占位符。
     * 这种路径说明我们知道大概位置，但不知道具体用户目录，不能直接同步。
     */
    if (containsUnresolvedPlaceholder(cleanPath)) {
        return {QStringLiteral("需确认存档目录"), QStringLiteral("路径包含未解析占位符"), false};
    }

    const QDir saveDir(cleanPath);
    if (!saveDir.exists()) {
        return {QStringLiteral("未生成存档"), QStringLiteral("目录不存在"), false};
    }

    if (!directoryHasFiles(cleanPath)) {
        if (m_steamCloudDetector.hasSteamCloudCacheFiles(steamPath, appId)) {
            return {
                QStringLiteral("本地存档目录为空，Steam已云存档，无需同步"),
                QStringLiteral("本地目录存在但没有存档文件，Steam userdata 中检测到该游戏的云存档缓存"),
                false
            };
        }

        return {QStringLiteral("存档目录为空"), QStringLiteral("目录存在但没有文件"), false};
    }

    return {QStringLiteral("可同步"), QStringLiteral("目录存在且包含文件"), true};
}

bool SavePathValidator::containsUnresolvedPlaceholder(const QString &path) const
{
    static const QRegularExpression placeholderRegex(QStringLiteral(R"(<[^<>]+>)"));
    return placeholderRegex.match(path).hasMatch();
}

bool SavePathValidator::directoryHasFiles(const QString &path) const
{
    QDirIterator iterator(path, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    return iterator.hasNext();
}
