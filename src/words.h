#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// A misspelled span in a Markdown document, in characters.
struct WordHit {
    int start = 0;
    int length = 0;
};

// English spellcheck and Tab completion, in the same shape as LibreSky:
// a word list, words you have kept, ghost text for the shortest match,
// and no underline on a word you are still typing when it can still grow
// into a real one. Capitalized words are left alone (names, sentence starts).
class Lexicon {
public:
    static Lexicon &english();

    void setLearned(const QStringList &words);
    void learn(const QString &word);
    QStringList learned() const;

    bool known(const QString &word) const;
    // True when some longer dictionary word starts with this prefix.
    bool extends(const QString &prefix) const;
    // Up to six completions. `extra` is tried first (names already in the post).
    QStringList complete(const QString &prefix, const QStringList &extra = {}) const;
    QStringList corrections(const QString &word, int limit = 5) const;
    // Lowercase unknown words outside front matter, fenced code, inline code, and URLs.
    // The word touching `caret` is skipped while it is a prefix of a real word.
    QVector<WordHit> typos(const QString &text, int caret) const;

private:
    void ensure() const;
    bool has(const QVector<QString> &pool, const QString &word) const;

    mutable bool loaded_ = false;
    mutable QVector<QString> words_;
    QVector<QString> learned_;
};
