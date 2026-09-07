#include "svnsettings.h"

#include <QSettings>
#include <QStandardPaths>
#include <QTest>

// Tests for the QSettings-backed accessors in svnsettings.h. Runs against an
// isolated settings location (QStandardPaths test mode), so the developer's own
// ~/.config/DolphinSvnPlugin is never read or written.
class TstSettings : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void stringValueRoundTrips();
    void boolValueRoundTrips();
    void timeoutDefaultsAndRoundTrips();
    void pendingMergeMessageIsPerWorkingCopy();
};

void TstSettings::initTestCase()
{
    // Redirect QSettings (NativeFormat, UserScope) to a throwaway test location.
    QStandardPaths::setTestModeEnabled(true);
}

void TstSettings::init()
{
    // Fresh slate before every test function.
    SvnSettings::open().clear();
}

void TstSettings::stringValueRoundTrips()
{
    SvnSettings::setSvnBinary(QStringLiteral("/opt/subversion/bin/svn"));
    QCOMPARE(SvnSettings::svnBinary(),
             QStringLiteral("/opt/subversion/bin/svn"));
}

void TstSettings::boolValueRoundTrips()
{
    SvnSettings::setKeepLocksOnCommit(true);
    QVERIFY(SvnSettings::keepLocksOnCommit());
    SvnSettings::setKeepLocksOnCommit(false);
    QVERIFY(!SvnSettings::keepLocksOnCommit());
}

void TstSettings::timeoutDefaultsAndRoundTrips()
{
    QCOMPARE(SvnSettings::processSyncTimeout(), 30); // documented default
    SvnSettings::setProcessSyncTimeout(5);
    QCOMPARE(SvnSettings::processSyncTimeout(), 5);
}

// The pending merge message is stored per working copy root, keyed by a
// Base64Url encoding of the path (so path slashes do not create QSettings
// groups). Two working copies must not clobber each other.
void TstSettings::pendingMergeMessageIsPerWorkingCopy()
{
    const QString wcA = QStringLiteral("/home/user/projects/alpha");
    const QString wcB = QStringLiteral("/home/user/projects/beta");

    QVERIFY(SvnSettings::pendingMergeMessage(wcA).isEmpty());

    SvnSettings::setPendingMergeMessage(wcA, QStringLiteral("Merge for alpha"));
    SvnSettings::setPendingMergeMessage(wcB, QStringLiteral("Merge for beta"));

    QCOMPARE(SvnSettings::pendingMergeMessage(wcA), QStringLiteral("Merge for alpha"));
    QCOMPARE(SvnSettings::pendingMergeMessage(wcB), QStringLiteral("Merge for beta"));

    // Clearing one leaves the other untouched.
    SvnSettings::clearPendingMergeMessage(wcA);
    QVERIFY(SvnSettings::pendingMergeMessage(wcA).isEmpty());
    QCOMPARE(SvnSettings::pendingMergeMessage(wcB), QStringLiteral("Merge for beta"));
}

QTEST_GUILESS_MAIN(TstSettings)
#include "tst_settings.moc"
