#include "ui/chatpanel.h"

#include "config.h"
#include "editor.h"
#include "grok.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextBrowser>
#include <QTextDocumentFragment>
#include <QVBoxLayout>

namespace {

const char *kSystem =
    "You are the writing assistant inside Blogy, a Markdown editor for GitHub Pages blogs. "
    "Keep the author's voice: direct, dry, facts first. Answer in Markdown. "
    "When asked to rewrite or produce content for the editor, reply with only that content: "
    "no preamble, no commentary, no code fences.";

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

ChatPanel::ChatPanel(Config *config, GrokClient *grok, MarkdownEdit *editor, QWidget *parent)
    : QWidget(parent), config_(config), grok_(grok), editor_(editor)
{
    setObjectName(QStringLiteral("chatPane"));
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(10, 10, 10, 10);
    auto *label = new QLabel(QStringLiteral("Talk to AI"), this);
    label->setObjectName(QStringLiteral("section"));
    v->addWidget(label);

    auto *grid = new QGridLayout;
    auto *polish = new QPushButton(QStringLiteral("Polish selection"), this);
    auto *front = new QPushButton(QStringLiteral("Front matter"), this);
    auto *title = new QPushButton(QStringLiteral("Title + tags"), this);
    auto *bsky = new QPushButton(QStringLiteral("Bluesky blurb"), this);
    polish->setToolTip(QStringLiteral("Rewrite the selected text (or the whole post) and keep your voice."));
    front->setToolTip(QStringLiteral("Write front matter for this post that fits the site."));
    title->setToolTip(QStringLiteral("Suggest titles and tags."));
    bsky->setToolTip(QStringLiteral("A post under 300 characters announcing this article."));
    grid->addWidget(polish, 0, 0);
    grid->addWidget(front, 0, 1);
    grid->addWidget(title, 1, 0);
    grid->addWidget(bsky, 1, 1);
    v->addLayout(grid);

    log_ = new QTextBrowser(this);
    log_->setObjectName(QStringLiteral("chatLog"));
    log_->setOpenExternalLinks(true);
    v->addWidget(log_, 1);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("muted"));
    v->addWidget(status_);

    input_ = new QPlainTextEdit(this);
    input_->setObjectName(QStringLiteral("chatInput"));
    input_->setPlaceholderText(QStringLiteral("Ask about the post. Ctrl+Enter sends."));
    input_->setMaximumHeight(90);
    v->addWidget(input_);

    auto *buttons = new QHBoxLayout;
    auto *clear = new QPushButton(QStringLiteral("Clear"), this);
    applyButton_ = new QPushButton(QStringLiteral("Apply to editor"), this);
    applyButton_->setEnabled(false);
    sendButton_ = new QPushButton(QStringLiteral("Send"), this);
    sendButton_->setObjectName(QStringLiteral("primary"));
    buttons->addWidget(clear);
    buttons->addStretch(1);
    buttons->addWidget(applyButton_);
    buttons->addWidget(sendButton_);
    v->addLayout(buttons);
    input_->installEventFilter(new SendOnCtrlEnter(sendButton_, this));

    connect(polish, &QPushButton::clicked, this, &ChatPanel::polishSelection);
    connect(front, &QPushButton::clicked, this, &ChatPanel::generateFrontMatter);
    connect(title, &QPushButton::clicked, this, &ChatPanel::suggestTitleTags);
    connect(bsky, &QPushButton::clicked, this, &ChatPanel::blueskySummary);
    connect(applyButton_, &QPushButton::clicked, this, &ChatPanel::applyLast);
    connect(clear, &QPushButton::clicked, this, [this] {
        history_ = QJsonArray();
        log_->clear();
        last_.clear();
        applyButton_->setEnabled(false);
    });
    connect(sendButton_, &QPushButton::clicked, this, [this] {
        const QString text = input_->toPlainText().trimmed();
        if (text.isEmpty())
            return;
        input_->clear();
        send(text, Apply::Insert, history_.isEmpty());
    });
    connect(grok_, &GrokClient::replied, this, [this](const QString &text) {
        sendButton_->setEnabled(true);
        status_->clear();
        history_.append(GrokClient::message(QStringLiteral("assistant"), text));
        last_ = stripFences(text);
        appendBubble(QStringLiteral("Grok"), text);
        applyButton_->setEnabled(true);
        if (lastWasBluesky_)
            emit blueskyTextReady(last_);
    });
    connect(grok_, &GrokClient::failed, this, [this](const QString &message) {
        sendButton_->setEnabled(true);
        status_->setText(message);
        appendBubble(QStringLiteral("Blogy"), message);
    });

    const QString source = config_->secretSource(QStringLiteral("grok"));
    status_->setText(source == QLatin1String("missing")
                         ? QStringLiteral("No xAI key yet: set XAI_API_KEY or add it in Settings.")
                         : QStringLiteral("Model %1, key from %2.").arg(config_->grokModel, source));
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

void ChatPanel::send(const QString &prompt, Apply mode, bool includePost)
{
    if (grok_->busy()) {
        status_->setText(QStringLiteral("Still waiting on the last answer."));
        return;
    }
    lastMode_ = mode;
    if (history_.isEmpty()) {
        QString system = QString::fromLatin1(kSystem);
        if (siteContext)
            system += QStringLiteral("\n\n") + siteContext();
        history_.append(GrokClient::message(QStringLiteral("system"), system));
    }
    QString content = prompt;
    if (includePost)
        content += QStringLiteral("\n\n--- The post ---\n") + editor_->toPlainText();
    history_.append(GrokClient::message(QStringLiteral("user"), content));
    appendBubble(QStringLiteral("You"), prompt);
    status_->setText(QStringLiteral("Asking %1…").arg(config_->grokModel));
    sendButton_->setEnabled(false);
    grok_->chat(config_->grokEndpoint, config_->grokModel, config_->grokApiKey(), history_);
}

void ChatPanel::polishSelection()
{
    lastWasBluesky_ = false;
    const bool hasSel = editor_->textCursor().hasSelection();
    const QString text = editor_->selectedOrAll();
    send(QStringLiteral("Polish this %1. Fix grammar, tighten wording, keep the voice and the Markdown. "
                        "Reply with only the rewritten text.\n\n%2")
             .arg(hasSel ? QStringLiteral("passage") : QStringLiteral("post (front matter included, keep it valid)"), text),
         Apply::Selection, false);
}

void ChatPanel::generateFrontMatter()
{
    lastWasBluesky_ = false;
    send(QStringLiteral("Write the front matter for this post. Keep existing keys and the same format "
                        "(YAML for Jekyll, or the MDX metadata keys). Fill title, description (one sentence, "
                        "under 160 characters) and tags (3-5, lowercase). Reply with only `key: value` lines, "
                        "no --- fences, lists as [a, b]."),
         Apply::FrontMatter, true);
}

void ChatPanel::suggestTitleTags()
{
    lastWasBluesky_ = false;
    send(QStringLiteral("Suggest five titles and five tags for this post. Short list, best first."), Apply::Insert, true);
}

void ChatPanel::blueskySummary()
{
    lastWasBluesky_ = true;
    send(QStringLiteral("Write a Bluesky post announcing this article, under 250 characters so a link fits. "
                        "No hashtags unless they earn it. Reply with only the post text."),
         Apply::Insert, true);
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
    case Apply::Insert:
        editor_->textCursor().insertText(last_);
        break;
    }
    editor_->setFocus();
}
