#pragma once

#include <QDialog>
#include <QString>

class QComboBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QSpinBox;
class SvnManager;

class SvnSwitchDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SvnSwitchDialog(const QString &wcPath, SvnManager *mgr,
                             QWidget *parent = nullptr);

    QString targetUrl() const;
    QString revision()  const;   // "HEAD" or revision number as string
    QString depth()     const;

protected:
    void accept() override;

private:
    void saveUrlToHistory(const QString &url);

    QLabel       *m_currentUrlLabel = nullptr;
    QComboBox    *m_urlCombo        = nullptr;
    QPushButton  *m_urlBrowse       = nullptr;
    QRadioButton *m_headRadio       = nullptr;
    QRadioButton *m_revRadio        = nullptr;
    QSpinBox     *m_revSpinBox      = nullptr;
    QComboBox    *m_depthCombo      = nullptr;
    QPushButton  *m_switchBtn       = nullptr;
};
