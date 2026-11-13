import io


def patch(p, a, b):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


# ============================================================ scene: a terrain-layer batch kind
patch('src/viewport/scene.h', '''    float decalTint[3] = {1, 1, 1};
    int decalOrder = 0;''', '''    float decalTint[3] = {1, 1, 1};
    int decalOrder = 0;
    // A super-terrain layer near the camera (research 103), drawn with the decals: material = "\\x05<colour image>",
    // decalMask = its alpha + reveal image ("" none), the weight in the tangent length (1 + w), the layer's UV as uv.
    bool terrainLayer = false;
    uint32_t terrainFlags = 0;  // the layer shader's flags: 16 reveal ramp, 8 alpha (research 97 / 103)
    float terrainK = 0;'''),

# ============================================================ camera: resolve them as layers
patch('src/qtapp/camera_view.h', '''    struct RefDecalVolume {
        bool on = false;
        std::string mask;''', '''    struct RefDecalVolume {
        bool on = false;
        bool terrainLayer = false;
        uint32_t flags = 0;
        float k = 0;
        std::string mask;''')
patch('src/qtapp/camera_view.cpp', '''            RefDecalVolume dv;
            dv.on = true, dv.mask = b.decalMask, dv.a = b.decalA, dv.b = b.decalB, dv.order = b.decalOrder;''',
      '''            RefDecalVolume dv;
            dv.on = true, dv.mask = b.decalMask, dv.a = b.decalA, dv.b = b.decalB, dv.order = b.decalOrder;
            dv.terrainLayer = b.terrainLayer, dv.flags = b.terrainFlags, dv.k = b.terrainK;''')
patch('src/qtapp/camera_view.cpp', '''                    refTexResolved_[k] = true;
                    if (cid < 0 || cid == checkerTex_) continue;  // no colour image: left out
                    const float p[16] = {2.0f, dv.a, dv.b, 0.0f, dv.tint[0], dv.tint[1], dv.tint[2], 0.0f,
                                         1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
                    renderer_.setReferenceTexture(k, cid);
                    renderer_.setReferenceDecal(k, 1000 + dv.order, mid == checkerTex_ ? -1 : mid, p);
                    continue;''', '''                    refTexResolved_[k] = true;
                    if (cid < 0 || cid == checkerTex_) continue;  // no colour image: left out
                    if (dv.terrainLayer) {
                        // A terrain layer: the layer mode (weight in the tangent length, reveal ramp / alpha by its flags),
                        // under the world's own decals (sort key 1).
                        const float p[16] = {1.0f, float(dv.flags), dv.k, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f,
                                             1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
                        renderer_.setReferenceTexture(k, cid);
                        renderer_.setReferenceDecal(k, 1, mid == checkerTex_ ? -1 : mid, p);
                        continue;
                    }
                    const float p[16] = {2.0f, dv.a, dv.b, 0.0f, dv.tint[0], dv.tint[1], dv.tint[2], 0.0f,
                                         1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
                    renderer_.setReferenceTexture(k, cid);
                    renderer_.setReferenceDecal(k, 1000 + dv.order, mid == checkerTex_ ? -1 : mid, p);
                    continue;''')

