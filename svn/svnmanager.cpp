#include "svnmanager.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

// Builds the environment for every svn call.
//
// svn interprets byte strings it receives (commit and lock messages, property
// values, path arguments) according to the locale's character set, and it
// converts them to UTF-8 internally. Forcing LC_ALL=C would make that character
// set ASCII, so any non-ASCII text (öäüß, accents, non-Latin scripts) fails the
// commit with "Can't convert string from native encoding to 'UTF-8'". We
// therefore keep a UTF-8 character set (LC_CTYPE) and force only the *message*
// category (LC_MESSAGES) to C, so the diagnostic text we parse (e.g. the English
// "Committed revision N.") stays locale-independent. The machine-readable data
// we rely on elsewhere comes from --xml and is not locale-dependent anyway.
static QProcessEnvironment cLocaleEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // Pick a UTF-8 character set: keep the user's if it already is UTF-8,
    // otherwise fall back to the portable C.UTF-8.
    QString ctype = env.value(QStringLiteral("LC_ALL"));
    if (ctype.isEmpty()) ctype = env.value(QStringLiteral("LC_CTYPE"));
    if (ctype.isEmpty()) ctype = env.value(QStringLiteral("LANG"));
    const bool ctypeIsUtf8 =
        ctype.contains(QLatin1String("UTF-8"), Qt::CaseInsensitive)
        || ctype.contains(QLatin1String("UTF8"), Qt::CaseInsensitive);
    if (!ctypeIsUtf8)
        ctype = QStringLiteral("C.UTF-8");

    // LC_ALL and LANGUAGE would override the categories set below, so drop them.
    env.remove(QStringLiteral("LC_ALL"));
    env.remove(QStringLiteral("LANGUAGE"));
    env.insert(QStringLiteral("LC_CTYPE"), ctype);
    env.insert(QStringLiteral("LC_MESSAGES"), QStringLiteral("C"));
    return env;
}

static SvnFileStatus statusFromString(const QString &s)
{
    if (s == QStringLiteral("normal"))      return SvnFileStatus::Normal;
    if (s == QStringLiteral("modified"))    return SvnFileStatus::Modified;
    if (s == QStringLiteral("added"))       return SvnFileStatus::Added;
    if (s == QStringLiteral("deleted"))     return SvnFileStatus::Deleted;
    if (s == QStringLiteral("replaced"))    return SvnFileStatus::Replaced;
    if (s == QStringLiteral("conflicted"))  return SvnFileStatus::Conflicted;
    if (s == QStringLiteral("missing"))     return SvnFileStatus::Missing;
    if (s == QStringLiteral("unversioned")) return SvnFileStatus::Unversioned;
    if (s == QStringLiteral("ignored"))     return SvnFileStatus::Ignored;
    if (s == QStringLiteral("external"))    return SvnFileStatus::External;
    if (s == QStringLiteral("incomplete"))  return SvnFileStatus::Incomplete;
    return SvnFileStatus::Unknown;
}

SvnManager::SvnManager(QObject *parent)
    : QObject(parent)
    , m_svnBinary(QStringLiteral("/usr/bin/svn"))
{
}

SvnManager::~SvnManager()
{
    cancelAsync();
}

// --- Configuration ---

void SvnManager::setSvnBinary(const QString &path)
{
    m_svnBinary = path;
}

QString SvnManager::svnBinary() const
{
    return m_svnBinary;
}

void SvnManager::setProcessSyncTimeout(int ms)
{
    m_syncTimeoutMs = ms > 0 ? ms : 30000;
}

int SvnManager::processSyncTimeout() const
{
    return m_syncTimeoutMs;
}

// --- Synchronous queries ---

bool SvnManager::isSvnWorkingCopy(const QString &path) const
{
    QDir dir(QFileInfo(path).isDir() ? path : QFileInfo(path).absolutePath());
    while (true) {
        if (dir.exists(QStringLiteral(".svn"))) {
            return true;
        }
        const QString current = dir.absolutePath();
        if (!dir.cdUp() || dir.absolutePath() == current) {
            break;
        }
    }
    return false;
}

QList<SvnStatusEntry> SvnManager::status(const QString &path, bool recursive,
                                         bool *ok, bool showUpdates,
                                         bool includeIgnored) const
{
    QStringList args = {
        QStringLiteral("status"),
        QStringLiteral("--xml"),
        QStringLiteral("--depth"),
        recursive ? QStringLiteral("infinity") : QStringLiteral("immediates"),
    };
    if (showUpdates)
        args << QStringLiteral("--show-updates");
    if (includeIgnored)
        args << QStringLiteral("--no-ignore");
    args << path;

    bool runOk = false;
    const QString xml = runSvnSync(args, &runOk);
    if (ok) *ok = runOk;
    if (!runOk || xml.isEmpty()) {
        return {};
    }

    QList<SvnStatusEntry> entries = parseStatusXml(xml);

    // SVN creates unversioned conflict-helper files alongside a conflicted entry:
    //   file.txt.mine   — local version before the merge
    //   file.txt.rNN    — old and new repository versions (e.g. .r37, .r39)
    // They appear as separate "unversioned" entries with no special XML marker.
    // Build a set of conflicted absolute paths, then drop helpers that match
    // the patterns "<conflicted-path>.mine" or "<conflicted-path>.rNNN".
    QSet<QString> conflicted;
    for (const SvnStatusEntry &e : std::as_const(entries)) {
        if (e.textStatus == SvnFileStatus::Conflicted)
            conflicted.insert(QFileInfo(e.path).absoluteFilePath());
    }
    if (!conflicted.isEmpty()) {
        static const QRegularExpression rRevSuffix(QStringLiteral("\\.r\\d+$"));
        entries.erase(
            std::remove_if(entries.begin(), entries.end(),
                [&](const SvnStatusEntry &e) {
                    if (e.textStatus != SvnFileStatus::Unversioned) return false;
                    const QString abs = QFileInfo(e.path).absoluteFilePath();
                    // pattern: file.txt.mine
                    if (abs.endsWith(QLatin1String(".mine"))) {
                        if (conflicted.contains(abs.chopped(5)))
                            return true;
                    }
                    // pattern: file.txt.rNNN
                    if (rRevSuffix.match(abs).hasMatch()) {
                        const int dot = abs.lastIndexOf(QLatin1Char('.'));
                        if (conflicted.contains(abs.left(dot)))
                            return true;
                    }
                    return false;
                }),
            entries.end());
    }

    return entries;
}

