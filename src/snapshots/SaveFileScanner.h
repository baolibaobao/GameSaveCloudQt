#pragma once

#include <QList>
#include <QString>

class SaveFileScanner
{
public:
    struct FileEntry
    {
        QString relativePath;
        qint64 size = 0;
        QString lastModifiedUtc;
        QString md5;
    };

    struct Result
    {
        bool success = false;
        QString error;
        QList<FileEntry> files;
        int fileCount = 0;
        qint64 totalBytes = 0;
        QString contentDigest;
    };

    Result scan(const QString &savePath) const;

private:
    QString fileMd5(const QString &filePath) const;
    QString buildContentDigest(const QList<FileEntry> &files) const;
};
