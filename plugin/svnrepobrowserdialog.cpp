#include "svnrepobrowserdialog.h"
#include "svnbranchtagdialog.h"
#include "svncheckoutdialog.h"
#include "svnexportdialog.h"
#include "svnlogdialog.h"
#include "svnmanager.h"
#include "svnsettings.h"
#include "svnuihelpers.h"

#include <KLocalizedString>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSaveFile>
#include <QShortcut>
#include <QSpinBox>
#include <QTemporaryFile>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int kRoleUrl   = Qt::UserRole;      // volle Repository-URL
constexpr int kRoleIsDir = Qt::UserRole + 1;  // bool
constexpr int kRoleDummy = Qt::UserRole + 2;  // bool: lazy-load placeholder

QString formatDate(const QString &isoDate)
{
    if (isoDate.length() < 16) return isoDate;
    return isoDate.left(10) + QLatin1Char(' ') + isoDate.mid(11, 5);
}

// Prompts for a commit message; false = cancelled.
bool askCommitMessage(QWidget *parent, const QString &title, QString *message)
{
    bool ok = false;
    const QString msg = QInputDialog::getMultiLineText(
        parent, title, i18n("Commit message:"), QString(), &ok);
    if (!ok)
        return false;
    *message = msg.trimmed().isEmpty()
        ? QStringLiteral("(no message)") : msg;
    return true;
}

// Tree with internal drag & drop: a drop reports the desired move via a
// callback — the items themselves are NOT changed (the reload after the svn
// move refreshes the view).
class RepoTreeWidget : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;

    std::function<void(QTreeWidgetItem *source, QTreeWidgetItem *target)>
        onMoveRequested;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->source() == this)
            event->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (event->source() == this)
            event->acceptProposedAction();
    }
    void dropEvent(QDropEvent *event) override
    {
        // IgnoreAction instead of the propagated move action: otherwise Qt
        // removes the source row from the view after a drop accepted as a move,
        // even though our code never changes the model itself (svn move + reload
        // does that asynchronously).
        event->setDropAction(Qt::IgnoreAction);
        event->accept(); // do NOT call the base class — no item manipulation
        if (event->source() != this || !onMoveRequested)
            return;
        onMoveRequested(currentItem(),
                        itemAt(event->position().toPoint()));
    }
};

} // namespace

