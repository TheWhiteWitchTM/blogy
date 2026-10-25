#include "highlighter.h"

#include <QColor>
#include <QFont>
#include <QSet>

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

enum State { Normal = 0, FrontMatter = 1, Fence = 2, AfterFront = 3, FenceBody = 4 };
constexpr int kLangShift = 8;
constexpr int kComment = 1 << 16;

enum Lang { LangPlain = 0, LangC, LangPy, LangSh, LangSql, LangCss, LangHtml, LangJson, LangYaml };

int languageId(const QString &tag)
{
    const QString t = tag.toLower();
    if (t == QLatin1String("py") || t == QLatin1String("python") || t == QLatin1String("ruby") || t == QLatin1String("rb")
        || t == QLatin1String("lua"))
        return LangPy;
    if (t == QLatin1String("bash") || t == QLatin1String("sh") || t == QLatin1String("zsh") || t == QLatin1String("shell"))
        return LangSh;
    if (t == QLatin1String("sql"))
        return LangSql;
    if (t == QLatin1String("css") || t == QLatin1String("scss"))
        return LangCss;
    if (t == QLatin1String("html") || t == QLatin1String("htm") || t == QLatin1String("xml") || t == QLatin1String("svg"))
        return LangHtml;
    if (t == QLatin1String("json"))
        return LangJson;
    if (t == QLatin1String("yaml") || t == QLatin1String("yml"))
        return LangYaml;
    if (t == QLatin1String("md") || t == QLatin1String("markdown") || t.isEmpty() || t == QLatin1String("text")
        || t == QLatin1String("plain"))
        return LangPlain;
    return LangC;
}

const QSet<QString> &keywords(int lang)
{
    static const QSet<QString> c = {QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("for"),
                                    QStringLiteral("while"), QStringLiteral("return"), QStringLiteral("break"),
                                    QStringLiteral("continue"), QStringLiteral("switch"), QStringLiteral("case"),
                                    QStringLiteral("default"), QStringLiteral("class"), QStringLiteral("struct"),
                                    QStringLiteral("enum"), QStringLiteral("public"), QStringLiteral("private"),
                                    QStringLiteral("protected"), QStringLiteral("static"), QStringLiteral("void"),
                                    QStringLiteral("int"), QStringLiteral("bool"), QStringLiteral("true"),
                                    QStringLiteral("false"), QStringLiteral("null"), QStringLiteral("nil"),
                                    QStringLiteral("new"), QStringLiteral("this"), QStringLiteral("import"),
                                    QStringLiteral("export"), QStringLiteral("from"), QStringLiteral("function"),
                                    QStringLiteral("func"), QStringLiteral("fn"), QStringLiteral("let"),
                                    QStringLiteral("var"), QStringLiteral("const"), QStringLiteral("async"),
                                    QStringLiteral("await"), QStringLiteral("package"), QStringLiteral("interface"),
                                    QStringLiteral("type"), QStringLiteral("yield"), QStringLiteral("try"),
                                    QStringLiteral("catch"), QStringLiteral("finally"), QStringLiteral("throw"),
                                    QStringLiteral("namespace"), QStringLiteral("template"), QStringLiteral("using"),
                                    QStringLiteral("pub"), QStringLiteral("impl"), QStringLiteral("trait"),
                                    QStringLiteral("match"), QStringLiteral("loop"), QStringLiteral("mut"),
                                    QStringLiteral("crate"), QStringLiteral("where"), QStringLiteral("typeof"),
                                    QStringLiteral("undefined")};
    static const QSet<QString> py = {QStringLiteral("def"), QStringLiteral("class"), QStringLiteral("if"),
                                     QStringLiteral("elif"), QStringLiteral("else"), QStringLiteral("for"),
                                     QStringLiteral("while"), QStringLiteral("return"), QStringLiteral("import"),
                                     QStringLiteral("from"), QStringLiteral("as"), QStringLiteral("with"),
                                     QStringLiteral("try"), QStringLiteral("except"), QStringLiteral("finally"),
                                     QStringLiteral("lambda"), QStringLiteral("pass"), QStringLiteral("yield"),
                                     QStringLiteral("True"), QStringLiteral("False"), QStringLiteral("None"),
                                     QStringLiteral("and"), QStringLiteral("or"), QStringLiteral("not"),
                                     QStringLiteral("in"), QStringLiteral("is"), QStringLiteral("async"),
                                     QStringLiteral("await"), QStringLiteral("raise"), QStringLiteral("global"),
                                     QStringLiteral("lambda")};
    static const QSet<QString> sh = {QStringLiteral("if"), QStringLiteral("then"), QStringLiteral("else"),
                                     QStringLiteral("fi"), QStringLiteral("for"), QStringLiteral("while"),
                                     QStringLiteral("do"), QStringLiteral("done"), QStringLiteral("case"),
                                     QStringLiteral("esac"), QStringLiteral("function"), QStringLiteral("echo"),
                                     QStringLiteral("export"), QStringLiteral("local"), QStringLiteral("return"),
                                     QStringLiteral("in")};
    static const QSet<QString> sql = {QStringLiteral("select"), QStringLiteral("from"), QStringLiteral("where"),
                                      QStringLiteral("and"), QStringLiteral("or"), QStringLiteral("insert"),
                                      QStringLiteral("into"), QStringLiteral("values"), QStringLiteral("update"),
                                      QStringLiteral("set"), QStringLiteral("delete"), QStringLiteral("create"),
                                      QStringLiteral("table"), QStringLiteral("join"), QStringLiteral("left"),
                                      QStringLiteral("right"), QStringLiteral("inner"), QStringLiteral("on"),
                                      QStringLiteral("group"), QStringLiteral("by"), QStringLiteral("order"),
                                      QStringLiteral("limit"), QStringLiteral("as"), QStringLiteral("null"),
                                      QStringLiteral("not"), QStringLiteral("primary"), QStringLiteral("key")};
    static const QSet<QString> none;
    switch (lang) {
    case LangPy: return py;
    case LangSh: return sh;
    case LangSql: return sql;
    case LangC: return c;
    default: return none;
    }
}

