#pragma once

#include "svntypes.h"

#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QByteArray>

class QProcess;

/**
 * Central class for all SVN operations.
 *
 * Synchronous methods (status, info, log, cat) run in the calling thread and
 * block until the result is ready. They suit short operations only.
 *
 * Asynchronous methods (updateAsync, commitAsync, ...) start a background
 * process and emit signals for progress and completion.
 */
class SvnManager : public QObject
{
    Q_OBJECT

public:
    explicit SvnManager(QObject *parent = nullptr);
    ~SvnManager() override;

    // --- Configuration ---

    void setSvnBinary(const QString &path);
    QString svnBinary() const;

    // Timeout for synchronous SVN operations in milliseconds (default 30000).
    void setProcessSyncTimeout(int ms);
    int  processSyncTimeout() const;

    // --- Synchronous queries ---

    /** Checks whether 'path' lies inside an SVN working copy. */
    bool isSvnWorkingCopy(const QString &path) const;

    /**
     * Returns the SVN status of all files under 'path'.
     * Uses 'svn status --xml'. showUpdates=true adds '--show-updates'
     * ('svn status -u') and additionally fills remoteTextStatus/
     * remotePropStatus per entry (also for purely remotely changed, locally
     * unchanged entries that would otherwise not be listed at all).
     * includeIgnored=true adds '--no-ignore': 'svn status' otherwise hides
     * paths ignored via svn:ignore COMPLETELY from the output (no entry, not
     * even with item="ignored"). Callers that must distinguish ignored files
     * from "unchanged versioned" (e.g. the overlay plugin) need this explicit
     * entry.
     */
    QList<SvnStatusEntry> status(const QString &path, bool recursive = true,
                                 bool *ok = nullptr, bool showUpdates = false,
                                 bool includeIgnored = false) const;

    /**
     * Returns SVN metadata for 'path' (URL, revision, author …).
     * Uses 'svn info --xml'.
     */
    SvnInfo info(const QString &path) const;

    /**
     * Determines, for a file in a conflict state, the three conflict files
     * (Base = prev-base-file, Mine = prev-wc-file or the working file,
     * Theirs = cur-base-file) from 'svn info --xml'. Returns false if the file
     * is not in a text conflict or the files are missing.
     */
    bool conflictFilesSync(const QString &path, QString *baseFile,
                           QString *mineFile, QString *theirsFile) const;

    /**
     * Description of a tree conflict from 'svn info --xml'
     * (<tree-conflict> operation/action/reason), e.g.
     * "merge: incoming delete vs. local edit". Empty if there is none.
     */
    QString treeConflictDescriptionSync(const QString &path) const;

    /**
     * Revisions of the source according to merge tracking
     * ('svn mergeinfo --show-revs <showRevs> source wcPath').
     * showRevs: "merged" (already merged) or "eligible" (still open).
     * Error → empty set + ok=false. IMPORTANT: an empty set with ok=true is a
     * valid result (e.g. everything merged → no eligible revisions).
     */
    QSet<long long> mergedRevisionsSync(
        const QString &source, const QString &wcPath,
        const QString &showRevs = QStringLiteral("merged"),
        bool *ok = nullptr) const;

    /**
     * Builds the comment appendix for the merge commit template (F-ARCH1):
     * determines the revisions to merge (explicit 'ranges' such as "5-7,10" are
     * parsed; for empty 'ranges' the eligible revisions of 'source' via
     * mergedRevisionsSync), fetches their log comments with ONE log() call over
     * the min-max span, filters to the revision set and appends them in
     * ascending order as "\n\n---\nrN (author): message". Cap 50; revisions
     * beyond that are counted in overflowCount (the GUI turns that into the
     * i18n "(and N more)" line). Uses only mergedRevisionsSync + log, so it is
     * backend-testable without GUI/WaitCursor.
     */
    SvnMergeMessageParts buildMergeMessage(const QString &url,
                                           const QString &ranges,
                                           const QString &wcPath) const;

    /**
     * Exports 'target' (WC path or repository URL) at revision 'revision' to
     * 'destDir' (which must not exist yet). Synchronous; for historical folder
     * comparisons in the external diff tool.
     */
    bool exportSync(const QString &target, const QString &revision,
                    const QString &destDir) const;