SvnInfo SvnManager::info(const QString &path) const
{
    bool ok = false;
    const QString xml = runSvnSync({QStringLiteral("info"), QStringLiteral("--xml"), path}, &ok);
    if (!ok || xml.isEmpty()) {
        return {};
    }
    return parseInfoXml(xml);
}

bool SvnManager::conflictFilesSync(const QString &path, QString *baseFile,
                                   QString *mineFile, QString *theirsFile) const
{
    bool ok = false;
    const QString xml = runSvnSync(
        {QStringLiteral("info"), QStringLiteral("--xml"), path}, &ok);
    if (!ok || xml.isEmpty())
        return false;

    QString prevBase, prevWc, curBase;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement())
            continue;
        if (reader.name() == QLatin1String("prev-base-file"))
            prevBase = reader.readElementText();
        else if (reader.name() == QLatin1String("prev-wc-file"))
            prevWc = reader.readElementText();
        else if (reader.name() == QLatin1String("cur-base-file"))
            curBase = reader.readElementText();
    }
    if (prevBase.isEmpty() || curBase.isEmpty())
        return false; // no text conflict

    // svn usually returns the helper files relative to the file's directory.
    const QDir dir = QFileInfo(path).absoluteDir();
    const auto absolute = [&dir](const QString &f) {
        return QFileInfo(f).isAbsolute() ? f : dir.filePath(f);
    };
    *baseFile   = absolute(prevBase);
    *mineFile   = prevWc.isEmpty() ? QFileInfo(path).absoluteFilePath()
                                   : absolute(prevWc);
    *theirsFile = absolute(curBase);
    return QFileInfo::exists(*baseFile) && QFileInfo::exists(*mineFile)
           && QFileInfo::exists(*theirsFile);
}

QString SvnManager::treeConflictDescriptionSync(const QString &path) const
{
    bool ok = false;
    const QString xml = runSvnSync(
        {QStringLiteral("info"), QStringLiteral("--xml"), path}, &ok);
    if (!ok || xml.isEmpty())
        return {};

    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()
            && reader.name() == QLatin1String("tree-conflict")) {
            const auto attrs = reader.attributes();
            const QString operation =
                attrs.value(QStringLiteral("operation")).toString();
            const QString action =
                attrs.value(QStringLiteral("action")).toString();
            const QString reason =
                attrs.value(QStringLiteral("reason")).toString();
            return QStringLiteral("%1: incoming %2 vs. local %3")
                .arg(operation, action, reason);
        }
    }
    return {};
}

QSet<long long> SvnManager::mergedRevisionsSync(const QString &source,
                                                const QString &wcPath,
                                                const QString &showRevs,
                                                bool *ok) const
{
    bool cmdOk = false;
    const QString out = runSvnSync(
        {QStringLiteral("mergeinfo"), QStringLiteral("--show-revs"),
         showRevs, source, wcPath}, &cmdOk);
    if (ok)
        *ok = cmdOk;
    QSet<long long> revs;
    if (!cmdOk)
        return revs;
    const QStringList lines = out.split(QLatin1Char('\n'),
                                        Qt::SkipEmptyParts);
    for (QString line : lines) {
        line = line.trimmed();
        if (line.startsWith(QLatin1Char('r')))
            line.remove(0, 1);
        if (line.endsWith(QLatin1Char('*'))) // non-inheritable Markierung
            line.chop(1);
        bool numOk = false;
        const long long rev = line.toLongLong(&numOk);
        if (numOk && rev > 0)
            revs.insert(rev);
    }
    return revs;
}

SvnMergeMessageParts SvnManager::buildMergeMessage(const QString &url,
                                                   const QString &ranges,
                                                   const QString &wcPath) const
{
    SvnMergeMessageParts parts;

    // Determine the revisions to merge: parse explicit ranges,
    // otherwise query the still-open (eligible) ones.
    QSet<long long> revSet;
    if (ranges.isEmpty()) {
        revSet = mergedRevisionsSync(url, wcPath, QStringLiteral("eligible"));
    } else {
        const QStringList rangeParts = QString(ranges)
            .remove(QLatin1Char(' '))
            .split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &p : rangeParts) {
            const int dash = p.indexOf(QLatin1Char('-'));
            if (dash > 0) {
                const long long a = p.left(dash).toLongLong();
                const long long b = p.mid(dash + 1).toLongLong();
                for (long long r = qMin(a, b); r <= qMax(a, b); ++r)
                    revSet.insert(r);
            } else {
                const long long r = p.toLongLong();
                if (r > 0)
                    revSet.insert(r);
            }
        }
    }
    if (revSet.isEmpty())
        return parts;

    // Fetch the comments with a single log call over the span.
    const auto minMax = std::minmax_element(revSet.cbegin(), revSet.cend());
    const auto logEntries = log(url, 1000,
                                QString::number(*minMax.second),
                                QString::number(*minMax.first));
    // log returns descending → ascending for the template.
    constexpr int kMaxComments = 50;
    int used = 0;
    QStringList comments;
    for (auto it = logEntries.crbegin(); it != logEntries.crend(); ++it) {
        if (!revSet.contains(it->revision.toLongLong()))
            continue;
        if (++used > kMaxComments)
            continue;
        comments << QStringLiteral("r%1 (%2): %3")
                        .arg(it->revision, it->author, it->message.trimmed());
    }
    for (const QString &c : std::as_const(comments)) {
        parts.commentsAppendix += QStringLiteral("\n\n---\n");
        parts.commentsAppendix += c;
    }
    if (used > kMaxComments)
        parts.overflowCount = used - kMaxComments;
    return parts;
}

