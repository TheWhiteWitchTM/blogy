#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// One folder of posts inside a site: a Jekyll collection, _posts, or an MDX tree.
struct Collection {
    QString name;
    QString dir;          // relative to the site root
    QString filePattern;  // e.g. "{date}-{slug}.md" or "{slug}/page.mdx"
    QString urlPattern;   // e.g. "/blog/{date}-{slug}.html"
    QJsonObject frontMatter; // overrides on top of the site defaults
};

struct Site {
    QString name;
    QString type = QStringLiteral("jekyll"); // "jekyll" (YAML front matter) or "mdx" (export const metadata)
    QString path;
    QString branch = QStringLiteral("main");
    QString remote = QStringLiteral("origin");
    QString url;
    QString slugSeparator = QStringLiteral("-");
    QString commitMessage = QStringLiteral("Post: {title}");
    QString templateText;
    QString signature; // appended to the post on publish
    QString tune;      // voice for Talk to AI, added to every request for this site
    // How Grok writes Markdown for this site. headingLevel is 1–6.
    // A template that is only a heading line is kept in step with headingLevel.
    int headingLevel = 2;
    QString emphasis = QStringLiteral("**"); // "**" or "__"
    QString listMarker = QStringLiteral("-"); // "-", "*", or "+"
    QString styleNotes;
    // Filename beside the post. {stem} {slug} {date} {ext} {n}. Default names the picture after the post.
    QString imagePattern = QStringLiteral("{stem}{ext}");
    bool autoFixLinks = true;
    bool hasAutoFix = false; // true when the site JSON set autoFixLinks itself
    // Jekyll / GitHub Pages layout new posts use. Empty for MDX.
    QString layout;
    QStringList layouts; // names found in _layouts and in the posts
    QStringList tags;    // tags the site already uses, newest posts first by frequency
    QJsonObject frontMatter;
    QList<Collection> collections;

    bool isMdx() const { return type == QLatin1String("mdx"); }
    QString root() const; // path with ~ expanded
    const Collection *collection(const QString &name) const;

    // 0 when the template does not start with a heading. Otherwise 1–6.
    static int leadingHeadingLevel(const QString &templateText);
    // Empty, or a single "{title}" heading line. New posts replace that with headingLevel.
    static bool isStockHeadingTemplate(const QString &templateText);
    QString headingTemplate() const;
    // Instructions added to every Grok request for this site.
    QString writingGuide() const;

    static Site fromJson(const QJsonObject &obj);
    QJsonObject toJson() const;
};

// Everything Blogy keeps between runs that matters: sites and secrets.
// Lives in ~/.config/blogy/config.json, never inside a git repo.
// Environment variables win over the file for secrets.
class Config {
public:
    // ~/.config/blogy, ignoring BLOGY_CONFIG. Window state sits next to the config.
    static QString configDir();
    // ~/.config/blogy/config.json. BLOGY_CONFIG wins unless that path is inside a git repo.
    static QString defaultPath();
    static QString userConfigPath();
    // True when path (or a parent) is a git work tree. Config must not be saved there.
    static bool isInsideGitWorkTree(const QString &path);
    QString path() const { return path_; }

    bool load(const QString &path = QString(), QString *error = nullptr);
    bool save(QString *error = nullptr) const;

    // Writes config.example.json to the config path when nothing is there yet.
    bool ensureExists(QString *error = nullptr);

    QList<Site> sites;
    int fontSize = 13;
    bool wrap = true;
    int previewDelayMs = 250;
    // On publish, copy attached pictures next to the post and rewrite links to the public URL.
    bool autoFixLinks = true;

    // "auto" uses the signed-in Grok CLI when it is installed, otherwise the API key.
    QString grokBackend = QStringLiteral("auto");
    QString grokCommand;
    QString grokEndpoint = QStringLiteral("https://api.x.ai/v1/chat/completions");
    QString grokModel = QStringLiteral("grok-4.7");
    QString bskyService = QStringLiteral("https://bsky.social");

    QString grokCommandResolved() const;
    bool grokCliAvailable() const;
    bool grokUsesCli() const;
    QString grokModelResolved() const;
    bool fixesLinks(const Site &site) const;

    // Values stored in the file. Use the resolved getters to read effective values.
    QString fileGrokKey;
    QString fileBskyHandle;
    QString fileBskyPassword;

    QString grokApiKey() const;     // XAI_API_KEY, then file
    QString bskyHandle() const;     // BLUESKY_HANDLE, then file
    QString bskyPassword() const;   // BLUESKY_APP_PASSWORD, then file
    QString secretSource(const QString &which) const; // "env", "config", or "missing"

    QJsonObject toJson() const;
    void fromJson(const QJsonObject &root);

    static QString expandHome(const QString &path);

private:
    QString path_;
};

QJsonObject exampleConfig();
