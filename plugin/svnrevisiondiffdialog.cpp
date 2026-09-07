#include "svnrevisiondiffdialog.h"
#include "svndiffwindow.h"
#include "svnmanager.h"
#include "svnuihelpers.h"

#include <KLocalizedString>

#include <QApplication>
#include <QDialogButtonBox>
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

#include <algorithm>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

SvnRevisionDiffDialog::SvnRevisionDiffDialog(const QString &path, SvnManager *mgr,
                                              const QString &tool, QObject *processParent,
                                              QWidget *parent)
    : QDialog(parent), m_path(path), m_tool(tool), m_mgr(mgr), m_procParent(processParent)
{
    setWindowTitle(i18n("Diff with Revision – %1", QFileInfo(path).fileName()));
    resize(920, 650);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    auto *pathLabel = new QLabel(
        i18n("Select revision for: <b>%1</b>", path), this);
    pathLabel->setWordWrap(true);
    mainLayout->addWidget(pathLabel);

    // Outer splitter: log list (top) | details panel (bottom)
    auto *mainSplit = new QSplitter(Qt::Vertical, this);
    mainLayout->addWidget(mainSplit, 1);

    // --- Log list ---
    m_logTree = new QTreeWidget(mainSplit);
    m_logTree->setColumnCount(4);
    m_logTree->setHeaderLabels({i18n("Revision"), i18n("Author"), i18n("Date"), i18n("Message")});
    m_logTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_logTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_logTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_logTree->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_logTree->setRootIsDecorated(false);
    m_logTree->setAlternatingRowColors(true);
    m_logTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_logTree->setSortingEnabled(true);
    m_logTree->sortByColumn(0, Qt::DescendingOrder);

    // --- Details panel: message (left) | changed files (right) ---
    auto *detailWidget = new QWidget(mainSplit);
    auto *detailLayout = new QVBoxLayout(detailWidget);
    detailLayout->setContentsMargins(0, 4, 0, 0);

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
    btnRow->addStretch();

    m_diffBtn = new QPushButton(
        QIcon::fromTheme(QStringLiteral("vcs-diff")), i18n("Show Diff"), this);
    m_diffBtn->setEnabled(false);
    m_diffBtn->setDefault(true);
    btnRow->addWidget(m_diffBtn);

    auto *closeBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    btnRow->addWidget(closeBox);
    mainLayout->addLayout(btnRow);

    // --- Connections ---
    connect(m_logTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
                showEntry(item);
                m_diffBtn->setEnabled(item != nullptr);
            });
    connect(m_logTree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *, int) { diffSelected(); });
    connect(m_moreBtn,  &QPushButton::clicked, this, &SvnRevisionDiffDialog::loadMore);
    connect(m_diffBtn,  &QPushButton::clicked, this, &SvnRevisionDiffDialog::diffSelected);
    connect(closeBox,   &QDialogButtonBox::rejected, this, &QDialog::reject);

    loadMore();
}

// ---------------------------------------------------------------------------
// Load a page of log entries
// ---------------------------------------------------------------------------

void SvnRevisionDiffDialog::loadMore()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_moreBtn->setEnabled(false);

    const QString fromRev = (m_oldestRevision > 1)
        ? QString::number(m_oldestRevision - 1) : QStringLiteral("HEAD");
    const QString toRev = QStringLiteral("1");

    const auto newEntries = m_mgr->log(m_path, kPageSize, fromRev, toRev);
    QApplication::restoreOverrideCursor();

    if (newEntries.isEmpty() && m_entries.isEmpty()) {
        auto *ph = new QTreeWidgetItem(m_logTree);
        ph->setText(0, i18n("No log available."));
        ph->setFlags(Qt::NoItemFlags);
        return;
    }

    m_logTree->setSortingEnabled(false);
    for (const SvnLogEntry &e : newEntries) {
        const int idx = m_entries.size();
        m_entries.append(e);
        const long long rev = e.revision.toLongLong();

        auto *item = new SvnRevisionItem(m_logTree);
        item->setData(0, Qt::UserRole,     rev);  // sort key
        item->setData(0, Qt::UserRole + 1, idx);  // entry index
        item->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(0, e.revision);
        item->setText(1, e.author);
        item->setText(2, formatDate(e.date));
        const int nl = e.message.indexOf(QLatin1Char('\n'));
        item->setText(3, nl >= 0 ? e.message.left(nl).trimmed() : e.message.trimmed());
        item->setToolTip(3, e.message.trimmed());

        if (rev > 0 && (m_oldestRevision < 0 || rev < m_oldestRevision))
            m_oldestRevision = rev;
    }
    m_logTree->setSortingEnabled(true);

    m_moreBtn->setEnabled(newEntries.size() == kPageSize && m_oldestRevision > 1);

    if (m_logTree->currentItem() == nullptr && m_logTree->topLevelItemCount() > 0)
        m_logTree->setCurrentItem(m_logTree->topLevelItem(0));
}

// ---------------------------------------------------------------------------
// Show details for the selected entry
// ---------------------------------------------------------------------------

void SvnRevisionDiffDialog::showEntry(QTreeWidgetItem *treeItem)
{
    if (!treeItem) {
        m_msgView->clear();
        m_pathsTree->clear();
        return;
    }

    const int idx = treeItem->data(0, Qt::UserRole + 1).toInt();
    if (idx < 0 || idx >= m_entries.size()) return;
    const SvnLogEntry &e = m_entries.at(idx);

    m_msgView->setPlainText(e.message.trimmed());

    m_pathsTree->clear();
    for (const auto &[filePath, action] : e.changedPaths) {
        auto *item = new QTreeWidgetItem(m_pathsTree);
        item->setText(0, actionLabel(action));
        item->setText(1, filePath);
        const QString icon = actionIconName(action);
        if (!icon.isEmpty())
            item->setIcon(0, QIcon::fromTheme(icon));
    }
}

// ---------------------------------------------------------------------------
// Launch diff for the currently selected revision
// ---------------------------------------------------------------------------

void SvnRevisionDiffDialog::diffSelected()
{
    QTreeWidgetItem *item = m_logTree->currentItem();
    if (!item) return;

    const int idx = item->data(0, Qt::UserRole + 1).toInt();
    if (idx < 0 || idx >= m_entries.size()) return;

    const QString revision = m_entries.at(idx).revision;
    SvnDiffWindow::launchFileDiff(m_tool, m_path, m_mgr, m_procParent, revision);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QString SvnRevisionDiffDialog::formatDate(const QString &isoDate)
{
    if (isoDate.length() < 19) return isoDate;
    return isoDate.left(10) + QLatin1Char(' ') + isoDate.mid(11, 5);
}

QString SvnRevisionDiffDialog::actionLabel(const QString &action)
{
    if (action == QStringLiteral("A")) return i18n("Added");
    if (action == QStringLiteral("M")) return i18n("Modified");
    if (action == QStringLiteral("D")) return i18n("Deleted");
    if (action == QStringLiteral("R")) return i18n("Replaced");
    if (action == QStringLiteral("C")) return i18n("Copied");
    return action;
}

QString SvnRevisionDiffDialog::actionIconName(const QString &action)
{
    if (action == QStringLiteral("A")) return QStringLiteral("vcs-added");
    if (action == QStringLiteral("M")) return QStringLiteral("vcs-locally-modified");
    if (action == QStringLiteral("D")) return QStringLiteral("vcs-removed");
    if (action == QStringLiteral("R")) return QStringLiteral("vcs-locally-modified");
    return {};
}
