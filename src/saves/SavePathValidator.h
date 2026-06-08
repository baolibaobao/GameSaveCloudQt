#pragma once

#include <QString>

#include "SteamCloudDetector.h"

class SavePathValidator
{
public:
    struct Result
    {
        QString availability;
        QString detail;
        bool canSync = false;
    };

    Result validate(const QString &path, const QString &appId, const QString &steamPath) const;

private:
    bool containsUnresolvedPlaceholder(const QString &path) const;
    bool directoryHasFiles(const QString &path) const;
    SteamCloudDetector m_steamCloudDetector;
};
