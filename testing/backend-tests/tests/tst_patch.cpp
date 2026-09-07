#include "TestFixtures.h"
#include "svnmanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

// Tests for createPatchSync() / applyPatchAsync() / patchOutputHasRejects()
// ("Create Patch" / "Apply Patch").
//
// The scenario is deliberately built locally rather than in TestFixtures: it
// needs TWO working copies of the same trunk in the initial state (patch from
// WC1 → apply in WC2), which none of the existing scenarios provides.
class TstPatch : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void patchPathsAreRelativeToRepositoryRoot();
    void stripLevelDetectedForTrunkCheckout();
    void stripLevelZeroForRepositoryRootCheckout();
    void applyingWithoutStripIsRejectedInTrunkCheckout();
    void stripLevelFallsBackToTargetLayoutForAddOnlyPatch();
    void parseTargetPathsFromGitAndPlainPatches();
    void roundtripBetweenWorkingCopies();
    void patchCarriesAddedAndDeletedFiles();
    void dryRunLeavesWorkingCopyUntouched();
    void reverseDiffUndoesAppliedPatch();
    void rejectedHunkIsDetectedDespiteExitCodeZero();
    void createPatchWithoutChangesFails();
    void createPatchOnBogusPathFails();
    void rejectDetectionOnSyntheticOutput();
};

namespace {

struct TwoWcScenario {
    QString repoUrl;
    QString trunkUrl;
    QString wc1;
    QString wc2;
};

// Two working copies of ^/trunk (NOT the repository root!). This layout is
// exactly the real-world case and the reason for the --strip handling: the
// paths in 'svn diff --git' then read "trunk/…", so they do not match a trunk
// working copy unchanged.
//
// r2: a.txt ("line1/line2/line3"), sub/b.txt ("keep"), c.txt ("todelete").
TwoWcScenario makeTwoWcScenario(const QString &root)
{
    TwoWcScenario s;
    s.repoUrl  = TestFixtures::createEmptyRepo(root);
    s.trunkUrl = s.repoUrl + QStringLiteral("/trunk");
    s.wc1      = root + QStringLiteral("/wc1");
    s.wc2      = root + QStringLiteral("/wc2");

    if (!TestFixtures::runSvn({QStringLiteral("mkdir"), QStringLiteral("--parents"),
                                s.trunkUrl, QStringLiteral("-m"),
                                QStringLiteral("layout")}))
        qFatal("mkdir trunk failed");

    if (!TestFixtures::checkout(s.trunkUrl, s.wc1))
        qFatal("checkout wc1 failed");

    QDir(s.wc1).mkdir(QStringLiteral("sub"));
    TestFixtures::writeFile(s.wc1 + QStringLiteral("/a.txt"),
                            QStringLiteral("line1\nline2\nline3\n"));
    TestFixtures::writeFile(s.wc1 + QStringLiteral("/sub/b.txt"),
                            QStringLiteral("keep\n"));
    TestFixtures::writeFile(s.wc1 + QStringLiteral("/c.txt"),
                            QStringLiteral("todelete\n"));
    if (!TestFixtures::svnAdd(s.wc1 + QStringLiteral("/a.txt"))
        || !TestFixtures::svnAdd(s.wc1 + QStringLiteral("/sub"))
        || !TestFixtures::svnAdd(s.wc1 + QStringLiteral("/c.txt")))
        qFatal("svn add failed");
    if (!TestFixtures::svnCommit(s.wc1, QStringLiteral("baseline")))
        qFatal("commit baseline failed");

    if (!TestFixtures::checkout(s.trunkUrl, s.wc2))
        qFatal("checkout wc2 failed");

    return s;
}

// Changes a.txt and sub/b.txt in wc, adds new.txt and deletes c.txt.
void makeLocalChanges(const QString &wc)
{
    TestFixtures::writeFile(wc + QStringLiteral("/a.txt"),
                            QStringLiteral("line1\nCHANGED\nline3\n"));
    TestFixtures::writeFile(wc + QStringLiteral("/sub/b.txt"),
                            QStringLiteral("keep\nmore\n"));
    TestFixtures::writeFile(wc + QStringLiteral("/new.txt"),
                            QStringLiteral("brand new\n"));
    if (!TestFixtures::svnAdd(wc + QStringLiteral("/new.txt")))
        qFatal("svn add new.txt failed");
    if (!TestFixtures::runSvn({QStringLiteral("rm"), wc + QStringLiteral("/c.txt")}))
        qFatal("svn rm c.txt failed");
}

QString readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}

