#include "editor.h"

#include "emoji.h"
#include "words.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
#include <QTextBlock>
#include <QVBoxLayout>
#include <QUrl>

MarkdownEdit::MarkdownEdit(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setObjectName(QStringLiteral("codeEdit"));
    highlighter_ = new Highlighter(document());
    setFontPointSize(13);
    setWrap(true);
    setTabChangesFocus(false);
    setCenterOnScroll(false);
    setPlaceholderText(QStringLiteral("Write."));
    document()->setDocumentMargin(16);

    ghost_ = new QLabel(viewport());
    ghost_->setAttribute(Qt::WA_TransparentForMouseEvents);
    ghost_->hide();

    suggest_ = new QFrame(viewport());
    suggest_->setObjectName(QStringLiteral("suggestPopup"));
    suggest_->hide();
    auto *layout = new QVBoxLayout(suggest_);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);
    auto *hint = new QLabel(QStringLiteral("Click a row, or press Tab"), suggest_);
    hint->setObjectName(QStringLiteral("suggestHint"));
    layout->addWidget(hint);
    suggestList_ = new QListWidget(suggest_);
    suggestList_->setObjectName(QStringLiteral("suggestList"));
    suggestList_->setFocusPolicy(Qt::NoFocus);
    suggestList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    suggestList_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(suggestList_);
    connect(suggestList_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (item)
            acceptSuggestion(item->data(Qt::UserRole).toString());
    });

    suggestTimer_.setSingleShot(true);
    spellTimer_.setSingleShot(true);
    connect(&suggestTimer_, &QTimer::timeout, this, &MarkdownEdit::refreshSuggest);
    connect(&spellTimer_, &QTimer::timeout, this, &MarkdownEdit::refreshSpell);
    connect(this, &QPlainTextEdit::textChanged, this, [this] { scheduleAssist(120, 500); });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        scheduleAssist(80, 80);
        placeAssist();
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { placeAssist(); });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { placeAssist(); });
}

void MarkdownEdit::setLearnedWords(const QStringList &words)
{
    Lexicon::english().setLearned(words);
    refreshSpell();
}

void MarkdownEdit::setFontPointSize(int points)
{
    fontSize_ = qBound(8, points, 40);
    QStringList families;
    for (const char *family : {"JetBrains Mono", "Fira Mono", "DejaVu Sans Mono", "Noto Sans Mono"}) {
        const QString name = QString::fromLatin1(family);
        if (QFontDatabase::hasFamily(name)) {
            families << name;
            break;
        }
    }
    if (families.isEmpty())
        families << QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    for (const char *family : {"Noto Color Emoji", "Noto Emoji", "Segoe UI Emoji"}) {
        const QString name = QString::fromLatin1(family);
        if (QFontDatabase::hasFamily(name))
            families << name;
    }
    QFont f;
    f.setFamilies(families);
    f.setStyleHint(QFont::Monospace);
    f.setPointSize(fontSize_);
    setFont(f);
    setTabStopDistance(QFontMetricsF(f).horizontalAdvance(QLatin1Char(' ')) * 4);
}

void MarkdownEdit::setWrap(bool on)
{
    setLineWrapMode(on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
}

void MarkdownEdit::wrapSelection(const QString &left, const QString &right)
{
    QTextCursor c = textCursor();
    const QString sel = c.selectedText();
    c.beginEditBlock();
    c.insertText(left + sel + right);
    if (sel.isEmpty())
        c.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, right.size());
    c.endEditBlock();
    setTextCursor(c);
}

void MarkdownEdit::toggleWrap(const QString &left, const QString &right)
{
    QTextCursor c = textCursor();
    QString sel = c.selectedText().replace(QChar(0x2029), QLatin1Char('\n'));
    if (sel.startsWith(left) && sel.endsWith(right) && sel.size() >= left.size() + right.size()) {
        sel = sel.mid(left.size(), sel.size() - left.size() - right.size());
        c.insertText(sel);
        setTextCursor(c);
        return;
    }
    wrapSelection(left, right);
}

