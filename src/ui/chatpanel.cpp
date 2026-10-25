#include "ui/chatpanel.h"

#include "config.h"
#include "editor.h"
#include "grok.h"

#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QTextBrowser>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const char *kSystem =
    "You are the writing assistant inside Blogy, a Markdown editor for GitHub Pages blogs. "
    "Answer in Markdown. When asked to rewrite or produce content for the editor, reply with only that content: "
    "no preamble, no commentary, no code fences. Do not use tools or search. "
    "If a site voice is given, write in that voice. Otherwise keep the author's voice: direct, dry, facts first. "
    "Follow the Markdown style given for the site: heading level, emphasis, lists, and any extra notes. "
    "Do not add a signature or a sign-off.";

void allowShrink(QWidget *widget)
{
    widget->setMinimumWidth(0);
    QSizePolicy policy = widget->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Ignored);
    widget->setSizePolicy(policy);
}

QString wholePostPrompt(const QString &ask)
{
    return ask + QStringLiteral(
               "\n\nWrite the whole post as one finished Markdown file. "
               "Include a valid front matter block in the site's format "
               "(YAML between --- lines for Jekyll, or `export const metadata = { ... };` for MDX). "
               "Keep the existing title, date, tags, and description when they are already filled in. "
               "Reply with only the file contents. No preamble and no code fence around the file.");
}

class SendOnCtrlEnter : public QObject {
public:
    SendOnCtrlEnter(QPushButton *button, QObject *parent) : QObject(parent), button_(button) {}
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && (key->modifiers() & Qt::ControlModifier)) {
                button_->click();
                return true;
            }
        }
        return QObject::eventFilter(obj, event);
    }
private:
    QPushButton *button_;
};

} // namespace

QString ChatPanel::withNote(const QString &prompt, const QString &note)
{
    const QString extra = note.trimmed();
    if (extra.isEmpty())
        return prompt;
    return prompt + QStringLiteral("\n\nThe author added this instruction. Follow it:\n") + extra;
}

