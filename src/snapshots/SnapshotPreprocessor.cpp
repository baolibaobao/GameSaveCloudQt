#include "SnapshotPreprocessor.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

QVariantMap SnapshotPreprocessor::analyzeGame(const GameInfo &game) const
{
    QVariantMap result;
    result.insert(QStringLiteral("success"), false);
    result.insert(QStringLiteral("needsSnapshot"), false);

    const QString rootPath = snapshotRootPath();
    if (rootPath.isEmpty()) {
        result.insert(QStringLiteral("status"), QStringLiteral("本地快照保存目录未设置"));
        result.insert(QStringLiteral("detail"), QStringLiteral("请先设置用于存放预处理 zip 快照的本地目录"));
        return result;
    }

    if (!game.savePathCanSync || game.savePath.trimmed().isEmpty()) {
        result.insert(QStringLiteral("status"), QStringLiteral("当前存档状态不可创建快照"));
        result.insert(QStringLiteral("detail"), QStringLiteral("只有状态为“可同步”的本地存档目录才会进入快照预处理"));
        return result;
    }

    const SaveFileScanner::Result scanResult = m_scanner.scan(game.savePath);
    if (!scanResult.success) {
        result.insert(QStringLiteral("status"), QStringLiteral("存档文件扫描失败"));
        result.insert(QStringLiteral("detail"), scanResult.error);
        return result;
    }

    if (scanResult.fileCount <= 0) {
        result.insert(QStringLiteral("status"), QStringLiteral("存档目录没有可快照化的文件"));
        result.insert(QStringLiteral("detail"), QStringLiteral("扫描成功，但目录中没有普通文件"));
        return result;
    }

    const QVariantMap oldManifest = m_manifestStore.loadManifest(rootPath, game);
    const QString lastSnapshotDigest = oldManifest.value(QStringLiteral("lastSnapshotContentDigest")).toString();
    const QVariantList snapshotRecords = oldManifest.value(QStringLiteral("snapshots")).toList();
    QVariantList existingSnapshotRecords;
    for (const QVariant &snapshotItem : snapshotRecords) {
        const QVariantMap snapshot = snapshotItem.toMap();
        const QFileInfo snapshotFile(snapshot.value(QStringLiteral("zipPath")).toString());
        if (snapshotFile.exists() && snapshotFile.isFile()) {
            existingSnapshotRecords.append(snapshot);
        }
    }
    const QVariantMap latestSnapshot = existingSnapshotRecords.isEmpty()
                                           ? QVariantMap()
                                           : existingSnapshotRecords.last().toMap();
    bool hasSnapshotForCurrentContent = false;
    bool hasMissingSnapshotRecordForCurrentContent = false;
    for (const QVariant &snapshotItem : snapshotRecords) {
        const QVariantMap snapshot = snapshotItem.toMap();
        if (snapshot.value(QStringLiteral("contentDigest")).toString() != scanResult.contentDigest) {
            continue;
        }

        const QFileInfo snapshotFile(snapshot.value(QStringLiteral("zipPath")).toString());
        if (snapshotFile.exists() && snapshotFile.isFile()) {
            hasSnapshotForCurrentContent = true;
            break;
        }

        hasMissingSnapshotRecordForCurrentContent = true;
    }

    const bool needsSnapshot = !hasSnapshotForCurrentContent;

    if (!m_manifestStore.saveScanBaseline(rootPath, game, scanResult)) {
        result.insert(QStringLiteral("status"), QStringLiteral("本地快照 manifest 写入失败"));
        result.insert(QStringLiteral("detail"), QStringLiteral("请确认快照保存目录有写入权限"));
        return result;
    }

    const QString gameSnapshotDirectory = m_manifestStore.gameSnapshotDirectory(rootPath, game);
    result.insert(QStringLiteral("success"), true);
    result.insert(QStringLiteral("snapshotDirectory"), QDir::toNativeSeparators(gameSnapshotDirectory));
    result.insert(QStringLiteral("fileCount"), scanResult.fileCount);
    result.insert(QStringLiteral("totalBytes"), QString::number(scanResult.totalBytes));
    result.insert(QStringLiteral("contentDigest"), scanResult.contentDigest);
    result.insert(QStringLiteral("needsSnapshot"), needsSnapshot);
    result.insert(QStringLiteral("snapshotCount"), existingSnapshotRecords.count());
    result.insert(QStringLiteral("latestSnapshotFileName"), latestSnapshot.value(QStringLiteral("fileName")).toString());
    result.insert(QStringLiteral("latestSnapshotPath"), latestSnapshot.value(QStringLiteral("zipPath")).toString());

    if (lastSnapshotDigest.isEmpty()) {
        result.insert(QStringLiteral("status"), QStringLiteral("首次扫描完成，需要创建新的存档快照"));
        result.insert(QStringLiteral("detail"),
                      QStringLiteral("已记录 %1 个文件，后续阶段会在该目录下生成带时间戳的 zip 快照：%2")
                          .arg(scanResult.fileCount)
                          .arg(QDir::toNativeSeparators(gameSnapshotDirectory)));
        return result;
    }

    if (hasMissingSnapshotRecordForCurrentContent && needsSnapshot) {
        result.insert(QStringLiteral("status"), QStringLiteral("本地快照文件已被删除，需要重新创建快照"));
        result.insert(QStringLiteral("detail"),
                      QStringLiteral("manifest 中仍有当前存档内容的历史记录，但对应 zip 文件已不在本地快照目录；可以重新创建快照，或从云端下载已有快照"));
        return result;
    }

    if (needsSnapshot) {
        result.insert(QStringLiteral("status"), QStringLiteral("检测到存档文件变化，需要创建新的存档快照"));
        result.insert(QStringLiteral("detail"),
                      QStringLiteral("当前扫描到 %1 个文件，内容指纹已变化，后续阶段会生成新的 zip 版本")
                          .arg(scanResult.fileCount));
        return result;
    }

    result.insert(QStringLiteral("status"), QStringLiteral("存档文件无变化，暂不需要创建新快照"));
    result.insert(QStringLiteral("detail"),
                  QStringLiteral("当前 %1 个文件与上次扫描内容一致，因此不会重复生成快照")
                      .arg(scanResult.fileCount));
    return result;
}

