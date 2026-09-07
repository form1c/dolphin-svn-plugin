#pragma once

#include <QDialog>
#include <QPushButton>
#include <QStringList>

class QTreeWidget;
class QTreeWidgetItem;
class QCheckBox;
class SvnManager;

class SvnRevertDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SvnRevertDialog(const QStringList &paths, SvnManager *svnManager,
                             QWidget *parent = nullptr);

    QStringList selectedPaths() const;

    // "Also delete unversioned items" — checked state and the (precomputed)
    // list of unversioned items found under the paths passed to the
    // constructor. The caller deletes them only AFTER a successful revert.
    bool        deleteUnversionedRequested() const;
    QStringList unversionedPathsToDelete()   const;

protected:
    void accept() override;

private:
    void populateTable(const QStringList &paths, SvnManager *mgr);
    void onItemChanged(QTreeWidgetItem *item, int col);
    void updateRevertButton();
    void refresh();

    QTreeWidget *m_tree              = nullptr;
    QCheckBox   *m_revertAdded       = nullptr;
    QCheckBox   *m_deleteUnversioned = nullptr;
    QPushButton *m_revertBtn         = nullptr;
    bool         m_block             = false;

    QStringList  m_paths;
    QStringList  m_unversionedPaths;
    SvnManager  *m_mgr = nullptr;
};
