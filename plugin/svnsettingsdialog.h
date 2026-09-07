#pragma once

#include <QDialog>
#include <QList>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;

class SvnSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SvnSettingsDialog(QWidget *parent = nullptr);

protected:
    void accept() override;

private:
    void checkSvnBinary();
    void resetToDefaults();

    struct ToolRadio {
        QRadioButton *radio;
        QString       path;
    };

    // SVN Binary
    QLineEdit        *m_svnBinaryEdit   = nullptr;
    QLabel           *m_svnVersionLabel = nullptr;

    // Diff Tool
    QList<ToolRadio>  m_toolRadios;
    QRadioButton     *m_diffCustomRadio = nullptr;
    QLineEdit        *m_diffCustomEdit  = nullptr;
    QPushButton      *m_diffBrowseBtn   = nullptr;

    // Advanced
    QSpinBox         *m_timeoutSpinBox  = nullptr;
    QLineEdit        *m_tempDirEdit     = nullptr;
    QSpinBox         *m_overlayTtlSpinBox = nullptr;

    // Dialog Defaults
    QCheckBox        *m_hideUnversioned = nullptr;
    QCheckBox        *m_showUnversioned = nullptr;
};
