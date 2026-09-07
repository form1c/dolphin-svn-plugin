#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;

class SvnRenameDialog : public QDialog
{
    Q_OBJECT
public:
    // srcPath: absolute path of the file/directory to rename or move.
    explicit SvnRenameDialog(const QString &srcPath, QWidget *parent = nullptr);

    // Full absolute destination path as entered by the user.
    QString destPath() const;

private:
    void updateState();

    QString  m_srcPath;
    QString  m_targetDir;   // current destination parent directory

    QLineEdit   *m_nameEdit       = nullptr;
    QLabel      *m_previewLabel   = nullptr;
    QLabel      *m_targetDirLabel = nullptr;
    QPushButton *m_okBtn          = nullptr;
};
