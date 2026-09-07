#include "svnswitchdialog.h"
#include "svnmanager.h"
#include "svntypes.h"
#include "svnuihelpers.h"

#include <KLocalizedString>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

SvnSwitchDialog::SvnSwitchDialog(const QString &wcPath, SvnManager *mgr,
                                 QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("SVN Switch"));
    setMinimumWidth(560);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    // -----------------------------------------------------------------------
    // Current URL (informational, read-only)
    // -----------------------------------------------------------------------
    auto *currentGroup  = new QGroupBox(i18n("Current URL"), this);
    auto *currentLayout = new QHBoxLayout(currentGroup);

    m_currentUrlLabel = new QLabel(currentGroup);
    m_currentUrlLabel->setWordWrap(true);
    m_currentUrlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    currentLayout->addWidget(m_currentUrlLabel);
    layout->addWidget(currentGroup);

    SvnInfo wcInfo;
    if (mgr) {
        wcInfo = mgr->info(wcPath);
        m_currentUrlLabel->setText(wcInfo.valid && !wcInfo.url.isEmpty()
                                   ? wcInfo.url
                                   : i18n("(unknown)"));
    }

    // -----------------------------------------------------------------------
    // Target URL
    // -----------------------------------------------------------------------
    auto *urlGroup  = new QGroupBox(i18n("Switch to URL"), this);
    auto *urlLayout = new QHBoxLayout(urlGroup);

    m_urlCombo = new QComboBox(urlGroup);
    m_urlCombo->setEditable(true);
    m_urlCombo->setInsertPolicy(QComboBox::NoInsert);
    m_urlCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_urlCombo->lineEdit()->setPlaceholderText(
        i18n("e.g. svn+ssh://host/repo/branches/my-branch"));
    urlLayout->addWidget(m_urlCombo, 1);
    m_urlBrowse = new QPushButton(
        QIcon::fromTheme(QStringLiteral("folder-remote")),
        i18n("Browse..."), urlGroup);
    connect(m_urlBrowse, &QPushButton::clicked, this, [this, mgr, wcInfo]() {
        const QString url = SvnUi::pickRepoUrl(
            m_urlCombo->currentText().trimmed(), wcInfo, mgr, this);
        if (!url.isEmpty())
            m_urlCombo->setCurrentText(url);
    });
    urlLayout->addWidget(m_urlBrowse);
    layout->addWidget(urlGroup);

    const QStringList urls = SvnUi::loadHistory(QStringLiteral("SwitchHistory"));
    for (const QString &u : urls)
        m_urlCombo->addItem(u, u);
    m_urlCombo->setCurrentIndex(-1);
    m_urlCombo->lineEdit()->clear();

    // -----------------------------------------------------------------------
    // Revision
    // -----------------------------------------------------------------------
    auto *revGroup  = new QGroupBox(i18n("Revision"), this);
    auto *revLayout = new QHBoxLayout(revGroup);

    m_headRadio = new QRadioButton(i18n("HEAD (latest)"), revGroup);
    m_headRadio->setChecked(true);
    revLayout->addWidget(m_headRadio);
    revLayout->addSpacing(16);

    m_revRadio  = new QRadioButton(i18n("Revision:"), revGroup);
    revLayout->addWidget(m_revRadio);

    m_revSpinBox = new QSpinBox(revGroup);
    m_revSpinBox->setRange(1, 9'999'999);
    m_revSpinBox->setValue(1);
    m_revSpinBox->setEnabled(false);
    m_revSpinBox->setMinimumWidth(90);
    revLayout->addWidget(m_revSpinBox);
    revLayout->addStretch();
    layout->addWidget(revGroup);

    // -----------------------------------------------------------------------
    // Depth
    // -----------------------------------------------------------------------
    auto *depthGroup  = new QGroupBox(i18n("Depth"), this);
    auto *depthLayout = new QHBoxLayout(depthGroup);

    m_depthCombo = new QComboBox(depthGroup);
    m_depthCombo->addItem(i18n("Fully recursive"),    QStringLiteral("infinity"));
    m_depthCombo->addItem(i18n("Immediate children"), QStringLiteral("immediates"));
    m_depthCombo->addItem(i18n("Only file children"), QStringLiteral("files"));
    m_depthCombo->addItem(i18n("Only this item"),     QStringLiteral("empty"));
    depthLayout->addWidget(m_depthCombo);
    depthLayout->addStretch();
    layout->addWidget(depthGroup);

    // -----------------------------------------------------------------------
    // Buttons
    // -----------------------------------------------------------------------
    auto *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_switchBtn = btnBox->button(QDialogButtonBox::Ok);
    m_switchBtn->setText(i18n("Switch"));
    m_switchBtn->setIcon(QIcon::fromTheme(QStringLiteral("vcs-branch")));
    m_switchBtn->setEnabled(false);
    layout->addWidget(btnBox);

    // -----------------------------------------------------------------------
    // Connections
    // -----------------------------------------------------------------------
    connect(m_revRadio,   &QRadioButton::toggled, m_revSpinBox, &QSpinBox::setEnabled);
    connect(m_revSpinBox, &QSpinBox::valueChanged, this, [this]() {
        m_revRadio->setChecked(true);
    });
    auto updateOkBtn = [this]() {
        m_switchBtn->setEnabled(!m_urlCombo->currentText().trimmed().isEmpty());
    };
    connect(m_urlCombo, &QComboBox::currentTextChanged, this, updateOkBtn);
    connect(btnBox, &QDialogButtonBox::accepted, this, &SvnSwitchDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SvnSwitchDialog::accept()
{
    saveUrlToHistory(targetUrl());
    QDialog::accept();
}

QString SvnSwitchDialog::targetUrl() const
{
    return m_urlCombo->currentText().trimmed();
}

QString SvnSwitchDialog::revision() const
{
    return m_headRadio->isChecked()
        ? QStringLiteral("HEAD")
        : QString::number(m_revSpinBox->value());
}

QString SvnSwitchDialog::depth() const
{
    return m_depthCombo->currentData().toString();
}

void SvnSwitchDialog::saveUrlToHistory(const QString &u)
{
    SvnUi::pushHistory(QStringLiteral("SwitchHistory"), u);
}
