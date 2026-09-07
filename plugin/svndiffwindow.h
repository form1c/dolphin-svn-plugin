#pragma once

#include <QDialog>
#include <QStringList>

class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class SvnManager;

class SvnDiffWindow : public QDialog
{
    Q_OBJECT
public:
    explicit SvnDiffWindow(const QStringList &paths, SvnManager *mgr,
                           const QString &tool, QObject *processParent,
                           QWidget *parent = nullptr);

    // Returns the absolute path to the first available diff tool, empty if none.
    static QString findDiffTool();

    // Compares the given revision vs local file using the given external tool.
    // Defaults to BASE (last committed revision in the WC).
    // processParent keeps the QProcess alive; the temp file is removed when done.
    static void launchFileDiff(const QString &tool, const QString &wcPath,
                               SvnManager *mgr, QObject *processParent,
                               const QString &revision = QStringLiteral("BASE"));

    // Compares two repository states of a FILE in the external diff tool
    // (both sides as temp files, read-only with AnGscheidrDiffer).
    // target: WC path or repo URL; revLeft < 1 → empty left side
    // (a file added in revRight). Returns false if the content of revRight
    // could not be obtained (e.g. a directory) — the caller then shows its
    // previous text fallback.
    static bool launchHistoricDiff(const QString &tool, const QString &target,
                                   long long revLeft, long long revRight,
                                   SvnManager *mgr, QObject *processParent);

    // Like launchHistoricDiff, but for FOLDERS (or whole revisions of a folder
    // log): exports both revision states into temp directories and opens the
    // diff tool as a folder comparison. Returns false if the export fails.
    static bool launchHistoricFolderDiff(const QString &tool,
                                         const QString &target,
                                         long long revLeft, long long revRight,
                                         SvnManager *mgr,
                                         QObject *processParent);

private:
    void diffItem(QTreeWidgetItem *item);

    QTreeWidget *m_tree       = nullptr;
    QPushButton *m_diffBtn    = nullptr;
    QString      m_tool;
    SvnManager  *m_mgr        = nullptr;
    QObject     *m_procParent = nullptr;
};
