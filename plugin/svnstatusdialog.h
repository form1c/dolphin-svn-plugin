#pragma once

#include <QDialog>
#include <QStringList>

#include <functional>

class QCheckBox;
class QLabel;
class QPoint;
class QTreeWidget;
class QTreeWidgetItem;
class SvnManager;

class SvnStatusDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SvnStatusDialog(const QStringList &paths, SvnManager *mgr,
                             QWidget *parent = nullptr);

private:
    void refresh();
    void showContextMenu(const QPoint &pos);
    void diffFile(const QString &absPath);
    void runOperation(const QString &title, std::function<void()> startOp);

    QTreeWidget *m_tree            = nullptr;
    QCheckBox   *m_showUnversioned = nullptr;
    QLabel      *m_countLabel      = nullptr;
    bool         m_showUpdates     = false;

    QStringList  m_paths;
    SvnManager  *m_mgr = nullptr;
};
