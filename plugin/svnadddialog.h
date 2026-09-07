#pragma once

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

class QTreeWidget;
class QTreeWidgetItem;
class SvnManager;

class SvnAddDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SvnAddDialog(const QStringList &paths, SvnManager *svnManager,
                          QWidget *parent = nullptr);

    // Returns all checked paths, with parent directories automatically
    // included so that svn add --depth empty can succeed.
    QStringList selectedPaths() const;

private:
    struct Item {
        QString absPath;
        bool isDir = false;
    };

    void collectItems(const QStringList &paths, SvnManager *mgr,
                      QList<Item> &out, bool *anyStatusFailed = nullptr) const;
    static void collectDirContents(const QString &dirPath, QList<Item> &out);
    void buildTable(const QList<Item> &items);
    void onItemChanged(QTreeWidgetItem *item, int col);
    void refresh();

    QTreeWidget *m_tree  = nullptr;
    bool         m_block = false;

    QStringList  m_addPaths;
    SvnManager  *m_mgr = nullptr;
};
