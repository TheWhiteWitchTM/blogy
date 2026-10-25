#include "bluesky.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTextBoundaryFinder>
#include <QUrl>

namespace {

QString errorText(QNetworkReply *reply, const QByteArray &body)
{
    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    const QString msg = obj.value(QStringLiteral("message")).toString();
    const QString err = obj.value(QStringLiteral("error")).toString();
    if (!msg.isEmpty() || !err.isEmpty())
        return (err + QStringLiteral(": ") + msg).trimmed();
    return reply->errorString();
}

QNetworkRequest jsonRequest(const QString &service, const QString &nsid)
{
    QString base = service;
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    QNetworkRequest req(QUrl(base + QStringLiteral("/xrpc/") + nsid));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(30000);
    return req;
}

} // namespace

BlueskyClient::BlueskyClient(QNetworkAccessManager *net, QObject *parent)
    : QObject(parent), net_(net)
{
}

int BlueskyClient::graphemeLength(const QString &text)
{
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    int count = 0;
    while (finder.toNextBoundary() != -1)
        ++count;
    return count;
}

QJsonObject BlueskyClient::buildRecord(const QString &text, const QString &linkUrl, const QString &cardTitle,
                                       const QString &cardDescription, const QString &createdAt)
{
    QJsonObject record;
    record.insert(QStringLiteral("$type"), QStringLiteral("app.bsky.feed.post"));
    record.insert(QStringLiteral("text"), text);
    record.insert(QStringLiteral("createdAt"), createdAt);
    record.insert(QStringLiteral("langs"), QJsonArray{QStringLiteral("en")});

    // Facet indices are UTF-8 byte offsets, not QString (UTF-16) positions.
    QJsonArray facets;
    static const QRegularExpression urlRx(QStringLiteral(R"(https?://[^\s<>"]+[^\s<>".,;:!?)\]])"));
    auto it = urlRx.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        const int byteStart = text.left(m.capturedStart()).toUtf8().size();
        const int byteEnd = byteStart + m.captured().toUtf8().size();
        QJsonObject index{{QStringLiteral("byteStart"), byteStart}, {QStringLiteral("byteEnd"), byteEnd}};
        QJsonObject feature{{QStringLiteral("$type"), QStringLiteral("app.bsky.richtext.facet#link")},
                            {QStringLiteral("uri"), m.captured()}};
        facets.append(QJsonObject{{QStringLiteral("index"), index}, {QStringLiteral("features"), QJsonArray{feature}}});
    }
    static const QRegularExpression tagRx(QStringLiteral(R"((?:^|\s)(#([^\d\s#]\S*))(?=\s|$))"));
    auto tags = tagRx.globalMatch(text);
    while (tags.hasNext()) {
        const auto m = tags.next();
        QString tag = m.captured(2);
        while (!tag.isEmpty() && QStringLiteral(".,;:!?").contains(tag.back()))
            tag.chop(1);
        if (tag.isEmpty())
            continue;
        const int byteStart = text.left(m.capturedStart(1)).toUtf8().size();
        const int byteEnd = byteStart + (QLatin1Char('#') + tag).toUtf8().size();
        QJsonObject index{{QStringLiteral("byteStart"), byteStart}, {QStringLiteral("byteEnd"), byteEnd}};
        QJsonObject feature{{QStringLiteral("$type"), QStringLiteral("app.bsky.richtext.facet#tag")},
                            {QStringLiteral("tag"), tag}};
        facets.append(QJsonObject{{QStringLiteral("index"), index}, {QStringLiteral("features"), QJsonArray{feature}}});
    }
    if (!facets.isEmpty())
        record.insert(QStringLiteral("facets"), facets);

    if (!linkUrl.isEmpty()) {
        QJsonObject external{{QStringLiteral("uri"), linkUrl},
                             {QStringLiteral("title"), cardTitle},
                             {QStringLiteral("description"), cardDescription}};
        record.insert(QStringLiteral("embed"), QJsonObject{{QStringLiteral("$type"), QStringLiteral("app.bsky.embed.external")},
                                                           {QStringLiteral("external"), external}});
    }
    return record;
}

QString BlueskyClient::webUrlForUri(const QString &atUri, const QString &handle)
{
    // at://did:plc:xyz/app.bsky.feed.post/3k... -> https://bsky.app/profile/<handle>/post/3k...
    const QString rkey = atUri.section(QLatin1Char('/'), -1);
    return QStringLiteral("https://bsky.app/profile/%1/post/%2").arg(handle, rkey);
}

void BlueskyClient::post(const QString &service, const QString &handle, const QString &appPassword,
                         const QString &text, const QString &linkUrl, const QString &cardTitle,
                         const QString &cardDescription)
{
    if (handle.isEmpty() || appPassword.isEmpty()) {
        emit failed(QStringLiteral("Bluesky handle or app password is missing. Set them in Settings or "
                                   "BLUESKY_HANDLE / BLUESKY_APP_PASSWORD."));
        return;
    }
    const QJsonObject login{{QStringLiteral("identifier"), handle}, {QStringLiteral("password"), appPassword}};
    QNetworkReply *reply = net_->post(jsonRequest(service, QStringLiteral("com.atproto.server.createSession")),
                                      QJsonDocument(login).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [=] {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(QStringLiteral("Bluesky sign-in failed: ") + errorText(reply, body));
            return;
        }
        const QJsonObject session = QJsonDocument::fromJson(body).object();
        const QString jwt = session.value(QStringLiteral("accessJwt")).toString();
        const QString did = session.value(QStringLiteral("did")).toString();
        const QString realHandle = session.value(QStringLiteral("handle")).toString(handle);
        if (jwt.isEmpty() || did.isEmpty()) {
            emit failed(QStringLiteral("Bluesky sign-in returned no session."));
            return;
        }
        const QString createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        const QJsonObject payload{{QStringLiteral("repo"), did},
                                  {QStringLiteral("collection"), QStringLiteral("app.bsky.feed.post")},
                                  {QStringLiteral("record"), buildRecord(text, linkUrl, cardTitle, cardDescription, createdAt)}};
        QNetworkRequest req = jsonRequest(service, QStringLiteral("com.atproto.repo.createRecord"));
        req.setRawHeader("Authorization", "Bearer " + jwt.toUtf8());
        QNetworkReply *create = net_->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        connect(create, &QNetworkReply::finished, this, [=] {
            create->deleteLater();
            const QByteArray out = create->readAll();
            if (create->error() != QNetworkReply::NoError) {
                emit failed(QStringLiteral("Bluesky post failed: ") + errorText(create, out));
                return;
            }
            const QString uri = QJsonDocument::fromJson(out).object().value(QStringLiteral("uri")).toString();
            emit posted(webUrlForUri(uri, realHandle));
        });
    });
}
