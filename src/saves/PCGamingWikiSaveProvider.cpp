#include "PCGamingWikiSaveProvider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

PCGamingWikiSaveProvider::PCGamingWikiSaveProvider(QObject *parent)
    : QObject(parent)
{
}

void PCGamingWikiSaveProvider::requestSavePath(const QString &appId)
{
    const QString cleanAppId = appId.trimmed();
    if (cleanAppId.isEmpty()) {
        return;
    }

    if (m_pathCache.contains(cleanAppId)) {
        emit savePathReady(cleanAppId, m_pathCache.value(cleanAppId), m_pageNameCache.value(cleanAppId));
        return;
    }

    if (m_queued.contains(cleanAppId) || m_inFlight.contains(cleanAppId)) {
        return;
    }

    m_queue.enqueue(cleanAppId);
    m_queued.insert(cleanAppId);
    startNextRequests();
}

void PCGamingWikiSaveProvider::startNextRequests()
{
    while (!m_queue.isEmpty() && m_inFlight.count() < m_maxConcurrentRequests) {
        const QString appId = m_queue.dequeue();
        m_queued.remove(appId);
        startCargoRequest(appId);
    }
}

void PCGamingWikiSaveProvider::startCargoRequest(const QString &appId)
{
    m_inFlight.insert(appId);

    /*
     * PCGamingWiki 是 MediaWiki + Cargo。
     * 第一跳用 Infobox_game.Steam_AppID 通过 Cargo 查页面 ID。
     * 存档路径本身没有稳定的独立 JSON 字段，所以拿到页面后还要拉 wikitext 解析。
     */
    QUrl url(QStringLiteral("https://www.pcgamingwiki.com/w/api.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("action"), QStringLiteral("cargoquery"));
    query.addQueryItem(QStringLiteral("tables"), QStringLiteral("Infobox_game"));
    query.addQueryItem(QStringLiteral("fields"),
                       QStringLiteral("Infobox_game._pageID=PageID,Infobox_game._pageName=Page"));
    query.addQueryItem(QStringLiteral("where"),
                       QStringLiteral("Infobox_game.Steam_AppID HOLDS \"%1\"").arg(appId));
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GameSaveCloudQt/0.1"));

    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, appId]() {
        const QNetworkReply::NetworkError error = reply->error();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (error != QNetworkReply::NoError) {
            m_inFlight.remove(appId);
            emit savePathFailed(appId, QStringLiteral("PCGamingWiki 页面查询失败"));
            startNextRequests();
            return;
        }

        const PageLookup lookup = parseCargoLookup(appId, payload);
        if (lookup.pageId.isEmpty()) {
            m_inFlight.remove(appId);
            emit savePathFailed(appId, QStringLiteral("PCGamingWiki 未匹配到 Steam AppID"));
            startNextRequests();
            return;
        }

        startPageRequest(appId, lookup);
    });
}

