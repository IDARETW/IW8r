import io

# Research 132 G7, Warzone: the main zone's light grid atlas and every loaded tile's atlas are stacked into one
# (core packStockLgvAtlases) on a worker thread and sent to the camera; a prop of transient zone k names its box as the
# stack's first box of k + its lighting index. Tiles arriving close together are stacked once. IW8R_NO_PROP_LGV_GPU:
# the centre sample per placement for all.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/qtapp/main_window.h'
patch(H, '''    bool refPropLgvGpu_ = false;  // the main zone's atlas went to the camera: its props sample it per vertex (G7)''',
      '''    bool refPropLgvGpu_ = false;  // a light grid stack went to the camera: props with a base sample it per vertex (G7)
    // The parsed light grid atlases (research 105): the main zone's (transient 0) and each loaded Warzone tile's, stacked
    // into one for the camera by restackPropLgv; refPropLgvBase_ = the stack's first box of each transient zone.
    std::map<int, std::shared_ptr<const StockLgvAtlas>> refPropAtlas_;
    std::map<int, uint32_t> refPropLgvBase_;
    uint64_t refPropLgvGen_ = 0;
    void restackPropLgv();''')
patch(H, '''    void loadStockLightGrid(const QString& zone);''', '''    void loadStockLightGrid(const QString& zone);''')

M = 'src/qtapp/main_window.cpp'
# zone change resets (two places)
patch(M, '''        refPropLgvGpu_ = false;
        camera_->setStockLgvAtlas(nullptr);''', '''        refPropLgvGpu_ = false;
        refPropAtlas_.clear(), refPropLgvBase_.clear(), ++refPropLgvGen_;
        camera_->setStockLgvAtlas(nullptr);''')
patch(M, '''                self->refPropLgvGpu_ = false;
                self->camera_->setStockLgvAtlas(nullptr);''', '''                self->refPropLgvGpu_ = false;
                self->refPropAtlas_.clear(), self->refPropLgvBase_.clear(), ++self->refPropLgvGen_;
                self->camera_->setStockLgvAtlas(nullptr);''')
# records
patch(M, '''    if (k.transient == 0 && refPropLgvGpu_) {
        // Per vertex from the atlas (research 132 G7): -3, the box, then the centre's DC and slots 0 / 2 (the fallback).
        out[0] = -3.0f;
        out[1] = float(k.lightingIndex);''', '''    const auto base = refPropLgvBase_.find(int(k.transient));
    if (refPropLgvGpu_ && base != refPropLgvBase_.end()) {
        // Per vertex from the stack (research 132 G7): -3, the box, then the centre's DC and slots 0 / 2 (the fallback).
        out[0] = -3.0f;
        out[1] = float(base->second + uint32_t(k.lightingIndex));''')
# the file reader returns the parsed atlas
patch(M, '''// 5.3: exact for 1x1x1 boxes, the mean for big ones), DC rgb then the first-order slots 0, 2 and 3. gpu: also the atlas
// for the camera (per-vertex sampling, research 132 G7).
std::shared_ptr<std::vector<float>> sampleLightGridFile(const QString& path, std::string* err,
                                                        std::shared_ptr<app::LgvAtlasUpload>* gpu = nullptr) {''',
      '''// 5.3: exact for 1x1x1 boxes, the mean for big ones), DC rgb then the first-order slots 0, 2 and 3. atlasOut: also the
// parsed atlas (per-vertex sampling through the stack, research 132 G7).
std::shared_ptr<std::vector<float>> sampleLightGridFile(const QString& path, std::string* err,
                                                        std::shared_ptr<const StockLgvAtlas>* atlasOut = nullptr) {''')
