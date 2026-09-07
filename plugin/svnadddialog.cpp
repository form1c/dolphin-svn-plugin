#include "svnadddialog.h"
#include "svnmanager.h"
#include "svnuihelpers.h"
#include "svntreecheck.h"

#include <KLocalizedString>

#include <QDialogButtonBox>
#include <QShortcut>
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

SvnAddDialog::SvnAddDialog(const QStringList &paths, SvnManager *mgr, QWidget *parent)
    : QDialog(parent)
{
    m_addPaths = paths;
    m_mgr      = mgr;

    setWindowTitle(i18n("Add – %1", QFileInfo(paths.first()).fileName()));
    resize(620, 460);

    auto *layout = new QVBoxLayout(this);

    QList<Item> items;
    bool anyStatusFailed = false;
    collectItems(paths, mgr, items, &anyStatusFailed);

    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        return a.absPath < b.absPath;
    });
    items.erase(
        std::unique(items.begin(), items.end(),
                    [](const Item &a, const Item &b) { return a.absPath == b.absPath; }),
        items.end());

    if (items.isEmpty()) {
        const QString msg = anyStatusFailed
            ? i18n("Failed to retrieve SVN status. "
                   "Check that svn is installed and this is a valid working copy.")
            : i18n("No unversioned items found.");
        layout->addWidget(new QLabel(msg, this));
        auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
        layout->addWidget(btnBox);
        connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
        return;
    }

    layout->addWidget(
        new QLabel(i18n("Select files to add to version control:"), this));

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({i18n("Path"), i18n("Extension")});
    m_tree->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setMinimumSectionSize(40);
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setAlternatingRowColors(true);
    layout->addWidget(m_tree);

    buildTable(items);

    // Select-all / deselect-all
    auto *btnRow = new QHBoxLayout;
    auto *selectAllBtn   = new QPushButton(i18n("Select All"), this);
    auto *deselectAllBtn = new QPushButton(i18n("Select None"), this);
    btnRow->addWidget(selectAllBtn);
    btnRow->addWidget(deselectAllBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    auto *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btnBox->button(QDialogButtonBox::Ok)->setText(i18n("Add"));
    layout->addWidget(btnBox);

    auto applyToAll = [this](Qt::CheckState state) {
        m_block = true;
        QList<QTreeWidgetItem *> stack;
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
            stack.push_back(m_tree->topLevelItem(i));
        while (!stack.isEmpty()) {
            QTreeWidgetItem *it = stack.takeFirst();
            it->setCheckState(0, state);
            for (int i = 0; i < it->childCount(); ++i)
                stack.push_back(it->child(i));
        }
        m_block = false;
    };
    connect(selectAllBtn,   &QPushButton::clicked, this, [applyToAll] { applyToAll(Qt::Checked);   });
    connect(deselectAllBtn, &QPushButton::clicked, this, [applyToAll] { applyToAll(Qt::Unchecked); });
    connect(btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_tree, &QTreeWidget::itemChanged, this, &SvnAddDialog::onItemChanged);

    auto *f5 = new QShortcut(Qt::Key_F5, this);
    connect(f5, &QShortcut::activated, this, &SvnAddDialog::refresh);
}

// ---------------------------------------------------------------------------
// Item collection (unchanged logic)
// ---------------------------------------------------------------------------

void SvnAddDialog::collectItems(const QStringList &paths, SvnManager *mgr,
                                QList<Item> &out, bool *anyStatusFailed) const
{
    for (const QString &p : paths) {
        const QFileInfo fi(p);
        const QString abs = fi.absoluteFilePath();

        if (fi.isDir()) {
            if (!mgr->info(abs).valid) {
                out.append({abs, true});
                collectDirContents(abs, out);
            } else {
                bool ok = false;
                const auto entries = mgr->status(abs, true, &ok);
                if (!ok && anyStatusFailed) *anyStatusFailed = true;
                for (const SvnStatusEntry &e : entries) {
                    if (e.textStatus != SvnFileStatus::Unversioned) continue;
                    const QFileInfo efi(e.path);
                    const bool isDir = efi.isDir();
                    out.append({efi.absoluteFilePath(), isDir});
                    if (isDir) collectDirContents(efi.absoluteFilePath(), out);
                }
            }
        } else {
            if (!mgr->info(abs).valid)
                out.append({abs, false});
        }
    }
}

