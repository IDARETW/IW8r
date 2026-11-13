import io


def patch(p, a, b):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


p = 'src/qtapp/main_window.cpp'
# --- the terrain helper also gives the ground for the clutter
patch(p, '''void appendStockSuperTerrain(const QString& zone, const QString& zoneDir, const StockDecalTools& tools,
                             std::vector<app::ReferenceTexBatch>& out);
// A copy''', '''void appendStockSuperTerrain(const QString& zone, const QString& zoneDir, const StockDecalTools& tools,
                             std::vector<app::ReferenceTexBatch>& out, StockGroundGrid* ground = nullptr);
// The ground clutter of a Warzone map (research 100: grass, reeds, rocks scattered by the game on its terrain) as model
// placements, about 250,000 of them; empty for maps without (every 6v6 map).
std::shared_ptr<std::vector<StockModelInstance>> scatterStockGroundClutter(const QString& zone, const QString& zoneDir,
                                                                          const StockDecalTools& tools, const StockGroundGrid& ground);
// A copy''')
patch(p, '''void appendStockSuperTerrain(const QString& zone, const QString& zoneDir, const StockDecalTools& tools,
                             std::vector<app::ReferenceTexBatch>& out) {''', '''void appendStockSuperTerrain(const QString& zone, const QString& zoneDir, const StockDecalTools& tools,
                             std::vector<app::ReferenceTexBatch>& out, StockGroundGrid* ground) {''')
patch(p, '''    const Vec3 light = normalize(Vec3{0.35, 0.55, 0.75});
    for (const StockTerrainMesh& tm : meshes.meshes) {
        const ImportedMesh& m = tm.mesh;
        if (m.idx.empty()) continue;''', '''    if (ground) {
        ground->setCellSize(512.0);
        for (const StockTerrainMesh& tm : meshes.meshes)
            for (size_t i = 0; i + 2 < tm.mesh.idx.size(); i += 3)
                ground->addTriangle(tm.mesh.pos[tm.mesh.idx[i]], tm.mesh.pos[tm.mesh.idx[i + 1]], tm.mesh.pos[tm.mesh.idx[i + 2]]);
        ground->finish();
    }
    const Vec3 light = normalize(Vec3{0.35, 0.55, 0.75});
    for (const StockTerrainMesh& tm : meshes.meshes) {
        const ImportedMesh& m = tm.mesh;
        if (m.idx.empty()) continue;''')
patch(p, '''std::shared_ptr<const StockWorld> displaceStockWorld(const StockWorld& world, const QString& zone, const QString& zoneDir,
                                                     const StockDecalTools& tools) {''', '''std::shared_ptr<std::vector<StockModelInstance>> scatterStockGroundClutter(const QString& zone, const QString& zoneDir,
                                                                          const StockDecalTools& tools, const StockGroundGrid& ground) {
    if (qEnvironmentVariableIsSet("IW8R_NO_CLUTTER") || ground.triangleCount() == 0) return nullptr;
    std::vector<StockClutterCollection> collections;
    std::string err;
    if (!readStockClutterCollections(zoneDir.toStdString(), collections, &err) || collections.empty()) return nullptr;
    const QString dir = zoneDir + "/terrain";
    QString json;
    for (QDirIterator it(dir, {"*.json"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
        const QString f = it.next();
        if (f.contains("/assets/stterrain/")) json = f;
    }
    std::string text;
    StockClutterTerrain terrain;
    if (json.isEmpty() || !readFile(json.toStdString(), text) || !parseStockClutterTerrain(text, terrain, &err)) return nullptr;
    text.clear();
    text.shrink_to_fit();
    // Each surface's sample bitmask is a stream key (one ACTS run each, about a second), kept beside the terrain.
    const QString zoneFolder = QFileInfo(tools.replay).absolutePath() + "/zone/";
    const QString keyDir = dir + "/mw19replay/" + zone + "/assets/streamkey";
    QStringList xp;
    for (const QFileInfo& f : QDir(zoneFolder).entryInfoList({"*.xpak"}, QDir::Files))
        if (f.size() >= 64 * 1024) xp << "--xpak" << f.absoluteFilePath();
    std::set<uint32_t> surfaces;
    for (const auto& c : collections) surfaces.insert(c.terrainSurfaceIndex);
    for (uint32_t s : surfaces) {
        if (s >= terrain.surfaces.size()) continue;
        const QString key = zone + "_s" + QString::number(s) + "__clutter_bitmask";
        const auto findKey = [&] {
            for (QDirIterator it(keyDir, {key + ".*"}, QDir::Files); it.hasNext();) return it.next();
            return QString();
        };
        QString f = findKey();
        if (f.isEmpty() && QFileInfo::exists(tools.acts)) {
            QStringList args{"--noUpdater", "fastfile", "-r", "mw19replay", "-g", tools.replay, "--oodle", tools.oodle};
            args << xp << "-a" << "streamkey" << "-n" << key << "-l" << "-o" << dir << zoneFolder + zone + ".ff";
            QProcess p;
            p.start(tools.acts, args);
            p.waitForFinished(120000);
            f = findKey();
        }
        QFile kf(f);
        if (f.isEmpty() || !kf.open(QIODevice::ReadOnly)) continue;
        const QByteArray b = kf.readAll();
        auto& words = terrain.surfaces[s].bitmask;
        words.resize(size_t(b.size()) / 4);
        std::memcpy(words.data(), b.constData(), words.size() * 4);
    }
    // About 250,000 placements over the whole map: every Nth sample of the game's budget.
    size_t budget = 0;
    for (const auto& c : collections) budget += c.maxInstanceCount;
    ClutterScatterOptions opt;
    opt.maxInstances = 260000;
    opt.keepEvery = uint32_t(std::max<size_t>(1, (budget + 249999) / 250000));
    StockClutterScatter scatter;
    scatterStockClutter(collections, terrain, [&](uint32_t s) -> ClutterGroundFn {
        if (s >= terrain.surfaces.size()) return {};
        return meshGround(ground, terrain.surfaces[s].objToWld);
    }, opt, scatter);
    auto out = std::make_shared<std::vector<StockModelInstance>>();
    out->reserve(scatter.instances.size());
    for (const StockClutterInstance& ci : scatter.instances) {
        if (ci.model >= scatter.models.size()) continue;
        StockModelInstance in;
        in.model = scatter.models[ci.model];
        in.origin = ci.origin;
        in.axis = ci.axis;
        in.scale = ci.scale;
        in.index = 0xFFFFFFFFu;  // not a placed model of the map: never hidden, moved or removed by the stock edits
        out->push_back(std::move(in));
    }
    return out->empty() ? nullptr : out;
}

std::shared_ptr<const StockWorld> displaceStockWorld(const StockWorld& world, const QString& zone, const QString& zoneDir,
                                                     const StockDecalTools& tools) {''')