patch(M, '''    if (gpu && atlas.xform.size() == atlas.pack.size()) {
        auto up = std::make_shared<app::LgvAtlasUpload>();
        up->width = atlas.width, up->depth = atlas.depth;
        up->tex0 = std::move(atlas.tex0), up->tex1 = std::move(atlas.tex1);
        up->boxes.resize(atlas.pack.size() * 20, 0.0f);
        for (size_t v = 0; v < atlas.pack.size(); ++v) {
            float* b = &up->boxes[v * 20];
            for (int r = 0; r < 3; ++r)
                for (int k = 0; k < 4; ++k) b[4 * r + k] = atlas.xform[v].row[r][k];
            for (int k = 0; k < 3; ++k) b[12 + k] = atlas.pack[v].uvScale[k], b[16 + k] = atlas.pack[v].uvBias[k];
        }
        *gpu = std::move(up);
    }
    return vol;
}
}  // namespace''', '''    static const bool noGpu = std::getenv("IW8R_NO_PROP_LGV_GPU") != nullptr;  // A/B: one sample per placement
    if (atlasOut && !noGpu && atlas.xform.size() == atlas.pack.size()) *atlasOut = std::make_shared<const StockLgvAtlas>(std::move(atlas));
    return vol;
}
}  // namespace

void MainWindow::restackPropLgv() {
    // Every parsed atlas (main zone first), stacked off the UI thread; the newest request wins.
    std::vector<std::pair<int, std::shared_ptr<const StockLgvAtlas>>> parts(refPropAtlas_.begin(), refPropAtlas_.end());
    const uint64_t gen = ++refPropLgvGen_;
    const QString zone = referenceZone_;
    QPointer<MainWindow> self(this);
    std::thread(guardedWorker(__FILE__, __LINE__, [self, parts, gen, zone] {
        std::vector<const StockLgvAtlas*> in;
        for (const auto& p : parts) in.push_back(p.second.get());
        StockLgvStack st;
        std::shared_ptr<app::LgvAtlasUpload> up;
        std::map<int, uint32_t> bases;
        if (packStockLgvAtlases(in, st)) {
            up = std::make_shared<app::LgvAtlasUpload>();
            up->width = st.atlas.width, up->depth = st.atlas.depth;
            up->boxes.resize(st.atlas.pack.size() * 20, 0.0f);
            for (size_t v = 0; v < st.atlas.pack.size(); ++v) {
                float* b = &up->boxes[v * 20];
                for (int r = 0; r < 3; ++r)
                    for (int k = 0; k < 4; ++k) b[4 * r + k] = st.atlas.xform[v].row[r][k];
                for (int k = 0; k < 3; ++k) b[12 + k] = st.atlas.pack[v].uvScale[k], b[16 + k] = st.atlas.pack[v].uvBias[k];
            }
            up->tex0 = std::move(st.atlas.tex0), up->tex1 = std::move(st.atlas.tex1);
            for (size_t i = 0; i < parts.size(); ++i)
                if (st.firstBox[i] != 0xFFFFFFFFu) bases[parts[i].first] = st.firstBox[i];
        }
        QMetaObject::invokeMethod(qApp, [self, up, bases, gen, zone] {
            if (!self || self->refPropLgvGen_ != gen || self->referenceZone_ != zone) return;
            self->refPropLgvBase_ = bases;
            self->refPropLgvGpu_ = up != nullptr;
            self->camera_->setStockLgvAtlas(up);
            self->relightReferenceProps();
        }, Qt::QueuedConnection);
    })).detach();
}''')
# tiles: keep their atlases, stack once for a burst
patch(M, '''        std::vector<std::pair<int, std::shared_ptr<std::vector<float>>>> got;
        for (const auto& [k, path] : todo) {
            std::string err;
            if (auto vol = sampleLightGridFile(path, &err)) got.push_back({k, vol});
        }
        QMetaObject::invokeMethod(qApp, [self, zone, got] {
            if (!self || self->referenceZone_ != zone || got.empty()) return;
            for (const auto& [k, vol] : got) self->refPropTileLight_[k] = vol;''', '''        std::vector<std::pair<int, std::shared_ptr<std::vector<float>>>> got;
        std::vector<std::pair<int, std::shared_ptr<const StockLgvAtlas>>> atlases;
        for (const auto& [k, path] : todo) {
            std::string err;
            std::shared_ptr<const StockLgvAtlas> atlas;
            if (auto vol = sampleLightGridFile(path, &err, &atlas)) got.push_back({k, vol});
            if (atlas) atlases.push_back({k, atlas});
        }
        QMetaObject::invokeMethod(qApp, [self, zone, got, atlases] {
            if (!self || self->referenceZone_ != zone || got.empty()) return;
            for (const auto& [k, vol] : got) self->refPropTileLight_[k] = vol;
            for (const auto& [k, a] : atlases) self->refPropAtlas_[k] = a;''')
patch(M, '''                QTimer::singleShot(1500, self, [self] {
                    if (!self) return;
                    self->refPropRelightQueued_ = false;
                    self->relightReferenceProps();
                });''', '''                QTimer::singleShot(1500, self, [self] {
                    if (!self) return;
                    self->refPropRelightQueued_ = false;
                    if (self->refPropAtlas_.empty()) self->relightReferenceProps();
                    else self->restackPropLgv();  // relights when the stack is in
                });''')
# main zone
patch(M, '''        std::string err;
        std::shared_ptr<app::LgvAtlasUpload> gpu;
        static const bool noGpu = std::getenv("IW8R_NO_PROP_LGV_GPU") != nullptr;  // A/B: one sample per placement
        const std::shared_ptr<std::vector<float>> vol = sampleLightGridFile(path, &err, noGpu ? nullptr : &gpu);
        const bool ok = vol != nullptr;
        QMetaObject::invokeMethod(qApp, [self, zone, vol, gpu, ok, err] {
            if (!self || self->referenceZone_ != zone) return;
            if (!ok) {
                self->ctx_.setStatus("Stock prop light grid of " + zone + " not read: " + QString::fromStdString(err));
                return;
            }
            self->refPropLgvGpu_ = gpu != nullptr;
            self->camera_->setStockLgvAtlas(gpu);
            self->refPropVolLight_ = vol;
            self->refPropVolLightZone_ = zone;
            self->relightReferenceProps();''', '''        std::string err;
        std::shared_ptr<const StockLgvAtlas> atlas;
        const std::shared_ptr<std::vector<float>> vol = sampleLightGridFile(path, &err, &atlas);
        const bool ok = vol != nullptr;
        QMetaObject::invokeMethod(qApp, [self, zone, vol, atlas, ok, err] {
            if (!self || self->referenceZone_ != zone) return;
            if (!ok) {
                self->ctx_.setStatus("Stock prop light grid of " + zone + " not read: " + QString::fromStdString(err));
                return;
            }
            self->refPropVolLight_ = vol;
            self->refPropVolLightZone_ = zone;
            if (atlas) {
                self->refPropAtlas_[0] = atlas;
                self->restackPropLgv();  // relights when the stack is in
            } else {
                self->relightReferenceProps();
            }''')
print('ok')
