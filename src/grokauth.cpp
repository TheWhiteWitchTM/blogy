#include "grokauth.h"

#include "config.h"

#include <QDir>
#include <QEventLoop>
#include <QTimeZone>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

QString authPath()
{
    return QDir::homePath() + QStringLiteral("/.grok/auth.json");
}

QDateTime parseExpiry(const QString &text)
{
    QString stamp = text.trimmed();
    if (stamp.endsWith(QLatin1Char('Z'), Qt::CaseInsensitive))
        stamp.chop(1);
    const int dot = stamp.indexOf(QLatin1Char('.'));
    QString frac;
    if (dot >= 0) {
        frac = stamp.mid(dot + 1);
        stamp = stamp.left(dot);
        frac = frac.left(3);
        while (frac.size() < 3)
            frac += QLatin1Char('0');
    }
    QDateTime dt = QDateTime::fromString(stamp, QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
    if (!dt.isValid())
        return {};
    dt.setTimeZone(QTimeZone::UTC);
    if (!frac.isEmpty())
        dt = dt.addMSecs(frac.toInt());
    return dt;
}

QString expiryText(const QDateTime &dt)
{
    return dt.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz")) + QLatin1Char('Z');
}

QString missingSignIn()
{
    return QStringLiteral("Dictation needs a Grok sign-in or an xAI key. Sign in with Grok, or add a key in Settings.");
}

bool writeSession(const GrokCliSession &session, const QByteArray &original, QString *error)
{
    QJsonObject root = QJsonDocument::fromJson(original).object();
    QJsonObject obj = root.value(session.storageKey).toObject();
    obj.insert(QStringLiteral("key"), session.accessToken);
    if (session.hasExpiry)
        obj.insert(QStringLiteral("expires_at"), expiryText(session.expires));
    if (!session.refreshToken.isEmpty())
        obj.insert(QStringLiteral("refresh_token"), session.refreshToken);
    root.insert(session.storageKey, obj);
    QSaveFile file(authPath());
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = missingSignIn();
        return false;
    }
    const QJsonDocument doc(root);
    const bool pretty = original.contains('\n');
    file.write(pretty ? doc.toJson(QJsonDocument::Indented) : doc.toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (error)
            *error = missingSignIn();
        return false;
    }
    QFile::setPermissions(authPath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

// Returns false when the refresh itself failed. The caller decides whether the old token is still usable.
bool refreshSession(GrokCliSession *session, const QByteArray &original, QString *error)
{
    if (session->refreshToken.isEmpty() || session->clientId.isEmpty()) {
        if (error)
            *error = QStringLiteral("The Grok sign-in has expired. Run grok login, or add an xAI key in Settings.");
        return false;
    }
    QString base = session->issuer.trimmed().isEmpty() ? QStringLiteral("https://auth.x.ai") : session->issuer.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    form.addQueryItem(QStringLiteral("refresh_token"), session->refreshToken);
    form.addQueryItem(QStringLiteral("client_id"), session->clientId);
    QNetworkRequest request(QUrl(base + QStringLiteral("/oauth2/token")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    QNetworkAccessManager net;
    QNetworkReply *reply = net.post(request, form.query(QUrl::FullyEncoded).toUtf8());
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(8000);
    loop.exec();
    if (!timer.isActive()) {
        reply->abort();
        reply->deleteLater();
        if (error)
            *error = QStringLiteral("The Grok sign-in could not be refreshed. Run grok login, or add an xAI key in Settings.");
        return false;
    }
    const QByteArray body = reply->readAll();
    const bool httpOk = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    const QString access = obj.value(QStringLiteral("access_token")).toString().trimmed();
    if (!httpOk || access.isEmpty()) {
        const QString reason = obj.value(QStringLiteral("error")).toString();
        if (error)
            *error = reason.isEmpty()
                         ? QStringLiteral("The Grok sign-in could not be refreshed. Run grok login, or add an xAI key in Settings.")
                         : QStringLiteral("The Grok sign-in could not be refreshed (%1). Run grok login, or add an xAI key in Settings.").arg(reason);
        return false;
    }
    session->accessToken = access;
    const QString rotated = obj.value(QStringLiteral("refresh_token")).toString().trimmed();
    if (!rotated.isEmpty())
        session->refreshToken = rotated;
    const int seconds = obj.value(QStringLiteral("expires_in")).toInt(3600);
    session->expires = QDateTime::currentDateTimeUtc().addSecs(qMax(60, seconds));
    session->hasExpiry = true;
    writeSession(*session, original, nullptr);
    return true;
}

} // namespace

bool grokCliSessionFromAuth(const QByteArray &json, GrokCliSession *session)
{
    if (!session)
        return false;
    *session = {};
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (!it->isObject())
            continue;
        const QJsonObject obj = it->toObject();
        const QString key = obj.value(QStringLiteral("key")).toString().trimmed();
        if (key.isEmpty())
            continue;
        session->storageKey = it.key();
        session->accessToken = key;
        session->refreshToken = obj.value(QStringLiteral("refresh_token")).toString();
        session->clientId = obj.value(QStringLiteral("oidc_client_id")).toString();
        session->issuer = obj.value(QStringLiteral("oidc_issuer")).toString();
        const QString exp = obj.value(QStringLiteral("expires_at")).toString();
        if (!exp.isEmpty()) {
            session->expires = parseExpiry(exp);
            session->hasExpiry = session->expires.isValid();
        }
        return true;
    }
    return false;
}

QString grokBearerToken(const Config &config, QString *error)
{
    const QString key = config.grokApiKey().trimmed();
    if (!key.isEmpty())
        return key;

    QFile file(authPath());
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = missingSignIn();
        return {};
    }
    const QByteArray original = file.readAll();
    GrokCliSession session;
    if (!grokCliSessionFromAuth(original, &session)) {
        if (error)
            *error = missingSignIn();
        return {};
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const bool fresh = !session.hasExpiry || session.expires > now.addSecs(90);
    if (fresh)
        return session.accessToken;
    QString refreshError;
    if (refreshSession(&session, original, &refreshError))
        return session.accessToken;
    if (session.hasExpiry && session.expires > now)
        return session.accessToken;
    if (error)
        *error = refreshError.isEmpty() ? missingSignIn() : refreshError;
    return {};
}