bool SvnManager::exportSync(const QString &target, const QString &revision,
                            const QString &destDir) const
{
    // Peg revision for URLs, so historical/renamed paths can be resolved.
    const bool isUrl = target.contains(QStringLiteral("://"));
    const QString pegged = isUrl ? target + QLatin1Char('@') + revision
                                 : target;
    bool ok = false;
    runSvnSync({QStringLiteral("export"), QStringLiteral("-q"),
                QStringLiteral("--revision"), revision, pegged, destDir},
               &ok);
    return ok;
}

QList<SvnLogEntry> SvnManager::log(const QString &path,
                                     int limit,
                                     const QString &fromRev,
                                     const QString &toRev,
                                     const SvnLogOptions &options) const
{
    QStringList args = {
        QStringLiteral("log"),
        QStringLiteral("--xml"),
        QStringLiteral("--verbose"),
        QStringLiteral("--limit"),
        QString::number(limit),
    };
    if (options.stopOnCopy)
        args << QStringLiteral("--stop-on-copy");
    if (options.includeMerged)
        args << QStringLiteral("--use-merge-history");

    if (!fromRev.isEmpty() && !toRev.isEmpty()) {
        args << QStringLiteral("--revision")
             << QStringLiteral("%1:%2").arg(fromRev, toRev);
    }
    args << path;

    bool ok = false;
    const QString xml = runSvnSync(args, &ok);
    if (!ok || xml.isEmpty()) {
        return {};
    }
    return parseLogXml(xml);
}

QByteArray SvnManager::cat(const QString &path, const QString &revision) const
{
    QProcess process;
    process.setProcessEnvironment(cLocaleEnvironment());
    process.start(m_svnBinary, {
        QStringLiteral("cat"),
        QStringLiteral("--revision"),
        revision,
        path
    });

    if (!process.waitForFinished(m_syncTimeoutMs)) {
        process.kill();
        return {};
    }
    if (process.exitCode() != 0) {
        return {};
    }
    return process.readAllStandardOutput();
}

// --- Asynchronous operations ---

void SvnManager::updateAsync(const QString &path,
                              const QString &revision,
                              const QString &depth)
{
    runSvnAsync({
        QStringLiteral("update"),
        QStringLiteral("--revision"), revision,
        QStringLiteral("--depth"), depth,
        QStringLiteral("--accept"), QStringLiteral("postpone"),
        path
    });
}

void SvnManager::commitAsync(const QStringList &paths, const QString &message,
                              bool keepLocks)
{
    QStringList args = {
        QStringLiteral("commit"),
        QStringLiteral("--message"), message,
        // 'svn commit <dir>' commits the whole subtree recursively. The commit
        // dialog instead passes the exact set of items the user checked (each
        // changed folder AND file), so '--depth empty' is required: it commits
        // precisely the listed paths and never pulls in an unchecked sibling
        // that happens to live under a listed folder. Deleting a folder still
        // removes its subtree in the repository, that is inherent to the delete.
        QStringLiteral("--depth"), QStringLiteral("empty"),
    };
    if (keepLocks)
        args << QStringLiteral("--no-unlock");
    args << paths;
    runSvnAsync(args);
}

void SvnManager::revertAsync(const QStringList &paths, const QString &depth)
{
    QStringList args = {QStringLiteral("revert"), QStringLiteral("--depth"), depth};
    args << paths;
    runSvnAsync(args);
}

void SvnManager::addAsync(const QStringList &paths, const QString &depth)
{
    QStringList args = {
        QStringLiteral("add"),
        QStringLiteral("--depth"), depth,
        QStringLiteral("--force")
    };
    args << paths;
    runSvnAsync(args);
}

bool SvnManager::addSync(const QStringList &paths, const QString &depth)
{
    QStringList args = {
        QStringLiteral("add"),
        QStringLiteral("--depth"), depth,
        QStringLiteral("--force")
    };
    args << paths;
    bool ok = false;
    runSvnSync(args, &ok);
    return ok;
}

void SvnManager::moveAsync(const QString &srcPath, const QString &dstPath)
{
    runSvnAsync({QStringLiteral("move"), srcPath, dstPath});
}

void SvnManager::lockAsync(const QStringList &paths, const QString &message, bool force)
{
    QStringList args = {QStringLiteral("lock"),
                        QStringLiteral("--message"), message};
    if (force)
        args << QStringLiteral("--force");
    args << paths;
    runSvnAsync(args);
}

void SvnManager::unlockAsync(const QStringList &paths)
{
    QStringList args = {QStringLiteral("unlock")};
    args << paths;
    runSvnAsync(args);
}

void SvnManager::exportAsync(const QString &srcPath, const QString &dstPath,
                              const QString &revision, bool force)
{
    QStringList args = {QStringLiteral("export"),
                        QStringLiteral("--revision"), revision};
    if (force)
        args << QStringLiteral("--force");
    args << srcPath << dstPath;
    runSvnAsync(args);
}

void SvnManager::cleanupAsync(const QString &path)
{
    runSvnAsync({QStringLiteral("cleanup"), path});
}

void SvnManager::resolveAsync(const QStringList &paths, const QString &accept)
{
    QStringList args = {QStringLiteral("resolve"),
                        QStringLiteral("--accept"), accept};
    args << paths;
    runSvnAsync(args);
}

void SvnManager::copyAsync(const QString &srcUrl, const QString &dstUrl,
                            const QString &message, const QString &revision)
{
    runSvnAsync({
        QStringLiteral("copy"),
        srcUrl,
        dstUrl,
        QStringLiteral("--revision"), revision,
        QStringLiteral("--message"), message
    });
}

