#pragma once

#include <QString>

class SnapshotSettings
{
public:
    SnapshotSettings();

    QString snapshotRootPath() const;
    bool setSnapshotRootPath(const QString &path) const;

private:
    QString normalizedPath(const QString &path) const;
};
