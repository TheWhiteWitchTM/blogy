#include "post.h"

#include "bluesky.h"

#include <QDir>
#include <QRegularExpression>
#include <QTextBoundaryFinder>

namespace post {

QString slugify(const QString &title, const QString &separator)
{
    QString s = title.normalized(QString::NormalizationForm_KD).toLower();
    QString out;
    bool pendingSep = false;
    for (const QChar c : s) {
        if (c.category() == QChar::Mark_NonSpacing)
            continue;
        if ((c >= QLatin1Char('a') && c <= QLatin1Char('z')) || (c >= QLatin1Char('0') && c <= QLatin1Char('9'))) {
            if (pendingSep && !out.isEmpty())
                out += separator;
            pendingSep = false;
            out += c;
        } else if (c == QLatin1Char('\'') || c == QChar(0x2019)) {
            continue; // "don't" -> "dont"
        } else {
            pendingSep = true;
        }
    }
    if (out.size() > 80) {
        out.truncate(80);
        while (out.endsWith(separator))
            out.chop(separator.size());
    }
    return out.isEmpty() ? QStringLiteral("untitled") : out;
}

QString expand(const QString &pattern, const QDate &date, const QString &slug,
               const QString &category, const QString &title)
{
    QString s = pattern;
    s.replace(QStringLiteral("{date}"), date.toString(QStringLiteral("yyyy-MM-dd")));
    s.replace(QStringLiteral("{year}"), date.toString(QStringLiteral("yyyy")));
    s.replace(QStringLiteral("{month}"), date.toString(QStringLiteral("MM")));
    s.replace(QStringLiteral("{day}"), date.toString(QStringLiteral("dd")));
    s.replace(QStringLiteral("{slug}"), slug);
    s.replace(QStringLiteral("{category}"), category);
    s.replace(QStringLiteral("{collection}"), category);
    s.replace(QStringLiteral("{title}"), title);
    return s;
}

namespace {

// GitHub Pages' built-in Jekyll engine ignores a file in _posts unless the
// name starts with YYYY-MM-DD-. Custom collections (and MDX trees) do not.
bool needsDatePrefix(const Site &site, const Collection &c)
{
    if (site.isMdx())
        return false;
    const QString dir = QDir::cleanPath(c.dir);
    return dir == QLatin1String("_posts") || dir.endsWith(QLatin1String("/_posts"));
}

bool parsePattern(const QString &pattern, const QString &dir, const QString &relative, QDate *date, QString *slug)
{
    QString file = relative;
    if (!dir.isEmpty() && file.startsWith(dir + QLatin1Char('/')))
        file = file.mid(dir.size() + 1);
    QString re = QRegularExpression::escape(pattern);
    re.replace(QStringLiteral("\\{date\\}"), QStringLiteral("(?<date>\\d{4}-\\d{2}-\\d{2})"));
    re.replace(QStringLiteral("\\{year\\}"), QStringLiteral("(?<year>\\d{4})"));
    re.replace(QStringLiteral("\\{month\\}"), QStringLiteral("(?<month>\\d{2})"));
    re.replace(QStringLiteral("\\{day\\}"), QStringLiteral("(?<day>\\d{2})"));
    re.replace(QStringLiteral("\\{slug\\}"), QStringLiteral("(?<slug>[^/]+?)"));
    re.replace(QStringLiteral("\\{category\\}"), QStringLiteral("[^/]+"));
    re.replace(QStringLiteral("\\{collection\\}"), QStringLiteral("[^/]+"));
    const QRegularExpression rx(QLatin1Char('^') + re + QLatin1Char('$'));
    const auto m = rx.match(file);
    if (!m.hasMatch())
        return false;
    if (slug)
        *slug = m.captured(QStringLiteral("slug"));
    if (date) {
        if (!m.captured(QStringLiteral("date")).isEmpty())
            *date = QDate::fromString(m.captured(QStringLiteral("date")), QStringLiteral("yyyy-MM-dd"));
        else if (!m.captured(QStringLiteral("year")).isEmpty())
            *date = QDate(m.captured(QStringLiteral("year")).toInt(), m.captured(QStringLiteral("month")).toInt(),
                          m.captured(QStringLiteral("day")).toInt());
    }
    return true;
}

} // namespace

QString effectiveFilePattern(const Site &site, const Collection &c)
{
    const QString pattern = c.filePattern.isEmpty() ? QStringLiteral("{date}-{slug}.md") : c.filePattern;
    if (pattern.contains(QLatin1String("{date}")) || pattern.contains(QLatin1String("{year}")))
        return pattern;
    if (!needsDatePrefix(site, c))
        return pattern;
    const int slash = pattern.lastIndexOf(QLatin1Char('/'));
    const QString head = slash >= 0 ? pattern.left(slash + 1) : QString();
    const QString tail = slash >= 0 ? pattern.mid(slash + 1) : pattern;
    return head + QStringLiteral("{date}-") + tail;
}

QString relativePath(const Collection &c, const QDate &date, const QString &slug)
{
    const QString file = expand(c.filePattern, date, slug, c.name);
    return c.dir.isEmpty() ? file : c.dir + QLatin1Char('/') + file;
}

QString relativePath(const Site &site, const Collection &c, const QDate &date, const QString &slug)
{
    const QString file = expand(effectiveFilePattern(site, c), date, slug, c.name);
    return c.dir.isEmpty() ? file : c.dir + QLatin1Char('/') + file;
}

bool parsePath(const Collection &c, const QString &relative, QDate *date, QString *slug)
{
    return parsePattern(c.filePattern, c.dir, relative, date, slug);
}

bool parsePath(const Site &site, const Collection &c, const QString &relative, QDate *date, QString *slug)
{
    const QString effective = effectiveFilePattern(site, c);
    if (parsePattern(effective, c.dir, relative, date, slug))
        return true;
    if (effective != c.filePattern)
        return parsePattern(c.filePattern, c.dir, relative, date, slug);
    return false;
}

QString withSignature(const QString &text, const QString &signature)
{
    const QString sig = signature.trimmed();
    if (sig.isEmpty())
        return text;
    QString end = text;
    while (!end.isEmpty() && (end.endsWith(QLatin1Char('\n')) || end.endsWith(QLatin1Char('\r'))
                              || end.endsWith(QLatin1Char(' ')) || end.endsWith(QLatin1Char('\t'))))
        end.chop(1);
    if (end.endsWith(sig))
        return text;
    QString out = text;
    if (!out.endsWith(QLatin1Char('\n')))
        out += QLatin1Char('\n');
    if (!out.endsWith(QStringLiteral("\n\n")))
        out += QLatin1Char('\n');
    out += sig;
    if (!out.endsWith(QLatin1Char('\n')))
        out += QLatin1Char('\n');
    return out;
}

QString plainSentence(const QString &in)
{
    QString s = in;
    s.replace(QRegularExpression(QStringLiteral(R"(!?\[[^\]]*\]\([^)]*\))")), QString());
    s.remove(QRegularExpression(QStringLiteral("[*_`~>#]")));
    s.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return s.trimmed();
}

QString takeGraphemes(const QString &text, int count)
{
    if (count <= 0 || text.isEmpty())
        return {};
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    int end = 0;
    for (int i = 0; i < count; ++i) {
        const int next = finder.toNextBoundary();
        if (next < 0)
            return text;
        end = next;
    }
    return text.left(end);
}

QString excerpt(const QString &markdown)
{
    QString body;
    const FrontMatter fm = FrontMatter::parse(markdown, &body);
    const QString desc = fm.string(QStringLiteral("description")).trimmed();
    if (!desc.isEmpty())
        return desc;
    QString para;
    const QStringList lines = body.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            if (!para.isEmpty())
                break;
            continue;
        }
        if (trimmed.startsWith(QLatin1Char('#')) || trimmed.startsWith(QLatin1String("```"))
            || trimmed.startsWith(QLatin1String("~~~")) || trimmed == QLatin1String("---")
            || trimmed.startsWith(QLatin1String("export ")))
            continue;
        QString piece = trimmed;
        if (piece.startsWith(QLatin1String("> ")))
            piece = piece.mid(2);
        if (!para.isEmpty())
            para += QLatin1Char(' ');
        para += piece;
    }
    return plainSentence(para);
}

