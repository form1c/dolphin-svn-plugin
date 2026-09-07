#pragma once

#include <QDialog>
#include <QMap>

class QTableWidget;
class QPushButton;
class SvnManager;

class SvnPropertiesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SvnPropertiesDialog(const QString &path, SvnManager *mgr, QWidget *parent = nullptr);

protected:
    void accept() override;

private:
    void loadProperties();
    void refreshTable();
    void addProperty();
    void editProperty();
    void deleteProperty();
    void updateButtons();

    QString                m_path;
    SvnManager            *m_mgr;
    QMap<QString, QString> m_original;
    QMap<QString, QString> m_props;

    QTableWidget *m_table     = nullptr;
    QPushButton  *m_editBtn   = nullptr;
    QPushButton  *m_deleteBtn = nullptr;
};
