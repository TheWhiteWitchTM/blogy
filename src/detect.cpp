#include "detect.h"

#include "frontmatter.h"
#include "git.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QProcess>
#include <QRegularExpression>
#include <QVector>

#include <algorithm>

namespace {

QString yamlBare(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2) {
        const QChar a = value.front();
        const QChar b = value.back();
        if ((a == QLatin1Char('"') && b == QLatin1Char('"')) || (a == QLatin1Char('\'') && b == QLatin1Char('\'')))
            value = value.mid(1, value.size() - 2).trimmed();
    }
    const int hash = value.indexOf(QLatin1String(" #"));
    if (hash >= 0)
        value = value.left(hash).trimmed();
    return value;
}

QString joinUrl(QString url, QString base)
{
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    base = base.trimmed();
    if (base.isEmpty() || base == QLatin1String("/"))
        return url;
    if (!base.startsWith(QLatin1Char('/')))
        base.prepend(QLatin1Char('/'));
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    if (url.endsWith(base))
        return url;
    return url + base;
}

QString withTilde(const QString &path)
{
    const QString home = QDir::homePath();
    const QString clean = QDir::cleanPath(path);
    if (clean == home)
        return QStringLiteral("~");
    if (clean.startsWith(home + QLatin1Char('/')))
        return QLatin1Char('~') + clean.mid(home.size());
    return clean;
}

QString gitLine(const QString &repo, const QStringList &args)
{
    QProcess process;
    process.setWorkingDirectory(repo);
    process.start(GitPublisher::gitProgram(), args);
    if (!process.waitForFinished(3000) || process.exitCode() != 0)
        return {};
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

bool mostlyDated(const QString &dir)
{
    const QStringList files = QDir(dir).entryList({QStringLiteral("*.md"), QStringLiteral("*.markdown")}, QDir::Files);
    if (files.isEmpty())
        return true;
    static const QRegularExpression dated(QStringLiteral(R"(^\d{4}-\d{2}-\d{2}-)"));
    int hits = 0;
    const int n = qMin(files.size(), 30);
    for (int i = 0; i < n; ++i) {
        if (dated.match(files.at(i)).hasMatch())
            ++hits;
    }
    return hits * 2 >= n;
}

QString separatorFromNames(const QStringList &names)
{
    int under = 0;
    int dash = 0;
    for (const QString &name : names) {
        under += name.count(QLatin1Char('_'));
        dash += name.count(QLatin1Char('-'));
    }
    return under > dash ? QStringLiteral("_") : QStringLiteral("-");
}

// Home is a landing page. post and default are what GitHub Pages posts use.
int layoutRank(const QString &name)
{
    if (name == QLatin1String("post"))
        return 0;
    if (name == QLatin1String("default"))
        return 1;
    if (name == QLatin1String("home") || name == QLatin1String("page") || name == QLatin1String("none"))
        return 9;
    return 2;
}

QStringList layoutFiles(const QString &root)
{
    QStringList names;
    const QDir dir(root + QStringLiteral("/_layouts"));
    if (!dir.exists())
        return names;
    const QStringList files = dir.entryList({QStringLiteral("*.html"), QStringLiteral("*.md"), QStringLiteral("*.markdown")},
                                            QDir::Files, QDir::Name);
    for (const QString &file : files) {
        const QString stem = QFileInfo(file).completeBaseName();
        if (!stem.isEmpty() && !names.contains(stem))
            names << stem;
    }
    return names;
}

QString chooseLayout(const QStringList &files, const QHash<QString, int> &used, const QString &configLayout, bool postsCollection)
{
    QString best;
    int bestCount = 0;
    for (auto it = used.constBegin(); it != used.constEnd(); ++it) {
        const QString name = it.key().trimmed();
        if (name.isEmpty() || it.value() <= 0)
            continue;
        const bool betterCount = it.value() > bestCount;
        const bool tie = it.value() == bestCount && (best.isEmpty() || layoutRank(name) < layoutRank(best));
        if (best.isEmpty() || betterCount || tie) {
            best = name;
            bestCount = it.value();
        }
    }
    // A landing-page layout only wins when the posts do not already share another one.
    if (best == QLatin1String("home")) {
        QString other;
        int otherCount = 0;
        for (auto it = used.constBegin(); it != used.constEnd(); ++it) {
            if (it.key() == QLatin1String("home") || it.value() <= 0)
                continue;
            if (other.isEmpty() || it.value() > otherCount || (it.value() == otherCount && layoutRank(it.key()) < layoutRank(other))) {
                other = it.key();
                otherCount = it.value();
            }
        }
        if (otherCount > 0 && otherCount * 3 >= bestCount)
            best = other;
    }
    if (bestCount > 0)
        return best;

    const QString configured = configLayout.trimmed();
    if (!configured.isEmpty() && configured != QLatin1String("home"))
        return configured;
    const auto has = [&files](const char *name) { return files.contains(QLatin1String(name)); };
    if (postsCollection && has("post"))
        return QStringLiteral("post");
    if (has("default"))
        return QStringLiteral("default");
    for (const QString &name : files) {
        if (layoutRank(name) < 9)
            return name;
    }
    if (!files.isEmpty())
        return files.first();
    // GitHub Pages' default theme publishes posts with layout "post" even when _layouts is absent.
    return QStringLiteral("post");
}

QStringList orderedLayouts(QStringList names, const QString &chosen)
{
    names.removeAll(QString());
    names.removeDuplicates();
    std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
        if (layoutRank(a) != layoutRank(b))
            return layoutRank(a) < layoutRank(b);
        return a.localeAwareCompare(b) < 0;
    });
    if (!chosen.isEmpty()) {
        names.removeAll(chosen);
        names.prepend(chosen);
    }
    return names;
}

