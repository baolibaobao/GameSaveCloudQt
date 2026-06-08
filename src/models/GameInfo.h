#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

struct GameInfo
{
    QString appId;
    QString name;
    QString localizedName;
    QString displayName;
    QStringList developers;
    QStringList publishers;
    QString shortDescription;
    QString installDir;
    QString libraryPath;
    QString manifestPath;
    QString headerImageUrl;
    QString savePath;
    QString savePathStatus;
    QString savePathSource;
    QString savePathAvailability;
    QString savePathDetail;
    bool savePathCanSync = false;
    bool isManualGame = false;
    QString executablePath;
    QString pcGamingWikiPageName;
    QString pcGamingWikiPageId;
    QString processName;
    QString runningStatus;
    QString syncStatus;
    QString metadataStatus;
    QString snapshotStatus;
    QString snapshotDetail;
    QString snapshotDirectory;
    QString latestSnapshotFileName;
    QString latestSnapshotPath;
    int snapshotCount = 0;
    bool snapshotNeedsCreate = false;
    QString cloudSnapshotStatus;
    QString cloudSnapshotDetail;
    QString latestCloudSnapshotFileName;
    QString latestCloudSnapshotPath;
    int cloudSnapshotCount = 0;

    bool isValid() const
    {
        return !appId.trimmed().isEmpty() && !name.trimmed().isEmpty();
    }

    QVariantMap toVariantMap() const
    {
        QVariantMap map;
        map.insert(QStringLiteral("appId"), appId);
        map.insert(QStringLiteral("name"), name);
        map.insert(QStringLiteral("localizedName"), localizedName);
        map.insert(QStringLiteral("displayName"), displayName.isEmpty() ? name : displayName);
        map.insert(QStringLiteral("developers"), developers);
        map.insert(QStringLiteral("publishers"), publishers);
        map.insert(QStringLiteral("shortDescription"), shortDescription);
        map.insert(QStringLiteral("installDir"), installDir);
        map.insert(QStringLiteral("libraryPath"), libraryPath);
        map.insert(QStringLiteral("manifestPath"), manifestPath);
        map.insert(QStringLiteral("headerImageUrl"), headerImageUrl);
        map.insert(QStringLiteral("savePath"), savePath);
        map.insert(QStringLiteral("savePathStatus"), savePathStatus);
        map.insert(QStringLiteral("savePathSource"), savePathSource);
        map.insert(QStringLiteral("savePathAvailability"), savePathAvailability);
        map.insert(QStringLiteral("savePathDetail"), savePathDetail);
        map.insert(QStringLiteral("savePathCanSync"), savePathCanSync);
        map.insert(QStringLiteral("isManualGame"), isManualGame);
        map.insert(QStringLiteral("executablePath"), executablePath);
        map.insert(QStringLiteral("pcGamingWikiPageName"), pcGamingWikiPageName);
        map.insert(QStringLiteral("pcGamingWikiPageId"), pcGamingWikiPageId);
        map.insert(QStringLiteral("processName"), processName);
        map.insert(QStringLiteral("runningStatus"), runningStatus);
        map.insert(QStringLiteral("syncStatus"), syncStatus);
        map.insert(QStringLiteral("metadataStatus"), metadataStatus);
        map.insert(QStringLiteral("snapshotStatus"), snapshotStatus);
        map.insert(QStringLiteral("snapshotDetail"), snapshotDetail);
        map.insert(QStringLiteral("snapshotDirectory"), snapshotDirectory);
        map.insert(QStringLiteral("latestSnapshotFileName"), latestSnapshotFileName);
        map.insert(QStringLiteral("latestSnapshotPath"), latestSnapshotPath);
        map.insert(QStringLiteral("snapshotCount"), snapshotCount);
        map.insert(QStringLiteral("snapshotNeedsCreate"), snapshotNeedsCreate);
        map.insert(QStringLiteral("cloudSnapshotStatus"), cloudSnapshotStatus);
        map.insert(QStringLiteral("cloudSnapshotDetail"), cloudSnapshotDetail);
        map.insert(QStringLiteral("latestCloudSnapshotFileName"), latestCloudSnapshotFileName);
        map.insert(QStringLiteral("latestCloudSnapshotPath"), latestCloudSnapshotPath);
        map.insert(QStringLiteral("cloudSnapshotCount"), cloudSnapshotCount);
        return map;
    }
};
