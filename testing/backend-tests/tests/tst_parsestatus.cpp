#include "TestFixtures.h"
#include "svnmanager.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

// Tests for SvnManager::status() / parseStatusXml():
// - tree-conflicted-Attribut
// - conflict-helper filter (.mine/.rNNN filtered; merge helpers NOT)
class TstParseStatus : public QObject
{
    Q_OBJECT

private slots:
    void treeConflictAttribute();
    void updateConflictHelpersAreFiltered();
    void mergeConflictHelpersAreNotFiltered();
    void lockOwnerAndCommentAreParsed();
    void includeIgnoredRevealsIgnoredEntryOtherwiseHidden();
};

static const SvnStatusEntry *findByBasename(const QList<SvnStatusEntry> &entries, const QString &basename)
{
    for (const SvnStatusEntry &e : entries) {
        if (QFileInfo(e.path).fileName() == basename)
            return &e;
    }
    return nullptr;
}

void TstParseStatus::treeConflictAttribute()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::MergeScenario s = TestFixtures::makeConflictScenario(dir.path());

    SvnManager mgr;
    bool ok = false;
    const QList<SvnStatusEntry> entries = mgr.status(s.trunkWc, true, &ok);
    QVERIFY(ok);

    const SvnStatusEntry *fileB = findByBasename(entries, QStringLiteral("fileB.txt"));
    QVERIFY(fileB != nullptr);
    QVERIFY(fileB->treeConflicted);

    const SvnStatusEntry *conflictTxt = findByBasename(entries, QStringLiteral("conflict.txt"));
    QVERIFY(conflictTxt != nullptr);
    QCOMPARE(conflictTxt->textStatus, SvnFileStatus::Conflicted);
}

void TstParseStatus::updateConflictHelpersAreFiltered()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::UpdateConflictScenario s = TestFixtures::makeUpdateConflictScenario(dir.path());

    SvnManager mgr;
    bool ok = false;
    const QList<SvnStatusEntry> entries = mgr.status(s.wcB, true, &ok);
    QVERIFY(ok);

    const SvnStatusEntry *conflictTxt = findByBasename(entries, QStringLiteral("conflict.txt"));
    QVERIFY(conflictTxt != nullptr);
    QCOMPARE(conflictTxt->textStatus, SvnFileStatus::Conflicted);

    // The helper files created by 'svn update' (.mine / .rN) must NOT appear as
    // their own unversioned entries.
    for (const SvnStatusEntry &e : entries) {
        const QString base = QFileInfo(e.path).fileName();
        QVERIFY2(!base.endsWith(QLatin1String(".mine")),
                 qUtf8Printable(QStringLiteral("Helper-Datei wurde nicht gefiltert: ") + base));
        static const QRegularExpression rRevSuffix(QStringLiteral("\\.r\\d+$"));
        if (base.startsWith(QLatin1String("conflict.txt")))
            QVERIFY2(!rRevSuffix.match(base).hasMatch(),
                     qUtf8Printable(QStringLiteral("Helper-Datei wurde nicht gefiltert: ") + base));
    }
}

void TstParseStatus::mergeConflictHelpersAreNotFiltered()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const TestFixtures::MergeScenario s = TestFixtures::makeConflictScenario(dir.path());

    SvnManager mgr;
    bool ok = false;
    const QList<SvnStatusEntry> entries = mgr.status(s.trunkWc, true, &ok);
    QVERIFY(ok);

    // Merge helper files are named .working / .merge-left.rN / .merge-right.rN.
    // The .mine/.rNNN filter does NOT apply to them, they must stay visible as
    // their own unversioned entries.
    bool foundWorkingHelper = false;
    for (const SvnStatusEntry &e : entries) {
        const QString base = QFileInfo(e.path).fileName();
        if (base.startsWith(QLatin1String("conflict.txt.")) && e.textStatus == SvnFileStatus::Unversioned)
            foundWorkingHelper = true;
    }
    QVERIFY2(foundWorkingHelper, "Erwartete Merge-Helferdatei (.working/.merge-left.rN/...) nicht gefunden");
}

void TstParseStatus::lockOwnerAndCommentAreParsed()
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
        mgr.lockAsync({filePath}, QStringLiteral("editing f.txt"));
    });
    QVERIFY2(lockResult.success, qUtf8Printable(lockResult.errorMessage));

    bool ok = false;
    const QList<SvnStatusEntry> entries = mgr.status(wc, true, &ok);
    QVERIFY(ok);
    const SvnStatusEntry *f = findByBasename(entries, QStringLiteral("f.txt"));
    QVERIFY(f != nullptr);
    QVERIFY(f->isLocked);
    QVERIFY(!f->lockOwner.isEmpty());
    QCOMPARE(f->lockComment, QStringLiteral("editing f.txt"));
}

void TstParseStatus::includeIgnoredRevealsIgnoredEntryOtherwiseHidden()
{
    // Regression: svn hides svn:ignore'd paths from 'status' completely
    // by default (no entry at all) -> callers that treat "no entry" as
    // "unchanged versioned" (the overlay plugin) need includeIgnored=true
    // to tell an ignored file apart from a plain unmodified one.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString repoUrl = TestFixtures::createEmptyRepo(dir.path());
    TestFixtures::createStandardLayout(repoUrl);
    const QString wc = dir.path() + QStringLiteral("/wc");
    QVERIFY(TestFixtures::checkout(repoUrl + QStringLiteral("/trunk"), wc));

    TestFixtures::writeFile(wc + QStringLiteral("/build.log"), QStringLiteral("artifact\n"));

    SvnManager mgr;
    QVERIFY(mgr.addToIgnoreSync(wc, QStringLiteral("build.log")));

    bool ok = false;
    const QList<SvnStatusEntry> plainEntries = mgr.status(wc, false, &ok);
    QVERIFY(ok);
    QVERIFY(findByBasename(plainEntries, QStringLiteral("build.log")) == nullptr);

    const QList<SvnStatusEntry> withIgnored =
        mgr.status(wc, false, &ok, /*showUpdates=*/false, /*includeIgnored=*/true);
    QVERIFY(ok);
    const SvnStatusEntry *ignored = findByBasename(withIgnored, QStringLiteral("build.log"));
    QVERIFY(ignored != nullptr);
    QCOMPARE(ignored->textStatus, SvnFileStatus::Ignored);
}

QTEST_GUILESS_MAIN(TstParseStatus)
#include "tst_parsestatus.moc"
