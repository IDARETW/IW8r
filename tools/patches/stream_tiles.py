import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b and s.count(b) >= 1:
        return
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


CH = 'src/qtapp/camera_view.h'
CC = 'src/qtapp/camera_view.cpp'
MH = 'src/qtapp/main_window.h'
M = 'src/qtapp/main_window.cpp'

# ====================================================================== camera: keyed extra groups
patch(CH, '''    void setReferenceDecalExtras(std::vector<app::ReferenceTexBatch> batches) {
        pendingDecalExtras_ = std::move(batches);
        decalExtrasDirty_ = true;
        update();
    }''', '''    void setReferenceDecalExtras(std::vector<app::ReferenceTexBatch> batches) {
        static uint64_t version = 0;
        ExtraGroup g;
        g.key = 1, g.version = ++version, g.nearOnly = true;
        g.batches = std::make_shared<const std::vector<app::ReferenceTexBatch>>(std::move(batches));
        setReferenceExtraGroups({g});
    }
    // The reference's extras as keyed groups (the terrain layer patch, the prop decals, each streamed world tile; docs/
    // PLAN_REMAINING.md 1.1). A group given again with the same key and version keeps its upload and its batches'
    // textures; world groups hold ordinary world batches (drawn and shadowed as the main reference).
    struct ExtraGroup {
        uint64_t key = 0, version = 0;
        bool world = false, nearOnly = false;
        std::shared_ptr<const std::vector<app::ReferenceTexBatch>> batches;
    };
    void setReferenceExtraGroups(std::vector<ExtraGroup> groups) {
        pendingExtraGroups_ = std::move(groups);
        decalExtrasDirty_ = true;
        update();
    }''')
patch(CH, '''    std::vector<app::ReferenceTexBatch> pendingDecalExtras_;''', '''    std::vector<ExtraGroup> pendingExtraGroups_;
    struct ExtraMeta {
        uint64_t key = 0, version = 0;
        size_t count = 0;
    };
    std::vector<ExtraMeta> extraMeta_;  // the groups uploaded now, in order (their batches follow refTexExtraFrom_)''')
patch(CC, '''        decalExtrasDirty_ = decalExtrasDirty_ || !pendingDecalExtras_.empty();''', '''        extraMeta_.clear();  // the renderer dropped the extras with the old reference
        decalExtrasDirty_ = decalExtrasDirty_ || !pendingExtraGroups_.empty();''')
