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

// File pattern actually used for this collection. GitHub Pages' default
// Jekyll engine only publishes a file in _posts when the name starts with
// YYYY-MM-DD-, so that folder gains a {date}- prefix when the pattern has none.
QString effectiveFilePattern(const Site &site, const Collection &c);

QString relativePath(const Site &site, const Collection &c, const QDate &date, const QString &slug);

// Pulls date and slug back out of a file path using the collection's pattern.
bool parsePath(const Collection &c, const QString &relative, QDate *date, QString *slug);

// Like parsePath, but understands a date prefix added for _posts.
bool parsePath(const Site &site, const Collection &c, const QString &relative, QDate *date, QString *slug);

// Appends signature to the end of a post, once. An empty signature returns text unchanged.
QString withSignature(const QString &text, const QString &signature);

// Front-matter description, or the first paragraph of the body, as plain text.
QString excerpt(const QString &markdown);

// Excerpt, a blank line, then the post URL. Stays within the Bluesky grapheme limit.
QString blueskyAnnouncement(const QString &excerpt, const QString &url, int limit = 300);

QString publicUrl(const Site &site, const Collection &c, const QDate &date, const QString &slug);

// Site defaults + collection overrides, with title/date filled in.
FrontMatter defaultFrontMatter(const Site &site, const Collection &c, const QString &title, const QDate &date);

QString newPostText(const Site &site, const Collection &c, const QString &title, const QDate &date,
                    const QStringList &tags = QStringList(), const QString &description = QString());

// Which collection of the site owns this absolute path, if any.
const Collection *collectionFor(const Site &site, const QString &absolutePath, QString *relative = nullptr);

} // namespace post
