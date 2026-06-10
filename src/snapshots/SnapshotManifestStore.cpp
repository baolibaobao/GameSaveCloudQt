#include "SnapshotManifestStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QVariant>
#include <QVariantList>

#include <algorithm>

QString SnapshotManifestStore::gameSnapshotDirectory(const QString &snapshotRootPath, const GameInfo &game) const
{
    if (snapshotRootPath.trimmed().isEmpty()) {
        return {};
    }

    return QDir::toNativeSeparators(QDir(snapshotRootPath).absoluteFilePath(safeDirectoryName(game)));
}

QVariantMap SnapshotManifestStore::loadManifest(const QString &snapshotRootPath, const GameInfo &game) const
{
    const QString path = manifestPath(snapshotRootPath, game);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();

    const QVariantMap manifest = document.object().toVariantMap();
    const QVariantMap normalizedManifest = normalizeSnapshotPaths(snapshotRootPath, game, manifest);
    if (normalizedManifest != manifest) {
        QFile writableFile(path);
        if (writableFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            writableFile.write(QJsonDocument::fromVariant(normalizedManifest).toJson(QJsonDocument::Indented));
        }
    }

    return normalizedManifest;
}

bool SnapshotManifestStore::saveScanBaseline(
    const QString &snapshotRootPath,
    const GameInfo &game,
    const SaveFileScanner::Result &scanResult) const
{
    const QString directoryPath = gameSnapshotDirectory(snapshotRootPath, game);
    if (directoryPath.isEmpty()) {
        return false;
    }

    QDir directory(directoryPath);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        return false;
    }

    QJsonArray files;
    for (const SaveFileScanner::FileEntry &file : scanResult.files) {
        QJsonObject item;
        item.insert(QStringLiteral("relativePath"), file.relativePath);
        item.insert(QStringLiteral("size"), QString::number(file.size));
        item.insert(QStringLiteral("lastModifiedUtc"), file.lastModifiedUtc);
        item.insert(QStringLiteral("md5"), file.md5);
        files.append(item);
    }

    const QVariantMap oldManifest = loadManifest(snapshotRootPath, game);

    /*
     * 这是本地 manifest，不是云端真相。
     * uploadState 只表示本地记录的上传状态；阶段 7 做 WebDAV 上传前，
     * 仍然必须请求云端确认 zip 是否存在。如果用户在网盘手动删除了文件，
     * 远程确认会发现缺失并重新上传。
     *
     * lastScanContentDigest 与 lastSnapshotContentDigest 必须分开：
     * - lastScanContentDigest：阶段 6B 最近一次扫描到的本地内容。
     * - lastSnapshotContentDigest：阶段 6C 真正生成 zip 后才会写入。
     * 否则只要扫描过一次，就会误以为已经有对应快照。
     */
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("appId"), game.appId);
    root.insert(QStringLiteral("gameName"), game.displayName.isEmpty() ? game.name : game.displayName);
    root.insert(QStringLiteral("savePath"), game.savePath);
    root.insert(QStringLiteral("lastScanAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("lastScanContentDigest"), scanResult.contentDigest);
    root.insert(QStringLiteral("lastSnapshotContentDigest"),
                oldManifest.value(QStringLiteral("lastSnapshotContentDigest")).toString());
    root.insert(QStringLiteral("latestSnapshotFileName"),
                oldManifest.value(QStringLiteral("latestSnapshotFileName")).toString());
    root.insert(QStringLiteral("latestSnapshotPath"),
                oldManifest.value(QStringLiteral("latestSnapshotPath")).toString());
    root.insert(QStringLiteral("fileCount"), scanResult.fileCount);
    root.insert(QStringLiteral("totalBytes"), QString::number(scanResult.totalBytes));
    root.insert(QStringLiteral("uploadState"),
                oldManifest.value(QStringLiteral("uploadState"), QStringLiteral("not_created_yet")).toString());
    root.insert(QStringLiteral("remoteVerifiedAt"),
                oldManifest.value(QStringLiteral("remoteVerifiedAt")).toString());
    root.insert(QStringLiteral("files"), files);
    root.insert(QStringLiteral("snapshots"),
                QJsonArray::fromVariantList(oldManifest.value(QStringLiteral("snapshots")).toList()));

    QFile file(manifestPath(snapshotRootPath, game));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.error() == QFile::NoError;
}

bool SnapshotManifestStore::appendSnapshotRecord(
    const QString &snapshotRootPath,
    const GameInfo &game,
    const SaveFileScanner::Result &scanResult,
    const SnapshotPackageWriter::Result &packageResult) const
{
    QVariantMap manifest = loadManifest(snapshotRootPath, game);
    QVariantList snapshots = manifest.value(QStringLiteral("snapshots")).toList();

    QVariantMap snapshot;
    snapshot.insert(QStringLiteral("fileName"), packageResult.fileName);
    snapshot.insert(QStringLiteral("zipPath"), packageResult.zipPath);
    snapshot.insert(QStringLiteral("createdAtUtc"), packageResult.createdAtUtc);
    snapshot.insert(QStringLiteral("zipSize"), QString::number(packageResult.zipSize));
    snapshot.insert(QStringLiteral("contentDigest"), scanResult.contentDigest);
    snapshot.insert(QStringLiteral("fileCount"), scanResult.fileCount);
    snapshot.insert(QStringLiteral("totalBytes"), QString::number(scanResult.totalBytes));
    snapshot.insert(QStringLiteral("uploadState"), QStringLiteral("not_uploaded"));
    snapshot.insert(QStringLiteral("remoteVerifiedAt"), QString());
    snapshots.append(snapshot);

    QJsonArray files;
    for (const SaveFileScanner::FileEntry &file : scanResult.files) {
        QJsonObject item;
        item.insert(QStringLiteral("relativePath"), file.relativePath);
        item.insert(QStringLiteral("size"), QString::number(file.size));
        item.insert(QStringLiteral("lastModifiedUtc"), file.lastModifiedUtc);
        item.insert(QStringLiteral("md5"), file.md5);
        files.append(item);
    }

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("appId"), game.appId);
    root.insert(QStringLiteral("gameName"), game.displayName.isEmpty() ? game.name : game.displayName);
    root.insert(QStringLiteral("savePath"), game.savePath);
    root.insert(QStringLiteral("lastScanAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("lastScanContentDigest"), scanResult.contentDigest);
    root.insert(QStringLiteral("lastSnapshotContentDigest"), scanResult.contentDigest);
    root.insert(QStringLiteral("latestSnapshotFileName"), packageResult.fileName);
    root.insert(QStringLiteral("latestSnapshotPath"), packageResult.zipPath);
    root.insert(QStringLiteral("fileCount"), scanResult.fileCount);
    root.insert(QStringLiteral("totalBytes"), QString::number(scanResult.totalBytes));
    root.insert(QStringLiteral("uploadState"), QStringLiteral("not_uploaded"));
    root.insert(QStringLiteral("remoteVerifiedAt"), QString());
    root.insert(QStringLiteral("files"), files);
    root.insert(QStringLiteral("snapshots"), QJsonArray::fromVariantList(snapshots));

    QFile file(manifestPath(snapshotRootPath, game));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.error() == QFile::NoError;
}

bool SnapshotManifestStore::updateSnapshotUploadState(
    const QString &snapshotRootPath,
    const GameInfo &game,
    const QString &zipPath,
    const QString &fileName,
    const QString &uploadState,
    const QString &remotePath) const
{
    QVariantMap manifest = loadManifest(snapshotRootPath, game);
    if (manifest.isEmpty()) {
        return false;
    }

    QVariantList snapshots = manifest.value(QStringLiteral("snapshots")).toList();
    const QString verifiedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    bool updated = false;

    for (QVariant &snapshotItem : snapshots) {
        QVariantMap snapshot = snapshotItem.toMap();
        const bool sameZipPath = !zipPath.trimmed().isEmpty()
                                 && snapshot.value(QStringLiteral("zipPath")).toString() == zipPath;
        const bool sameFileName = !fileName.trimmed().isEmpty()
                                  && snapshot.value(QStringLiteral("fileName")).toString() == fileName;

        if (!sameZipPath && !sameFileName) {
            continue;
        }

        snapshot.insert(QStringLiteral("uploadState"), uploadState);
        snapshot.insert(QStringLiteral("remotePath"), remotePath);
        snapshot.insert(QStringLiteral("remoteVerifiedAt"), verifiedAt);
        snapshotItem = snapshot;
        updated = true;
        break;
    }

    if (!updated) {
        return false;
    }

    manifest.insert(QStringLiteral("uploadState"), uploadState);
    manifest.insert(QStringLiteral("remotePath"), remotePath);
    manifest.insert(QStringLiteral("remoteVerifiedAt"), verifiedAt);
    manifest.insert(QStringLiteral("snapshots"), snapshots);

    QFile file(manifestPath(snapshotRootPath, game));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    file.write(QJsonDocument::fromVariant(manifest).toJson(QJsonDocument::Indented));
    return file.error() == QFile::NoError;
}

bool SnapshotManifestStore::importCloudSnapshotRecord(
    const QString &snapshotRootPath,
    const GameInfo &game,
    const QVariantMap &cloudRecord,
    const QString &zipPath) const
{
    const QString directoryPath = gameSnapshotDirectory(snapshotRootPath, game);
    const QString fileName = cloudRecord.value(QStringLiteral("fileName")).toString().trimmed();
    if (directoryPath.isEmpty() || fileName.isEmpty() || zipPath.trimmed().isEmpty()) {
        return false;
    }

    QDir directory(directoryPath);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        return false;
    }

    QVariantMap manifest = loadManifest(snapshotRootPath, game);
    QVariantList snapshots = manifest.value(QStringLiteral("snapshots")).toList();
    const QFileInfo zipInfo(zipPath);
    const QString nowUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    bool updated = false;

    for (QVariant &snapshotItem : snapshots) {
        QVariantMap snapshot = snapshotItem.toMap();
        if (snapshot.value(QStringLiteral("fileName")).toString() != fileName) {
            continue;
        }

        snapshot.insert(QStringLiteral("zipPath"), QDir::toNativeSeparators(zipInfo.absoluteFilePath()));
        snapshot.insert(QStringLiteral("remotePath"), cloudRecord.value(QStringLiteral("remotePath")).toString());
        snapshot.insert(QStringLiteral("uploadState"), QStringLiteral("cloud_downloaded"));
        snapshot.insert(QStringLiteral("remoteVerifiedAt"),
                        cloudRecord.value(QStringLiteral("remoteVerifiedAt"), QVariant(nowUtc)).toString());
        if (!cloudRecord.value(QStringLiteral("createdAtUtc")).toString().isEmpty()) {
            snapshot.insert(QStringLiteral("createdAtUtc"), cloudRecord.value(QStringLiteral("createdAtUtc")).toString());
        }
        if (!cloudRecord.value(QStringLiteral("zipSize")).toString().isEmpty()) {
            snapshot.insert(QStringLiteral("zipSize"), cloudRecord.value(QStringLiteral("zipSize")).toString());
        } else if (zipInfo.exists()) {
            snapshot.insert(QStringLiteral("zipSize"), QString::number(zipInfo.size()));
        }
        if (!cloudRecord.value(QStringLiteral("contentDigest")).toString().isEmpty()) {
            snapshot.insert(QStringLiteral("contentDigest"), cloudRecord.value(QStringLiteral("contentDigest")).toString());
        }
        if (cloudRecord.contains(QStringLiteral("fileCount"))) {
            snapshot.insert(QStringLiteral("fileCount"), cloudRecord.value(QStringLiteral("fileCount")).toInt());
        }
        if (!cloudRecord.value(QStringLiteral("totalBytes")).toString().isEmpty()) {
            snapshot.insert(QStringLiteral("totalBytes"), cloudRecord.value(QStringLiteral("totalBytes")).toString());
        }

        snapshotItem = snapshot;
        updated = true;
        break;
    }

    if (!updated) {
        QVariantMap snapshot;
        snapshot.insert(QStringLiteral("fileName"), fileName);
        snapshot.insert(QStringLiteral("zipPath"), QDir::toNativeSeparators(zipInfo.absoluteFilePath()));
        snapshot.insert(QStringLiteral("createdAtUtc"),
                        cloudRecord.value(QStringLiteral("createdAtUtc"), QVariant(nowUtc)).toString());
        snapshot.insert(QStringLiteral("zipSize"),
                        cloudRecord.value(QStringLiteral("zipSize"),
                                          QVariant(zipInfo.exists() ? QString::number(zipInfo.size()) : QString())).toString());
        snapshot.insert(QStringLiteral("contentDigest"), cloudRecord.value(QStringLiteral("contentDigest")).toString());
        snapshot.insert(QStringLiteral("fileCount"), cloudRecord.value(QStringLiteral("fileCount")).toInt());
        snapshot.insert(QStringLiteral("totalBytes"), cloudRecord.value(QStringLiteral("totalBytes")).toString());
        snapshot.insert(QStringLiteral("uploadState"), QStringLiteral("cloud_downloaded"));
        snapshot.insert(QStringLiteral("remotePath"), cloudRecord.value(QStringLiteral("remotePath")).toString());
        snapshot.insert(QStringLiteral("remoteVerifiedAt"),
                        cloudRecord.value(QStringLiteral("remoteVerifiedAt"), QVariant(nowUtc)).toString());
        snapshots.append(snapshot);
    }

    std::sort(snapshots.begin(), snapshots.end(), [](const QVariant &left, const QVariant &right) {
        const QVariantMap leftSnapshot = left.toMap();
        const QVariantMap rightSnapshot = right.toMap();
        const QString leftCreatedAt = leftSnapshot.value(QStringLiteral("createdAtUtc")).toString();
        const QString rightCreatedAt = rightSnapshot.value(QStringLiteral("createdAtUtc")).toString();
        if (!leftCreatedAt.isEmpty() && !rightCreatedAt.isEmpty() && leftCreatedAt != rightCreatedAt) {
            return leftCreatedAt < rightCreatedAt;
        }

        return leftSnapshot.value(QStringLiteral("fileName")).toString()
               < rightSnapshot.value(QStringLiteral("fileName")).toString();
    });

    const QVariantMap latestSnapshot = snapshots.isEmpty() ? QVariantMap() : snapshots.last().toMap();
    const QString gameName = game.displayName.isEmpty() ? game.name : game.displayName;
    manifest.insert(QStringLiteral("schemaVersion"), 1);
    manifest.insert(QStringLiteral("appId"), game.appId);
    manifest.insert(QStringLiteral("gameName"), gameName);
    manifest.insert(QStringLiteral("savePath"), game.savePath);
    manifest.insert(QStringLiteral("latestSnapshotFileName"), latestSnapshot.value(QStringLiteral("fileName")).toString());
    manifest.insert(QStringLiteral("latestSnapshotPath"), latestSnapshot.value(QStringLiteral("zipPath")).toString());
    manifest.insert(QStringLiteral("uploadState"), latestSnapshot.value(QStringLiteral("uploadState")).toString());
    manifest.insert(QStringLiteral("remotePath"), latestSnapshot.value(QStringLiteral("remotePath")).toString());
    manifest.insert(QStringLiteral("remoteVerifiedAt"),
                    latestSnapshot.value(QStringLiteral("remoteVerifiedAt"), QVariant(nowUtc)).toString());
    manifest.insert(QStringLiteral("snapshots"), snapshots);

    QFile file(manifestPath(snapshotRootPath, game));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    file.write(QJsonDocument::fromVariant(manifest).toJson(QJsonDocument::Indented));
    return file.error() == QFile::NoError;
}

QVariantList SnapshotManifestStore::snapshotRecords(const QString &snapshotRootPath, const GameInfo &game) const
{
    return loadManifest(snapshotRootPath, game).value(QStringLiteral("snapshots")).toList();
}

QVariantMap SnapshotManifestStore::normalizeSnapshotPaths(
    const QString &snapshotRootPath,
    const GameInfo &game,
    QVariantMap manifest) const
{
    const QString snapshotDirectory = gameSnapshotDirectory(snapshotRootPath, game);
    if (snapshotDirectory.trimmed().isEmpty() || manifest.isEmpty()) {
        return manifest;
    }

    QVariantList snapshots = manifest.value(QStringLiteral("snapshots")).toList();
    QVariantList existingSnapshots;
    for (QVariant &snapshotItem : snapshots) {
        QVariantMap snapshot = snapshotItem.toMap();
        const QString normalizedPath = normalizedZipPathForRecord(
            snapshotDirectory,
            snapshot.value(QStringLiteral("fileName")).toString(),
            snapshot.value(QStringLiteral("zipPath")).toString());
        const QFileInfo zipInfo(normalizedPath);
        if (!normalizedPath.isEmpty() && zipInfo.exists() && zipInfo.isFile()) {
            snapshot.insert(QStringLiteral("zipPath"), QDir::toNativeSeparators(zipInfo.absoluteFilePath()));
            existingSnapshots.append(snapshot);
        }
    }
    manifest.insert(QStringLiteral("snapshots"), existingSnapshots);

    const QVariantMap latestSnapshot = existingSnapshots.isEmpty() ? QVariantMap() : existingSnapshots.last().toMap();
    if (latestSnapshot.isEmpty()) {
        manifest.insert(QStringLiteral("latestSnapshotFileName"), QString());
        manifest.insert(QStringLiteral("latestSnapshotPath"), QString());
    } else {
        manifest.insert(QStringLiteral("latestSnapshotFileName"), latestSnapshot.value(QStringLiteral("fileName")).toString());
        manifest.insert(QStringLiteral("latestSnapshotPath"), latestSnapshot.value(QStringLiteral("zipPath")).toString());
    }

    return manifest;
}

QString SnapshotManifestStore::normalizedZipPathForRecord(
    const QString &snapshotDirectory,
    const QString &fileName,
    const QString &storedZipPath) const
{
    if (!fileName.trimmed().isEmpty()) {
        const QString currentPath = QDir(snapshotDirectory).absoluteFilePath(fileName.trimmed());
        const QFileInfo currentInfo(currentPath);
        if (currentInfo.exists()) {
            return QDir::toNativeSeparators(currentInfo.absoluteFilePath());
        }

        const QFileInfo storedInfo(storedZipPath.trimmed());
        if (storedInfo.exists()) {
            return QDir::toNativeSeparators(storedInfo.absoluteFilePath());
        }

        return QDir::toNativeSeparators(currentPath);
    }

    const QFileInfo storedInfo(storedZipPath.trimmed());
    if (storedInfo.exists()) {
        return QDir::toNativeSeparators(storedInfo.absoluteFilePath());
    }

    return storedZipPath;
}

QString SnapshotManifestStore::manifestPath(const QString &snapshotRootPath, const GameInfo &game) const
{
    const QString directoryPath = gameSnapshotDirectory(snapshotRootPath, game);
    if (directoryPath.isEmpty()) {
        return {};
    }

    return QDir(directoryPath).absoluteFilePath(QStringLiteral("snapshot-manifest.json"));
}

QString SnapshotManifestStore::safeDirectoryName(const GameInfo &game) const
{
    QString name = game.displayName.isEmpty() ? game.name : game.displayName;
    if (name.trimmed().isEmpty()) {
        name = game.appId;
    }

    name.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*])")), QStringLiteral("_"));
    name.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));
    name = name.trimmed();

    if (name.isEmpty()) {
        return QStringLiteral("Unknown_Game");
    }

    return name.left(80);
}
