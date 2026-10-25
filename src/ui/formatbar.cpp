#include "ui/formatbar.h"

#include "editor.h"
#include "emoji.h"
#include "ui/icons.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QStandardPaths>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

class GlyphPicker : public QFrame {
public:
    GlyphPicker(const QVector<Glyph> &glyphs, MarkdownEdit *editor, QWidget *parent)
        : QFrame(parent, Qt::Popup), editor_(editor), glyphs_(glyphs)
    {
        setObjectName(QStringLiteral("glyphPicker"));
        setFrameShape(QFrame::StyledPanel);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 8, 8, 8);
        search_ = new QLineEdit(this);
        search_->setPlaceholderText(QStringLiteral("Search name, shortcode, or country"));
        layout->addWidget(search_);
        list_ = new QListWidget(this);
        list_->setUniformItemSizes(true);
        list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        font.setFamilies({QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans"), QStringLiteral("Noto Color Emoji"),
                          QStringLiteral("Noto Emoji")});
        font.setPointSize(13);
        list_->setFont(font);
        layout->addWidget(list_, 1);
        auto *hint = new QLabel(QStringLiteral("Enter inserts. In the post, :smile: and :flag-at: work too."), this);
        hint->setObjectName(QStringLiteral("hint"));
        hint->setWordWrap(true);
        layout->addWidget(hint);
        resize(360, 420);
        connect(search_, &QLineEdit::textChanged, this, [this](const QString &text) { refill(text); });
        connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) { take(item); });
        connect(search_, &QLineEdit::returnPressed, this, [this] {
            if (list_->count() > 0)
                take(list_->item(0));
        });
        refill({});
    }

    void popupAt(QWidget *anchor)
    {
        const QPoint pos = anchor->mapToGlobal(QPoint(0, anchor->height()));
        move(pos);
        show();
        search_->setFocus();
        search_->selectAll();
    }

private:
    void refill(const QString &query)
    {
        list_->clear();
        const QString q = query.trimmed();
        for (const Glyph &g : glyphs_) {
            if (!q.isEmpty() && !g.name.contains(q, Qt::CaseInsensitive) && !g.emoji.contains(q))
                continue;
            auto *item = new QListWidgetItem(g.emoji + QStringLiteral("   ") + g.name, list_);
            item->setData(Qt::UserRole, g.emoji);
        }
    }

    void take(QListWidgetItem *item)
    {
        if (!item || !editor_)
            return;
        editor_->insertSnippet(item->data(Qt::UserRole).toString());
        editor_->setFocus();
        close();
    }

    MarkdownEdit *editor_ = nullptr;
    QVector<Glyph> glyphs_;
    QLineEdit *search_ = nullptr;
    QListWidget *list_ = nullptr;
};

QString imageAlt(const QString &path)
{
    QString name = QFileInfo(path).completeBaseName();
    name.replace(QLatin1Char('_'), QLatin1Char(' '));
    name.replace(QLatin1Char('-'), QLatin1Char(' '));
    return name.trimmed().isEmpty() ? QStringLiteral("image") : name.trimmed();
}

struct Lang {
    const char *label;
    const char *tag;
};

const Lang kLangs[] = {
    {"Plain", ""},       {"Bash", "bash"},     {"C", "c"},
    {"C++", "cpp"},      {"CSS", "css"},       {"Go", "go"},
    {"HTML", "html"},    {"Java", "java"},     {"JavaScript", "javascript"},
    {"JSON", "json"},    {"Kotlin", "kotlin"}, {"Markdown", "markdown"},
    {"PHP", "php"},      {"Python", "python"}, {"Ruby", "ruby"},
    {"Rust", "rust"},    {"SQL", "sql"},       {"Swift", "swift"},
    {"TypeScript", "typescript"}, {"XML", "xml"}, {"YAML", "yaml"},
};

} // namespace

QAction *FormatBar::addIcon(ToolGlyph glyph, const QString &text, const QString &tip, const QKeySequence &key,
                           const std::function<void()> &fn)
{
    QAction *action = addAction(glyphIcon(glyph), text);
    action->setStatusTip(tip);
    const QString keys = key.toString(QKeySequence::NativeText);
    action->setToolTip(keys.isEmpty() ? tip : tip + QStringLiteral(" (") + keys + QLatin1Char(')'));
    if (!key.isEmpty())
        action->setShortcut(key);
    connect(action, &QAction::triggered, this, [fn](bool) { fn(); });
    return action;
}

void FormatBar::rebuildHeadings(QMenu *menu)
{
    menu->clear();
    for (QAction *action : headingActions_)
        menu->addAction(action);
    menu->addSeparator();
    const QVector<HeadingMark> marks = editor_->headings();
    if (marks.isEmpty()) {
        QAction *none = menu->addAction(QStringLiteral("No headings in this post"));
        none->setEnabled(false);
        return;
    }
    for (const HeadingMark &mark : marks) {
        QString label = QStringLiteral("H%1  %2").arg(mark.level).arg(mark.text);
        if (label.size() > 72)
            label = label.left(69) + QStringLiteral("…");
        QAction *jump = menu->addAction(label);
        const int position = mark.position;
        connect(jump, &QAction::triggered, this, [this, position] { editor_->goToPosition(position); });
    }
}

