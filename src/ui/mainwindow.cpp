#include "ui/mainwindow.h"

#include "bluesky.h"
#include "editor.h"
#include "frontmatter.h"
#include "git.h"
#include "grok.h"
#include "post.h"
#include "preview.h"
#include "ui/chatpanel.h"
#include "ui/dialogs.h"
#include "ui/frontmatterform.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

MainWindow::MainWindow(Config *config, QWidget *parent)
    : QMainWindow(parent), config_(config)
{
    net_ = new QNetworkAccessManager(this);
    grok_ = new GrokClient(net_, this);
    bsky_ = new BlueskyClient(net_, this);
    git_ = new GitPublisher(this);
    formSync_.setSingleShot(true);
    formSync_.setInterval(300);
    connect(&formSync_, &QTimer::timeout, this, &MainWindow::syncFormFromEditor);

    buildUi();
    buildMenus();
    refreshSites();

    if (!settings_.geometry().isEmpty())
        restoreGeometry(settings_.geometry());
    else
        resize(1440, 900);
    if (!settings_.splitter().isEmpty())
        split_->restoreState(settings_.splitter());
    previewAct_->setChecked(settings_.previewVisible());
    updateTitle();
    updateStatus();
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("root"));
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    split_ = new QSplitter(Qt::Horizontal, central);
    split_->setChildrenCollapsible(false);
    layout->addWidget(split_);

    // Sidebar: site picker and the posts in each collection.
    auto *sidebar = new QWidget(split_);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    auto *sv = new QVBoxLayout(sidebar);
    sv->setContentsMargins(10, 10, 10, 10);
    auto *word = new QLabel(QStringLiteral("Blogy"), sidebar);
    word->setObjectName(QStringLiteral("wordmark"));
    QFont wf = word->font();
    wf.setPointSize(18);
    wf.setBold(true);
    word->setFont(wf);
    sv->addWidget(word);
    siteBox_ = new QComboBox(sidebar);
    sv->addWidget(siteBox_);
    tree_ = new QTreeWidget(sidebar);
    tree_->setHeaderHidden(true);
    tree_->setRootIsDecorated(true);
    tree_->setUniformRowHeights(true);
    sv->addWidget(tree_, 1);
    auto *newButton = new QPushButton(QStringLiteral("New Post"), sidebar);
    newButton->setObjectName(QStringLiteral("primary"));
    sv->addWidget(newButton);
    connect(newButton, &QPushButton::clicked, this, &MainWindow::newPost);
    connect(siteBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (i >= 0 && i < config_->sites.size())
            settings_.setLastSite(config_->sites.at(i).name);
        refreshPosts();
    });
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        const QString path = item->data(0, Qt::UserRole).toString();
        if (!path.isEmpty() && path != path_ && maybeSave())
            openFile(path);
    });
    connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        const QString path = item->data(0, Qt::UserRole).toString();
        if (!path.isEmpty() && path != path_ && maybeSave())
            openFile(path);
    });

    editor_ = new MarkdownEdit(split_);
    editor_->setFontPointSize(config_->fontSize);
    editor_->setWrap(config_->wrap);
    preview_ = new Preview(split_);
    preview_->setDelay(config_->previewDelayMs);
    connect(editor_, &QPlainTextEdit::textChanged, this, &MainWindow::onTextChanged);
    connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, &MainWindow::updateStatus);
    connect(editor_->document(), &QTextDocument::modificationChanged, this, &MainWindow::updateTitle);

    side_ = new QTabWidget(split_);
    front_ = new FrontMatterForm(side_);
    chat_ = new ChatPanel(config_, grok_, editor_, side_);
    log_ = new QPlainTextEdit(side_);
    log_->setObjectName(QStringLiteral("gitLog"));
    log_->setReadOnly(true);
    log_->setPlaceholderText(QStringLiteral("git output shows up here when you publish."));
    side_->addTab(front_, QStringLiteral("Front matter"));
    side_->addTab(chat_, QStringLiteral("Talk to AI"));
    side_->addTab(log_, QStringLiteral("Publish"));
    connect(front_, &FrontMatterForm::edited, this, &MainWindow::syncEditorFromForm);
    chat_->replaceFrontMatter = [this](const QString &yaml) { replaceFrontMatterYaml(yaml); };
    chat_->siteContext = [this] { return siteContext(); };

    split_->addWidget(sidebar);
    split_->addWidget(editor_);
    split_->addWidget(preview_);
    split_->addWidget(side_);
    split_->setStretchFactor(0, 0);
    split_->setStretchFactor(1, 3);
    split_->setStretchFactor(2, 3);
    split_->setStretchFactor(3, 2);
    split_->setSizes({260, 560, 480, 380});
    setCentralWidget(central);

    statusLeft_ = new QLabel(this);
    statusRight_ = new QLabel(this);
    statusRight_->setObjectName(QStringLiteral("muted"));
    statusBar()->addWidget(statusLeft_, 1);
    statusBar()->addPermanentWidget(statusRight_);

    connect(git_, &GitPublisher::output, this, [this](const QString &text) {
        log_->moveCursor(QTextCursor::End);
        log_->insertPlainText(text);
        log_->moveCursor(QTextCursor::End);
    });
}

