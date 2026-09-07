#pragma once

#include "svntypes.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QWidget;
class SvnManager;

/**
 * Merge dialog (simplified TortoiseSVN wizard):
 *  - Mode A: merge a revision range from a source URL
 *  - Mode B: merge two different trees (URL1@R1 → URL2@R2)
 * The dry run (preview without change) runs directly from the dialog, which
 * stays open; accept() always means: start the real merge.
 */
class SvnMergeDialog : public QDialog
{
    Q_OBJECT
public:
    SvnMergeDialog(const QString &wcPath, SvnManager *mgr,
                   QWidget *parent = nullptr);

    bool rangeMode() const;   // true = mode A
    // All chosen merge options for the svn call (dryRun = false).
    SvnMergeOptions mergeOptions() const;

    // Mode A
    QString sourceUrl() const;
    QString revisionRanges() const; // empty = all open revisions

    // Mode B
    QString treeUrl1() const;
    QString treeRev1() const;
    QString treeUrl2() const;
    QString treeRev2() const;

protected:
    void accept() override;

private:
    void updateModeWidgets();
    void updateButtons();
    void saveUrlToHistory(const QString &url);
    // Fills the source combo with sibling branches (trunk/branches/tags).
    void populateSiblingBranches();

    QRadioButton *m_rangeRadio = nullptr;
    QRadioButton *m_treeRadio  = nullptr;

    // Mode A
    QComboBox   *m_srcCombo    = nullptr;
    QLineEdit   *m_rangesEdit  = nullptr;
    QPushButton *m_srcBrowse   = nullptr;
    QPushButton *m_logPickBtn  = nullptr;

    // Mode B
    QLineEdit    *m_url1Edit   = nullptr;
    QRadioButton *m_head1Radio = nullptr;
    QSpinBox     *m_rev1Spin   = nullptr;
    QPushButton  *m_url1Browse = nullptr;
    QWidget      *m_rev1Row    = nullptr;
    QLineEdit    *m_url2Edit   = nullptr;
    QRadioButton *m_head2Radio = nullptr;
    QSpinBox     *m_rev2Spin   = nullptr;
    QPushButton  *m_url2Browse = nullptr;
    QWidget      *m_rev2Row    = nullptr;

    SvnManager *m_mgr = nullptr;
    QString     m_wcPath;
    SvnInfo     m_wcInfo;

    // Merge options
    QComboBox *m_depthCombo      = nullptr;
    QComboBox *m_whitespaceCombo = nullptr;
    QCheckBox *m_ignoreAncestryBox = nullptr;
    QCheckBox *m_forceBox          = nullptr;
    QCheckBox *m_recordOnlyBox     = nullptr;
    QCheckBox *m_ignoreEolBox      = nullptr;

    QPushButton *m_dryRunBtn = nullptr;
    QPushButton *m_mergeBtn  = nullptr;
};
