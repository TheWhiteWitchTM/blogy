#include "ui/publishpanel.h"

#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTextOption>
#include <QVBoxLayout>

PublishPanel::PublishPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("publishPane"));
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    summary_ = new QPlainTextEdit(this);
    summary_->setObjectName(QStringLiteral("publishSummary"));
    summary_->setReadOnly(true);
    summary_->setFrameShape(QFrame::NoFrame);
    summary_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    summary_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    summary_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    summary_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    summary_->setMinimumWidth(0);
    summary_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    summary_->document()->setDocumentMargin(0);
    // Site, repo, branch, and path. Anything longer scrolls inside this block.
    const int summaryLine = summary_->fontMetrics().lineSpacing();
    summary_->setMinimumHeight(summaryLine * 2 + 4);
    summary_->setMaximumHeight(summaryLine * 5);
    layout->addWidget(summary_);

    auto *messageLabel = new QLabel(QStringLiteral("Commit message"), this);
    messageLabel->setObjectName(QStringLiteral("section"));
    layout->addWidget(messageLabel);
    message_ = new QLineEdit(this);
    message_->setObjectName(QStringLiteral("publishMessage"));
    message_->setPlaceholderText(QStringLiteral("Post: title"));
    message_->setMinimumWidth(0);
    message_->setTextMargins(4, 0, 4, 0);
    layout->addWidget(message_);

    button_ = new QPushButton(QStringLiteral("Publish"), this);
    button_->setObjectName(QStringLiteral("primary"));
    button_->setEnabled(false);
    button_->setMinimumWidth(0);
    button_->setToolTip(QStringLiteral("Save this post, commit it, and push it to the site repo."));
    layout->addWidget(button_);

    auto *commandLabel = new QLabel(QStringLiteral("Command"), this);
    commandLabel->setObjectName(QStringLiteral("section"));
    layout->addWidget(commandLabel);
    command_ = new QPlainTextEdit(this);
    command_->setObjectName(QStringLiteral("publishCommand"));
    command_->setReadOnly(true);
    command_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    command_->setPlaceholderText(QStringLiteral("The git command for this post shows up here."));
    command_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    command_->setMinimumWidth(0);
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    command_->setFont(mono);
    const int commandLine = command_->fontMetrics().lineSpacing();
    command_->setFixedHeight(commandLine * 3 + 12);
    command_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    layout->addWidget(command_);

    log_ = new QPlainTextEdit(this);
    log_->setObjectName(QStringLiteral("gitLog"));
    log_->setReadOnly(true);
    log_->setFont(mono);
    log_->setMinimumHeight(commandLine);
    log_->setMinimumWidth(0);
    log_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    log_->setPlaceholderText(QStringLiteral("git output shows up here when you publish."));
    layout->addWidget(log_, 1);

    connect(button_, &QPushButton::clicked, this, &PublishPanel::publishRequested);
    connect(message_, &QLineEdit::textEdited, this, &PublishPanel::messageEdited);
}

void PublishPanel::setSummary(const QString &text)
{
    summary_->setPlainText(text);
}

void PublishPanel::setCommand(const QString &text)
{
    command_->setPlainText(text);
}

void PublishPanel::setMessage(const QString &text)
{
    const QSignalBlocker block(message_);
    if (message_->text() != text) {
        message_->setText(text);
        // A long title is wider than the field. Keep the start on screen.
        if (!message_->hasFocus())
            message_->setCursorPosition(0);
    }
}

QString PublishPanel::message() const
{
    return message_->text();
}

void PublishPanel::setReady(bool ready)
{
    ready_ = ready;
    updateButton();
}

void PublishPanel::setBusy(bool busy)
{
    busy_ = busy;
    updateButton();
}

void PublishPanel::updateButton()
{
    button_->setEnabled(ready_ && !busy_);
}

void PublishPanel::clearOutput()
{
    log_->clear();
}

void PublishPanel::appendOutput(const QString &text)
{
    log_->moveCursor(QTextCursor::End);
    log_->insertPlainText(text);
    log_->moveCursor(QTextCursor::End);
}