void MainWindow::buildMenus()
{
    auto *newAct = new QAction(QStringLiteral("New Post…"), this);
    newAct->setShortcut(QKeySequence::New);
    auto *openAct = new QAction(QStringLiteral("Open…"), this);
    openAct->setShortcut(QKeySequence::Open);
    auto *saveAct = new QAction(QStringLiteral("Save"), this);
    saveAct->setShortcut(QKeySequence::Save);
    auto *saveAsAct = new QAction(QStringLiteral("Save As…"), this);
    saveAsAct->setShortcut(QKeySequence::SaveAs);
    publishAct_ = new QAction(QStringLiteral("Publish"), this);
    publishAct_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
    auto *bskyAct = new QAction(QStringLiteral("Post to Bluesky…"), this);
    bskyAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+B")));
    auto *reloadAct = new QAction(QStringLiteral("Reload Posts"), this);
    reloadAct->setShortcut(QKeySequence::Refresh);
    auto *settingsAct = new QAction(QStringLiteral("Settings…"), this);
    settingsAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    auto *openSite = new QAction(QStringLiteral("Open Site Folder"), this);
    auto *viewSite = new QAction(QStringLiteral("Open Site in Browser"), this);
    auto *quitAct = new QAction(QStringLiteral("Quit"), this);
    quitAct->setShortcut(QKeySequence::Quit);

    connect(newAct, &QAction::triggered, this, &MainWindow::newPost);
    connect(openAct, &QAction::triggered, this, &MainWindow::openDialog);
    connect(saveAct, &QAction::triggered, this, &MainWindow::save);
    connect(saveAsAct, &QAction::triggered, this, &MainWindow::saveAs);
    connect(publishAct_, &QAction::triggered, this, &MainWindow::publish);
    connect(bskyAct, &QAction::triggered, this, [this] { postToBluesky(postUrl(path_)); });
    connect(reloadAct, &QAction::triggered, this, &MainWindow::refreshPosts);
    connect(settingsAct, &QAction::triggered, this, &MainWindow::settingsDialog);
    connect(openSite, &QAction::triggered, this, [this] {
        const int i = currentSiteIndex();
        if (i >= 0)
            QDesktopServices::openUrl(QUrl::fromLocalFile(config_->sites.at(i).root()));
    });
    connect(viewSite, &QAction::triggered, this, [this] {
        const int i = currentSiteIndex();
        if (i >= 0)
            QDesktopServices::openUrl(QUrl(config_->sites.at(i).url));
    });
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    QMenu *file = menuBar()->addMenu(QStringLiteral("File"));
    file->addAction(newAct);
    file->addAction(openAct);
    file->addSeparator();
    file->addAction(saveAct);
    file->addAction(saveAsAct);
    file->addSeparator();
    file->addAction(publishAct_);
    file->addAction(bskyAct);
    file->addSeparator();
    file->addAction(settingsAct);
    file->addSeparator();
    file->addAction(quitAct);

    QMenu *edit = menuBar()->addMenu(QStringLiteral("Format"));
    auto addFormat = [this, edit](const QString &name, const QString &keys, std::function<void()> fn) {
        auto *a = edit->addAction(name);
        a->setShortcut(QKeySequence(keys));
        connect(a, &QAction::triggered, this, fn);
    };
    addFormat(QStringLiteral("Bold"), QStringLiteral("Ctrl+B"), [this] { editor_->wrapSelection(QStringLiteral("**"), QStringLiteral("**")); });
    addFormat(QStringLiteral("Italic"), QStringLiteral("Ctrl+I"), [this] { editor_->wrapSelection(QStringLiteral("*"), QStringLiteral("*")); });
    addFormat(QStringLiteral("Inline Code"), QStringLiteral("Ctrl+`"), [this] { editor_->wrapSelection(QStringLiteral("`"), QStringLiteral("`")); });
    addFormat(QStringLiteral("Link"), QStringLiteral("Ctrl+K"), [this] { editor_->insertLink(); });
    addFormat(QStringLiteral("Heading"), QStringLiteral("Ctrl+2"), [this] {
        QTextCursor c = editor_->textCursor();
        c.movePosition(QTextCursor::StartOfBlock);
        c.insertText(QStringLiteral("## "));
    });

    QMenu *site = menuBar()->addMenu(QStringLiteral("Site"));
    site->addAction(reloadAct);
    site->addAction(openSite);
    site->addAction(viewSite);

    QMenu *view = menuBar()->addMenu(QStringLiteral("View"));
    previewAct_ = view->addAction(QStringLiteral("Preview"));
    previewAct_->setCheckable(true);
    previewAct_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    connect(previewAct_, &QAction::toggled, this, [this](bool on) {
        preview_->setVisible(on);
        settings_.setPreviewVisible(on);
    });
    auto *aiAct = view->addAction(QStringLiteral("Talk to AI"));
    aiAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+J")));
    connect(aiAct, &QAction::triggered, this, [this] { side_->setCurrentWidget(chat_); });
    auto *fmAct = view->addAction(QStringLiteral("Front Matter"));
    fmAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+M")));
    connect(fmAct, &QAction::triggered, this, [this] { side_->setCurrentWidget(front_); });
    auto *zoomIn = view->addAction(QStringLiteral("Bigger Text"));
    zoomIn->setShortcut(QKeySequence::ZoomIn);
    connect(zoomIn, &QAction::triggered, this, [this] { editor_->setFontPointSize(editor_->fontPointSize() + 1); });
    auto *zoomOut = view->addAction(QStringLiteral("Smaller Text"));
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOut, &QAction::triggered, this, [this] { editor_->setFontPointSize(editor_->fontPointSize() - 1); });

    auto *aboutAct = new QAction(QStringLiteral("About Blogy"), this);
    connect(aboutAct, &QAction::triggered, this, &MainWindow::about);
    menuBar()->addMenu(QStringLiteral("Help"))->addAction(aboutAct);

    QToolBar *bar = addToolBar(QStringLiteral("Main"));
    bar->setObjectName(QStringLiteral("mainToolbar"));
    bar->setMovable(false);
    bar->addAction(newAct);
    bar->addAction(openAct);
    bar->addAction(saveAct);
    bar->addSeparator();
    bar->addAction(publishAct_);
    bar->addAction(bskyAct);
    bar->addSeparator();
    bar->addAction(previewAct_);
    bar->addAction(aiAct);
    bar->addAction(settingsAct);
    if (auto *b = qobject_cast<QToolButton *>(bar->widgetForAction(publishAct_)))
        b->setObjectName(QStringLiteral("primary"));
}