patch(CC, '''        // Decal volumes on the props: after the world's batches (dropping an earlier set).
        decalExtrasDirty_ = false;
        if (refTexExtraFrom_ != ~size_t(0) && refTexExtraFrom_ <= refTexMaterials_.size()) {
            refTexMaterials_.resize(refTexExtraFrom_), refTexLayer_.resize(refTexExtraFrom_);
            refTexVolume_.resize(refTexExtraFrom_), refTexResolved_.resize(refTexExtraFrom_);
        }
        refTexExtraFrom_ = refTexMaterials_.size();
        std::vector<std::vector<app::GpuTexVertex>> vb;
        for (const auto& b : pendingDecalExtras_) {
            refTexMaterials_.push_back(b.material);
            refTexLayer_.push_back(0);
            RefDecalVolume dv;
            dv.on = true, dv.mask = b.decalMask, dv.a = b.decalA, dv.b = b.decalB, dv.order = b.decalOrder;
            dv.terrainLayer = b.terrainLayer, dv.flags = b.terrainFlags, dv.k = b.terrainK, dv.terrainBase = b.terrainBase;
            for (int c = 0; c < 3; ++c) dv.tint[c] = b.decalTint[c];
            refTexVolume_.push_back(dv);
            refTexResolved_.push_back(false);
            vb.push_back(b.tris);
        }
        renderer_.appendReferenceDecals(vb);
        refTexPending_ = refPollDue_ = true;''', '''        // The extras after the world's batches, group by group: a group with the key and version it had keeps its
        // upload and its batches' state; the others are uploaded (docs/PLAN_REMAINING.md 1.1).
        decalExtrasDirty_ = false;
        const size_t from = refTexExtraFrom_ != ~size_t(0) && refTexExtraFrom_ <= refTexMaterials_.size() ? refTexExtraFrom_ : refTexMaterials_.size();
        struct OldSlice {
            size_t first = 0, count = 0;
            uint64_t version = 0;
        };
        std::map<uint64_t, OldSlice> oldAt;
        {
            size_t at = from;
            for (const ExtraMeta& m : extraMeta_) oldAt[m.key] = {at, m.count, m.version}, at += m.count;
        }
        const std::vector<std::string> oMat(refTexMaterials_.begin() + std::ptrdiff_t(from), refTexMaterials_.end());
        const std::vector<int> oLay(refTexLayer_.begin() + std::ptrdiff_t(std::min(from, refTexLayer_.size())), refTexLayer_.end());
        const std::vector<RefDecalVolume> oVol(refTexVolume_.begin() + std::ptrdiff_t(std::min(from, refTexVolume_.size())), refTexVolume_.end());
        const std::vector<bool> oRes(refTexResolved_.begin() + std::ptrdiff_t(std::min(from, refTexResolved_.size())), refTexResolved_.end());
        refTexMaterials_.resize(from), refTexLayer_.resize(from), refTexVolume_.resize(from), refTexResolved_.resize(from);
        std::vector<app::SceneRenderer::ExtraGroupInput> in;
        std::vector<std::vector<std::vector<app::GpuTexVertex>>> keep;
        keep.reserve(pendingExtraGroups_.size());
        std::vector<ExtraMeta> meta;
        for (const ExtraGroup& g : pendingExtraGroups_) {
            const size_t n = g.batches ? g.batches->size() : 0;
            if (n == 0) continue;
            app::SceneRenderer::ExtraGroupInput gi;
            gi.key = g.key, gi.version = g.version, gi.world = g.world, gi.nearOnly = g.nearOnly;
            const auto o = oldAt.find(g.key);
            if (o != oldAt.end() && o->second.version == g.version && o->second.count == n) {
                const size_t s0 = o->second.first - from;
                for (size_t j = 0; j < n; ++j) {
                    refTexMaterials_.push_back(s0 + j < oMat.size() ? oMat[s0 + j] : std::string());
                    refTexLayer_.push_back(s0 + j < oLay.size() ? oLay[s0 + j] : 0);
                    refTexVolume_.push_back(s0 + j < oVol.size() ? oVol[s0 + j] : RefDecalVolume{});
                    refTexResolved_.push_back(s0 + j < oRes.size() ? bool(oRes[s0 + j]) : false);
                }
            } else {
                keep.emplace_back();
                auto& vb = keep.back();
                for (const auto& b : *g.batches) {
                    refTexMaterials_.push_back(b.material);
                    RefDecalVolume dv;
                    if (g.world) {  // world batches: base / layer / decal volume as in the main reference
                        refTexLayer_.push_back(b.layer);
                        dv.on = b.decalVolume, dv.mask = b.decalMask, dv.a = b.decalA, dv.b = b.decalB, dv.order = b.decalOrder;
                    } else {
                        refTexLayer_.push_back(0);
                        dv.on = true, dv.mask = b.decalMask, dv.a = b.decalA, dv.b = b.decalB, dv.order = b.decalOrder;
                        dv.terrainLayer = b.terrainLayer, dv.flags = b.terrainFlags, dv.k = b.terrainK, dv.terrainBase = b.terrainBase;
                    }
                    for (int c = 0; c < 3; ++c) dv.tint[c] = b.decalTint[c];
                    refTexVolume_.push_back(dv);
                    refTexResolved_.push_back(false);
                    vb.push_back(b.tris);
                    gi.layers.push_back(g.world ? b.layer : 0);
                }
                gi.batches = &vb;
            }
            in.push_back(std::move(gi));
            meta.push_back({g.key, g.version, n});
        }
        renderer_.setReferenceExtras(in);
        refTexExtraFrom_ = from;
        extraMeta_ = std::move(meta);
        refTexPending_ = refPollDue_ = true;''')

# ====================================================================== main window: shared, versioned groups
patch(MH, '''    std::vector<app::ReferenceTexBatch> terrainLayerBatches_, propDecalBatches_;''', '''    // The extras handed to the camera as keyed, versioned groups (CameraView::setReferenceExtraGroups).
    std::shared_ptr<const std::vector<app::ReferenceTexBatch>> terrainLayerShared_, propDecalShared_;
    uint64_t terrainLayerVersion_ = 0, propDecalVersion_ = 0;
    // Streamed world tiles of a big map, built by transient zone index (docs/PLAN_REMAINING.md 1.1).
    struct StreamTile {
        std::shared_ptr<const std::vector<app::ReferenceTexBatch>> batches;
        uint64_t version = 0;
    };
    std::map<int64_t, StreamTile> streamTiles_;
    std::vector<StreamedWorldZone> streamZones_;  // of referenceZone_ (empty: no streamed tiles, or streaming off)
    std::set<int64_t> streamBuilding_;
    std::set<QString> streamRequested_;
    QTimer* streamTimer_ = nullptr;
    Vec3 streamCentre_{1e30, 1e30, 0};
    bool streamDirty_ = false;
    uint64_t streamVersion_ = 0;
    void updateStreamedTiles();
    void evictStreamedTiles(const QStringList& wanted);''')