void SvnAddDialog::collectDirContents(const QString &dirPath, QList<Item> &out)
{
    const QDir dir(dirPath);
    const QStringList names =
        dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QString &name : names) {
        if (name == QLatin1String(".svn")) continue;
        const QString abs = dir.absoluteFilePath(name);
        const bool isDir  = QFileInfo(abs).isDir();
        out.append({abs, isDir});
        if (isDir) collectDirContents(abs, out);
    }
}

// ---------------------------------------------------------------------------
// Table construction (nested: files under their parent directory)
// ---------------------------------------------------------------------------

void SvnAddDialog::buildTable(const QList<Item> &items)
{
    // Find the common path prefix of all items for relative display paths.
    auto toParts = [](const QString &s) {
        return s.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    };
    QStringList common = toParts(items.first().absPath);
    for (const Item &it : items) {
        const QStringList p = toParts(it.absPath);
        int n = 0;
        while (n < common.size() && n < p.size() && common[n] == p[n]) ++n;
        common = common.mid(0, n);
    }
    const QString commonFull = QLatin1Char('/') + common.join(QLatin1Char('/'));
    const int lastSlash      = commonFull.lastIndexOf(QLatin1Char('/'));
    const QString basePath   = lastSlash > 0 ? commonFull.left(lastSlash)
                                              : QStringLiteral("/");

    // Suppress itemChanged while filling the tree (refresh() rebuilds it after
    // the signal is already connected).
    m_block = true;

    QFileIconProvider iconProvider;
    // Maps absolute path → QTreeWidgetItem so children can locate their parent.
    QHash<QString, QTreeWidgetItem *> pathToItem;

    for (const Item &it : items) {
        const QFileInfo fi(it.absPath);

        // If the direct parent directory is already in the tree, nest under it.
        QTreeWidgetItem *parentItem = pathToItem.value(fi.absolutePath(), nullptr);

        QString relPath = QDir(basePath).relativeFilePath(it.absPath);
        if (relPath.startsWith(QLatin1String("./")))
            relPath = relPath.mid(2);
        // Child items show only the filename; top-level items show the relative path.
        const QString displayText = parentItem ? fi.fileName() : relPath;

        const QString ext = it.isDir ? QString()
                                     : (fi.suffix().isEmpty() ? QString()
                                        : QLatin1Char('.') + fi.suffix());

        auto *row = parentItem ? new QTreeWidgetItem(parentItem)
                               : new QTreeWidgetItem(m_tree);
        row->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        row->setCheckState(0, Qt::Checked);
        row->setText(0, displayText);
        row->setToolTip(0, relPath);
        row->setIcon(0, it.isDir ? SvnUi::makeFolderIcon() : iconProvider.icon(fi));
        row->setText(1, ext);
        row->setData(0, Qt::UserRole,     it.absPath);
        row->setData(0, Qt::UserRole + 1, it.isDir);

        pathToItem.insert(it.absPath, row);
    }

    m_tree->expandAll();
    m_tree->resizeColumnToContents(1);
    m_tree->resizeColumnToContents(0);

    SvnUi::initTreeCheckStates(m_tree);
    m_block = false;
}

// ---------------------------------------------------------------------------
// Checkbox propagation (nested: uses native parent/child relationships)
// ---------------------------------------------------------------------------

void SvnAddDialog::onItemChanged(QTreeWidgetItem *item, int col)
{
    if (col != 0 || m_block) return;
    m_block = true;
    SvnUi::cascadeItemCheckState(item);
    m_block = false;
}

// ---------------------------------------------------------------------------
// Result
// ---------------------------------------------------------------------------

void SvnAddDialog::refresh()
{
    if (!m_tree) return;
    m_tree->clear();

    QList<Item> items;
    collectItems(m_addPaths, m_mgr, items);
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        return a.absPath < b.absPath;
    });
    items.erase(
        std::unique(items.begin(), items.end(),
                    [](const Item &a, const Item &b) { return a.absPath == b.absPath; }),
        items.end());

    if (!items.isEmpty())
        buildTable(items);
}

QStringList SvnAddDialog::selectedPaths() const
{
    // 'svn add --depth empty <paths...>' adds exactly the listed items; a
    // partially checked directory (UserRole+1 is the isDir flag) comes along as
    // the new parent its children need. The sort puts parents before children.
    QStringList result = SvnUi::collectCheckedPaths(m_tree, Qt::UserRole,
        [](const QTreeWidgetItem *item) {
            return SvnUi::includeForDepthEmpty(
                item->checkState(0), item->data(0, Qt::UserRole + 1).toBool());
        });
    result.sort();
    return result;
}
