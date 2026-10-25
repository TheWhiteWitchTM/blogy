#include "highlighter.h"

#include <QColor>
#include <QFont>

namespace {
QTextCharFormat fmt(const char *color, bool bold = false, bool italic = false)
{
    QTextCharFormat f;
    f.setForeground(QColor(QString::fromLatin1(color)));
    if (bold)
        f.setFontWeight(QFont::Bold);
    if (italic)
        f.setFontItalic(true);
    return f;
}

enum State { Normal = 0, FrontMatter = 1, Fence = 2, AfterFront = 3 };
} // namespace

Highlighter::Highlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    heading_ = fmt("#ffb454", true);
    front_ = fmt("#9a8f80");
    frontKey_ = fmt("#d2a6ff");
    fence_ = fmt("#7fd962");
    code_ = fmt("#7fd962");
    quote_ = fmt("#8a8a8a", false, true);

    rules_.append({QRegularExpression(QStringLiteral(R"(^\s*([-*+]|\d+\.)\s)")), fmt("#ffb454", true), 0});
    rules_.append({QRegularExpression(QStringLiteral(R"((\*\*|__)(?=\S)(.+?)(?<=\S)\1)")), fmt("#fff6e4", true), 0});
    rules_.append({QRegularExpression(QStringLiteral(R"((?<![*\w])\*(?=\S)([^*]+?)(?<=\S)\*(?!\*)|(?<!\w)_(?=\S)([^_]+?)(?<=\S)_(?!\w))")),
                   fmt("#e8d5b5", false, true), 0});
    rules_.append({QRegularExpression(QStringLiteral(R"(~~.+?~~)")), fmt("#6a6a6a"), 0});
    rules_.append({QRegularExpression(QStringLiteral(R"(!?\[[^\]]*\]\([^)]*\))")), fmt("#59c2ff"), 0});
    rules_.append({QRegularExpression(QStringLiteral(R"(<https?://[^>]+>|https?://\S+)")), fmt("#59c2ff"), 0});
    rules_.append({QRegularExpression(QStringLiteral(R"(\{%.*?%\}|\{\{.*?\}\})")), fmt("#d2a6ff"), 0});
    rules_.append({QRegularExpression(QStringLiteral(R"(<[^>\n]+>)")), fmt("#95e6cb"), 0});
    rules_.append({QRegularExpression(QStringLiteral(R"(`[^`]+`)")), code_, 0});
}

void Highlighter::highlightBlock(const QString &text)
{
    const int prev = previousBlockState();
    const QString trimmed = text.trimmed();

    // Front matter only counts at the very first line.
    if (currentBlock().blockNumber() == 0 && (trimmed == QLatin1String("---") || trimmed.startsWith(QLatin1String("export const metadata")))) {
        setFormat(0, text.size(), front_);
        setCurrentBlockState(FrontMatter);
        return;
    }
    if (prev == FrontMatter) {
        if (trimmed == QLatin1String("---") || trimmed.startsWith(QLatin1String("};")) || trimmed == QLatin1String("}")) {
            setFormat(0, text.size(), front_);
            setCurrentBlockState(AfterFront);
            return;
        }
        setFormat(0, text.size(), front_);
        const int colon = text.indexOf(QLatin1Char(':'));
        if (colon > 0)
            setFormat(0, colon, frontKey_);
        setCurrentBlockState(FrontMatter);
        return;
    }
    if (trimmed.startsWith(QLatin1String("```")) || trimmed.startsWith(QLatin1String("~~~"))) {
        setFormat(0, text.size(), fence_);
        setCurrentBlockState(prev == Fence ? Normal : Fence);
        return;
    }
    if (prev == Fence) {
        setFormat(0, text.size(), code_);
        setCurrentBlockState(Fence);
        return;
    }
    setCurrentBlockState(Normal);

    if (trimmed.startsWith(QLatin1Char('#'))) {
        setFormat(0, text.size(), heading_);
        return;
    }
    if (trimmed.startsWith(QLatin1Char('>'))) {
        setFormat(0, text.size(), quote_);
    }
    if (trimmed == QLatin1String("---") || trimmed == QLatin1String("***")) {
        setFormat(0, text.size(), front_);
        return;
    }
    if (text.startsWith(QLatin1String("    ")) || text.startsWith(QLatin1Char('\t'))) {
        if (!trimmed.startsWith(QLatin1Char('-')) && !trimmed.startsWith(QLatin1Char('*'))) {
            setFormat(0, text.size(), code_);
            return;
        }
    }
    for (const Rule &rule : rules_) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            setFormat(m.capturedStart(rule.group), m.capturedLength(rule.group), rule.format);
        }
    }
}