struct PostSample {
    QString layout;
    QStringList tags;
    QHash<QString, int> kinds;
};

QList<PostSample> readPosts(const QString &dir)
{
    QList<PostSample> out;
    if (!QDir(dir).exists())
        return out;
    QDirIterator it(dir, {QStringLiteral("*.md"), QStringLiteral("*.markdown"), QStringLiteral("*.mdx")}, QDir::Files,
                    QDirIterator::Subdirectories);
    int guard = 0;
    while (it.hasNext() && guard++ < 80) {
        const QString path = it.next();
        if (path.contains(QLatin1String("/node_modules/")) || path.contains(QLatin1String("/_site/"))
            || path.contains(QLatin1String("/.git/")))
            continue;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const FrontMatter fm = FrontMatter::parse(QString::fromUtf8(file.read(6000)), nullptr);
        if (fm.isEmpty())
            continue;
        PostSample sample;
        sample.layout = fm.string(QStringLiteral("layout")).trimmed();
        sample.tags = fm.list(QStringLiteral("tags"));
        for (const auto &field : fm.fields) {
            int kind = 0;
            if (field.second.typeId() == QMetaType::Bool)
                kind = 1;
            else if (field.second.typeId() == QMetaType::QStringList)
                kind = 2;
            sample.kinds.insert(field.first, kind);
        }
        out.append(sample);
    }
    return out;
}

QJsonValue blankValue(int kind)
{
    if (kind == 1)
        return false;
    if (kind == 2)
        return QJsonArray();
    return QString();
}

// More than half the posts. A key on exactly half stays with the collections that use it.
bool majority(int hits, int total)
{
    return total > 0 && hits * 2 > total;
}

// Used often enough to be part of a new post. One stray key is not.
bool common(int hits, int total)
{
    return total > 0 && hits * 3 >= total;
}

const QStringList kOptionalKeys = {QStringLiteral("featured"), QStringLiteral("highlight"), QStringLiteral("highlights"),
                                   QStringLiteral("image"), QStringLiteral("video"), QStringLiteral("categories")};

struct ColStat {
    int posts = 0;
    QHash<QString, int> hits;
    QHash<QString, int> kinds;
    QHash<QString, int> layouts;
};