# --- the loader: ground grid, clutter, hand over
patch(p, '''        if (world) appendStockSuperTerrain(zone, zoneDir, tools, *texBatches);  // Warzone ground (research 102)''',
      '''        StockGroundGrid terrainGround;
        if (world) appendStockSuperTerrain(zone, zoneDir, tools, *texBatches, &terrainGround);  // Warzone ground (research 102)
        std::shared_ptr<std::vector<StockModelInstance>> clutter =
            world ? scatterStockGroundClutter(zone, zoneDir, tools, terrainGround) : nullptr;  // research 100''')
patch(p, '''        QMetaObject::invokeMethod(qApp, [self, zone, tris, n, err, props, plans, world, texBatches, decals] {''',
      '''        QMetaObject::invokeMethod(qApp, [self, zone, tris, n, err, props, plans, world, texBatches, decals, clutter] {''')
patch(p, '''            self->stockDecals_ = decals;''', '''            self->stockDecals_ = decals;
            self->refClutter_ = clutter;''')
# --- drawing: the clutter after the map's own props
patch(p, '''    const std::vector<uint32_t> hiddenProps = stockEditHiddenProps(ctx_.doc.map());  // 11.5: hidden in the rebuilt map
    for (const StockModelInstance& in : *refProps_) {''', '''    const std::vector<uint32_t> hiddenProps = stockEditHiddenProps(ctx_.doc.map());  // 11.5: hidden in the rebuilt map
    // The map's placed models, then its ground clutter (scatterStockGroundClutter; never hidden or edited).
    std::vector<StockModelInstance> withClutter;
    const std::vector<StockModelInstance>* drawProps = refProps_.get();
    if (refClutter_ && !refClutter_->empty()) {
        withClutter.reserve(refProps_->size() + refClutter_->size());
        withClutter = *refProps_;
        withClutter.insert(withClutter.end(), refClutter_->begin(), refClutter_->end());
        drawProps = &withClutter;
    }
    for (const StockModelInstance& in : *drawProps) {''')
s = io.open(p, encoding='utf-8', newline='').read()
if '#include "stock_clutter.h"' not in s:
    i = s.index('#include ')
    s = s[:i] + '#include "stock_clutter.h"\n' + s[i:]
io.open(p, 'w', encoding='utf-8', newline='').write(s)
p = 'src/qtapp/main_window.h'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '    std::shared_ptr<std::vector<StockModelInstance>> refProps_;'
assert s.count(a) == 1
s = s.replace(a, a + nl + '    std::shared_ptr<std::vector<StockModelInstance>> refClutter_;  // Warzone ground clutter (drawn only)', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