int MainWindow::currentSiteIndex() const
{
    const int i = siteBox_->currentIndex();
    return (i >= 0 && i < config_->sites.size()) ? i : -1;
}

void MainWindow::selectSite(int index)
{
    siteBox_->setCurrentIndex(index);
}

void MainWindow::refreshSites()
{
    QSignalBlocker block(siteBox_);
    siteBox_->clear();
    int pick = 0;
    for (int i = 0; i < config_->sites.size(); ++i) {
        siteBox_->addItem(config_->sites.at(i).name);
        if (config_->sites.at(i).name == settings_.lastSite())
            pick = i;
    }
    siteBox_->setCurrentIndex(config_->sites.isEmpty() ? -1 : pick);
    refreshPosts();
}

void MainWindow::refreshPosts()
{
    tree_->clear();
    const int i = currentSiteIndex();
    if (i < 0) {
        auto *hint = new QTreeWidgetItem(tree_, {QStringLiteral("No sites yet. File → Settings.")});
        hint->setDisabled(true);
        return;
    }
    const Site &site = config_->sites.at(i);
    if (!QFileInfo::exists(site.root())) {
        auto *hint = new QTreeWidgetItem(tree_, {QStringLiteral("Folder not found: %1").arg(site.path)});
        hint->setDisabled(true);
        new QTreeWidgetItem(tree_, {QStringLiteral("Clone the repo there, or fix the path in Settings.")});
        return;
    }
    for (const Collection &c : site.collections) {
        auto *group = new QTreeWidgetItem(tree_, {c.name});
        group->setFlags(Qt::ItemIsEnabled);
        QFont f = group->font(0);
        f.setBold(true);
        group->setFont(0, f);
        group->setForeground(0, QColor(QStringLiteral("#ffb454")));
        const QString dir = site.root() + QLatin1Char('/') + c.dir;
        QStringList files;
        QDirIterator it(dir, {QStringLiteral("*.md"), QStringLiteral("*.markdown"), QStringLiteral("*.mdx")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            files << it.next();
        std::sort(files.begin(), files.end(), [&](const QString &a, const QString &b) {
            QDate da, db;
            QString sa, sb;
            post::parsePath(c, QDir(site.root()).relativeFilePath(a), &da, &sa);
            post::parsePath(c, QDir(site.root()).relativeFilePath(b), &db, &sb);
            if (da != db)
                return da > db;
            return a > b;
        });
        for (const QString &file : files) {
            const QString rel = QDir(site.root()).relativeFilePath(file);
            QDate date;
            QString slug;
            QString label = QDir(dir).relativeFilePath(file);
            if (post::parsePath(c, rel, &date, &slug))
                label = date.isValid() ? date.toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral("  ") + slug : slug;
            auto *item = new QTreeWidgetItem(group, {label});
            item->setData(0, Qt::UserRole, file);
            item->setToolTip(0, rel);
            if (file == path_)
                tree_->setCurrentItem(item);
        }
        group->setText(0, QStringLiteral("%1  (%2)").arg(c.name).arg(files.size()));
        group->setExpanded(true);
    }
}

const Site *MainWindow::siteForPath(const QString &path, int *index) const
{
    for (int i = 0; i < config_->sites.size(); ++i) {
        const QString root = QDir::cleanPath(config_->sites.at(i).root());
        if (!root.isEmpty() && QDir::cleanPath(path).startsWith(root + QLatin1Char('/'))) {
            if (index)
                *index = i;
            return &config_->sites.at(i);
        }
    }
    return nullptr;
}

bool MainWindow::isMdxFile() const
{
    return path_.endsWith(QLatin1String(".mdx"));
}

bool MainWindow::openFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("Open"), QStringLiteral("Could not open %1: %2").arg(path, file.errorString()));
        return false;
    }
    syncing_ = true;
    editor_->setPlainText(QString::fromUtf8(file.readAll()));
    syncing_ = false;
    editor_->document()->setModified(false);
    path_ = QFileInfo(path).absoluteFilePath();
    settings_.setLastFile(path_);
    int idx = -1;
    if (siteForPath(path_, &idx) && idx != currentSiteIndex())
        selectSite(idx);
    syncFormFromEditor();
    preview_->setSource(editor_->toPlainText(), QFileInfo(path_).absolutePath());
    updateTitle();
    updateStatus();
    note(QStringLiteral("Opened %1").arg(QFileInfo(path_).fileName()));
    return true;
}

