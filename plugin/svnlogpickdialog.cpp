#include "svnlogpickdialog.h"
#include "svnmanager.h"
#include "svnrevrange.h"
#include "svnsettings.h"
#include "svnuihelpers.h"

#include <KLocalizedString>

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTextEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString formatDate(const QString &isoDate)
{
    if (isoDate.length() < 16) return isoDate;
    return isoDate.left(10) + QLatin1Char(' ') + isoDate.mid(11, 5);
}

} // namespace

SvnLogPickDialog::SvnLogPickDialog(const QString &urlOrPath, SvnManager *mgr,
                                   SelectionMode mode, QWidget *parent)
    : QDialog(parent), m_target(urlOrPath), m_mgr(mgr)
{
    setWindowTitle(mode == SelectionMode::Multiple
                       ? i18n("Select Revisions to Merge")
                       : i18n("Select Revision"));
    resize(880, 620);

    auto *layout = new QVBoxLayout(this);

    auto *pathLabel = new QLabel(i18n("Log of: <b>%1</b>", urlOrPath), this);
    pathLabel->setWordWrap(true);
    layout->addWidget(pathLabel);
    if (mode == SelectionMode::Multiple) {
        layout->addWidget(new QLabel(
            i18n("Select one or more revisions (Ctrl/Shift for multiple)."),
            this));
    }

    // --- Log list (top) | details (bottom), as in the update dialog ---
    auto *split = new QSplitter(Qt::Vertical, this);
    layout->addWidget(split, 1);

    m_logTree = new QTreeWidget(split);
    m_logTree->setColumnCount(4);
    m_logTree->setHeaderLabels({i18n("Revision"), i18n("Author"),
                                i18n("Date"), i18n("Message")});
    for (int c = 0; c < 3; ++c)
        m_logTree->header()->setSectionResizeMode(c,
            QHeaderView::ResizeToContents);
    m_logTree->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_logTree->setRootIsDecorated(false);
    m_logTree->setAlternatingRowColors(true);
    m_logTree->setSelectionMode(mode == SelectionMode::Multiple
                                    ? QAbstractItemView::ExtendedSelection
                                    : QAbstractItemView::SingleSelection);
    m_logTree->setSortingEnabled(true);
    m_logTree->sortByColumn(0, Qt::DescendingOrder);

    auto *detail = new QSplitter(Qt::Horizontal, split);
    auto *msgWidget = new QWidget(detail);
    auto *msgLayout = new QVBoxLayout(msgWidget);
    msgLayout->setContentsMargins(0, 0, 4, 0);
    msgLayout->addWidget(new QLabel(i18n("Log message:"), msgWidget));
    m_msgView = new QTextEdit(msgWidget);
    m_msgView->setReadOnly(true);
    msgLayout->addWidget(m_msgView, 1);
    detail->addWidget(msgWidget);

    auto *pathWidget = new QWidget(detail);
    auto *pathLayout = new QVBoxLayout(pathWidget);
    pathLayout->setContentsMargins(4, 0, 0, 0);
    pathLayout->addWidget(new QLabel(i18n("Changed paths:"), pathWidget));
    m_pathsTree = new QTreeWidget(pathWidget);
    m_pathsTree->setColumnCount(2);
    m_pathsTree->setHeaderLabels({i18n("Action"), i18n("Path")});
    m_pathsTree->header()->setSectionResizeMode(0,
        QHeaderView::ResizeToContents);
    m_pathsTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_pathsTree->setRootIsDecorated(false);
    m_pathsTree->setAlternatingRowColors(true);
    pathLayout->addWidget(m_pathsTree, 1);
    detail->addWidget(pathWidget);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 1);

    // --- Bottom row ---
    auto *bottomRow = new QHBoxLayout;
    m_moreBtn = new QPushButton(i18n("Load more..."), this);
    m_moreBtn->setEnabled(false);
    bottomRow->addWidget(m_moreBtn);
    bottomRow->addSpacing(12);
    const SvnUi::LogOptionsBoxes logOpts = SvnUi::createLogOptionsRow(
        this, bottomRow,
        [this]() { reloadLog(); },
        [this]() { showEntry(m_lastShownItem); });
    m_onlyAffectedBox = logOpts.onlyAffectedPaths;
    bottomRow->addStretch();
    layout->addLayout(bottomRow);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okBtn = buttons->button(QDialogButtonBox::Ok);
    m_okBtn->setEnabled(false);
    layout->addWidget(buttons);

    connect(m_logTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *cur, QTreeWidgetItem *) { showEntry(cur); });
    connect(m_logTree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        m_okBtn->setEnabled(!selectedRevisions().isEmpty());
    });
    connect(m_logTree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
                if (item && item->data(0, Qt::UserRole).toLongLong() > 0)
                    accept();
            });
    connect(m_moreBtn, &QPushButton::clicked,
            this, &SvnLogPickDialog::loadMore);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    loadMore();
}

void SvnLogPickDialog::reloadLog()
{
    m_logTree->clear();
    m_entries.clear();
    m_oldestRevision = -1;
    m_lastShownItem = nullptr;
    m_msgView->clear();
    m_pathsTree->clear();
    m_okBtn->setEnabled(false);
    loadMore();
}

