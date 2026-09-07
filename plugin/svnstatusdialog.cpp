#include "svnstatusdialog.h"
#include "svnadddialog.h"
#include "svncommitdialog.h"
#include "svndiffwindow.h"
#include "svnmanager.h"
#include "svnrevertdialog.h"
#include "svnsettings.h"
#include "svnuihelpers.h"

#include <KLocalizedString>

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QShortcut>
#include <QDir>
#include <QFileIconProvider>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
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
    case SvnFileStatus::Unversioned: return i18n("Unversioned");
    case SvnFileStatus::External:    return i18n("External");
    default:                         return QString();
    }
}

QIcon statusIcon(SvnFileStatus s)
{
    switch (s) {
    case SvnFileStatus::Modified:
    case SvnFileStatus::Replaced:    return QIcon::fromTheme(QStringLiteral("vcs-locally-modified"));
    case SvnFileStatus::Added:       return QIcon::fromTheme(QStringLiteral("vcs-added"));
    case SvnFileStatus::Deleted:
    case SvnFileStatus::Missing:     return QIcon::fromTheme(QStringLiteral("vcs-removed"));
    case SvnFileStatus::Conflicted:  return QIcon::fromTheme(QStringLiteral("vcs-conflicting"));
    case SvnFileStatus::Unversioned: return QIcon::fromTheme(QStringLiteral("unknown"),
                                                              QIcon::fromTheme(QStringLiteral("vcs-none")));
    case SvnFileStatus::External:    return QIcon::fromTheme(QStringLiteral("vcs-normal"));
    default:                         return QIcon();
    }
}

bool isInteresting(SvnFileStatus s)
{
    switch (s) {
    case SvnFileStatus::Modified:
    case SvnFileStatus::Added:
    case SvnFileStatus::Deleted:
    case SvnFileStatus::Replaced:
    case SvnFileStatus::Conflicted:
    case SvnFileStatus::Missing:
    case SvnFileStatus::Unversioned:
    case SvnFileStatus::External:
        return true;
    default:
        return false;
    }
}
} // namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

SvnStatusDialog::SvnStatusDialog(const QStringList &paths, SvnManager *mgr,
                                  QWidget *parent)
    : QDialog(parent), m_paths(paths), m_mgr(mgr)
{
    setWindowTitle(i18n("Check for Modifications – %1", QFileInfo(m_paths.first()).fileName()));
    resize(720, 520);

    auto *layout = new QVBoxLayout(this);

    // --- File tree ---
    layout->addWidget(new QLabel(i18n("Local modifications:"), this));

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({i18n("Path"), i18n("Extension"), i18n("Status"), i18n("Remote")});
    m_tree->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setMinimumSectionSize(40);
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setAlternatingRowColors(true);
    m_tree->setColumnHidden(3, true); // only visible after "Check Repository"
    layout->addWidget(m_tree, 1);

    // --- Count label ---
    m_countLabel = new QLabel(this);
    layout->addWidget(m_countLabel);

    // --- Options + buttons ---
    m_showUnversioned = new QCheckBox(
        i18n("Show unversioned files"), this);
    m_showUnversioned->setChecked(SvnSettings::showUnversioned());
    layout->addWidget(m_showUnversioned);

    auto *btnRow = new QHBoxLayout;
    auto *refreshBtn = new QPushButton(
        QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Refresh"), this);
    btnRow->addWidget(refreshBtn);
    auto *checkRepoBtn = new QPushButton(
        QIcon::fromTheme(QStringLiteral("network-server")),
        i18n("Check Repository"), this);
    btnRow->addWidget(checkRepoBtn);
    btnRow->addStretch();
    auto *closeBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    btnRow->addWidget(closeBox);
    layout->addLayout(btnRow);

    connect(m_showUnversioned, &QCheckBox::toggled, this, [this](bool show) {
        SvnSettings::setShowUnversioned(show);
        refresh();
    });
    connect(refreshBtn, &QPushButton::clicked, this, &SvnStatusDialog::refresh);
    connect(checkRepoBtn, &QPushButton::clicked, this, [this]() {
        m_showUpdates = true;
        refresh();
    });
    connect(closeBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *f5 = new QShortcut(Qt::Key_F5, this);
    connect(f5, &QShortcut::activated, this, &SvnStatusDialog::refresh);

    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &SvnStatusDialog::showContextMenu);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
                if (!item) return;
                const QString abs = item->data(0, Qt::UserRole).toString();
                if (abs.isEmpty()) return;
                const auto st = static_cast<SvnFileStatus>(
                    item->data(0, Qt::UserRole + 1).toInt());
                if (QFileInfo(abs).isFile() &&
                    (st == SvnFileStatus::Modified  ||
                     st == SvnFileStatus::Replaced  ||
                     st == SvnFileStatus::Conflicted))
                    diffFile(abs);
            });

    refresh();
}

