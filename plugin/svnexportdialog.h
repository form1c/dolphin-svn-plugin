#pragma once

#include <QDialog>

class QLineEdit;
class QRadioButton;
class QSpinBox;
class QPushButton;

class SvnExportDialog : public QDialog
{
    Q_OBJECT
public:
    // srcPath may be empty (non-WC context) — user must then supply a URL.
    explicit SvnExportDialog(const QString &srcPath, QWidget *parent = nullptr);

    QString srcPath()  const;
    QString destPath() const;
    QString revision() const;  // "HEAD" or number string
    bool    force()    const;

protected:
    void accept() override;

private:
    void updateState();

    QLineEdit    *m_srcEdit    = nullptr;
    QLineEdit    *m_destEdit   = nullptr;
    QRadioButton *m_headRadio  = nullptr;
    QRadioButton *m_revRadio   = nullptr;
    QSpinBox     *m_revSpinBox = nullptr;
    QPushButton  *m_exportBtn  = nullptr;

    bool m_force = false;
};
