#include "svncommitdialog.h"
#include "svncommittree.h"
#include "svndiffwindow.h"
#include "svnmanager.h"
#include "svnsettings.h"
#include "svnuihelpers.h"
#include "svntreecheck.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFont>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QShortcut>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Above this many entries an unversioned directory is shown as a single
// collapsed row instead of listing every file. A working copy can hold a large
// unversioned directory (a build output, node_modules, a virtualenv) that is not
// svn:ignore'd; enumerating all of it would create tens of thousands of rows and
// make the dialog slow to open. See SvnUi::collectUnversionedDirCapped().
static constexpr int kUnversionedDirCap = 1000;

// The pure tree helpers (commonAncestorDir, collectUnversionedDirCapped,
// descendantPathsSorted, buildCommitTree) live in svncommittree.* so they can be
// unit-tested without KF6. This file keeps only status collection and the item
// decoration (status text, icons, fonts) that needs KF6 and the backend.

static QString statusText(SvnFileStatus s)
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

static QIcon statusIcon(SvnFileStatus s)
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

static bool isCommittable(SvnFileStatus s)
{
    switch (s) {
    case SvnFileStatus::Modified:
    case SvnFileStatus::Added:
    case SvnFileStatus::Deleted:
    case SvnFileStatus::Replaced:
    case SvnFileStatus::Conflicted:
    case SvnFileStatus::Missing:
        return true;
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

SvnCommitDialog::SvnCommitDialog(const QStringList &paths, SvnManager *mgr,
                                  QWidget *parent)
    : QDialog(parent)
{
    m_paths = paths;
    m_mgr   = mgr;

    setWindowTitle(i18n("Commit – %1", QFileInfo(paths.first()).fileName()));
    resize(680, 560);

    auto *layout = new QVBoxLayout(this);

    // --- File tree ---
    auto *filesWidget = new QWidget(this);
    auto *filesLayout = new QVBoxLayout(filesWidget);
    filesLayout->setContentsMargins(0, 0, 0, 0);

    auto *headerRow = new QHBoxLayout;
    headerRow->addWidget(new QLabel(i18n("Changes made:"), filesWidget));
    headerRow->addStretch();
    m_selectionLabel = new QLabel(filesWidget);
    headerRow->addWidget(m_selectionLabel);
    filesLayout->addLayout(headerRow);

    m_tree = new QTreeWidget(filesWidget);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({i18n("Path"), i18n("Extension"), i18n("Status")});
    m_tree->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setMinimumSectionSize(40);
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setAlternatingRowColors(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    filesLayout->addWidget(m_tree);

    auto *btnRow = new QHBoxLayout;
    auto *selectAllBtn   = new QPushButton(i18n("Select All"), filesWidget);
    auto *deselectAllBtn = new QPushButton(i18n("Select None"), filesWidget);
    btnRow->addWidget(selectAllBtn);
    btnRow->addWidget(deselectAllBtn);
    btnRow->addStretch();
    m_hideUnversioned = new QCheckBox(i18n("Hide unversioned items"), filesWidget);
    m_hideUnversioned->setChecked(SvnSettings::hideUnversioned());
    btnRow->addWidget(m_hideUnversioned);
    filesLayout->addLayout(btnRow);

    // --- Message area ---
    auto *msgWidget = new QWidget(this);
    auto *msgLayout = new QVBoxLayout(msgWidget);
    msgLayout->setContentsMargins(0, 0, 0, 0);

    msgLayout->addWidget(new QLabel(i18n("Message:"), msgWidget));

    auto *histRow = new QHBoxLayout;
    histRow->addWidget(new QLabel(i18n("Recent messages:"), msgWidget));
    m_historyCombo = new QComboBox(msgWidget);
    m_historyCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    histRow->addWidget(m_historyCombo);
    // A1: small button to clear the recent-messages history (per repo).
    auto *clearHistoryBtn = new QToolButton(msgWidget);
    clearHistoryBtn->setIcon(
        QIcon::fromTheme(QStringLiteral("edit-clear-history")));
    clearHistoryBtn->setToolTip(i18n("Clear recent messages"));
    clearHistoryBtn->setAutoRaise(true);
    connect(clearHistoryBtn, &QToolButton::clicked,
            this, &SvnCommitDialog::clearMessageHistory);
    histRow->addWidget(clearHistoryBtn);
    msgLayout->addLayout(histRow);

    m_messageEdit = new QPlainTextEdit(msgWidget);
    m_messageEdit->setMinimumHeight(80);
    m_messageEdit->setPlaceholderText(i18n("Enter log message..."));
    msgLayout->addWidget(m_messageEdit);

    m_charCountLabel = new QLabel(i18n("0 characters"), msgWidget);
    m_charCountLabel->setAlignment(Qt::AlignRight);
    msgLayout->addWidget(m_charCountLabel);

    // --- Splitter ---
    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(filesWidget);
    splitter->addWidget(msgWidget);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);

    // --- Keep locks ---
    m_keepLocks = new QCheckBox(
        i18n("Keep locks (do not release locks held by this working copy)"), this);
    m_keepLocks->setChecked(SvnSettings::keepLocksOnCommit());
    layout->addWidget(m_keepLocks);
    connect(m_keepLocks, &QCheckBox::toggled, this, [](bool keep) {
        SvnSettings::setKeepLocksOnCommit(keep);
    });

    // --- Buttons ---
    auto *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_commitBtn = btnBox->button(QDialogButtonBox::Ok);
    m_commitBtn->setText(i18n("Commit"));
    m_commitBtn->setIcon(QIcon::fromTheme(QStringLiteral("vcs-commit")));
    m_commitBtn->setEnabled(false);
    layout->addWidget(btnBox);

    // --- Populate ---
    populateTree(paths, mgr);
    loadMessageHistory();

    // --- Connections ---
    connect(m_tree, &QTreeWidget::itemChanged,
            this, &SvnCommitDialog::onItemChanged);
    auto applyToAll = [this](Qt::CheckState state) {
        m_block = true;
        QList<QTreeWidgetItem *> stack;
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
            stack.push_back(m_tree->topLevelItem(i));
        while (!stack.isEmpty()) {
            QTreeWidgetItem *it = stack.takeFirst();
            if (!it->isHidden())
                it->setCheckState(0, state);
            for (int i = 0; i < it->childCount(); ++i)
                stack.push_back(it->child(i));
        }
        m_block = false;
        updateSelectionLabel();
    };
    connect(selectAllBtn,   &QPushButton::clicked, this, [applyToAll] { applyToAll(Qt::Checked);   });
    connect(deselectAllBtn, &QPushButton::clicked, this, [applyToAll] { applyToAll(Qt::Unchecked); });
    connect(m_historyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (index <= 0) return;
        const QString msg =
            m_historyCombo->itemData(index, Qt::UserRole).toString();
        if (!msg.isEmpty())
            m_messageEdit->setPlainText(msg);
        m_historyCombo->setCurrentIndex(0);
    });
    connect(m_messageEdit, &QPlainTextEdit::textChanged, this, [this]() {
        const int n = m_messageEdit->toPlainText().length();
        m_charCountLabel->setText(i18np("%1 character", "%1 characters", n));
        updateSelectionLabel();
    });
    connect(m_hideUnversioned, &QCheckBox::toggled, this, [this](bool hide) {
        SvnSettings::setHideUnversioned(hide);
        setUnversionedHidden(hide);
    });
    connect(btnBox, &QDialogButtonBox::accepted, this, &SvnCommitDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // --- Right-click context menu on tree items ---
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                QTreeWidgetItem *item = m_tree->itemAt(pos);
                if (!item) return;
                const bool isDir         = item->data(0, Qt::UserRole + 2).toBool();
                const bool isUnversioned = item->data(0, Qt::UserRole + 1).toBool();
                const auto status = static_cast<SvnFileStatus>(
                    item->data(0, Qt::UserRole + 3).toInt());
                const bool isMissing = (status == SvnFileStatus::Missing);
                const QString path = item->data(0, Qt::UserRole).toString();

                // Right-clicking a row that is part of a multi-selection acts on
                // the whole selection; right-clicking outside it acts on that one
                // row (standard file-manager behaviour). Collect the missing rows
                // the delete action should schedule for removal.
                const auto selected = m_tree->selectedItems();
                const bool useSelection = selected.contains(item);
                QStringList missingPaths;
                if (useSelection) {
                    for (QTreeWidgetItem *sel : selected) {
                        if (static_cast<SvnFileStatus>(
                                sel->data(0, Qt::UserRole + 3).toInt())
                            == SvnFileStatus::Missing) {
                            const QString p = sel->data(0, Qt::UserRole).toString();
                            if (!p.isEmpty())
                                missingPaths << p;
                        }
                    }
                } else if (isMissing && !path.isEmpty()) {
                    missingPaths << path;
                }
                missingPaths.removeDuplicates();

                // Unversioned rows are driven by their check box (checking one
                // schedules an add on commit), so they carry no context menu.
                if (isUnversioned) return;
                // A directory carries no per-item actions unless it is missing:
                // a missing directory can still be scheduled for deletion.
                if (isDir && !isMissing) return;

                QMenu menu(this);

                // A missing item has no working copy content, so the diff would
                // compare against nothing — offer it only for present files.
                if (!isDir && !isMissing) {
                    auto *diffBaseAct = menu.addAction(
                        QIcon::fromTheme(QStringLiteral("vcs-diff")),
                        i18n("Diff with BASE"));
                    connect(diffBaseAct, &QAction::triggered, this, [this, path]() {
                        const QString tool = SvnDiffWindow::findDiffTool();
                        if (tool.isEmpty()) return;
                        SvnDiffWindow::launchFileDiff(tool, path, m_mgr, this,
                                                      QStringLiteral("BASE"));
                    });
                    auto *diffHeadAct = menu.addAction(
                        QIcon::fromTheme(QStringLiteral("vcs-diff")),
                        i18n("Diff with HEAD"));
                    connect(diffHeadAct, &QAction::triggered, this, [this, path]() {
                        const QString tool = SvnDiffWindow::findDiffTool();
                        if (tool.isEmpty()) return;
                        SvnDiffWindow::launchFileDiff(tool, path, m_mgr, this,
                                                      QStringLiteral("HEAD"));
                    });
                    menu.addSeparator();
                }

                auto *revertAct = menu.addAction(
                    QIcon::fromTheme(QStringLiteral("edit-undo")),
                    i18n("Revert..."));
                connect(revertAct, &QAction::triggered, this, [this, path]() {
                    const auto ret = QMessageBox::warning(
                        this, i18n("SVN: Revert"),
                        i18n("Revert all local changes of\n%1?\n\n"
                             "This cannot be undone.",
                             QFileInfo(path).fileName()),
                        QMessageBox::Yes | QMessageBox::Cancel);
                    if (ret != QMessageBox::Yes)
                        return;
                    // Rebuild the file list on completion (once).
                    connect(m_mgr, &SvnManager::operationFinished, this,
                            [this](const SvnOperationResult &) { refresh(); },
                            static_cast<Qt::ConnectionType>(
                                Qt::SingleShotConnection | Qt::QueuedConnection));
                    m_mgr->revertAsync({path}, QStringLiteral("infinity"));
                });

                // A missing item (file or directory) can be recorded as a
                // deletion ('svn delete'), which turns it into a committable
                // "Deleted" entry. This mirrors how checking an unversioned row
                // schedules an add before the commit.
                if (isMissing && !missingPaths.isEmpty()) {
                    const int count = missingPaths.size();
                    auto *deleteAct = menu.addAction(
                        QIcon::fromTheme(QStringLiteral("vcs-removed")),
                        count > 1
                            ? i18n("Delete %1 items (schedule removal)", count)
                            : i18n("Delete (schedule removal)"));
                    connect(deleteAct, &QAction::triggered, this,
                            [this, missingPaths]() {
                        const QString what = missingPaths.size() == 1
                            ? QFileInfo(missingPaths.first()).fileName()
                            : i18np("%1 item", "%1 items", missingPaths.size());
                        const auto ret = QMessageBox::warning(
                            this, i18n("SVN: Delete"),
                            i18n("Schedule\n%1\nfor deletion so it is removed "
                                 "from the repository on the next commit?", what),
                            QMessageBox::Yes | QMessageBox::Cancel);
                        if (ret != QMessageBox::Yes)
                            return;
                        // Rebuild the file list on completion (once).
                        connect(m_mgr, &SvnManager::operationFinished, this,
                                [this](const SvnOperationResult &) { refresh(); },
                                static_cast<Qt::ConnectionType>(
                                    Qt::SingleShotConnection | Qt::QueuedConnection));
                        m_mgr->removeAsync(missingPaths, /*keepLocal=*/false);
                    });
                }

                menu.exec(m_tree->viewport()->mapToGlobal(pos));
            });

    // --- Double-click on versioned file → Diff with BASE ---
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
                if (!item) return;
                const bool isDir         = item->data(0, Qt::UserRole + 2).toBool();
                const bool isUnversioned = item->data(0, Qt::UserRole + 1).toBool();
                const auto status = static_cast<SvnFileStatus>(
                    item->data(0, Qt::UserRole + 3).toInt());
                // A missing file is absent on disk, so there is nothing to diff.
                if (isDir || isUnversioned || status == SvnFileStatus::Missing)
                    return;
                const QString path = item->data(0, Qt::UserRole).toString();
                const QString tool = SvnDiffWindow::findDiffTool();
                if (tool.isEmpty()) return;
                SvnDiffWindow::launchFileDiff(tool, path, m_mgr, this,
                                              QStringLiteral("BASE"));
            });

    // --- F5 refresh shortcut ---
    auto *f5 = new QShortcut(Qt::Key_F5, this);
    connect(f5, &QShortcut::activated, this, &SvnCommitDialog::refresh);

    // Apply the "hide unversioned" setting to the initial view too, so it matches
    // what refresh() later does — otherwise the first render would show
    // unversioned rows the setting asks to hide, and the first refresh would make
    // them (and any folder hierarchy they carry) disappear unexpectedly.
    setUnversionedHidden(m_hideUnversioned->isChecked());

    updateSelectionLabel();
}

