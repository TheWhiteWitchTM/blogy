#include "selftest.h"

#include "bluesky.h"
#include "config.h"
#include "detect.h"
#include "dictate.h"
#include "editor.h"
#include "highlighter.h"
#include "emoji.h"
#include "frontmatter.h"
#include "git.h"
#include "grok.h"
#include "grokauth.h"
#include "media.h"
#include "post.h"
#include "preview.h"
#include "ui/chatpanel.h"
#include "ui/dialogs.h"
#include "ui/frontmatterform.h"
#include "ui/mainwindow.h"
#include "words.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QEventLoop>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QDateTime>
#include <QDialog>
#include <QProcess>
#include <QPushButton>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QColor>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QTime>
#include <QToolBar>
#include <QToolButton>
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
    // Keep the test from rewriting the real ~/.config/blogy/blogy.conf.
    qputenv("BLOGY_SETTINGS", tmp.filePath(QStringLiteral("state.conf")).toUtf8());

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
    check(ww.headingLevel == 2 && ww.writingGuide().contains(QLatin1String("heading 2"))
              && ww.writingGuide().contains(QLatin1String("##")),
          "example site starts sections at heading 2", ww.writingGuide());
    check(mira.headingLevel == 4 && mira.writingGuide().contains(QLatin1String("heading 4")),
          "mdx example starts at heading 4", mira.writingGuide());

    // GitHub Pages' default Jekyll engine: files in _posts need a date prefix.
    Collection posts;
    posts.name = QStringLiteral("posts");
    posts.dir = QStringLiteral("_posts");
    posts.filePattern = QStringLiteral("{slug}.md");
    Site jekyll;
    jekyll.type = QStringLiteral("jekyll");
    jekyll.slugSeparator = QStringLiteral("-");
    check(post::effectiveFilePattern(jekyll, posts) == QLatin1String("{date}-{slug}.md"), "posts gain a date prefix");
    check(post::relativePath(jekyll, posts, QDate(2026, 10, 25), QStringLiteral("my-title"))
              == QLatin1String("_posts/2026-10-25-my-title.md"),
          "dated posts path");
    check(post::parsePath(jekyll, posts, QStringLiteral("_posts/2026-10-25-my-title.md"), &pd, &ps)
              && pd == QDate(2026, 10, 25) && ps == QLatin1String("my-title"),
          "parse dated posts path");
    check(post::parsePath(jekyll, posts, QStringLiteral("_posts/my-title.md"), &pd, &ps) && ps == QLatin1String("my-title"),
          "parse undated posts path");
    Collection blog;
    blog.dir = QStringLiteral("collections/_blog");
    blog.filePattern = QStringLiteral("{slug}.md");
    check(post::effectiveFilePattern(jekyll, blog) == QLatin1String("{slug}.md"), "custom collection keeps its pattern");
    jekyll.type = QStringLiteral("mdx");
    check(post::effectiveFilePattern(jekyll, posts) == QLatin1String("{slug}.md"), "mdx does not gain a date prefix");

    const QString doc = QStringLiteral("---\ntitle: T\n---\n\nHello\n");
    const QString sig = QStringLiteral("— Mira");
    const QString signedOnce = post::withSignature(doc, sig);
    check(signedOnce == doc + QStringLiteral("\n— Mira\n"), "signature appended", signedOnce);
    check(post::withSignature(signedOnce, sig) == signedOnce, "signature is not duplicated");
    check(post::withSignature(doc, QStringLiteral("  \n")) == doc, "blank signature is a no-op");
    Site signedSite = ww;
    signedSite.signature = QStringLiteral("— Mira\nhttps://example.com");
    signedSite.tune = QStringLiteral("dark humor and sarcasm");
    const Site signedBack = Site::fromJson(signedSite.toJson());
    check(signedBack.signature == signedSite.signature, "signature round trip");
    check(signedBack.tune == signedSite.tune, "tune round trip");
    check(signedBack.layout == signedSite.layout && signedBack.layouts == signedSite.layouts && signedBack.tags == signedSite.tags,
          "layout and tags round trip", signedBack.layout + QLatin1Char(' ') + signedBack.tags.join(QLatin1Char(',')));
    Site styled = ww;
    styled.headingLevel = 3;
    styled.emphasis = QStringLiteral("__");
    styled.listMarker = QStringLiteral("*");
    styled.styleNotes = QStringLiteral("short paragraphs");
    styled.signature = QStringLiteral("— Mira");
    const QString guide = styled.writingGuide();
    check(guide.contains(QLatin1String("heading 3")) && guide.contains(QLatin1String("###"))
              && guide.contains(QLatin1String("__bold__")) && guide.contains(QLatin1String("* item"))
              && guide.contains(QLatin1String("short paragraphs")) && guide.contains(QLatin1String("signature")),
          "writing guide tells Grok the markdown style", guide);
    const Site styledBack = Site::fromJson(styled.toJson());
    check(styledBack.headingLevel == 3 && styledBack.emphasis == QLatin1String("__")
              && styledBack.listMarker == QLatin1String("*") && styledBack.styleNotes == styled.styleNotes,
          "markdown style round trip");
    QJsonObject rawHeading{{QStringLiteral("name"), QStringLiteral("M")},
                           {QStringLiteral("template"), QStringLiteral("#### {title}\n\n")}};
    const Site inferred = Site::fromJson(rawHeading);
    check(inferred.headingLevel == 4, "heading follows a stock template", QString::number(inferred.headingLevel));
    check(Site::fromJson(inferred.toJson()).headingLevel == 4, "inferred heading survives a save");
    rawHeading.insert(QStringLiteral("headingLevel"), 2);
    check(Site::fromJson(rawHeading).headingLevel == 2, "explicit heading wins over the template");
    Site headed;
    headed.type = QStringLiteral("jekyll");
    headed.headingLevel = 3;
    headed.templateText = QStringLiteral("## {title}\n\n");
    Collection postsCol;
    postsCol.name = QStringLiteral("posts");
    postsCol.dir = QStringLiteral("_posts");
    postsCol.filePattern = QStringLiteral("{date}-{slug}.md");
    const QString headedPost = post::newPostText(headed, postsCol, QStringLiteral("Hello"), QDate(2026, 10, 25));
    check(headedPost.contains(QStringLiteral("### Hello")), "new post uses the markdown heading", headedPost);
    headed.templateText = QStringLiteral("Intro {title}\n");
    const QString customPost = post::newPostText(headed, postsCol, QStringLiteral("Hello"), QDate(2026, 10, 25));
    check(customPost.contains(QStringLiteral("Intro Hello")) && !customPost.contains(QStringLiteral("### Hello")),
          "a custom template keeps its own heading", customPost);

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
    const QByteArray cliStream =
        "{\"type\":\"stream_event\",\"event\":{\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"pon\"}}}\n"
        "{\"type\":\"stream_event\",\"event\":{\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"g\"}}}\n"
        "{\"type\":\"result\",\"subtype\":\"success\",\"is_error\":false,\"result\":\"pong\"}\n";
    check(GrokClient::textFromCliStream(cliStream, &err) == QLatin1String("pong"), "grok cli stream", err);
    err.clear();
    check(GrokClient::textFromCliStream(QByteArray("{\"type\":\"result\",\"subtype\":\"error\",\"is_error\":true,\"result\":\"nope\"}\n"), &err).isEmpty()
              && err == QLatin1String("nope"),
          "grok cli stream error", err);
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

    if (!GitPublisher::gitProgram().isEmpty()) {
        const QString repo = tmp.filePath(QStringLiteral("repo"));
        QDir().mkpath(repo);
        Config outside;
        outside.load(repo + QStringLiteral("/config.json"));
        check(outside.save(), "config save outside git");
        QProcess init;
        init.start(GitPublisher::gitProgram(), {QStringLiteral("init"), QStringLiteral("-q"), repo});
        init.waitForFinished(10000);
        QString refused;
        check(init.exitCode() == 0 && Config::isInsideGitWorkTree(repo + QStringLiteral("/config.json")), "git repo detected");
        check(!outside.save(&refused) && refused.contains(QLatin1String("git")), "config save refused inside git", refused);
        check(!Config::isInsideGitWorkTree(tmp.filePath(QStringLiteral("cfg/config.json"))), "temp config is not a repo");
    }

    check(Preview::renderMarkdown(QStringLiteral("---\ntitle: \"T\"\n---\n{% include x.html %}\nHi\n")).startsWith(QLatin1String("# T")),
          "preview header");
    check(ChatPanel::applyModeForPrompt(QStringLiteral("write a post to test")) == ChatPanel::Apply::Document,
          "write a post fills the editor");
    check(ChatPanel::applyModeForPrompt(QStringLiteral("write a short test post about rain")) == ChatPanel::Apply::Document,
          "write a short post");
    check(ChatPanel::applyModeForPrompt(QStringLiteral("make this a post")) == ChatPanel::Apply::Document,
          "make this a post");
    check(ChatPanel::applyModeForPrompt(QStringLiteral("finish the outline")) == ChatPanel::Apply::Document,
          "finish the outline rewrites the file");
    check(ChatPanel::applyModeForPrompt(QStringLiteral("what does this paragraph mean?")) == ChatPanel::Apply::Insert,
          "a question stays in the chat");
    check(ChatPanel::applyModeForPrompt(QStringLiteral("write a title for this post")) == ChatPanel::Apply::Insert,
          "a title request is not the whole post");
    check(ChatPanel::looksLikeWholePost(QStringLiteral("---\ntitle: T\n---\n\nHello from the editor.\n")),
          "a file with front matter is the post");
    check(!ChatPanel::looksLikeWholePost(QStringLiteral("Here are five titles.")), "a list is not the post");
    check(ChatPanel::withNote(QStringLiteral("Polish this."), QString()) == QLatin1String("Polish this."), "empty note stays out");
    check(ChatPanel::withNote(QStringLiteral("Polish this."), QStringLiteral("shorter")).contains(QLatin1String("shorter"))
              && ChatPanel::withNote(QStringLiteral("Polish this."), QStringLiteral("shorter")).startsWith(QLatin1String("Polish this.")),
          "a note is part of the job");

    // The window, on a fake site.
    const QString root = tmp.filePath(QStringLiteral("site"));
    writeFile(root + QStringLiteral("/collections/_blog/2026-10-10-first.md"), QStringLiteral("---\ntitle: \"First\"\ndate: 2026-10-10\n---\n\nHi\n"));
    writeFile(root + QStringLiteral("/collections/_blog/2026-10-12-second.md"), QStringLiteral("---\ntitle: \"Second\"\ndate: 2026-10-12\ntags: [a]\n---\n\nYo\n"));
    Config winConfig;
    winConfig.load(tmp.filePath(QStringLiteral("cfg/config.json")));
    Site site = ww;
    site.path = root;
    winConfig.sites = {site};
    // Keep "Finish the post" from launching the signed-in Grok CLI during the test.
    winConfig.grokBackend = QStringLiteral("api");
    winConfig.fileGrokKey.clear();
    {
        MainWindow window(&winConfig);
        window.show();
        QApplication::processEvents();
        QTreeWidgetItem *blog = window.postTree()->topLevelItem(0);
        check(blog && blog->childCount() == 2 && blog->child(0)->text(0).contains(QLatin1String("second")), "post list newest first");
        const QString file = root + QStringLiteral("/collections/_blog/2026-10-12-second.md");
        check(window.openFile(file), "open post");
        {
            SettingsDialog settings(&winConfig, &window);
            settings.resize(960, 720);
            settings.show();
            QApplication::processEvents();
            auto *tune = settings.findChild<QPlainTextEdit *>(QStringLiteral("siteTune"));
            check(tune && tune->placeholderText().contains(QLatin1String("sarcasm")), "site tune field");
            auto *tabs = settings.findChild<QTabWidget *>(QStringLiteral("settingsTabs"));
            QStringList titles;
            if (tabs) {
                for (int i = 0; i < tabs->count(); ++i)
                    titles << tabs->tabText(i);
            }
            check(titles == QStringList({QStringLiteral("Sites"), QStringLiteral("Markdown"), QStringLiteral("AI"),
                                         QStringLiteral("Bluesky"), QStringLiteral("Editor")}),
                  "settings tabs are split", titles.join(QLatin1Char(',')));
            auto onPage = [](QWidget *widget, QWidget *page) {
                for (QWidget *at = widget; at; at = at->parentWidget()) {
                    if (at == page)
                        return true;
                }
                return false;
            };
            const int sitesTab = titles.indexOf(QStringLiteral("Sites"));
            const int markdownTab = titles.indexOf(QStringLiteral("Markdown"));
            const int aiTab = titles.indexOf(QStringLiteral("AI"));
            const int bskyTab = titles.indexOf(QStringLiteral("Bluesky"));
            auto *hint = settings.findChild<QLabel *>(QStringLiteral("layoutHint"));
            auto *sig = settings.findChild<QPlainTextEdit *>(QStringLiteral("siteSignature"));
            auto *heading = settings.findChild<QComboBox *>(QStringLiteral("markdownHeading"));
            auto *handle = settings.findChild<QLineEdit *>(QStringLiteral("bskyHandle"));
            const int hintNeed = hint ? hint->heightForWidth(qMax(hint->width(), 1)) : 0;
            check(hint && tabs && onPage(hint, tabs->widget(sitesTab)) && hint->isVisible()
                      && hint->width() >= 360 && hint->height() >= hint->fontMetrics().lineSpacing() * 2
                      && hint->height() + 1 >= hintNeed,
                  "layout hint is readable",
                  hint ? QString::number(hint->width()) + QLatin1Char('x') + QString::number(hint->height())
                             + QStringLiteral(" need ") + QString::number(hintNeed)
                       : QStringLiteral("missing"));
            check(sig && heading && tabs && onPage(sig, tabs->widget(markdownTab))
                      && onPage(heading, tabs->widget(markdownTab)) && heading->currentData().toInt() == 2
                      && sig->placeholderText().contains(QLatin1String("Your name")),
                  "signature and default heading live on Markdown");
            check(tune && handle && tabs && onPage(tune, tabs->widget(aiTab)) && !onPage(tune, tabs->widget(sitesTab))
                      && onPage(handle, tabs->widget(bskyTab)) && !onPage(handle, tabs->widget(aiTab)),
                  "voice stays with AI and Bluesky has its own tab");
            tabs->setCurrentIndex(aiTab);
            QApplication::processEvents();
            auto *grokStatus = settings.findChild<QLabel *>(QStringLiteral("grokStatus"));
            const int statusNeed = grokStatus ? grokStatus->heightForWidth(qMax(grokStatus->width(), 1)) : 0;
            check(grokStatus && grokStatus->isVisible() && grokStatus->width() >= 360
                      && grokStatus->height() >= grokStatus->fontMetrics().lineSpacing() * 2
                      && grokStatus->height() + 1 >= statusNeed
                      && !grokStatus->text().contains(QLatin1String("API key"))
                      && !grokStatus->text().contains(QLatin1String("endpoint"))
                      && settings.findChild<QWidget *>(QStringLiteral("grokBackend")) == nullptr
                      && settings.findChild<QWidget *>(QStringLiteral("grokKey")) == nullptr,
                  "AI is the Grok CLI sign-in, and the status is readable",
                  grokStatus ? QString::number(grokStatus->width()) + QLatin1Char('x') + QString::number(grokStatus->height())
                                   + QStringLiteral(" need ") + QString::number(statusNeed) + QStringLiteral(" / ")
                                   + grokStatus->text()
                             : QStringLiteral("missing"));
            if (winConfig.grokCliAvailable())
                check(grokStatus && grokStatus->text().startsWith(QLatin1String("Signed in")),
                      "this machine's Grok sign-in is detected");
            QPushButton *saveButton = nullptr;
            QPushButton *cancelButton = nullptr;
            for (QPushButton *button : settings.findChildren<QPushButton *>()) {
                if (button->text() == QLatin1String("Save"))
                    saveButton = button;
                else if (button->text() == QLatin1String("Cancel"))
                    cancelButton = button;
            }
            auto *footer = settings.findChild<QWidget *>(QStringLiteral("settingsFooter"));
            const QPoint saveAt = saveButton ? saveButton->mapTo(&settings, QPoint(0, 0)) : QPoint();
            const QPoint cancelAt = cancelButton ? cancelButton->mapTo(&settings, QPoint(0, 0)) : QPoint();
            const int saveRight = saveButton ? saveAt.x() + saveButton->width() : 0;
            const int cancelRight = cancelButton ? cancelAt.x() + cancelButton->width() : 0;
            check(footer && saveButton && cancelButton
                      && saveButton->objectName() == QLatin1String("primary")
                      && footer->width() >= settings.width() - 2
                      && footer->geometry().bottom() >= settings.height() - 2
                      && saveAt.x() >= 12 && saveAt.x() <= 24
                      && cancelAt.x() >= saveRight && cancelAt.x() <= saveRight + 16
                      && cancelRight < settings.width() / 2
                      && saveAt.y() >= settings.height() - 72
                      && saveButton->height() >= 36 && cancelButton->height() >= 36,
                  "settings save and cancel sit in the left of the footer",
                  QStringLiteral("save %1,%2 %3x%4 cancel %5,%6 footer %7x%8 dialog %9x%10")
                      .arg(saveAt.x())
                      .arg(saveAt.y())
                      .arg(saveButton ? saveButton->width() : 0)
                      .arg(saveButton ? saveButton->height() : 0)
                      .arg(cancelAt.x())
                      .arg(cancelAt.y())
                      .arg(footer ? footer->width() : 0)
                      .arg(footer ? footer->height() : 0)
                      .arg(settings.width())
                      .arg(settings.height()));
            settings.hide();
        }
        QPushButton *writePost = nullptr;
        for (QPushButton *button : window.findChildren<QPushButton *>()) {
            if (button->text() == QLatin1String("Finish the post"))
                writePost = button;
        }
        QPlainTextEdit *prompt = nullptr;
        bool aiHeading = false;
        for (QPlainTextEdit *edit : window.findChildren<QPlainTextEdit *>()) {
            if (edit->objectName() == QLatin1String("chatPrompt"))
                prompt = edit;
        }
        if (QWidget *chat = window.findChild<QWidget *>(QStringLiteral("chatPane"))) {
            for (QLabel *label : chat->findChildren<QLabel *>()) {
                if (label->text() == QLatin1String("AI"))
                    aiHeading = true;
            }
        }
        bool frontHeading = false;
        if (QWidget *frontPane = window.findChild<QWidget *>(QStringLiteral("frontPane"))) {
            for (QLabel *label : frontPane->findChildren<QLabel *>()) {
                if (label->text() == QLatin1String("Front matter"))
                    frontHeading = true;
            }
        }
        check(prompt && prompt->font().pointSize() >= 16 && prompt->minimumHeight() >= 72,
              "prompt is large enough to write in",
              prompt ? QString::number(prompt->font().pointSize()) + QStringLiteral("pt / ")
                          + QString::number(prompt->minimumHeight())
                      : QStringLiteral("missing"));
        check(!aiHeading && !frontHeading, "pane tabs are the only titles");
        bool asked = false;
        QTimer::singleShot(0, [&asked] {
            const QWidgetList widgets = QApplication::allWidgets();
            for (QWidget *widget : widgets) {
                auto *dialog = qobject_cast<QDialog *>(widget);
                if (dialog && (dialog->windowTitle() == QLatin1String("Write the post")
                               || dialog->windowTitle() == QLatin1String("Finish the post"))) {
                    asked = true;
                    dialog->reject();
                }
            }
        });
        check(writePost, "finish the post button");
        if (prompt)
            prompt->setPlainText(QStringLiteral("rain on the quay"));
        if (writePost)
            writePost->click();
        QApplication::processEvents();
        check(!asked && prompt && prompt->toPlainText().isEmpty(), "finish the post uses the prompt");
        check(window.frontForm()->frontMatter().string(QStringLiteral("title")) == QLatin1String("Second"), "form loads front matter");
        check(window.postUrl(file) == QLatin1String("https://thewhitewitchtm.github.io/whitewitch/blog/2026-10-12-second.html"),
              "post url", window.postUrl(file));
        if (auto *command = window.findChild<QPlainTextEdit *>(QStringLiteral("publishCommand"))) {
            const QString shown = command->toPlainText();
            check(shown.contains(QStringLiteral("git ")) && shown.contains(root)
                      && shown.contains(QStringLiteral("collections/_blog/2026-10-12-second.md"))
                      && shown.contains(QStringLiteral("HEAD:main")) && shown.contains(QStringLiteral("origin")),
                  "publish shows the repo command", shown);
        } else {
            check(false, "publish shows the repo command", QStringLiteral("missing"));
        }

        bool sawBold = false;
        bool sawHeading = false;
        bool sawCodeBlock = false;
        bool formatText = false;
        int boldWidth = 0;
        int headingWidth = 0;
        if (QToolBar *format = window.findChild<QToolBar *>(QStringLiteral("formatToolbar"))) {
            check(format->toolButtonStyle() == Qt::ToolButtonIconOnly, "format toolbar is symbols");
            for (QToolButton *button : format->findChildren<QToolButton *>()) {
                const auto style = button->toolButtonStyle();
                if (style == Qt::ToolButtonTextOnly || style == Qt::ToolButtonTextBesideIcon
                    || style == Qt::ToolButtonTextUnderIcon)
                    formatText = true;
                const int width = button->sizeHint().width();
                if (button->text() == QLatin1String("Bold")) {
                    sawBold = true;
                    boldWidth = width;
                }
                if (button->text() == QLatin1String("Heading") && !button->icon().isNull()
                    && style == Qt::ToolButtonIconOnly) {
                    sawHeading = true;
                    headingWidth = width;
                }
                if (button->text() == QLatin1String("Code block") && !button->icon().isNull()
                    && style == Qt::ToolButtonIconOnly)
                    sawCodeBlock = true;
            }
        } else {
            check(false, "format toolbar");
        }
        check(sawBold && sawHeading && sawCodeBlock && !formatText, "format row is symbols, heading included");
        check(boldWidth > 0 && headingWidth > 0 && headingWidth <= boldWidth + 16, "heading symbol matches the other symbols",
              QString::number(headingWidth) + QStringLiteral(" vs ") + QString::number(boldWidth));
        QAction *bold = nullptr;
        QAction *helpAction = nullptr;
        QAction *settingsAction = nullptr;
        QAction *dictateAction = nullptr;
        for (QAction *action : window.findChildren<QAction *>()) {
            if (action->text() == QLatin1String("Bold"))
                bold = action;
            if (action->text() == QStringLiteral("Settings…"))
                settingsAction = action;
            if (action->shortcut() == QKeySequence(QKeySequence::HelpContents))
                helpAction = action;
            if (action->text() == QLatin1String("Dictate"))
                dictateAction = action;
        }
        check(bold && !bold->icon().isNull(), "format buttons are symbols");
        check(settingsAction && settingsAction->icon().isNull(), "settings stays a menu item");
        check(helpAction && helpAction->icon().isNull(), "help stays a menu item");
        QToolBar *formatBar = window.findChild<QToolBar *>(QStringLiteral("formatToolbar"));
        check(dictateAction && dictateAction->isCheckable() && dictateAction->icon().isNull()
                  && dictateAction->iconText() == QLatin1String("Dictate")
                  && !(formatBar && formatBar->actions().contains(dictateAction)),
              "dictate is a word, not a format symbol");
        bool settingsInFile = false;
        bool helpInHelpMenu = false;
        bool dictateInFormatMenu = false;
        if (QMenuBar *menus = window.menuBar()) {
            for (QAction *top : menus->actions()) {
                QMenu *menu = top->menu();
                if (!menu)
                    continue;
                if (menu->title() == QLatin1String("File"))
                    settingsInFile = settingsAction && menu->actions().contains(settingsAction);
                if (menu->title() == QLatin1String("Help"))
                    helpInHelpMenu = helpAction && menu->actions().contains(helpAction);
                if (menu->title() == QLatin1String("Format")) {
                    for (QAction *action : menu->actions()) {
                        if (action == dictateAction || (action->menu() && action->menu()->actions().contains(dictateAction)))
                            dictateInFormatMenu = true;
                    }
                }
            }
        }
        check(settingsInFile && helpInHelpMenu && !dictateInFormatMenu, "settings and help are menus, dictate is not format");
        if (QToolBar *mainBar = window.findChild<QToolBar *>(QStringLiteral("mainToolbar"))) {
            window.resize(1440, 900);
            QApplication::processEvents();
            bool symbol = false;
            bool loose = false;
            QString detail;
            const QStringList words = {QStringLiteral("New Post"), QStringLiteral("Open"), QStringLiteral("Save"),
                                       QStringLiteral("Publish"), QStringLiteral("Post to Bluesky"),
                                       QStringLiteral("Preview"), QStringLiteral("Tools"), QStringLiteral("Dictate")};
            QStringList seen;
            for (QAction *action : mainBar->actions()) {
                if (action->isSeparator())
                    continue;
                const QString label = action->iconText();
                seen << label;
                if (!action->icon().isNull())
                    symbol = true;
                if (auto *button = qobject_cast<QToolButton *>(mainBar->widgetForAction(action))) {
                    if (button->toolButtonStyle() != Qt::ToolButtonTextOnly || !button->icon().isNull())
                        symbol = true;
                    const int textW = button->fontMetrics().horizontalAdvance(label);
                    const int width = button->width();
                    // The laid-out button holds the whole word, with a small pad.
                    if (textW <= 0 || width < textW + 8 || width > textW + 48) {
                        loose = true;
                        detail = label + QStringLiteral(" text ") + QString::number(textW)
                                 + QStringLiteral(" button ") + QString::number(width);
                    }
                }
            }
            check(mainBar->toolButtonStyle() == Qt::ToolButtonTextOnly && !symbol && seen == words
                      && !mainBar->actions().contains(settingsAction) && !mainBar->actions().contains(helpAction)
                      && mainBar->actions().contains(dictateAction),
                  "main toolbar is words", seen.join(QLatin1Char(' ')));
            check(!loose, "main toolbar words fit the buttons", detail);
            QAction *previewAction = nullptr;
            for (QAction *action : mainBar->actions()) {
                if (action->iconText() == QLatin1String("Preview"))
                    previewAction = action;
            }
            const int textWidth = window.editor()->width();
            check(previewAction && textWidth > 400, "preview toggle", QString::number(textWidth));
            if (previewAction) {
                previewAction->setChecked(true);
                QApplication::processEvents();
                QWidget *previewPane = window.findChild<QWidget *>(QStringLiteral("preview"));
                check(previewPane && previewPane->isVisible() && !window.editor()->isVisible(),
                      "preview covers the editor");
                check(previewPane && previewPane->width() + 4 >= textWidth,
                      "preview fills the text area",
                      QString::number(previewPane ? previewPane->width() : 0) + QStringLiteral(" vs ")
                          + QString::number(textWidth));
                previewAction->setChecked(false);
                QApplication::processEvents();
                check(window.editor()->isVisible() && previewPane && !previewPane->isVisible(),
                      "editor returns when preview closes");
            }
            QAction *tools = nullptr;
            bool sidePanel = false;
            bool namedAi = false;
            for (QAction *action : window.findChildren<QAction *>()) {
                if (action->text() == QLatin1String("Side Panel") || action->iconText() == QLatin1String("Side Panel"))
                    sidePanel = true;
                if (action->iconText() == QLatin1String("AI"))
                    namedAi = true;
                if (action->iconText() == QLatin1String("Tools"))
                    tools = action;
            }
            check(tools && tools->isCheckable() && !sidePanel && !namedAi, "Tools toggles the pane");
            window.resize(1024, 640);
            QApplication::processEvents();
            if (tools)
                tools->setChecked(true);
            QApplication::processEvents();
            QPushButton *send = nullptr;
            QPushButton *clearChat = nullptr;
            for (QPushButton *button : window.findChildren<QPushButton *>()) {
                if (button->text() == QLatin1String("Ask"))
                    send = button;
                if (button->text() == QLatin1String("Clear"))
                    clearChat = button;
            }
            QWidget *chat = window.findChild<QWidget *>(QStringLiteral("chatPane"));
            QWidget *sidebar = window.findChild<QWidget *>(QStringLiteral("sidebar"));
            QToolButton *filesToggle = window.findChild<QToolButton *>(QStringLiteral("paneToggle"));
            check(send && chat && sidebar && filesToggle, "pane controls");
            auto fullyInside = [](QWidget *widget, QWidget *pane) {
                if (!widget || !pane || !widget->isVisible())
                    return false;
                const QRect inPane(widget->mapTo(pane, QPoint(0, 0)), widget->size());
                return pane->rect().contains(inPane) && widget->width() > 24 && widget->height() > 16;
            };
            if (send && chat && prompt) {
                const QRect inChat(send->mapTo(chat, QPoint(0, 0)), send->size());
                check(send->width() >= 72 && chat->rect().contains(inChat), "ask stays inside the chat pane",
                      QString::number(inChat.x()) + QLatin1Char(',') + QString::number(inChat.right())
                          + QStringLiteral(" of ") + QString::number(chat->width()));
                const QRect onWindow(send->mapTo(&window, QPoint(0, 0)), send->size());
                check(window.rect().contains(onWindow), "ask stays in the window",
                      QString::number(onWindow.right()) + QStringLiteral(" of ") + QString::number(window.width()));
                window.resize(1440, 900);
                QApplication::processEvents();
                check(prompt->isVisible() && prompt->height() >= 140 && prompt->width() + 36 >= chat->width(),
                      "prompt uses the chat pane",
                      QString::number(prompt->width()) + QLatin1Char('x') + QString::number(prompt->height())
                          + QStringLiteral(" pane ") + QString::number(chat->width()));
                // The saved window on a short display. Publish and the prompt used to fall off it.
                window.resize(960, 520);
                QApplication::processEvents();
                const QRect promptOnWindow(prompt->mapTo(&window, QPoint(0, 0)), prompt->size());
                const QRect sendOnWindow(send->mapTo(&window, QPoint(0, 0)), send->size());
                check(fullyInside(prompt, chat) && fullyInside(send, chat) && fullyInside(clearChat, chat)
                          && window.rect().contains(promptOnWindow) && window.rect().contains(sendOnWindow)
                          && prompt->height() >= 72 && prompt->width() + 36 >= chat->width(),
                      "prompt and ask fit a short window",
                      QString::number(prompt->x()) + QLatin1Char(',') + QString::number(prompt->y())
                          + QLatin1Char(' ') + QString::number(prompt->width()) + QLatin1Char('x')
                          + QString::number(prompt->height()) + QStringLiteral(" send ")
                          + QString::number(sendOnWindow.right()) + QLatin1Char(',')
                          + QString::number(sendOnWindow.bottom()));
                QWidget *publishPane = window.findChild<QWidget *>(QStringLiteral("publishPane"));
                QTabWidget *tabs = window.findChild<QTabWidget *>();
                QPushButton *publishButton = nullptr;
                if (publishPane) {
                    for (QPushButton *button : publishPane->findChildren<QPushButton *>()) {
                        if (button->text() == QLatin1String("Publish"))
                            publishButton = button;
                    }
                }
                if (tabs && publishPane)
                    tabs->setCurrentWidget(publishPane);
                QApplication::processEvents();
                if (publishButton && publishPane) {
                    const QRect publishOnWindow(publishButton->mapTo(&window, QPoint(0, 0)), publishButton->size());
                    check(fullyInside(publishButton, publishPane) && window.rect().contains(publishOnWindow)
                              && publishButton->width() + 28 >= publishPane->width(),
                          "publish button fits the pane",
                          QString::number(publishOnWindow.x()) + QLatin1Char(',')
                              + QString::number(publishOnWindow.y()) + QLatin1Char(' ')
                              + QString::number(publishButton->width()) + QLatin1Char('x')
                              + QString::number(publishButton->height()) + QStringLiteral(" pane ")
                              + QString::number(publishPane->width()) + QLatin1Char('x')
                              + QString::number(publishPane->height()));
                } else {
                    check(false, "publish button fits the pane", QStringLiteral("missing"));
                }
                if (tabs && chat)
                    tabs->setCurrentWidget(chat);
                window.resize(1024, 640);
                QApplication::processEvents();
            }
            QAction *frontMatter = nullptr;
            for (QAction *action : window.findChildren<QAction *>()) {
                if (action->text() == QLatin1String("Front Matter"))
                    frontMatter = action;
            }
            if (frontMatter && tools && chat) {
                frontMatter->trigger();
                QApplication::processEvents();
                QWidget *frontPane = window.findChild<QWidget *>(QStringLiteral("frontPane"));
                check(frontPane && frontPane->isVisible() && tools->isChecked() && !chat->isVisible(),
                      "front matter keeps the tools pane open");
                tools->setChecked(false);
                QApplication::processEvents();
                check(!tools->isChecked() && !chat->isVisible() && frontPane && !frontPane->isVisible(),
                      "tools closes the pane");
                tools->setChecked(true);
                QApplication::processEvents();
                check(tools->isChecked() && chat->isVisible() && frontPane && !frontPane->isVisible(),
                      "tools opens on the chat");
            }
            if (filesToggle && sidebar) {
                const auto onSidebar = [&](QWidget *widget) {
                    return sidebar->mapFromGlobal(widget->mapToGlobal(QPoint(0, 0)));
                };
                QWidget *host = filesToggle->parentWidget();
                const QPoint onSide = onSidebar(filesToggle);
                QWidget *top = host ? host->childAt(filesToggle->geometry().center()) : nullptr;
                check(filesToggle->text() == QStringLiteral("‹") && window.postTree()->isVisible()
                          && filesToggle->width() >= 32 && filesToggle->height() >= 32,
                      "posts arrow collapses",
                      QString::number(filesToggle->width()) + QLatin1Char('x') + QString::number(filesToggle->height()));
                check(filesToggle->isVisible() && onSide.y() >= 0 && onSide.y() <= 12 && onSide.x() >= 0
                          && onSide.x() < sidebar->width() && top == filesToggle,
                      "posts arrow stays on top",
                      QString::number(onSide.x()) + QLatin1Char(',') + QString::number(onSide.y())
                          + QStringLiteral(" top ") + (top ? top->objectName() : QStringLiteral("none")));
                filesToggle->click();
                QApplication::processEvents();
                check(filesToggle->isVisible() && filesToggle->text() == QStringLiteral("›") && sidebar->width() <= 40
                          && !window.postTree()->isVisible(),
                      "posts list hides", QString::number(sidebar->width()));
                const QPoint collapsed = onSidebar(filesToggle);
                check(collapsed.y() <= 12 && collapsed.x() >= 0 && collapsed.x() + filesToggle->width() <= sidebar->width() + 1
                          && host && host->childAt(filesToggle->geometry().center()) == filesToggle,
                      "posts arrow stays on top when hidden",
                      QString::number(collapsed.x()) + QLatin1Char(',') + QString::number(collapsed.y())
                          + QStringLiteral(" rail ") + QString::number(sidebar->width()));
                filesToggle->click();
                QApplication::processEvents();
                check(filesToggle->text() == QStringLiteral("‹") && sidebar->width() >= 180 && window.postTree()->isVisible(),
                      "posts list comes back", QString::number(sidebar->width()));
            }
        } else {
            check(false, "main toolbar");
        }
        if (helpAction) {
            QTimer::singleShot(0, &window, [&window] {
                QTextBrowser *view = window.findChild<QTextBrowser *>(QStringLiteral("helpView"));
                const QString page = view ? view->toPlainText() : QString();
                check(view && page.contains(QLatin1String("Dictate")) && page.contains(QLatin1String("Heading"))
                          && page.contains(QLatin1String("code block")) && page.contains(QLatin1String("posts list"))
                          && page.contains(QLatin1String("Tools")) && page.contains(QLatin1String("Finish the post"))
                          && !page.contains(QLatin1String("Side Panel")),
                      "help page", page.left(80));
                if (view)
                    view->window()->close();
            });
            helpAction->trigger();
        } else {
            check(false, "help action");
        }

        const QString crap = root + QStringLiteral("/collections/_blog/2026-10-20-crap.md");
        const QString original = QStringLiteral("---\ntitle: \"Crap\"\ndate: 2026-10-20\n---\n\nnope\n");
        writeFile(crap, original);
        check(window.openFile(crap), "open a post to leave");
        window.editor()->insertPlainText(QStringLiteral("junk"));
        const auto clickLeave = [](const QString &label) {
            for (QWidget *widget : QApplication::allWidgets()) {
                auto *box = qobject_cast<QMessageBox *>(widget);
                if (!box || !box->isVisible())
                    continue;
                for (QAbstractButton *button : box->buttons()) {
                    if (button->text().contains(label)) {
                        button->click();
                        return;
                    }
                }
            }
        };
        QTimer::singleShot(0, [&clickLeave] { clickLeave(QStringLiteral("Cancel")); });
        window.leaveFile();
        check(QFileInfo::exists(crap) && window.currentPath() == QFileInfo(crap).absoluteFilePath()
                  && window.editor()->toPlainText().contains(QLatin1String("junk")),
              "escape cancel stays in the file");
        QTimer::singleShot(0, [&clickLeave] { clickLeave(QStringLiteral("Discard")); });
        window.leaveFile();
        QFile kept(crap);
        const bool keptOpen = kept.open(QIODevice::ReadOnly);
        check(keptOpen && QFileInfo::exists(crap) && QString::fromUtf8(kept.readAll()) == original
                  && window.currentPath().isEmpty(),
              "escape discard keeps the file");
        check(window.openFile(crap), "reopen the kept file");
        QTimer::singleShot(0, [&clickLeave] { clickLeave(QStringLiteral("Delete")); });
        window.leaveFile();
        check(!QFileInfo::exists(crap) && window.currentPath().isEmpty(), "escape delete removes the file");

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
        const QList<GitStep> steps = gitPublishSteps(work, QStringLiteral("_posts/2026-10-25-hello.md"),
                                                     QStringLiteral("Post: Hello"), QStringLiteral("origin"),
                                                     QStringLiteral("main"));
        const QString addLine = steps.isEmpty() ? QString() : gitCommandLine(steps.first().args);
        const QString pushLine = steps.isEmpty() ? QString() : gitCommandLine(steps.last().args);
        check(steps.size() == 3 && addLine.contains(work) && addLine.contains(QStringLiteral("_posts/2026-10-25-hello.md"))
                  && addLine.startsWith(QLatin1String("git ")) && pushLine.contains(QStringLiteral("push"))
                  && pushLine.contains(QStringLiteral("origin")) && pushLine.contains(QStringLiteral("HEAD:main")),
              "publish command names the repo", addLine + QStringLiteral(" | ") + pushLine);
        publisher.publish(work, QStringLiteral("_posts/2026-10-25-hello.md"), QStringLiteral("Post: Hello"),
                          QStringLiteral("origin"), QStringLiteral("main"));
        loop.exec();
        QProcess log;
        log.start(GitPublisher::gitProgram(), {QStringLiteral("--git-dir"), bare, QStringLiteral("log"), QStringLiteral("--format=%s"), QStringLiteral("main")});
        log.waitForFinished(10000);
        check(okPublish && QString::fromUtf8(log.readAll()).trimmed() == QLatin1String("Post: Hello"), "git publish to bare remote");
    }

    const QString longPost = QStringLiteral("---\ntitle: Hello\ndescription: \"The kettle has not boiled.\"\n---\n\n## Ignored\n\nThis body is not the excerpt.\n");
    check(post::excerpt(longPost) == QLatin1String("The kettle has not boiled."), "excerpt prefers description");
    check(post::excerpt(QStringLiteral("## Title\n\nFirst paragraph lives here.\n\nSecond.")) == QLatin1String("First paragraph lives here."),
          "excerpt from body");
    const QString link = QStringLiteral("https://thewhitewitchtm.github.io/whitewitch/blog/2026-10-25-hello.html");
    const QString announcement = post::blueskyAnnouncement(QStringLiteral("The kettle has not boiled."), link);
    check(announcement.startsWith(QLatin1String("The kettle has not boiled.\n\n")) && announcement.endsWith(link),
          "bluesky text is excerpt then link", announcement);
    check(BlueskyClient::graphemeLength(announcement) <= 300, "bluesky text fits");
    const QString huge(500, QLatin1Char('a'));
    const QString trimmed = post::blueskyAnnouncement(huge, link);
    check(trimmed.endsWith(link) && trimmed.contains(QStringLiteral("…")) && BlueskyClient::graphemeLength(trimmed) <= 300,
          "bluesky text keeps the link under 300", QString::number(BlueskyClient::graphemeLength(trimmed)));

    const char32_t austria[2] = {0x1F1E6u, 0x1F1F9u};
    check(emojiForShortcode(QStringLiteral("flag-at")) == QString::fromUcs4(austria, 2), "flag shortcode");
    check(emojiForShortcode(QStringLiteral(":smile:")) == QString::fromUtf8("😄"), "emoji shortcode");
    check(emojiForShortcode(QStringLiteral("it")).isEmpty(), "bare country code is not an emoji");

    QString owner;
    QString repoName;
    check(parseGitHubRemote(QStringLiteral("git@github.com:TheWhiteWitchTM/whitewitch.git"), &owner, &repoName)
              && owner == QLatin1String("TheWhiteWitchTM") && repoName == QLatin1String("whitewitch"),
          "parse github ssh remote");
    check(githubPagesUrl(owner, repoName) == QLatin1String("https://thewhitewitchtm.github.io/whitewitch"), "github pages project url");
    check(githubPagesUrl(QStringLiteral("TheWhiteWitchTM"), QStringLiteral("TheWhiteWitchTM.github.io"))
              == QLatin1String("https://thewhitewitchtm.github.io"),
          "github pages user url");

    const QString detected = tmp.filePath(QStringLiteral("detected"));
    writeFile(detected + QStringLiteral("/_config.yml"),
              QStringLiteral("title: Demo Witch\nurl: \"https://example.github.io\"\nbaseurl: /demo\n"));
    writeFile(detected + QStringLiteral("/collections/_notes/2026-01-02-hi.md"), QStringLiteral("---\ntitle: Hi\nlayout: default\n---\n\n"));
    bool isBlog = false;
    const Site inspected = inspectBlog(detected, &isBlog);
    check(isBlog && inspected.name == QLatin1String("Demo Witch")
              && inspected.url == QLatin1String("https://example.github.io/demo") && inspected.collections.size() == 1
              && inspected.collections.first().urlPattern == QLatin1String("/notes/{date}-{slug}.html")
              && inspected.layout == QLatin1String("default")
              && inspected.frontMatter.value(QStringLiteral("layout")).toString() == QLatin1String("default"),
          "inspect jekyll repo", inspected.url + QLatin1Char(' ') + inspected.layout + QLatin1Char(' ')
                                      + inspected.collections.value(0).urlPattern);

    const QString surveyed = tmp.filePath(QStringLiteral("surveyed"));
    writeFile(surveyed + QStringLiteral("/_config.yml"),
              QStringLiteral("title: Survey\nurl: \"https://example.github.io\"\nbaseurl: /survey\ndefaults:\n  - scope:\n      path: \"\"\n    values:\n      layout: post\n"));
    writeFile(surveyed + QStringLiteral("/_layouts/default.html"), QStringLiteral("<html></html>\n"));
    writeFile(surveyed + QStringLiteral("/_layouts/home.html"), QStringLiteral("<html></html>\n"));
    writeFile(surveyed + QStringLiteral("/_layouts/post.html"), QStringLiteral("<html></html>\n"));
    writeFile(surveyed + QStringLiteral("/index.md"), QStringLiteral("---\nlayout: home\ntitle: Home\n---\n\n"));
    writeFile(surveyed + QStringLiteral("/collections/_notes/2026-01-02-one.md"),
              QStringLiteral("---\nlayout: default\ntitle: One\ndate: 2026-01-02\ndescription: A\ntags: [tea, news]\nfeatured: true\n---\n\n"));
    writeFile(surveyed + QStringLiteral("/collections/_notes/2026-01-03-two.md"),
              QStringLiteral("---\nlayout: default\ntitle: Two\ndate: 2026-01-03\ndescription: B\ntags: [tea]\nfeatured: true\n---\n\n"));
    writeFile(surveyed + QStringLiteral("/collections/_clips/2026-01-04-a.md"),
              QStringLiteral("---\nlayout: default\ntitle: A\ndate: 2026-01-04\ndescription: C\ntags: [news]\nvideo: https://example\n---\n\n"));
    writeFile(surveyed + QStringLiteral("/collections/_clips/2026-01-05-b.md"),
              QStringLiteral("---\nlayout: default\ntitle: B\ndate: 2026-01-05\ndescription: D\ntags: [news]\nvideo: https://example/2\n---\n\n"));
    const Site survey = inspectBlog(surveyed);
    const Collection *clips = survey.collection(QStringLiteral("clips"));
    check(survey.layout == QLatin1String("default") && survey.layouts.contains(QStringLiteral("default"))
              && survey.layouts.contains(QStringLiteral("home")) && survey.layouts.contains(QStringLiteral("post"))
              && survey.layouts.first() == QLatin1String("default")
              && survey.tags == QStringList({QStringLiteral("news"), QStringLiteral("tea")})
              && survey.frontMatter.value(QStringLiteral("layout")).toString() == QLatin1String("default")
              && !survey.frontMatter.contains(QStringLiteral("featured")) && !survey.frontMatter.contains(QStringLiteral("video"))
              && clips && clips->frontMatter.contains(QStringLiteral("video"))
              && survey.collection(QStringLiteral("notes"))
              && survey.collection(QStringLiteral("notes"))->frontMatter.contains(QStringLiteral("featured")),
          "inspect stores the layout the posts use and the tags",
          survey.layout + QLatin1Char(' ') + survey.tags.join(QLatin1Char(',')) + QLatin1Char(' ')
              + survey.layouts.join(QLatin1Char(',')));

    const QString plain = tmp.filePath(QStringLiteral("plain-pages"));
    writeFile(plain + QStringLiteral("/_posts/2026-02-02-hi.md"), QStringLiteral("---\ntitle: Hi\n---\n\nHello\n"));
    const Site plainSite = inspectBlog(plain);
    check(plainSite.layout == QLatin1String("post") && plainSite.frontMatter.value(QStringLiteral("layout")).toString() == QLatin1String("post"),
          "a pages blog with no layouts uses post", plainSite.layout);

    const QString shot = tmp.filePath(QStringLiteral("shot.png"));
    writeFile(shot, QStringLiteral("not really a png"));
    const QString postFile = root + QStringLiteral("/collections/_blog/2026-10-25-hello.md");
    const QString fileUrl = QUrl::fromLocalFile(shot).toString();
    const QString draft = QStringLiteral("---\ntitle: \"Hello\"\ndate: 2026-10-25\ndescription: \"A card.\"\n---\n\n")
                          + QStringLiteral("See [other](%1)\n\n![cat](%2)\n\n[page](/magic/retro/)\n")
                                .arg(root + QStringLiteral("/collections/_blog/2026-10-12-second.md"), fileUrl);
    Site bundleSite = ww;
    bundleSite.path = root;
    const media::PublishFix bundled = media::fixOnPublish(draft, postFile, bundleSite);
    check(bundled.markdown.contains(QStringLiteral("{{ '/blog/2026-10-25-hello.png' | relative_url }}")),
          "image link becomes the public path", bundled.markdown);
    check(QFileInfo::exists(root + QStringLiteral("/collections/_blog/2026-10-25-hello.png")), "image copied beside the post");
    check(QFileInfo::exists(root + QStringLiteral("/blog/2026-10-25-hello.png")), "image copied to the served path");
    check(bundled.markdown.contains(QStringLiteral("image: /blog/2026-10-25-hello.png")), "front matter image", bundled.markdown);
    check(bundled.gitPaths.contains(QStringLiteral("collections/_blog/2026-10-25-hello.png"))
              && bundled.gitPaths.contains(QStringLiteral("blog/2026-10-25-hello.png")),
          "git add lists both copies", bundled.gitPaths.join(QLatin1Char(' ')));
    check(bundled.markdown.contains(QStringLiteral("https://thewhitewitchtm.github.io/whitewitch/blog/2026-10-12-second.html")),
          "local post link becomes the public url");
    check(bundled.markdown.contains(QStringLiteral("{{ '/magic/retro/' | relative_url }}")), "root link gets the site base");
    const QString preview = media::previewImages(bundled.markdown, QFileInfo(postFile).absolutePath(), root, bundleSite.url);
    check(preview.contains(QStringLiteral("file://")) && preview.contains(QStringLiteral("2026-10-25-hello.png")),
          "preview resolves the published image", preview);

    const QString mdxRoot = tmp.filePath(QStringLiteral("mira"));
    const QString mdxPost = mdxRoot + QStringLiteral("/blog/dev-blog/hello_world/page.mdx");
    writeFile(mdxPost, QStringLiteral("export const metadata = {\ntitle: \"Hello\",\n};\n\n"));
    Site mdxSite = mira;
    mdxSite.path = mdxRoot;
    const QString mdxDraft = QStringLiteral("export const metadata = {\ntitle: \"Hello\",\n};\n\n![cover](%1)\n").arg(fileUrl);
    const media::PublishFix mdxFixed = media::fixOnPublish(mdxDraft, mdxPost, mdxSite);
    check(mdxFixed.markdown.contains(QStringLiteral("/mira/blog/dev-blog/hello_world/hello_world.png")), "mdx image public path",
          mdxFixed.markdown);
    check(QFileInfo::exists(mdxRoot + QStringLiteral("/blog/dev-blog/hello_world/hello_world.png")), "mdx image beside page.mdx");
    check(QFileInfo::exists(mdxRoot + QStringLiteral("/public/blog/dev-blog/hello_world/hello_world.png")), "mdx image in public/");

    QFile wordList(QStringLiteral(":/words-en.txt"));
    check(wordList.open(QIODevice::ReadOnly) && wordList.size() > 100000, "word list bundled", QString::number(wordList.size()));
    QFile helpFile(QStringLiteral(":/help.html"));
    const QString helpHtml = helpFile.open(QIODevice::ReadOnly) ? QString::fromUtf8(helpFile.readAll()) : QString();
    check(helpHtml.contains(QLatin1String("Dictate")) && helpHtml.contains(QLatin1String("scratch that")), "help resource");

    check(Lexicon::english().known(QStringLiteral("happy")), "dictionary knows happy");
    check(!Lexicon::english().known(QStringLiteral("zzzzwrong")), "dictionary rejects nonsense");
    check(Lexicon::english().complete(QStringLiteral("happ")).contains(QStringLiteral("happy")), "completion offers happy");
    check(Lexicon::english().extends(QStringLiteral("happ")), "happ can still grow");
    check(Lexicon::english().typos(QStringLiteral("happ"), 4).isEmpty(), "no underline while the caret is still in the prefix");
    const QVector<WordHit> finished = Lexicon::english().typos(QStringLiteral("happ "), 5);
    const bool happKnown = Lexicon::english().known(QStringLiteral("happ"));
    check(happKnown || (finished.size() == 1 && finished.first().start == 0 && finished.first().length == 4),
          "prefix is a typo once the caret leaves it");
    const QString nonsense = QStringLiteral("The kettle zzzzwrong today.");
    const QVector<WordHit> typos = Lexicon::english().typos(nonsense, nonsense.size());
    check(typos.size() == 1 && nonsense.mid(typos.first().start, typos.first().length) == QLatin1String("zzzzwrong"),
          "finished nonsense word is underlined");
    check(Lexicon::english().typos(QStringLiteral("---\ntitle: zzzzwrong\n---\n\nHi\n"), 20).isEmpty(), "front matter is not spellchecked");
    check(Lexicon::english().typos(QStringLiteral("```\nzzzzwrong\n```\n"), 8).isEmpty(), "fenced code is not spellchecked");

    DictateCursor spoken;
    check(dictateMarkdown(QStringLiteral("heading two the market fell"), QString(), &spoken).text == QLatin1String("## The market fell"),
          "dictate heading");
    spoken = {};
    const DictatePiece bold = dictateMarkdown(QStringLiteral("bold quiet end bold"), QString(), &spoken);
    check(bold.text == QLatin1String("**Quiet**"), "dictate bold", bold.text);
    spoken = {};
    spoken.cap = false;
    check(dictateMarkdown(QStringLiteral("bold quiet end bold"), QStringLiteral("Hello "), &spoken).text == QLatin1String("**quiet**"),
          "dictate bold mid sentence");
    spoken = {};
    check(dictateMarkdown(QStringLiteral("hello comma world"), QString(), &spoken).text == QLatin1String("Hello, world"), "dictate comma");
    spoken = {};
    check(dictateMarkdown(QStringLiteral("bullet milk"), QString(), &spoken).text == QLatin1String("- Milk"), "dictate bullet");
    spoken = {};
    check(dictateMarkdown(QStringLiteral("hello new paragraph there"), QString(), &spoken).text == QLatin1String("Hello\n\nThere"),
          "dictate paragraph");
    spoken = {};
    check(dictateMarkdown(QStringLiteral("code block python print hello end code"), QString(), &spoken).text
              == QLatin1String("```python\nprint hello\n```\n"),
          "dictate code fence");
    spoken = {};
    check(dictateMarkdown(QStringLiteral("heading for the win"), QString(), &spoken).text == QLatin1String("## For the win"),
          "heading ignores the word for");
    spoken = {};
    const DictatePiece scratched = dictateMarkdown(QStringLiteral("scratch that"), QString(), &spoken);
    check(scratched.scratch && scratched.text.isEmpty(), "scratch that drops the phrase");
    const DictatePiece finish = dictateMarkdown(QStringLiteral("Finish the post."), QString(), &spoken);
    check(finish.finish && finish.text.isEmpty() && !finish.scratch, "finish the post is a command", finish.text);
    const DictatePiece writeSpoken = dictateMarkdown(QStringLiteral("write the post"), QString(), &spoken);
    check(writeSpoken.finish && writeSpoken.text.isEmpty(), "write the post is the same command");
    const DictatePiece notFinish = dictateMarkdown(QStringLiteral("write the post about rain"), QString(), &spoken);
    check(!notFinish.finish && notFinish.text.contains(QLatin1String("rain")), "a sentence is not the command", notFinish.text);

    const QByteArray authJson = QByteArrayLiteral(
        "{\"https://auth.x.ai::abc\":{\"key\":\"test-token\",\"expires_at\":\"2026-10-25T17:41:25.789650608Z\","
        "\"refresh_token\":\"r\",\"oidc_client_id\":\"c\",\"oidc_issuer\":\"https://auth.x.ai\","
        "\"email\":\"ada@example.com\",\"first_name\":\"Ada\"}}");
    GrokCliSession session;
    check(grokCliSessionFromAuth(authJson, &session) && session.accessToken == QLatin1String("test-token")
              && session.hasExpiry && session.expires.date() == QDate(2026, 10, 25)
              && session.expires.time() == QTime(17, 41, 25, 789) && session.issuer == QLatin1String("https://auth.x.ai")
              && session.email == QLatin1String("ada@example.com") && session.firstName == QLatin1String("Ada"),
          "grok auth token", session.accessToken + QLatin1Char(' ') + session.expires.toString(Qt::ISODateWithMs));
    check(!grokCliSessionFromAuth(QByteArrayLiteral("{\"other\":{}}"), &session), "grok auth without a token");

    MarkdownEdit edit;
    edit.setPlainText(QStringLiteral("Hello"));
    edit.setHeading(4);
    check(edit.toPlainText() == QLatin1String("#### Hello"), "heading four", edit.toPlainText());
    edit.setHeading(4);
    check(edit.toPlainText() == QLatin1String("Hello"), "heading toggles off", edit.toPlainText());
    edit.setPlainText(QString());
    edit.insertCodeBlock(QStringLiteral("python"), true);
    check(edit.toPlainText() == QLatin1String("```python\n\n```\n"), "python fence", edit.toPlainText());
    QTextCursor inside = edit.textCursor();
    inside.setPosition(edit.toPlainText().indexOf(QLatin1Char('\n')) + 1);
    edit.setTextCursor(inside);
    edit.insertCodeBlock(QStringLiteral("rust"), true);
    check(edit.toPlainText().startsWith(QLatin1String("```rust\n")), "fence language retargets", edit.toPlainText());
    edit.insertCodeBlock(QStringLiteral("go"), false);
    check(edit.toPlainText().startsWith(QLatin1String("```rust\n")) && !edit.toPlainText().contains(QLatin1String("```go")),
          "shortcut does not open a second fence", edit.toPlainText());
    edit.setPlainText(QStringLiteral("---\ntitle: T\n---\n\n## Market\n\n```python\n# not a heading\n```\n\n### Later\n"));
    const QVector<HeadingMark> marks = edit.headings();
    check(marks.size() == 2 && marks.at(0).level == 2 && marks.at(0).text == QLatin1String("Market") && marks.at(1).level == 3
              && marks.at(1).text == QLatin1String("Later"),
          "headings skip front matter and fences");
    edit.setPlainText(QStringLiteral("alpha beta Alpha"));
    QTextCursor atStart = edit.textCursor();
    atStart.movePosition(QTextCursor::Start);
    edit.setTextCursor(atStart);
    check(edit.findText(QStringLiteral("alpha"), true, false) && edit.textCursor().selectedText() == QLatin1String("alpha"),
          "find wraps to the first match");
    check(edit.findText(QStringLiteral("alpha"), true, false) && edit.textCursor().selectedText() == QLatin1String("Alpha"),
          "find is case insensitive");
    check(edit.replaceAllText(QStringLiteral("alpha"), QStringLiteral("gamma"), false) == 2
              && edit.toPlainText() == QLatin1String("gamma beta gamma"),
          "replace all", edit.toPlainText());
    {
        MarkdownEdit stream;
        stream.setPlainText(QStringLiteral("OLD"));
        stream.replaceAll(QStringLiteral("A"), false);
        stream.replaceAll(QStringLiteral("AB"), true);
        stream.replaceAll(QStringLiteral("ABC"), true);
        check(stream.toPlainText() == QLatin1String("ABC"), "streamed text lands", stream.toPlainText());
        stream.undo();
        check(stream.toPlainText() == QLatin1String("OLD"), "streamed replace is one undo", stream.toPlainText());
    }

    edit.setPlainText(QStringLiteral("happ"));
    QTextCursor tail = edit.textCursor();
    tail.movePosition(QTextCursor::End);
    edit.setTextCursor(tail);
    edit.resize(640, 480);
    edit.show();
    QEventLoop suggestWait;
    QTimer::singleShot(300, &suggestWait, &QEventLoop::quit);
    suggestWait.exec();
    QListWidget *suggestions = edit.findChild<QListWidget *>(QStringLiteral("suggestList"));
    check(suggestions && suggestions->isVisible() && suggestions->count() > 0
              && suggestions->item(0)->text().startsWith(QLatin1String("happ")),
          "suggest popup", suggestions ? suggestions->item(0)->text() : QString());
    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QApplication::sendEvent(&edit, &tab);
    const QString completed = edit.toPlainText();
    check(completed.startsWith(QLatin1String("happ")) && completed.endsWith(QLatin1Char(' ')) && completed.trimmed().size() > 4,
          "tab completes the first suggestion", completed);

    edit.setPlainText(QStringLiteral("zzzzwrong "));
    QTextCursor afterTypo = edit.textCursor();
    afterTypo.movePosition(QTextCursor::End);
    edit.setTextCursor(afterTypo);
    QEventLoop spellWait;
    QTimer::singleShot(700, &spellWait, &QEventLoop::quit);
    spellWait.exec();
    check(!edit.extraSelections().isEmpty(), "spell underline");
    edit.hide();

    QTextDocument codeDoc;
    Highlighter codeHighlight(&codeDoc);
    codeDoc.setPlainText(QStringLiteral("```python\ndef hi():\n    return \"x\"\n```\n"));
    codeDoc.documentLayout()->documentSize();
    const QColor keywordColor(QStringLiteral("#d2a6ff"));
    const QColor stringColor(QStringLiteral("#ffb454"));
    bool sawKeyword = false;
    bool sawString = false;
    QTextLayout *defLayout = codeDoc.findBlockByNumber(1).layout();
    QTextLayout *stringLayout = codeDoc.findBlockByNumber(2).layout();
    if (defLayout) {
        for (const QTextLayout::FormatRange &range : defLayout->formats()) {
            if (range.start == 0 && range.length == 3 && range.format.foreground().color() == keywordColor)
                sawKeyword = true;
        }
    }
    if (stringLayout) {
        for (const QTextLayout::FormatRange &range : stringLayout->formats()) {
            if (range.format.foreground().color() == stringColor)
                sawString = true;
        }
    }
    check(sawKeyword && sawString, "fence syntax colors");

    std::fprintf(stderr, gFailed ? "%d check(s) failed\n" : "all checks passed\n", gFailed);
    return gFailed ? 1 : 0;
}
