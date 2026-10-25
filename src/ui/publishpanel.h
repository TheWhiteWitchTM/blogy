#pragma once

#include <QWidget>

class QLineEdit;
class QPlainTextEdit;
class QPushButton;

// The Publish tab: the repo Blogy already knows, the git command it will run,
// and the output of that command.
class PublishPanel : public QWidget {
    Q_OBJECT
public:
    explicit PublishPanel(QWidget *parent = nullptr);

    void setSummary(const QString &text);
    void setCommand(const QString &text);
    void setMessage(const QString &text);
    QString message() const;
    void setReady(bool ready);
    void setBusy(bool busy);
    void clearOutput();
    void appendOutput(const QString &text);

signals:
    void publishRequested();
    void messageEdited();

private:
    void updateButton();

    QPlainTextEdit *summary_ = nullptr;
    QLineEdit *message_ = nullptr;
    QPlainTextEdit *command_ = nullptr;
    QPlainTextEdit *log_ = nullptr;
    QPushButton *button_ = nullptr;
    bool ready_ = false;
    bool busy_ = false;
};