void MarkdownEdit::mapLines(const std::function<QString(const QString &)> &fn)
{
    QTextCursor c = textCursor();
    const int start = c.selectionStart();
    const int end = c.selectionEnd();
    c.setPosition(start);
    QTextBlock first = c.block();
    c.setPosition(qMax(start, end > start ? end - 1 : start));
    QTextBlock last = c.block();
    c.beginEditBlock();
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        const QString next = fn(block.text());
        if (next != block.text()) {
            QTextCursor line(block);
            line.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            line.insertText(next);
        }
        if (block == last)
            break;
    }
    c.endEditBlock();
}

void MarkdownEdit::setHeading(int level)
{
    level = qBound(1, level, 6);
    const QString prefix = QString(level, QLatin1Char('#')) + QLatin1Char(' ');
    static const QRegularExpression marks(QStringLiteral("^#{1,6}\\s+"));
    mapLines([&](const QString &line) {
        QString rest = line;
        rest.remove(marks);
        if (line.startsWith(prefix))
            return rest;
        return prefix + rest;
    });
}

void MarkdownEdit::toggleLinePrefix(const QString &prefix)
{
    bool all = true;
    bool any = false;
    QTextCursor c = textCursor();
    const int start = c.selectionStart();
    const int end = c.selectionEnd();
    c.setPosition(start);
    QTextBlock first = c.block();
    c.setPosition(qMax(start, end > start ? end - 1 : start));
    QTextBlock last = c.block();
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        if (!block.text().trimmed().isEmpty()) {
            any = true;
            if (!block.text().startsWith(prefix))
                all = false;
        }
        if (block == last)
            break;
    }
    const bool remove = any && all;
    mapLines([&](const QString &line) {
        if (line.trimmed().isEmpty())
            return line;
        if (remove && line.startsWith(prefix))
            return line.mid(prefix.size());
        if (!remove && !line.startsWith(prefix))
            return prefix + line;
        return line;
    });
}

void MarkdownEdit::toggleNumberedList()
{
    static const QRegularExpression num(QStringLiteral(R"(^\d+\.\s+)"));
    bool all = true;
    bool any = false;
    QTextCursor c = textCursor();
    const int start = c.selectionStart();
    const int end = c.selectionEnd();
    c.setPosition(start);
    QTextBlock first = c.block();
    c.setPosition(qMax(start, end > start ? end - 1 : start));
    QTextBlock last = c.block();
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        if (!block.text().trimmed().isEmpty()) {
            any = true;
            if (!num.match(block.text()).hasMatch())
                all = false;
        }
        if (block == last)
            break;
    }
    const bool remove = any && all;
    int n = 1;
    mapLines([&](const QString &line) {
        if (line.trimmed().isEmpty())
            return line;
        QString rest = line;
        rest.remove(num);
        if (remove)
            return rest;
        return QString::number(n++) + QStringLiteral(". ") + rest;
    });
}

void MarkdownEdit::toggleTaskList()
{
    static const QRegularExpression task(QStringLiteral(R"(^[-*+]\s+\[[ xX]\]\s+)"));
    bool all = true;
    bool any = false;
    QTextCursor c = textCursor();
    const int start = c.selectionStart();
    const int end = c.selectionEnd();
    c.setPosition(start);
    QTextBlock first = c.block();
    c.setPosition(qMax(start, end > start ? end - 1 : start));
    QTextBlock last = c.block();
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        if (!block.text().trimmed().isEmpty()) {
            any = true;
            if (!task.match(block.text()).hasMatch())
                all = false;
        }
        if (block == last)
            break;
    }
    const bool remove = any && all;
    mapLines([&](const QString &line) {
        if (line.trimmed().isEmpty())
            return line;
        QString rest = line;
        rest.remove(task);
        if (remove)
            return rest;
        if (rest.startsWith(QLatin1String("- ")) || rest.startsWith(QLatin1String("* ")))
            return QStringLiteral("- [ ] ") + rest.mid(2);
        return QStringLiteral("- [ ] ") + rest;
    });
}

