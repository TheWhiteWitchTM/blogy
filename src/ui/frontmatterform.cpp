#include "ui/frontmatterform.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QString listText(const QStringList &items) { return items.join(QStringLiteral(", ")); }
QStringList splitList(const QString &text)
{
    QStringList out;
    for (const QString &s : text.split(QLatin1Char(','))) {
        const QString t = s.trimmed();
        if (!t.isEmpty())
            out << t;
    }
    return out;
}
} // namespace

FrontMatterForm::FrontMatterForm(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("frontPane"));
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 8, 10, 8);
    outer->setSpacing(6);
    auto *hint = new QLabel(QStringLiteral("Lists are comma separated. Changes go straight into the post."), this);
    hint->setObjectName(QStringLiteral("hint"));
    hint->setWordWrap(true);
    outer->addWidget(hint);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *inner = new QWidget(scroll);
    inner->setObjectName(QStringLiteral("frontPane"));
    form_ = new QFormLayout(inner);
    form_->setContentsMargins(0, 6, 0, 6);
    form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    scroll->setWidget(inner);
    outer->addWidget(scroll, 1);

    auto *addRowBox = new QHBoxLayout;
    newKey_ = new QLineEdit(this);
    newKey_->setPlaceholderText(QStringLiteral("new key, e.g. image"));
    auto *addText = new QPushButton(QStringLiteral("+ Text"), this);
    auto *addBool = new QPushButton(QStringLiteral("+ Yes/No"), this);
    auto *addList = new QPushButton(QStringLiteral("+ List"), this);
    addRowBox->addWidget(newKey_, 1);
    addRowBox->addWidget(addText);
    addRowBox->addWidget(addBool);
    addRowBox->addWidget(addList);
    outer->addLayout(addRowBox);

    auto add = [this](const QVariant &v) {
        const QString key = newKey_->text().trimmed();
        if (key.isEmpty())
            return;
        FrontMatter fm = frontMatter();
        if (!fm.contains(key))
            fm.set(key, v);
        newKey_->clear();
        rebuild(fm);
        emit edited();
    };
    connect(addText, &QPushButton::clicked, this, [add] { add(QString()); });
    connect(addBool, &QPushButton::clicked, this, [add] { add(false); });
    connect(addList, &QPushButton::clicked, this, [add] { add(QStringList()); });
}

bool FrontMatterForm::isEditing() const
{
    QWidget *f = focusWidget();
    return f && isAncestorOf(f) && f->hasFocus();
}

void FrontMatterForm::addRow(const QString &key, const QVariant &value)
{
    Row row;
    row.key = key;
    auto *wrap = new QWidget;
    auto *h = new QHBoxLayout(wrap);
    h->setContentsMargins(0, 0, 0, 0);
    if (value.typeId() == QMetaType::Bool) {
        row.kind = 1;
        auto *box = new QCheckBox(wrap);
        box->setChecked(value.toBool());
        connect(box, &QCheckBox::toggled, this, [this] { if (!loading_) emit edited(); });
        row.editor = box;
    } else {
        row.kind = value.typeId() == QMetaType::QStringList ? 2 : 0;
        auto *line = new QLineEdit(wrap);
        line->setText(row.kind == 2 ? listText(value.toStringList()) : value.toString());
        line->setCursorPosition(0);
        if (row.kind == 2)
            line->setPlaceholderText(QStringLiteral("a, b, c"));
        connect(line, &QLineEdit::textEdited, this, [this] { if (!loading_) emit edited(); });
        row.editor = line;
    }
    h->addWidget(row.editor, 1);
    auto *remove = new QToolButton(wrap);
    remove->setText(QStringLiteral("×"));
    remove->setToolTip(QStringLiteral("Remove %1").arg(key));
    h->addWidget(remove);
    connect(remove, &QToolButton::clicked, this, [this, key] {
        FrontMatter fm = frontMatter();
        fm.remove(key);
        rebuild(fm);
        emit edited();
    });
    row.rowWidget = wrap;
    form_->addRow(key, wrap);
    rows_.append(row);
}

void FrontMatterForm::rebuild(const FrontMatter &fm)
{
    loading_ = true;
    while (form_->rowCount() > 0)
        form_->removeRow(0);
    rows_.clear();
    for (const auto &f : fm.fields)
        addRow(f.first, f.second);
    loading_ = false;
}

void FrontMatterForm::setFrontMatter(const FrontMatter &fm)
{
    // Same keys in the same order: update values in place so focus is kept.
    bool sameShape = fm.fields.size() == rows_.size();
    for (int i = 0; sameShape && i < rows_.size(); ++i) {
        const QVariant &v = fm.fields.at(i).second;
        const int kind = v.typeId() == QMetaType::Bool ? 1 : (v.typeId() == QMetaType::QStringList ? 2 : 0);
        sameShape = rows_.at(i).key == fm.fields.at(i).first && rows_.at(i).kind == kind;
    }
    if (!sameShape) {
        rebuild(fm);
        return;
    }
    loading_ = true;
    for (int i = 0; i < rows_.size(); ++i) {
        const Row &row = rows_.at(i);
        if (row.editor->hasFocus())
            continue;
        const QVariant &v = fm.fields.at(i).second;
        if (row.kind == 1)
            static_cast<QCheckBox *>(row.editor)->setChecked(v.toBool());
        else if (row.kind == 2)
            static_cast<QLineEdit *>(row.editor)->setText(listText(v.toStringList()));
        else
            static_cast<QLineEdit *>(row.editor)->setText(v.toString());
        if (row.kind != 1)
            static_cast<QLineEdit *>(row.editor)->setCursorPosition(0);
    }
    loading_ = false;
}

FrontMatter FrontMatterForm::frontMatter() const
{
    FrontMatter fm;
    for (const Row &row : rows_) {
        if (row.kind == 1)
            fm.set(row.key, static_cast<QCheckBox *>(row.editor)->isChecked());
        else if (row.kind == 2)
            fm.set(row.key, splitList(static_cast<QLineEdit *>(row.editor)->text()));
        else
            fm.set(row.key, static_cast<QLineEdit *>(row.editor)->text());
    }
    return fm;
}
