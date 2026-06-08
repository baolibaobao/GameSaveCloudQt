#include "SnapshotPackageWriter.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

SnapshotPackageWriter::Result SnapshotPackageWriter::createZipSnapshot(
    const QString &savePath,
    const QString &snapshotDirectory,
    const GameInfo &game) const
{
    Result result;

    const QDir saveDir(QDir::cleanPath(savePath.trimmed()));
    if (!saveDir.exists()) {
        result.error = QStringLiteral("存档目录不存在，无法创建 zip 快照");
        return result;
    }

    QDir outputDir(QDir::cleanPath(snapshotDirectory.trimmed()));
    if (!outputDir.exists() && !outputDir.mkpath(QStringLiteral("."))) {
        result.error = QStringLiteral("快照输出目录创建失败");
        return result;
    }

    const QString fileName = QStringLiteral("%1_%2.zip")
                                 .arg(safeSnapshotBaseName(game), timestampForFileName());
    const QString zipPath = outputDir.absoluteFilePath(fileName);

#ifdef Q_OS_WIN
    /*
     * 阶段 6C 先通过 PowerShell 调用 .NET ZipFile 完成 zip 打包。
     * 这样不需要额外引入第三方压缩库，能先把“快照版本管理”的产品闭环跑通。
     *
     * 注意这里压缩的是存档目录下的所有内容，而不是把存档根目录本身套进 zip。
     * 例如 savePath = ...\GameSave，zip 内部会直接包含 PlayerSave.dat 等文件。
     */
    const QString nativeSavePath = QDir::toNativeSeparators(saveDir.absolutePath());
    const QString nativeZipPath = QDir::toNativeSeparators(zipPath);
    const QString command = QStringLiteral(
        "Add-Type -AssemblyName System.IO.Compression.FileSystem; "
        "if (Test-Path -LiteralPath '%2') { Remove-Item -LiteralPath '%2' -Force }; "
        "[System.IO.Compression.ZipFile]::CreateFromDirectory('%1', '%2', [System.IO.Compression.CompressionLevel]::Optimal, $false)")
                                .arg(escapedPowerShellSingleQuoted(nativeSavePath),
                                     escapedPowerShellSingleQuoted(nativeZipPath));

    QProcess process;
    process.start(QStringLiteral("powershell.exe"),
                  {QStringLiteral("-NoProfile"),
                   QStringLiteral("-ExecutionPolicy"),
                   QStringLiteral("Bypass"),
                   QStringLiteral("-Command"),
                   command});

    if (!process.waitForStarted(10000)) {
        result.error = QStringLiteral("PowerShell 压缩进程启动失败");
        return result;
    }

    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished(3000);
        result.error = QStringLiteral("zip 快照创建超时");
        return result;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString stderrText = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        result.error = stderrText.isEmpty()
                           ? QStringLiteral("PowerShell Compress-Archive 执行失败")
                           : stderrText;
        return result;
    }
#else
    result.error = QStringLiteral("当前阶段的 zip 快照创建仅支持 Windows PowerShell");
    return result;
#endif

    const QFileInfo zipInfo(zipPath);
    if (!zipInfo.exists() || zipInfo.size() <= 0) {
        result.error = QStringLiteral("zip 快照文件未生成或文件为空");
        return result;
    }

    result.success = true;
    result.zipPath = QDir::toNativeSeparators(zipInfo.absoluteFilePath());
    result.fileName = zipInfo.fileName();
    result.zipSize = zipInfo.size();
    result.createdAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return result;
}

QString SnapshotPackageWriter::safeSnapshotBaseName(const GameInfo &game) const
{
    QString name = game.displayName.isEmpty() ? game.name : game.displayName;
    if (name.trimmed().isEmpty()) {
        name = game.appId;
    }

    name.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*])")), QStringLiteral("_"));
    name.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral("_"));
    name = name.trimmed();

    if (name.isEmpty()) {
        return QStringLiteral("Game_Save");
    }

    return name.left(80);
}

QString SnapshotPackageWriter::timestampForFileName() const
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd(hh-mm-ss)"));
}

QString SnapshotPackageWriter::escapedPowerShellSingleQuoted(QString value) const
{
    value.replace(QStringLiteral("'"), QStringLiteral("''"));
    return value;
}
