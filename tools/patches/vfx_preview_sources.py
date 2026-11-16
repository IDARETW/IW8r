import io

# The effect preview's child effects and models come from the main window (vfx_preview_draw_more.py). A child effect a
# runner plays is not in the common effects catalogue: it is exported from the zone its parent came from.

ROOT = 'E:/IW8-Radiant/src/'


def rw(p, pairs):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        if b in s and a not in s:
            continue
        assert s.count(a) == 1, (p, s.count(a), a[:90])
        s = s.replace(a, b)
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


rw('qtapp/main_window.h', [
('''    bool requestVfxExport(const QString& name);''', '''    // zoneHint: the zone to export from when the catalogue does not list the effect (a child effect: its parent's).
    bool requestVfxExport(const QString& name, const QString& zoneHint = QString());
    QString vfxZoneOf(const QString& name) const;  // the catalogue's zone, or the zone a child effect was exported from'''),
('''    QSet<QString> vfxExportFailed_;  // not exportable or failed: not asked again this session''', '''    QSet<QString> vfxExportFailed_;  // not exportable or failed: not asked again this session
    QHash<QString, QString> vfxChildZone_;  // child effects (not in the catalogue) -> the zone they were exported from
    QHash<QString, std::shared_ptr<const VfxSimDef>> vfxPreviewDefs_;  // the effect previews' child effects, read once'''),
])

rw('qtapp/main_window.cpp', [
('''bool MainWindow::requestVfxExport(const QString& name) {
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
    if (zone.isEmpty()) {''', '''// 8.9: the zone that holds a common effect (the always-loaded MP zones the common effects catalogue lists).
static const QHash<QString, QString>& commonVfxZones() {
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
    return zones;
}

QString MainWindow::vfxZoneOf(const QString& name) const {
    QString stem = name;
    stem.replace('\\\\', '/');
    const QString z = commonVfxZones().value(stem);
    return z.isEmpty() ? vfxChildZone_.value(stem) : z;
}

bool MainWindow::requestVfxExport(const QString& name, const QString& zoneHint) {
    if (name.isEmpty() || vfxExporting_.contains(name) || vfxExportFailed_.contains(name)) return vfxExporting_.contains(name);
    QString stem = name;
    stem.replace('\\\\', '/');
    QString zone = vfxZoneOf(stem);
    if (zone.isEmpty() && !zoneHint.isEmpty()) {
        zone = zoneHint;  // a child effect: the zone its parent came from holds it too
        vfxChildZone_.insert(stem, zone);
    }
    if (zone.isEmpty()) {'''),
('''    VfxPreview::setMaterialCache(matCache_);  // the effect previews' sprites (side bar, Effect Info)''', '''    VfxPreview::setMaterialCache(matCache_);  // the effect previews' sprites (side bar, Effect Info)
    // The effect previews' child effects (runners) and models (owner 2026-11-16: "a lot of the vfx dont show anything").
    VfxPreview::setEffectSource([this](const std::string& child, const std::string& parent, bool* pending) -> std::shared_ptr<const VfxSimDef> {
        const QString name = QString::fromStdString(child);
        if (const auto it = vfxPreviewDefs_.constFind(name); it != vfxPreviewDefs_.constEnd()) return *it;
        if (const QString f = findVfxExport(name); !f.isEmpty()) {
            std::string text;
            auto def = std::make_shared<VfxSimDef>();
            if (readFile(f.toStdString(), text) && buildVfxSim(text, *def)) {
                vfxPreviewDefs_.insert(name, def);
                return def;
            }
            return nullptr;
        }
        *pending = requestVfxExport(name, vfxZoneOf(QString::fromStdString(parent)));
        return nullptr;
    });
    VfxPreview::setModelSource([this](const std::string& model, bool* pending) -> std::shared_ptr<const ImportedModel> {
        if (!modelCacheStock_) return nullptr;
        const QString n = QString::fromStdString(model);
        const auto e = modelCacheStock_->lookup(n);
        if (!e) {
            modelCacheStock_->request({n});
            *pending = true;
            return nullptr;
        }
        if (e->failed) return nullptr;
        auto m = modelCacheStock_->loadModel(n);
        return m ? std::make_shared<const ImportedModel>(std::move(*m)) : nullptr;
    });'''),
])
print('ok')