    /**
     * Returns the commit history for 'path'.
     * Uses 'svn log --xml --verbose'. options: --stop-on-copy and/or
     * -g (merged revisions as entries with mergeDepth > 0, directly below their
     * merge revision).
     */
    QList<SvnLogEntry> log(const QString &path,
                            int limit = 100,
                            const QString &fromRev = QString(),
                            const QString &toRev = QString(),
                            const SvnLogOptions &options = {}) const;

    /**
     * Returns the file content at a given revision.
     * Uses 'svn cat --revision'. For the diff functionality.
     */
    QByteArray cat(const QString &path,
                   const QString &revision = QStringLiteral("HEAD")) const;

    /**
     * Returns all SVN properties of 'path' (name → value).
     * Uses 'svn proplist --xml --verbose'.
     */
    /**
     * Lists the content of a repository URL ('svn list --xml').
     * For the repository browser; depth immediates (one level).
     */
    QList<SvnListEntry> listSync(const QString &url,
                                 const QString &revision = QStringLiteral("HEAD"),
                                 bool *ok = nullptr) const;

    /**
     * Returns the line-by-line annotation ('svn annotate --xml').
     * Empty list on error (e.g. an unversioned or binary file).
     */
    QList<SvnBlameEntry> blameSync(const QString &path) const;

    /**
     * Returns the text diff for 'path' between revFrom and revTo.
     * Uses 'svn diff --revision from:to'.
     * Empty string on error.
     */
    QString diffSync(const QString &path,
                     const QString &revFrom,
                     const QString &revTo) const;

    /**
     * Returns the diff of the change in revision 'rev' (rev-1 → rev).
     * Uses 'svn diff --change rev target@rev'. The peg revision makes this work
     * even for files added in 'rev' (the path does not exist in rev-1 yet).
     * 'target' can be a WC path or a repository URL.
     * Empty string on error.
     */
    QString diffChangeSync(const QString &target, const QString &rev) const;

    /**
     * Writes the local changes of 'paths' as a patch file to 'outFile'
     * ('svn diff --git path...'). '--git' provides (a) markers for
     * added/deleted/binary files and (b) portable path names
     * ("diff --git a/trunk/sub/x.txt …") instead of the absolute paths of the
     * "Index:" line. The output is written byte for byte (without UTF-8
     * re-encoding), so files in other encodings stay intact.
     *
     * ATTENTION: the '--git' paths are relative to the REPOSITORY root, not the
     * WC root. A WC of ^/trunk produces "a/trunk/sub/x.txt". When applying, this
     * prefix must be removed via '--strip'; detectPatchStripLevelSync() is there
     * for that. ('svn patch' evaluates only the git header for git patches, the
     * "Index:" line is ignored — verified with svn 1.14.5.)
     *
     * false on an svn error AND on an empty diff (no local changes — an empty
     * patch file would be useless); errorMessage describes both.
     */
    bool createPatchSync(const QStringList &paths, const QString &outFile,
                         QString *errorMessage = nullptr) const;

    /**
     * Detects from the output of 'svn patch' whether hunks were rejected.
     * NEEDED because 'svn patch' also exits with code 0 on rejects and the WC
     * status afterwards only shows "modified" (no conflict state) — the rejected
     * hunks land in '<file>.svnpatch.rej'. Detected are the markers
     * ">  rejected hunk …" and "Summary of conflicts:" (LC_MESSAGES=C is
     * guaranteed by cLocaleEnvironment(), so the markers are stable).
     */
    static bool patchOutputHasRejects(const QStringList &outputLines);

    /**
     * The target paths of a patch as 'svn patch' sees them: from the
     * "--- <path>\t(…)" lines, without the leading "a/" for git patches.
     * Pure text analysis (static + testable without a working copy).
     */
    static QStringList patchTargetPaths(const QByteArray &patchContent);

