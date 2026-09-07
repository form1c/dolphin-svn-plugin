#include "svndiffwindow.h"
#include "svnmanager.h"
#include "svnsettings.h"
#include "svnuihelpers.h"

#include <KLocalizedString>

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString statusText(SvnFileStatus s)
{
    switch (s) {
    case SvnFileStatus::Modified:    return i18n("Modified");
    case SvnFileStatus::Added:       return i18n("Added");
    case SvnFileStatus::Deleted:     return i18n("Deleted");
    case SvnFileStatus::Replaced:    return i18n("Replaced");
    case SvnFileStatus::Conflicted:  return i18n("Conflicted");
    case SvnFileStatus::Missing:     return i18n("Missing");
    case SvnFileStatus::External:    return i18n("External");
    default:                         return QString();
    }
}

QString statusIconName(SvnFileStatus s)
{
    switch (s) {
    case SvnFileStatus::Modified:
    case SvnFileStatus::Replaced:   return QStringLiteral("vcs-locally-modified");
    case SvnFileStatus::Added:      return QStringLiteral("vcs-added");
    case SvnFileStatus::Deleted:
    case SvnFileStatus::Missing:    return QStringLiteral("vcs-removed");
    case SvnFileStatus::Conflicted: return QStringLiteral("vcs-conflicting");
    case SvnFileStatus::External:   return QStringLiteral("vcs-normal");
    default:                        return QString();
    }
}

// True = file exists locally, can be opened by a diff tool.
bool isDiffable(SvnFileStatus s)
{
    switch (s) {
    case SvnFileStatus::Modified:
    case SvnFileStatus::Added:
    case SvnFileStatus::Replaced:
    case SvnFileStatus::Conflicted:
        return true;
    default:
        return false;
    }
}

bool isShowable(SvnFileStatus s)
{
    switch (s) {
    case SvnFileStatus::Modified:
    case SvnFileStatus::Added:
    case SvnFileStatus::Deleted:
    case SvnFileStatus::Replaced:
    case SvnFileStatus::Conflicted:
    case SvnFileStatus::Missing:
    case SvnFileStatus::External:
        return true;
    default:
        return false;
    }
}
} // namespace

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

QString SvnDiffWindow::findDiffTool()
{
    // User-configured tool takes precedence.
    const QString preferred = SvnSettings::preferredDiffTool();
    if (!preferred.isEmpty() && QFileInfo::exists(preferred))
        return preferred;

    for (const QString &name : {QStringLiteral("angscheidrdiffer"),
                                  QStringLiteral("meld"),
                                  QStringLiteral("kdiff3"),
                                  QStringLiteral("kompare")}) {
        const QString path = QStandardPaths::findExecutable(name);
        if (!path.isEmpty())
            return path;
    }
    return {};
}

void SvnDiffWindow::launchFileDiff(const QString &tool, const QString &wcPath,
                                    SvnManager *mgr, QObject *processParent,
                                    const QString &revision)
{
    // Retrieve the repository content at the given revision (empty on failure).
    const QByteArray baseContent = mgr->cat(wcPath, revision);

    // Write BASE content to a temp file, preserving the original file extension
    // so the diff tool can apply syntax highlighting.
    const QString suffix = QFileInfo(wcPath).suffix();
    QString tmpDir = SvnSettings::tempDirectory();
    if (tmpDir.isEmpty() || !QDir(tmpDir).exists())
        tmpDir = QDir::tempPath();
    const QString tmpl = tmpDir + QStringLiteral("/svndiff_XXXXXX")
        + (suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix);

    auto *tmp = new QTemporaryFile(tmpl);
    tmp->setAutoRemove(false);
    if (!tmp->open()) {
        delete tmp;
        return;
    }
    tmp->write(baseContent);
    const QString tmpPath = tmp->fileName();
    tmp->close();
    delete tmp; // Destroy the object but keep the file on disk.

    // Launch the diff tool. Clean up the temp file once the tool is closed.
    auto *proc = new QProcess(processParent);
    QObject::connect(proc,
                     QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     proc, [proc, tmpPath]() {
                         QFile::remove(tmpPath);
                         proc->deleteLater();
                     });
    proc->start(tool, {tmpPath, wcPath});
}

