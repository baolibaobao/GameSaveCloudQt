#pragma once

#include <QString>
#include <QVariantMap>

struct WebDavConfig
{
    QString serverUrl;
    QString username;
    QString password;
    QString remoteRootPath;

    bool isValid() const;
    QVariantMap toVariantMap() const;
};

class WebDavSettings
{
public:
    WebDavSettings();

    WebDavConfig loadConfig() const;
    bool saveConfig(const WebDavConfig &config) const;

private:
    QString normalizeServerUrl(const QString &serverUrl) const;
    QString normalizeRemoteRootPath(const QString &remoteRootPath) const;
};
