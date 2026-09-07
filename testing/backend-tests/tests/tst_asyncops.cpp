#include "TestFixtures.h"
#include "svnmanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

// Tests for the asynchronous core of SvnManager: updateAsync, commitAsync
// (incl. the revision number in the result — a historical bug), revertAsync,
// addSync+removeAsync, moveAsync, resolveAsync, mergeTreesAsync,
// busy behaviour (another historical bug) and cancelAsync.
class TstAsyncOps : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void updateAsyncPullsRemoteChange();
    void commitAsyncReturnsNewRevision();
    void commitAsyncUnicodeMessageRoundTrips();
    void commitAsyncDepthEmptyDoesNotRecurse();
    void commitAsyncKeepLocksPreservesLock();
    void revertAsyncRestoresContent();
    void addSyncThenRemoveAsyncKeepsLocalFile();
    void removeAsyncOnMissingFileSchedulesDeletion();
    void removeAsyncOnMultipleMissingPathsSchedulesAll();
    void removeAsyncKeepLocalHandlesLocalModifications();
    void removeAsyncForceHandlesLocalModifications();
    void moveAsyncPreservesContentAndHistory();
    void addAsyncPutsFileUnderVersionControl();
    void resolveAsyncClearsConflict();
    void cleanupAsyncSucceedsOnWorkingCopy();
    void mergeTreesAsyncAppliesChange();
    void busySecondOperationIsRejectedImmediately();
    void cancelAsyncLeavesNotBusy();
};

void TstAsyncOps::initTestCase()
{
    TestFixtures::registerMetaTypes();
}

void TstAsyncOps::updateAsyncPullsRemoteChange()
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
    QVERIFY(TestFixtures::svnCommit(wcA, QStringLiteral("add f.txt")));

    SvnManager mgr;
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.updateAsync(wcB);
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    QFile f(wcB + QStringLiteral("/f.txt"));
    QVERIFY(f.exists());
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("v1\n"));
}

void TstAsyncOps::commitAsyncReturnsNewRevision()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    SvnManager mgr;
    TestFixtures::writeFile(wc + QStringLiteral("/f.txt"), QStringLiteral("content\n"));
    QVERIFY(mgr.addSync({wc + QStringLiteral("/f.txt")}));

    // Historical bug: the readyRead handler consumed stdout via readLine, the
    // finished handler saw only the rest -> the regex never found "Committed
    // revision N". Fixed. A second historical bug: the regex matched only the
    // ENGLISH svn message; under a German SVN locale newRevision stayed empty. Fixed
    // by LC_MESSAGES=C for all svn subprocesses (cLocaleEnvironment in
    // svnmanager.cpp), which keeps the diagnostic text English while LC_CTYPE
    // stays UTF-8 (see commitAsyncUnicodeMessageRoundTrips).
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.commitAsync({wc + QStringLiteral("/f.txt")}, QStringLiteral("Add f.txt"));
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    QVERIFY(!result.newRevision.isEmpty());

    // Reliable (locale-independent): determine the actual new revision via
    // info() on the COMMITTED FILE (not the WC root — after a single-file commit
    // that may stay mixed-revision and would show an older revision, see the
    // fixture gotcha in TestFixtures.cpp).
    const SvnInfo infoAfter = mgr.info(wc + QStringLiteral("/f.txt"));
    QVERIFY(infoAfter.valid);
    bool ok = false;
    const int rev = infoAfter.revision.toInt(&ok);
    QVERIFY(ok);
    QVERIFY(rev >= 2); // r1 = layout
}

// Bug: a commit message with non-ASCII characters (öäüß, accents, other
// scripts) failed with "svn: E000022: Can't convert string from native encoding
// to 'UTF-8'". Cause: LC_ALL=C forced svn's native charset to ASCII. Fixed by
// keeping a UTF-8 LC_CTYPE while only LC_MESSAGES stays C.
void TstAsyncOps::commitAsyncUnicodeMessageRoundTrips()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    SvnManager mgr;
    TestFixtures::writeFile(wc + QStringLiteral("/f.txt"), QStringLiteral("content\n"));
    QVERIFY(mgr.addSync({wc + QStringLiteral("/f.txt")}));

    const QString message =
        QString::fromUtf8("Änderung öäüß café 日本語");
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.commitAsync({wc + QStringLiteral("/f.txt")}, message);
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    // The stored log message must round-trip byte for byte.
    const auto entries = mgr.log(wc + QStringLiteral("/f.txt"), 1);
    QVERIFY(!entries.isEmpty());
    QCOMPARE(entries.first().message, message);
}

