#include "ui/dialogs.h"

#include "bluesky.h"
#include "detect.h"
#include "grokauth.h"
#include "post.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDateEdit>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
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
    return s.root() + QLatin1Char('/') + post::relativePath(s, *c, date(), slug().isEmpty() ? QStringLiteral("untitled") : slug());
}

void NewPostDialog::refreshCollections()
{
    collection_->clear();
    if (siteIndex() < 0)
        return;
    const Site &site = config_.sites.at(siteIndex());
    for (const Collection &c : site.collections)
        collection_->addItem(c.name);
    tags_->setPlaceholderText(site.tags.isEmpty() ? QStringLiteral("politics, tech")
                                                  : site.tags.mid(0, 8).join(QStringLiteral(", ")));
    if (QCompleter *old = tags_->completer()) {
        tags_->setCompleter(nullptr);
        old->deleteLater();
    }
    if (!site.tags.isEmpty()) {
        auto *completer = new QCompleter(site.tags, tags_);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
        tags_->setCompleter(completer);
    }
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

namespace {

QLabel *sectionLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("section"));
    return label;
}

QLabel *hintLabel(const QString &text, QWidget *parent, int lines = 3)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("hint"));
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setMinimumWidth(360);
    label->setMinimumHeight(label->fontMetrics().lineSpacing() * lines + 8);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    return label;
}

QScrollArea *settingsScroll(QWidget *page, QWidget *inner)
{
    auto *scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("settingsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(inner);
    return scroll;
}

void fitWrapped(QLabel *label)
{
    const QFontMetrics metrics = label->fontMetrics();
    const QRect bounds = metrics.boundingRect(QRect(0, 0, 640, 4000), Qt::AlignLeft | Qt::TextWordWrap, label->text());
    label->setMinimumWidth(360);
    label->setMinimumHeight(bounds.height() + 8);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
}

QString grokSignInText(const Config *config)
{
    const QString command = config->grokCommandResolved();
    const bool installed = !command.isEmpty() && QFileInfo(command).isExecutable();
    GrokCliSession session;
    const bool signedIn = installed && grokCliLocalSession(&session);
    if (signedIn) {
        QString who;
        if (!session.firstName.isEmpty() && !session.email.isEmpty())
            who = QStringLiteral("%1 (%2)").arg(session.firstName, session.email);
        else if (!session.email.isEmpty())
            who = session.email;
        else if (!session.firstName.isEmpty())
            who = session.firstName;
        const QString line = who.isEmpty() ? QStringLiteral("Signed in with Grok.")
                                           : QStringLiteral("Signed in as %1.").arg(who);
        return line + QStringLiteral("\nChat and dictation use this sign-in. Model %1.").arg(config->grokModelResolved());
    }
    if (installed)
        return QStringLiteral("Grok is installed, but this machine is not signed in.\n"
                              "Run grok login in a terminal, then open Settings again.");
    return QStringLiteral("Grok is not installed.\nBlogy uses the Grok CLI. Install it, then run grok login.");
}

void growBox(QPlainTextEdit *edit, int minHeight)
{
    edit->setMinimumHeight(minHeight);
    QSizePolicy policy = edit->sizePolicy();
    policy.setVerticalPolicy(QSizePolicy::Ignored);
    policy.setHorizontalPolicy(QSizePolicy::Expanding);
    edit->setSizePolicy(policy);
}

} // namespace

