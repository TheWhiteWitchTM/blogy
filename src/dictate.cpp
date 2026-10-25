#include "dictate.h"

#include "editor.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QUrlQuery>
#include <QWebSocket>

namespace {

bool ieq(const QString &text, const char *word)
{
    return text.compare(QLatin1String(word), Qt::CaseInsensitive) == 0;
}

bool nextIs(const QStringList &toks, int i, std::initializer_list<const char *> words)
{
    int k = 0;
    for (const char *word : words) {
        if (i + k >= toks.size() || !ieq(toks.at(i + k), word))
            return false;
        ++k;
    }
    return true;
}

QChar lastChar(const QString &out, const QString &before)
{
    if (!out.isEmpty())
        return out.back();
    if (!before.isEmpty())
        return before.back();
    return {};
}

bool sticks(QChar c)
{
    return c.isNull() || c.isSpace() || c == QLatin1Char('(') || c == QLatin1Char('[') || c == QLatin1Char('{')
           || c == QLatin1Char('"') || c == QChar(0x201C) || c == QLatin1Char('`') || c == QLatin1Char('\'')
           || c == QLatin1Char('-') || c == QLatin1Char('/') || c == QLatin1Char('*') || c == QLatin1Char('~')
           || c == QLatin1Char('>');
}

void breakLines(QString *out, const QString &before, int n)
{
    if (n <= 0)
        return;
    while (out->endsWith(QLatin1Char(' ')))
        out->chop(1);
    int have = 0;
    if (!out->isEmpty()) {
        while (have < out->size() && out->at(out->size() - 1 - have) == QLatin1Char('\n'))
            ++have;
    } else if (before.isEmpty()) {
        return;
    } else {
        for (int i = before.size() - 1; i >= 0 && before.at(i) == QLatin1Char('\n'); --i)
            ++have;
    }
    while (have < n) {
        out->append(QLatin1Char('\n'));
        ++have;
    }
}

void pad(QString *out, const QString &before)
{
    if (!sticks(lastChar(*out, before)))
        out->append(QLatin1Char(' '));
}

void attach(QString *out, const QString &mark, DictateCursor *cursor)
{
    while (out->endsWith(QLatin1Char(' ')))
        out->chop(1);
    out->append(mark);
    if (mark == QLatin1String(".") || mark == QLatin1String("?") || mark == QLatin1String("!")
        || mark == QString(QChar(0x2026)))
        cursor->cap = true;
}

int headingLevel(const QString &token)
{
    if (ieq(token, "one") || token == QLatin1String("1"))
        return 1;
    if (ieq(token, "two") || token == QLatin1String("2"))
        return 2;
    if (ieq(token, "three") || token == QLatin1String("3"))
        return 3;
    if (ieq(token, "four") || token == QLatin1String("4"))
        return 4;
    if (ieq(token, "five") || token == QLatin1String("5"))
        return 5;
    if (ieq(token, "six") || token == QLatin1String("6"))
        return 6;
    return 0;
}

bool languageTag(const QString &token, QString *tag)
{
    const QString s = token.toLower();
    if (s == QLatin1String("plain") || s == QLatin1String("text")) {
        *tag = {};
        return true;
    }
    static const QSet<QString> known = {QStringLiteral("python"), QStringLiteral("py"),
                                        QStringLiteral("javascript"), QStringLiteral("js"),
                                        QStringLiteral("typescript"), QStringLiteral("ts"),
                                        QStringLiteral("jsx"), QStringLiteral("tsx"),
                                        QStringLiteral("bash"), QStringLiteral("sh"),
                                        QStringLiteral("html"), QStringLiteral("css"),
                                        QStringLiteral("json"), QStringLiteral("yaml"),
                                        QStringLiteral("yml"), QStringLiteral("cpp"),
                                        QStringLiteral("c"), QStringLiteral("rust"),
                                        QStringLiteral("go"), QStringLiteral("java"),
                                        QStringLiteral("ruby"), QStringLiteral("sql"),
                                        QStringLiteral("php"), QStringLiteral("kotlin"),
                                        QStringLiteral("swift"), QStringLiteral("markdown"),
                                        QStringLiteral("md"), QStringLiteral("xml"),
                                        QStringLiteral("toml"), QStringLiteral("lua"),
                                        QStringLiteral("csharp")};
    if (!known.contains(s))
        return false;
    if (s == QLatin1String("py"))
        *tag = QStringLiteral("python");
    else if (s == QLatin1String("js"))
        *tag = QStringLiteral("javascript");
    else if (s == QLatin1String("ts"))
        *tag = QStringLiteral("typescript");
    else if (s == QLatin1String("sh"))
        *tag = QStringLiteral("bash");
    else if (s == QLatin1String("yml"))
        *tag = QStringLiteral("yaml");
    else if (s == QLatin1String("md"))
        *tag = QStringLiteral("markdown");
    else
        *tag = s;
    return true;
}

QString wrapMark(int wrap)
{
    switch (wrap) {
    case 1: return QStringLiteral("**");
    case 2: return QStringLiteral("*");
    case 3: return QStringLiteral("~~");
    case 4: return QStringLiteral("`");
    default: return {};
    }
}

void closeWrap(QString *out, DictateCursor *cursor)
{
    if (!cursor->wrap)
        return;
    out->append(wrapMark(cursor->wrap));
    cursor->wrap = 0;
}

void openWrap(QString *out, const QString &before, DictateCursor *cursor, int wrap)
{
    if (cursor->wrap == wrap)
        return;
    closeWrap(out, cursor);
    pad(out, before);
    out->append(wrapMark(wrap));
    cursor->wrap = wrap;
    if (wrap == 4)
        cursor->cap = false;
}

int nextNumber(const QString &before, const QString &out)
{
    QString all = before + out;
    while (all.endsWith(QLatin1Char('\n')) || all.endsWith(QLatin1Char(' ')))
        all.chop(1);
    const int nl = all.lastIndexOf(QLatin1Char('\n'));
    const QString last = all.mid(nl + 1);
    static const QRegularExpression re(QStringLiteral("^\\s*(\\d+)\\.\\s"));
    const auto match = re.match(last);
    return match.hasMatch() ? match.captured(1).toInt() + 1 : 1;
}

void emitWord(QString *out, const QString &before, DictateCursor *cursor, QString word, int how)
{
    if (word.isEmpty())
        return;
    const bool literal = how == 3 || cursor->fence || cursor->wrap == 4;
    if (!literal) {
        if (how == 1)
            word = word.toUpper();
        else if (how != 2 && cursor->cap && word.at(0).isLetter())
            word[0] = word.at(0).toUpper();
        cursor->cap = false;
    }
    if (!sticks(lastChar(*out, before)))
        out->append(QLatin1Char(' '));
    out->append(word);
}

QString transcriptText(const QJsonObject &obj)
{
    if (obj.contains(QStringLiteral("text")))
        return obj.value(QStringLiteral("text")).toString();
    if (obj.contains(QStringLiteral("transcript")))
        return obj.value(QStringLiteral("transcript")).toString();
    const QJsonArray alts = obj.value(QStringLiteral("channel")).toObject().value(QStringLiteral("alternatives")).toArray();
    if (!alts.isEmpty()) {
        const QString nested = alts.at(0).toObject().value(QStringLiteral("transcript")).toString();
        if (!nested.isEmpty())
            return nested;
    }
    QStringList words;
    const QJsonArray list = obj.value(QStringLiteral("words")).toArray();
    for (const QJsonValue &value : list) {
        const QString word = value.toObject().value(QStringLiteral("text")).toString();
        if (!word.isEmpty())
            words.append(word);
    }
    return words.join(QLatin1Char(' '));
}

} // namespace

