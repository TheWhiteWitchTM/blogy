#include "config.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

QString Site::root() const
{
    return Config::expandHome(path);
}

const Collection *Site::collection(const QString &name) const
{
    for (const Collection &c : collections) {
        if (c.name == name)
            return &c;
    }
    return collections.isEmpty() ? nullptr : &collections.first();
}

Site Site::fromJson(const QJsonObject &obj)
{
    Site s;
    s.name = obj.value(QStringLiteral("name")).toString();
    s.type = obj.value(QStringLiteral("type")).toString(QStringLiteral("jekyll"));
    s.path = obj.value(QStringLiteral("path")).toString();
    s.branch = obj.value(QStringLiteral("branch")).toString(QStringLiteral("main"));
    s.remote = obj.value(QStringLiteral("remote")).toString(QStringLiteral("origin"));
    s.url = obj.value(QStringLiteral("url")).toString();
    s.slugSeparator = obj.value(QStringLiteral("slugSeparator")).toString(QStringLiteral("-"));
    s.commitMessage = obj.value(QStringLiteral("commitMessage")).toString(QStringLiteral("Post: {title}"));
    s.templateText = obj.value(QStringLiteral("template")).toString();
    s.signature = obj.value(QStringLiteral("signature")).toString();
    s.tune = obj.value(QStringLiteral("tune")).toString();
    s.imagePattern = obj.value(QStringLiteral("imagePattern")).toString(QStringLiteral("{stem}{ext}"));
    if (s.imagePattern.trimmed().isEmpty())
        s.imagePattern = QStringLiteral("{stem}{ext}");
    if (obj.contains(QStringLiteral("autoFixLinks"))) {
        s.autoFixLinks = obj.value(QStringLiteral("autoFixLinks")).toBool(true);
        s.hasAutoFix = true;
    }
    s.frontMatter = obj.value(QStringLiteral("frontMatter")).toObject();
    const QJsonArray cols = obj.value(QStringLiteral("collections")).toArray();
    for (const QJsonValue &v : cols) {
        const QJsonObject o = v.toObject();
        Collection c;
        c.name = o.value(QStringLiteral("name")).toString();
        c.dir = o.value(QStringLiteral("dir")).toString();
        c.filePattern = o.value(QStringLiteral("file")).toString(QStringLiteral("{date}-{slug}.md"));
        c.urlPattern = o.value(QStringLiteral("url")).toString();
        c.frontMatter = o.value(QStringLiteral("frontMatter")).toObject();
        s.collections.append(c);
    }
    if (s.collections.isEmpty()) {
        Collection c;
        c.name = QStringLiteral("posts");
        c.dir = QStringLiteral("_posts");
        c.filePattern = QStringLiteral("{date}-{slug}.md");
        c.urlPattern = QStringLiteral("/{year}/{month}/{day}/{slug}.html");
        s.collections.append(c);
    }
    return s;
}

QJsonObject Site::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("name"), name);
    obj.insert(QStringLiteral("type"), type);
    obj.insert(QStringLiteral("path"), path);
    obj.insert(QStringLiteral("branch"), branch);
    obj.insert(QStringLiteral("remote"), remote);
    obj.insert(QStringLiteral("url"), url);
    obj.insert(QStringLiteral("slugSeparator"), slugSeparator);
    obj.insert(QStringLiteral("commitMessage"), commitMessage);
    obj.insert(QStringLiteral("template"), templateText);
    if (!signature.trimmed().isEmpty())
        obj.insert(QStringLiteral("signature"), signature);
    if (!tune.trimmed().isEmpty())
        obj.insert(QStringLiteral("tune"), tune);
    if (!imagePattern.isEmpty())
        obj.insert(QStringLiteral("imagePattern"), imagePattern);
    if (hasAutoFix)
        obj.insert(QStringLiteral("autoFixLinks"), autoFixLinks);
    obj.insert(QStringLiteral("frontMatter"), frontMatter);
    QJsonArray cols;
    for (const Collection &c : collections) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), c.name);
        o.insert(QStringLiteral("dir"), c.dir);
        o.insert(QStringLiteral("file"), c.filePattern);
        o.insert(QStringLiteral("url"), c.urlPattern);
        if (!c.frontMatter.isEmpty())
            o.insert(QStringLiteral("frontMatter"), c.frontMatter);
        cols.append(o);
    }
    obj.insert(QStringLiteral("collections"), cols);
    return obj;
}