// Text status of a path according to the WC's 'svn status' (Unknown = not
// listed, i.e. unchanged).
SvnFileStatus statusOf(const SvnManager &mgr, const QString &wc,
                       const QString &relPath)
{
    const QString abs = QFileInfo(wc + QLatin1Char('/') + relPath).absoluteFilePath();
    const auto entries = mgr.status(wc);
    for (const SvnStatusEntry &e : entries) {
        if (QFileInfo(e.path).absoluteFilePath() == abs)
            return e.textStatus;
    }
    return SvnFileStatus::Unknown;
}

} // namespace

void TstPatch::initTestCase()
{
    TestFixtures::registerMetaTypes();
}

// The '--git' paths are relative to the REPOSITORY root: a WC of ^/trunk
// produces "a/trunk/a.txt". That is the reason for the --strip handling.
void TstPatch::patchPathsAreRelativeToRepositoryRoot()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TwoWcScenario s = makeTwoWcScenario(dir.path());
    makeLocalChanges(s.wc1);

    SvnManager mgr;
    const QString patch = dir.path() + QStringLiteral("/rel.patch");
    QString err;
    QVERIFY2(mgr.createPatchSync({s.wc1}, patch, &err), qPrintable(err));

    const QString content = readAll(patch);
    QVERIFY(content.contains(QStringLiteral("diff --git a/trunk/a.txt b/trunk/a.txt")));
    QVERIFY(content.contains(
        QStringLiteral("diff --git a/trunk/sub/b.txt b/trunk/sub/b.txt")));
    // --git markers for added/deleted files
    QVERIFY(content.contains(QStringLiteral("new file mode")));
    QVERIFY(content.contains(QStringLiteral("deleted file mode")));
}

void TstPatch::stripLevelDetectedForTrunkCheckout()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TwoWcScenario s = makeTwoWcScenario(dir.path());
    makeLocalChanges(s.wc1);

    SvnManager mgr;
    const QString patch = dir.path() + QStringLiteral("/strip.patch");
    QString err;
    QVERIFY2(mgr.createPatchSync({s.wc1}, patch, &err), qPrintable(err));

    // "trunk/a.txt" → remove one component so it matches the WC.
    QCOMPARE(mgr.detectPatchStripLevelSync(patch, s.wc2), 1);
}

void TstPatch::stripLevelZeroForRepositoryRootCheckout()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TwoWcScenario s = makeTwoWcScenario(dir.path());

    // The same repository, but checked out as the root: there the
    // patch paths already match and --strip must stay 0.
    const QString rootWc = dir.path() + QStringLiteral("/wcroot");
    QVERIFY(TestFixtures::checkout(s.repoUrl, rootWc));

    TestFixtures::writeFile(rootWc + QStringLiteral("/trunk/a.txt"),
                            QStringLiteral("line1\nCHANGED\nline3\n"));

    SvnManager mgr;
    const QString patch = dir.path() + QStringLiteral("/root.patch");
    QString err;
    QVERIFY2(mgr.createPatchSync({rootWc}, patch, &err), qPrintable(err));
    QCOMPARE(mgr.detectPatchStripLevelSync(patch, rootWc), 0);
}

// Regression for the device-test finding: without --strip the hunks go nowhere
// and svn writes a .svnpatch.rej in the WC root directory instead of patching
// the file in the subfolder.
void TstPatch::applyingWithoutStripIsRejectedInTrunkCheckout()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TwoWcScenario s = makeTwoWcScenario(dir.path());

    TestFixtures::writeFile(s.wc1 + QStringLiteral("/sub/b.txt"),
                            QStringLiteral("keep\nmore\n"));

    SvnManager mgr;
    const QString patch = dir.path() + QStringLiteral("/nostrip.patch");
    QString err;
    QVERIFY2(mgr.createPatchSync({s.wc1}, patch, &err), qPrintable(err));

    // Wrong: without --strip.
    SvnOperationResult res = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.applyPatchAsync(s.wc2, patch, false, false, /*strip=*/0);
    });
    QVERIFY(res.success);                                    // Exit 0 ...
    QVERIFY(SvnManager::patchOutputHasRejects(res.output));  // ... but nothing was applied
    QCOMPARE(statusOf(mgr, s.wc2, QStringLiteral("sub/b.txt")), SvnFileStatus::Unknown);
    QVERIFY(QFileInfo::exists(s.wc2 + QStringLiteral("/b.txt.svnpatch.rej")));

    QVERIFY(QFile::remove(s.wc2 + QStringLiteral("/b.txt.svnpatch.rej")));

    // Correct: with the detected strip level.
    const int strip = mgr.detectPatchStripLevelSync(patch, s.wc2);
    res = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.applyPatchAsync(s.wc2, patch, false, false, strip);
    });
    QVERIFY(res.success);
    QVERIFY(!SvnManager::patchOutputHasRejects(res.output));
    QCOMPARE(statusOf(mgr, s.wc2, QStringLiteral("sub/b.txt")), SvnFileStatus::Modified);
    QCOMPARE(readAll(s.wc2 + QStringLiteral("/sub/b.txt")),
             QStringLiteral("keep\nmore\n"));
}