# ============================================================ main window
p = 'src/qtapp/main_window.cpp'
patch(p, '''void appendStockSuperTerrain(const QString& zone, const QString& zoneDir, const StockDecalTools& tools,
                             std::vector<app::ReferenceTexBatch>& out, StockGroundGrid* ground = nullptr);''',
      '''// What the near-camera terrain layers need (research 103), kept from the load (MainWindow::updateTerrainLayers).
struct StockTerrainLayerState {
    QString zone, imageDir;
    StockDecalTools tools;
    StockTerrain terrain;
    StockTerrainLayerTable table;
    std::vector<StockTerrainHeightGrid> heights;
    std::vector<StockTerrainCutout> cutouts;
    std::map<int, std::vector<StockTerrainLayerMask>> masks;  // per surface, by layer (loaded on demand)
    std::map<int, StockTerrainMesh> meshes;                   // per surface, stride 2 (built on demand)
};
void appendStockSuperTerrain(const QString& zone, const QString& zoneDir, const StockDecalTools& tools,
                             std::vector<app::ReferenceTexBatch>& out, StockGroundGrid* ground = nullptr,
                             std::shared_ptr<StockTerrainLayerState>* layersOut = nullptr);
// The terrain layers of the surfaces within `radius` of (x, y), as blended batches (base layer, then the layers), their
// weights faded to 0 at the radius so the patch melts into the baked colour beyond.
std::vector<app::ReferenceTexBatch> buildTerrainLayerBatches(StockTerrainLayerState& st, double x, double y, double radius);''')
patch(p, '''void appendStockSuperTerrain(const QString& zone, const QString& zoneDir, const StockDecalTools& tools,
                             std::vector<app::ReferenceTexBatch>& out, StockGroundGrid* ground) {''',
      '''void appendStockSuperTerrain(const QString& zone, const QString& zoneDir, const StockDecalTools& tools,
                             std::vector<app::ReferenceTexBatch>& out, StockGroundGrid* ground,
                             std::shared_ptr<StockTerrainLayerState>* layersOut) {''')
patch(p, '''    if (ground) {
        ground->setCellSize(512.0);''', '''    if (layersOut) {
        auto st = std::make_shared<StockTerrainLayerState>();
        st->zone = zone, st->imageDir = imageDir, st->tools = tools;
        st->terrain = terrain, st->heights = heights, st->cutouts = cutouts;
        if (readStockTerrainLayers(json.toStdString(), st->table) && !st->table.surfaces.empty()) *layersOut = st;
    }
    if (ground) {
        ground->setCellSize(512.0);''')
