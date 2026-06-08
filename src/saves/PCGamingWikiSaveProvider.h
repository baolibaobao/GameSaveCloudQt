#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>

class PCGamingWikiSaveProvider : public QObject
{
    Q_OBJECT

public:
    explicit PCGamingWikiSaveProvider(QObject *parent = nullptr);

    void requestSavePath(const QString &appId);

signals:
    void savePathReady(const QString &appId, const QString &rawPath, const QString &pageName);
    void savePathFailed(const QString &appId, const QString &reason);

private:
    struct PageLookup
    {
        QString pageId;
        QString pageName;
    };

    void startNextRequests();
    void startCargoRequest(const QString &appId);
    void startPageRequest(const QString &appId, const PageLookup &lookup);

    PageLookup parseCargoLookup(const QString &appId, const QByteArray &payload) const;
    QString parseSavePathFromWikitext(const QString &wikitext) const;
    QString saveDataSection(const QString &wikitext) const;
    QStringList extractTemplates(const QString &text, const QString &templateName) const;
    QStringList splitTemplateArguments(const QString &templateText) const;
    QString cleanTemplateArgument(QString value) const;
    int preferenceRank(const QString &platform) const;

    QNetworkAccessManager m_network;
    QQueue<QString> m_queue;
    QSet<QString> m_queued;
    QSet<QString> m_inFlight;
    QHash<QString, QString> m_pathCache;
    QHash<QString, QString> m_pageNameCache;
    int m_maxConcurrentRequests = 2;
};