static bool isFinishPhrase(const QString &spoken)
{
    QString text = spoken.simplified().toLower();
    while (!text.isEmpty()) {
        const QChar last = text.back();
        if (last == QLatin1Char('.') || last == QLatin1Char('!') || last == QLatin1Char('?') || last == QLatin1Char(',')
            || last == QChar(0x2026))
            text.chop(1);
        else
            break;
    }
    text = text.trimmed();
    return text == QLatin1String("finish the post") || text == QLatin1String("finish this post")
           || text == QLatin1String("finish the outline") || text == QLatin1String("complete the post")
           || text == QLatin1String("complete this post") || text == QLatin1String("complete the outline")
           || text == QLatin1String("write the post") || text == QLatin1String("write this post");
}

QStringList dictateKeyterms()
{
    return {QStringLiteral("heading one"), QStringLiteral("heading two"), QStringLiteral("heading three"),
            QStringLiteral("heading four"), QStringLiteral("heading five"), QStringLiteral("heading six"),
            QStringLiteral("new paragraph"), QStringLiteral("new line"), QStringLiteral("line break"),
            QStringLiteral("bullet"), QStringLiteral("bulleted list"), QStringLiteral("numbered list"),
            QStringLiteral("task list"), QStringLiteral("block quote"), QStringLiteral("horizontal rule"),
            QStringLiteral("code block"), QStringLiteral("end code"), QStringLiteral("inline code"),
            QStringLiteral("end bold"), QStringLiteral("end italic"), QStringLiteral("end strike"),
            QStringLiteral("scratch that"), QStringLiteral("question mark"), QStringLiteral("open quote"),
            QStringLiteral("close quote"), QStringLiteral("open paren"), QStringLiteral("close paren"),
            QStringLiteral("finish the post"), QStringLiteral("write the post"),
            QStringLiteral("complete the post"), QStringLiteral("finish the outline")};
}

