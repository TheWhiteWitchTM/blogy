#pragma once

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>

class QNetworkReply;
class QProcess;

// xAI Grok over the OpenAI-compatible chat completions endpoint.
// The key comes from XAI_API_KEY or the local config; it is only placed in
// the Authorization header and never written to logs or the chat view.
class GrokClient : public QObject {
    Q_OBJECT
public:
    explicit GrokClient(QNetworkAccessManager *net, QObject *parent = nullptr);

    struct Request {
        bool useCli = false;
        QString command;   // absolute path of the grok binary
        QString endpoint;
        QString model;
        QString apiKey;
        QJsonArray messages;
    };

    void chat(const Request &request);
    void cancel();
    bool busy() const { return !reply_.isNull() || proc_ != nullptr; }

    static QJsonObject message(const QString &role, const QString &content);
    static QJsonObject buildRequest(const QString &model, const QJsonArray &messages);
    static QString parseReply(const QByteArray &body, QString *error);
    // Headless CLI stream (streaming-messages-json). Empty when *error is set.
    static QString textFromCliStream(const QByteArray &ndjson, QString *error);

signals:
    void replied(const QString &text);
    void failed(const QString &message);
    // Full reply so far, as text arrives from the CLI.
    void draft(const QString &text);

private:
    void startCli(const Request &request);
    void startApi(const Request &request);

    void ingestCli(const QByteArray &chunk, bool flush);
    QString takeCliText(QString *error);

    QNetworkAccessManager *net_;
    QPointer<QNetworkReply> reply_;
    QProcess *proc_ = nullptr;
    QString promptPath_;
    bool cliGaveUp_ = false;
    bool cliReported_ = false;
    int cliGeneration_ = 0;
    QByteArray cliBuf_;
    QString cliText_;
    QString cliResult_;
    bool cliFailed_ = false;
    bool cliSawResult_ = false;
};
