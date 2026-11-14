import io

# "One process per run" (PROGRESS, 2026-11-11 perf notes): opening a stock map started its own acts.exe for the
# map_ents export. That run loaded the whole zone, and the world export that follows loaded it again in a server of
# the shared runner. The map_ents export now goes through the runner too, so the zone is loaded once and the world
# export finds it in memory. A zone outside the game's zone folder, or the view copy of an installed edit, still runs
# as its own process (the runner reads zones by name from the game folder).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, n=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/qtapp/main_window.h'
patch(H, '''    QByteArray webRenderFrame(const struct WebCamera& cam, int w, int h, int quality);''',
      '''    QByteArray webRenderFrame(const struct WebCamera& cam, int w, int h, int quality);
    // The map_ents export of a stock zone file into `cache` (<cache>/mw19replay/<zone>/assets/map_ents/...): through the
    // shared ACTS runner when the file is the game's own zone (the zone then stays loaded for the world export), else
    // as one acts.exe run. Returns the JSON's path, or "" with *err set. *viaRunner says which way it went.
    QString exportStockMapEnts(const QString& zone, const QString& cache, QString* err, bool* viaRunner = nullptr);''')
M = 'src/qtapp/main_window.cpp'
patch(M, '''    QString json = findMapEntsJson(cache);
    if (json.isEmpty()) {
        QDir().mkpath(cache);
        // While an edit of this map is installed, the patch beside the zone is ours: the stock map is read from a
        // copy with the stock patch saved at install (main_window_stock_edit.cpp).
        QString viewDir;
        const QString readZone = stockZoneForReading(zone, &viewDir, err);
        if (readZone.isEmpty()) return false;
        QProcess proc;
        proc.setProgram(acts);
        proc.setArguments({"--noUpdater", "fastfile", "-r", "mw19replay", "-g", replay, "--oodle", oodle, "-a", "map_ents", "-o", cache, readZone});
        proc.setProcessChannelMode(QProcess::MergedChannels);
        QProgressDialog progress("Reading " + name + " with ACTS (map_ents only)...", "Cancel", 0, 0, this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(500);
        proc.start();
        while (!proc.waitForFinished(100)) {
            QApplication::processEvents();
            if (progress.wasCanceled()) {
                proc.kill();
                proc.waitForFinished(3000);
                if (!viewDir.isEmpty()) QDir(viewDir).removeRecursively();
                if (err) *err = "ACTS cancelled";
                return false;
            }
            if (proc.state() == QProcess::NotRunning) break;
        }
        const QString log = QString::fromLocal8Bit(proc.readAll());
        if (!viewDir.isEmpty()) QDir(viewDir).removeRecursively();
        QFile logFile(cache + "/acts.log");
        if (logFile.open(QIODevice::WriteOnly)) logFile.write(log.toUtf8());
        json = findMapEntsJson(cache);
        if (proc.exitCode() != 0 || json.isEmpty()) {
            if (err) *err = "ACTS did not produce map_ents for " + name + " (exit " + QString::number(proc.exitCode()) + ").\\n\\n" + log.right(1500);
            return false;
        }
    }
    loadDocument(json);''', '''    QString json = findMapEntsJson(cache);
    if (json.isEmpty()) json = exportStockMapEnts(zone, cache, err);
    if (json.isEmpty()) return false;
    loadDocument(json);''')
