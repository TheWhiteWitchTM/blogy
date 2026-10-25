#include "frontmatter.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace {

QString unquote(QString v)
{
    v = v.trimmed();
    if (v.size() >= 2 && ((v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"')))
                          || (v.startsWith(QLatin1Char('\'')) && v.endsWith(QLatin1Char('\''))))) {
        const QChar q = v.at(0);
        v = v.mid(1, v.size() - 2);
        if (q == QLatin1Char('"')) {
            v.replace(QStringLiteral("\\\""), QStringLiteral("\""));
            v.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
        } else {
            v.replace(QStringLiteral("''"), QStringLiteral("'"));
        }
    }
    return v;
}

QStringList splitInline(const QString &inner)
{
    QStringList out;
    QString cur;
    QChar quote;
    for (const QChar c : inner) {
        if (!quote.isNull()) {
            cur += c;
            if (c == quote)
                quote = QChar();
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            quote = c;
            cur += c;
        } else if (c == QLatin1Char(',')) {
            out << unquote(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.trimmed().isEmpty())
        out << unquote(cur);
    out.removeAll(QString());
    return out;
}

QVariant parseValue(const QString &raw)
{
    const QString v = raw.trimmed();
    if (v.startsWith(QLatin1Char('[')) && v.endsWith(QLatin1Char(']')))
        return splitInline(v.mid(1, v.size() - 2));
    if (v == QLatin1String("true"))
        return true;
    if (v == QLatin1String("false"))
        return false;
    return unquote(v);
}

QString quoteDouble(const QString &v)
{
    QString e = v;
    e.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    e.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + e + QLatin1Char('"');
}

bool needsQuotes(const QString &v)
{
    if (v.isEmpty())
        return true;
    static const QRegularExpression risky(QStringLiteral(R"(^[\s\[\]{}>|*&!%@`#'",?-]|: | #|\s$)"));
    if (risky.match(v).hasMatch())
        return true;
    return v == QLatin1String("true") || v == QLatin1String("false") || v == QLatin1String("null")
        || v == QLatin1String("yes") || v == QLatin1String("no");
}

} // namespace

QString yamlScalar(const QString &key, const QString &value)
{
    // Titles and descriptions are always quoted on the sites; dates and urls stay bare.
    const bool alwaysQuote = key == QLatin1String("title") || key == QLatin1String("description");
    return (alwaysQuote || needsQuotes(value)) ? quoteDouble(value) : value;
}

bool FrontMatter::contains(const QString &key) const
{
    for (const auto &f : fields) {
        if (f.first == key)
            return true;
    }
    return false;
}

QVariant FrontMatter::value(const QString &key) const
{
    for (const auto &f : fields) {
        if (f.first == key)
            return f.second;
    }
    return {};
}

QString FrontMatter::string(const QString &key) const
{
    const QVariant v = value(key);
    if (v.typeId() == QMetaType::QStringList)
        return v.toStringList().join(QStringLiteral(", "));
    if (v.typeId() == QMetaType::Bool)
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return v.toString();
}

QStringList FrontMatter::list(const QString &key) const
{
    const QVariant v = value(key);
    if (v.typeId() == QMetaType::QStringList)
        return v.toStringList();
    const QString s = v.toString().trimmed();
    return s.isEmpty() ? QStringList() : splitInline(s);
}

void FrontMatter::set(const QString &key, const QVariant &value)
{
    for (auto &f : fields) {
        if (f.first == key) {
            f.second = value;
            return;
        }
    }
    fields.append({key, value});
}

void FrontMatter::remove(const QString &key)
{
    for (int i = fields.size() - 1; i >= 0; --i) {
        if (fields.at(i).first == key)
            fields.removeAt(i);
    }
}

FrontMatter FrontMatter::parseYaml(const QString &yaml)
{
    FrontMatter fm;
    static const QRegularExpression keyLine(QStringLiteral(R"(^([A-Za-z0-9_\-]+)\s*:\s?(.*)$)"));
    static const QRegularExpression itemLine(QStringLiteral(R"(^\s+-\s+(.*)$)"));
    QString pendingKey;
    QStringList pendingItems;
    auto flush = [&] {
        if (!pendingKey.isEmpty())
            fm.set(pendingKey, pendingItems.isEmpty() ? QVariant(QString()) : QVariant(pendingItems));
        pendingKey.clear();
        pendingItems.clear();
    };
    const QStringList lines = yaml.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.trimmed().isEmpty() || line.trimmed().startsWith(QLatin1Char('#')))
            continue;
        const auto item = itemLine.match(line);
        if (!pendingKey.isEmpty() && item.hasMatch()) {
            pendingItems << unquote(item.captured(1));
            continue;
        }
        const auto m = keyLine.match(line);
        if (!m.hasMatch())
            continue;
        flush();
        const QString key = m.captured(1);
        const QString rest = m.captured(2);
        if (rest.trimmed().isEmpty())
            pendingKey = key;
        else
            fm.set(key, parseValue(rest));
    }
    flush();
    return fm;
}

FrontMatter FrontMatter::parseMdx(const QString &objectBody)
{
    // Best effort for `key: "value",` and `key: ["a", "b"],` lines.
    FrontMatter fm;
    static const QRegularExpression keyLine(QStringLiteral(R"(^\s*([A-Za-z0-9_]+)\s*:\s*(.*?),?\s*$)"));
    const QStringList lines = objectBody.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const auto m = keyLine.match(line);
        if (m.hasMatch())
            fm.set(m.captured(1), parseValue(m.captured(2)));
    }
    return fm;
}

