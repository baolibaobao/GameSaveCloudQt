#include "SnapshotRestoreManager.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>

QVariantMap SnapshotRestoreManager::restoreSnapshot(
    const GameInfo &game,
    const QString &zipPath,
    const QString &backupRootPath) const
{
    QVariantMap result;
    result.insert(QStringLiteral("success"), false);

    const QFileInfo zipInfo(QDir::cleanPath(zipPath.trimmed()));
    if (!zipInfo.exists() || !zipInfo.isFile() || zipInfo.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) != 0) {
        result.insert(QStringLiteral("status"), QStringLiteral("快照恢复失败"));
        result.insert(QStringLiteral("detail"), QStringLiteral("请选择本地已经存在的 zip 快照文件"));
        return result;
    }

    const QDir saveDir(QDir::cleanPath(game.savePath.trimmed()));
    const QFileInfo saveInfo(saveDir.absolutePath());
    if (game.savePath.trimmed().isEmpty() || !saveInfo.exists() || !saveInfo.isDir()) {
        result.insert(QStringLiteral("status"), QStringLiteral("快照恢复失败"));
        result.insert(QStringLiteral("detail"), QStringLiteral("游戏真实存档目录不存在，无法恢复快照"));
        return result;
    }

    const QString saveAbsolutePath = QDir::fromNativeSeparators(QDir::cleanPath(saveInfo.absoluteFilePath()));
    if (saveAbsolutePath.length() < 6 || QDir(saveAbsolutePath).isRoot()) {
        result.insert(QStringLiteral("status"), QStringLiteral("快照恢复失败"));
        result.insert(QStringLiteral("detail"), QStringLiteral("存档目录安全检查未通过，已停止恢复"));
        return result;
    }

    const QString backupDirectoryPath = backupDirectoryForGame(backupRootPath, game);
    QDir backupDirectory(backupDirectoryPath);
    if (!backupDirectory.exists() && !backupDirectory.mkpath(QStringLiteral("."))) {
        result.insert(QStringLiteral("status"), QStringLiteral("恢复前备份失败"));
        result.insert(QStringLiteral("detail"), QStringLiteral("无法创建恢复前备份目录：%1").arg(QDir::toNativeSeparators(backupDirectoryPath)));
        return result;
    }

    const QString backupFileName = QStringLiteral("%1_before_restore_%2.zip")
                                       .arg(safeSnapshotBaseName(game), timestampForFileName());
    const QString backupZipPath = backupDirectory.absoluteFilePath(backupFileName);
    QString error;
    if (!createZipFromDirectory(saveAbsolutePath, backupZipPath, &error)) {
        result.insert(QStringLiteral("status"), QStringLiteral("恢复前备份失败"));
        result.insert(QStringLiteral("detail"), QStringLiteral("恢复已停止，当前存档未被修改。原因：%1").arg(error));
        return result;
    }

    QTemporaryDir extractDirectory;
    if (!extractDirectory.isValid()) {
        result.insert(QStringLiteral("status"), QStringLiteral("快照恢复失败"));
        result.insert(QStringLiteral("detail"), QStringLiteral("无法创建临时解压目录，当前存档未被修改"));
        result.insert(QStringLiteral("backupPath"), QDir::toNativeSeparators(backupZipPath));
        return result;
    }

    if (!extractZipToDirectory(zipInfo.absoluteFilePath(), extractDirectory.path(), &error)) {
        result.insert(QStringLiteral("status"), QStringLiteral("快照解压失败"));
        result.insert(QStringLiteral("detail"), QStringLiteral("当前存档已完成恢复前备份，但快照未覆盖。原因：%1").arg(error));
        result.insert(QStringLiteral("backupPath"), QDir::toNativeSeparators(backupZipPath));
        return result;
    }

    if (!directoryHasEntries(extractDirectory.path())) {
        result.insert(QStringLiteral("status"), QStringLiteral("快照恢复失败"));
        result.insert(QStringLiteral("detail"), QStringLiteral("zip 快照解压后没有任何文件或文件夹，已停止覆盖"));
        result.insert(QStringLiteral("backupPath"), QDir::toNativeSeparators(backupZipPath));
        return result;
    }

    if (!removeDirectoryChildren(saveAbsolutePath, &error)) {
        result.insert(QStringLiteral("status"), QStringLiteral("快照恢复失败"));
        result.insert(QStringLiteral("detail"), QStringLiteral("恢复前备份已完成，但清理当前存档目录失败：%1").arg(error));
        result.insert(QStringLiteral("backupPath"), QDir::toNativeSeparators(backupZipPath));
        return result;
    }

    if (!copyDirectoryContents(extractDirectory.path(), saveAbsolutePath, &error)) {
        result.insert(QStringLiteral("status"), QStringLiteral("快照恢复失败"));
        result.insert(QStringLiteral("detail"), QStringLiteral("恢复前备份已完成，但复制快照内容失败：%1").arg(error));
        result.insert(QStringLiteral("backupPath"), QDir::toNativeSeparators(backupZipPath));
        return result;
    }

    result.insert(QStringLiteral("success"), true);
    result.insert(QStringLiteral("status"), QStringLiteral("快照恢复完成"));
    result.insert(QStringLiteral("detail"),
                  QStringLiteral("已将 %1 恢复到游戏存档目录；覆盖前当前存档已自动备份")
                      .arg(zipInfo.fileName()));
    result.insert(QStringLiteral("backupPath"), QDir::toNativeSeparators(backupZipPath));
    result.insert(QStringLiteral("snapshotPath"), QDir::toNativeSeparators(zipInfo.absoluteFilePath()));
    return result;
}

