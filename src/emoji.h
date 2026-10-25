#pragma once

#include <QString>
#include <QVector>

// One insertable glyph: the characters that go into the post, and a name
// used for search and :shortcodes:.
struct Glyph {
    QString emoji;
    QString name;
};

QVector<Glyph> emojiGlyphs();
QVector<Glyph> flagGlyphs();

// ":smile:", "flag-at", "Austria" — colons optional. Empty when unknown.
// Two-letter codes only count with a flag- prefix, so ":it:" stays text.
QString emojiForShortcode(const QString &code);
