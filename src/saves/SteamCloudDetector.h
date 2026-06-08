#pragma once

#include <QString>

class SteamCloudDetector
{
public:
    bool hasSteamCloudCacheFiles(const QString &steamPath, const QString &appId) const;

private:
    bool directoryHasFiles(const QString &path) const;
};