void learnFromPosts(Site &site, const QString &root, const QStringList &files, const QString &configLayout)
{
    QVector<ColStat> stats(site.collections.size());
    QHash<QString, int> tagHits;
    QHash<QString, int> allLayouts;
    QHash<QString, int> allHits;
    QHash<QString, int> allKinds;
    int totalPosts = 0;
    bool postsCollection = false;
    for (int i = 0; i < site.collections.size(); ++i) {
        if (site.collections.at(i).dir == QLatin1String("_posts"))
            postsCollection = true;
        const QList<PostSample> posts = readPosts(root + QLatin1Char('/') + site.collections.at(i).dir);
        ColStat &stat = stats[i];
        stat.posts = posts.size();
        totalPosts += posts.size();
        for (const PostSample &post : posts) {
            if (!post.layout.isEmpty()) {
                stat.layouts[post.layout] += 1;
                allLayouts[post.layout] += 1;
            }
            for (const QString &tag : post.tags) {
                const QString clean = tag.trimmed();
                if (!clean.isEmpty())
                    tagHits[clean] += 1;
            }
            for (auto it = post.kinds.constBegin(); it != post.kinds.constEnd(); ++it) {
                stat.hits[it.key()] += 1;
                allHits[it.key()] += 1;
                stat.kinds.insert(it.key(), it.value());
                allKinds.insert(it.key(), it.value());
            }
        }
    }

    QList<QPair<QString, int>> tagRows;
    for (auto it = tagHits.constBegin(); it != tagHits.constEnd(); ++it)
        tagRows.append({it.key(), it.value()});
    std::sort(tagRows.begin(), tagRows.end(), [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
        if (a.second != b.second)
            return a.second > b.second;
        return a.first.localeAwareCompare(b.first) < 0;
    });
    site.tags.clear();
    for (const auto &row : tagRows)
        site.tags << row.first;

    const bool pages = !site.isMdx();
    const QString chosen = pages ? chooseLayout(files, allLayouts, configLayout, postsCollection) : QString();
    site.layout = chosen;
    QStringList names = files;
    for (auto it = allLayouts.constBegin(); it != allLayouts.constEnd(); ++it)
        names << it.key();
    site.layouts = pages ? orderedLayouts(names, chosen) : QStringList();

    QJsonObject fm;
    if (pages) {
        fm.insert(QStringLiteral("layout"), chosen);
        fm.insert(QStringLiteral("title"), QString());
        fm.insert(QStringLiteral("date"), QStringLiteral("{date}"));
        if (totalPosts == 0 || common(allHits.value(QStringLiteral("description")), totalPosts))
            fm.insert(QStringLiteral("description"), QString());
        if (totalPosts == 0 || common(allHits.value(QStringLiteral("tags")), totalPosts) || !tagHits.isEmpty())
            fm.insert(QStringLiteral("tags"), QJsonArray());
    } else {
        fm.insert(QStringLiteral("title"), QString());
        if (common(allHits.value(QStringLiteral("description")), totalPosts))
            fm.insert(QStringLiteral("description"), QString());
    }
    for (const QString &key : kOptionalKeys) {
        if (majority(allHits.value(key), totalPosts))
            fm.insert(key, blankValue(allKinds.value(key)));
    }
    site.frontMatter = fm;

    for (int i = 0; i < site.collections.size(); ++i) {
        const ColStat &stat = stats.at(i);
        QJsonObject extra;
        if (pages && stat.posts > 0 && !stat.layouts.isEmpty()) {
            const QString mode = chooseLayout(files, stat.layouts, chosen, site.collections.at(i).dir == QLatin1String("_posts"));
            if (!mode.isEmpty() && mode != chosen && majority(stat.layouts.value(mode), stat.posts))
                extra.insert(QStringLiteral("layout"), mode);
        }
        for (const QString &key : kOptionalKeys) {
            if (fm.contains(key))
                continue;
            if (majority(stat.hits.value(key), stat.posts))
                extra.insert(key, blankValue(stat.kinds.value(key)));
        }
        site.collections[i].frontMatter = extra;
    }
}

bool dirHasPage(const QString &dir)
{
    if (QFileInfo::exists(dir + QStringLiteral("/page.mdx")) || QFileInfo::exists(dir + QStringLiteral("/page.md")))
        return true;
    const QStringList kids = QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &kid : kids) {
        if (kid.startsWith(QLatin1Char('.')) || kid == QLatin1String("node_modules"))
            continue;
        const QString sub = dir + QLatin1Char('/') + kid;
        if (QFileInfo::exists(sub + QStringLiteral("/page.mdx")) || QFileInfo::exists(sub + QStringLiteral("/page.md")))
            return true;
    }
    return false;
}

} // namespace

