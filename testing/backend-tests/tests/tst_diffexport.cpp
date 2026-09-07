#include "TestFixtures.h"
#include "svnmanager.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

// Tests for diffChangeSync() (peg revision, a file added in N),
// exportSync() and cat() with a peg revision.
class TstDiffExport : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void diffChangeShowsAddedFile();
    void diffChangeOnWorkingCopyPath();
    void exportAtRevisionBeforeAdd();
    void exportAtRevisionAfterAdd();
    void catWithPegRevision();
    void catNonExistentAtEarlierRevisionIsEmpty();
    void exportAsyncWritesTreeWithoutSvnMetadata();
};

void TstDiffExport::initTestCase()
{
    TestFixtures::registerMetaTypes();
}

// Smoke test for the asynchronous export (menu "Export..."). exportSync is
// covered above; this guards the async wrapper end to end.
void TstDiffExport::exportAsyncWritesTreeWithoutSvnMetadata()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::PegScenario s = TestFixtures::makePegAddScenario(dir.path());

    SvnManager mgr;
    const QString destDir = dir.path() + QStringLiteral("/exported");
    const SvnOperationResult result = TestFixtures::runAsyncAndWait(mgr, [&]() {
        mgr.exportAsync(s.trunkWc, destDir);
    });
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    QVERIFY(QFileInfo::exists(destDir + QStringLiteral("/baseline.txt")));
    QVERIFY(!QFileInfo::exists(destDir + QStringLiteral("/.svn")));
}

void TstDiffExport::diffChangeShowsAddedFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::PegScenario s = TestFixtures::makePegAddScenario(dir.path());

    SvnManager mgr;
    const QString url = s.trunkUrl + QStringLiteral("/newfile.txt");
    const QString diff = mgr.diffChangeSync(url, s.addedFileRevision);

    QVERIFY(!diff.isEmpty());
    QVERIFY(diff.contains(QStringLiteral("+first")));
    QVERIFY(diff.contains(QStringLiteral("+second")));
    QVERIFY(diff.contains(QStringLiteral("+third")));
}

void TstDiffExport::diffChangeOnWorkingCopyPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::PegScenario s = TestFixtures::makePegAddScenario(dir.path());

    SvnManager mgr;
    const QString path = s.trunkWc + QStringLiteral("/newfile.txt");
    const QString diff = mgr.diffChangeSync(path, s.addedFileRevision);

    QVERIFY(!diff.isEmpty());
    QVERIFY(diff.contains(QStringLiteral("+first")));
}

void TstDiffExport::exportAtRevisionBeforeAdd()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::PegScenario s = TestFixtures::makePegAddScenario(dir.path());

    SvnManager mgr;
    const QString destDir = dir.path() + QStringLiteral("/export_r2");
    QVERIFY(mgr.exportSync(s.trunkUrl, QStringLiteral("2"), destDir));

    QVERIFY(QFileInfo::exists(destDir + QStringLiteral("/baseline.txt")));
    QVERIFY(!QFileInfo::exists(destDir + QStringLiteral("/newfile.txt")));
}

void TstDiffExport::exportAtRevisionAfterAdd()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::PegScenario s = TestFixtures::makePegAddScenario(dir.path());

    SvnManager mgr;
    const QString destDir = dir.path() + QStringLiteral("/export_added");
    QVERIFY(mgr.exportSync(s.trunkUrl, s.addedFileRevision, destDir));

    QVERIFY(QFileInfo::exists(destDir + QStringLiteral("/newfile.txt")));
}

void TstDiffExport::catWithPegRevision()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::PegScenario s = TestFixtures::makePegAddScenario(dir.path());

    SvnManager mgr;
    const QString url = s.trunkUrl + QStringLiteral("/baseline.txt");
    const QByteArray atR2 = mgr.cat(url, QStringLiteral("2"));
    const QByteArray atR3 = mgr.cat(url, QStringLiteral("3"));

    QCOMPARE(atR2, QByteArray("baseline\n"));
    QCOMPARE(atR3, QByteArray("baseline\nmore\n"));
}

void TstDiffExport::catNonExistentAtEarlierRevisionIsEmpty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::PegScenario s = TestFixtures::makePegAddScenario(dir.path());

    SvnManager mgr;
    // newfile.txt only exists from addedFileRevision on -> cat at r2 (before)
    // must fail (empty ByteArray, no crash).
    const QByteArray content = mgr.cat(s.trunkUrl + QStringLiteral("/newfile.txt"), QStringLiteral("2"));
    QVERIFY(content.isEmpty());
}

QTEST_GUILESS_MAIN(TstDiffExport)
#include "tst_diffexport.moc"
