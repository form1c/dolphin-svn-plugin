#include "svnlogdialog.h"
#include "svndiffwindow.h"
#include "svnmanager.h"
#include "svnsettings.h"
#include "svnuihelpers.h"

#include <KLocalizedString>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QShortcut>

#include <algorithm>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTextEdit>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

SvnLogDialog::SvnLogDialog(const QString &path, SvnManager *mgr, QWidget *parent)
    : QDialog(parent), m_path(path), m_mgr(mgr)
{
    setWindowTitle(i18n("Log Messages – %1", QFileInfo(path).fileName()));
    resize(920, 650);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    auto *pathLabel = new QLabel(i18n("Log of: <b>%1</b>", path), this);
    pathLabel->setWordWrap(true);
    mainLayout->addWidget(pathLabel);

    // --- Search / filter bar ---
    auto *searchBar = new QWidget(this);
    auto *searchLayout = new QHBoxLayout(searchBar);
    searchLayout->setContentsMargins(0, 0, 0, 4);

    auto *searchIconLabel = new QLabel(searchBar);
    searchIconLabel->setPixmap(
        QIcon::fromTheme(QStringLiteral("edit-find")).pixmap(16, 16));
    searchLayout->addWidget(searchIconLabel);

    m_searchEdit = new QLineEdit(searchBar);
    m_searchEdit->setPlaceholderText(i18n("Search in revision, author, message…"));
    m_searchEdit->setClearButtonEnabled(true);
    searchLayout->addWidget(m_searchEdit, 1);

    searchLayout->addWidget(new QLabel(i18n("Author:"), searchBar));

    m_authorCombo = new QComboBox(searchBar);
    m_authorCombo->addItem(i18n("All Authors"), QString());
    m_authorCombo->setMinimumWidth(120);
    searchLayout->addWidget(m_authorCombo);

    auto *clearFiltersBtn = new QPushButton(
        QIcon::fromTheme(QStringLiteral("edit-clear-all")), QString(), searchBar);
    clearFiltersBtn->setFlat(true);
    clearFiltersBtn->setFixedSize(24, 24);
    clearFiltersBtn->setToolTip(i18n("Clear all filters"));
    searchLayout->addWidget(clearFiltersBtn);

    mainLayout->addWidget(searchBar);

    // Outer splitter: commit list (top) | details panel (bottom)
    auto *mainSplit = new QSplitter(Qt::Vertical, this);
    mainLayout->addWidget(mainSplit, 1);

    // --- Commit list ---
    m_logTree = new QTreeWidget(mainSplit);
    m_logTree->setColumnCount(4);
    m_logTree->setHeaderLabels({i18n("Revision"), i18n("Author"), i18n("Date"), i18n("Message")});
    m_logTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_logTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_logTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_logTree->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_logTree->setRootIsDecorated(false);
    m_logTree->setAlternatingRowColors(true);
    m_logTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_logTree->setSortingEnabled(true);
    m_logTree->sortByColumn(0, Qt::DescendingOrder);

    // --- Details panel ---
    auto *detailWidget = new QWidget(mainSplit);
    auto *detailLayout = new QVBoxLayout(detailWidget);
    detailLayout->setContentsMargins(0, 4, 0, 0);

    // Inner splitter: commit message (left) | changed files (right)
    auto *innerSplit = new QSplitter(Qt::Horizontal, detailWidget);
    detailLayout->addWidget(innerSplit, 1);

    auto *msgWidget = new QWidget(innerSplit);
    auto *msgLayout = new QVBoxLayout(msgWidget);
    msgLayout->setContentsMargins(0, 0, 4, 0);
    msgLayout->addWidget(new QLabel(i18n("Log message:"), msgWidget));
    m_msgView = new QTextEdit(msgWidget);
    m_msgView->setReadOnly(true);
    msgLayout->addWidget(m_msgView, 1);
    innerSplit->addWidget(msgWidget);

    auto *pathWidget = new QWidget(innerSplit);
    auto *pathLayout = new QVBoxLayout(pathWidget);
    pathLayout->setContentsMargins(4, 0, 0, 0);
    pathLayout->addWidget(new QLabel(i18n("Changed paths:"), pathWidget));
    m_pathsTree = new QTreeWidget(pathWidget);
    m_pathsTree->setColumnCount(2);
    m_pathsTree->setHeaderLabels({i18n("Action"), i18n("Path")});
    m_pathsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_pathsTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_pathsTree->setRootIsDecorated(false);
    m_pathsTree->setAlternatingRowColors(true);
    pathLayout->addWidget(m_pathsTree, 1);
    innerSplit->addWidget(pathWidget);

    innerSplit->setStretchFactor(0, 1);
    innerSplit->setStretchFactor(1, 2);

    mainSplit->setStretchFactor(0, 2);
    mainSplit->setStretchFactor(1, 1);

    // --- Button row ---
    auto *btnRow = new QHBoxLayout;
    m_moreBtn = new QPushButton(i18n("Load more..."), this);
    m_moreBtn->setEnabled(false);
    btnRow->addWidget(m_moreBtn);

    m_compareBtn = new QPushButton(
        QIcon::fromTheme(QStringLiteral("vcs-diff")),
        i18n("Compare Revisions"), this);
    m_compareBtn->setEnabled(false);
    m_compareBtn->setToolTip(i18n("Select exactly two revisions to compare them"));
    btnRow->addWidget(m_compareBtn);

    btnRow->addSpacing(12);
    const SvnUi::LogOptionsBoxes logOpts = SvnUi::createLogOptionsRow(
        this, btnRow,
        [this]() { reloadLog(); },
        [this]() { showSelection(); });
    m_includeMergedBox = logOpts.includeMerged;
    m_onlyAffectedBox  = logOpts.onlyAffectedPaths;

    btnRow->addStretch();
    auto *closeBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    btnRow->addWidget(closeBox);
    mainLayout->addLayout(btnRow);

    connect(m_logTree, &QTreeWidget::itemSelectionChanged,
            this, &SvnLogDialog::showSelection);
    connect(m_moreBtn,    &QPushButton::clicked, this, &SvnLogDialog::loadMore);
    connect(m_compareBtn, &QPushButton::clicked, this, &SvnLogDialog::compareSelectedRevisions);
    connect(closeBox,     &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_logTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_logTree, &QTreeWidget::customContextMenuRequested,
            this, &SvnLogDialog::showContextMenu);

    // Double-click a changed path to diff that file at the selected revision.
    connect(m_pathsTree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) { diffChangedPath(item); });

    // Same diff also via right-click → "Diff" in the changed-paths table.
    m_pathsTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_pathsTree, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                QTreeWidgetItem *item = m_pathsTree->itemAt(pos);
                if (!item) return;
                const long long rev = item->data(0, Qt::UserRole).toLongLong();

                QMenu menu(this);
                QAction *diffAct = menu.addAction(
                    QIcon::fromTheme(QStringLiteral("vcs-diff")),
                    i18n("Diff r%1 → r%2", rev - 1, rev));
                diffAct->setEnabled(rev > 1 && !item->text(1).isEmpty());
                connect(diffAct, &QAction::triggered, this,
                        [this, item]() { diffChangedPath(item); });
                menu.exec(m_pathsTree->viewport()->mapToGlobal(pos));
            });

    // Search bar signals
    connect(m_searchEdit,  &QLineEdit::textChanged,
            this, &SvnLogDialog::applyFilter);
    connect(m_authorCombo, &QComboBox::currentIndexChanged,
            this, &SvnLogDialog::applyFilter);
    connect(clearFiltersBtn, &QPushButton::clicked, this, [this]() {
        m_searchEdit->clear();
        m_authorCombo->setCurrentIndex(0);
        applyFilter();
    });

    // Ctrl+F: focus the search field
    auto *ctrlF = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), this);
    connect(ctrlF, &QShortcut::activated, this, [this]() {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    });

    // Escape on the search field: clear filters
    auto *escShortcut = new QShortcut(Qt::Key_Escape, m_searchEdit);
    escShortcut->setContext(Qt::WidgetShortcut);
    connect(escShortcut, &QShortcut::activated, clearFiltersBtn, &QPushButton::click);

    loadMore();
}

