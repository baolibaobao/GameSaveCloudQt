#include "SteamAutoCloudSaveProvider.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtEndian>

#include <algorithm>
#include <cstring>

namespace {

class BinaryVdfReader
{
public:
    explicit BinaryVdfReader(const QByteArray &data, qsizetype start = 0, qsizetype end = -1)
        : m_data(data)
        , m_pos(start)
        , m_end(end >= 0 ? std::min(end, data.size()) : data.size())
    {
    }

    bool canRead(qsizetype size) const
    {
        return size >= 0 && m_pos + size <= m_end;
    }

    bool atEnd() const
    {
        return m_pos >= m_end;
    }

    qsizetype position() const
    {
        return m_pos;
    }

    void setPosition(qsizetype position)
    {
        m_pos = std::clamp(position, qsizetype(0), m_end);
    }

    quint8 readU8(bool *ok = nullptr)
    {
        if (!canRead(1)) {
            setOk(ok, false);
            return 0;
        }

        setOk(ok, true);
        return static_cast<quint8>(m_data.at(m_pos++));
    }

    quint32 readU32(bool *ok = nullptr)
    {
        if (!canRead(4)) {
            setOk(ok, false);
            return 0;
        }

        quint32 value = 0;
        std::memcpy(&value, m_data.constData() + m_pos, sizeof(value));
        m_pos += 4;
        setOk(ok, true);
        return qFromLittleEndian(value);
    }

    quint64 readU64(bool *ok = nullptr)
    {
        if (!canRead(8)) {
            setOk(ok, false);
            return 0;
        }

        quint64 value = 0;
        std::memcpy(&value, m_data.constData() + m_pos, sizeof(value));
        m_pos += 8;
        setOk(ok, true);
        return qFromLittleEndian(value);
    }

    QString readCString(bool *ok = nullptr)
    {
        const qsizetype zeroIndex = m_data.indexOf('\0', m_pos);
        if (zeroIndex < 0 || zeroIndex >= m_end) {
            setOk(ok, false);
            return {};
        }

        const QByteArray bytes = m_data.mid(m_pos, zeroIndex - m_pos);
        m_pos = zeroIndex + 1;
        setOk(ok, true);
        return QString::fromUtf8(bytes);
    }

    bool skip(qsizetype size)
    {
        if (!canRead(size)) {
            return false;
        }

        m_pos += size;
        return true;
    }

    SteamAutoCloudSaveProvider::BinaryObject readObject(bool *ok = nullptr)
    {
        SteamAutoCloudSaveProvider::BinaryObject object;
        while (!atEnd()) {
            bool typeOk = false;
            const quint8 type = readU8(&typeOk);
            if (!typeOk) {
                setOk(ok, false);
                return {};
            }

            if (type == 8) {
                setOk(ok, true);
                return object;
            }

            bool nameOk = false;
            const QString name = readCString(&nameOk);
            if (!nameOk) {
                setOk(ok, false);
                return {};
            }

            bool valueOk = false;
            const QVariant value = readValue(type, &valueOk);
            if (!valueOk) {
                setOk(ok, false);
                return {};
            }

            object.insert(name, value);
        }

        setOk(ok, false);
        return {};
    }

private:
    QVariant readValue(quint8 type, bool *ok)
    {
        switch (type) {
        case 0:
            return QVariant::fromValue(readObject(ok));
        case 1:
            return readCString(ok);
        case 2: {
            const quint32 value = readU32(ok);
            return static_cast<qint32>(value);
        }
        case 3: {
            bool readOk = false;
            const quint32 raw = readU32(&readOk);
            float value = 0.0f;
            std::memcpy(&value, &raw, sizeof(value));
            setOk(ok, readOk);
            return value;
        }
        case 4:
        case 6:
            return readU32(ok);
        case 7:
            return QString::number(readU64(ok));
        case 10:
            return QString::number(static_cast<qint64>(readU64(ok)));
        default:
            setOk(ok, false);
            return {};
        }
    }

    static void setOk(bool *ok, bool value)
    {
        if (ok) {
            *ok = value;
        }
    }

    const QByteArray &m_data;
    qsizetype m_pos = 0;
    qsizetype m_end = 0;
};

class BinaryVdfKeyedReader
{
public:
    BinaryVdfKeyedReader(const QByteArray &data, const QHash<quint32, QString> &keyNames, qsizetype start, qsizetype end)
        : m_data(data)
        , m_keyNames(keyNames)
        , m_pos(start)
        , m_end(std::min(end, data.size()))
    {
    }

