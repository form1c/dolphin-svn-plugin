#pragma once

#include "svntypes.h"

#include <QIcon>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <functional>

class QBoxLayout;
class QCheckBox;
class QWidget;
class SvnManager;

namespace SvnUi {
QIcon makeFolderIcon();

// The checkbox cascade (cascadeItemCheckState / initTreeCheckStates) lives in
// svntreecheck.h — deliberately as a KF6-free, headless-testable unit.

/**
 * "Browse..." helper: opens the repo browser as a URL picker
 * (SvnRepoBrowserDialog::Mode::PickUrl). Start point: 'currentText' if not
 * empty, otherwise 'wcInfo.repositoryRoot' (if wcInfo.valid).
 * Returns the chosen URL or empty on cancel.
 */
QString pickRepoUrl(const QString &currentText, const SvnInfo &wcInfo,
                    SvnManager *mgr, QWidget *parent);

/**
 * Loads a persisted history (newest first) from
 * QSettings("DolphinSvnPlugin", group)[key]. 'key' allows arbitrary, also
 * dynamically built keys (e.g. the repo-specific Base64Url key of the commit
 * history).
 */
QStringList loadHistory(const QString &group, const QString &key = QStringLiteral("urls"));

/**
 * Inserts 'value' at the front of the history at group/key (duplicates removed,
 * cap 10 entries) and persists it. No-op for an empty 'value'.
 */
void pushHistory(const QString &group, const QString &value,
                 const QString &key = QStringLiteral("urls"));

// The three TortoiseSVN log checkboxes (stop on copy/rename, include merged
// revisions, show only affected paths) — shared by the log, update and pick
// dialogs. State comes from SvnSettings and is persisted on toggle; afterwards
// onReloadNeeded fires (the first two: reload the log) or onFilterChanged (the
// path filter: only refresh the view).
struct LogOptionsBoxes {
    QCheckBox *stopOnCopy        = nullptr;
    QCheckBox *includeMerged     = nullptr;
    QCheckBox *onlyAffectedPaths = nullptr;
};

LogOptionsBoxes createLogOptionsRow(QWidget *parent, QBoxLayout *row,
                                    std::function<void()> onReloadNeeded,
                                    std::function<void()> onFilterChanged);

/**
 * Starts 'startOp' behind an SvnProgressDialog (WA_DeleteOnClose). Rejects the
 * operation with a notice box if 'mgr' is already busy (isBusy()). 'onFinished'
 * (optional) runs when the dialog is closed (the connect context is
 * 'parentWidget', so the connection is dropped automatically if the caller is
 * destroyed before the dialog).
 */
void runWithProgress(SvnManager *mgr, const QString &title,
                     QWidget *parentWidget, std::function<void()> startOp,
                     std::function<void()> onFinished = {});

/**
 * Starts the three-way merge of a file in a conflict state with AnGscheidrDiffer
 * (Base/Mine/Theirs from svn info, the result overwrites the working file). On
 * exit code 0 (saved & conflict-free), the user is asked and 'svn resolve
 * --accept working' is triggered; afterwards onResolved runs (e.g. reload the
 * list; may be empty).
 * ctx: context QObject for the QProcess lifetime and connects.
 */
void resolveWithThreeWayMerge(const QString &path, SvnManager *mgr,
                              QWidget *parentWidget, QObject *ctx,
                              std::function<void()> onResolved = {});
}

// QTreeWidgetItem subclass that sorts column 0 numerically.
// Store the revision number via setData(0, Qt::UserRole, revisionAsLongLong).
// The entry-list index must be stored in Qt::UserRole + 1.
class SvnRevisionItem : public QTreeWidgetItem
{
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &other) const override
    {
        if (treeWidget() && treeWidget()->sortColumn() == 0)
            return data(0, Qt::UserRole).toLongLong()
                 < other.data(0, Qt::UserRole).toLongLong();
        return QTreeWidgetItem::operator<(other);
    }
};
