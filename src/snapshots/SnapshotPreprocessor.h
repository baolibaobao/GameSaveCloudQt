#pragma once

#include <QVariantList>
#include <QVariantMap>

#include "models/GameInfo.h"
#include "SaveFileScanner.h"
#include "SnapshotManifestStore.h"
#include "SnapshotPackageWriter.h"
#include "SnapshotSettings.h"

class SnapshotPreprocessor
{
public:
    QVariantMap analyzeGame(const GameInfo &game) const;
    QVariantMap createSnapshotForGame(const GameInfo &game) const;
    QVariantMap deleteSnapshotsForGame(const GameInfo &game) const;
    QVariantList snapshotsForGame(const GameInfo &game) const;
    QVariantList allSnapshotRecordsForGame(const GameInfo &game) const;
    QString snapshotDirectoryForGame(const GameInfo &game) const;
    bool markSnapshotUploadState(
        const GameInfo &game,
        const QString &zipPath,
        const QString &fileName,
        const QString &uploadState,
        const QString &remotePath) const;
    bool importCloudSnapshotRecord(
        const GameInfo &game,
        const QVariantMap &cloudRecord,
        const QString &zipPath) const;
    QString snapshotRootPath() const;
    bool setSnapshotRootPath(const QString &path) const;

private:
    SnapshotSettings m_settings;
    SaveFileScanner m_scanner;
    SnapshotManifestStore m_manifestStore;
    SnapshotPackageWriter m_packageWriter;
};