// The commit dialog lists the exact items to commit and relies on
// 'svn commit --depth empty' to not pull in unchecked siblings that live under
// a listed folder. This guards that behaviour of commitAsync.
void TstAsyncOps::commitAsyncDepthEmptyDoesNotRecurse()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    SvnManager mgr;
    QDir(wc).mkdir(QStringLiteral("d"));
    TestFixtures::writeFile(wc + QStringLiteral("/d/a.txt"), QStringLiteral("a\n"));
    TestFixtures::writeFile(wc + QStringLiteral("/d/b.txt"), QStringLiteral("b\n"));
    QVERIFY(mgr.addSync({wc + QStringLiteral("/d")}, QStringLiteral("infinity")));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("base")));

    // Modify both files inside the (otherwise unchanged) folder.
    TestFixtures::writeFile(wc + QStringLiteral("/d/a.txt"), QStringLiteral("a2\n"));
    TestFixtures::writeFile(wc + QStringLiteral("/d/b.txt"), QStringLiteral("b2\n"));

    const auto textStatus = [&](const QString &rel) {
        const QString abs = QFileInfo(wc + QLatin1Char('/') + rel).absoluteFilePath();
        for (const SvnStatusEntry &e : mgr.status(wc)) {
            if (QFileInfo(e.path).absoluteFilePath() == abs)
                return e.textStatus;
        }
        return SvnFileStatus::Unknown;
    };

    // Committing the FOLDER with --depth empty must not commit the files inside.
    SvnOperationResult res = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.commitAsync({wc + QStringLiteral("/d")}, QStringLiteral("folder only"));
    });
    QVERIFY2(res.success, qUtf8Printable(res.errorMessage));
    QCOMPARE(textStatus(QStringLiteral("d/a.txt")), SvnFileStatus::Modified);
    QCOMPARE(textStatus(QStringLiteral("d/b.txt")), SvnFileStatus::Modified);

    // Committing a single file commits exactly that file, the sibling remains.
    res = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.commitAsync({wc + QStringLiteral("/d/a.txt")}, QStringLiteral("a only"));
    });
    QVERIFY2(res.success, qUtf8Printable(res.errorMessage));
    QCOMPARE(textStatus(QStringLiteral("d/a.txt")), SvnFileStatus::Unknown); // committed
    QCOMPARE(textStatus(QStringLiteral("d/b.txt")), SvnFileStatus::Modified);
}

void TstAsyncOps::commitAsyncKeepLocksPreservesLock()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    const QString filePath = wc + QStringLiteral("/f.txt");
    TestFixtures::writeFile(filePath, QStringLiteral("v1\n"));
    QVERIFY(TestFixtures::svnAdd(filePath));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("add f.txt")));

    SvnManager mgr;
    QVERIFY2(TestFixtures::runAsyncAndWait(mgr, [&]() {
                 mgr.lockAsync({filePath}, QStringLiteral("locking"));
             }).success, "lockAsync failed");

    TestFixtures::writeFile(filePath, QStringLiteral("v2\n"));

    // keepLocks=true (--no-unlock): the lock must survive the commit.
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.commitAsync({filePath}, QStringLiteral("v2"), /*keepLocks=*/true);
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    bool ok = false;
    const QList<SvnStatusEntry> entries = mgr.status(wc, true, &ok);
    QVERIFY(ok);
    bool foundLocked = false;
    for (const SvnStatusEntry &e : entries) {
        if (QFileInfo(e.path).fileName() == QLatin1String("f.txt"))
            foundLocked = e.isLocked;
    }
    QVERIFY(foundLocked);

    // Counter-check: the default (keepLocks=false) releases the lock.
    TestFixtures::writeFile(filePath, QStringLiteral("v3\n"));
    const SvnOperationResult result2 = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.commitAsync({filePath}, QStringLiteral("v3"));
    });
    QVERIFY2(result2.success, qUtf8Printable(result2.errorMessage));

    const QList<SvnStatusEntry> entries2 = mgr.status(wc, true, &ok);
    QVERIFY(ok);
    bool stillLocked = false;
    for (const SvnStatusEntry &e : entries2) {
        if (QFileInfo(e.path).fileName() == QLatin1String("f.txt"))
            stillLocked = e.isLocked;
    }
    QVERIFY(!stillLocked);
}

