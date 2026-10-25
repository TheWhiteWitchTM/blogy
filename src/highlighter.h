#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

// Markdown on a black page: front matter, headings, emphasis, code, links,
// quotes, lists, and Liquid tags.
class Highlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit Highlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
        int group = 0;
    };
    QList<Rule> rules_;
    QTextCharFormat heading_, front_, frontKey_, fence_, code_, quote_;
};