bool wordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}
} // namespace

Highlighter::Highlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    heading_ = fmt("#ffb454", true);
    front_ = fmt("#9a8f80");
    frontKey_ = fmt("#d2a6ff");
    fence_ = fmt("#7fd962");
    code_ = fmt("#9ddeaf");
    quote_ = fmt("#8a8a8a", false, true);
    keyword_ = fmt("#d2a6ff");
    string_ = fmt("#ffb454");
    comment_ = fmt("#6a6a6a", false, true);
    number_ = fmt("#59c2ff");

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

void Highlighter::paintCode(const QString &text, int lang, bool &inComment)
{
    setFormat(0, text.size(), code_);
    const QSet<QString> &words = keywords(lang);
    const bool hashComment = lang == LangPy || lang == LangSh || lang == LangYaml || lang == LangPlain;
    const bool slashComment = lang != LangPy && lang != LangYaml;
    for (int i = 0; i < text.size();) {
        if (inComment) {
            const int end = text.indexOf(QLatin1String("*/"), i);
            if (end < 0) {
                setFormat(i, text.size() - i, comment_);
                return;
            }
            setFormat(i, end + 2 - i, comment_);
            i = end + 2;
            inComment = false;
            continue;
        }
        const QChar c = text.at(i);
        if (hashComment && c == QLatin1Char('#')) {
            setFormat(i, text.size() - i, comment_);
            return;
        }
        if (lang == LangC && c == QLatin1Char('#') && text.left(i).trimmed().isEmpty()) {
            setFormat(i, text.size() - i, keyword_);
            return;
        }
        if (slashComment && c == QLatin1Char('/') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('/')) {
            setFormat(i, text.size() - i, comment_);
            return;
        }
        if (slashComment && c == QLatin1Char('/') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('*')) {
            inComment = true;
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'') || (c == QLatin1Char('`') && lang == LangC)) {
            const QChar quote = c;
            int j = i + 1;
            while (j < text.size()) {
                if (text.at(j) == QLatin1Char('\\') && j + 1 < text.size()) {
                    j += 2;
                    continue;
                }
                if (text.at(j) == quote) {
                    ++j;
                    break;
                }
                ++j;
            }
            setFormat(i, j - i, string_);
            i = j;
            continue;
        }
        if (c.isDigit() || (lang == LangCss && c == QLatin1Char('#') && i + 1 < text.size() && text.at(i + 1).isLetterOrNumber())) {
            int j = i + 1;
            while (j < text.size() && (text.at(j).isLetterOrNumber() || text.at(j) == QLatin1Char('.')))
                ++j;
            setFormat(i, j - i, number_);
            i = j;
            continue;
        }
        if (lang == LangHtml && c == QLatin1Char('<')) {
            int j = text.indexOf(QLatin1Char('>'), i + 1);
            if (j < 0)
                j = text.size() - 1;
            setFormat(i, j - i + 1, keyword_);
            i = j + 1;
            continue;
        }
        if (c.isLetter() || c == QLatin1Char('_')) {
            int j = i + 1;
            while (j < text.size() && wordChar(text.at(j)))
                ++j;
            const QString token = text.mid(i, j - i);
            if (words.contains(token) || (lang != LangPy && words.contains(token.toLower()))
                || (lang == LangJson && (token == QLatin1String("true") || token == QLatin1String("false") || token == QLatin1String("null")))
                || (lang == LangYaml && j < text.size() && text.at(j) == QLatin1Char(':')))
                setFormat(i, j - i, keyword_);
            i = j;
            continue;
        }
        ++i;
    }
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
    const int prevState = prev < 0 ? Normal : (prev & 0xff);
    const bool inFence = prevState == FenceBody;
    if (trimmed.startsWith(QLatin1String("```")) || trimmed.startsWith(QLatin1String("~~~"))) {
        setFormat(0, text.size(), fence_);
        if (inFence) {
            setCurrentBlockState(Normal);
            return;
        }
        static const QRegularExpression langRe(QStringLiteral("^(?:```+|~~~+)\\s*([A-Za-z0-9_+#.-]*)"));
        const QString tag = langRe.match(trimmed).captured(1);
        if (!tag.isEmpty()) {
            const int at = text.indexOf(tag);
            if (at >= 0)
                setFormat(at, tag.size(), string_);
        }
        setCurrentBlockState(FenceBody | (languageId(tag) << kLangShift));
        return;
    }
    if (inFence) {
        bool comment = prev >= 0 && (prev & kComment);
        const int lang = prev < 0 ? LangPlain : (prev >> kLangShift) & 0xff;
        paintCode(text, lang, comment);
        int state = FenceBody | (lang << kLangShift);
        if (comment)
            state |= kComment;
        setCurrentBlockState(state);
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
