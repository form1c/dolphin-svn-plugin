#include "svnpropertiesdialog.h"
#include "../svn/svnmanager.h"

#include <KLocalizedString>

#include <QCompleter>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringListModel>
#include <QTableWidget>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Local sub-dialog for adding/editing a single property
// ---------------------------------------------------------------------------

class SvnPropertyEditDialog : public QDialog
{
public:
    SvnPropertyEditDialog(const QString &name, const QString &value,
                           bool nameEditable, QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(nameEditable ? i18n("Add Property") : i18n("Edit Property"));
        setMinimumWidth(420);

        auto *layout = new QVBoxLayout(this);

        layout->addWidget(new QLabel(i18n("Property name:"), this));
        m_nameEdit = new QLineEdit(name, this);
        m_nameEdit->setReadOnly(!nameEditable);
        if (nameEditable) {
            static const QStringList common = {
                QStringLiteral("svn:ignore"),
                QStringLiteral("svn:mime-type"),
                QStringLiteral("svn:keywords"),
                QStringLiteral("svn:eol-style"),
                QStringLiteral("svn:executable"),
                QStringLiteral("svn:needs-lock"),
                QStringLiteral("svn:externals"),
                QStringLiteral("svn:special"),
            };
            auto *comp = new QCompleter(common, m_nameEdit);
            comp->setCaseSensitivity(Qt::CaseInsensitive);
            m_nameEdit->setCompleter(comp);
        }
        layout->addWidget(m_nameEdit);

        layout->addWidget(new QLabel(i18n("Value:"), this));
        m_valueEdit = new QPlainTextEdit(value, this);
        m_valueEdit->setMinimumHeight(120);
        m_valueEdit->setFocus();
        layout->addWidget(m_valueEdit);

        auto *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        m_okBtn = buttons->button(QDialogButtonBox::Ok);
        m_okBtn->setEnabled(!name.isEmpty());
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);

        if (nameEditable) {
            connect(m_nameEdit, &QLineEdit::textChanged, this, [this](const QString &t) {
                m_okBtn->setEnabled(!t.trimmed().isEmpty());
            });
        }
    }

    QString propName()  const { return m_nameEdit->text().trimmed(); }
    QString propValue() const { return m_valueEdit->toPlainText(); }

private:
    QLineEdit      *m_nameEdit  = nullptr;
    QPlainTextEdit *m_valueEdit = nullptr;
    QPushButton    *m_okBtn     = nullptr;
};

// ---------------------------------------------------------------------------
// Helper: collapse multiline values for table display
// ---------------------------------------------------------------------------

static QString toDisplayValue(const QString &v)
{
    QString d = v.trimmed();
    d.replace(QLatin1Char('\n'), QStringLiteral("  ↵  "));  // ↵
    return d;
}

// ---------------------------------------------------------------------------
// SvnPropertiesDialog
// ---------------------------------------------------------------------------

SvnPropertiesDialog::SvnPropertiesDialog(const QString &path, SvnManager *mgr, QWidget *parent)
    : QDialog(parent)
    , m_path(path)
    , m_mgr(mgr)
{
    setWindowTitle(i18n("SVN Properties – %1", QFileInfo(path).fileName()));
    setMinimumSize(540, 380);

    auto *layout = new QVBoxLayout(this);

    // Table
    m_table = new QTableWidget(0, 2, this);
    m_table->setHorizontalHeaderLabels({i18n("Property"), i18n("Value")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    layout->addWidget(m_table);

    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &SvnPropertiesDialog::updateButtons);
    connect(m_table, &QTableWidget::itemDoubleClicked,
            this, [this](QTableWidgetItem *) { editProperty(); });

    // Action buttons
    auto *btnRow    = new QHBoxLayout;
    auto *addBtn    = new QPushButton(
        QIcon::fromTheme(QStringLiteral("list-add")),    i18n("Add..."), this);
    m_editBtn       = new QPushButton(
        QIcon::fromTheme(QStringLiteral("document-edit")), i18n("Edit..."), this);
    m_deleteBtn     = new QPushButton(
        QIcon::fromTheme(QStringLiteral("list-remove")), i18n("Delete"), this);
    m_editBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);

    connect(addBtn,      &QPushButton::clicked, this, &SvnPropertiesDialog::addProperty);
    connect(m_editBtn,   &QPushButton::clicked, this, &SvnPropertiesDialog::editProperty);
    connect(m_deleteBtn, &QPushButton::clicked, this, &SvnPropertiesDialog::deleteProperty);

    btnRow->addWidget(addBtn);
    btnRow->addWidget(m_editBtn);
    btnRow->addWidget(m_deleteBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    // OK / Cancel
    auto *dlgBtns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(dlgBtns, &QDialogButtonBox::accepted, this, &SvnPropertiesDialog::accept);
    connect(dlgBtns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(dlgBtns);

    loadProperties();
}

void SvnPropertiesDialog::loadProperties()
{
    m_original = m_mgr->propListSync(m_path);
    m_props    = m_original;
    refreshTable();
}

void SvnPropertiesDialog::refreshTable()
{
    m_table->setRowCount(0);
    for (auto it = m_props.cbegin(); it != m_props.cend(); ++it) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(it.key()));
        m_table->setItem(row, 1, new QTableWidgetItem(toDisplayValue(it.value())));
    }
    updateButtons();
}

void SvnPropertiesDialog::updateButtons()
{
    const bool sel = !m_table->selectedItems().isEmpty();
    m_editBtn->setEnabled(sel);
    m_deleteBtn->setEnabled(sel);
}

void SvnPropertiesDialog::addProperty()
{
    SvnPropertyEditDialog dlg(QString(), QString(), true, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const QString name = dlg.propName();
    if (name.isEmpty()) return;
    m_props[name] = dlg.propValue();
    refreshTable();
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (m_table->item(r, 0)->text() == name) {
            m_table->selectRow(r);
            break;
        }
    }
}

void SvnPropertiesDialog::editProperty()
{
    const int row = m_table->currentRow();
    if (row < 0) return;
    const QString name  = m_table->item(row, 0)->text();
    const QString value = m_props.value(name);
    SvnPropertyEditDialog dlg(name, value, false, this);
    if (dlg.exec() != QDialog::Accepted) return;
    m_props[name] = dlg.propValue();
    m_table->item(row, 1)->setText(toDisplayValue(dlg.propValue()));
}

void SvnPropertiesDialog::deleteProperty()
{
    const int row = m_table->currentRow();
    if (row < 0) return;
    m_props.remove(m_table->item(row, 0)->text());
    m_table->removeRow(row);
    updateButtons();
}

void SvnPropertiesDialog::accept()
{
    QStringList errors;

    // Delete removed properties
    for (auto it = m_original.cbegin(); it != m_original.cend(); ++it) {
        if (!m_props.contains(it.key())) {
            if (!m_mgr->propDelSync(m_path, it.key()))
                errors << i18n("Failed to delete '%1'", it.key());
        }
    }

    // Set new / modified properties
    for (auto it = m_props.cbegin(); it != m_props.cend(); ++it) {
        if (!m_original.contains(it.key()) || m_original.value(it.key()) != it.value()) {
            if (!m_mgr->propSetSync(m_path, it.key(), it.value()))
                errors << i18n("Failed to set '%1'", it.key());
        }
    }

    if (!errors.isEmpty()) {
        QMessageBox::warning(this, i18n("SVN Properties"),
                             i18n("Some operations failed:\n%1",
                                  errors.join(QLatin1Char('\n'))));
        loadProperties();
        return;
    }

    QDialog::accept();
}
