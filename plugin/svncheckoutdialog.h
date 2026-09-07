#pragma once

#include <QDialog>
#include <QString>

class QComboBox;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;

class SvnCheckoutDialog : public QDialog
{
    Q_OBJECT
public:
    // localPath: pre-filled checkout target directory.
    explicit SvnCheckoutDialog(const QString &localPath, QWidget *parent = nullptr);

    // Pre-fills the repository URL (e.g. from the Repository Browser).
    void setUrl(const QString &url);

    QString url() const;
    QString localPath() const;
    QString revision() const;   // "HEAD" or revision number as string
    QString depth() const;      // "infinity", "immediates", "files", "empty"

protected:
    void accept() override;

private:
    void saveUrlToHistory(const QString &url);

    QComboBox    *m_urlCombo    = nullptr;
    QLineEdit    *m_dirEdit     = nullptr;
    QRadioButton *m_headRadio   = nullptr;
    QRadioButton *m_revRadio    = nullptr;
    QSpinBox     *m_revSpinBox  = nullptr;
    QComboBox    *m_depthCombo  = nullptr;
    QPushButton  *m_checkoutBtn = nullptr;
};
