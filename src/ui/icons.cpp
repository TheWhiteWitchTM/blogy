#include "ui/icons.h"

#include <QFont>
#include <QFontInfo>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace {

QPen pen(const QColor &c, qreal width)
{
    return QPen(c, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

QFont markFont(int pixel, bool italic)
{
    QFont font(QStringLiteral("Fira Sans"));
    if (!QFontInfo(font).exactMatch())
        font = QFont(QStringLiteral("DejaVu Sans"));
    font.setPixelSize(pixel);
    font.setWeight(QFont::Black);
    font.setItalic(italic);
    return font;
}

void drawMark(QPainter &p, const QColor &c, const QString &text, int pixel, bool italic)
{
    p.setFont(markFont(pixel, italic));
    p.setPen(c);
    p.drawText(QRectF(0, -1, 24, 26), Qt::AlignCenter, text);
}

void drawNew(QPainter &p, const QColor &c)
{
    p.setPen(pen(c, 1.7));
    p.setBrush(Qt::NoBrush);
    QPainterPath page;
    page.moveTo(5.5, 2.5);
    page.lineTo(13.5, 2.5);
    page.lineTo(19.5, 8.5);
    page.lineTo(19.5, 21.5);
    page.lineTo(5.5, 21.5);
    page.closeSubpath();
    p.drawPath(page);
    p.drawLine(QPointF(13.5, 2.5), QPointF(13.5, 8.5));
    p.drawLine(QPointF(13.5, 8.5), QPointF(19.5, 8.5));
    p.setPen(pen(c, 1.8));
    p.drawLine(QPointF(12.2, 12.6), QPointF(12.2, 18.6));
    p.drawLine(QPointF(9.2, 15.6), QPointF(15.2, 15.6));
}

void drawOpen(QPainter &p, const QColor &c)
{
    p.setPen(pen(c, 1.7));
    p.setBrush(Qt::NoBrush);
    QPainterPath folder;
    folder.moveTo(2, 9);
    folder.lineTo(2, 5.5);
    folder.lineTo(9, 5.5);
    folder.lineTo(11.5, 8.5);
    folder.lineTo(22, 8.5);
    folder.lineTo(22, 19.5);
    folder.lineTo(2, 19.5);
    folder.closeSubpath();
    p.drawPath(folder);
    p.drawLine(QPointF(2, 9), QPointF(22, 9));
}

void drawSave(QPainter &p, const QColor &c)
{
    p.setPen(pen(c, 1.7));
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(12, 2.5), QPointF(12, 12.2));
    p.drawLine(QPointF(7.4, 8.2), QPointF(12, 12.8));
    p.drawLine(QPointF(16.6, 8.2), QPointF(12, 12.8));
    QPainterPath tray;
    tray.moveTo(3, 11);
    tray.lineTo(3, 21);
    tray.lineTo(21, 21);
    tray.lineTo(21, 11);
    p.drawPath(tray);
    p.drawLine(QPointF(3, 11), QPointF(8, 11));
    p.drawLine(QPointF(16, 11), QPointF(21, 11));
}

void drawPublish(QPainter &p, const QColor &c)
{
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    QPainterPath plane;
    plane.moveTo(21.5, 2.5);
    plane.lineTo(2, 11);
    plane.lineTo(10.2, 13.6);
    plane.lineTo(7.6, 21.5);
    plane.lineTo(13.2, 14.4);
    plane.closeSubpath();
    p.drawPath(plane);
}

void drawBluesky(QPainter &p, const QColor &c)
{
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    QPainterPath upper;
    upper.moveTo(12, 8.2);
    upper.cubicTo(10.5, 1.2, 1.2, 1.6, 2.2, 9);
    upper.cubicTo(3, 14.2, 8.2, 13.2, 12, 11.6);
    upper.closeSubpath();
    QPainterPath lower;
    lower.moveTo(12, 12.4);
    lower.cubicTo(9.2, 12.6, 3.2, 14.6, 4.4, 19);
    lower.cubicTo(5.4, 22.2, 10, 21, 12, 17.6);
    lower.closeSubpath();
    p.drawPath(upper);
    p.drawPath(lower);
    p.save();
    p.translate(24, 0);
    p.scale(-1, 1);
    p.drawPath(upper);
    p.drawPath(lower);
    p.restore();
}

void drawEye(QPainter &p, const QColor &c)
{
    p.setPen(pen(c, 1.7));
    p.setBrush(Qt::NoBrush);
    QPainterPath eye;
    eye.moveTo(1.5, 12);
    eye.cubicTo(5.5, 5.2, 18.5, 5.2, 22.5, 12);
    eye.cubicTo(18.5, 18.8, 5.5, 18.8, 1.5, 12);
    p.drawPath(eye);
    p.drawEllipse(QPointF(12, 12), 3.3, 3.3);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(QPointF(12, 12), 1.35, 1.35);
}

void drawSidePanel(QPainter &p, const QColor &c)
{
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.save();
    p.setClipRect(QRectF(14.2, 3, 8, 18));
    p.drawRoundedRect(QRectF(2, 3, 20, 18), 2.2, 2.2);
    p.restore();
    p.setPen(pen(c, 1.7));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(2, 3, 20, 18), 2.2, 2.2);
    p.drawLine(QPointF(14.2, 4.2), QPointF(14.2, 19.8));
}