void MarkdownEdit::insertCodeBlock(const QString &language, bool fromMenu)
{
    QString lang = language.trimmed();
    lang.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9_+#.-]")));
    int open = -1;
    if (fromMenu && cursorInFence(&open) && open >= 0) {
        QTextBlock block = document()->findBlock(open);
        static const QRegularExpression marks(QStringLiteral("^(```+|~~~+)\\s*\\S*"));
        const auto match = marks.match(block.text());
        const QString ticks = match.hasMatch() ? match.captured(1) : QStringLiteral("```");
        QTextCursor line(block);
        line.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        line.insertText(ticks + (lang.isEmpty() ? QString() : lang));
        return;
    }
    if (cursorInFence())
        return;
    QTextCursor c = textCursor();
    const QString sel = c.selectedText().replace(QChar(0x2029), QLatin1Char('\n'));
    const QString before = textBeforeCursor();
    QString chunk;
    if (!before.isEmpty() && !before.endsWith(QLatin1Char('\n')))
        chunk += QStringLiteral("\n\n");
    else if (before.endsWith(QLatin1Char('\n')) && !before.endsWith(QLatin1String("\n\n")))
        chunk += QLatin1Char('\n');
    chunk += QStringLiteral("```") + lang + QLatin1Char('\n');
    chunk += sel;
    if (!sel.endsWith(QLatin1Char('\n')))
        chunk += QLatin1Char('\n');
    chunk += QStringLiteral("```\n");
    c.beginEditBlock();
    c.insertText(chunk);
    if (sel.isEmpty()) {
        c.movePosition(QTextCursor::Up);
        c.movePosition(QTextCursor::Up);
        c.movePosition(QTextCursor::EndOfBlock);
    }
    c.endEditBlock();
    setTextCursor(c);
}

void MarkdownEdit::insertTable()
{
    insertSnippet(QStringLiteral("\n| Column | Column |\n| --- | --- |\n|  |  |\n"));
}

void MarkdownEdit::insertHorizontalRule()
{
    insertSnippet(QStringLiteral("\n---\n"));
}

void MarkdownEdit::insertSnippet(const QString &text)
{
    QTextCursor c = textCursor();
    c.insertText(text);
    setTextCursor(c);
}

void MarkdownEdit::insertImage(const QString &alt, const QString &destination)
{
    QString safe = alt;
    safe.replace(QLatin1Char('['), QString());
    safe.replace(QLatin1Char(']'), QString());
    safe.replace(QLatin1Char('\n'), QLatin1Char(' '));
    insertSnippet(QStringLiteral("![%1](%2)\n").arg(safe, destination));
}

void MarkdownEdit::insertLink()
{
    QTextCursor c = textCursor();
    const QString sel = c.selectedText();
    const bool isUrl = sel.startsWith(QLatin1String("http"));
    c.beginEditBlock();
    if (isUrl)
        c.insertText(QStringLiteral("[](") + sel + QLatin1Char(')'));
    else
        c.insertText(QLatin1Char('[') + sel + QStringLiteral("](https://)"));
    c.endEditBlock();
    if (isUrl)
        c.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, sel.size() + 3);
    else
        c.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 1);
    setTextCursor(c);
}

QString MarkdownEdit::selectedOrAll() const
{
    const QString sel = textCursor().selectedText();
    if (!sel.isEmpty())
        return QString(sel).replace(QChar(0x2029), QLatin1Char('\n'));
    return toPlainText();
}

void MarkdownEdit::replaceSelectionOrAll(const QString &text)
{
    QTextCursor c = textCursor();
    c.beginEditBlock();
    if (!c.hasSelection())
        c.select(QTextCursor::Document);
    c.insertText(text);
    c.endEditBlock();
    setTextCursor(c);
}

void MarkdownEdit::replaceAll(const QString &text, bool joinUndo)
{
    QTextCursor c = textCursor();
    if (joinUndo)
        c.joinPreviousEditBlock();
    else
        c.beginEditBlock();
    c.select(QTextCursor::Document);
    c.insertText(text);
    c.endEditBlock();
    c.movePosition(QTextCursor::End);
    setTextCursor(c);
}

int MarkdownEdit::wordCount() const
{
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    return toPlainText().split(ws, Qt::SkipEmptyParts).size();
}

