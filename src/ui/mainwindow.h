#pragma once

#include "config.h"
#include "settings.h"

#include <QMainWindow>
#include <QShowEvent>
#include <QTimer>

class BlueskyClient;
class ChatPanel;
class DictateController;
class FormatBar;
class FrontMatterForm;
class GitPublisher;
class GrokClient;
class MarkdownEdit;
class Preview;
class PublishPanel;
class QComboBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Config *config, QWidget *parent = nullptr);

    bool openFile(const QString &path);
    void openPaths(const QStringList &paths);
    QString currentPath() const { return path_; }
    MarkdownEdit *editor() const { return editor_; }
    QTreeWidget *postTree() const { return tree_; }
    FrontMatterForm *frontForm() const { return front_; }
    int currentSiteIndex() const;
    void selectSite(int index);

    // Publish state, exposed for --self-test.
    QString postUrl(const QString &path) const;

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void buildUi();
    void buildMenus();
    void applyPanes();
    void setFilesOpen(bool open);
    void showTools(QWidget *tab);
    void refreshSites();
    void refreshPosts();
    bool save();
    bool saveTo(bool nameFromTitle);
    bool saveAs();
    bool maybeSave();
    // Renames the open file from its title. Sets renamed when the path changed.
    // Returns a short note, or empty when the name was already right.
    QString adoptTitleFileName(bool *renamed);
    void newPost();
    void openDialog();
    void publish();
    void refreshPublish();
    void finishFromOutline();
    void postToBluesky(const QString &url);
    void settingsDialog();
    void about();
    void help();
    void showFind(bool replace);
    void updateTitle();
    void updateStatus();
    void refreshPreview();
    void onTextChanged();
    void syncFormFromEditor();
    void syncEditorFromForm();
    void replaceFrontMatterYaml(const QString &yaml);
    bool isMdxFile() const;
    QString siteContext() const;
    void note(const QString &text);
    const Site *siteForPath(const QString &path, int *index = nullptr) const;

    Config *config_;
    Settings settings_;
    QNetworkAccessManager *net_ = nullptr;
    GrokClient *grok_ = nullptr;
    BlueskyClient *bsky_ = nullptr;
    GitPublisher *git_ = nullptr;

    QWidget *sidebar_ = nullptr;
    QWidget *filesBody_ = nullptr;
    QLabel *wordmark_ = nullptr;
    QToolButton *filesToggle_ = nullptr;
    QComboBox *siteBox_ = nullptr;
    QTreeWidget *tree_ = nullptr;
    MarkdownEdit *editor_ = nullptr;
    FormatBar *formatBar_ = nullptr;
    Preview *preview_ = nullptr;
    QStackedWidget *page_ = nullptr;
    QSplitter *split_ = nullptr;
    QTabWidget *side_ = nullptr;
    FrontMatterForm *front_ = nullptr;
    ChatPanel *chat_ = nullptr;
    PublishPanel *publish_ = nullptr;
    QLabel *statusLeft_ = nullptr;
    QLabel *statusRight_ = nullptr;
    QWidget *findBar_ = nullptr;
    QLineEdit *findEdit_ = nullptr;
    QLineEdit *replaceEdit_ = nullptr;
    QAction *previewAct_ = nullptr;
    QAction *toolsAct_ = nullptr;
    QAction *publishAct_ = nullptr;
    QAction *dictateAct_ = nullptr;
    DictateController *dictate_ = nullptr;

    QString path_;
    QTimer formSync_;
    bool syncing_ = false;
    bool panesReady_ = false;
    bool toolsOpen_ = false;
    int toolsWidth_ = 420;
    int filesWidth_ = 280;
    bool filesOpen_ = true;
    QString lastPublishedUrl_;
    QString publishPath_;
    QString lastSuggested_;
};
