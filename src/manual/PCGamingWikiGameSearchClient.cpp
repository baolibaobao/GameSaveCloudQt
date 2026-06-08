#include "PCGamingWikiGameSearchClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>
#include <QVariant>

PCGamingWikiGameSearchClient::PCGamingWikiGameSearchClient(QObject *parent)
    : QObject(parent)
{
}

void PCGamingWikiGameSearchClient::requestGameSearch(
    const QString &requestId,
    const QStringList &candidateNames)
{
    const QString cleanRequestId = requestId.trimmed();
    if (cleanRequestId.isEmpty()) {
        return;
    }

    QStringList cleanCandidates;
    QSet<QString> seenNames;
    for (const QString &candidate : candidateNames) {
        const QString cleanName = candidate.trimmed();
        const QString key = cleanName.toLower();
        if (cleanName.isEmpty() || seenNames.contains(key)) {
            continue;
        }

        seenNames.insert(key);
        cleanCandidates.append(cleanName);
    }

    if (cleanCandidates.isEmpty()) {
        emit gameSearchFailed(cleanRequestId, QStringLiteral("无法从 exe 文件名推断游戏名称"));
        return;
    }

    /*
     * 手动添加游戏时没有本地 AppID，因此先走 MediaWiki 搜索。
     * 搜到页面后再用 Cargo 读取 Infobox_game.Steam_AppID：
     * - 如果页面有 Steam AppID，就能复用 Steam CDN 图片、Steam appdetails 中文名、
     *   以及现有的 PCGamingWiki AppID 存档路径解析。
     * - 如果页面没有 Steam AppID，仍然保留手动游戏，但让用户手动指定存档目录。
     */
    PendingSearch pending;
    pending.candidateNames = cleanCandidates;
    m_pendingSearches.insert(cleanRequestId, pending);
    startNextNameSearch(cleanRequestId);
}

void PCGamingWikiGameSearchClient::startNextNameSearch(const QString &requestId)
{
    if (!m_pendingSearches.contains(requestId)) {
        return;
    }

    PendingSearch &pending = m_pendingSearches[requestId];
    if (pending.nextCandidateIndex >= pending.candidateNames.count()) {
        const QString fallbackPageName = pending.fallbackPageName;
        const QString fallbackPageId = pending.fallbackPageId;
        m_pendingSearches.remove(requestId);

        if (!fallbackPageName.isEmpty()) {
            emit gameSearchResolved(requestId, fallbackPageName, fallbackPageId, QString());
        } else {
            emit gameSearchFailed(requestId, QStringLiteral("PCGamingWiki 未搜索到匹配的游戏页面"));
        }
        return;
    }

    const QString candidateName = pending.candidateNames.at(pending.nextCandidateIndex);
    ++pending.nextCandidateIndex;

    QUrl url(QStringLiteral("https://www.pcgamingwiki.com/w/api.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("action"), QStringLiteral("query"));
    query.addQueryItem(QStringLiteral("list"), QStringLiteral("search"));
    query.addQueryItem(QStringLiteral("srsearch"), candidateName);
    query.addQueryItem(QStringLiteral("srlimit"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GameSaveCloudQt/0.1"));

    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (error != QNetworkReply::NoError) {
            startNextNameSearch(requestId);
            return;
        }

        const SearchPage page = parseFirstSearchPage(payload);
        if (page.pageName.isEmpty()) {
            startNextNameSearch(requestId);
            return;
        }

        startCargoRequest(requestId, page);
    });
}

void PCGamingWikiGameSearchClient::startCargoRequest(const QString &requestId, const SearchPage &page)
{
    if (!m_pendingSearches.contains(requestId)) {
        return;
    }

    QUrl url(QStringLiteral("https://www.pcgamingwiki.com/w/api.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("action"), QStringLiteral("cargoquery"));
    query.addQueryItem(QStringLiteral("tables"), QStringLiteral("Infobox_game"));
    query.addQueryItem(QStringLiteral("fields"),
                       QStringLiteral("Infobox_game.Steam_AppID=SteamAppID"));
    query.addQueryItem(QStringLiteral("where"),
                       QStringLiteral("Infobox_game._pageName=\"%1\"").arg(escapedCargoString(page.pageName)));
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GameSaveCloudQt/0.1"));

    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId, page]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (!m_pendingSearches.contains(requestId)) {
            return;
        }

        if (error != QNetworkReply::NoError) {
            startNextNameSearch(requestId);
            return;
        }

        const QString steamAppId = parseSteamAppId(payload);
        if (steamAppId.isEmpty()) {
            PendingSearch &pending = m_pendingSearches[requestId];
            if (pending.fallbackPageName.isEmpty()) {
                pending.fallbackPageName = page.pageName;
                pending.fallbackPageId = page.pageId;
            }
            startNextNameSearch(requestId);
            return;
        }

        m_pendingSearches.remove(requestId);
        emit gameSearchResolved(requestId, page.pageName, page.pageId, steamAppId);
    });
}

PCGamingWikiGameSearchClient::SearchPage PCGamingWikiGameSearchClient::parseFirstSearchPage(
    const QByteArray &payload) const
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    const QJsonArray results = document.object()
                                   .value(QStringLiteral("query")).toObject()
                                   .value(QStringLiteral("search")).toArray();
    if (results.isEmpty()) {
        return {};
    }

    const QJsonObject result = results.first().toObject();
    SearchPage page;
    page.pageName = result.value(QStringLiteral("title")).toString().trimmed();
    const int pageId = result.value(QStringLiteral("pageid")).toInt();
    if (pageId > 0) {
        page.pageId = QString::number(pageId);
    }
    return page;
}

QString PCGamingWikiGameSearchClient::parseSteamAppId(const QByteArray &payload) const
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    const QJsonArray results = document.object().value(QStringLiteral("cargoquery")).toArray();
    if (results.isEmpty()) {
        return {};
    }

    QString steamAppId = results.first().toObject()
                             .value(QStringLiteral("title")).toObject()
                             .value(QStringLiteral("SteamAppID")).toVariant()
                             .toString()
                             .trimmed();

    /*
     * Cargo 的 Steam_AppID 字段有时可能是逗号分隔或带额外标记的文本。
     * 我们只取第一个数字 AppID；后续如果遇到多商店版本，可以再让用户选择。
     */
    const QRegularExpression appIdRegex(QStringLiteral(R"(\d+)"));
    const QRegularExpressionMatch match = appIdRegex.match(steamAppId);
    return match.hasMatch() ? match.captured(0) : QString();
}

QString PCGamingWikiGameSearchClient::escapedCargoString(QString value) const
{
    value.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    value.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    return value;
}
