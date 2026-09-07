#include "TestFixtures.h"
#include "svnmanager.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

// Tests for the remaining synchronous methods: info(), diffSync(),
// blameSync() (svn 1.14 --xml returns NO <text> elements -> the fallback must
// fill the line contents from BASE), propListSync/propSetSync/propDelSync
// (multi-line value), isSvnWorkingCopy() edge cases and the 'ok' error path of
// status() on a broken/missing WC (A2 pattern).
class TstSyncMisc : public QObject
{
    Q_OBJECT

private slots:
    void infoReturnsCoreFields();
    void diffSyncShowsChangeBetweenRevisions();
    void blameSyncFillsTextFromBaseWhenXmlOmitsIt();
    void blameSyncUsesBaseNotLocallyModifiedFile();
    void propRoundtripMultilineValue();
    void propDelSyncRemovesProperty();
    void addToIgnoreSyncAppendsPatternWithoutDuplicates();
    void isSvnWorkingCopySubfolderIsTrue();
    void isSvnWorkingCopyOutsideIsFalse();
    void statusOkFalseWhenSvnBinaryMissing();
    void statusOkTrueButEmptyForNonWorkingCopy();
};

void TstSyncMisc::infoReturnsCoreFields()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString trunkUrl = repoUrl + QStringLiteral("/trunk");
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(trunkUrl, wc));

    const QString filePath = wc + QStringLiteral("/f.txt");
    TestFixtures::writeFile(filePath, QStringLiteral("x\n"));
    QVERIFY(TestFixtures::svnAdd(filePath));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("add f.txt")));

    SvnManager mgr;
    // Query on the committed FILE, not the WC root (which, on a
    // single-file commits may be mixed-revisioned, see the fixture gotcha).
    const SvnInfo info = mgr.info(filePath);
    QVERIFY(info.valid);
    QCOMPARE(info.url, trunkUrl + QStringLiteral("/f.txt"));
    QCOMPARE(info.repositoryRoot, repoUrl);
    QCOMPARE(QDir(info.wcRootPath).absolutePath(), QDir(wc).absolutePath());
    bool ok = false;
    const int rev = info.revision.toInt(&ok);
    QVERIFY(ok);
    QVERIFY(rev >= 2);
}

void TstSyncMisc::diffSyncShowsChangeBetweenRevisions()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    const QString filePath = wc + QStringLiteral("/f.txt");
    TestFixtures::writeFile(filePath, QStringLiteral("line1\nline2\n"));
    QVERIFY(TestFixtures::svnAdd(filePath));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("r2"))); // r2

    TestFixtures::writeFile(filePath, QStringLiteral("line1\nCHANGED\n"));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("r3"))); // r3

    SvnManager mgr;
    const QString diff = mgr.diffSync(filePath, QStringLiteral("2"), QStringLiteral("3"));
    QVERIFY(!diff.isEmpty());
    QVERIFY(diff.contains(QStringLiteral("-line2")));
    QVERIFY(diff.contains(QStringLiteral("+CHANGED")));
}

void TstSyncMisc::blameSyncFillsTextFromBaseWhenXmlOmitsIt()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    const QString filePath = wc + QStringLiteral("/f.txt");
    TestFixtures::writeFile(filePath, QStringLiteral("alpha\nbeta\ngamma\n"));
    QVERIFY(TestFixtures::svnAdd(filePath));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("add f.txt")));

    SvnManager mgr;
    const QList<SvnBlameEntry> entries = mgr.blameSync(filePath);
    QCOMPARE(entries.size(), 3);
    QCOMPARE(entries[0].text, QStringLiteral("alpha"));
    QCOMPARE(entries[1].text, QStringLiteral("beta"));
    QCOMPARE(entries[2].text, QStringLiteral("gamma"));
    for (const SvnBlameEntry &e : entries)
        QVERIFY(!e.revision.isEmpty());
}

void TstSyncMisc::blameSyncUsesBaseNotLocallyModifiedFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    const QString filePath = wc + QStringLiteral("/f.txt");
    TestFixtures::writeFile(filePath, QStringLiteral("alpha\nbeta\n"));
    QVERIFY(TestFixtures::svnAdd(filePath));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("add f.txt")));

    // A local (uncommitted) change -> annotate still covers the COMMITTED state;
    // the fallback must read from BASE, not from the locally changed file
    // (see an earlier bugfix).
    TestFixtures::writeFile(filePath, QStringLiteral("alpha\nLOCALLY CHANGED\n"));

    SvnManager mgr;
    const QList<SvnBlameEntry> entries = mgr.blameSync(filePath);
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries[0].text, QStringLiteral("alpha"));
    QCOMPARE(entries[1].text, QStringLiteral("beta"));
}

