#pragma once

#include "config.h"
#include "settings.h"

#include <QMainWindow>
#include <QTimer>

class BlueskyClient;
class ChatPanel;
class FrontMatterForm;
class GitPublisher;
class GrokClient;
class MarkdownEdit;
class Preview;
class QComboBox;
class QLabel;
class QNetworkAccessManager;
class QPlainTextEdit;
class QSplitter;
class QTabWidget;
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

private:
    void buildUi();
    void buildMenus();
    void refreshSites();
    void refreshPosts();
    bool save();
    bool saveAs();
    bool maybeSave();
    void newPost();
    void openDialog();
    void publish();
    void postToBluesky(const QString &url);
    void settingsDialog();
    void about();
    void updateTitle();
    void updateStatus();
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

    QComboBox *siteBox_ = nullptr;
    QTreeWidget *tree_ = nullptr;
    MarkdownEdit *editor_ = nullptr;
    Preview *preview_ = nullptr;
    QSplitter *split_ = nullptr;
    QTabWidget *side_ = nullptr;
    FrontMatterForm *front_ = nullptr;
    ChatPanel *chat_ = nullptr;
    QPlainTextEdit *log_ = nullptr;
    QLabel *statusLeft_ = nullptr;
    QLabel *statusRight_ = nullptr;
    QAction *previewAct_ = nullptr;
    QAction *publishAct_ = nullptr;

    QString path_;
    QTimer formSync_;
    bool syncing_ = false;
    QString lastPublishedUrl_;
};