QString blueskyAnnouncement(const QString &leadIn, const QString &url, int limit)
{
    const QString link = url.trimmed();
    QString lead = leadIn.trimmed();
    lead.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    if (link.isEmpty()) {
        if (BlueskyClient::graphemeLength(lead) <= limit)
            return lead;
        return takeGraphemes(lead, qMax(1, limit - 1)).trimmed() + QStringLiteral("…");
    }
    if (lead.isEmpty())
        return link;
    const QString full = lead + QStringLiteral("\n\n") + link;
    if (BlueskyClient::graphemeLength(full) <= limit)
        return full;
    const int room = limit - BlueskyClient::graphemeLength(link) - 2;
    if (room <= 1)
        return link;
    QString cut = takeGraphemes(lead, room - 1).trimmed();
    while (!cut.isEmpty() && QStringLiteral(".,;: ").contains(cut.back()))
        cut.chop(1);
    return cut + QStringLiteral("…\n\n") + link;
}

QString publicUrl(const Site &site, const Collection &c, const QDate &date, const QString &slug)
{
    QString base = site.url;
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    QString path = expand(c.urlPattern, date, slug, c.name);
    if (!path.startsWith(QLatin1Char('/')))
        path.prepend(QLatin1Char('/'));
    return base + path;
}

namespace {
// QJsonObject sorts keys, so put front matter back in the order the sites use.
FrontMatter ordered(const FrontMatter &in)
{
    static const QStringList order = {QStringLiteral("layout"), QStringLiteral("title"), QStringLiteral("date"),
                                      QStringLiteral("featured"), QStringLiteral("highlight"), QStringLiteral("highlights"),
                                      QStringLiteral("description"), QStringLiteral("tags"), QStringLiteral("categories"),
                                      QStringLiteral("image"), QStringLiteral("video")};
    FrontMatter out;
    for (const QString &key : order) {
        if (in.contains(key))
            out.set(key, in.value(key));
    }
    for (const auto &f : in.fields) {
        if (!out.contains(f.first))
            out.set(f.first, f.second);
    }
    return out;
}
} // namespace