QVariantMap SnapshotPreprocessor::createSnapshotForGame(const GameInfo &game) const
{
    QVariantMap result = analyzeGame(game);
    if (!result.value(QStringLiteral("success")).toBool()) {
        return result;
    }

    const bool needsSnapshot = result.value(QStringLiteral("needsSnapshot")).toBool();
    if (!needsSnapshot) {
        result.insert(QStringLiteral("status"), QStringLiteral("存档文件无变化，未创建重复快照"));
        result.insert(QStringLiteral("detail"), QStringLiteral("当前内容已经有对应的本地 zip 快照"));
        return result;
    }

    const QString rootPath = snapshotRootPath();
    const SaveFileScanner::Result scanResult = m_scanner.scan(game.savePath);
    if (!scanResult.success) {
        result.insert(QStringLiteral("success"), false);
        result.insert(QStringLiteral("status"), QStringLiteral("创建快照前扫描失败"));
        result.insert(QStringLiteral("detail"), scanResult.error);
        return result;
    }

    const QString snapshotDirectory = m_manifestStore.gameSnapshotDirectory(rootPath, game);
    const SnapshotPackageWriter::Result packageResult = m_packageWriter.createZipSnapshot(
        game.savePath,
        snapshotDirectory,
        game);
    if (!packageResult.success) {
        result.insert(QStringLiteral("success"), false);
        result.insert(QStringLiteral("status"), QStringLiteral("zip 快照创建失败"));
        result.insert(QStringLiteral("detail"), packageResult.error);
        return result;
    }

    if (!m_manifestStore.appendSnapshotRecord(rootPath, game, scanResult, packageResult)) {
        result.insert(QStringLiteral("success"), false);
        result.insert(QStringLiteral("status"), QStringLiteral("快照已生成，但 manifest 更新失败"));
        result.insert(QStringLiteral("detail"), QStringLiteral("zip 文件已创建，请检查快照目录写入权限"));
        result.insert(QStringLiteral("snapshotPath"), packageResult.zipPath);
        return result;
    }

    result.insert(QStringLiteral("success"), true);
    result.insert(QStringLiteral("needsSnapshot"), false);
    result.insert(QStringLiteral("status"), QStringLiteral("本地 zip 快照已创建"));
    result.insert(QStringLiteral("detail"),
                  QStringLiteral("已生成快照 %1，等待后续 WebDAV 阶段上传云端")
                      .arg(packageResult.fileName));
    result.insert(QStringLiteral("snapshotDirectory"), snapshotDirectory);
    result.insert(QStringLiteral("snapshotPath"), packageResult.zipPath);
    result.insert(QStringLiteral("snapshotFileName"), packageResult.fileName);
    result.insert(QStringLiteral("snapshotCount"), snapshotsForGame(game).count());
    return result;
}

