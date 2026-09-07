#include "svncheckoutdialog.h"
#include "svnuihelpers.h"

#include <KLocalizedString>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

SvnCheckoutDialog::SvnCheckoutDialog(const QString &localPath, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("SVN Checkout"));
    setMinimumWidth(560);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    // -----------------------------------------------------------------------
    // Repository URL
    // -----------------------------------------------------------------------
    auto *urlGroup  = new QGroupBox(i18n("Repository URL"), this);
    auto *urlLayout = new QHBoxLayout(urlGroup);

    m_urlCombo = new QComboBox(urlGroup);
    m_urlCombo->setEditable(true);
    m_urlCombo->setInsertPolicy(QComboBox::NoInsert);
    m_urlCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_urlCombo->lineEdit()->setPlaceholderText(
        i18n("e.g. svn+ssh://host/repo  or  file:///path/to/repo"));
    urlLayout->addWidget(m_urlCombo, 1);

    layout->addWidget(urlGroup);

    // Load recent URLs
    const QStringList urls = SvnUi::loadHistory(QStringLiteral("CheckoutHistory"));
    for (const QString &u : urls)
        m_urlCombo->addItem(u, u);
    m_urlCombo->setCurrentIndex(-1);
    m_urlCombo->lineEdit()->clear();

    // -----------------------------------------------------------------------
    // Checkout directory
    // -----------------------------------------------------------------------
    auto *dirGroup  = new QGroupBox(i18n("Checkout Directory"), this);
    auto *dirLayout = new QHBoxLayout(dirGroup);

    m_dirEdit = new QLineEdit(localPath, dirGroup);
    dirLayout->addWidget(m_dirEdit, 1);

    auto *browseBtn = new QPushButton(i18n("Browse..."), dirGroup);
    dirLayout->addWidget(browseBtn);

    layout->addWidget(dirGroup);

    // -----------------------------------------------------------------------
    // Revision
    // -----------------------------------------------------------------------
    auto *revGroup  = new QGroupBox(i18n("Revision"), this);
    auto *revLayout = new QHBoxLayout(revGroup);

    m_headRadio = new QRadioButton(i18n("HEAD (latest)"), revGroup);
    m_headRadio->setChecked(true);
    revLayout->addWidget(m_headRadio);

    revLayout->addSpacing(16);

    m_revRadio = new QRadioButton(i18n("Revision:"), revGroup);
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
    m_checkoutBtn = btnBox->button(QDialogButtonBox::Ok);
    m_checkoutBtn->setText(i18n("Checkout"));
    m_checkoutBtn->setIcon(QIcon::fromTheme(QStringLiteral("vcs-update-cvs-cervisia")));
    m_checkoutBtn->setEnabled(false);
    layout->addWidget(btnBox);

    // -----------------------------------------------------------------------
    // Connections
    // -----------------------------------------------------------------------
    connect(m_revRadio,   &QRadioButton::toggled, m_revSpinBox, &QSpinBox::setEnabled);
    connect(m_revSpinBox, &QSpinBox::valueChanged, this, [this]() {
        m_revRadio->setChecked(true);
    });
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        const QString d = QFileDialog::getExistingDirectory(
            this, i18n("Select checkout directory"), m_dirEdit->text());
        if (!d.isEmpty())
            m_dirEdit->setText(d);
    });
    // Enable Checkout button only when URL and directory are non-empty.
    auto updateOkBtn = [this]() {
        const bool ok = !m_urlCombo->currentText().trimmed().isEmpty()
                     && !m_dirEdit->text().trimmed().isEmpty();
        m_checkoutBtn->setEnabled(ok);
    };
    connect(m_urlCombo, &QComboBox::currentTextChanged, this, updateOkBtn);
    connect(m_dirEdit,  &QLineEdit::textChanged,        this, updateOkBtn);
    connect(btnBox, &QDialogButtonBox::accepted, this, &SvnCheckoutDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

// ---------------------------------------------------------------------------
// Accept — save URL to history before closing
// ---------------------------------------------------------------------------

void SvnCheckoutDialog::accept()
{
    saveUrlToHistory(url());
    QDialog::accept();
}

// ---------------------------------------------------------------------------
// Public accessors
// ---------------------------------------------------------------------------

void SvnCheckoutDialog::setUrl(const QString &url)
{
    m_urlCombo->setCurrentText(url);
}

QString SvnCheckoutDialog::url() const
{
    return m_urlCombo->currentText().trimmed();
}

QString SvnCheckoutDialog::localPath() const
{
    return m_dirEdit->text().trimmed();
}

QString SvnCheckoutDialog::revision() const
{
    return m_headRadio->isChecked()
        ? QStringLiteral("HEAD")
        : QString::number(m_revSpinBox->value());
}

QString SvnCheckoutDialog::depth() const
{
    return m_depthCombo->currentData().toString();
}

// ---------------------------------------------------------------------------
// URL history (max 10 entries, newest first)
// ---------------------------------------------------------------------------

void SvnCheckoutDialog::saveUrlToHistory(const QString &u)
{
    SvnUi::pushHistory(QStringLiteral("CheckoutHistory"), u);
}