void MainWindow::openPaths(const QStringList &paths)
{
    for (const QString &p : paths) {
        if (QFileInfo(p).isFile()) {
            openFile(p);
            return;
        }
    }
    const QString last = settings_.lastFile();
    if (paths.isEmpty() && !last.isEmpty() && QFileInfo::exists(last))
        openFile(last);
}

void MainWindow::openDialog()
{
    if (!maybeSave())
        return;
    const int i = currentSiteIndex();
    const QString start = i >= 0 ? config_->sites.at(i).root() : QDir::homePath();
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open Post"), start,
                                                      QStringLiteral("Markdown (*.md *.markdown *.mdx);;All files (*)"));
    if (!path.isEmpty())
        openFile(path);
}

bool MainWindow::save()
{
    if (path_.isEmpty())
        return saveAs();
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, QStringLiteral("Save"), file.errorString());
        return false;
    }
    QString text = editor_->toPlainText();
    if (!text.endsWith(QLatin1Char('\n')))
        text += QLatin1Char('\n');
    file.write(text.toUtf8());
    if (!file.commit()) {
        QMessageBox::warning(this, QStringLiteral("Save"), file.errorString());
        return false;
    }
    editor_->document()->setModified(false);
    note(QStringLiteral("Saved %1").arg(QFileInfo(path_).fileName()));
    return true;
}

