#pragma once

#include "ui/icons.h"

#include <QToolBar>
#include <QVector>

#include <functional>

class MarkdownEdit;
class QMenu;

// The row under the main toolbar. Every button is a symbol.
// Heading (H) and Code block open menus. The same actions fill the Format menu.
class FormatBar : public QToolBar {
    Q_OBJECT
public:
    explicit FormatBar(MarkdownEdit *editor, QWidget *parent = nullptr);
    void fillFormatMenu(QMenu *menu);

private:
    void rebuildHeadings(QMenu *menu);
    QAction *addIcon(ToolGlyph glyph, const QString &text, const QString &tip, const QKeySequence &key,
                     const std::function<void()> &fn);

    MarkdownEdit *editor_;
    QMenu *headingMenu_ = nullptr;
    QMenu *codeMenu_ = nullptr;
    QVector<QAction *> simple_;
    QVector<QAction *> headingActions_;
    QVector<QAction *> codeActions_;
    QAction *otherLang_ = nullptr;
    QAction *emoji_ = nullptr;
    QAction *flags_ = nullptr;
};
