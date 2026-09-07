#include "svnmergedialog.h"
#include "svnlogpickdialog.h"
#include "svnmanager.h"
#include "svnprogressdialog.h"
#include "svntypes.h"
#include "svnuihelpers.h"

#include <KLocalizedString>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

SvnMergeDialog::SvnMergeDialog(const QString &wcPath, SvnManager *mgr,
                               QWidget *parent)
    : QDialog(parent), m_mgr(mgr), m_wcPath(wcPath)
{
    setWindowTitle(i18n("SVN Merge"));
    setMinimumWidth(680);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    // --- Target (working copy) ---
    auto *targetGroup  = new QGroupBox(i18n("Merge Target (Working Copy)"), this);
    auto *targetLayout = new QVBoxLayout(targetGroup);
    auto *pathLabel = new QLabel(wcPath, targetGroup);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    targetLayout->addWidget(pathLabel);
    if (m_mgr) {
        m_wcInfo = m_mgr->info(wcPath);
        if (m_wcInfo.valid && !m_wcInfo.url.isEmpty()) {
            auto *urlLabel = new QLabel(i18n("URL: %1", m_wcInfo.url),
                                        targetGroup);
            urlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            targetLayout->addWidget(urlLabel);
        }
    }
    layout->addWidget(targetGroup);

    // --- Modus A: Revisions-Range ---
    m_rangeRadio = new QRadioButton(
        i18n("Merge a range of revisions"), this);
    m_rangeRadio->setChecked(true);
    layout->addWidget(m_rangeRadio);

    auto *rangeGroup  = new QGroupBox(this);
    auto *rangeLayout = new QGridLayout(rangeGroup);

    rangeLayout->addWidget(new QLabel(i18n("Source URL:"), rangeGroup), 0, 0);
    m_srcCombo = new QComboBox(rangeGroup);
    m_srcCombo->setEditable(true);
    m_srcCombo->setInsertPolicy(QComboBox::NoInsert);
    m_srcCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_srcCombo->lineEdit()->setPlaceholderText(
        i18n("e.g. svn+ssh://host/repo/branches/feature-x"));
    rangeLayout->addWidget(m_srcCombo, 0, 1);
    m_srcBrowse = new QPushButton(
        QIcon::fromTheme(QStringLiteral("folder-remote")),
        i18n("Browse..."), rangeGroup);
    connect(m_srcBrowse, &QPushButton::clicked, this, [this]() {
        const QString url = SvnUi::pickRepoUrl(sourceUrl(), m_wcInfo, m_mgr, this);
        if (!url.isEmpty())
            m_srcCombo->setCurrentText(url);
    });
    rangeLayout->addWidget(m_srcBrowse, 0, 2);

    rangeLayout->addWidget(new QLabel(i18n("Revisions:"), rangeGroup), 1, 0);
    m_rangesEdit = new QLineEdit(rangeGroup);
    m_rangesEdit->setPlaceholderText(
        i18n("empty = all eligible revisions;  e.g. 5-10,14"));
    static const QRegularExpression rangesRx(
        QStringLiteral("^[0-9,\\-\\s]*$"));
    m_rangesEdit->setValidator(
        new QRegularExpressionValidator(rangesRx, m_rangesEdit));
    rangeLayout->addWidget(m_rangesEdit, 1, 1);
    m_logPickBtn = new QPushButton(
        QIcon::fromTheme(QStringLiteral("view-history")),
        i18n("Show Log..."), rangeGroup);
    m_logPickBtn->setToolTip(
        i18n("Pick the revisions to merge from the log of the source URL"));
    rangeLayout->addWidget(m_logPickBtn, 1, 2);

    layout->addWidget(rangeGroup);

    // --- Mode B: two trees ---
    m_treeRadio = new QRadioButton(i18n("Merge two different trees"), this);
    layout->addWidget(m_treeRadio);

    auto *treeGroup  = new QGroupBox(this);
    auto *treeLayout = new QGridLayout(treeGroup);

    // A container WIDGET instead of a sub-layout in the grid cell — sub-layouts
    // via addLayout drifted to the top-left corner of the group box (layout bug).
    const auto makeRevRow = [this, treeGroup](QRadioButton **headRadio,
                                              QSpinBox **spin,
                                              QLineEdit *urlEdit) {
        auto *rowWidget = new QWidget(treeGroup);
        auto *row = new QHBoxLayout(rowWidget);
        row->setContentsMargins(0, 0, 0, 0);
        *headRadio = new QRadioButton(i18n("HEAD"), rowWidget);
        (*headRadio)->setChecked(true);
        row->addWidget(*headRadio);
        auto *revRadio = new QRadioButton(i18n("Revision:"), rowWidget);
        row->addWidget(revRadio);
        *spin = new QSpinBox(rowWidget);
        (*spin)->setRange(1, 9'999'999);
        (*spin)->setEnabled(false);
        (*spin)->setMinimumWidth(90);
        QObject::connect(revRadio, &QRadioButton::toggled,
                         *spin, &QSpinBox::setEnabled);
        QObject::connect(*spin, &QSpinBox::valueChanged, revRadio,
                         [revRadio]() { revRadio->setChecked(true); });
        row->addWidget(*spin);
        auto *logBtn = new QPushButton(
            QIcon::fromTheme(QStringLiteral("view-history")),
            i18n("Show Log..."), rowWidget);
        QSpinBox *spinPtr = *spin;
        connect(logBtn, &QPushButton::clicked, this,
                [this, urlEdit, spinPtr, revRadio]() {
            const QString url = urlEdit->text().trimmed();
            if (url.isEmpty())
                return;
            SvnLogPickDialog dlg(url, m_mgr,
                                 SvnLogPickDialog::SelectionMode::Single, this);
            if (dlg.exec() != QDialog::Accepted)
                return;
            const long long rev = dlg.selectedRevision();
            if (rev <= 0)
                return;
            spinPtr->setValue(int(rev));
            revRadio->setChecked(true); // valueChanged does not fire for an unchanged value
        });
        row->addWidget(logBtn);
        row->addStretch();
        return rowWidget;
    };

    const auto makeUrlBrowse = [this, treeGroup](QLineEdit *edit) {
        auto *btn = new QPushButton(
            QIcon::fromTheme(QStringLiteral("folder-remote")),
            i18n("Browse..."), treeGroup);
        connect(btn, &QPushButton::clicked, this, [this, edit]() {
            const QString url = SvnUi::pickRepoUrl(edit->text().trimmed(),
                                                   m_wcInfo, m_mgr, this);
            if (!url.isEmpty())
                edit->setText(url);
        });
        return btn;
    };

    treeLayout->addWidget(new QLabel(i18n("From URL (start):"), treeGroup), 0, 0);
    m_url1Edit = new QLineEdit(treeGroup);
    treeLayout->addWidget(m_url1Edit, 0, 1);
    m_url1Browse = makeUrlBrowse(m_url1Edit);
    treeLayout->addWidget(m_url1Browse, 0, 2);
    m_rev1Row = makeRevRow(&m_head1Radio, &m_rev1Spin, m_url1Edit);
    treeLayout->addWidget(m_rev1Row, 1, 1);

    treeLayout->addWidget(new QLabel(i18n("To URL (end):"), treeGroup), 2, 0);
    m_url2Edit = new QLineEdit(treeGroup);
    treeLayout->addWidget(m_url2Edit, 2, 1);
    m_url2Browse = makeUrlBrowse(m_url2Edit);
    treeLayout->addWidget(m_url2Browse, 2, 2);
    m_rev2Row = makeRevRow(&m_head2Radio, &m_rev2Spin, m_url2Edit);
    treeLayout->addWidget(m_rev2Row, 3, 1);

    layout->addWidget(treeGroup);

    // --- Merge options (modelled on TortoiseSVN), apply to both modes ---
    auto *optGroup  = new QGroupBox(i18n("Merge options"), this);
    auto *optLayout = new QGridLayout(optGroup);

    QSettings optSettings(QStringLiteral("DolphinSvnPlugin"),
                          QStringLiteral("MergeHistory"));

    optLayout->addWidget(new QLabel(i18n("Merge depth:"), optGroup), 0, 0);
    m_depthCombo = new QComboBox(optGroup);
    m_depthCombo->addItem(i18n("Working copy"), QString());
    m_depthCombo->addItem(i18n("Fully recursive"),
                          QStringLiteral("infinity"));
    m_depthCombo->addItem(i18n("Immediate children, including folders"),
                          QStringLiteral("immediates"));
    m_depthCombo->addItem(i18n("Only file children"),
                          QStringLiteral("files"));
    m_depthCombo->addItem(i18n("Only this item"), QStringLiteral("empty"));
    m_depthCombo->setCurrentIndex(qMax(0, m_depthCombo->findData(
        optSettings.value(QStringLiteral("mergeDepth")).toString())));
    optLayout->addWidget(m_depthCombo, 0, 1);

    auto *checkRow = new QHBoxLayout;
    m_ignoreAncestryBox = new QCheckBox(i18n("Ignore ancestry"), optGroup);
    m_ignoreAncestryBox->setToolTip(
        i18n("Compare paths only, without taking history into account"));
    checkRow->addWidget(m_ignoreAncestryBox);
    m_forceBox = new QCheckBox(i18n("Force the merge"), optGroup);
    m_forceBox->setToolTip(
        i18n("Avoid a tree conflict when an incoming delete affects a "
             "locally modified or unversioned file"));
    checkRow->addWidget(m_forceBox);
    m_recordOnlyBox = new QCheckBox(i18n("Only record the merge"), optGroup);
    m_recordOnlyBox->setToolTip(
        i18n("Mark the revisions as merged (svn:mergeinfo) without "
             "changing any file"));
    checkRow->addWidget(m_recordOnlyBox);
    checkRow->addStretch();
    optLayout->addLayout(checkRow, 1, 0, 1, 2);

    auto *wsRow = new QHBoxLayout;
    wsRow->addWidget(new QLabel(i18n("Whitespace:"), optGroup));
    m_whitespaceCombo = new QComboBox(optGroup);
    m_whitespaceCombo->addItem(i18n("Compare whitespaces"), QString());
    m_whitespaceCombo->addItem(i18n("Ignore whitespace changes"),
                               QStringLiteral("-b"));
    m_whitespaceCombo->addItem(i18n("Ignore all whitespaces"),
                               QStringLiteral("-w"));
    m_whitespaceCombo->setCurrentIndex(qMax(0, m_whitespaceCombo->findData(
        optSettings.value(QStringLiteral("mergeWhitespace")).toString())));
    wsRow->addWidget(m_whitespaceCombo);
    m_ignoreEolBox = new QCheckBox(i18n("Ignore line endings"), optGroup);
    m_ignoreEolBox->setChecked(
        optSettings.value(QStringLiteral("mergeIgnoreEol"), false).toBool());
    wsRow->addWidget(m_ignoreEolBox);
    wsRow->addStretch();
    optLayout->addLayout(wsRow, 2, 0, 1, 2);

    layout->addWidget(optGroup);

    // --- Hinweis + Buttons ---
    auto *hint = new QLabel(
        i18n("Conflicts are postponed — resolve them afterwards via "
             "\"Check for Modifications\" → \"Resolve Conflict\"."), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *btnBox = new QDialogButtonBox(this);
    m_dryRunBtn = btnBox->addButton(i18n("Dry Run"),
                                    QDialogButtonBox::ActionRole);
    m_dryRunBtn->setIcon(QIcon::fromTheme(QStringLiteral("document-preview")));
    m_dryRunBtn->setToolTip(
        i18n("Preview the merge result without changing the working copy"));
    m_mergeBtn = btnBox->addButton(i18n("Merge"), QDialogButtonBox::AcceptRole);
    m_mergeBtn->setIcon(QIcon::fromTheme(QStringLiteral("merge")));
    btnBox->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(btnBox);

    // --- History (Modus A) ---
    const QStringList urls = SvnUi::loadHistory(QStringLiteral("MergeHistory"));
    for (const QString &u : urls)
        m_srcCombo->addItem(u, u);
    m_srcCombo->setCurrentIndex(-1);
    m_srcCombo->lineEdit()->clear();

    // --- Verbindungen ---
    connect(m_rangeRadio, &QRadioButton::toggled,
            this, &SvnMergeDialog::updateModeWidgets);
    connect(m_srcCombo, &QComboBox::currentTextChanged,
            this, &SvnMergeDialog::updateButtons);
    connect(m_url1Edit, &QLineEdit::textChanged,
            this, &SvnMergeDialog::updateButtons);
    connect(m_url2Edit, &QLineEdit::textChanged,
            this, &SvnMergeDialog::updateButtons);
    connect(m_logPickBtn, &QPushButton::clicked, this, [this]() {
        const QString url = sourceUrl();
        if (url.isEmpty())
            return;
        SvnLogPickDialog dlg(url, m_mgr,
                             SvnLogPickDialog::SelectionMode::Multiple, this);
        // Merge tracking: grey out everything that is NOT eligible (already
        // merged or natural history). Only on a query error grey out nothing —
        // an empty set is valid (a fully merged branch → everything grey).
        QApplication::setOverrideCursor(Qt::WaitCursor);
        bool eligibleOk = false;
        const QSet<long long> eligible = m_mgr->mergedRevisionsSync(
            url, m_wcPath, QStringLiteral("eligible"), &eligibleOk);
        QApplication::restoreOverrideCursor();
        if (eligibleOk)
            dlg.setEligibleRevisions(eligible);
        if (dlg.exec() != QDialog::Accepted)
            return;
        const QString ranges =
            SvnLogPickDialog::formatRevisionRanges(dlg.selectedRevisions());
        if (!ranges.isEmpty())
            m_rangesEdit->setText(ranges); // ersetzt den Feldinhalt
    });
    // The dry run runs directly from the dialog — the dialog stays open, all
    // input is preserved and "Merge" can follow directly.
    connect(m_dryRunBtn, &QPushButton::clicked, this, [this]() {
        if (m_mgr->isBusy()) {
            QMessageBox::information(this, i18n("SVN"),
                i18n("An SVN operation is already in progress. Please wait."));
            return;
        }
        auto *progress = new SvnProgressDialog(
            i18n("SVN: Merge (dry run)"), m_mgr, this);
        progress->setAttribute(Qt::WA_DeleteOnClose);
        progress->show();
        SvnMergeOptions opts = mergeOptions();
        opts.dryRun = true;
        if (rangeMode())
            m_mgr->mergeRangeAsync(sourceUrl(), revisionRanges(),
                                   m_wcPath, opts);
        else
            m_mgr->mergeTreesAsync(treeUrl1(), treeRev1(),
                                   treeUrl2(), treeRev2(),
                                   m_wcPath, opts);
    });
    connect(btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateModeWidgets();

    // Load the branch suggestions after showing (listSync is synchronous).
    QTimer::singleShot(0, this, &SvnMergeDialog::populateSiblingBranches);
}

void SvnMergeDialog::updateModeWidgets()
{
    const bool range = m_rangeRadio->isChecked();
    m_srcCombo->setEnabled(range);
    m_rangesEdit->setEnabled(range);
    m_srcBrowse->setEnabled(range);
    m_logPickBtn->setEnabled(range && !sourceUrl().isEmpty());
    m_url1Edit->setEnabled(!range);
    m_url1Browse->setEnabled(!range);
    m_rev1Row->setEnabled(!range);
    m_rev1Spin->setEnabled(!range && !m_head1Radio->isChecked());
    m_url2Edit->setEnabled(!range);
    m_url2Browse->setEnabled(!range);
    m_rev2Row->setEnabled(!range);
    m_rev2Spin->setEnabled(!range && !m_head2Radio->isChecked());
    updateButtons();
}

void SvnMergeDialog::updateButtons()
{
    const bool ok = rangeMode()
        ? !sourceUrl().isEmpty()
        : (!treeUrl1().isEmpty() && !treeUrl2().isEmpty());
    m_dryRunBtn->setEnabled(ok);
    m_mergeBtn->setEnabled(ok);
    m_logPickBtn->setEnabled(rangeMode() && !sourceUrl().isEmpty());
}

// Standard-layout heuristic: fill the combo with trunk + branches/* (+ tags/*).
void SvnMergeDialog::populateSiblingBranches()
{
    if (!m_mgr || !m_wcInfo.valid || m_wcInfo.url.isEmpty())
        return;

    // Project root: the prefix before /trunk, /branches/ or /tags/ in the WC
    // URL — covers multi-project repos; otherwise the repository root.
    QString base = m_wcInfo.repositoryRoot;
    const QString url = m_wcInfo.url;
    for (const QString &marker : {QStringLiteral("/trunk"),
                                  QStringLiteral("/branches"),
                                  QStringLiteral("/tags")}) {
        const int idx = url.indexOf(marker);
        if (idx > 0
            && (idx + marker.size() == url.size()
                || url.at(idx + marker.size()) == QLatin1Char('/'))) {
            base = url.left(idx);
            break;
        }
    }
    if (base.isEmpty())
        return;

    QStringList candidates;
    bool ok = false;
    const QList<SvnListEntry> rootEntries = m_mgr->listSync(base,
        QStringLiteral("HEAD"), &ok);
    if (!ok)
        return; // no standard layout / unreachable → silently no suggestions
    for (const SvnListEntry &e : rootEntries) {
        if (!e.isDir)
            continue;
        if (e.name == QLatin1String("trunk")) {
            candidates << base + QStringLiteral("/trunk");
        } else if (e.name == QLatin1String("branches")
                   || e.name == QLatin1String("tags")) {
            const QList<SvnListEntry> subs = m_mgr->listSync(
                base + QLatin1Char('/') + e.name, QStringLiteral("HEAD"), &ok);
            if (!ok)
                continue;
            for (const SvnListEntry &s : subs) {
                if (s.isDir)
                    candidates << base + QLatin1Char('/') + e.name
                                  + QLatin1Char('/') + s.name;
            }
        }
    }

    // Hide the WC's own URL and history duplicates.
    QStringList fresh;
    for (const QString &c : std::as_const(candidates)) {
        if (c != url && m_srcCombo->findText(c) < 0)
            fresh << c;
    }
    if (fresh.isEmpty())
        return;
    const QString current = m_srcCombo->lineEdit()->text();
    if (m_srcCombo->count() > 0)
        m_srcCombo->insertSeparator(m_srcCombo->count());
    for (const QString &c : std::as_const(fresh))
        m_srcCombo->addItem(c, c);
    m_srcCombo->lineEdit()->setText(current); // do not overwrite the input
}

void SvnMergeDialog::accept()
{
    if (rangeMode())
        saveUrlToHistory(sourceUrl());
    // Remember the workflow options; force/record-only are deliberately
    // transient (dangerous as a silent default).
    QSettings optSettings(QStringLiteral("DolphinSvnPlugin"),
                          QStringLiteral("MergeHistory"));
    optSettings.setValue(QStringLiteral("mergeDepth"),
                         m_depthCombo->currentData().toString());
    optSettings.setValue(QStringLiteral("mergeWhitespace"),
                         m_whitespaceCombo->currentData().toString());
    optSettings.setValue(QStringLiteral("mergeIgnoreEol"),
                         m_ignoreEolBox->isChecked());
    QDialog::accept();
}

SvnMergeOptions SvnMergeDialog::mergeOptions() const
{
    SvnMergeOptions opts;
    opts.depth          = m_depthCombo->currentData().toString();
    opts.ignoreAncestry = m_ignoreAncestryBox->isChecked();
    opts.force          = m_forceBox->isChecked();
    opts.recordOnly     = m_recordOnlyBox->isChecked();
    opts.whitespace     = m_whitespaceCombo->currentData().toString();
    opts.ignoreEol      = m_ignoreEolBox->isChecked();
    return opts;
}

bool SvnMergeDialog::rangeMode() const
{
    return m_rangeRadio->isChecked();
}

QString SvnMergeDialog::sourceUrl() const
{
    return m_srcCombo->currentText().trimmed();
}

QString SvnMergeDialog::revisionRanges() const
{
    return m_rangesEdit->text().trimmed();
}

QString SvnMergeDialog::treeUrl1() const
{
    return m_url1Edit->text().trimmed();
}

QString SvnMergeDialog::treeRev1() const
{
    return m_head1Radio->isChecked()
        ? QStringLiteral("HEAD") : QString::number(m_rev1Spin->value());
}

QString SvnMergeDialog::treeUrl2() const
{
    return m_url2Edit->text().trimmed();
}

QString SvnMergeDialog::treeRev2() const
{
    return m_head2Radio->isChecked()
        ? QStringLiteral("HEAD") : QString::number(m_rev2Spin->value());
}

void SvnMergeDialog::saveUrlToHistory(const QString &u)
{
    SvnUi::pushHistory(QStringLiteral("MergeHistory"), u);
}
