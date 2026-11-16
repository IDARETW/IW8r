import io

# Effects of an own map's fx_origins that have no export yet are exported with ACTS on demand (from the zone the
# common effects catalogue lists), as Effect Info did for its own window: MainWindow::requestVfxExport, used by Effect
# Info and the camera's effect source. Owner checklist 2026-11-15 (B8): of the compact showcase's four effects only the
# one exported earlier (by the dev showcase) was drawn.

P = 'E:/IW8-Radiant/src/qtapp/main_window.cpp'
H = 'E:/IW8-Radiant/src/qtapp/main_window.h'


def rw(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        if b in s and a not in s:
            continue
        assert s.count(a) == 1, (p, s.count(a), a[:90])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


rw(P, [
('''    if (found.isEmpty() && !name.endsWith(".asset.json") && !vfxExporting_.contains(name)) {
        // 8.9: export it now with ACTS from the zone that holds it (always-loaded MP zones list), then show it again.
        QString zone;
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 4 && zone.isEmpty(); ++up, d.cdUp())
            if (QFile f(d.filePath("data/asset_catalog/iw8_common_vfx.tsv")); f.open(QIODevice::ReadOnly | QIODevice::Text))
                while (!f.atEnd()) {
                    const QStringList c = QString::fromUtf8(f.readLine()).trimmed().split('\\t');
                    if (c.size() >= 2 && c[0] == stem) {
                        zone = c[1].section(',', 0, 0).trimmed();
                        break;
                    }
                }
        if (!zone.isEmpty()) {
            vfxExporting_.insert(name);
            ActsRequest req;
            req.zone = zone;
            req.types = "vfx";
            req.names = {stem};
            req.outDir = ownCache + "/" + zone;
            QDir().mkpath(req.outDir);
            ActsRunner::shared()->submit([this](const QString& k) { return toolPath(k); }, req, this, [this, name](const ActsResult& r) {
                vfxExporting_.remove(name);
                forgetVfxFiles();                   // new export files
                fxSims_.erase(name.toStdString());  // the camera preview picks the new export up
                if (findChild<QDialog*>("effectInfoDialog")) showEffectInfo(name);  // refresh the open window
                if (!r.started || (r.exitCode != 0 && r.exitCode != 127)) ctx_.setStatus("Effect export failed for " + name + " (ACTS)");
            });
            ctx_.setStatus("Exporting " + name + " from " + zone + " with ACTS (Effect Info refreshes when it is done)");
        }
    }''', '''    if (found.isEmpty() && !name.endsWith(".asset.json") && requestVfxExport(name))
        ctx_.setStatus("Exporting " + name + " with ACTS (Effect Info refreshes when it is done)");'''),
('''QString MainWindow::showEffectInfo(const QString& name, bool show) {''', '''bool MainWindow::requestVfxExport(const QString& name) {
    if (name.isEmpty() || vfxExporting_.contains(name) || vfxExportFailed_.contains(name)) return vfxExporting_.contains(name);
    QString stem = name;
    stem.replace('\\\\', '/');
    // 8.9: the zone that holds it (the always-loaded MP zones the common effects catalogue lists).
    static const QHash<QString, QString> zones = [] {
        QHash<QString, QString> z;
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 4 && z.isEmpty(); ++up, d.cdUp())
            if (QFile f(d.filePath("data/asset_catalog/iw8_common_vfx.tsv")); f.open(QIODevice::ReadOnly | QIODevice::Text))
                while (!f.atEnd()) {
                    const QStringList c = QString::fromUtf8(f.readLine()).trimmed().split('\\t');
                    if (c.size() >= 2 && !c[0].startsWith('#')) z.insert(c[0], c[1].section(',', 0, 0).trimmed());
                }
        return z;
    }();
    const QString zone = zones.value(stem);
    if (zone.isEmpty()) {
        vfxExportFailed_.insert(name);  // not in a zone the editor can export it from: asked once
        return false;
    }
    const QString ownCache = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/vfx_cache";
    vfxExporting_.insert(name);
    ActsRequest req;
    req.zone = zone;
    req.types = "vfx";
    req.names = {stem};
    req.outDir = ownCache + "/" + zone;
    QDir().mkpath(req.outDir);
    ActsRunner::shared()->submit([this](const QString& k) { return toolPath(k); }, req, this, [this, name](const ActsResult& r) {
        vfxExporting_.remove(name);
        forgetVfxFiles();                   // new export files
        fxSims_.erase(name.toStdString());  // the camera preview picks the new export up
        const bool ok = r.started && (r.exitCode == 0 || r.exitCode == 127);
        if (!ok) {
            vfxExportFailed_.insert(name);
            ctx_.setStatus("Effect export failed for " + name + " (ACTS)");
        }
        if (findChild<QDialog*>("effectInfoDialog")) showEffectInfo(name);  // refresh the open window
        if (camera_) camera_->update();
        emit vfxExportDone(name, ok);
    });
    return true;
}

QString MainWindow::showEffectInfo(const QString& name, bool show) {'''),
('''        fxSims_[fx] = out;  // also remembers "no export" (cleared when Effect Info exports one)
        return out;''', '''        // No export yet (an own map's fx_origin of a common effect): exported now with ACTS; the camera asks again when
        // it is done (requestVfxExport clears this "no export" entry).
        if (!out && f.isEmpty()) requestVfxExport(qs(fx));
        fxSims_[fx] = out;
        return out;'''),
])

rw(H, [
('''    QString findVfxExport(const QString& name) const;''', '''    QString findVfxExport(const QString& name) const;
public:
    // Exports a stock effect with ACTS when the caches hold none (from the zone the common effects catalogue lists);
    // true while an export runs. vfxExportDone follows (Effect Info, the camera and the Effects panel refresh then).
    bool requestVfxExport(const QString& name);
signals:
    void vfxExportDone(const QString& name, bool ok);
private:
    QSet<QString> vfxExportFailed_;  // not exportable or failed: not asked again this session
public:'''),
])
print('ok')
