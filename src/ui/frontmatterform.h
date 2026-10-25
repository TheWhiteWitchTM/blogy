#pragma once

#include "frontmatter.h"

#include <QWidget>

class QFormLayout;
class QLineEdit;

// A form built from whatever keys the post has: text fields for strings,
// check boxes for true/false, comma lists for arrays. Add or remove keys freely.
class FrontMatterForm : public QWidget {
    Q_OBJECT
public:
    explicit FrontMatterForm(QWidget *parent = nullptr);

    void setFrontMatter(const FrontMatter &fm);
    FrontMatter frontMatter() const;
    bool isEditing() const;

signals:
    void edited();

private:
    struct Row {
        QString key;
        int kind = 0; // 0 string, 1 bool, 2 list
        QWidget *editor = nullptr;
        QWidget *rowWidget = nullptr;
    };
    void rebuild(const FrontMatter &fm);
    void addRow(const QString &key, const QVariant &value);

    QFormLayout *form_ = nullptr;
    QLineEdit *newKey_ = nullptr;
    QList<Row> rows_;
    bool loading_ = false;
};
