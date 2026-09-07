#include "svnconflictdialog.h"
#include "svnmanager.h"
#include "svntypes.h"
#include "svnuihelpers.h"

#include <KLocalizedString>

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int kRolePath   = Qt::UserRole;      // absoluter Pfad
constexpr int kRoleIsTree = Qt::UserRole + 1;  // bool: Baum-Konflikt
constexpr int kRoleIsText = Qt::UserRole + 2;  // bool: Textkonflikt

} // namespace

SvnConflictDialog::SvnConflictDialog(const QString &wcPath, SvnManager *mgr,
                                     QWidget *parent)
    : QDialog(parent), m_wcPath(wcPath), m_mgr(mgr)
{
    setWindowTitle(i18n("SVN: Resolve Conflicts"));
    resize(760, 480);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        i18n("Conflicts in: <b>%1</b>", wcPath), this));

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({i18n("File"), i18n("Conflict Type")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(m_tree, 1);

    m_detailLabel = new QLabel(this);
    m_detailLabel->setWordWrap(true);
    layout->addWidget(m_detailLabel);

    // --- Resolution buttons (two rows) ---
    auto *row1 = new QHBoxLayout;
    m_editBtn = new QPushButton(
        QIcon::fromTheme(QStringLiteral("vcs-merge")),
        i18n("Edit (3-Way Merge)..."), this);
    m_editBtn->setToolTip(
        i18n("Open Base/Mine/Theirs in AnGscheidrDiffer and resolve manually"));
    connect(m_editBtn, &QPushButton::clicked,
            this, &SvnConflictDialog::editSelected);
    row1->addWidget(m_editBtn);
    m_workingBtn = new QPushButton(i18n("Resolved (use working copy)"), this);
    m_workingBtn->setToolTip(
        i18n("Mark as resolved — keep the file as it currently is"));
    connect(m_workingBtn, &QPushButton::clicked, this,
            [this]() { resolveSelected(QStringLiteral("working")); });
    row1->addWidget(m_workingBtn);
    row1->addStretch();
    layout->addLayout(row1);

    auto *row2 = new QHBoxLayout;
    const auto addResolve = [this, row2](QPushButton **btn,
                                         const QString &text,
                                         const QString &accept,
                                         const QString &tip) {
        *btn = new QPushButton(text, this);
        (*btn)->setToolTip(tip);
        connect(*btn, &QPushButton::clicked, this,
                [this, accept]() { resolveSelected(accept); });
        row2->addWidget(*btn);
    };
    addResolve(&m_mineFullBtn, i18n("Use Mine (whole file)"),
               QStringLiteral("mine-full"),
               i18n("Discard the incoming changes, keep your local file"));
    addResolve(&m_theirsFullBtn, i18n("Use Theirs (whole file)"),
               QStringLiteral("theirs-full"),
               i18n("Discard your local changes, take the incoming file"));
    addResolve(&m_mineConflictBtn, i18n("Prefer Mine (conflicts only)"),
               QStringLiteral("mine-conflict"),
               i18n("Merge normally, prefer your side where lines conflict"));
    addResolve(&m_theirsConflictBtn, i18n("Prefer Theirs (conflicts only)"),
               QStringLiteral("theirs-conflict"),
               i18n("Merge normally, prefer the incoming side where lines "
                    "conflict"));
    row2->addStretch();
    layout->addLayout(row2);

    auto *btnBox = new QDialogButtonBox(this);
    auto *postponeBtn = btnBox->addButton(i18n("Postpone"),
                                          QDialogButtonBox::RejectRole);
    postponeBtn->setToolTip(
        i18n("Leave the remaining conflicts unresolved and close"));
    auto *refreshBtn = btnBox->addButton(i18n("Refresh"),
                                         QDialogButtonBox::ActionRole);
    connect(refreshBtn, &QPushButton::clicked,
            this, &SvnConflictDialog::reload);
    // "Continue Merge": only visible once setContinueMerge() has set a
    // callback. Restarts the interrupted merge.
    m_continueBtn = btnBox->addButton(i18n("Continue Merge"),
                                      QDialogButtonBox::ActionRole);
    m_continueBtn->setIcon(QIcon::fromTheme(QStringLiteral("merge")));
    m_continueBtn->setVisible(false);
    connect(m_continueBtn, &QPushButton::clicked, this, [this]() {
        // Copy the callback: accept() deletes the dialog (WA_DeleteOnClose)
        // and the repeated merge may open a new conflict dialog.
        auto cb = m_continueMerge;
        accept();
        if (cb)
            cb();
    });
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(btnBox);

    connect(m_tree, &QTreeWidget::itemSelectionChanged,
            this, &SvnConflictDialog::updateActions);

    reload();
}

void SvnConflictDialog::setContinueMerge(std::function<void()> cb)
{
    m_continueMerge = std::move(cb);
    m_continueBtn->setVisible(static_cast<bool>(m_continueMerge));
    updateActions();
}

void SvnConflictDialog::reload()
{
    m_busy = false;
    m_tree->clear();

    const QList<SvnStatusEntry> entries = m_mgr->status(m_wcPath, true);

    // File/property conflicts first, tree conflicts after.
    const auto addEntry = [this](const SvnStatusEntry &e, bool asTree) {
        auto *item = new QTreeWidgetItem(m_tree);
        const bool isText = e.textStatus == SvnFileStatus::Conflicted;
        const bool isProp = e.propStatus == SvnFileStatus::Conflicted;
        QStringList kinds;
        if (asTree)
            kinds << i18n("Tree");
        if (isText)
            kinds << i18n("Text");
        if (isProp)
            kinds << i18n("Property");
        item->setText(0, e.path);
        item->setText(1, kinds.join(QStringLiteral(" + ")));
        item->setIcon(0, QIcon::fromTheme(QStringLiteral("vcs-conflicting")));
        item->setData(0, kRolePath, e.path);
        item->setData(0, kRoleIsTree, asTree);
        item->setData(0, kRoleIsText, isText);
    };
    for (const SvnStatusEntry &e : entries) {
        if (!e.treeConflicted
            && (e.textStatus == SvnFileStatus::Conflicted
                || e.propStatus == SvnFileStatus::Conflicted))
            addEntry(e, false);
    }
    for (const SvnStatusEntry &e : entries) {
        if (e.treeConflicted)
            addEntry(e, true);
    }

    if (m_tree->topLevelItemCount() == 0) {
        m_detailLabel->setText(
            i18n("No conflicts — everything is resolved."));
    } else {
        m_tree->setCurrentItem(m_tree->topLevelItem(0));
    }
    updateActions();
}

void SvnConflictDialog::updateActions()
{
    bool containsTree = false;
    const QStringList paths = selectedPaths(&containsTree);
    const bool any = !paths.isEmpty() && !m_busy;

    // Tree conflicts can only be resolved with --accept working via the CLI.
    m_workingBtn->setEnabled(any);
    m_mineFullBtn->setEnabled(any && !containsTree);
    m_theirsFullBtn->setEnabled(any && !containsTree);
    m_mineConflictBtn->setEnabled(any && !containsTree);
    m_theirsConflictBtn->setEnabled(any && !containsTree);

    const bool singleText = paths.size() == 1 && !containsTree
        && m_tree->currentItem()
        && m_tree->currentItem()->data(0, kRoleIsText).toBool();
    m_editBtn->setEnabled(singleText && !m_busy);

    // "Continue Merge" only when no conflicts remain open — otherwise the
    // repeated merge would abort immediately again. Gate on the callback (not
    // isVisible()): at setContinueMerge() the window is not yet visible via
    // show(), isVisible() of a child widget would then be false and the enabled
    // state would never be set (the button would wrongly stay active).
    if (m_continueBtn && m_continueMerge) {
        const bool noConflicts = m_tree->topLevelItemCount() == 0;
        m_continueBtn->setEnabled(noConflicts && !m_busy);
        m_continueBtn->setToolTip(noConflicts
            ? i18n("Continue the merge — apply the remaining revisions")
            : i18n("Resolve all conflicts first."));
    }

    // Details of the current entry.
    if (QTreeWidgetItem *item = m_tree->currentItem()) {
        const QString path = item->data(0, kRolePath).toString();
        if (item->data(0, kRoleIsTree).toBool()) {
            const QString desc = m_mgr->treeConflictDescriptionSync(path);
            m_detailLabel->setText(desc.isEmpty()
                ? i18n("Tree conflict — resolvable only by accepting the "
                       "working copy state.")
                : i18n("Tree conflict (%1) — resolvable only by accepting "
                       "the working copy state.", desc));
        } else {
            m_detailLabel->setText(
                i18n("Conflict helper files (.mine / .rOLD / .rNEW) are "
                     "next to the file."));
        }
    }
}

QStringList SvnConflictDialog::selectedPaths(bool *containsTree) const
{
    QStringList paths;
    bool tree = false;
    const QList<QTreeWidgetItem *> items = m_tree->selectedItems();
    for (const QTreeWidgetItem *item : items) {
        paths << item->data(0, kRolePath).toString();
        tree = tree || item->data(0, kRoleIsTree).toBool();
    }
    if (containsTree)
        *containsTree = tree;
    return paths;
}

void SvnConflictDialog::resolveSelected(const QString &accept)
{
    bool containsTree = false;
    const QStringList paths = selectedPaths(&containsTree);
    if (paths.isEmpty())
        return;
    if (m_mgr->isBusy()) {
        QMessageBox::information(this, i18n("SVN"),
            i18n("An SVN operation is already in progress. Please wait."));
        return;
    }

    m_busy = true;
    updateActions();
    connect(m_mgr, &SvnManager::operationFinished, this,
            [this](const SvnOperationResult &) { reload(); },
            static_cast<Qt::ConnectionType>(
                Qt::SingleShotConnection | Qt::QueuedConnection));
    m_mgr->resolveAsync(paths, accept);
}

void SvnConflictDialog::editSelected()
{
    bool containsTree = false;
    const QStringList paths = selectedPaths(&containsTree);
    if (paths.size() != 1 || containsTree)
        return;
    // onResolved fires right after the async resolve starts — reload the list
    // only after it completes.
    SvnUi::resolveWithThreeWayMerge(paths.first(), m_mgr, this, this,
        [this]() {
            connect(m_mgr, &SvnManager::operationFinished, this,
                    [this](const SvnOperationResult &) { reload(); },
                    static_cast<Qt::ConnectionType>(
                        Qt::SingleShotConnection | Qt::QueuedConnection));
        });
}
