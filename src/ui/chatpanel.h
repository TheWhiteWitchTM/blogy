#pragma once

#include <QElapsedTimer>
#include <QJsonArray>
#include <QWidget>
#include <functional>

class QTimer;

class Config;
class GrokClient;
class MarkdownEdit;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;

// "Talk to AI": a Grok chat that can see the post, with one-click jobs.
class ChatPanel : public QWidget {
    Q_OBJECT
public:
    enum class Apply { Selection, FrontMatter, Insert, Document };

    ChatPanel(Config *config, GrokClient *grok, MarkdownEdit *editor, QWidget *parent = nullptr);

    // Rewrites the post's front matter block; set by the main window.
    std::function<void(const QString &yaml)> replaceFrontMatter;
    std::function<QString()> siteContext;
    std::function<QString()> siteTune;
    std::function<QString()> postUrl;

    void polishSelection();
    void generateFrontMatter();
    void suggestTitleTags();
    void blueskySummary();
    void writePost();
    void focusInput();

    // "Write a post" replaces the open file. A question stays in the chat.
    static Apply applyModeForPrompt(const QString &text);
    // A finished Markdown file: front matter plus a body.
    static bool looksLikeWholePost(const QString &text);

signals:
    void blueskyTextReady(const QString &text);
    void activity(const QString &text);

private:
    void send(const QString &prompt, Apply mode, bool includePost, const QString &shown = {});
    void appendBubble(const QString &who, const QString &text);
    void applyLast();
    void refreshSystem();
    void onTick();
    void applyDraft(const QString &text);
    void finishRequest(bool restoreEditor);
    QString progressLine() const;
    static QString stripFences(const QString &text);

    Config *config_;
    GrokClient *grok_;
    MarkdownEdit *editor_;
    QTextBrowser *log_ = nullptr;
    QPlainTextEdit *input_ = nullptr;
    QPushButton *sendButton_ = nullptr;
    QPushButton *applyButton_ = nullptr;
    QLabel *status_ = nullptr;
    QPlainTextEdit *live_ = nullptr;
    QTimer *tick_ = nullptr;
    QElapsedTimer elapsed_;
    QJsonArray history_;
    QString last_;
    QString pendingDraft_;
    Apply lastMode_ = Apply::Insert;
    bool lastWasBluesky_ = false;
    bool streamingDocument_ = false;
    bool streamedIntoEditor_ = false;
    bool draftDirty_ = false;
};
