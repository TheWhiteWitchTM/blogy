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
    QJsonObject frontMatter;
    QList<Collection> collections;

    bool isMdx() const { return type == QLatin1String("mdx"); }
    QString root() const; // path with ~ expanded
    const Collection *collection(const QString &name) const;

    static Site fromJson(const QJsonObject &obj);
    QJsonObject toJson() const;
};

// Everything Blogy keeps between runs that matters: sites and secrets.
// Lives in ~/.config/blogy/config.json (QStandardPaths), never in a repo.
// Environment variables win over the file for secrets.
class Config {
public:
    static QString defaultPath();
    QString path() const { return path_; }

    bool load(const QString &path = QString(), QString *error = nullptr);
    bool save(QString *error = nullptr) const;

    // Writes config.example.json to the config path when nothing is there yet.
    bool ensureExists(QString *error = nullptr);

    QList<Site> sites;
    int fontSize = 13;
    bool wrap = true;
    int previewDelayMs = 250;

    QString grokEndpoint = QStringLiteral("https://api.x.ai/v1/chat/completions");
    QString grokModel = QStringLiteral("grok-4");
    QString bskyService = QStringLiteral("https://bsky.social");

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