void MarkdownEdit::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Tab && suggest_->isVisible() && event->modifiers() == Qt::NoModifier) {
        if (suggestList_->count() > 0)
            acceptSuggestion(suggestList_->item(0)->data(Qt::UserRole).toString());
        return;
    }
    if (event->key() == Qt::Key_Escape && suggest_->isVisible()) {
        hideSuggest();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !(event->modifiers() & Qt::ShiftModifier)) {
        // Continue lists and quotes; an empty item ends the list.
        static const QRegularExpression item(QStringLiteral(R"(^(\s*)([-*+]|(\d+)\.|>)(\s+)(\[[ xX]\]\s+)?)"));
        QTextCursor c = textCursor();
        const QString line = c.block().text();
        const auto m = item.match(line);
        if (m.hasMatch() && c.positionInBlock() == line.size()) {
            if (line.trimmed() == m.captured(0).trimmed()) {
                c.select(QTextCursor::LineUnderCursor);
                c.removeSelectedText();
                QPlainTextEdit::keyPressEvent(event);
                return;
            }
            QString marker = m.captured(2);
            if (!m.captured(3).isEmpty())
                marker = QString::number(m.captured(3).toInt() + 1) + QLatin1Char('.');
            QString next = m.captured(1) + marker + m.captured(4);
            if (!m.captured(5).isEmpty())
                next += QStringLiteral("[ ] ");
            c.insertText(QStringLiteral("\n") + next);
            setTextCursor(c);
            return;
        }
    }
    QPlainTextEdit::keyPressEvent(event);
    if (event->key() == Qt::Key_Colon || event->key() == Qt::Key_Space)
        tryExpandShortcode();
}

void MarkdownEdit::tryExpandShortcode()
{
    QTextCursor c = textCursor();
    const QString before = c.block().text().left(c.positionInBlock());
    static const QRegularExpression rx(QStringLiteral(R"(:([a-z0-9_+-]{2,40}):$)"));
    const auto m = rx.match(before);
    if (!m.hasMatch())
        return;
    const int ticks = before.left(m.capturedStart()).count(QLatin1Char('`'));
    if (ticks % 2 == 1)
        return;
    const QString emoji = emojiForShortcode(m.captured(1));
    if (emoji.isEmpty())
        return;
    const int blockPos = c.block().position();
    c.setPosition(blockPos + m.capturedStart());
    c.setPosition(blockPos + m.capturedEnd(), QTextCursor::KeepAnchor);
    c.insertText(emoji);
    setTextCursor(c);
}

void MarkdownEdit::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        for (const QUrl &url : event->mimeData()->urls()) {
            const QString ext = QFileInfo(url.toLocalFile()).suffix().toLower();
            if (QStringList{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("gif"),
                            QStringLiteral("webp"), QStringLiteral("svg"), QStringLiteral("avif")}
                    .contains(ext)) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    QPlainTextEdit::dragEnterEvent(event);
}

void MarkdownEdit::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        QPlainTextEdit::dragMoveEvent(event);
}

void MarkdownEdit::dropEvent(QDropEvent *event)
{
    QStringList images;
    if (event->mimeData()->hasUrls()) {
        for (const QUrl &url : event->mimeData()->urls()) {
            if (!url.isLocalFile())
                continue;
            const QString path = url.toLocalFile();
            const QString ext = QFileInfo(path).suffix().toLower();
            if (QStringList{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("gif"),
                            QStringLiteral("webp"), QStringLiteral("svg"), QStringLiteral("avif")}
                    .contains(ext))
                images << path;
        }
    }
    if (images.isEmpty()) {
        QPlainTextEdit::dropEvent(event);
        return;
    }
    event->acceptProposedAction();
    for (const QString &path : images)
        insertImage(QFileInfo(path).completeBaseName(), QUrl::fromLocalFile(path).toString());
}