void TstSyncMisc::propRoundtripMultilineValue()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    const QString filePath = wc + QStringLiteral("/f.txt");
    TestFixtures::writeFile(filePath, QStringLiteral("x\n"));
    QVERIFY(TestFixtures::svnAdd(filePath));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("add f.txt")));

    SvnManager mgr;
    const QString value = QStringLiteral("line one\nline two\nline three (mehrzeilig)");
    QVERIFY(mgr.propSetSync(filePath, QStringLiteral("custom:multiline"), value));

    const QMap<QString, QString> props = mgr.propListSync(filePath);
    QVERIFY(props.contains(QStringLiteral("custom:multiline")));
    // Byte-identical: no temp-file detour that could change newlines etc.
    QCOMPARE(props.value(QStringLiteral("custom:multiline")), value);
}

void TstSyncMisc::propDelSyncRemovesProperty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    const QString filePath = wc + QStringLiteral("/f.txt");
    TestFixtures::writeFile(filePath, QStringLiteral("x\n"));
    QVERIFY(TestFixtures::svnAdd(filePath));
    QVERIFY(TestFixtures::svnCommit(wc, QStringLiteral("add f.txt")));

    SvnManager mgr;
    QVERIFY(mgr.propSetSync(filePath, QStringLiteral("custom:temp"), QStringLiteral("value")));
    QVERIFY(mgr.propListSync(filePath).contains(QStringLiteral("custom:temp")));

    QVERIFY(mgr.propDelSync(filePath, QStringLiteral("custom:temp")));
    QVERIFY(!mgr.propListSync(filePath).contains(QStringLiteral("custom:temp")));
}

void TstSyncMisc::addToIgnoreSyncAppendsPatternWithoutDuplicates()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    SvnManager mgr;
    QVERIFY(mgr.addToIgnoreSync(wc, QStringLiteral("build")));
    QCOMPARE(mgr.propListSync(wc).value(QStringLiteral("svn:ignore")),
             QStringLiteral("build\n"));

    // Duplicate: must not create a second line.
    QVERIFY(mgr.addToIgnoreSync(wc, QStringLiteral("build")));
    QCOMPARE(mgr.propListSync(wc).value(QStringLiteral("svn:ignore")),
             QStringLiteral("build\n"));

    // A second, different pattern is appended.
    QVERIFY(mgr.addToIgnoreSync(wc, QStringLiteral("*.log")));
    QCOMPARE(mgr.propListSync(wc).value(QStringLiteral("svn:ignore")),
             QStringLiteral("build\n*.log\n"));
}

void TstSyncMisc::isSvnWorkingCopySubfolderIsTrue()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));
    QDir().mkpath(wc + QStringLiteral("/sub/deeper"));

    SvnManager mgr;
    QVERIFY(mgr.isSvnWorkingCopy(wc + QStringLiteral("/sub")));
    QVERIFY(mgr.isSvnWorkingCopy(wc + QStringLiteral("/sub/deeper")));
}

void TstSyncMisc::isSvnWorkingCopyOutsideIsFalse()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    // A sibling folder NEXT TO the WC (not underneath it) -> no .svn in the
    // tree, as long as QTemporaryDir::path() (the parent) is not a WC itself.
    const QString outside = dir.path() + QStringLiteral("/not_a_wc");
    QDir().mkpath(outside);

    SvnManager mgr;
    QVERIFY(!mgr.isSvnWorkingCopy(outside));
}

void TstSyncMisc::statusOkFalseWhenSvnBinaryMissing()
{
    // The only reliably triggerable error path for 'ok' (see A2 pattern):
    // the svn process cannot even be started -> waitForFinished
    // scheitert -> ok=false. Simuliert z.B. eine falsche svnBinary-Einstellung.
    SvnManager mgr;
    mgr.setSvnBinary(QStringLiteral("/definitely/not/a/svn/binary"));

    bool ok = true; // deliberately preset to true, must be set to false
    const QList<SvnStatusEntry> entries = mgr.status(QStringLiteral("/tmp"), true, &ok);
    QVERIFY(!ok);
    QVERIFY(entries.isEmpty());
}

void TstSyncMisc::statusOkTrueButEmptyForNonWorkingCopy()
{
    // A DISCOVERED quirk (not a bug per se, but important for P3/callers):
    // 'svn status --xml' on a non-WC directory (or a path that does not exist
    // at all) returns exit code 0 with an empty <target> — the warning ("is not
    // a working copy") goes only to stderr, WITHOUT the
    // affecting the exit code (verified against real svn 1.14). The A2 error
    // path (bool *ok) therefore CANNOT detect this case — 'ok'
    // stays true, entries stays empty, indistinguishable from a genuinely clean
    // WC without changes. Callers must additionally check isSvnWorkingCopy() if
    // they want to detect "not a WC" explicitly.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDir().mkpath(dir.path() + QStringLiteral("/plain"));

    SvnManager mgr;
    bool ok = false;
    const QList<SvnStatusEntry> entries =
        mgr.status(dir.path() + QStringLiteral("/plain"), true, &ok);
    QVERIFY(ok);
    QVERIFY(entries.isEmpty());
    QVERIFY(!mgr.isSvnWorkingCopy(dir.path() + QStringLiteral("/plain")));
}

QTEST_GUILESS_MAIN(TstSyncMisc)
#include "tst_syncmisc.moc"