patch(p, '''std::shared_ptr<std::vector<StockModelInstance>> scatterStockGroundClutter(const QString& zone, const QString& zoneDir,
                                                                          const StockDecalTools& tools, const StockGroundGrid& ground) {''',
      '''std::vector<app::ReferenceTexBatch> buildTerrainLayerBatches(StockTerrainLayerState& st, double x, double y, double radius) {
    std::vector<app::ReferenceTexBatch> out;
    const QString zoneFolder = QFileInfo(st.tools.replay).absolutePath() + "/zone/";
    for (const int s : stockTerrainSurfacesNear(st.terrain, x, y, radius)) {
        if (s < 0 || size_t(s) >= st.terrain.surfaces.size() || size_t(s) >= st.heights.size() || st.heights[size_t(s)].data.empty())
            continue;
        const StockTerrainSurfaceLayers* sl = nullptr;
        for (const auto& t : st.table.surfaces)
            if (t.surface == s) sl = &t;
        if (!sl || sl->layers.empty()) continue;
        // The layer masks of the surface (streamed images: one ACTS run per surface, kept beside the terrain).
        if (!st.masks.count(s)) {
            const std::vector<std::string> names = stockTerrainLayerMaskImageNames(st.table, s);
            QStringList missing;
            for (const std::string& n : names)
                if (findStockTerrainImage(st.imageDir.toStdString(), n).empty()) missing << QString::fromStdString(n);
            if (!missing.isEmpty() && QFileInfo::exists(st.tools.acts)) {
                QStringList args{"--noUpdater", "fastfile", "-r", "mw19replay", "-g", st.tools.replay, "--oodle", st.tools.oodle};
                for (const QFileInfo& f : QDir(zoneFolder).entryInfoList({"*.xpak"}, QDir::Files))
                    if (f.size() >= 64 * 1024) args << "--xpak" << f.absoluteFilePath();
                args << "-a" << "image" << "-n" << missing.join(',') << "-l" << "-o"
                     << QFileInfo(st.imageDir).absolutePath() + "/../../.." << zoneFolder + st.zone + ".ff";
                QProcess p;
                p.start(st.tools.acts, args);
                p.waitForFinished(300000);
            }
            std::vector<StockTerrainLayerMask> masks(sl->layers.size());
            for (size_t L = 0; L < sl->layers.size(); ++L) {
                const auto& li = sl->layers[L];
                if (li.maskImage.empty()) continue;
                const std::string f = findStockTerrainImage(st.imageDir.toStdString(), li.maskImage);
                if (!f.empty()) readStockTerrainLayerMaskDds(f, masks[L]);
            }
            st.masks[s] = std::move(masks);
        }
        if (!st.meshes.count(s)) {
            StockTerrainBuildOptions bo;
            bo.stride = 2;
            StockTerrainMesh m;
            const StockTerrainCutout* cut = size_t(s) < st.cutouts.size() && !st.cutouts[size_t(s)].words.empty() ? &st.cutouts[size_t(s)] : nullptr;
            if (!buildStockTerrainMesh(st.terrain, s, st.heights[size_t(s)], cut, bo, m)) continue;
            st.meshes[s] = std::move(m);
        }
        StockTerrainLayerPatchOptions po;
        po.centerX = x, po.centerY = y, po.radius = radius;
        StockTerrainLayerPatch patch;
        if (!buildStockTerrainLayerPatch(st.terrain, st.table, st.meshes[s], st.masks[s], po, patch) || patch.base.idx.empty()) continue;
        const auto fade = [&](const Vec3& p) {
            const double d = std::hypot(p.x - x, p.y - y), t = std::clamp((radius - d) / (0.25 * radius), 0.0, 1.0);
            return t * t * (3.0 - 2.0 * t);
        };
        const auto emit = [&](const ImportedMesh& m, const std::function<double(size_t)>& weight, const std::string& color,
                              const std::string& reveal, uint32_t flags, double k) {
            if (m.idx.empty() || color.empty()) return;
            app::ReferenceTexBatch rb;
            rb.material = "\\x05" + color;
            rb.decalVolume = true;
            rb.terrainLayer = true;
            rb.decalMask = reveal;
            rb.terrainFlags = flags;
            rb.terrainK = float(k);
            rb.tris.reserve(m.idx.size());
            for (uint32_t i : m.idx) {
                const Vec3& p = m.pos[i];
                const Vec3 n = i < m.normal.size() ? m.normal[i] : Vec3{0, 0, 1};
                app::GpuTexVertex v{float(p.x), float(p.y), float(p.z), 0, 0, 0xFFFFFFFFu};
                if (i < m.uv.size()) v.u = float(m.uv[i][0]), v.v = float(1.0 - m.uv[i][1]);
                v.nx = float(n.x), v.ny = float(n.y), v.nz = float(n.z);
                const double w = std::clamp(weight(i) * fade(p), 0.0, 1.0);
                v.tx = float(1.0 + w), v.ty = 0, v.tz = 0;  // tangent +x, length 1 + weight
                rb.tris.push_back(v);
            }
            out.push_back(std::move(rb));
        };
        const auto& base = sl->layers[size_t(std::clamp(patch.baseLayer, 0, int(sl->layers.size()) - 1))];
        emit(patch.base, [](size_t) { return 1.0; }, base.colorImage, std::string(), 0, 0);
        for (const StockTerrainLayerSet& ls : patch.layers) {
            const uint32_t flags = (ls.revealRamp ? 16u : 0u) | (ls.alphaReveal ? 8u : 0u);
            emit(ls.mesh, [&](size_t i) { return i < ls.weight.size() ? double(ls.weight[i]) : 0.0; }, ls.colorImage,
                 ls.alphaReveal ? ls.revealImage : std::string(), flags, ls.revealK);
        }
    }
    return out;
}

std::shared_ptr<std::vector<StockModelInstance>> scatterStockGroundClutter(const QString& zone, const QString& zoneDir,
                                                                          const StockDecalTools& tools, const StockGroundGrid& ground) {''')
# loader: keep the state
patch(p, '''        StockGroundGrid terrainGround;
        if (world) appendStockSuperTerrain(zone, zoneDir, tools, *texBatches, &terrainGround);  // Warzone ground (research 102)''',
      '''        StockGroundGrid terrainGround;
        std::shared_ptr<StockTerrainLayerState> terrainLayers;
        if (world) appendStockSuperTerrain(zone, zoneDir, tools, *texBatches, &terrainGround, &terrainLayers);  // research 102''')