void drawTalk(QPainter &p, const QColor &c)
{
    auto spark = [&](qreal cx, qreal cy, qreal r) {
        QPainterPath s;
        s.moveTo(cx, cy - r);
        s.quadTo(cx + r * 0.16, cy - r * 0.16, cx + r, cy);
        s.quadTo(cx + r * 0.16, cy + r * 0.16, cx, cy + r);
        s.quadTo(cx - r * 0.16, cy + r * 0.16, cx - r, cy);
        s.quadTo(cx - r * 0.16, cy - r * 0.16, cx, cy - r);
        p.drawPath(s);
    };
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    spark(10.5, 13, 8.2);
    spark(19.2, 5.6, 3.6);
}

void drawSettings(QPainter &p, const QColor &c)
{
    p.save();
    p.translate(12, 12);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    for (int i = 0; i < 8; ++i) {
        p.drawRoundedRect(QRectF(-1.8, -11.2, 3.6, 5.4), 1.2, 1.2);
        p.rotate(45);
    }
    p.drawEllipse(QPointF(0, 0), 6.5, 6.5);
    p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    p.drawEllipse(QPointF(0, 0), 2.8, 2.8);
    p.restore();
}

void drawHelp(QPainter &p, const QColor &c)
{
    p.setPen(pen(c, 1.7));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(2.2, 2.2, 19.6, 19.6));
    p.setFont(markFont(14, false));
    p.setPen(c);
    p.drawText(QRectF(2.2, 1.2, 19.6, 19.6), Qt::AlignCenter, QStringLiteral("?"));
}

void drawHeading(QPainter &p, const QColor &c, const QString &n, qreal bar)
{
    p.setFont(markFont(13, false));
    p.setPen(c);
    p.drawText(QRectF(0, 0, 15, 18), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("H"));
    p.setFont(markFont(11, false));
    p.drawText(QRectF(15, 1, 9, 18), Qt::AlignLeft | Qt::AlignVCenter, n);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(QRectF(3.5, 19.2, 17, bar), 0.7, 0.7);
}

void drawBullets(QPainter &p, const QColor &c)
{
    // Word's list mark: three round bullets and three lines, nothing else.
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    for (int i = 0; i < 3; ++i)
        p.drawEllipse(QPointF(4.6, 5.2 + i * 6.8), 2.35, 2.35);
    p.setPen(pen(c, 2.0));
    p.drawLine(QPointF(9.6, 5.2), QPointF(21.4, 5.2));
    p.drawLine(QPointF(9.6, 12.0), QPointF(21.4, 12.0));
    p.drawLine(QPointF(9.6, 18.8), QPointF(21.4, 18.8));
}

void drawNumbers(QPainter &p, const QColor &c)
{
    const char *labels[] = {"1", "2", "3"};
    for (int i = 0; i < 3; ++i) {
        p.setFont(markFont(9, false));
        p.setPen(c);
        p.drawText(QRectF(0, i * 8.0, 8, 8), Qt::AlignCenter, QString::fromLatin1(labels[i]));
        p.setPen(pen(c, 2.0));
        p.drawLine(QPointF(9.4, 4.0 + i * 8.0), QPointF(21.6, 4.0 + i * 8.0));
    }
}

void drawTasks(QPainter &p, const QColor &c)
{
    p.setPen(pen(c, 1.8));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(2, 2.4, 8.6, 8.6), 1.6, 1.6);
    p.drawRoundedRect(QRectF(2, 13.2, 8.6, 8.6), 1.6, 1.6);
    p.setPen(pen(c, 2.0));
    p.drawLine(QPointF(3.7, 6.8), QPointF(5.8, 9.1));
    p.drawLine(QPointF(5.8, 9.1), QPointF(9.4, 4.2));
    p.drawLine(QPointF(13.2, 6.6), QPointF(22, 6.6));
    p.drawLine(QPointF(13.2, 17.4), QPointF(22, 17.4));
}

void drawQuote(QPainter &p, const QColor &c)
{
    QFont font(QStringLiteral("DejaVu Serif"));
    if (!QFontInfo(font).exactMatch())
        font = markFont(22, false);
    font.setPixelSize(26);
    font.setWeight(QFont::Bold);
    p.setFont(font);
    p.setPen(c);
    p.drawText(QRectF(0, -1, 24, 26), Qt::AlignCenter, QString(QChar(0x201C)));
}

