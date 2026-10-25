#include "editor.h"

#include <QFontDatabase>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QTextBlock>

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
}

void MarkdownEdit::setFontPointSize(int points)
{
    fontSize_ = qBound(8, points, 40);
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    for (const char *family : {"Fira Mono", "JetBrains Mono", "DejaVu Sans Mono"}) {
        const QFont test(QString::fromLatin1(family));
        if (QFontInfo(test).family() == QString::fromLatin1(family)) {
            f.setFamily(QString::fromLatin1(family));
            break;
        }
    }
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

int MarkdownEdit::wordCount() const
{
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    return toPlainText().split(ws, Qt::SkipEmptyParts).size();
}

void MarkdownEdit::keyPressEvent(QKeyEvent *event)
{
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
}