    /**
     * Determines the matching '--strip' argument for 'patchFile' in 'wcPath':
     * the number of leading path components whose removal makes the patch's
     * paths match the working copy. A patch from a ^/trunk WC contains
     * "trunk/src/x.cpp" and needs '--strip 1' in another ^/trunk WC.
     *
     * Chosen is the smallest level with the most matches on the filesystem. If
     * no level finds a match (e.g. a patch that only adds new files), it falls
     * back to the depth of the repository-relative URL of 'wcPath' (^/trunk → 1),
     * assuming patch and target share the same branch layout.
     */
    int detectPatchStripLevelSync(const QString &patchFile,
                                  const QString &wcPath) const;

    QMap<QString, QString> propListSync(const QString &path) const;

    /** Sets an SVN property (value may be multi-line). */
    bool propSetSync(const QString &path, const QString &name, const QString &value);

    /** Deletes an SVN property. */
    bool propDelSync(const QString &path, const QString &name);

    /**
     * Adds 'pattern' (e.g. "foo.txt" or "*.log") to the svn:ignore property of
     * 'dirPath' (line-based; lines already present are not added twice).
     * Returns false only on a propset error.
     */
    bool addToIgnoreSync(const QString &dirPath, const QString &pattern);

    // --- Asynchronous operations ---
    // The signals operationFinished / errorOccurred are emitted on completion.

    void updateAsync(const QString &path,
                     const QString &revision = QStringLiteral("HEAD"),
                     const QString &depth = QStringLiteral("infinity"));

    /** keepLocks: --no-unlock — locks stay in place after the commit. */
    void commitAsync(const QStringList &paths, const QString &message,
                     bool keepLocks = false);

    void revertAsync(const QStringList &paths,
                     const QString &depth = QStringLiteral("infinity"));

    void addAsync(const QStringList &paths,
                  const QString &depth = QStringLiteral("infinity"));

    bool addSync(const QStringList &paths,
                 const QString &depth = QStringLiteral("empty"));

    // keepLocal: --keep-local (removes from version control but keeps the files
    // on disk; this also succeeds when the items have local modifications).
    // force: --force (deletes even items with local modifications, discarding
    // them). keepLocal and force are meant to be used one at a time.
    void removeAsync(const QStringList &paths, bool keepLocal = true,
                     bool force = false);

    /** Renames or moves a file/directory (keeps SVN history). */
    void moveAsync(const QString &srcPath, const QString &dstPath);

    /** Locks files in the repository. force = steal lock. */
    void lockAsync(const QStringList &paths, const QString &message, bool force = false);

    /** Releases locks on files. */
    void unlockAsync(const QStringList &paths);

    /** Exports a WC path without .svn directories. */
    void exportAsync(const QString &srcPath, const QString &dstPath,
                     const QString &revision = QStringLiteral("HEAD"),
                     bool force = false);

    /** Cleans up a corrupt or locked working copy. */
    void cleanupAsync(const QString &path);

    /** Resolves conflicts. accept: "working", "mine-full" or "theirs-full". */
    void resolveAsync(const QStringList &paths, const QString &accept);

    /** Creates a branch or tag via svn copy (server-side). */
    void copyAsync(const QString &srcUrl, const QString &dstUrl,
                   const QString &message,
                   const QString &revision = QStringLiteral("HEAD"));

    /** Changes the working copy URL (svn switch). */
    void switchAsync(const QString &path, const QString &url,
                     const QString &revision = QStringLiteral("HEAD"),
                     const QString &depth    = QStringLiteral("infinity"));

    /**
     * Records a purely syntactic URL change in the WC metadata
     * ('svn relocate fromUrl toUrl wcPath') — for the case that only the
     * repository URL has changed (server move, DNS change) while the WC still
     * maps the same location in the same repository. Unlike switchAsync, NOTHING
     * is fetched from the repository.
     */
    void relocateAsync(const QString &wcPath, const QString &fromUrl,
                       const QString &toUrl);