void TstAsyncOps::revertAsyncRestoresContent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    const QString filePath = wc + QStringLiteral("/f.txt");
    TestFixtures::writeFile(filePath, QStringLiteral("original\n"));
    QVERIFY(TestFixtures::svnAdd(filePath));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("add f.txt")));

    TestFixtures::writeFile(filePath, QStringLiteral("LOCALLY CHANGED\n"));

    SvnManager mgr;
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.revertAsync({filePath});
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    QFile f(filePath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("original\n"));
}

void TstAsyncOps::addSyncThenRemoveAsyncKeepsLocalFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    const QString filePath = wc + QStringLiteral("/f.txt");
    TestFixtures::writeFile(filePath, QStringLiteral("committed\n"));
    QVERIFY(TestFixtures::svnAdd(filePath));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("add f.txt")));

    SvnManager mgr;
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.removeAsync({filePath}, /*keepLocal=*/true);
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    // keepLocal=true: the file stays on disk but is scheduled for deletion.
    QVERIFY(QFileInfo::exists(filePath));
    bool ok = false;
    const QList<SvnStatusEntry> entries = mgr.status(wc, true, &ok);
    QVERIFY(ok);
    bool foundDeleted = false;
    for (const SvnStatusEntry &e : entries) {
        if (QFileInfo(e.path).fileName() == QLatin1String("f.txt")
            && e.textStatus == SvnFileStatus::Deleted)
            foundDeleted = true;
    }
    QVERIFY(foundDeleted);
}

// Backend basis of the commit dialog's "Delete (schedule removal)" action for
// a missing file: a versioned file removed from disk shows status Missing, and
// removeAsync turns it into a committable Deleted entry.
void TstAsyncOps::removeAsyncOnMissingFileSchedulesDeletion()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    const QString filePath = wc + QStringLiteral("/gone.txt");
    TestFixtures::writeFile(filePath, QStringLiteral("committed\n"));
    QVERIFY(TestFixtures::svnAdd(filePath));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("add gone.txt")));

    // Delete the file behind SVN's back — this is exactly the state that shows
    // up as "Missing" in the commit dialog.
    QVERIFY(QFile::remove(filePath));

    SvnManager mgr;
    bool okBefore = false;
    bool foundMissing = false;
    for (const SvnStatusEntry &e : mgr.status(wc, true, &okBefore)) {
        if (QFileInfo(e.path).fileName() == QLatin1String("gone.txt")
            && e.textStatus == SvnFileStatus::Missing)
            foundMissing = true;
    }
    QVERIFY(okBefore);
    QVERIFY(foundMissing);

    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.removeAsync({filePath}, /*keepLocal=*/false);
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    bool okAfter = false;
    bool foundDeleted = false;
    for (const SvnStatusEntry &e : mgr.status(wc, true, &okAfter)) {
        if (QFileInfo(e.path).fileName() == QLatin1String("gone.txt")
            && e.textStatus == SvnFileStatus::Deleted)
            foundDeleted = true;
    }
    QVERIFY(okAfter);
    QVERIFY(foundDeleted);
}

// Backend basis of the commit dialog's multi-selection delete: passing several
// missing paths to removeAsync in one call schedules every one of them.
void TstAsyncOps::removeAsyncOnMultipleMissingPathsSchedulesAll()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    const QStringList names = {QStringLiteral("one.txt"),
                               QStringLiteral("two.txt"),
                               QStringLiteral("three.txt")};
    QStringList paths;
    for (const QString &n : names) {
        const QString p = wc + QLatin1Char('/') + n;
        TestFixtures::writeFile(p, QStringLiteral("x\n"));
        QVERIFY(TestFixtures::svnAdd(p));
        paths << p;
    }
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("add three files")));

    // Remove all three from disk behind SVN's back -> all Missing.
    for (const QString &p : paths)
        QVERIFY(QFile::remove(p));

    SvnManager mgr;
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.removeAsync(paths, /*keepLocal=*/false);
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    bool ok = false;
    int deleted = 0;
    for (const SvnStatusEntry &e : mgr.status(wc, true, &ok)) {
        if (names.contains(QFileInfo(e.path).fileName())
            && e.textStatus == SvnFileStatus::Deleted)
            ++deleted;
    }
    QVERIFY(ok);
    QCOMPARE(deleted, names.size());
}