bool MainWindow::saveAs()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Post"),
                                                      path_.isEmpty() ? QDir::homePath() : path_,
                                                      QStringLiteral("Markdown (*.md *.mdx)"));
    if (path.isEmpty())
        return false;
    path_ = path;
    const bool ok = save();
    refreshPosts();
    updateTitle();
    return ok;
}

bool MainWindow::maybeSave()
{
    if (!editor_->document()->isModified())
        return true;
    const auto answer = QMessageBox::question(this, QStringLiteral("Unsaved changes"),
                                              QStringLiteral("Save changes to %1?").arg(path_.isEmpty() ? QStringLiteral("this post") : QFileInfo(path_).fileName()),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save)
        return save();
    return answer == QMessageBox::Discard;
}

void MainWindow::newPost()
{
    if (!maybeSave())
        return;
    NewPostDialog dialog(*config_, currentSiteIndex(), this);
    if (dialog.exec() != QDialog::Accepted || dialog.siteIndex() < 0)
        return;
    const Site &site = config_->sites.at(dialog.siteIndex());
    const Collection *c = site.collection(dialog.collectionName());
    if (!c)
        return;
    const QString path = dialog.absolutePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, QStringLiteral("New Post"), file.errorString());
        return;
    }
    file.write(post::newPostText(site, *c, dialog.title(), dialog.date(), dialog.tags(), dialog.description()).toUtf8());
    if (!file.commit()) {
        QMessageBox::warning(this, QStringLiteral("New Post"), file.errorString());
        return;
    }
    selectSite(dialog.siteIndex());
    openFile(path);
    refreshPosts();
    editor_->moveCursor(QTextCursor::End);
    editor_->setFocus();
}

