#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QPushButton;
class SvnManager;

/**
 * "Apply Patch…": patch file + options, with a dry-run preview.
 *
 * The preview runs in the dialog itself (SvnProgressDialog via
 * SvnUi::runWithProgress, as in the merge dialog); Accepted means the caller
 * starts the real 'svn patch' run.
 */
class SvnApplyPatchDialog : public QDialog
{
    Q_OBJECT
public:
    SvnApplyPatchDialog(const QString &wcPath, const QString &initialPatchFile,
                        SvnManager *svnManager, QWidget *parent = nullptr);

    QString patchFile() const;
    bool    reverse()   const;
    /** '--strip'-Ebene (0 = weglassen). */
    int     strip()     const;

protected:
    void accept() override;

private:
    void updateState();
    // Recompute the strip level for the current patch file.
    void redetectStrip();

    QLineEdit   *m_fileEdit    = nullptr;
    QCheckBox   *m_reverseBox  = nullptr;
    QSpinBox    *m_stripSpin   = nullptr;
    QLabel      *m_stripHint   = nullptr;
    QPushButton *m_dryRunBtn   = nullptr;
    QPushButton *m_applyBtn    = nullptr;

    QString     m_wcPath;
    SvnManager *m_mgr = nullptr;
};
