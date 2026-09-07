#include "TestFixtures.h"
#include "svnmanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

// Tests for the repository operations: checkoutAsync, switchAsync,
// copyAsync (branch/tag), mkdirUrlAsync/mkdirUrlsAsync (one commit for
// several URLs), deleteUrlAsync, moveUrlAsync, listSync (ordering, field
// correctness) as well as lockAsync/unlockAsync incl. steal-force.
class TstRepoOps : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void createRepositorySyncMakesUsableRepo();
    void createRepositorySyncFailsOnNonEmptyDir();
    void checkoutAsyncCreatesWorkingCopy();
    void switchAsyncChangesUrl();
    void relocateAsyncRewritesUrlAndKeepsUpdateWorking();
    void importAsyncCreatesSingleCommitWithoutLocalWorkingCopy();
    void copyAsyncCreatesBranch();
    void mkdirUrlAsyncCreatesFolder();
    void mkdirUrlsAsyncUsesSingleCommit();
    void deleteUrlAsyncRemovesEntry();
    void moveUrlAsyncRenamesEntry();
    void listSyncOrderAndFields();
    void lockUnlockAsyncRoundtrip();
    void lockAsyncWithoutForceFailsWhenAlreadyLocked();
    void lockAsyncWithForceSteals();
};

void TstRepoOps::initTestCase()
{
    TestFixtures::registerMetaTypes();
}

void TstRepoOps::createRepositorySyncMakesUsableRepo()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoDir = dir.path() + QStringLiteral("/newrepo");

    SvnManager mgr;
    QString err;
    QVERIFY2(mgr.createRepositorySync(repoDir, &err), qUtf8Printable(err));

    // A fresh repository must answer 'svn list' on its file:// URL (empty list,
    // ok=true), which proves it is a usable repository, not just a directory.
    const QString url = TestFixtures::fileUrl(repoDir);
    bool ok = false;
    const auto entries = mgr.listSync(url, QStringLiteral("HEAD"), &ok);
    QVERIFY(ok);
    QVERIFY(entries.isEmpty());
}

void TstRepoOps::createRepositorySyncFailsOnNonEmptyDir()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoDir = dir.path() + QStringLiteral("/notempty");
    QVERIFY(QDir().mkpath(repoDir));
    QFile f(repoDir + QStringLiteral("/stray.txt"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();

    SvnManager mgr;
    QString err;
    QVERIFY(!mgr.createRepositorySync(repoDir, &err));
    QVERIFY(!err.isEmpty());
}

void TstRepoOps::checkoutAsyncCreatesWorkingCopy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");

    SvnManager mgr;
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.checkoutAsync(repoUrl + QStringLiteral("/trunk"), wc);
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));
    QVERIFY(mgr.isSvnWorkingCopy(wc));
}

void TstRepoOps::switchAsyncChangesUrl()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::MergeScenario s = TestFixtures::makeMergedBranchScenario(dir.path());

    SvnManager mgr;
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.switchAsync(s.trunkWc, s.branchUrl);
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    const SvnInfo info = mgr.info(s.trunkWc);
    QCOMPARE(info.url, s.branchUrl);
}

void TstRepoOps::relocateAsyncRewritesUrlAndKeepsUpdateWorking()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString trunkUrl = repoUrl + QStringLiteral("/trunk");
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(trunkUrl, wc));

    // Simulate the repository moving to a new location on disk — the old
    // URL becomes unreachable, so a subsequent update only succeeds if the
    // relocate genuinely rewrote the WC's URL metadata (not merely cosmetic).
    QDir root(dir.path());
    QVERIFY(root.rename(QStringLiteral("repo"), QStringLiteral("repo-moved")));
    const QString newTrunkUrl =
        TestFixtures::fileUrl(dir.path() + QStringLiteral("/repo-moved"))
        + QStringLiteral("/trunk");

    SvnManager mgr;
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.relocateAsync(wc, trunkUrl, newTrunkUrl);
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    const SvnInfo info = mgr.info(wc);
    QCOMPARE(info.url, newTrunkUrl);

    const SvnOperationResult updateResult = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.updateAsync(wc);
    });
    QVERIFY2(updateResult.success, qUtf8Printable(updateResult.errorMessage));
}