QString MainWindow::postUrl(const QString &path) const
{
    const Site *site = siteForPath(path);
    if (!site)
        return {};
    QString rel;
    const Collection *c = post::collectionFor(*site, path, &rel);
    if (!c)
        return {};
    QDate date;
    QString slug;
    if (!post::parsePath(*c, rel, &date, &slug))
        return {};
    if (!date.isValid()) {
        // MDX trees have no date in the path; take it from the front matter if present.
        date = QDate::fromString(FrontMatter::parse(editor_->toPlainText(), nullptr).string(QStringLiteral("date")).left(10),
                                 QStringLiteral("yyyy-MM-dd"));
        if (!date.isValid())
            date = QDate::currentDate();
    }
    return post::publicUrl(*site, *c, date, slug);
}

void MainWindow::publish()
{
    if (path_.isEmpty()) {
        note(QStringLiteral("Save the post inside a site first."));
        return;
    }
    if (git_->isRunning()) {
        note(QStringLiteral("A publish is already running."));
        return;
    }
    const Site *site = siteForPath(path_);
    if (!site) {
        QMessageBox::information(this, QStringLiteral("Publish"),
                                 QStringLiteral("This file is not inside any configured site. Add the site in Settings."));
        return;
    }
    if (!save())
        return;
    const FrontMatter fm = FrontMatter::parse(editor_->toPlainText(), nullptr);
    QString title = fm.string(QStringLiteral("title"));
    if (title.isEmpty())
        title = QFileInfo(path_).completeBaseName();
    QString message = site->commitMessage;
    message.replace(QStringLiteral("{title}"), title);
    bool ok = false;
    message = QInputDialog::getText(this, QStringLiteral("Publish"),
                                    QStringLiteral("Commit and push to %1/%2.\nCommit message:").arg(site->remote, site->branch),
                                    QLineEdit::Normal, message, &ok);
    if (!ok || message.trimmed().isEmpty())
        return;
    const QString rel = QDir(site->root()).relativeFilePath(path_);
    const QString url = postUrl(path_);
    const QString siteName = site->name;
    log_->clear();
    side_->setCurrentWidget(log_);
    publishAct_->setEnabled(false);
    note(QStringLiteral("Publishing to %1…").arg(siteName));
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(git_, &GitPublisher::finished, this, [this, conn, url, title](bool success, const QString &summary) {
        disconnect(*conn);
        publishAct_->setEnabled(true);
        log_->appendPlainText(QStringLiteral("\n") + summary);
        if (!success) {
            note(summary);
            return;
        }
        lastPublishedUrl_ = url;
        log_->appendPlainText(QStringLiteral("Post URL: %1\n(GitHub Pages usually needs a minute or two to rebuild.)").arg(url));
        note(QStringLiteral("Published: %1").arg(url));
        const auto answer = QMessageBox::question(this, QStringLiteral("Published"),
                                                  QStringLiteral("%1\n\n%2\n\nPost it to Bluesky?").arg(title, url),
                                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer == QMessageBox::Yes)
            postToBluesky(url);
    });
    git_->publish(site->root(), rel, message.trimmed(), site->remote, site->branch);
}