SvnRepoBrowserDialog::SvnRepoBrowserDialog(const QString &startUrl,
                                           SvnManager *mgr, QWidget *parent,
                                           Mode mode)
    : QDialog(parent), m_mgr(mgr), m_mode(mode)
{
    setWindowTitle(m_mode == Mode::PickUrl
                       ? i18n("Select Repository URL")
                       : i18n("Repository Browser"));
    resize(920, 620);

    auto *layout = new QVBoxLayout(this);

    // --- Header: URL + Go + Revision ---
    auto *headRow = new QHBoxLayout;
    headRow->addWidget(new QLabel(i18n("URL:"), this));

    m_urlCombo = new QComboBox(this);
    m_urlCombo->setEditable(true);
    m_urlCombo->setInsertPolicy(QComboBox::NoInsert);
    m_urlCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_urlCombo->lineEdit()->setPlaceholderText(
        i18n("e.g. svn+ssh://host/repo  or  file:///path/to/repo"));
    headRow->addWidget(m_urlCombo, 1);

    auto *goBtn = new QPushButton(
        QIcon::fromTheme(QStringLiteral("go-jump")), i18n("Go"), this);
    headRow->addWidget(goBtn);
    layout->addLayout(headRow);

    auto *revRow = new QHBoxLayout;
    m_headRadio = new QRadioButton(i18n("HEAD (latest)"), this);
    m_headRadio->setChecked(true);
    revRow->addWidget(m_headRadio);
    revRow->addSpacing(16);
    m_revRadio = new QRadioButton(i18n("Revision:"), this);
    revRow->addWidget(m_revRadio);
    m_revSpinBox = new QSpinBox(this);
    m_revSpinBox->setRange(1, 9'999'999);
    m_revSpinBox->setValue(1);
    m_revSpinBox->setEnabled(false);
    m_revSpinBox->setMinimumWidth(90);
    revRow->addWidget(m_revSpinBox);
    revRow->addStretch();
    layout->addLayout(revRow);

    // --- Baum ---
    auto *tree = new RepoTreeWidget(this);
    m_tree = tree;
    m_tree->setColumnCount(5);
    m_tree->setHeaderLabels({i18n("Name"), i18n("Revision"), i18n("Author"),
                             i18n("Date"), i18n("Size")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 5; ++c)
        m_tree->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_tree->setAlternatingRowColors(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    // Drag & drop move within the repository (svn move).
    m_tree->setDragEnabled(true);
    m_tree->viewport()->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(true);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    tree->onMoveRequested = [this](QTreeWidgetItem *src, QTreeWidgetItem *dst) {
        handleDragMove(src, dst);
    };
    layout->addWidget(m_tree, 1);

    auto *btnBox = new QDialogButtonBox(
        m_mode == Mode::PickUrl
            ? QDialogButtonBox::Ok | QDialogButtonBox::Cancel
            : QDialogButtonBox::StandardButtons(QDialogButtonBox::Close),
        this);
    if (m_mode == Mode::PickUrl) {
        m_okBtn = btnBox->button(QDialogButtonBox::Ok);
        m_okBtn->setEnabled(!startUrl.isEmpty());
        connect(btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    }
    layout->addWidget(btnBox);

    // --- History laden ---
    const QStringList urls = SvnUi::loadHistory(QStringLiteral("RepoBrowserHistory"));
    for (const QString &u : urls)
        m_urlCombo->addItem(u, u);
    m_urlCombo->setCurrentText(startUrl);

    // --- Verbindungen ---
    connect(m_revRadio,   &QRadioButton::toggled, m_revSpinBox, &QSpinBox::setEnabled);
    connect(m_revSpinBox, &QSpinBox::valueChanged, this, [this]() {
        m_revRadio->setChecked(true);
    });
    connect(m_headRadio, &QRadioButton::toggled, this,
            [this](bool) { loadRoot(); });
    connect(m_revSpinBox, &QSpinBox::editingFinished, this,
            &SvnRepoBrowserDialog::loadRoot);
    connect(goBtn, &QPushButton::clicked, this, &SvnRepoBrowserDialog::loadRoot);
    connect(m_urlCombo->lineEdit(), &QLineEdit::returnPressed,
            this, &SvnRepoBrowserDialog::loadRoot);
    connect(m_tree, &QTreeWidget::itemExpanded,
            this, &SvnRepoBrowserDialog::onItemExpanded);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) { onItemDoubleClicked(item); });
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &SvnRepoBrowserDialog::showContextMenu);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *f5 = new QShortcut(Qt::Key_F5, this);
    connect(f5, &QShortcut::activated, this, &SvnRepoBrowserDialog::loadRoot);

    if (m_mode == Mode::PickUrl) {
        const auto updateOk = [this]() {
            m_okBtn->setEnabled(!selectedUrl().isEmpty());
        };
        connect(m_tree, &QTreeWidget::itemSelectionChanged, this, updateOk);
        connect(m_urlCombo, &QComboBox::currentTextChanged, this, updateOk);
    }

    if (!startUrl.isEmpty())
        loadRoot();
}

QString SvnRepoBrowserDialog::selectedUrl() const
{
    if (QTreeWidgetItem *item = m_tree->currentItem()) {
        const QString url = item->data(0, kRoleUrl).toString();
        if (!url.isEmpty())
            return url;
    }
    return currentUrl();
}

QString SvnRepoBrowserDialog::currentUrl() const
{
    QString url = m_urlCombo->currentText().trimmed();
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    return url;
}

QString SvnRepoBrowserDialog::revision() const
{
    return m_headRadio->isChecked()
        ? QStringLiteral("HEAD")
        : QString::number(m_revSpinBox->value());
}

// ---------------------------------------------------------------------------
// Laden (lazy)
// ---------------------------------------------------------------------------

void SvnRepoBrowserDialog::loadRoot()
{
    m_tree->clear();
    const QString url = currentUrl();
    if (url.isEmpty())
        return;

    if (loadChildren(nullptr, url))
        saveUrlToHistory(url);
}

