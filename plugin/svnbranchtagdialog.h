#pragma once

#include <QDialog>
#include <QString>

class QComboBox;
class QLineEdit;
class QPushButton;
class QPlainTextEdit;
class QRadioButton;
class QSpinBox;
class SvnManager;

class SvnBranchTagDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SvnBranchTagDialog(const QString &wcPath, SvnManager *mgr,
                                QWidget *parent = nullptr);

    // Overrides the source URL (e.g. from the repository browser).
    void setSourceUrl(const QString &url);

    QString sourceUrl()   const;
    QString targetUrl()   const;
    QString revision()    const;   // "HEAD" or revision number as string
    QString message()     const;

protected:
    void accept() override;

private:
    void updateOkButton();
    void saveTargetToHistory(const QString &url);

    QLineEdit    *m_srcEdit     = nullptr;
    QPushButton  *m_srcBrowse   = nullptr;
    QComboBox    *m_dstCombo    = nullptr;
    QPushButton  *m_dstBrowse   = nullptr;
    QRadioButton *m_headRadio   = nullptr;
    QRadioButton *m_revRadio    = nullptr;
    QSpinBox     *m_revSpinBox  = nullptr;
    QPlainTextEdit *m_msgEdit   = nullptr;
    QPushButton  *m_okBtn       = nullptr;
};