    bool canRead(qsizetype size) const
    {
        return size >= 0 && m_pos + size <= m_end;
    }

    qsizetype position() const
    {
        return m_pos;
    }

    SteamAutoCloudSaveProvider::BinaryObject readObject(bool *ok = nullptr)
    {
        SteamAutoCloudSaveProvider::BinaryObject object;
        while (canRead(1)) {
            bool typeOk = false;
            const quint8 type = readU8(&typeOk);
            if (!typeOk) {
                setOk(ok, false);
                return {};
            }

            if (type == 8) {
                setOk(ok, true);
                return object;
            }

            bool keyOk = false;
            const quint32 keyId = readU32(&keyOk);
            if (!keyOk) {
                setOk(ok, false);
                return {};
            }

            const QString keyName = m_keyNames.value(keyId, QString::number(keyId));
            bool valueOk = false;
            const QVariant value = readValue(type, &valueOk);
            if (!valueOk) {
                setOk(ok, false);
                return {};
            }

            object.insert(keyName, value);
        }

        setOk(ok, false);
        return {};
    }

private:
    quint8 readU8(bool *ok = nullptr)
    {
        if (!canRead(1)) {
            setOk(ok, false);
            return 0;
        }

        setOk(ok, true);
        return static_cast<quint8>(m_data.at(m_pos++));
    }

    quint32 readU32(bool *ok = nullptr)
    {
        if (!canRead(4)) {
            setOk(ok, false);
            return 0;
        }

        const quint32 value = qFromLittleEndian<quint32>(m_data.constData() + m_pos);
        m_pos += 4;
        setOk(ok, true);
        return value;
    }

    quint64 readU64(bool *ok = nullptr)
    {
        if (!canRead(8)) {
            setOk(ok, false);
            return 0;
        }

        const quint64 value = qFromLittleEndian<quint64>(m_data.constData() + m_pos);
        m_pos += 8;
        setOk(ok, true);
        return value;
    }

    QString readCString(bool *ok = nullptr)
    {
        const qsizetype zeroIndex = m_data.indexOf('\0', m_pos);
        if (zeroIndex < 0 || zeroIndex >= m_end) {
            setOk(ok, false);
            return {};
        }

        const QByteArray bytes = m_data.mid(m_pos, zeroIndex - m_pos);
        m_pos = zeroIndex + 1;
        setOk(ok, true);
        return QString::fromUtf8(bytes);
    }

    QVariant readValue(quint8 type, bool *ok)
    {
        switch (type) {
        case 0:
            return QVariant::fromValue(readObject(ok));
        case 1:
            return readCString(ok);
        case 2: {
            const quint32 value = readU32(ok);
            return static_cast<qint32>(value);
        }
        case 3: {
            bool readOk = false;
            const quint32 raw = readU32(&readOk);
            float value = 0.0f;
            std::memcpy(&value, &raw, sizeof(value));
            setOk(ok, readOk);
            return value;
        }
        case 4:
        case 6:
            return readU32(ok);
        case 7:
            return QString::number(readU64(ok));
        case 10:
            return QString::number(static_cast<qint64>(readU64(ok)));
        default:
            setOk(ok, false);
            return {};
        }
    }

    static void setOk(bool *ok, bool value)
    {
        if (ok) {
            *ok = value;
        }
    }

    const QByteArray &m_data;
    const QHash<quint32, QString> &m_keyNames;
    qsizetype m_pos = 0;
    qsizetype m_end = 0;
};

QString variantToString(const QVariant &value)
{
    return value.toString().trimmed();
}

SteamAutoCloudSaveProvider::BinaryObject variantToObject(const QVariant &value)
{
    return value.toMap();
}

bool variantToBool(const QVariant &value)
{
    if (value.typeId() == QMetaType::Bool) {
        return value.toBool();
    }

    const QString text = value.toString().trimmed().toLower();
    return value.toInt() != 0
        || text == QStringLiteral("true")
        || text == QStringLiteral("yes");
}

} // namespace

