#include "ui/mainwindow.h"

#include "bluesky.h"
#include "editor.h"
#include "frontmatter.h"
#include "git.h"
#include "grok.h"
#include "media.h"
#include "post.h"
#include "preview.h"
#include "ui/chatpanel.h"
#include "ui/dialogs.h"
#include "dictate.h"
#include "ui/formatbar.h"
#include "ui/frontmatterform.h"
#include "ui/icons.h"
#include "ui/publishpanel.h"
#include "grokauth.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QPlainTextEdit>
#include <QPointer>
#include <QRegularExpression>
#include <QPushButton>
#include <QSaveFile>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBrowser>
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
    formatBar_ = new FormatBar(editor_, this);
    buildMenus();
    addToolBarBreak();
    addToolBar(formatBar_);
    editor_->setLearnedWords(settings_.dictionary());
    connect(editor_, &MarkdownEdit::learnedWordsChanged, this, [this](const QStringList &words) { settings_.setDictionary(words); });
    dictate_ = new DictateController(editor_, this);
    if (dictateAct_) {
        connect(dictateAct_, &QAction::toggled, this, [this](bool on) {
            if (!on) {
                dictate_->stop();
                return;
            }
            QString why;
            const QString bearer = grokBearerToken(*config_, &why);
            if (bearer.isEmpty()) {
                QSignalBlocker block(dictateAct_);
                dictateAct_->setChecked(false);
                note(why);
                QMessageBox::warning(this, QStringLiteral("Dictate"), why);
                return;
            }
            dictate_->start(bearer);
        });
        connect(dictate_, &DictateController::listeningChanged, this, [this](bool on) {
            QSignalBlocker block(dictateAct_);
            dictateAct_->setChecked(on);
            const QString keys = dictateAct_->shortcut().toString(QKeySequence::NativeText);
            dictateAct_->setToolTip(on ? QStringLiteral("Listening. Click to stop.")
                                       : QStringLiteral("Dictate (") + keys + QLatin1Char(')'));
            if (!on)
                note(QStringLiteral("Dictation off."));
        });
        connect(dictate_, &DictateController::status, this, &MainWindow::note);
        connect(dictate_, &DictateController::failed, this, [this](const QString &message) {
            note(message);
            QMessageBox::warning(this, QStringLiteral("Dictate"), message);
        });
    }
    connect(editor_, &MarkdownEdit::finishFromOutline, this, &MainWindow::finishFromOutline, Qt::QueuedConnection);
    refreshSites();

    if (!settings_.geometry().isEmpty())
        restoreGeometry(settings_.geometry());
    else
        resize(1440, 900);
    if (!settings_.splitter().isEmpty())
        split_->restoreState(settings_.splitter());
    {
        QSignalBlocker blockPreview(previewAct_);
        previewAct_->setChecked(settings_.previewVisible());
        toolsOpen_ = settings_.toolsVisible();
    }
    setFilesOpen(settings_.filesVisible());
    applyPanes();
    editor_->setFocus();
    updateTitle();
    updateStatus();
    refreshPublish();
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("root"));
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    split_ = new QSplitter(Qt::Horizontal, central);
    split_->setChildrenCollapsible(true);
    layout->addWidget(split_);

    // Sidebar: site picker and the posts in each collection.
    sidebar_ = new QWidget(split_);
    sidebar_->setObjectName(QStringLiteral("sidebar"));
    auto *outer = new QVBoxLayout(sidebar_);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *header = new QWidget(sidebar_);
    auto *headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(6, 8, 8, 4);
    headerRow->setSpacing(6);
    filesToggle_ = new QToolButton(header);
    filesToggle_->setObjectName(QStringLiteral("paneToggle"));
    filesToggle_->setText(QStringLiteral("‹"));
    filesToggle_->setFixedSize(22, 22);
    filesToggle_->setFocusPolicy(Qt::NoFocus);
    filesToggle_->setCursor(Qt::PointingHandCursor);
    filesToggle_->setToolTip(QStringLiteral("Hide posts"));
    filesToggle_->setAccessibleName(QStringLiteral("Hide posts"));
    wordmark_ = new QLabel(QStringLiteral("Blogy"), header);
    wordmark_->setObjectName(QStringLiteral("wordmark"));
    QFont wf = wordmark_->font();
    wf.setPointSize(18);
    wf.setBold(true);
    wordmark_->setFont(wf);
    headerRow->addWidget(filesToggle_, 0, Qt::AlignVCenter);
    headerRow->addWidget(wordmark_, 1);
    outer->addWidget(header);
    connect(filesToggle_, &QToolButton::clicked, this, [this] { setFilesOpen(!filesOpen_); });

    filesBody_ = new QWidget(sidebar_);
    auto *sv = new QVBoxLayout(filesBody_);
    sv->setContentsMargins(10, 0, 10, 10);
    siteBox_ = new QComboBox(filesBody_);
    siteBox_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    siteBox_->setMinimumContentsLength(10);
    siteBox_->setMinimumWidth(0);
    siteBox_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    sv->addWidget(siteBox_);
    tree_ = new QTreeWidget(filesBody_);
    tree_->setHeaderHidden(true);
    tree_->setRootIsDecorated(true);
    tree_->setUniformRowHeights(true);
    tree_->header()->setStretchLastSection(true);
    tree_->header()->setSectionResizeMode(QHeaderView::Stretch);
    sv->addWidget(tree_, 1);
    auto *newButton = new QPushButton(QStringLiteral("New Post"), filesBody_);
    newButton->setObjectName(QStringLiteral("primary"));
    sv->addWidget(newButton);
    outer->addWidget(filesBody_, 1);
    connect(newButton, &QPushButton::clicked, this, &MainWindow::newPost);
    connect(siteBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
        siteBox_->setToolTip(siteBox_->currentText());
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

    // Preview replaces the editor. It takes the whole text column, not a strip beside it.
    page_ = new QStackedWidget(split_);
    page_->setObjectName(QStringLiteral("page"));
    page_->setMinimumWidth(0);
    page_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    editor_ = new MarkdownEdit(page_);
    editor_->setFontPointSize(config_->fontSize);
    editor_->setWrap(config_->wrap);
    preview_ = new Preview(page_);
    preview_->setDelay(config_->previewDelayMs);
    preview_->setMinimumWidth(0);
    preview_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    page_->addWidget(editor_);
    page_->addWidget(preview_);
    connect(editor_, &QPlainTextEdit::textChanged, this, &MainWindow::onTextChanged);
    connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, &MainWindow::updateStatus);
    connect(editor_->document(), &QTextDocument::modificationChanged, this, &MainWindow::updateTitle);

    side_ = new QTabWidget(split_);
    front_ = new FrontMatterForm(side_);
    chat_ = new ChatPanel(config_, grok_, editor_, side_);
    publish_ = new PublishPanel(side_);
    side_->addTab(chat_, QStringLiteral("Chat"));
    side_->addTab(front_, QStringLiteral("Front matter"));
    side_->addTab(publish_, QStringLiteral("Publish"));
    side_->setTabToolTip(0, QStringLiteral("Ask Grok about this post. Finish the post writes the full article from the outline."));
    side_->setTabToolTip(1, QStringLiteral("The fields at the top of the post."));
    side_->setTabToolTip(2, QStringLiteral("The git command for this site, and its output."));
    side_->setDocumentMode(true);
    side_->tabBar()->setExpanding(true);
    side_->tabBar()->setUsesScrollButtons(false);
    side_->tabBar()->setElideMode(Qt::ElideNone);
    side_->setMinimumHeight(0);
    side_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    side_->setCurrentWidget(chat_);
    connect(side_, &QTabWidget::currentChanged, this, [this](int) {
        if (toolsOpen_)
            applyPanes();
    });
    connect(front_, &FrontMatterForm::edited, this, &MainWindow::syncEditorFromForm);
    chat_->replaceFrontMatter = [this](const QString &yaml) { replaceFrontMatterYaml(yaml); };
    chat_->siteContext = [this] { return siteContext(); };
    chat_->siteTune = [this] {
        const int i = currentSiteIndex();
        return i < 0 ? QString() : config_->sites.at(i).tune;
    };
    chat_->postUrl = [this] { return postUrl(path_); };
    connect(chat_, &ChatPanel::activity, this, &MainWindow::note);
    connect(publish_, &PublishPanel::publishRequested, this, &MainWindow::publish);
    connect(publish_, &PublishPanel::messageEdited, this, &MainWindow::refreshPublish);

    split_->addWidget(sidebar_);
    split_->addWidget(page_);
    split_->addWidget(side_);
    split_->setStretchFactor(0, 0);
    split_->setStretchFactor(1, 1);
    split_->setStretchFactor(2, 0);
    split_->setCollapsible(0, false);
    split_->setCollapsible(1, false);
    split_->setCollapsible(2, true);
    side_->setMinimumWidth(0);
    sidebar_->setMinimumWidth(180);
    split_->setSizes({280, 900, 0});

    findBar_ = new QWidget(central);
    findBar_->setObjectName(QStringLiteral("findBar"));
    findBar_->hide();
    auto *findRow = new QHBoxLayout(findBar_);
    findRow->setContentsMargins(8, 6, 8, 6);
    findEdit_ = new QLineEdit(findBar_);
    findEdit_->setPlaceholderText(QStringLiteral("Find"));
    replaceEdit_ = new QLineEdit(findBar_);
    replaceEdit_->setPlaceholderText(QStringLiteral("Replace"));
    auto *next = new QPushButton(QStringLiteral("Next"), findBar_);
    auto *previous = new QPushButton(QStringLiteral("Previous"), findBar_);
    auto *replaceOne = new QPushButton(QStringLiteral("Replace"), findBar_);
    auto *replaceAll = new QPushButton(QStringLiteral("All"), findBar_);
    auto *closeFind = new QPushButton(QStringLiteral("Close"), findBar_);
    findRow->addWidget(findEdit_, 1);
    findRow->addWidget(next);
    findRow->addWidget(previous);
    findRow->addWidget(replaceEdit_, 1);
    findRow->addWidget(replaceOne);
    findRow->addWidget(replaceAll);
    findRow->addWidget(closeFind);
    layout->addWidget(findBar_);
    const auto runFind = [this](bool forward) {
        if (!editor_->findText(findEdit_->text(), forward, false))
            note(QStringLiteral("Not found."));
        else
            note(QString());
    };
    connect(next, &QPushButton::clicked, this, [runFind] { runFind(true); });
    connect(previous, &QPushButton::clicked, this, [runFind] { runFind(false); });
    connect(findEdit_, &QLineEdit::returnPressed, this, [this, runFind] {
        runFind(!(QApplication::keyboardModifiers() & Qt::ShiftModifier));
    });
    connect(replaceOne, &QPushButton::clicked, this, [this] {
        const QString needle = findEdit_->text();
        if (needle.isEmpty())
            return;
        QTextCursor c = editor_->textCursor();
        if (c.hasSelection() && c.selectedText().compare(needle, Qt::CaseInsensitive) == 0)
            c.insertText(replaceEdit_->text());
        if (!editor_->findText(needle, true, false))
            note(QStringLiteral("Not found."));
    });
    connect(replaceAll, &QPushButton::clicked, this, [this] {
        const int count = editor_->replaceAllText(findEdit_->text(), replaceEdit_->text(), false);
        note(count == 0 ? QStringLiteral("Not found.") : QStringLiteral("Replaced %1.").arg(count));
    });
    connect(closeFind, &QPushButton::clicked, this, [this] {
        findBar_->hide();
        editor_->setFocus();
    });
    auto *closeFindKey = new QShortcut(QKeySequence(Qt::Key_Escape), findBar_);
    closeFindKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(closeFindKey, &QShortcut::activated, closeFind, &QPushButton::click);
    setCentralWidget(central);

    statusLeft_ = new QLabel(this);
    statusRight_ = new QLabel(this);
    statusRight_->setObjectName(QStringLiteral("muted"));
    statusBar()->addWidget(statusLeft_, 1);
    statusBar()->addPermanentWidget(statusRight_);

    connect(git_, &GitPublisher::output, this, [this](const QString &text) { publish_->appendOutput(text); });
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

    auto tip = [](QAction *action, const QString &text) {
        action->setStatusTip(text);
        const QString keys = action->shortcut().toString(QKeySequence::NativeText);
        action->setToolTip(keys.isEmpty() ? text : text + QStringLiteral(" (") + keys + QLatin1Char(')'));
    };
    tip(newAct, QStringLiteral("New post"));
    tip(openAct, QStringLiteral("Open"));
    tip(saveAct, QStringLiteral("Save"));
    tip(publishAct_, QStringLiteral("Publish"));
    tip(bskyAct, QStringLiteral("Post to Bluesky"));
    tip(settingsAct, QStringLiteral("Settings"));

    dictateAct_ = new QAction(QStringLiteral("Dictate"), this);
    dictateAct_->setCheckable(true);
    dictateAct_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+D")));
    dictateAct_->setStatusTip(QStringLiteral("Click to listen. Click again to stop. Speech becomes Markdown."));
    dictateAct_->setToolTip(QStringLiteral("Dictate (")
                            + dictateAct_->shortcut().toString(QKeySequence::NativeText) + QLatin1Char(')'));

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

    QMenu *editMenu = menuBar()->addMenu(QStringLiteral("Edit"));
    auto *undoAct = editMenu->addAction(QStringLiteral("Undo"));
    undoAct->setShortcut(QKeySequence::Undo);
    connect(undoAct, &QAction::triggered, editor_, &QPlainTextEdit::undo);
    auto *redoAct = editMenu->addAction(QStringLiteral("Redo"));
    redoAct->setShortcut(QKeySequence::Redo);
    connect(redoAct, &QAction::triggered, editor_, &QPlainTextEdit::redo);
    editMenu->addSeparator();
    auto *cutAct = editMenu->addAction(QStringLiteral("Cut"));
    cutAct->setShortcut(QKeySequence::Cut);
    connect(cutAct, &QAction::triggered, editor_, &QPlainTextEdit::cut);
    auto *copyAct = editMenu->addAction(QStringLiteral("Copy"));
    copyAct->setShortcut(QKeySequence::Copy);
    connect(copyAct, &QAction::triggered, editor_, &QPlainTextEdit::copy);
    auto *pasteAct = editMenu->addAction(QStringLiteral("Paste"));
    pasteAct->setShortcut(QKeySequence::Paste);
    connect(pasteAct, &QAction::triggered, editor_, &QPlainTextEdit::paste);
    auto *allAct = editMenu->addAction(QStringLiteral("Select All"));
    allAct->setShortcut(QKeySequence::SelectAll);
    connect(allAct, &QAction::triggered, editor_, &QPlainTextEdit::selectAll);
    editMenu->addSeparator();
    auto *findAct = editMenu->addAction(QStringLiteral("Find…"));
    findAct->setShortcut(QKeySequence::Find);
    connect(findAct, &QAction::triggered, this, [this] { showFind(false); });
    auto *replaceAct = editMenu->addAction(QStringLiteral("Replace…"));
    replaceAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+H")));
    connect(replaceAct, &QAction::triggered, this, [this] { showFind(true); });
    editMenu->addSeparator();
    editMenu->addAction(dictateAct_);

    QMenu *format = menuBar()->addMenu(QStringLiteral("Format"));
    if (formatBar_)
        formatBar_->fillFormatMenu(format);

    QMenu *site = menuBar()->addMenu(QStringLiteral("Site"));
    site->addAction(reloadAct);
    site->addAction(openSite);
    site->addAction(viewSite);

    QMenu *view = menuBar()->addMenu(QStringLiteral("View"));
    previewAct_ = view->addAction(QStringLiteral("Preview"));
    previewAct_->setCheckable(true);
    previewAct_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    previewAct_->setStatusTip(QStringLiteral("Preview"));
    previewAct_->setToolTip(QStringLiteral("Preview (")
                            + previewAct_->shortcut().toString(QKeySequence::NativeText) + QLatin1Char(')'));
    connect(previewAct_, &QAction::toggled, this, [this](bool on) {
        applyPanes();
        if (on)
            preview_->setFocus();
        else
            editor_->setFocus();
    });
    toolsAct_ = view->addAction(QStringLiteral("Tools"));
    toolsAct_->setCheckable(true);
    toolsAct_->setShortcuts({QKeySequence(QStringLiteral("Ctrl+J")), QKeySequence(QStringLiteral("Ctrl+Shift+M"))});
    toolsAct_->setStatusTip(QStringLiteral("Chat, front matter, and publish."));
    toolsAct_->setToolTip(QStringLiteral("Tools (Ctrl+J)"));
    connect(toolsAct_, &QAction::toggled, this, [this](bool on) {
        if (on) {
            toolsOpen_ = true;
            side_->setCurrentWidget(chat_);
            applyPanes();
            chat_->focusInput();
            return;
        }
        toolsOpen_ = false;
        applyPanes();
        if (previewAct_->isChecked())
            preview_->setFocus();
        else
            editor_->setFocus();
    });
    auto *fmAct = view->addAction(QStringLiteral("Front Matter"));
    fmAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+M")));
    connect(fmAct, &QAction::triggered, this, [this] {
        if (toolsOpen_ && side_->currentWidget() == front_) {
            toolsOpen_ = false;
            applyPanes();
            editor_->setFocus();
            return;
        }
        showTools(front_);
    });
    auto *zoomIn = view->addAction(QStringLiteral("Bigger Text"));
    zoomIn->setShortcut(QKeySequence::ZoomIn);
    connect(zoomIn, &QAction::triggered, this, [this] { editor_->setFontPointSize(editor_->fontPointSize() + 1); });
    auto *zoomOut = view->addAction(QStringLiteral("Smaller Text"));
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOut, &QAction::triggered, this, [this] { editor_->setFontPointSize(editor_->fontPointSize() - 1); });

    auto *helpAct = new QAction(QStringLiteral("Help"), this);
    helpAct->setShortcut(QKeySequence::HelpContents);
    helpAct->setStatusTip(QStringLiteral("Help"));
    helpAct->setToolTip(QStringLiteral("Help (") + helpAct->shortcut().toString(QKeySequence::NativeText)
                        + QLatin1Char(')'));
    connect(helpAct, &QAction::triggered, this, &MainWindow::help);
    auto *aboutAct = new QAction(QStringLiteral("About Blogy"), this);
    connect(aboutAct, &QAction::triggered, this, &MainWindow::about);
    QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("Help"));
    helpMenu->addAction(helpAct);
    helpMenu->addAction(aboutAct);

    // Words on this row. Symbols stay on the format row under it.
    // iconText is the label on the button. The menu keeps the longer name.
    QToolBar *bar = addToolBar(QStringLiteral("Main"));
    bar->setObjectName(QStringLiteral("mainToolbar"));
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto word = [&](QAction *action, const QString &label) {
        action->setIcon(QIcon());
        action->setIconText(label);
        bar->addAction(action);
        if (auto *button = qobject_cast<QToolButton *>(bar->widgetForAction(action))) {
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
            const int textW = button->fontMetrics().horizontalAdvance(label);
            button->setMinimumWidth(textW + 24);
        }
    };
    word(newAct, QStringLiteral("New Post"));
    word(openAct, QStringLiteral("Open"));
    word(saveAct, QStringLiteral("Save"));
    bar->addSeparator();
    word(publishAct_, QStringLiteral("Publish"));
    if (auto *b = qobject_cast<QToolButton *>(bar->widgetForAction(publishAct_)))
        b->setObjectName(QStringLiteral("primary"));
    word(bskyAct, QStringLiteral("Post to Bluesky"));
    bar->addSeparator();
    word(previewAct_, QStringLiteral("Preview"));
    word(toolsAct_, QStringLiteral("Tools"));
    bar->addSeparator();
    word(dictateAct_, QStringLiteral("Dictate"));
    if (auto *dictateButton = qobject_cast<QToolButton *>(bar->widgetForAction(dictateAct_))) {
        dictateButton->setAutoRepeat(false);
        dictateButton->setCheckable(true);
    }
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (!panesReady_) {
        panesReady_ = true;
        // Sizes set before the window is shown get thrown away on the first layout.
        if (!settings_.splitter().isEmpty() && !split_->restoreState(settings_.splitter())) {
            // Older builds saved four columns, with preview beside the editor.
            if (split_->width() > 400)
                split_->setSizes({280, qMax(200, split_->width() - 280), 0});
        } else if (settings_.splitter().isEmpty() && split_->width() > 400) {
            split_->setSizes({280, split_->width() - 280, 0});
        }
        setFilesOpen(settings_.filesVisible());
        applyPanes();
        if (previewAct_->isChecked())
            preview_->setFocus();
        else
            editor_->setFocus();
    }
}

