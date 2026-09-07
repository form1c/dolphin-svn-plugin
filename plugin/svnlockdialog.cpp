#include "svnlockdialog.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

SvnLockDialog::SvnLockDialog(const QStringList &paths, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("SVN: Get Lock"));
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);

    if (!paths.isEmpty()) {
        const int shown = qMin(paths.size(), 5);
        QStringList preview = paths.mid(0, shown);
        if (paths.size() > shown)
            preview << i18n("… and %1 more", paths.size() - shown);
        auto *filesLabel = new QLabel(preview.join(QLatin1Char('\n')), this);
        filesLabel->setWordWrap(true);
        layout->addWidget(filesLabel);
    }

    layout->addWidget(new QLabel(i18n("Lock comment (optional):"), this));

    m_messageEdit = new QPlainTextEdit(this);
    m_messageEdit->setPlaceholderText(i18n("Reason for locking…"));
    m_messageEdit->setFixedHeight(80);
    layout->addWidget(m_messageEdit);

    m_stealCheck = new QCheckBox(i18n("Steal lock (take over existing locks)"), this);
    layout->addWidget(m_stealCheck);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(i18n("Lock"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString SvnLockDialog::message() const
{
    return m_messageEdit->toPlainText().trimmed();
}

bool SvnLockDialog::stealLock() const
{
    return m_stealCheck->isChecked();
}
