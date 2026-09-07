#pragma once

#include "svnmanager.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>

// SvnOperationResult is not registered as a Qt metatype in code/ (direct
// signal/slot use is enough there) — for QSignalSpy/QVariant it is registered
// centrally here, so test files do not each have to declare it themselves.
Q_DECLARE_METATYPE(SvnOperationResult)

// Low-level + scenario helpers for creating fresh file:// SVN repos for the
// backend tests. Every scenario is built under an empty root directory provided
// by the caller (typically a QTemporaryDir::path()), so every test run is
// guaranteed to start from a fresh state.
namespace TestFixtures {

// Runs a command synchronously (timeout 30s), returns true on exit code 0.
// Low-level — also used for commands meant to fail (e.g. 'svn merge' returns
// exit 1 on a conflict). The scenario builders call qFatal() on UNEXPECTED
// errors (repo creation, checkout, commit), since a broken fixture makes every
// test built on it meaningless.
bool run(const QString &program, const QStringList &args,
         const QString &workDir = QString(),
         QByteArray *stdOut = nullptr, QByteArray *stdErr = nullptr,
         int *exitCode = nullptr);

bool runSvn(const QStringList &args, const QString &workDir = QString(),
            QByteArray *stdOut = nullptr, int *exitCode = nullptr);

// Converts a local directory path into a file:// URL (spaces are escaped;
// not needed in production code since we build the URLs ourselves).
QString fileUrl(const QString &localPath);

// Creates an empty SVN repository under <root>/repo and returns its file:// URL.
QString createEmptyRepo(const QString &root);

// Creates trunk/branches/tags in the repository (one commit, r1).
void createStandardLayout(const QString &repoUrl);

// Checks out <repoUrl> into <wcPath>.
bool checkout(const QString &repoUrl, const QString &wcPath,
              const QString &revision = QStringLiteral("HEAD"));

// Writes a text file (overwrites existing content).
void writeFile(const QString &path, const QString &content);

// svn add <path> (recursion not needed, path is usually a single file).
bool svnAdd(const QString &path);

// svn commit -m message in workDir; returns true on success.
bool svnCommit(const QString &workDir, const QString &message);

// svn copy src -> dst (server-side), with a commit message.
bool svnCopy(const QString &srcUrl, const QString &dstUrl, const QString &message);

// --- Scenario fixtures ---

struct MergeScenario {
    QString repoUrl;
    QString trunkUrl;
    QString branchUrl;
    QString trunkWc;
    QString branchWc;
};

// Scenario 1: trunk + branches/b, several commits on the branch, the merge into
// trunk COMMITTED (for 'svn log -g' nesting tests).
// Revisions: r1 layout, r2 trunk baseline, r3 branch copy,
// r4+r5 branch commits, r6 = merge commit on trunk (r4,r5 as children).
MergeScenario makeMergedBranchScenario(const QString &root);

// Scenario 2: as above, but WITHOUT the merge commit — r4/r5 are still open
// (eligible) on the branch for trunk.
MergeScenario makeEligibleBranchScenario(const QString &root);

// Scenario 3: merge with a text conflict (conflict.txt, opposing edits of the
// same line) AND a tree conflict (fileB.txt: locally deleted, incoming edit) in
// the trunk WC. The merge is run but NOT committed — the WC stays in the
// conflict state for the tests.
MergeScenario makeConflictScenario(const QString &root);

struct PegScenario {
    QString repoUrl;
    QString trunkUrl;
    QString trunkWc;
    QString addedFileRevision; // the revision in which newfile.txt was added
};

// Scenario 4: newfile.txt is only added in a later revision (for
// diffChangeSync/cat with a peg revision and 'log --stop-on-copy').
PegScenario makePegAddScenario(const QString &root);

struct UpdateConflictScenario {
    QString repoUrl;
    QString trunkUrl;
    QString wcA;
    QString wcB; // holds the text conflict after the update (.mine/.rNNN helpers)
};

// Two WCs of the same trunk file; wcA commits a change, wcB changes the same
// line differently and is updated AFTERWARDS ('svn update') — this produces a
// classic update conflict with the helper files "conflict.txt.mine" /
// "conflict.txt.rN" (≠ the merge helpers from scenario 3).
UpdateConflictScenario makeUpdateConflictScenario(const QString &root);

// Scenario for mergeRangeAsync with an interrupted merge (see memory
// "Nachtrag 2"): r4 (branch) changes file1.txt in a conflict-prone way, r5
// (trunk) changes the same line differently, r6+r7 (branch) change file2.txt
// independently. trunkWc is already updated to r5.
// - 'svn merge -c 6,7' (contiguous, no conflict) -> exit 0
// - 'svn merge -c 4,7' (conflict at r4, r7 is no longer applied)
//   -> E155015 / exit 1, the status still shows 'conflicted' on file1.txt
MergeScenario makeMergeRangeScenario(const QString &root);

// Registers qRegisterMetaType<SvnOperationResult>() — must be called once per
// test binary before the first QSignalSpy use (e.g. in initTestCase()).
void registerMetaTypes();

// Starts 'op' (must call an *Async method on mgr) and waits synchronously for
// 'operationFinished' (timeout default 30s). On timeout, a default-constructed
// SvnOperationResult (success=false) is returned.
SvnOperationResult runAsyncAndWait(SvnManager &mgr, const std::function<void()> &op,
                                    int timeoutMs = 30000);

} // namespace TestFixtures
