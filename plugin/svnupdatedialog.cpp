#include "svnupdatedialog.h"
#include "svnmanager.h"
#include "svnsettings.h"
#include "svnuihelpers.h"

#include <KLocalizedString>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTextEdit>
#include <QTreeWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

SvnUpdateDialog::SvnUpdateDialog(const QString &path, SvnManager *mgr, QWidget *parent)
    : QDialog(parent), m_path(path), m_mgr(mgr)
{
    setWindowTitle(i18n("Update to Revision"));
    resize(920, 680);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    auto *pathLabel = new QLabel(i18n("Log of: <b>%1</b>", path), this);
    pathLabel->setWordWrap(true);
    mainLayout->addWidget(pathLabel);

    // Show current WC revision so the user knows their baseline.
    m_wcInfo = mgr->info(path);
    if (m_wcInfo.valid && !m_wcInfo.revision.isEmpty()) {
        auto *revLabel = new QLabel(
            i18n("Current working copy revision: <b>%1</b>", m_wcInfo.revision), this);
        mainLayout->addWidget(revLabel);
    }
    if (m_wcInfo.valid && m_wcInfo.url.startsWith(m_wcInfo.repositoryRoot))
        m_repoRelPath = m_wcInfo.url.mid(m_wcInfo.repositoryRoot.length());

    // TortoiseSVN log options (settings shared with the other log views).
    auto *logOptsRow = new QHBoxLayout;
    const SvnUi::LogOptionsBoxes logOpts = SvnUi::createLogOptionsRow(
        this, logOptsRow,
        [this]() { reloadLog(); },
        [this]() { showEntry(m_lastShownItem); });
    m_onlyAffectedBox = logOpts.onlyAffectedPaths;
    logOptsRow->addStretch();
    mainLayout->addLayout(logOptsRow);

    // -----------------------------------------------------------------------
    // Log splitter: list (top) | details (bottom)
    // -----------------------------------------------------------------------
    auto *mainSplit = new QSplitter(Qt::Vertical, this);
    mainLayout->addWidget(mainSplit, 1);

    // --- Log-Liste ---
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

    // --- Details: commit message (left) | changed files (right) ---
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

    // -----------------------------------------------------------------------
    // "Load more" + revision + depth – compact in a single row
    // -----------------------------------------------------------------------
    auto *optRow = new QHBoxLayout;
    optRow->setContentsMargins(0, 6, 0, 0);

    m_moreBtn = new QPushButton(i18n("Load more..."), this);
    m_moreBtn->setEnabled(false);
    optRow->addWidget(m_moreBtn);

    optRow->addSpacing(16);

    // Revisionsauswahl
    auto *revGroup  = new QGroupBox(i18n("Revision"), this);
    auto *revLayout = new QHBoxLayout(revGroup);
    revLayout->setContentsMargins(8, 4, 8, 4);

    m_headRadio = new QRadioButton(i18n("HEAD"), revGroup);
    m_headRadio->setChecked(true);
    revLayout->addWidget(m_headRadio);

    m_revRadio = new QRadioButton(i18n("Revision:"), revGroup);
    revLayout->addWidget(m_revRadio);

    m_revSpinBox = new QSpinBox(revGroup);
    m_revSpinBox->setRange(1, 9'999'999);
    m_revSpinBox->setValue(1);
    m_revSpinBox->setEnabled(false);
    m_revSpinBox->setMinimumWidth(90);
    revLayout->addWidget(m_revSpinBox);

    optRow->addWidget(revGroup);
    optRow->addSpacing(16);

    // Tiefe
    auto *depthGroup  = new QGroupBox(i18n("Depth"), this);
    auto *depthLayout = new QHBoxLayout(depthGroup);
    depthLayout->setContentsMargins(8, 4, 8, 4);
    m_depthCombo = new QComboBox(depthGroup);
    m_depthCombo->addItem(i18n("Fully recursive"),     QStringLiteral("infinity"));
    m_depthCombo->addItem(i18n("Immediate children"),  QStringLiteral("immediates"));
    m_depthCombo->addItem(i18n("Only file children"),  QStringLiteral("files"));
    m_depthCombo->addItem(i18n("Only this item"),      QStringLiteral("empty"));
    depthLayout->addWidget(m_depthCombo);
    optRow->addWidget(depthGroup);

    optRow->addStretch();
    mainLayout->addLayout(optRow);

    // -----------------------------------------------------------------------
    // OK / Cancel
    // -----------------------------------------------------------------------
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(i18n("Update"));
    mainLayout->addWidget(buttons);

    // -----------------------------------------------------------------------
    // Verbindungen
    // -----------------------------------------------------------------------
    connect(m_revRadio,   &QRadioButton::toggled, m_revSpinBox, &QSpinBox::setEnabled);
    connect(m_revSpinBox, &QSpinBox::valueChanged, this, [this]() {
        m_revRadio->setChecked(true);
    });
    connect(m_logTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
                showEntry(item);
                if (!item) return;
                const int idx = item->data(0, Qt::UserRole + 1).toInt();
                if (idx < 0 || idx >= m_entries.size()) return;
                const long long rev = m_entries.at(idx).revision.toLongLong();
                if (rev > 0) {
                    m_revRadio->setChecked(true);
                    // Block valueChanged so it doesn't toggle the radio unnecessarily.
                    m_revSpinBox->blockSignals(true);
                    m_revSpinBox->setValue(static_cast<int>(rev));
                    m_revSpinBox->blockSignals(false);
                }
            });
    connect(m_logTree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *, int) { accept(); });
    connect(m_moreBtn, &QPushButton::clicked, this, &SvnUpdateDialog::loadMore);
    connect(buttons,   &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons,   &QDialogButtonBox::rejected, this, &QDialog::reject);

    loadMore();
}

