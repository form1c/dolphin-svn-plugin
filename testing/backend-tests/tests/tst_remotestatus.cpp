#include "TestFixtures.h"
#include "svnmanager.h"

#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

// Tests for the "Check Repository" feature ('svn status -u'):
// status() with showUpdates=true must fill remoteTextStatus/remotePropStatus,
// including entries that are ONLY changed remotely (without showUpdates they
// would not appear in the result list at all). The existing behaviour WITHOUT
// -u must stay byte-identical (note: the other 11 suites already ran green
// unchanged).
class TstRemoteStatus : public QObject
{
    Q_OBJECT

private slots:
    void showUpdatesReportsRemoteModifiedFromSecondWorkingCopy();
    void defaultCallStillHidesRemoteOnlyChanges();
};

void TstRemoteStatus::showUpdatesReportsRemoteModifiedFromSecondWorkingCopy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString trunkUrl = repoUrl + QStringLiteral("/trunk");
    const QString wcA = dir.path() + QStringLiteral("/wcA");
    const QString wcB = dir.path() + QStringLiteral("/wcB");
    QVERIFY(TestFixtures::checkout(trunkUrl, wcA));
    QVERIFY(TestFixtures::checkout(trunkUrl, wcB));

    TestFixtures::writeFile(wcA + QStringLiteral("/f.txt"), QStringLiteral("v1\n"));
    QVERIFY(TestFixtures::svnAdd(wcA + QStringLiteral("/f.txt")));
    QVERIFY(TestFixtures::svnCommit(wcA, QStringLiteral("add f.txt"))); // r2

    TestFixtures::writeFile(wcA + QStringLiteral("/f.txt"), QStringLiteral("v2\n"));
    QVERIFY(TestFixtures::svnCommit(wcA, QStringLiteral("change f.txt"))); // r3

    // wcB never updates — its copy of f.txt is now out of date (Added at
    // r2 checkout time it never saw, so from wcB's perspective it doesn't
    // even have the file locally: remote-only "Added").
    SvnManager mgr;
    bool ok = false;
    const QList<SvnStatusEntry> entries =
        mgr.status(wcB, true, &ok, /*showUpdates=*/true);
    QVERIFY(ok);

    bool foundRemoteAdded = false;
    for (const SvnStatusEntry &e : entries) {
        if (QFileInfo(e.path).fileName() == QLatin1String("f.txt")) {
            QCOMPARE(e.remoteTextStatus, SvnFileStatus::Added);
            foundRemoteAdded = true;
        }
    }
    QVERIFY(foundRemoteAdded);

    // Now bring wcB up to r2 (file present, but still one revision behind
    // r3) so the "remote Modified" case (not remote Added) is also covered.
    QVERIFY(TestFixtures::runAsyncAndWait(mgr, [&]() {
                mgr.updateAsync(wcB, QStringLiteral("2"));
            }).success);

    const QList<SvnStatusEntry> entries2 =
        mgr.status(wcB, true, &ok, /*showUpdates=*/true);
    QVERIFY(ok);
    bool foundRemoteModified = false;
    for (const SvnStatusEntry &e : entries2) {
        if (QFileInfo(e.path).fileName() == QLatin1String("f.txt")) {
            QCOMPARE(e.remoteTextStatus, SvnFileStatus::Modified);
            foundRemoteModified = true;
        }
    }
    QVERIFY(foundRemoteModified);

    // Without showUpdates, wcB@r2 has no LOCAL modifications at all -> the
    // remote-only change must not appear (matches the earlier behaviour).
    const QList<SvnStatusEntry> plainEntries = mgr.status(wcB, true, &ok);
    QVERIFY(ok);
    QVERIFY(plainEntries.isEmpty());
}

void TstRemoteStatus::defaultCallStillHidesRemoteOnlyChanges()
{
    // showUpdates defaults to false -> existing callers keep seeing exactly the
    // old behaviour.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString trunkUrl = repoUrl + QStringLiteral("/trunk");
    const QString wcA = dir.path() + QStringLiteral("/wcA");
    const QString wcB = dir.path() + QStringLiteral("/wcB");
    QVERIFY(TestFixtures::checkout(trunkUrl, wcA));
    QVERIFY(TestFixtures::checkout(trunkUrl, wcB));

    TestFixtures::writeFile(wcA + QStringLiteral("/f.txt"), QStringLiteral("v1\n"));
    QVERIFY(TestFixtures::svnAdd(wcA + QStringLiteral("/f.txt")));
    QVERIFY(TestFixtures::svnCommit(wcA, QStringLiteral("add f.txt")));

    SvnManager mgr;
    bool ok = false;
    const QList<SvnStatusEntry> entries = mgr.status(wcB, true, &ok);
    QVERIFY(ok);
    QVERIFY(entries.isEmpty());
    for (const SvnStatusEntry &e : entries) {
        QCOMPARE(e.remoteTextStatus, SvnFileStatus::Unknown);
        QCOMPARE(e.remotePropStatus, SvnFileStatus::Unknown);
    }
}

QTEST_GUILESS_MAIN(TstRemoteStatus)
#include "tst_remotestatus.moc"
