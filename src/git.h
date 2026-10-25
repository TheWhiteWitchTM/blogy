#pragma once

#include <QList>
#include <QObject>
#include <QProcess>
#include <QStringList>

// One git invocation. args are the arguments after the program name, including -C <repo>.
struct GitStep {
    QStringList args;
    bool allowNothingToCommit = false;
};

// add, commit, push for one post. previousFile, when tracked, is staged as a deletion.
// also: extra paths (images) committed with the post, relative to the repo.
QList<GitStep> gitPublishSteps(const QString &repo, const QString &relativeFile, const QString &message,
                               const QString &remote, const QString &branch,
                               const QString &previousFile = QString(), const QStringList &also = {});

// "git -C <repo> add -- <file>" with shell quoting, so the Publish tab can show the real command.
QString gitCommandLine(const QStringList &args);

// Publishes one file: git add, git commit, git push <remote> HEAD:<branch>.
// Runs the git CLI so the user's own credentials (ssh agent, credential
// helper, gh auth) do the pushing. Blogy never sees a GitHub token.
class GitPublisher : public QObject {
    Q_OBJECT
public:
    explicit GitPublisher(QObject *parent = nullptr);

    void publish(const QString &repo, const QString &relativeFile, const QString &message,
                 const QString &remote, const QString &branch, const QString &previousFile = QString(),
                 const QStringList &also = {});
    void run(const QString &repo, const QList<GitStep> &steps);
    bool isRunning() const;
    static QString gitProgram();

signals:
    void output(const QString &text);
    void finished(bool ok, const QString &summary);

private:
    void next();
    void onFinished(int code, QProcess::ExitStatus status);

    QProcess process_;
    QString repo_;
    QList<GitStep> steps_;
    GitStep current_;
    QString captured_;
};
