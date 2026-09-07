#pragma once

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QTreeWidget;
class QTreeWidgetItem;
class QPlainTextEdit;
class QLabel;
class QPushButton;
class QComboBox;
class SvnManager;

class SvnCommitDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SvnCommitDialog(const QStringList &paths, SvnManager *svnManager,
                              QWidget *parent = nullptr);

    QString     message()           const;
    QStringList selectedPaths()     const;  // all checked items (versioned + unversioned)
    QStringList unversionedPaths()  const;  // checked unversioned items (need svn add first)
    bool        keepLocks()         const;  // "Keep locks" checkbox (--no-unlock)

    // Merge commit template: appears as the top entry in the "Recent messages"
    // dropdown (italic), NOT prefilled into the editor — otherwise it would end
    // up in commits that have nothing to do with the merge.
    void setMergeMessage(const QString &msg);

protected:
    void accept() override;

private:
    void populateTree(const QStringList &paths, SvnManager *mgr);
    void onItemChanged(QTreeWidgetItem *item, int col);
    // QSettings key of the commit message history (per repository).
    QString historyKey() const;
    void loadMessageHistory();
    void saveMessageToHistory(const QString &msg);
    // A1: clear the history of the current repo after a confirmation.
    void clearMessageHistory();
    void updateSelectionLabel();
    // Shows or hides every unversioned row (and its children). Kept in one place
    // so the initial population, the checkbox toggle and refresh() stay in sync.
    void setUnversionedHidden(bool hide);

    void refresh();

    QTreeWidget    *m_tree              = nullptr;
    QLabel         *m_selectionLabel   = nullptr;
    QComboBox      *m_historyCombo     = nullptr;
    QPlainTextEdit *m_messageEdit      = nullptr;
    QLabel         *m_charCountLabel   = nullptr;
    QPushButton    *m_commitBtn        = nullptr;
    QCheckBox      *m_hideUnversioned  = nullptr;
    QCheckBox      *m_keepLocks        = nullptr;
    bool            m_block            = false;

    QStringList  m_paths;
    QString      m_historyKey;
    QString      m_mergeMessage;  // "(Merge)" template (not part of the history)
    SvnManager  *m_mgr = nullptr;
};
