#pragma once

#include "config.h"

#include <QList>
#include <QString>

// A git checkout under the folder Blogy scans. blog is true when the tree
// looks like a Jekyll site or an MDX blog; other GitHub repos are listed too.
struct FoundRepo {
    Site site;
    bool blog = false;
    QString detail;
};

bool parseGitHubRemote(const QString &remote, QString *owner, QString *repo);
QString githubPagesUrl(const QString &owner, const QString &repo);

// Reads _config.yml, collections, _posts, and blog/*/page.mdx. No git calls.
Site inspectBlog(const QString &absolutePath, bool *blog = nullptr);

// Top-level directories of `directory` that are GitHub clones.
QList<FoundRepo> scanGitHubRepos(const QString &directory);