void SvnManager::switchAsync(const QString &path, const QString &url,
                              const QString &revision, const QString &depth)
{
    runSvnAsync({
        QStringLiteral("switch"),
        url,
        path,
        QStringLiteral("--revision"), revision,
        QStringLiteral("--depth"), depth
    });
}

void SvnManager::relocateAsync(const QString &wcPath, const QString &fromUrl,
                                const QString &toUrl)
{
    runSvnAsync({QStringLiteral("relocate"), fromUrl, toUrl, wcPath});
}

void SvnManager::applyPatchAsync(const QString &wcPath, const QString &patchFile,
                                  bool dryRun, bool reverse, int strip)
{
    QStringList args = {QStringLiteral("patch")};
    if (dryRun)  args << QStringLiteral("--dry-run");
    if (reverse) args << QStringLiteral("--reverse-diff");
    if (strip > 0)
        args << QStringLiteral("--strip") << QString::number(strip);
    args << patchFile << wcPath;
    runSvnAsync(args);
}

bool SvnManager::createRepositorySync(const QString &repoDir,
                                      QString *errorMessage) const
{
    // svnadmin usually sits next to the svn binary.
    QString svnadmin = QFileInfo(m_svnBinary).absolutePath()
                       + QStringLiteral("/svnadmin");
    if (!QFileInfo::exists(svnadmin))
        svnadmin = QStringLiteral("svnadmin"); // PATH-Fallback

    QProcess process;
    process.setProcessEnvironment(cLocaleEnvironment());
    process.start(svnadmin, {QStringLiteral("create"), repoDir});
    if (!process.waitForFinished(m_syncTimeoutMs)) {
        process.kill();
        if (errorMessage)
            *errorMessage = QStringLiteral("svnadmin create timed out.");
        return false;
    }
    if (process.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage =
                QString::fromUtf8(process.readAllStandardError()).trimmed();
            if (errorMessage->isEmpty())
                *errorMessage = QStringLiteral("svnadmin create failed.");
        }
        return false;
    }
    return true;
}

void SvnManager::mkdirUrlAsync(const QString &url, const QString &message)
{
    runSvnAsync({QStringLiteral("mkdir"), url,
                 QStringLiteral("--message"), message});
}

void SvnManager::mkdirUrlsAsync(const QStringList &urls, const QString &message)
{
    QStringList args = {QStringLiteral("mkdir")};
    args << urls;
    args << QStringLiteral("--message") << message;
    runSvnAsync(args);
}

void SvnManager::deleteUrlAsync(const QString &url, const QString &message)
{
    runSvnAsync({QStringLiteral("delete"), url,
                 QStringLiteral("--message"), message});
}

void SvnManager::moveUrlAsync(const QString &srcUrl, const QString &dstUrl,
                               const QString &message)
{
    runSvnAsync({QStringLiteral("move"), srcUrl, dstUrl,
                 QStringLiteral("--message"), message});
}

namespace {

// Translates the TortoiseSVN-style merge options into svn arguments.
void appendMergeOptions(QStringList &args, const SvnMergeOptions &options)
{
    if (!options.depth.isEmpty())
        args << QStringLiteral("--depth") << options.depth;
    if (options.ignoreAncestry)
        args << QStringLiteral("--ignore-ancestry");
    if (options.force)
        args << QStringLiteral("--force");
    if (options.recordOnly)
        args << QStringLiteral("--record-only");
    // Diff extensions as ONE joined -x argument (svn parses the string).
    QStringList ext;
    if (!options.whitespace.isEmpty())
        ext << options.whitespace;
    if (options.ignoreEol)
        ext << QStringLiteral("--ignore-eol-style");
    if (!ext.isEmpty())
        args << QStringLiteral("-x") << ext.join(QLatin1Char(' '));
    if (options.dryRun)
        args << QStringLiteral("--dry-run");
}

} // namespace

void SvnManager::mergeRangeAsync(const QString &url,
                                  const QString &revisionRanges,
                                  const QString &wcPath,
                                  const SvnMergeOptions &options)
{
    QStringList args = {QStringLiteral("merge")};
    if (!revisionRanges.trimmed().isEmpty()) {
        args << QStringLiteral("-c")
             << QString(revisionRanges).remove(QLatin1Char(' '));
    }
    args << url << wcPath
         << QStringLiteral("--accept") << QStringLiteral("postpone");
    appendMergeOptions(args, options);
    runSvnAsync(args);
}

void SvnManager::mergeTreesAsync(const QString &url1, const QString &rev1,
                                  const QString &url2, const QString &rev2,
                                  const QString &wcPath,
                                  const SvnMergeOptions &options)
{
    QStringList args = {
        QStringLiteral("merge"),
        url1 + QLatin1Char('@') + rev1,
        url2 + QLatin1Char('@') + rev2,
        wcPath,
        QStringLiteral("--accept"), QStringLiteral("postpone")
    };
    appendMergeOptions(args, options);
    runSvnAsync(args);
}

void SvnManager::checkoutAsync(const QString &url, const QString &localPath,
                                const QString &revision, const QString &depth)
{
    runSvnAsync({
        QStringLiteral("checkout"),
        url,
        localPath,
        QStringLiteral("--revision"), revision,
        QStringLiteral("--depth"), depth
    });
}

void SvnManager::importAsync(const QString &localDir, const QString &url,
                              const QString &message)
{
    runSvnAsync({QStringLiteral("import"), localDir, url,
                 QStringLiteral("--message"), message});
}

void SvnManager::removeAsync(const QStringList &paths, bool keepLocal, bool force)
{
    QStringList args = {QStringLiteral("delete")};
    if (keepLocal) {
        args << QStringLiteral("--keep-local");
    }
    if (force) {
        args << QStringLiteral("--force");
    }
    args << paths;
    runSvnAsync(args);
}

