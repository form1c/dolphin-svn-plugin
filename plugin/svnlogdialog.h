#pragma once

#include "svntypes.h"

#include <QDialog>
#include <functional>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPoint;
class QTreeWidget;
class QTreeWidgetItem;
class QTextEdit;
class QPushButton;
class QWidget;
class SvnManager;

class SvnLogDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SvnLogDialog(const QString &path, SvnManager *mgr, QWidget *parent = nullptr);

    // Selects and scrolls to the given revision in the log tree.
    void scrollToRevision(long long rev);

private:
    void loadMore();
    void reloadLog();
    void showSelection();
    void applyFilter();
    void updateAuthorCombo();
    void showContextMenu(const QPoint &pos);
    void showRevisionDiff(long long rev);
    void diffChangedPath(QTreeWidgetItem *item);
    void compareSelectedRevisions();
    void updateCompareButton();
    void runOperation(const QString &title, std::function<void()> startOp);
    static QString formatDate(const QString &isoDate);
    static QString actionLabel(const QString &action);
    static QString actionIconName(const QString &action);

    QLineEdit   *m_searchEdit  = nullptr;
    QComboBox   *m_authorCombo = nullptr;
    QTreeWidget *m_logTree     = nullptr;
    QTextEdit   *m_msgView     = nullptr;
    QTreeWidget *m_pathsTree   = nullptr;
    QPushButton *m_moreBtn     = nullptr;
    QPushButton *m_compareBtn  = nullptr;

    QString     m_path;
    SvnManager *m_mgr = nullptr;
    QList<SvnLogEntry> m_entries;
    long long   m_oldestRevision = -1;

    // Repo-relative path of the log target (for "Show only affected paths"),
    // resolved lazily.
    QString m_repoRelPath;
    bool m_repoRelResolved = false;
    QCheckBox *m_onlyAffectedBox = nullptr;
    QCheckBox *m_includeMergedBox = nullptr;

    static constexpr int kPageSize = 100;
};
