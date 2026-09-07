#include "svnplugin.h"
#include "svnadddialog.h"
#include "svnapplypatchdialog.h"
#include "svncheckoutdialog.h"
#include "svncommitdialog.h"
#include "svncreatepatchdialog.h"
#include "svndiffwindow.h"
#include "svnrevisiondiffdialog.h"
#include "svnlogdialog.h"
#include "svnblamedialog.h"
#include "svnexportdialog.h"
#include "svnlockdialog.h"
#include "svnpropertiesdialog.h"
#include "svnrenamedialog.h"
#include "svnrevertdialog.h"
#include "svnsettings.h"
#include "svnsettingsdialog.h"
#include "svnstatusdialog.h"
#include "svnbranchtagdialog.h"
#include "svnmergedialog.h"
#include "svnrepobrowserdialog.h"
#include "svnswitchdialog.h"
#include "svnrelocatedialog.h"
#include "svnimportdialog.h"
#include "svnconflictdialog.h"
#include "svnuihelpers.h"
#include "svnupdatedialog.h"

#include <KFileItem>
#include <KLocalizedString>
#include <KPluginFactory>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QCheckBox>
#include <QMessageBox>
#include <QMenu>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantList>

#include <algorithm>
#include <functional>
#include <memory>

K_PLUGIN_CLASS_WITH_JSON(SvnPlugin, "svnplugin.json")

SvnPlugin::SvnPlugin(QObject *parent, const QVariantList &args)
    : KAbstractFileItemActionPlugin(parent)
{
    Q_UNUSED(args)
}

// Opens the progress dialog and then starts the operation.
void SvnPlugin::runWithProgressDialog(const QString        &title,
                                      QWidget              *parentWidget,
                                      std::function<void()> startOperation)
{
    SvnUi::runWithProgress(&m_svnManager, title, parentWidget, startOperation);
}

void SvnPlugin::deleteWithProgress(const QStringList &paths, bool keepLocal,
                                   bool force, QWidget *parentWidget)
{
    runWithProgressDialog(i18n("SVN: Delete"), parentWidget,
                          [this, paths, keepLocal, force, parentWidget]() {
        // Only a plain delete can hit the local-modifications restriction; watch
        // the result once and offer a --force retry. Connected inside the start
        // lambda (which runs only when not busy) so a busy abort leaves no hook.
        if (!keepLocal && !force) {
            connect(&m_svnManager, &SvnManager::operationFinished, this,
                    [this, paths, parentWidget](const SvnOperationResult &result) {
                        if (result.success)
                            return;
                        const QString detail = result.errorMessage
                            + QLatin1Char('\n') + result.output.join(QLatin1Char('\n'));
                        if (!detail.contains(QStringLiteral("E195006")))
                            return; // a different error — leave it to the dialog
                        const auto ret = QMessageBox::warning(
                            parentWidget, i18n("SVN: Delete"),
                            i18n("Some of the selected items have local "
                                 "modifications.\n\nDelete them anyway and discard "
                                 "those local changes?"),
                            QMessageBox::Yes | QMessageBox::Cancel,
                            QMessageBox::Cancel);
                        if (ret == QMessageBox::Yes)
                            deleteWithProgress(paths, /*keepLocal=*/false,
                                               /*force=*/true, parentWidget);
                    },
                    static_cast<Qt::ConnectionType>(
                        Qt::SingleShotConnection | Qt::QueuedConnection));
        }
        m_svnManager.removeAsync(paths, keepLocal, force);
    });
}

