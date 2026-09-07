#include "svnrelocatedialog.h"
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
#include <QVBoxLayout>

SvnRelocateDialog::SvnRelocateDialog(const QString &wcPath, SvnManager *mgr,
                                     QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("SVN Relocate"));
    setMinimumWidth(560);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    // -----------------------------------------------------------------------
    // Warning
    // -----------------------------------------------------------------------
    auto *warnLabel = new QLabel(
        i18n("Only use this if the repository itself has moved (e.g. a server "
             "or DNS name change) and this working copy still points at the "
             "same location inside it. Nothing is fetched from the repository — "
             "only the working copy's URL metadata is rewritten. To point this "
             "working copy at a <i>different</i> location in the same "
             "repository, use Switch instead."),
        this);
    warnLabel->setWordWrap(true);
    layout->addWidget(warnLabel);

    // -----------------------------------------------------------------------
    // Current URL (informational, read-only)
    // -----------------------------------------------------------------------
    auto *currentGroup  = new QGroupBox(i18n("Current URL"), this);
    auto *currentLayout = new QHBoxLayout(currentGroup);

    auto *currentUrlLabel = new QLabel(currentGroup);
    currentUrlLabel->setWordWrap(true);
    currentUrlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    currentLayout->addWidget(currentUrlLabel);
    layout->addWidget(currentGroup);

    SvnInfo wcInfo;
    if (mgr) {
        wcInfo = mgr->info(wcPath);
        m_fromUrl = wcInfo.valid ? wcInfo.url : QString();
        currentUrlLabel->setText(m_fromUrl.isEmpty() ? i18n("(unknown)") : m_fromUrl);
    }

    // -----------------------------------------------------------------------
    // New (repository-root) URL
    // -----------------------------------------------------------------------
    auto *urlGroup  = new QGroupBox(i18n("New URL"), this);
    auto *urlLayout = new QHBoxLayout(urlGroup);

    m_urlCombo = new QComboBox(urlGroup);
    m_urlCombo->setEditable(true);
    m_urlCombo->setInsertPolicy(QComboBox::NoInsert);
    m_urlCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_urlCombo->lineEdit()->setPlaceholderText(
        i18n("e.g. svn+ssh://new-host/repo"));
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

    const QStringList urls = SvnUi::loadHistory(QStringLiteral("RelocateHistory"));
    for (const QString &u : urls)
        m_urlCombo->addItem(u, u);
    m_urlCombo->setCurrentIndex(-1);
    m_urlCombo->lineEdit()->clear();

    // -----------------------------------------------------------------------
    // Buttons
    // -----------------------------------------------------------------------
    auto *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_relocateBtn = btnBox->button(QDialogButtonBox::Ok);
    m_relocateBtn->setText(i18n("Relocate"));
    m_relocateBtn->setIcon(QIcon::fromTheme(QStringLiteral("network-server")));
    m_relocateBtn->setEnabled(false);
    layout->addWidget(btnBox);

    // -----------------------------------------------------------------------
    // Connections
    // -----------------------------------------------------------------------
    auto updateOkBtn = [this]() {
        m_relocateBtn->setEnabled(!m_urlCombo->currentText().trimmed().isEmpty()
                                  && !m_fromUrl.isEmpty());
    };
    connect(m_urlCombo, &QComboBox::currentTextChanged, this, updateOkBtn);
    connect(btnBox, &QDialogButtonBox::accepted, this, &SvnRelocateDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SvnRelocateDialog::accept()
{
    SvnUi::pushHistory(QStringLiteral("RelocateHistory"), toUrl());
    QDialog::accept();
}

QString SvnRelocateDialog::fromUrl() const
{
    return m_fromUrl;
}

QString SvnRelocateDialog::toUrl() const
{
    return m_urlCombo->currentText().trimmed();
}
