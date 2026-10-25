#include "git.h"

#include <QRegularExpression>
#include <QStandardPaths>

namespace {

QString shellQuote(const QString &arg)
{
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9_./:@+=,-]+$"));
    if (!arg.isEmpty() && safe.match(arg).hasMatch())
        return arg;
    QString escaped = arg;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

QStringList withRepo(const QString &repo, QStringList args)
{
    if (!repo.isEmpty())
        args = QStringList{QStringLiteral("-C"), repo} + args;
    return args;
}

} // namespace

QString gitCommandLine(const QStringList &args)
{
    QStringList parts{QStringLiteral("git")};
    for (const QString &arg : args)
        parts << shellQuote(arg);
    return parts.join(QLatin1Char(' '));
}

QList<GitStep> gitPublishSteps(const QString &repo, const QString &relativeFile, const QString &message,
                               const QString &remote, const QString &branch, const QString &previousFile,
                               const QStringList &also)
{
    QStringList paths;
    auto addPath = [&paths](const QString &path) {
        if (!path.isEmpty() && !paths.contains(path))
            paths << path;
    };
    addPath(relativeFile);
    for (const QString &extra : also)
        addPath(extra);
    if (paths.isEmpty() || repo.isEmpty())
        return {};
    bool previousTracked = false;
    if (!previousFile.isEmpty() && previousFile != relativeFile) {
        QProcess listed;
        listed.setWorkingDirectory(repo);
        listed.start(GitPublisher::gitProgram(), {QStringLiteral("ls-files"), QStringLiteral("--error-unmatch"),
                                                   QStringLiteral("--"), previousFile});
        listed.waitForFinished(3000);
        previousTracked = listed.exitCode() == 0;
    }
    QStringList commitPaths = paths;
    if (previousTracked)
        commitPaths << previousFile;
    QStringList addArgs = {QStringLiteral("add")};
    if (previousTracked)
        addArgs << QStringLiteral("-A");
    addArgs << QStringLiteral("--") << commitPaths;
    QStringList commitArgs = {QStringLiteral("commit"), QStringLiteral("-m"), message, QStringLiteral("--")};
    commitArgs << commitPaths;
    const QStringList pushArgs = {QStringLiteral("push"), remote, QStringLiteral("HEAD:") + branch};
    QList<GitStep> steps;
    steps.append(GitStep{withRepo(repo, addArgs), false});
    steps.append(GitStep{withRepo(repo, commitArgs), true});
    steps.append(GitStep{withRepo(repo, pushArgs), false});
    return steps;
}

GitPublisher::GitPublisher(QObject *parent)
    : QObject(parent)
{
    process_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process_, &QProcess::readyRead, this, [this] {
        const QString text = QString::fromUtf8(process_.readAll());
        captured_ += text;
        emit output(text);
    });
    connect(&process_, &QProcess::finished, this, &GitPublisher::onFinished);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) {
            steps_.clear();
            emit finished(false, QStringLiteral("git could not be started. Is it installed?"));
        }
    });
}

QString GitPublisher::gitProgram()
{
    const QString found = QStandardPaths::findExecutable(QStringLiteral("git"));
    return found.isEmpty() ? QStringLiteral("git") : found;
}

bool GitPublisher::isRunning() const
{
    return process_.state() != QProcess::NotRunning || !steps_.isEmpty();
}

void GitPublisher::publish(const QString &repo, const QString &relativeFile, const QString &message,
                           const QString &remote, const QString &branch, const QString &previousFile,
                           const QStringList &also)
{
    run(repo, gitPublishSteps(repo, relativeFile, message, remote, branch, previousFile, also));
}

void GitPublisher::run(const QString &repo, const QList<GitStep> &steps)
{
    repo_ = repo;
    steps_ = steps;
    next();
}

void GitPublisher::next()
{
    if (steps_.isEmpty()) {
        emit finished(true, QStringLiteral("Published."));
        return;
    }
    current_ = steps_.takeFirst();
    captured_.clear();
    emit output(QStringLiteral("$ ") + gitCommandLine(current_.args) + QLatin1Char('\n'));
    process_.setWorkingDirectory(repo_);
    // Never block on an interactive credential prompt inside a GUI.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    process_.setProcessEnvironment(env);
    process_.start(gitProgram(), current_.args);
}

void GitPublisher::onFinished(int code, QProcess::ExitStatus status)
{
    if (status == QProcess::NormalExit && code == 0) {
        next();
        return;
    }
    if (current_.allowNothingToCommit && (captured_.contains(QLatin1String("nothing to commit"))
                                          || captured_.contains(QLatin1String("no changes added")))) {
        emit output(QStringLiteral("(nothing new to commit, pushing anyway)\n"));
        next();
        return;
    }
    steps_.clear();
    emit finished(false, QStringLiteral("git %1 failed (exit %2).").arg(current_.args.value(0)).arg(code));
}
