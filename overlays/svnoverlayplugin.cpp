#include "svnoverlayplugin.h"

#include "svnsettings.h"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QRegularExpression>
#include <QUrl>
#include <QtConcurrentRun>

static qint64 cacheTtlMs()
{
    return static_cast<qint64>(SvnSettings::overlayTtlSeconds()) * 1000;
}

SvnOverlayPlugin::SvnOverlayPlugin()
    : KOverlayIconPlugin()
{
    m_clock.start();
    connect(&m_svnDirWatcher, &QFileSystemWatcher::directoryChanged,
            this, &SvnOverlayPlugin::onSvnDirChanged);
}

QString SvnOverlayPlugin::overlayForStatus(SvnFileStatus status)
{
    switch (status) {
    case SvnFileStatus::Modified:    return QStringLiteral("vcs-locally-modified");
    case SvnFileStatus::Added:       return QStringLiteral("vcs-added");
    case SvnFileStatus::Deleted:     return QStringLiteral("vcs-removed");
    case SvnFileStatus::Replaced:    return QStringLiteral("vcs-locally-modified");
    case SvnFileStatus::Conflicted:  return QStringLiteral("vcs-conflicting");
    case SvnFileStatus::Missing:     return QStringLiteral("vcs-removed");
    case SvnFileStatus::Incomplete:
    case SvnFileStatus::External:    return QStringLiteral("vcs-update-required");
    case SvnFileStatus::Normal:      return QStringLiteral("vcs-normal");
    case SvnFileStatus::Unversioned:
    case SvnFileStatus::Ignored:
    case SvnFileStatus::Unknown:
    default:                         return QString();
    }
}

QStringList SvnOverlayPlugin::getOverlays(const QUrl &url)
{
    if (!url.isLocalFile()) {
        return {};
    }

    const QString path = url.toLocalFile();
    const QFileInfo fileInfo(path);

    // Never put overlays on internal SVN metadata.
    if (fileInfo.fileName() == QStringLiteral(".svn")) {
        return {};
    }

    const QString dirPath = fileInfo.isDir() ? path : fileInfo.absolutePath();

    // Skip .svn directories in parent paths (e.g. when Dolphin shows the
    // content of .svn itself).
    if (dirPath.contains(QStringLiteral("/.svn"))) {
        return {};
    }

    // Re-read settings (refresh token + TTL) at most once per second —
    // getOverlays() runs per visible file and each QSettings costs a file stat.
    const qint64 nowMs = m_clock.elapsed();
    if (m_lastSettingsReadMs < 0 || nowMs - m_lastSettingsReadMs >= 1000) {
        m_lastSettingsReadMs = nowMs;
        m_cachedTtlMs = cacheTtlMs();

        // Check if the user triggered a manual refresh (token written by action plugin).
        const int currentToken = SvnSettings::overlayRefreshToken();
        if (currentToken != m_lastRefreshToken) {
            m_lastRefreshToken = currentToken;
            // Invalidate entire cache and notify Dolphin for every cached path.
            for (auto it2 = m_statusCache.begin(); it2 != m_statusCache.end(); ++it2)
                Q_EMIT overlaysChanged(QUrl::fromLocalFile(it2.key()), {});
            m_statusCache.clear();
        }
    }

    const auto it = m_statusCache.constFind(path);
    const bool haveCachedEntry = (it != m_statusCache.constEnd());
    const bool cacheExpired = !haveCachedEntry
        || (m_clock.elapsed() - it->timestampMs) > m_cachedTtlMs;

    if (cacheExpired && !m_pendingDirs.contains(dirPath)) {
        refreshDirectory(dirPath);
    }

    if (!haveCachedEntry) {
        return {};
    }

    const QString overlay = overlayForStatus(it->status);
    return overlay.isEmpty() ? QStringList() : QStringList{overlay};
}