// A patch that only adds new files offers no match on the filesystem — then the
// layout of the target working copy decides.
void TstPatch::stripLevelFallsBackToTargetLayoutForAddOnlyPatch()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TwoWcScenario s = makeTwoWcScenario(dir.path());

    TestFixtures::writeFile(s.wc1 + QStringLiteral("/fresh.txt"),
                            QStringLiteral("only new\n"));
    QVERIFY(TestFixtures::svnAdd(s.wc1 + QStringLiteral("/fresh.txt")));

    SvnManager mgr;
    const QString patch = dir.path() + QStringLiteral("/addonly.patch");
    QString err;
    QVERIFY2(mgr.createPatchSync({s.wc1}, patch, &err), qPrintable(err));

    QCOMPARE(mgr.detectPatchStripLevelSync(patch, s.wc2), 1);   // ^/trunk → 1

    const int strip = mgr.detectPatchStripLevelSync(patch, s.wc2);
    const SvnOperationResult res = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.applyPatchAsync(s.wc2, patch, false, false, strip);
    });
    QVERIFY2(res.success, qPrintable(res.errorMessage));
    QCOMPARE(statusOf(mgr, s.wc2, QStringLiteral("fresh.txt")), SvnFileStatus::Added);
}

void TstPatch::parseTargetPathsFromGitAndPlainPatches()
{
    // git variant: the leading "a/" belongs to the format, not the path.
    const QByteArray git =
        "Index: /abs/pfad/src/N.cpp\n"
        "===================================================================\n"
        "diff --git a/trunk/src/N.cpp b/trunk/src/N.cpp\n"
        "--- a/trunk/src/N.cpp\t(revision 4)\n"
        "+++ b/trunk/src/N.cpp\t(working copy)\n"
        "@@ -1,3 +1,3 @@\n";
    QCOMPARE(SvnManager::patchTargetPaths(git),
             QStringList{QStringLiteral("trunk/src/N.cpp")});

    // Without --git the path stands unchanged.
    const QByteArray plain =
        "Index: src/N.cpp\n"
        "===================================================================\n"
        "--- src/N.cpp\t(revision 4)\n"
        "+++ src/N.cpp\t(working copy)\n";
    QCOMPARE(SvnManager::patchTargetPaths(plain),
             QStringList{QStringLiteral("src/N.cpp")});

    // The tab also cleanly separates paths that contain spaces.
    const QByteArray spaced =
        "diff --git a/trunk/my file.txt b/trunk/my file.txt\n"
        "--- a/trunk/my file.txt\t(revision 1)\n";
    QCOMPARE(SvnManager::patchTargetPaths(spaced),
             QStringList{QStringLiteral("trunk/my file.txt")});

    QVERIFY(SvnManager::patchTargetPaths(QByteArray()).isEmpty());
}

