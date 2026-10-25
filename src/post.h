#pragma once

#include "config.h"
#include "frontmatter.h"

#include <QDate>
#include <QString>

// Naming and URL rules for a post, shared by "New Post" and "Publish".
namespace post {

QString slugify(const QString &title, const QString &separator = QStringLiteral("-"));

// Replaces {date} {year} {month} {day} {slug} {category} {title} in a pattern.
QString expand(const QString &pattern, const QDate &date, const QString &slug,
               const QString &category, const QString &title = QString());

// Path relative to the site root, e.g. collections/_blog/2026-10-25-hello.md
QString relativePath(const Collection &c, const QDate &date, const QString &slug);

// Pulls date and slug back out of a file path using the collection's pattern.
bool parsePath(const Collection &c, const QString &relative, QDate *date, QString *slug);

QString publicUrl(const Site &site, const Collection &c, const QDate &date, const QString &slug);

// Site defaults + collection overrides, with title/date filled in.
FrontMatter defaultFrontMatter(const Site &site, const Collection &c, const QString &title, const QDate &date);

QString newPostText(const Site &site, const Collection &c, const QString &title, const QDate &date,
                    const QStringList &tags = QStringList(), const QString &description = QString());

// Which collection of the site owns this absolute path, if any.
const Collection *collectionFor(const Site &site, const QString &absolutePath, QString *relative = nullptr);

} // namespace post
