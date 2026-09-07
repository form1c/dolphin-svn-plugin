#pragma once

#include <QDialog>
#include <QString>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class SvnManager;

class SvnImportDialog : public QDialog
{
    Q_OBJECT
public:
    // localDir: pre-filled source folder (the folder the action was invoked on).
    explicit SvnImportDialog(const QString &localDir, SvnManager *mgr,
                             QWidget *parent = nullptr);

    QString sourcePath() const;
    QString targetUrl()  const;
    QString message()    const;

protected:
    void accept() override;

private:
    QLineEdit      *m_srcEdit    = nullptr;
    QComboBox      *m_urlCombo   = nullptr;
    QPushButton    *m_urlBrowse  = nullptr;
    QPlainTextEdit *m_messageEdit = nullptr;
    QPushButton    *m_importBtn  = nullptr;
};
