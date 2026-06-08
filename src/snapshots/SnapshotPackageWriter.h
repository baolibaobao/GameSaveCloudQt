#pragma once

#include <QString>

#include "models/GameInfo.h"

class SnapshotPackageWriter
{
public:
    struct Result
    {
        bool success = false;
        QString error;
        QString zipPath;
        QString fileName;
        qint64 zipSize = 0;
        QString createdAtUtc;
    };

    Result createZipSnapshot(
        const QString &savePath,
        const QString &snapshotDirectory,
        const GameInfo &game) const;

private:
    QString safeSnapshotBaseName(const GameInfo &game) const;
    QString timestampForFileName() const;
    QString escapedPowerShellSingleQuoted(QString value) const;
};