void MainWindow::applyPanes()
{
    if (!page_ || !preview_ || !side_ || !previewAct_ || !toolsAct_)
        return;
    const bool previewOn = previewAct_->isChecked();
    const bool toolsOn = toolsOpen_;
    page_->setCurrentWidget(previewOn ? static_cast<QWidget *>(preview_) : static_cast<QWidget *>(editor_));
    side_->setVisible(toolsOn);
    side_->setMinimumWidth(toolsOn ? 300 : 0);

    QList<int> sizes = split_->sizes();
    if (sizes.size() == 3) {
        if (!toolsOn) {
            if (sizes.at(2) > 40)
                toolsWidth_ = sizes.at(2);
            sizes[2] = 0;
        } else if (sizes.at(2) < 40) {
            sizes[2] = toolsWidth_;
        }
        if (split_->width() > 200) {
            const int width = split_->width();
            int files = sizes.at(0);
            int side = sizes.at(2);
            int page = width - files - side;
            // The text column stays the wide one. Preview uses this same width.
            if (page < 280) {
                int need = 280 - page;
                const int sideFloor = toolsOn ? 300 : 0;
                const int takeSide = qMin(need, qMax(0, side - sideFloor));
                side -= takeSide;
                need -= takeSide;
                const int fileFloor = filesOpen_ ? 180 : 32;
                files -= qMin(need, qMax(0, files - fileFloor));
                page = qMax(80, width - files - side);
            }
            sizes[0] = files;
            sizes[1] = page;
            sizes[2] = side;
        }
        split_->setSizes(sizes);
    }
    settings_.setPreviewVisible(previewOn);
    settings_.setToolsVisible(toolsOn);
    if (previewOn)
        refreshPreview();
    if (toolsAct_->isChecked() != toolsOn) {
        QSignalBlocker block(toolsAct_);
        toolsAct_->setChecked(toolsOn);
    }
}

