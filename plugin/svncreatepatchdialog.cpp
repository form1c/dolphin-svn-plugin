#include "svncreatepatchdialog.h"
#include "svnmanager.h"
#include "svnuihelpers.h"

#include <KLocalizedString>

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

// ---------------------------------------------------------------------------
// Helpers (deliberate duplicates of svncommitdialog.cpp — there they are bound
// to the commit semantics, here to the diff semantics)
// ---------------------------------------------------------------------------

static QString statusText(SvnFileStatus s)
{
    switch (s) {
    case SvnFileStatus::Modified:   return i18n("Modified");
    case SvnFileStatus::Added:      return i18n("Added");
    case SvnFileStatus::Deleted:    return i18n("Deleted");
    case SvnFileStatus::Replaced:   return i18n("Replaced");
    case SvnFileStatus::Conflicted: return i18n("Conflicted");
    default:                        return QString();
    }
}

static QIcon statusIcon(SvnFileStatus s)
{
    switch (s) {
    case SvnFileStatus::Modified:
    case SvnFileStatus::Replaced:   return QIcon::fromTheme(QStringLiteral("vcs-locally-modified"));
    case SvnFileStatus::Added:      return QIcon::fromTheme(QStringLiteral("vcs-added"));
    case SvnFileStatus::Deleted:    return QIcon::fromTheme(QStringLiteral("vcs-removed"));
    case SvnFileStatus::Conflicted: return QIcon::fromTheme(QStringLiteral("vcs-conflicting"));
    default:                        return QIcon();
    }
}

// 'svn diff' represents exactly these states. "Missing" is deliberately absent:
// a file deleted without 'svn rm' is still versioned for svn, its diff would be
// empty — the entry would only mislead the user.
static bool isDiffable(const SvnStatusEntry &e)
{
    switch (e.textStatus) {
    case SvnFileStatus::Modified:
    case SvnFileStatus::Added:
    case SvnFileStatus::Deleted:
    case SvnFileStatus::Replaced:
    case SvnFileStatus::Conflicted:
        return true;
    default:
        return e.propStatus == SvnFileStatus::Modified;
    }
}

// ---------------------------------------------------------------------------

SvnCreatePatchDialog::SvnCreatePatchDialog(const QStringList &paths,
                                            SvnManager *mgr, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("SVN: Create Patch"));
    resize(660, 460);

    auto *layout = new QVBoxLayout(this);

    auto *headerRow = new QHBoxLayout;
    headerRow->addWidget(new QLabel(i18n("Changes to include in the patch:"), this));
    headerRow->addStretch();
    m_countLabel = new QLabel(this);
    headerRow->addWidget(m_countLabel);
    layout->addLayout(headerRow);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({i18n("Path"), i18n("Status")});
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->header()->setStretchLastSection(false);
    layout->addWidget(m_tree, 1);

    auto *btnRow = new QHBoxLayout;
    auto *selectAllBtn   = new QPushButton(i18n("Select All"), this);
    auto *deselectAllBtn = new QPushButton(i18n("Select None"), this);
    btnRow->addWidget(selectAllBtn);
    btnRow->addWidget(deselectAllBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    // --- Zieldatei ---
    auto *fileRow = new QHBoxLayout;
    fileRow->addWidget(new QLabel(i18n("Patch file:"), this));
    m_fileEdit = new QLineEdit(this);
    fileRow->addWidget(m_fileEdit, 1);
    auto *browseBtn = new QPushButton(i18n("Browse…"), this);
    fileRow->addWidget(browseBtn);
    layout->addLayout(fileRow);

    // --- Buttons ---
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_saveBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-save")),
                                i18n("Save Patch"), this);
    m_saveBtn->setDefault(true);
    buttons->addButton(m_saveBtn, QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &SvnCreatePatchDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    populateTree(paths, mgr);

    // Default: <wcname>.patch next to the working copy.
    const QDir wcDir(m_wcRoot);
    const QString defaultName =
        (wcDir.dirName().isEmpty() ? QStringLiteral("changes") : wcDir.dirName())
        + QStringLiteral(".patch");
    m_fileEdit->setText(QFileInfo(m_wcRoot).absolutePath()
                        + QLatin1Char('/') + defaultName);

    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getSaveFileName(
            this, i18n("Save Patch As"), m_fileEdit->text(),
            i18n("Patch files (*.patch *.diff);;All files (*)"));
        if (!file.isEmpty())
            m_fileEdit->setText(file);
    });
    connect(m_fileEdit, &QLineEdit::textChanged,
            this, &SvnCreatePatchDialog::updateState);
    connect(m_tree, &QTreeWidget::itemChanged,
            this, [this](QTreeWidgetItem *, int) { updateState(); });

    auto applyToAll = [this](Qt::CheckState state) {
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
            m_tree->topLevelItem(i)->setCheckState(0, state);
    };
    connect(selectAllBtn,   &QPushButton::clicked, this, [applyToAll] { applyToAll(Qt::Checked);   });
    connect(deselectAllBtn, &QPushButton::clicked, this, [applyToAll] { applyToAll(Qt::Unchecked); });

    updateState();
}

