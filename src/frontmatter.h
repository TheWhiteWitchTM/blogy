#pragma once

#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariant>

// Ordered front matter. Values are QString, bool, or QStringList.
// Jekyll posts carry it as a YAML block between --- lines; MDX posts as
// `export const metadata = { ... };` at the top of the file.
class FrontMatter {
public:
    QList<QPair<QString, QVariant>> fields;

    bool contains(const QString &key) const;
    QVariant value(const QString &key) const;
    QString string(const QString &key) const;
    QStringList list(const QString &key) const;
    void set(const QString &key, const QVariant &value);
    void remove(const QString &key);
    bool isEmpty() const { return fields.isEmpty(); }

    // Splits text into front matter and body. hadBlock says whether a block was found.
    static FrontMatter parse(const QString &text, QString *body, bool *hadBlock = nullptr);
    static FrontMatter parseYaml(const QString &yaml);
    static FrontMatter parseMdx(const QString &objectBody);
    static FrontMatter fromJson(const QJsonObject &obj);

    QString toYaml() const;    // without the --- fences
    QString toBlock(bool mdx) const; // fenced YAML or the MDX export
    QString compose(const QString &body, bool mdx) const;
};

QString yamlScalar(const QString &key, const QString &value);
