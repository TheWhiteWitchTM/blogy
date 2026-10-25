#include "words.h"

#include <QFile>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {

bool latinWord(const QString &word)
{
    if (word.isEmpty() || word.size() > 40 || !word.at(0).isLetter())
        return false;
    for (const QChar c : word) {
        if (!(c.isLetter() || c == QLatin1Char('\'') || c == QLatin1Char('-')))
            return false;
    }
    return true;
}

QString fold(QString word)
{
    word.replace(QChar(0x2019), QLatin1Char('\''));
    return word.toLower();
}

QString applyCase(const QString &prefix, const QString &word)
{
    bool hasLetter = false;
    bool allCaps = !prefix.isEmpty();
    for (const QChar c : prefix) {
        if (!c.isLetter())
            continue;
        hasLetter = true;
        if (c != c.toUpper())
            allCaps = false;
    }
    if (hasLetter && allCaps)
        return word.toUpper();
    if (!prefix.isEmpty() && prefix.at(0).isLetter() && prefix.at(0) == prefix.at(0).toUpper())
        return word.at(0).toUpper() + word.mid(1);
    return word;
}

bool oneEdit(const QString &a, const QString &b)
{
    if (a == b)
        return false;
    const int la = a.size();
    const int lb = b.size();
    if (qAbs(la - lb) > 1)
        return false;
    if (la == lb) {
        int diff = 0;
        for (int i = 0; i < la; ++i) {
            if (a.at(i) != b.at(i) && ++diff > 1)
                return false;
        }
        return diff == 1;
    }
    const QString &shorter = la < lb ? a : b;
    const QString &longer = la < lb ? b : a;
    int i = 0;
    int j = 0;
    int diff = 0;
    while (i < shorter.size() && j < longer.size()) {
        if (shorter.at(i) == longer.at(j)) {
            ++i;
            ++j;
        } else {
            if (++diff > 1)
                return false;
            ++j;
        }
    }
    return true;
}

void keepShortest(QStringList *out, const QString &word, int cap)
{
    if (out->contains(word))
        return;
    out->append(word);
    std::sort(out->begin(), out->end(), [](const QString &a, const QString &b) {
        return a.size() < b.size() || (a.size() == b.size() && a < b);
    });
    if (out->size() > cap)
        out->resize(cap);
}

} // namespace

Lexicon &Lexicon::english()
{
    static Lexicon lexicon;
    return lexicon;
}