QString SnapshotRestoreManager::backupDirectoryForGame(const QString &backupRootPath, const GameInfo &game) const
{
    QString rootPath = QDir::cleanPath(backupRootPath.trimmed());
    if (rootPath.isEmpty()) {
        rootPath = QDir::cleanPath(QStringLiteral("restore-backups"));
    }

    return QDir(rootPath).absoluteFilePath(safeSnapshotBaseName(game));
}

QString SnapshotRestoreManager::safeSnapshotBaseName(const GameInfo &game) const
{
    QString name = game.displayName.isEmpty() ? game.name : game.displayName;
    if (name.trimmed().isEmpty()) {
        name = game.appId;
    }

    name.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*])")), QStringLiteral("_"));
    name.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral("_"));
    name = name.trimmed();
    return name.isEmpty() ? QStringLiteral("Game_Save") : name.left(80);
}

QString SnapshotRestoreManager::timestampForFileName() const
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd(hh-mm-ss)"));
}

QString SnapshotRestoreManager::escapedPowerShellSingleQuoted(QString value) const
{
    value.replace(QStringLiteral("'"), QStringLiteral("''"));
    return value;
}

bool SnapshotRestoreManager::createZipFromDirectory(
    const QString &sourceDirectoryPath,
    const QString &zipPath,
    QString *error) const
{
#ifdef Q_OS_WIN
    const QString nativeSourcePath = QDir::toNativeSeparators(sourceDirectoryPath);
    const QString nativeZipPath = QDir::toNativeSeparators(zipPath);
    const QString command = QStringLiteral(
        "Add-Type -AssemblyName System.IO.Compression.FileSystem; "
        "if (Test-Path -LiteralPath '%2') { Remove-Item -LiteralPath '%2' -Force }; "
        "[System.IO.Compression.ZipFile]::CreateFromDirectory('%1', '%2', [System.IO.Compression.CompressionLevel]::Optimal, $false)")
                                .arg(escapedPowerShellSingleQuoted(nativeSourcePath),
                                     escapedPowerShellSingleQuoted(nativeZipPath));

    QProcess process;
    process.start(QStringLiteral("powershell.exe"),
                  {QStringLiteral("-NoProfile"),
                   QStringLiteral("-ExecutionPolicy"),
                   QStringLiteral("Bypass"),
                   QStringLiteral("-Command"),
                   command});

    if (!process.waitForStarted(10000)) {
        if (error) {
            *error = QStringLiteral("PowerShell 备份进程启动失败");
        }
        return false;
    }

    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished(3000);
        if (error) {
            *error = QStringLiteral("恢复前备份创建超时");
        }
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString stderrText = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (error) {
            *error = stderrText.isEmpty() ? QStringLiteral("PowerShell 备份执行失败") : stderrText;
        }
        return false;
    }

    const QFileInfo zipInfo(zipPath);
    if (!zipInfo.exists() || zipInfo.size() <= 0) {
        if (error) {
            *error = QStringLiteral("恢复前备份 zip 未生成或文件为空");
        }
        return false;
    }

    return true;
#else
    if (error) {
        *error = QStringLiteral("当前阶段仅支持 Windows PowerShell 恢复前备份");
    }
    return false;
#endif
}

