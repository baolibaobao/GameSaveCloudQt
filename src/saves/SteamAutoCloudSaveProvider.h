#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include "models/GameInfo.h"

class SteamAutoCloudSaveProvider
{
public:
    using BinaryObject = QVariantMap;

    struct Result
    {
        bool found = false;
        QString path;
        QString source;
        QString detail;
        QString pattern;
        bool recursive = false;
    };

    Result findSavePath(const GameInfo &game, const QString &steamPath) const;

private:
    struct SaveFileRule
    {
        QString root;
        QString path;
        QString pattern;
        QStringList platforms;
        bool recursive = false;
    };

    struct Candidate
    {
        SaveFileRule rule;
        QString path;
        bool exists = false;
        bool hasMatchingFiles = false;
    };

    void ensureLoaded(const QString &steamPath) const;
    void loadAppInfo(const QString &steamPath) const;
    void loadAppInfoV29(const QByteArray &data) const;
    QList<SaveFileRule> loadV29SaveRulesForGame(const GameInfo &game, const QString &steamPath) const;
    Result resultFromRules(const QList<SaveFileRule> &rules, const GameInfo &game, const QString &steamPath) const;
    bool readV29KeyTable(const QByteArray &data, qsizetype *tableStart, QHash<quint32, QString> *keyNames) const;
    bool findObjectByKey(const BinaryObject &object, const QString &key, BinaryObject *found) const;
    QList<SaveFileRule> saveRulesFromUfsObject(const BinaryObject &ufsObject) const;
    SaveFileRule saveRuleFromObject(const BinaryObject &object) const;
    bool isWindowsRule(const SaveFileRule &rule) const;
    QString rootPathForRule(const QString &root, const GameInfo &game, const QString &steamPath) const;
    QString normalizeRulePath(const QString &path) const;
    QString installPathForGame(const GameInfo &game) const;
    QString userProfilePath() const;
    QString savedGamesPath() const;
    bool directoryHasMatchingFiles(const QString &path, const QString &pattern, bool recursive) const;
    QString candidateDetail(const Candidate &candidate) const;
    bool isNumericAppId(const QString &appId) const;

    mutable QString m_loadedSteamPath;
    mutable bool m_loaded = false;
    mutable QHash<QString, QList<SaveFileRule>> m_rulesByAppId;
};
