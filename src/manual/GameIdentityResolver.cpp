#include "GameIdentityResolver.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

QString GameIdentityResolver::manualAppIdForExecutable(const QString &executablePath) const
{
    const QString cleanPath = QDir::cleanPath(executablePath.trimmed()).toLower();
    const QByteArray hash = QCryptographicHash::hash(cleanPath.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStringLiteral("manual:%1").arg(QString::fromLatin1(hash.left(12)));
}

QStringList GameIdentityResolver::candidateNamesForExecutable(const QString &executablePath) const
{
    const QFileInfo executableInfo(executablePath);
    QStringList candidates;

    const QString exeBaseName = cleanedName(executableInfo.completeBaseName());
    const QString parentFolderName = cleanedName(executableInfo.dir().dirName());

    /*
     * 手动选择 exe 时，我们没有 Steam 的 appmanifest 可读。
     * 因此先用两个最稳妥的来源推断游戏名：
     * 1. exe 文件名，例如 RealmOfInk.exe -> Realm Of Ink
     * 2. exe 所在文件夹名，例如 .../Realm of Ink/RealmOfInk.exe -> Realm of Ink
     *
     * PCGamingWiki 的搜索更接近“游戏页面标题”，文件夹名通常比 exe 名更自然，
     * 所以优先使用文件夹名，再把 exe 名作为兜底候选。
     */
    if (!parentFolderName.isEmpty()) {
        candidates.append(parentFolderName);
    }

    if (!exeBaseName.isEmpty() && !candidates.contains(exeBaseName, Qt::CaseInsensitive)) {
        candidates.append(exeBaseName);
    }

    return candidates;
}

GameInfo GameIdentityResolver::createPendingManualGame(const QString &executablePath) const
{
    const QFileInfo executableInfo(executablePath);
    const QString cleanExecutablePath = QDir::toNativeSeparators(QDir::cleanPath(executablePath.trimmed()));
    const QStringList candidates = candidateNamesForExecutable(cleanExecutablePath);
    const QString inferredName = candidates.isEmpty() ? executableInfo.completeBaseName() : candidates.first();

    GameInfo game;
    game.appId = manualAppIdForExecutable(cleanExecutablePath);
    game.name = inferredName;
    game.displayName = inferredName;
    game.installDir = executableInfo.dir().dirName();
    game.isManualGame = true;
    game.executablePath = cleanExecutablePath;
    game.processName = executableInfo.fileName();
    game.metadataStatus = QStringLiteral("正在通过 PCGamingWiki 搜索游戏资料");
    game.savePathStatus = QStringLiteral("正在识别存档路径");
    game.savePathSource = QStringLiteral("PCGamingWiki");
    game.savePathAvailability = QStringLiteral("等待检测");
    game.savePathDetail = QStringLiteral("等待 PCGamingWiki 匹配游戏页面");
    game.savePathCanSync = false;
    game.runningStatus = QStringLiteral("等待进程监控");
    game.syncStatus = QStringLiteral("未同步");
    game.snapshotStatus = QStringLiteral("未检查本地快照");
    game.snapshotDetail = QStringLiteral("等待用户设置快照目录并执行快照预处理检查");
    game.snapshotNeedsCreate = false;
    return game;
}

QString GameIdentityResolver::cleanedName(QString value) const
{
    value = value.trimmed();
    value.replace(QLatin1Char('_'), QLatin1Char(' '));
    value.replace(QLatin1Char('-'), QLatin1Char(' '));
    value.replace(QRegularExpression(QStringLiteral(R"((?<=[a-z])(?=[A-Z]))")), QStringLiteral(" "));
    value.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));

    const QString lowerValue = value.toLower();
    const QStringList ignoredNames = {
        QStringLiteral("game"),
        QStringLiteral("launcher"),
        QStringLiteral("start"),
        QStringLiteral("win64"),
        QStringLiteral("win32"),
        QStringLiteral("binaries"),
        QStringLiteral("shipping")
    };

    if (ignoredNames.contains(lowerValue)) {
        return {};
    }

    return value.trimmed();
}
