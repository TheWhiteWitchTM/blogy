#include "media.h"

#include "frontmatter.h"
#include "post.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace media {

namespace {

struct Origin {
    QString origin; // https://host
    QString base;   // /whitewitch, or empty
};

Origin splitSite(const QString &url)
{
    const QUrl u(url);
    Origin o;
    if (u.isValid() && !u.host().isEmpty())
        o.origin = u.scheme() + QStringLiteral("://") + u.authority();
    o.base = u.path();
    if (o.base == QLatin1String("/"))
        o.base.clear();
    while (o.base.endsWith(QLatin1Char('/')))
        o.base.chop(1);
    return o;
}

QString assetStem(const QString &postFile)
{
    const QFileInfo info(postFile);
    const QString base = info.completeBaseName();
    if (base == QLatin1String("page") || base == QLatin1String("index"))
        return info.dir().dirName();
    return base.isEmpty() ? QStringLiteral("image") : base;
}

QString liquidTag(const QString &path)
{
    if (path.contains(QLatin1Char('\'')))
        return QStringLiteral("{{ \"") + path + QStringLiteral("\" | relative_url }}");
    return QStringLiteral("{{ '") + path + QStringLiteral("' | relative_url }}");
}

QString applyPattern(QString pattern, const QString &stem, const QString &slug, const QString &date,
                     const QString &ext, int index)
{
    if (pattern.isEmpty() || pattern.contains(QLatin1String("..")))
        pattern = QStringLiteral("{stem}{ext}");
    const bool hadIndex = pattern.contains(QLatin1String("{n}"));
    pattern.replace(QStringLiteral("{stem}"), stem);
    pattern.replace(QStringLiteral("{slug}"), slug);
    pattern.replace(QStringLiteral("{date}"), date);
    pattern.replace(QStringLiteral("{ext}"), ext);
    pattern.replace(QStringLiteral("{n}"), index <= 1 ? QString() : QString::number(index));
    if (index > 1 && !hadIndex) {
        if (!ext.isEmpty() && pattern.endsWith(ext))
            pattern.insert(pattern.size() - ext.size(), QLatin1Char('-') + QString::number(index));
        else
            pattern += QLatin1Char('-') + QString::number(index);
    }
    return pattern;
}

QList<QPair<int, int>> codeRanges(const QString &body)
{
    QList<QPair<int, int>> ranges;
    int i = 0;
    int open = -1;
    while (i < body.size()) {
        int lineEnd = body.indexOf(QLatin1Char('\n'), i);
        if (lineEnd < 0)
            lineEnd = body.size();
        const QString line = body.mid(i, lineEnd - i).trimmed();
        if (line.startsWith(QLatin1String("```")) || line.startsWith(QLatin1String("~~~"))) {
            if (open < 0)
                open = i;
            else {
                ranges.append({open, lineEnd});
                open = -1;
            }
        }
        if (lineEnd >= body.size())
            break;
        i = lineEnd + 1;
    }
    return ranges;
}

bool inside(const QList<QPair<int, int>> &ranges, qsizetype pos)
{
    for (const auto &r : ranges) {
        if (pos >= r.first && pos < r.second)
            return true;
    }
    return false;
}

QString resolveLocal(const QString &target, const QString &postDir)
{
    QString t = target.trimmed();
    if (t.isEmpty() || t.contains(QLatin1String("{{")) || t.startsWith(QLatin1Char('#')))
        return {};
    if (t.startsWith(QLatin1String("http://")) || t.startsWith(QLatin1String("https://")))
        return {};
    if (t.startsWith(QLatin1Char('<')) && t.endsWith(QLatin1Char('>')) && t.size() > 2)
        t = t.mid(1, t.size() - 2);
    if (t.startsWith(QLatin1String("file:")))
        return QUrl(t).toLocalFile();
    if (t.startsWith(QLatin1String("~/")))
        t = QDir::homePath() + t.mid(1);
    else if (t == QLatin1String("~"))
        t = QDir::homePath();
    QFileInfo info(t);
    if (!info.isAbsolute())
        info = QFileInfo(postDir + QLatin1Char('/') + t);
    if (info.exists() && info.isFile())
        return info.absoluteFilePath();
    return {};
}

QString imagePublicPath(const QString &postUrl, const QString &fileName)
{
    QString path = QUrl(postUrl).path();
    if (path.endsWith(QLatin1String(".html")) || path.endsWith(QLatin1String(".htm"))) {
        const int slash = path.lastIndexOf(QLatin1Char('/'));
        return path.left(slash + 1) + fileName;
    }
    if (!path.endsWith(QLatin1Char('/')))
        path += QLatin1Char('/');
    return path + fileName;
}

bool copyFile(const QString &from, const QString &to)
{
    const QString src = QFileInfo(from).absoluteFilePath();
    const QString dest = QFileInfo(to).absoluteFilePath();
    if (src == dest)
        return true;
    QDir().mkpath(QFileInfo(dest).absolutePath());
    if (QFileInfo::exists(dest) && !QFile::remove(dest))
        return false;
    return QFile::copy(src, dest);
}

QString relativeTo(const QString &root, const QString &absolute)
{
    const QString base = QDir::cleanPath(root);
    const QString path = QDir::cleanPath(absolute);
    const QString prefix = base + QLatin1Char('/');
    if (path.startsWith(prefix))
        return path.mid(prefix.size());
    return {};
}

QString findLocalImage(const QString &url, const QString &postDir, const QString &siteRoot, const QString &siteUrl)
{
    if (url.startsWith(QLatin1String("file:"))) {
        const QString local = QUrl(url).toLocalFile();
        if (QFileInfo::exists(local))
            return local;
    }
    const QString direct = resolveLocal(url, postDir);
    if (!direct.isEmpty())
        return direct;

    QString path = url;
    if (!siteUrl.isEmpty() && path.startsWith(siteUrl))
        path = path.mid(siteUrl.size());
    static const QRegularExpression liquid(QStringLiteral(R"(\{\{\s*['\"]([^'\"]+)['\"]\s*\|\s*relative_url\s*\}\})"));
    const auto m = liquid.match(path);
    if (m.hasMatch())
        path = m.captured(1);
    if (!path.startsWith(QLatin1Char('/')))
        return {};
    const Origin origin = splitSite(siteUrl);
    QString rel = path;
    if (!origin.base.isEmpty() && rel.startsWith(origin.base + QLatin1Char('/')))
        rel = rel.mid(origin.base.size());
    if (rel.startsWith(QLatin1Char('/')))
        rel = rel.mid(1);
    const QString name = QFileInfo(rel).fileName();
    const QStringList candidates = {
        siteRoot + QLatin1Char('/') + rel,
        siteRoot + QStringLiteral("/public/") + rel,
        postDir + QLatin1Char('/') + name,
    };
    for (const QString &c : candidates) {
        if (!c.isEmpty() && QFileInfo::exists(c))
            return QFileInfo(c).absoluteFilePath();
    }
    return {};
}

} // namespace

