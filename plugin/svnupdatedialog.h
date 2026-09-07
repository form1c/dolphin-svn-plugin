#pragma once

#include "svntypes.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QRadioButton;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;
class QTextEdit;
class QPushButton;
class SvnManager;

class SvnUpdateDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SvnUpdateDialog(const QString &path, SvnManager *mgr,
                             QWidget *parent = nullptr);

    /** Returns "HEAD" or the entered revision number. */
    QString revision() const;

    /** Returns the chosen depth: "infinity", "immediates", "files" or "empty". */
    QString depth() const;

private:
    void loadMore();
    void reloadLog();
    void showEntry(QTreeWidgetItem *item);
    static QString formatDate(const QString &isoDate);
    static QString actionLabel(const QString &action);
    static QString actionIconName(const QString &action);

    // Revision + Tiefe
    QRadioButton *m_headRadio  = nullptr;
    QRadioButton *m_revRadio   = nullptr;
    QSpinBox     *m_revSpinBox = nullptr;
    QComboBox    *m_depthCombo = nullptr;

    // Log-Tabelle
    QTreeWidget *m_logTree   = nullptr;
    QTextEdit   *m_msgView   = nullptr;
    QTreeWidget *m_pathsTree = nullptr;
    QPushButton *m_moreBtn   = nullptr;

    QString     m_path;
    SvnManager *m_mgr = nullptr;
    QList<SvnLogEntry> m_entries;
    long long   m_oldestRevision = -1;

    SvnInfo   m_wcInfo;                       // cached from the ctor
    QString   m_repoRelPath;                  // for "Show only affected paths"
    QCheckBox *m_onlyAffectedBox = nullptr;
    QTreeWidgetItem *m_lastShownItem = nullptr;

    static constexpr int kPageSize = 100;
};