bool SvnDiffWindow::launchHistoricDiff(const QString &tool,
                                       const QString &target,
                                       long long revLeft, long long revRight,
                                       SvnManager *mgr, QObject *processParent)
{
    // Peg to revRight, so URLs resolve correctly even after renames; WC paths
    // stay unchanged.
    const bool isUrl = target.contains(QStringLiteral("://"));
    const QString pegged = isUrl
        ? target + QLatin1Char('@') + QString::number(revRight)
        : target;

    const QByteArray rightContent =
        mgr->cat(pegged, QString::number(revRight));
    if (rightContent.isEmpty())
        return false; // directory, binary or read error → text fallback

    QByteArray leftContent;
    if (revLeft >= 1)
        leftContent = mgr->cat(pegged, QString::number(revLeft));
    // empty = file was newly added in revRight → empty left side

    const QString suffix = QFileInfo(target).suffix();
    QString tmpDir = SvnSettings::tempDirectory();
    if (tmpDir.isEmpty() || !QDir(tmpDir).exists())
        tmpDir = QDir::tempPath();
    const auto writeTemp = [&tmpDir, &suffix](long long rev,
                                              const QByteArray &content) {
        const QString tmpl = tmpDir
            + QStringLiteral("/svndiff_r%1_XXXXXX").arg(rev)
            + (suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix);
        QTemporaryFile tmp(tmpl);
        tmp.setAutoRemove(false);
        if (!tmp.open())
            return QString();
        tmp.write(content);
        return tmp.fileName();
    };
    const QString leftPath  = writeTemp(qMax(revLeft, 0LL), leftContent);
    const QString rightPath = writeTemp(revRight, rightContent);
    if (leftPath.isEmpty() || rightPath.isEmpty()) {
        QFile::remove(leftPath);
        QFile::remove(rightPath);
        return false;
    }

    QStringList args;
    if (QFileInfo(tool).fileName().contains(
            QStringLiteral("angscheidrdiffer")))
        args << QStringLiteral("--readonly"); // both sides are historical
    args << leftPath << rightPath;

    auto *proc = new QProcess(processParent);
    QObject::connect(proc,
                     QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     proc, [proc, leftPath, rightPath]() {
                         QFile::remove(leftPath);
                         QFile::remove(rightPath);
                         proc->deleteLater();
                     });
    proc->start(tool, args);
    return true;
}

