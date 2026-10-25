#pragma once

#include "config.h"

#include <QDate>
#include <QDialog>

class BlueskyClient;
class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

// New post from the site's template: picks the folder, the file name, and the front matter.
class NewPostDialog : public QDialog {
    Q_OBJECT
public:
    NewPostDialog(const Config &config, int siteIndex, QWidget *parent = nullptr);

    int siteIndex() const;
    QString collectionName() const;
    QString title() const;
    QString slug() const;
    QDate date() const;
    QStringList tags() const;
    QString description() const;
    QString absolutePath() const;

private:
    void refreshCollections();
    void refreshPreview();

    const Config &config_;
    QComboBox *site_ = nullptr;
    QComboBox *collection_ = nullptr;
    QLineEdit *title_ = nullptr;
    QLineEdit *slug_ = nullptr;
    QDateEdit *date_ = nullptr;
    QLineEdit *tags_ = nullptr;
    QLineEdit *description_ = nullptr;
    QLabel *preview_ = nullptr;
    QPushButton *create_ = nullptr;
    bool slugTouched_ = false;
};

// After a publish: write a Bluesky post with a link card for the new page.
class BlueskyDialog : public QDialog {
    Q_OBJECT
public:
    BlueskyDialog(Config *config, BlueskyClient *client, const QString &title, const QString &url,
                  const QString &description, QWidget *parent = nullptr);
    void setText(const QString &text);

signals:
    void askGrok();

private:
    void refreshCount();

    Config *config_;
    BlueskyClient *client_;
    QPlainTextEdit *text_ = nullptr;
    QLineEdit *url_ = nullptr;
    QLineEdit *cardTitle_ = nullptr;
    QLineEdit *cardDesc_ = nullptr;
    QLabel *count_ = nullptr;
    QLabel *status_ = nullptr;
    QPushButton *post_ = nullptr;
};

// Sites, Grok, Bluesky, and editor settings. Saves to the local config file.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(Config *config, QWidget *parent = nullptr);

private:
    void loadSite(int row);
    bool storeSite(int row);
    void accept() override;

    Config *config_;
    QList<Site> sites_;
    int currentRow_ = -1;
    QListWidget *siteList_ = nullptr;
    QPlainTextEdit *siteJson_ = nullptr;
    QLabel *siteError_ = nullptr;
    QLineEdit *grokEndpoint_ = nullptr;
    QLineEdit *grokModel_ = nullptr;
    QLineEdit *grokKey_ = nullptr;
    QLineEdit *bskyService_ = nullptr;
    QLineEdit *bskyHandle_ = nullptr;
    QLineEdit *bskyPassword_ = nullptr;
    QSpinBox *fontSize_ = nullptr;
    QCheckBox *wrap_ = nullptr;
};
