#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QVariantMap>

class QJsonArray;

class SteamMetadataClient : public QObject
{
    Q_OBJECT

public:
    explicit SteamMetadataClient(QObject *parent = nullptr);

    void requestAppDetails(const QString &appId);

signals:
    void metadataReady(const QString &appId, const QVariantMap &metadata);
    void metadataFailed(const QString &appId, const QString &reason);

private:
    void startNextRequests();
    void startRequest(const QString &appId);
    QVariantMap parseAppDetails(const QString &appId, const QByteArray &payload) const;
    QStringList jsonArrayToStringList(const QJsonArray &array) const;

    QNetworkAccessManager m_network;
    QQueue<QString> m_queue;
    QSet<QString> m_queued;
    QSet<QString> m_inFlight;
    QHash<QString, QVariantMap> m_cache;
    int m_maxConcurrentRequests = 4;
};
