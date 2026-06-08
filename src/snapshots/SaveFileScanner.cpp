#include "SaveFileScanner.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>

#include <algorithm>

SaveFileScanner::Result SaveFileScanner::scan(const QString &savePath) const
{
    Result result;
    const QString cleanPath = QDir::cleanPath(savePath.trimmed());

    if (cleanPath.isEmpty()) {
        result.error = QStringLiteral("存档路径为空，无法扫描");
        return result;
    }

    const QDir rootDir(cleanPath);
    if (!rootDir.exists()) {
        result.error = QStringLiteral("存档目录不存在，无法生成文件清单");
        return result;
    }

    /*
     * 快照化之前先做本地文件清单：
     * - relativePath：记录相对存档根目录的路径，后续 zip 内也会使用这个路径。
     * - size / lastModifiedUtc：用于 UI 展示和快速排查。
     * - md5：用于判断内容是否真的变化，避免只靠修改时间误判。
     *
     * 这里暂时扫描所有普通文件。后续如果发现某些游戏有临时锁文件，
     * 可以在这里增加排除规则。
     */
    QDirIterator iterator(
        cleanPath,
        QDir::Files | QDir::NoDotAndDotDot | QDir::Readable,
        QDirIterator::Subdirectories);

    while (iterator.hasNext()) {
        iterator.next();

        const QFileInfo fileInfo(iterator.filePath());
        FileEntry entry;
        entry.relativePath = QDir::toNativeSeparators(rootDir.relativeFilePath(fileInfo.absoluteFilePath()));
        entry.size = fileInfo.size();
        entry.lastModifiedUtc = fileInfo.lastModified().toUTC().toString(Qt::ISODate);
        entry.md5 = fileMd5(fileInfo.absoluteFilePath());

        if (entry.md5.isEmpty()) {
            result.error = QStringLiteral("读取存档文件失败：%1").arg(entry.relativePath);
            return result;
        }

        result.totalBytes += entry.size;
        result.files.append(entry);
    }

    std::sort(result.files.begin(), result.files.end(), [](const FileEntry &left, const FileEntry &right) {
        return QString::compare(left.relativePath, right.relativePath, Qt::CaseInsensitive) < 0;
    });

    result.fileCount = result.files.count();
    result.contentDigest = buildContentDigest(result.files);
    result.success = true;
    return result;
}

QString SaveFileScanner::fileMd5(const QString &filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Md5);
    if (!hash.addData(&file)) {
        return {};
    }

    return QString::fromLatin1(hash.result().toHex());
}

QString SaveFileScanner::buildContentDigest(const QList<FileEntry> &files) const
{
    QCryptographicHash hash(QCryptographicHash::Sha256);

    /*
     * 聚合指纹用于判断“整个存档目录”是否变化。
     * 只要文件相对路径、大小或 MD5 发生变化，最终 digest 就会不同。
     * 修改时间不参与 digest，避免某些同步/解压操作只改时间却没改内容时误建快照。
     */
    for (const FileEntry &file : files) {
        hash.addData(file.relativePath.toUtf8());
        hash.addData("\n", 1);
        hash.addData(QByteArray::number(file.size));
        hash.addData("\n", 1);
        hash.addData(file.md5.toLatin1());
        hash.addData("\n", 1);
    }

    return QString::fromLatin1(hash.result().toHex());
}