bool SvnRepoBrowserDialog::loadChildren(QTreeWidgetItem *parentItem,
                                        const QString &url)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = false;
    QList<SvnListEntry> entries = m_mgr->listSync(url, revision(), &ok);
    QApplication::restoreOverrideCursor();

    if (!ok) {
        auto *ph = parentItem ? new QTreeWidgetItem(parentItem)
                              : new QTreeWidgetItem(m_tree);
        ph->setText(0, i18n("Failed to list repository URL. "
                            "Check the URL and revision."));
        ph->setIcon(0, QIcon::fromTheme(QStringLiteral("dialog-warning")));
        ph->setFlags(Qt::NoItemFlags);
        return false;
    }

    // Folders first, then alphabetically.
    std::sort(entries.begin(), entries.end(),
              [](const SvnListEntry &a, const SvnListEntry &b) {
                  if (a.isDir != b.isDir)
                      return a.isDir;
                  return a.name.localeAwareCompare(b.name) < 0;
              });

    for (const SvnListEntry &e : std::as_const(entries)) {
        auto *item = parentItem ? new QTreeWidgetItem(parentItem)
                                : new QTreeWidgetItem(m_tree);
        item->setText(0, e.name);
        item->setIcon(0, e.isDir
            ? SvnUi::makeFolderIcon()
            : QIcon::fromTheme(QStringLiteral("text-x-generic")));
        item->setText(1, e.revision);
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(2, e.author);
        item->setText(3, formatDate(e.date));
        if (!e.isDir) {
            item->setText(4, QLocale().formattedDataSize(e.size));
            item->setTextAlignment(4, Qt::AlignRight | Qt::AlignVCenter);
        }
        if (!e.lockOwner.isEmpty()) {
            item->setIcon(1, QIcon::fromTheme(QStringLiteral("object-locked")));
            item->setToolTip(1, i18n("Locked by %1", e.lockOwner));
        }
        item->setData(0, kRoleUrl,
                      QString(url + QLatin1Char('/') + e.name));
        item->setData(0, kRoleIsDir, e.isDir);

        if (e.isDir) {
            // Lazy-load placeholder, so the expander appears.
            auto *dummy = new QTreeWidgetItem(item);
            dummy->setData(0, kRoleDummy, true);
            dummy->setText(0, QStringLiteral("…"));
        }
    }

    if (entries.isEmpty() && !parentItem) {
        auto *ph = new QTreeWidgetItem(m_tree);
        ph->setText(0, i18n("(empty)"));
        ph->setFlags(Qt::NoItemFlags);
    }
    return true;
}

void SvnRepoBrowserDialog::onItemExpanded(QTreeWidgetItem *item)
{
    // Not loaded yet? (exactly one dummy child)
    if (item->childCount() == 1
        && item->child(0)->data(0, kRoleDummy).toBool()) {
        delete item->takeChild(0);
        loadChildren(item, item->data(0, kRoleUrl).toString());
    }
}

// ---------------------------------------------------------------------------
// Open / save a file
// ---------------------------------------------------------------------------

void SvnRepoBrowserDialog::onItemDoubleClicked(QTreeWidgetItem *item)
{
    if (!item || item->data(0, kRoleIsDir).toBool()
        || item->data(0, kRoleUrl).toString().isEmpty())
        return; // folder: default expand; placeholder: nothing
    if (m_mode == Mode::PickUrl) {
        accept(); // file double-click = accept the selection
        return;
    }
    openFileAtRevision(item->data(0, kRoleUrl).toString(), item->text(0));
}

void SvnRepoBrowserDialog::openFileAtRevision(const QString &url,
                                              const QString &name)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QByteArray content = m_mgr->cat(url, revision());
    QApplication::restoreOverrideCursor();

    QString tmpDir = SvnSettings::tempDirectory();
    if (tmpDir.isEmpty() || !QDir(tmpDir).exists())
        tmpDir = QDir::tempPath();
    const QString suffix = QFileInfo(name).suffix();
    const QString tmpl = tmpDir + QStringLiteral("/repobrowser_XXXXXX")
        + (suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix);

    QTemporaryFile tmp(tmpl);
    tmp.setAutoRemove(false);
    if (!tmp.open()) {
        QMessageBox::warning(this, i18n("Open File"),
                             i18n("Cannot create temporary file."));
        return;
    }
    tmp.write(content);
    const QString tmpPath = tmp.fileName();
    tmp.close();

    QDesktopServices::openUrl(QUrl::fromLocalFile(tmpPath));
}

void SvnRepoBrowserDialog::saveRevisionAs(const QString &url,
                                          const QString &name)
{
    const QString target = QFileDialog::getSaveFileName(
        this, i18n("Save Revision As"), name);
    if (target.isEmpty())
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QByteArray content = m_mgr->cat(url, revision());
    QApplication::restoreOverrideCursor();

    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(content) < 0 || !file.commit()) {
        QMessageBox::warning(this, i18n("Save Revision As"),
                             i18n("Cannot save file:\n%1", target));
    }
}

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------