patch(M, '''bool MainWindow::openStockZone(const QString& zone, QString* err) {''', '''QString MainWindow::exportStockMapEnts(const QString& zone, const QString& cache, QString* err, bool* viaRunner) {
    if (viaRunner) *viaRunner = false;
    const QString acts = toolPath("acts"), replay = toolPath("replay"), oodle = toolPath("oodle");
    const QString name = QFileInfo(zone).completeBaseName();
    QDir().mkpath(cache);
    // While an edit of this map is installed, the patch beside the zone is ours: the stock map is read from a
    // copy with the stock patch saved at install (main_window_stock_edit.cpp).
    QString viewDir;
    const QString readZone = stockZoneForReading(zone, &viewDir, err);
    if (readZone.isEmpty()) return {};
    QProgressDialog progress("Reading " + name + " with ACTS (map_ents only)...", "Cancel", 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);
    // The game's own zone goes through the shared runner: its server keeps the zone in memory, and the world export
    // that follows does not load it a second time.
    const QString gameZone = QFileInfo(QFileInfo(replay).absolutePath() + "/zone/" + name + ".ff").canonicalFilePath();
    const bool useRunner = viewDir.isEmpty() && readZone == zone && !gameZone.isEmpty() &&
                           gameZone.compare(QFileInfo(zone).canonicalFilePath(), Qt::CaseInsensitive) == 0;
    int exitCode = -1;
    QString log;
    if (useRunner) {
        if (viaRunner) *viaRunner = true;
        ActsRequest req;
        req.zone = name;
        req.types = "map_ents";
        req.outDir = cache;
        QObject owner;  // this request alone: a cancel drops it without touching the other exports
        bool done = false;
        ActsResult res;
        ActsRunner::shared()->submit([this](const QString& k) { return toolPath(k); }, req, &owner, [&done, &res](const ActsResult& r) {
            res = r;
            done = true;
        });
        while (!done) {
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            if (progress.wasCanceled()) {
                ActsRunner::shared()->cancel(&owner);
                if (err) *err = "ACTS cancelled";
                return {};
            }
            if (!done) QThread::msleep(5);
        }
        exitCode = res.started ? res.exitCode : -1;
        log = QString::fromLocal8Bit(res.log);
        if (!res.error.isEmpty()) log += "\\n" + res.error;
    } else {
        QProcess proc;
        proc.setProgram(acts);
        proc.setArguments({"--noUpdater", "fastfile", "-r", "mw19replay", "-g", replay, "--oodle", oodle, "-a", "map_ents", "-o", cache, readZone});
        proc.setProcessChannelMode(QProcess::MergedChannels);
        proc.start();
        while (!proc.waitForFinished(100)) {
            QApplication::processEvents();
            if (progress.wasCanceled()) {
                proc.kill();
                proc.waitForFinished(3000);
                if (!viewDir.isEmpty()) QDir(viewDir).removeRecursively();
                if (err) *err = "ACTS cancelled";
                return {};
            }
            if (proc.state() == QProcess::NotRunning) break;
        }
        exitCode = proc.exitCode();
        log = QString::fromLocal8Bit(proc.readAll());
    }
    if (!viewDir.isEmpty()) QDir(viewDir).removeRecursively();
    QFile logFile(cache + "/acts.log");
    if (logFile.open(QIODevice::WriteOnly)) logFile.write(log.toUtf8());
    const QString json = findMapEntsJson(cache);
    // 127: the export is complete but some asset was not available; the map_ents file decides.
    if ((exitCode != 0 && exitCode != 127) || json.isEmpty()) {
        if (err) *err = "ACTS did not produce map_ents for " + name + " (exit " + QString::number(exitCode) + ").\\n\\n" + log.right(1500);
        return {};
    }
    return json;
}

bool MainWindow::openStockZone(const QString& zone, QString* err) {''')
patch('src/qtapp/qt_gui_test.cpp', '''        QString zerr;
        const bool ok = w.openStockZone(zone, &zerr);''', '''        {
            // The map_ents export goes through the shared ACTS runner (the zone stays loaded for the world export):
            // twice into empty folders, the second one finds the zone in the server's memory.
            ActsRunner* runner = ActsRunner::shared();
            const int served0 = runner->servedRequests(), cached0 = runner->cachedRequests(), oneShot0 = runner->oneShotRuns();
            const QString c1 = QDir(outDir).filePath("map_ents_runner_1"), c2 = QDir(outDir).filePath("map_ents_runner_2");
            QDir(c1).removeRecursively();
            QDir(c2).removeRecursively();
            QString e1, e2;
            bool via1 = false, via2 = false;
            const QString j1 = w.exportStockMapEnts(zone, c1, &e1, &via1);
            const QString j2 = w.exportStockMapEnts(zone, c2, &e2, &via2);
            const int served = runner->servedRequests() - served0, cached = runner->cachedRequests() - cached0,
                      oneShot = runner->oneShotRuns() - oneShot0;
            const bool wrote = !j1.isEmpty() && !j2.isEmpty() && QFileInfo(j1).size() > 1000 && QFileInfo(j1).size() == QFileInfo(j2).size();
            // An acts.exe without the serve mode runs each request as one process: then nothing is served or cached.
            const bool reused = runner->serveSupported() ? served == 2 && cached >= 1 : oneShot == 2;
            r.check(via1 && via2 && wrote && reused,
                    QString("the stock map's entities are exported through the shared ACTS runner, the second time from the loaded zone "
                            "(served %1, from memory %2, own processes %3)%4")
                        .arg(served).arg(cached).arg(oneShot).arg(e1.isEmpty() && e2.isEmpty() ? QString() : " (" + (e1 + e2).left(160) + ")"));
        }
        QString zerr;
        const bool ok = w.openStockZone(zone, &zerr);''')
print('ok')