DictatePiece dictateMarkdown(const QString &spoken, const QString &before, DictateCursor *cursor)
{
    DictateCursor local;
    if (!cursor)
        cursor = &local;
    DictatePiece piece;
    const QString trimmed = spoken.trimmed();
    if (trimmed.isEmpty())
        return piece;
    const DictateCursor saved = *cursor;
    if (!cursor->fence && cursor->wrap != 4 && isFinishPhrase(trimmed)) {
        piece.finish = true;
        return piece;
    }
    const QStringList toks = trimmed.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    QString out;
    int force = 0;
    for (int i = 0; i < toks.size();) {
        const bool code = cursor->fence || cursor->wrap == 4;
        if (nextIs(toks, i, {"scratch", "that"})) {
            if (out.isEmpty()) {
                piece.scratch = true;
                *cursor = saved;
                return piece;
            }
            out.clear();
            *cursor = saved;
            force = 0;
            i += 2;
            continue;
        }
        if (nextIs(toks, i, {"end", "code", "block"}) || nextIs(toks, i, {"end", "code"})) {
            const int taken = nextIs(toks, i, {"end", "code", "block"}) ? 3 : 2;
            if (cursor->fence) {
                if (!out.isEmpty() && !out.endsWith(QLatin1Char('\n')))
                    out.append(QLatin1Char('\n'));
                else if (out.isEmpty() && !before.isEmpty() && !before.endsWith(QLatin1Char('\n')))
                    out.append(QLatin1Char('\n'));
                out.append(QStringLiteral("```\n"));
                cursor->fence = false;
                cursor->cap = true;
            } else if (cursor->wrap == 4) {
                out.append(QLatin1Char('`'));
                cursor->wrap = 0;
            }
            i += taken;
            continue;
        }
        if (nextIs(toks, i, {"new", "paragraph"})) {
            breakLines(&out, before, 2);
            if (!cursor->fence)
                cursor->cap = true;
            i += 2;
            continue;
        }
        if (nextIs(toks, i, {"new", "line"}) || nextIs(toks, i, {"line", "break"})) {
            breakLines(&out, before, 1);
            i += 2;
            continue;
        }
        if (nextIs(toks, i, {"full", "stop"})) {
            attach(&out, QStringLiteral("."), cursor);
            i += 2;
            continue;
        }
        if (nextIs(toks, i, {"question", "mark"})) {
            attach(&out, QStringLiteral("?"), cursor);
            i += 2;
            continue;
        }
        if (nextIs(toks, i, {"exclamation", "point"}) || nextIs(toks, i, {"exclamation", "mark"})) {
            attach(&out, QStringLiteral("!"), cursor);
            i += 2;
            continue;
        }
        if (nextIs(toks, i, {"open", "quote"})) {
            pad(&out, before);
            out.append(QChar(0x201C));
            cursor->quote = true;
            i += 2;
            continue;
        }
        if (nextIs(toks, i, {"close", "quote"}) || nextIs(toks, i, {"end", "quote"})) {
            attach(&out, QString(QChar(0x201D)), cursor);
            cursor->quote = false;
            cursor->cap = false;
            i += 2;
            continue;
        }
        if (nextIs(toks, i, {"open", "paren"}) || nextIs(toks, i, {"open", "parenthesis"})) {
            pad(&out, before);
            out.append(QLatin1Char('('));
            i += 2;
            continue;
        }
        if (nextIs(toks, i, {"close", "paren"}) || nextIs(toks, i, {"close", "parenthesis"})) {
            attach(&out, QStringLiteral(")"), cursor);
            cursor->cap = false;
            i += 2;
            continue;
        }
        if (nextIs(toks, i, {"open", "bracket"})) {
            pad(&out, before);
            out.append(QLatin1Char('['));
            i += 2;
            continue;
        }
        if (nextIs(toks, i, {"close", "bracket"})) {
            attach(&out, QStringLiteral("]"), cursor);
            cursor->cap = false;
            i += 2;
            continue;
        }
        if (nextIs(toks, i, {"open", "brace"})) {
            pad(&out, before);
            out.append(QLatin1Char('{'));
            i += 2;
            continue;
        }
        if (nextIs(toks, i, {"close", "brace"})) {
            attach(&out, QStringLiteral("}"), cursor);
            cursor->cap = false;
            i += 2;
            continue;
        }
        if (nextIs(toks, i, {"em", "dash"})) {
            attach(&out, QString(QChar(0x2014)), cursor);
            cursor->cap = false;
            i += 2;
            continue;
        }
        if (nextIs(toks, i, {"ellipsis"})) {
            attach(&out, QString(QChar(0x2026)), cursor);
            i += 1;
            continue;
        }
        if (!code && nextIs(toks, i, {"code", "block"})) {
            QString lang;
            int taken = 2;
            if (nextIs(toks, i + 2, {"c", "plus", "plus"})) {
                lang = QStringLiteral("cpp");
                taken = 5;
            } else if (nextIs(toks, i + 2, {"c", "sharp"})) {
                lang = QStringLiteral("csharp");
                taken = 4;
            } else if (i + 2 < toks.size() && languageTag(toks.at(i + 2), &lang)) {
                taken = 3;
            }
            closeWrap(&out, cursor);
            breakLines(&out, before, 2);
            out.append(QStringLiteral("```"));
            if (!lang.isEmpty())
                out.append(lang);
            out.append(QLatin1Char('\n'));
            cursor->fence = true;
            cursor->cap = false;
            i += taken;
            continue;
        }
        if (!code && nextIs(toks, i, {"inline", "code"})) {
            openWrap(&out, before, cursor, 4);
            i += 2;
            continue;
        }
        if (!code && nextIs(toks, i, {"end", "bold"})) {
            if (cursor->wrap == 1)
                closeWrap(&out, cursor);
            i += 2;
            continue;
        }
        if (!code && nextIs(toks, i, {"end", "italic"})) {
            if (cursor->wrap == 2)
                closeWrap(&out, cursor);
            i += 2;
            continue;
        }
        if (!code && (nextIs(toks, i, {"end", "strike"}) || nextIs(toks, i, {"end", "strikethrough"}))) {
            if (cursor->wrap == 3)
                closeWrap(&out, cursor);
            i += 2;
            continue;
        }
        if (!code && nextIs(toks, i, {"horizontal", "rule"})) {
            closeWrap(&out, cursor);
            breakLines(&out, before, 2);
            out.append(QStringLiteral("---\n\n"));
            cursor->cap = true;
            i += 2;
            continue;
        }
        if (!code && (nextIs(toks, i, {"horizontal", "line"}) || ieq(toks.at(i), "divider"))) {
            closeWrap(&out, cursor);
            breakLines(&out, before, 2);
            out.append(QStringLiteral("---\n\n"));
            cursor->cap = true;
            i += ieq(toks.at(i), "divider") ? 1 : 2;
            continue;
        }
        if (!code && (nextIs(toks, i, {"bulleted", "list"}) || ieq(toks.at(i), "bullet"))) {
            closeWrap(&out, cursor);
            breakLines(&out, before, 1);
            out.append(QStringLiteral("- "));
            cursor->cap = true;
            i += ieq(toks.at(i), "bullet") ? 1 : 2;
            continue;
        }
        if (!code && (nextIs(toks, i, {"numbered", "list"}) || nextIs(toks, i, {"number", "list"}))) {
            const int n = nextNumber(before, out);
            closeWrap(&out, cursor);
            breakLines(&out, before, 1);
            out.append(QString::number(n) + QStringLiteral(". "));
            cursor->cap = true;
            i += 2;
            continue;
        }
        if (!code && (nextIs(toks, i, {"task", "list"}) || nextIs(toks, i, {"check", "list"}) || ieq(toks.at(i), "checkbox"))) {
            closeWrap(&out, cursor);
            breakLines(&out, before, 1);
            out.append(QStringLiteral("- [ ] "));
            cursor->cap = true;
            i += ieq(toks.at(i), "checkbox") ? 1 : 2;
            continue;
        }
        if (!code && (nextIs(toks, i, {"block", "quote"}) || ieq(toks.at(i), "blockquote"))) {
            closeWrap(&out, cursor);
            breakLines(&out, before, 1);
            out.append(QStringLiteral("> "));
            cursor->cap = true;
            i += ieq(toks.at(i), "blockquote") ? 1 : 2;
            continue;
        }
        if (!code && ieq(toks.at(i), "heading")) {
            int level = 2;
            int taken = 1;
            if (i + 1 < toks.size()) {
                const int found = headingLevel(toks.at(i + 1));
                if (found) {
                    level = found;
                    taken = 2;
                }
            }
            closeWrap(&out, cursor);
            breakLines(&out, before, 2);
            out.append(QString(level, QLatin1Char('#')) + QLatin1Char(' '));
            cursor->cap = true;
            i += taken;
            continue;
        }
        if (!code && ieq(toks.at(i), "bold")) {
            openWrap(&out, before, cursor, 1);
            ++i;
            continue;
        }
        if (!code && ieq(toks.at(i), "italic")) {
            openWrap(&out, before, cursor, 2);
            ++i;
            continue;
        }
        if (!code && (ieq(toks.at(i), "strike") || ieq(toks.at(i), "strikethrough"))) {
            openWrap(&out, before, cursor, 3);
            ++i;
            continue;
        }
        if (!code && nextIs(toks, i, {"all", "caps"})) {
            force = 1;
            i += 2;
            continue;
        }
        if (!code && nextIs(toks, i, {"no", "caps"})) {
            force = 2;
            i += 2;
            continue;
        }
        if (!code && ieq(toks.at(i), "cap")) {
            cursor->cap = true;
            ++i;
            continue;
        }
        const QString token = toks.at(i);
        if (ieq(token, "period")) {
            attach(&out, QStringLiteral("."), cursor);
        } else if (ieq(token, "comma")) {
            attach(&out, QStringLiteral(","), cursor);
            cursor->cap = false;
        } else if (ieq(token, "colon")) {
            attach(&out, QStringLiteral(":"), cursor);
            cursor->cap = false;
        } else if (ieq(token, "semicolon")) {
            attach(&out, QStringLiteral(";"), cursor);
            cursor->cap = false;
        } else if (ieq(token, "dash")) {
            attach(&out, QString(QChar(0x2014)), cursor);
            cursor->cap = false;
        } else if (ieq(token, "hyphen")) {
            attach(&out, QStringLiteral("-"), cursor);
            cursor->cap = false;
        } else if (ieq(token, "apostrophe")) {
            attach(&out, QStringLiteral("'"), cursor);
            cursor->cap = false;
        } else if (ieq(token, "slash")) {
            attach(&out, QStringLiteral("/"), cursor);
            cursor->cap = false;
        } else if (ieq(token, "asterisk")) {
            attach(&out, QStringLiteral("*"), cursor);
            cursor->cap = false;
        } else if (ieq(token, "backtick")) {
            attach(&out, QStringLiteral("`"), cursor);
            cursor->cap = false;
        } else if (ieq(token, "exclamation")) {
            attach(&out, QStringLiteral("!"), cursor);
        } else if (ieq(token, "quote")) {
            if (cursor->quote) {
                attach(&out, QString(QChar(0x201D)), cursor);
                cursor->quote = false;
                cursor->cap = false;
            } else {
                pad(&out, before);
                out.append(QChar(0x201C));
                cursor->quote = true;
            }
        } else {
            const int how = code ? 3 : force;
            emitWord(&out, before, cursor, token, how);
            force = 0;
        }
        ++i;
    }
    piece.text = out;
    return piece;
}

