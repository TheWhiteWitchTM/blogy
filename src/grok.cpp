#include "grok.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QUuid>

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

namespace {

// The CLI is a coding agent. With tools left on it spends the one turn
// searching the working directory and the post never arrives.
const char *kDisabledTools =
    "run_terminal_command,read_file,search_replace,list_dir,grep,"
    "kill_command_or_subagent,todo_write,get_command_or_subagent_output,"
    "scheduler_create,scheduler_delete,scheduler_list,monitor,search_tool,use_tool,"
    "workflow,enter_plan_mode,exit_plan_mode,ask_user_question,send_feedback,"
    "image_gen,image_edit,image_to_video,reference_to_video,write";

struct CliAccum {
    QString text;
    QString result;
    bool failed = false;
    bool sawResult = false;
};

void applyCliObject(const QJsonObject &obj, CliAccum *acc)
{
    const QString type = obj.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("stream_event")) {
        const QJsonObject event = obj.value(QStringLiteral("event")).toObject();
        if (event.value(QStringLiteral("type")).toString() != QLatin1String("content_block_delta"))
            return;
        const QJsonObject delta = event.value(QStringLiteral("delta")).toObject();
        if (delta.value(QStringLiteral("type")).toString() != QLatin1String("text_delta"))
            return;
        const QString piece = delta.value(QStringLiteral("text")).toString();
        if (piece.isEmpty())
            return;
        acc->text += piece;
        return;
    }
    if (type != QLatin1String("result"))
        return;
    acc->sawResult = true;
    acc->result = obj.value(QStringLiteral("result")).toString();
    const QString subtype = obj.value(QStringLiteral("subtype")).toString();
    const bool bad = obj.value(QStringLiteral("is_error")).toBool()
                     || (!subtype.isEmpty() && subtype != QLatin1String("success"));
    if (!bad)
        return;
    acc->failed = true;
    if (!acc->result.isEmpty())
        return;
    const QJsonValue err = obj.value(QStringLiteral("error"));
    acc->result = err.isObject() ? err.toObject().value(QStringLiteral("message")).toString() : err.toString();
    if (acc->result.isEmpty())
        acc->result = QStringLiteral("Grok stopped (%1).").arg(subtype.isEmpty() ? QStringLiteral("error") : subtype);
}

QString finishCliAccum(const CliAccum &acc, QString *error)
{
    if (acc.failed) {
        if (error)
            *error = acc.result.isEmpty() ? QStringLiteral("Grok returned no text.") : acc.result;
        return {};
    }
    if (!acc.result.isEmpty())
        return acc.result;
    if (!acc.text.isEmpty())
        return acc.text;
    if (error)
        *error = QStringLiteral("Grok returned no text.");
    return {};
}

} // namespace

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

QString GrokClient::textFromCliStream(const QByteArray &ndjson, QString *error)
{
    CliAccum acc;
    QByteArray pending = ndjson;
    if (!pending.endsWith('\n'))
        pending.append('\n');
    while (true) {
        const int nl = pending.indexOf('\n');
        if (nl < 0)
            break;
        const QByteArray line = pending.left(nl).trimmed();
        pending = pending.mid(nl + 1);
        if (line.isEmpty())
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject())
            continue;
        applyCliObject(doc.object(), &acc);
    }
    return finishCliAccum(acc, error);
}

void GrokClient::ingestCli(const QByteArray &chunk, bool flush)
{
    cliBuf_ += chunk;
    if (flush && !cliBuf_.isEmpty() && !cliBuf_.endsWith('\n'))
        cliBuf_.append('\n');
    while (true) {
        const int nl = cliBuf_.indexOf('\n');
        if (nl < 0)
            break;
        const QByteArray line = cliBuf_.left(nl).trimmed();
        cliBuf_ = cliBuf_.mid(nl + 1);
        if (line.isEmpty())
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject())
            continue;
        CliAccum acc;
        acc.text = cliText_;
        acc.result = cliResult_;
        acc.failed = cliFailed_;
        acc.sawResult = cliSawResult_;
        applyCliObject(doc.object(), &acc);
        const bool grew = acc.text != cliText_;
        cliText_ = acc.text;
        cliResult_ = acc.result;
        cliFailed_ = acc.failed;
        cliSawResult_ = acc.sawResult;
        if (grew)
            emit draft(cliText_);
    }
}

QString GrokClient::takeCliText(QString *error)
{
    ingestCli({}, true);
    CliAccum acc;
    acc.text = cliText_;
    acc.result = cliResult_;
    acc.failed = cliFailed_;
    acc.sawResult = cliSawResult_;
    return finishCliAccum(acc, error);
}

void GrokClient::chat(const Request &request)
{
    cancel();
    if (request.useCli)
        startCli(request);
    else
        startApi(request);
}