void MainWindow::refreshPreview()
{
    const Site *site = siteForPath(path_);
    preview_->setSource(editor_->toPlainText(), path_.isEmpty() ? QString() : QFileInfo(path_).absolutePath(),
                        site ? site->root() : QString(), site ? site->url : QString());
}

void MainWindow::setFilesOpen(bool open)
{
    filesOpen_ = open;
    if (wordmark_)
        wordmark_->setVisible(open);
    if (filesBody_)
        filesBody_->setVisible(open);
    if (filesToggle_) {
        filesToggle_->setText(open ? QStringLiteral("‹") : QStringLiteral("›"));
        const QString tip = open ? QStringLiteral("Hide posts") : QStringLiteral("Show posts");
        filesToggle_->setToolTip(tip);
        filesToggle_->setAccessibleName(tip);
        filesToggle_->setStatusTip(tip);
    }
    if (!sidebar_ || !split_)
        return;
    sidebar_->setMinimumWidth(open ? 180 : 32);
    sidebar_->setMaximumWidth(open ? QWIDGETSIZE_MAX : 32);
    QList<int> sizes = split_->sizes();
    if (sizes.size() == 3) {
        if (!open) {
            if (sizes.at(0) > 48) {
                filesWidth_ = sizes.at(0);
                settings_.setFilesWidth(filesWidth_);
            }
            sizes[0] = 32;
        } else {
            filesWidth_ = qMax(180, settings_.filesWidth());
            if (sizes.at(0) < 80)
                sizes[0] = filesWidth_;
        }
        if (split_->width() > 200) {
            const int extra = split_->width() - (sizes.at(0) + sizes.at(1) + sizes.at(2));
            if (extra != 0)
                sizes[1] = qMax(120, sizes.at(1) + extra);
            split_->setSizes(sizes);
        }
    }
    settings_.setFilesVisible(open);
}