DictateController::DictateController(MarkdownEdit *editor, QObject *parent)
    : QObject(parent), editor_(editor)
{
}

void DictateController::fail(const QString &message)
{
    if (stopping_)
        return;
    const bool was = listening_;
    stop();
    if (was)
        emit failed(message.isEmpty() ? QStringLiteral("Dictation stopped.") : message);
}

void DictateController::start(const QString &apiKey)
{
    if (listening_)
        return;
    apiKey_ = apiKey.trimmed();
    if (apiKey_.isEmpty()) {
        emit failed(QStringLiteral("Dictation needs a Grok sign-in or an xAI key. Sign in with Grok, or add a key in Settings."));
        emit listeningChanged(false);
        return;
    }
    QString bin = QStandardPaths::findExecutable(QStringLiteral("pw-record"));
    QStringList args;
    if (!bin.isEmpty()) {
        args = {QStringLiteral("--rate"), QStringLiteral("16000"), QStringLiteral("--channels"), QStringLiteral("1"),
                QStringLiteral("--format"), QStringLiteral("s16"), QStringLiteral("--channel-map"), QStringLiteral("mono"),
                QStringLiteral("--raw"), QStringLiteral("--latency"), QStringLiteral("100ms"), QStringLiteral("-")};
    } else {
        bin = QStandardPaths::findExecutable(QStringLiteral("parecord"));
        args = {QStringLiteral("--rate=16000"), QStringLiteral("--channels=1"), QStringLiteral("--format=s16le"),
                QStringLiteral("--raw")};
    }
    if (bin.isEmpty()) {
        emit failed(QStringLiteral("Dictation needs pw-record or parecord, and neither is installed."));
        emit listeningChanged(false);
        return;
    }

    stopping_ = false;
    ready_ = false;
    locked_.clear();
    pending_.clear();
    editor_->beginDictation();

    if (!socket_) {
        socket_ = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
        connect(socket_, &QWebSocket::textMessageReceived, this, &DictateController::onMessage);
        connect(socket_, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) { fail(socket_->errorString()); });
    }

    QUrl url(QStringLiteral("wss://api.x.ai/v1/stt"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("sample_rate"), QStringLiteral("16000"));
    query.addQueryItem(QStringLiteral("encoding"), QStringLiteral("pcm"));
    query.addQueryItem(QStringLiteral("interim_results"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("language"), QStringLiteral("en"));
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("endpointing"), QStringLiteral("800"));
    QStringList terms = dictateKeyterms();
    for (const QString &hint : editor_->dictationHints()) {
        if (terms.size() >= 80)
            break;
        if (!hint.trimmed().isEmpty() && hint.size() <= 50 && !terms.contains(hint, Qt::CaseInsensitive))
            terms.append(hint.trimmed());
    }
    for (const QString &term : terms)
        query.addQueryItem(QStringLiteral("keyterm"), term);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + apiKey_.toUtf8());
    socket_->open(request);

    if (audio_) {
        audio_->deleteLater();
        audio_ = nullptr;
    }
    audio_ = new QProcess(this);
    audio_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(audio_, &QProcess::readyReadStandardOutput, this, &DictateController::sendAudio);
    connect(audio_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        fail(QStringLiteral("The microphone did not start."));
    });
    connect(audio_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int, QProcess::ExitStatus) {
        if (stopping_ || !listening_)
            return;
        const QString err = QString::fromUtf8(audio_->readAllStandardError()).trimmed();
        fail(err.isEmpty() ? QStringLiteral("The microphone stopped.") : err);
    });
    audio_->start(bin, args);

    listening_ = true;
    emit listeningChanged(true);
    emit status(QStringLiteral("Listening. Click Dictate again to stop. Say “finish the post” to write the full post from this outline."));
}

