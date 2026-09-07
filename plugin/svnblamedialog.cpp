#include "svnblamedialog.h"
#include "svnlogdialog.h"
#include "../svn/svnmanager.h"

#include <KLocalizedString>

#include <QApplication>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static QString formatDate(const QString &iso)
{
    // "2024-01-15T10:30:00.000000Z" → "2024-01-15"
    return iso.left(10);
}

// Returns a background color for a given revision index, adapted to the
// current palette (light / dark mode).
static QColor revColor(int index, const QPalette &pal)
{
    // 12 evenly-spaced hues around the color wheel
    static const int hues[] = {0, 120, 240, 60, 180, 300, 30, 150, 270, 90, 210, 330};
    const int h = hues[index % 12];

    const bool dark = pal.color(QPalette::Base).lightness() < 128;
    // Light mode: high-lightness pastels; dark mode: low-lightness muted tones
    return QColor::fromHsl(h, dark ? 70 : 55, dark ? 55 : 225);
}

// ---------------------------------------------------------------------------
// SvnBlameDialog
// ---------------------------------------------------------------------------

SvnBlameDialog::SvnBlameDialog(const QString &path, SvnManager *mgr, QWidget *parent)
    : QDialog(parent)
    , m_path(path)
    , m_mgr(mgr)
{
    setWindowTitle(i18n("SVN Blame – %1", QFileInfo(path).fileName()));
    setMinimumSize(860, 540);
    resize(1020, 640);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({
        i18n("Line"), i18n("Rev"), i18n("Author"), i18n("Date"), i18n("Content")
    });
    m_table->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(false); // custom colors per revision
    m_table->setWordWrap(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(
        m_table->fontMetrics().height() + 4);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    layout->addWidget(m_table);

    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, &SvnBlameDialog::showContextMenu);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    loadBlame();
}

void SvnBlameDialog::loadBlame()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QList<SvnBlameEntry> entries = m_mgr->blameSync(m_path);
    QApplication::restoreOverrideCursor();

    if (entries.isEmpty()) {
        m_table->setEnabled(false);
        m_table->setRowCount(1);
        m_table->setColumnCount(1);
        m_table->setHorizontalHeaderLabels({QString()});
        auto *msg = new QTableWidgetItem(
            i18n("No blame information available. "
                 "The file may be unversioned, binary, or not yet committed."));
        msg->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(0, 0, msg);
        return;
    }

    // Assign a stable color index to each unique revision (in order of first
    // appearance so adjacent same-revision blocks share their color).
    QHash<QString, int> revIndex;
    for (const SvnBlameEntry &e : entries) {
        if (!revIndex.contains(e.revision))
            revIndex.insert(e.revision, revIndex.size());
    }

    const QPalette &pal = palette();
    const QColor textColor = pal.color(QPalette::Text);

    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(entries.size());

    const int digits = QString::number(entries.size()).length();

    for (int row = 0; row < entries.size(); ++row) {
        const SvnBlameEntry &e = entries[row];
        const QColor bg = revColor(revIndex.value(e.revision), pal);

        auto makeItem = [&](const QString &text, Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
            auto *it = new QTableWidgetItem(text);
            it->setBackground(bg);
            it->setForeground(textColor);
            it->setTextAlignment(align);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            return it;
        };

        m_table->setItem(row, 0,
            makeItem(QString::number(e.lineNumber).rightJustified(digits),
                     Qt::AlignRight | Qt::AlignVCenter));
        m_table->setItem(row, 1,
            makeItem(QStringLiteral("r") + e.revision,
                     Qt::AlignRight | Qt::AlignVCenter));
        m_table->setItem(row, 2, makeItem(e.author));
        m_table->setItem(row, 3, makeItem(formatDate(e.date)));
        m_table->setItem(row, 4, makeItem(e.text));
    }

    m_table->setUpdatesEnabled(true);
}

// ---------------------------------------------------------------------------
// Context menu: Show Log / Show Diff for selected revision
// ---------------------------------------------------------------------------

void SvnBlameDialog::showContextMenu(const QPoint &pos)
{
    // Use the row under the cursor; currentRow() may differ from the
    // actually right-clicked row (e.g. when nothing was selected yet).
    QTableWidgetItem *hit = m_table->itemAt(pos);
    const int row = hit ? hit->row() : m_table->currentRow();
    if (row < 0) return;

    QTableWidgetItem *revItem = m_table->item(row, 1);
    if (!revItem) return;

    // Revision text is stored as "r123" — strip the "r" prefix.
    const QString revText = revItem->text();
    if (revText.isEmpty() || !revText.startsWith(QLatin1Char('r'))) return;
    const long long rev = revText.mid(1).toLongLong();
    if (rev <= 0) return;

    QMenu menu(this);

    connect(menu.addAction(QIcon::fromTheme(QStringLiteral("view-history")),
                           i18n("Show Log (jump to r%1)", rev)),
            &QAction::triggered, this, [this, rev]() {
                auto *dlg = new SvnLogDialog(m_path, m_mgr, this);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                dlg->show();
                dlg->scrollToRevision(rev);
            });

    if (rev > 1) {
        connect(menu.addAction(QIcon::fromTheme(QStringLiteral("vcs-diff")),
                               i18n("Show Diff (r%1 → r%2)", rev - 1, rev)),
                &QAction::triggered, this, [this, rev]() {
                    QApplication::setOverrideCursor(Qt::WaitCursor);
                    const QString diff = m_mgr->diffChangeSync(
                        m_path, QString::number(rev));
                    QApplication::restoreOverrideCursor();

                    auto *dlg = new QDialog(this);
                    dlg->setAttribute(Qt::WA_DeleteOnClose);
                    dlg->setWindowTitle(i18n("Diff r%1 → r%2 – %3",
                                             rev - 1, rev,
                                             QFileInfo(m_path).fileName()));
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
                });
    }

    menu.exec(m_table->viewport()->mapToGlobal(pos));
}