// ---------------------------------------------------------------------------
// Load a page of log entries
// ---------------------------------------------------------------------------

// Full rebuild (stop-on-copy / include-merged changed).
void SvnLogDialog::reloadLog()
{
    m_logTree->clear();
    m_entries.clear();
    m_oldestRevision = -1;
    m_msgView->clear();
    m_pathsTree->clear();
    loadMore();
}

void SvnLogDialog::loadMore()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_moreBtn->setEnabled(false);

    // Always use HEAD as the upper bound on the first load so newly committed
    // revisions appear even before the working copy is updated (svn log without
    // an explicit -r uses the WC BASE revision, which misses post-commit entries).
    const QString fromRev = (m_oldestRevision > 1)
        ? QString::number(m_oldestRevision - 1) : QStringLiteral("HEAD");
    const QString toRev = QStringLiteral("1");

    SvnLogOptions opts;
    opts.stopOnCopy    = SvnSettings::logStopOnCopy();
    opts.includeMerged = SvnSettings::logIncludeMerged();
    const auto newEntries = m_mgr->log(m_path, kPageSize, fromRev, toRev,
                                       opts);
    QApplication::restoreOverrideCursor();

    if (newEntries.isEmpty() && m_entries.isEmpty()) {
        auto *placeholder = new QTreeWidgetItem(m_logTree);
        placeholder->setText(0, i18n("No log available."));
        placeholder->setFlags(Qt::NoItemFlags);
        return;
    }

    m_logTree->setRootIsDecorated(opts.includeMerged);
    m_logTree->setSortingEnabled(false); // suspend sorting during batch insert
    int newTopLevel = 0;
    QList<QTreeWidgetItem *> parentAtDepth; // [d] = last entry at depth d
    for (const SvnLogEntry &e : newEntries) {
        const int idx = m_entries.size();
        m_entries.append(e);
        const long long rev = e.revision.toLongLong();

        // Merged revisions (-g) as children of their merge revision —
        // the numeric sort only orders siblings, the group
        // stays together.
        SvnRevisionItem *item = nullptr;
        if (e.mergeDepth > 0 && e.mergeDepth <= parentAtDepth.size()) {
            item = new SvnRevisionItem(parentAtDepth[e.mergeDepth - 1]);
            const QColor gray =
                palette().color(QPalette::Disabled, QPalette::Text);
            QFont italic = item->font(0);
            italic.setItalic(true);
            for (int c = 0; c < 4; ++c) {
                item->setForeground(c, gray);
                item->setFont(c, italic);
            }
            item->setToolTip(0, i18n("Merged via r%1",
                parentAtDepth[e.mergeDepth - 1]->text(0)));
        } else {
            item = new SvnRevisionItem(m_logTree);
            ++newTopLevel;
        }
        parentAtDepth.resize(e.mergeDepth);
        parentAtDepth.append(item);

        item->setData(0, Qt::UserRole,     rev);  // sort key
        item->setData(0, Qt::UserRole + 1, idx);  // entry index
        item->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(0, e.revision);
        item->setText(1, e.author);
        item->setText(2, formatDate(e.date));
        // Show only the first line of the message as a preview.
        const int nl = e.message.indexOf(QLatin1Char('\n'));
        item->setText(3, nl >= 0 ? e.message.left(nl).trimmed() : e.message.trimmed());
        item->setToolTip(3, e.message.trimmed());

        // Paging cursor only from top-level entries (with -g, --limit counts
        // only the outer logentries — verified in the scratchpad).
        if (e.mergeDepth == 0 && rev > 0
            && (m_oldestRevision < 0 || rev < m_oldestRevision))
            m_oldestRevision = rev;
    }
    m_logTree->setSortingEnabled(true);
    if (opts.includeMerged)
        m_logTree->expandAll();

    // Enable "load more" only if there may be older entries.
    m_moreBtn->setEnabled(newTopLevel == kPageSize && m_oldestRevision > 1);

    updateAuthorCombo();
    applyFilter();

    // Auto-select first row on initial load.
    if (m_logTree->currentItem() == nullptr && m_logTree->topLevelItemCount() > 0)
        m_logTree->setCurrentItem(m_logTree->topLevelItem(0));
}

