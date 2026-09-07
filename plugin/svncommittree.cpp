#include "svncommittree.h"
#include "svntreecheck.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <algorithm>
#include <functional>

namespace {

// Recursively lists the contents of an unversioned directory (excluding .svn).
void collectDirContents(const QString &dirPath, QStringList &out)
{
    const QDir dir(dirPath);
    const QStringList names =
        dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QString &name : names) {
        if (name == QLatin1String(".svn")) continue;
        const QString abs = dir.absoluteFilePath(name);
        out << abs;
        if (QFileInfo(abs).isDir())
            collectDirContents(abs, out);
    }
}

} // namespace

QString SvnUi::commonAncestorDir(const QStringList &paths)
{
    if (paths.isEmpty())
        return QString();
    // Root at the PARENT of each selected item, files and directories alike. A
    // selected directory must be the parent's child so it appears as its own
    // folder node with its contents nested underneath. Rooting at the directory
    // itself would make the directory and its own contents siblings on the top
    // level (the "flat single-folder commit" bug).
    QStringList dirs;
    for (const QString &p : paths)
        dirs << QFileInfo(p).absolutePath();
    QStringList common = dirs.first().split(QLatin1Char('/'));
    for (const QString &d : std::as_const(dirs)) {
        const QStringList parts = d.split(QLatin1Char('/'));
        int i = 0;
        while (i < common.size() && i < parts.size() && common[i] == parts[i])
            ++i;
        common = common.mid(0, i);
    }
    return common.join(QLatin1Char('/'));
}

bool SvnUi::collectUnversionedDirCapped(const QString &dir, QStringList &out,
                                        int cap)
{
    QList<QString> stack{dir};
    int count = 0;
    while (!stack.isEmpty()) {
        const QDir d(stack.takeLast());
        const QStringList names =
            d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const QString &name : names) {
            if (name == QLatin1String(".svn")) continue;
            const QString abs = d.absoluteFilePath(name);
            if (++count > cap)
                return false;
            out << abs;
            if (QFileInfo(abs).isDir())
                stack << abs;
        }
    }
    return true;
}

QStringList SvnUi::descendantPathsSorted(const QString &dir)
{
    QStringList paths;
    collectDirContents(dir, paths);
    std::sort(paths.begin(), paths.end());
    return paths;
}

void SvnUi::buildCommitTree(QTreeWidget *tree, const QList<CommitRow> &rowsIn,
                            const QString &displayRoot)
{
    QList<CommitRow> rows = rowsIn;
    // A parent always precedes its children.
    std::sort(rows.begin(), rows.end(),
              [](const CommitRow &a, const CommitRow &b) { return a.abs < b.abs; });

    // Any path that is the parent of another row is a directory. This catches a
    // missing directory, which is gone from disk so its isDir hint is false.
    QSet<QString> parentDirs;
    for (const CommitRow &r : std::as_const(rows))
        parentDirs.insert(QFileInfo(r.abs).absolutePath());

    const QDir rootDir(displayRoot);
    QHash<QString, QTreeWidgetItem *> dirNodes;

    std::function<QTreeWidgetItem *(const QString &)> ensureDir =
        [&](const QString &absDir) -> QTreeWidgetItem * {
            // At (or above) the display root the children become top-level items.
            if (absDir == displayRoot || absDir.isEmpty()
                || !(absDir.startsWith(displayRoot + QLatin1Char('/'))))
                return nullptr;
            if (QTreeWidgetItem *n = dirNodes.value(absDir, nullptr))
                return n;
            const QFileInfo fi(absDir);
            QTreeWidgetItem *parent = ensureDir(fi.absolutePath());
            auto *node = parent ? new QTreeWidgetItem(parent)
                                : new QTreeWidgetItem(tree);
            node->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled
                           | Qt::ItemIsSelectable);
            node->setCheckState(0, Qt::Unchecked); // recomputed from children
            node->setText(0, fi.fileName());
            node->setToolTip(0, rootDir.relativeFilePath(absDir));
            node->setData(0, Qt::UserRole,     absDir);
            node->setData(0, Qt::UserRole + 1, false); // not unversioned
            node->setData(0, Qt::UserRole + 2, true);  // is a directory
            node->setData(0, Qt::UserRole + 3, 0);
            node->setData(0, Qt::UserRole + 4, false); // grouping node
            node->setData(0, Qt::UserRole + 5, false);
            dirNodes.insert(absDir, node);
            return node;
        };

    for (const CommitRow &r : std::as_const(rows)) {
        const QFileInfo fi(r.abs);
        const bool isDir = r.isDir || parentDirs.contains(r.abs);

        QTreeWidgetItem *item = isDir ? dirNodes.value(r.abs, nullptr) : nullptr;
        if (!item) {
            QTreeWidgetItem *parentNode = ensureDir(fi.absolutePath());
            item = parentNode ? new QTreeWidgetItem(parentNode)
                              : new QTreeWidgetItem(tree);
            if (isDir)
                dirNodes.insert(r.abs, item);
        }

        item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled
                       | Qt::ItemIsSelectable);
        item->setCheckState(0, r.isUnversioned ? Qt::Unchecked : Qt::Checked);
        item->setText(0, fi.fileName());
        item->setToolTip(0, rootDir.relativeFilePath(r.abs));
        item->setData(0, Qt::UserRole,     r.abs);
        item->setData(0, Qt::UserRole + 1, r.isUnversioned);
        item->setData(0, Qt::UserRole + 2, isDir);
        item->setData(0, Qt::UserRole + 3, r.status);
        item->setData(0, Qt::UserRole + 4, true);       // a real, committable entry
        item->setData(0, Qt::UserRole + 5, r.collapsed);
    }

    // Give every folder the tri-state of its children.
    SvnUi::initTreeCheckStates(tree);
}

QHash<QString, Qt::CheckState> SvnUi::snapshotCheckStates(QTreeWidget *tree)
{
    QHash<QString, Qt::CheckState> saved;
    std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *it) {
        if (it->childCount() == 0 && it->data(0, Qt::UserRole + 4).toBool()) {
            const QString p = it->data(0, Qt::UserRole).toString();
            if (!p.isEmpty())
                saved.insert(p, it->checkState(0));
        }
        for (int i = 0; i < it->childCount(); ++i)
            walk(it->child(i));
    };
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        walk(tree->topLevelItem(i));
    return saved;
}

void SvnUi::restoreCheckStates(QTreeWidget *tree,
                               const QHash<QString, Qt::CheckState> &saved)
{
    if (saved.isEmpty())
        return;
    std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *it) {
        if (it->childCount() == 0 && it->data(0, Qt::UserRole + 4).toBool()) {
            const auto found =
                saved.constFind(it->data(0, Qt::UserRole).toString());
            if (found != saved.constEnd())
                it->setCheckState(0, found.value());
        }
        for (int i = 0; i < it->childCount(); ++i)
            walk(it->child(i));
    };
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        walk(tree->topLevelItem(i));
    SvnUi::initTreeCheckStates(tree);
}
