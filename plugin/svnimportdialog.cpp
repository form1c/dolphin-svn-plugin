#include "svnimportdialog.h"
#include "svntypes.h"
#include "svnuihelpers.h"

#include <KLocalizedString>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

SvnImportDialog::SvnImportDialog(const QString &localDir, SvnManager *mgr,
                                 QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("SVN Import"));
    setMinimumWidth(560);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    // -----------------------------------------------------------------------
    // Hint
    // -----------------------------------------------------------------------
    auto *hintLabel = new QLabel(
        i18n("Imports the folder recursively into the repository in a single "
             "commit. The folder itself is <b>not</b> turned into a working "
             "copy — afterward, use Checkout to get a working copy of the "
             "imported location."),
        this);
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    // -----------------------------------------------------------------------
    // Source folder
    // -----------------------------------------------------------------------
    auto *srcGroup  = new QGroupBox(i18n("Source Folder"), this);
    auto *srcLayout = new QHBoxLayout(srcGroup);

    m_srcEdit = new QLineEdit(localDir, srcGroup);
    srcLayout->addWidget(m_srcEdit, 1);
    auto *srcBrowseBtn = new QPushButton(i18n("Browse..."), srcGroup);
    connect(srcBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString d = QFileDialog::getExistingDirectory(
            this, i18n("Select source folder"), m_srcEdit->text());
        if (!d.isEmpty())
            m_srcEdit->setText(d);
    });
    srcLayout->addWidget(srcBrowseBtn);
    layout->addWidget(srcGroup);

    // -----------------------------------------------------------------------
    // Target URL
    // -----------------------------------------------------------------------
    auto *urlGroup  = new QGroupBox(i18n("Target URL"), this);
    auto *urlLayout = new QHBoxLayout(urlGroup);

    m_urlCombo = new QComboBox(urlGroup);
    m_urlCombo->setEditable(true);
    m_urlCombo->setInsertPolicy(QComboBox::NoInsert);
    m_urlCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_urlCombo->lineEdit()->setPlaceholderText(
        i18n("e.g. svn+ssh://host/repo/trunk/new-folder"));
    urlLayout->addWidget(m_urlCombo, 1);
    m_urlBrowse = new QPushButton(
        QIcon::fromTheme(QStringLiteral("folder-remote")),
        i18n("Browse..."), urlGroup);
    connect(m_urlBrowse, &QPushButton::clicked, this, [this, mgr]() {
        const QString url = SvnUi::pickRepoUrl(
            m_urlCombo->currentText().trimmed(), SvnInfo(), mgr, this);
        if (!url.isEmpty())
            m_urlCombo->setCurrentText(url);
    });
    urlLayout->addWidget(m_urlBrowse);
    layout->addWidget(urlGroup);

    const QStringList urls = SvnUi::loadHistory(QStringLiteral("ImportHistory"));
    for (const QString &u : urls)
        m_urlCombo->addItem(u, u);
    m_urlCombo->setCurrentIndex(-1);
    m_urlCombo->lineEdit()->clear();

    // -----------------------------------------------------------------------
    // Commit message
    // -----------------------------------------------------------------------
    auto *msgGroup  = new QGroupBox(i18n("Commit Message"), this);
    auto *msgLayout = new QVBoxLayout(msgGroup);
    m_messageEdit = new QPlainTextEdit(msgGroup);
    m_messageEdit->setMinimumHeight(70);
    msgLayout->addWidget(m_messageEdit);
    layout->addWidget(msgGroup);

    // -----------------------------------------------------------------------
    // Buttons
    // -----------------------------------------------------------------------
    auto *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_importBtn = btnBox->button(QDialogButtonBox::Ok);
    m_importBtn->setText(i18n("Import"));
    m_importBtn->setIcon(QIcon::fromTheme(QStringLiteral("document-import")));
    m_importBtn->setEnabled(false);
    layout->addWidget(btnBox);

    // -----------------------------------------------------------------------
    // Connections
    // -----------------------------------------------------------------------
    auto updateOkBtn = [this]() {
        const bool ok = !m_srcEdit->text().trimmed().isEmpty()
                     && !m_urlCombo->currentText().trimmed().isEmpty()
                     && !m_messageEdit->toPlainText().trimmed().isEmpty();
        m_importBtn->setEnabled(ok);
    };
    connect(m_srcEdit,   &QLineEdit::textChanged,        this, updateOkBtn);
    connect(m_urlCombo,  &QComboBox::currentTextChanged, this, updateOkBtn);
    connect(m_messageEdit, &QPlainTextEdit::textChanged, this, updateOkBtn);
    connect(btnBox, &QDialogButtonBox::accepted, this, &SvnImportDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SvnImportDialog::accept()
{
    SvnUi::pushHistory(QStringLiteral("ImportHistory"), targetUrl());
    QDialog::accept();
}

QString SvnImportDialog::sourcePath() const
{
    return m_srcEdit->text().trimmed();
}

QString SvnImportDialog::targetUrl() const
{
    return m_urlCombo->currentText().trimmed();
}

QString SvnImportDialog::message() const
{
    return m_messageEdit->toPlainText().trimmed();
}