QHash<QString, SvnFileStatus> SvnOverlayPlugin::queryDirectoryStatus(const QString &dirPath) const
{
    QHash<QString, SvnFileStatus> result;

    // svn info fails for unversioned paths (exit != 0), even when they lie
    // physically inside a working copy. That is more reliable than the exit
    // code of 'svn status', which returns 0 for W200009 warnings.
    if (!m_svnManager.info(dirPath).valid) {
        return result;
    }

    // includeIgnored: svn status hides svn:ignore'd paths entirely by
    // default (no entry, not even item="ignored") — without this, an
    // ignored file has no status entry and falls through to the "no entry
    // = unchanged versioned" fallback below, wrongly getting vcs-normal's
    // green checkmark instead of no overlay at all.
    const auto entries = m_svnManager.status(dirPath, false, nullptr, false,
                                             /*includeIgnored=*/true);
    QSet<QString> known;
    for (const SvnStatusEntry &entry : entries) {
        const QString absPath = QFileInfo(entry.path).absoluteFilePath();
        // Promote Normal+propModified to Modified so the overlay reflects any pending change.
        const SvnFileStatus effective =
            (entry.textStatus == SvnFileStatus::Normal
             && entry.propStatus == SvnFileStatus::Modified)
            ? SvnFileStatus::Modified
            : entry.textStatus;
        result.insert(absPath, effective);
        known.insert(absPath);
    }

    // Files without an svn-status entry are unchanged versioned files (Normal).
    // Safe, because svn info above confirmed the versioning.
    // Exception: conflict helpers (.mine/.rNNN) — SvnManager::status() filters
    // those out; they are unversioned and must not get an overlay.
    static const QRegularExpression rRevSuffix(QStringLiteral("\\.r\\d+$"));
    const auto isConflictHelper = [&result](const QString &absPath) {
        if (absPath.endsWith(QLatin1String(".mine")))
            return result.value(absPath.chopped(5), SvnFileStatus::Normal)
                   == SvnFileStatus::Conflicted;
        if (rRevSuffix.match(absPath).hasMatch()) {
            const int dot = absPath.lastIndexOf(QLatin1Char('.'));
            return result.value(absPath.left(dot), SvnFileStatus::Normal)
                   == SvnFileStatus::Conflicted;
        }
        return false;
    };

    const QDir dir(dirPath);
    const QStringList children = dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QString &name : children) {
        if (name == QStringLiteral(".svn")) {
            continue;
        }
        const QString absPath = dir.absoluteFilePath(name);
        if (!known.contains(absPath) && !isConflictHelper(absPath)) {
            result.insert(absPath, SvnFileStatus::Normal);
        }
    }

    return result;
}

QString SvnOverlayPlugin::findSvnMetaDir(const QString &startDir)
{
    QDir dir(startDir);
    while (true) {
        const QString candidate = dir.filePath(QStringLiteral(".svn"));
        if (QDir(candidate).exists())
            return candidate;
        const QString current = dir.absolutePath();
        if (!dir.cdUp() || dir.absolutePath() == current)
            return {};
    }
}

void SvnOverlayPlugin::onSvnDirChanged(const QString &svnDir)
{
    // Re-add in case the platform removed it from the watcher after the event.
    if (!m_svnDirWatcher.directories().contains(svnDir))
        m_svnDirWatcher.addPath(svnDir);

    const QString wcRoot = m_svnDirToWcRoot.value(svnDir);
    if (wcRoot.isEmpty())
        return;

    // Invalidate every cached entry inside this working copy so the next
    // getOverlays() call triggers a fresh svn status query.
    const QString prefix = wcRoot + QLatin1Char('/');
    for (auto it = m_statusCache.begin(); it != m_statusCache.end(); ) {
        const QString &key = it.key();
        if (key == wcRoot || key.startsWith(prefix)) {
            Q_EMIT overlaysChanged(QUrl::fromLocalFile(key), {});
            it = m_statusCache.erase(it);
        } else {
            ++it;
        }
    }
}

void SvnOverlayPlugin::refreshDirectory(const QString &dirPath)
{
    if (m_pendingDirs.contains(dirPath) || !m_svnManager.isSvnWorkingCopy(dirPath)) {
        return;
    }
    m_pendingDirs.insert(dirPath);

    // Register the .svn meta-directory for this working copy so that external
    // SVN write operations (add, revert, commit…) trigger cache invalidation.
    const QString svnDir = findSvnMetaDir(dirPath);
    if (!svnDir.isEmpty() && !m_svnDirWatcher.directories().contains(svnDir)) {
        m_svnDirWatcher.addPath(svnDir);
        m_svnDirToWcRoot.insert(svnDir, QFileInfo(svnDir).absolutePath());
    }

    auto *watcher = new QFutureWatcher<QHash<QString, SvnFileStatus>>(this);
    connect(watcher, &QFutureWatcher<QHash<QString, SvnFileStatus>>::finished, this, [this, dirPath, watcher]() {
        const auto results = watcher->result();
        watcher->deleteLater();
        m_pendingDirs.remove(dirPath);

        const qint64 now = m_clock.elapsed();
        for (auto resultIt = results.constBegin(); resultIt != results.constEnd(); ++resultIt) {
            m_statusCache.insert(resultIt.key(), {resultIt.value(), now});

            const QString overlay = overlayForStatus(resultIt.value());
            Q_EMIT overlaysChanged(QUrl::fromLocalFile(resultIt.key()),
                                    overlay.isEmpty() ? QStringList() : QStringList{overlay});
        }
    });

    watcher->setFuture(QtConcurrent::run([this, dirPath]() {
        return queryDirectoryStatus(dirPath);
    }));
}