QString Config::expandHome(const QString &path)
{
    if (path == QLatin1String("~"))
        return QDir::homePath();
    if (path.startsWith(QLatin1String("~/")))
        return QDir::homePath() + path.mid(1);
    return path;
}

QString Config::configDir()
{
    return QFileInfo(userConfigPath()).absolutePath();
}

QString Config::userConfigPath()
{
    // GenericConfigLocation + /blogy is the plain ~/.config/blogy the README promises.
    // AppConfigLocation would nest another blogy/ under the organization name.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return base + QStringLiteral("/blogy/config.json");
}

bool Config::isInsideGitWorkTree(const QString &path)
{
    if (path.trimmed().isEmpty())
        return false;
    const QFileInfo info(path);
    QDir dir = info.isDir() ? QDir(info.absoluteFilePath()) : info.absoluteDir();
    for (int i = 0; i < 64; ++i) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral(".git"))))
            return true;
        const QString here = dir.absolutePath();
        if (!dir.cdUp() || dir.absolutePath() == here)
            return false;
    }
    return false;
}

QString Config::defaultPath()
{
    const QByteArray env = qgetenv("BLOGY_CONFIG");
    if (!env.isEmpty()) {
        const QString chosen = expandHome(QString::fromLocal8Bit(env));
        if (!isInsideGitWorkTree(chosen))
            return chosen;
        qWarning().noquote() << QStringLiteral("Blogy: %1 is inside a git repository; using %2")
                                    .arg(chosen, userConfigPath());
    }
    return userConfigPath();
}

