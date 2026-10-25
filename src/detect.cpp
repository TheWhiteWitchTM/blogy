#include "detect.h"

#include "frontmatter.h"
#include "git.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>

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

void sampleFrontMatter(Site &site, const QString &file)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly))
        return;
    const FrontMatter fm = FrontMatter::parse(QString::fromUtf8(in.read(8000)), nullptr);
    if (fm.isEmpty())
        return;
    QJsonObject obj;
    for (const auto &field : fm.fields) {
        const QString &key = field.first;
        const QVariant &value = field.second;
        if (key == QLatin1String("layout"))
            obj.insert(key, value.toString().isEmpty() ? QStringLiteral("post") : value.toString());
        else if (key == QLatin1String("date"))
            obj.insert(key, QStringLiteral("{date}"));
        else if (key == QLatin1String("title"))
            obj.insert(key, QString());
        else if (value.typeId() == QMetaType::Bool)
            obj.insert(key, false);
        else if (value.typeId() == QMetaType::QStringList)
            obj.insert(key, QJsonArray());
        else
            obj.insert(key, QString());
    }
    if (!obj.isEmpty())
        site.frontMatter = obj;
}

QString firstMarkdown(const QString &dir)
{
    QDirIterator it(dir, {QStringLiteral("*.md"), QStringLiteral("*.markdown"), QStringLiteral("*.mdx")},
                    QDir::Files, QDirIterator::Subdirectories);
    int guard = 0;
    while (it.hasNext() && guard++ < 40) {
        const QString path = it.next();
        if (path.contains(QLatin1String("/node_modules/")) || path.contains(QLatin1String("/_site/")))
            continue;
        return path;
    }
    return {};
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
    site.templateText = QStringLiteral("## {title}\n\n");
    site.imagePattern = QStringLiteral("{stem}{ext}");

    QString ymlUrl;
    QString ymlBase;
    QFile yml(root + QStringLiteral("/_config.yml"));
    const bool jekyll = yml.exists();
    if (yml.open(QIODevice::ReadOnly)) {
        while (!yml.atEnd()) {
            const QString line = QString::fromUtf8(yml.readLine());
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith(QLatin1Char('#')) || trimmed.isEmpty())
                continue;
            if (line.startsWith(QLatin1Char(' ')) || line.startsWith(QLatin1Char('\t')))
                continue;
            if (trimmed.startsWith(QLatin1String("title:"))) {
                const QString title = yamlBare(trimmed.mid(6));
                if (!title.isEmpty())
                    site.name = title;
            } else if (trimmed.startsWith(QLatin1String("url:"))) {
                ymlUrl = yamlBare(trimmed.mid(4));
            } else if (trimmed.startsWith(QLatin1String("baseurl:"))) {
                ymlBase = yamlBare(trimmed.mid(8));
            }
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
        site.templateText = QStringLiteral("#### {title}\n\n");
        if (site.frontMatter.isEmpty())
            site.frontMatter = QJsonObject{{QStringLiteral("title"), QString()}};
    }

    const bool looks = jekyll || !site.collections.isEmpty() || hasMdx;
    if (blog)
        *blog = looks;
    if (site.collections.isEmpty()) {
        Collection c;
        c.name = QStringLiteral("posts");
        c.dir = QStringLiteral("_posts");
        c.filePattern = QStringLiteral("{date}-{slug}.md");
        c.urlPattern = QStringLiteral("/{year}/{month}/{day}/{slug}.html");
        site.collections.append(c);
        site.frontMatter = QJsonObject{{QStringLiteral("layout"), QStringLiteral("post")},
                                       {QStringLiteral("title"), QString()},
                                       {QStringLiteral("date"), QStringLiteral("{date}")},
                                       {QStringLiteral("tags"), QJsonArray()}};
    } else if (site.frontMatter.isEmpty()) {
        const QString sample = firstMarkdown(root + QLatin1Char('/') + site.collections.first().dir);
        if (!sample.isEmpty())
            sampleFrontMatter(site, sample);
        if (site.frontMatter.isEmpty() && !site.isMdx()) {
            site.frontMatter = QJsonObject{{QStringLiteral("layout"), QStringLiteral("default")},
                                           {QStringLiteral("title"), QString()},
                                           {QStringLiteral("date"), QStringLiteral("{date}")},
                                           {QStringLiteral("description"), QString()},
                                           {QStringLiteral("tags"), QJsonArray()}};
        }
    }
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
        bits << QStringLiteral("%1 collection%2").arg(site.collections.size()).arg(site.collections.size() == 1 ? QString() : QStringLiteral("s"));
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
