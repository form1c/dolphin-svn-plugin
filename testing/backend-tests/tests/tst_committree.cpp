#include "svncommittree.h"
#include "svntypes.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItem>

// Tests for the pure commit-tree logic (plugin/svncommittree.cpp): the folder
// nesting, grouping nodes, missing-directory handling and collapsing that all had
// bugs in svncommitdialog before they were extracted here. buildCommitTree works
// on plain path strings, so most cases need no real files; only the filesystem
// helpers use a QTemporaryDir.
class TstCommitTree : public QObject
{
    Q_OBJECT

private slots:
    void commonAncestorSingleFile();
    void commonAncestorSingleDir();
    void commonAncestorMultiple();
    void cappedStopsEarly();
    void cappedListsWhenUnderCap();
    void descendantsParentBeforeChild();
    void buildNestsUnderGroupingFolders();
    void buildGroupingFolderIsNotARealEntry();
    void buildMissingDirIsSingleNode();
    void buildCollapsedDirIsOneLeafRow();
    void buildDefaultCheckStates();
    void refreshPreservesUserCheckStates();
    void selectedFolderNestsItsContents();

private:
    static int st(SvnFileStatus s) { return static_cast<int>(s); }
    static QTreeWidgetItem *top(QTreeWidget *t, const QString &name)
    {
        for (int i = 0; i < t->topLevelItemCount(); ++i)
            if (t->topLevelItem(i)->text(0) == name)
                return t->topLevelItem(i);
        return nullptr;
    }
    static QTreeWidgetItem *kid(QTreeWidgetItem *p, const QString &name)
    {
        for (int i = 0; i < p->childCount(); ++i)
            if (p->child(i)->text(0) == name)
                return p->child(i);
        return nullptr;
    }
    static int countTop(QTreeWidget *t, const QString &name)
    {
        int n = 0;
        for (int i = 0; i < t->topLevelItemCount(); ++i)
            if (t->topLevelItem(i)->text(0) == name)
                ++n;
        return n;
    }
};

void TstCommitTree::commonAncestorSingleFile()
{
    // A non-existent file path -> its parent directory (pure path arithmetic).
    QCOMPARE(SvnUi::commonAncestorDir({QStringLiteral("/wc/src/main.cpp")}),
             QStringLiteral("/wc/src"));
}

void TstCommitTree::commonAncestorSingleDir()
{
    // A single selected directory roots at its PARENT, so the directory itself
    // becomes a folder node with its contents nested (not flattened beside them).
    QCOMPARE(SvnUi::commonAncestorDir({QStringLiteral("/wc/backup")}),
             QStringLiteral("/wc"));
}

void TstCommitTree::commonAncestorMultiple()
{
    QCOMPARE(SvnUi::commonAncestorDir({QStringLiteral("/wc/src/a.txt"),
                                       QStringLiteral("/wc/src/sub/b.txt")}),
             QStringLiteral("/wc/src"));
    QCOMPARE(SvnUi::commonAncestorDir({QStringLiteral("/wc/src/a.txt"),
                                       QStringLiteral("/wc/docs/b.txt")}),
             QStringLiteral("/wc"));
}

void TstCommitTree::cappedStopsEarly()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir d(tmp.path());
    for (int i = 0; i < 50; ++i) {
        QFile f(d.absoluteFilePath(QStringLiteral("f%1.txt").arg(i)));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    QStringList out;
    const bool within = SvnUi::collectUnversionedDirCapped(tmp.path(), out, 10);
    QVERIFY(!within);            // over the cap
    QVERIFY(out.size() <= 11);   // early exit: at most cap+1 collected, not all 50
}

void TstCommitTree::cappedListsWhenUnderCap()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir d(tmp.path());
    QVERIFY(d.mkdir(QStringLiteral("sub")));
    for (const QString &rel : {QStringLiteral("a.txt"), QStringLiteral("sub/b.txt")}) {
        QFile f(d.absoluteFilePath(rel));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    QStringList out;
    const bool within = SvnUi::collectUnversionedDirCapped(tmp.path(), out, 100);
    QVERIFY(within);
    // a.txt, sub, sub/b.txt
    QCOMPARE(out.size(), 3);
}

