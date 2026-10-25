#pragma once

#include "dictate.h"
#include "highlighter.h"

#include <QEvent>
#include <QPlainTextEdit>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <functional>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QFrame;
class QLabel;
class QListWidget;

struct HeadingMark {
    int level = 0;
    int position = 0;
    QString text;
};

// The writing surface: monospace on black, soft wrap, list continuation,
// Markdown shortcuts, spelling, and Tab completion.
class MarkdownEdit : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit MarkdownEdit(QWidget *parent = nullptr);

    void setFontPointSize(int points);
    int fontPointSize() const { return fontSize_; }
    void setWrap(bool on);
    void setLearnedWords(const QStringList &words);

    void wrapSelection(const QString &left, const QString &right);
    void toggleWrap(const QString &left, const QString &right);
    void setHeading(int level);
    void toggleLinePrefix(const QString &prefix);
    void toggleNumberedList();
    void toggleTaskList();
    // fromMenu retargets the fence the cursor is already in. The shortcut inserts one.
    void insertCodeBlock(const QString &language = QString(), bool fromMenu = false);
    void insertTable();
    void insertHorizontalRule();
    void insertSnippet(const QString &text);
    void insertImage(const QString &alt, const QString &destination);
    void insertLink();
    void replaceSelectionOrAll(const QString &text);
    // joinUndo folds this replacement into the previous one, so a stream is a single undo.
    void replaceAll(const QString &text, bool joinUndo = false);
    QString selectedOrAll() const;
    int wordCount() const;
    QVector<HeadingMark> headings() const;
    void goToPosition(int position);

    bool findText(const QString &needle, bool forward, bool matchCase);
    int replaceAllText(const QString &needle, const QString &replacement, bool matchCase);
    bool suggestVisible() const;

    void beginDictation();
    void endDictation();
    void commitDictation(const QString &spoken);
    void previewDictation(const QString &spoken);
    void clearDictationPreview();
    QStringList dictationHints() const;

signals:
    void learnedWordsChanged(const QStringList &words);
    // The dictated phrase was "finish the post". The draft stays as it is.
    void finishFromOutline();

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void mapLines(const std::function<QString(const QString &)> &fn);
    void tryExpandShortcode();
    bool spellablePosition(int pos) const;
    bool cursorInFence(int *openPosition = nullptr) const;
    void replaceRange(int start, int length, const QString &text);
    QString textBeforeCursor() const;
    QStringList completionExtras() const;
    void scheduleAssist(int suggestMs, int spellMs);
    void refreshSuggest();
    void refreshSpell();
    void hideSuggest();
    void placeAssist();
    void showGhost(const QString &text, bool dictation);
    void acceptSuggestion(const QString &insert);
    void scratchDictation();

    Highlighter *highlighter_ = nullptr;
    QFrame *suggest_ = nullptr;
    QListWidget *suggestList_ = nullptr;
    QLabel *ghost_ = nullptr;
    QTimer suggestTimer_;
    QTimer spellTimer_;
    int fontSize_ = 13;
    int suggestStart_ = -1;
    int suggestLength_ = 0;
    int lastDictated_ = 0;
    bool dictating_ = false;
    bool fenceAtStart_ = false;
    DictateCursor dictate_;
};
