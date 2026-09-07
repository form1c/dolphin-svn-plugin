#pragma once

#include "svntypes.h"

#include <QDialog>
#include <QList>
#include <QSet>

class QCheckBox;
class QLabel;
class QPushButton;
class QTextEdit;
class QTreeWidget;
class QTreeWidgetItem;
class SvnManager;

/**
 * Modal revision picker with an embedded log (modelled on SvnUpdateDialog).
 * Single: pick one revision (e.g. start/end revision in a tree merge).
 * Multiple: pick several revisions (cherry-pick ranges for the merge).
 */
class SvnLogPickDialog : public QDialog
{
    Q_OBJECT
public:
    enum class SelectionMode { Single, Multiple };

    SvnLogPickDialog(const QString &urlOrPath, SvnManager *mgr,
                     SelectionMode mode, QWidget *parent = nullptr);

    QList<long long> selectedRevisions() const; // sorted ascending
    long long selectedRevision() const;         // -1 = no selection

    // "5-10,14": consecutive runs collapsed into a-b, compatible with
    // 'svn merge -c' and the ranges field of the merge dialog.
    static QString formatRevisionRanges(QList<long long> revs);

    // Merge tracking: revisions NOT in the eligible set (already merged or
    // natural history) are greyed out — they stay selectable (svn no-ops them
    // during the merge). Call only after a successful mergeinfo query; an empty
    // set is valid (everything merged → everything grey).
    void setEligibleRevisions(const QSet<long long> &eligible);

private:
    void loadMore();
    void reloadLog();
    void showEntry(QTreeWidgetItem *item);
    void markNotEligibleItems();

    QString m_target;
    SvnManager *m_mgr = nullptr;
    QString m_repoRelPath;             // lazy, for "Show only affected paths"
    bool m_repoRelResolved = false;
    QCheckBox *m_onlyAffectedBox = nullptr;
    QTreeWidgetItem *m_lastShownItem = nullptr;

    QTreeWidget *m_logTree   = nullptr;
    QTextEdit   *m_msgView   = nullptr;
    QTreeWidget *m_pathsTree = nullptr;
    QPushButton *m_moreBtn   = nullptr;
    QPushButton *m_okBtn     = nullptr;

    QList<SvnLogEntry> m_entries;
    long long m_oldestRevision = -1;
    QSet<long long> m_eligibleRevisions;
    bool m_eligibleKnown = false; // grey out only after setEligibleRevisions()

    static constexpr int kPageSize = 100;
};