void drawLink(QPainter &p, const QColor &c)
{
    // Two chain links, the same mark Word uses for a hyperlink.
    p.save();
    p.translate(12, 12);
    p.rotate(-45);
    p.setPen(pen(c, 2.2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(-11.2, -3.5, 13.2, 7.0), 3.5, 3.5);
    p.drawRoundedRect(QRectF(-2.0, -3.5, 13.2, 7.0), 3.5, 3.5);
    p.restore();
}

void drawImage(QPainter &p, const QColor &c)
{
    p.setPen(pen(c, 1.8));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(2, 3, 20, 18), 2, 2);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(QPointF(7.6, 8.2), 2.3, 2.3);
    QPainterPath hill;
    hill.moveTo(3.1, 19.2);
    hill.lineTo(8.6, 12.4);
    hill.lineTo(12.2, 16.2);
    hill.lineTo(16.4, 11.2);
    hill.lineTo(20.9, 19.2);
    hill.closeSubpath();
    p.drawPath(hill);
}

void drawTable(QPainter &p, const QColor &c)
{
    p.setPen(pen(c, 1.7));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(2.2, 3, 19.6, 18), 1.5, 1.5);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRect(QRectF(3.2, 4.0, 17.6, 4.6));
    p.setPen(pen(c, 1.7));
    p.drawLine(QPointF(2.2, 9.2), QPointF(21.8, 9.2));
    p.drawLine(QPointF(2.2, 14.6), QPointF(21.8, 14.6));
    p.drawLine(QPointF(8.8, 3), QPointF(8.8, 21));
    p.drawLine(QPointF(15.2, 3), QPointF(15.2, 21));
}

void drawRule(QPainter &p, const QColor &c)
{
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(QRectF(2, 10.6, 20, 2.8), 1.2, 1.2);
}

void drawCode(QPainter &p, const QColor &c)
{
    QFont font(QStringLiteral("DejaVu Sans Mono"));
    if (!QFontInfo(font).exactMatch())
        font = markFont(12, false);
    font.setPixelSize(11);
    font.setWeight(QFont::Bold);
    font.setItalic(false);
    p.setFont(font);
    p.setPen(c);
    p.drawText(QRectF(0, 0, 24, 24), Qt::AlignCenter, QStringLiteral("</>"));
}

void drawCodeBlock(QPainter &p, const QColor &c)
{
    // A fenced block: a page, and the same </> mark used for inline code.
    p.setPen(pen(c, 1.7));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(1.6, 2.2, 20.8, 19.6), 2.2, 2.2);
    QFont font(QStringLiteral("DejaVu Sans Mono"));
    if (!QFontInfo(font).exactMatch())
        font = markFont(9, false);
    font.setPixelSize(8);
    font.setWeight(QFont::Bold);
    p.setFont(font);
    p.setPen(c);
    p.drawText(QRectF(1.6, 6, 20.8, 14), Qt::AlignCenter, QStringLiteral("</>"));
}

void drawEmoji(QPainter &p, const QColor &c)
{
    p.setPen(pen(c, 1.8));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(2, 2, 20, 20));
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(QPointF(8.6, 10), 1.45, 1.45);
    p.drawEllipse(QPointF(15.4, 10), 1.45, 1.45);
    p.setPen(pen(c, 1.8));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(7.2, 10.4, 9.6, 7.4), 200 * 16, 140 * 16);
}

void drawFlag(QPainter &p, const QColor &c)
{
    p.setPen(pen(c, 2.0));
    p.drawLine(QPointF(4.2, 2), QPointF(4.2, 22));
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    QPainterPath flag;
    flag.moveTo(5.4, 3);
    flag.lineTo(21, 3);
    flag.lineTo(21, 13.2);
    flag.lineTo(5.4, 13.2);
    flag.closeSubpath();
    p.drawPath(flag);
}

void drawCoffee(QPainter &p, const QColor &c)
{
    p.setPen(pen(c, 1.8));
    p.setBrush(Qt::NoBrush);
    QPainterPath steam;
    steam.moveTo(8, 6.4);
    steam.cubicTo(8, 4.2, 11.4, 4.4, 10.5, 1.8);
    p.drawPath(steam);
    QPainterPath steam2;
    steam2.moveTo(13.4, 6.4);
    steam2.cubicTo(13.4, 3.6, 16.8, 3.8, 15.8, 1.4);
    p.drawPath(steam2);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    QPainterPath cup;
    cup.moveTo(4.8, 8);
    cup.lineTo(6.6, 16.6);
    cup.quadTo(12, 21.2, 17.4, 16.6);
    cup.lineTo(19.2, 8);
    cup.closeSubpath();
    p.drawPath(cup);
    p.setPen(pen(c, 2.2));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(16.4, 9.2, 6.4, 6.6), -50 * 16, 190 * 16);
}

