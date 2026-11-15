import io

# Research 132 G7, wiring: the main zone's light grid atlas goes to the camera (once per zone), and the main zone's
# probe-lit props get records that name their box (-3, box, the centre's DC and slots 0 / 2 as the fallback).
# IW8R_NO_PROP_LGV_GPU: the per-placement centre sample as before (A/B).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/viewport/render.h'
patch(H, '''struct InstancedTexMesh {''', '''// The stock props' light grid atlas for SceneRenderer::setStockLgvAtlas (core StockLgvAtlas; research 105): the two
// textures as they are and five float4 per box (rows 0..2, uvScale, uvBias).
struct LgvAtlasUpload {
    int width = 0, depth = 0;
    std::vector<uint32_t> tex0, tex1;
    std::vector<float> boxes;  // 20 floats per box
};

struct InstancedTexMesh {''')

CH = 'src/qtapp/camera_view.h'
patch(CH, '''    // New per-placement light for every textured prop mesh (same order and counts as setReferenceTexInstances gave).''',
      '''    // The stock props' light grid atlas (research 105; null clears it): uploaded on the next frame. Props whose light
    // record starts with -3 sample their box per vertex.
    void setStockLgvAtlas(std::shared_ptr<const app::LgvAtlasUpload> atlas) {
        pendingLgv_ = atlas ? std::move(atlas) : std::make_shared<const app::LgvAtlasUpload>();
        update();
    }
    bool hasStockLgvAtlas() const { return renderer_.hasStockLgvAtlas(); }
    // New per-placement light for every textured prop mesh (same order and counts as setReferenceTexInstances gave).''')
patch(CH, '''    std::vector<std::vector<float>> pendingTexLight_;  // setReferenceTexInstanceLight''',
      '''    std::vector<std::vector<float>> pendingTexLight_;  // setReferenceTexInstanceLight
    std::shared_ptr<const app::LgvAtlasUpload> pendingLgv_;  // setStockLgvAtlas''')
CC = 'src/qtapp/camera_view.cpp'
patch(CC, '''    if (texLightDirty_) {
        IW8R_PROF("r.up_texlight");''', '''    if (pendingLgv_) {
        const app::LgvAtlasUpload& a = *pendingLgv_;
        const bool ok = renderer_.setStockLgvAtlas(a.width, a.depth, a.tex0.empty() ? nullptr : a.tex0.data(),
                                                   a.tex1.empty() ? nullptr : a.tex1.data(), a.boxes.empty() ? nullptr : a.boxes.data(),
                                                   a.boxes.size() / 20);
        if (!ok && a.width > 0) emit ctx_->statusMessage("Stock prop light grid: the atlas could not be uploaded (props use their box centre)");
        pendingLgv_.reset();
    }
    if (texLightDirty_) {
        IW8R_PROF("r.up_texlight");''')

MH = 'src/qtapp/main_window.h'
patch(MH, '''    std::shared_ptr<const std::vector<float>> refPropVolLight_;''', '''    std::shared_ptr<const std::vector<float>> refPropVolLight_;
    bool refPropLgvGpu_ = false;  // the main zone's atlas went to the camera: its props sample it per vertex (G7)''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''        refPropVolLight_.reset();
        refPropVolLightZone_.clear();
        refPropTileLight_.clear();''', '''        refPropVolLight_.reset();
        refPropLgvGpu_ = false;
        camera_->setStockLgvAtlas(nullptr);
        refPropVolLightZone_.clear();
        refPropTileLight_.clear();''')
patch(M, '''            if (self->refPropVolLightZone_ != zone) {
                self->refPropVolLight_.reset();''', '''            if (self->refPropVolLightZone_ != zone) {
                self->refPropVolLight_.reset();
                self->refPropLgvGpu_ = false;
                self->camera_->setStockLgvAtlas(nullptr);''')
patch(M, '''// A light grid cache file (lightgrid.iw8rlg) to 12 floats per volume: each box at its centre (research 105 section
// 5.3: exact for 1x1x1 boxes, the mean for big ones), DC rgb then the first-order slots 0, 2 and 3.
std::shared_ptr<std::vector<float>> sampleLightGridFile(const QString& path, std::string* err) {''',
      '''// A light grid cache file (lightgrid.iw8rlg) to 12 floats per volume: each box at its centre (research 105 section
// 5.3: exact for 1x1x1 boxes, the mean for big ones), DC rgb then the first-order slots 0, 2 and 3. gpu: also the atlas
// for the camera (per-vertex sampling, research 132 G7).
std::shared_ptr<std::vector<float>> sampleLightGridFile(const QString& path, std::string* err,
                                                        std::shared_ptr<app::LgvAtlasUpload>* gpu = nullptr) {''')
patch(M, '''            o[9 + c] = sh.sh[3][c];
        }
    }
    return vol;
}''', '''            o[9 + c] = sh.sh[3][c];
        }
    }
    if (gpu && atlas.xform.size() == atlas.pack.size()) {
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
}''')
patch(M, '''        std::string err;
        const std::shared_ptr<std::vector<float>> vol = sampleLightGridFile(path, &err);
        const bool ok = vol != nullptr;
        QMetaObject::invokeMethod(qApp, [self, zone, vol, ok, err] {
            if (!self || self->referenceZone_ != zone) return;
            if (!ok) {
                self->ctx_.setStatus("Stock prop light grid of " + zone + " not read: " + QString::fromStdString(err));
                return;
            }
            self->refPropVolLight_ = vol;''', '''        std::string err;
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
            self->refPropVolLight_ = vol;''')
patch(M, '''    if (!grid || size_t(k.lightingIndex) >= grid->size() / 12) return false;
    std::memcpy(out, &(*grid)[size_t(k.lightingIndex) * 12], 12 * sizeof(float));
    return true;''', '''    if (!grid || size_t(k.lightingIndex) >= grid->size() / 12) return false;
    const float* c = &(*grid)[size_t(k.lightingIndex) * 12];
    if (k.transient == 0 && refPropLgvGpu_) {
        // Per vertex from the atlas (research 132 G7): -3, the box, then the centre's DC and slots 0 / 2 (the fallback).
        out[0] = -3.0f;
        out[1] = float(k.lightingIndex);
        std::memcpy(out + 2, c, 9 * sizeof(float));
        out[11] = 0.0f;
        return true;
    }
    std::memcpy(out, c, 12 * sizeof(float));
    return true;''')
print('ok')