PublishFix fixOnPublish(const QString &markdown, const QString &postFile, const Site &site)
{
    PublishFix out;
    out.markdown = markdown;
    if (postFile.isEmpty())
        return out;

    QString body;
    FrontMatter fm = FrontMatter::parse(markdown, &body);
    const QString postDir = QFileInfo(postFile).absolutePath();
    const Origin origin = splitSite(site.url);
    QString relPath;
    const Collection *collection = post::collectionFor(site, postFile, &relPath);
    QDate date;
    QString slug;
    QString postUrl;
    if (collection && post::parsePath(site, *collection, relPath, &date, &slug)) {
        if (!date.isValid())
            date = QDate::fromString(fm.string(QStringLiteral("date")).left(10), QStringLiteral("yyyy-MM-dd"));
        if (!date.isValid())
            date = QDate::currentDate();
        if (slug.isEmpty())
            slug = assetStem(postFile);
        postUrl = post::publicUrl(site, *collection, date, slug);
    }

    const QList<QPair<int, int>> fences = codeRanges(body);
    static const QRegularExpression linkRx(QStringLiteral(R"((!?)\[([^\]\n]*)\]\(\s*<?([^>\s)]+)>?((?:\s+"[^"]*")?)\))"));
    struct Edit {
        qsizetype start = 0;
        qsizetype length = 0;
        QString text;
    };
    QList<Edit> edits;
    QStringList notes;
    QString firstImageFront;
    int imageIndex = 0;
    bool setSize = false;
    int imageWidth = 0;
    int imageHeight = 0;

    auto it = linkRx.globalMatch(body);
    while (it.hasNext()) {
        const auto m = it.next();
        if (inside(fences, m.capturedStart()))
            continue;
        const bool isImage = !m.captured(1).isEmpty();
        const QString url = m.captured(3);
        const QString local = resolveLocal(url, postDir);

        if (isImage) {
            if (local.isEmpty())
                continue;
            ++imageIndex;
            const QString ext = QLatin1Char('.') + QFileInfo(local).suffix().toLower();
            const QString stem = assetStem(postFile);
            const QString fileName = applyPattern(site.imagePattern, stem, slug.isEmpty() ? stem : slug,
                                                  date.isValid() ? date.toString(QStringLiteral("yyyy-MM-dd")) : QString(),
                                                  ext == QLatin1String(".") ? QStringLiteral(".png") : ext, imageIndex);
            const QString beside = QDir::cleanPath(postDir + QLatin1Char('/') + fileName);
            QString markdownTarget = QFileInfo(fileName).fileName();
            QString frontPath;
            QString served;
            if (!postUrl.isEmpty()) {
                const QString publicPath = imagePublicPath(postUrl, QFileInfo(fileName).fileName());
                QString liquidPath = publicPath;
                if (!origin.base.isEmpty() && liquidPath.startsWith(origin.base + QLatin1Char('/')))
                    liquidPath = liquidPath.mid(origin.base.size());
                if (!liquidPath.startsWith(QLatin1Char('/')))
                    liquidPath.prepend(QLatin1Char('/'));
                QString servedRel = liquidPath.mid(1);
                if (site.isMdx())
                    served = QDir::cleanPath(site.root() + QStringLiteral("/public/") + servedRel);
                else
                    served = QDir::cleanPath(site.root() + QLatin1Char('/') + servedRel);
                markdownTarget = site.isMdx() ? publicPath : liquidTag(liquidPath);
                frontPath = site.isMdx() ? publicPath : liquidPath;
            }
            if (!copyFile(local, beside)) {
                notes << QStringLiteral("Could not copy %1 next to the post.").arg(QFileInfo(local).fileName());
                continue;
            }
            if (!served.isEmpty() && QDir::cleanPath(served) != QDir::cleanPath(beside)) {
                if (!copyFile(local, served))
                    notes << QStringLiteral("Could not copy %1 to %2.").arg(QFileInfo(local).fileName(), served);
            }
            if (const QString rel = relativeTo(site.root(), beside); !rel.isEmpty() && !out.gitPaths.contains(rel))
                out.gitPaths << rel;
            if (!served.isEmpty()) {
                if (const QString rel = relativeTo(site.root(), served); !rel.isEmpty() && !out.gitPaths.contains(rel))
                    out.gitPaths << rel;
            }
            edits.append({m.capturedStart(3), m.capturedLength(3), markdownTarget});
            notes << QStringLiteral("Image %1 → %2").arg(QFileInfo(local).fileName(), QFileInfo(beside).fileName());
            if (firstImageFront.isEmpty())
                firstImageFront = frontPath.isEmpty() ? markdownTarget : frontPath;
            if (!setSize) {
                const QSize size = QImageReader(local).size();
                if (size.isValid()) {
                    imageWidth = size.width();
                    imageHeight = size.height();
                    setSize = true;
                }
            }
            continue;
        }

        if (!local.isEmpty() && collection) {
            QString linkRel;
            const Collection *linkCollection = post::collectionFor(site, local, &linkRel);
            if (!linkCollection)
                continue;
            QDate linkDate;
            QString linkSlug;
            if (!post::parsePath(site, *linkCollection, linkRel, &linkDate, &linkSlug) || linkSlug.isEmpty())
                continue;
            if (!linkDate.isValid())
                linkDate = QDate::currentDate();
            edits.append({m.capturedStart(3), m.capturedLength(3), post::publicUrl(site, *linkCollection, linkDate, linkSlug)});
            notes << QStringLiteral("Link %1 → %2").arg(QFileInfo(local).fileName(), edits.last().text);
            continue;
        }

        if (url.contains(QLatin1String("{{")) || url.startsWith(QLatin1String("http")) || url.startsWith(QLatin1Char('#')))
            continue;
        if (url.startsWith(QLatin1Char('/')) && !url.startsWith(QLatin1String("//")) && !origin.base.isEmpty()) {
            if (url == origin.base || url.startsWith(origin.base + QLatin1Char('/')))
                continue;
            if (site.isMdx())
                edits.append({m.capturedStart(3), m.capturedLength(3), origin.base + url});
            else
                edits.append({m.capturedStart(3), m.capturedLength(3), liquidTag(url)});
        }
    }

    if (edits.isEmpty()) {
        notes.removeDuplicates();
        out.note = notes.join(QLatin1Char('\n'));
        return out;
    }

    std::sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) { return a.start > b.start; });
    for (const Edit &e : edits)
        body.replace(e.start, e.length, e.text);

    if (!firstImageFront.isEmpty() && !firstImageFront.contains(QLatin1String("{{"))) {
        fm.set(QStringLiteral("image"), firstImageFront);
        if (setSize) {
            fm.set(QStringLiteral("image_width"), QString::number(imageWidth));
            fm.set(QStringLiteral("image_height"), QString::number(imageHeight));
        }
    }
    out.markdown = fm.isEmpty() ? body : fm.compose(body, site.isMdx() || postFile.endsWith(QLatin1String(".mdx")));
    notes.removeDuplicates();
    out.note = notes.join(QLatin1Char('\n'));
    return out;
}

QString previewImages(const QString &markdown, const QString &postDir, const QString &siteRoot, const QString &siteUrl)
{
    if (markdown.isEmpty())
        return markdown;
    // Liquid destinations contain spaces, so they are matched as a whole {{ ... }} block.
    static const QRegularExpression imageRx(
        QStringLiteral(R"(!\[([^\]\n]*)\]\(\s*((?:\{\{.+?\}\})|(?:<[^>]+>)|(?:[^)\s]+))\s*\))"));
    struct Span {
        qsizetype start = 0;
        qsizetype length = 0;
        QString text;
    };
    QList<Span> spans;
    auto it = imageRx.globalMatch(markdown);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString url = m.captured(2);
        if (url.startsWith(QLatin1String("file:")))
            continue;
        const QString local = findLocalImage(url, postDir, siteRoot, siteUrl);
        if (local.isEmpty())
            continue;
        spans.append({m.capturedStart(2), m.capturedLength(2), QUrl::fromLocalFile(local).toString()});
    }
    std::sort(spans.begin(), spans.end(), [](const Span &a, const Span &b) { return a.start > b.start; });
    QString body = markdown;
    for (const Span &s : spans)
        body.replace(s.start, s.length, s.text);
    return body;
}

} // namespace media