SteamAutoCloudSaveProvider::Result SteamAutoCloudSaveProvider::findSavePath(
    const GameInfo &game,
    const QString &steamPath) const
{
    const QString cleanAppId = game.appId.trimmed();
    if (!isNumericAppId(cleanAppId)) {
        return {};
    }

    ensureLoaded(steamPath);

    QList<SaveFileRule> rules = m_rulesByAppId.value(cleanAppId);
    if (rules.isEmpty()) {
        rules = loadV29SaveRulesForGame(game, steamPath);
        if (!rules.isEmpty()) {
            m_rulesByAppId.insert(cleanAppId, rules);
        }
    }

    return resultFromRules(rules, game, steamPath);
}

SteamAutoCloudSaveProvider::Result SteamAutoCloudSaveProvider::resultFromRules(
    const QList<SaveFileRule> &rules,
    const GameInfo &game,
    const QString &steamPath) const
{
    if (rules.isEmpty()) {
        return {};
    }

    QList<Candidate> candidates;
    for (const SaveFileRule &rule : rules) {
        if (!isWindowsRule(rule)) {
            continue;
        }

        const QString rootPath = rootPathForRule(rule.root, game, steamPath);
        if (rootPath.trimmed().isEmpty()) {
            continue;
        }

        const QString relativePath = normalizeRulePath(rule.path);
        const QString candidatePath = QDir::toNativeSeparators(
            QDir::cleanPath(relativePath.isEmpty()
                                ? rootPath
                                : QDir(rootPath).absoluteFilePath(relativePath)));
        if (candidatePath.trimmed().isEmpty()) {
            continue;
        }

        Candidate candidate;
        candidate.rule = rule;
        candidate.path = candidatePath;
        candidate.exists = QDir(candidatePath).exists();
        candidate.hasMatchingFiles = directoryHasMatchingFiles(candidatePath, rule.pattern, rule.recursive);
        candidates.append(candidate);
    }

    if (candidates.isEmpty()) {
        return {};
    }

    auto best = std::find_if(candidates.cbegin(), candidates.cend(), [](const Candidate &candidate) {
        return candidate.hasMatchingFiles;
    });
    if (best == candidates.cend()) {
        best = std::find_if(candidates.cbegin(), candidates.cend(), [](const Candidate &candidate) {
            return candidate.exists;
        });
    }
    if (best == candidates.cend()) {
        best = candidates.cbegin();
    }

    Result result;
    result.found = true;
    result.path = best->path;
    result.source = QStringLiteral("Steam Auto-Cloud");
    result.detail = candidateDetail(*best);
    result.pattern = best->rule.pattern;
    result.recursive = best->rule.recursive;
    return result;
}

void SteamAutoCloudSaveProvider::ensureLoaded(const QString &steamPath) const
{
    const QString cleanSteamPath = QDir::cleanPath(steamPath.trimmed());
    if (m_loaded && m_loadedSteamPath == cleanSteamPath) {
        return;
    }

    m_loadedSteamPath = cleanSteamPath;
    m_loaded = true;
    m_rulesByAppId.clear();
    loadAppInfo(cleanSteamPath);
}

void SteamAutoCloudSaveProvider::loadAppInfo(const QString &steamPath) const
{
    if (steamPath.trimmed().isEmpty()) {
        return;
    }

    const QString appInfoPath = QDir(steamPath).absoluteFilePath(QStringLiteral("appcache/appinfo.vdf"));
    QFile file(appInfoPath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;
    }

    const QByteArray data = file.readAll();
    if (data.size() < 16) {
        return;
    }

    BinaryVdfReader reader(data);
    bool ok = false;
    const quint32 magic = reader.readU32(&ok);
    if (!ok) {
        return;
    }

    if (magic == 0x07564429) {
        loadAppInfoV29(data);
        return;
    }

    if (magic != 0x07564427 && magic != 0x07564428) {
        return;
    }

    reader.skip(4);

    while (reader.canRead(8)) {
        const qsizetype entryStart = reader.position();
        const quint32 appId = reader.readU32(&ok);
        if (!ok || appId == 0) {
            return;
        }

        const quint32 size = reader.readU32(&ok);
        if (!ok || size < 40) {
            return;
        }

        const qsizetype entryPayloadStart = reader.position();
        const qsizetype entryEnd = entryPayloadStart + size;
        if (entryEnd <= entryPayloadStart || entryEnd > data.size()) {
            return;
        }

        if (!reader.skip(40)) {
            return;
        }

        QList<SaveFileRule> rules;
        while (reader.position() < entryEnd && reader.canRead(1)) {
            const quint8 sectionType = reader.readU8(&ok);
            if (!ok || sectionType == 0) {
                break;
            }

            const QString sectionName = reader.readCString(&ok);
            if (!ok || sectionName.isEmpty()) {
                break;
            }

            const qsizetype beforeObject = reader.position();
            const BinaryObject sectionObject = reader.readObject(&ok);
            if (!ok) {
                reader.setPosition(entryEnd);
                break;
            }

            if (sectionName.compare(QStringLiteral("ufs"), Qt::CaseInsensitive) == 0) {
                rules = saveRulesFromUfsObject(sectionObject);
            }

            if (reader.position() <= beforeObject) {
                break;
            }
        }

        if (!rules.isEmpty()) {
            m_rulesByAppId.insert(QString::number(appId), rules);
        }

        reader.setPosition(entryEnd);
        if (reader.position() <= entryStart) {
            return;
        }
    }
}

