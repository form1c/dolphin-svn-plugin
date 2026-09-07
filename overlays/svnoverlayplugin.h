#pragma once

#include <KOverlayIconPlugin>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QHash>
#include <QSet>

#include "svnmanager.h"

class SvnOverlayPlugin : public KOverlayIconPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.overlayicon.svnoverlayplugin")

public:
    SvnOverlayPlugin();

    QStringList getOverlays(const QUrl &url) override;

private Q_SLOTS:
    void onSvnDirChanged(const QString &svnDir);

private:
    struct CacheEntry {
        SvnFileStatus status = SvnFileStatus::Normal;
        qint64 timestampMs = 0;
    };

    QHash<QString, SvnFileStatus> queryDirectoryStatus(const QString &dirPath) const;
    void refreshDirectory(const QString &dirPath);

    static QString overlayForStatus(SvnFileStatus status);
    static QString findSvnMetaDir(const QString &startDir);

    SvnManager m_svnManager;
    QHash<QString, CacheEntry> m_statusCache;
    QSet<QString> m_pendingDirs;
    QElapsedTimer m_clock;
    QFileSystemWatcher m_svnDirWatcher;
    QHash<QString, QString> m_svnDirToWcRoot;
    int m_lastRefreshToken = -1;

    // Settings (TTL + refresh token) are re-read at most once per second to
    // avoid a QSettings file stat per visible file.
    qint64 m_lastSettingsReadMs = -1;
    qint64 m_cachedTtlMs = 15000;
};