void MainWindow::showTools(QWidget *tab)
{
    toolsOpen_ = true;
    side_->setCurrentWidget(tab);
    applyPanes();
    if (tab == chat_)
        chat_->focusInput();
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
    siteBox_->setToolTip(siteBox_->currentText());
    refreshPosts();
    refreshPublish();
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
            post::parsePath(site, c, QDir(site.root()).relativeFilePath(a), &da, &sa);
            post::parsePath(site, c, QDir(site.root()).relativeFilePath(b), &db, &sb);
            if (da != db)
                return da > db;
            return a > b;
        });
        for (const QString &file : files) {
            const QString rel = QDir(site.root()).relativeFilePath(file);
            QDate date;
            QString slug;
            QString label = QDir(dir).relativeFilePath(file);
            if (post::parsePath(site, c, rel, &date, &slug))
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
    refreshPreview();
    updateTitle();
    updateStatus();
    note(QStringLiteral("Opened %1").arg(QFileInfo(path_).fileName()));
    editor_->setFocus();
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
    return saveTo(true);
}

bool MainWindow::saveTo(bool nameFromTitle)
{
    if (path_.isEmpty())
        return saveAs();
    bool renamed = false;
    const QString nameNote = nameFromTitle ? adoptTitleFileName(&renamed) : QString();
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
    if (renamed) {
        refreshPosts();
        updateTitle();
    }
    QString msg = QStringLiteral("Saved %1").arg(QFileInfo(path_).fileName());
    if (!nameNote.isEmpty())
        msg += QStringLiteral(" — ") + nameNote;
    note(msg);
    return true;
}