patch(p, '''        QMetaObject::invokeMethod(qApp, [self, zone, tris, n, err, props, plans, world, texBatches, decals, clutter] {''',
      '''        QMetaObject::invokeMethod(qApp, [self, zone, tris, n, err, props, plans, world, texBatches, decals, clutter, terrainLayers] {''')
patch(p, '''            self->refClutter_ = clutter;''', '''            self->refClutter_ = clutter;
            self->terrainLayers_ = terrainLayers;
            self->terrainLayerCentre_ = Vec3{1e30, 1e30, 0};
            self->terrainLayerBatches_.clear();
            self->propDecalBatches_.clear();''')
# prop decals: keep them, push both
patch(p, '''    if (!out.empty()) camera_->setReferenceDecalExtras(std::move(out));
}''', '''    propDecalBatches_ = std::move(out);
    pushDecalExtras();
}

void MainWindow::pushDecalExtras() {
    std::vector<app::ReferenceTexBatch> all = terrainLayerBatches_;  // the terrain layers first, under the decals
    all.insert(all.end(), propDecalBatches_.begin(), propDecalBatches_.end());
    camera_->setReferenceDecalExtras(std::move(all));
}

void MainWindow::updateTerrainLayers() {
    if (!terrainLayers_ || terrainLayerBusy_ || referenceZone_.isEmpty() || terrainLayers_->zone != referenceZone_) return;
    const Vec3 c = ctx_.cam.pos;
    if (std::hypot(c.x - terrainLayerCentre_.x, c.y - terrainLayerCentre_.y) < 2000.0) return;
    terrainLayerBusy_ = true;
    terrainLayerCentre_ = c;
    QPointer<MainWindow> self(this);
    auto st = terrainLayers_;
    const QString zone = referenceZone_;
    std::thread([self, st, c, zone] {
        auto batches = std::make_shared<std::vector<app::ReferenceTexBatch>>(buildTerrainLayerBatches(*st, c.x, c.y, 6000.0));
        QMetaObject::invokeMethod(qApp, [self, batches, zone] {
            if (!self) return;
            self->terrainLayerBusy_ = false;
            if (self->referenceZone_ != zone) return;
            self->terrainLayerBatches_ = std::move(*batches);
            self->pushDecalExtras();
        }, Qt::QueuedConnection);
    }).detach();
}''')
s = io.open(p, encoding='utf-8', newline='').read()
if '#include "stock_terrain_layers.h"' not in s:
    i = s.index('#include ')
    s = s[:i] + '#include "stock_terrain_layers.h"\n' + s[i:]
io.open(p, 'w', encoding='utf-8', newline='').write(s)

p = 'src/qtapp/main_window.h'
patch(p, '''    std::shared_ptr<std::vector<StockModelInstance>> refClutter_;  // Warzone ground clutter (drawn only)''',
      '''    std::shared_ptr<std::vector<StockModelInstance>> refClutter_;  // Warzone ground clutter (drawn only)
    // Near-camera super-terrain layers (research 103): rebuilt in the background when the camera has moved 2,000 units.
    std::shared_ptr<struct StockTerrainLayerState> terrainLayers_;
    Vec3 terrainLayerCentre_{1e30, 1e30, 0};
    bool terrainLayerBusy_ = false;
    std::vector<app::ReferenceTexBatch> terrainLayerBatches_, propDecalBatches_;
    void pushDecalExtras();
    void updateTerrainLayers();''')
print('ok')

p = 'src/qtapp/main_window.cpp'
patch(p, '''            self->terrainLayers_ = terrainLayers;''', '''            self->terrainLayers_ = terrainLayers;
            if (terrainLayers && !self->terrainLayerTimer_) {
                self->terrainLayerTimer_ = new QTimer(self);
                self->terrainLayerTimer_->setInterval(700);
                QObject::connect(self->terrainLayerTimer_, &QTimer::timeout, self, [self] { self->updateTerrainLayers(); });
                self->terrainLayerTimer_->start();
            }''')
patch('src/qtapp/main_window.h', '''    void updateTerrainLayers();''', '''    void updateTerrainLayers();
    class QTimer* terrainLayerTimer_ = nullptr;''')
print('ok2')