// ---------------------------------------------------------------------------
// Tree population
// ---------------------------------------------------------------------------

void SvnCommitDialog::populateTree(const QStringList &paths, SvnManager *mgr)
{
    // Block itemChanged handling for the whole (re)population so the check-state
    // cascade never runs on a half-built tree.
    m_block = true;

    // One row per changed item. isReal distinguishes real entries from the pure
    // grouping folders created below; collapsed marks an over-threshold
    // unversioned directory whose contents are not listed.
    struct Row {
        QString       abs;
        bool          isUnversioned = false;
        SvnFileStatus status        = SvnFileStatus::Unknown;
        bool          isDir         = false;
        bool          collapsed     = false;
        bool          propOnlyMod   = false;
    };
    QList<Row> rows;
    QSet<QString> seen;
    QList<QString> unversionedDirs;
    bool anyStatusFailed = false;

    auto addRow = [&](const Row &r) {
        if (seen.contains(r.abs)) return;
        seen.insert(r.abs);
        rows << r;
    };

    // 1. Collect committable and unversioned entries from svn status.
    for (const QString &p : paths) {
        const QFileInfo fi(p);
        bool ok = false;
        const auto statusEntries = mgr->status(fi.absoluteFilePath(), fi.isDir(), &ok);
        if (!ok) anyStatusFailed = true;
        for (const SvnStatusEntry &e : statusEntries) {
            const QString abs = QFileInfo(e.path).absoluteFilePath();
            const bool committable = isCommittable(e.textStatus)
                                      || e.propStatus == SvnFileStatus::Modified;
            if (committable) {
                Row r;
                r.abs         = abs;
                r.status      = e.textStatus;
                r.isDir       = QFileInfo(abs).isDir();
                r.propOnlyMod = !isCommittable(e.textStatus)
                                 && e.propStatus == SvnFileStatus::Modified;
                addRow(r);
            } else if (e.textStatus == SvnFileStatus::Unversioned) {
                Row r;
                r.abs           = abs;
                r.isUnversioned = true;
                r.status        = SvnFileStatus::Unversioned;
                r.isDir         = QFileInfo(abs).isDir();
                addRow(r);
                if (r.isDir)
                    unversionedDirs << abs;
            }
        }
    }

    // 2. svn status does not recurse into unversioned directories. List their
    //    contents, but collapse any directory larger than the cap to one row.
    for (const QString &dir : std::as_const(unversionedDirs)) {
        QStringList contents;
        if (SvnUi::collectUnversionedDirCapped(dir, contents, kUnversionedDirCap)) {
            for (const QString &c : std::as_const(contents)) {
                const QString abs = QFileInfo(c).absoluteFilePath();
                Row r;
                r.abs           = abs;
                r.isUnversioned = true;
                r.status        = SvnFileStatus::Unversioned;
                r.isDir         = QFileInfo(abs).isDir();
                addRow(r);
            }
        } else {
            for (Row &r : rows) {
                if (r.abs == dir) { r.collapsed = true; break; }
            }
        }
    }

    if (rows.isEmpty()) {
        m_tree->setEnabled(false);
        auto *placeholder = new QTreeWidgetItem(m_tree);
        if (anyStatusFailed) {
            placeholder->setText(0, i18n("Failed to retrieve SVN status. "
                                         "Check that svn is installed and this is a valid working copy."));
            placeholder->setIcon(0, QIcon::fromTheme(QStringLiteral("dialog-warning")));
        } else {
            placeholder->setText(0, i18n("No pending changes found."));
        }
        placeholder->setFlags(Qt::NoItemFlags);
        m_block = false;
        updateSelectionLabel();
        return;
    }

    // 3. Build the folder tree structure (KF6-free, unit-tested in tst_committree)
    //    and keep the rich row per path for the decoration pass below.
    QList<SvnUi::CommitRow> treeRows;
    QHash<QString, Row> rowByPath;
    for (const Row &r : std::as_const(rows)) {
        treeRows.append({r.abs, r.isUnversioned, static_cast<int>(r.status),
                         r.isDir, r.collapsed});
        rowByPath.insert(r.abs, r);
    }
    const QString displayRoot = SvnUi::commonAncestorDir(paths);
    SvnUi::buildCommitTree(m_tree, treeRows, displayRoot);

    // 4. Decorate the items: status text, icons and the dim italic font for
    //    unversioned rows. Grouping folders (UserRole+4 == false) only get a
    //    folder icon. This is the KF6/backend-dependent part kept out of the
    //    testable structure builder.
    QFileIconProvider iconProvider;
    const QColor dimColor =
        m_tree->palette().color(QPalette::Disabled, QPalette::Text);
    std::function<void(QTreeWidgetItem *)> decorate = [&](QTreeWidgetItem *it) {
        const bool isDir  = it->data(0, Qt::UserRole + 2).toBool();
        const bool isReal = it->data(0, Qt::UserRole + 4).toBool();
        if (!isReal) {
            it->setIcon(0, SvnUi::makeFolderIcon());
        } else {
            const QString path = it->data(0, Qt::UserRole).toString();
            const Row r = rowByPath.value(path);
            const QFileInfo fi(path);
            const QString ext = isDir ? QString()
                : (fi.suffix().isEmpty() ? QString()
                   : QLatin1Char('.') + fi.suffix());
            const QString statusStr = r.propOnlyMod
                ? i18n("Prop. Modified")
                : (r.collapsed ? i18n("Unversioned (folder)")
                               : statusText(r.status));
            const QIcon statusIconValue = r.propOnlyMod
                ? QIcon::fromTheme(QStringLiteral("vcs-locally-modified"))
                : statusIcon(r.status);
            it->setIcon(0, isDir ? SvnUi::makeFolderIcon()
                                 : iconProvider.icon(fi));
            it->setText(1, ext);
            it->setText(2, statusStr);
            if (!statusIconValue.isNull())
                it->setIcon(2, statusIconValue);
            if (r.isUnversioned) {
                QFont f = it->font(0);
                f.setItalic(true);
                for (int col = 0; col < 3; ++col) {
                    it->setForeground(col, dimColor);
                    it->setFont(col, f);
                }
            }
        }
        for (int i = 0; i < it->childCount(); ++i)
            decorate(it->child(i));
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        decorate(m_tree->topLevelItem(i));

    m_tree->expandAll();
    m_tree->resizeColumnToContents(2);
    m_tree->resizeColumnToContents(1);
    m_tree->resizeColumnToContents(0);

    m_block = false;
    updateSelectionLabel();
}

// ---------------------------------------------------------------------------
// Message history
// ---------------------------------------------------------------------------

QString SvnCommitDialog::historyKey() const
{
    // History per REPOSITORY (not per WC): two working copies of the same repo,
    // or the same WC after a switch, share the messages.
    // Base64Url, since path slashes would create QSettings groups.
    const QString repoRoot = m_paths.isEmpty()
        ? QString() : m_mgr->info(m_paths.first()).repositoryRoot;
    if (repoRoot.isEmpty())
        return QStringLiteral("messages"); // Fallback: globale Liste
    return QStringLiteral("messages/")
        + QString::fromLatin1(repoRoot.toUtf8().toBase64(
              QByteArray::Base64UrlEncoding));
}

void SvnCommitDialog::loadMessageHistory()
{
    if (m_historyKey.isEmpty())
        m_historyKey = historyKey();
    QSettings settings(QStringLiteral("DolphinSvnPlugin"),
                       QStringLiteral("CommitHistory"));
    QStringList history = settings.value(m_historyKey).toStringList();
    // Migration: ONLY on the very first access of a repo (the key does not
    // exist yet) adopt the previous global list as the starting set.
    // Check contains() instead of isEmpty() — otherwise a deliberately cleared
    // history (clearMessageHistory) would be re-imported immediately.
    if (!settings.contains(m_historyKey)
        && m_historyKey != QLatin1String("messages")) {
        const QStringList global = SvnUi::loadHistory(
            QStringLiteral("CommitHistory"), QStringLiteral("messages"));
        if (!global.isEmpty()) {
            settings.setValue(m_historyKey, global);
            history = global;
        }
    }

    m_historyCombo->clear();
    m_historyCombo->addItem(i18n("(Select a recent message...)"));
    for (const QString &msg : history) {
        const QString preview = msg.section(QLatin1Char('\n'), 0, 0).left(60);
        m_historyCombo->addItem(preview);
        m_historyCombo->setItemData(m_historyCombo->count() - 1, msg, Qt::UserRole);
    }
    m_historyCombo->setEnabled(history.size() > 0);
}

void SvnCommitDialog::saveMessageToHistory(const QString &msg)
{
    const QString trimmed = msg.trimmed();
    if (trimmed.isEmpty()) return;
    if (m_historyKey.isEmpty())
        m_historyKey = historyKey();
    SvnUi::pushHistory(QStringLiteral("CommitHistory"), trimmed, m_historyKey);
}

void SvnCommitDialog::clearMessageHistory()
{
    if (m_historyKey.isEmpty())
        m_historyKey = historyKey();
    const QStringList history =
        SvnUi::loadHistory(QStringLiteral("CommitHistory"), m_historyKey);
    if (history.isEmpty()) {
        QMessageBox::information(this, i18n("Clear Recent Messages"),
            i18n("There are no recent messages to clear."));
        return;
    }
    const auto answer = QMessageBox::question(this,
        i18n("Clear Recent Messages"),
        i18np("Delete the recent commit message stored for this repository?",
              "Delete all %1 recent commit messages stored for this "
              "repository?", history.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    QSettings settings(QStringLiteral("DolphinSvnPlugin"),
                       QStringLiteral("CommitHistory"));
    // Write an empty list as a marker (not remove) — this keeps the key present
    // and loadMessageHistory() does NOT re-import the global
    // Migrations-Liste.
    settings.setValue(m_historyKey, QStringList());
    settings.sync();

    loadMessageHistory();  // rebuilds the combo (only "(Select...)" left)
    // The "(Merge)" template is not history → re-append it after the rebuild so
    // it is preserved.
    if (!m_mergeMessage.isEmpty())
        setMergeMessage(m_mergeMessage);
}

// ---------------------------------------------------------------------------
// Checkbox propagation (unversioned items only — they need svn add before commit)
// ---------------------------------------------------------------------------

void SvnCommitDialog::onItemChanged(QTreeWidgetItem *item, int col)
{
    if (col != 0 || m_block) return;
    m_block = true;
    SvnUi::cascadeItemCheckState(item);
    m_block = false;
    updateSelectionLabel();
}

// ---------------------------------------------------------------------------
// Selection label
// ---------------------------------------------------------------------------

void SvnCommitDialog::updateSelectionLabel()
{
    if (!m_tree->isEnabled()) {
        m_selectionLabel->setText(QString());
        if (m_commitBtn) m_commitBtn->setEnabled(false);
        return;
    }

    // Count only real entries (UserRole+4): the grouping folders are containers,
    // not something the user commits, so they do not belong in "N of M".
    int total = 0, selected = 0;
    QList<QTreeWidgetItem *> stack;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        stack.push_back(m_tree->topLevelItem(i));
    while (!stack.isEmpty()) {
        QTreeWidgetItem *item = stack.takeFirst();
        if (!item->isHidden() && item->data(0, Qt::UserRole + 4).toBool()) {
            ++total;
            if (item->checkState(0) == Qt::Checked) ++selected;
        }
        for (int i = 0; i < item->childCount(); ++i)
            stack.push_back(item->child(i));
    }
    m_selectionLabel->setText(i18n("%1 of %2 selected", selected, total));
    if (m_commitBtn) m_commitBtn->setEnabled(selected > 0);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

QString SvnCommitDialog::message() const
{
    return m_messageEdit->toPlainText().trimmed();
}

void SvnCommitDialog::setMergeMessage(const QString &msg)
{
    if (msg.trimmed().isEmpty())
        return;
    m_mergeMessage = msg;  // remember it so clearMessageHistory() keeps it
    const QString preview = i18n("(Merge) %1",
        msg.section(QLatin1Char('\n'), 0, 0).left(60));
    m_historyCombo->insertItem(1, preview);
    m_historyCombo->setItemData(1, msg, Qt::UserRole);
    QFont f = m_historyCombo->font();
    f.setItalic(true);
    m_historyCombo->setItemData(1, f, Qt::FontRole);
    m_historyCombo->setEnabled(true);
}

QStringList SvnCommitDialog::selectedPaths() const
{
    // 'svn commit --depth empty <paths...>' commits exactly the listed items.
    // Names every checked real entry (UserRole+4) plus any partially checked
    // new-parent folder; pure grouping folders are skipped, and a checked
    // collapsed unversioned directory (UserRole+5) is expanded to its full
    // content list so its files are committed too.
    QStringList result;
    std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *it) {
        if (!it->isHidden() && it->data(0, Qt::UserRole + 4).toBool()
            && SvnUi::includeForDepthEmpty(it->checkState(0),
                                           it->data(0, Qt::UserRole + 2).toBool())) {
            const QString path = it->data(0, Qt::UserRole).toString();
            if (!path.isEmpty()) {
                result << path;
                if (it->data(0, Qt::UserRole + 5).toBool())
                    result << SvnUi::descendantPathsSorted(path);
            }
        }
        for (int i = 0; i < it->childCount(); ++i)
            walk(it->child(i));
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        walk(m_tree->topLevelItem(i));
    return result;
}

bool SvnCommitDialog::keepLocks() const
{
    return m_keepLocks->isChecked();
}

QStringList SvnCommitDialog::unversionedPaths() const
{
    // These are added with 'svn add --depth empty <paths...>' before the commit.
    // Only unversioned real rows (UserRole+1 && UserRole+4), same include rule as
    // the commit list so a partially checked unversioned directory comes along as
    // the new parent its checked children need. A checked collapsed directory is
    // expanded (parents before children) so 'svn add --depth empty' adds all of
    // it in the correct order.
    QStringList result;
    std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *it) {
        if (!it->isHidden() && it->data(0, Qt::UserRole + 4).toBool()
            && it->data(0, Qt::UserRole + 1).toBool()
            && SvnUi::includeForDepthEmpty(it->checkState(0),
                                           it->data(0, Qt::UserRole + 2).toBool())) {
            const QString path = it->data(0, Qt::UserRole).toString();
            if (!path.isEmpty()) {
                result << path;
                if (it->data(0, Qt::UserRole + 5).toBool())
                    result << SvnUi::descendantPathsSorted(path);
            }
        }
        for (int i = 0; i < it->childCount(); ++i)
            walk(it->child(i));
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        walk(m_tree->topLevelItem(i));
    return result;
}

void SvnCommitDialog::accept()
{
    saveMessageToHistory(message());
    QDialog::accept();
}

// ---------------------------------------------------------------------------
// F5 refresh
// ---------------------------------------------------------------------------

void SvnCommitDialog::setUnversionedHidden(bool hide)
{
    // Nothing to filter when the tree only holds the "no changes"/"error"
    // placeholder (the tree is disabled in that case).
    if (!m_tree->isEnabled())
        return;

    m_block = true;
    // Recurse bottom-up and return whether the item stays visible. An unversioned
    // real entry follows the setting; a versioned real entry is never hidden by
    // it; a grouping folder is hidden only when every descendant is hidden, so no
    // empty folder shells are left behind when unversioned items disappear.
    std::function<bool(QTreeWidgetItem *)> apply = [&](QTreeWidgetItem *it) -> bool {
        bool anyChildVisible = false;
        for (int i = 0; i < it->childCount(); ++i)
            anyChildVisible |= apply(it->child(i));

        const bool isReal = it->data(0, Qt::UserRole + 4).toBool();
        const bool isUnv  = it->data(0, Qt::UserRole + 1).toBool();
        bool hidden;
        if (!isReal)
            hidden = !anyChildVisible;   // grouping folder
        else if (isUnv)
            hidden = hide;               // unversioned entry follows the setting
        else
            hidden = false;              // versioned change stays visible
        it->setHidden(hidden);
        return !hidden;
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        apply(m_tree->topLevelItem(i));
    m_block = false;
    updateSelectionLabel();
}

void SvnCommitDialog::refresh()
{
    // Remember the user's current selection by path so a refresh (e.g. after
    // deleting a missing file, or F5) does not throw away check marks the user
    // set on other rows. The snapshot/restore pair lives in svncommittree so it
    // can be unit-tested (tst_committree).
    const QHash<QString, Qt::CheckState> saved = SvnUi::snapshotCheckStates(m_tree);

    m_tree->clear();
    m_tree->setEnabled(true);
    populateTree(m_paths, m_mgr);

    if (m_tree->isEnabled()) {
        m_block = true;
        SvnUi::restoreCheckStates(m_tree, saved);
        m_block = false;
    }

    setUnversionedHidden(m_hideUnversioned->isChecked());
}
