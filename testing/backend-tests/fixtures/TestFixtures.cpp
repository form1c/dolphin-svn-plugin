#include "TestFixtures.h"

#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTextStream>
#include <QTimer>

namespace TestFixtures {

bool run(const QString &program, const QStringList &args,
         const QString &workDir, QByteArray *stdOut, QByteArray *stdErr,
         int *exitCode)
{
    QProcess proc;
    if (!workDir.isEmpty())
        proc.setWorkingDirectory(workDir);
    proc.start(program, args);
    if (!proc.waitForStarted(10000)) {
        qWarning() << "Fixture: konnte Prozess nicht starten:" << program << args;
        return false;
    }
    if (!proc.waitForFinished(30000)) {
        qWarning() << "Fixture: Timeout:" << program << args;
        proc.kill();
        proc.waitForFinished(3000);
        return false;
    }
    if (stdOut)
        *stdOut = proc.readAllStandardOutput();
    if (stdErr)
        *stdErr = proc.readAllStandardError();
    if (exitCode)
        *exitCode = proc.exitCode();
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

bool runSvn(const QStringList &args, const QString &workDir, QByteArray *stdOut, int *exitCode)
{
    QByteArray err;
    bool ok = run(QStringLiteral("svn"), args, workDir, stdOut, &err, exitCode);
    if (!ok)
        qWarning() << "svn" << args << "in" << workDir << "->" << err;
    return ok;
}

QString fileUrl(const QString &localPath)
{
    QString p = QDir(localPath).absolutePath();
    p.replace(QLatin1Char(' '), QStringLiteral("%20"));
    return QStringLiteral("file://") + p;
}

QString createEmptyRepo(const QString &root)
{
    const QString repoDir = root + QStringLiteral("/repo");
    QByteArray err;
    if (!run(QStringLiteral("svnadmin"), {QStringLiteral("create"), repoDir}, QString(), nullptr, &err))
        qFatal("Fixture: svnadmin create fehlgeschlagen: %s", err.constData());
    return fileUrl(repoDir);
}

void createStandardLayout(const QString &repoUrl)
{
    if (!runSvn({QStringLiteral("mkdir"),
                 repoUrl + QStringLiteral("/trunk"),
                 repoUrl + QStringLiteral("/branches"),
                 repoUrl + QStringLiteral("/tags"),
                 QStringLiteral("-m"), QStringLiteral("Create standard layout")}))
        qFatal("Fixture: svn mkdir (layout) fehlgeschlagen");
}

bool checkout(const QString &repoUrl, const QString &wcPath, const QString &revision)
{
    return runSvn({QStringLiteral("checkout"), QStringLiteral("--revision"), revision,
                    repoUrl, wcPath});
}

void writeFile(const QString &path, const QString &content)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        qFatal("Fixture: konnte Datei nicht schreiben: %s", qUtf8Printable(path));
    QTextStream out(&f);
    out << content;
}

bool svnAdd(const QString &path)
{
    return runSvn({QStringLiteral("add"), path});
}

bool svnCommit(const QString &workDir, const QString &message)
{
    return runSvn({QStringLiteral("commit"), QStringLiteral("-m"), message}, workDir);
}

bool svnCopy(const QString &srcUrl, const QString &dstUrl, const QString &message)
{
    return runSvn({QStringLiteral("copy"), srcUrl, dstUrl,
                    QStringLiteral("-m"), message});
}

MergeScenario makeMergedBranchScenario(const QString &root)
{
    MergeScenario s;
    s.repoUrl = createEmptyRepo(root);
    createStandardLayout(s.repoUrl);
    s.trunkUrl = s.repoUrl + QStringLiteral("/trunk");
    s.branchUrl = s.repoUrl + QStringLiteral("/branches/b");
    s.trunkWc = root + QStringLiteral("/wc_trunk");
    s.branchWc = root + QStringLiteral("/wc_branch");

    if (!checkout(s.trunkUrl, s.trunkWc))
        qFatal("Fixture: checkout trunk fehlgeschlagen");

    // r2: baseline file on trunk.
    writeFile(s.trunkWc + QStringLiteral("/file1.txt"), QStringLiteral("line1\nline2\nline3\n"));
    if (!svnAdd(s.trunkWc + QStringLiteral("/file1.txt")) || !svnCommit(s.trunkWc, QStringLiteral("Add file1.txt")))
        qFatal("Fixture: initial commit trunk fehlgeschlagen");

    // r3: Branch von trunk.
    if (!svnCopy(s.trunkUrl, s.branchUrl, QStringLiteral("Create branch b")))
        qFatal("Fixture: branch-copy fehlgeschlagen");

    if (!checkout(s.branchUrl, s.branchWc))
        qFatal("Fixture: checkout branch fehlgeschlagen");

    // r4: first branch commit.
    writeFile(s.branchWc + QStringLiteral("/file1.txt"), QStringLiteral("line1\nBRANCH CHANGE 1\nline3\n"));
    if (!svnCommit(s.branchWc, QStringLiteral("Branch change 1")))
        qFatal("Fixture: branch commit r4 fehlgeschlagen");

    // r5: second branch commit.
    writeFile(s.branchWc + QStringLiteral("/file1.txt"), QStringLiteral("line1\nBRANCH CHANGE 1\nBRANCH CHANGE 2\n"));
    if (!svnCommit(s.branchWc, QStringLiteral("Branch change 2")))
        qFatal("Fixture: branch commit r5 fehlgeschlagen");

    // After its own commit, trunkWc may be mixed-revision (only file1.txt was
    // bumped at r2) — 'svn merge' requires a uniform revision in the target
    // tree, so update first.
    if (!runSvn({QStringLiteral("update")}, s.trunkWc))
        qFatal("Fixture: update trunkWc vor Merge fehlgeschlagen");

    // r6: Merge branch -> trunk, committed.
    if (!runSvn({QStringLiteral("merge"), s.branchUrl, QStringLiteral(".")}, s.trunkWc))
        qFatal("Fixture: merge branch->trunk fehlgeschlagen");
    if (!svnCommit(s.trunkWc, QStringLiteral("Merge branch b into trunk")))
        qFatal("Fixture: merge-commit fehlgeschlagen");

    return s;
}

MergeScenario makeEligibleBranchScenario(const QString &root)
{
    // Same setup up to r5, but without the merge commit — duplicates the first
    // steps instead of calling makeMergedBranchScenario() and "undoing" the last
    // step, so the revision history matches the description in the test plan
    // exactly (r4/r5 open).
    MergeScenario s;
    s.repoUrl = createEmptyRepo(root);
    createStandardLayout(s.repoUrl);
    s.trunkUrl = s.repoUrl + QStringLiteral("/trunk");
    s.branchUrl = s.repoUrl + QStringLiteral("/branches/b");
    s.trunkWc = root + QStringLiteral("/wc_trunk");
    s.branchWc = root + QStringLiteral("/wc_branch");

    if (!checkout(s.trunkUrl, s.trunkWc))
        qFatal("Fixture: checkout trunk fehlgeschlagen");

    writeFile(s.trunkWc + QStringLiteral("/file1.txt"), QStringLiteral("line1\nline2\nline3\n"));
    if (!svnAdd(s.trunkWc + QStringLiteral("/file1.txt")) || !svnCommit(s.trunkWc, QStringLiteral("Add file1.txt")))
        qFatal("Fixture: initial commit trunk fehlgeschlagen");

    if (!svnCopy(s.trunkUrl, s.branchUrl, QStringLiteral("Create branch b")))
        qFatal("Fixture: branch-copy fehlgeschlagen");

    if (!checkout(s.branchUrl, s.branchWc))
        qFatal("Fixture: checkout branch fehlgeschlagen");

    writeFile(s.branchWc + QStringLiteral("/file1.txt"), QStringLiteral("line1\nBRANCH CHANGE 1\nline3\n"));
    if (!svnCommit(s.branchWc, QStringLiteral("Branch change 1")))
        qFatal("Fixture: branch commit r4 fehlgeschlagen");

    writeFile(s.branchWc + QStringLiteral("/file1.txt"), QStringLiteral("line1\nBRANCH CHANGE 1\nBRANCH CHANGE 2\n"));
    if (!svnCommit(s.branchWc, QStringLiteral("Branch change 2")))
        qFatal("Fixture: branch commit r5 fehlgeschlagen");

    return s;
}

MergeScenario makeConflictScenario(const QString &root)
{
    MergeScenario s;
    s.repoUrl = createEmptyRepo(root);
    createStandardLayout(s.repoUrl);
    s.trunkUrl = s.repoUrl + QStringLiteral("/trunk");
    s.branchUrl = s.repoUrl + QStringLiteral("/branches/b");
    s.trunkWc = root + QStringLiteral("/wc_trunk");
    s.branchWc = root + QStringLiteral("/wc_branch");

    if (!checkout(s.trunkUrl, s.trunkWc))
        qFatal("Fixture: checkout trunk fehlgeschlagen");

    // r2: base files for the text and tree conflict.
    writeFile(s.trunkWc + QStringLiteral("/conflict.txt"), QStringLiteral("line1\nORIGINAL\nline3\n"));
    writeFile(s.trunkWc + QStringLiteral("/fileB.txt"), QStringLiteral("original content\n"));
    if (!svnAdd(s.trunkWc + QStringLiteral("/conflict.txt")) || !svnAdd(s.trunkWc + QStringLiteral("/fileB.txt"))
        || !svnCommit(s.trunkWc, QStringLiteral("Add conflict.txt + fileB.txt")))
        qFatal("Fixture: initial commit trunk fehlgeschlagen");

    // r3: Branch von trunk.
    if (!svnCopy(s.trunkUrl, s.branchUrl, QStringLiteral("Create branch b")))
        qFatal("Fixture: branch-copy fehlgeschlagen");

    // r4: trunk changes the same line AND deletes fileB.txt.
    if (!checkout(s.trunkUrl, s.trunkWc + QStringLiteral("_tmp")))
        qFatal("Fixture: temp-checkout trunk fehlgeschlagen");
    writeFile(s.trunkWc + QStringLiteral("_tmp/conflict.txt"), QStringLiteral("line1\nTRUNK CHANGE\nline3\n"));
    if (!runSvn({QStringLiteral("delete"), QStringLiteral("fileB.txt")}, s.trunkWc + QStringLiteral("_tmp")))
        qFatal("Fixture: delete fileB.txt (trunk) fehlgeschlagen");
    if (!svnCommit(s.trunkWc + QStringLiteral("_tmp"), QStringLiteral("Trunk: change conflict.txt + delete fileB.txt")))
        qFatal("Fixture: trunk commit r4 fehlgeschlagen");

    if (!checkout(s.branchUrl, s.branchWc))
        qFatal("Fixture: checkout branch fehlgeschlagen");

    // r5: branch changes the same line differently AND edits fileB.txt.
    writeFile(s.branchWc + QStringLiteral("/conflict.txt"), QStringLiteral("line1\nBRANCH CHANGE\nline3\n"));
    writeFile(s.branchWc + QStringLiteral("/fileB.txt"), QStringLiteral("original content\nedited on branch\n"));
    if (!svnCommit(s.branchWc, QStringLiteral("Branch: change conflict.txt + edit fileB.txt")))
        qFatal("Fixture: branch commit r5 fehlgeschlagen");

    // Update trunkWc (the actual WC used by the tests) to r4.
    if (!runSvn({QStringLiteral("update")}, s.trunkWc))
        qFatal("Fixture: update trunkWc fehlgeschlagen");

    // Merge branch -> trunk: produces a text conflict (conflict.txt) and a tree
    // conflict (fileB.txt: locally deleted, incoming edit).
    // A non-zero exit code is EXPECTED here (conflict) — deliberately not
    // checked via qFatal, the return value is ignored.
    runSvn({QStringLiteral("merge"), QStringLiteral("--accept"), QStringLiteral("postpone"),
            s.branchUrl, QStringLiteral(".")}, s.trunkWc);

    return s;
}

PegScenario makePegAddScenario(const QString &root)
{
    PegScenario s;
    s.repoUrl = createEmptyRepo(root);
    createStandardLayout(s.repoUrl);
    s.trunkUrl = s.repoUrl + QStringLiteral("/trunk");
    s.trunkWc = root + QStringLiteral("/wc_trunk");

    if (!checkout(s.trunkUrl, s.trunkWc))
        qFatal("Fixture: checkout trunk fehlgeschlagen");

    // r2: baseline without newfile.txt.
    writeFile(s.trunkWc + QStringLiteral("/baseline.txt"), QStringLiteral("baseline\n"));
    if (!svnAdd(s.trunkWc + QStringLiteral("/baseline.txt")) || !svnCommit(s.trunkWc, QStringLiteral("Baseline")))
        qFatal("Fixture: baseline commit fehlgeschlagen");

    // r3: change baseline.txt (noise before the add).
    writeFile(s.trunkWc + QStringLiteral("/baseline.txt"), QStringLiteral("baseline\nmore\n"));
    if (!svnCommit(s.trunkWc, QStringLiteral("Change baseline")))
        qFatal("Fixture: baseline-change commit fehlgeschlagen");

    // r4: newfile.txt is newly added.
    writeFile(s.trunkWc + QStringLiteral("/newfile.txt"), QStringLiteral("first\nsecond\nthird\n"));
    if (!svnAdd(s.trunkWc + QStringLiteral("/newfile.txt")) || !svnCommit(s.trunkWc, QStringLiteral("Add newfile.txt")))
        qFatal("Fixture: add newfile.txt commit fehlgeschlagen");

    // After the commits trunkWc may be mixed-revision (only the changed paths
    // are bumped) — update before 'svn info', otherwise the ROOT revision
    // (possibly still r1) would be read instead of HEAD.
    if (!runSvn({QStringLiteral("update")}, s.trunkWc))
        qFatal("Fixture: update trunkWc vor svn info fehlgeschlagen");

    QByteArray infoOut;
    if (!runSvn({QStringLiteral("info"), s.trunkWc}, QString(), &infoOut))
        qFatal("Fixture: svn info (HEAD-Revision) fehlgeschlagen");
    const QString info = QString::fromUtf8(infoOut);
    const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("Revision: (\\d+)")).match(info);
    s.addedFileRevision = m.hasMatch() ? m.captured(1) : QStringLiteral("4");

