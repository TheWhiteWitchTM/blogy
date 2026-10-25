#include "selftest.h"

#include "bluesky.h"
#include "config.h"
#include "editor.h"
#include "frontmatter.h"
#include "git.h"
#include "grok.h"
#include "post.h"
#include "preview.h"
#include "ui/frontmatterform.h"
#include "ui/mainwindow.h"

#include <QApplication>
#include <QEventLoop>
#include <QProcess>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTreeWidget>

#include <cstdio>

namespace {

int gFailed = 0;

void check(bool cond, const char *name, const QString &detail = QString())
{
    if (cond) {
        std::fprintf(stderr, "ok %s\n", name);
    } else {
        std::fprintf(stderr, "FAIL %s %s\n", name, detail.toUtf8().constData());
        ++gFailed;
    }
}

void writeFile(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(text.toUtf8());
}

} // namespace

int runSelfTest()
{
    // Never touch real secrets or the real config during tests.
    qunsetenv("XAI_API_KEY");
    qunsetenv("BLUESKY_HANDLE");
    qunsetenv("BLUESKY_APP_PASSWORD");
    QTemporaryDir tmp;

    // Slugs and names.
    check(post::slugify(QStringLiteral("America: From Leader of the Free World to Putin’s Diesel Errand Boy"))
              == QLatin1String("america-from-leader-of-the-free-world-to-putins-diesel-errand-boy"),
          "slugify punctuation");
    check(post::slugify(QStringLiteral("Café über alles!")) == QLatin1String("cafe-uber-alles"), "slugify accents");
    check(post::slugify(QStringLiteral("Why we need to talk"), QStringLiteral("_")) == QLatin1String("why_we_need_to_talk"),
          "slugify underscore");

    Config config;
    config.fromJson(exampleConfig());
    check(config.sites.size() == 3, "example config has three sites");
    const Site ww = config.sites.value(0);
    const Collection *articles = ww.collection(QStringLiteral("articles"));
    const QDate d(2026, 10, 10);
    check(articles && post::relativePath(*articles, d, QStringLiteral("diesel-errand-boy"))
                          == QLatin1String("collections/_articles/2026-10-10-diesel-errand-boy.md"),
          "jekyll collection path");
    check(articles && post::publicUrl(ww, *articles, d, QStringLiteral("diesel-errand-boy"))
                          == QLatin1String("https://thewhitewitchtm.github.io/whitewitch/articles/2026-10-10-diesel-errand-boy.html"),
          "jekyll collection url matches the live site");
    QDate pd;
    QString ps;
    check(articles && post::parsePath(*articles, QStringLiteral("collections/_articles/2026-10-21-too-late.md"), &pd, &ps)
              && pd == QDate(2026, 10, 21) && ps == QLatin1String("too-late"),
          "parse path back");
    const Site gh = config.sites.value(2);
    check(post::publicUrl(gh, gh.collections.first(), QDate(2026, 1, 2), QStringLiteral("hi"))
              == QLatin1String("https://thewhitewitchtm.github.io/2026/01/02/hi.html"),
          "_posts default permalink");
    const Site mira = config.sites.value(1);
    const Collection *dev = mira.collection(QStringLiteral("dev-blog"));
    check(dev && post::relativePath(*dev, d, QStringLiteral("miras_world")) == QLatin1String("blog/dev-blog/miras_world/page.mdx"),
          "mdx path");
    check(dev && post::parsePath(*dev, QStringLiteral("blog/dev-blog/nextjs_vs_php/page.mdx"), &pd, &ps)
              && ps == QLatin1String("nextjs_vs_php"),
          "mdx parse path");

    // Front matter.
    const QString sample = QStringLiteral("---\nlayout: default\ntitle: \"A \\\"quoted\\\" title\"\ndate: 2026-10-10\n"
                                          "featured: true\ndescription: \"Short: sharp.\"\ntags: [politics, us, russia]\n"
                                          "list:\n  - one\n  - two\n---\n\n## Body\n");
    QString body;
    bool had = false;
    FrontMatter fm = FrontMatter::parse(sample, &body, &had);
    check(had && fm.string(QStringLiteral("title")) == QLatin1String("A \"quoted\" title"), "yaml title");
    check(fm.value(QStringLiteral("featured")).toBool(), "yaml bool");
    check(fm.list(QStringLiteral("tags")) == QStringList({QStringLiteral("politics"), QStringLiteral("us"), QStringLiteral("russia")}),
          "yaml inline list");
    check(fm.list(QStringLiteral("list")).size() == 2, "yaml block list");
    check(body.startsWith(QLatin1String("\n## Body")), "body split", body.left(20));
    FrontMatter again = FrontMatter::parse(fm.compose(body, false), nullptr);
    check(again.fields == fm.fields, "yaml round trip");
    check(fm.toYaml().contains(QLatin1String("date: 2026-10-10")), "date stays bare");

    const QString mdx = QStringLiteral("export const metadata = {\ntitle: \"Behind the Scenes\",\n};\n\n#### Hi\n");
    fm = FrontMatter::parse(mdx, &body, &had);
    check(had && fm.string(QStringLiteral("title")) == QLatin1String("Behind the Scenes"), "mdx metadata");
    check(FrontMatter::parse(fm.compose(body, true), nullptr).string(QStringLiteral("title")) == QLatin1String("Behind the Scenes"),
          "mdx round trip");

    const QString fresh = post::newPostText(ww, *articles, QStringLiteral("Hello: World"), d, {QStringLiteral("tech")}, QString());
    fm = FrontMatter::parse(fresh, &body);
    check(fm.string(QStringLiteral("layout")) == QLatin1String("default") && fm.string(QStringLiteral("date")) == QLatin1String("2026-10-10")
              && fm.string(QStringLiteral("title")) == QLatin1String("Hello: World") && fm.list(QStringLiteral("tags")).size() == 1,
          "new post front matter", fresh);
    check(fresh.startsWith(QLatin1String("---\nlayout: default\ntitle: \"Hello: World\"\ndate: 2026-10-10\n")), "new post layout order", fresh);

    // Bluesky record: facets use UTF-8 byte offsets.
    const QString text = QStringLiteral("🧙‍♀️ New post\n\nhttps://example.com/a.html #retro");
    const QJsonObject rec = BlueskyClient::buildRecord(text, QStringLiteral("https://example.com/a.html"), QStringLiteral("T"),
                                                       QStringLiteral("D"), QStringLiteral("2026-10-25T00:00:00.000Z"));
    const QJsonArray facets = rec.value(QStringLiteral("facets")).toArray();
    const QByteArray utf8 = text.toUtf8();
    const int start = utf8.indexOf("https://");
    check(facets.size() == 2, "bluesky facets found");
    const QJsonObject idx = facets.at(0).toObject().value(QStringLiteral("index")).toObject();
    check(idx.value(QStringLiteral("byteStart")).toInt() == start
              && idx.value(QStringLiteral("byteEnd")).toInt() == start + int(qstrlen("https://example.com/a.html")),
          "bluesky link facet byte offsets");
    check(rec.value(QStringLiteral("embed")).toObject().value(QStringLiteral("$type")).toString() == QLatin1String("app.bsky.embed.external"),
          "bluesky external embed");
    check(BlueskyClient::graphemeLength(QStringLiteral("🧙‍♀️ab")) == 3, "grapheme count");
    check(BlueskyClient::webUrlForUri(QStringLiteral("at://did:plc:x/app.bsky.feed.post/3abc"), QStringLiteral("me.bsky.social"))
              == QLatin1String("https://bsky.app/profile/me.bsky.social/post/3abc"),
          "bluesky web url");

    // Grok wire format.
    QString err;
    check(GrokClient::parseReply(R"({"choices":[{"message":{"role":"assistant","content":"hi"}}]})", &err) == QLatin1String("hi"),
          "grok parse reply");
    GrokClient::parseReply(R"({"error":{"message":"bad key"}})", &err);
    check(err == QLatin1String("bad key"), "grok parse error");

    // Config file: secrets saved owner-only, env wins.
    Config saved;
    saved.load(tmp.filePath(QStringLiteral("cfg/config.json")));
    saved.fileGrokKey = QStringLiteral("file-key");
    check(saved.save(), "config save");
    check((QFile::permissions(saved.path()) & (QFileDevice::ReadGroup | QFileDevice::ReadOther)) == 0, "config is owner-only");
    Config loaded;
    loaded.load(saved.path());
    check(loaded.grokApiKey() == QLatin1String("file-key") && loaded.secretSource(QStringLiteral("grok")) == QLatin1String("config"),
          "config key from file");
    qputenv("XAI_API_KEY", "env-key");
    check(loaded.grokApiKey() == QLatin1String("env-key"), "env overrides config");
    qunsetenv("XAI_API_KEY");

    check(Preview::renderMarkdown(QStringLiteral("---\ntitle: \"T\"\n---\n{% include x.html %}\nHi\n")).startsWith(QLatin1String("# T")),
          "preview header");

    // The window, on a fake site.
    const QString root = tmp.filePath(QStringLiteral("site"));
    writeFile(root + QStringLiteral("/collections/_blog/2026-10-10-first.md"), QStringLiteral("---\ntitle: \"First\"\ndate: 2026-10-10\n---\n\nHi\n"));
    writeFile(root + QStringLiteral("/collections/_blog/2026-10-12-second.md"), QStringLiteral("---\ntitle: \"Second\"\ndate: 2026-10-12\ntags: [a]\n---\n\nYo\n"));
    Config winConfig;
    winConfig.load(tmp.filePath(QStringLiteral("cfg/config.json")));
    Site site = ww;
    site.path = root;
    winConfig.sites = {site};
    {
        MainWindow window(&winConfig);
        window.show();
        QApplication::processEvents();
        QTreeWidgetItem *blog = window.postTree()->topLevelItem(0);
        check(blog && blog->childCount() == 2 && blog->child(0)->text(0).contains(QLatin1String("second")), "post list newest first");
        const QString file = root + QStringLiteral("/collections/_blog/2026-10-12-second.md");
        check(window.openFile(file), "open post");
        check(window.frontForm()->frontMatter().string(QStringLiteral("title")) == QLatin1String("Second"), "form loads front matter");
        check(window.postUrl(file) == QLatin1String("https://thewhitewitchtm.github.io/whitewitch/blog/2026-10-12-second.html"),
              "post url", window.postUrl(file));
        window.editor()->document()->setModified(false);
        window.close();
    }

    // Publish through a real git CLI into a local bare repo.
    if (!GitPublisher::gitProgram().isEmpty()) {
        const QString bare = tmp.filePath(QStringLiteral("remote.git"));
        const QString work = tmp.filePath(QStringLiteral("work"));
        auto git = [](const QString &dir, const QStringList &args) {
            QProcess p;
            p.setWorkingDirectory(dir);
            p.start(GitPublisher::gitProgram(), args);
            p.waitForFinished(20000);
            return p.exitCode() == 0;
        };
        QDir().mkpath(work);
        git(tmp.path(), {QStringLiteral("init"), QStringLiteral("-q"), QStringLiteral("--bare"), QStringLiteral("-b"), QStringLiteral("main"), bare});
        git(work, {QStringLiteral("init"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("main")});
        git(work, {QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("test@example.com")});
        git(work, {QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("Blogy Test")});
        git(work, {QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), bare});
        writeFile(work + QStringLiteral("/_posts/2026-10-25-hello.md"), QStringLiteral("---\ntitle: \"Hello\"\n---\n\nHi\n"));
        GitPublisher publisher;
        QEventLoop loop;
        bool okPublish = false;
        QObject::connect(&publisher, &GitPublisher::finished, &loop, [&](bool ok, const QString &) { okPublish = ok; loop.quit(); });
        QTimer::singleShot(30000, &loop, &QEventLoop::quit);
        publisher.publish(work, QStringLiteral("_posts/2026-10-25-hello.md"), QStringLiteral("Post: Hello"),
                          QStringLiteral("origin"), QStringLiteral("main"));
        loop.exec();
        QProcess log;
        log.start(GitPublisher::gitProgram(), {QStringLiteral("--git-dir"), bare, QStringLiteral("log"), QStringLiteral("--format=%s"), QStringLiteral("main")});
        log.waitForFinished(10000);
        check(okPublish && QString::fromUtf8(log.readAll()).trimmed() == QLatin1String("Post: Hello"), "git publish to bare remote");
    }

    std::fprintf(stderr, gFailed ? "%d check(s) failed\n" : "all checks passed\n", gFailed);
    return gFailed ? 1 : 0;
}