// ---------------------------------------------------------------------------
// Load a page of log entries
// ---------------------------------------------------------------------------

void SvnUpdateDialog::reloadLog()
{
    m_logTree->clear();
    m_entries.clear();
    m_oldestRevision = -1;
    m_lastShownItem = nullptr;
    m_msgView->clear();
    m_pathsTree->clear();
    loadMore();
}

void SvnUpdateDialog::loadMore()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_moreBtn->setEnabled(false);

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
        auto *ph = new QTreeWidgetItem(m_logTree);
        ph->setText(0, i18n("No log available."));
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

        // Merged revisions (-g) as children of their merge revision; a click
        // to it is allowed (a global revision number = a valid update target).
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
        const int nl = e.message.indexOf(QLatin1Char('\n'));
        item->setText(3, nl >= 0 ? e.message.left(nl).trimmed() : e.message.trimmed());
        item->setToolTip(3, e.message.trimmed());

        if (e.mergeDepth == 0 && rev > 0
            && (m_oldestRevision < 0 || rev < m_oldestRevision))
            m_oldestRevision = rev;
    }
    m_logTree->setSortingEnabled(true);
    if (opts.includeMerged)
        m_logTree->expandAll();

    m_moreBtn->setEnabled(newTopLevel == kPageSize && m_oldestRevision > 1);

    if (m_logTree->currentItem() == nullptr && m_logTree->topLevelItemCount() > 0)
        m_logTree->setCurrentItem(m_logTree->topLevelItem(0));
}

// ---------------------------------------------------------------------------
// Show details for selected entry
// ---------------------------------------------------------------------------

void SvnUpdateDialog::showEntry(QTreeWidgetItem *treeItem)
{
    m_lastShownItem = treeItem;
    if (!treeItem) {
        m_msgView->clear();
        m_pathsTree->clear();
        return;
    }

    const int idx = treeItem->data(0, Qt::UserRole + 1).toInt();
    if (idx < 0 || idx >= m_entries.size()) return;
    const SvnLogEntry &e = m_entries.at(idx);

    m_msgView->setPlainText(e.message.trimmed());

    const bool onlyAffected = m_onlyAffectedBox
                              && m_onlyAffectedBox->isChecked()
                              && !m_repoRelPath.isEmpty();
    m_pathsTree->clear();
    for (const auto &[filePath, action] : e.changedPaths) {
        if (onlyAffected && filePath != m_repoRelPath
            && !filePath.startsWith(m_repoRelPath + QLatin1Char('/')))
            continue;
        auto *item = new QTreeWidgetItem(m_pathsTree);
        item->setText(0, actionLabel(action));
        item->setText(1, filePath);
        const QString icon = actionIconName(action);
        if (!icon.isEmpty())
            item->setIcon(0, QIcon::fromTheme(icon));
    }
}

// ---------------------------------------------------------------------------
// Result accessors
// ---------------------------------------------------------------------------

QString SvnUpdateDialog::revision() const
{
    return m_headRadio->isChecked() ? QStringLiteral("HEAD")
                                    : QString::number(m_revSpinBox->value());
}

QString SvnUpdateDialog::depth() const
{
    return m_depthCombo->currentData().toString();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QString SvnUpdateDialog::formatDate(const QString &isoDate)
{
    if (isoDate.length() < 19) return isoDate;
    return isoDate.left(10) + QLatin1Char(' ') + isoDate.mid(11, 5);
}

QString SvnUpdateDialog::actionLabel(const QString &action)
{
    if (action == QStringLiteral("A")) return i18n("Added");
    if (action == QStringLiteral("M")) return i18n("Modified");
    if (action == QStringLiteral("D")) return i18n("Deleted");
    if (action == QStringLiteral("R")) return i18n("Replaced");
    if (action == QStringLiteral("C")) return i18n("Copied");
    return action;
}

QString SvnUpdateDialog::actionIconName(const QString &action)
{
    if (action == QStringLiteral("A")) return QStringLiteral("vcs-added");
    if (action == QStringLiteral("M")) return QStringLiteral("vcs-locally-modified");
    if (action == QStringLiteral("D")) return QStringLiteral("vcs-removed");
    if (action == QStringLiteral("R")) return QStringLiteral("vcs-locally-modified");
    return {};
}
