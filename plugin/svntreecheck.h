#pragma once

#include <QStringList>

#include <functional>

class QTreeWidget;
class QTreeWidgetItem;

// Check-state cascade and selection derivation for the hierarchical file lists
// of the commit, add and revert dialogs.
//
// Deliberately free of KF6 and SvnManager (QtWidgets only), so the logic can be
// tested headless without the full plugin context (see tst_treecheck).
namespace SvnUi {

/**
 * Call from the itemChanged handler. A user-set Checked/Unchecked state is
 * pushed to ALL descendants, then every ancestor recomputes its own tri-state
 * (Checked if all children are checked, Unchecked if none, PartiallyChecked
 * otherwise). The caller must suppress the itemChanged signals this triggers
 * (the m_block pattern).
 */
void cascadeItemCheckState(QTreeWidgetItem *item);

/**
 * Call once after populating the tree, so every folder carries the correct
 * tri-state for its children's initial states (a folder may start checked while
 * a child starts unchecked).
 */
void initTreeCheckStates(QTreeWidget *tree);

/**
 * Rule for 'svn commit/add --depth empty <paths...>': every listed path is
 * committed or added exactly, so include fully checked items and, in addition,
 * a partially checked directory. That directory is a new parent its checked
 * children need present, otherwise svn fails with E200009.
 */
bool includeForDepthEmpty(Qt::CheckState state, bool isDir);

/**
 * Rule for 'svn revert --depth infinity <paths...>': a fully checked folder is
 * reverted recursively, so include only fully checked items. A partially
 * checked folder is left out, otherwise the recursion would revert its
 * unchecked children too.
 */
bool includeForDepthInfinity(Qt::CheckState state, bool isDir);

/**
 * Collects the path stored at 'pathRole' of every visible item for which
 * 'accept(item)' is true. Breadth-first, so a parent path always precedes its
 * children (required for the '--depth empty' add and commit calls).
 */
QStringList collectCheckedPaths(
    QTreeWidget *tree, int pathRole,
    const std::function<bool(const QTreeWidgetItem *)> &accept);

} // namespace SvnUi
