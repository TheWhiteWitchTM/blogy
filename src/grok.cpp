#include "grok.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

GrokClient::GrokClient(QNetworkAccessManager *net, QObject *parent)
    : QObject(parent), net_(net)
{
}

QJsonObject GrokClient::message(const QString &role, const QString &content)
{
    return QJsonObject{{QStringLiteral("role"), role}, {QStringLiteral("content"), content}};
}

QJsonObject GrokClient::buildRequest(const QString &model, const QJsonArray &messages)
{
    return QJsonObject{{QStringLiteral("model"), model},
                       {QStringLiteral("messages"), messages},
                       {QStringLiteral("stream"), false},
                       {QStringLiteral("temperature"), 0.7}};
}

QString GrokClient::parseReply(const QByteArray &body, QString *error)
{
    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    if (obj.contains(QStringLiteral("error"))) {
        const QJsonValue e = obj.value(QStringLiteral("error"));
        if (error)
            *error = e.isObject() ? e.toObject().value(QStringLiteral("message")).toString() : e.toString();
        return {};
    }
    const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        if (error)
            *error = QStringLiteral("No reply in the response.");
        return {};
    }
    return choices.first().toObject().value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
}

void GrokClient::chat(const QString &endpoint, const QString &model, const QString &apiKey, const QJsonArray &messages)
{
    if (apiKey.isEmpty()) {
        emit failed(QStringLiteral("No xAI API key. Set XAI_API_KEY or add it in Settings."));
        return;
    }
    cancel();
    QNetworkRequest req{QUrl(endpoint)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
    req.setTransferTimeout(180000);
    reply_ = net_->post(req, QJsonDocument(buildRequest(model, messages)).toJson(QJsonDocument::Compact));
    QNetworkReply *reply = reply_;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply_ == reply)
            reply_.clear();
        if (reply->error() == QNetworkReply::OperationCanceledError)
            return;
        const QByteArray body = reply->readAll();
        QString error;
        const QString text = parseReply(body, &error);
        if (reply->error() != QNetworkReply::NoError && text.isEmpty()) {
            emit failed(error.isEmpty() ? reply->errorString() : error);
            return;
        }
        if (text.isEmpty()) {
            emit failed(error.isEmpty() ? QStringLiteral("Empty reply.") : error);
            return;
        }
        emit replied(text);
    });
}

void GrokClient::cancel()
{
    if (reply_) {
        QNetworkReply *r = reply_;
        reply_.clear();
        r->abort();
    }
}