QList<QAction *> SvnPlugin::actions(const KFileItemListProperties &fileItemInfos,
                                     QWidget *parentWidget)
{
    // Apply current settings to the manager on every invocation so that
    // changes made in the Settings dialog take effect immediately.
    m_svnManager.setSvnBinary(SvnSettings::svnBinary());
    m_svnManager.setProcessSyncTimeout(SvnSettings::processSyncTimeout() * 1000);

    const KFileItemList items = fileItemInfos.items();
    if (items.isEmpty()) {
        return {};
    }

    const QString firstPath = items.first().localPath();
    if (firstPath.isEmpty()) {
        return {};
    }

    // Check if path lies within an SVN working copy (.svn directory search).
    QDir dir(QFileInfo(firstPath).isDir() ? firstPath : QFileInfo(firstPath).absolutePath());
    bool isSvnWc = false;
    while (true) {
        if (dir.exists(QStringLiteral(".svn"))) { isSvnWc = true; break; }
        const QString cur = dir.absolutePath();
        if (!dir.cdUp() || dir.absolutePath() == cur) break;
    }

    // Collect the paths of all selected items.
    QStringList paths;
    for (const KFileItem &item : items) {
        const QString p = item.localPath();
        if (!p.isEmpty()) {
            paths << p;
        }
    }

    // Checkout target: use selected directory, or parent if a file was selected.
    const QString checkoutBaseDir =
        QFileInfo(firstPath).isDir() ? firstPath
                                     : QFileInfo(firstPath).absolutePath();

    auto *menu = new QMenu(i18n("SVN"), parentWidget);
    menu->setIcon(QIcon::fromTheme(QStringLiteral("vcs-normal")));

    // -----------------------------------------------------------------------
    // Helpers shared between WC and non-WC menus
    // -----------------------------------------------------------------------
    auto makeCheckoutAction = [&]() {
        auto *a = new QAction(
            QIcon::fromTheme(QStringLiteral("vcs-update-cvs-cervisia")),
            i18n("Checkout..."), menu);
        connect(a, &QAction::triggered, this, [this, checkoutBaseDir, parentWidget]() {
            SvnCheckoutDialog dlg(checkoutBaseDir, parentWidget);
            if (dlg.exec() != QDialog::Accepted) return;
            const QString u   = dlg.url();
            const QString lp  = dlg.localPath();
            const QString rev = dlg.revision();
            const QString dep = dlg.depth();
            runWithProgressDialog(
                i18n("SVN: Checkout"),
                parentWidget,
                [this, u, lp, rev, dep]() {
                    m_svnManager.checkoutAsync(u, lp, rev, dep);
                });
        });
        return a;
    };

    auto makeRepoBrowserAction = [&](const QString &startUrl) {
        auto *a = new QAction(
            QIcon::fromTheme(QStringLiteral("network-server")),
            i18n("Repo-Browser..."), menu);
        connect(a, &QAction::triggered, this, [this, startUrl, parentWidget]() {
            auto *dlg = new SvnRepoBrowserDialog(startUrl, &m_svnManager,
                                                 parentWidget);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
        });
        return a;
    };

    auto makeSettingsAction = [&]() {
        auto *a = new QAction(
            QIcon::fromTheme(QStringLiteral("configure")),
            i18n("Settings..."), menu);
        connect(a, &QAction::triggered, this, [parentWidget]() {
            SvnSettingsDialog dlg(parentWidget);
            dlg.exec();
        });
        return a;
    };

    auto makeExportAction = [&](const QString &srcHint) {
        auto *a = new QAction(
            QIcon::fromTheme(QStringLiteral("document-export")),
            i18n("Export..."), menu);
        connect(a, &QAction::triggered, this, [this, srcHint, parentWidget]() {
            SvnExportDialog dlg(srcHint, parentWidget);
            if (dlg.exec() != QDialog::Accepted) return;
            const QString src = dlg.srcPath();
            const QString dst = dlg.destPath();
            const QString rev = dlg.revision();
            const bool    frc = dlg.force();
            runWithProgressDialog(i18n("SVN: Export"), parentWidget,
                                  [this, src, dst, rev, frc]() {
                                      m_svnManager.exportAsync(src, dst, rev, frc);
                                  });
        });
        return a;
    };

    // -----------------------------------------------------------------------
    // Non-WC context: minimal menu (Checkout + Export + Settings)
    // -----------------------------------------------------------------------
    if (!isSvnWc) {
        // --- Create Repository Here (svnadmin create + optional Layout) ---
        auto *createRepoAction = new QAction(
            QIcon::fromTheme(QStringLiteral("folder-new")),
            i18n("Create Repository Here..."), menu);
        connect(createRepoAction, &QAction::triggered, this,
                [this, checkoutBaseDir, parentWidget]() {
                    if (QMessageBox::question(
                            parentWidget, i18n("Create Repository"),
                            i18n("Create a local SVN repository in this "
                                 "folder?\n%1\n\nThe folder must be empty.",
                                 checkoutBaseDir))
                        != QMessageBox::Yes)
                        return;

                    QString error;
                    if (!m_svnManager.createRepositorySync(checkoutBaseDir,
                                                           &error)) {
                        QMessageBox::warning(
                            parentWidget, i18n("Create Repository"),
                            i18n("Creating the repository failed:\n%1", error));
                        return;
                    }

                    const QString repoUrl =
                        QUrl::fromLocalFile(checkoutBaseDir).toString();

                    const auto layout = QMessageBox::question(
                        parentWidget, i18n("Create Repository"),
                        i18n("Repository created:\n%1\n\n"
                             "Create the standard folders /trunk, /branches "
                             "and /tags?", repoUrl),
                        QMessageBox::Yes | QMessageBox::No);
                    if (layout == QMessageBox::Yes) {
                        runWithProgressDialog(
                            i18n("SVN: Create Standard Folders"), parentWidget,
                            [this, repoUrl]() {
                                m_svnManager.mkdirUrlsAsync(
                                    {repoUrl + QStringLiteral("/trunk"),
                                     repoUrl + QStringLiteral("/branches"),
                                     repoUrl + QStringLiteral("/tags")},
                                    QStringLiteral(
                                        "Create standard repository layout"));
                            });
                    }
                });
        menu->addAction(createRepoAction);

        // --- Import ---
        auto *importAction = new QAction(
            QIcon::fromTheme(QStringLiteral("document-import")),
            i18n("Import..."), menu);
        connect(importAction, &QAction::triggered, this,
                [this, checkoutBaseDir, parentWidget]() {
                    SvnImportDialog dlg(checkoutBaseDir, &m_svnManager, parentWidget);
                    if (dlg.exec() != QDialog::Accepted) return;
                    const QString src = dlg.sourcePath();
                    const QString url = dlg.targetUrl();
                    const QString msg = dlg.message();
                    runWithProgressDialog(
                        i18n("SVN: Import"), parentWidget,
                        [this, src, url, msg]() {
                            m_svnManager.importAsync(src, url, msg);
                        });
                });
        menu->addAction(importAction);

        menu->addAction(makeCheckoutAction());
        menu->addAction(makeExportAction(QString()));
        menu->addAction(makeRepoBrowserAction(QString()));
        menu->addSeparator();
        menu->addAction(makeSettingsAction());
        return {menu->menuAction()};
    }

    // --- Update (HEAD) ---
    auto *updateAction = new QAction(QIcon::fromTheme(QStringLiteral("vcs-update-cvs-cervisia")),
                                     i18n("Update"), menu);
    connect(updateAction, &QAction::triggered, this, [this, firstPath, parentWidget]() {
        runWithProgressDialog(i18n("SVN: Update"), parentWidget, [this, firstPath]() {
            m_svnManager.updateAsync(firstPath);
        });
    });
    menu->addAction(updateAction);

    // --- Update to Revision… ---
    auto *updateRevAction = new QAction(QIcon::fromTheme(QStringLiteral("vcs-update-cvs-cervisia")),
                                        i18n("Update to Revision..."), menu);
    connect(updateRevAction, &QAction::triggered, this, [this, firstPath, parentWidget]() {
        SvnUpdateDialog dlg(firstPath, &m_svnManager, parentWidget);
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        const QString rev   = dlg.revision();
        const QString depth = dlg.depth();
        runWithProgressDialog(
            rev == QStringLiteral("HEAD")
                ? i18n("SVN: Update (HEAD)")
                : i18n("SVN: Update to Revision %1", rev),
            parentWidget,
            [this, firstPath, rev, depth]() {
                m_svnManager.updateAsync(firstPath, rev, depth);
            });
    });
    menu->addAction(updateRevAction);

    // --- Commit ---
    auto *commitAction = new QAction(QIcon::fromTheme(QStringLiteral("vcs-commit")),
                                     i18n("Commit…"), menu);
    connect(commitAction, &QAction::triggered, this,
            [this, paths, firstPath, parentWidget]() {
        // Tag commit warning: TortoiseSVN warns because tags are meant to stay
        // immutable by convention.
        const QString url = m_svnManager.info(firstPath).url;
        if (url.contains(QStringLiteral("/tags/"))) {
            const auto ret = QMessageBox::question(
                parentWidget, i18n("SVN: Commit"),
                i18n("You are committing to a tag. Are you sure?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ret != QMessageBox::Yes)
                return;
        }
        SvnCommitDialog dlg(paths, &m_svnManager, parentWidget);
        // Offer the merge commit template (if stored after a merge): as a
        // dropdown entry, not as a prefill — otherwise it would end up in
        // commits that have nothing to do with the merge.
        QString wcRoot = m_svnManager.info(firstPath).wcRootPath;
        if (wcRoot.isEmpty())
            wcRoot = firstPath;
        const QString pendingMsg = SvnSettings::pendingMergeMessage(wcRoot);
        if (!pendingMsg.isEmpty())
            dlg.setMergeMessage(pendingMsg);
        if (dlg.exec() != QDialog::Accepted) return;
        const QStringList selected    = dlg.selectedPaths();
        const QStringList toAdd       = dlg.unversionedPaths();
        const QString msg             = dlg.message();
        const bool keepLocks          = dlg.keepLocks();
        if (selected.isEmpty()) return;
        // Add unversioned files synchronously before the async commit.
        if (!toAdd.isEmpty())
            m_svnManager.addSync(toAdd, QStringLiteral("empty"));
        runWithProgressDialog(i18n("SVN: Commit"), parentWidget,
                              [this, selected, msg, wcRoot, pendingMsg, keepLocks]() {
            // Discard the template once the merge commit happened — i.e. the
            // commit message starts with the template's header line.
            // Andere Commits in derselben WC lassen sie stehen.
            connect(&m_svnManager, &SvnManager::operationFinished, this,
                    [wcRoot, msg, pendingMsg](const SvnOperationResult &result) {
                        if (!result.success || pendingMsg.isEmpty())
                            return;
                        const QString head = pendingMsg
                            .section(QLatin1Char('\n'), 0, 0).trimmed();
                        if (!head.isEmpty()
                            && msg.trimmed().startsWith(head))
                            SvnSettings::clearPendingMergeMessage(wcRoot);
                    },
                    static_cast<Qt::ConnectionType>(
                        Qt::SingleShotConnection | Qt::QueuedConnection));
            m_svnManager.commitAsync(selected, msg, keepLocks);
        });
    });
    menu->addAction(commitAction);

    menu->addSeparator();

    // --- Show Log ---
    auto *logAction = new QAction(QIcon::fromTheme(QStringLiteral("view-history")),
                                  i18n("Show Log"), menu);
    connect(logAction, &QAction::triggered, this, [this, firstPath, parentWidget]() {
        SvnLogDialog dlg(firstPath, &m_svnManager, parentWidget);
        dlg.exec();
    });
    menu->addAction(logAction);

    // --- Check for Modifications ---
    auto *statusAction = new QAction(QIcon::fromTheme(QStringLiteral("view-list-details")),
                                     i18n("Check for Modifications"), menu);
    connect(statusAction, &QAction::triggered, this, [this, paths, parentWidget]() {
        SvnStatusDialog dlg(paths, &m_svnManager, parentWidget);
        dlg.exec();
    });
    menu->addAction(statusAction);

    menu->addSeparator();

    // --- Add ---
    auto *addAction = new QAction(QIcon::fromTheme(QStringLiteral("list-add")),
                                  i18n("Add..."), menu);
    connect(addAction, &QAction::triggered, this, [this, paths, parentWidget]() {
        SvnAddDialog dlg(paths, &m_svnManager, parentWidget);
        if (dlg.exec() != QDialog::Accepted) return;
        const QStringList selected = dlg.selectedPaths();
        if (selected.isEmpty()) return;
        // Use --depth empty so only the explicitly checked items are added;
        // checked directories are not recursed into (children listed separately).
        runWithProgressDialog(i18n("SVN: Add"), parentWidget, [this, selected]() {
            m_svnManager.addAsync(selected, QStringLiteral("empty"));
        });
    });
    menu->addAction(addAction);

    // --- Add to Ignore List (WC menu, unversioned selection only) ---
    {
        const QString parentDir = QFileInfo(firstPath).absolutePath();
        bool statusOk = false;
        const auto siblingEntries = m_svnManager.status(parentDir, false, &statusOk);
        QSet<QString> unversionedSet;
        for (const SvnStatusEntry &e : siblingEntries) {
            if (e.textStatus == SvnFileStatus::Unversioned)
                unversionedSet.insert(QFileInfo(e.path).absoluteFilePath());
        }
        QStringList ignorePaths;
        for (const QString &p : paths) {
            if (unversionedSet.contains(QFileInfo(p).absoluteFilePath()))
                ignorePaths << p;
        }
        if (!ignorePaths.isEmpty()) {
            auto *ignoreMenu = menu->addMenu(
                QIcon::fromTheme(QStringLiteral("vcs-ignored")),
                i18n("Add to Ignore List"));

            const QString nameLabel = ignorePaths.size() == 1
                ? QFileInfo(ignorePaths.first()).fileName()
                : i18n("%1 selected items (by name)", ignorePaths.size());
            connect(ignoreMenu->addAction(nameLabel), &QAction::triggered, this,
                    [this, ignorePaths]() {
                        for (const QString &p : ignorePaths) {
                            m_svnManager.addToIgnoreSync(
                                QFileInfo(p).absolutePath(), QFileInfo(p).fileName());
                        }
                        SvnSettings::incrementOverlayRefreshToken();
                    });

            QSet<QString> extensions;
            for (const QString &p : ignorePaths) {
                const QString ext = QFileInfo(p).suffix();
                if (!ext.isEmpty())
                    extensions.insert(ext);
            }
            if (extensions.size() == 1) {
                const QString ext = *extensions.cbegin();
                connect(ignoreMenu->addAction(i18n("*.%1", ext)), &QAction::triggered,
                        this, [this, parentDir, ext]() {
                            m_svnManager.addToIgnoreSync(
                                parentDir, QStringLiteral("*.") + ext);
                            SvnSettings::incrementOverlayRefreshToken();
                        });
            }
        }
    }

    // --- Delete ---
    auto *deleteAction = new QAction(QIcon::fromTheme(QStringLiteral("list-remove")),
                                     i18n("Delete"), menu);
    connect(deleteAction, &QAction::triggered, this, [this, paths, parentWidget]() {
        QMessageBox box(parentWidget);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(i18n("SVN: Delete"));
        box.setText(
            i18np("Do you really want to delete this file from version control?",
                  "Do you really want to delete %1 files from version control?",
                  paths.size()));
        // Keeping the local copy also works when the items have local
        // modifications, so it is the safe way to stop tracking, for example an
        // accidentally committed build directory.
        auto *keepLocalCb = new QCheckBox(
            i18n("Keep local copy (only remove from version control)"), &box);
        box.setCheckBox(keepLocalCb);
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        if (box.exec() != QMessageBox::Yes)
            return;
        deleteWithProgress(paths, keepLocalCb->isChecked(), /*force=*/false,
                           parentWidget);
    });
    menu->addAction(deleteAction);

    // --- Rename / Move — single selection only ---
    if (paths.size() == 1) {
        auto *renameAction = new QAction(
            QIcon::fromTheme(QStringLiteral("edit-rename")),
            i18n("Rename..."), menu);
        connect(renameAction, &QAction::triggered, this,
                [this, firstPath, parentWidget]() {
                    SvnRenameDialog dlg(firstPath, parentWidget);
                    if (dlg.exec() != QDialog::Accepted) return;
                    const QString dst = dlg.destPath();
                    runWithProgressDialog(
                        i18n("SVN: Rename / Move"), parentWidget,
                        [this, firstPath, dst]() {
                            m_svnManager.moveAsync(firstPath, dst);
                        });
                });
        menu->addAction(renameAction);
    }

    // --- Revert ---
    auto *revertAction = new QAction(QIcon::fromTheme(QStringLiteral("edit-undo")),
                                     i18n("Revert..."), menu);
    connect(revertAction, &QAction::triggered, this,
            [this, paths, firstPath, parentWidget]() {
        SvnRevertDialog dlg(paths, &m_svnManager, parentWidget);
        if (dlg.exec() != QDialog::Accepted) return;
        const QStringList selected = dlg.selectedPaths();
        if (selected.isEmpty()) return;
        // Revert invalidates any stored merge commit template.
        QString wcRoot = m_svnManager.info(firstPath).wcRootPath;
        if (wcRoot.isEmpty())
            wcRoot = firstPath;
        SvnSettings::clearPendingMergeMessage(wcRoot);
        const QStringList toDelete = dlg.deleteUnversionedRequested()
            ? dlg.unversionedPathsToDelete() : QStringList();
        runWithProgressDialog(i18n("SVN: Revert"), parentWidget,
                              [this, selected, toDelete]() {
            if (!toDelete.isEmpty()) {
                // Delete only AFTER a successful revert.
                connect(&m_svnManager, &SvnManager::operationFinished, this,
                        [toDelete](const SvnOperationResult &result) {
                            if (!result.success) return;
                            for (const QString &p : toDelete) {
                                const QFileInfo fi(p);
                                if (fi.isDir())
                                    QDir(p).removeRecursively();
                                else
                                    QFile::remove(p);
                            }
                        },
                        static_cast<Qt::ConnectionType>(
                            Qt::SingleShotConnection | Qt::QueuedConnection));
            }
            m_svnManager.revertAsync(selected, QStringLiteral("infinity"));
        });
    });
    menu->addAction(revertAction);

    // --- Cleanup ---
    auto *cleanupAction = new QAction(QIcon::fromTheme(QStringLiteral("edit-clear")),
                                      i18n("Cleanup"), menu);
    connect(cleanupAction, &QAction::triggered, this, [this, firstPath, parentWidget]() {
        runWithProgressDialog(i18n("SVN: Cleanup"), parentWidget, [this, firstPath]() {
            m_svnManager.cleanupAsync(firstPath);
        });
    });
    menu->addAction(cleanupAction);

    // --- Get Lock ---
    auto *lockAction = new QAction(QIcon::fromTheme(QStringLiteral("object-locked")),
                                   i18n("Get Lock..."), menu);
    connect(lockAction, &QAction::triggered, this, [this, paths, parentWidget]() {
        SvnLockDialog dlg(paths, parentWidget);
        if (dlg.exec() != QDialog::Accepted) return;
        const QString msg   = dlg.message();
        const bool    steal = dlg.stealLock();
        runWithProgressDialog(i18n("SVN: Lock"), parentWidget, [this, paths, msg, steal]() {
            m_svnManager.lockAsync(paths, msg, steal);
        });
    });
    menu->addAction(lockAction);

    // --- Release Lock ---
    auto *unlockAction = new QAction(QIcon::fromTheme(QStringLiteral("object-unlocked")),
                                     i18n("Release Lock"), menu);
    connect(unlockAction, &QAction::triggered, this, [this, paths, parentWidget]() {
        runWithProgressDialog(i18n("SVN: Unlock"), parentWidget, [this, paths]() {
            m_svnManager.unlockAsync(paths);
        });
    });
    menu->addAction(unlockAction);

    // --- Properties ---
    auto *propsAction = new QAction(
        QIcon::fromTheme(QStringLiteral("document-properties")),
        i18n("Properties..."), menu);
    connect(propsAction, &QAction::triggered, this, [this, firstPath, parentWidget]() {
        SvnPropertiesDialog dlg(firstPath, &m_svnManager, parentWidget);
        dlg.exec();
    });
    menu->addAction(propsAction);

    // --- Copy Repository URL ---
    auto *copyUrlAction = new QAction(
        QIcon::fromTheme(QStringLiteral("edit-copy")),
        i18n("Copy Repository URL"), menu);
    connect(copyUrlAction, &QAction::triggered, this, [this, firstPath]() {
        const SvnInfo info = m_svnManager.info(firstPath);
        if (!info.valid || info.url.isEmpty()) return;
        QGuiApplication::clipboard()->setText(info.url);
    });
    menu->addAction(copyUrlAction);

    menu->addSeparator();

    // --- Diff with BASE ---
    auto *diffAction = new QAction(QIcon::fromTheme(QStringLiteral("vcs-diff")),
                                   i18n("Diff with BASE"), menu);
    connect(diffAction, &QAction::triggered, this, [this, paths, firstPath, parentWidget]() {
        const QString tool = SvnDiffWindow::findDiffTool();
        if (tool.isEmpty()) {
            QMessageBox::warning(
                parentWidget, i18n("SVN: Diff"),
                i18n("No supported diff tool found.\n\n"
                     "Please install one of the following tools:\n"
                     "  • Meld  (sudo apt install meld)\n"
                     "  • KDiff3  (sudo apt install kdiff3)\n"
                     "  • Kompare  (sudo apt install kompare)"));
            return;
        }
        // Single file that exists locally → diff without dialog.
        if (paths.size() == 1) {
            const QFileInfo fi(firstPath);
            if (fi.isFile() && fi.exists()) {
                SvnDiffWindow::launchFileDiff(tool, fi.absoluteFilePath(),
                                              &m_svnManager, this);
                return;
            }
        }
        // Directory or multiple files → show selection dialog.
        SvnDiffWindow dlg(paths, &m_svnManager, tool, this, parentWidget);
        dlg.exec();
    });
    menu->addAction(diffAction);

    // --- Diff with Revision… (single files only) ---
    auto *diffRevAction = new QAction(QIcon::fromTheme(QStringLiteral("vcs-diff")),
                                      i18n("Diff with Revision..."), menu);
    const bool isSingleFile = (paths.size() == 1 && QFileInfo(firstPath).isFile());
    diffRevAction->setEnabled(isSingleFile);
    connect(diffRevAction, &QAction::triggered, this, [this, firstPath, parentWidget]() {
        const QString tool = SvnDiffWindow::findDiffTool();
        if (tool.isEmpty()) {
            QMessageBox::warning(
                parentWidget, i18n("SVN: Diff"),
                i18n("No supported diff tool found.\n\n"
                     "Please install one of the following tools:\n"
                     "  • Meld  (sudo apt install meld)\n"
                     "  • KDiff3  (sudo apt install kdiff3)\n"
                     "  • Kompare  (sudo apt install kompare)"));
            return;
        }
        SvnRevisionDiffDialog dlg(firstPath, &m_svnManager, tool, this, parentWidget);
        dlg.exec();
    });
    menu->addAction(diffRevAction);

    // --- Resolve conflict with three-way merge — single file only ---
    auto *resolveAction = new QAction(
        QIcon::fromTheme(QStringLiteral("vcs-merge")),
        i18n("Resolve Conflict (3-Way Merge)..."), menu);
    resolveAction->setEnabled(isSingleFile);
    connect(resolveAction, &QAction::triggered, this,
            [this, firstPath, parentWidget]() {
        SvnUi::resolveWithThreeWayMerge(firstPath, &m_svnManager,
                                        parentWidget, this);
    });
    menu->addAction(resolveAction);

    // --- Resolve all conflicts in the WC ---
    auto *conflictsAction = new QAction(
        QIcon::fromTheme(QStringLiteral("vcs-conflicting")),
        i18n("Resolve Conflicts..."), menu);
    connect(conflictsAction, &QAction::triggered, this,
            [this, firstPath, parentWidget]() {
        auto *cdlg = new SvnConflictDialog(firstPath, &m_svnManager,
                                           parentWidget);
        cdlg->setAttribute(Qt::WA_DeleteOnClose);
        cdlg->show();
    });
    menu->addAction(conflictsAction);

    // --- Blame — single file only ---
    auto *blameAction = new QAction(
        QIcon::fromTheme(QStringLiteral("view-list-text")),
        i18n("Blame..."), menu);
    blameAction->setEnabled(isSingleFile);
    connect(blameAction, &QAction::triggered, this, [this, firstPath, parentWidget]() {
        SvnBlameDialog dlg(firstPath, &m_svnManager, parentWidget);
        dlg.exec();
    });
    menu->addAction(blameAction);

    menu->addSeparator();

    // --- Checkout ---
    menu->addAction(makeCheckoutAction());

    // --- Export ---
    menu->addAction(makeExportAction(firstPath));

    // --- Create Patch ---
    auto *createPatchAction = new QAction(
        QIcon::fromTheme(QStringLiteral("text-x-patch"),
                         QIcon::fromTheme(QStringLiteral("vcs-diff"))),
        i18n("Create Patch..."), menu);
    connect(createPatchAction, &QAction::triggered, this,
            [this, paths, parentWidget]() {
                SvnCreatePatchDialog dlg(paths, &m_svnManager, parentWidget);
                if (dlg.exec() != QDialog::Accepted) return;
                const QStringList selected = dlg.selectedPaths();
                const QString outFile      = dlg.outputFile();

                // 'svn diff' is purely local and fast — no progress dialog
                // needed, but the cursor signals the brief block.
                QGuiApplication::setOverrideCursor(Qt::WaitCursor);
                QString error;
                const bool ok =
                    m_svnManager.createPatchSync(selected, outFile, &error);
                QGuiApplication::restoreOverrideCursor();

                if (!ok) {
                    QMessageBox::warning(
                        parentWidget, i18n("SVN: Create Patch"),
                        i18n("Creating the patch failed:\n%1", error));
                    return;
                }
                QMessageBox::information(
                    parentWidget, i18n("SVN: Create Patch"),
                    i18np("Patch with %1 file written to:\n%2",
                          "Patch with %1 files written to:\n%2",
                          selected.size(), outFile));
            });
    menu->addAction(createPatchAction);

    // --- Apply Patch ---
    auto *applyPatchAction = new QAction(
        QIcon::fromTheme(QStringLiteral("text-x-patch"),
                         QIcon::fromTheme(QStringLiteral("document-import"))),
        i18n("Apply Patch..."), menu);
    connect(applyPatchAction, &QAction::triggered, this,
            [this, firstPath, parentWidget]() {
                // Patch paths from 'svn diff --git' are relative to the WC root
                // — only there do they land in the right place when applied.
                QString wcRoot = m_svnManager.info(firstPath).wcRootPath;
                if (wcRoot.isEmpty()) {
                    wcRoot = QFileInfo(firstPath).isDir()
                        ? firstPath : QFileInfo(firstPath).absolutePath();
                }

                const QString chosen = QFileDialog::getOpenFileName(
                    parentWidget, i18n("Select Patch File"), wcRoot,
                    i18n("Patch files (*.patch *.diff);;All files (*)"));
                if (chosen.isEmpty()) return;

                SvnApplyPatchDialog dlg(wcRoot, chosen, &m_svnManager,
                                        parentWidget);
                if (dlg.exec() != QDialog::Accepted) return;
                const QString file  = dlg.patchFile();
                const bool    rev   = dlg.reverse();
                const int     strip = dlg.strip();

                // 'svn patch' also exits with code 0 on rejected hunks and
                // leaves no conflict status — success must therefore be read
                // from the output.
                const auto hook = [this, wcRoot, parentWidget](
                                      const SvnOperationResult &result) {
                    if (!result.success) return;   // the progress dialog shows the error
                    if (!SvnManager::patchOutputHasRejects(result.output))
                        return;

                    QStringList rejected;
                    for (const QString &line : result.output) {
                        // "C         path/to/file.txt" marks the file
                        // deren Hunks in .svnpatch.rej gelandet sind.
                        if (!line.startsWith(QLatin1Char('C'))) continue;
                        const QString rest = line.mid(1).trimmed();
                        if (!rest.isEmpty())
                            rejected << rest;
                    }
                    const QString list = rejected.isEmpty()
                        ? QString()
                        : QStringLiteral("\n\n") + rejected.join(QLatin1Char('\n'));
                    QMessageBox::warning(
                        parentWidget, i18n("SVN: Apply Patch"),
                        i18n("The patch was applied, but some hunks did not "
                             "fit and were rejected. The rejected hunks were "
                             "written next to the affected files as "
                             "'*.svnpatch.rej' and must be merged by hand.%1",
                             list));
                };

                runWithProgressDialog(
                    i18n("SVN: Apply Patch"), parentWidget,
                    [this, wcRoot, file, rev, strip, hook]() {
                        connect(&m_svnManager, &SvnManager::operationFinished,
                                this, hook,
                                static_cast<Qt::ConnectionType>(
                                    Qt::SingleShotConnection
                                    | Qt::QueuedConnection));
                        m_svnManager.applyPatchAsync(wcRoot, file,
                                                     /*dryRun=*/false, rev,
                                                     strip);
                    });
            });
    menu->addAction(applyPatchAction);

    // --- Branch / Tag ---
    auto *branchTagAction = new QAction(
        QIcon::fromTheme(QStringLiteral("vcs-branch")),
        i18n("Branch / Tag..."), menu);
    connect(branchTagAction, &QAction::triggered, this,
            [this, firstPath, parentWidget]() {
                SvnBranchTagDialog dlg(firstPath, &m_svnManager, parentWidget);
                if (dlg.exec() != QDialog::Accepted) return;
                const QString src = dlg.sourceUrl();
                const QString dst = dlg.targetUrl();
                const QString rev = dlg.revision();
                const QString msg = dlg.message();
                runWithProgressDialog(
                    i18n("SVN: Branch / Tag"), parentWidget,
                    [this, src, dst, msg, rev]() {
                        m_svnManager.copyAsync(src, dst, msg, rev);
                    });
            });
    menu->addAction(branchTagAction);

    // --- Switch ---
    auto *switchAction = new QAction(
        QIcon::fromTheme(QStringLiteral("vcs-branch")),
        i18n("Switch..."), menu);
    connect(switchAction, &QAction::triggered, this,
            [this, firstPath, parentWidget]() {
                SvnSwitchDialog dlg(firstPath, &m_svnManager, parentWidget);
                if (dlg.exec() != QDialog::Accepted) return;
                const QString url   = dlg.targetUrl();
                const QString rev   = dlg.revision();
                const QString depth = dlg.depth();
                runWithProgressDialog(
                    i18n("SVN: Switch"), parentWidget,
                    [this, firstPath, url, rev, depth]() {
                        m_svnManager.switchAsync(firstPath, url, rev, depth);
                    });
            });
    menu->addAction(switchAction);

    // --- Relocate ---
    auto *relocateAction = new QAction(
        QIcon::fromTheme(QStringLiteral("network-server")),
        i18n("Relocate..."), menu);
    connect(relocateAction, &QAction::triggered, this,
            [this, firstPath, parentWidget]() {
                SvnRelocateDialog dlg(firstPath, &m_svnManager, parentWidget);
                if (dlg.exec() != QDialog::Accepted) return;
                const QString from = dlg.fromUrl();
                const QString to   = dlg.toUrl();
                runWithProgressDialog(
                    i18n("SVN: Relocate"), parentWidget,
                    [this, firstPath, from, to]() {
                        m_svnManager.relocateAsync(firstPath, from, to);
                    });
            });
    menu->addAction(relocateAction);

    // --- Merge ---
    auto *mergeAction = new QAction(
        QIcon::fromTheme(QStringLiteral("merge")),
        i18n("Merge..."), menu);
    connect(mergeAction, &QAction::triggered, this,
            [this, firstPath, parentWidget]() {
                SvnMergeDialog dlg(firstPath, &m_svnManager, parentWidget);
                // The dry run runs in the dialog itself; Accepted = real merge.
                if (dlg.exec() != QDialog::Accepted) return;
                const QString title = i18n("SVN: Merge");
                // After the merge, automatically check for conflicts and open
                // the conflict dialog if needed. The connect happens only INSIDE
                // the startOperation lambda — if runWithProgressDialog aborts
                // earlier due to isBusy(), no single-shot hook must remain.
                // Commit template (inspired by tsvn:mergelogtemplate):
                // a header line + the log comments of all merged revisions,
                // separated by a blank line and "---".
                QString mergeMsg;
                if (dlg.rangeMode()) {
                    const QString url    = dlg.sourceUrl();
                    const QString ranges = dlg.revisionRanges();
                    mergeMsg = ranges.isEmpty()
                        ? i18n("Merged all eligible revisions from %1.", url)
                        : i18n("Merged revision(s) %1 from %2.", ranges, url);

                    // Logik (Ranges parsen, Log-Kommentare filtern/formatieren,
                    // cap 50) lives backend-testable in
                    // SvnManager::buildMergeMessage; only the WaitCursor and the
                    // i18n lines remain here (F-ARCH1).
                    QApplication::setOverrideCursor(Qt::WaitCursor);
                    const SvnMergeMessageParts parts =
                        m_svnManager.buildMergeMessage(url, ranges, firstPath);
                    QApplication::restoreOverrideCursor();

                    mergeMsg += parts.commentsAppendix;
                    if (parts.overflowCount > 0) {
                        mergeMsg += QStringLiteral("\n\n---\n");
                        mergeMsg += i18n("(and %1 more revisions)",
                                         parts.overflowCount);
                    }
                } else {
                    mergeMsg = i18n("Merged %1@%2 → %3@%4.",
                                    dlg.treeUrl1(), dlg.treeRev1(),
                                    dlg.treeUrl2(), dlg.treeRev2());
                }

                // Encapsulate the merge call (range or tree variant), so it can
                // be repeated identically for the update-retry and continue-merge
                // cases.
                const SvnMergeOptions mergeOpts = dlg.mergeOptions();
                std::function<void()> doMerge;
                if (dlg.rangeMode()) {
                    const QString url    = dlg.sourceUrl();
                    const QString ranges = dlg.revisionRanges();
                    doMerge = [this, url, ranges, firstPath, mergeOpts]() {
                        m_svnManager.mergeRangeAsync(url, ranges, firstPath,
                                                     mergeOpts);
                    };
                } else {
                    const QString u1 = dlg.treeUrl1(), r1 = dlg.treeRev1();
                    const QString u2 = dlg.treeUrl2(), r2 = dlg.treeRev2();
                    doMerge = [this, u1, r1, u2, r2, firstPath, mergeOpts]() {
                        m_svnManager.mergeTreesAsync(u1, r1, u2, r2, firstPath,
                                                     mergeOpts);
                    };
                }

                // Self-referential retry closure: on every (re)start it
                // reinstalls the post-merge hook (SingleShot — no double
                // connect) and then starts the merge. The update-retry and
                // continue-merge cases re-attach it once the obstacle is cleared.
                // mergeMsg stays constant across repetitions.
                auto startMerge = std::make_shared<std::function<void()>>();
                *startMerge = [this, firstPath, parentWidget, title, mergeMsg,
                               doMerge, startMerge]() {
                    const auto hook = [this, firstPath, parentWidget, mergeMsg,
                                       startMerge](
                                          const SvnOperationResult &result) {
                        // svn merge exits with an error code on postponed
                        // conflicts (E155015) — the WC status decides whether the
                        // merge actually happened: if there are conflicts, the
                        // merge did happen, just with conflicts.
                        const auto entries =
                            m_svnManager.status(firstPath, true);
                        const bool hasConflicts = std::any_of(
                            entries.cbegin(), entries.cend(),
                            [](const SvnStatusEntry &e) {
                                return e.treeConflicted
                                    || e.textStatus
                                           == SvnFileStatus::Conflicted
                                    || e.propStatus
                                           == SvnFileStatus::Conflicted;
                            });
                        if (!result.success && !hasConflicts) {
                            // Merge into a mixed-revision WC (E195020,
                            // typical after a previous merge commit) → offer an
                            // update and repeat the merge automatically.
                            if (result.errorMessage.contains(
                                    QStringLiteral("E195020"))) {
                                QString wcRoot =
                                    m_svnManager.info(firstPath).wcRootPath;
                                if (wcRoot.isEmpty())
                                    wcRoot = firstPath;
                                const auto answer = QMessageBox::question(
                                    parentWidget, i18n("SVN: Merge"),
                                    i18n("The working copy has mixed revisions "
                                         "(from a previous commit). Update the "
                                         "working copy and retry the merge?"),
                                    QMessageBox::Yes | QMessageBox::No,
                                    QMessageBox::Yes);
                                if (answer == QMessageBox::Yes) {
                                    runWithProgressDialog(i18n("SVN: Update"),
                                        parentWidget,
                                        [this, wcRoot, startMerge]() {
                                            connect(&m_svnManager,
                                                &SvnManager::operationFinished,
                                                this,
                                                [startMerge](
                                                    const SvnOperationResult
                                                        &upd) {
                                                    if (upd.success)
                                                        (*startMerge)();
                                                },
                                                static_cast<Qt::ConnectionType>(
                                                    Qt::SingleShotConnection
                                                    | Qt::QueuedConnection));
                                            m_svnManager.updateAsync(wcRoot);
                                        });
                                }
                            }
                            return; // a real error, or the mixed-revision case handled above
                        }
                        // A no-op merge (e.g. all revisions already merged)
                        // produces no commit template — otherwise "(Merge)"
                        // would land in the recent messages without a real change.
                        const auto isRealChange = [](SvnFileStatus s) {
                            switch (s) {
                            case SvnFileStatus::Normal:
                            case SvnFileStatus::Unversioned:
                            case SvnFileStatus::Ignored:
                            case SvnFileStatus::External:
                                return false;
                            default:
                                return true;
                            }
                        };
                        const bool hasRealChanges = std::any_of(
                            entries.cbegin(), entries.cend(),
                            [&isRealChange](const SvnStatusEntry &e) {
                                return isRealChange(e.textStatus)
                                    || isRealChange(e.propStatus);
                            });
                        if (!hasRealChanges && !hasConflicts)
                            return;
                        // Store the commit template per WC root.
                        QString wcRoot =
                            m_svnManager.info(firstPath).wcRootPath;
                        if (wcRoot.isEmpty())
                            wcRoot = firstPath;
                        SvnSettings::setPendingMergeMessage(wcRoot, mergeMsg);
                        if (!hasConflicts)
                            return;
                        auto *cdlg = new SvnConflictDialog(
                            firstPath, &m_svnManager, parentWidget);
                        cdlg->setAttribute(Qt::WA_DeleteOnClose);
                        // "Continue Merge" restarts the merge; merge
                        // tracking skips already-merged revisions and applies
                        // the rest.
                        cdlg->setContinueMerge([startMerge]() {
                            (*startMerge)();
                        });
                        cdlg->show();
                    };

                    runWithProgressDialog(title, parentWidget,
                        [this, doMerge, hook]() {
                            connect(&m_svnManager,
                                    &SvnManager::operationFinished, this, hook,
                                    static_cast<Qt::ConnectionType>(
                                        Qt::SingleShotConnection
                                        | Qt::QueuedConnection));
                            doMerge();
                        });
                };
                (*startMerge)();
            });
    menu->addAction(mergeAction);

    // --- Repo browser — determine the repo root only on click ---
    auto *repoBrowserAction = new QAction(
        QIcon::fromTheme(QStringLiteral("network-server")),
        i18n("Repo-Browser..."), menu);
    connect(repoBrowserAction, &QAction::triggered, this,
            [this, firstPath, parentWidget]() {
                const SvnInfo wcInfo = m_svnManager.info(firstPath);
                auto *dlg = new SvnRepoBrowserDialog(
                    wcInfo.valid ? wcInfo.repositoryRoot : QString(),
                    &m_svnManager, parentWidget);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                dlg->show();
            });
    menu->addAction(repoBrowserAction);

    menu->addSeparator();

    // --- Refresh Overlays ---
    auto *refreshOverlaysAction = new QAction(
        QIcon::fromTheme(QStringLiteral("view-refresh")),
        i18n("Refresh Status Overlays"), menu);
    connect(refreshOverlaysAction, &QAction::triggered, this, []() {
        SvnSettings::incrementOverlayRefreshToken();
    });
    menu->addAction(refreshOverlaysAction);

    // --- Settings ---
    menu->addAction(makeSettingsAction());

    return {menu->menuAction()};
}

#include "svnplugin.moc"
