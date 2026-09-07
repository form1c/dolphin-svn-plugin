#include "svntreecheck.h"

#include <QList>
#include <QTreeWidget>
#include <QTreeWidgetItem>

// Sets every descendant of 'item' to 'state'.
static void setSubtreeCheckState(QTreeWidgetItem *item, Qt::CheckState state)
{
    QList<QTreeWidgetItem *> stack;
    for (int i = 0; i < item->childCount(); ++i)
        stack.push_back(item->child(i));
    while (!stack.isEmpty()) {
        QTreeWidgetItem *cur = stack.takeFirst();
        cur->setCheckState(0, state);
        for (int i = 0; i < cur->childCount(); ++i)
            stack.push_back(cur->child(i));
    }
}

// Derives a folder's tri-state from its direct children.
static Qt::CheckState stateFromChildren(QTreeWidgetItem *item)
{
    const int n = item->childCount();
    int checked = 0, partial = 0;
    for (int i = 0; i < n; ++i) {
        const Qt::CheckState cs = item->child(i)->checkState(0);
        if (cs == Qt::Checked)               ++checked;
        else if (cs == Qt::PartiallyChecked) ++partial;
    }
    if (checked == 0 && partial == 0) return Qt::Unchecked;
    if (checked == n)                 return Qt::Checked;
    return Qt::PartiallyChecked;
}

// Recomputes the tri-state of every ancestor of 'item' from its children.
static void refreshAncestorStates(QTreeWidgetItem *item)
{
    for (QTreeWidgetItem *p = item->parent(); p; p = p->parent())
        p->setCheckState(0, stateFromChildren(p));
}

// Bottom-up: gives every folder the tri-state of its children.
static void recomputeParentStates(QTreeWidgetItem *item)
{
    for (int i = 0; i < item->childCount(); ++i)
        recomputeParentStates(item->child(i));
    if (item->childCount() > 0)
        item->setCheckState(0, stateFromChildren(item));
}

void SvnUi::cascadeItemCheckState(QTreeWidgetItem *item)
{
    // A click yields a definite Checked/Unchecked state. Push it to every
    // descendant, then let the ancestors recompute their tri-state.
    // (PartiallyChecked only ever comes from our own recompute, never a click.)
    const Qt::CheckState state = item->checkState(0);
    if (state != Qt::PartiallyChecked)
        setSubtreeCheckState(item, state);
    refreshAncestorStates(item);
}

void SvnUi::initTreeCheckStates(QTreeWidget *tree)
{
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        recomputeParentStates(tree->topLevelItem(i));
}

bool SvnUi::includeForDepthEmpty(Qt::CheckState state, bool isDir)
{
    return state == Qt::Checked
        || (state == Qt::PartiallyChecked && isDir);
}

bool SvnUi::includeForDepthInfinity(Qt::CheckState state, bool /*isDir*/)
{
    return state == Qt::Checked;
}

QStringList SvnUi::collectCheckedPaths(
    QTreeWidget *tree, int pathRole,
    const std::function<bool(const QTreeWidgetItem *)> &accept)
{
    QStringList result;
    QList<QTreeWidgetItem *> stack;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        stack.push_back(tree->topLevelItem(i));
    while (!stack.isEmpty()) {
        QTreeWidgetItem *item = stack.takeFirst();
        if (!item->isHidden() && accept(item)) {
            const QString path = item->data(0, pathRole).toString();
            if (!path.isEmpty())
                result << path;
        }
        for (int i = 0; i < item->childCount(); ++i)
            stack.push_back(item->child(i));
    }
    return result;
}