QJsonObject exampleConfig()
{
    QFile file(QStringLiteral(":/config.example.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool Config::ensureExists(QString *error)
{
    if (path_.isEmpty())
        path_ = defaultPath();
    if (QFileInfo::exists(path_))
        return true;
    fromJson(exampleConfig());
    return save(error);
}

bool Config::load(const QString &path, QString *error)
{
    path_ = path.isEmpty() ? defaultPath() : path;
    QFile file(path_);
    if (!file.exists()) {
        fromJson(exampleConfig());
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("%1 at offset %2").arg(parseError.errorString()).arg(parseError.offset);
        fromJson(exampleConfig());
        return false;
    }
    fromJson(doc.object());
    return true;
}

bool Config::save(QString *error) const
{
    const QString target = path_.isEmpty() ? defaultPath() : path_;
    if (isInsideGitWorkTree(target)) {
        if (error)
            *error = QStringLiteral("Refusing to write %1 inside a git repository. Blogy keeps config in %2.")
                         .arg(QFileInfo(target).absoluteFilePath(), userConfigPath());
        return false;
    }
    QDir().mkpath(QFileInfo(target).absolutePath());
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    // Secrets live here: owner read/write only.
    QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

void Config::fromJson(const QJsonObject &root)
{
    const QJsonObject editor = root.value(QStringLiteral("editor")).toObject();
    fontSize = editor.value(QStringLiteral("fontSize")).toInt(13);
    wrap = editor.value(QStringLiteral("wrap")).toBool(true);
    previewDelayMs = editor.value(QStringLiteral("previewDelayMs")).toInt(250);
    autoFixLinks = editor.contains(QStringLiteral("autoFixLinks")) ? editor.value(QStringLiteral("autoFixLinks")).toBool(true) : true;

    const QJsonObject grok = root.value(QStringLiteral("grok")).toObject();
    grokBackend = grok.value(QStringLiteral("backend")).toString(QStringLiteral("auto"));
    if (grokBackend != QLatin1String("cli") && grokBackend != QLatin1String("api") && grokBackend != QLatin1String("auto"))
        grokBackend = QStringLiteral("auto");
    grokCommand = grok.value(QStringLiteral("command")).toString();
    grokEndpoint = grok.value(QStringLiteral("endpoint")).toString(QStringLiteral("https://api.x.ai/v1/chat/completions"));
    grokModel = grok.value(QStringLiteral("model")).toString(QStringLiteral("grok-4.7"));
    fileGrokKey = grok.value(QStringLiteral("apiKey")).toString();

    const QJsonObject bsky = root.value(QStringLiteral("bluesky")).toObject();
    bskyService = bsky.value(QStringLiteral("service")).toString(QStringLiteral("https://bsky.social"));
    fileBskyHandle = bsky.value(QStringLiteral("handle")).toString();
    fileBskyPassword = bsky.value(QStringLiteral("appPassword")).toString();

    sites.clear();
    const QJsonArray arr = root.value(QStringLiteral("sites")).toArray();
    for (const QJsonValue &v : arr)
        sites.append(Site::fromJson(v.toObject()));
}

QJsonObject Config::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    QJsonObject editor;
    editor.insert(QStringLiteral("fontSize"), fontSize);
    editor.insert(QStringLiteral("wrap"), wrap);
    editor.insert(QStringLiteral("previewDelayMs"), previewDelayMs);
    editor.insert(QStringLiteral("autoFixLinks"), autoFixLinks);
    root.insert(QStringLiteral("editor"), editor);

    QJsonObject grok;
    grok.insert(QStringLiteral("backend"), grokBackend);
    if (!grokCommand.isEmpty())
        grok.insert(QStringLiteral("command"), grokCommand);
    grok.insert(QStringLiteral("endpoint"), grokEndpoint);
    grok.insert(QStringLiteral("model"), grokModel);
    grok.insert(QStringLiteral("apiKey"), fileGrokKey);
    root.insert(QStringLiteral("grok"), grok);

    QJsonObject bsky;
    bsky.insert(QStringLiteral("service"), bskyService);
    bsky.insert(QStringLiteral("handle"), fileBskyHandle);
    bsky.insert(QStringLiteral("appPassword"), fileBskyPassword);
    root.insert(QStringLiteral("bluesky"), bsky);

    QJsonArray arr;
    for (const Site &s : sites)
        arr.append(s.toJson());
    root.insert(QStringLiteral("sites"), arr);
    return root;
}

namespace {
QString envOr(const char *name, const QString &fallback)
{
    const QByteArray v = qgetenv(name);
    return v.isEmpty() ? fallback : QString::fromUtf8(v).trimmed();
}
} // namespace

QString Config::grokCommandResolved() const
{
    if (!grokCommand.trimmed().isEmpty())
        return expandHome(grokCommand.trimmed());
    const QString bundled = QDir::homePath() + QStringLiteral("/.grok/bin/grok");
    if (QFileInfo(bundled).isExecutable())
        return bundled;
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("grok"));
    return onPath;
}

bool Config::grokCliAvailable() const
{
    const QString command = grokCommandResolved();
    if (command.isEmpty() || !QFileInfo(command).isExecutable())
        return false;
    return QFileInfo(QDir::homePath() + QStringLiteral("/.grok/auth.json")).isReadable();
}

bool Config::grokUsesCli() const
{
    if (grokBackend == QLatin1String("api"))
        return false;
    if (grokBackend == QLatin1String("cli"))
        return true;
    return grokCliAvailable();
}

QString Config::grokModelResolved() const
{
    if (grokModel.trimmed().isEmpty())
        return QStringLiteral("grok-4.7");
    // The old default is an API name. The signed-in CLI lists grok-4.7.
    if (grokUsesCli() && grokModel == QLatin1String("grok-4"))
        return QStringLiteral("grok-4.7");
    return grokModel;
}

bool Config::fixesLinks(const Site &site) const
{
    return site.hasAutoFix ? site.autoFixLinks : autoFixLinks;
}

QString Config::grokApiKey() const { return envOr("XAI_API_KEY", fileGrokKey); }
QString Config::bskyHandle() const { return envOr("BLUESKY_HANDLE", fileBskyHandle); }
QString Config::bskyPassword() const { return envOr("BLUESKY_APP_PASSWORD", fileBskyPassword); }

QString Config::secretSource(const QString &which) const
{
    const char *env = nullptr;
    QString file;
    if (which == QLatin1String("grok")) {
        env = "XAI_API_KEY";
        file = fileGrokKey;
    } else if (which == QLatin1String("handle")) {
        env = "BLUESKY_HANDLE";
        file = fileBskyHandle;
    } else {
        env = "BLUESKY_APP_PASSWORD";
        file = fileBskyPassword;
    }
    if (!qgetenv(env).isEmpty())
        return QStringLiteral("env");
    return file.isEmpty() ? QStringLiteral("missing") : QStringLiteral("config");
}
