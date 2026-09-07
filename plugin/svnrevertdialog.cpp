#include "svnrevertdialog.h"
#include "svnmanager.h"
#include "svnuihelpers.h"
#include "svntreecheck.h"

#include <KLocalizedString>

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QShortcut>
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static QString statusText(SvnFileStatus s)
{
    switch (s) {
    case SvnFileStatus::Modified:   return i18n("Modified");
    case SvnFileStatus::Added:      return i18n("Added");
    case SvnFileStatus::Deleted:    return i18n("Deleted");
    case SvnFileStatus::Replaced:   return i18n("Replaced");
    case SvnFileStatus::Conflicted: return i18n("Conflicted");
    case SvnFileStatus::Missing:    return i18n("Missing");
    case SvnFileStatus::External:   return i18n("External");
    default:                        return QString();
    }
}

static QString statusIconName(SvnFileStatus s)
{
    switch (s) {
    case SvnFileStatus::Modified:
    case SvnFileStatus::Replaced:   return QStringLiteral("vcs-locally-modified");
    case SvnFileStatus::Added:      return QStringLiteral("vcs-added");
    case SvnFileStatus::Deleted:
    case SvnFileStatus::Missing:    return QStringLiteral("vcs-removed");
    case SvnFileStatus::Conflicted: return QStringLiteral("vcs-conflicting");
    default:                        return QString();
    }
}

static bool isRevertable(SvnFileStatus s)
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

