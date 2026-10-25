#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

// Minimal AT Protocol client for one job: post a link to a new blog post.
//   1. com.atproto.server.createSession  (handle + app password -> accessJwt, did)
//   2. com.atproto.repo.createRecord     (app.bsky.feed.post with a link facet
//                                         and an app.bsky.embed.external card)
// The app password is only sent to the configured PDS and never logged.
class BlueskyClient : public QObject {
    Q_OBJECT
public:
    explicit BlueskyClient(QNetworkAccessManager *net, QObject *parent = nullptr);

    void post(const QString &service, const QString &handle, const QString &appPassword,
              const QString &text, const QString &linkUrl, const QString &cardTitle,
              const QString &cardDescription);

    // Pure helpers, covered by --self-test.
    static QJsonObject buildRecord(const QString &text, const QString &linkUrl, const QString &cardTitle,
                                   const QString &cardDescription, const QString &createdAt);
    static int graphemeLength(const QString &text);
    static QString webUrlForUri(const QString &atUri, const QString &handle);

signals:
    void posted(const QString &webUrl);
    void failed(const QString &message);

private:
    QNetworkAccessManager *net_;
};