// Backend basis of the Delete dialog's "Keep local copy" option: --keep-local
// removes an item from version control even when it has local modifications
// (the E195006 case) and leaves the file on disk.
void TstAsyncOps::removeAsyncKeepLocalHandlesLocalModifications()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    const QString filePath = wc + QStringLiteral("/f.txt");
    TestFixtures::writeFile(filePath, QStringLiteral("committed\n"));
    QVERIFY(TestFixtures::svnAdd(filePath));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("add f.txt")));
    TestFixtures::writeFile(filePath, QStringLiteral("locally modified\n"));

    SvnManager mgr;
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.removeAsync({filePath}, /*keepLocal=*/true);
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));
    QVERIFY(QFileInfo::exists(filePath)); // file kept on disk

    bool ok = false;
    bool foundDeleted = false;
    for (const SvnStatusEntry &e : mgr.status(wc, true, &ok)) {
        if (QFileInfo(e.path).fileName() == QLatin1String("f.txt")
            && e.textStatus == SvnFileStatus::Deleted)
            foundDeleted = true;
    }
    QVERIFY(ok);
    QVERIFY(foundDeleted);
}

// Backend basis of the Delete dialog's force retry: --force deletes an item even
// when it has local modifications, discarding them and removing the file.
void TstAsyncOps::removeAsyncForceHandlesLocalModifications()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    const QString filePath = wc + QStringLiteral("/f.txt");
    TestFixtures::writeFile(filePath, QStringLiteral("committed\n"));
    QVERIFY(TestFixtures::svnAdd(filePath));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("add f.txt")));
    TestFixtures::writeFile(filePath, QStringLiteral("locally modified\n"));

    SvnManager mgr;
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.removeAsync({filePath}, /*keepLocal=*/false, /*force=*/true);
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));
    QVERIFY(!QFileInfo::exists(filePath)); // force discards the local file

    bool ok = false;
    bool foundDeleted = false;
    for (const SvnStatusEntry &e : mgr.status(wc, true, &ok)) {
        if (QFileInfo(e.path).fileName() == QLatin1String("f.txt")
            && e.textStatus == SvnFileStatus::Deleted)
            foundDeleted = true;
    }
    QVERIFY(ok);
    QVERIFY(foundDeleted);
}

void TstAsyncOps::moveAsyncPreservesContentAndHistory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    const QString oldPath = wc + QStringLiteral("/old.txt");
    const QString newPath = wc + QStringLiteral("/new.txt");
    TestFixtures::writeFile(oldPath, QStringLiteral("payload\n"));
    QVERIFY(TestFixtures::svnAdd(oldPath));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("add old.txt")));

    SvnManager mgr;
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.moveAsync(oldPath, newPath);
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    QVERIFY(!QFileInfo::exists(oldPath));
    QVERIFY(QFileInfo::exists(newPath));
    QFile f(newPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("payload\n"));

    bool ok = false;
    const QList<SvnStatusEntry> entries = mgr.status(wc, true, &ok);
    QVERIFY(ok);
    bool foundAddedNewPath = false;
    for (const SvnStatusEntry &e : entries) {
        if (QFileInfo(e.path).fileName() == QLatin1String("new.txt")
            && e.textStatus == SvnFileStatus::Added)
            foundAddedNewPath = true;
    }
    QVERIFY(foundAddedNewPath);
}

void TstAsyncOps::resolveAsyncClearsConflict()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::MergeScenario s = TestFixtures::makeConflictScenario(dir.path());

    SvnManager mgr;
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.resolveAsync({s.trunkWc + QStringLiteral("/conflict.txt")}, QStringLiteral("mine-full"));
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    bool ok = false;
    const QList<SvnStatusEntry> entries = mgr.status(s.trunkWc, true, &ok);
    QVERIFY(ok);
    for (const SvnStatusEntry &e : entries) {
        if (QFileInfo(e.path).fileName() == QLatin1String("conflict.txt"))
            QVERIFY(e.textStatus != SvnFileStatus::Conflicted);
    }
}

// Smoke test for the asynchronous add (the menu "Add..." path). addSync is
// covered elsewhere; this guards the async wrapper and its default depth.
void TstAsyncOps::addAsyncPutsFileUnderVersionControl()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    TestFixtures::writeFile(wc + QStringLiteral("/n.txt"), QStringLiteral("new\n"));

    SvnManager mgr;
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.addAsync({wc + QStringLiteral("/n.txt")});
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    const QString abs = QFileInfo(wc + QStringLiteral("/n.txt")).absoluteFilePath();
    SvnFileStatus st = SvnFileStatus::Unknown;
    for (const SvnStatusEntry &e : mgr.status(wc)) {
        if (QFileInfo(e.path).absoluteFilePath() == abs) st = e.textStatus;
    }
    QCOMPARE(st, SvnFileStatus::Added);
}

