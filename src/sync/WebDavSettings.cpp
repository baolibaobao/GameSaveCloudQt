#include "WebDavSettings.h"

#include <QDir>
#include <QSettings>
#include <QUrl>

#include <memory>

#include "storage/AppSettings.h"

bool WebDavConfig::isValid() const
{
    const QUrl url(serverUrl);
    return url.isValid()
           && !url.scheme().trimmed().isEmpty()
           && !url.host().trimmed().isEmpty()
           && !remoteRootPath.trimmed().isEmpty();
}

QVariantMap WebDavConfig::toVariantMap() const
{
    QVariantMap map;
    map.insert(QStringLiteral("serverUrl"), serverUrl);
    map.insert(QStringLiteral("username"), username);
    map.insert(QStringLiteral("password"), password);
    map.insert(QStringLiteral("remoteRootPath"), remoteRootPath);
    return map;
}

WebDavSettings::WebDavSettings() = default;

WebDavConfig WebDavSettings::loadConfig() const
{
    const std::unique_ptr<QSettings> settings = AppSettings::create();

    WebDavConfig config;
    config.serverUrl = normalizeServerUrl(settings->value(QStringLiteral("webdav/serverUrl")).toString());
    config.username = settings->value(QStringLiteral("webdav/username")).toString().trimmed();
    config.password = settings->value(QStringLiteral("webdav/password")).toString();
    config.remoteRootPath = normalizeRemoteRootPath(settings->value(
        QStringLiteral("webdav/remoteRootPath"),
        QStringLiteral("/GameSaveCloudQt")).toString());
    return config;
}

bool WebDavSettings::saveConfig(const WebDavConfig &config) const
{
    const QString serverUrl = normalizeServerUrl(config.serverUrl);
    const QString remoteRootPath = normalizeRemoteRootPath(config.remoteRootPath);

    if (serverUrl.isEmpty() || remoteRootPath.isEmpty()) {
        return false;
    }

    const std::unique_ptr<QSettings> settings = AppSettings::create();
    settings->setValue(QStringLiteral("webdav/serverUrl"), serverUrl);
    settings->setValue(QStringLiteral("webdav/username"), config.username.trimmed());
    settings->setValue(QStringLiteral("webdav/password"), config.password);
    settings->setValue(QStringLiteral("webdav/remoteRootPath"), remoteRootPath);
    settings->sync();
    return settings->status() == QSettings::NoError;
}

QString WebDavSettings::normalizeServerUrl(const QString &serverUrl) const
{
    QString cleanUrl = serverUrl.trimmed();
    while (cleanUrl.endsWith(QLatin1Char('/'))) {
        cleanUrl.chop(1);
    }

    return cleanUrl;
}

QString WebDavSettings::normalizeRemoteRootPath(const QString &remoteRootPath) const
{
    QString cleanPath = remoteRootPath.trimmed();
    if (cleanPath.isEmpty()) {
        cleanPath = QStringLiteral("/GameSaveCloudQt");
    }

    cleanPath.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (!cleanPath.startsWith(QLatin1Char('/'))) {
        cleanPath.prepend(QLatin1Char('/'));
    }

    while (cleanPath.length() > 1 && cleanPath.endsWith(QLatin1Char('/'))) {
        cleanPath.chop(1);
    }

    return cleanPath;
}
