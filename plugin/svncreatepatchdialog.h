#pragma once

#include <QDialog>
#include <QStringList>

class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class SvnManager;

/**
 * "Create Patch…": selects the files to patch and the target file.
 *
 * Built after the SvnCommitDialog model (checkbox file list, everything
 * preselected), but flat and without a message area. Unversioned entries are
 * deliberately absent: 'svn diff' cannot represent them, they would silently
 * disappear from the patch.
 */
class SvnCreatePatchDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SvnCreatePatchDialog(const QStringList &paths, SvnManager *svnManager,
                                  QWidget *parent = nullptr);

    /** Checked file paths (absolute). */
    QStringList selectedPaths() const;
    /** Target file of the patch (absolute). */
    QString     outputFile()    const;

protected:
    void accept() override;

private:
    void populateTree(const QStringList &paths, SvnManager *mgr);
    void updateState();

    QTreeWidget *m_tree       = nullptr;
    QLabel      *m_countLabel = nullptr;
    QLineEdit   *m_fileEdit   = nullptr;
    QPushButton *m_saveBtn    = nullptr;

    QString m_wcRoot;
};