void SvnManager::cancelAsync()
{
    if (m_asyncProcess && m_asyncProcess->state() != QProcess::NotRunning) {
        m_asyncProcess->kill();
        m_asyncProcess->waitForFinished(3000);
    }
}

bool SvnManager::isBusy() const
{
    return m_asyncProcess && m_asyncProcess->state() != QProcess::NotRunning;
}

// --- Private helper methods ---

QString SvnManager::runSvnSync(const QStringList &args, bool *ok) const
{
    return QString::fromUtf8(runSvnSyncRaw(args, ok));
}

QByteArray SvnManager::runSvnSyncRaw(const QStringList &args, bool *ok) const
{
    QProcess process;
    process.setProcessEnvironment(cLocaleEnvironment());
    process.start(m_svnBinary, args);

    if (!process.waitForFinished(m_syncTimeoutMs)) {
        process.kill();
        if (ok) *ok = false;
        return process.readAllStandardError();
    }

    if (process.exitCode() != 0) {
        if (ok) *ok = false;
        return process.readAllStandardError();
    }

    if (ok) *ok = true;
    return process.readAllStandardOutput();
}

void SvnManager::runSvnAsync(const QStringList &args)
{
    if (isBusy()) {
        // Report the rejection instead of returning silently — otherwise the
        // caller's progress dialog would spin forever.
        SvnOperationResult result;
        result.success = false;
        result.errorMessage =
            QStringLiteral("Another SVN operation is already in progress.");
        Q_EMIT operationFinished(result);
        return;
    }

    if (!m_asyncProcess) {
        m_asyncProcess = new QProcess(this);
        m_asyncProcess->setProcessEnvironment(cLocaleEnvironment());

        connect(m_asyncProcess, &QProcess::readyReadStandardOutput, this, [this]() {
            while (m_asyncProcess->canReadLine()) {
                const QString line =
                    QString::fromUtf8(m_asyncProcess->readLine()).trimmed();
                // Keep every line: the finished handler needs the full output
                // (e.g. "Committed revision N."), not just the unread tail.
                m_asyncOutputLines << line;
                Q_EMIT progressOutput(line);
            }
        });

        connect(m_asyncProcess,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this,
                [this](int exitCode, QProcess::ExitStatus) {
                    SvnOperationResult result;
                    result.success = (exitCode == 0);

                    // Collect any tail output not yet consumed by readLine().
                    const QString tail =
                        QString::fromUtf8(m_asyncProcess->readAllStandardOutput());
                    if (!tail.trimmed().isEmpty())
                        m_asyncOutputLines += tail.split(QLatin1Char('\n'),
                                                          Qt::SkipEmptyParts);
                    const QString out = m_asyncOutputLines.join(QLatin1Char('\n'));
                    const QString err =
                        QString::fromUtf8(m_asyncProcess->readAllStandardError());

                    if (!result.success) {
                        result.errorMessage = err.isEmpty() ? out : err;
                    } else {
                        // Extract the revision number from the commit output
                        // svn prints: "Committed revision 42."
                        static const QRegularExpression revRx(
                            QStringLiteral("Committed revision (\\d+)"));
                        const auto match = revRx.match(out.isEmpty() ? err : out);
                        if (match.hasMatch()) {
                            result.newRevision = match.captured(1);
                        }
                        result.output = m_asyncOutputLines;
                    }
                    Q_EMIT operationFinished(result);
                });
    }

    m_asyncOutputLines.clear();
    m_asyncProcess->start(m_svnBinary, args);
}

// --- XML-Parser ---

QList<SvnStatusEntry> SvnManager::parseStatusXml(const QString &xml) const
{
    QList<SvnStatusEntry> entries;
    QXmlStreamReader reader(xml);

    SvnStatusEntry current;
    bool inEntry = false;
    bool inLock  = false;

    while (!reader.atEnd()) {
        reader.readNext();

        if (reader.isStartElement()) {
            const QStringView name = reader.name();

            if (name == QStringLiteral("entry")) {
                current = SvnStatusEntry{};
                current.path = reader.attributes().value(QStringLiteral("path")).toString();
                inEntry = true;
                inLock  = false;

            } else if (inEntry && name == QStringLiteral("wc-status")) {
                const auto attrs = reader.attributes();
                current.textStatus = statusFromString(
                    attrs.value(QStringLiteral("item")).toString());
                current.propStatus = statusFromString(
                    attrs.value(QStringLiteral("props")).toString());
                current.revision   = attrs.value(QStringLiteral("revision")).toString();
                current.treeConflicted =
                    attrs.value(QStringLiteral("tree-conflicted"))
                        == QStringLiteral("true");

            } else if (inEntry && name == QStringLiteral("commit")) {
                current.lastCommitRevision =
                    reader.attributes().value(QStringLiteral("revision")).toString();

            } else if (inEntry && name == QStringLiteral("author")) {
                current.lastCommitAuthor = reader.readElementText();

            } else if (inEntry && name == QStringLiteral("lock")) {
                current.isLocked = true;
                inLock = true;

            } else if (inEntry && name == QStringLiteral("owner") && inLock) {
                current.lockOwner = reader.readElementText();

            } else if (inEntry && name == QStringLiteral("comment") && inLock) {
                current.lockComment = reader.readElementText();

            } else if (inEntry && name == QStringLiteral("repos-status")) {
                const auto attrs = reader.attributes();
                current.remoteTextStatus = statusFromString(
                    attrs.value(QStringLiteral("item")).toString());
                current.remotePropStatus = statusFromString(
                    attrs.value(QStringLiteral("props")).toString());
            }

        } else if (reader.isEndElement()) {
            const QStringView name = reader.name();
            if (name == QStringLiteral("entry") && inEntry) {
                entries.append(current);
                inEntry = false;
            } else if (name == QStringLiteral("lock")) {
                inLock = false;
            }
        }
    }

    return entries;
}