patch(MH, '''    size_t terrainLayerBatchCount() const { return terrainLayerBatches_.size(); }''', '''    size_t terrainLayerBatchCount() const { return terrainLayerShared_ ? terrainLayerShared_->size() : 0; }
    size_t streamedTilesDrawn() const { return streamTiles_.size(); }
    size_t streamedTilesKnown() const { return streamZones_.size(); }''')
patch(M, '''    propDecalBatches_ = std::move(out);''', '''    propDecalShared_ = std::make_shared<const std::vector<app::ReferenceTexBatch>>(std::move(out));
    ++propDecalVersion_;''')
patch(M, '''    std::vector<app::ReferenceTexBatch> all = terrainLayerBatches_;  // the terrain layers first, under the decals
    all.insert(all.end(), propDecalBatches_.begin(), propDecalBatches_.end());''', '''    // The terrain layers first (under the decals), the prop decals, then every streamed world tile.
    std::vector<CameraView::ExtraGroup> groups;
    if (terrainLayerShared_ && !terrainLayerShared_->empty()) {
        CameraView::ExtraGroup g;
        g.key = 0x54455252, g.version = terrainLayerVersion_, g.batches = terrainLayerShared_;
        groups.push_back(g);
    }
    if (propDecalShared_ && !propDecalShared_->empty()) {
        CameraView::ExtraGroup g;
        g.key = 0x50524f50, g.version = propDecalVersion_, g.nearOnly = true, g.batches = propDecalShared_;
        groups.push_back(g);
    }
    for (const auto& [index, t] : streamTiles_)
        if (t.batches && !t.batches->empty()) {
            CameraView::ExtraGroup g;
            g.key = 0x1000000000ull + uint64_t(index), g.version = t.version, g.world = true, g.batches = t.batches;
            groups.push_back(g);
        }
    std::vector<app::ReferenceTexBatch> all;  // for the diagnosis below only
    const size_t terrainCount = terrainLayerShared_ ? terrainLayerShared_->size() : 0;
    if (qEnvironmentVariableIsSet("IW8R_REF_NEAR"))
        for (const auto& g : groups) all.insert(all.end(), g.batches->begin(), g.batches->end());''')
patch(M, '''                    lf.write(QString("%1 %2 tris %3 terrain %4 base %5 order %6 z %7..%8\\n").arg(k < terrainLayerBatches_.size() ? "T" : "P")''',
      '''                    lf.write(QString("%1 %2 tris %3 terrain %4 base %5 order %6 z %7..%8\\n").arg(k < terrainCount ? "T" : "P")''')
patch(M, '''    camera_->setReferenceDecalExtras(std::move(all));
}''', '''    camera_->setReferenceExtraGroups(std::move(groups));
}''')
patch(M, '''            self->terrainLayerBatches_ = std::move(*batches);
            self->camera_->setTerrainCut(c.x, c.y, self->terrainLayerBatches_.empty() ? 0.0 : 0.74 * terrainLayerRadius());''', '''            self->terrainLayerShared_ = batches;
            ++self->terrainLayerVersion_;
            self->camera_->setTerrainCut(c.x, c.y, batches->empty() ? 0.0 : 0.74 * terrainLayerRadius());''')
patch(M, '''            self->terrainLayerBatches_.clear();
            self->camera_->setTerrainCut(0, 0, 0);
            self->propDecalBatches_.clear();''', '''            self->terrainLayerShared_.reset();
            ++self->terrainLayerVersion_;
            self->camera_->setTerrainCut(0, 0, 0);
            self->propDecalShared_.reset();
            ++self->propDecalVersion_;
            // Streamed world tiles (Warzone): built again around the camera for this map.
            self->streamTiles_.clear();
            self->streamBuilding_.clear();
            self->streamRequested_.clear();
            self->streamZones_ = std::move(*streamZones);
            self->streamCentre_ = Vec3{1e30, 1e30, 0};
            if (!self->streamZones_.empty() && !self->streamTimer_) {
                self->streamTimer_ = new QTimer(self);
                self->streamTimer_->setInterval(1000);
                QObject::connect(self->streamTimer_, &QTimer::timeout, self, [self] { self->updateStreamedTiles(); });
                QObject::connect(self->worldCache_, &WorldCache::streamedReady, self, [self](const QString& z, int, int) {
                    if (z == self->referenceZone_) self->streamDirty_ = true;  // exports done: build them
                });
                self->streamTimer_->start();
            }''')
