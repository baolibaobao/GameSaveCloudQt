#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>

class PCGamingWikiGameSearchClient : public QObject
{
    Q_OBJECT

public:
    explicit PCGamingWikiGameSearchClient(QObject *parent = nullptr);

    void requestGameSearch(const QString &requestId, const QStringList &candidateNames);

signals:
    void gameSearchResolved(
        const QString &requestId,
        const QString &pageName,
        const QString &pageId,
        const QString &steamAppId);
    void gameSearchFailed(const QString &requestId, const QString &reason);

private:
    struct PendingSearch
    {
        QStringList candidateNames;
        int nextCandidateIndex = 0;
        QString fallbackPageName;
        QString fallbackPageId;
    };

    struct SearchPage
    {
        QString pageName;
        QString pageId;
    };

    void startNextNameSearch(const QString &requestId);
    void startCargoRequest(const QString &requestId, const SearchPage &page);
    SearchPage parseFirstSearchPage(const QByteArray &payload) const;
    QString parseSteamAppId(const QByteArray &payload) const;
    QString escapedCargoString(QString value) const;

    QNetworkAccessManager m_network;
    QHash<QString, PendingSearch> m_pendingSearches;
};