    return s;
}

UpdateConflictScenario makeUpdateConflictScenario(const QString &root)
{
    UpdateConflictScenario s;
    s.repoUrl = createEmptyRepo(root);
    createStandardLayout(s.repoUrl);
    s.trunkUrl = s.repoUrl + QStringLiteral("/trunk");
    s.wcA = root + QStringLiteral("/wc_a");
    s.wcB = root + QStringLiteral("/wc_b");

    if (!checkout(s.trunkUrl, s.wcA))
        qFatal("Fixture: checkout wcA fehlgeschlagen");

    writeFile(s.wcA + QStringLiteral("/conflict.txt"), QStringLiteral("line1\nORIGINAL\nline3\n"));
    if (!svnAdd(s.wcA + QStringLiteral("/conflict.txt")) || !svnCommit(s.wcA, QStringLiteral("Add conflict.txt")))
        qFatal("Fixture: initial commit wcA fehlgeschlagen");

    if (!checkout(s.trunkUrl, s.wcB))
        qFatal("Fixture: checkout wcB fehlgeschlagen");

    // wcA changes + commits first.
    writeFile(s.wcA + QStringLiteral("/conflict.txt"), QStringLiteral("line1\nCHANGED BY A\nline3\n"));
    if (!svnCommit(s.wcA, QStringLiteral("A changes conflict.txt")))
        qFatal("Fixture: A-commit fehlgeschlagen");

    // wcB changes the same line differently, BEFORE the update -> local change.
    writeFile(s.wcB + QStringLiteral("/conflict.txt"), QStringLiteral("line1\nCHANGED BY B\nline3\n"));

    // Updating wcB produces the text conflict (the exit code may be != 0
    // depending on the svn version for conflicts during 'update' — deliberately
    // ignored).
    runSvn({QStringLiteral("update")}, s.wcB);

    return s;
}