QVariantMap SnapshotPreprocessor::deleteSnapshotsForGame(const GameInfo &game) const
{
    QVariantMap result;
    result.insert(QStringLiteral("success"), false);
    result.insert(QStringLiteral("needsSnapshot"), false);

    const QString rootPath = snapshotRootPath();
    if (rootPath.isEmpty() || !game.isValid()) {
        result.insert(QStringLiteral("status"), QStringLiteral("本地存档快照删除失败"));
        result.insert(QStringLiteral("detail"), QStringLiteral("快照根目录未设置或游戏信息无效"));
        return result;
    }

    const QFileInfo rootInfo(rootPath);
    const QString rootAbsolutePath = QDir::fromNativeSeparators(QDir::cleanPath(rootInfo.absoluteFilePath()));
    const QString directoryPath = snapshotDirectoryForGame(game);
    const QFileInfo directoryInfo(directoryPath);
    const QString directoryAbsolutePath = QDir::fromNativeSeparators(QDir::cleanPath(directoryInfo.absoluteFilePath()));
    const QString rootPrefix = rootAbsolutePath.endsWith(QLatin1Char('/'))
                                   ? rootAbsolutePath
                                   : rootAbsolutePath + QLatin1Char('/');

    /*
     * 删除的是软件自己的“本地存档快照目录”，不是游戏真实存档目录。
     * 这里必须确认目标目录位于快照根目录内部，防止路径异常时误删其他位置。
     */
    if (directoryAbsolutePath.isEmpty()
        || directoryAbsolutePath.compare(rootAbsolutePath, Qt::CaseInsensitive) == 0
        || !directoryAbsolutePath.startsWith(rootPrefix, Qt::CaseInsensitive)) {
        result.insert(QStringLiteral("status"), QStringLiteral("本地存档快照删除失败"));
        result.insert(QStringLiteral("detail"),
                      QStringLiteral("安全检查未通过，目标目录不在快照根目录内部：%1")
                          .arg(QDir::toNativeSeparators(directoryAbsolutePath)));
        return result;
    }

    const int oldSnapshotCount = snapshotsForGame(game).count();
    if (!directoryInfo.exists()) {
        result.insert(QStringLiteral("success"), true);
        result.insert(QStringLiteral("status"), QStringLiteral("本地存档快照已为空"));
        result.insert(QStringLiteral("detail"), QStringLiteral("该游戏没有可删除的本地存档快照目录"));
        result.insert(QStringLiteral("snapshotDirectory"), QDir::toNativeSeparators(directoryAbsolutePath));
        result.insert(QStringLiteral("snapshotCount"), 0);
        return result;
    }

    QDir directory(directoryAbsolutePath);
    if (!directory.removeRecursively()) {
        result.insert(QStringLiteral("status"), QStringLiteral("本地存档快照删除失败"));
        result.insert(QStringLiteral("detail"),
                      QStringLiteral("无法删除目录，请确认没有程序正在占用其中的 zip 或 manifest 文件：%1")
                          .arg(QDir::toNativeSeparators(directoryAbsolutePath)));
        result.insert(QStringLiteral("snapshotDirectory"), QDir::toNativeSeparators(directoryAbsolutePath));
        return result;
    }

    result.insert(QStringLiteral("success"), true);
    result.insert(QStringLiteral("status"), QStringLiteral("本地存档快照已删除"));
    result.insert(QStringLiteral("detail"),
                  QStringLiteral("已删除该游戏的本地存档快照目录，共清理 %1 个本地 zip 快照；云端快照不受影响")
                      .arg(oldSnapshotCount));
    result.insert(QStringLiteral("snapshotDirectory"), QDir::toNativeSeparators(directoryAbsolutePath));
    result.insert(QStringLiteral("latestSnapshotFileName"), QString());
    result.insert(QStringLiteral("latestSnapshotPath"), QString());
    result.insert(QStringLiteral("snapshotCount"), 0);
    return result;
}