FormatBar::FormatBar(MarkdownEdit *editor, QWidget *parent)
    : QToolBar(QStringLiteral("Format"), parent), editor_(editor)
{
    setObjectName(QStringLiteral("formatToolbar"));
    setMovable(false);
    setFloatable(false);
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setIconSize(QSize(20, 20));

    auto keep = [this](QAction *action) {
        simple_.append(action);
        return action;
    };

    keep(addIcon(ToolGlyph::Bold, QStringLiteral("Bold"), QStringLiteral("Bold"), QKeySequence(QStringLiteral("Ctrl+B")),
                 [this] { editor_->toggleWrap(QStringLiteral("**"), QStringLiteral("**")); }));
    keep(addIcon(ToolGlyph::Italic, QStringLiteral("Italic"), QStringLiteral("Italic"), QKeySequence(QStringLiteral("Ctrl+I")),
                 [this] { editor_->toggleWrap(QStringLiteral("*"), QStringLiteral("*")); }));
    keep(addIcon(ToolGlyph::Strike, QStringLiteral("Strikethrough"), QStringLiteral("Strikethrough"),
                 QKeySequence(QStringLiteral("Ctrl+Shift+X")),
                 [this] { editor_->toggleWrap(QStringLiteral("~~"), QStringLiteral("~~")); }));
    keep(addIcon(ToolGlyph::Code, QStringLiteral("Code"), QStringLiteral("Inline code"), QKeySequence(QStringLiteral("Ctrl+`")),
                 [this] { editor_->toggleWrap(QStringLiteral("`"), QStringLiteral("`")); }));
    addSeparator();

    headingMenu_ = new QMenu(this);
    for (int level = 1; level <= 6; ++level) {
        auto *action = new QAction(QStringLiteral("Heading %1").arg(level), this);
        action->setShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(level)));
        action->setStatusTip(QStringLiteral("Heading %1. Choose it again to make the line a paragraph.").arg(level));
        connect(action, &QAction::triggered, this, [this, level] { editor_->setHeading(level); });
        editor_->addAction(action);
        headingActions_.append(action);
    }
    connect(headingMenu_, &QMenu::aboutToShow, this, [this] { rebuildHeadings(headingMenu_); });
    auto *headingButton = new QToolButton(this);
    headingButton->setObjectName(QStringLiteral("menuTool"));
    headingButton->setText(QStringLiteral("Heading"));
    headingButton->setIcon(glyphIcon(ToolGlyph::Heading));
    headingButton->setIconSize(iconSize());
    headingButton->setMenu(headingMenu_);
    headingButton->setPopupMode(QToolButton::InstantPopup);
    headingButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    headingButton->setToolTip(QStringLiteral("Heading level, or jump to one in this post"));
    headingButton->setStatusTip(QStringLiteral("Heading"));
    addWidget(headingButton);

    keep(addIcon(ToolGlyph::Bullets, QStringLiteral("Bullets"), QStringLiteral("Bulleted list"), QKeySequence(),
                 [this] { editor_->toggleLinePrefix(QStringLiteral("- ")); }));
    keep(addIcon(ToolGlyph::Numbers, QStringLiteral("Numbers"), QStringLiteral("Numbered list"), QKeySequence(),
                 [this] { editor_->toggleNumberedList(); }));
    keep(addIcon(ToolGlyph::Tasks, QStringLiteral("Tasks"), QStringLiteral("Task list"), QKeySequence(),
                 [this] { editor_->toggleTaskList(); }));
    keep(addIcon(ToolGlyph::Quote, QStringLiteral("Quote"), QStringLiteral("Quote"), QKeySequence(QStringLiteral("Ctrl+Shift+.")),
                 [this] { editor_->toggleLinePrefix(QStringLiteral("> ")); }));
    addSeparator();

    keep(addIcon(ToolGlyph::Link, QStringLiteral("Link"), QStringLiteral("Link"), QKeySequence(QStringLiteral("Ctrl+K")),
                 [this] { editor_->insertLink(); }));
    keep(addIcon(ToolGlyph::Image, QStringLiteral("Image"),
                 QStringLiteral("Attach a picture. Publish copies it next to the post."),
                 QKeySequence(QStringLiteral("Ctrl+Shift+I")), [this] {
                     QString start = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
                     if (start.isEmpty())
                         start = QDir::homePath();
                     const QString path = QFileDialog::getOpenFileName(
                         this, QStringLiteral("Attach image"), start,
                         QStringLiteral("Images (*.png *.jpg *.jpeg *.gif *.webp *.svg *.avif);;All files (*)"));
                     if (!path.isEmpty())
                         editor_->insertImage(imageAlt(path), QUrl::fromLocalFile(path).toString());
                 }));
    keep(addIcon(ToolGlyph::Table, QStringLiteral("Table"), QStringLiteral("Table"), QKeySequence(),
                 [this] { editor_->insertTable(); }));
    keep(addIcon(ToolGlyph::Rule, QStringLiteral("Rule"), QStringLiteral("Horizontal rule"), QKeySequence(),
                 [this] { editor_->insertHorizontalRule(); }));

    codeMenu_ = new QMenu(this);
    for (const Lang &lang : kLangs) {
        auto *action = new QAction(QString::fromUtf8(lang.label), this);
        const QString tag = QString::fromUtf8(lang.tag);
        action->setStatusTip(tag.isEmpty() ? QStringLiteral("Plain code block")
                                           : QStringLiteral("Code block tagged %1").arg(tag));
        connect(action, &QAction::triggered, this, [this, tag] { editor_->insertCodeBlock(tag, true); });
        codeActions_.append(action);
        codeMenu_->addAction(action);
    }
    codeMenu_->addSeparator();
    otherLang_ = new QAction(QStringLiteral("Other language…"), this);
    connect(otherLang_, &QAction::triggered, this, [this] {
        bool ok = false;
        const QString lang = QInputDialog::getText(this, QStringLiteral("Code block"), QStringLiteral("Language"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (ok)
            editor_->insertCodeBlock(lang.trimmed(), true);
    });
    codeMenu_->addAction(otherLang_);
    auto *codeButton = new QToolButton(this);
    codeButton->setObjectName(QStringLiteral("menuTool"));
    codeButton->setText(QStringLiteral("Code block"));
    codeButton->setIcon(glyphIcon(ToolGlyph::CodeBlock));
    codeButton->setIconSize(iconSize());
    codeButton->setMenu(codeMenu_);
    codeButton->setPopupMode(QToolButton::InstantPopup);
    codeButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    codeButton->setToolTip(QStringLiteral("Fenced code with a language. Ctrl+Shift+` inserts a plain one."));
    codeButton->setStatusTip(QStringLiteral("Code block"));
    addWidget(codeButton);
    auto *codeShortcut = new QAction(this);
    codeShortcut->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+`")));
    connect(codeShortcut, &QAction::triggered, this, [this] { editor_->insertCodeBlock(); });
    editor_->addAction(codeShortcut);
    addSeparator();

    emoji_ = keep(addIcon(ToolGlyph::Emoji, QStringLiteral("Emoji"),
                          QStringLiteral("Insert an emoji. :smile: works in the text too."),
                          QKeySequence(QStringLiteral("Ctrl+Shift+E")), [] {}));
    flags_ = keep(addIcon(ToolGlyph::Flag, QStringLiteral("Flags"),
                          QStringLiteral("Insert a flag. :flag-at: or :Austria: works in the text too."),
                          QKeySequence(QStringLiteral("Ctrl+Shift+F")), [] {}));
    connect(emoji_, &QAction::triggered, this, [this] {
        auto *picker = new GlyphPicker(emojiGlyphs(), editor_, this);
        picker->setAttribute(Qt::WA_DeleteOnClose);
        QWidget *anchor = widgetForAction(emoji_);
        picker->popupAt(anchor ? anchor : this);
    });
    connect(flags_, &QAction::triggered, this, [this] {
        auto *picker = new GlyphPicker(flagGlyphs(), editor_, this);
        picker->setAttribute(Qt::WA_DeleteOnClose);
        QWidget *anchor = widgetForAction(flags_);
        picker->popupAt(anchor ? anchor : this);
    });
}

void FormatBar::fillFormatMenu(QMenu *menu)
{
    // simple_ is Bold, Italic, Strike, Code, then lists, then link tools, then Emoji and Flags.
    for (int i = 0; i < 4 && i < simple_.size(); ++i)
        menu->addAction(simple_.at(i));
    QMenu *headings = menu->addMenu(glyphIcon(ToolGlyph::Heading), QStringLiteral("Heading"));
    connect(headings, &QMenu::aboutToShow, this, [this, headings] { rebuildHeadings(headings); });
    menu->addSeparator();
    for (int i = 4; i < 8 && i < simple_.size(); ++i)
        menu->addAction(simple_.at(i));
    menu->addSeparator();
    for (int i = 8; i < 12 && i < simple_.size(); ++i)
        menu->addAction(simple_.at(i));
    QMenu *code = menu->addMenu(glyphIcon(ToolGlyph::CodeBlock), QStringLiteral("Code block"));
    for (QAction *action : codeActions_)
        code->addAction(action);
    code->addSeparator();
    code->addAction(otherLang_);
    menu->addSeparator();
    for (int i = 12; i < simple_.size(); ++i)
        menu->addAction(simple_.at(i));
}
