#include "TestFixtures.h"
#include "svnmanager.h"

#include <QTemporaryDir>
#include <QTest>

// Smoke test: verifies that svnmanager.cpp compiles + links directly from
// svn/ and that the fixture helper can build a minimal repo.
class TstSmoke : public QObject
{
    Q_OBJECT

private slots:
    void notAWorkingCopy();
    void statusOnFreshCheckout();
};

void TstSmoke::notAWorkingCopy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SvnManager mgr;
    QVERIFY(!mgr.isSvnWorkingCopy(dir.path()));
}

void TstSmoke::statusOnFreshCheckout()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString trunkWc = dir.path() + QStringLiteral("/wc_trunk");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), trunkWc));

    SvnManager mgr;
    QVERIFY(mgr.isSvnWorkingCopy(trunkWc));

    bool ok = false;
    const QList<SvnStatusEntry> entries = mgr.status(trunkWc, true, &ok);
    QVERIFY(ok);
    QVERIFY(entries.isEmpty()); // fresh checkout: no changes
}

QTEST_GUILESS_MAIN(TstSmoke)
#include "tst_smoke.moc"
