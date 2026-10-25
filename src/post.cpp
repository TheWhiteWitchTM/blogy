#include "post.h"

#include <QDir>
#include <QRegularExpression>

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

QString relativePath(const Collection &c, const QDate &date, const QString &slug)
{
    const QString file = expand(c.filePattern, date, slug, c.name);
    return c.dir.isEmpty() ? file : c.dir + QLatin1Char('/') + file;
}

bool parsePath(const Collection &c, const QString &relative, QDate *date, QString *slug)
{
    QString file = relative;
    if (!c.dir.isEmpty() && file.startsWith(c.dir + QLatin1Char('/')))
        file = file.mid(c.dir.size() + 1);
    QString re = QRegularExpression::escape(c.filePattern);
    re.replace(QStringLiteral("\\{date\\}"), QStringLiteral("(?<date>\\d{4}-\\d{2}-\\d{2})"));
    re.replace(QStringLiteral("\\{year\\}"), QStringLiteral("(?<year>\\d{4})"));
    re.replace(QStringLiteral("\\{month\\}"), QStringLiteral("(?<month>\\d{2})"));
    re.replace(QStringLiteral("\\{day\\}"), QStringLiteral("(?<day>\\d{2})"));
    re.replace(QStringLiteral("\\{slug\\}"), QStringLiteral("(?<slug>[^/]+?)"));
    re.replace(QStringLiteral("\\{category\\}"), QStringLiteral("[^/]+"));
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
    QString body = site.templateText.isEmpty() ? QStringLiteral("## {title}\n\n") : site.templateText;
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