SettingsDialog::SettingsDialog(Config *config, QWidget *parent)
    : QDialog(parent), config_(config), sites_(config->sites)
{
    setWindowTitle(QStringLiteral("Settings"));
    resize(960, 720);
    setMinimumSize(800, 560);
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    auto *head = new QWidget(this);
    auto *headLayout = new QVBoxLayout(head);
    headLayout->setContentsMargins(16, 14, 16, 10);
    auto *path = new QLabel(QStringLiteral("Saved to %1 (owner-only, never part of a repo).").arg(config_->path()), head);
    path->setObjectName(QStringLiteral("muted"));
    path->setWordWrap(true);
    headLayout->addWidget(path);
    v->addWidget(head);
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("settingsTabs"));
    v->addWidget(tabs, 1);

    // Sites: the repo, the layout, and the tags. Writing style lives on Markdown.
    auto *sitesTab = new QWidget(tabs);
    auto *sh = new QHBoxLayout(sitesTab);
    sh->setContentsMargins(12, 12, 12, 12);
    sh->setSpacing(16);
    auto *leftWrap = new QWidget(sitesTab);
    leftWrap->setFixedWidth(220);
    auto *left = new QVBoxLayout(leftWrap);
    left->setContentsMargins(0, 0, 0, 0);
    siteList_ = new QListWidget(leftWrap);
    for (const Site &s : sites_)
        siteList_->addItem(s.name);
    left->addWidget(siteList_, 1);
    auto *addSite = new QPushButton(QStringLiteral("Add"), leftWrap);
    auto *dupSite = new QPushButton(QStringLiteral("Duplicate"), leftWrap);
    auto *detectSites = new QPushButton(QStringLiteral("Detect GitHub repos"), leftWrap);
    detectSites->setToolTip(QStringLiteral("Look through ~/Projects for GitHub clones and offer the ones that look like blogs."));
    auto *delSite = new QPushButton(QStringLiteral("Remove"), leftWrap);
    left->addWidget(addSite);
    left->addWidget(dupSite);
    left->addWidget(detectSites);
    left->addWidget(delSite);
    sh->addWidget(leftWrap);
    auto *siteBody = new QWidget(sitesTab);
    auto *right = new QVBoxLayout(siteBody);
    right->setContentsMargins(0, 0, 4, 0);
    right->setSpacing(8);
    right->addWidget(sectionLabel(QStringLiteral("Layout"), siteBody));
    layoutBox_ = new QComboBox(siteBody);
    layoutBox_->setEditable(true);
    layoutBox_->setInsertPolicy(QComboBox::NoInsert);
    layoutBox_->setMinimumHeight(32);
    layoutBox_->setToolTip(QStringLiteral("The layout GitHub Pages uses for a new post. Read from folder lists the ones in the repo."));
    right->addWidget(layoutBox_);
    layoutHint_ = hintLabel(QStringLiteral("New posts get this layout in their front matter. Read from folder lists the layouts in the repo."), siteBody);
    layoutHint_->setObjectName(QStringLiteral("layoutHint"));
    right->addWidget(layoutHint_);
    right->addWidget(sectionLabel(QStringLiteral("Tags"), siteBody));
    right->addWidget(hintLabel(QStringLiteral("Tags this site already uses, separated by commas. New posts and Grok pick from this list."), siteBody));
    tagsEdit_ = new QLineEdit(siteBody);
    tagsEdit_->setPlaceholderText(QStringLiteral("politics, tech"));
    tagsEdit_->setMinimumHeight(32);
    tagsEdit_->setToolTip(QStringLiteral("Tags this site already uses. New posts and Grok pick from this list."));
    right->addWidget(tagsEdit_);
    auto *readRepo = new QPushButton(QStringLiteral("Read from folder"), siteBody);
    readRepo->setToolTip(QStringLiteral("Look at the site folder and store its layout, tags, and collections."));
    right->addWidget(readRepo, 0, Qt::AlignLeft);
    right->addWidget(sectionLabel(QStringLiteral("Site"), siteBody));
    right->addWidget(hintLabel(QStringLiteral(
                                    "JSON for this site. type is \"jekyll\" or \"mdx\". "
                                    "Patterns take {date} {year} {month} {day} {slug} {category}. "
                                    "url is the public base URL, with baseurl included."),
                                siteBody, 4));
    siteJson_ = new QPlainTextEdit(siteBody);
    siteJson_->setObjectName(QStringLiteral("jsonEdit"));
    siteJson_->setFont(QFont(QStringLiteral("DejaVu Sans Mono"), 10));
    growBox(siteJson_, 160);
    right->addWidget(siteJson_, 1);
    siteError_ = new QLabel(siteBody);
    siteError_->setObjectName(QStringLiteral("muted"));
    siteError_->setWordWrap(true);
    siteError_->setMinimumHeight(siteError_->fontMetrics().lineSpacing() + 4);
    right->addWidget(siteError_);
    sh->addWidget(settingsScroll(sitesTab, siteBody), 1);
    tabs->addTab(sitesTab, QStringLiteral("Sites"));
    tabs->setTabToolTip(tabs->count() - 1, QStringLiteral("Repo, layout, and tags."));

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
                                            {QStringLiteral("url"), QStringLiteral("https://username.github.io")},
                                            {QStringLiteral("layout"), QStringLiteral("post")}});
        s.frontMatter = QJsonObject{{QStringLiteral("layout"), QStringLiteral("post")},
                                    {QStringLiteral("title"), QString()},
                                    {QStringLiteral("date"), QStringLiteral("{date}")},
                                    {QStringLiteral("description"), QString()},
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
    connect(detectSites, &QPushButton::clicked, this, [this] {
        if (currentRow_ >= 0 && !storeSite(currentRow_))
            return;
        const QList<FoundRepo> found = scanGitHubRepos(QDir::homePath() + QStringLiteral("/Projects"));
        if (found.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Detect GitHub repos"),
                                     QStringLiteral("No GitHub clones under ~/Projects."));
            return;
        }
        QDialog pick(this);
        pick.setWindowTitle(QStringLiteral("GitHub repos"));
        pick.resize(640, 420);
        auto *layout = new QVBoxLayout(&pick);
        auto *hint = new QLabel(QStringLiteral("Blogs are checked. Other GitHub repos are listed so you can add one anyway."), &pick);
        hint->setObjectName(QStringLiteral("hint"));
        hint->setWordWrap(true);
        layout->addWidget(hint);
        auto *list = new QListWidget(&pick);
        for (int repoIndex = 0; repoIndex < found.size(); ++repoIndex) {
            const FoundRepo &repo = found.at(repoIndex);
            const QString root = Config::expandHome(repo.site.path);
            bool already = false;
            for (const Site &have : sites_) {
                if (QDir::cleanPath(Config::expandHome(have.path)) == QDir::cleanPath(root))
                    already = true;
            }
            auto *item = new QListWidgetItem(QStringLiteral("%1\n%2\n%3 — %4")
                                                 .arg(repo.site.name, repo.site.path, repo.site.url, repo.detail),
                                             list);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(!already && repo.blog ? Qt::Checked : Qt::Unchecked);
            if (already) {
                item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
                item->setText(item->text() + QStringLiteral("\nAlready in Blogy"));
            }
            item->setData(Qt::UserRole + 1, repoIndex);
        }
        layout->addWidget(list, 1);
        auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &pick);
        layout->addWidget(box);
        connect(box, &QDialogButtonBox::accepted, &pick, &QDialog::accept);
        connect(box, &QDialogButtonBox::rejected, &pick, &QDialog::reject);
        if (pick.exec() != QDialog::Accepted)
            return;
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem *item = list->item(i);
            if (item->checkState() != Qt::Checked || !(item->flags() & Qt::ItemIsEnabled))
                continue;
            const int index = item->data(Qt::UserRole + 1).toInt();
            if (index < 0 || index >= found.size())
                continue;
            sites_.append(found.at(index).site);
            siteList_->addItem(found.at(index).site.name);
        }
        if (!sites_.isEmpty())
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
    connect(readRepo, &QPushButton::clicked, this, [this] {
        if (currentRow_ < 0 || !storeSite(currentRow_))
            return;
        Site edited = sites_.at(currentRow_);
        const QString root = Config::expandHome(edited.path);
        if (!QFileInfo::exists(root)) {
            siteError_->setText(QStringLiteral("That folder is not on this machine."));
            return;
        }
        bool blog = false;
        Site found = inspectBlog(root, &blog);
        found.name = edited.name;
        found.path = edited.path;
        found.branch = edited.branch.isEmpty() ? found.branch : edited.branch;
        found.remote = edited.remote.isEmpty() ? found.remote : edited.remote;
        if (!edited.url.isEmpty())
            found.url = edited.url;
        if (!edited.commitMessage.isEmpty())
            found.commitMessage = edited.commitMessage;
        if (!edited.templateText.isEmpty())
            found.templateText = edited.templateText;
        found.signature = edited.signature;
        found.tune = edited.tune;
        found.headingLevel = edited.headingLevel;
        found.emphasis = edited.emphasis;
        found.listMarker = edited.listMarker;
        found.styleNotes = edited.styleNotes;
        found.imagePattern = edited.imagePattern;
        found.autoFixLinks = edited.autoFixLinks;
        found.hasAutoFix = edited.hasAutoFix;
        sites_[currentRow_] = found;
        const int row = currentRow_;
        currentRow_ = -1;
        loadSite(row);
        QStringList bits;
        if (found.isMdx())
            bits << QStringLiteral("MDX");
        else
            bits << QStringLiteral("Layout %1").arg(found.layout.isEmpty() ? QStringLiteral("(none)") : found.layout);
        bits << QStringLiteral("%1 collection%2").arg(found.collections.size()).arg(found.collections.size() == 1 ? QString() : QStringLiteral("s"));
        bits << QStringLiteral("%1 tag%2").arg(found.tags.size()).arg(found.tags.size() == 1 ? QString() : QStringLiteral("s"));
        if (!blog)
            bits << QStringLiteral("no posts found, using defaults");
        siteError_->setText(bits.join(QStringLiteral(". ")) + QLatin1Char('.'));
    });

    // Markdown: signature and the style Grok follows. One set per site.
    auto *markdownTab = new QWidget(tabs);
    auto *markdownBody = new QWidget(markdownTab);
    auto *mv = new QVBoxLayout(markdownBody);
    mv->setContentsMargins(16, 16, 16, 16);
    mv->setSpacing(8);
    mv->addWidget(hintLabel(QStringLiteral(
                                 "Writing rules for the site selected under Sites. "
                                 "Grok follows them when it writes or polishes a post. "
                                 "Publish still adds the signature once, at the end."),
                             markdownBody, 3));
    mv->addWidget(sectionLabel(QStringLiteral("Site"), markdownBody));
    markdownSite_ = new QLabel(markdownBody);
    markdownSite_->setObjectName(QStringLiteral("markdownSite"));
    markdownSite_->setWordWrap(true);
    mv->addWidget(markdownSite_);
    mv->addWidget(sectionLabel(QStringLiteral("Signature"), markdownBody));
    mv->addWidget(hintLabel(QStringLiteral("Added to the end of the post when you publish. A later publish does not add it twice."),
                             markdownBody, 2));
    signature_ = new QPlainTextEdit(markdownBody);
    signature_->setObjectName(QStringLiteral("siteSignature"));
    signature_->setPlaceholderText(QStringLiteral("— Your name\nhttps://yoursite.example"));
    growBox(signature_, 100);
    mv->addWidget(signature_, 1);
    mv->addWidget(sectionLabel(QStringLiteral("Default heading"), markdownBody));
    mv->addWidget(hintLabel(QStringLiteral(
                                 "The title stays in the front matter. Sections in the post start at this level. "
                                 "Grok uses it, and so does a new post whose template is only a heading."),
                             markdownBody, 3));
    heading_ = new QComboBox(markdownBody);
    heading_->setObjectName(QStringLiteral("markdownHeading"));
    heading_->setMinimumHeight(32);
    for (int level = 1; level <= 6; ++level)
        heading_->addItem(QStringLiteral("Heading %1     %2").arg(level).arg(QString(level, QLatin1Char('#'))), level);
    mv->addWidget(heading_);
    mv->addWidget(sectionLabel(QStringLiteral("Bold and italic"), markdownBody));
    emphasis_ = new QComboBox(markdownBody);
    emphasis_->setObjectName(QStringLiteral("markdownEmphasis"));
    emphasis_->setMinimumHeight(32);
    emphasis_->addItem(QStringLiteral("Asterisks     **bold** and *italic*"), QStringLiteral("**"));
    emphasis_->addItem(QStringLiteral("Underscores     __bold__ and _italic_"), QStringLiteral("__"));
    mv->addWidget(emphasis_);
    mv->addWidget(sectionLabel(QStringLiteral("Lists"), markdownBody));
    listMarker_ = new QComboBox(markdownBody);
    listMarker_->setObjectName(QStringLiteral("markdownLists"));
    listMarker_->setMinimumHeight(32);
    listMarker_->addItem(QStringLiteral("Hyphen     - item"), QStringLiteral("-"));
    listMarker_->addItem(QStringLiteral("Asterisk     * item"), QStringLiteral("*"));
    listMarker_->addItem(QStringLiteral("Plus     + item"), QStringLiteral("+"));
    mv->addWidget(listMarker_);
    mv->addWidget(sectionLabel(QStringLiteral("Style notes"), markdownBody));
    mv->addWidget(hintLabel(QStringLiteral("Anything else Grok should follow: paragraph length, spelling, what to leave out."),
                             markdownBody, 2));
    styleNotes_ = new QPlainTextEdit(markdownBody);
    styleNotes_->setObjectName(QStringLiteral("markdownStyle"));
    styleNotes_->setPlaceholderText(QStringLiteral("short paragraphs, sentence-case headings, no emoji"));
    growBox(styleNotes_, 88);
    mv->addWidget(styleNotes_, 1);
    auto *markdownScroll = settingsScroll(markdownTab, markdownBody);
    auto *markdownOuter = new QVBoxLayout(markdownTab);
    markdownOuter->setContentsMargins(0, 0, 0, 0);
    markdownOuter->addWidget(markdownScroll);
    tabs->addTab(markdownTab, QStringLiteral("Markdown"));
    tabs->setTabToolTip(tabs->count() - 1, QStringLiteral("Signature and the style Grok follows."));
    connect(heading_, &QComboBox::currentIndexChanged, this, [this] {
        if (currentRow_ < 0)
            return;
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(siteJson_->toPlainText().toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            return;
        QJsonObject obj = doc.object();
        if (!Site::isStockHeadingTemplate(obj.value(QStringLiteral("template")).toString()))
            return;
        const int level = heading_->currentData().toInt();
        if (level < 1 || level > 6)
            return;
        obj.insert(QStringLiteral("template"), QString(level, QLatin1Char('#')) + QStringLiteral(" {title}\n\n"));
        siteJson_->setPlainText(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented)));
    });

    // AI: the signed-in Grok CLI, and the voice for the site selected under Sites.
    auto *aiTab = new QWidget(tabs);
    auto *aiBody = new QWidget(aiTab);
    auto *aiPage = new QVBoxLayout(aiBody);
    aiPage->setContentsMargins(16, 16, 16, 16);
    aiPage->setSpacing(8);
    aiPage->addWidget(hintLabel(QStringLiteral("Blogy uses the Grok CLI already signed in on this machine. "
                                               "Chat and dictation both use that sign-in."),
                                 aiBody, 2));
    aiPage->addWidget(sectionLabel(QStringLiteral("Grok"), aiBody));
    grokStatus_ = new QLabel(aiBody);
    grokStatus_->setObjectName(QStringLiteral("grokStatus"));
    grokStatus_->setWordWrap(true);
    grokStatus_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    grokStatus_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    grokStatus_->setText(grokSignInText(config_));
    fitWrapped(grokStatus_);
    aiPage->addWidget(grokStatus_);
    aiPage->addWidget(sectionLabel(QStringLiteral("Voice"), aiBody));
    aiPage->addWidget(hintLabel(QStringLiteral("How the selected site should sound. Finish the post, and every other Grok request, uses it."),
                                 aiBody, 2));
    aiSite_ = new QLabel(aiBody);
    aiSite_->setObjectName(QStringLiteral("aiSite"));
    aiSite_->setWordWrap(true);
    aiPage->addWidget(aiSite_);
    tune_ = new QPlainTextEdit(aiBody);
    tune_->setObjectName(QStringLiteral("siteTune"));
    tune_->setPlaceholderText(QStringLiteral("dark humor and sarcasm"));
    growBox(tune_, 120);
    aiPage->addWidget(tune_, 1);
    auto *aiScroll = settingsScroll(aiTab, aiBody);
    auto *aiOuter = new QVBoxLayout(aiTab);
    aiOuter->setContentsMargins(0, 0, 0, 0);
    aiOuter->addWidget(aiScroll);
    tabs->addTab(aiTab, QStringLiteral("AI"));
    tabs->setTabToolTip(tabs->count() - 1, QStringLiteral("Grok sign-in, and the voice for the selected site."));

    auto envNote = [this](const QString &which, const char *var) {
        return config_->secretSource(which) == QLatin1String("env")
                   ? QStringLiteral("%1 is set in the environment and wins over this field.").arg(QString::fromLatin1(var))
                   : QStringLiteral("Or set %1 in the environment.").arg(QString::fromLatin1(var));
    };

    // Bluesky
    auto *bskyTab = new QWidget(tabs);
    auto *bf = new QFormLayout(bskyTab);
    bf->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    bf->setRowWrapPolicy(QFormLayout::WrapLongRows);
    bf->setHorizontalSpacing(16);
    bf->setVerticalSpacing(10);
    bf->setContentsMargins(16, 16, 16, 16);
    bskyService_ = new QLineEdit(config_->bskyService, bskyTab);
    bskyHandle_ = new QLineEdit(config_->fileBskyHandle, bskyTab);
    bskyHandle_->setObjectName(QStringLiteral("bskyHandle"));
    bskyHandle_->setPlaceholderText(envNote(QStringLiteral("handle"), "BLUESKY_HANDLE"));
    bskyPassword_ = new QLineEdit(config_->fileBskyPassword, bskyTab);
    bskyPassword_->setEchoMode(QLineEdit::Password);
    bskyPassword_->setPlaceholderText(envNote(QStringLiteral("password"), "BLUESKY_APP_PASSWORD"));
    bf->addRow(sectionLabel(QStringLiteral("Account"), bskyTab));
    bf->addRow(hintLabel(QStringLiteral("Post to Bluesky uses this account. It is separate from Grok."), bskyTab, 2));
    bf->addRow(QStringLiteral("Service (PDS)"), bskyService_);
    bf->addRow(QStringLiteral("Handle"), bskyHandle_);
    bf->addRow(QStringLiteral("App password"), bskyPassword_);
    bf->addRow(hintLabel(QStringLiteral("Use an app password from Bluesky Settings → Privacy and security → App passwords, "
                                        "never your main password."),
                          bskyTab, 3));
    tabs->addTab(bskyTab, QStringLiteral("Bluesky"));
    tabs->setTabToolTip(tabs->count() - 1, QStringLiteral("Handle and app password."));

    // Editor
    auto *editor = new QWidget(tabs);
    auto *ef = new QFormLayout(editor);
    ef->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    ef->setRowWrapPolicy(QFormLayout::WrapLongRows);
    ef->setHorizontalSpacing(16);
    ef->setVerticalSpacing(10);
    ef->setContentsMargins(16, 16, 16, 16);
    fontSize_ = new QSpinBox(editor);
    fontSize_->setRange(8, 40);
    fontSize_->setValue(config_->fontSize);
    wrap_ = new QCheckBox(QStringLiteral("Soft wrap long lines"), editor);
    wrap_->setChecked(config_->wrap);
    autoFix_ = new QCheckBox(QStringLiteral("Auto-fix links on publish"), editor);
    autoFix_->setChecked(config_->autoFixLinks);
    auto *fixHint = new QLabel(QStringLiteral(
        "When you publish, a picture you attached (from Downloads or a drag onto the page) is copied beside the post "
        "and named after it — 2026-10-25-hello.png, or my_slug/my_slug.png for an MDX page. "
        "The Markdown link is rewritten to the public URL from the site config, and a copy is placed where the site serves that URL. "
        "Links to other posts in the same site become their public URLs. A site can turn this off with \"autoFixLinks\": false."),
                               editor);
    fixHint->setObjectName(QStringLiteral("hint"));
    fixHint->setWordWrap(true);
    fixHint->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    fixHint->setMinimumHeight(fixHint->fontMetrics().lineSpacing() * 5 + 8);
    fixHint->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    ef->addRow(QStringLiteral("Font size"), fontSize_);
    ef->addRow(QString(), wrap_);
    ef->addRow(QString(), autoFix_);
    ef->addRow(fixHint);
    auto *openFolder = new QPushButton(QStringLiteral("Open config folder"), editor);
    connect(openFolder, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(config_->path()).absolutePath()));
    });
    ef->addRow(QString(), openFolder);
    tabs->addTab(editor, QStringLiteral("Editor"));
    tabs->setTabToolTip(tabs->count() - 1, QStringLiteral("Font, wrapping, and link fixing."));

    // A button box tucks these into the corner. They sit on the left of a footer,
    // on the same inset as the page, in the same order you read the form.
    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("settingsFooter"));
    auto *actions = new QHBoxLayout(footer);
    actions->setContentsMargins(16, 12, 16, 14);
    actions->setSpacing(8);
    auto *save = new QPushButton(QStringLiteral("Save"), footer);
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), footer);
    save->setObjectName(QStringLiteral("primary"));
    save->setDefault(true);
    save->setAutoDefault(true);
    cancel->setAutoDefault(false);
    const int actionWidth = qMax(132, qMax(save->sizeHint().width(), cancel->sizeHint().width()));
    for (QPushButton *button : {save, cancel}) {
        button->setMinimumWidth(actionWidth);
        button->setMinimumHeight(36);
    }
    actions->addWidget(save);
    actions->addWidget(cancel);
    actions->addStretch(1);
    connect(save, &QPushButton::clicked, this, &SettingsDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    v->addWidget(footer);

    if (!sites_.isEmpty())
        siteList_->setCurrentRow(0);
    if (currentRow_ < 0 && siteList_->currentRow() >= 0)
        loadSite(siteList_->currentRow());
}

