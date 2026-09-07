#pragma once

#include <QDialog>

#include "svntypes.h"

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class SvnManager;

class SvnProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SvnProgressDialog(const QString &title,
                               SvnManager    *manager,
                               QWidget       *parent = nullptr);

private:
    void onProgressOutput(const QString &line);
    void onOperationFinished(const SvnOperationResult &result);

    SvnManager     *m_manager;
    QPlainTextEdit *m_output;
    QProgressBar   *m_progressBar;
    QLabel         *m_statusLabel;
    QPushButton    *m_cancelButton;
    QPushButton    *m_closeButton;
    int             m_fileCount = 0;
};