QString MainWindow::adoptTitleFileName(bool *renamed)
{
    if (renamed)
        *renamed = false;
    if (path_.isEmpty())
        return {};
    const Site *site = siteForPath(path_);
    if (!site)
        return {};
    QString rel;
    const Collection *c = post::collectionFor(*site, path_, &rel);
    if (!c)
        return {};
    const FrontMatter fm = FrontMatter::parse(editor_->toPlainText(), nullptr);
    const QString title = fm.string(QStringLiteral("title")).trimmed();
    if (title.isEmpty())
        return {};

    QDate fromPath;
    QString oldSlug;
    post::parsePath(*site, *c, rel, &fromPath, &oldSlug);
    QDate date = fromPath;
    if (!date.isValid())
        date = QDate::fromString(fm.string(QStringLiteral("date")).left(10), QStringLiteral("yyyy-MM-dd"));
    if (!date.isValid())
        date = QDate::currentDate();

    const QString slug = post::slugify(title, site->slugSeparator);
    const QString desiredRel = post::relativePath(*site, *c, date, slug);
    const QString desired = QDir::cleanPath(site->root() + QLatin1Char('/') + desiredRel);
    const QString current = QDir::cleanPath(path_);
    if (desired == current)
        return {};
    if (QFileInfo::exists(desired))
        return QStringLiteral("kept this file name; %1 already exists").arg(QFileInfo(desired).fileName());

    QDir().mkpath(QFileInfo(desired).absolutePath());
    if (QFileInfo::exists(current) && !QFile::rename(current, desired))
        return QStringLiteral("could not rename the file to match the title");
    const QString oldParent = QFileInfo(current).absolutePath();
    if (oldParent != QFileInfo(desired).absolutePath() && !oldSlug.isEmpty() && QFileInfo(oldParent).fileName() == oldSlug)
        QDir().rmdir(oldParent);

    QString source = settings_.pendingRename(current);
    settings_.clearPendingRename(current);
    if (source.isEmpty())
        source = rel;
    if (source == desiredRel)
        settings_.clearPendingRename(desired);
    else
        settings_.setPendingRename(desired, source);

    path_ = desired;
    settings_.setLastFile(path_);
    if (renamed)
        *renamed = true;
    return QStringLiteral("named from the title");
}

