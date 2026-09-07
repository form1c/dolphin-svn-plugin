#include "TestFixtures.h"
#include "svnmanager.h"

#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

// Tests for conflictFilesSync(), treeConflictDescriptionSync() and
// mergedRevisionsSync() (merged AND eligible).
class TstConflicts : public QObject
{
    Q_OBJECT

private slots:
    void conflictFilesFromUpdateConflict();
    void conflictFilesFromMergeConflict();
    void treeConflictDescription();
    void mergedRevisions();
    void eligibleRevisions();
};

void TstConflicts::conflictFilesFromUpdateConflict()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::UpdateConflictScenario s = TestFixtures::makeUpdateConflictScenario(dir.path());

    SvnManager mgr;
    QString base, mine, theirs;
    QVERIFY(mgr.conflictFilesSync(s.wcB + QStringLiteral("/conflict.txt"), &base, &mine, &theirs));
    QVERIFY(QFileInfo::exists(base));
    QVERIFY(QFileInfo::exists(mine));
    QVERIFY(QFileInfo::exists(theirs));
}

void TstConflicts::conflictFilesFromMergeConflict()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::MergeScenario s = TestFixtures::makeConflictScenario(dir.path());

    SvnManager mgr;
    QString base, mine, theirs;
    QVERIFY(mgr.conflictFilesSync(s.trunkWc + QStringLiteral("/conflict.txt"), &base, &mine, &theirs));
    QVERIFY(QFileInfo::exists(base));
    QVERIFY(QFileInfo::exists(mine));
    QVERIFY(QFileInfo::exists(theirs));
}

void TstConflicts::treeConflictDescription()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::MergeScenario s = TestFixtures::makeConflictScenario(dir.path());

    SvnManager mgr;
    const QString desc = mgr.treeConflictDescriptionSync(s.trunkWc + QStringLiteral("/fileB.txt"));
    QVERIFY(!desc.isEmpty());
    QVERIFY2(desc.contains(QStringLiteral("incoming")), qUtf8Printable(desc));
    QVERIFY2(desc.contains(QStringLiteral("local")), qUtf8Printable(desc));
}

void TstConflicts::mergedRevisions()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::MergeScenario s = TestFixtures::makeMergedBranchScenario(dir.path());

    SvnManager mgr;
    const QSet<long long> merged = mgr.mergedRevisionsSync(s.branchUrl, s.trunkWc, QStringLiteral("merged"));
    QVERIFY(merged.contains(4));
    QVERIFY(merged.contains(5));

    // Fully merged branch -> the eligible set is empty, but ok stays true. That
    // is a valid result, not an error case (the caller then greys out everything).
    bool ok = false;
    const QSet<long long> eligible = mgr.mergedRevisionsSync(
        s.branchUrl, s.trunkWc, QStringLiteral("eligible"), &ok);
    QVERIFY(ok);
    QVERIFY(eligible.isEmpty());

    // Error case: unknown source → ok=false (the caller greys out nothing).
    bool badOk = true;
    mgr.mergedRevisionsSync(s.repoUrl + QStringLiteral("/branches/missing"),
                            s.trunkWc, QStringLiteral("eligible"), &badOk);
    QVERIFY(!badOk);
}

void TstConflicts::eligibleRevisions()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::MergeScenario s = TestFixtures::makeEligibleBranchScenario(dir.path());

    SvnManager mgr;
    const QSet<long long> eligible = mgr.mergedRevisionsSync(s.branchUrl, s.trunkWc, QStringLiteral("eligible"));
    QVERIFY(eligible.contains(4));
    QVERIFY(eligible.contains(5));

    const QSet<long long> merged = mgr.mergedRevisionsSync(s.branchUrl, s.trunkWc, QStringLiteral("merged"));
    QVERIFY(merged.isEmpty());
}

QTEST_GUILESS_MAIN(TstConflicts)
#include "tst_conflicts.moc"
