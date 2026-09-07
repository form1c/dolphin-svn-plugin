#pragma once

#include <QSettings>
#include <QString>

// Thin accessor for plugin-wide settings stored in QSettings.
// All functions create a temporary QSettings object – cheap since the OS
// caches the backing file.  Callers do NOT need to include QSettings.
namespace SvnSettings {

inline QSettings open()
{
    return QSettings(QStringLiteral("DolphinSvnPlugin"), QStringLiteral("Settings"));
}

inline QString svnBinary()
{
    return open().value(QStringLiteral("svnBinary"),
                        QStringLiteral("/usr/bin/svn")).toString();
}

inline QString preferredDiffTool()
{
    return open().value(QStringLiteral("diffTool"), QString()).toString();
}

inline void setSvnBinary(const QString &path)
{
    auto s = open();
    s.setValue(QStringLiteral("svnBinary"), path);
}

inline void setPreferredDiffTool(const QString &path)
{
    auto s = open();
    s.setValue(QStringLiteral("diffTool"), path);
}

// "Keep locks" commit option (svn commit --no-unlock), persisted across
// commits like the other commit-dialog checkboxes.
inline bool keepLocksOnCommit()
{
    return open().value(QStringLiteral("keepLocksOnCommit"), false).toBool();
}

inline void setKeepLocksOnCommit(bool keep)
{
    auto s = open();
    s.setValue(QStringLiteral("keepLocksOnCommit"), keep);
}

inline bool hideUnversioned()
{
    return open().value(QStringLiteral("hideUnversioned"), false).toBool();
}

inline void setHideUnversioned(bool hide)
{
    auto s = open();
    s.setValue(QStringLiteral("hideUnversioned"), hide);
}

inline bool showUnversioned()
{
    return open().value(QStringLiteral("showUnversioned"), false).toBool();
}

inline void setShowUnversioned(bool show)
{
    auto s = open();
    s.setValue(QStringLiteral("showUnversioned"), show);
}

// SVN process timeout for synchronous operations (seconds, default 30).
inline int processSyncTimeout()
{
    return open().value(QStringLiteral("processSyncTimeout"), 30).toInt();
}

inline void setProcessSyncTimeout(int secs)
{
    auto s = open();
    s.setValue(QStringLiteral("processSyncTimeout"), secs);
}

// Directory for temporary diff files. Empty string = use system temp dir.
inline QString tempDirectory()
{
    return open().value(QStringLiteral("tempDirectory")).toString();
}

inline void setTempDirectory(const QString &path)
{
    auto s = open();
    s.setValue(QStringLiteral("tempDirectory"), path);
}

// --- Merge commit template (inspired by tsvn:mergelogtemplate) ---
// After a successful merge, a suggested commit message is stored per WC root;
// the commit dialog offers it. The key is Base64, since path slashes would
// otherwise create QSettings groups.

inline QString pendingMergeMessageKey(const QString &wcRoot)
{
    return QStringLiteral("pendingMergeMsg/")
        + QString::fromLatin1(wcRoot.toUtf8().toBase64(
              QByteArray::Base64UrlEncoding));
}

inline QString pendingMergeMessage(const QString &wcRoot)
{
    return open().value(pendingMergeMessageKey(wcRoot)).toString();
}

inline void setPendingMergeMessage(const QString &wcRoot, const QString &msg)
{
    auto s = open();
    s.setValue(pendingMergeMessageKey(wcRoot), msg);
}

inline void clearPendingMergeMessage(const QString &wcRoot)
{
    auto s = open();
    s.remove(pendingMergeMessageKey(wcRoot));
}

// --- Log view options (shared by the log, update and pick dialogs) ---

inline bool logStopOnCopy()
{
    return open().value(QStringLiteral("logStopOnCopy"), false).toBool();
}

inline void setLogStopOnCopy(bool on)
{
    auto s = open();
    s.setValue(QStringLiteral("logStopOnCopy"), on);
}

inline bool logIncludeMerged()
{
    return open().value(QStringLiteral("logIncludeMerged"), false).toBool();
}

inline void setLogIncludeMerged(bool on)
{
    auto s = open();
    s.setValue(QStringLiteral("logIncludeMerged"), on);
}

inline bool logOnlyAffectedPaths()
{
    return open().value(QStringLiteral("logOnlyAffectedPaths"), false).toBool();
}

inline void setLogOnlyAffectedPaths(bool on)
{
    auto s = open();
    s.setValue(QStringLiteral("logOnlyAffectedPaths"), on);
}

// TTL for SVN status cache in the overlay plugin (seconds, default 15).
inline int overlayTtlSeconds()
{
    return open().value(QStringLiteral("overlayTtlSeconds"), 15).toInt();
}

inline void setOverlayTtlSeconds(int secs)
{
    auto s = open();
    s.setValue(QStringLiteral("overlayTtlSeconds"), secs);
}

// Monotonically-increasing token that the overlay plugin checks on every
// getOverlays() call. Incrementing it triggers a full cache clear.
inline int overlayRefreshToken()
{
    return open().value(QStringLiteral("overlayRefreshToken"), 0).toInt();
}

inline void incrementOverlayRefreshToken()
{
    auto s = open();
    s.setValue(QStringLiteral("overlayRefreshToken"),
               s.value(QStringLiteral("overlayRefreshToken"), 0).toInt() + 1);
}

} // namespace SvnSettings