void Lexicon::ensure() const
{
    if (loaded_)
        return;
    loaded_ = true;
    QFile file(QStringLiteral(":/words-en.txt"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (latinWord(line))
            words_.append(line);
    }
    std::sort(words_.begin(), words_.end());
    words_.erase(std::unique(words_.begin(), words_.end()), words_.end());
}

void Lexicon::setLearned(const QStringList &words)
{
    learned_.clear();
    for (const QString &word : words) {
        const QString folded = fold(word);
        if (!latinWord(folded))
            continue;
        auto it = std::lower_bound(learned_.begin(), learned_.end(), folded);
        if (it == learned_.end() || *it != folded)
            learned_.insert(it, folded);
        if (learned_.size() >= 400)
            break;
    }
}

void Lexicon::learn(const QString &word)
{
    const QString folded = fold(word);
    if (!latinWord(folded) || folded.size() < 2)
        return;
    auto it = std::lower_bound(learned_.begin(), learned_.end(), folded);
    if (it != learned_.end() && *it == folded)
        return;
    if (learned_.size() >= 400)
        learned_.removeFirst();
    it = std::lower_bound(learned_.begin(), learned_.end(), folded);
    learned_.insert(it, folded);
}

QStringList Lexicon::learned() const
{
    return QStringList(learned_.begin(), learned_.end());
}

bool Lexicon::has(const QVector<QString> &pool, const QString &word) const
{
    return std::binary_search(pool.begin(), pool.end(), word);
}

bool Lexicon::known(const QString &word) const
{
    ensure();
    const QString lower = fold(word);
    if (lower.size() < 3)
        return true;
    if (has(words_, lower) || has(learned_, lower))
        return true;
    if (lower.endsWith(QLatin1String("'s")) && has(words_, lower.left(lower.size() - 2)))
        return true;
    const QStringList parts = lower.split(QRegularExpression(QStringLiteral("['-]")), Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return false;
    bool longEnough = false;
    for (const QString &part : parts) {
        if (part.size() >= 3)
            longEnough = true;
        if (part.size() < 2 || (!has(words_, part) && !has(learned_, part)))
            return false;
    }
    return longEnough;
}

bool Lexicon::extends(const QString &prefix) const
{
    ensure();
    const QString lower = fold(prefix);
    if (lower.size() < 2)
        return false;
    const auto scan = [&](const QVector<QString> &pool) {
        auto it = std::lower_bound(pool.begin(), pool.end(), lower);
        for (; it != pool.end() && it->startsWith(lower); ++it) {
            if (it->size() > lower.size())
                return true;
        }
        return false;
    };
    return scan(learned_) || scan(words_);
}

QStringList Lexicon::complete(const QString &prefix, const QStringList &extra) const
{
    ensure();
    if (!latinWord(prefix) || prefix.size() < 2)
        return {};
    const QString lower = fold(prefix);
    QStringList pools[3];
    const auto take = [&](QStringList *pool, const QString &word) {
        const QString folded = fold(word);
        if (folded == lower || !folded.startsWith(lower))
            return;
        keepShortest(pool, folded, 8);
    };
    for (const QString &word : extra)
        take(&pools[0], word);
    for (auto it = std::lower_bound(learned_.begin(), learned_.end(), lower); it != learned_.end() && it->startsWith(lower); ++it)
        take(&pools[1], *it);
    auto it = std::lower_bound(words_.begin(), words_.end(), lower);
    for (; it != words_.end() && it->startsWith(lower); ++it)
        take(&pools[2], *it);

    QStringList out;
    QSet<QString> seen;
    for (const QStringList &pool : pools) {
        for (const QString &word : pool) {
            if (seen.contains(word))
                continue;
            seen.insert(word);
            out.append(applyCase(prefix, word));
            if (out.size() == 6)
                return out;
        }
    }
    return out;
}

QStringList Lexicon::corrections(const QString &word, int limit) const
{
    ensure();
    const QString lower = fold(word);
    if (lower.size() < 3 || lower.size() > 40 || known(lower))
        return {};
    struct Candidate {
        QString word;
        int distance = 0;
    };
    QVector<Candidate> found;
    const auto scan = [&](const QVector<QString> &pool) {
        if (pool.isEmpty())
            return;
        const QString from(lower.at(0));
        auto it = std::lower_bound(pool.begin(), pool.end(), from);
        for (; it != pool.end() && it->startsWith(from); ++it) {
            if (qAbs(it->size() - lower.size()) > 1)
                continue;
            if (!oneEdit(lower, *it))
                continue;
            found.append({*it, it->size() == lower.size() ? 0 : 1});
        }
    };
    scan(learned_);
    scan(words_);
    std::sort(found.begin(), found.end(), [](const Candidate &a, const Candidate &b) {
        if (a.distance != b.distance)
            return a.distance < b.distance;
        if (a.word.size() != b.word.size())
            return a.word.size() < b.word.size();
        return a.word < b.word;
    });
    QStringList out;
    QSet<QString> seen;
    for (const Candidate &item : found) {
        if (seen.contains(item.word))
            continue;
        seen.insert(item.word);
        out.append(applyCase(word, item.word));
        if (out.size() == limit)
            break;
    }
    return out;
}

QVector<WordHit> Lexicon::typos(const QString &text, int caret) const
{
    ensure();
    QVector<WordHit> bad;
    bool front = false;
    bool fence = false;
    bool started = false;
    const int n = text.size();
    int line = 0;
    while (line <= n) {
        int end = text.indexOf(QLatin1Char('\n'), line);
        if (end < 0)
            end = n;
        const QString row = text.mid(line, end - line);
        const QString trimmed = row.trimmed();
        const bool fenceLine = trimmed.startsWith(QLatin1String("```")) || trimmed.startsWith(QLatin1String("~~~"));
        const bool wasFront = front;
        if (!started && (trimmed == QLatin1String("---") || trimmed.startsWith(QLatin1String("export const metadata"))))
            front = true;
        started = true;
        if (!wasFront && !front && !fence && !fenceLine) {
            bool code = false;
            for (int i = 0; i < row.size();) {
                const QChar c = row.at(i);
                if (c == QLatin1Char('`')) {
                    code = !code;
                    ++i;
                    continue;
                }
                if (code) {
                    ++i;
                    continue;
                }
                if (row.mid(i).startsWith(QLatin1String("http://")) || row.mid(i).startsWith(QLatin1String("https://"))) {
                    while (i < row.size() && !row.at(i).isSpace())
                        ++i;
                    continue;
                }
                if (!c.isLetter()) {
                    ++i;
                    continue;
                }
                int j = i + 1;
                while (j < row.size()) {
                    const QChar d = row.at(j);
                    if (d.isLetter() || d == QLatin1Char('\'') || d == QLatin1Char('-') || d == QChar(0x2019))
                        ++j;
                    else
                        break;
                }
                const int abs = line + i;
                const QString token = row.mid(i, j - i);
                const QChar prev = i > 0 ? row.at(i - 1) : QChar();
                const bool marked = prev == QLatin1Char('@') || prev == QLatin1Char('#') || prev == QLatin1Char('.')
                                    || prev == QLatin1Char('/') || prev == QLatin1Char('\\');
                if (!marked && token.size() >= 3 && token == token.toLower()) {
                    const QString lower = fold(token);
                    const bool typing = caret >= abs && caret <= abs + token.size();
                    if (!known(lower) && !(typing && extends(lower)))
                        bad.append({abs, int(token.size())});
                }
                i = j;
            }
        }
        if (wasFront) {
            if (trimmed == QLatin1String("---") || trimmed.startsWith(QLatin1String("};")) || trimmed == QLatin1String("}"))
                front = false;
        } else if (!front && fenceLine) {
            fence = !fence;
        }
        if (end >= n)
            break;
        line = end + 1;
    }
    return bad;
}