bool MarkdownEdit::spellablePosition(int pos) const
{
    const QString text = toPlainText();
    if (pos < 0 || pos > text.size())
        return false;
    bool front = false;
    bool fence = false;
    bool started = false;
    int line = 0;
    const int n = text.size();
    while (line <= n) {
        int end = text.indexOf(QLatin1Char('\n'), line);
        if (end < 0)
            end = n;
        const bool here = pos >= line && pos <= end;
        const QString row = text.mid(line, end - line);
        const QString trimmed = row.trimmed();
        const bool fenceLine = trimmed.startsWith(QLatin1String("```")) || trimmed.startsWith(QLatin1String("~~~"));
        const bool wasFront = front;
        if (!started && (trimmed == QLatin1String("---") || trimmed.startsWith(QLatin1String("export const metadata"))))
            front = true;
        started = true;
        if (here) {
            if (wasFront || front || fence || fenceLine)
                return false;
            int ticks = 0;
            for (int k = line; k < pos && k < n; ++k) {
                if (text.at(k) == QLatin1Char('`'))
                    ++ticks;
            }
            return ticks % 2 == 0;
        }
        if (wasFront) {
            if (trimmed == QLatin1String("---") || trimmed.startsWith(QLatin1String("};")) || trimmed == QLatin1String("}"))
                front = false;
        } else if (!front && fenceLine) {
            fence = !fence;
        }
        if (end >= n)
            break;
        line = end + 1;
    }
    return true;
}

bool MarkdownEdit::cursorInFence(int *openPosition) const
{
    if (openPosition)
        *openPosition = -1;
    bool front = false;
    bool fence = false;
    bool started = false;
    int open = -1;
    const int caret = textCursor().position();
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        const QString trimmed = block.text().trimmed();
        const bool fenceLine = trimmed.startsWith(QLatin1String("```")) || trimmed.startsWith(QLatin1String("~~~"));
        const bool wasFront = front;
        if (!started && (trimmed == QLatin1String("---") || trimmed.startsWith(QLatin1String("export const metadata"))))
            front = true;
        started = true;
        if (!wasFront && !front && fenceLine) {
            if (fence) {
                fence = false;
                open = -1;
            } else {
                fence = true;
                open = block.position();
            }
        } else if (wasFront && (trimmed == QLatin1String("---") || trimmed.startsWith(QLatin1String("};")) || trimmed == QLatin1String("}"))) {
            front = false;
        }
        if (block == textCursor().block() || block.position() + block.length() > caret)
            break;
    }
    if (openPosition)
        *openPosition = open;
    return fence;
}

void MarkdownEdit::replaceRange(int start, int length, const QString &text)
{
    QTextCursor c = textCursor();
    c.beginEditBlock();
    c.setPosition(start);
    c.setPosition(start + length, QTextCursor::KeepAnchor);
    c.insertText(text);
    c.endEditBlock();
    setTextCursor(c);
}

QString MarkdownEdit::textBeforeCursor() const
{
    QTextCursor c = textCursor();
    c.setPosition(0, QTextCursor::KeepAnchor);
    return c.selectedText().replace(QChar(0x2029), QLatin1Char('\n'));
}

QVector<HeadingMark> MarkdownEdit::headings() const
{
    QVector<HeadingMark> marks;
    bool front = false;
    bool fence = false;
    bool started = false;
    static const QRegularExpression hashes(QStringLiteral("^(#{1,6})\\s+(\\S.*)"));
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        const QString trimmed = block.text().trimmed();
        const bool fenceLine = trimmed.startsWith(QLatin1String("```")) || trimmed.startsWith(QLatin1String("~~~"));
        const bool wasFront = front;
        if (!started && (trimmed == QLatin1String("---") || trimmed.startsWith(QLatin1String("export const metadata"))))
            front = true;
        started = true;
        if (!wasFront && !front && !fence && !fenceLine) {
            const auto match = hashes.match(block.text());
            if (match.hasMatch())
                marks.append({int(match.captured(1).size()), int(block.position()), match.captured(2).trimmed()});
        }
        if (wasFront) {
            if (trimmed == QLatin1String("---") || trimmed.startsWith(QLatin1String("};")) || trimmed == QLatin1String("}"))
                front = false;
        } else if (!front && fenceLine) {
            fence = !fence;
        }
    }
    return marks;
}

void MarkdownEdit::goToPosition(int position)
{
    QTextCursor c = textCursor();
    c.setPosition(qBound(0, position, qMax(0, document()->characterCount() - 1)));
    setTextCursor(c);
    setFocus();
    ensureCursorVisible();
}