SvnRevertDialog::SvnRevertDialog(const QStringList &paths, SvnManager *mgr,
                                  QWidget *parent)
    : QDialog(parent)
{
    m_paths = paths;
    m_mgr   = mgr;

    setWindowTitle(i18n("Revert – %1", QFileInfo(paths.first()).fileName()));
    resize(680, 500);

    auto *layout = new QVBoxLayout(this);

    // --- Warning banner ---
    // Use theme-aware colors: dark-red tones for dark themes, light-red for light themes.
    const bool isDark = QApplication::palette().color(QPalette::Window).lightness() < 128;
    const QColor bgColor     = isDark ? QColor(0x5c, 0x10, 0x10) : QColor(0xff, 0xec, 0xec);
    const QColor borderColor = isDark ? QColor(0x9b, 0x1c, 0x1c) : QColor(0xe5, 0xa0, 0xa0);
    const QColor fgColor     = isDark ? QColor(0xf8, 0xd7, 0xda) : QColor(0x72, 0x1c, 0x24);

    auto *warnFrame = new QFrame(this);
    warnFrame->setFrameShape(QFrame::StyledPanel);
    warnFrame->setStyleSheet(
        QStringLiteral("QFrame { background-color: %1; border: 1px solid %2; "
                       "border-radius: 4px; padding: 4px; }")
        .arg(bgColor.name(), borderColor.name()));
    auto *warnLayout = new QHBoxLayout(warnFrame);
    warnLayout->setContentsMargins(8, 6, 8, 6);

    auto *warnIcon = new QLabel(warnFrame);
    warnIcon->setPixmap(QIcon::fromTheme(QStringLiteral("dialog-warning"))
                            .pixmap(22, 22));
    warnLayout->addWidget(warnIcon);

    auto *warnLabel = new QLabel(
        i18n("<b>All local changes will be lost!</b> "
             "This action cannot be undone."),
        warnFrame);
    warnLabel->setStyleSheet(QStringLiteral("color: %1;").arg(fgColor.name()));
    warnLabel->setWordWrap(true);
    warnLayout->addWidget(warnLabel, 1);

    layout->addWidget(warnFrame);

    // --- File table ---
    layout->addWidget(new QLabel(i18n("Local modifications:"), this));

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({i18n("Path"), i18n("Extension"), i18n("Status")});
    m_tree->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setMinimumSectionSize(40);
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setAlternatingRowColors(true);
    layout->addWidget(m_tree, 1);

    // --- Select-all / deselect-all ---
    auto *btnRow = new QHBoxLayout;
    auto *selectAllBtn   = new QPushButton(i18n("Select All"), this);
    auto *deselectAllBtn = new QPushButton(i18n("Select None"), this);
    btnRow->addWidget(selectAllBtn);
    btnRow->addWidget(deselectAllBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    // --- "Revert added files" option ---
    m_revertAdded = new QCheckBox(
        i18n("Also revert added files (they will no longer be tracked)"), this);
    m_revertAdded->setChecked(true);
    layout->addWidget(m_revertAdded);

    // --- "Also delete unversioned items" option ---
    // Collected up front so the list can be shown to the user BEFORE they
    // decide; deletion itself only happens after a successful revert
    // (the caller reads unversionedPathsToDelete() and deletes then).
    {
        QSet<QString> seenUnversioned;
        for (const QString &p : paths) {
            const QFileInfo fi(p);
            bool ok = false;
            const auto entries = mgr->status(fi.absoluteFilePath(), fi.isDir(), &ok);
            for (const SvnStatusEntry &e : entries) {
                if (e.textStatus != SvnFileStatus::Unversioned) continue;
                const QString abs = QFileInfo(e.path).absoluteFilePath();
                if (seenUnversioned.contains(abs)) continue;
                seenUnversioned.insert(abs);
                m_unversionedPaths << abs;
            }
        }
    }
    if (!m_unversionedPaths.isEmpty()) {
        m_deleteUnversioned = new QCheckBox(
            i18np("Also delete 1 unversioned item after revert",
                  "Also delete %1 unversioned items after revert",
                  m_unversionedPaths.size()), this);
        layout->addWidget(m_deleteUnversioned);

        auto *listLabel = new QLabel(m_unversionedPaths.join(QLatin1Char('\n')), this);
        listLabel->setWordWrap(true);
        listLabel->setStyleSheet(QStringLiteral("color: %1;")
            .arg(palette().color(QPalette::Disabled, QPalette::WindowText).name()));
        layout->addWidget(listLabel);
    }

    // --- Dialog buttons ---
    auto *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_revertBtn = btnBox->button(QDialogButtonBox::Ok);
    m_revertBtn->setText(i18n("Revert"));
    m_revertBtn->setIcon(QIcon::fromTheme(QStringLiteral("edit-undo")));
    layout->addWidget(btnBox);

    // --- Populate ---
    populateTable(paths, mgr);
    // Give every folder the correct tri-state for its children's start states.
    m_block = true;
    SvnUi::initTreeCheckStates(m_tree);
    m_block = false;

    // --- Connections ---
    connect(m_tree, &QTreeWidget::itemChanged, this, &SvnRevertDialog::onItemChanged);
    connect(m_revertAdded, &QCheckBox::toggled, this, [this](bool checked) {
        // Show/hide all Added items at every tree level.
        QList<QTreeWidgetItem *> stack;
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
            stack.push_back(m_tree->topLevelItem(i));
        while (!stack.isEmpty()) {
            QTreeWidgetItem *item = stack.takeFirst();
            if (item->data(0, Qt::UserRole + 1).toInt()
                    == static_cast<int>(SvnFileStatus::Added))
                item->setHidden(!checked);
            for (int i = 0; i < item->childCount(); ++i)
                stack.push_back(item->child(i));
        }
        updateRevertButton();
    });
    connect(selectAllBtn, &QPushButton::clicked, this, [this]() {
        m_block = true;
        QList<QTreeWidgetItem *> stack;
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
            stack.push_back(m_tree->topLevelItem(i));
        while (!stack.isEmpty()) {
            QTreeWidgetItem *item = stack.takeFirst();
            if (!item->isHidden())
                item->setCheckState(0, Qt::Checked);
            for (int i = 0; i < item->childCount(); ++i)
                stack.push_back(item->child(i));
        }
        m_block = false;
        updateRevertButton();
    });
    connect(deselectAllBtn, &QPushButton::clicked, this, [this]() {
        m_block = true;
        QList<QTreeWidgetItem *> stack;
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
            stack.push_back(m_tree->topLevelItem(i));
        while (!stack.isEmpty()) {
            QTreeWidgetItem *item = stack.takeFirst();
            item->setCheckState(0, Qt::Unchecked);
            for (int i = 0; i < item->childCount(); ++i)
                stack.push_back(item->child(i));
        }
        m_block = false;
        updateRevertButton();
    });
    connect(btnBox, &QDialogButtonBox::accepted, this, &SvnRevertDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *f5 = new QShortcut(Qt::Key_F5, this);
    connect(f5, &QShortcut::activated, this, &SvnRevertDialog::refresh);
}

// ---------------------------------------------------------------------------
// Table population
// ---------------------------------------------------------------------------

void SvnRevertDialog::populateTable(const QStringList &paths, SvnManager *mgr)
{
    const SvnInfo wcInfo = mgr->info(paths.first());
    const QString wcRoot = wcInfo.valid
        ? wcInfo.wcRootPath
        : QFileInfo(paths.first()).absolutePath();

    QList<SvnStatusEntry> entries;
    QSet<QString> seen;
    bool anyStatusFailed = false;
    for (const QString &p : paths) {
        const QFileInfo fi(p);
        bool ok = false;
        const auto statusEntries = mgr->status(fi.absoluteFilePath(), fi.isDir(), &ok);
        if (!ok) anyStatusFailed = true;
        for (const SvnStatusEntry &e : statusEntries) {
            if (!isRevertable(e.textStatus)) continue;
            const QString abs = QFileInfo(e.path).absoluteFilePath();
            if (seen.contains(abs)) continue;
            entries << e;
            seen.insert(abs);
        }
    }
    std::sort(entries.begin(), entries.end(), [](const SvnStatusEntry &a,
                                                  const SvnStatusEntry &b) {
        return a.path < b.path;
    });

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
        updateRevertButton();
        return;
    }

    QFileIconProvider iconProvider;
    const QDir wcDir(wcRoot);
    // Maps absolute path → QTreeWidgetItem so children can locate their parent.
    QHash<QString, QTreeWidgetItem *> pathToItem;

    for (const SvnStatusEntry &e : entries) {
        const QString abs = QFileInfo(e.path).absoluteFilePath();
        const QFileInfo fi(abs);
        const bool isDir = fi.isDir();

        // If the direct parent directory is already in the tree, nest under it.
        QTreeWidgetItem *parentItem = pathToItem.value(fi.absolutePath(), nullptr);

        const QString relPath = wcDir.relativeFilePath(abs);
        const QString fullDisplay = relPath.startsWith(QLatin1String("./"))
            ? relPath.mid(2) : relPath;
        const QString displayText = parentItem ? fi.fileName() : fullDisplay;

        const QString ext = isDir ? QString()
                                  : (fi.suffix().isEmpty() ? QString()
                                     : QLatin1Char('.') + fi.suffix());
        const QString statusName = statusIconName(e.textStatus);

        auto *item = parentItem ? new QTreeWidgetItem(parentItem)
                                : new QTreeWidgetItem(m_tree);
        item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setCheckState(0, Qt::Checked);
        item->setText(0, displayText);
        item->setToolTip(0, fullDisplay);
        item->setIcon(0, isDir ? SvnUi::makeFolderIcon() : iconProvider.icon(fi));
        item->setText(1, ext);
        item->setText(2, statusText(e.textStatus));
        if (!statusName.isEmpty())
            item->setIcon(2, QIcon::fromTheme(statusName));
        item->setData(0, Qt::UserRole,     abs);
        item->setData(0, Qt::UserRole + 1, static_cast<int>(e.textStatus));
        item->setData(0, Qt::UserRole + 2, isDir);

        pathToItem.insert(abs, item);
    }

    m_tree->expandAll();
    m_tree->resizeColumnToContents(2);
    m_tree->resizeColumnToContents(1);
    m_tree->resizeColumnToContents(0);

    updateRevertButton();
}

// ---------------------------------------------------------------------------
// Checkbox propagation
// ---------------------------------------------------------------------------

void SvnRevertDialog::onItemChanged(QTreeWidgetItem *item, int col)
{
    if (col != 0 || m_block) return;
    m_block = true;
    // Checking or unchecking a folder cascades to all of its contents, and each
    // ancestor recomputes its tri-state. The revert itself runs with
    // '--depth infinity', so a fully checked folder reverts its whole subtree
    // while a partially checked folder is left out of the target list (its
    // checked files are reverted individually).
    SvnUi::cascadeItemCheckState(item);
    m_block = false;
    updateRevertButton();
}

// ---------------------------------------------------------------------------
// Button state
// ---------------------------------------------------------------------------

void SvnRevertDialog::updateRevertButton()
{
    bool any = false;
    QList<QTreeWidgetItem *> stack;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        stack.push_back(m_tree->topLevelItem(i));
    while (!stack.isEmpty() && !any) {
        QTreeWidgetItem *item = stack.takeFirst();
        if (!item->isHidden() && item->checkState(0) == Qt::Checked)
            any = true;
        for (int i = 0; i < item->childCount(); ++i)
            stack.push_back(item->child(i));
    }
    m_revertBtn->setEnabled(any);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

QStringList SvnRevertDialog::selectedPaths() const
{
    // 'svn revert --depth infinity' reverts a fully checked folder recursively,
    // so only fully checked items are listed. A partially checked folder is left
    // out and its checked files are reverted individually.
    return SvnUi::collectCheckedPaths(m_tree, Qt::UserRole,
        [](const QTreeWidgetItem *item) {
            return SvnUi::includeForDepthInfinity(item->checkState(0), false);
        });
}

bool SvnRevertDialog::deleteUnversionedRequested() const
{
    return m_deleteUnversioned && m_deleteUnversioned->isChecked();
}

QStringList SvnRevertDialog::unversionedPathsToDelete() const
{
    return m_unversionedPaths;
}

void SvnRevertDialog::refresh()
{
    m_tree->clear();
    m_tree->setEnabled(true);
    populateTable(m_paths, m_mgr);
}

void SvnRevertDialog::accept()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty()) return;

    const int ret = QMessageBox::warning(
        this,
        i18n("Confirm Revert"),
        i18np("Are you sure you want to revert the changes to <b>1 file</b>?<br>"
              "All local changes will be permanently lost.",
              "Are you sure you want to revert the changes to <b>%1 files</b>?<br>"
              "All local changes will be permanently lost.",
              paths.size()),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (ret == QMessageBox::Yes)
        QDialog::accept();
}
