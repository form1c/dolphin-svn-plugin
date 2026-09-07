#include "svnapplypatchdialog.h"
#include "svnmanager.h"
#include "svnuihelpers.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SvnApplyPatchDialog::SvnApplyPatchDialog(const QString &wcPath,
                                          const QString &initialPatchFile,
                                          SvnManager *mgr, QWidget *parent)
    : QDialog(parent)
    , m_wcPath(wcPath)
    , m_mgr(mgr)
{
    setWindowTitle(i18n("SVN: Apply Patch"));
    setMinimumWidth(560);

    auto *layout = new QVBoxLayout(this);
    auto *form   = new QFormLayout;

    auto *fileRow = new QHBoxLayout;
    m_fileEdit = new QLineEdit(initialPatchFile, this);
    fileRow->addWidget(m_fileEdit, 1);
    auto *browseBtn = new QPushButton(i18n("Browse…"), this);
    fileRow->addWidget(browseBtn);
    form->addRow(i18n("Patch file:"), fileRow);

    auto *targetLabel = new QLabel(wcPath, this);
    targetLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(i18n("Apply to:"), targetLabel);

    // Paths in an svn patch are relative to the REPOSITORY root (a WC of
    // ^/trunk produces "trunk/src/x.cpp"). How many of those components must be
    // stripped is determined by the backend from patch + target WC; the spin box
    // allows a manual correction (foreign patches).
    auto *stripRow = new QHBoxLayout;
    m_stripSpin = new QSpinBox(this);
    m_stripSpin->setRange(0, 20);
    stripRow->addWidget(m_stripSpin);
    m_stripHint = new QLabel(this);
    m_stripHint->setEnabled(false);
    stripRow->addWidget(m_stripHint, 1);
    form->addRow(i18n("Strip path components:"), stripRow);
    layout->addLayout(form);

    m_reverseBox = new QCheckBox(
        i18n("Reverse patch (undo the changes it contains)"), this);
    layout->addWidget(m_reverseBox);

    auto *hint = new QLabel(
        i18n("Hunks that do not fit are rejected and written to "
             "'<file>.svnpatch.rej' — the rest of the patch is still applied. "
             "Use the preview first if you are unsure."), this);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    layout->addWidget(hint);

    // --- Buttons ---
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_dryRunBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-preview")),
                                   i18n("Preview (Dry Run)"), this);
    buttons->addButton(m_dryRunBtn, QDialogButtonBox::ActionRole);
    m_applyBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")),
                                  i18n("Apply Patch"), this);
    m_applyBtn->setDefault(true);
    buttons->addButton(m_applyBtn, QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &SvnApplyPatchDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        const QString start = m_fileEdit->text().trimmed().isEmpty()
            ? m_wcPath : m_fileEdit->text();
        const QString file = QFileDialog::getOpenFileName(
            this, i18n("Select Patch File"), start,
            i18n("Patch files (*.patch *.diff);;All files (*)"));
        if (!file.isEmpty())
            m_fileEdit->setText(file);
    });
    connect(m_fileEdit, &QLineEdit::textChanged, this, [this]() {
        updateState();
        redetectStrip();
    });

    // Dry run: the same arguments as the real run, only with --dry-run.
    // The output appears in the SvnProgressDialog; the dialog stays open so the
    // user can decide afterwards.
    connect(m_dryRunBtn, &QPushButton::clicked, this, [this]() {
        const QString file = patchFile();
        const bool rev = reverse();
        const int str = strip();
        SvnUi::runWithProgress(
            m_mgr, i18n("SVN: Apply Patch (Preview)"), this,
            [this, file, rev, str]() {
                m_mgr->applyPatchAsync(m_wcPath, file, /*dryRun=*/true, rev, str);
            });
    });

    updateState();
    redetectStrip();
}

void SvnApplyPatchDialog::updateState()
{
    const bool ok = !patchFile().isEmpty();
    m_applyBtn->setEnabled(ok);
    m_dryRunBtn->setEnabled(ok);
}

void SvnApplyPatchDialog::redetectStrip()
{
    const QString file = patchFile();
    if (file.isEmpty() || !QFileInfo::exists(file)) {
        m_stripHint->clear();
        return;
    }
    const int detected = m_mgr->detectPatchStripLevelSync(file, m_wcPath);
    m_stripSpin->setValue(detected);
    m_stripHint->setText(detected == 0
        ? i18n("auto-detected: the patch paths already match this working copy")
        : i18np("auto-detected: %1 leading path component is removed",
                "auto-detected: %1 leading path components are removed",
                detected));
}

int SvnApplyPatchDialog::strip() const
{
    return m_stripSpin->value();
}

QString SvnApplyPatchDialog::patchFile() const
{
    return m_fileEdit->text().trimmed();
}

bool SvnApplyPatchDialog::reverse() const
{
    return m_reverseBox->isChecked();
}

void SvnApplyPatchDialog::accept()
{
    const QString file = patchFile();
    if (file.isEmpty())
        return;
    if (!QFileInfo::exists(file)) {
        QMessageBox::warning(this, i18n("SVN: Apply Patch"),
                             i18n("The patch file does not exist:\n%1", file));
        return;
    }
    QDialog::accept();
}
