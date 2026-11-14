import io

# Headless install / restore of an edited stock map (owner request 2026-11-14: "get the toon shipment files deployed
# with backups made"): IW8Radiant.exe --install-stock <folder> [--out <report folder>] and --restore-stock <zone>.
# The same core functions and folders as File > Install Edited Stock Map Into Game / Restore Stock Map In Game, no
# dialog. The report (<out>/stock-install.txt) lists every file removed, moved and installed.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, count=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    write(p, s.replace(a, b))


patch('src/qtapp/main_window.h', '''    bool installEditedStockZones(const QString& outDir, const QString& zone, bool ask);''',
      '''    bool installEditedStockZones(const QString& outDir, const QString& zone, bool ask);
    // The same install and restore without any dialog (--install-stock / --restore-stock): `report` gets one line per
    // file removed, moved to the keep folder, installed or put back, and the reason when it fails. `zone` of the
    // install is taken from the folder's first zone file (techsets_<zone>.ff or <zone>.ff).
    bool installStockZonesQuiet(const QString& outDir, QStringList& report);
    bool restoreStockZonesQuiet(const QString& zone, QStringList& report);''')

patch('src/qtapp/main_window_stock_edit.cpp', '''void MainWindow::restoreStockMap() {''', '''bool MainWindow::installStockZonesQuiet(const QString& outDir, QStringList& out) {
    const QString replay = toolPath("replay");
    const QString zoneDir = QFileInfo(replay).absolutePath() + "/zone";
    const std::vector<std::string> files = stockZoneFiles(outDir.toStdString());
    if (replay.isEmpty() || !QFileInfo(zoneDir).isDir()) {
        out << "FAIL the game folder is not set (Tools > Tool Paths)";
        return false;
    }
    if (files.empty()) {
        out << "FAIL there is no edited zone file (.ff) in " + QDir::toNativeSeparators(outDir);
        return false;
    }
    QString zone = QFileInfo(qs(files.front())).completeBaseName();
    if (zone.startsWith("techsets_")) zone = zone.mid(9);
    const QString backup = stockBackupDir(zone), keep = stockKeepDir();
    out << "zone " + zone << "game zone folder " + QDir::toNativeSeparators(zoneDir) << "stock files kept in " + QDir::toNativeSeparators(keep)
        << "install markers in " + QDir::toNativeSeparators(backup);
    StockInstallReport report;
    std::string err;
    const bool ok = installStockZones(outDir.toStdString(), zoneDir.toStdString(), keep.toStdString(), backup.toStdString(), report, &err);
    for (const std::string& f : report.removed) out << "removed " + qs(f), EditorLog::instance().add(LogLevel::Info, "stock", "removed " + qs(f) + " from " + zoneDir);
    for (const std::string& f : report.backedUp) out << "moved the stock " + qs(f) + " to the keep folder", EditorLog::instance().add(LogLevel::Info, "stock", "moved the stock " + qs(f) + " to " + keep);
    for (const std::string& f : report.installed) out << "installed " + qs(f), EditorLog::instance().add(LogLevel::Info, "stock", "installed " + qs(f) + " into " + zoneDir);
    if (!ok) {
        out << "FAIL " + qs(err);
        return false;
    }
    lastStockRebuildZone_ = zone;
    out << QString("ok %1 zone file(s) installed").arg(report.installed.size());
    return true;
}

bool MainWindow::restoreStockZonesQuiet(const QString& zone, QStringList& out) {
    const QString replay = toolPath("replay");
    const QString zoneDir = QFileInfo(replay).absolutePath() + "/zone";
    if (replay.isEmpty() || !QFileInfo(zoneDir).isDir() || zone.isEmpty()) {
        out << "FAIL the game folder is not set (Tools > Tool Paths), or no map was named";
        return false;
    }
    const QString backup = stockBackupDir(zone);
    std::vector<std::string> installed = installedStockZones(backup.toStdString());
    for (const std::string& f : installedStockPatches(backup.toStdString())) installed.push_back(f);
    if (installed.empty()) {
        out << "FAIL no edit of " + zone + " is installed by this editor";
        return false;
    }
    StockInstallReport report;
    std::string err;
    const bool ok = restoreStockZones(backup.toStdString(), zoneDir.toStdString(), stockKeepDir().toStdString(), report, &err);
    for (const std::string& f : report.removed) out << "removed " + qs(f), EditorLog::instance().add(LogLevel::Info, "stock", "removed " + qs(f) + " from " + zoneDir);
    for (const std::string& f : report.restored) out << "put back the stock " + qs(f), EditorLog::instance().add(LogLevel::Info, "stock", "put back the stock " + qs(f));
    if (!ok) {
        out << "FAIL " + qs(err);
        return false;
    }
    out << QString("ok %1 file(s) removed, %2 stock file(s) put back").arg(report.removed.size()).arg(report.restored.size());
    return true;
}

void MainWindow::restoreStockMap() {''')

patch('src/qtapp/main.cpp', '''    QString filterShaderFile;  // --shader-filter-program <file.iw8shader.hlsl> --out <file.cso>''',
      '''    QString filterShaderFile;  // --shader-filter-program <file.iw8shader.hlsl> --out <file.cso>
    // --install-stock <folder with edited zone files> / --restore-stock <map>: File > Install Edited Stock Map Into
    // Game and Restore Stock Map In Game without a dialog; the report goes to <--out folder>/stock-install.txt.
    QString installStockDir, restoreStockZone;''')
patch('src/qtapp/main.cpp', '''        else if (a == "--shader-filter-program" && i + 1 < argc) filterShaderFile = QString::fromLocal8Bit(argv[++i]);''',
      '''        else if (a == "--shader-filter-program" && i + 1 < argc) filterShaderFile = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--install-stock" && i + 1 < argc) installStockDir = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--restore-stock" && i + 1 < argc) restoreStockZone = QString::fromLocal8Bit(argv[++i]);''')
patch('src/qtapp/main.cpp', '''    const bool smoke = !smokeFile.isEmpty() || !compileFile.isEmpty() || !propsZone.isEmpty() || featureShots;''',
      '''    const bool stockInstall = !installStockDir.isEmpty() || !restoreStockZone.isEmpty();
    const bool smoke = !smokeFile.isEmpty() || !compileFile.isEmpty() || !propsZone.isEmpty() || featureShots || stockInstall;
    if (stockInstall && !outDir.isEmpty()) QDir().mkpath(outDir);''')
patch('src/qtapp/main.cpp', '''        if (!openFile.isEmpty()) w.openPath(openFile);
        if (!compileFile.isEmpty()) {''', '''        if (!openFile.isEmpty()) w.openPath(openFile);
        if (stockInstall) {
            QTimer::singleShot(300, &w, [&] {
                QStringList report;
                const bool ok = !installStockDir.isEmpty() ? w.installStockZonesQuiet(QDir::fromNativeSeparators(installStockDir), report)
                                                           : w.restoreStockZonesQuiet(restoreStockZone, report);
                QFile rf(QDir(outDir.isEmpty() ? QDir::tempPath() : outDir).filePath("stock-install.txt"));
                if (rf.open(QIODevice::WriteOnly | QIODevice::Text)) rf.write((report.join("\\n") + "\\n").toUtf8());
                app.exit(ok ? 0 : 5);
            });
        }
        if (!compileFile.isEmpty()) {''')
print('ok')
