#pragma once

#include <QDialog>

#include <functional>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class SvnManager;

/**
 * TortoiseSVN-style conflict dialog: lists all conflicts of a working copy
 * (text, property and tree conflicts) and offers the svn-CLI resolutions:
 *   - Edit (three-way merge in AnGscheidrDiffer, single text conflicts only)
 *   - Resolved (working) — mark the working copy state as resolved
 *   - mine-full / theirs-full — the whole file from one side
 *   - mine-conflict / theirs-conflict — only the conflict regions of one side
 * Tree conflicts can only be resolved with "working" via the CLI.
 * "Postpone" = close the dialog, conflicts remain (no svn call).
 */
class SvnConflictDialog : public QDialog
{
    Q_OBJECT
public:
    SvnConflictDialog(const QString &wcPath, SvnManager *mgr,
                      QWidget *parent = nullptr);

    /**
     * Optional "Continue Merge" button: only when a callback is set does a
     * "Continue Merge" button appear next to Refresh/Postpone, which restarts
     * the interrupted merge with identical options. Active only while NO
     * conflicts remain in the list; svn merge tracking makes already-merged
     * revisions a no-op and applies the rest. A click closes the dialog and
     * calls the callback.
     */
    void setContinueMerge(std::function<void()> cb);

private:
    void reload();
    void resolveSelected(const QString &accept);
    void editSelected();
    void updateActions();
    QStringList selectedPaths(bool *containsTree) const;

    QString m_wcPath;
    SvnManager *m_mgr = nullptr;

    QTreeWidget *m_tree        = nullptr;
    QLabel      *m_detailLabel = nullptr;
    QPushButton *m_editBtn     = nullptr;
    QPushButton *m_workingBtn  = nullptr;
    QPushButton *m_mineFullBtn = nullptr;
    QPushButton *m_theirsFullBtn = nullptr;
    QPushButton *m_mineConflictBtn = nullptr;
    QPushButton *m_theirsConflictBtn = nullptr;
    QPushButton *m_continueBtn = nullptr;
    std::function<void()> m_continueMerge;
    bool m_busy = false;
};