void drawStrike(QPainter &p, const QColor &c)
{
    // Word's strikethrough mark is the letters with a line through them.
    // A lone S reads as a dollar sign once the line crosses it.
    QFont font(QStringLiteral("DejaVu Sans"));
    font.setPixelSize(11);
    font.setWeight(QFont::Bold);
    font.setItalic(false);
    p.setFont(font);
    p.setPen(c);
    p.drawText(QRectF(0, 0, 24, 24), Qt::AlignCenter, QStringLiteral("abc"));
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(QRectF(1.6, 11.4, 20.8, 1.7), 0.6, 0.6);
}

void drawMic(QPainter &p, const QColor &c)
{
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(QRectF(8.2, 2, 7.6, 12.2), 3.8, 3.8);
    p.setPen(pen(c, 1.8));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(5.2, 8.2, 13.6, 10.2), 200 * 16, 140 * 16);
    p.drawLine(QPointF(12, 18.2), QPointF(12, 21.2));
    p.drawLine(QPointF(8.2, 21.2), QPointF(15.8, 21.2));
}

void paintGlyph(QPainter &p, ToolGlyph glyph, const QColor &c)
{
    switch (glyph) {
    case ToolGlyph::NewPost: drawNew(p, c); break;
    case ToolGlyph::Open: drawOpen(p, c); break;
    case ToolGlyph::Save: drawSave(p, c); break;
    case ToolGlyph::Publish: drawPublish(p, c); break;
    case ToolGlyph::Bluesky: drawBluesky(p, c); break;
    case ToolGlyph::Eye: drawEye(p, c); break;
    case ToolGlyph::SidePanel: drawSidePanel(p, c); break;
    case ToolGlyph::Talk: drawTalk(p, c); break;
    case ToolGlyph::Settings: drawSettings(p, c); break;
    case ToolGlyph::Help: drawHelp(p, c); break;
    case ToolGlyph::Bold: drawMark(p, c, QStringLiteral("B"), 18, false); break;
    case ToolGlyph::Italic: drawMark(p, c, QStringLiteral("I"), 18, true); break;
    case ToolGlyph::Strike: drawStrike(p, c); break;
    case ToolGlyph::Code: drawCode(p, c); break;
    case ToolGlyph::Heading: drawMark(p, c, QStringLiteral("H"), 18, false); break;
    case ToolGlyph::H1: drawHeading(p, c, QStringLiteral("1"), 2.6); break;
    case ToolGlyph::H2: drawHeading(p, c, QStringLiteral("2"), 1.7); break;
    case ToolGlyph::H3: drawHeading(p, c, QStringLiteral("3"), 1.15); break;
    case ToolGlyph::Bullets: drawBullets(p, c); break;
    case ToolGlyph::Numbers: drawNumbers(p, c); break;
    case ToolGlyph::Tasks: drawTasks(p, c); break;
    case ToolGlyph::Quote: drawQuote(p, c); break;
    case ToolGlyph::Link: drawLink(p, c); break;
    case ToolGlyph::Image: drawImage(p, c); break;
    case ToolGlyph::Table: drawTable(p, c); break;
    case ToolGlyph::Rule: drawRule(p, c); break;
    case ToolGlyph::CodeBlock: drawCodeBlock(p, c); break;
    case ToolGlyph::Emoji: drawEmoji(p, c); break;
    case ToolGlyph::Flag: drawFlag(p, c); break;
    case ToolGlyph::Mic: drawMic(p, c); break;
    case ToolGlyph::Coffee: drawCoffee(p, c); break;
    }
}

QPixmap render(ToolGlyph glyph, const QColor &color, int logical, qreal dpr)
{
    QPixmap pix(qRound(logical * dpr), qRound(logical * dpr));
    pix.setDevicePixelRatio(dpr);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.scale(dpr * logical / 24.0, dpr * logical / 24.0);
    paintGlyph(p, glyph, color);
    p.end();
    return pix;
}

} // namespace

QIcon glyphIcon(ToolGlyph glyph, const GlyphInk &ink)
{
    QIcon icon;
    for (int logical : {16, 20, 24}) {
        for (qreal dpr : {1.0, 2.0}) {
            icon.addPixmap(render(glyph, ink.ink, logical, dpr), QIcon::Normal, QIcon::Off);
            icon.addPixmap(render(glyph, ink.disabled, logical, dpr), QIcon::Disabled, QIcon::Off);
            icon.addPixmap(render(glyph, ink.ink, logical, dpr), QIcon::Active, QIcon::Off);
            icon.addPixmap(render(glyph, ink.ink, logical, dpr), QIcon::Selected, QIcon::Off);
        }
    }
    return icon;
}
