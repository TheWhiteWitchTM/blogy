#pragma once

#include <QByteArray>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>

// Window state only (geometry, splitters, last site). Lives in
// ~/.config/blogy/blogy.conf. Sites and secrets are in Config.
class Settings : public QObject {
    Q_OBJECT
public:
    explicit Settings(QObject *parent = nullptr);

    QByteArray geometry() const;
    void setGeometry(const QByteArray &bytes);
    QByteArray state() const;
    void setState(const QByteArray &bytes);
    QByteArray splitter() const;
    void setSplitter(const QByteArray &bytes);
    QString lastSite() const;
    void setLastSite(const QString &name);
    QString lastFile() const;
    void setLastFile(const QString &path);
    bool previewVisible() const;
    void setPreviewVisible(bool on);
    bool toolsVisible() const;
    void setToolsVisible(bool on);
    bool filesVisible() const;
    void setFilesVisible(bool on);
    int filesWidth() const;
    void setFilesWidth(int width);
    QStringList dictionary() const;
    void setDictionary(const QStringList &words);

    // A file renamed on save, remembered until publish so git can drop the old path.
    // oldRelative is relative to the site root.
    QString pendingRename(const QString &absolutePath) const;
    void setPendingRename(const QString &absolutePath, const QString &oldRelative);
    void clearPendingRename(const QString &absolutePath);

private:
    QSettings settings_;
};
