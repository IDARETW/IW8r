import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/qtapp/main_window.h', [(
 '    int showAllStockProps();  // clears the list; returns how many were hidden\n',
 '    int showAllStockProps();  // clears the list; returns how many were hidden\n'
 '    // Owner actions (File menu): copy the patch files of the last stock rebuild into the game\'s zone folder (a stock\n'
 '    // patch of the same name is backed up first), and take them out again. Both ask before they write.\n'
 '    void installEditedStockMap();\n'
 '    void restoreStockMap();\n'
 '    QString stockBackupDir(const QString& zone) const;\n'),
 ('    QString stockZone_;            // zone name of an opened stock map ("" otherwise)\n',
  '    QString stockZone_;            // zone name of an opened stock map ("" otherwise)\n'
  '    QString lastStockRebuild_;     // work folder of the last stock rebuild that passed its check ("" = none)\n'
  '    QString lastStockRebuildZone_;\n')])

patch('src/qtapp/main_window.cpp', [(
 '    addCommand(file, "HideStockProps", "Hide Stock Props In Selection", QKeySequence(), [this] { hideStockPropsInSelection(); });\n',
 '    addCommand(file, "InstallStockMap", "Install Edited Stock Map Into Game...", QKeySequence(), [this] { installEditedStockMap(); });\n'
 '    addCommand(file, "RestoreStockMap", "Restore Stock Map In Game...", QKeySequence(), [this] { restoreStockMap(); });\n'
 '    addCommand(file, "HideStockProps", "Hide Stock Props In Selection", QKeySequence(), [this] { hideStockPropsInSelection(); });\n')])

p = 'src/qtapp/main_window_stock_edit.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (s.count(a), a[:70])
    s = s.replace(a, b)
rep('''                if (--*remaining > 0) return;
                ctx_.setStatus(*failed == 0 ? "Stock zone rebuilt and checked (ACTS, 0 failed): " + work + "/out (not installed)"
''', '''                if (--*remaining > 0) return;
                if (*failed == 0) {
                    lastStockRebuild_ = work;
                    lastStockRebuildZone_ = zone;
                }
                ctx_.setStatus(*failed == 0 ? "Stock zone rebuilt and checked (ACTS, 0 failed): " + work +
                                                  "/out (not installed; File > Install Edited Stock Map Into Game)"
''')
rep('''int MainWindow::hideStockPropsInSelection() {''', '''QString MainWindow::stockBackupDir(const QString& zone) const {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/stock_backup/" + zone;
}

// The edited map goes into the game as patch files (<zone>.fp beside the stock <zone>.ff, which the game applies when
// it loads the zone): the stock zones are not changed. Only this action and Restore write to the game folder, and
// only after the owner confirms the list.
void MainWindow::installEditedStockMap() {
    if (lastStockRebuild_.isEmpty()) {
        QMessageBox::information(this, "Install Edited Stock Map",
                                 "Rebuild the stock map first (File > Rebuild Stock Zone). The install uses the last rebuild that passed its check.");
        return;
    }
    const QString replay = toolPath("replay");
    const QString zoneDir = QFileInfo(replay).absolutePath() + "/zone";
    const QString patchDir = lastStockRebuild_ + "/out/patch";
    const std::vector<std::string> files = stockPatchFiles(patchDir.toStdString());
    if (replay.isEmpty() || !QFileInfo(zoneDir).isDir() || files.empty()) {
        QMessageBox::warning(this, "Install Edited Stock Map",
                             files.empty() ? "The last rebuild wrote no patch files (" + patchDir + "). An old converter writes none: update it."
                                           : "The game folder is not set (Tools > Tool Paths).");
        return;
    }
    QStringList names;
    for (const std::string& f : files) names << qs(f);
    const QString backup = stockBackupDir(lastStockRebuildZone_);
    if (QMessageBox::question(this, "Install Edited Stock Map",
                              QString("Copy %1 patch file(s) into the game?\\n\\n%2\\n\\nTo: %3\\n\\nThe stock zone files are not changed: the game "
                                      "applies a patch beside its zone. A stock patch of the same name is saved first in\\n%4\\n\\n"
                                      "Close the game before you continue. File > Restore Stock Map In Game undoes this.")
                                  .arg(names.size()).arg(names.join("\\n"), QDir::toNativeSeparators(zoneDir), QDir::toNativeSeparators(backup)),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    StockInstallReport report;
    std::string err;
    const bool ok = installStockPatches(patchDir.toStdString(), zoneDir.toStdString(), backup.toStdString(), report, &err);
    for (const std::string& f : report.backedUp) EditorLog::instance().add(LogLevel::Info, "stock", "saved the stock " + qs(f) + " in " + backup);
    for (const std::string& f : report.installed) EditorLog::instance().add(LogLevel::Info, "stock", "installed " + qs(f) + " into " + zoneDir);
    if (!ok) {
        QMessageBox::warning(this, "Install Edited Stock Map", qs(err));
        ctx_.setStatus("Install Edited Stock Map failed: " + qs(err));
        return;
    }
    ctx_.setStatus(QString("Installed the edited %1 (%2 patch file(s)); File > Restore Stock Map In Game takes it out")
                       .arg(lastStockRebuildZone_).arg(report.installed.size()));
}

void MainWindow::restoreStockMap() {
    const QString zone = !stockZone_.isEmpty() ? stockZone_ : lastStockRebuildZone_;
    if (zone.isEmpty()) {
        QMessageBox::information(this, "Restore Stock Map", "Open the stock map whose edit you want to take out (File > Open IW8 Stock Map).");
        return;
    }
    const QString backup = stockBackupDir(zone);
    const std::vector<std::string> installed = installedStockPatches(backup.toStdString());
    if (installed.empty()) {
        QMessageBox::information(this, "Restore Stock Map", "No edit of " + zone + " is installed by this editor.");
        return;
    }
    const QString zoneDir = QFileInfo(toolPath("replay")).absolutePath() + "/zone";
    QStringList names;
    for (const std::string& f : installed) names << qs(f);
    if (QMessageBox::question(this, "Restore Stock Map",
                              QString("Take the edit of %1 out of the game?\\n\\n%2\\n\\nFrom: %3\\n\\nA stock patch that was saved at install "
                                      "is put back. Close the game before you continue.")
                                  .arg(zone, names.join("\\n"), QDir::toNativeSeparators(zoneDir)),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    StockInstallReport report;
    std::string err;
    const bool ok = restoreStockPatches(backup.toStdString(), zoneDir.toStdString(), report, &err);
    for (const std::string& f : report.removed) EditorLog::instance().add(LogLevel::Info, "stock", "removed " + qs(f) + " from " + zoneDir);
    for (const std::string& f : report.restored) EditorLog::instance().add(LogLevel::Info, "stock", "put back the stock " + qs(f));
    if (!ok) {
        QMessageBox::warning(this, "Restore Stock Map", qs(err));
        return;
    }
    ctx_.setStatus(QString("Restored the stock %1 (%2 file(s) removed, %3 stock file(s) put back)")
                       .arg(zone).arg(report.removed.size()).arg(report.restored.size()));
}

int MainWindow::hideStockPropsInSelection() {''')
if '#include "stock_install.h"' not in s:
    rep('#include "stock_spawns.h"\n', '#include "stock_install.h"\n#include "stock_spawns.h"\n')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
