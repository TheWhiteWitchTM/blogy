#include "ui/dialogs.h"

#include "bluesky.h"
#include "post.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

// ---------------------------------------------------------------- New post

NewPostDialog::NewPostDialog(const Config &config, int initialSite, QWidget *parent)
    : QDialog(parent), config_(config)
{
    setWindowTitle(QStringLiteral("New Post"));
    setMinimumWidth(560);
    auto *v = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    site_ = new QComboBox(this);
    for (const Site &s : config_.sites)
        site_->addItem(s.name);
    site_->setCurrentIndex(qBound(0, initialSite, qMax(0, config_.sites.size() - 1)));
    collection_ = new QComboBox(this);
    title_ = new QLineEdit(this);
    title_->setPlaceholderText(QStringLiteral("What is it called?"));
    slug_ = new QLineEdit(this);
    date_ = new QDateEdit(QDate::currentDate(), this);
    date_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    date_->setCalendarPopup(true);
    tags_ = new QLineEdit(this);
    tags_->setPlaceholderText(QStringLiteral("politics, tech"));
    description_ = new QLineEdit(this);
    description_->setPlaceholderText(QStringLiteral("One sentence for feeds and link cards"));
    form->addRow(QStringLiteral("Site"), site_);
    form->addRow(QStringLiteral("Collection"), collection_);
    form->addRow(QStringLiteral("Title"), title_);
    form->addRow(QStringLiteral("Slug"), slug_);
    form->addRow(QStringLiteral("Date"), date_);
    form->addRow(QStringLiteral("Tags"), tags_);
    form->addRow(QStringLiteral("Description"), description_);
    v->addLayout(form);
    preview_ = new QLabel(this);
    preview_->setObjectName(QStringLiteral("muted"));
    preview_->setWordWrap(true);
    preview_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(preview_);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    create_ = box->addButton(QStringLiteral("Create"), QDialogButtonBox::AcceptRole);
    create_->setObjectName(QStringLiteral("primary"));
    v->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(site_, &QComboBox::currentIndexChanged, this, [this] { refreshCollections(); refreshPreview(); });
    connect(collection_, &QComboBox::currentIndexChanged, this, &NewPostDialog::refreshPreview);
    connect(title_, &QLineEdit::textChanged, this, [this](const QString &t) {
        if (!slugTouched_ && siteIndex() >= 0)
            slug_->setText(post::slugify(t, config_.sites.at(siteIndex()).slugSeparator));
        refreshPreview();
    });
    connect(slug_, &QLineEdit::textEdited, this, [this] { slugTouched_ = !slug_->text().isEmpty(); refreshPreview(); });
    connect(date_, &QDateEdit::dateChanged, this, &NewPostDialog::refreshPreview);
    refreshCollections();
    refreshPreview();
}

int NewPostDialog::siteIndex() const { return config_.sites.isEmpty() ? -1 : site_->currentIndex(); }
QString NewPostDialog::collectionName() const { return collection_->currentText(); }
QString NewPostDialog::title() const { return title_->text().trimmed(); }
QString NewPostDialog::slug() const { return slug_->text().trimmed(); }
QDate NewPostDialog::date() const { return date_->date(); }
QString NewPostDialog::description() const { return description_->text().trimmed(); }

QStringList NewPostDialog::tags() const
{
    QStringList out;
    for (const QString &t : tags_->text().split(QLatin1Char(','))) {
        if (!t.trimmed().isEmpty())
            out << t.trimmed();
    }
    return out;
}

QString NewPostDialog::absolutePath() const
{
    if (siteIndex() < 0)
        return {};
    const Site &s = config_.sites.at(siteIndex());
    const Collection *c = s.collection(collectionName());
    if (!c)
        return {};
    return s.root() + QLatin1Char('/') + post::relativePath(*c, date(), slug().isEmpty() ? QStringLiteral("untitled") : slug());
}

void NewPostDialog::refreshCollections()
{
    collection_->clear();
    if (siteIndex() < 0)
        return;
    for (const Collection &c : config_.sites.at(siteIndex()).collections)
        collection_->addItem(c.name);
}

void NewPostDialog::refreshPreview()
{
    if (siteIndex() < 0) {
        preview_->setText(QStringLiteral("Add a site in Settings first."));
        create_->setEnabled(false);
        return;
    }
    const Site &s = config_.sites.at(siteIndex());
    const Collection *c = s.collection(collectionName());
    if (!c)
        return;
    const QString sl = slug().isEmpty() ? QStringLiteral("untitled") : slug();
    const QString path = absolutePath();
    QString text = QStringLiteral("File: %1\nURL after publish: %2").arg(path, post::publicUrl(s, *c, date(), sl));
    if (!QFileInfo::exists(s.root()))
        text += QStringLiteral("\nThe site folder does not exist yet. Clone the repo there or fix the path in Settings.");
    else if (QFileInfo::exists(path))
        text += QStringLiteral("\nA file with that name already exists.");
    preview_->setText(text);
    create_->setEnabled(!title().isEmpty() && QFileInfo::exists(s.root()) && !QFileInfo::exists(path));
}