    /**
     * Applies 'patchFile' to the working copy 'wcPath'
     * ('svn patch [--dry-run] [--reverse-diff] patchFile wcPath').
     * dryRun changes nothing on disk but produces the same lines.
     * reverse undoes the patch (--reverse-diff).
     *
     * strip: '--strip N' (0 = omit). N counts AFTER removing the git prefix
     * "a/" — for "a/trunk/src/x.cpp", N=1 makes it "src/x.cpp". Determine the
     * matching value with detectPatchStripLevelSync().
     *
     * ATTENTION: success (success=true) does NOT mean everything was applied —
     * on non-matching hunks the exit code stays 0. Callers must run
     * result.output through patchOutputHasRejects().
     */
    void applyPatchAsync(const QString &wcPath, const QString &patchFile,
                         bool dryRun = false, bool reverse = false,
                         int strip = 0);

    // --- Direct repository operations (repo browser, 1 commit each) ---

    /**
     * Creates a local repository via 'svnadmin create' (synchronous).
     * repoDir must be empty or not exist.
     * false on error; errorMessage then contains the svnadmin output.
     */
    bool createRepositorySync(const QString &repoDir,
                              QString *errorMessage = nullptr) const;

    /** Creates a folder directly in the repository. */
    void mkdirUrlAsync(const QString &url, const QString &message);

    /** Creates several folder URLs in ONE commit (standard layout). */
    void mkdirUrlsAsync(const QStringList &urls, const QString &message);

    /** Deletes a URL directly in the repository. */
    void deleteUrlAsync(const QString &url, const QString &message);

    /** Renames / moves directly in the repository. */
    void moveUrlAsync(const QString &srcUrl, const QString &dstUrl,
                      const QString &message);

    // --- Merge ---

    /**
     * Merges a range of revisions from 'url' into the WC.
     * revisionRanges: empty = all not-yet-merged revisions,
     * otherwise e.g. "5,8,10-12" (passed through to 'svn merge -c').
     */
    void mergeRangeAsync(const QString &url, const QString &revisionRanges,
                         const QString &wcPath,
                         const SvnMergeOptions &options = {});

    /** Merges two trees: 'svn merge URL1@R1 URL2@R2 wcPath'. */
    void mergeTreesAsync(const QString &url1, const QString &rev1,
                         const QString &url2, const QString &rev2,
                         const QString &wcPath,
                         const SvnMergeOptions &options = {});

    /** Checks out a repository URL into a local directory. */
    void checkoutAsync(const QString &url, const QString &localPath,
                       const QString &revision = QStringLiteral("HEAD"),
                       const QString &depth = QStringLiteral("infinity"));

    /**
     * Imports 'localDir' recursively (default depth infinity) directly to 'url'
     * in the repository — ONE commit. 'localDir' remains a plain (unversioned)
     * local copy afterwards. A checkout is needed to obtain a working copy of
     * the imported content.
     */
    void importAsync(const QString &localDir, const QString &url,
                     const QString &message);

    /** Cancels a running asynchronous operation. */
    void cancelAsync();

    /** Returns true while an operation is running. */
    bool isBusy() const;

Q_SIGNALS:
    /** Emitted for every line the SVN operation outputs. */
    void progressOutput(const QString &line);

    /** Emitted after an asynchronous operation completes. */
    void operationFinished(const SvnOperationResult &result);

private:
    // Runs svn synchronously and returns stdout.
    // On error, ok=false and the return value contains stderr.
    QString runSvnSync(const QStringList &args, bool *ok = nullptr) const;

    // Like runSvnSync, but without UTF-8 decoding — for output that must be
    // preserved byte for byte (patch files).
    QByteArray runSvnSyncRaw(const QStringList &args, bool *ok = nullptr) const;

    // Runs svn asynchronously.
    void runSvnAsync(const QStringList &args);

    // XML parser helpers
    QList<SvnStatusEntry>  parseStatusXml(const QString &xml) const;
    QList<SvnListEntry>    parseListXml(const QString &xml) const;
    SvnInfo                parseInfoXml(const QString &xml) const;
    QList<SvnLogEntry>     parseLogXml(const QString &xml) const;
    QMap<QString, QString> parsePropListXml(const QString &xml) const;
    QList<SvnBlameEntry>   parseBlameXml(const QString &xml) const;

    static SvnFileStatus charToStatus(QChar c);

    QString     m_svnBinary;
    int         m_syncTimeoutMs = 30000;
    QProcess   *m_asyncProcess  = nullptr;
    QStringList m_asyncOutputLines;
};