FrontMatter FrontMatter::parse(const QString &text, QString *body, bool *hadBlock)
{
    if (hadBlock)
        *hadBlock = false;
    QString t = text;
    if (t.startsWith(QChar(0xfeff)))
        t.remove(0, 1);
    if (t.startsWith(QLatin1String("---\n")) || t.startsWith(QLatin1String("---\r\n"))) {
        static const QRegularExpression end(QStringLiteral(R"(\r?\n---[ \t]*(\r?\n|$))"));
        const int start = t.indexOf(QLatin1Char('\n')) + 1;
        const auto m = end.match(t, start - 1);
        if (m.hasMatch()) {
            if (hadBlock)
                *hadBlock = true;
            if (body)
                *body = t.mid(m.capturedEnd());
            return parseYaml(t.mid(start, m.capturedStart() - start));
        }
    }
    static const QRegularExpression mdx(QStringLiteral(R"(^\s*export\s+const\s+metadata\s*=\s*\{(.*?)\}\s*;?[ \t]*\r?\n?)"),
                                        QRegularExpression::DotMatchesEverythingOption);
    const auto m = mdx.match(t);
    if (m.hasMatch()) {
        if (hadBlock)
            *hadBlock = true;
        if (body)
            *body = t.mid(m.capturedEnd());
        return parseMdx(m.captured(1));
    }
    if (body)
        *body = t;
    return {};
}

FrontMatter FrontMatter::fromJson(const QJsonObject &obj)
{
    FrontMatter fm;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QJsonValue v = it.value();
        if (v.isBool()) {
            fm.set(it.key(), v.toBool());
        } else if (v.isArray()) {
            QStringList items;
            for (const QJsonValue &x : v.toArray())
                items << x.toVariant().toString();
            fm.set(it.key(), items);
        } else {
            fm.set(it.key(), v.toVariant().toString());
        }
    }
    return fm;
}

QString FrontMatter::toYaml() const
{
    QStringList out;
    for (const auto &f : fields) {
        const QVariant &v = f.second;
        if (v.typeId() == QMetaType::Bool) {
            out << f.first + QStringLiteral(": ") + (v.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
        } else if (v.typeId() == QMetaType::QStringList) {
            QStringList items;
            for (const QString &s : v.toStringList())
                items << (needsQuotes(s) || s.contains(QLatin1Char(',')) ? quoteDouble(s) : s);
            out << f.first + QStringLiteral(": [") + items.join(QStringLiteral(", ")) + QLatin1Char(']');
        } else {
            out << f.first + QStringLiteral(": ") + yamlScalar(f.first, v.toString());
        }
    }
    return out.join(QLatin1Char('\n'));
}

QString FrontMatter::toBlock(bool mdx) const
{
    if (!mdx)
        return QStringLiteral("---\n") + toYaml() + QStringLiteral("\n---\n");
    QStringList out;
    out << QStringLiteral("export const metadata = {");
    for (const auto &f : fields) {
        const QVariant &v = f.second;
        if (v.typeId() == QMetaType::Bool) {
            out << f.first + QStringLiteral(": ") + (v.toBool() ? QStringLiteral("true") : QStringLiteral("false")) + QLatin1Char(',');
        } else if (v.typeId() == QMetaType::QStringList) {
            QStringList items;
            for (const QString &s : v.toStringList())
                items << quoteDouble(s);
            out << f.first + QStringLiteral(": [") + items.join(QStringLiteral(", ")) + QStringLiteral("],");
        } else {
            out << f.first + QStringLiteral(": ") + quoteDouble(v.toString()) + QLatin1Char(',');
        }
    }
    out << QStringLiteral("};");
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString FrontMatter::compose(const QString &body, bool mdx) const
{
    QString b = body;
    if (!b.startsWith(QLatin1Char('\n')))
        b.prepend(QLatin1Char('\n'));
    return toBlock(mdx) + b;
}