// ---------------------------------------------------------------------------
// Refresh / populate tree
// ---------------------------------------------------------------------------

void SvnStatusDialog::refresh()
{
    m_tree->clear();

    QApplication::setOverrideCursor(Qt::WaitCursor);

    const bool showUnversioned = m_showUnversioned->isChecked();

    QList<SvnStatusEntry> entries;
    QSet<QString> seen;
    bool anyStatusFailed = false;
    for (const QString &p : m_paths) {
        const QFileInfo fi(p);
        bool ok = false;
        const auto all = m_mgr->status(fi.absoluteFilePath(), fi.isDir(), &ok, m_showUpdates);
        if (!ok) anyStatusFailed = true;
        for (const SvnStatusEntry &e : all) {
            const bool remoteChanged = e.remoteTextStatus != SvnFileStatus::Unknown
                                     || e.remotePropStatus != SvnFileStatus::Unknown;
            if (!isInteresting(e.textStatus) && e.propStatus != SvnFileStatus::Modified
                && !remoteChanged) continue;
            if (e.textStatus == SvnFileStatus::Unversioned && !showUnversioned) continue;
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

    m_tree->setColumnHidden(3, !m_showUpdates);

    if (entries.isEmpty()) {
        m_tree->setEnabled(false);
        auto *placeholder = new QTreeWidgetItem(m_tree);
        if (anyStatusFailed) {
            placeholder->setText(0, i18n("Failed to retrieve SVN status. "
                                         "Check that svn is installed and this is a valid working copy."));
            placeholder->setIcon(0, QIcon::fromTheme(QStringLiteral("dialog-warning")));
        } else {
            placeholder->setText(0, i18n("No local modifications found."));
        }
        placeholder->setFlags(Qt::NoItemFlags);
        m_countLabel->setText(QString());
        return;
    }

    m_tree->setEnabled(true);

    // Determine working copy root for relative paths.
    const SvnInfo wcInfo = m_mgr->info(m_paths.first());
    const QDir wcDir(wcInfo.valid ? wcInfo.wcRootPath
                                  : QFileInfo(m_paths.first()).absolutePath());

    QFileIconProvider iconProvider;
    QHash<QString, QTreeWidgetItem *> pathToItem;
    int versionedCount = 0;

    for (const SvnStatusEntry &e : entries) {
        const QString abs = QFileInfo(e.path).absoluteFilePath();
        const QFileInfo fi(abs);
        const bool isDir = fi.isDir();

        QTreeWidgetItem *parentItem = pathToItem.value(fi.absolutePath(), nullptr);

        const QString relPath = wcDir.relativeFilePath(abs);
        const QString fullDisplay = relPath.startsWith(QLatin1String("./"))
            ? relPath.mid(2) : relPath;
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
        const bool onlyPropMod = !isInteresting(e.textStatus)
                                  && e.propStatus == SvnFileStatus::Modified;
        item->setText(2, onlyPropMod ? i18n("Prop. Modified") : statusText(e.textStatus));
        const QIcon icon = onlyPropMod
            ? QIcon::fromTheme(QStringLiteral("vcs-locally-modified"))
            : statusIcon(e.textStatus);
        if (!icon.isNull())
            item->setIcon(2, icon);
        if (e.isLocked) {
            item->setText(2, item->text(2)
                + (e.lockOwner.isEmpty()
                       ? i18n(" · Locked")
                       : i18n(" · Locked (%1)", e.lockOwner)));
            item->setIcon(1, QIcon::fromTheme(QStringLiteral("object-locked")));
            item->setToolTip(1, e.lockComment.isEmpty()
                ? i18n("Locked") : e.lockComment);
        }
        if (m_showUpdates) {
            const bool remoteOnlyPropMod = e.remoteTextStatus == SvnFileStatus::Unknown
                                         && e.remotePropStatus == SvnFileStatus::Modified;
            item->setText(3, remoteOnlyPropMod ? i18n("Prop. Modified")
                                               : statusText(e.remoteTextStatus));
            const QIcon remoteIcon = remoteOnlyPropMod
                ? QIcon::fromTheme(QStringLiteral("vcs-locally-modified"))
                : statusIcon(e.remoteTextStatus);
            if (!remoteIcon.isNull())
                item->setIcon(3, remoteIcon);
        }
        item->setData(0, Qt::UserRole,     abs);
        item->setData(0, Qt::UserRole + 1, static_cast<int>(e.textStatus));
        item->setData(0, Qt::UserRole + 2, static_cast<int>(e.propStatus));

        pathToItem.insert(abs, item);

        if (e.textStatus != SvnFileStatus::Unversioned)
            ++versionedCount;
    }

    m_tree->expandAll();
    if (m_showUpdates)
        m_tree->resizeColumnToContents(3);
    m_tree->resizeColumnToContents(2);
    m_tree->resizeColumnToContents(1);
    m_tree->resizeColumnToContents(0);

    const int total = entries.size();
    if (showUnversioned && total > versionedCount) {
        m_countLabel->setText(i18np(
            "%1 modification found (%2 unversioned).",
            "%1 modifications found (%2 unversioned).",
            total, total - versionedCount));
    } else {
        m_countLabel->setText(i18np(
            "%1 modification found.",
            "%1 modifications found.",
            total));
    }
}

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------

void SvnStatusDialog::showContextMenu(const QPoint &pos)
{
    const QList<QTreeWidgetItem *> sel = m_tree->selectedItems();
    if (sel.isEmpty()) return;

    QStringList revertPaths;
    QStringList resolvePaths;
    QStringList addPaths;
    QStringList commitPaths;
    QString     diffPath;
    bool        canDiff = false;

    for (QTreeWidgetItem *it : sel) {
        const QString abs = it->data(0, Qt::UserRole).toString();
        if (abs.isEmpty()) continue;
        const auto st = static_cast<SvnFileStatus>(it->data(0, Qt::UserRole + 1).toInt());
        const auto ps = static_cast<SvnFileStatus>(it->data(0, Qt::UserRole + 2).toInt());

        if (sel.size() == 1 && QFileInfo(abs).isFile()) {
            switch (st) {
            case SvnFileStatus::Modified:
            case SvnFileStatus::Replaced:
            case SvnFileStatus::Conflicted:
                canDiff  = true;
                diffPath = abs;
                break;
            default: break;
            }
        }

        switch (st) {
        case SvnFileStatus::Modified:
        case SvnFileStatus::Added:
        case SvnFileStatus::Deleted:
        case SvnFileStatus::Replaced:
        case SvnFileStatus::Conflicted:
        case SvnFileStatus::Missing:
            revertPaths << abs;
            commitPaths << abs;
            break;
        default:
            if (ps == SvnFileStatus::Modified) {
                revertPaths << abs;
                commitPaths << abs;
            }
            break;
        }

        if (st == SvnFileStatus::Conflicted)
            resolvePaths << abs;

        if (st == SvnFileStatus::Unversioned)
            addPaths << abs;
    }

    QMenu menu(this);

    if (canDiff)
        connect(menu.addAction(QIcon::fromTheme(QStringLiteral("vcs-diff")),
                               i18n("Diff with BASE")),
                &QAction::triggered, this,
                [this, diffPath]() { diffFile(diffPath); });

    if (!resolvePaths.isEmpty()) {
        auto *resolveMenu = menu.addMenu(
            QIcon::fromTheme(QStringLiteral("vcs-conflicting")),
            i18n("Resolve Conflict"));
        connect(resolveMenu->addAction(
                    QIcon::fromTheme(QStringLiteral("dialog-ok-apply")),
                    i18n("Mark as Resolved (keep working copy)")),
                &QAction::triggered, this, [this, resolvePaths]() {
                    runOperation(i18n("SVN: Resolve"), [this, resolvePaths]() {
                        m_mgr->resolveAsync(resolvePaths, QStringLiteral("working"));
                    });
                });
        connect(resolveMenu->addAction(
                    QIcon::fromTheme(QStringLiteral("go-previous")),
                    i18n("Use Mine (discard their changes)")),
                &QAction::triggered, this, [this, resolvePaths]() {
                    runOperation(i18n("SVN: Resolve (mine)"), [this, resolvePaths]() {
                        m_mgr->resolveAsync(resolvePaths, QStringLiteral("mine-full"));
                    });
                });
        connect(resolveMenu->addAction(
                    QIcon::fromTheme(QStringLiteral("go-next")),
                    i18n("Use Theirs (discard my changes)")),
                &QAction::triggered, this, [this, resolvePaths]() {
                    runOperation(i18n("SVN: Resolve (theirs)"), [this, resolvePaths]() {
                        m_mgr->resolveAsync(resolvePaths, QStringLiteral("theirs-full"));
                    });
                });
    }

    if (!revertPaths.isEmpty())
        connect(menu.addAction(QIcon::fromTheme(QStringLiteral("edit-undo")),
                               i18n("Revert...")),
                &QAction::triggered, this, [this, revertPaths]() {
                    SvnRevertDialog dlg(revertPaths, m_mgr, this);
                    if (dlg.exec() != QDialog::Accepted) return;
                    const QStringList paths = dlg.selectedPaths();
                    if (paths.isEmpty()) return;
                    runOperation(i18n("SVN: Revert"), [this, paths]() {
                        m_mgr->revertAsync(paths, QStringLiteral("infinity"));
                    });
                });

    if (!addPaths.isEmpty())
        connect(menu.addAction(QIcon::fromTheme(QStringLiteral("list-add")),
                               i18n("Add...")),
                &QAction::triggered, this, [this, addPaths]() {
                    SvnAddDialog dlg(addPaths, m_mgr, this);
                    if (dlg.exec() != QDialog::Accepted) return;
                    const QStringList paths = dlg.selectedPaths();
                    if (paths.isEmpty()) return;
                    runOperation(i18n("SVN: Add"), [this, paths]() {
                        m_mgr->addAsync(paths, QStringLiteral("empty"));
                    });
                });

    if (!addPaths.isEmpty())
        connect(menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")),
                               i18n("Delete (unversioned)")),
                &QAction::triggered, this, [this, addPaths]() {
                    const int ret = QMessageBox::warning(
                        this, i18n("Delete Unversioned Items"),
                        i18np("Permanently delete this unversioned item from disk?"
                              "\n\n%2",
                              "Permanently delete these %1 unversioned items "
                              "from disk?\n\n%2",
                              addPaths.size(), addPaths.join(QLatin1Char('\n'))),
                        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
                    if (ret != QMessageBox::Yes) return;
                    for (const QString &p : addPaths) {
                        const QFileInfo fi(p);
                        if (fi.isDir())
                            QDir(p).removeRecursively();
                        else
                            QFile::remove(p);
                    }
                    refresh();
                });

    if (!commitPaths.isEmpty())
        connect(menu.addAction(QIcon::fromTheme(QStringLiteral("vcs-commit")),
                               i18n("Commit selected...")),
                &QAction::triggered, this, [this, commitPaths]() {
                    SvnCommitDialog dlg(commitPaths, m_mgr, this);
                    if (dlg.exec() != QDialog::Accepted) return;
                    const QStringList selected = dlg.selectedPaths();
                    const QStringList toAdd    = dlg.unversionedPaths();
                    const QString msg          = dlg.message();
                    const bool keepLocks       = dlg.keepLocks();
                    if (selected.isEmpty()) return;
                    if (!toAdd.isEmpty())
                        m_mgr->addSync(toAdd, QStringLiteral("empty"));
                    runOperation(i18n("SVN: Commit"), [this, selected, msg, keepLocks]() {
                        m_mgr->commitAsync(selected, msg, keepLocks);
                    });
                });

    if (!menu.isEmpty())
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// Diff a single file against BASE
// ---------------------------------------------------------------------------

void SvnStatusDialog::diffFile(const QString &absPath)
{
    const QString tool = SvnDiffWindow::findDiffTool();
    if (tool.isEmpty()) {
        QMessageBox::warning(this, i18n("SVN: Diff"),
            i18n("No supported diff tool found.\n\n"
                 "Please install one of the following tools:\n"
                 "  • Meld  (sudo apt install meld)\n"
                 "  • KDiff3  (sudo apt install kdiff3)\n"
                 "  • Kompare  (sudo apt install kompare)"));
        return;
    }
    SvnDiffWindow::launchFileDiff(tool, absPath, m_mgr, this);
}

// ---------------------------------------------------------------------------
// Run an SVN operation with a progress dialog; refresh on close
// ---------------------------------------------------------------------------

void SvnStatusDialog::runOperation(const QString &title, std::function<void()> startOp)
{
    SvnUi::runWithProgress(m_mgr, title, this, startOp, [this]() { refresh(); });
}