// ---------------------------------------------------------------- Bluesky

BlueskyDialog::BlueskyDialog(Config *config, BlueskyClient *client, const QString &title, const QString &url,
                             const QString &description, QWidget *parent)
    : QDialog(parent), config_(config), client_(client)
{
    setWindowTitle(QStringLiteral("Post to Bluesky"));
    setMinimumWidth(560);
    auto *v = new QVBoxLayout(this);
    auto *who = new QLabel(this);
    who->setObjectName(QStringLiteral("muted"));
    const QString handle = config_->bskyHandle();
    who->setText(handle.isEmpty() ? QStringLiteral("No Bluesky handle yet. Add it in Settings or set BLUESKY_HANDLE.")
                                  : QStringLiteral("Posting as @%1 via %2").arg(handle, config_->bskyService));
    v->addWidget(who);
    text_ = new QPlainTextEdit(this);
    text_->setObjectName(QStringLiteral("postText"));
    text_->setPlainText(title + QStringLiteral("\n\n") + url);
    v->addWidget(text_, 1);
    count_ = new QLabel(this);
    count_->setObjectName(QStringLiteral("muted"));
    v->addWidget(count_);
    auto *form = new QFormLayout;
    url_ = new QLineEdit(url, this);
    cardTitle_ = new QLineEdit(title, this);
    cardDesc_ = new QLineEdit(description, this);
    form->addRow(QStringLiteral("Link card"), url_);
    form->addRow(QStringLiteral("Card title"), cardTitle_);
    form->addRow(QStringLiteral("Card text"), cardDesc_);
    v->addLayout(form);
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setOpenExternalLinks(true);
    v->addWidget(status_);
    auto *buttons = new QHBoxLayout;
    auto *grok = new QPushButton(QStringLiteral("Ask Grok for a blurb"), this);
    auto *close = new QPushButton(QStringLiteral("Close"), this);
    post_ = new QPushButton(QStringLiteral("Post"), this);
    post_->setObjectName(QStringLiteral("primary"));
    buttons->addWidget(grok);
    buttons->addStretch(1);
    buttons->addWidget(close);
    buttons->addWidget(post_);
    v->addLayout(buttons);

    connect(text_, &QPlainTextEdit::textChanged, this, &BlueskyDialog::refreshCount);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(grok, &QPushButton::clicked, this, &BlueskyDialog::askGrok);
    connect(post_, &QPushButton::clicked, this, [this] {
        post_->setEnabled(false);
        status_->setText(QStringLiteral("Posting…"));
        client_->post(config_->bskyService, config_->bskyHandle(), config_->bskyPassword(), text_->toPlainText().trimmed(),
                      url_->text().trimmed(), cardTitle_->text().trimmed(), cardDesc_->text().trimmed());
    });
    connect(client_, &BlueskyClient::posted, this, [this](const QString &web) {
        status_->setText(QStringLiteral("Posted: <a href=\"%1\" style=\"color:#ffb454\">%1</a>").arg(web.toHtmlEscaped()));
        post_->setText(QStringLiteral("Posted"));
    });
    connect(client_, &BlueskyClient::failed, this, [this](const QString &message) {
        status_->setText(message.toHtmlEscaped());
        refreshCount();
    });
    refreshCount();
}

void BlueskyDialog::setText(const QString &text)
{
    QString t = text.trimmed();
    if (!url_->text().isEmpty() && !t.contains(url_->text()))
        t += QStringLiteral("\n\n") + url_->text();
    text_->setPlainText(t);
}

void BlueskyDialog::refreshCount()
{
    const int n = BlueskyClient::graphemeLength(text_->toPlainText().trimmed());
    count_->setText(QStringLiteral("%1 / 300").arg(n));
    post_->setEnabled(n > 0 && n <= 300 && post_->text() != QLatin1String("Posted"));
}

// ---------------------------------------------------------------- Settings