SvnInfo SvnManager::parseInfoXml(const QString &xml) const
{
    SvnInfo result;
    QXmlStreamReader reader(xml);

    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement()) {
            continue;
        }

        const QStringView name = reader.name();
        const auto attrs       = reader.attributes();

        if (name == QStringLiteral("entry")) {
            result.path     = attrs.value(QStringLiteral("path")).toString();
            result.revision = attrs.value(QStringLiteral("revision")).toString();
            result.nodeKind = attrs.value(QStringLiteral("kind")).toString();
            result.valid    = true;

        } else if (name == QStringLiteral("url")) {
            result.url = reader.readElementText();

        } else if (name == QStringLiteral("root")) {
            result.repositoryRoot = reader.readElementText();

        } else if (name == QStringLiteral("uuid")) {
            result.repositoryUuid = reader.readElementText();

        } else if (name == QStringLiteral("wcroot-abspath")) {
            result.wcRootPath = reader.readElementText();

        } else if (name == QStringLiteral("schedule")) {
            result.schedule = reader.readElementText();

        } else if (name == QStringLiteral("commit")) {
            result.lastChangedRevision = attrs.value(QStringLiteral("revision")).toString();

        } else if (name == QStringLiteral("author")) {
            result.lastChangedAuthor = reader.readElementText();

        } else if (name == QStringLiteral("date")) {
            result.lastChangedDate = reader.readElementText();
        }
    }

    return result;
}

QList<SvnLogEntry> SvnManager::parseLogXml(const QString &xml) const
{
    QList<SvnLogEntry> entries;
    QXmlStreamReader reader(xml);

    // 'svn log -g' returns merged revisions as NESTED <logentry> elements →
    // an index stack instead of flat flags (indices, not pointers, since
    // entries are reallocated as the list grows). Document order:
    // the merge revision before its merged children.
    QList<int> stack;
    bool inPaths = false;

    while (!reader.atEnd()) {
        reader.readNext();

        if (reader.isStartElement()) {
            const QStringView name = reader.name();

            if (name == QStringLiteral("logentry")) {
                SvnLogEntry entry;
                entry.revision = reader.attributes()
                                     .value(QStringLiteral("revision"))
                                     .toString();
                entry.mergeDepth = stack.size();
                entries.append(entry);
                stack.append(entries.size() - 1);

            } else if (!stack.isEmpty()
                       && name == QStringLiteral("author")) {
                entries[stack.last()].author = reader.readElementText();

            } else if (!stack.isEmpty() && name == QStringLiteral("date")) {
                entries[stack.last()].date = reader.readElementText();

            } else if (!stack.isEmpty() && name == QStringLiteral("msg")) {
                entries[stack.last()].message = reader.readElementText();

            } else if (!stack.isEmpty() && name == QStringLiteral("paths")) {
                inPaths = true;

            } else if (inPaths && !stack.isEmpty()
                       && name == QStringLiteral("path")) {
                const QString action =
                    reader.attributes().value(QStringLiteral("action")).toString();
                const QString filePath = reader.readElementText();
                entries[stack.last()].changedPaths.append({filePath, action});
            }

        } else if (reader.isEndElement()) {
            const QStringView name = reader.name();
            if (name == QStringLiteral("logentry") && !stack.isEmpty()) {
                stack.removeLast();
            } else if (name == QStringLiteral("paths")) {
                inPaths = false;
            }
        }
    }

    return entries;
}

QMap<QString, QString> SvnManager::propListSync(const QString &path) const
{
    bool ok = false;
    const QString xml = runSvnSync(
        {QStringLiteral("proplist"), QStringLiteral("--xml"),
         QStringLiteral("--verbose"), path}, &ok);
    if (!ok || xml.isEmpty())
        return {};
    return parsePropListXml(xml);
}

bool SvnManager::propSetSync(const QString &path, const QString &name, const QString &value)
{
    // QProcess does not go through a shell, so newlines in argv are valid on Linux.
    bool ok = false;
    runSvnSync({QStringLiteral("propset"), name, value, path}, &ok);
    return ok;
}

bool SvnManager::propDelSync(const QString &path, const QString &name)
{
    bool ok = false;
    runSvnSync({QStringLiteral("propdel"), name, path}, &ok);
    return ok;
}

bool SvnManager::addToIgnoreSync(const QString &dirPath, const QString &pattern)
{
    if (pattern.isEmpty())
        return false;

    const QMap<QString, QString> props = propListSync(dirPath);
    QStringList lines = props.value(QStringLiteral("svn:ignore"))
                             .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.contains(pattern))
        return true; // already present — nothing to do.

    lines << pattern;
    return propSetSync(dirPath, QStringLiteral("svn:ignore"),
                       lines.join(QLatin1Char('\n')) + QLatin1Char('\n'));
}

QMap<QString, QString> SvnManager::parsePropListXml(const QString &xml) const
{
    QMap<QString, QString> props;
    QXmlStreamReader reader(xml);

    QString currentName;
    QString currentValue;
    bool    inProperty = false;

    while (!reader.atEnd()) {
        reader.readNext();

        if (reader.isStartElement()) {
            if (reader.name() == QStringLiteral("property")) {
                currentName  = reader.attributes().value(QStringLiteral("name")).toString();
                currentValue.clear();
                inProperty   = true;
            }
        } else if (inProperty && reader.isCharacters()) {
            currentValue += reader.text().toString();
        } else if (reader.isEndElement()) {
            if (reader.name() == QStringLiteral("property") && inProperty) {
                props[currentName] = currentValue;
                inProperty = false;
            }
        }
    }

    return props;
}

QList<SvnListEntry> SvnManager::listSync(const QString &url,
                                          const QString &revision,
                                          bool *ok) const
{
    bool runOk = false;
    const QString xml = runSvnSync({
        QStringLiteral("list"), QStringLiteral("--xml"),
        QStringLiteral("--depth"), QStringLiteral("immediates"),
        QStringLiteral("--revision"), revision,
        url
    }, &runOk);
    if (ok) *ok = runOk;
    if (!runOk || xml.isEmpty())
        return {};
    return parseListXml(xml);
}

