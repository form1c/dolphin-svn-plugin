#include "svntreecheck.h"

#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItem>

// Tests for the checkbox cascade and the selection derivation
// (SvnUi::cascadeItemCheckState / initTreeCheckStates / includeForDepthEmpty /
// includeForDepthInfinity / collectCheckedPaths) shared by the commit, add and
// revert dialogs. Runs headless (QT_QPA_PLATFORM=offscreen): QTreeWidget needs
// QtWidgets only, no running Dolphin.
class TstTreeCheck : public QObject
{
    Q_OBJECT

private:
    // Creates an item under 'parent' (or the tree root) with a check state.
    static QTreeWidgetItem *mkItem(QTreeWidgetItem *parent, QTreeWidget *tree,
                                   Qt::CheckState state)
    {
        auto *it = parent ? new QTreeWidgetItem(parent)
                          : new QTreeWidgetItem(tree);
        it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled
                     | Qt::ItemIsSelectable);
        it->setCheckState(0, state);
        return it;
    }

    // As mkItem, plus the path (UserRole) and isDir (UserRole+2) data the
    // collector reads, matching the commit dialog's role layout.
    static QTreeWidgetItem *mkNode(QTreeWidgetItem *parent, QTreeWidget *tree,
                                   Qt::CheckState state, const QString &path,
                                   bool isDir)
    {
        QTreeWidgetItem *it = mkItem(parent, tree, state);
        it->setData(0, Qt::UserRole,     path);
        it->setData(0, Qt::UserRole + 2, isDir);
        return it;
    }

    // Simulates a user click: sets the state and runs the cascade (as the
    // dialogs' itemChanged handler does).
    static void click(QTreeWidgetItem *item, Qt::CheckState state)
    {
        item->setCheckState(0, state);
        SvnUi::cascadeItemCheckState(item);
    }

    // The commit/add rule: fully checked, or a partially checked directory.
    static bool acceptDepthEmpty(const QTreeWidgetItem *item)
    {
        return SvnUi::includeForDepthEmpty(
            item->checkState(0), item->data(0, Qt::UserRole + 2).toBool());
    }
    // The revert rule: fully checked only.
    static bool acceptDepthInfinity(const QTreeWidgetItem *item)
    {
        return SvnUi::includeForDepthInfinity(
            item->checkState(0), item->data(0, Qt::UserRole + 2).toBool());
    }

private slots:
    // Cascade / tri-state
    void checkingFolderChecksAllDescendants();
    void uncheckingFolderUnchecksAllDescendants();
    void uncheckingOneChildMakesParentPartiallyChecked();
    void recheckingLastChildMakesParentChecked();
    void nestedFoldersCascadeAndPropagate();
    void initTreeCheckStatesReflectsMixedChildren();
    void clickingPartiallyCheckedFolderSelectsWholeSubtree();

    // Selection derivation
    void depthEmptyRuleIncludesCheckedAndPartialDirs();
    void depthInfinityRuleIncludesOnlyChecked();
    void collectCheckedPathsIsBreadthFirstAndSkipsHidden();
    void commitTargetsKeepPartialAddedParent();
    void revertTargetsDropPartialFolder();
};

void TstTreeCheck::checkingFolderChecksAllDescendants()
{
    QTreeWidget tree;
    auto *sub = mkItem(nullptr, &tree, Qt::Unchecked);
    auto *b   = mkItem(sub, &tree, Qt::Unchecked);
    auto *c   = mkItem(sub, &tree, Qt::Unchecked);

    click(sub, Qt::Checked);

    QCOMPARE(sub->checkState(0), Qt::Checked);
    QCOMPARE(b->checkState(0),   Qt::Checked);
    QCOMPARE(c->checkState(0),   Qt::Checked);
}

void TstTreeCheck::uncheckingFolderUnchecksAllDescendants()
{
    QTreeWidget tree;
    auto *sub = mkItem(nullptr, &tree, Qt::Checked);
    auto *b   = mkItem(sub, &tree, Qt::Checked);
    auto *c   = mkItem(sub, &tree, Qt::Checked);

    click(sub, Qt::Unchecked);

    QCOMPARE(sub->checkState(0), Qt::Unchecked);
    QCOMPARE(b->checkState(0),   Qt::Unchecked);
    QCOMPARE(c->checkState(0),   Qt::Unchecked);
}