SettingsDialog::SettingsDialog(Config *config, QWidget *parent)
    : QDialog(parent), config_(config), sites_(config->sites)
{
    setWindowTitle(QStringLiteral("Settings"));
    resize(820, 600);
    auto *v = new QVBoxLayout(this);
    auto *path = new QLabel(QStringLiteral("Saved to %1 (owner-only, never part of a repo).").arg(config_->path()), this);
    path->setObjectName(QStringLiteral("muted"));
    path->setWordWrap(true);
    v->addWidget(path);
    auto *tabs = new QTabWidget(this);
    v->addWidget(tabs, 1);

    // Sites
    auto *sitesTab = new QWidget(tabs);
    auto *sh = new QHBoxLayout(sitesTab);
    auto *left = new QVBoxLayout;
    siteList_ = new QListWidget(sitesTab);
    for (const Site &s : sites_)
        siteList_->addItem(s.name);
    left->addWidget(siteList_, 1);
    auto *addSite = new QPushButton(QStringLiteral("Add"), sitesTab);
    auto *dupSite = new QPushButton(QStringLiteral("Duplicate"), sitesTab);
    auto *delSite = new QPushButton(QStringLiteral("Remove"), sitesTab);
    left->addWidget(addSite);
    left->addWidget(dupSite);
    left->addWidget(delSite);
    sh->addLayout(left, 1);
    auto *right = new QVBoxLayout;
    auto *help = new QLabel(QStringLiteral(
        "One site as JSON. type is \"jekyll\" (YAML front matter) or \"mdx\" (export const metadata). "
        "Patterns take {date} {year} {month} {day} {slug} {category}. url is the public base URL, baseurl included."), sitesTab);
    help->setObjectName(QStringLiteral("hint"));
    help->setWordWrap(true);
    right->addWidget(help);
    siteJson_ = new QPlainTextEdit(sitesTab);
    siteJson_->setObjectName(QStringLiteral("jsonEdit"));
    siteJson_->setFont(QFont(QStringLiteral("DejaVu Sans Mono"), 10));
    right->addWidget(siteJson_, 1);
    siteError_ = new QLabel(sitesTab);
    siteError_->setObjectName(QStringLiteral("muted"));
    right->addWidget(siteError_);
    sh->addLayout(right, 3);
    tabs->addTab(sitesTab, QStringLiteral("Sites"));

    connect(siteList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (currentRow_ >= 0 && currentRow_ != row && !storeSite(currentRow_)) {
            QSignalBlocker block(siteList_);
            siteList_->setCurrentRow(currentRow_);
            return;
        }
        loadSite(row);
    });
    connect(addSite, &QPushButton::clicked, this, [this] {
        if (currentRow_ >= 0 && !storeSite(currentRow_))
            return;
        Site s = Site::fromJson(QJsonObject{{QStringLiteral("name"), QStringLiteral("My blog")},
                                            {QStringLiteral("path"), QStringLiteral("~/Projects/my-blog")},
                                            {QStringLiteral("url"), QStringLiteral("https://username.github.io")}});
        s.frontMatter = QJsonObject{{QStringLiteral("layout"), QStringLiteral("post")},
                                    {QStringLiteral("title"), QString()},
                                    {QStringLiteral("date"), QStringLiteral("{date}")},
                                    {QStringLiteral("tags"), QJsonArray()}};
        sites_.append(s);
        siteList_->addItem(s.name);
        currentRow_ = -1;
        siteList_->setCurrentRow(sites_.size() - 1);
    });
    connect(dupSite, &QPushButton::clicked, this, [this] {
        if (currentRow_ < 0 || !storeSite(currentRow_))
            return;
        Site s = sites_.at(currentRow_);
        s.name += QStringLiteral(" copy");
        sites_.append(s);
        siteList_->addItem(s.name);
        siteList_->setCurrentRow(sites_.size() - 1);
    });
    connect(delSite, &QPushButton::clicked, this, [this] {
        const int row = siteList_->currentRow();
        if (row < 0)
            return;
        currentRow_ = -1;
        sites_.removeAt(row);
        delete siteList_->takeItem(row);
        loadSite(siteList_->currentRow());
    });

    // AI and Bluesky
    auto *accounts = new QWidget(tabs);
    auto *af = new QFormLayout(accounts);
    auto envNote = [this](const QString &which, const char *var) {
        return config_->secretSource(which) == QLatin1String("env")
                   ? QStringLiteral("%1 is set in the environment and wins over this field.").arg(QString::fromLatin1(var))
                   : QStringLiteral("Or set %1 in the environment.").arg(QString::fromLatin1(var));
    };
    grokEndpoint_ = new QLineEdit(config_->grokEndpoint, accounts);
    grokModel_ = new QLineEdit(config_->grokModel, accounts);
    grokKey_ = new QLineEdit(config_->fileGrokKey, accounts);
    grokKey_->setEchoMode(QLineEdit::Password);
    grokKey_->setPlaceholderText(envNote(QStringLiteral("grok"), "XAI_API_KEY"));
    bskyService_ = new QLineEdit(config_->bskyService, accounts);
    bskyHandle_ = new QLineEdit(config_->fileBskyHandle, accounts);
    bskyHandle_->setPlaceholderText(envNote(QStringLiteral("handle"), "BLUESKY_HANDLE"));
    bskyPassword_ = new QLineEdit(config_->fileBskyPassword, accounts);
    bskyPassword_->setEchoMode(QLineEdit::Password);
    bskyPassword_->setPlaceholderText(envNote(QStringLiteral("password"), "BLUESKY_APP_PASSWORD"));
    auto *grokLabel = new QLabel(QStringLiteral("Grok (xAI)"), accounts);
    grokLabel->setObjectName(QStringLiteral("section"));
    af->addRow(grokLabel);
    af->addRow(QStringLiteral("Endpoint"), grokEndpoint_);
    af->addRow(QStringLiteral("Model"), grokModel_);
    af->addRow(QStringLiteral("API key"), grokKey_);
    auto *bskyLabel = new QLabel(QStringLiteral("Bluesky"), accounts);
    bskyLabel->setObjectName(QStringLiteral("section"));
    af->addRow(bskyLabel);
    af->addRow(QStringLiteral("Service (PDS)"), bskyService_);
    af->addRow(QStringLiteral("Handle"), bskyHandle_);
    af->addRow(QStringLiteral("App password"), bskyPassword_);
    auto *appPw = new QLabel(QStringLiteral("Use an app password from Bluesky Settings → Privacy and security → App passwords, "
                                            "never your main password."), accounts);
    appPw->setObjectName(QStringLiteral("hint"));
    appPw->setWordWrap(true);
    af->addRow(appPw);
    tabs->addTab(accounts, QStringLiteral("AI and Bluesky"));

    // Editor
    auto *editor = new QWidget(tabs);
    auto *ef = new QFormLayout(editor);
    fontSize_ = new QSpinBox(editor);
    fontSize_->setRange(8, 40);
    fontSize_->setValue(config_->fontSize);
    wrap_ = new QCheckBox(QStringLiteral("Soft wrap long lines"), editor);
    wrap_->setChecked(config_->wrap);
    ef->addRow(QStringLiteral("Font size"), fontSize_);
    ef->addRow(QString(), wrap_);
    auto *openFolder = new QPushButton(QStringLiteral("Open config folder"), editor);
    connect(openFolder, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(config_->path()).absolutePath()));
    });
    ef->addRow(QString(), openFolder);
    tabs->addTab(editor, QStringLiteral("Editor"));

    auto *box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    box->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("primary"));
    connect(box, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(box);

    if (!sites_.isEmpty())
        siteList_->setCurrentRow(0);
}

