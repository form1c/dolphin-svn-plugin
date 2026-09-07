#include "svnexportdialog.h"

#include <KLocalizedString>

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

SvnExportDialog::SvnExportDialog(const QString &srcPath, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("SVN: Export"));
    setMinimumWidth(480);

    auto *layout = new QVBoxLayout(this);

    // ---- Source ----
    auto *srcGroup  = new QGroupBox(i18n("Source (working copy path or repository URL)"), this);
    auto *srcLayout = new QHBoxLayout(srcGroup);

    m_srcEdit = new QLineEdit(this);
    if (srcPath.isEmpty())
        m_srcEdit->setPlaceholderText(i18n("e.g. file:///repo/trunk  or  /path/to/working-copy"));
    else
        m_srcEdit->setText(srcPath);
    srcLayout->addWidget(m_srcEdit);

    // Browse button only makes sense for local paths
    auto *srcBrowseBtn = new QPushButton(i18n("Browse…"), this);
    srcBrowseBtn->setFixedWidth(80);
    connect(srcBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, i18n("Select Source Directory"), m_srcEdit->text());
        if (!dir.isEmpty())
            m_srcEdit->setText(dir);
    });
    srcLayout->addWidget(srcBrowseBtn);
    layout->addWidget(srcGroup);

    connect(m_srcEdit, &QLineEdit::textChanged, this, &SvnExportDialog::updateState);

    // ---- Destination ----
    auto *destGroup  = new QGroupBox(i18n("Export destination"), this);
    auto *destLayout = new QHBoxLayout(destGroup);

    m_destEdit = new QLineEdit(this);
    m_destEdit->setPlaceholderText(i18n("Select target directory…"));
    destLayout->addWidget(m_destEdit);

    auto *destBrowseBtn = new QPushButton(i18n("Browse…"), this);
    destBrowseBtn->setFixedWidth(80);
    connect(destBrowseBtn, &QPushButton::clicked, this, [this, srcPath]() {
        const QString start = m_destEdit->text().isEmpty()
            ? QFileInfo(srcPath.isEmpty() ? QDir::homePath() : srcPath).absolutePath()
            : m_destEdit->text();
        const QString dir = QFileDialog::getExistingDirectory(
            this, i18n("Choose Export Destination"), start);
        if (!dir.isEmpty())
            m_destEdit->setText(dir);
    });
    destLayout->addWidget(destBrowseBtn);
    layout->addWidget(destGroup);

    connect(m_destEdit, &QLineEdit::textChanged, this, &SvnExportDialog::updateState);

    // ---- Revision ----
    auto *revGroup  = new QGroupBox(i18n("Revision"), this);
    auto *revLayout = new QVBoxLayout(revGroup);

    m_headRadio = new QRadioButton(i18n("HEAD (latest revision)"), this);
    m_headRadio->setChecked(true);
    revLayout->addWidget(m_headRadio);

    auto *revRow = new QHBoxLayout;
    m_revRadio   = new QRadioButton(i18n("Specific revision:"), this);
    m_revSpinBox = new QSpinBox(this);
    m_revSpinBox->setRange(1, 9999999);
    m_revSpinBox->setValue(1);
    m_revSpinBox->setEnabled(false);
    revRow->addWidget(m_revRadio);
    revRow->addWidget(m_revSpinBox);
    revRow->addStretch();
    revLayout->addLayout(revRow);
    layout->addWidget(revGroup);

    connect(m_revRadio, &QRadioButton::toggled, m_revSpinBox, &QSpinBox::setEnabled);

    // ---- Buttons ----
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_exportBtn = new QPushButton(i18n("Export"), this);
    m_exportBtn->setEnabled(false);
    m_exportBtn->setDefault(true);
    buttons->addButton(m_exportBtn, QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &SvnExportDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void SvnExportDialog::updateState()
{
    const bool ok = !m_srcEdit->text().trimmed().isEmpty()
                 && !m_destEdit->text().trimmed().isEmpty();
    m_exportBtn->setEnabled(ok);
}

QString SvnExportDialog::srcPath() const
{
    return m_srcEdit->text().trimmed();
}

QString SvnExportDialog::destPath() const
{
    return m_destEdit->text().trimmed();
}

QString SvnExportDialog::revision() const
{
    if (m_revRadio->isChecked())
        return QString::number(m_revSpinBox->value());
    return QStringLiteral("HEAD");
}

bool SvnExportDialog::force() const
{
    return m_force;
}

void SvnExportDialog::accept()
{
    if (m_srcEdit->text().trimmed().isEmpty() || m_destEdit->text().trimmed().isEmpty())
        return;

    const QString dst = m_destEdit->text().trimmed();
    if (QDir(dst).exists()) {
        const int ret = QMessageBox::question(
            this,
            i18n("Directory already exists"),
            i18n("The directory\n%1\nalready exists. Existing files with the same name will be overwritten.\n\nContinue?", dst));
        if (ret != QMessageBox::Yes)
            return;
        m_force = true;
    } else {
        m_force = false;
    }

    QDialog::accept();
}
