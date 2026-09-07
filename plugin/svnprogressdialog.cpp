#include "svnprogressdialog.h"
#include "svnmanager.h"

#include <KLocalizedString>
#include <KNotification>

#include <QDialogButtonBox>
#include <QFont>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

SvnProgressDialog::SvnProgressDialog(const QString &title,
                                     SvnManager    *manager,
                                     QWidget       *parent)
    : QDialog(parent)
    , m_manager(manager)
{
    setWindowTitle(title);
    setMinimumSize(640, 380);

    auto *layout = new QVBoxLayout(this);

    m_output = new QPlainTextEdit(this);
    m_output->setReadOnly(true);
    QFont mono(QStringLiteral("Monospace"));
    mono.setStyleHint(QFont::Monospace);
    m_output->setFont(mono);
    layout->addWidget(m_output);

    // Indeterminate progress bar — animates while the operation runs.
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0);  // indeterminate
    m_progressBar->setTextVisible(false);
    layout->addWidget(m_progressBar);

    m_statusLabel = new QLabel(i18n("Running…"), this);
    layout->addWidget(m_statusLabel);

    auto *buttons = new QDialogButtonBox(this);
    m_cancelButton = buttons->addButton(i18n("Cancel"), QDialogButtonBox::RejectRole);
    m_closeButton  = buttons->addButton(i18n("Close"),  QDialogButtonBox::AcceptRole);
    m_closeButton->setEnabled(false);
    layout->addWidget(buttons);

    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        m_manager->cancelAsync();
        m_cancelButton->setEnabled(false);
    });
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_manager, &SvnManager::progressOutput,
            this, &SvnProgressDialog::onProgressOutput);
    connect(m_manager, &SvnManager::operationFinished,
            this, &SvnProgressDialog::onOperationFinished);
}

void SvnProgressDialog::onProgressOutput(const QString &line)
{
    m_output->appendPlainText(line);

    // Count lines that carry a file action (A/U/M/C/D/E/! followed by spaces).
    static const QRegularExpression actionRe(QStringLiteral("^\\s*[AUMCDE!]\\s+"));
    if (actionRe.match(line).hasMatch()) {
        ++m_fileCount;
        m_statusLabel->setText(
            i18np("Processing 1 file…", "Processing %1 files…", m_fileCount));
    }
}

void SvnProgressDialog::onOperationFinished(const SvnOperationResult &result)
{
    // This dialog's operation is done. Detach from the manager so a later
    // operation does not update this (possibly still open) dialog or send
    // duplicate notifications through it.
    disconnect(m_manager, nullptr, this, nullptr);

    m_cancelButton->setEnabled(false);
    m_closeButton->setEnabled(true);

    // Stop the progress bar animation.
    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(1);
    m_progressBar->setVisible(false);

    QString statusMsg;
    if (result.success) {
        const QString rev = result.newRevision.isEmpty()
            ? QString()
            : i18n(" (Revision %1)", result.newRevision);
        statusMsg = i18n("✓ Completed successfully%1.", rev);
        m_statusLabel->setText(statusMsg);

        auto *notif = new KNotification(QStringLiteral("svnSuccess"),
                                        KNotification::CloseOnTimeout, this);
        notif->setComponentName(QStringLiteral("dolphinsvnplugin"));
        notif->setTitle(windowTitle());
        notif->setText(result.newRevision.isEmpty()
                       ? i18n("Operation completed successfully.")
                       : i18n("Committed successfully. New revision: %1", result.newRevision));
        notif->setIconName(QStringLiteral("vcs-commit"));
        notif->sendEvent();
    } else {
        if (!result.errorMessage.isEmpty())
            m_output->appendPlainText(result.errorMessage);
        statusMsg = i18n("✗ Operation failed.");
        m_statusLabel->setText(statusMsg);

        auto *notif = new KNotification(QStringLiteral("svnError"),
                                        KNotification::CloseOnTimeout, this);
        notif->setComponentName(QStringLiteral("dolphinsvnplugin"));
        notif->setTitle(windowTitle());
        notif->setText(result.errorMessage.isEmpty()
                       ? i18n("Operation failed.")
                       : result.errorMessage.left(200));
        notif->setIconName(QStringLiteral("dialog-error"));
        notif->sendEvent();
    }
}
