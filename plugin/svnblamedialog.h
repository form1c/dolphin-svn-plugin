#pragma once

#include <QDialog>

class QPoint;
class QTableWidget;
class SvnManager;

class SvnBlameDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SvnBlameDialog(const QString &path, SvnManager *mgr, QWidget *parent = nullptr);

private:
    void loadBlame();
    void showContextMenu(const QPoint &pos);

    QString       m_path;
    SvnManager   *m_mgr;
    QTableWidget *m_table = nullptr;
};
