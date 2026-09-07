#include "svnsettingsdialog.h"
#include "svnsettings.h"

#include <KLocalizedString>

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

// Injected by the build (top-level CMakeLists.txt project() version). The
// fallback only applies to a build that does not set it (e.g. an IDE indexer).
#ifndef DOLPHIN_SVN_PLUGIN_VERSION
#define DOLPHIN_SVN_PLUGIN_VERSION "unknown"
#endif

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

SvnSettingsDialog::SvnSettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Settings"));
    setMinimumWidth(520);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    // -----------------------------------------------------------------------
    // SVN Binary
    // -----------------------------------------------------------------------
    auto *svnGroup  = new QGroupBox(i18n("SVN Binary"), this);
    auto *svnLayout = new QVBoxLayout(svnGroup);

    auto *svnRow = new QHBoxLayout;
    svnRow->addWidget(new QLabel(i18n("Path:"), svnGroup));
    m_svnBinaryEdit = new QLineEdit(SvnSettings::svnBinary(), svnGroup);
    svnRow->addWidget(m_svnBinaryEdit, 1);
    auto *svnBrowseBtn = new QPushButton(i18n("Browse..."), svnGroup);
    svnRow->addWidget(svnBrowseBtn);
    auto *svnCheckBtn  = new QPushButton(i18n("Check"), svnGroup);
    svnRow->addWidget(svnCheckBtn);
    svnLayout->addLayout(svnRow);

    m_svnVersionLabel = new QLabel(svnGroup);
    m_svnVersionLabel->setContentsMargins(4, 0, 0, 0);
    svnLayout->addWidget(m_svnVersionLabel);

    layout->addWidget(svnGroup);

    // -----------------------------------------------------------------------
    // Diff Tool  –  one radio per auto-detected tool + "Custom"
    // -----------------------------------------------------------------------
    auto *diffGroup  = new QGroupBox(i18n("Diff Tool"), this);
    auto *diffLayout = new QVBoxLayout(diffGroup);

    // Mutual exclusion across all radios (auto + custom).
    auto *btnGroup = new QButtonGroup(diffGroup);

    // Discover known tools.
    const QStringList knownTools = {
        QStringLiteral("angscheidrdiffer"),
        QStringLiteral("meld"),
        QStringLiteral("kdiff3"),
        QStringLiteral("kompare"),
    };
    for (const QString &name : knownTools) {
        const QString path = QStandardPaths::findExecutable(name);
        if (path.isEmpty()) continue;

        auto *radio = new QRadioButton(
            QStringLiteral("%1  (%2)").arg(name, path), diffGroup);
        btnGroup->addButton(radio);
        diffLayout->addWidget(radio);
        m_toolRadios.append({radio, path});
    }

    if (m_toolRadios.isEmpty()) {
        auto *noneLabel = new QLabel(
            i18n("⚠ No tool found. Please install Meld, KDiff3 or Kompare."),
            diffGroup);
        noneLabel->setContentsMargins(4, 0, 0, 0);
        diffLayout->addWidget(noneLabel);
    }

    // Custom radio + line edit + browse button.
    m_diffCustomRadio = new QRadioButton(i18n("Custom:"), diffGroup);
    btnGroup->addButton(m_diffCustomRadio);
    diffLayout->addWidget(m_diffCustomRadio);

    auto *customRow = new QHBoxLayout;
    customRow->setContentsMargins(22, 0, 0, 0);
    m_diffCustomEdit = new QLineEdit(diffGroup);
    m_diffCustomEdit->setEnabled(false);
    customRow->addWidget(m_diffCustomEdit, 1);
    m_diffBrowseBtn = new QPushButton(i18n("Browse..."), diffGroup);
    m_diffBrowseBtn->setEnabled(false);
    customRow->addWidget(m_diffBrowseBtn);
    diffLayout->addLayout(customRow);

    layout->addWidget(diffGroup);

    // -----------------------------------------------------------------------
    // Advanced
    // -----------------------------------------------------------------------
    auto *advGroup  = new QGroupBox(i18n("Advanced"), this);
    auto *advLayout = new QVBoxLayout(advGroup);

    auto *timeoutRow = new QHBoxLayout;
    timeoutRow->addWidget(new QLabel(i18n("Process timeout:"), advGroup));
    m_timeoutSpinBox = new QSpinBox(advGroup);
    m_timeoutSpinBox->setRange(5, 120);
    m_timeoutSpinBox->setSuffix(i18n(" s"));
    m_timeoutSpinBox->setValue(SvnSettings::processSyncTimeout());
    m_timeoutSpinBox->setToolTip(
        i18n("Timeout for synchronous SVN operations (status, log, info, diff)."));
    timeoutRow->addWidget(m_timeoutSpinBox);
    timeoutRow->addStretch();
    advLayout->addLayout(timeoutRow);

    auto *tempRow = new QHBoxLayout;
    tempRow->addWidget(new QLabel(i18n("Temp directory:"), advGroup));
    m_tempDirEdit = new QLineEdit(advGroup);
    m_tempDirEdit->setPlaceholderText(
        i18n("Leave empty to use system default (%1)", QDir::tempPath()));
    m_tempDirEdit->setText(SvnSettings::tempDirectory());
    tempRow->addWidget(m_tempDirEdit, 1);
    auto *tempBrowseBtn = new QPushButton(i18n("Browse..."), advGroup);
    tempRow->addWidget(tempBrowseBtn);
    advLayout->addLayout(tempRow);

    auto *ttlRow = new QHBoxLayout;
    ttlRow->addWidget(new QLabel(i18n("Overlay cache TTL:"), advGroup));
    m_overlayTtlSpinBox = new QSpinBox(advGroup);
    m_overlayTtlSpinBox->setRange(5, 300);
    m_overlayTtlSpinBox->setSuffix(i18n(" s"));
    m_overlayTtlSpinBox->setValue(SvnSettings::overlayTtlSeconds());
    m_overlayTtlSpinBox->setToolTip(
        i18n("How long SVN status is cached for file overlay icons (seconds). "
             "Lower values = more up-to-date overlays but more SVN queries."));
    ttlRow->addWidget(m_overlayTtlSpinBox);
    ttlRow->addStretch();
    advLayout->addLayout(ttlRow);

    layout->addWidget(advGroup);

    // -----------------------------------------------------------------------
    // Dialog Defaults
    // -----------------------------------------------------------------------
    auto *defaultsGroup  = new QGroupBox(i18n("Dialog Defaults"), this);
    auto *defaultsLayout = new QVBoxLayout(defaultsGroup);

    m_hideUnversioned = new QCheckBox(
        i18n("Hide unversioned items by default (Commit dialog)"), defaultsGroup);
    m_hideUnversioned->setChecked(SvnSettings::hideUnversioned());
    defaultsLayout->addWidget(m_hideUnversioned);

    m_showUnversioned = new QCheckBox(
        i18n("Show unversioned files by default (Check for Modifications dialog)"),
        defaultsGroup);
    m_showUnversioned->setChecked(SvnSettings::showUnversioned());
    defaultsLayout->addWidget(m_showUnversioned);

    layout->addWidget(defaultsGroup);

    // -----------------------------------------------------------------------
    // About
    // -----------------------------------------------------------------------
    auto *aboutGroup  = new QGroupBox(i18n("About"), this);
    auto *aboutLayout = new QVBoxLayout(aboutGroup);
    auto *versionLabel = new QLabel(
        i18n("Dolphin SVN Plugin, version %1",
             QStringLiteral(DOLPHIN_SVN_PLUGIN_VERSION)),
        aboutGroup);
    versionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    aboutLayout->addWidget(versionLabel);
    auto *creditLabel = new QLabel(
        i18n("Built with Qt and the KDE Frameworks (LGPL)."), aboutGroup);
    aboutLayout->addWidget(creditLabel);
    layout->addWidget(aboutGroup);

    // -----------------------------------------------------------------------
    // Load current settings into UI
    // -----------------------------------------------------------------------
    const QString savedTool = SvnSettings::preferredDiffTool();

    // Check whether the saved path matches one of the auto-detected radios.
    bool matched = false;
    for (const ToolRadio &tr : std::as_const(m_toolRadios)) {
        if (tr.path == savedTool) {
            tr.radio->setChecked(true);
            matched = true;
            break;
        }
    }

    if (!matched) {
        if (!savedTool.isEmpty()) {
            // Non-empty path not in the auto-detect list → Custom.
            m_diffCustomRadio->setChecked(true);
            m_diffCustomEdit->setText(savedTool);
            m_diffCustomEdit->setEnabled(true);
            m_diffBrowseBtn->setEnabled(true);
        } else {
            // No saved preference → select first found tool, or Custom.
            if (!m_toolRadios.isEmpty())
                m_toolRadios.first().radio->setChecked(true);
            else
                m_diffCustomRadio->setChecked(true);
        }
    }

    checkSvnBinary();

    // -----------------------------------------------------------------------
    // Buttons
    // -----------------------------------------------------------------------
    auto *btnRow = new QHBoxLayout;
    auto *defaultsBtn = new QPushButton(i18n("Restore Defaults"), this);
    btnRow->addWidget(defaultsBtn);
    btnRow->addStretch();
    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btnRow->addWidget(btnBox);
    layout->addLayout(btnRow);

    // -----------------------------------------------------------------------
    // Connections
    // -----------------------------------------------------------------------
    connect(svnCheckBtn,  &QPushButton::clicked, this, &SvnSettingsDialog::checkSvnBinary);
    connect(svnBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString p = QFileDialog::getOpenFileName(
            this, i18n("Select SVN binary"), QStringLiteral("/usr/bin"));
        if (!p.isEmpty()) {
            m_svnBinaryEdit->setText(p);
            checkSvnBinary();
        }
    });
    connect(m_diffCustomRadio, &QRadioButton::toggled, this, [this](bool on) {
        m_diffCustomEdit->setEnabled(on);
        m_diffBrowseBtn->setEnabled(on);
    });
    connect(m_diffBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString p = QFileDialog::getOpenFileName(
            this, i18n("Select diff tool"), QStringLiteral("/usr/bin"));
        if (!p.isEmpty())
            m_diffCustomEdit->setText(p);
    });
    connect(tempBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString d = QFileDialog::getExistingDirectory(
            this, i18n("Select temp directory"), m_tempDirEdit->text());
        if (!d.isEmpty())
            m_tempDirEdit->setText(d);
    });
    connect(defaultsBtn, &QPushButton::clicked, this, &SvnSettingsDialog::resetToDefaults);
    connect(btnBox, &QDialogButtonBox::accepted, this, &SvnSettingsDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

// ---------------------------------------------------------------------------
// Save on accept
// ---------------------------------------------------------------------------

void SvnSettingsDialog::accept()
{
    SvnSettings::setSvnBinary(m_svnBinaryEdit->text().trimmed());

    // Determine selected diff tool path.
    QString diffTool;
    for (const ToolRadio &tr : std::as_const(m_toolRadios)) {
        if (tr.radio->isChecked()) {
            diffTool = tr.path;
            break;
        }
    }
    if (m_diffCustomRadio->isChecked())
        diffTool = m_diffCustomEdit->text().trimmed();

    SvnSettings::setPreferredDiffTool(diffTool);
    SvnSettings::setProcessSyncTimeout(m_timeoutSpinBox->value());
    SvnSettings::setTempDirectory(m_tempDirEdit->text().trimmed());
    SvnSettings::setOverlayTtlSeconds(m_overlayTtlSpinBox->value());
    SvnSettings::setHideUnversioned(m_hideUnversioned->isChecked());
    SvnSettings::setShowUnversioned(m_showUnversioned->isChecked());
    QDialog::accept();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void SvnSettingsDialog::checkSvnBinary()
{
    const QString bin = m_svnBinaryEdit->text().trimmed();
    if (bin.isEmpty()) {
        m_svnVersionLabel->setText(i18n("No path specified."));
        return;
    }
    QProcess proc;
    proc.start(bin, {QStringLiteral("--version"), QStringLiteral("--quiet")});
    if (!proc.waitForFinished(3000) || proc.exitCode() != 0) {
        m_svnVersionLabel->setText(i18n("⚠ Program not found or error."));
        return;
    }
    const QString version = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    m_svnVersionLabel->setText(i18n("✓ Version: %1", version));
}

void SvnSettingsDialog::resetToDefaults()
{
    m_svnBinaryEdit->setText(QStringLiteral("/usr/bin/svn"));
    m_diffCustomEdit->clear();
    // Select first auto-detected tool, or Custom if none found.
    if (!m_toolRadios.isEmpty())
        m_toolRadios.first().radio->setChecked(true);
    else
        m_diffCustomRadio->setChecked(true);
    m_timeoutSpinBox->setValue(30);
    m_tempDirEdit->clear();
    m_overlayTtlSpinBox->setValue(15);
    m_hideUnversioned->setChecked(false);
    m_showUnversioned->setChecked(false);
    checkSvnBinary();
}
