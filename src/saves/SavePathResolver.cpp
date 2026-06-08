#include "SavePathResolver.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

SavePathResolver::SavePathResolver(QObject *parent)
    : QObject(parent)
{
    connect(&m_pcGamingWikiProvider, &PCGamingWikiSaveProvider::savePathReady, this,
            [this](const QString &appId, const QString &rawPath, const QString &pageName) {
                const GameInfo game = m_pendingGames.value(appId);
                const QString normalizedPath = normalizePcGamingWikiPath(rawPath, game);

                if (normalizedPath.isEmpty()) {
                    emit savePathNeedsManual(appId, QStringLiteral("PCGamingWiki 路径为空"));
                    return;
                }

                const QString source = pageName.isEmpty()
                                           ? QStringLiteral("PCGamingWiki")
                                           : QStringLiteral("PCGamingWiki: %1").arg(pageName);
                const SavePathValidator::Result validation = m_savePathValidator.validate(normalizedPath, appId, m_steamPath);
                emit savePathResolved(appId, normalizedPath, source, validation.availability, validation.detail, validation.canSync);
            });

    connect(&m_pcGamingWikiProvider, &PCGamingWikiSaveProvider::savePathFailed, this,
            [this](const QString &appId, const QString &reason) {
                emit savePathNeedsManual(appId, reason);
            });
}

void SavePathResolver::setSteamPath(const QString &steamPath)
{
    m_steamPath = QDir::cleanPath(steamPath);
}

void SavePathResolver::resolveGameSavePath(const GameInfo &game)
{
    if (game.appId.trimmed().isEmpty()) {
        return;
    }

    rememberGame(game);

    const QString manualPath = m_settingsManager.manualSavePathForApp(game.appId);
    if (!manualPath.isEmpty()) {
        const SavePathValidator::Result validation = m_savePathValidator.validate(manualPath, game.appId, m_steamPath);
        emit savePathResolved(game.appId, manualPath, QStringLiteral("用户指定"), validation.availability, validation.detail, validation.canSync);
        return;
    }

    if (!isNumericAppId(game.appId)) {
        emit savePathNeedsManual(
            game.appId,
            QStringLiteral("手动添加游戏尚未匹配到 Steam AppID，无法自动查询 PCGamingWiki 存档路径"));
        return;
    }

    m_pcGamingWikiProvider.requestSavePath(game.appId);
}

bool SavePathResolver::setManualSavePath(const QString &appId, const QString &path)
{
    const QString cleanAppId = appId.trimmed();
    const QString cleanPath = QDir::cleanPath(path.trimmed());

    if (cleanAppId.isEmpty() || cleanPath.isEmpty()) {
        return false;
    }

    if (!m_settingsManager.setManualSavePathForApp(cleanAppId, cleanPath)) {
        return false;
    }

    const QString nativePath = QDir::toNativeSeparators(cleanPath);
    const SavePathValidator::Result validation = m_savePathValidator.validate(nativePath, cleanAppId, m_steamPath);
    emit savePathResolved(cleanAppId, nativePath, QStringLiteral("用户指定"), validation.availability, validation.detail, validation.canSync);
    return true;
}

QString SavePathResolver::normalizePcGamingWikiPath(QString rawPath, const GameInfo &game) const
{
    rawPath = rawPath.trimmed();
    if (rawPath.isEmpty()) {
        return {};
    }

    /*
     * PCGamingWiki 的路径常用模板变量，例如：
     * {{p|appdata}}、{{p|localappdata}}、{{p|steam}}、{{p|game}}。
     * 这里先把常见 Windows 模板转换成真实目录；无法确定的用户 ID
     * 保留成尖括号占位符，UI 仍能提示用户路径大致位置。
     */
    QRegularExpression pathTemplateRegex(QStringLiteral(R"(\{\{p\|([^}|]+)(?:\|[^}]*)?\}\})"),
                                         QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator matches = pathTemplateRegex.globalMatch(rawPath);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString replacement = resolvePathTemplateValue(match.captured(1), game);
        rawPath.replace(match.captured(0), replacement);
    }

    rawPath.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    rawPath.replace(QStringLiteral("{{!}}"), QStringLiteral("|"));
    rawPath.remove(QRegularExpression(QStringLiteral(R"(\{\{[^{}]+\}\})")));
    while (rawPath.endsWith(QStringLiteral("}}"))) {
        rawPath.chop(2);
        rawPath = rawPath.trimmed();
    }
    rawPath.replace(QStringLiteral("/"), QStringLiteral("\\"));
    rawPath.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));

    return QDir::toNativeSeparators(QDir::cleanPath(rawPath.trimmed()));
}

QString SavePathResolver::resolvePathTemplateValue(const QString &value, const GameInfo &game) const
{
    const QString key = value.trimmed().toLower();
    if (key.isEmpty()) {
        return {};
    }

    const QHash<QString, QString> replacements = {
        {QStringLiteral("localappdata"), qEnvironmentVariable("LOCALAPPDATA")},
        {QStringLiteral("appdata"), qEnvironmentVariable("APPDATA")},
        {QStringLiteral("userprofile"), userProfilePath()},
        {QStringLiteral("userprofiledocuments"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)},
        {QStringLiteral("documents"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)},
        {QStringLiteral("savedgames"), savedGamesPath()},
        {QStringLiteral("steam"), m_steamPath},
        {QStringLiteral("game"), installPathForGame(game)},
        {QStringLiteral("path-to-game"), installPathForGame(game)},
        {QStringLiteral("uid"), QStringLiteral("<SteamID>")},
        {QStringLiteral("userid"), QStringLiteral("<SteamID>")},
        {QStringLiteral("osusername"), qEnvironmentVariable("USERNAME")},
        {QStringLiteral("userprofile\\appdata\\roaming"), qEnvironmentVariable("APPDATA")},
        {QStringLiteral("userprofile\\appdata\\local"), qEnvironmentVariable("LOCALAPPDATA")},
        {QStringLiteral("userprofile\\appdata\\locallow"), QDir(userProfilePath()).absoluteFilePath(QStringLiteral("AppData/LocalLow"))}
    };

    if (replacements.contains(key)) {
        return replacements.value(key);
    }

    if (key.startsWith(QStringLiteral("userprofile\\"))) {
        const QString suffix = value.trimmed().mid(QStringLiteral("userprofile\\").length());
        return QDir(userProfilePath()).absoluteFilePath(suffix);
    }

    return QStringLiteral("<%1>").arg(value.trimmed());
}

QString SavePathResolver::installPathForGame(const GameInfo &game) const
{
    if (!game.executablePath.isEmpty()) {
        return QFileInfo(game.executablePath).absoluteDir().absolutePath();
    }

    if (game.libraryPath.isEmpty() || game.installDir.isEmpty()) {
        return {};
    }

    return QDir::cleanPath(QDir(game.libraryPath).absoluteFilePath(
        QStringLiteral("steamapps/common/%1").arg(game.installDir)));
}

QString SavePathResolver::userProfilePath() const
{
    const QString profile = qEnvironmentVariable("USERPROFILE");
    return profile.isEmpty() ? QDir::homePath() : profile;
}

QString SavePathResolver::savedGamesPath() const
{
    return QDir(userProfilePath()).absoluteFilePath(QStringLiteral("Saved Games"));
}

void SavePathResolver::rememberGame(const GameInfo &game)
{
    m_pendingGames.insert(game.appId, game);
}

bool SavePathResolver::isNumericAppId(const QString &appId) const
{
    static const QRegularExpression numericRegex(QStringLiteral(R"(^\d+$)"));
    return numericRegex.match(appId.trimmed()).hasMatch();
}