bool parseGitHubRemote(const QString &remote, QString *owner, QString *repo)
{
    static const QRegularExpression rx(QStringLiteral(R"((?:github\.com[:/])([^/\s:]+)/([^/\s]+?)(?:\.git)?$)"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = rx.match(remote.trimmed());
    if (!m.hasMatch())
        return false;
    if (owner)
        *owner = m.captured(1);
    if (repo)
        *repo = m.captured(2);
    return true;
}

QString githubPagesUrl(const QString &owner, const QString &repo)
{
    const QString user = owner.toLower();
    const QString name = repo.toLower();
    if (name == user + QLatin1String(".github.io"))
        return QStringLiteral("https://") + name;
    return QStringLiteral("https://") + user + QStringLiteral(".github.io/") + name;
}

Site inspectBlog(const QString &absolutePath, bool *blog)
{
    Site site;
    const QString root = QDir::cleanPath(absolutePath);
    site.name = QFileInfo(root).fileName();
    site.path = root;
    site.type = QStringLiteral("jekyll");
    site.branch = QStringLiteral("main");
    site.remote = QStringLiteral("origin");
    site.slugSeparator = QStringLiteral("-");
    site.commitMessage = QStringLiteral("Post: {title}");
    site.headingLevel = 2;
    site.templateText = QStringLiteral("## {title}\n\n");
    site.imagePattern = QStringLiteral("{stem}{ext}");

    QString ymlUrl;
    QString ymlBase;
    QString configLayout;
    bool inDefaults = false;
    QFile yml(root + QStringLiteral("/_config.yml"));
    const bool jekyll = yml.exists();
    if (yml.open(QIODevice::ReadOnly)) {
        while (!yml.atEnd()) {
            const QString line = QString::fromUtf8(yml.readLine());
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith(QLatin1Char('#')) || trimmed.isEmpty())
                continue;
            const bool nested = line.startsWith(QLatin1Char(' ')) || line.startsWith(QLatin1Char('\t'));
            if (!nested) {
                inDefaults = trimmed.startsWith(QLatin1String("defaults:"));
                if (trimmed.startsWith(QLatin1String("title:"))) {
                    const QString title = yamlBare(trimmed.mid(6));
                    if (!title.isEmpty())
                        site.name = title;
                } else if (trimmed.startsWith(QLatin1String("url:"))) {
                    ymlUrl = yamlBare(trimmed.mid(4));
                } else if (trimmed.startsWith(QLatin1String("baseurl:"))) {
                    ymlBase = yamlBare(trimmed.mid(8));
                }
                continue;
            }
            if (inDefaults && trimmed.startsWith(QLatin1String("layout:")))
                configLayout = yamlBare(trimmed.mid(7));
        }
    }
    if (!ymlUrl.isEmpty())
        site.url = joinUrl(ymlUrl, ymlBase);

    QDir collections(root + QStringLiteral("/collections"));
    if (collections.exists()) {
        const QStringList dirs = collections.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &dirName : dirs) {
            if (!dirName.startsWith(QLatin1Char('_')))
                continue;
            const QString label = dirName.mid(1);
            if (label.isEmpty())
                continue;
            Collection c;
            c.name = label;
            c.dir = QStringLiteral("collections/") + dirName;
            const bool dated = mostlyDated(root + QLatin1Char('/') + c.dir);
            c.filePattern = dated ? QStringLiteral("{date}-{slug}.md") : QStringLiteral("{slug}.md");
            c.urlPattern = dated ? QStringLiteral("/") + label + QStringLiteral("/{date}-{slug}.html")
                                 : QStringLiteral("/") + label + QStringLiteral("/{slug}.html");
            site.collections.append(c);
        }
    }
    if (QDir(root + QStringLiteral("/_posts")).exists()) {
        Collection c;
        c.name = QStringLiteral("posts");
        c.dir = QStringLiteral("_posts");
        c.filePattern = QStringLiteral("{date}-{slug}.md");
        c.urlPattern = QStringLiteral("/{year}/{month}/{day}/{slug}.html");
        site.collections.append(c);
    }

    QList<Collection> mdx;
    QStringList slugNames;
    QDir blogDir(root + QStringLiteral("/blog"));
    if (blogDir.exists()) {
        const QStringList kids = blogDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &kid : kids) {
            if (kid.startsWith(QLatin1Char('.')) || kid == QLatin1String("node_modules"))
                continue;
            const QString sub = blogDir.absoluteFilePath(kid);
            if (!dirHasPage(sub))
                continue;
            Collection c;
            c.name = kid;
            c.dir = QStringLiteral("blog/") + kid;
            c.filePattern = QStringLiteral("{slug}/page.mdx");
            c.urlPattern = QStringLiteral("/blog/") + kid + QStringLiteral("/{slug}");
            mdx.append(c);
            const QStringList posts = QDir(sub).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            slugNames << posts;
        }
    }

    const bool hasMdx = !mdx.isEmpty();
    if (hasMdx && !jekyll) {
        site.type = QStringLiteral("mdx");
        site.collections = mdx;
        site.slugSeparator = separatorFromNames(slugNames);
        site.commitMessage = QStringLiteral("Blog: {title}");
        site.headingLevel = 4;
        site.templateText = QStringLiteral("#### {title}\n\n");
    }

    const bool looks = jekyll || !site.collections.isEmpty() || hasMdx;
    if (blog)
        *blog = looks;
    if (site.collections.isEmpty()) {
        // A pages repo with no posts yet still gets the GitHub Pages post folder.
        Collection c;
        c.name = QStringLiteral("posts");
        c.dir = QStringLiteral("_posts");
        c.filePattern = QStringLiteral("{date}-{slug}.md");
        c.urlPattern = QStringLiteral("/{year}/{month}/{day}/{slug}.html");
        site.collections.append(c);
    }
    learnFromPosts(site, root, layoutFiles(root), configLayout);
    return site;
}