# the main load: without the streamed tiles when they stream around the camera; the zone list for the loader
patch(M, '''        std::optional<StockWorld> loaded = cache->load(zone, &err);''', '''        const bool autoStream = Settings::instance().value("stock/autoStream", true).toBool();
        std::optional<StockWorld> loaded = cache->load(zone, &err, !autoStream);
        auto streamZones = std::make_shared<std::vector<StreamedWorldZone>>();
        if (loaded && autoStream) {
            std::string serr;
            if (!readStreamedWorldZones(zoneDir.toStdString(), *streamZones, &serr)) streamZones->clear();
        }''')
patch(M, '''        QMetaObject::invokeMethod(qApp, [self, zone, tris, n, err, props, plans, world, texBatches, decals, clutter, terrainLayers] {''',
      '''        if (terrainLayers && !streamZones->empty()) terrainLayers->fineCutout = true;  // the tiles fill the cut-outs
        QMetaObject::invokeMethod(qApp, [self, zone, tris, n, err, props, plans, world, texBatches, decals, clutter, terrainLayers, streamZones] {''')
patch(M, '''    int coarseStride = 0;                                     // the coarse terrain's stride: the patch keeps its cells''',
      '''    int coarseStride = 0;                                     // the coarse terrain's stride: the patch keeps its cells
    bool fineCutout = false;  // the streamed tiles are drawn (their geometry fills the cut-outs): the fine cut-out only''')
patch(M, '''            bo.cutoutStride = qEnvironmentVariableIsSet("IW8R_TERRAIN_FINE_CUTOUT") ? 0 : st.coarseStride;''',
      '''            bo.cutoutStride = qEnvironmentVariableIsSet("IW8R_TERRAIN_FINE_CUTOUT") || st.fineCutout ? 0 : st.coarseStride;''')

# the manual command: with streaming on, it asks the loader to look now
patch('src/qtapp/main_window_stock_edit.cpp', '''int MainWindow::loadStreamedWorldAroundCamera() {
    if (referenceZone_.isEmpty() || !worldCache_) {''', '''int MainWindow::loadStreamedWorldAroundCamera() {
    if (!streamZones_.empty()) {  // the tiles follow the camera already: look now
        streamDirty_ = true;
        updateStreamedTiles();
        ctx_.setStatus(QString("Streamed world: %1 tile(s) drawn around the camera, %2 in the map").arg(streamTiles_.size()).arg(streamZones_.size()));
        return int(streamTiles_.size());
    }
    if (referenceZone_.isEmpty() || !worldCache_) {''')

