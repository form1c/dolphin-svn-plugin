#include "TestFixtures.h"
#include "svnmanager.h"

#include <QTemporaryDir>
#include <QTest>

// Tests for SvnManager::log() / parseLogXml():
// - '-g' nesting (nested <logentry> -> mergeDepth), ordering
// - '--limit' counts only top-level entries with '-g'
// - fromRev/toRev-Spannen, --stop-on-copy
class TstParseLog : public QObject
{
    Q_OBJECT

private slots:
    void mergeNestingAndOrder();
    void limitCountsOnlyTopLevelWithG();
    void fromToRevSpan();
    void stopOnCopy();
};

void TstParseLog::mergeNestingAndOrder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::MergeScenario s = TestFixtures::makeMergedBranchScenario(dir.path());

    SvnManager mgr;
    SvnLogOptions opts;
    opts.includeMerged = true;
    const QList<SvnLogEntry> entries = mgr.log(s.trunkWc, 100, QString(), QString(), opts);

    // Expected order: r6(depth0), r5,r4,r3(depth1, children of r6), r2,r1(depth0)
    QCOMPARE(entries.size(), 6);
    QCOMPARE(entries[0].revision, QStringLiteral("6"));
    QCOMPARE(entries[0].mergeDepth, 0);
    QCOMPARE(entries[1].revision, QStringLiteral("5"));
    QVERIFY(entries[1].mergeDepth > 0);
    QCOMPARE(entries[2].revision, QStringLiteral("4"));
    QVERIFY(entries[2].mergeDepth > 0);
    QCOMPARE(entries[3].revision, QStringLiteral("3"));
    QVERIFY(entries[3].mergeDepth > 0);
    QCOMPARE(entries[4].revision, QStringLiteral("2"));
    QCOMPARE(entries[4].mergeDepth, 0);
    QCOMPARE(entries[5].revision, QStringLiteral("1"));
    QCOMPARE(entries[5].mergeDepth, 0);
}

void TstParseLog::limitCountsOnlyTopLevelWithG()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::MergeScenario s = TestFixtures::makeMergedBranchScenario(dir.path());

    SvnManager mgr;
    SvnLogOptions opts;
    opts.includeMerged = true;
    // limit=2 must NOT count the 3 nested children of r6 ->
    // result: r6 + children (r5,r4,r3) + r2 = 5 entries, 2 of them depth0.
    const QList<SvnLogEntry> entries = mgr.log(s.trunkWc, 2, QString(), QString(), opts);

    int topLevel = 0;
    for (const SvnLogEntry &e : entries) {
        if (e.mergeDepth == 0)
            ++topLevel;
    }
    QCOMPARE(topLevel, 2);
    QCOMPARE(entries.size(), 5);
    QCOMPARE(entries.last().revision, QStringLiteral("2"));
}

void TstParseLog::fromToRevSpan()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::MergeScenario s = TestFixtures::makeMergedBranchScenario(dir.path());

    SvnManager mgr;
    const QList<SvnLogEntry> entries = mgr.log(s.trunkWc, 100, QStringLiteral("4"), QStringLiteral("2"));
    // Without -g: only revisions that directly affect trunk, in the range 2..4.
    // r3 (branch copy) and r4 (branch commit) do NOT directly affect trunk.
    QVERIFY(!entries.isEmpty());
    for (const SvnLogEntry &e : entries) {
        const int rev = e.revision.toInt();
        QVERIFY(rev >= 2 && rev <= 4);
    }
}

void TstParseLog::stopOnCopy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::MergeScenario s = TestFixtures::makeMergedBranchScenario(dir.path());

    SvnManager mgr;
    SvnLogOptions opts;
    opts.stopOnCopy = true;
    // Log on the branch with --stop-on-copy must not contain the trunk history
    // BEFORE the branch was created (r2, r1).
    const QList<SvnLogEntry> entries = mgr.log(s.branchWc, 100, QString(), QString(), opts);
    for (const SvnLogEntry &e : entries) {
        QVERIFY(e.revision.toInt() >= 3);
    }
    QVERIFY(!entries.isEmpty());
}

QTEST_GUILESS_MAIN(TstParseLog)
#include "tst_parselog.moc"