bool MainWindow::saveAs()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Post"),
                                                      path_.isEmpty() ? QDir::homePath() : path_,
                                                      QStringLiteral("Markdown (*.md *.mdx)"));
    if (path.isEmpty())
        return false;
    path_ = path;
    const bool ok = saveTo(false);
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
    if (!post::parsePath(*site, *c, rel, &date, &slug))
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

void MainWindow::refreshPublish()
{
    if (!publish_ || (git_ && git_->isRunning()))
        return;
    const Site *site = path_.isEmpty() ? nullptr : siteForPath(path_);
    if (!site) {
        QStringList lines;
        lines << (path_.isEmpty() ? QStringLiteral("Open a post from one of these sites. Publish commits that file and pushes it.")
                                  : QStringLiteral("This file is not in a site Blogy knows."));
        lines << QString();
        if (config_->sites.isEmpty())
            lines << QStringLiteral("Add a site in Settings.");
        for (const Site &known : config_->sites)
            lines << QStringLiteral("%1\n%2").arg(known.name, known.root());
        publish_->setSummary(lines.join(QLatin1Char('\n')));
        publish_->setCommand(QString());
        publish_->setReady(false);
        return;
    }

    const QString repo = site->root();
    QString title = FrontMatter::parse(editor_->toPlainText(), nullptr).string(QStringLiteral("title")).trimmed();
    if (title.isEmpty())
        title = QFileInfo(path_).completeBaseName();
    QString suggested = site->commitMessage;
    suggested.replace(QStringLiteral("{title}"), title);
    if (path_ != publishPath_) {
        publishPath_ = path_;
        publish_->setMessage(suggested);
        lastSuggested_ = suggested;
    } else if (publish_->message().isEmpty() || publish_->message() == lastSuggested_) {
        publish_->setMessage(suggested);
        lastSuggested_ = suggested;
    }
    const QString message = publish_->message().trimmed().isEmpty() ? suggested : publish_->message().trimmed();
    const QString rel = QDir(repo).relativeFilePath(path_);
    const QList<GitStep> steps = gitPublishSteps(repo, rel, message, site->remote, site->branch, settings_.pendingRename(path_));
    QStringList commands;
    for (const GitStep &step : steps)
        commands << gitCommandLine(step.args);
    const bool git = QFileInfo::exists(repo + QStringLiteral("/.git"));
    QString summary = QStringLiteral("%1\n%2\n%3 → %4\n%5").arg(site->name, repo, site->remote, site->branch, rel);
    const QString url = postUrl(path_);
    if (!url.isEmpty())
        summary += QStringLiteral("\n") + url;
    if (!git)
        summary += QStringLiteral("\nThis folder is not a git repository.");
    else if (!site->signature.trimmed().isEmpty())
        summary += QStringLiteral("\nA signature is added once, at the end.");
    publish_->setSummary(summary);
    publish_->setCommand(commands.join(QLatin1Char('\n')));
    publish_->setReady(git && !rel.isEmpty() && !message.isEmpty() && QFileInfo(repo).isDir());
}