# the loader itself
patch(M, '''QStringList MainWindow::stockSkyList() const { return stockSkyNames(); }''', '''// Streamed world tiles follow the camera (docs/PLAN_REMAINING.md 1.1): the tiles within stock/streamRadius (20,000)
// are exported through ACTS when missing (WorldCache::requestStreamed, one at a time), built on worker threads from
// the main zone's surface table (buildStockWorldTile) and drawn as world groups of the camera's extras; tiles past 1.3
// times the radius are dropped. Exported tiles stay on disk up to stock/streamCacheGB (3), least recently used first.
void MainWindow::updateStreamedTiles() {
    if (streamZones_.empty() || !worldCache_ || !stockWorld_ || !stockWorld_->surfaceTable || referenceZone_.isEmpty()) return;
    const Vec3 c = ctx_.cam.pos;
    const bool moved = std::hypot(c.x - streamCentre_.x, c.y - streamCentre_.y) > 1500.0;
    if (!moved && !streamDirty_) return;
    streamDirty_ = false;
    streamCentre_ = c;
    const auto dir = worldCache_->lookup(referenceZone_);
    if (!dir) return;
    const double radius = Settings::instance().value("stock/streamRadius", 20000.0).toDouble();
    const size_t limit = size_t(std::max(1, Settings::instance().value("stock/streamLimit", 96).toInt()));
    const auto wanted = streamedZonesNear(streamZones_, c.x, c.y, radius, limit);
    const auto keepZones = streamedZonesNear(streamZones_, c.x, c.y, radius * 1.3, limit * 2);
    std::set<int64_t> keep;
    for (const auto& z : keepZones) keep.insert(z.index);
    bool changed = false;
    for (auto it = streamTiles_.begin(); it != streamTiles_.end();)
        if (!keep.count(it->first)) it = streamTiles_.erase(it), changed = true;
        else ++it;
    QStringList missing, wantedNames;
    QPointer<MainWindow> self(this);
    const QString zone = referenceZone_;
    for (const StreamedWorldZone& z : wanted) {
        const QString name = QString::fromStdString(z.name);
        wantedNames << name;
        const QString tileDir = *dir + "/streamed/" + name;
        if (!QFileInfo(tileDir).isDir()) {
            if (!streamRequested_.count(name)) missing << name, streamRequested_.insert(name);
            continue;
        }
        QFile(tileDir + "/used").open(QIODevice::WriteOnly);  // its last use (the disk budget drops the oldest)
        if (streamTiles_.count(z.index) || streamBuilding_.count(z.index) || streamBuilding_.size() >= 2) continue;
        streamBuilding_.insert(z.index);
        const std::shared_ptr<const StockWorld> world = stockWorld_;
        const int64_t index = z.index;
        std::thread([self, world, tileDir, index, zone] {
            auto batches = std::make_shared<std::vector<app::ReferenceTexBatch>>();
            StockWorld tw;
            std::string err;
            if (buildStockWorldTile(*world->surfaceTable, tileDir.toStdString(), tw, &err)) app::buildReferenceTexBatches(tw, *batches);
            QMetaObject::invokeMethod(qApp, [self, batches, index, zone] {
                if (!self) return;
                self->streamBuilding_.erase(index);
                if (self->referenceZone_ != zone) return;
                self->streamTiles_[index] = StreamTile{batches, ++self->streamVersion_};
                self->streamDirty_ = true;  // the next tick starts the next builds
                self->pushDecalExtras();
            }, Qt::QueuedConnection);
        }).detach();
    }
    if (!missing.isEmpty()) worldCache_->requestStreamed(referenceZone_, missing);
    if (changed) pushDecalExtras();
    evictStreamedTiles(wantedNames);
}

void MainWindow::evictStreamedTiles(const QStringList& wanted) {
    const auto dir = worldCache_ ? worldCache_->lookup(referenceZone_) : std::nullopt;
    if (!dir) return;
    const qint64 budget = qint64(Settings::instance().value("stock/streamCacheGB", 3.0).toDouble() * 1024.0 * 1024.0 * 1024.0);
    struct Tile {
        QString path;
        qint64 bytes = 0;
        QDateTime used;
    };
    std::vector<Tile> tiles;
    qint64 total = 0;
    for (const QFileInfo& t : QDir(*dir + "/streamed").entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (t.fileName() == "w") continue;  // the export scratch
        Tile e;
        e.path = t.absoluteFilePath();
        for (QDirIterator it(e.path, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) e.bytes += QFileInfo(it.next()).size();
        const QFileInfo used(e.path + "/used");
        e.used = used.exists() ? used.lastModified() : t.lastModified();
        total += e.bytes;
        if (!wanted.contains(t.fileName())) tiles.push_back(e);
    }
    if (total <= budget) return;
    std::sort(tiles.begin(), tiles.end(), [](const Tile& a, const Tile& b) { return a.used < b.used; });
    for (const Tile& t : tiles) {
        if (total <= budget) break;
        if (QDir(t.path).removeRecursively()) total -= t.bytes;
    }
}

QStringList MainWindow::stockSkyList() const { return stockSkyNames(); }''')
print('ok')

patch(MH, '''    size_t streamedTilesKnown() const { return streamZones_.size(); }''', '''    size_t streamedTilesKnown() const { return streamZones_.size(); }
    // Nothing being exported or built for the streamed tiles (benchmark: wait before measuring).
    bool streamedTilesSettled() const { return streamBuilding_.empty() && !streamDirty_ && (!worldCache_ || !worldCache_->busy()); }''')
patch('src/qtapp/main.cpp', '''    const auto camB = [&] { setCam(Vec3{centre.x + 40, centre.y - 150, all.mins.z + 88}, 30, -5); };''', '''    const auto camB = [&] { setCam(Vec3{centre.x + 40, centre.y - 150, all.mins.z + 88}, 30, -5); };
    // Streamed world tiles (Warzone) follow the camera: stand at the bench camera and let them come in (at most ten
    // minutes; settled for three seconds) before anything is measured.
    if (fixedCam && w.streamedTilesKnown() > 0) {
        camA();
        QElapsedTimer st, calm;
        st.start();
        calm.start();
        while (st.elapsed() < 600000) {
            cam->renderNow();
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            if (!w.streamedTilesSettled() || cam->texturesPending()) calm.restart();
            else if (calm.elapsed() > 3000) break;
        }
        report << QString("streamed_tiles drawn %1 of %2 after %3 ms").arg(w.streamedTilesDrawn()).arg(w.streamedTilesKnown()).arg(st.elapsed());
    }''')
print('ok2')
