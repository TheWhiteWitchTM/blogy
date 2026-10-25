#pragma once

#include "highlighter.h"

#include <QPlainTextEdit>

// The writing surface: monospace on black, soft wrap, list continuation,
// and Markdown shortcuts (bold, italic, link, code).
class MarkdownEdit : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit MarkdownEdit(QWidget *parent = nullptr);

    void setFontPointSize(int points);
    int fontPointSize() const { return fontSize_; }
    void setWrap(bool on);

    void wrapSelection(const QString &left, const QString &right);
    void insertLink();
    void replaceSelectionOrAll(const QString &text);
    QString selectedOrAll() const;
    int wordCount() const;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    Highlighter *highlighter_ = nullptr;
    int fontSize_ = 13;
};
