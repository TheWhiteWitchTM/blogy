#include "preview.h"

#include "frontmatter.h"
#include "media.h"

#include <QFont>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextDocument>
#include <QUrl>

Preview::Preview(QWidget *parent)
    : QTextBrowser(parent)
{
    setObjectName(QStringLiteral("preview"));
    setOpenExternalLinks(true);
    timer_.setSingleShot(true);
    timer_.setInterval(250);
    connect(&timer_, &QTimer::timeout, this, &Preview::render);
    QFont reading = font();
    reading.setPointSize(17);
    setFont(reading);
    document()->setDefaultFont(reading);
    document()->setDefaultStyleSheet(QStringLiteral(
        "a { color: #ffb454; } code { color: #7fd962; } pre { color: #7fd962; background: #0e0e0e; }"
        "h1, h2, h3, h4 { color: #ffd18a; } blockquote { color: #a0a0a0; }"));
    document()->setDocumentMargin(28);
}

void Preview::setSource(const QString &markdown, const QString &baseDir, const QString &siteRoot, const QString &siteUrl)
{
    pending_ = markdown;
    postDir_ = baseDir;
    siteRoot_ = siteRoot;
    siteUrl_ = siteUrl;
    if (!baseDir.isEmpty())
        document()->setBaseUrl(QUrl::fromLocalFile(baseDir + QLatin1Char('/')));
    timer_.start();
}

QString Preview::renderMarkdown(const QString &markdown)
{
    QString body;
    const FrontMatter fm = FrontMatter::parse(markdown, &body);
    static const QRegularExpression liquid(QStringLiteral(R"(\{%.*?%\}|\{\{.*?\}\})"),
                                           QRegularExpression::DotMatchesEverythingOption);
    body.replace(liquid, QString());
    QString head;
    if (!fm.isEmpty()) {
        const QString title = fm.string(QStringLiteral("title"));
        if (!title.isEmpty())
            head += QStringLiteral("# ") + title + QStringLiteral("\n\n");
        QStringList meta;
        if (fm.contains(QStringLiteral("date")))
            meta << fm.string(QStringLiteral("date"));
        const QStringList tags = fm.list(QStringLiteral("tags"));
        if (!tags.isEmpty())
            meta << (QStringLiteral("#") + tags.join(QStringLiteral(" #")));
        if (!meta.isEmpty())
            head += QStringLiteral("*") + meta.join(QStringLiteral(" · ")) + QStringLiteral("*\n\n");
        const QString desc = fm.string(QStringLiteral("description"));
        if (!desc.isEmpty())
            head += QStringLiteral("> ") + desc + QStringLiteral("\n\n");
        head += QStringLiteral("---\n\n");
    }
    return head + body;
}

void Preview::render()
{
    const int pos = verticalScrollBar()->value();
    const QString withImages = media::previewImages(pending_, postDir_, siteRoot_, siteUrl_);
    document()->setDefaultFont(font());
    setMarkdown(renderMarkdown(withImages));
    verticalScrollBar()->setValue(pos);
}