QList<SvnListEntry> SvnManager::parseListXml(const QString &xml) const
{
    QList<SvnListEntry> entries;
    QXmlStreamReader reader(xml);

    SvnListEntry current;
    bool inEntry = false;
    bool inLock  = false;

    while (!reader.atEnd()) {
        reader.readNext();

        if (reader.isStartElement()) {
            const QStringView name = reader.name();

            if (name == QStringLiteral("entry")) {
                current = SvnListEntry{};
                current.isDir = reader.attributes()
                                    .value(QStringLiteral("kind"))
                                == QStringLiteral("dir");
                inEntry = true;
                inLock  = false;

            } else if (inEntry && name == QStringLiteral("name")) {
                current.name = reader.readElementText();

            } else if (inEntry && name == QStringLiteral("size")) {
                current.size = reader.readElementText().toLongLong();

            } else if (inEntry && name == QStringLiteral("commit")) {
                current.revision = reader.attributes()
                                       .value(QStringLiteral("revision"))
                                       .toString();

            } else if (inEntry && name == QStringLiteral("lock")) {
                inLock = true;

            } else if (inEntry && name == QStringLiteral("owner") && inLock) {
                current.lockOwner = reader.readElementText();

            } else if (inEntry && name == QStringLiteral("author")) {
                current.author = reader.readElementText();

            } else if (inEntry && name == QStringLiteral("date")) {
                current.date = reader.readElementText();
            }

        } else if (reader.isEndElement()) {
            const QStringView name = reader.name();
            if (name == QStringLiteral("entry") && inEntry) {
                // 'svn list URL' on the URL itself may return an entry with an
                // empty name (the URL itself) — skip it.
                if (!current.name.isEmpty())
                    entries.append(current);
                inEntry = false;
            } else if (name == QStringLiteral("lock")) {
                inLock = false;
            }
        }
    }

    return entries;
}

QList<SvnBlameEntry> SvnManager::blameSync(const QString &path) const
{
    bool ok = false;
    const QString xml = runSvnSync(
        {QStringLiteral("annotate"), QStringLiteral("--xml"), path}, &ok);
    if (!ok || xml.isEmpty())
        return {};

    QList<SvnBlameEntry> entries = parseBlameXml(xml);
    if (entries.isEmpty())
        return entries;

    // SVN 1.14 --xml omits <text> for line content; fall back to zipping lines
    // by 1-based lineNumber. The content must come from BASE — annotate covers
    // the committed state, so a locally modified file would shift all line
    // numbers and mis-align revision/author with the text.
    bool hasText = false;
    for (const SvnBlameEntry &e : std::as_const(entries)) {
        if (!e.text.isEmpty()) { hasText = true; break; }
    }
    if (!hasText) {
        QByteArray content = cat(path, QStringLiteral("BASE"));
        if (content.isEmpty()) {
            // Last resort (e.g. cat failed): local file.
            QFile file(path);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text))
                content = file.readAll();
        }
        if (!content.isEmpty()) {
            const QStringList lines = QString::fromUtf8(content)
                                          .split(QLatin1Char('\n'));
            for (SvnBlameEntry &e : entries) {
                const int idx = e.lineNumber - 1;
                if (idx >= 0 && idx < lines.size()) {
                    e.text = lines[idx];
                    if (e.text.endsWith(QLatin1Char('\r')))
                        e.text.chop(1);
                }
            }
        }
    }

    return entries;
}

QString SvnManager::diffSync(const QString &path,
                              const QString &revFrom,
                              const QString &revTo) const
{
    bool ok = false;
    const QString out = runSvnSync({
        QStringLiteral("diff"),
        QStringLiteral("--revision"),
        QStringLiteral("%1:%2").arg(revFrom, revTo),
        path
    }, &ok);
    return ok ? out : QString();
}

bool SvnManager::createPatchSync(const QStringList &paths, const QString &outFile,
                                  QString *errorMessage) const
{
    if (paths.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("No paths selected.");
        return false;
    }

    QStringList args = {QStringLiteral("diff"), QStringLiteral("--git")};
    args << paths;

    bool ok = false;
    const QByteArray diff = runSvnSyncRaw(args, &ok);
    if (!ok) {
        if (errorMessage)
            *errorMessage = QString::fromUtf8(diff);
        return false;
    }
    if (diff.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("No local changes to save as a patch.");
        return false;
    }

    QFile f(outFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage)
            *errorMessage = f.errorString();
        return false;
    }
    if (f.write(diff) != diff.size() || !f.flush()) {
        if (errorMessage)
            *errorMessage = f.errorString();
        f.close();
        return false;
    }
    f.close();
    return true;
}

bool SvnManager::patchOutputHasRejects(const QStringList &outputLines)
{
    for (const QString &line : outputLines) {
        const QString t = line.trimmed();
        if (t.startsWith(QLatin1Char('>')) && t.contains(QLatin1String("rejected")))
            return true;
        if (t.startsWith(QLatin1String("Summary of conflicts:")))
            return true;
    }
    return false;
}

QStringList SvnManager::patchTargetPaths(const QByteArray &patchContent)
{
    // git patches introduce each path with "a/" or "b/"; 'svn patch' strips the
    // prefix itself before '--strip' applies, so do the same here.
    const bool isGit = patchContent.startsWith("diff --git ")
                    || patchContent.contains("\ndiff --git ");

    QStringList paths;
    const QList<QByteArray> lines = patchContent.split('\n');
    for (const QByteArray &raw : lines) {
        if (!raw.startsWith("--- "))
            continue;
        // "--- <path>\t(revision 4)" — the tab reliably separates the path even
        // when it contains spaces.
        QByteArray rest = raw.mid(4);
        const int tab = rest.indexOf('\t');
        if (tab >= 0)
            rest = rest.left(tab);
        QString path = QString::fromUtf8(rest).trimmed();
        if (path.isEmpty() || path == QLatin1String("/dev/null"))
            continue;
        if (isGit) {
            const int slash = path.indexOf(QLatin1Char('/'));
            if (slash < 0)
                continue;              // no prefix → unusable
            path = path.mid(slash + 1);
        }
        if (!path.isEmpty() && !paths.contains(path))
            paths << path;
    }
    return paths;
}

