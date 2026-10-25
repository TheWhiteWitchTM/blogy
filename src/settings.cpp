#include "settings.h"

Settings::Settings(QObject *parent) : QObject(parent) {}

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
bool Settings::previewVisible() const { return settings_.value(QStringLiteral("preview"), true).toBool(); }
void Settings::setPreviewVisible(bool on) { settings_.setValue(QStringLiteral("preview"), on); }