void TstRepoOps::importAsyncCreatesSingleCommitWithoutLocalWorkingCopy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());

    const QString localDir = dir.path() + QStringLiteral("/local-src");
    QDir().mkpath(localDir + QStringLiteral("/sub"));
    TestFixtures::writeFile(localDir + QStringLiteral("/a.txt"), QStringLiteral("a\n"));
    TestFixtures::writeFile(localDir + QStringLiteral("/sub/b.txt"), QStringLiteral("b\n"));

    const QString targetUrl = repoUrl + QStringLiteral("/imported");

    SvnManager mgr;
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.importAsync(localDir, targetUrl, QStringLiteral("import"));
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    // The local folder itself must remain a plain (non-SVN) directory.
    QVERIFY(!mgr.isSvnWorkingCopy(localDir));
    QVERIFY(!QFileInfo::exists(localDir + QStringLiteral("/.svn")));

    // Repository must contain the imported tree, recursively.
    bool ok = false;
    const QList<SvnListEntry> topEntries =
        mgr.listSync(targetUrl, QStringLiteral("HEAD"), &ok);
    QVERIFY(ok);
    QStringList topNames;
    for (const SvnListEntry &e : topEntries) topNames << e.name;
    QVERIFY(topNames.contains(QStringLiteral("a.txt")));
    QVERIFY(topNames.contains(QStringLiteral("sub")));

    const QList<SvnListEntry> subEntries =
        mgr.listSync(targetUrl + QStringLiteral("/sub"), QStringLiteral("HEAD"), &ok);
    QVERIFY(ok);
    QCOMPARE(subEntries.size(), 1);
    QCOMPARE(subEntries.first().name, QStringLiteral("b.txt"));

    // Single commit: this was the very first change in the (empty) repo.
    const SvnInfo info = mgr.info(targetUrl);
    QCOMPARE(info.revision, QStringLiteral("1"));
}

void TstRepoOps::copyAsyncCreatesBranch()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString trunkUrl = repoUrl + QStringLiteral("/trunk");
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(trunkUrl, wc));
    TestFixtures::writeFile(wc + QStringLiteral("/f.txt"), QStringLiteral("payload\n"));
    QVERIFY(TestFixtures::svnAdd(wc + QStringLiteral("/f.txt")));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("add f.txt")));

    const QString tagUrl = repoUrl + QStringLiteral("/tags/v1");
    SvnManager mgr;
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.copyAsync(trunkUrl, tagUrl, QStringLiteral("Tag v1"));
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    bool ok = false;
    const QList<SvnListEntry> entries = mgr.listSync(tagUrl, QStringLiteral("HEAD"), &ok);
    QVERIFY(ok);
    bool foundF = false;
    for (const SvnListEntry &e : entries) {
        if (e.name == QLatin1String("f.txt"))
            foundF = true;
    }
    QVERIFY(foundF);
}

void TstRepoOps::mkdirUrlAsyncCreatesFolder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);

    SvnManager mgr;
    const QString newDirUrl = repoUrl + QStringLiteral("/trunk/newdir");
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.mkdirUrlAsync(newDirUrl, QStringLiteral("Create newdir"));
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    bool ok = false;
    const QList<SvnListEntry> entries = mgr.listSync(repoUrl + QStringLiteral("/trunk"), QStringLiteral("HEAD"), &ok);
    QVERIFY(ok);
    bool found = false;
    for (const SvnListEntry &e : entries) {
        if (e.name == QLatin1String("newdir")) {
            found = true;
            QVERIFY(e.isDir);
        }
    }
    QVERIFY(found);
}

