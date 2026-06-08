#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "models/GameInfo.h"
#include "SaveFileScanner.h"
#include "SnapshotPackageWriter.h"

class SnapshotManifestStore
{
public:
    QString gameSnapshotDirectory(const QString &snapshotRootPath, const GameInfo &game) const;
    QVariantMap loadManifest(const QString &snapshotRootPath, const GameInfo &game) const;
    bool saveScanBaseline(
        const QString &snapshotRootPath,
        const GameInfo &game,
        const SaveFileScanner::Result &scanResult) const;
    bool appendSnapshotRecord(
        const QString &snapshotRootPath,
        const GameInfo &game,
        const SaveFileScanner::Result &scanResult,
        const SnapshotPackageWriter::Result &packageResult) const;
    bool updateSnapshotUploadState(
        const QString &snapshotRootPath,
        const GameInfo &game,
        const QString &zipPath,
        const QString &fileName,
        const QString &uploadState,
        const QString &remotePath) const;
    bool importCloudSnapshotRecord(
        const QString &snapshotRootPath,
        const GameInfo &game,
        const QVariantMap &cloudRecord,
        const QString &zipPath) const;
    QVariantList snapshotRecords(const QString &snapshotRootPath, const GameInfo &game) const;
    QVariantList existingSnapshotRecords(const QString &snapshotRootPath, const GameInfo &game) const;

private:
    QVariantMap normalizeSnapshotPaths(const QString &snapshotRootPath, const GameInfo &game, QVariantMap manifest) const;
    QString normalizedZipPathForRecord(
        const QString &snapshotDirectory,
        const QString &fileName,
        const QString &storedZipPath) const;
    QString manifestPath(const QString &snapshotRootPath, const GameInfo &game) const;
    QString safeDirectoryName(const GameInfo &game) const;
};