int SvnManager::detectPatchStripLevelSync(const QString &patchFile,
                                           const QString &wcPath) const
{
    QFile f(patchFile);
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    const QStringList paths = patchTargetPaths(f.readAll());
    f.close();

    const QDir wcDir(wcPath);

    int maxComponents = 0;
    for (const QString &p : paths)
        maxComponents = std::max<int>(maxComponents, static_cast<int>(p.count(QLatin1Char('/'))));

    int bestLevel = -1;
    int bestHits  = 0;
    for (int level = 0; level <= maxComponents; ++level) {
        int hits = 0;
        for (const QString &p : paths) {
            QString stripped = p;
            for (int i = 0; i < level; ++i) {
                const int slash = stripped.indexOf(QLatin1Char('/'));
                if (slash < 0) { stripped.clear(); break; }
                stripped = stripped.mid(slash + 1);
            }
            if (!stripped.isEmpty() && wcDir.exists(stripped))
                ++hits;
        }
        // Only real improvements count: on a tie the smaller level wins
        // (it removes less information).
        if (hits > bestHits) {
            bestHits  = hits;
            bestLevel = level;
        }
    }
    if (bestLevel >= 0)
        return bestLevel;

    // No match at all (e.g. a patch that only adds new files): fall back to the
    // layout of the target WC. 'svn info' returns the repository-relative URL as
    // "^/trunk/sub".
    SvnInfo wcInfo = info(wcPath);
    // What matters is the layout of the WC ROOT, not that of a subdirectory the
    // call happens to come from.
    if (wcInfo.valid && !wcInfo.wcRootPath.isEmpty()
        && wcInfo.wcRootPath != wcPath)
        wcInfo = info(wcInfo.wcRootPath);
    if (!wcInfo.valid || wcInfo.url.isEmpty() || wcInfo.repositoryRoot.isEmpty())
        return 0;
    QString rel = wcInfo.url.mid(wcInfo.repositoryRoot.length());
    while (rel.startsWith(QLatin1Char('/')))
        rel = rel.mid(1);
    while (rel.endsWith(QLatin1Char('/')))
        rel.chop(1);
    return rel.isEmpty() ? 0 : rel.count(QLatin1Char('/')) + 1;
}

QString SvnManager::diffChangeSync(const QString &target, const QString &rev) const
{
    // Repository URLs get a peg revision so paths added in 'rev' resolve
    // (they do not exist in rev-1). Working-copy paths need no peg.
    const bool isUrl = target.contains(QLatin1String("://"));
    const QString pegged = isUrl ? target + QLatin1Char('@') + rev : target;

    bool ok = false;
    const QString out = runSvnSync({
        QStringLiteral("diff"),
        QStringLiteral("--change"), rev,
        pegged
    }, &ok);
    return ok ? out : QString();
}

QList<SvnBlameEntry> SvnManager::parseBlameXml(const QString &xml) const
{
    QList<SvnBlameEntry> entries;
    QXmlStreamReader reader(xml);

    SvnBlameEntry current;
    bool inEntry  = false;
    bool inCommit = false;

    while (!reader.atEnd()) {
        reader.readNext();

        if (reader.isStartElement()) {
            const QStringView name = reader.name();

            if (name == QStringLiteral("entry")) {
                current = SvnBlameEntry{};
                current.lineNumber =
                    reader.attributes().value(QStringLiteral("line-number")).toInt();
                inEntry  = true;
                inCommit = false;

            } else if (inEntry && name == QStringLiteral("commit")) {
                current.revision =
                    reader.attributes().value(QStringLiteral("revision")).toString();
                inCommit = true;

            } else if (inEntry && inCommit && name == QStringLiteral("author")) {
                current.author = reader.readElementText();

            } else if (inEntry && inCommit && name == QStringLiteral("date")) {
                current.date = reader.readElementText();

            } else if (inEntry && name == QStringLiteral("text")) {
                current.text = reader.readElementText();
                // SVN includes the line terminator inside <text>; strip it so
                // QTableWidgetItem renders a single line (not two).
                if (current.text.endsWith(QLatin1Char('\n'))) current.text.chop(1);
                if (current.text.endsWith(QLatin1Char('\r'))) current.text.chop(1);
            }

        } else if (reader.isEndElement()) {
            const QStringView name = reader.name();
            if (name == QStringLiteral("entry") && inEntry) {
                entries.append(current);
                inEntry  = false;
                inCommit = false;
            } else if (name == QStringLiteral("commit")) {
                inCommit = false;
            }
        }
    }

    return entries;
}

SvnFileStatus SvnManager::charToStatus(QChar c)
{
    // Converts the single-character status code from 'svn status' (without --xml)
    switch (c.toLatin1()) {
    case 'M': return SvnFileStatus::Modified;
    case 'A': return SvnFileStatus::Added;
    case 'D': return SvnFileStatus::Deleted;
    case 'R': return SvnFileStatus::Replaced;
    case 'C': return SvnFileStatus::Conflicted;
    case '!': return SvnFileStatus::Missing;
    case '?': return SvnFileStatus::Unversioned;
    case 'I': return SvnFileStatus::Ignored;
    case 'X': return SvnFileStatus::External;
    case '~': return SvnFileStatus::Incomplete;
    case ' ':
    default:  return SvnFileStatus::Normal;
    }
}