void MainWindow::postToBluesky(const QString &url)
{
    const FrontMatter fm = FrontMatter::parse(editor_->toPlainText(), nullptr);
    QString title = fm.string(QStringLiteral("title"));
    if (title.isEmpty() && !path_.isEmpty())
        title = QFileInfo(path_).completeBaseName();
    auto *dialog = new BlueskyDialog(config_, bsky_, title, url, fm.string(QStringLiteral("description")), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<BlueskyDialog> guard(dialog);
    connect(dialog, &BlueskyDialog::askGrok, this, [this] {
        side_->setCurrentWidget(chat_);
        chat_->blueskySummary();
    });
    connect(chat_, &ChatPanel::blueskyTextReady, dialog, [guard](const QString &text) {
        if (guard)
            guard->setText(text);
    });
    dialog->show();
}

void MainWindow::settingsDialog()
{
    SettingsDialog dialog(config_, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    editor_->setFontPointSize(config_->fontSize);
    editor_->setWrap(config_->wrap);
    refreshSites();
    note(QStringLiteral("Settings saved."));
}

void MainWindow::about()
{
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("About Blogy"));
    box.setIconPixmap(QIcon(QStringLiteral(":/icon-512.png")).pixmap(96, 96));
    box.setText(QStringLiteral("Blogy"));
    box.setInformativeText(QStringLiteral(
        "A black Markdown editor for GitHub Pages blogs.\n\n"
        "Write, preview, publish with git, tell Bluesky, and ask Grok.\n"
        "MIT License.\n\n"
        "Mira, The White Witch"));
    box.exec();
}

void MainWindow::onTextChanged()
{
    if (syncing_)
        return;
    preview_->setSource(editor_->toPlainText(), path_.isEmpty() ? QString() : QFileInfo(path_).absolutePath());
    if (!front_->isEditing())
        formSync_.start();
    updateStatus();
}

void MainWindow::syncFormFromEditor()
{
    front_->setFrontMatter(FrontMatter::parse(editor_->toPlainText(), nullptr));
}

void MainWindow::syncEditorFromForm()
{
    QString body;
    bool hadBlock = false;
    FrontMatter::parse(editor_->toPlainText(), &body, &hadBlock);
    const FrontMatter fm = front_->frontMatter();
    const QString text = fm.isEmpty() ? body : fm.compose(hadBlock ? body : QStringLiteral("\n") + body, isMdxFile());
    QTextCursor c(editor_->document());
    const int pos = editor_->textCursor().position();
    syncing_ = true;
    c.beginEditBlock();
    c.select(QTextCursor::Document);
    c.insertText(text);
    c.endEditBlock();
    syncing_ = false;
    QTextCursor restore = editor_->textCursor();
    restore.setPosition(qMin(pos, editor_->document()->characterCount() - 1));
    editor_->setTextCursor(restore);
    preview_->setSource(editor_->toPlainText(), QString());
}

void MainWindow::replaceFrontMatterYaml(const QString &yaml)
{
    const FrontMatter fm = FrontMatter::parseYaml(yaml);
    if (fm.isEmpty()) {
        note(QStringLiteral("The reply did not look like front matter."));
        return;
    }
    QString body;
    FrontMatter::parse(editor_->toPlainText(), &body);
    QTextCursor c(editor_->document());
    c.beginEditBlock();
    c.select(QTextCursor::Document);
    c.insertText(fm.compose(body, isMdxFile()));
    c.endEditBlock();
    syncFormFromEditor();
}

QString MainWindow::siteContext() const
{
    const int i = currentSiteIndex();
    if (i < 0)
        return {};
    const Site &s = config_->sites.at(i);
    QStringList cols;
    for (const Collection &c : s.collections)
        cols << c.name;
    return QStringLiteral("Site: %1 (%2, %3). Collections: %4. Default front matter keys: %5.")
        .arg(s.name, s.type, s.url, cols.join(QStringLiteral(", ")), s.frontMatter.keys().join(QStringLiteral(", ")));
}

void MainWindow::updateTitle()
{
    const QString name = path_.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(path_).fileName();
    setWindowTitle(QStringLiteral("%1%2 — Blogy").arg(name, editor_->document()->isModified() ? QStringLiteral(" •") : QString()));
}

void MainWindow::updateStatus()
{
    const QTextCursor c = editor_->textCursor();
    const int words = editor_->wordCount();
    statusRight_->setText(QStringLiteral("Ln %1, Col %2   %3 words   ~%4 min")
                              .arg(c.blockNumber() + 1)
                              .arg(c.positionInBlock() + 1)
                              .arg(words)
                              .arg(qMax(1, (words + 199) / 200)));
}

void MainWindow::note(const QString &text)
{
    statusLeft_->setText(text);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    settings_.setGeometry(saveGeometry());
    settings_.setSplitter(split_->saveState());
    event->accept();
}