void SteamAutoCloudSaveProvider::loadAppInfoV29(const QByteArray &data) const
{
    Q_UNUSED(data)
    // AppInfo v29 uses numeric key ids and variable entry headers. It is handled
    // conservatively per game in loadV29SaveRulesForGame().
}

QList<SteamAutoCloudSaveProvider::SaveFileRule> SteamAutoCloudSaveProvider::loadV29SaveRulesForGame(
    const GameInfo &game,
    const QString &steamPath) const
{
    const QString appInfoPath = QDir(steamPath).absoluteFilePath(QStringLiteral("appcache/appinfo.vdf"));
    QFile file(appInfoPath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QByteArray data = file.readAll();
    if (data.size() < 16 || qFromLittleEndian<quint32>(data.constData()) != 0x07564429) {
        return {};
    }

    qsizetype tableStart = -1;
    QHash<quint32, QString> keyNames;
    if (!readV29KeyTable(data, &tableStart, &keyNames)) {
        return {};
    }

    quint32 ufsKeyId = 0;
    bool hasUfsKey = false;
    for (auto it = keyNames.cbegin(); it != keyNames.cend(); ++it) {
        if (it.value() == QStringLiteral("ufs")) {
            ufsKeyId = it.key();
            hasUfsKey = true;
            break;
        }
    }
    if (!hasUfsKey) {
        return {};
    }

    QByteArray ufsMarker;
    ufsMarker.append(char(0));
    for (int i = 0; i < 4; ++i) {
        ufsMarker.append(char((ufsKeyId >> (8 * i)) & 0xff));
    }

    QStringList searchTexts;
    const QStringList rawTexts = {
        game.name,
        game.displayName,
        game.localizedName,
        game.installDir,
        QFileInfo(game.executablePath).fileName()
    };
    for (const QString &text : rawTexts) {
        const QString cleanText = text.trimmed();
        if (!cleanText.isEmpty() && !searchTexts.contains(cleanText, Qt::CaseInsensitive)) {
            searchTexts.append(cleanText);
        }
    }

    for (const QString &searchText : searchTexts) {
        const QByteArray needle = searchText.toUtf8();
        qsizetype matchPosition = data.indexOf(needle, 8);
        while (matchPosition >= 0 && matchPosition < tableStart) {
            const qsizetype windowEnd = std::min(tableStart, matchPosition + qsizetype(32768));
            const qsizetype ufsPosition = data.indexOf(ufsMarker, matchPosition);
            if (ufsPosition >= 0 && ufsPosition < windowEnd) {
                BinaryVdfKeyedReader reader(data, keyNames, ufsPosition, windowEnd);
                bool ok = false;
                const BinaryObject rootObject = reader.readObject(&ok);
                BinaryObject ufsObject;
                if (ok && findObjectByKey(rootObject, QStringLiteral("ufs"), &ufsObject)) {
                    const QList<SaveFileRule> rules = saveRulesFromUfsObject(ufsObject);
                    if (!rules.isEmpty()) {
                        return rules;
                    }
                }
            }

            matchPosition = data.indexOf(needle, matchPosition + needle.size());
        }
    }

    return {};
}

bool SteamAutoCloudSaveProvider::readV29KeyTable(
    const QByteArray &data,
    qsizetype *tableStart,
    QHash<quint32, QString> *keyNames) const
{
    static const QByteArray marker("appinfo\0appid\0public_only\0common\0", 33);
    const qsizetype start = data.indexOf(marker);
    if (start < 4) {
        return false;
    }

    bool ok = false;
    BinaryVdfReader countReader(data, start - 4, start);
    const quint32 count = countReader.readU32(&ok);
    if (!ok || count == 0) {
        return false;
    }

    QHash<quint32, QString> names;
    names.reserve(static_cast<int>(count));
    qsizetype position = start;
    for (quint32 id = 0; id < count && position < data.size(); ++id) {
        const qsizetype zeroIndex = data.indexOf('\0', position);
        if (zeroIndex < 0) {
            return false;
        }

        names.insert(id, QString::fromUtf8(data.mid(position, zeroIndex - position)));
        position = zeroIndex + 1;
    }

    if (!names.values().contains(QStringLiteral("ufs"))
        || !names.values().contains(QStringLiteral("savefiles"))) {
        return false;
    }

    *tableStart = start - 8;
    *keyNames = names;
    return true;
}

bool SteamAutoCloudSaveProvider::findObjectByKey(
    const BinaryObject &object,
    const QString &key,
    BinaryObject *found) const
{
    const QVariant directValue = object.value(key);
    if (directValue.isValid()) {
        const BinaryObject directObject = variantToObject(directValue);
        if (!directObject.isEmpty()) {
            *found = directObject;
            return true;
        }
    }

    for (auto it = object.cbegin(); it != object.cend(); ++it) {
        const BinaryObject child = variantToObject(it.value());
        if (!child.isEmpty() && findObjectByKey(child, key, found)) {
            return true;
        }
    }

    return false;
}

QList<SteamAutoCloudSaveProvider::SaveFileRule> SteamAutoCloudSaveProvider::saveRulesFromUfsObject(
    const BinaryObject &ufsObject) const
{
    QList<SaveFileRule> rules;
    const BinaryObject saveFiles = variantToObject(ufsObject.value(QStringLiteral("savefiles")));
    for (auto it = saveFiles.cbegin(); it != saveFiles.cend(); ++it) {
        const BinaryObject ruleObject = variantToObject(it.value());
        SaveFileRule rule = saveRuleFromObject(ruleObject);
        if (!rule.root.trimmed().isEmpty()
            && (!rule.path.trimmed().isEmpty() || !rule.pattern.trimmed().isEmpty())) {
            rules.append(rule);
        }
    }

    return rules;
}

SteamAutoCloudSaveProvider::SaveFileRule SteamAutoCloudSaveProvider::saveRuleFromObject(
    const BinaryObject &object) const
{
    SaveFileRule rule;
    rule.root = variantToString(object.value(QStringLiteral("root")));
    rule.path = variantToString(object.value(QStringLiteral("path")));
    rule.pattern = variantToString(object.value(QStringLiteral("pattern")));
    rule.recursive = variantToBool(object.value(QStringLiteral("recursive")));

    const BinaryObject platforms = variantToObject(object.value(QStringLiteral("platforms")));
    for (auto it = platforms.cbegin(); it != platforms.cend(); ++it) {
        const QString key = it.key().trimmed();
        const QString value = variantToString(it.value());
        if (!key.isEmpty()) {
            rule.platforms.append(key);
        }
        if (!value.isEmpty()) {
            rule.platforms.append(value);
        }
    }

    return rule;
}

bool SteamAutoCloudSaveProvider::isWindowsRule(const SaveFileRule &rule) const
{
    if (rule.platforms.isEmpty()) {
        return true;
    }

    for (const QString &platform : rule.platforms) {
        if (platform.contains(QStringLiteral("win"), Qt::CaseInsensitive)) {
            return true;
        }
    }

    return false;
}

QString SteamAutoCloudSaveProvider::rootPathForRule(
    const QString &root,
    const GameInfo &game,
    const QString &steamPath) const
{
    QString normalizedRoot = root.trimmed().toLower();
    normalizedRoot.remove(QRegularExpression(QStringLiteral(R"([\s_\-])")));

    if (normalizedRoot == QStringLiteral("winappdatalocal")
        || normalizedRoot == QStringLiteral("appdatalocal")) {
        return qEnvironmentVariable("LOCALAPPDATA");
    }

    if (normalizedRoot == QStringLiteral("winappdatalocallow")
        || normalizedRoot == QStringLiteral("appdatalocallow")) {
        return QDir(userProfilePath()).absoluteFilePath(QStringLiteral("AppData/LocalLow"));
    }

    if (normalizedRoot == QStringLiteral("winappdataroaming")
        || normalizedRoot == QStringLiteral("appdataroaming")) {
        return qEnvironmentVariable("APPDATA");
    }

    if (normalizedRoot == QStringLiteral("winmydocuments")
        || normalizedRoot == QStringLiteral("documents")
        || normalizedRoot == QStringLiteral("mydocuments")) {
        return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }

    if (normalizedRoot == QStringLiteral("winsavedgames")
        || normalizedRoot == QStringLiteral("savedgames")) {
        return savedGamesPath();
    }

    if (normalizedRoot == QStringLiteral("winuserdir")
        || normalizedRoot == QStringLiteral("userprofile")
        || normalizedRoot == QStringLiteral("userdir")) {
        return userProfilePath();
    }

    if (normalizedRoot == QStringLiteral("gameinstall")
        || normalizedRoot == QStringLiteral("appinstalldir")
        || normalizedRoot == QStringLiteral("installfolder")
        || normalizedRoot == QStringLiteral("gameinstalldir")) {
        return installPathForGame(game);
    }

    if (normalizedRoot == QStringLiteral("steampath")
        || normalizedRoot == QStringLiteral("steam")) {
        return QDir::cleanPath(steamPath.trimmed());
    }

    if (normalizedRoot == QStringLiteral("winprogramdata")
        || normalizedRoot == QStringLiteral("programdata")) {
        return qEnvironmentVariable("PROGRAMDATA");
    }

    return {};
}

QString SteamAutoCloudSaveProvider::normalizeRulePath(const QString &path) const
{
    QString cleanPath = path.trimmed();
    cleanPath.replace(QStringLiteral("/"), QStringLiteral("\\"));
    while (cleanPath.startsWith(QLatin1Char('\\'))) {
        cleanPath.remove(0, 1);
    }
    return cleanPath;
}

QString SteamAutoCloudSaveProvider::installPathForGame(const GameInfo &game) const
{
    if (!game.executablePath.trimmed().isEmpty()) {
        return QFileInfo(game.executablePath).absoluteDir().absolutePath();
    }

    if (game.libraryPath.trimmed().isEmpty() || game.installDir.trimmed().isEmpty()) {
        return {};
    }

    return QDir::cleanPath(QDir(game.libraryPath).absoluteFilePath(
        QStringLiteral("steamapps/common/%1").arg(game.installDir)));
}

QString SteamAutoCloudSaveProvider::userProfilePath() const
{
    const QString profile = qEnvironmentVariable("USERPROFILE");
    return profile.isEmpty() ? QDir::homePath() : profile;
}

QString SteamAutoCloudSaveProvider::savedGamesPath() const
{
    return QDir(userProfilePath()).absoluteFilePath(QStringLiteral("Saved Games"));
}

bool SteamAutoCloudSaveProvider::directoryHasMatchingFiles(
    const QString &path,
    const QString &pattern,
    bool recursive) const
{
    const QDir dir(path);
    if (!dir.exists()) {
        return false;
    }

    QStringList nameFilters;
    const QString cleanPattern = pattern.trimmed();
    if (!cleanPattern.isEmpty()) {
        nameFilters = cleanPattern.split(QRegularExpression(QStringLiteral(R"([;,])")), Qt::SkipEmptyParts);
        for (QString &filter : nameFilters) {
            filter = filter.trimmed();
        }
    }

    if (nameFilters.isEmpty()) {
        nameFilters.append(QStringLiteral("*"));
    }

    const QDirIterator::IteratorFlags flags = recursive
                                                  ? QDirIterator::Subdirectories
                                                  : QDirIterator::NoIteratorFlags;
    QDirIterator iterator(path, nameFilters, QDir::Files | QDir::NoDotAndDotDot, flags);
    return iterator.hasNext();
}

QString SteamAutoCloudSaveProvider::candidateDetail(const Candidate &candidate) const
{
    QStringList parts = {
        QStringLiteral("Steam Auto-Cloud 配置：root=%1").arg(candidate.rule.root),
        QStringLiteral("path=%1").arg(candidate.rule.path)
    };

    if (!candidate.rule.pattern.trimmed().isEmpty()) {
        parts.append(QStringLiteral("pattern=%1").arg(candidate.rule.pattern));
    }

    parts.append(QStringLiteral("recursive=%1").arg(candidate.rule.recursive ? 1 : 0));
    return parts.join(QStringLiteral("，"));
}

bool SteamAutoCloudSaveProvider::isNumericAppId(const QString &appId) const
{
    static const QRegularExpression numericRegex(QStringLiteral(R"(^\d+$)"));
    return numericRegex.match(appId.trimmed()).hasMatch();
}
