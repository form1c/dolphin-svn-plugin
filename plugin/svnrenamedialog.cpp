#include "svnrenamedialog.h"

#include <KLocalizedString>

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

SvnRenameDialog::SvnRenameDialog(const QString &srcPath, QWidget *parent)
    : QDialog(parent)
    , m_srcPath(srcPath)
    , m_targetDir(QFileInfo(srcPath).absolutePath())
{
    const QFileInfo fi(srcPath);
    setWindowTitle(i18n("Rename / Move – %1", fi.fileName()));
    setMinimumWidth(520);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    // -----------------------------------------------------------------------
    // Source label
    // -----------------------------------------------------------------------
    auto *fromLabel = new QLabel(
        i18n("From: <b>%1</b>", QDir::toNativeSeparators(srcPath)), this);
    fromLabel->setWordWrap(true);
    layout->addWidget(fromLabel);

    // -----------------------------------------------------------------------
    // New name group
    // -----------------------------------------------------------------------
    auto *nameGroup  = new QGroupBox(i18n("New name"), this);
    auto *nameLayout = new QVBoxLayout(nameGroup);

    m_nameEdit = new QLineEdit(fi.fileName(), nameGroup);
    nameLayout->addWidget(m_nameEdit);

    m_previewLabel = new QLabel(nameGroup);
    m_previewLabel->setWordWrap(true);
    // Render in muted colour for the "preview" feel.
    QPalette pal = m_previewLabel->palette();
    pal.setColor(QPalette::WindowText,
                 pal.color(QPalette::Disabled, QPalette::WindowText));
    m_previewLabel->setPalette(pal);
    nameLayout->addWidget(m_previewLabel);

    layout->addWidget(nameGroup);

    // -----------------------------------------------------------------------
    // Target folder row
    // -----------------------------------------------------------------------
    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(new QLabel(i18n("Target folder:"), this));
    m_targetDirLabel = new QLabel(QDir::toNativeSeparators(m_targetDir), this);
    m_targetDirLabel->setWordWrap(true);
    m_targetDirLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    folderRow->addWidget(m_targetDirLabel, 1);
    auto *changeFolderBtn = new QPushButton(i18n("Change folder..."), this);
    folderRow->addWidget(changeFolderBtn);
    layout->addLayout(folderRow);

    layout->addStretch();

    // -----------------------------------------------------------------------
    // Buttons
    // -----------------------------------------------------------------------
    auto *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okBtn = btnBox->button(QDialogButtonBox::Ok);
    m_okBtn->setText(i18n("Rename"));
    m_okBtn->setEnabled(false);
    layout->addWidget(btnBox);

    // -----------------------------------------------------------------------
    // Connections
    // -----------------------------------------------------------------------
    connect(m_nameEdit, &QLineEdit::textChanged,
            this, [this](const QString &) { updateState(); });
    connect(changeFolderBtn, &QPushButton::clicked, this, [this]() {
        const QString d = QFileDialog::getExistingDirectory(
            this, i18n("Select target folder"), m_targetDir);
        if (d.isEmpty()) return;
        m_targetDir = d;
        m_targetDirLabel->setText(QDir::toNativeSeparators(d));
        updateState();
    });
    connect(btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // -----------------------------------------------------------------------
    // Initial state + selection
    // -----------------------------------------------------------------------
    updateState();

    // Select just the base name (without extension) for quick typing.
    m_nameEdit->setSelection(0, fi.completeBaseName().length());
}

// ---------------------------------------------------------------------------
// Public accessor
// ---------------------------------------------------------------------------

QString SvnRenameDialog::destPath() const
{
    return m_targetDir + QLatin1Char('/') + m_nameEdit->text().trimmed();
}

// ---------------------------------------------------------------------------
// Update preview + OK button
// ---------------------------------------------------------------------------

void SvnRenameDialog::updateState()
{
    const QString newName = m_nameEdit->text().trimmed();
    const QString dest    = m_targetDir + QLatin1Char('/') + newName;

    m_previewLabel->setText(
        newName.isEmpty() ? QString() : QDir::toNativeSeparators(dest));

    // Valid when: name non-empty, no path separators (use folder picker for that),
    // and the destination differs from the source.
    const bool valid = !newName.isEmpty()
                    && !newName.contains(QLatin1Char('/'))
                    && !newName.contains(QLatin1Char('\\'))
                    && dest != m_srcPath;
    m_okBtn->setEnabled(valid);

    // Label: "Rename" within same directory, "Move" when folder differs.
    const bool sameDir = (m_targetDir == QFileInfo(m_srcPath).absolutePath());
    m_okBtn->setText(sameDir && newName == QFileInfo(m_srcPath).fileName()
                         ? i18n("Rename")
                         : (sameDir ? i18n("Rename") : i18n("Move")));
}
