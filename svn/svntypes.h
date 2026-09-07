#pragma once

#include <QString>
#include <QList>
#include <QPair>

// SVN status of a single file or folder
enum class SvnFileStatus {
    Unknown,
    Normal,      // unchanged
    Modified,    // M – locally modified
    Added,       // A – scheduled for addition
    Deleted,     // D – scheduled for deletion
    Replaced,    // R – replaced
    Conflicted,  // C – merge conflict
    Missing,     // ! – in SVN, but not on the filesystem
    Unversioned, // ? – not under SVN control
    Ignored,     // I – ignored
    External,    // X – external item
    Incomplete,  // ~ – broken entry
};

// Result of 'svn status'
struct SvnStatusEntry {
    QString path;
    SvnFileStatus textStatus  = SvnFileStatus::Unknown;
    SvnFileStatus propStatus  = SvnFileStatus::Unknown;
    QString revision;
    QString lastCommitRevision;
    QString lastCommitAuthor;
    bool isLocked = false;
    QString lockOwner;
    QString lockComment;
    bool treeConflicted = false;  // tree conflict (e.g. edit vs. delete)

    // Only filled when status() was called with showUpdates=true
    // ('svn status -u'): the state in the repository (<repos-status>), if it
    // differs from the local state. Default Unknown = "no difference to HEAD"
    // (same convention as propStatus).
    SvnFileStatus remoteTextStatus = SvnFileStatus::Unknown;
    SvnFileStatus remotePropStatus = SvnFileStatus::Unknown;
};

// Result of 'svn info'
struct SvnInfo {
    QString path;
    QString url;
    QString repositoryRoot;
    QString repositoryUuid;
    QString revision;
    QString nodeKind;   // "file" or "dir"
    QString schedule;   // "normal", "add", "delete"
    QString lastChangedRevision;
    QString lastChangedAuthor;
    QString lastChangedDate;
    QString wcRootPath;
    bool valid = false;
};

// A single entry from 'svn log'
struct SvnLogEntry {
    QString revision;
    QString author;
    QString date;
    QString message;
    QList<QPair<QString, QString>> changedPaths; // <path, action>
    // Nesting depth with 'svn log -g': 0 = a normal revision,
    // >0 = reached through a merge into the revision above it.
    int mergeDepth = 0;
};

// Options for SvnManager::log().
struct SvnLogOptions {
    bool stopOnCopy    = false;  // --stop-on-copy
    bool includeMerged = false;  // -g / --use-merge-history
};

// Options for SvnManager::mergeRangeAsync/mergeTreesAsync
// (the TortoiseSVN merge options).
struct SvnMergeOptions {
    QString depth;               // empty = working copy depth (no --depth)
    bool ignoreAncestry = false; // --ignore-ancestry
    bool force          = false; // --force
    bool recordOnly     = false; // --record-only
    QString whitespace;          // "" | "-b" (changes) | "-w" (all)
    bool ignoreEol      = false; // --ignore-eol-style
    bool dryRun         = false; // --dry-run
};

// Result of SvnManager::buildMergeMessage(): the comment appendix for the merge
// commit template. The i18n header line and the "(and N more)" line are built
// around it by the GUI layer (KLocalizedString does not belong in the backend).
struct SvnMergeMessageParts {
    QString commentsAppendix; // "" or "\n\n---\nrN (author): message" blocks
    int     overflowCount = 0; // revisions beyond the cap (0 = none)
};

// A single entry from 'svn list --xml' (repository browser)
struct SvnListEntry {
    QString name;          // file or folder name
    bool    isDir = false; // kind="dir"
    qint64  size = 0;      // files only
    QString revision;      // last commit revision
    QString author;
    QString date;          // ISO-8601
    QString lockOwner;     // empty = not locked
};

// An annotated line from 'svn annotate --xml'
struct SvnBlameEntry {
    int     lineNumber = 0;
    QString revision;
    QString author;
    QString date;    // ISO-8601, e.g. "2024-01-15T10:30:00.000000Z"
    QString text;    // line content (without trailing newline)
};

// Result of an asynchronous SVN operation
struct SvnOperationResult {
    bool success = false;
    QString newRevision;   // on commit: the new revision number
    QString errorMessage;
    QStringList output;    // line-by-line output from svn
};
