#pragma once

#include <QColor>
#include <QIcon>

// Format-toolbar symbols in the word-processor set: a letter where the
// command is a letter (B, I), a picture where the command is a picture.
enum class ToolGlyph {
    NewPost,
    Open,
    Save,
    Publish,
    Bluesky,
    Eye,
    SidePanel,
    Talk,
    Settings,
    Help,
    Bold,
    Italic,
    Strike,
    Code,
    Heading,
    H1,
    H2,
    H3,
    Bullets,
    Numbers,
    Tasks,
    Quote,
    Link,
    Image,
    Table,
    Rule,
    CodeBlock,
    Emoji,
    Flag,
    Mic,
    Coffee
};

struct GlyphInk {
    QColor ink = QColor(0xe8, 0xe6, 0xe3);
    QColor disabled = QColor(0x5c, 0x5c, 0x5c);
};

QIcon glyphIcon(ToolGlyph glyph, const GlyphInk &ink = {});