// ---------------------------------------------------------------------------
// Show details for all selected entries (multi-selection support)
// ---------------------------------------------------------------------------

void SvnLogDialog::showSelection()
{
    // Collect selected items sorted by their visual position (newest = top = first).
    QList<QTreeWidgetItem *> selected = m_logTree->selectedItems();

    updateCompareButton();

    if (selected.isEmpty()) {
        m_msgView->clear();
        m_pathsTree->clear();
        return;
    }
    std::sort(selected.begin(), selected.end(), [this](QTreeWidgetItem *a, QTreeWidgetItem *b) {
        return m_logTree->indexOfTopLevelItem(a) < m_logTree->indexOfTopLevelItem(b);
    });

    // Build combined commit message in requested format.
    static const QString kSeparator = QStringLiteral("-----------------------");
    QString combinedMsg;
    for (QTreeWidgetItem *treeItem : selected) {
        const int idx = treeItem->data(0, Qt::UserRole + 1).toInt();
        if (idx < 0 || idx >= m_entries.size()) continue;
        const SvnLogEntry &e = m_entries.at(idx);
        if (!combinedMsg.isEmpty())
            combinedMsg += QLatin1Char('\n');
        combinedMsg += QStringLiteral("r%1\n").arg(e.revision);
        combinedMsg += e.message.trimmed();
        combinedMsg += QLatin1Char('\n');
        combinedMsg += kSeparator;
        combinedMsg += QLatin1Char('\n');
    }
    m_msgView->setPlainText(combinedMsg.trimmed());

    // Revision for per-path diff — only meaningful when exactly one entry is selected.
    const long long diffRev = (selected.size() == 1)
        ? selected.first()->data(0, Qt::UserRole).toLongLong()
        : 0LL;

    // Merge changed paths from all selected entries.
    // Newest revision takes priority when the same path appears in multiple commits.
    QMap<QString, QString> pathToAction; // repository path → action letter
    for (QTreeWidgetItem *treeItem : selected) {
        const int idx = treeItem->data(0, Qt::UserRole + 1).toInt();
        if (idx < 0 || idx >= m_entries.size()) continue;
        for (const auto &[filePath, action] : m_entries.at(idx).changedPaths) {
            if (!pathToAction.contains(filePath))
                pathToAction.insert(filePath, action);
        }
    }

    // "Show only affected paths": filter to paths below the log target.
    // Resolve the repo-relative path lazily (a network round-trip only once).
    const bool onlyAffected = m_onlyAffectedBox
                              && m_onlyAffectedBox->isChecked();
    if (onlyAffected && !m_repoRelResolved) {
        m_repoRelResolved = true;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const SvnInfo info = m_mgr->info(m_path);
        QApplication::restoreOverrideCursor();
        if (info.valid && info.url.startsWith(info.repositoryRoot))
            m_repoRelPath = info.url.mid(info.repositoryRoot.length());
        // error/repo root → empty = no filter (show everything)
    }

    m_pathsTree->clear();
    for (auto it = pathToAction.cbegin(); it != pathToAction.cend(); ++it) {
        if (onlyAffected && !m_repoRelPath.isEmpty()
            && it.key() != m_repoRelPath
            && !it.key().startsWith(m_repoRelPath + QLatin1Char('/')))
            continue;
        auto *item = new QTreeWidgetItem(m_pathsTree);
        item->setText(0, actionLabel(it.value()));
        item->setText(1, it.key());
        item->setData(0, Qt::UserRole, diffRev);
        if (diffRev > 1)
            item->setToolTip(1, i18n("Double-click to show diff at r%1", diffRev));
        const QString icon = actionIconName(it.value());
        if (!icon.isEmpty())
            item->setIcon(0, QIcon::fromTheme(icon));
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QString SvnLogDialog::formatDate(const QString &isoDate)
{
    // SVN date: "2023-12-15T14:30:00.123456Z" — extract date + time directly.
    if (isoDate.length() < 19) return isoDate;
    return isoDate.left(10) + QLatin1Char(' ') + isoDate.mid(11, 5);
}

QString SvnLogDialog::actionLabel(const QString &action)
{
    if (action == QStringLiteral("A")) return i18n("Added");
    if (action == QStringLiteral("M")) return i18n("Modified");
    if (action == QStringLiteral("D")) return i18n("Deleted");
    if (action == QStringLiteral("R")) return i18n("Replaced");
    if (action == QStringLiteral("C")) return i18n("Copied");
    return action;
}

QString SvnLogDialog::actionIconName(const QString &action)
{
    if (action == QStringLiteral("A")) return QStringLiteral("vcs-added");
    if (action == QStringLiteral("M")) return QStringLiteral("vcs-locally-modified");
    if (action == QStringLiteral("D")) return QStringLiteral("vcs-removed");
    if (action == QStringLiteral("R")) return QStringLiteral("vcs-locally-modified");
    return {};
}

// ---------------------------------------------------------------------------
// Search / filter
// ---------------------------------------------------------------------------

void SvnLogDialog::updateAuthorCombo()
{
    // Collect authors already in the combo (indices 1+).
    QStringList existing;
    for (int i = 1; i < m_authorCombo->count(); ++i)
        existing << m_authorCombo->itemData(i).toString();

    // Find authors in m_entries not yet in the combo.
    QStringList added;
    for (const SvnLogEntry &e : std::as_const(m_entries)) {
        if (!e.author.isEmpty() && !existing.contains(e.author) && !added.contains(e.author))
            added << e.author;
    }
    std::sort(added.begin(), added.end());
    for (const QString &a : std::as_const(added))
        m_authorCombo->addItem(a, a);
}

void SvnLogDialog::applyFilter()
{
    const QString text   = m_searchEdit->text().trimmed();
    const QString author = m_authorCombo->currentData().toString();
    const bool noFilter  = text.isEmpty() && author.isEmpty();

    for (int i = 0; i < m_logTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_logTree->topLevelItem(i);

        // Skip non-selectable placeholder items.
        if (!(item->flags() & Qt::ItemIsEnabled)) {
            item->setHidden(false);
            continue;
        }

        if (noFilter) {
            item->setHidden(false);
            continue;
        }

        const bool authorMatch =
            author.isEmpty() || item->text(1) == author;

        bool textMatch = text.isEmpty();
        if (!textMatch) {
            const QString lo = text.toLower();
            textMatch = item->text(0).contains(lo, Qt::CaseInsensitive)    // revision
                     || item->text(1).contains(lo, Qt::CaseInsensitive)    // author
                     || item->text(3).contains(lo, Qt::CaseInsensitive)    // message preview
                     || item->toolTip(3).contains(lo, Qt::CaseInsensitive); // full message
        }

        item->setHidden(!(authorMatch && textMatch));
    }

    // Hiding a selected item changes what "Compare Revisions" would act on.
    updateCompareButton();
}

void SvnLogDialog::updateCompareButton()
{
    int visibleSelectedCount = 0;
    const QList<QTreeWidgetItem *> selected = m_logTree->selectedItems();
    for (QTreeWidgetItem *item : selected) {
        if (!item->isHidden() && (item->flags() & Qt::ItemIsEnabled))
            ++visibleSelectedCount;
    }
    m_compareBtn->setEnabled(visibleSelectedCount == 2);
}

// ---------------------------------------------------------------------------
// Context menu on log entries
// ---------------------------------------------------------------------------

void SvnLogDialog::showContextMenu(const QPoint &pos)
{
    const QList<QTreeWidgetItem *> sel = m_logTree->selectedItems();
    if (sel.isEmpty()) return;

    // Single selection with a valid revision for diff / update actions.
    long long singleRev = -1;
    if (sel.size() == 1) {
        const long long rv = sel.first()->data(0, Qt::UserRole).toLongLong();
        if (rv > 0)
            singleRev = rv;
    }

    QMenu menu(this);

    if (singleRev > 1) {
        connect(menu.addAction(QIcon::fromTheme(QStringLiteral("vcs-diff")),
                               i18n("Show Diff (r%1 → r%2)", singleRev - 1, singleRev)),
                &QAction::triggered, this, [this, singleRev]() {
                    showRevisionDiff(singleRev);
                });
    }

    if (singleRev > 0) {
        connect(menu.addAction(QIcon::fromTheme(QStringLiteral("vcs-update-cvs-cervisia")),
                               i18n("Update to r%1", singleRev)),
                &QAction::triggered, this, [this, singleRev]() {
                    runOperation(i18n("SVN: Update to r%1", singleRev), [this, singleRev]() {
                        m_mgr->updateAsync(m_path, QString::number(singleRev));
                    });
                });
    }

    if (singleRev > 0)
        menu.addSeparator();

    // "Copy Revision Number" is available for any single selection.
    if (singleRev > 0) {
        connect(menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")),
                               i18n("Copy Revision Number")),
                &QAction::triggered, this, [singleRev]() {
                    QGuiApplication::clipboard()->setText(QString::number(singleRev));
                });
    }

    if (!menu.isEmpty())
        menu.exec(m_logTree->viewport()->mapToGlobal(pos));
}

// Diff of a file from the changed-paths table (rN-1 → rN) — preferably in the
// external differ, otherwise unified diff text.
void SvnLogDialog::diffChangedPath(QTreeWidgetItem *item)
{
    if (!item)
        return;
    const long long rev = item->data(0, Qt::UserRole).toLongLong();
    if (rev <= 1)
        return; // revision 1 has no predecessor, or multiple selection

    const QString repoPath = item->text(1);
    if (repoPath.isEmpty())
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QString repoRoot = m_mgr->info(m_path).repositoryRoot;

    const QString tool = SvnDiffWindow::findDiffTool();
    if (!tool.isEmpty() && !repoRoot.isEmpty()
        && SvnDiffWindow::launchHistoricDiff(
               tool, repoRoot + repoPath, rev - 1, rev, m_mgr, this)) {
        QApplication::restoreOverrideCursor();
        return;
    }

    // Fallback (no tool or a directory): unified diff text.
    const QString diff = repoRoot.isEmpty() ? QString()
        : m_mgr->diffChangeSync(repoRoot + repoPath, QString::number(rev));
    QApplication::restoreOverrideCursor();

    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(i18n("Diff r%1 → r%2 – %3",
                             rev - 1, rev, QFileInfo(repoPath).fileName()));
    dlg->resize(900, 600);
    auto *layout = new QVBoxLayout(dlg);
    auto *pte = new QPlainTextEdit(dlg);
    pte->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pte->setReadOnly(true);
    pte->setPlainText(diff.isEmpty()
                      ? i18n("(No differences or diff unavailable)")
                      : diff);
    layout->addWidget(pte);
    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    connect(btnBox, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    layout->addWidget(btnBox);
    dlg->exec();
}

void SvnLogDialog::showRevisionDiff(long long rev)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);

    // Preferred in the external differ: files side by side; folders (a whole
    // revision, several files) as a folder comparison of two export states.
    const QString tool = SvnDiffWindow::findDiffTool();
    if (!tool.isEmpty()) {
        bool launched = false;
        if (!QFileInfo(m_path).isDir())
            launched = SvnDiffWindow::launchHistoricDiff(
                tool, m_path, rev - 1, rev, m_mgr, this);
        if (!launched)
            launched = SvnDiffWindow::launchHistoricFolderDiff(
                tool, m_path, rev - 1, rev, m_mgr, this);
        if (launched) {
            QApplication::restoreOverrideCursor();
            return;
        }
    }

    const QString diff = m_mgr->diffChangeSync(m_path, QString::number(rev));
    QApplication::restoreOverrideCursor();

    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(i18n("Diff r%1 → r%2 – %3",
                              rev - 1, rev, QFileInfo(m_path).fileName()));
    dlg->resize(900, 600);
    auto *layout = new QVBoxLayout(dlg);
    auto *pte = new QPlainTextEdit(dlg);
    pte->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pte->setReadOnly(true);
    pte->setPlainText(diff.isEmpty() ? i18n("(No differences or diff unavailable)") : diff);
    layout->addWidget(pte);
    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    connect(btnBox, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    layout->addWidget(btnBox);
    dlg->exec();
}

void SvnLogDialog::runOperation(const QString &title, std::function<void()> startOp)
{
    SvnUi::runWithProgress(m_mgr, title, this, startOp);
}

void SvnLogDialog::compareSelectedRevisions()
{
    QList<long long> revs;
    for (QTreeWidgetItem *item : m_logTree->selectedItems()) {
        if (item->isHidden() || !(item->flags() & Qt::ItemIsEnabled))
            continue;
        const long long rv = item->data(0, Qt::UserRole).toLongLong();
        if (rv > 0)
            revs << rv;
    }
    if (revs.size() != 2)
        return;

    const long long r1 = qMin(revs[0], revs[1]);
    const long long r2 = qMax(revs[0], revs[1]);

    QApplication::setOverrideCursor(Qt::WaitCursor);

    // Preferred in the external differ: files side by side; folders as a folder
    // comparison of two export states. Fallback: unified diff text.
    const QString tool = SvnDiffWindow::findDiffTool();
    if (!tool.isEmpty()) {
        bool launched = false;
        if (!QFileInfo(m_path).isDir())
            launched = SvnDiffWindow::launchHistoricDiff(
                tool, m_path, r1, r2, m_mgr, this);
        if (!launched)
            launched = SvnDiffWindow::launchHistoricFolderDiff(
                tool, m_path, r1, r2, m_mgr, this);
        if (launched) {
            QApplication::restoreOverrideCursor();
            return;
        }
    }

    const QString diff = m_mgr->diffSync(m_path,
                                          QString::number(r1),
                                          QString::number(r2));
    QApplication::restoreOverrideCursor();

    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(i18n("Diff r%1 → r%2 – %3",
                              r1, r2, QFileInfo(m_path).fileName()));
    dlg->resize(900, 600);
    auto *layout = new QVBoxLayout(dlg);
    auto *pte = new QPlainTextEdit(dlg);
    pte->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pte->setReadOnly(true);
    pte->setPlainText(diff.isEmpty() ? i18n("(No differences or diff unavailable)") : diff);
    layout->addWidget(pte);
    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    connect(btnBox, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    layout->addWidget(btnBox);
    dlg->exec();
}

void SvnLogDialog::scrollToRevision(long long rev)
{
    const auto findItem = [this, rev]() -> QTreeWidgetItem * {
        for (int i = 0; i < m_logTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = m_logTree->topLevelItem(i);
            if (item->data(0, Qt::UserRole).toLongLong() == rev)
                return item;
        }
        return nullptr;
    };

    QTreeWidgetItem *item = findItem();

    // The revision may lie beyond the loaded pages — fetch more (bounded).
    int pageGuard = 0;
    while (!item && m_moreBtn->isEnabled() && pageGuard++ < 10) {
        loadMore();
        item = findItem();
    }

    if (item) {
        m_logTree->setCurrentItem(item);
        m_logTree->scrollToItem(item, QAbstractItemView::PositionAtCenter);
    }
}