void DictateController::stop()
{
    if (!listening_ && !socket_ && !audio_)
        return;
    stopping_ = true;
    const bool was = listening_;
    listening_ = false;
    ready_ = false;
    if (audio_) {
        audio_->kill();
        audio_->waitForFinished(300);
        audio_->deleteLater();
        audio_ = nullptr;
    }
    pending_.clear();
    if (socket_ && socket_->state() == QAbstractSocket::ConnectedState) {
        socket_->sendTextMessage(QStringLiteral("{\"type\":\"audio.done\"}"));
        socket_->close();
    }
    if (editor_)
        editor_->endDictation();
    if (was)
        emit listeningChanged(false);
}

void DictateController::sendAudio()
{
    if (!audio_)
        return;
    pending_.append(audio_->readAllStandardOutput());
    if (!ready_) {
        const int cap = 16000 * 2;
        if (pending_.size() > cap)
            pending_.remove(0, pending_.size() - cap);
        return;
    }
    flushAudio();
}

void DictateController::flushAudio()
{
    if (!socket_ || socket_->state() != QAbstractSocket::ConnectedState)
        return;
    constexpr int frame = 3200;
    while (pending_.size() >= frame) {
        socket_->sendBinaryMessage(pending_.left(frame));
        pending_.remove(0, frame);
    }
}