void TstPatch::roundtripBetweenWorkingCopies()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TwoWcScenario s = makeTwoWcScenario(dir.path());

    // Change in wc1 → patch → revert wc1 → apply the patch in wc2.
    TestFixtures::writeFile(s.wc1 + QStringLiteral("/a.txt"),
                            QStringLiteral("line1\nCHANGED\nline3\n"));
    TestFixtures::writeFile(s.wc1 + QStringLiteral("/sub/b.txt"),
                            QStringLiteral("keep\nmore\n"));

    SvnManager mgr;
    const QString patch = dir.path() + QStringLiteral("/round.patch");
    QString err;
    QVERIFY2(mgr.createPatchSync({s.wc1}, patch, &err), qPrintable(err));
    const int strip = mgr.detectPatchStripLevelSync(patch, s.wc2);

    const QString expectedA = readAll(s.wc1 + QStringLiteral("/a.txt"));
    const QString expectedB = readAll(s.wc1 + QStringLiteral("/sub/b.txt"));

    QVERIFY(TestFixtures::runSvn({QStringLiteral("revert"), QStringLiteral("-R"),
                                   s.wc1}));
    QCOMPARE(statusOf(mgr, s.wc1, QStringLiteral("a.txt")), SvnFileStatus::Unknown);

    const SvnOperationResult res = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.applyPatchAsync(s.wc2, patch, false, false, strip);
    });
    QVERIFY2(res.success, qPrintable(res.errorMessage));
    QVERIFY(!SvnManager::patchOutputHasRejects(res.output));

    QCOMPARE(statusOf(mgr, s.wc2, QStringLiteral("a.txt")), SvnFileStatus::Modified);
    QCOMPARE(statusOf(mgr, s.wc2, QStringLiteral("sub/b.txt")), SvnFileStatus::Modified);
    QCOMPARE(readAll(s.wc2 + QStringLiteral("/a.txt")), expectedA);
    QCOMPARE(readAll(s.wc2 + QStringLiteral("/sub/b.txt")), expectedB);
}

void TstPatch::patchCarriesAddedAndDeletedFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TwoWcScenario s = makeTwoWcScenario(dir.path());
    makeLocalChanges(s.wc1);

    SvnManager mgr;
    const QString patch = dir.path() + QStringLiteral("/addel.patch");
    QString err;
    QVERIFY2(mgr.createPatchSync({s.wc1}, patch, &err), qPrintable(err));
    const int strip = mgr.detectPatchStripLevelSync(patch, s.wc2);

    const SvnOperationResult res = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.applyPatchAsync(s.wc2, patch, false, false, strip);
    });
    QVERIFY2(res.success, qPrintable(res.errorMessage));

    // The patch carries add and delete forward as real SVN operations.
    QCOMPARE(statusOf(mgr, s.wc2, QStringLiteral("new.txt")), SvnFileStatus::Added);
    QCOMPARE(statusOf(mgr, s.wc2, QStringLiteral("c.txt")), SvnFileStatus::Deleted);
    QCOMPARE(readAll(s.wc2 + QStringLiteral("/new.txt")),
             QStringLiteral("brand new\n"));
}

void TstPatch::dryRunLeavesWorkingCopyUntouched()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TwoWcScenario s = makeTwoWcScenario(dir.path());
    makeLocalChanges(s.wc1);

    SvnManager mgr;
    const QString patch = dir.path() + QStringLiteral("/dry.patch");
    QString err;
    QVERIFY2(mgr.createPatchSync({s.wc1}, patch, &err), qPrintable(err));
    const int strip = mgr.detectPatchStripLevelSync(patch, s.wc2);

    const QString before = readAll(s.wc2 + QStringLiteral("/a.txt"));
    const SvnOperationResult res = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.applyPatchAsync(s.wc2, patch, /*dryRun=*/true, false, strip);
    });
    QVERIFY2(res.success, qPrintable(res.errorMessage));

    // Die Vorschau meldet dieselben Aktionen ...
    QVERIFY(res.output.join(QLatin1Char('\n')).contains(QStringLiteral("a.txt")));
    // ... but does not change the WC.
    QCOMPARE(readAll(s.wc2 + QStringLiteral("/a.txt")), before);
    QVERIFY(!QFileInfo::exists(s.wc2 + QStringLiteral("/new.txt")));
    QVERIFY(mgr.status(s.wc2).isEmpty());
}