MergeScenario makeMergeRangeScenario(const QString &root)
{
    MergeScenario s;
    s.repoUrl = createEmptyRepo(root);
    createStandardLayout(s.repoUrl);
    s.trunkUrl = s.repoUrl + QStringLiteral("/trunk");
    s.branchUrl = s.repoUrl + QStringLiteral("/branches/b");
    s.trunkWc = root + QStringLiteral("/wc_trunk");
    s.branchWc = root + QStringLiteral("/wc_branch");

    if (!checkout(s.trunkUrl, s.trunkWc))
        qFatal("Fixture: checkout trunk fehlgeschlagen");

    // r2: baseline with two independent files.
    writeFile(s.trunkWc + QStringLiteral("/file1.txt"), QStringLiteral("line1\nline2\nline3\n"));
    writeFile(s.trunkWc + QStringLiteral("/file2.txt"), QStringLiteral("f2\n"));
    if (!svnAdd(s.trunkWc + QStringLiteral("/file1.txt")) || !svnAdd(s.trunkWc + QStringLiteral("/file2.txt"))
        || !svnCommit(s.trunkWc, QStringLiteral("Add file1.txt + file2.txt")))
        qFatal("Fixture: initial commit trunk fehlgeschlagen");

    // r3: Branch von trunk.
    if (!svnCopy(s.trunkUrl, s.branchUrl, QStringLiteral("Create branch b")))
        qFatal("Fixture: branch-copy fehlgeschlagen");

    if (!checkout(s.branchUrl, s.branchWc))
        qFatal("Fixture: checkout branch fehlgeschlagen");

    // r4: branch changes file1.txt (will later collide with r5).
    writeFile(s.branchWc + QStringLiteral("/file1.txt"), QStringLiteral("line1\nBRANCH-r4\nline3\n"));
    if (!svnCommit(s.branchWc, QStringLiteral("Branch: change file1.txt (r4)")))
        qFatal("Fixture: branch commit r4 fehlgeschlagen");

    // r5: trunk changes the same line differently (conflict source for merging r4).
    if (!runSvn({QStringLiteral("update")}, s.trunkWc))
        qFatal("Fixture: update trunkWc vor r5 fehlgeschlagen");
    writeFile(s.trunkWc + QStringLiteral("/file1.txt"), QStringLiteral("line1\nTRUNK-r5\nline3\n"));
    if (!svnCommit(s.trunkWc, QStringLiteral("Trunk: change file1.txt (r5)")))
        qFatal("Fixture: trunk commit r5 fehlgeschlagen");

    // r6+r7: branch changes file2.txt independently (no conflict with trunk).
    if (!runSvn({QStringLiteral("update")}, s.branchWc))
        qFatal("Fixture: update branchWc vor r6 fehlgeschlagen");
    writeFile(s.branchWc + QStringLiteral("/file2.txt"), QStringLiteral("f2\nb6\n"));
    if (!svnCommit(s.branchWc, QStringLiteral("Branch: change file2.txt (r6)")))
        qFatal("Fixture: branch commit r6 fehlgeschlagen");
    writeFile(s.branchWc + QStringLiteral("/file2.txt"), QStringLiteral("f2\nb6\nb7\n"));
    if (!svnCommit(s.branchWc, QStringLiteral("Branch: change file2.txt (r7)")))
        qFatal("Fixture: branch commit r7 fehlgeschlagen");

    // Keep trunkWc current at r5 (the last own revision) — the merge itself is
    // called by the tests, not here.
    if (!runSvn({QStringLiteral("update")}, s.trunkWc))
        qFatal("Fixture: finales update trunkWc fehlgeschlagen");

    return s;
}

void registerMetaTypes()
{
    qRegisterMetaType<SvnOperationResult>();
}

SvnOperationResult runAsyncAndWait(SvnManager &mgr, const std::function<void()> &op, int timeoutMs)
{
    QSignalSpy spy(&mgr, &SvnManager::operationFinished);
    op();

    QEventLoop loop;
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    QObject::connect(&mgr, &SvnManager::operationFinished, &loop, &QEventLoop::quit);
    if (spy.isEmpty())
        loop.exec();

    if (spy.isEmpty())
        return SvnOperationResult{}; // Timeout -> success=false (Default)
    return qvariant_cast<SvnOperationResult>(spy.first().at(0));
}

} // namespace TestFixtures