void TstTreeCheck::uncheckingOneChildMakesParentPartiallyChecked()
{
    QTreeWidget tree;
    auto *sub = mkItem(nullptr, &tree, Qt::Checked);
    auto *b   = mkItem(sub, &tree, Qt::Checked);
    auto *c   = mkItem(sub, &tree, Qt::Checked);

    click(b, Qt::Unchecked);

    QCOMPARE(b->checkState(0),   Qt::Unchecked);
    QCOMPARE(c->checkState(0),   Qt::Checked);          // sibling untouched
    QCOMPARE(sub->checkState(0), Qt::PartiallyChecked); // folder no longer lies
}

void TstTreeCheck::recheckingLastChildMakesParentChecked()
{
    QTreeWidget tree;
    auto *sub = mkItem(nullptr, &tree, Qt::PartiallyChecked);
    auto *b   = mkItem(sub, &tree, Qt::Unchecked);
    auto *c   = mkItem(sub, &tree, Qt::Checked);

    click(b, Qt::Checked);
    QCOMPARE(sub->checkState(0), Qt::Checked);           // all children on

    click(c, Qt::Unchecked);
    QCOMPARE(sub->checkState(0), Qt::PartiallyChecked);
    click(b, Qt::Unchecked);
    QCOMPARE(sub->checkState(0), Qt::Unchecked);         // no child on
}

void TstTreeCheck::nestedFoldersCascadeAndPropagate()
{
    QTreeWidget tree;
    // root/ └─ mid/ ├─ leaf1  └─ deep/ └─ leaf2
    auto *root  = mkItem(nullptr, &tree, Qt::Unchecked);
    auto *mid   = mkItem(root, &tree, Qt::Unchecked);
    auto *leaf1 = mkItem(mid, &tree, Qt::Unchecked);
    auto *deep  = mkItem(mid, &tree, Qt::Unchecked);
    auto *leaf2 = mkItem(deep, &tree, Qt::Unchecked);

    // Check deep down: every ancestor that is the only checked branch follows.
    click(leaf2, Qt::Checked);
    QCOMPARE(deep->checkState(0), Qt::Checked);
    QCOMPARE(leaf1->checkState(0), Qt::Unchecked);        // other leaf untouched
    QCOMPARE(mid->checkState(0),  Qt::PartiallyChecked);  // leaf1 still missing
    QCOMPARE(root->checkState(0), Qt::PartiallyChecked);

    // Check the missing leaf → everything fully checked.
    click(leaf1, Qt::Checked);
    QCOMPARE(mid->checkState(0),  Qt::Checked);
    QCOMPARE(root->checkState(0), Qt::Checked);

    // Unchecking at the top cascades all the way down.
    click(root, Qt::Unchecked);
    QCOMPARE(leaf2->checkState(0), Qt::Unchecked);
    QCOMPARE(deep->checkState(0),  Qt::Unchecked);
}

void TstTreeCheck::initTreeCheckStatesReflectsMixedChildren()
{
    QTreeWidget tree;
    // Folder starts "Checked" but holds an unchecked child (e.g. an unversioned
    // file in the commit dialog).
    auto *folder = mkItem(nullptr, &tree, Qt::Checked);
    mkItem(folder, &tree, Qt::Checked);
    mkItem(folder, &tree, Qt::Unchecked);

    SvnUi::initTreeCheckStates(&tree);

    QCOMPARE(folder->checkState(0), Qt::PartiallyChecked);
}

void TstTreeCheck::clickingPartiallyCheckedFolderSelectsWholeSubtree()
{
    QTreeWidget tree;
    auto *sub = mkItem(nullptr, &tree, Qt::PartiallyChecked);
    auto *b   = mkItem(sub, &tree, Qt::Checked);
    auto *c   = mkItem(sub, &tree, Qt::Unchecked);

    // A click on a partially checked checkbox yields Checked (Qt), so the
    // cascade selects the whole subtree.
    click(sub, Qt::Checked);
    QCOMPARE(b->checkState(0), Qt::Checked);
    QCOMPARE(c->checkState(0), Qt::Checked);
}

