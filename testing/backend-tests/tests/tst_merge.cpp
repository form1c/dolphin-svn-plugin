#include "TestFixtures.h"
#include "svnmanager.h"

#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

// Tests for mergeRangeAsync() behaviour on a conflict (see memory
// "Nachtrag 2", 2026-07-19):
// - contiguous range (no conflict) -> exit 0 / success=true
// - interrupted merge (-c 4,7 with a conflict at 4) -> E155015/exit 1,
//   the status still shows 'conflicted' (the basis of the post-merge hook!)
class TstMerge : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void contiguousRangeSucceeds();
    void interruptedRangeFailsButLeavesConflictStatus();
    void resolveThenRerunAppliesRemainingRevisions();
};

void TstMerge::initTestCase()
{
    TestFixtures::registerMetaTypes();
}

void TstMerge::contiguousRangeSucceeds()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::MergeScenario s = TestFixtures::makeMergeRangeScenario(dir.path());

    SvnManager mgr;
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.mergeRangeAsync(s.branchUrl, QStringLiteral("6,7"), s.trunkWc);
    });

    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    bool ok = false;
    const QList<SvnStatusEntry> entries = mgr.status(s.trunkWc, true, &ok);
    QVERIFY(ok);
    for (const SvnStatusEntry &e : entries)
        QVERIFY(e.textStatus != SvnFileStatus::Conflicted);
}

void TstMerge::interruptedRangeFailsButLeavesConflictStatus()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::MergeScenario s = TestFixtures::makeMergeRangeScenario(dir.path());

    SvnManager mgr;
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.mergeRangeAsync(s.branchUrl, QStringLiteral("4,7"), s.trunkWc);
    });

    // r4 produces a conflict -> svn merge aborts with E155015, r7 is NO longer
    // applied. This is the expected (not the faulty) behaviour of
    // 'svn merge -c'.
    QVERIFY(!result.success);
    QVERIFY2(result.errorMessage.contains(QStringLiteral("E155015")),
             qUtf8Printable(result.errorMessage));

    // Despite exit != 0 the status MUST show conflicts — this is the basis of
    // the post-merge hook (svnplugin.cpp), which on !success
    // additionally checks the status instead of aborting immediately.
    bool ok = false;
    const QList<SvnStatusEntry> entries = mgr.status(s.trunkWc, true, &ok);
    QVERIFY(ok);
    bool foundConflict = false;
    for (const SvnStatusEntry &e : entries) {
        if (QFileInfo(e.path).fileName() == QLatin1String("file1.txt")
            && e.textStatus == SvnFileStatus::Conflicted)
            foundConflict = true;
    }
    QVERIFY(foundConflict);
}

// "Continue Merge": after resolving the conflict, an IDENTICAL second
// svn-merge call (thanks to merge tracking) applies only the remaining
// revisions — the already-merged r4 is a no-op. This is the backend basis
// of the "Continue Merge" button (svnconflictdialog / svnplugin startMerge).
void TstMerge::resolveThenRerunAppliesRemainingRevisions()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::MergeScenario s = TestFixtures::makeMergeRangeScenario(dir.path());

    SvnManager mgr;

    // -c 4,6: r4 changes file1.txt (collides with trunk's r5 → conflict),
    // r6 changes file2.txt independently and applies cleanly on its own.
    // (r7 depends on r6 and would conflict on its own — hence r6.)

    // 1. Merge -c 4,6 aborts at r4 with a conflict (E155015); r6 open.
    const SvnOperationResult first = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.mergeRangeAsync(s.branchUrl, QStringLiteral("4,6"), s.trunkWc);
    });
    QVERIFY(!first.success);
    QVERIFY2(first.errorMessage.contains(QStringLiteral("E155015")),
             qUtf8Printable(first.errorMessage));

    // So far only r4 counts as merged.
    bool okBefore = false;
    const QSet<long long> mergedBefore =
        mgr.mergedRevisionsSync(s.branchUrl, s.trunkWc,
                                QStringLiteral("merged"), &okBefore);
    QVERIFY(okBefore);
    QVERIFY(mergedBefore.contains(4));
    QVERIFY(!mergedBefore.contains(6));

    // 2. Determine the conflict file and resolve it with --accept working.
    QString conflictedPath;
    bool okStatus = false;
    for (const SvnStatusEntry &e : mgr.status(s.trunkWc, true, &okStatus)) {
        if (e.textStatus == SvnFileStatus::Conflicted)
            conflictedPath = e.path;
    }
    QVERIFY(okStatus);
    QVERIFY(!conflictedPath.isEmpty());
    const SvnOperationResult resolved = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.resolveAsync({conflictedPath}, QStringLiteral("working"));
    });
    QVERIFY2(resolved.success, qUtf8Printable(resolved.errorMessage));

    // 3. Identical second merge -c 4,6: r4 is a no-op, r6 is applied → exit 0.
    const SvnOperationResult second = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.mergeRangeAsync(s.branchUrl, QStringLiteral("4,6"), s.trunkWc);
    });
    QVERIFY2(second.success, qUtf8Printable(second.errorMessage));

    // 4. Mergeinfo now contains r4 AND r6.
    bool okAfter = false;
    const QSet<long long> mergedAfter =
        mgr.mergedRevisionsSync(s.branchUrl, s.trunkWc,
                                QStringLiteral("merged"), &okAfter);
    QVERIFY(okAfter);
    QVERIFY(mergedAfter.contains(4));
    QVERIFY(mergedAfter.contains(6));

    // 5. No unresolved conflicts remain.
    bool okFinal = false;
    for (const SvnStatusEntry &e : mgr.status(s.trunkWc, true, &okFinal))
        QVERIFY(e.textStatus != SvnFileStatus::Conflicted);
    QVERIFY(okFinal);
}

QTEST_GUILESS_MAIN(TstMerge)
#include "tst_merge.moc"