void SvnCreatePatchDialog::populateTree(const QStringList &paths, SvnManager *mgr)
{
    const SvnInfo wcInfo = mgr->info(paths.first());
    m_wcRoot = wcInfo.valid
        ? wcInfo.wcRootPath
        : (QFileInfo(paths.first()).isDir() ? paths.first()
                                            : QFileInfo(paths.first()).absolutePath());

    QList<SvnStatusEntry> entries;
    QSet<QString> seen;
    bool anyStatusFailed = false;
    for (const QString &p : paths) {
        const QFileInfo fi(p);
        bool ok = false;
        const auto statusEntries = mgr->status(fi.absoluteFilePath(), fi.isDir(), &ok);
        if (!ok) anyStatusFailed = true;
        for (const SvnStatusEntry &e : statusEntries) {
            const QString abs = QFileInfo(e.path).absoluteFilePath();
            if (seen.contains(abs)) continue;
            seen.insert(abs);
            if (isDiffable(e))
                entries << e;
        }
    }

    if (entries.isEmpty()) {
        m_tree->setEnabled(false);
        auto *placeholder = new QTreeWidgetItem(m_tree);
        if (anyStatusFailed) {
            placeholder->setText(0, i18n("Failed to retrieve SVN status. Check that "
                                         "svn is installed and this is a valid working copy."));
            placeholder->setIcon(0, QIcon::fromTheme(QStringLiteral("dialog-warning")));
        } else {
            placeholder->setText(0, i18n("No local changes to save as a patch."));
        }
        placeholder->setFlags(Qt::NoItemFlags);
        return;
    }

    std::sort(entries.begin(), entries.end(),
              [](const SvnStatusEntry &a, const SvnStatusEntry &b) {
                  return QFileInfo(a.path).absoluteFilePath()
                       < QFileInfo(b.path).absoluteFilePath();
              });

    QFileIconProvider iconProvider;
    const QDir wcDir(m_wcRoot);
    for (const SvnStatusEntry &e : std::as_const(entries)) {
        const QString abs = QFileInfo(e.path).absoluteFilePath();
        const QFileInfo fi(abs);
        const bool onlyPropMod = statusText(e.textStatus).isEmpty();

        auto *item = new QTreeWidgetItem(m_tree);
        item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setCheckState(0, Qt::Checked);
        item->setText(0, wcDir.relativeFilePath(abs));
        item->setToolTip(0, abs);
        item->setIcon(0, fi.isDir() ? SvnUi::makeFolderIcon() : iconProvider.icon(fi));
        item->setText(1, onlyPropMod ? i18n("Prop. Modified") : statusText(e.textStatus));
        const QIcon ico = onlyPropMod
            ? QIcon::fromTheme(QStringLiteral("vcs-locally-modified"))
            : statusIcon(e.textStatus);
        if (!ico.isNull())
            item->setIcon(1, ico);
        item->setData(0, Qt::UserRole, abs);
    }

    m_tree->resizeColumnToContents(1);
    m_tree->resizeColumnToContents(0);
}

void SvnCreatePatchDialog::updateState()
{
    const QStringList sel = selectedPaths();
    m_countLabel->setText(i18np("%1 file selected", "%1 files selected", sel.size()));
    m_saveBtn->setEnabled(!sel.isEmpty()
                          && !m_fileEdit->text().trimmed().isEmpty());
}

QStringList SvnCreatePatchDialog::selectedPaths() const
{
    QStringList out;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (item->checkState(0) == Qt::Checked) {
            const QString p = item->data(0, Qt::UserRole).toString();
            if (!p.isEmpty())
                out << p;
        }
    }
    return out;
}

QString SvnCreatePatchDialog::outputFile() const
{
    return m_fileEdit->text().trimmed();
}

void SvnCreatePatchDialog::accept()
{
    const QString file = outputFile();
    if (file.isEmpty() || selectedPaths().isEmpty())
        return;

    if (QFileInfo::exists(file)) {
        const auto ret = QMessageBox::question(
            this, i18n("SVN: Create Patch"),
            i18n("The file\n%1\nalready exists. Overwrite it?", file),
            QMessageBox::Yes | QMessageBox::Cancel);
        if (ret != QMessageBox::Yes)
            return;
    }

    QDialog::accept();
}