void TstTreeCheck::depthEmptyRuleIncludesCheckedAndPartialDirs()
{
    QVERIFY(SvnUi::includeForDepthEmpty(Qt::Checked, false));
    QVERIFY(SvnUi::includeForDepthEmpty(Qt::Checked, true));
    QVERIFY(!SvnUi::includeForDepthEmpty(Qt::Unchecked, false));
    QVERIFY(!SvnUi::includeForDepthEmpty(Qt::Unchecked, true));
    // A partially checked directory is a new parent that must come along.
    QVERIFY(SvnUi::includeForDepthEmpty(Qt::PartiallyChecked, true));
    // A partially checked file makes no sense and must not be included.
    QVERIFY(!SvnUi::includeForDepthEmpty(Qt::PartiallyChecked, false));
}

void TstTreeCheck::depthInfinityRuleIncludesOnlyChecked()
{
    QVERIFY(SvnUi::includeForDepthInfinity(Qt::Checked, false));
    QVERIFY(SvnUi::includeForDepthInfinity(Qt::Checked, true));
    QVERIFY(!SvnUi::includeForDepthInfinity(Qt::Unchecked, true));
    // A partially checked folder must be dropped, otherwise the recursive
    // revert would touch its unchecked children.
    QVERIFY(!SvnUi::includeForDepthInfinity(Qt::PartiallyChecked, true));
}

void TstTreeCheck::collectCheckedPathsIsBreadthFirstAndSkipsHidden()
{
    QTreeWidget tree;
    auto *dir  = mkNode(nullptr, &tree, Qt::Checked, QStringLiteral("dir"), true);
    auto *a    = mkNode(dir, &tree, Qt::Checked, QStringLiteral("dir/a"), false);
    auto *hid  = mkNode(dir, &tree, Qt::Checked, QStringLiteral("dir/hidden"), false);
    Q_UNUSED(a)
    hid->setHidden(true);

    const QStringList got = SvnUi::collectCheckedPaths(
        &tree, Qt::UserRole, &TstTreeCheck::acceptDepthEmpty);

    // Parent before child, hidden item skipped.
    QCOMPARE(got, (QStringList{QStringLiteral("dir"), QStringLiteral("dir/a")}));
}

void TstTreeCheck::commitTargetsKeepPartialAddedParent()
{
    QTreeWidget tree;
    // A new (added) folder with two new files; one child unchecked → folder
    // becomes partially checked. The folder must still be listed as the parent.
    auto *dir = mkNode(nullptr, &tree, Qt::Checked, QStringLiteral("newdir"), true);
    auto *n1  = mkNode(dir, &tree, Qt::Checked, QStringLiteral("newdir/n1"), false);
    auto *n2  = mkNode(dir, &tree, Qt::Checked, QStringLiteral("newdir/n2"), false);
    Q_UNUSED(n1)

    click(n2, Qt::Unchecked);
    QCOMPARE(dir->checkState(0), Qt::PartiallyChecked);

    const QStringList got = SvnUi::collectCheckedPaths(
        &tree, Qt::UserRole, &TstTreeCheck::acceptDepthEmpty);

    QCOMPARE(got, (QStringList{QStringLiteral("newdir"), QStringLiteral("newdir/n1")}));
}

void TstTreeCheck::revertTargetsDropPartialFolder()
{
    QTreeWidget tree;
    auto *dir = mkNode(nullptr, &tree, Qt::Checked, QStringLiteral("dir"), true);
    auto *a   = mkNode(dir, &tree, Qt::Checked, QStringLiteral("dir/a"), false);
    auto *b   = mkNode(dir, &tree, Qt::Checked, QStringLiteral("dir/b"), false);
    Q_UNUSED(a)

    click(b, Qt::Unchecked);
    QCOMPARE(dir->checkState(0), Qt::PartiallyChecked);

    // Depth-infinity: the partial folder is dropped (recursion would revert b),
    // only the individually checked file remains.
    const QStringList got = SvnUi::collectCheckedPaths(
        &tree, Qt::UserRole, &TstTreeCheck::acceptDepthInfinity);

    QCOMPARE(got, (QStringList{QStringLiteral("dir/a")}));
}

QTEST_MAIN(TstTreeCheck)
#include "tst_treecheck.moc"
