#pragma once

#include <KAbstractFileItemActionPlugin>
#include <KFileItemListProperties>

#include "svnmanager.h"

class SvnPlugin : public KAbstractFileItemActionPlugin
{
    Q_OBJECT

public:
    SvnPlugin(QObject *parent, const QVariantList &args);

    QList<QAction *> actions(const KFileItemListProperties &fileItemInfos,
                             QWidget *parentWidget) override;

private:
    void runWithProgressDialog(const QString       &title,
                               QWidget             *parentWidget,
                               std::function<void()> startOperation);

    // Runs 'svn delete' behind the progress dialog. When a plain delete (neither
    // keepLocal nor force) fails because items have local modifications (E195006),
    // it offers to retry with --force. Calls itself once for that retry.
    void deleteWithProgress(const QStringList &paths, bool keepLocal, bool force,
                            QWidget *parentWidget);

    SvnManager m_svnManager;
};
