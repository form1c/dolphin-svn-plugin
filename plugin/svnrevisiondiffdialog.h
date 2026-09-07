#pragma once

#include "svntypes.h"

#include <QDialog>

class QTreeWidget;
class QTreeWidgetItem;
class QTextEdit;
class QPushButton;
class SvnManager;

class SvnRevisionDiffDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SvnRevisionDiffDialog(const QString &path, SvnManager *mgr,
                                   const QString &tool, QObject *processParent,
                                   QWidget *parent = nullptr);

private:
    void loadMore();
    void showEntry(QTreeWidgetItem *item);
    void diffSelected();
    static QString formatDate(const QString &isoDate);
    static QString actionLabel(const QString &action);
    static QString actionIconName(const QString &action);

    QTreeWidget *m_logTree   = nullptr;
    QTextEdit   *m_msgView   = nullptr;
    QTreeWidget *m_pathsTree = nullptr;
    QPushButton *m_moreBtn   = nullptr;
    QPushButton *m_diffBtn   = nullptr;

    QString     m_path;
    QString     m_tool;
    SvnManager *m_mgr        = nullptr;
    QObject    *m_procParent = nullptr;
    QList<SvnLogEntry> m_entries;
    long long   m_oldestRevision = -1;

    static constexpr int kPageSize = 100;
};