void SvnRepoBrowserDialog::showContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    const QString itemUrl = item
        ? item->data(0, kRoleUrl).toString() : QString();
    if (item && itemUrl.isEmpty())
        return; // placeholder row

    const bool isDir  = !item || item->data(0, kRoleIsDir).toBool();
    // No item clicked → actions on the base URL (e.g. Create folder).
    const QString url = item ? itemUrl : currentUrl();
    const QString name = item ? item->text(0) : QString();

    QMenu menu(this);

    if (!isDir) {
        connect(menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")),
                               i18n("Open")),
                &QAction::triggered, this,
                [this, url, name]() { openFileAtRevision(url, name); });
        connect(menu.addAction(QIcon::fromTheme(QStringLiteral("document-save-as")),
                               i18n("Save revision as...")),
                &QAction::triggered, this,
                [this, url, name]() { saveRevisionAs(url, name); });
        menu.addSeparator();
    }

    if (isDir) {
        connect(menu.addAction(
                    QIcon::fromTheme(QStringLiteral("vcs-update-cvs-cervisia")),
                    i18n("Checkout...")),
                &QAction::triggered, this, [this, url]() {
                    SvnCheckoutDialog dlg(QDir::homePath(), this);
                    dlg.setUrl(url);
                    if (dlg.exec() != QDialog::Accepted) return;
                    const QString u = dlg.url(), lp = dlg.localPath();
                    const QString rev = dlg.revision(), dep = dlg.depth();
                    runOperation(i18n("SVN: Checkout"), [this, u, lp, rev, dep]() {
                        m_mgr->checkoutAsync(u, lp, rev, dep);
                    });
                });
        connect(menu.addAction(QIcon::fromTheme(QStringLiteral("document-export")),
                               i18n("Export...")),
                &QAction::triggered, this, [this, url]() {
                    SvnExportDialog dlg(url, this);
                    if (dlg.exec() != QDialog::Accepted) return;
                    const QString src = dlg.srcPath(), dst = dlg.destPath();
                    const QString rev = dlg.revision();
                    const bool frc = dlg.force();
                    runOperation(i18n("SVN: Export"), [this, src, dst, rev, frc]() {
                        m_mgr->exportAsync(src, dst, rev, frc);
                    });
                });
    }

    connect(menu.addAction(QIcon::fromTheme(QStringLiteral("view-history")),
                           i18n("Show Log")),
            &QAction::triggered, this, [this, url]() {
                SvnLogDialog dlg(url, m_mgr, this);
                dlg.exec();
            });

    if (isDir) {
        connect(menu.addAction(QIcon::fromTheme(QStringLiteral("vcs-branch")),
                               i18n("Branch/Tag from here...")),
                &QAction::triggered, this, [this, url]() {
                    SvnBranchTagDialog dlg(QString(), m_mgr, this);
                    dlg.setSourceUrl(url);
                    if (dlg.exec() != QDialog::Accepted) return;
                    const QString src = dlg.sourceUrl(), dst = dlg.targetUrl();
                    const QString rev = dlg.revision(), msg = dlg.message();
                    runOperation(i18n("SVN: Branch / Tag"),
                                 [this, src, dst, msg, rev]() {
                                     m_mgr->copyAsync(src, dst, msg, rev);
                                 });
                });
    }

    connect(menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")),
                           i18n("Copy URL")),
            &QAction::triggered, this, [url]() {
                QGuiApplication::clipboard()->setText(url);
            });

    // --- Write operations (direct repo commit) ---
    menu.addSeparator();

    if (isDir) {
        connect(menu.addAction(QIcon::fromTheme(QStringLiteral("folder-new")),
                               i18n("Create folder...")),
                &QAction::triggered, this,
                [this, url]() { createFolder(url); });
    }
    if (item) {
        connect(menu.addAction(QIcon::fromTheme(QStringLiteral("edit-rename")),
                               i18n("Rename/Move...")),
                &QAction::triggered, this,
                [this, url, name]() { renameEntry(url, name); });
        connect(menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")),
                               i18n("Delete...")),
                &QAction::triggered, this,
                [this, url, name]() { deleteEntry(url, name); });
    }

    connect(menu.addAction(QIcon::fromTheme(QStringLiteral("view-refresh")),
                           i18n("Refresh")),
            &QAction::triggered, this, &SvnRepoBrowserDialog::loadRoot);

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// Schreiboperationen
// ---------------------------------------------------------------------------