void TstCommitTree::descendantsParentBeforeChild()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir d(tmp.path());
    QVERIFY(d.mkdir(QStringLiteral("sub")));
    QFile f(d.absoluteFilePath(QStringLiteral("sub/b.txt")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    const QStringList paths = SvnUi::descendantPathsSorted(tmp.path());
    const int iSub = paths.indexOf(d.absoluteFilePath(QStringLiteral("sub")));
    const int iB   = paths.indexOf(d.absoluteFilePath(QStringLiteral("sub/b.txt")));
    QVERIFY(iSub >= 0 && iB >= 0);
    QVERIFY(iSub < iB);   // parent precedes child
}

void TstCommitTree::buildNestsUnderGroupingFolders()
{
    QTreeWidget tree;
    const QList<SvnUi::CommitRow> rows = {
        {QStringLiteral("/wc/root.txt"),      false, st(SvnFileStatus::Modified), false, false},
        {QStringLiteral("/wc/src/main.cpp"),  false, st(SvnFileStatus::Modified), false, false},
        {QStringLiteral("/wc/src/util/u.cpp"),false, st(SvnFileStatus::Modified), false, false},
    };
    SvnUi::buildCommitTree(&tree, rows, QStringLiteral("/wc"));

    QVERIFY(top(&tree, QStringLiteral("root.txt")));
    QTreeWidgetItem *src = top(&tree, QStringLiteral("src"));
    QVERIFY(src);
    QVERIFY(kid(src, QStringLiteral("main.cpp")));
    QTreeWidgetItem *util = kid(src, QStringLiteral("util"));
    QVERIFY(util);
    QVERIFY(kid(util, QStringLiteral("u.cpp")));
}

void TstCommitTree::buildGroupingFolderIsNotARealEntry()
{
    QTreeWidget tree;
    const QList<SvnUi::CommitRow> rows = {
        {QStringLiteral("/wc/src/main.cpp"), false, st(SvnFileStatus::Modified), false, false},
    };
    SvnUi::buildCommitTree(&tree, rows, QStringLiteral("/wc"));

    QTreeWidgetItem *src = top(&tree, QStringLiteral("src"));
    QVERIFY(src);
    QCOMPARE(src->data(0, Qt::UserRole + 4).toBool(), false);  // grouping folder
    QTreeWidgetItem *main = kid(src, QStringLiteral("main.cpp"));
    QVERIFY(main);
    QCOMPARE(main->data(0, Qt::UserRole + 4).toBool(), true);  // real entry
}

void TstCommitTree::buildMissingDirIsSingleNode()
{
    // A whole directory removed from disk: it plus its children come from svn
    // status as Missing. The directory is gone, so its isDir hint is false — the
    // builder must still treat it as one directory node holding its children, not
    // a duplicate leaf beside a grouping node (the earlier bug).
    QTreeWidget tree;
    const QList<SvnUi::CommitRow> rows = {
        {QStringLiteral("/wc/docs"),         false, st(SvnFileStatus::Missing), false, false},
        {QStringLiteral("/wc/docs/a.txt"),   false, st(SvnFileStatus::Missing), false, false},
        {QStringLiteral("/wc/docs/sub"),     false, st(SvnFileStatus::Missing), false, false},
        {QStringLiteral("/wc/docs/sub/b.txt"),false,st(SvnFileStatus::Missing), false, false},
    };
    SvnUi::buildCommitTree(&tree, rows, QStringLiteral("/wc"));

    QCOMPARE(countTop(&tree, QStringLiteral("docs")), 1);   // no duplicate
    QTreeWidgetItem *docs = top(&tree, QStringLiteral("docs"));
    QVERIFY(docs);
    QCOMPARE(docs->data(0, Qt::UserRole + 2).toBool(), true);  // is a directory
    QCOMPARE(docs->data(0, Qt::UserRole + 4).toBool(), true);  // real entry (deletable)
    QVERIFY(kid(docs, QStringLiteral("a.txt")));
    QTreeWidgetItem *sub = kid(docs, QStringLiteral("sub"));
    QVERIFY(sub);
    QVERIFY(kid(sub, QStringLiteral("b.txt")));
}

void TstCommitTree::buildCollapsedDirIsOneLeafRow()
{
    QTreeWidget tree;
    const QList<SvnUi::CommitRow> rows = {
        {QStringLiteral("/wc/build"), true, st(SvnFileStatus::Unversioned), true, true},
    };
    SvnUi::buildCommitTree(&tree, rows, QStringLiteral("/wc"));

    QTreeWidgetItem *build = top(&tree, QStringLiteral("build"));
    QVERIFY(build);
    QCOMPARE(build->data(0, Qt::UserRole + 5).toBool(), true);  // collapsed flag
    QCOMPARE(build->childCount(), 0);                           // contents not listed
}

void TstCommitTree::buildDefaultCheckStates()
{
    QTreeWidget tree;
    const QList<SvnUi::CommitRow> rows = {
        {QStringLiteral("/wc/src/mod.cpp"), false, st(SvnFileStatus::Modified),   false, false},
        {QStringLiteral("/wc/src/new.txt"), true,  st(SvnFileStatus::Unversioned),false, false},
    };
    SvnUi::buildCommitTree(&tree, rows, QStringLiteral("/wc"));

    QTreeWidgetItem *src = top(&tree, QStringLiteral("src"));
    QVERIFY(src);
    QCOMPARE(kid(src, QStringLiteral("mod.cpp"))->checkState(0), Qt::Checked);
    QCOMPARE(kid(src, QStringLiteral("new.txt"))->checkState(0), Qt::Unchecked);
    // Folder with one checked and one unchecked child -> partially checked.
    QCOMPARE(src->checkState(0), Qt::PartiallyChecked);
}

// The backend basis of the commit dialog's refresh: the user's check marks must
// survive a rebuild of the tree (this was the "checkmarks lost after delete" bug).
void TstCommitTree::refreshPreservesUserCheckStates()
{
    const QList<SvnUi::CommitRow> rows = {
        {QStringLiteral("/wc/src/mod.cpp"), false, st(SvnFileStatus::Modified),   false, false},
        {QStringLiteral("/wc/src/new.txt"), true,  st(SvnFileStatus::Unversioned),false, false},
    };
    QTreeWidget tree;
    SvnUi::buildCommitTree(&tree, rows, QStringLiteral("/wc"));

    // User changes the defaults: unchecks the modified file, checks the new one.
    QTreeWidgetItem *src = top(&tree, QStringLiteral("src"));
    QVERIFY(src);
    kid(src, QStringLiteral("mod.cpp"))->setCheckState(0, Qt::Unchecked);
    kid(src, QStringLiteral("new.txt"))->setCheckState(0, Qt::Checked);

    // Rebuild (as refresh does) and restore the snapshot.
    const QHash<QString, Qt::CheckState> saved = SvnUi::snapshotCheckStates(&tree);
    tree.clear();
    SvnUi::buildCommitTree(&tree, rows, QStringLiteral("/wc"));
    // Straight after the rebuild the defaults are back.
    src = top(&tree, QStringLiteral("src"));
    QCOMPARE(kid(src, QStringLiteral("mod.cpp"))->checkState(0), Qt::Checked);
    QCOMPARE(kid(src, QStringLiteral("new.txt"))->checkState(0), Qt::Unchecked);

    SvnUi::restoreCheckStates(&tree, saved);

    // The user's selection is preserved across the rebuild.
    src = top(&tree, QStringLiteral("src"));
    QCOMPARE(kid(src, QStringLiteral("mod.cpp"))->checkState(0), Qt::Unchecked);
    QCOMPARE(kid(src, QStringLiteral("new.txt"))->checkState(0), Qt::Checked);
}

// Reproduces the "flat single-folder commit" bug: committing a single
// unversioned folder. With the folder's PARENT as the display root
// (commonAncestorDir), the folder is one node and its files nest inside it,
// instead of appearing as siblings on the same level.
void TstCommitTree::selectedFolderNestsItsContents()
{
    const QList<SvnUi::CommitRow> rows = {
        {QStringLiteral("/wc/backup"),       true, st(SvnFileStatus::Unversioned), true,  false},
        {QStringLiteral("/wc/backup/a.zip"), true, st(SvnFileStatus::Unversioned), false, false},
        {QStringLiteral("/wc/backup/b.zip"), true, st(SvnFileStatus::Unversioned), false, false},
    };
    QTreeWidget tree;
    SvnUi::buildCommitTree(&tree, rows,
                           SvnUi::commonAncestorDir({QStringLiteral("/wc/backup")}));

    QCOMPARE(tree.topLevelItemCount(), 1);   // only "backup" at the top level
    QTreeWidgetItem *backup = top(&tree, QStringLiteral("backup"));
    QVERIFY(backup);
    QCOMPARE(backup->childCount(), 2);       // both zips nested inside backup
    QVERIFY(kid(backup, QStringLiteral("a.zip")));
    QVERIFY(kid(backup, QStringLiteral("b.zip")));
}

QTEST_MAIN(TstCommitTree)
#include "tst_committree.moc"
