#pragma once

#include <QTextBrowser>
#include <QTimer>

// Live preview through QTextDocument's own Markdown reader (CommonMark + GFM).
// Front matter becomes a header card; Liquid tags are dropped.
class Preview : public QTextBrowser {
    Q_OBJECT
public:
    explicit Preview(QWidget *parent = nullptr);
    void setSource(const QString &markdown, const QString &baseDir);
    void setDelay(int ms) { timer_.setInterval(ms); }
    static QString renderMarkdown(const QString &markdown);

private:
    void render();
    QTimer timer_;
    QString pending_;
};
