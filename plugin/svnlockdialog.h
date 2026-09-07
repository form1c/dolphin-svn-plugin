#pragma once

#include <QDialog>

class QCheckBox;
class QPlainTextEdit;

class SvnLockDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SvnLockDialog(const QStringList &paths, QWidget *parent = nullptr);

    QString message()   const;
    bool    stealLock() const;

private:
    QPlainTextEdit *m_messageEdit = nullptr;
    QCheckBox      *m_stealCheck  = nullptr;
};
