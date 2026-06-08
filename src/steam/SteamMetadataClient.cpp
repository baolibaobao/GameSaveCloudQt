#include "SteamMetadataClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

SteamMetadataClient::SteamMetadataClient(QObject *parent)
    : QObject(parent)
{
}

void SteamMetadataClient::requestAppDetails(const QString &appId)
{
    const QString cleanAppId = appId.trimmed();
    if (cleanAppId.isEmpty()) {
        return;
    }

    /*
     * Steam appdetails 返回的是相对稳定的商店元数据。
     * 这里用内存缓存避免“重新扫描”时重复请求同一个 AppID：
     * - 缓存命中：立即把结果发回模型。
     * - 正在排队/请求：不重复加入队列。
     * - 缓存未命中：进入限流队列，避免一次性对大量游戏并发发请求。
     */
    if (m_cache.contains(cleanAppId)) {
        emit metadataReady(cleanAppId, m_cache.value(cleanAppId));
        return;
    }

    if (m_queued.contains(cleanAppId) || m_inFlight.contains(cleanAppId)) {
        return;
    }

    m_queue.enqueue(cleanAppId);
    m_queued.insert(cleanAppId);
    startNextRequests();
}

void SteamMetadataClient::startNextRequests()
{
    while (!m_queue.isEmpty() && m_inFlight.count() < m_maxConcurrentRequests) {
        const QString appId = m_queue.dequeue();
        m_queued.remove(appId);
        startRequest(appId);
    }
}

void SteamMetadataClient::startRequest(const QString &appId)
{
    m_inFlight.insert(appId);

    QUrl url(QStringLiteral("https://store.steampowered.com/api/appdetails"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("appids"), appId);
    query.addQueryItem(QStringLiteral("l"), QStringLiteral("schinese"));
    query.addQueryItem(QStringLiteral("cc"), QStringLiteral("cn"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GameSaveCloudQt/0.1"));

    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, appId]() {
        m_inFlight.remove(appId);

        const QNetworkReply::NetworkError error = reply->error();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (error != QNetworkReply::NoError) {
            emit metadataFailed(appId, QStringLiteral("Steam appdetails 请求失败"));
            startNextRequests();
            return;
        }

        const QVariantMap metadata = parseAppDetails(appId, payload);
        if (metadata.isEmpty()) {
            emit metadataFailed(appId, QStringLiteral("Steam appdetails 未返回可用资料"));
            startNextRequests();
            return;
        }

        m_cache.insert(appId, metadata);
        emit metadataReady(appId, metadata);
        startNextRequests();
    });
}

QVariantMap SteamMetadataClient::parseAppDetails(const QString &appId, const QByteArray &payload) const
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        return {};
    }

    const QJsonObject root = document.object();
    const QJsonObject appObject = root.value(appId).toObject();
    if (!appObject.value(QStringLiteral("success")).toBool(false)) {
        return {};
    }

    const QJsonObject data = appObject.value(QStringLiteral("data")).toObject();
    if (data.isEmpty()) {
        return {};
    }

    const QString localizedName = data.value(QStringLiteral("name")).toString().trimmed();
    const QString headerImageUrl = data.value(QStringLiteral("header_image")).toString().trimmed();
    const QString shortDescription = data.value(QStringLiteral("short_description")).toString().trimmed();
    const QStringList developers = jsonArrayToStringList(data.value(QStringLiteral("developers")).toArray());
    const QStringList publishers = jsonArrayToStringList(data.value(QStringLiteral("publishers")).toArray());

    QVariantMap metadata;
    metadata.insert(QStringLiteral("localizedName"), localizedName);
    metadata.insert(QStringLiteral("displayName"), localizedName);
    metadata.insert(QStringLiteral("developers"), developers);
    metadata.insert(QStringLiteral("publishers"), publishers);
    metadata.insert(QStringLiteral("shortDescription"), shortDescription);
    metadata.insert(QStringLiteral("headerImageUrl"), headerImageUrl);
    metadata.insert(QStringLiteral("metadataStatus"), QStringLiteral("资料已更新"));
    return metadata;
}

QStringList SteamMetadataClient::jsonArrayToStringList(const QJsonArray &array) const
{
    QStringList values;
    for (const QJsonValue &value : array) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) {
            values.append(text);
        }
    }

    return values;
}