void PCGamingWikiSaveProvider::startPageRequest(const QString &appId, const PageLookup &lookup)
{
    QUrl url(QStringLiteral("https://www.pcgamingwiki.com/w/api.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("action"), QStringLiteral("query"));
    query.addQueryItem(QStringLiteral("prop"), QStringLiteral("revisions"));
    query.addQueryItem(QStringLiteral("rvprop"), QStringLiteral("content"));
    query.addQueryItem(QStringLiteral("rvslots"), QStringLiteral("main"));
    query.addQueryItem(QStringLiteral("pageids"), lookup.pageId);
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    query.addQueryItem(QStringLiteral("formatversion"), QStringLiteral("2"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GameSaveCloudQt/0.1"));

    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, appId, lookup]() {
        m_inFlight.remove(appId);

        const QNetworkReply::NetworkError error = reply->error();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (error != QNetworkReply::NoError) {
            emit savePathFailed(appId, QStringLiteral("PCGamingWiki 页面内容下载失败"));
            startNextRequests();
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(payload);
        const QJsonArray pages = document.object()
                                   .value(QStringLiteral("query")).toObject()
                                   .value(QStringLiteral("pages")).toArray();
        if (pages.isEmpty()) {
            emit savePathFailed(appId, QStringLiteral("PCGamingWiki 页面内容为空"));
            startNextRequests();
            return;
        }

        const QJsonArray revisions = pages.first().toObject().value(QStringLiteral("revisions")).toArray();
        if (revisions.isEmpty()) {
            emit savePathFailed(appId, QStringLiteral("PCGamingWiki 没有页面版本内容"));
            startNextRequests();
            return;
        }

        const QString wikitext = revisions.first().toObject()
                                  .value(QStringLiteral("slots")).toObject()
                                  .value(QStringLiteral("main")).toObject()
                                  .value(QStringLiteral("content")).toString();
        const QString rawPath = parseSavePathFromWikitext(wikitext);
        if (rawPath.isEmpty()) {
            emit savePathFailed(appId, QStringLiteral("PCGamingWiki 未解析到 Windows 存档路径"));
            startNextRequests();
            return;
        }

        m_pathCache.insert(appId, rawPath);
        m_pageNameCache.insert(appId, lookup.pageName);
        emit savePathReady(appId, rawPath, lookup.pageName);
        startNextRequests();
    });
}

PCGamingWikiSaveProvider::PageLookup PCGamingWikiSaveProvider::parseCargoLookup(
    const QString &appId,
    const QByteArray &payload) const
{
    Q_UNUSED(appId)

    const QJsonDocument document = QJsonDocument::fromJson(payload);
    const QJsonArray results = document.object().value(QStringLiteral("cargoquery")).toArray();
    if (results.isEmpty()) {
        return {};
    }

    const QJsonObject title = results.first().toObject().value(QStringLiteral("title")).toObject();
    PageLookup lookup;
    lookup.pageId = title.value(QStringLiteral("PageID")).toString().trimmed();
    lookup.pageName = title.value(QStringLiteral("Page")).toString().trimmed();
    return lookup;
}

QString PCGamingWikiSaveProvider::parseSavePathFromWikitext(const QString &wikitext) const
{
    const QString section = saveDataSection(wikitext);
    if (section.isEmpty()) {
        return {};
    }

    QString bestPath;
    int bestRank = 100;

    for (const QString &templateText : extractTemplates(section, QStringLiteral("Game data/saves"))) {
        const QStringList args = splitTemplateArguments(templateText);
        if (args.count() < 3) {
            continue;
        }

        const QString platform = cleanTemplateArgument(args.at(1));
        const QString rawPath = cleanTemplateArgument(args.at(2));
        const int rank = preferenceRank(platform);

        if (rank < bestRank && !rawPath.isEmpty()) {
            bestRank = rank;
            bestPath = rawPath;
        }
    }

    return bestPath;
}

QString PCGamingWikiSaveProvider::saveDataSection(const QString &wikitext) const
{
    const QRegularExpression startRegex(
        QStringLiteral(R"((?im)^={2,4}\s*Save game data location\s*={2,4}\s*$)"));
    const QRegularExpressionMatch startMatch = startRegex.match(wikitext);
    if (!startMatch.hasMatch()) {
        return {};
    }

    const int sectionStart = startMatch.capturedEnd();
    const QRegularExpression nextHeadingRegex(QStringLiteral(R"((?m)^={2,4}\s*[^=\n].*={2,4}\s*$)"));
    const QRegularExpressionMatch nextHeading = nextHeadingRegex.match(wikitext, sectionStart);
    const int sectionEnd = nextHeading.hasMatch() ? nextHeading.capturedStart() : wikitext.length();
    return wikitext.mid(sectionStart, sectionEnd - sectionStart);
}

QStringList PCGamingWikiSaveProvider::extractTemplates(const QString &text, const QString &templateName) const
{
    QStringList templates;
    const QString marker = QStringLiteral("{{%1").arg(templateName);
    int searchFrom = 0;

    while (searchFrom < text.length()) {
        const int start = text.indexOf(marker, searchFrom, Qt::CaseInsensitive);
        if (start < 0) {
            break;
        }

        int depth = 0;
        for (int i = start; i < text.length() - 1; ++i) {
            const QString twoChars = text.mid(i, 2);
            if (twoChars == QStringLiteral("{{")) {
                ++depth;
                ++i;
                continue;
            }

            if (twoChars == QStringLiteral("}}")) {
                --depth;
                ++i;
                if (depth == 0) {
                    templates.append(text.mid(start, i - start + 1));
                    searchFrom = i + 1;
                    break;
                }
            }
        }

        if (searchFrom <= start) {
            searchFrom = start + marker.length();
        }
    }

    return templates;
}

QStringList PCGamingWikiSaveProvider::splitTemplateArguments(const QString &templateText) const
{
    QStringList args;
    QString current;
    QString innerText = templateText.trimmed();

    /*
     * templateText 是完整模板，例如：
     *
     *   {{Game data/saves|Windows|{{P|userprofile\appdata\locallow}}\Foo\Bar\}}
     *
     * 如果直接逐字符切分，最后一个参数会把外层模板的 "}}" 也吃进去，
     * 导致 UI 里出现路径末尾残留 "}}"。先剥掉最外层 "{{" / "}}"，再按
     * 顶层管道符拆参数，可以保留内部 {{P|...}} 模板，同时不污染路径值。
     */
    if (innerText.startsWith(QStringLiteral("{{"))) {
        innerText.remove(0, 2);
    }

    if (innerText.endsWith(QStringLiteral("}}"))) {
        innerText.chop(2);
    }

    int depth = 0;

    for (int i = 0; i < innerText.length(); ++i) {
        const QString twoChars = innerText.mid(i, 2);
        if (twoChars == QStringLiteral("{{")) {
            ++depth;
            current += twoChars;
            ++i;
            continue;
        }

        if (twoChars == QStringLiteral("}}")) {
            current += twoChars;
            --depth;
            ++i;
            continue;
        }

        if (innerText.at(i) == QLatin1Char('|') && depth == 0) {
            args.append(current.trimmed());
            current.clear();
            continue;
        }

        current += innerText.at(i);
    }

    if (!current.trimmed().isEmpty()) {
        args.append(current.trimmed());
    }

    return args;
}

QString PCGamingWikiSaveProvider::cleanTemplateArgument(QString value) const
{
    value = value.trimmed();
    value.remove(QRegularExpression(QStringLiteral(R"(<ref[^>]*>.*?</ref>)"),
                                    QRegularExpression::DotMatchesEverythingOption));
    value.remove(QRegularExpression(QStringLiteral(R"(<ref[^/]*/>)")));
    value.remove(QRegularExpression(QStringLiteral(R"(<!--.*?-->)"),
                                    QRegularExpression::DotMatchesEverythingOption));
    value.remove(QStringLiteral("'''"));
    value.remove(QStringLiteral("''"));
    return value.trimmed();
}

int PCGamingWikiSaveProvider::preferenceRank(const QString &platform) const
{
    const QString cleanPlatform = platform.toLower();
    if (cleanPlatform.contains(QStringLiteral("windows"))) {
        return 0;
    }

    if (cleanPlatform.contains(QStringLiteral("steam"))) {
        return 1;
    }

    return 10;
}