// cleanupAsync has no easily reproducible "locked working copy" precondition,
// so this checks the wrapper runs and reports success on a valid working copy
// (the common menu action). A broken wrapper would fail or hang here.
void TstAsyncOps::cleanupAsyncSucceedsOnWorkingCopy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    SvnManager mgr;
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.cleanupAsync(wc);
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));
}

void TstAsyncOps::mergeTreesAsyncAppliesChange()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString trunkUrl = repoUrl + QStringLiteral("/trunk");
    const QString wc1 = dir.path() + QStringLiteral("/wc1");
    QVERIFY(TestFixtures::checkout(trunkUrl, wc1));

    TestFixtures::writeFile(wc1 + QStringLiteral("/baseline.txt"), QStringLiteral("baseline\n"));
    QVERIFY(TestFixtures::svnAdd(wc1 + QStringLiteral("/baseline.txt")));
    QVERIFY(TestFixtures::svnCommit(wc1, QStringLiteral("r2"))); // r2

    TestFixtures::writeFile(wc1 + QStringLiteral("/newfile.txt"), QStringLiteral("first\nsecond\n"));
    QVERIFY(TestFixtures::svnAdd(wc1 + QStringLiteral("/newfile.txt")));
    QVERIFY(TestFixtures::svnCommit(wc1, QStringLiteral("r3"))); // r3

    // A second, independent WC at r2 (before newfile.txt was added) —
    // mergeTreesAsync should apply the change between r2 and r3.
    const QString wc2 = dir.path() + QStringLiteral("/wc2");
    QVERIFY(TestFixtures::checkout(trunkUrl, wc2, QStringLiteral("2")));

    SvnManager mgr;
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.mergeTreesAsync(trunkUrl, QStringLiteral("2"), trunkUrl, QStringLiteral("3"), wc2);
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    QVERIFY(QFileInfo::exists(wc2 + QStringLiteral("/newfile.txt")));
    bool ok = false;
    const QList<SvnStatusEntry> entries = mgr.status(wc2, true, &ok);
    QVERIFY(ok);
    bool foundAdded = false;
    for (const SvnStatusEntry &e : entries) {
        if (QFileInfo(e.path).fileName() == QLatin1String("newfile.txt")
            && e.textStatus == SvnFileStatus::Added)
            foundAdded = true;
    }
    QVERIFY(foundAdded);
}

void TstAsyncOps::busySecondOperationIsRejectedImmediately()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    SvnManager mgr;
    QSignalSpy spy(&mgr, &SvnManager::operationFinished);

    mgr.updateAsync(wc);              // starts a real operation (runs in the background)
    QVERIFY(mgr.isBusy());
    mgr.updateAsync(wc);               // MUST be rejected synchronously right away

    // The rejection is emitted synchronously (still within the same function
    // call, before any event-loop iteration) -> must be in the spy immediately.
    QCOMPARE(spy.count(), 1);
    const SvnOperationResult rejected = qvariant_cast<SvnOperationResult>(spy.at(0).at(0));
    QVERIFY(!rejected.success);
    QVERIFY2(rejected.errorMessage.contains(QStringLiteral("already in progress")),
             qUtf8Printable(rejected.errorMessage));

    // The FIRST (actually running) operation must still finish normally
    // (no hang caused by the rejected second request).
    while (spy.count() < 2) {
        QVERIFY2(spy.wait(30000), "Timeout: erste Operation hat nie operationFinished gesendet");
    }
    QCOMPARE(spy.count(), 2);
    const SvnOperationResult finished = qvariant_cast<SvnOperationResult>(spy.at(1).at(0));
    QVERIFY2(finished.success, qUtf8Printable(finished.errorMessage));
}

void TstAsyncOps::cancelAsyncLeavesNotBusy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    SvnManager mgr;
    mgr.updateAsync(wc);
    mgr.cancelAsync();
    QVERIFY(!mgr.isBusy());
}

QTEST_GUILESS_MAIN(TstAsyncOps)
#include "tst_asyncops.moc"
