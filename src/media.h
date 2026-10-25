#pragma once

#include "config.h"

#include <QString>
#include <QStringList>

// Pictures and links at publish time.
// The image file is stored beside the post, named after the post
// (2026-10-25-hello.png, or my_slug/my_slug.png for page.mdx).
// A second copy is written where the site actually serves the post's URL,
// and the Markdown link is rewritten to that public address.
namespace media {

struct PublishFix {
    QString markdown;
    QStringList gitPaths; // relative to the site root, ready for git add
    QString note;
};

PublishFix fixOnPublish(const QString &markdown, const QString &postFile, const Site &site);

// Turns published image links back into local file:// urls so the preview
// can show a picture that lives beside the post or in the site's public path.
QString previewImages(const QString &markdown, const QString &postDir, const QString &siteRoot, const QString &siteUrl);

} // namespace media
