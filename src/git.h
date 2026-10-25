#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

// Publishes one file: git add, git commit, git push <remote> HEAD:<branch>.
// Runs the git CLI so the user's own credentials (ssh agent, credential
// helper, gh auth) do the pushing. Blogy never sees a GitHub token.
class GitPublisher : public QObject {
    Q_OBJECT
public:
    explicit GitPublisher(QObject *parent = nullptr);

    void publish(const QString &repo, const QString &relativeFile, const QString &message,
                 const QString &remote, const QString &branch);
    bool isRunning() const;
    static QString gitProgram();

signals:
    void output(const QString &text);
    void finished(bool ok, const QString &summary);

private:
    struct Step {
        QStringList args;
        bool allowNothingToCommit = false;
    };
    void next();
    void onFinished(int code, QProcess::ExitStatus status);

    QProcess process_;
    QString repo_;
    QList<Step> steps_;
    Step current_;
    QString captured_;
};