void MainWindow::finishFromOutline()
{
    if (grok_->busy())
        return;
    if (dictateAct_ && dictateAct_->isChecked())
        dictateAct_->setChecked(false);
    showTools(chat_);
    chat_->writePost();
}

void MainWindow::publish()
{
    refreshPublish();
    showTools(publish_);
    if (git_->isRunning()) {
        note(QStringLiteral("A publish is already running."));
        return;
    }
    if (path_.isEmpty()) {
        note(QStringLiteral("Open a post from a site first."));
        return;
    }
    const Site *site = siteForPath(path_);
    if (!site) {
        note(QStringLiteral("This file is not inside a configured site."));
        return;
    }
    if (!QFileInfo::exists(site->root() + QStringLiteral("/.git"))) {
        note(QStringLiteral("%1 is not a git repository.").arg(site->root()));
        return;
    }
    const QString message = publish_->message().trimmed();
    if (message.isEmpty()) {
        note(QStringLiteral("The commit message is empty."));
        return;
    }
    if (!save())
        return;
    site = siteForPath(path_);
    if (!site)
        return;
    QStringList also;
    QString bundleNote;
    if (config_->fixesLinks(*site)) {
        const media::PublishFix fixed = media::fixOnPublish(editor_->toPlainText(), path_, *site);
        if (fixed.markdown != editor_->toPlainText()) {
            editor_->replaceAll(fixed.markdown);
            syncFormFromEditor();
            if (!save())
                return;
        }
        also = fixed.gitPaths;
        bundleNote = fixed.note;
    }
    site = siteForPath(path_);
    if (!site)
        return;
    const QString signedText = post::withSignature(editor_->toPlainText(), site->signature);
    if (signedText != editor_->toPlainText())
        editor_->replaceAll(signedText);
    if (!save())
        return;
    site = siteForPath(path_);
    if (!site)
        return;

    const QString repo = site->root();
    const QString rel = QDir(repo).relativeFilePath(path_);
    const QString previous = settings_.pendingRename(path_);
    const QList<GitStep> steps = gitPublishSteps(repo, rel, message, site->remote, site->branch, previous, also);
    if (steps.isEmpty()) {
        note(QStringLiteral("Nothing to publish."));
        return;
    }
    QStringList commands;
    for (const GitStep &step : steps)
        commands << gitCommandLine(step.args);
    publishPath_ = path_;
    publish_->setCommand(commands.join(QLatin1Char('\n')));
    publish_->clearOutput();
    if (!bundleNote.isEmpty())
        publish_->appendOutput(bundleNote + QLatin1Char('\n'));

    const FrontMatter fm = FrontMatter::parse(editor_->toPlainText(), nullptr);
    QString title = fm.string(QStringLiteral("title")).trimmed();
    if (title.isEmpty())
        title = QFileInfo(path_).completeBaseName();
    const QString published = path_;
    const QString url = postUrl(path_);
    const QString siteName = site->name;
    publishAct_->setEnabled(false);
    publish_->setBusy(true);
    note(QStringLiteral("Publishing to %1…").arg(siteName));
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(git_, &GitPublisher::finished, this, [this, conn, url, title, published](bool success, const QString &summary) {
        disconnect(*conn);
        publishAct_->setEnabled(true);
        publish_->setBusy(false);
        publish_->appendOutput(QStringLiteral("\n") + summary + QLatin1Char('\n'));
        if (!success) {
            note(summary);
            return;
        }
        settings_.clearPendingRename(published);
        lastPublishedUrl_ = url;
        publish_->appendOutput(QStringLiteral("Post URL: %1\n(GitHub Pages usually needs a minute or two to rebuild.)\n").arg(url));
        note(QStringLiteral("Published: %1").arg(url));
        const auto answer = QMessageBox::question(this, QStringLiteral("Published"),
                                                  QStringLiteral("%1\n\n%2\n\nPost it to Bluesky?").arg(title, url),
                                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer == QMessageBox::Yes)
            postToBluesky(url);
    });
    git_->run(repo, steps);
}