void TstRepoOps::mkdirUrlsAsyncUsesSingleCommit()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    // No createStandardLayout() -> we create trunk/branches/tags OURSELVES as
    // ONE multi-URL mkdirUrlsAsync, to verify the single-commit guarantee
    // (r1 = all three folders in one revision).
    SvnManager mgr;
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.mkdirUrlsAsync({repoUrl + QStringLiteral("/trunk"),
                             repoUrl + QStringLiteral("/branches"),
                             repoUrl + QStringLiteral("/tags")},
                            QStringLiteral("Create standard layout"));
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));
    const QList<SvnLogEntry> entries = mgr.log(wc, 100);
    // Exactly ONE revision (not three) -> all three URLs in a single commit.
    QCOMPARE(entries.size(), 1);

    bool ok = false;
    const QList<SvnListEntry> rootEntries = mgr.listSync(repoUrl, QStringLiteral("HEAD"), &ok);
    QVERIFY(ok);
    QCOMPARE(rootEntries.size(), 3);
}

void TstRepoOps::deleteUrlAsyncRemovesEntry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));
    TestFixtures::writeFile(wc + QStringLiteral("/f.txt"), QStringLiteral("x\n"));
    QVERIFY(TestFixtures::svnAdd(wc + QStringLiteral("/f.txt")));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("add f.txt")));

    SvnManager mgr;
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.deleteUrlAsync(repoUrl + QStringLiteral("/trunk/f.txt"), QStringLiteral("Delete f.txt"));
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    bool ok = false;
    const QList<SvnListEntry> entries = mgr.listSync(repoUrl + QStringLiteral("/trunk"), QStringLiteral("HEAD"), &ok);
    QVERIFY(ok);
    for (const SvnListEntry &e : entries)
        QVERIFY(e.name != QLatin1String("f.txt"));
}

void TstRepoOps::moveUrlAsyncRenamesEntry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));
    TestFixtures::writeFile(wc + QStringLiteral("/old.txt"), QStringLiteral("x\n"));
    QVERIFY(TestFixtures::svnAdd(wc + QStringLiteral("/old.txt")));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("add old.txt")));

    SvnManager mgr;
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.moveUrlAsync(repoUrl + QStringLiteral("/trunk/old.txt"),
                          repoUrl + QStringLiteral("/trunk/renamed.txt"),
                          QStringLiteral("Rename"));
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    bool ok = false;
    const QList<SvnListEntry> entries = mgr.listSync(repoUrl + QStringLiteral("/trunk"), QStringLiteral("HEAD"), &ok);
    QVERIFY(ok);
    bool foundOld = false, foundRenamed = false;
    for (const SvnListEntry &e : entries) {
        if (e.name == QLatin1String("old.txt")) foundOld = true;
        if (e.name == QLatin1String("renamed.txt")) foundRenamed = true;
    }
    QVERIFY(!foundOld);
    QVERIFY(foundRenamed);
}

void TstRepoOps::listSyncOrderAndFields()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    // Deliberately mixed order (zzz created before aaa on the filesystem) —
    // svn list returns purely ALPHABETICALLY, NOT folders-first (verified
    // against real svn 1.14, see memory/TESTING.md).
    QDir().mkpath(wc + QStringLiteral("/zzz_dir"));
    QDir().mkpath(wc + QStringLiteral("/aaa_dir"));
    TestFixtures::writeFile(wc + QStringLiteral("/bbb_file.txt"), QStringLiteral("x\n"));
    QVERIFY(TestFixtures::svnAdd(wc + QStringLiteral("/zzz_dir")));
    QVERIFY(TestFixtures::svnAdd(wc + QStringLiteral("/aaa_dir")));
    QVERIFY(TestFixtures::svnAdd(wc + QStringLiteral("/bbb_file.txt")));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("mixed entries")));

    SvnManager mgr;
    bool ok = false;
    const QList<SvnListEntry> entries = mgr.listSync(repoUrl + QStringLiteral("/trunk"), QStringLiteral("HEAD"), &ok);
    QVERIFY(ok);
    QCOMPARE(entries.size(), 3);
    // Alphabetical: aaa_dir, bbb_file.txt, zzz_dir — NOT folders first.
    QCOMPARE(entries[0].name, QStringLiteral("aaa_dir"));
    QVERIFY(entries[0].isDir);
    QCOMPARE(entries[1].name, QStringLiteral("bbb_file.txt"));
    QVERIFY(!entries[1].isDir);
    QCOMPARE(entries[2].name, QStringLiteral("zzz_dir"));
    QVERIFY(entries[2].isDir);
}