bool MarkdownEdit::findText(const QString &needle, bool forward, bool matchCase)
{
    if (needle.isEmpty())
        return false;
    QTextDocument::FindFlags flags;
    if (!forward)
        flags |= QTextDocument::FindBackward;
    if (matchCase)
        flags |= QTextDocument::FindCaseSensitively;
    QTextCursor found = document()->find(needle, textCursor(), flags);
    if (found.isNull()) {
        QTextCursor wrap(document());
        if (!forward)
            wrap.movePosition(QTextCursor::End);
        found = document()->find(needle, wrap, flags);
    }
    if (found.isNull())
        return false;
    setTextCursor(found);
    setFocus();
    return true;
}

int MarkdownEdit::replaceAllText(const QString &needle, const QString &replacement, bool matchCase)
{
    if (needle.isEmpty())
        return 0;
    QTextDocument::FindFlags flags;
    if (matchCase)
        flags |= QTextDocument::FindCaseSensitively;
    QTextCursor c(document());
    c.beginEditBlock();
    int count = 0;
    while (count < 100000) {
        QTextCursor found = document()->find(needle, c, flags);
        if (found.isNull())
            break;
        c.setPosition(found.selectionStart());
        c.setPosition(found.selectionEnd(), QTextCursor::KeepAnchor);
        c.insertText(replacement);
        ++count;
    }
    c.endEditBlock();
    setTextCursor(c);
    return count;
}

QStringList MarkdownEdit::completionExtras() const
{
    QStringList extra;
    QSet<QString> seen;
    static const QRegularExpression word(QStringLiteral("[A-Za-z][A-Za-z'’-]{1,40}"));
    const QString text = toPlainText();
    auto it = word.globalMatch(text);
    while (it.hasNext() && extra.size() < 200) {
        const QString token = it.next().captured(0);
        bool upper = false;
        for (const QChar c : token) {
            if (c.isLetter() && c == c.toUpper())
                upper = true;
        }
        if (!upper)
            continue;
        const QString key = token.toLower();
        if (seen.contains(key))
            continue;
        seen.insert(key);
        extra.append(token);
    }
    return extra;
}

QStringList MarkdownEdit::dictationHints() const
{
    QStringList hints;
    QSet<QString> seen;
    const auto add = [&](const QString &word) {
        const QString clean = word.trimmed();
        if (clean.size() < 2 || clean.size() > 50 || seen.contains(clean.toLower()))
            return;
        seen.insert(clean.toLower());
        hints.append(clean);
    };
    for (const HeadingMark &mark : headings())
        add(mark.text);
    for (const QString &name : completionExtras())
        add(name);
    return hints.mid(0, 40);
}

void MarkdownEdit::scheduleAssist(int suggestMs, int spellMs)
{
    suggestTimer_.start(suggestMs);
    spellTimer_.start(spellMs);
}

void MarkdownEdit::hideSuggest()
{
    suggest_->hide();
    if (!dictating_)
        ghost_->hide();
}

void MarkdownEdit::showGhost(const QString &text, bool dictation)
{
    if (text.isEmpty()) {
        ghost_->hide();
        return;
    }
    ghost_->setFont(font());
    ghost_->setStyleSheet(dictation ? QStringLiteral("color: #ffd18a; background: transparent;")
                                    : QStringLiteral("color: #8a8a8a; background: transparent;"));
    ghost_->setText(text);
    ghost_->adjustSize();
    const QRect caret = cursorRect();
    ghost_->move(caret.topRight());
    ghost_->show();
    ghost_->raise();
}

void MarkdownEdit::placeAssist()
{
    if (ghost_->isVisible()) {
        const QRect caret = cursorRect();
        ghost_->move(caret.topRight());
    }
    if (!suggest_->isVisible())
        return;
    suggest_->adjustSize();
    const int width = qMin(280, qMax(160, viewport()->width() - 12));
    const int height = suggest_->sizeHint().height();
    const QRect caret = cursorRect();
    int x = qBound(4, caret.left(), qMax(4, viewport()->width() - width - 4));
    int y = caret.bottom() + 4;
    if (y + height > viewport()->height() - 4)
        y = qMax(4, caret.top() - height - 4);
    suggest_->setFixedWidth(width);
    suggest_->move(x, y);
    suggest_->raise();
}