void SvnRepoBrowserDialog::createFolder(const QString &parentUrl)
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, i18n("Create Folder"), i18n("Folder name:"),
        QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;

    QString msg;
    if (!askCommitMessage(this, i18n("Create Folder"), &msg))
        return;

    const QString newUrl = parentUrl + QLatin1Char('/') + name;
    runOperation(i18n("SVN: Create Folder"), [this, newUrl, msg]() {
        m_mgr->mkdirUrlAsync(newUrl, msg);
    });
}

void SvnRepoBrowserDialog::renameEntry(const QString &url, const QString &name)
{
    bool ok = false;
    const QString newName = QInputDialog::getText(
        this, i18n("Rename/Move"),
        i18n("New name (or relative path within the parent folder):"),
        QLineEdit::Normal, name, &ok).trimmed();
    if (!ok || newName.isEmpty() || newName == name)
        return;

    QString msg;
    if (!askCommitMessage(this, i18n("Rename/Move"), &msg))
        return;

    const QString parent = url.left(url.lastIndexOf(QLatin1Char('/')));
    const QString dstUrl = parent + QLatin1Char('/') + newName;
    runOperation(i18n("SVN: Rename/Move"), [this, url, dstUrl, msg]() {
        m_mgr->moveUrlAsync(url, dstUrl, msg);
    });
}

void SvnRepoBrowserDialog::deleteEntry(const QString &url, const QString &name)
{
    const auto ret = QMessageBox::warning(
        this, i18n("Delete"),
        i18n("Really delete \"%1\" from the repository?\n"
             "This creates a new commit (the item remains in older revisions).",
             name),
        QMessageBox::Yes | QMessageBox::Cancel);
    if (ret != QMessageBox::Yes)
        return;

    QString msg;
    if (!askCommitMessage(this, i18n("Delete"), &msg))
        return;

    runOperation(i18n("SVN: Delete"), [this, url, msg]() {
        m_mgr->deleteUrlAsync(url, msg);
    });
}

// Drag & drop: move the source onto the target folder (svn move = 1 commit).
void SvnRepoBrowserDialog::handleDragMove(QTreeWidgetItem *source,
                                          QTreeWidgetItem *target)
{
    if (!source)
        return;
    const QString srcUrl = source->data(0, kRoleUrl).toString();
    if (srcUrl.isEmpty())
        return; // placeholder row

    // Determine the target folder: a folder item directly, a file item → its
    // folder, empty space → the base URL.
    QString dstDirUrl;
    if (!target) {
        dstDirUrl = currentUrl();
    } else {
        const QString targetUrl = target->data(0, kRoleUrl).toString();
        if (targetUrl.isEmpty())
            return;
        dstDirUrl = target->data(0, kRoleIsDir).toBool()
            ? targetUrl
            : targetUrl.left(targetUrl.lastIndexOf(QLatin1Char('/')));
    }

    const QString srcParent = srcUrl.left(srcUrl.lastIndexOf(QLatin1Char('/')));
    if (dstDirUrl == srcParent)
        return; // same location — nothing to do
    if (dstDirUrl == srcUrl
        || dstDirUrl.startsWith(srcUrl + QLatin1Char('/'))) {
        QMessageBox::warning(this, i18n("Move"),
            i18n("Cannot move a folder into itself."));
        return;
    }

    const QString name = source->text(0);
    const auto ret = QMessageBox::question(
        this, i18n("Move"),
        i18n("Move \"%1\" to\n%2 ?\n\nThis creates a new commit.",
             name, dstDirUrl));
    if (ret != QMessageBox::Yes)
        return;

    QString msg;
    if (!askCommitMessage(this, i18n("Move"), &msg))
        return;

    const QString dstUrl = dstDirUrl + QLatin1Char('/') + name;
    runOperation(i18n("SVN: Move"), [this, srcUrl, dstUrl, msg]() {
        m_mgr->moveUrlAsync(srcUrl, dstUrl, msg);
    });
}

// ---------------------------------------------------------------------------
// Hilfsfunktionen
// ---------------------------------------------------------------------------

void SvnRepoBrowserDialog::runOperation(const QString &title,
                                        std::function<void()> startOp)
{
    SvnUi::runWithProgress(m_mgr, title, this, startOp, [this]() { loadRoot(); });
}

void SvnRepoBrowserDialog::saveUrlToHistory(const QString &url)
{
    SvnUi::pushHistory(QStringLiteral("RepoBrowserHistory"), url);
}