void MainWindow::postToBluesky(const QString &url)
{
    const QString body = editor_->toPlainText();
    const FrontMatter fm = FrontMatter::parse(body, nullptr);
    QString title = fm.string(QStringLiteral("title"));
    if (title.isEmpty() && !path_.isEmpty())
        title = QFileInfo(path_).completeBaseName();
    const QString lead = post::excerpt(body);
    const QString announcement = post::blueskyAnnouncement(lead.isEmpty() ? title : lead, url);
    auto *dialog = new BlueskyDialog(config_, bsky_, title, url, lead.isEmpty() ? fm.string(QStringLiteral("description")) : lead, this);
    dialog->setText(announcement);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<BlueskyDialog> guard(dialog);
    connect(dialog, &BlueskyDialog::askGrok, this, [this] {
        showTools(chat_);
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

void MainWindow::showFind(bool replace)
{
    findBar_->show();
    if (!editor_->textCursor().selectedText().contains(QChar(0x2029))) {
        const QString selected = editor_->textCursor().selectedText();
        if (!selected.isEmpty())
            findEdit_->setText(selected);
    }
    if (replace) {
        replaceEdit_->setFocus();
        replaceEdit_->selectAll();
    } else {
        findEdit_->setFocus();
        findEdit_->selectAll();
    }
}

void MainWindow::help()
{
    QFile file(QStringLiteral(":/help.html"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Help"));
    dialog.resize(760, 680);
    auto *layout = new QVBoxLayout(&dialog);
    auto *view = new QTextBrowser(&dialog);
    view->setObjectName(QStringLiteral("helpView"));
    view->setOpenExternalLinks(true);
    view->document()->setDefaultStyleSheet(QStringLiteral(
        "h1 { color: #ffb454; font-size: 22px; } h2 { color: #ffd18a; font-size: 16px; }"
        "a { color: #ffb454; } td { padding: 3px 16px 3px 0; }"));
    QPalette pal = view->palette();
    pal.setColor(QPalette::Base, QColor(QStringLiteral("#000000")));
    pal.setColor(QPalette::Text, QColor(QStringLiteral("#e8e6e3")));
    view->setPalette(pal);
    view->setHtml(QString::fromUtf8(file.readAll()));
    layout->addWidget(view);
    auto *close = new QPushButton(QStringLiteral("Close"), &dialog);
    close->setObjectName(QStringLiteral("primary"));
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(close);
    layout->addLayout(row);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
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
    GlyphInk onAmber;
    onAmber.ink = QColor(0x1a, 0x12, 0x06);
    onAmber.disabled = QColor(0x6a, 0x5a, 0x40);
    auto *coffee = box.addButton(QStringLiteral("Buy me a coffee"), QMessageBox::ActionRole);
    coffee->setIcon(glyphIcon(ToolGlyph::Coffee, onAmber));
    coffee->setIconSize(QSize(20, 20));
    coffee->setCursor(Qt::PointingHandCursor);
    coffee->setToolTip(QStringLiteral("PayPal"));
    // QMessageBox buttons ignore the app's #primary rule, so the amber is set here.
    coffee->setStyleSheet(QStringLiteral(
        "QPushButton { background: #ffb454; color: #1a1206; border: 1px solid #ffb454;"
        " border-radius: 8px; padding: 6px 14px; font-weight: 600; }"
        "QPushButton:hover { background: #ffd18a; border-color: #ffd18a; }"));
    auto *ok = qobject_cast<QPushButton *>(box.addButton(QMessageBox::Ok));
    box.setDefaultButton(ok);
    box.setEscapeButton(ok);
    box.exec();
    if (box.clickedButton() == coffee)
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.paypal.com/ncp/payment/4P85EQRES3RGU")));
}

void MainWindow::onTextChanged()
{
    if (syncing_)
        return;
    if (preview_->isVisible())
        refreshPreview();
    if (!front_->isEditing())
        formSync_.start();
    updateStatus();
}

void MainWindow::syncFormFromEditor()
{
    front_->setFrontMatter(FrontMatter::parse(editor_->toPlainText(), nullptr));
    refreshPublish();
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
    refreshPreview();
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
    QString text = QStringLiteral("Site: %1 (%2, %3). Collections: %4. Default front matter keys: %5.")
                       .arg(s.name, s.type, s.url, cols.join(QStringLiteral(", ")), s.frontMatter.keys().join(QStringLiteral(", ")));
    const QString tune = s.tune.trimmed();
    if (!tune.isEmpty())
        text += QStringLiteral("\nVoice for this site: %1. Write in that voice.").arg(tune);
    return text;
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
    QString tail = QStringLiteral("Ln %1, Col %2   %3 words   ~%4 min")
                       .arg(c.blockNumber() + 1)
                       .arg(c.positionInBlock() + 1)
                       .arg(words)
                       .arg(qMax(1, (words + 199) / 200));
    if (c.hasSelection()) {
        const int selected = c.selectedText().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
        tail += QStringLiteral("   %1 selected").arg(selected);
    }
    statusRight_->setText(tail);
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