void MarkdownEdit::acceptSuggestion(const QString &insert)
{
    if (suggestStart_ < 0 || insert.isEmpty())
        return;
    const QString text = toPlainText();
    const int end = suggestStart_ + suggestLength_;
    if (end > text.size() || text.mid(suggestStart_, suggestLength_).compare(insert.left(suggestLength_), Qt::CaseInsensitive) != 0) {
        hideSuggest();
        return;
    }
    const QChar after = end < text.size() ? text.at(end) : QChar();
    const bool gap = after.isNull() || (after != QLatin1Char(' ') && after != QLatin1Char('\n'));
    hideSuggest();
    replaceRange(suggestStart_, suggestLength_, insert + (gap ? QStringLiteral(" ") : QString()));
}

void MarkdownEdit::refreshSuggest()
{
    if (dictating_ || textCursor().hasSelection()) {
        hideSuggest();
        return;
    }
    const QString text = toPlainText();
    const int caret = textCursor().position();
    if (caret < text.size()) {
        const QChar next = text.at(caret);
        if (next.isLetter() || next == QLatin1Char('\'') || next == QLatin1Char('-') || next == QChar(0x2019)) {
            hideSuggest();
            return;
        }
    }
    int start = caret;
    while (start > 0) {
        const QChar c = text.at(start - 1);
        if (!(c.isLetter() || c == QLatin1Char('\'') || c == QLatin1Char('-') || c == QChar(0x2019)))
            break;
        --start;
    }
    const QString prefix = text.mid(start, caret - start);
    if (prefix.size() < 2 || !prefix.at(0).isLetter() || !spellablePosition(start)) {
        hideSuggest();
        return;
    }
    if (start > 0) {
        const QChar prev = text.at(start - 1);
        if (prev == QLatin1Char('.') || prev == QLatin1Char('/') || prev == QLatin1Char('@') || prev == QLatin1Char('#')
            || prev == QLatin1Char('\\') || prev == QLatin1Char('`')) {
            hideSuggest();
            return;
        }
    }
    const QStringList rows = Lexicon::english().complete(prefix, completionExtras());
    if (rows.isEmpty()) {
        hideSuggest();
        return;
    }
    suggestStart_ = start;
    suggestLength_ = prefix.size();
    suggestList_->clear();
    for (const QString &row : rows) {
        auto *item = new QListWidgetItem(row, suggestList_);
        item->setData(Qt::UserRole, row);
    }
    suggestList_->setCurrentRow(0);
    suggestList_->setFixedHeight(qMin(rows.size(), 6) * 28 + 4);
    const QString first = rows.first();
    const QString rest = first.size() > prefix.size() && first.startsWith(prefix, Qt::CaseInsensitive) ? first.mid(prefix.size()) : QString();
    suggest_->show();
    showGhost(rest, false);
    placeAssist();
}

void MarkdownEdit::refreshSpell()
{
    const QVector<WordHit> hits = Lexicon::english().typos(toPlainText(), textCursor().position());
    QList<QTextEdit::ExtraSelection> extras;
    QTextCharFormat format;
    format.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
    format.setUnderlineColor(QColor(QStringLiteral("#e06c75")));
    for (const WordHit &hit : hits) {
        QTextEdit::ExtraSelection selection;
        selection.format = format;
        selection.cursor = QTextCursor(document());
        selection.cursor.setPosition(hit.start);
        selection.cursor.setPosition(hit.start + hit.length, QTextCursor::KeepAnchor);
        extras.append(selection);
    }
    setExtraSelections(extras);
}

