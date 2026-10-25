#include "settings.h"

#include "config.h"

namespace {
QString settingsFile()
{
    // Self-test points this at a temp file so it does not rewrite ~/.config/blogy/blogy.conf.
    const QByteArray env = qgetenv("BLOGY_SETTINGS");
    if (!env.isEmpty())
        return QString::fromLocal8Bit(env);
    return Config::configDir() + QStringLiteral("/blogy.conf");
}
} // namespace

Settings::Settings(QObject *parent)
    : QObject(parent), settings_(settingsFile(), QSettings::IniFormat)
{
}

QByteArray Settings::geometry() const { return settings_.value(QStringLiteral("geometry")).toByteArray(); }
void Settings::setGeometry(const QByteArray &bytes) { settings_.setValue(QStringLiteral("geometry"), bytes); }
QByteArray Settings::state() const { return settings_.value(QStringLiteral("state")).toByteArray(); }
void Settings::setState(const QByteArray &bytes) { settings_.setValue(QStringLiteral("state"), bytes); }
QByteArray Settings::splitter() const { return settings_.value(QStringLiteral("splitter")).toByteArray(); }
void Settings::setSplitter(const QByteArray &bytes) { settings_.setValue(QStringLiteral("splitter"), bytes); }
QString Settings::lastSite() const { return settings_.value(QStringLiteral("lastSite")).toString(); }
void Settings::setLastSite(const QString &name) { settings_.setValue(QStringLiteral("lastSite"), name); }
QString Settings::lastFile() const { return settings_.value(QStringLiteral("lastFile")).toString(); }
void Settings::setLastFile(const QString &path) { settings_.setValue(QStringLiteral("lastFile"), path); }
bool Settings::previewVisible() const { return settings_.value(QStringLiteral("preview"), false).toBool(); }
void Settings::setPreviewVisible(bool on) { settings_.setValue(QStringLiteral("preview"), on); }
bool Settings::toolsVisible() const { return settings_.value(QStringLiteral("tools"), false).toBool(); }
void Settings::setToolsVisible(bool on) { settings_.setValue(QStringLiteral("tools"), on); }
bool Settings::filesVisible() const { return settings_.value(QStringLiteral("files"), true).toBool(); }
void Settings::setFilesVisible(bool on) { settings_.setValue(QStringLiteral("files"), on); }
int Settings::filesWidth() const { return settings_.value(QStringLiteral("filesWidth"), 280).toInt(); }
void Settings::setFilesWidth(int width) { settings_.setValue(QStringLiteral("filesWidth"), width); }
QStringList Settings::dictionary() const { return settings_.value(QStringLiteral("dictionary")).toStringList(); }
void Settings::setDictionary(const QStringList &words) { settings_.setValue(QStringLiteral("dictionary"), words); }

namespace {
const QString kRenames = QStringLiteral("pendingRenames");
}

QString Settings::pendingRename(const QString &absolutePath) const
{
    const QStringList all = settings_.value(kRenames).toStringList();
    for (const QString &row : all) {
        const int tab = row.indexOf(QLatin1Char('\t'));
        if (tab > 0 && row.left(tab) == absolutePath)
            return row.mid(tab + 1);
    }
    return {};
}

void Settings::setPendingRename(const QString &absolutePath, const QString &oldRelative)
{
    QStringList all = settings_.value(kRenames).toStringList();
    for (int i = all.size() - 1; i >= 0; --i) {
        const int tab = all.at(i).indexOf(QLatin1Char('\t'));
        if (tab > 0 && all.at(i).left(tab) == absolutePath)
            all.removeAt(i);
    }
    all.append(absolutePath + QLatin1Char('\t') + oldRelative);
    settings_.setValue(kRenames, all);
}

void Settings::clearPendingRename(const QString &absolutePath)
{
    QStringList all = settings_.value(kRenames).toStringList();
    bool changed = false;
    for (int i = all.size() - 1; i >= 0; --i) {
        const int tab = all.at(i).indexOf(QLatin1Char('\t'));
        if (tab > 0 && all.at(i).left(tab) == absolutePath) {
            all.removeAt(i);
            changed = true;
        }
    }
    if (changed)
        settings_.setValue(kRenames, all);
}
