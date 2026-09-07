#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtCore/qnamespace.h>

class QTreeWidget;

// Pure, KF6-free building blocks of the commit dialog's file tree. Kept out of
// svncommitdialog.cpp so the part that had the tree bugs (nesting, grouping,
// missing directories, collapsing) can be unit-tested with only QtWidgets, the
// same way svntreecheck.* is. The dialog collects the rows from 'svn status',
// calls buildCommitTree(), and then decorates the items (status text, icons,
// fonts) which needs KF6 and the backend and is not part of the structure.
namespace SvnUi {

// One change to place in the tree. 'status' carries an SvnFileStatus value as an
// int so this header stays free of the backend enum.
struct CommitRow {
    QString abs;                  // absolute path
    bool    isUnversioned = false;
    int     status        = 0;    // SvnFileStatus as int
    bool    isDir         = false; // filesystem hint (false for a gone/missing dir)
    bool    collapsed     = false; // over-threshold unversioned directory
};

// The directory the tree is rooted at: the common PARENT of the selected items.
// A single directory -> its parent (so the directory becomes a folder node with
// its contents nested), a single file -> its folder, several items -> the common
// parent of them all.
QString commonAncestorDir(const QStringList &paths);

// Recursively lists the paths under 'dir' (excluding .svn) into 'out', stopping
// and returning false as soon as more than 'cap' are found. The early exit means
// a huge directory is walked only up to the cap, never fully.
bool collectUnversionedDirCapped(const QString &dir, QStringList &out, int cap);

// Full recursive content list of 'dir', sorted so a parent precedes its children.
QStringList descendantPathsSorted(const QString &dir);

// Builds the folder tree into 'tree' from 'rows' rooted at 'displayRoot'.
// Unchanged intermediate folders become grouping nodes. A path that is the parent
// of another row is treated as a directory even when it is gone from disk (a
// missing directory). Sets the item data roles and default check states, then
// recomputes the folder tri-states. Sets NO status text, icons or fonts, so it
// needs neither KF6 nor the backend. Data roles per item:
//   UserRole   path (QString)
//   UserRole+1 isUnversioned (bool)
//   UserRole+2 isDir (bool)
//   UserRole+3 status (int, SvnFileStatus)
//   UserRole+4 isRealEntry (bool)  -- false for a pure grouping folder
//   UserRole+5 collapsed (bool)
void buildCommitTree(QTreeWidget *tree, const QList<CommitRow> &rows,
                     const QString &displayRoot);

// Records the check state of every leaf real entry (UserRole+4) by its path
// (UserRole). Used to preserve the user's selection across a rebuild, for example
// when refresh() rebuilds the tree after deleting a missing file.
QHash<QString, Qt::CheckState> snapshotCheckStates(QTreeWidget *tree);

// Reapplies a snapshot to the matching leaf real entries and recomputes the
// folder tri-states. Leaves not present in the snapshot keep their current state.
void restoreCheckStates(QTreeWidget *tree,
                        const QHash<QString, Qt::CheckState> &saved);

} // namespace SvnUi
