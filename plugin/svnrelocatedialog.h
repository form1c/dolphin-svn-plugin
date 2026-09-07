#pragma once

#include <QDialog>
#include <QString>

class QComboBox;
class QLabel;
class QPushButton;
class SvnManager;

class SvnRelocateDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SvnRelocateDialog(const QString &wcPath, SvnManager *mgr,
                               QWidget *parent = nullptr);

    QString fromUrl() const;
    QString toUrl()   const;

protected:
    void accept() override;

private:
    QString      m_fromUrl;
    QComboBox   *m_urlCombo    = nullptr;
    QPushButton *m_urlBrowse   = nullptr;
    QPushButton *m_relocateBtn = nullptr;
};
