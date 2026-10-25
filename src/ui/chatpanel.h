#pragma once

#include <QJsonArray>
#include <QWidget>
#include <functional>

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
    enum class Apply { Selection, FrontMatter, Insert };

    ChatPanel(Config *config, GrokClient *grok, MarkdownEdit *editor, QWidget *parent = nullptr);

    // Rewrites the post's front matter block; set by the main window.
    std::function<void(const QString &yaml)> replaceFrontMatter;
    std::function<QString()> siteContext;

    void polishSelection();
    void generateFrontMatter();
    void suggestTitleTags();
    void blueskySummary();

signals:
    void blueskyTextReady(const QString &text);

private:
    void send(const QString &prompt, Apply mode, bool includePost);
    void appendBubble(const QString &who, const QString &text);
    void applyLast();
    static QString stripFences(const QString &text);

    Config *config_;
    GrokClient *grok_;
    MarkdownEdit *editor_;
    QTextBrowser *log_ = nullptr;
    QPlainTextEdit *input_ = nullptr;
    QPushButton *sendButton_ = nullptr;
    QPushButton *applyButton_ = nullptr;
    QLabel *status_ = nullptr;
    QJsonArray history_;
    QString last_;
    Apply lastMode_ = Apply::Insert;
    bool lastWasBluesky_ = false;
};
