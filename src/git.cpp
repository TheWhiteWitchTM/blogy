#include "git.h"

#include <QStandardPaths>

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
                           const QString &remote, const QString &branch)
{
    repo_ = repo;
    steps_.clear();
    steps_.append({{QStringLiteral("add"), QStringLiteral("--"), relativeFile}, false});
    steps_.append({{QStringLiteral("commit"), QStringLiteral("-m"), message, QStringLiteral("--"), relativeFile}, true});
    steps_.append({{QStringLiteral("push"), remote, QStringLiteral("HEAD:") + branch}, false});
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
    emit output(QStringLiteral("$ git ") + current_.args.join(QLatin1Char(' ')) + QLatin1Char('\n'));
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