bool SnapshotRestoreManager::extractZipToDirectory(
    const QString &zipPath,
    const QString &targetDirectoryPath,
    QString *error) const
{
#ifdef Q_OS_WIN
    const QString nativeZipPath = QDir::toNativeSeparators(zipPath);
    const QString nativeTargetPath = QDir::toNativeSeparators(targetDirectoryPath);
    const QString command = QStringLiteral(
        "Add-Type -AssemblyName System.IO.Compression.FileSystem; "
        "[System.IO.Compression.ZipFile]::ExtractToDirectory('%1', '%2')")
                                .arg(escapedPowerShellSingleQuoted(nativeZipPath),
                                     escapedPowerShellSingleQuoted(nativeTargetPath));

    QProcess process;
    process.start(QStringLiteral("powershell.exe"),
                  {QStringLiteral("-NoProfile"),
                   QStringLiteral("-ExecutionPolicy"),
                   QStringLiteral("Bypass"),
                   QStringLiteral("-Command"),
                   command});

    if (!process.waitForStarted(10000)) {
        if (error) {
            *error = QStringLiteral("PowerShell 解压进程启动失败");
        }
        return false;
    }

    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished(3000);
        if (error) {
            *error = QStringLiteral("zip 快照解压超时");
        }
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString stderrText = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (error) {
            *error = stderrText.isEmpty() ? QStringLiteral("PowerShell 解压执行失败") : stderrText;
        }
        return false;
    }

    return true;
#else
    if (error) {
        *error = QStringLiteral("当前阶段仅支持 Windows PowerShell 解压恢复");
    }
    return false;
#endif
}

bool SnapshotRestoreManager::directoryHasEntries(const QString &directoryPath) const
{
    const QDir directory(directoryPath);
    return !directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty();
}

bool SnapshotRestoreManager::removeDirectoryChildren(const QString &directoryPath, QString *error) const
{
    QDir directory(directoryPath);
    if (!directory.exists()) {
        if (error) {
            *error = QStringLiteral("目录不存在：%1").arg(QDir::toNativeSeparators(directoryPath));
        }
        return false;
    }

    const QFileInfo directoryInfo(directory.absolutePath());
    if (directoryInfo.absoluteFilePath().length() < 6 || directory.isRoot()) {
        if (error) {
            *error = QStringLiteral("目录安全检查未通过：%1").arg(QDir::toNativeSeparators(directory.absolutePath()));
        }
        return false;
    }

    const QFileInfoList entries = directory.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::DirsFirst);
    for (const QFileInfo &entry : entries) {
        bool ok = false;
        if (entry.isDir() && !entry.isSymLink()) {
            ok = QDir(entry.absoluteFilePath()).removeRecursively();
        } else {
            ok = QFile::remove(entry.absoluteFilePath());
        }

        if (!ok) {
            if (error) {
                *error = QStringLiteral("无法删除：%1").arg(QDir::toNativeSeparators(entry.absoluteFilePath()));
            }
            return false;
        }
    }

    return true;
}

bool SnapshotRestoreManager::copyDirectoryContents(
    const QString &sourceDirectoryPath,
    const QString &targetDirectoryPath,
    QString *error) const
{
    QDir sourceDirectory(sourceDirectoryPath);
    QDir targetDirectory(targetDirectoryPath);
    if (!sourceDirectory.exists()) {
        if (error) {
            *error = QStringLiteral("源目录不存在：%1").arg(QDir::toNativeSeparators(sourceDirectoryPath));
        }
        return false;
    }

    if (!targetDirectory.exists() && !targetDirectory.mkpath(QStringLiteral("."))) {
        if (error) {
            *error = QStringLiteral("无法创建目标目录：%1").arg(QDir::toNativeSeparators(targetDirectoryPath));
        }
        return false;
    }

    QDirIterator iterator(
        sourceDirectory.absolutePath(),
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDirIterator::Subdirectories);

    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo sourceInfo = iterator.fileInfo();
        const QString relativePath = sourceDirectory.relativeFilePath(sourceInfo.absoluteFilePath());
        const QString targetPath = targetDirectory.absoluteFilePath(relativePath);

        if (sourceInfo.isDir() && !sourceInfo.isSymLink()) {
            if (!QDir().mkpath(targetPath)) {
                if (error) {
                    *error = QStringLiteral("无法创建目录：%1").arg(QDir::toNativeSeparators(targetPath));
                }
                return false;
            }
            continue;
        }

        const QFileInfo targetInfo(targetPath);
        if (!QDir().mkpath(targetInfo.absolutePath())) {
            if (error) {
                *error = QStringLiteral("无法创建文件所在目录：%1").arg(QDir::toNativeSeparators(targetInfo.absolutePath()));
            }
            return false;
        }

        if (targetInfo.exists() && !QFile::remove(targetPath)) {
            if (error) {
                *error = QStringLiteral("无法覆盖文件：%1").arg(QDir::toNativeSeparators(targetPath));
            }
            return false;
        }

        if (!QFile::copy(sourceInfo.absoluteFilePath(), targetPath)) {
            if (error) {
                *error = QStringLiteral("无法复制文件：%1").arg(QDir::toNativeSeparators(targetPath));
            }
            return false;
        }
    }

    return true;
}