ChatPanel::ChatPanel(Config *config, GrokClient *grok, MarkdownEdit *editor, QWidget *parent)
    : QWidget(parent), config_(config), grok_(grok), editor_(editor)
{
    setObjectName(QStringLiteral("chatPane"));
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(10, 8, 10, 8);
    v->setSpacing(6);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(4);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    auto *write = new QPushButton(QStringLiteral("Finish the post"), this);
    auto *polish = new QPushButton(QStringLiteral("Polish selection"), this);
    auto *front = new QPushButton(QStringLiteral("Front matter"), this);
    auto *title = new QPushButton(QStringLiteral("Title + tags"), this);
    auto *bsky = new QPushButton(QStringLiteral("Bluesky blurb"), this);
    sendButton_ = new QPushButton(QStringLiteral("Ask"), this);
    sendButton_->setObjectName(QStringLiteral("primary"));
    for (QPushButton *button : {write, polish, front, title, bsky, sendButton_}) {
        allowShrink(button);
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        jobs_.append(button);
    }
    write->setToolTip(QStringLiteral("Turns the outline into the full post, in this site's voice. Text in the box is part of that. Leave the box empty to write from what is already there."));
    polish->setToolTip(QStringLiteral("Rewrite the selected text, or the whole post. Text in the box is part of the rewrite."));
    front->setToolTip(QStringLiteral("Write front matter that fits this site. Text in the box is included."));
    title->setToolTip(QStringLiteral("Suggest titles and tags this site already uses. Text in the box is included."));
    bsky->setToolTip(QStringLiteral("A post under 300 characters. Text in the box is included."));
    sendButton_->setToolTip(QStringLiteral("Send whatever you typed. Ctrl+Enter does this too."));
    grid->addWidget(write, 0, 0, 1, 2);
    grid->addWidget(polish, 1, 0);
    grid->addWidget(front, 1, 1);
    grid->addWidget(title, 2, 0);
    grid->addWidget(bsky, 2, 1);
    grid->addWidget(sendButton_, 3, 0, 1, 2);
    v->addLayout(grid);

    log_ = new QTextBrowser(this);
    log_->setObjectName(QStringLiteral("chatLog"));
    log_->setOpenExternalLinks(true);
    log_->setMinimumHeight(0);
    log_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    v->addWidget(log_, 1);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("muted"));
    status_->setWordWrap(true);
    status_->setMinimumWidth(0);
    status_->setMinimumHeight(0);
    status_->setMaximumHeight(status_->fontMetrics().lineSpacing() * 2 + 6);
    v->addWidget(status_);

    live_ = new QPlainTextEdit(this);
    live_->setObjectName(QStringLiteral("chatInput"));
    live_->setReadOnly(true);
    live_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    live_->setPlaceholderText(QStringLiteral("Grok's reply shows up here as it is written."));
    live_->setMinimumHeight(0);
    live_->setMaximumHeight(72);
    allowShrink(live_);
    live_->hide();
    v->addWidget(live_);

    tick_ = new QTimer(this);
    tick_->setInterval(80);
    connect(tick_, &QTimer::timeout, this, &ChatPanel::onTick);

    input_ = new QPlainTextEdit(this);
    input_->setObjectName(QStringLiteral("chatPrompt"));
    input_->setPlaceholderText(QStringLiteral("Ask anything, or a note for Finish, Polish, and the other buttons. Ctrl+Enter asks."));
    input_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    // The box grows when the pane is tall. On a short window it shrinks so Ask stays on screen.
    input_->setMinimumHeight(72);
    input_->setMinimumWidth(0);
    input_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    input_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    input_->document()->setDocumentMargin(2);
    QFont promptFont = input_->font();
    promptFont.setPointSize(16);
    input_->setFont(promptFont);
    v->addWidget(input_, 2);

    clearButton_ = new QPushButton(QStringLiteral("Clear"), this);
    applyButton_ = new QPushButton(QStringLiteral("Apply to editor"), this);
    applyButton_->setEnabled(false);
    clearButton_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    allowShrink(applyButton_);
    auto *applyRow = new QHBoxLayout;
    applyRow->setSpacing(6);
    applyRow->addWidget(clearButton_);
    applyRow->addWidget(applyButton_, 1);
    v->addLayout(applyRow);
    input_->installEventFilter(new SendOnCtrlEnter(sendButton_, this));

    connect(write, &QPushButton::clicked, this, &ChatPanel::writePost);
    connect(polish, &QPushButton::clicked, this, &ChatPanel::polishSelection);
    connect(front, &QPushButton::clicked, this, &ChatPanel::generateFrontMatter);
    connect(title, &QPushButton::clicked, this, &ChatPanel::suggestTitleTags);
    connect(bsky, &QPushButton::clicked, this, &ChatPanel::blueskySummary);
    connect(applyButton_, &QPushButton::clicked, this, &ChatPanel::applyLast);
    connect(clearButton_, &QPushButton::clicked, this, [this] {
        history_ = QJsonArray();
        log_->clear();
        last_.clear();
        applyButton_->setEnabled(false);
    });
    connect(sendButton_, &QPushButton::clicked, this, [this] {
        if (grok_->busy()) {
            status_->setText(QStringLiteral("Still waiting on the last answer."));
            emit activity(status_->text());
            return;
        }
        const QString text = input_->toPlainText().trimmed();
        if (text.isEmpty()) {
            status_->setText(QStringLiteral("Type what you want, then press Ask."));
            emit activity(status_->text());
            input_->setFocus();
            return;
        }
        input_->clear();
        lastWasBluesky_ = false;
        const Apply mode = applyModeForPrompt(text);
        const QString prompt = mode == Apply::Document ? wholePostPrompt(text) : text;
        send(prompt, mode, history_.isEmpty() || mode == Apply::Document, text);
    });
    connect(grok_, &GrokClient::draft, this, [this](const QString &text) {
        pendingDraft_ = text;
        draftDirty_ = true;
    });
    connect(grok_, &GrokClient::replied, this, [this](const QString &text) {
        const bool streamed = streamedIntoEditor_;
        finishRequest(false);
        history_.append(GrokClient::message(QStringLiteral("assistant"), text));
        last_ = stripFences(text);
        if (lastMode_ == Apply::Insert && !lastWasBluesky_ && looksLikeWholePost(last_))
            lastMode_ = Apply::Document;
        if (lastMode_ == Apply::Document) {
            editor_->replaceAll(last_, streamed);
            editor_->setFocus();
            status_->setText(QStringLiteral("Wrote the post into the editor."));
            emit activity(status_->text());
        } else {
            status_->clear();
        }
        appendBubble(QStringLiteral("Grok"), text);
        applyButton_->setText(lastMode_ == Apply::Document ? QStringLiteral("Replace post")
                               : lastMode_ == Apply::Insert ? QStringLiteral("Insert")
                                                            : QStringLiteral("Apply to editor"));
        applyButton_->setEnabled(true);
        if (lastWasBluesky_)
            emit blueskyTextReady(last_);
    });
    connect(grok_, &GrokClient::failed, this, [this](const QString &message) {
        const QString partial = pendingDraft_;
        const bool keepPartial = streamedIntoEditor_ && !partial.trimmed().isEmpty();
        const bool wasDocument = streamingDocument_;
        finishRequest(!keepPartial);
        QString shown = message;
        if (keepPartial)
            shown += QStringLiteral(" What arrived is in the editor.");
        else if (!wasDocument && !partial.trimmed().isEmpty())
            appendBubble(QStringLiteral("Grok"), partial);
        status_->setText(shown);
        emit activity(shown);
        appendBubble(QStringLiteral("Blogy"), shown);
    });

    if (config_->grokUsesCli())
        status_->setText(QStringLiteral("Grok CLI, signed in. Model %1.").arg(config_->grokModelResolved()));
    else {
        const QString source = config_->secretSource(QStringLiteral("grok"));
        status_->setText(source == QLatin1String("missing")
                             ? QStringLiteral("Grok is not signed in. Install Grok, then run grok login.")
                             : QStringLiteral("Model %1, key from %2.").arg(config_->grokModelResolved(), source));
    }
}