FrontMatter defaultFrontMatter(const Site &site, const Collection &c, const QString &title, const QDate &date)
{
    FrontMatter fm = FrontMatter::fromJson(site.frontMatter);
    const FrontMatter extra = FrontMatter::fromJson(c.frontMatter);
    for (const auto &f : extra.fields)
        fm.set(f.first, f.second);
    for (auto &f : fm.fields) {
        if (f.second.typeId() == QMetaType::QString)
            f.second = expand(f.second.toString(), date, QString(), c.name, title);
    }
    fm.set(QStringLiteral("title"), title);
    if (!site.isMdx() && !site.layout.isEmpty())
        fm.set(QStringLiteral("layout"), site.layout);
    if (!site.isMdx() && !fm.contains(QStringLiteral("date")))
        fm.set(QStringLiteral("date"), date.toString(QStringLiteral("yyyy-MM-dd")));
    return ordered(fm);
}

QString newPostText(const Site &site, const Collection &c, const QString &title, const QDate &date,
                    const QStringList &tags, const QString &description)
{
    FrontMatter fm = defaultFrontMatter(site, c, title, date);
    if (!tags.isEmpty() || fm.contains(QStringLiteral("tags")))
        fm.set(QStringLiteral("tags"), tags);
    if (!description.isEmpty() || fm.contains(QStringLiteral("description")))
        fm.set(QStringLiteral("description"), description);
    QString body = Site::isStockHeadingTemplate(site.templateText) ? site.headingTemplate() : site.templateText;
    body = expand(body, date, QString(), c.name, title);
    return fm.compose(body, site.isMdx());
}

const Collection *collectionFor(const Site &site, const QString &absolutePath, QString *relative)
{
    const QString root = QDir::cleanPath(site.root());
    const QString path = QDir::cleanPath(absolutePath);
    if (!path.startsWith(root + QLatin1Char('/')))
        return nullptr;
    const QString rel = path.mid(root.size() + 1);
    if (relative)
        *relative = rel;
    const Collection *best = nullptr;
    for (const Collection &c : site.collections) {
        if (rel.startsWith(c.dir + QLatin1Char('/')) && (!best || c.dir.size() > best->dir.size()))
            best = &c;
    }
    return best;
}

} // namespace post