void SvnLogPickDialog::loadMore()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_moreBtn->setEnabled(false);

    const QString fromRev = (m_oldestRevision > 1)
        ? QString::number(m_oldestRevision - 1) : QStringLiteral("HEAD");
    SvnLogOptions opts;
    opts.stopOnCopy    = SvnSettings::logStopOnCopy();
    opts.includeMerged = SvnSettings::logIncludeMerged();
    const auto newEntries = m_mgr->log(m_target, kPageSize, fromRev,
                                       QStringLiteral("1"), opts);
    QApplication::restoreOverrideCursor();

    if (newEntries.isEmpty() && m_entries.isEmpty()) {
        auto *ph = new QTreeWidgetItem(m_logTree);
        ph->setText(0, i18n("No log entries found."));
        ph->setFlags(Qt::NoItemFlags);
        return;
    }

    m_logTree->setRootIsDecorated(opts.includeMerged);
    m_logTree->setSortingEnabled(false);
    int newTopLevel = 0;
    QList<QTreeWidgetItem *> parentAtDepth;
    for (const SvnLogEntry &e : newEntries) {
        const int idx = m_entries.size();
        m_entries.append(e);
        const long long rev = e.revision.toLongLong();

        // Merged revisions (-g): shown, but NOT selectable — picking them as a
        // merge source would be misleading.
        SvnRevisionItem *item = nullptr;
        if (e.mergeDepth > 0 && e.mergeDepth <= parentAtDepth.size()) {
            item = new SvnRevisionItem(parentAtDepth[e.mergeDepth - 1]);
            item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
            const QColor gray =
                palette().color(QPalette::Disabled, QPalette::Text);
            QFont italic = item->font(0);
            italic.setItalic(true);
            for (int c = 0; c < 4; ++c) {
                item->setForeground(c, gray);
                item->setFont(c, italic);
            }
        } else {
            item = new SvnRevisionItem(m_logTree);
            ++newTopLevel;
        }
        parentAtDepth.resize(e.mergeDepth);
        parentAtDepth.append(item);

        item->setData(0, Qt::UserRole,     rev);
        item->setData(0, Qt::UserRole + 1, idx);
        item->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(0, e.revision);
        item->setText(1, e.author);
        item->setText(2, formatDate(e.date));
        const int nl = e.message.indexOf(QLatin1Char('\n'));
        item->setText(3, nl >= 0 ? e.message.left(nl).trimmed()
                                 : e.message.trimmed());
        item->setToolTip(3, e.message.trimmed());

        if (e.mergeDepth == 0 && rev > 0
            && (m_oldestRevision < 0 || rev < m_oldestRevision))
            m_oldestRevision = rev;
    }
    m_logTree->setSortingEnabled(true);
    if (opts.includeMerged)
        m_logTree->expandAll();

    m_moreBtn->setEnabled(newTopLevel == kPageSize
                          && m_oldestRevision > 1);
    markNotEligibleItems();
}

void SvnLogPickDialog::setEligibleRevisions(const QSet<long long> &eligible)
{
    m_eligibleRevisions = eligible;
    m_eligibleKnown = true;
    markNotEligibleItems();
}

void SvnLogPickDialog::markNotEligibleItems()
{
    if (!m_eligibleKnown)
        return; // the mergeinfo query was missing or failed — grey out nothing
    const QColor gray = palette().color(QPalette::Disabled, QPalette::Text);
    for (QTreeWidgetItemIterator it(m_logTree); *it; ++it) {
        QTreeWidgetItem *item = *it;
        const long long rev = item->data(0, Qt::UserRole).toLongLong();
        if (rev <= 0 || m_eligibleRevisions.contains(rev))
            continue;
        for (int c = 0; c < 4; ++c)
            item->setForeground(c, gray);
        item->setToolTip(0,
            i18n("Not eligible (already merged or natural history)"));
    }
}

void SvnLogPickDialog::showEntry(QTreeWidgetItem *item)
{
    m_lastShownItem = item;
    m_msgView->clear();
    m_pathsTree->clear();
    if (!item)
        return;
    const int idx = item->data(0, Qt::UserRole + 1).toInt();
    if (idx < 0 || idx >= m_entries.size())
        return;
    const SvnLogEntry &e = m_entries.at(idx);
    m_msgView->setPlainText(e.message.trimmed());

    // "Show only affected paths": resolve the repo-relative path of the log
    // target lazily (m_target is usually a URL).
    const bool onlyAffected = m_onlyAffectedBox
                              && m_onlyAffectedBox->isChecked();
    if (onlyAffected && !m_repoRelResolved) {
        m_repoRelResolved = true;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const SvnInfo info = m_mgr->info(m_target);
        QApplication::restoreOverrideCursor();
        if (info.valid && info.url.startsWith(info.repositoryRoot))
            m_repoRelPath = info.url.mid(info.repositoryRoot.length());
    }

    for (const auto &[filePath, action] : e.changedPaths) {
        if (onlyAffected && !m_repoRelPath.isEmpty()
            && filePath != m_repoRelPath
            && !filePath.startsWith(m_repoRelPath + QLatin1Char('/')))
            continue;
        auto *pathItem = new QTreeWidgetItem(m_pathsTree);
        pathItem->setText(0, action);
        pathItem->setText(1, filePath);
    }
}

QList<long long> SvnLogPickDialog::selectedRevisions() const
{
    QList<long long> revs;
    const QList<QTreeWidgetItem *> items = m_logTree->selectedItems();
    for (const QTreeWidgetItem *item : items) {
        const long long rev = item->data(0, Qt::UserRole).toLongLong();
        if (rev > 0)
            revs << rev;
    }
    std::sort(revs.begin(), revs.end());
    revs.erase(std::unique(revs.begin(), revs.end()), revs.end());
    return revs;
}

long long SvnLogPickDialog::selectedRevision() const
{
    const QList<long long> revs = selectedRevisions();
    return revs.isEmpty() ? -1 : revs.first();
}

QString SvnLogPickDialog::formatRevisionRanges(QList<long long> revs)
{
    return ::formatRevisionRanges(std::move(revs));
}