void SettingsDialog::loadSite(int row)
{
    currentRow_ = row;
    siteError_->clear();
    if (row < 0 || row >= sites_.size()) {
        siteJson_->clear();
        siteJson_->setEnabled(false);
        return;
    }
    siteJson_->setEnabled(true);
    siteJson_->setPlainText(QString::fromUtf8(QJsonDocument(sites_.at(row).toJson()).toJson(QJsonDocument::Indented)));
}

bool SettingsDialog::storeSite(int row)
{
    if (row < 0 || row >= sites_.size())
        return true;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(siteJson_->toPlainText().toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        siteError_->setText(QStringLiteral("Not valid JSON: %1 at %2").arg(err.errorString()).arg(err.offset));
        return false;
    }
    sites_[row] = Site::fromJson(doc.object());
    if (QListWidgetItem *item = siteList_->item(row))
        item->setText(sites_.at(row).name);
    siteError_->clear();
    return true;
}

void SettingsDialog::accept()
{
    if (!storeSite(currentRow_))
        return;
    config_->sites = sites_;
    config_->grokEndpoint = grokEndpoint_->text().trimmed();
    config_->grokModel = grokModel_->text().trimmed();
    config_->fileGrokKey = grokKey_->text().trimmed();
    config_->bskyService = bskyService_->text().trimmed();
    config_->fileBskyHandle = bskyHandle_->text().trimmed().remove(QLatin1Char('@'));
    config_->fileBskyPassword = bskyPassword_->text().trimmed();
    config_->fontSize = fontSize_->value();
    config_->wrap = wrap_->isChecked();
    QString error;
    if (!config_->save(&error)) {
        QMessageBox::warning(this, QStringLiteral("Settings"), QStringLiteral("Could not save: ") + error);
        return;
    }
    QDialog::accept();
}