void TstPatch::reverseDiffUndoesAppliedPatch()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TwoWcScenario s = makeTwoWcScenario(dir.path());

    // Text changes only: for add/delete, 'svn patch --reverse-diff' leaves a
    // 'replaced' behind (delete+re-add), which muddies the comparison.
    TestFixtures::writeFile(s.wc1 + QStringLiteral("/a.txt"),
                            QStringLiteral("line1\nCHANGED\nline3\n"));

    SvnManager mgr;
    const QString patch = dir.path() + QStringLiteral("/rev.patch");
    QString err;
    QVERIFY2(mgr.createPatchSync({s.wc1}, patch, &err), qPrintable(err));
    const int strip = mgr.detectPatchStripLevelSync(patch, s.wc2);

    const QString pristine = readAll(s.wc2 + QStringLiteral("/a.txt"));

    SvnOperationResult res = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.applyPatchAsync(s.wc2, patch, false, false, strip);
    });
    QVERIFY2(res.success, qPrintable(res.errorMessage));
    QCOMPARE(statusOf(mgr, s.wc2, QStringLiteral("a.txt")), SvnFileStatus::Modified);

    res = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.applyPatchAsync(s.wc2, patch, /*dryRun=*/false, /*reverse=*/true,
                            strip);
    });
    QVERIFY2(res.success, qPrintable(res.errorMessage));
    QVERIFY(!SvnManager::patchOutputHasRejects(res.output));

    QCOMPARE(readAll(s.wc2 + QStringLiteral("/a.txt")), pristine);
    QCOMPARE(statusOf(mgr, s.wc2, QStringLiteral("a.txt")), SvnFileStatus::Unknown);
}

// The most important svn pitfall: non-matching hunks are rejected, 'svn patch'
// still exits with code 0 and the file status is "modified", NOT "conflicted".
// Only the output and '.svnpatch.rej' reveal it.
void TstPatch::rejectedHunkIsDetectedDespiteExitCodeZero()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TwoWcScenario s = makeTwoWcScenario(dir.path());

    TestFixtures::writeFile(s.wc1 + QStringLiteral("/a.txt"),
                            QStringLiteral("line1\nCHANGED\nline3\n"));

    SvnManager mgr;
    const QString patch = dir.path() + QStringLiteral("/reject.patch");
    QString err;
    QVERIFY2(mgr.createPatchSync({s.wc1}, patch, &err), qPrintable(err));
    const int strip = mgr.detectPatchStripLevelSync(patch, s.wc2);

    // wc2 has completely different content at the same spot → the hunk does not fit.
    TestFixtures::writeFile(s.wc2 + QStringLiteral("/a.txt"),
                            QStringLiteral("totally\ndifferent\ncontent\nhere\nxx\n"));

    const SvnOperationResult res = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.applyPatchAsync(s.wc2, patch, false, false, strip);
    });
    QVERIFY2(res.success, qPrintable(res.errorMessage));   // Exit 0 trotz Reject!
    QVERIFY(SvnManager::patchOutputHasRejects(res.output));
    QVERIFY(QFileInfo::exists(s.wc2 + QStringLiteral("/a.txt.svnpatch.rej")));
    // The status stays "modified" — so a status check alone is not enough.
    QCOMPARE(statusOf(mgr, s.wc2, QStringLiteral("a.txt")), SvnFileStatus::Modified);
}

void TstPatch::createPatchWithoutChangesFails()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TwoWcScenario s = makeTwoWcScenario(dir.path());

    SvnManager mgr;
    const QString patch = dir.path() + QStringLiteral("/empty.patch");
    QString err;
    QVERIFY(!mgr.createPatchSync({s.wc2}, patch, &err));
    QVERIFY(!err.isEmpty());
    QVERIFY(!QFileInfo::exists(patch));   // do not leave an empty file behind
}

void TstPatch::createPatchOnBogusPathFails()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SvnManager mgr;
    QString err;
    QVERIFY(!mgr.createPatchSync({dir.path() + QStringLiteral("/not-a-wc")},
                                  dir.path() + QStringLiteral("/x.patch"), &err));
    QVERIFY(!err.isEmpty());

    QVERIFY(!mgr.createPatchSync({}, dir.path() + QStringLiteral("/y.patch"), &err));
}

void TstPatch::rejectDetectionOnSyntheticOutput()
{
    QVERIFY(!SvnManager::patchOutputHasRejects({}));
    QVERIFY(!SvnManager::patchOutputHasRejects(
        {QStringLiteral("U         a.txt"), QStringLiteral("A         new.txt")}));
    QVERIFY(SvnManager::patchOutputHasRejects(
        {QStringLiteral("C         a.txt"),
         QStringLiteral(">         rejected hunk @@ -1,3 +1,3 @@")}));
    QVERIFY(SvnManager::patchOutputHasRejects(
        {QStringLiteral("Summary of conflicts:"),
         QStringLiteral("  Text conflicts: 1")}));
}

QTEST_GUILESS_MAIN(TstPatch)
#include "tst_patch.moc"