bool SvnDiffWindow::launchHistoricFolderDiff(const QString &tool,
                                             const QString &target,
                                             long long revLeft,
                                             long long revRight,
                                             SvnManager *mgr,
                                             QObject *processParent)
{
    QString tmpBase = SvnSettings::tempDirectory();
    if (tmpBase.isEmpty() || !QDir(tmpBase).exists())
        tmpBase = QDir::tempPath();

    // A unique root directory; the export targets below it must not exist yet
    // (svn export creates them).
    QTemporaryDir tmpRoot(tmpBase + QStringLiteral("/svndiff_folders_XXXXXX"));
    if (!tmpRoot.isValid())
        return false;
    tmpRoot.setAutoRemove(false);
    const QString root = tmpRoot.path();
    const QString leftDir =
        root + QStringLiteral("/r%1").arg(revLeft);
    const QString rightDir =
        root + QStringLiteral("/r%1").arg(revRight);

    if (!mgr->exportSync(target, QString::number(revLeft), leftDir)
        || !mgr->exportSync(target, QString::number(revRight), rightDir)) {
        QDir(root).removeRecursively();
        return false;
    }

    QStringList args;
    if (QFileInfo(tool).fileName().contains(
            QStringLiteral("angscheidrdiffer")))
        args << QStringLiteral("--readonly");
    args << leftDir << rightDir;

    auto *proc = new QProcess(processParent);
    QObject::connect(proc,
                     QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     proc, [proc, root]() {
                         QDir(root).removeRecursively();
                         proc->deleteLater();
                     });
    proc->start(tool, args);
    return true;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

SvnDiffWindow::SvnDiffWindow(const QStringList &paths, SvnManager *mgr,
                              const QString &tool, QObject *processParent,
                              QWidget *parent)
    : QDialog(parent), m_tool(tool), m_mgr(mgr), m_procParent(processParent)
{
    setWindowTitle(i18n("Diff"));
    resize(700, 500);

    auto *layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel(
        i18n("Select file to compare (double-click or button):"), this));
    layout->addWidget(new QLabel(
        i18n("Tool: <b>%1</b>", QFileInfo(tool).fileName()), this));

    // --- File tree ---
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({i18n("Path"), i18n("Extension"), i18n("Status")});
    m_tree->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setMinimumSectionSize(40);
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setAlternatingRowColors(true);
    layout->addWidget(m_tree, 1);

    // --- Buttons ---
    auto *btnRow = new QHBoxLayout;
    m_diffBtn = new QPushButton(
        QIcon::fromTheme(QStringLiteral("vcs-diff")), i18n("Show Diff"), this);
    m_diffBtn->setEnabled(false);
    btnRow->addWidget(m_diffBtn);
    btnRow->addStretch();
    auto *closeBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    btnRow->addWidget(closeBox);
    layout->addLayout(btnRow);

    // --- Connections ---
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        const auto sel = m_tree->selectedItems();
        if (sel.size() != 1) { m_diffBtn->setEnabled(false); return; }
        QTreeWidgetItem *item = sel.first();
        const bool canDiff = item->data(0, Qt::UserRole + 2).toBool();
        m_diffBtn->setEnabled(canDiff);
    });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
                if (item->data(0, Qt::UserRole + 2).toBool())
                    diffItem(item);
            });
    connect(m_diffBtn, &QPushButton::clicked, this, [this]() {
        const auto sel = m_tree->selectedItems();
        if (sel.size() == 1) diffItem(sel.first());
    });
    connect(closeBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // --- Populate tree ---
    QApplication::setOverrideCursor(Qt::WaitCursor);

    const SvnInfo wcInfo = mgr->info(paths.first());
    const QDir wcDir(wcInfo.valid ? wcInfo.wcRootPath
                                  : QFileInfo(paths.first()).absolutePath());

    QList<SvnStatusEntry> entries;
    QSet<QString> seen;
    bool anyStatusFailed = false;
    for (const QString &p : paths) {
        const QFileInfo fi(p);
        bool ok = false;
        const auto statusEntries = mgr->status(fi.absoluteFilePath(), fi.isDir(), &ok);
        if (!ok) anyStatusFailed = true;
        for (const SvnStatusEntry &e : statusEntries) {
            if (!isShowable(e.textStatus)) continue;
            const QString abs = QFileInfo(e.path).absoluteFilePath();
            if (seen.contains(abs)) continue;
            entries << e;
            seen.insert(abs);
        }
    }

    QApplication::restoreOverrideCursor();

    std::sort(entries.begin(), entries.end(), [](const SvnStatusEntry &a,
                                                  const SvnStatusEntry &b) {
        return a.path < b.path;
    });

    if (entries.isEmpty()) {
        m_tree->setEnabled(false);
        auto *ph = new QTreeWidgetItem(m_tree);
        if (anyStatusFailed) {
            ph->setText(0, i18n("Failed to retrieve SVN status. "
                                "Check that svn is installed and this is a valid working copy."));
            ph->setIcon(0, QIcon::fromTheme(QStringLiteral("dialog-warning")));
        } else {
            ph->setText(0, i18n("No local changes found."));
        }
        ph->setFlags(Qt::NoItemFlags);
        return;
    }

    QFileIconProvider iconProvider;
    QHash<QString, QTreeWidgetItem *> pathToItem;

    for (const SvnStatusEntry &e : entries) {
        const QString abs = QFileInfo(e.path).absoluteFilePath();
        const QFileInfo fi(abs);
        const bool isDir   = fi.isDir();
        const bool canDiff = !isDir && isDiffable(e.textStatus);

        QTreeWidgetItem *parentItem = pathToItem.value(fi.absolutePath(), nullptr);

        const QString rel = wcDir.relativeFilePath(abs);
        const QString fullDisplay = rel.startsWith(QLatin1String("./")) ? rel.mid(2) : rel;
        const QString displayText = parentItem ? fi.fileName() : fullDisplay;
        const QString ext = isDir ? QString()
                                  : (fi.suffix().isEmpty() ? QString()
                                     : QLatin1Char('.') + fi.suffix());

        auto *item = parentItem ? new QTreeWidgetItem(parentItem)
                                : new QTreeWidgetItem(m_tree);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setText(0, displayText);
        item->setToolTip(0, fullDisplay);
        item->setIcon(0, isDir ? SvnUi::makeFolderIcon() : iconProvider.icon(fi));
        item->setText(1, ext);
        item->setText(2, statusText(e.textStatus));
        const QString icon = statusIconName(e.textStatus);
        if (!icon.isEmpty())
            item->setIcon(2, QIcon::fromTheme(icon));

        // Items that can't be diffed are shown grayed out.
        if (!canDiff && !isDir) {
            for (int c = 0; c < m_tree->columnCount(); ++c)
                item->setForeground(c, QApplication::palette().color(QPalette::Disabled,
                                                                       QPalette::Text));
        }

        item->setData(0, Qt::UserRole,     abs);
        item->setData(0, Qt::UserRole + 1, isDir);
        item->setData(0, Qt::UserRole + 2, canDiff);

        pathToItem.insert(abs, item);
    }

    m_tree->expandAll();
    m_tree->resizeColumnToContents(2);
    m_tree->resizeColumnToContents(1);
    m_tree->resizeColumnToContents(0);
}

// ---------------------------------------------------------------------------
// Launch diff for a single tree item
// ---------------------------------------------------------------------------

void SvnDiffWindow::diffItem(QTreeWidgetItem *item)
{
    const QString path = item->data(0, Qt::UserRole).toString();
    launchFileDiff(m_tool, path, m_mgr, m_procParent);
}
