#pragma once

#include <QByteArray>
#include <QObject>
#include <QSettings>
#include <QString>

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

private:
    QSettings settings_;
};