QList<FoundRepo> scanGitHubRepos(const QString &directory)
{
    QList<FoundRepo> found;
    QDir dir(directory);
    if (!dir.exists())
        return found;
    const QStringList names = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : names) {
        if (name.startsWith(QLatin1Char('.')))
            continue;
        const QString path = dir.absoluteFilePath(name);
        if (!QFileInfo::exists(path + QStringLiteral("/.git")))
            continue;
        const QString remote = gitLine(path, {QStringLiteral("remote"), QStringLiteral("get-url"), QStringLiteral("origin")});
        QString owner;
        QString repo;
        if (!parseGitHubRemote(remote, &owner, &repo))
            continue;
        bool blog = false;
        Site site = inspectBlog(path, &blog);
        site.path = withTilde(path);
        if (site.url.isEmpty())
            site.url = githubPagesUrl(owner, repo);
        const QString branch = gitLine(path, {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"), QStringLiteral("HEAD")});
        if (!branch.isEmpty() && branch != QLatin1String("HEAD"))
            site.branch = branch;
        FoundRepo item;
        item.site = site;
        item.blog = blog;
        QStringList bits;
        bits << (site.isMdx() ? QStringLiteral("MDX") : QStringLiteral("Jekyll"));
        if (!site.layout.isEmpty())
            bits << QStringLiteral("layout %1").arg(site.layout);
        bits << QStringLiteral("%1 collection%2").arg(site.collections.size()).arg(site.collections.size() == 1 ? QString() : QStringLiteral("s"));
        if (!site.tags.isEmpty())
            bits << QStringLiteral("%1 tag%2").arg(site.tags.size()).arg(site.tags.size() == 1 ? QString() : QStringLiteral("s"));
        if (!blog)
            bits << QStringLiteral("no posts found");
        item.detail = bits.join(QStringLiteral(", "));
        found.append(item);
    }
    std::sort(found.begin(), found.end(), [](const FoundRepo &a, const FoundRepo &b) {
        if (a.blog != b.blog)
            return a.blog;
        return a.site.name.localeAwareCompare(b.site.name) < 0;
    });
    return found;
}
