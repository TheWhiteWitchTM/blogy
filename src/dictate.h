#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class MarkdownEdit;
class QProcess;
class QWebSocket;

// Carries bold / quote / fence state from one spoken phrase to the next.
struct DictateCursor {
    bool cap = true;
    bool fence = false;
    int wrap = 0; // 0 none, 1 bold, 2 italic, 3 strike, 4 inline code
    bool quote = false;
};

struct DictatePiece {
    QString text;
    bool scratch = false;
    // The whole phrase was "finish the post" (or the same request under another name).
    // Nothing is inserted. The editor asks Grok to write the post from the draft.
    bool finish = false;
};

// Turns a spoken phrase into Markdown. `before` is the text already sitting
// in front of the cursor, so a heading or a list starts on its own line.
DictatePiece dictateMarkdown(const QString &spoken, const QString &before, DictateCursor *cursor);

// Phrases the speech engine should prefer to hear ("heading two", "code block").
QStringList dictateKeyterms();

// Streams the microphone to xAI speech-to-text and writes Markdown into the editor.
// Audio leaves the machine only while Dictate is on, and only to the xAI key
// already used for Talk to AI.
class DictateController : public QObject {
    Q_OBJECT
public:
    explicit DictateController(MarkdownEdit *editor, QObject *parent = nullptr);
    bool listening() const { return listening_; }

public slots:
    void start(const QString &apiKey);
    void stop();

signals:
    void listeningChanged(bool on);
    void status(const QString &text);
    void failed(const QString &message);

private:
    void fail(const QString &message);
    void sendAudio();
    void flushAudio();
    void onMessage(const QString &message);
    void commitFinal(const QString &full, bool speechFinal);
    QString delta(const QString &full) const;

    MarkdownEdit *editor_ = nullptr;
    QWebSocket *socket_ = nullptr;
    QProcess *audio_ = nullptr;
    QString apiKey_;
    QByteArray pending_;
    QString locked_;
    bool listening_ = false;
    bool stopping_ = false;
    bool ready_ = false;
};
