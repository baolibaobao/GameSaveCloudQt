#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "models/GameInfo.h"

class SnapshotRestoreManager
{
public:
    QVariantMap restoreSnapshot(
        const GameInfo &game,
        const QString &zipPath,
        const QString &backupRootPath) const;
    QVariantList backupsForGame(const QString &backupRootPath, const GameInfo &game) const;
    QVariantMap deleteBackupsForGame(const QString &backupRootPath, const GameInfo &game) const;
    QString backupDirectoryForGame(const QString &backupRootPath, const GameInfo &game) const;

private:
    QString safeSnapshotBaseName(const GameInfo &game) const;
    QString timestampForFileName() const;
    QString escapedPowerShellSingleQuoted(QString value) const;
    bool createZipFromDirectory(
        const QString &sourceDirectoryPath,
        const QString &zipPath,
        QString *error) const;
    bool extractZipToDirectory(
        const QString &zipPath,
        const QString &targetDirectoryPath,
        QString *error) const;
    bool directoryHasEntries(const QString &directoryPath) const;
    bool removeDirectoryChildren(const QString &directoryPath, QString *error) const;
    bool copyDirectoryContents(
        const QString &sourceDirectoryPath,
        const QString &targetDirectoryPath,
        QString *error) const;
};