void TstRepoOps::lockUnlockAsyncRoundtrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));
    TestFixtures::writeFile(wc + QStringLiteral("/f.txt"), QStringLiteral("x\n"));
    QVERIFY(TestFixtures::svnAdd(wc + QStringLiteral("/f.txt")));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("add f.txt")));

    SvnManager mgr;
    const QString filePath = wc + QStringLiteral("/f.txt");
    const SvnOperationResult lockResult = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.lockAsync({filePath}, QStringLiteral("locking"));
    });
    QVERIFY2(lockResult.success, qUtf8Printable(lockResult.errorMessage));

    bool ok = false;
    QList<SvnStatusEntry> entries = mgr.status(wc, true, &ok);
    QVERIFY(ok);
    bool foundLocked = false;
    for (const SvnStatusEntry &e : entries) {
        if (QFileInfo(e.path).fileName() == QLatin1String("f.txt"))
            foundLocked = e.isLocked;
    }
    QVERIFY(foundLocked);

    const SvnOperationResult unlockResult = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.unlockAsync({filePath});
    });
    QVERIFY2(unlockResult.success, qUtf8Printable(unlockResult.errorMessage));

    entries = mgr.status(wc, true, &ok);
    QVERIFY(ok);
    for (const SvnStatusEntry &e : entries) {
        if (QFileInfo(e.path).fileName() == QLatin1String("f.txt"))
            QVERIFY(!e.isLocked);
    }
}

void TstRepoOps::lockAsyncWithoutForceFailsWhenAlreadyLocked()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));
    TestFixtures::writeFile(wc + QStringLiteral("/f.txt"), QStringLiteral("x\n"));
    QVERIFY(TestFixtures::svnAdd(wc + QStringLiteral("/f.txt")));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("add f.txt")));

    SvnManager mgr;
    const QString filePath = wc + QStringLiteral("/f.txt");
    const SvnOperationResult first = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.lockAsync({filePath}, QStringLiteral("first lock"));
    });
    QVERIFY2(first.success, qUtf8Printable(first.errorMessage));

    // Even the OWN re-lock without --force fails (svn behaviour,
    // verified against real svn 1.14: E200009 "already locked").
    const SvnOperationResult second = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.lockAsync({filePath}, QStringLiteral("second lock"), /*force=*/false);
    });
    QVERIFY(!second.success);
}

void TstRepoOps::lockAsyncWithForceSteals()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));
    TestFixtures::writeFile(wc + QStringLiteral("/f.txt"), QStringLiteral("x\n"));
    QVERIFY(TestFixtures::svnAdd(wc + QStringLiteral("/f.txt")));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("add f.txt")));

    SvnManager mgr;
    const QString filePath = wc + QStringLiteral("/f.txt");
    const SvnOperationResult first = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.lockAsync({filePath}, QStringLiteral("first lock"));
    });
    QVERIFY2(first.success, qUtf8Printable(first.errorMessage));

    const SvnOperationResult stolen = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.lockAsync({filePath}, QStringLiteral("steal"), /*force=*/true);
    });
    QVERIFY2(stolen.success, qUtf8Printable(stolen.errorMessage));
}

QTEST_GUILESS_MAIN(TstRepoOps)
#include "tst_repoops.moc"