QVariantList SnapshotPreprocessor::snapshotsForGame(const GameInfo &game) const
{
    const QString rootPath = snapshotRootPath();
    if (rootPath.isEmpty() || !game.isValid()) {
        return {};
    }

    QVariantList snapshots;
    const QVariantList records = m_manifestStore.snapshotRecords(rootPath, game);
    for (const QVariant &snapshotItem : records) {
        const QVariantMap snapshot = snapshotItem.toMap();
        const QFileInfo snapshotFile(snapshot.value(QStringLiteral("zipPath")).toString());
        if (snapshotFile.exists() && snapshotFile.isFile()) {
            snapshots.append(snapshot);
        }
    }
    std::reverse(snapshots.begin(), snapshots.end());
    return snapshots;
}

QVariantList SnapshotPreprocessor::allSnapshotRecordsForGame(const GameInfo &game) const
{
    const QString rootPath = snapshotRootPath();
    if (rootPath.isEmpty() || !game.isValid()) {
        return {};
    }

    QVariantList snapshots = m_manifestStore.snapshotRecords(rootPath, game);
    std::reverse(snapshots.begin(), snapshots.end());
    return snapshots;
}

QString SnapshotPreprocessor::snapshotDirectoryForGame(const GameInfo &game) const
{
    const QString rootPath = snapshotRootPath();
    if (rootPath.isEmpty() || !game.isValid()) {
        return {};
    }

    return m_manifestStore.gameSnapshotDirectory(rootPath, game);
}

bool SnapshotPreprocessor::markSnapshotUploadState(
    const GameInfo &game,
    const QString &zipPath,
    const QString &fileName,
    const QString &uploadState,
    const QString &remotePath) const
{
    const QString rootPath = snapshotRootPath();
    if (rootPath.isEmpty() || !game.isValid()) {
        return false;
    }

    return m_manifestStore.updateSnapshotUploadState(
        rootPath,
        game,
        zipPath,
        fileName,
        uploadState,
        remotePath);
}

bool SnapshotPreprocessor::importCloudSnapshotRecord(
    const GameInfo &game,
    const QVariantMap &cloudRecord,
    const QString &zipPath) const
{
    const QString rootPath = snapshotRootPath();
    if (rootPath.isEmpty() || !game.isValid()) {
        return false;
    }

    return m_manifestStore.importCloudSnapshotRecord(rootPath, game, cloudRecord, zipPath);
}

QString SnapshotPreprocessor::snapshotRootPath() const
{
    return m_settings.snapshotRootPath();
}

bool SnapshotPreprocessor::setSnapshotRootPath(const QString &path) const
{
    return m_settings.setSnapshotRootPath(path);
}
