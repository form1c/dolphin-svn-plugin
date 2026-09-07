#include "svnbranchtagdialog.h"
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
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

SvnBranchTagDialog::SvnBranchTagDialog(const QString &wcPath, SvnManager *mgr,
                                       QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("SVN Branch / Tag"));
    setMinimumWidth(580);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    // -----------------------------------------------------------------------
    // Source URL
    // -----------------------------------------------------------------------
    auto *srcGroup  = new QGroupBox(i18n("Source URL"), this);
    auto *srcLayout = new QHBoxLayout(srcGroup);

    m_srcEdit = new QLineEdit(srcGroup);
    m_srcEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    srcLayout->addWidget(m_srcEdit, 1);
    m_srcBrowse = new QPushButton(
        QIcon::fromTheme(QStringLiteral("folder-remote")),
        i18n("Browse..."), srcGroup);
    srcLayout->addWidget(m_srcBrowse);
    layout->addWidget(srcGroup);

    SvnInfo wcInfo;
    if (mgr) {
        wcInfo = mgr->info(wcPath);
        if (wcInfo.valid && !wcInfo.url.isEmpty())
            m_srcEdit->setText(wcInfo.url);
    }

    connect(m_srcBrowse, &QPushButton::clicked, this, [this, mgr, wcInfo]() {
        const QString url = SvnUi::pickRepoUrl(
            m_srcEdit->text().trimmed(), wcInfo, mgr, this);
        if (!url.isEmpty())
            m_srcEdit->setText(url);
    });

    // -----------------------------------------------------------------------
    // Destination URL
    // -----------------------------------------------------------------------
    auto *dstGroup  = new QGroupBox(i18n("Destination URL"), this);
    auto *dstLayout = new QHBoxLayout(dstGroup);

    m_dstCombo = new QComboBox(dstGroup);
    m_dstCombo->setEditable(true);
    m_dstCombo->setInsertPolicy(QComboBox::NoInsert);
    m_dstCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_dstCombo->lineEdit()->setPlaceholderText(
        i18n("e.g. svn+ssh://host/repo/branches/my-branch"));
    dstLayout->addWidget(m_dstCombo, 1);
    m_dstBrowse = new QPushButton(
        QIcon::fromTheme(QStringLiteral("folder-remote")),
        i18n("Browse..."), dstGroup);
    connect(m_dstBrowse, &QPushButton::clicked, this, [this, mgr, wcInfo]() {
        const QString url = SvnUi::pickRepoUrl(
            m_dstCombo->currentText().trimmed(), wcInfo, mgr, this);
        if (!url.isEmpty())
            m_dstCombo->setCurrentText(url);
    });
    dstLayout->addWidget(m_dstBrowse);
    layout->addWidget(dstGroup);

    const QStringList urls = SvnUi::loadHistory(QStringLiteral("BranchTagHistory"));
    for (const QString &u : urls)
        m_dstCombo->addItem(u, u);
    m_dstCombo->setCurrentIndex(-1);
    m_dstCombo->lineEdit()->clear();

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
    // Commit message
    // -----------------------------------------------------------------------
    auto *msgGroup  = new QGroupBox(i18n("Commit Message"), this);
    auto *msgLayout = new QVBoxLayout(msgGroup);

    m_msgEdit = new QPlainTextEdit(msgGroup);
    m_msgEdit->setPlaceholderText(i18n("Enter a commit message..."));
    m_msgEdit->setMinimumHeight(80);
    msgLayout->addWidget(m_msgEdit);
    layout->addWidget(msgGroup);

    // -----------------------------------------------------------------------
    // Buttons
    // -----------------------------------------------------------------------
    auto *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okBtn = btnBox->button(QDialogButtonBox::Ok);
    m_okBtn->setText(i18n("Create Branch / Tag"));
    m_okBtn->setIcon(QIcon::fromTheme(QStringLiteral("vcs-branch")));
    m_okBtn->setEnabled(false);
    layout->addWidget(btnBox);

    // -----------------------------------------------------------------------
    // Connections
    // -----------------------------------------------------------------------
    connect(m_revRadio,   &QRadioButton::toggled, m_revSpinBox, &QSpinBox::setEnabled);
    connect(m_revSpinBox, &QSpinBox::valueChanged, this, [this]() {
        m_revRadio->setChecked(true);
    });
    connect(m_srcEdit,  &QLineEdit::textChanged,        this, &SvnBranchTagDialog::updateOkButton);
    connect(m_dstCombo, &QComboBox::currentTextChanged, this, &SvnBranchTagDialog::updateOkButton);
    connect(m_msgEdit,  &QPlainTextEdit::textChanged,   this, &SvnBranchTagDialog::updateOkButton);
    connect(btnBox, &QDialogButtonBox::accepted, this, &SvnBranchTagDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateOkButton();
}

void SvnBranchTagDialog::accept()
{
    saveTargetToHistory(targetUrl());
    QDialog::accept();
}

void SvnBranchTagDialog::setSourceUrl(const QString &url)
{
    m_srcEdit->setText(url);
}

QString SvnBranchTagDialog::sourceUrl() const
{
    return m_srcEdit->text().trimmed();
}

QString SvnBranchTagDialog::targetUrl() const
{
    return m_dstCombo->currentText().trimmed();
}

QString SvnBranchTagDialog::revision() const
{
    return m_headRadio->isChecked()
        ? QStringLiteral("HEAD")
        : QString::number(m_revSpinBox->value());
}

QString SvnBranchTagDialog::message() const
{
    return m_msgEdit->toPlainText().trimmed();
}

void SvnBranchTagDialog::updateOkButton()
{
    const bool ok = !sourceUrl().isEmpty()
                 && !targetUrl().isEmpty()
                 && !message().isEmpty();
    m_okBtn->setEnabled(ok);
}

void SvnBranchTagDialog::saveTargetToHistory(const QString &u)
{
    SvnUi::pushHistory(QStringLiteral("BranchTagHistory"), u);
}