ChatPanel::Apply ChatPanel::applyModeForPrompt(const QString &text)
{
    const QString t = text.simplified();
    static const QRegularExpression writePost(
        QStringLiteral(
            R"(\b(write|draft|rewrite|compose|create|generate|finish)\s+(?:(?:me|us|the|a|an|this|my|our|whole|full|entire|short|new|test|another|complete|quick)\s+){0,6}(post|article|blog|draft|outline)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression makePost(
        QStringLiteral(
            R"(\bmake\s+(?:me\s+)?(?:a|the|this|another|it)\s+(?:(?:a|an|the|short|new|test|quick|whole|into)\s+){0,4}(post|article|outline)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression turnPost(
        QStringLiteral(R"(\bturn\s+(?:this\s+|it\s+)?into\s+(?:a\s+|the\s+)?(post|article|outline)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    if (writePost.match(t).hasMatch() || makePost.match(t).hasMatch() || turnPost.match(t).hasMatch())
        return Apply::Document;
    return Apply::Insert;
}

bool ChatPanel::looksLikeWholePost(const QString &text)
{
    const QString t = text.trimmed();
    if (t.startsWith(QLatin1String("export const metadata")))
        return t.contains(QLatin1Char('\n')) && t.size() > 80;
    if (!t.startsWith(QLatin1String("---")))
        return false;
    const int close = t.indexOf(QLatin1String("\n---"), 3);
    if (close < 0)
        return false;
    return t.mid(close + 4).trimmed().size() >= 20;
}

QString ChatPanel::stripFences(const QString &text)
{
    static const QRegularExpression fenced(QStringLiteral(R"(^\s*```[a-zA-Z]*\n(.*?)\n```\s*$)"),
                                           QRegularExpression::DotMatchesEverythingOption);
    const auto m = fenced.match(text);
    return m.hasMatch() ? m.captured(1) : text.trimmed();
}

void ChatPanel::appendBubble(const QString &who, const QString &text)
{
    QString md = QStringLiteral("**") + who + QStringLiteral("**\n\n") + text + QStringLiteral("\n\n---\n");
    QTextCursor c(log_->document());
    c.movePosition(QTextCursor::End);
    QTextDocument piece;
    piece.setMarkdown(md);
    c.insertFragment(QTextDocumentFragment(&piece));
    log_->moveCursor(QTextCursor::End);
    log_->ensureCursorVisible();
}

void ChatPanel::refreshSystem()
{
    QString system = QString::fromLatin1(kSystem);
    if (siteContext) {
        const QString context = siteContext();
        if (!context.isEmpty())
            system += QStringLiteral("\n\n") + context;
    }
    const QJsonObject message = GrokClient::message(QStringLiteral("system"), system);
    if (!history_.isEmpty() && history_.at(0).toObject().value(QStringLiteral("role")).toString() == QLatin1String("system"))
        history_.replace(0, message);
    else
        history_.prepend(message);
}

QString ChatPanel::progressLine() const
{
    const int secs = int(elapsed_.elapsed() / 1000);
    if (pendingDraft_.trimmed().isEmpty())
        return QStringLiteral("Asking Grok… %1s").arg(secs);
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    const int words = pendingDraft_.split(ws, Qt::SkipEmptyParts).size();
    return QStringLiteral("Writing… %1 words · %2s").arg(words).arg(secs);
}

void ChatPanel::applyDraft(const QString &text)
{
    live_->setPlainText(text);
    live_->show();
    QTextCursor end(live_->document());
    end.movePosition(QTextCursor::End);
    live_->setTextCursor(end);
    live_->ensureCursorVisible();
    if (!streamingDocument_ || text.isEmpty())
        return;
    editor_->replaceAll(text, streamedIntoEditor_);
    streamedIntoEditor_ = true;
}

void ChatPanel::onTick()
{
    if (!tick_->isActive())
        return;
    if (draftDirty_) {
        draftDirty_ = false;
        applyDraft(pendingDraft_);
    }
    const QString line = progressLine();
    status_->setText(line);
    emit activity(line);
}

void ChatPanel::setWorking(bool on)
{
    for (QPushButton *button : jobs_)
        button->setEnabled(!on);
    if (clearButton_)
        clearButton_->setEnabled(!on);
    if (applyButton_)
        applyButton_->setEnabled(!on && !last_.isEmpty());
}

QString ChatPanel::takeNote()
{
    const QString note = input_->toPlainText().trimmed();
    if (!note.isEmpty())
        input_->clear();
    return note;
}

void ChatPanel::finishRequest(bool restoreEditor)
{
    tick_->stop();
    draftDirty_ = false;
    live_->hide();
    setWorking(false);
    if (restoreEditor && streamedIntoEditor_)
        editor_->undo();
    streamedIntoEditor_ = false;
    streamingDocument_ = false;
    pendingDraft_.clear();
}

void ChatPanel::send(const QString &prompt, Apply mode, bool includePost, const QString &shown)
{
    if (grok_->busy()) {
        status_->setText(QStringLiteral("Still waiting on the last answer."));
        emit activity(status_->text());
        return;
    }
    lastMode_ = mode;
    streamingDocument_ = mode == Apply::Document;
    streamedIntoEditor_ = false;
    pendingDraft_.clear();
    draftDirty_ = false;
    refreshSystem();
    QString content = prompt;
    if (includePost)
        content += QStringLiteral("\n\n--- The post ---\n") + editor_->toPlainText();
    history_.append(GrokClient::message(QStringLiteral("user"), content));
    appendBubble(QStringLiteral("You"), shown.isEmpty() ? prompt : shown);
    live_->clear();
    live_->show();
    elapsed_.restart();
    tick_->start();
    onTick();
    setWorking(true);
    GrokClient::Request request;
    request.useCli = config_->grokUsesCli();
    request.command = config_->grokCommandResolved();
    request.endpoint = config_->grokEndpoint;
    request.model = config_->grokModelResolved();
    request.apiKey = config_->grokApiKey();
    request.messages = history_;
    grok_->chat(request);
}

void ChatPanel::polishSelection()
{
    if (grok_->busy()) {
        status_->setText(QStringLiteral("Still waiting on the last answer."));
        emit activity(status_->text());
        return;
    }
    lastWasBluesky_ = false;
    const QString note = takeNote();
    const bool hasSel = editor_->textCursor().hasSelection();
    const QString text = editor_->selectedOrAll();
    const QString prompt = withNote(QStringLiteral("Polish this %1. Fix grammar, tighten wording, keep the voice, and follow the site's Markdown style. "
                                                   "Reply with only the rewritten text.\n\n%2")
                                        .arg(hasSel ? QStringLiteral("passage")
                                                    : QStringLiteral("post (front matter included, keep it valid)"),
                                             text),
                                    note);
    send(prompt, Apply::Selection, false, note.isEmpty() ? QString() : QStringLiteral("Polish. ") + note);
}

void ChatPanel::generateFrontMatter()
{
    if (grok_->busy()) {
        status_->setText(QStringLiteral("Still waiting on the last answer."));
        emit activity(status_->text());
        return;
    }
    lastWasBluesky_ = false;
    const QString note = takeNote();
    send(withNote(QStringLiteral("Write the front matter for this post. Keep existing keys and the same format "
                                 "(YAML for Jekyll, or the MDX metadata keys). Use this site's layout. "
                                 "Fill title, description (one sentence, under 160 characters) and tags "
                                 "(3-5, lowercase, preferring tags this site already uses). "
                                 "Reply with only `key: value` lines, no --- fences, lists as [a, b]."),
                  note),
         Apply::FrontMatter, true, note.isEmpty() ? QStringLiteral("Front matter.") : QStringLiteral("Front matter. ") + note);
}

void ChatPanel::suggestTitleTags()
{
    if (grok_->busy()) {
        status_->setText(QStringLiteral("Still waiting on the last answer."));
        emit activity(status_->text());
        return;
    }
    lastWasBluesky_ = false;
    const QString note = takeNote();
    send(withNote(QStringLiteral("Suggest five titles and five tags for this post. Prefer tags this site already uses. "
                                 "Short list, best first."),
                  note),
         Apply::Insert, true, note.isEmpty() ? QStringLiteral("Title and tags.") : QStringLiteral("Title and tags. ") + note);
}

void ChatPanel::writePost()
{
    lastWasBluesky_ = false;
    if (grok_->busy()) {
        status_->setText(QStringLiteral("Still waiting on the last answer."));
        emit activity(status_->text());
        return;
    }
    const QString brief = takeNote();
    if (brief.isEmpty() && editor_->toPlainText().trimmed().isEmpty()) {
        status_->setText(QStringLiteral("Dictate an outline first, or type what the post should be."));
        emit activity(status_->text());
        input_->setFocus();
        return;
    }
    QString prompt = QStringLiteral(
        "The text below is the draft already in the editor. It may be an outline, dictation, or a post in progress. "
        "Turn it into one finished Markdown file. "
        "Keep its facts and the order of its points. Keep any paragraph that is already written in full. "
        "Expand notes and fragments into the article. "
        "Include a valid front matter block in the site's format "
        "(YAML between --- lines for Jekyll, or `export const metadata = { ... };` for MDX). "
        "Keep the existing title, date, tags, and description when they are already filled in. "
        "Write in the site's voice. "
        "Follow the Markdown style given for this site: the heading level, emphasis, lists, and notes. "
        "Do not add a signature. "
        "Reply with only the file contents. No preamble and no code fence around the file.");
    if (!brief.isEmpty())
        prompt += QStringLiteral("\n\nChange it like this:\n") + brief;
    else
        prompt += QStringLiteral("\n\nNo extra instructions. Finish the draft from what is already there.");
    send(prompt, Apply::Document, true, brief.isEmpty() ? QStringLiteral("Finish the post from the outline.") : brief);
}

void ChatPanel::focusInput()
{
    input_->setFocus();
}

void ChatPanel::blueskySummary()
{
    if (grok_->busy()) {
        status_->setText(QStringLiteral("Still waiting on the last answer."));
        emit activity(status_->text());
        return;
    }
    lastWasBluesky_ = true;
    const QString note = takeNote();
    const QString url = postUrl ? postUrl() : QString();
    QString prompt = QStringLiteral(
        "Write a Bluesky post. Start with a short excerpt of the article, one or two sentences in the site's voice, "
        "then a blank line, then the post's URL on its own line. Stay under 300 characters. "
        "No hashtags unless they earn it. Reply with only the post text.");
    if (!url.isEmpty())
        prompt += QStringLiteral("\n\nUse this exact URL:\n") + url;
    else
        prompt += QStringLiteral("\n\nThe public URL is not known yet. Write the excerpt only.");
    send(withNote(prompt, note), Apply::Insert, true,
         note.isEmpty() ? QStringLiteral("Bluesky blurb.") : QStringLiteral("Bluesky blurb. ") + note);
}

void ChatPanel::applyLast()
{
    if (last_.isEmpty())
        return;
    switch (lastMode_) {
    case Apply::FrontMatter:
        if (replaceFrontMatter) {
            QString yaml = last_;
            yaml.remove(QRegularExpression(QStringLiteral(R"(^\s*---\s*\n|\n---\s*$)")));
            replaceFrontMatter(yaml);
        }
        break;
    case Apply::Selection:
        editor_->replaceSelectionOrAll(last_);
        break;
    case Apply::Document:
        editor_->replaceAll(last_);
        break;
    case Apply::Insert:
        editor_->textCursor().insertText(last_);
        break;
    }
    editor_->setFocus();
}