void SettingsDialog::loadSite(int row)
{
    currentRow_ = row;
    siteError_->clear();
    if (row < 0 || row >= sites_.size()) {
        siteJson_->clear();
        siteJson_->setEnabled(false);
        layoutBox_->clear();
        layoutBox_->setEnabled(false);
        tagsEdit_->clear();
        tagsEdit_->setEnabled(false);
        signature_->clear();
        signature_->setEnabled(false);
        heading_->setEnabled(false);
        emphasis_->setEnabled(false);
        listMarker_->setEnabled(false);
        styleNotes_->clear();
        styleNotes_->setEnabled(false);
        tune_->clear();
        tune_->setEnabled(false);
        markdownSite_->setText(QStringLiteral("Add a site on the Sites tab."));
        aiSite_->setText(markdownSite_->text());
        return;
    }
    const Site &site = sites_.at(row);
    siteJson_->setEnabled(true);
    signature_->setEnabled(true);
    heading_->setEnabled(true);
    emphasis_->setEnabled(true);
    listMarker_->setEnabled(true);
    styleNotes_->setEnabled(true);
    tune_->setEnabled(true);
    tagsEdit_->setEnabled(true);
    const QString siteName = site.name.isEmpty() ? QStringLiteral("This site has no name yet.") : site.name;
    markdownSite_->setText(siteName);
    aiSite_->setText(siteName);
    const bool pages = !site.isMdx();
    layoutBox_->setEnabled(pages);
    layoutHint_->setText(pages
                             ? QStringLiteral("New posts get this layout in their front matter. Read from folder lists the layouts in the repo.")
                             : QStringLiteral("MDX pages do not use a Jekyll layout. Tags below still apply."));
    {
        QSignalBlocker block(layoutBox_);
        layoutBox_->clear();
        for (const QString &name : site.layouts)
            layoutBox_->addItem(name);
        if (!site.layout.isEmpty() && layoutBox_->findText(site.layout) < 0)
            layoutBox_->addItem(site.layout);
        layoutBox_->setCurrentText(site.layout);
    }
    tagsEdit_->setText(site.tags.join(QStringLiteral(", ")));
    QJsonObject shown = site.toJson();
    shown.remove(QStringLiteral("signature"));
    shown.remove(QStringLiteral("tune"));
    shown.remove(QStringLiteral("layout"));
    shown.remove(QStringLiteral("layouts"));
    shown.remove(QStringLiteral("tags"));
    shown.remove(QStringLiteral("headingLevel"));
    shown.remove(QStringLiteral("emphasis"));
    shown.remove(QStringLiteral("listMarker"));
    shown.remove(QStringLiteral("style"));
    siteJson_->setPlainText(QString::fromUtf8(QJsonDocument(shown).toJson(QJsonDocument::Indented)));
    signature_->setPlainText(site.signature);
    {
        QSignalBlocker blockHeading(heading_);
        QSignalBlocker blockEmphasis(emphasis_);
        QSignalBlocker blockList(listMarker_);
        const int level = site.headingLevel >= 1 && site.headingLevel <= 6 ? site.headingLevel : 2;
        const int headingRow = heading_->findData(level);
        heading_->setCurrentIndex(headingRow < 0 ? heading_->findData(2) : headingRow);
        const int emphasisRow = emphasis_->findData(site.emphasis == QLatin1String("__") ? QStringLiteral("__") : QStringLiteral("**"));
        emphasis_->setCurrentIndex(emphasisRow < 0 ? 0 : emphasisRow);
        const int listRow = listMarker_->findData(site.listMarker);
        listMarker_->setCurrentIndex(listRow < 0 ? 0 : listRow);
    }
    styleNotes_->setPlainText(site.styleNotes);
    tune_->setPlainText(site.tune);
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
    sites_[row].signature = signature_->toPlainText();
    if (sites_[row].signature.trimmed().isEmpty())
        sites_[row].signature.clear();
    int level = heading_->currentData().toInt();
    if (level < 1 || level > 6)
        level = 2;
    sites_[row].headingLevel = level;
    const QString emphasis = emphasis_->currentData().toString();
    sites_[row].emphasis = emphasis == QLatin1String("__") ? QStringLiteral("__") : QStringLiteral("**");
    const QString marker = listMarker_->currentData().toString();
    sites_[row].listMarker = (marker == QLatin1String("*") || marker == QLatin1String("+")) ? marker : QStringLiteral("-");
    sites_[row].styleNotes = styleNotes_->toPlainText();
    if (sites_[row].styleNotes.trimmed().isEmpty())
        sites_[row].styleNotes.clear();
    if (Site::isStockHeadingTemplate(sites_[row].templateText))
        sites_[row].templateText = sites_[row].headingTemplate();
    sites_[row].tune = tune_->toPlainText();
    if (sites_[row].tune.trimmed().isEmpty())
        sites_[row].tune.clear();
    sites_[row].tags.clear();
    for (const QString &tag : tagsEdit_->text().split(QLatin1Char(','))) {
        const QString clean = tag.trimmed();
        if (!clean.isEmpty() && !sites_[row].tags.contains(clean))
            sites_[row].tags << clean;
    }
    if (!sites_[row].isMdx()) {
        const QString layout = layoutBox_->currentText().trimmed();
        sites_[row].layout = layout;
        QStringList layouts;
        for (int i = 0; i < layoutBox_->count(); ++i) {
            const QString name = layoutBox_->itemText(i).trimmed();
            if (!name.isEmpty() && !layouts.contains(name))
                layouts << name;
        }
        if (!layout.isEmpty() && !layouts.contains(layout))
            layouts.prepend(layout);
        sites_[row].layouts = layouts;
        if (!layout.isEmpty())
            sites_[row].frontMatter.insert(QStringLiteral("layout"), layout);
    } else {
        sites_[row].layout.clear();
        sites_[row].layouts.clear();
    }
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
    config_->bskyService = bskyService_->text().trimmed();
    config_->fileBskyHandle = bskyHandle_->text().trimmed().remove(QLatin1Char('@'));
    config_->fileBskyPassword = bskyPassword_->text().trimmed();
    config_->fontSize = fontSize_->value();
    config_->wrap = wrap_->isChecked();
    config_->autoFixLinks = autoFix_->isChecked();
    QString error;
    if (!config_->save(&error)) {
        QMessageBox::warning(this, QStringLiteral("Settings"), QStringLiteral("Could not save: ") + error);
        return;
    }
    QDialog::accept();
}