QString DictateController::delta(const QString &full) const
{
    const QString text = full.trimmed();
    if (!locked_.isEmpty() && text.startsWith(locked_))
        return text.mid(locked_.size()).trimmed();
    return text;
}

void DictateController::commitFinal(const QString &full, bool speechFinal)
{
    const QString piece = delta(full);
    editor_->clearDictationPreview();
    if (!piece.isEmpty())
        editor_->commitDictation(piece);
    locked_ = speechFinal ? QString() : full.trimmed();
    if (speechFinal)
        emit status(QStringLiteral("Listening."));
}

void DictateController::onMessage(const QString &message)
{
    if (!listening_)
        return;
    const QJsonObject obj = QJsonDocument::fromJson(message.toUtf8()).object();
    const QString type = obj.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("transcript.created")) {
        ready_ = true;
        flushAudio();
        return;
    }
    if (type == QLatin1String("error")) {
        const QString text = obj.value(QStringLiteral("message")).toString();
        fail(text.isEmpty() ? obj.value(QStringLiteral("error")).toString() : text);
        return;
    }
    if (type == QLatin1String("transcript.done")) {
        const QString text = transcriptText(obj);
        if (!text.trimmed().isEmpty())
            commitFinal(text, true);
        return;
    }
    if (!type.isEmpty() && type != QLatin1String("transcript.partial"))
        return;
    const bool isFinal = obj.value(QStringLiteral("is_final")).toBool();
    const bool speechFinal = obj.value(QStringLiteral("speech_final")).toBool();
    const QString text = transcriptText(obj);
    if (!isFinal) {
        const QString heard = delta(text);
        if (!heard.isEmpty()) {
            editor_->previewDictation(heard);
            emit status(QStringLiteral("Hearing: %1").arg(heard));
        }
        return;
    }
    commitFinal(text, speechFinal);
}
