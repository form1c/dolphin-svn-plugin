#include "svnuihelpers.h"
#include "svnmanager.h"
#include "svnprogressdialog.h"
#include "svnrepobrowserdialog.h"
#include "svnsettings.h"

#include <KLocalizedString>

#include <QBoxLayout>
#include <QCheckBox>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

QStringList SvnUi::loadHistory(const QString &group, const QString &key)
{
    QSettings settings(QStringLiteral("DolphinSvnPlugin"), group);
    return settings.value(key).toStringList();
}

void SvnUi::pushHistory(const QString &group, const QString &value,
                        const QString &key)
{
    if (value.isEmpty())
        return;
    QSettings settings(QStringLiteral("DolphinSvnPlugin"), group);
    QStringList history = settings.value(key).toStringList();
    history.removeAll(value);
    history.prepend(value);
    while (history.size() > 10)
        history.removeLast();
    settings.setValue(key, history);
}

QString SvnUi::pickRepoUrl(const QString &currentText, const SvnInfo &wcInfo,
                          SvnManager *mgr, QWidget *parent)
{
    QString startUrl = currentText;
    if (startUrl.isEmpty() && wcInfo.valid)
        startUrl = wcInfo.repositoryRoot;
    SvnRepoBrowserDialog dlg(startUrl, mgr, parent,
                             SvnRepoBrowserDialog::Mode::PickUrl);
    if (dlg.exec() != QDialog::Accepted)
        return {};
    return dlg.selectedUrl();
}

QIcon SvnUi::makeFolderIcon()
{
    auto draw = [](int sz) -> QPixmap {
        QPixmap px(sz, sz);
        px.fill(Qt::transparent);
        QPainter p(&px);
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal tabH = sz * 0.26, tabW = sz * 0.42;
        QPolygonF tab;
        tab << QPointF(0.5, tabH) << QPointF(0.5, 1.5)
            << QPointF(tabW, 1.5) << QPointF(tabW + tabH, tabH);
        const QRectF body(0.5, tabH, sz - 1.0, sz - tabH - 0.5);
        QLinearGradient grad(0, tabH, 0, sz);
        grad.setColorAt(0.0, QColor(255, 205, 35));
        grad.setColorAt(1.0, QColor(220, 162, 10));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 228, 80));
        p.drawPolygon(tab);
        p.setBrush(grad);
        p.drawRoundedRect(body, 1.5, 1.5);
        p.setPen(QPen(QColor(170, 118, 8), sz < 20 ? 0.8 : 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(tab);
        p.drawRoundedRect(body, 1.5, 1.5);
        p.end();
        return px;
    };
    QIcon icon;
    for (const int sz : {16, 22, 32, 48}) icon.addPixmap(draw(sz));
    return icon;
}

SvnUi::LogOptionsBoxes SvnUi::createLogOptionsRow(
    QWidget *parent, QBoxLayout *row,
    std::function<void()> onReloadNeeded,
    std::function<void()> onFilterChanged)
{
    LogOptionsBoxes boxes;

    boxes.stopOnCopy = new QCheckBox(i18n("Stop on copy/rename"), parent);
    boxes.stopOnCopy->setToolTip(
        i18n("Stop the log at the revision where the item was copied "
             "(e.g. the branch point)"));
    boxes.stopOnCopy->setChecked(SvnSettings::logStopOnCopy());
    QObject::connect(boxes.stopOnCopy, &QCheckBox::toggled, parent,
                     [onReloadNeeded](bool on) {
                         SvnSettings::setLogStopOnCopy(on);
                         if (onReloadNeeded)
                             onReloadNeeded();
                     });
    row->addWidget(boxes.stopOnCopy);

    boxes.includeMerged = new QCheckBox(i18n("Include merged revisions"),
                                        parent);
    boxes.includeMerged->setToolTip(
        i18n("Also show revisions that arrived via merge, grouped under "
             "their merge revision"));
    boxes.includeMerged->setChecked(SvnSettings::logIncludeMerged());
    QObject::connect(boxes.includeMerged, &QCheckBox::toggled, parent,
                     [onReloadNeeded](bool on) {
                         SvnSettings::setLogIncludeMerged(on);
                         if (onReloadNeeded)
                             onReloadNeeded();
                     });
    row->addWidget(boxes.includeMerged);

    boxes.onlyAffectedPaths = new QCheckBox(i18n("Show only affected paths"),
                                            parent);
    boxes.onlyAffectedPaths->setToolTip(
        i18n("In the changed-paths list, hide paths outside the item this "
             "log was opened for"));
    boxes.onlyAffectedPaths->setChecked(SvnSettings::logOnlyAffectedPaths());
    QObject::connect(boxes.onlyAffectedPaths, &QCheckBox::toggled, parent,
                     [onFilterChanged](bool on) {
                         SvnSettings::setLogOnlyAffectedPaths(on);
                         if (onFilterChanged)
                             onFilterChanged();
                     });
    row->addWidget(boxes.onlyAffectedPaths);

    return boxes;
}

void SvnUi::runWithProgress(SvnManager *mgr, const QString &title,
                            QWidget *parentWidget, std::function<void()> startOp,
                            std::function<void()> onFinished)
{
    if (mgr->isBusy()) {
        QMessageBox::information(
            parentWidget, i18n("SVN"),
            i18n("An SVN operation is already in progress. Please wait."));
        return;
    }
    auto *dlg = new SvnProgressDialog(title, mgr, parentWidget);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    if (onFinished) {
        QObject::connect(dlg, &QDialog::finished, parentWidget,
                         [onFinished](int) { onFinished(); });
    }
    dlg->show();
    startOp();
}

void SvnUi::resolveWithThreeWayMerge(const QString &path, SvnManager *mgr,
                                     QWidget *parentWidget, QObject *ctx,
                                     std::function<void()> onResolved)
{
    QString base, mine, theirs;
    if (!mgr->conflictFilesSync(path, &base, &mine, &theirs)) {
        QMessageBox::information(
            parentWidget, i18n("SVN: Resolve Conflict"),
            i18n("This file is not in a text-conflict state\n"
                 "(no conflict helper files found)."));
        return;
    }
    const QString tool =
        QStandardPaths::findExecutable(QStringLiteral("angscheidrdiffer"));
    if (tool.isEmpty()) {
        QMessageBox::warning(
            parentWidget, i18n("SVN: Resolve Conflict"),
            i18n("AnGscheidrDiffer was not found — the 3-way merge "
                 "needs it.\nPlease install it and try again."));
        return;
    }
    // Start the differ with Base/Mine/Theirs; the result overwrites the working
    // file. Exit code 0 = saved and conflict-free.
    auto *proc = new QProcess(ctx);
    proc->setProgram(tool);
    proc->setArguments({QStringLiteral("--merge"), base, mine, theirs,
                        QStringLiteral("-o"), path});
    QObject::connect(proc, &QProcess::finished, ctx,
            [proc, path, mgr, parentWidget, onResolved](
                int exitCode, QProcess::ExitStatus st) {
        proc->deleteLater();
        if (st != QProcess::NormalExit || exitCode != 0)
            return; // cancelled or conflicts still open — mark nothing
        const auto ret = QMessageBox::question(
            parentWidget, i18n("SVN: Resolve Conflict"),
            i18n("The merge result was saved.\n"
                 "Mark the conflict as resolved (svn resolve)?"));
        if (ret == QMessageBox::Yes) {
            mgr->resolveAsync({path}, QStringLiteral("working"));
            if (onResolved)
                onResolved();
        }
    });
    proc->start();
}
