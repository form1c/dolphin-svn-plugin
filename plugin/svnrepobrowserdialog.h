#pragma once

#include <QDialog>

#include <functional>

class QComboBox;
class QPoint;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;
class SvnManager;

/**
 * TortoiseSVN-style repository browser: browsing via 'svn list' with lazy
 * loading, actions (checkout, export, log, branch/tag, open/save a file) and
 * direct repository write operations (mkdir/move/delete, one commit with a log
 * message each).
 */
class SvnRepoBrowserDialog : public QDialog
{
    Q_OBJECT
public:
    // PickUrl: Ok/Cancel instead of Close — the dialog serves as a URL picker
    // (e.g. branch selection in the merge dialog); selectedUrl() returns the choice.
    enum class Mode { Browse, PickUrl };

    SvnRepoBrowserDialog(const QString &startUrl, SvnManager *mgr,
                         QWidget *parent = nullptr, Mode mode = Mode::Browse);

    // Currently chosen URL: the selected tree entry, otherwise the header field.
    QString selectedUrl() const;

private:
    void loadRoot();
    bool loadChildren(QTreeWidgetItem *parentItem, const QString &url);
    void onItemExpanded(QTreeWidgetItem *item);
    void onItemDoubleClicked(QTreeWidgetItem *item);
    void showContextMenu(const QPoint &pos);
    void runOperation(const QString &title, std::function<void()> startOp);
    void openFileAtRevision(const QString &url, const QString &name);
    void saveRevisionAs(const QString &url, const QString &name);
    void createFolder(const QString &parentUrl);
    void renameEntry(const QString &url, const QString &name);
    void deleteEntry(const QString &url, const QString &name);
    void handleDragMove(QTreeWidgetItem *source, QTreeWidgetItem *target);
    QString currentUrl() const;   // URL in the header field
    QString revision() const;     // "HEAD" or a number
    void saveUrlToHistory(const QString &url);

    QComboBox    *m_urlCombo   = nullptr;
    QRadioButton *m_headRadio  = nullptr;
    QRadioButton *m_revRadio   = nullptr;
    QSpinBox     *m_revSpinBox = nullptr;
    QTreeWidget  *m_tree       = nullptr;
    QPushButton  *m_okBtn      = nullptr;   // only in PickUrl mode

    SvnManager *m_mgr = nullptr;
    Mode m_mode = Mode::Browse;
};