void GrokClient::startCli(const Request &request)
{
    if (request.command.isEmpty()) {
        emit failed(QStringLiteral("Grok CLI is not installed. Install Grok, then run grok login."));
        return;
    }
    QString system;
    QString prompt;
    for (const QJsonValue &value : request.messages) {
        const QJsonObject message = value.toObject();
        const QString role = message.value(QStringLiteral("role")).toString();
        const QString content = message.value(QStringLiteral("content")).toString();
        if (role == QLatin1String("system")) {
            if (!system.isEmpty())
                system += QLatin1Char('\n');
            system += content;
            continue;
        }
        prompt += (role == QLatin1String("assistant") ? QStringLiteral("Assistant:\n") : QStringLiteral("User:\n"));
        prompt += content;
        prompt += QStringLiteral("\n\n");
    }
    promptPath_ = QDir::tempPath() + QStringLiteral("/blogy-grok-")
                  + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".txt");
    QFile promptFile(promptPath_);
    if (!promptFile.open(QIODevice::WriteOnly)) {
        emit failed(QStringLiteral("Could not write a prompt for the Grok CLI."));
        return;
    }
    promptFile.write(prompt.toUtf8());
    promptFile.close();
    const QString promptPath = promptPath_;

    cliGaveUp_ = false;
    cliReported_ = false;
    const int generation = ++cliGeneration_;
    cliBuf_.clear();
    cliText_.clear();
    cliResult_.clear();
    cliFailed_ = false;
    cliSawResult_ = false;

    proc_ = new QProcess(this);
    proc_->setProcessChannelMode(QProcess::SeparateChannels);
    proc_->setWorkingDirectory(QDir::tempPath());
    QProcess *proc = proc_;
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc, generation] {
        if (proc_ != proc || generation != cliGeneration_)
            return;
        ingestCli(proc->readAllStandardOutput(), false);
    });
    connect(proc, &QProcess::finished, this, [this, proc, promptPath, generation](int code, QProcess::ExitStatus status) {
        if (generation != cliGeneration_) {
            QFile::remove(promptPath);
            proc->deleteLater();
            return;
        }
        ingestCli(proc->readAllStandardOutput(), true);
        const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        QFile::remove(promptPath);
        if (proc_ == proc)
            proc_ = nullptr;
        proc->deleteLater();
        if (cliReported_)
            return;
        cliReported_ = true;
        if (cliGaveUp_) {
            cliGaveUp_ = false;
            emit failed(QStringLiteral("Grok took too long and was stopped."));
            return;
        }
        QString error;
        const QString text = takeCliText(&error);
        if (!text.isEmpty() && error.isEmpty()) {
            emit replied(text);
            return;
        }
        QString message = error;
        if ((message.isEmpty() || message == QLatin1String("Grok returned no text.")) && !err.isEmpty())
            message = err;
        if (message.isEmpty())
            message = (status != QProcess::NormalExit || code != 0)
                          ? QStringLiteral("Grok CLI exited with code %1.").arg(code)
                          : QStringLiteral("Grok returned no text.");
        if (message.size() > 500)
            message = message.left(500);
        emit failed(message);
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc, generation](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || proc_ != proc || generation != cliGeneration_ || cliReported_)
            return;
        cliReported_ = true;
        proc_ = nullptr;
        proc->deleteLater();
        emit failed(QStringLiteral("The Grok CLI could not be started."));
    });
    QTimer::singleShot(180000, this, [this, proc, generation] {
        if (generation != cliGeneration_ || proc_ != proc || proc->state() == QProcess::NotRunning)
            return;
        cliGaveUp_ = true;
        proc->kill();
    });

    QStringList args = {QStringLiteral("--prompt-file"), promptPath,
                        QStringLiteral("--verbatim"),
                        QStringLiteral("--output-format"), QStringLiteral("streaming-messages-json"),
                        QStringLiteral("--include-partial-messages"),
                        QStringLiteral("--max-turns"), QStringLiteral("1"),
                        QStringLiteral("--no-subagents"),
                        QStringLiteral("--disable-web-search"),
                        QStringLiteral("--disallowed-tools"), QString::fromLatin1(kDisabledTools),
                        QStringLiteral("--permission-mode"), QStringLiteral("dontAsk"),
                        QStringLiteral("--reasoning-effort"), QStringLiteral("low"),
                        QStringLiteral("--cwd"), QDir::tempPath()};
    if (!request.model.isEmpty())
        args << QStringLiteral("-m") << request.model;
    if (!system.trimmed().isEmpty())
        args << QStringLiteral("--system-prompt-override") << system;
    proc_->start(request.command, args);
}

void GrokClient::startApi(const Request &request)
{
    if (request.apiKey.isEmpty()) {
        emit failed(QStringLiteral("Grok is not signed in. Run grok login."));
        return;
    }
    QNetworkRequest req{QUrl(request.endpoint)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Authorization", "Bearer " + request.apiKey.toUtf8());
    req.setTransferTimeout(180000);
    reply_ = net_->post(req, QJsonDocument(buildRequest(request.model, request.messages)).toJson(QJsonDocument::Compact));
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
    if (proc_) {
        ++cliGeneration_;
        QProcess *proc = proc_;
        proc_ = nullptr;
        proc->disconnect(this);
        proc->kill();
        proc->deleteLater();
        QFile::remove(promptPath_);
    }
}
