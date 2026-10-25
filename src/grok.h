#pragma once

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>

class QNetworkReply;

// xAI Grok over the OpenAI-compatible chat completions endpoint.
// The key comes from XAI_API_KEY or the local config; it is only placed in
// the Authorization header and never written to logs or the chat view.
class GrokClient : public QObject {
    Q_OBJECT
public:
    explicit GrokClient(QNetworkAccessManager *net, QObject *parent = nullptr);

    void chat(const QString &endpoint, const QString &model, const QString &apiKey, const QJsonArray &messages);
    void cancel();
    bool busy() const { return !reply_.isNull(); }

    static QJsonObject message(const QString &role, const QString &content);
    static QJsonObject buildRequest(const QString &model, const QJsonArray &messages);
    static QString parseReply(const QByteArray &body, QString *error);

signals:
    void replied(const QString &text);
    void failed(const QString &message);

private:
    QNetworkAccessManager *net_;
    QPointer<QNetworkReply> reply_;
};
