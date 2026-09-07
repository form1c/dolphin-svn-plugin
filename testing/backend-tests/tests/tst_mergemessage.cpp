#include "TestFixtures.h"
#include "svnmanager.h"

#include <QTemporaryDir>
#include <QTest>

// Tests for SvnManager::buildMergeMessage() (F-ARCH1): the comment appendix of
// the merge template, extracted from the merge-action lambda.
// Covers: explicit ranges, empty field (eligible), a revision without a log
// match, ascending order + "---" separation, cap-50 behaviour.
class TstMergeMessage : public QObject
{
    Q_OBJECT

private slots:
    void explicitRangesAscendingWithSeparators();
    void emptyRangesUsesEligible();
    void revisionWithoutLogHitIsSkipped();
    void capFiftyReportsOverflow();
};

void TstMergeMessage::explicitRangesAscendingWithSeparators()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::MergeScenario s =
        TestFixtures::makeEligibleBranchScenario(dir.path());

    SvnManager mgr;
    const SvnMergeMessageParts parts =
        mgr.buildMergeMessage(s.branchUrl, QStringLiteral("4,5"), s.trunkWc);

    // The author is environment-dependent (the running OS user) → fetch it
    // dynamically from the log, so the test does not depend on the user name.
    const QList<SvnLogEntry> entries =
        mgr.log(s.branchUrl, 1000, QStringLiteral("5"), QStringLiteral("4"));
    QString author;
    for (const SvnLogEntry &e : entries)
        if (e.revision == QStringLiteral("4"))
            author = e.author;
    QVERIFY(!author.isEmpty());

    // Byte-identisch: Leerzeile + "---" + "rN (autor): message", aufsteigend.
    const QString expected =
        QStringLiteral("\n\n---\nr4 (%1): Branch change 1"
                       "\n\n---\nr5 (%1): Branch change 2").arg(author);
    QCOMPARE(parts.commentsAppendix, expected);
    QCOMPARE(parts.overflowCount, 0);
}

void TstMergeMessage::emptyRangesUsesEligible()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::MergeScenario s =
        TestFixtures::makeEligibleBranchScenario(dir.path());

    SvnManager mgr;
    // Empty ranges field → eligible revisions via mergedRevisionsSync. Since the
    // branch was copied from trunk@r2, 'svn mergeinfo --show-revs eligible'
    // reports r2, r4 AND r5 as open here — so the eligible set must produce the
    // same result as the explicit "2,4,5".
    const SvnMergeMessageParts fromEmpty =
        mgr.buildMergeMessage(s.branchUrl, QString(), s.trunkWc);
    const SvnMergeMessageParts fromExplicit =
        mgr.buildMergeMessage(s.branchUrl, QStringLiteral("2,4,5"), s.trunkWc);

    QCOMPARE(fromEmpty.commentsAppendix, fromExplicit.commentsAppendix);
    QVERIFY(fromEmpty.commentsAppendix.contains(QStringLiteral("Branch change 1")));
    QVERIFY(fromEmpty.commentsAppendix.contains(QStringLiteral("Branch change 2")));
    QCOMPARE(fromEmpty.overflowCount, 0);
}

void TstMergeMessage::revisionWithoutLogHitIsSkipped()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::MergeScenario s =
        TestFixtures::makeEligibleBranchScenario(dir.path());

    SvnManager mgr;
    // On the trunk path, r4/r5 (pure branch commits) have NO log entry; only r2
    // ("Add file1.txt") matches. Revisions without a match are silently dropped.
    const SvnMergeMessageParts parts =
        mgr.buildMergeMessage(s.trunkUrl, QStringLiteral("2,4,5"), s.trunkWc);

    QVERIFY(parts.commentsAppendix.contains(QStringLiteral("Add file1.txt")));
    QVERIFY(!parts.commentsAppendix.contains(QStringLiteral("Branch change")));
    // Genau ein Kommentarblock → genau eine "---"-Trennung.
    QCOMPARE(parts.commentsAppendix.count(QStringLiteral("\n\n---\n")), 1);
    QCOMPARE(parts.overflowCount, 0);
}

void TstMergeMessage::capFiftyReportsOverflow()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // A small dedicated scenario: trunk with 52 content commits (r2..r53), so
    // the cap-50 limit is exceeded.
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    QVERIFY(TestFixtures::runSvn({QStringLiteral("mkdir"),
                                  repoUrl + QStringLiteral("/trunk"),
                                  QStringLiteral("-m"),
                                  QStringLiteral("Create trunk")})); // r1
    const QString trunkUrl = repoUrl + QStringLiteral("/trunk");
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(trunkUrl, wc));

    const QString file = wc + QStringLiteral("/f.txt");
    TestFixtures::writeFile(file, QStringLiteral("v0\n"));
    QVERIFY(TestFixtures::svnAdd(file));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("commit 2"))); // r2
    for (int rev = 3; rev <= 53; ++rev) {
        TestFixtures::writeFile(file,
            QStringLiteral("v%1\n").arg(rev));
        QVERIFY(TestFixtures::svnCommit(wc,
            QStringLiteral("commit %1").arg(rev))); // r3..r53
    }

    SvnManager mgr;
    // 52 revisions in the set (r2..r53), cap 50 → 50 blocks, overflow 2.
    const SvnMergeMessageParts parts =
        mgr.buildMergeMessage(trunkUrl, QStringLiteral("2-53"), wc);

    QCOMPARE(parts.overflowCount, 2);
    QCOMPARE(parts.commentsAppendix.count(QStringLiteral("\n\n---\n")), 50);
    // The 50 lowest are kept (ascending): r2..r51.
    QVERIFY(parts.commentsAppendix.contains(QStringLiteral("r2 (")));
    QVERIFY(parts.commentsAppendix.contains(QStringLiteral("r51 (")));
    QVERIFY(!parts.commentsAppendix.contains(QStringLiteral("r52 (")));
    // Ascending order.
    QVERIFY(parts.commentsAppendix.indexOf(QStringLiteral("r2 ("))
            < parts.commentsAppendix.indexOf(QStringLiteral("r51 (")));
}

QTEST_GUILESS_MAIN(TstMergeMessage)
#include "tst_mergemessage.moc"