void MarkdownEdit::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu *menu = createStandardContextMenu();
    const QString text = toPlainText();
    QTextCursor at = cursorForPosition(event->pos());
    int pos = at.position();
    int start = pos;
    int end = pos;
    while (start > 0 && (text.at(start - 1).isLetter() || text.at(start - 1) == QLatin1Char('\'')))
        --start;
    while (end < text.size() && (text.at(end).isLetter() || text.at(end) == QLatin1Char('\'')))
        ++end;
    const QString token = text.mid(start, end - start);
    if (token.size() >= 3 && token == token.toLower() && spellablePosition(start) && !Lexicon::english().known(token)) {
        QAction *before = menu->actions().value(0);
        const QStringList fixes = Lexicon::english().corrections(token);
        if (fixes.isEmpty()) {
            auto *none = new QAction(QStringLiteral("No other spelling"), menu);
            none->setEnabled(false);
            menu->insertAction(before, none);
        } else {
            for (int i = fixes.size() - 1; i >= 0; --i) {
                auto *fix = new QAction(fixes.at(i), menu);
                const QString replacement = fixes.at(i);
                connect(fix, &QAction::triggered, this, [this, start, end, replacement] { replaceRange(start, end - start, replacement); });
                menu->insertAction(before, fix);
            }
        }
        auto *add = new QAction(QStringLiteral("Add to dictionary"), menu);
        connect(add, &QAction::triggered, this, [this, token] {
            Lexicon::english().learn(token);
            emit learnedWordsChanged(Lexicon::english().learned());
            refreshSpell();
        });
        menu->insertAction(before, add);
        menu->insertSeparator(before);
    }
    menu->exec(event->globalPos());
    delete menu;
}

void MarkdownEdit::beginDictation()
{
    dictating_ = true;
    hideSuggest();
    fenceAtStart_ = cursorInFence();
    dictate_ = {};
    dictate_.fence = fenceAtStart_;
    const QString before = textBeforeCursor();
    dictate_.cap = before.isEmpty() || before.endsWith(QLatin1Char('\n')) || before.endsWith(QLatin1Char('.'))
                   || before.endsWith(QLatin1Char('?')) || before.endsWith(QLatin1Char('!'));
    lastDictated_ = 0;
}

void MarkdownEdit::endDictation()
{
    if (!dictating_)
        return;
    dictating_ = false;
    ghost_->hide();
    QString close;
    if (dictate_.wrap)
        close += (dictate_.wrap == 1 ? QStringLiteral("**")
                                      : dictate_.wrap == 2 ? QStringLiteral("*")
                                                           : dictate_.wrap == 3 ? QStringLiteral("~~")
                                                                                : QStringLiteral("`"));
    if (dictate_.fence && !fenceAtStart_)
        close += QStringLiteral("\n```\n");
    dictate_.wrap = 0;
    dictate_.fence = false;
    if (!close.isEmpty()) {
        QTextCursor c = textCursor();
        c.insertText(close);
        setTextCursor(c);
        lastDictated_ += close.size();
    }
}

void MarkdownEdit::commitDictation(const QString &spoken)
{
    if (!dictating_)
        return;
    ghost_->hide();
    const DictatePiece piece = dictateMarkdown(spoken, textBeforeCursor(), &dictate_);
    if (piece.finish) {
        emit finishFromOutline();
        return;
    }
    if (piece.scratch) {
        scratchDictation();
        return;
    }
    if (piece.text.isEmpty())
        return;
    QTextCursor c = textCursor();
    c.beginEditBlock();
    if (c.hasSelection())
        c.removeSelectedText();
    c.insertText(piece.text);
    c.endEditBlock();
    lastDictated_ = piece.text.size();
    setTextCursor(c);
}

void MarkdownEdit::previewDictation(const QString &spoken)
{
    if (!dictating_)
        return;
    DictateCursor copy = dictate_;
    const DictatePiece piece = dictateMarkdown(spoken, textBeforeCursor(), &copy);
    const QString ghost = piece.finish ? QStringLiteral("Finishing the post…")
                                        : piece.scratch ? QStringLiteral("scratch that")
                                                        : piece.text;
    showGhost(ghost, true);
}

void MarkdownEdit::clearDictationPreview()
{
    if (dictating_)
        ghost_->hide();
}

void MarkdownEdit::scratchDictation()
{
    if (lastDictated_ <= 0)
        return;
    QTextCursor c = textCursor();
    const int end = c.position();
    const int start = end - lastDictated_;
    if (start < 0)
        return;
    c.setPosition(start);
    c.setPosition(end, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    setTextCursor(c);
    lastDictated_ = 0;
}
