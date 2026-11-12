import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    # scene.cpp: only the reference batch builder (the same loop header also opens another builder above it).
    pre = ''
    if p.endswith('scene.cpp'):
        at = s.index('size_t buildReferenceTexBatches(const StockWorld& world')
        pre, s = s[:at], s[at:]
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        if a != b and s.count(a) == 0 and s.count(b) >= 1:
            continue  # applied by an earlier run
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(pre + s)


# ---- viewport: layers from a stock lightmap
patch('src/viewport/game_lightmap.h', [
 ('''#include "geometry.h"
#include "lightmap_bake.h"''', '''#include "geometry.h"
#include "lightmap_bake.h"
#include "stock_lightmap.h"'''),
 ('''// Bakes (any thread). Never null; `error` is set when nothing could be baked.''', '''// A stock map's own lightmap (core stock_lightmap, research 94) as the renderer's three layers
// (SceneRenderer::setStockLightmap): a = (f C, 1), b = ((1 - f) C / max(D.z, .001), 0), d = (D, 0), D in tangent space.
struct StockLightmapLayers {
    int width = 0, height = 0;
    std::vector<float> a, b, d;
};
std::shared_ptr<StockLightmapLayers> stockLightmapLayers(const StockLightmap& lm);

// Bakes (any thread). Never null; `error` is set when nothing could be baked.'''),
])
patch('src/viewport/game_lightmap.cpp', [
 ('''GameBakeInput gatherGameBake(''', '''std::shared_ptr<StockLightmapLayers> stockLightmapLayers(const StockLightmap& lm) {
    auto out = std::make_shared<StockLightmapLayers>();
    const size_t n = size_t(std::max(lm.width, 0)) * size_t(std::max(lm.height, 0));
    if (!n || lm.irradiance.size() < n * 3 || lm.ambientFraction.size() < n || lm.direction.size() < n * 3) return out;
    out->width = lm.width, out->height = lm.height;
    out->a.resize(n * 4), out->b.resize(n * 4), out->d.resize(n * 4);
    for (size_t i = 0; i < n; ++i) {
        const float f = std::clamp(lm.ambientFraction[i], 0.0f, 1.0f);
        const float dz = std::max(lm.direction[i * 3 + 2], 0.001f);
        for (int c = 0; c < 3; ++c) {
            const float C = lm.irradiance[i * 3 + c];
            out->a[i * 4 + c] = f * C;
            out->b[i * 4 + c] = (1.0f - f) * C / dz;
            out->d[i * 4 + c] = lm.direction[i * 3 + c];
        }
        out->a[i * 4 + 3] = 1.0f;
        out->b[i * 4 + 3] = 0.0f;
        out->d[i * 4 + 3] = 0.0f;
    }
    return out;
}

GameBakeInput gatherGameBake('''),
])

# ---- scene.cpp: reference vertices carry the stock lightmap UVs
patch('src/viewport/scene.cpp', [
 ('''    for (const auto& m : world.meshes) {
        std::string lm = m.material;
        for (char& c : lm) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (skipSky && lm.find("sky") != std::string::npos) continue;''', '''    for (size_t mi = 0; mi < world.meshes.size(); ++mi) {
        const auto& m = world.meshes[mi];
        // Its stock lightmap UVs (research 94; (-1, -1) where the surface has none).
        const std::vector<std::array<float, 2>>* lmuv =
            mi < world.lightmapUv.size() && world.lightmapUv[mi].size() == m.pos.size() ? &world.lightmapUv[mi] : nullptr;
        std::string lm = m.material;
        for (char& c : lm) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (skipSky && lm.find("sky") != std::string::npos) continue;'''),
 ('''                GpuTexVertex v{float(m.pos[k].x), float(m.pos[k].y), float(m.pos[k].z), m.uv[k][0], 1.0f - m.uv[k][1], rgba};
                v.nx = float(n.x), v.ny = float(n.y), v.nz = float(n.z);
                dst.push_back(v);''', '''                GpuTexVertex v{float(m.pos[k].x), float(m.pos[k].y), float(m.pos[k].z), m.uv[k][0], 1.0f - m.uv[k][1], rgba};
                v.nx = float(n.x), v.ny = float(n.y), v.nz = float(n.z);
                if (lmuv && (*lmuv)[k][0] >= 0) v.lu = (*lmuv)[k][0], v.lv = (*lmuv)[k][1];
                dst.push_back(v);'''),
])

# ---- camera view: pending stock lightmap
patch('src/qtapp/camera_view.h', [
 ('''    void setGameStockMap(const std::string& zone) { gameStockMap_ = zone; update(); }''',
  '''    void setGameStockMap(const std::string& zone) { gameStockMap_ = zone; update(); }
    // The stock reference's own lightmap (null clears it), sent to the renderer on the next frame.
    void setStockLightmap(std::shared_ptr<const app::StockLightmapLayers> layers) {
        pendingStockLm_ = std::move(layers);
        stockLmDirty_ = true;
        update();
    }
    bool hasStockLightmap() const { return renderer_.hasStockLightmap(); }'''),
 ('''    std::string gameStockMap_;''', '''    std::string gameStockMap_;
    std::shared_ptr<const app::StockLightmapLayers> pendingStockLm_;
    bool stockLmDirty_ = false;'''),
])
patch('src/qtapp/camera_view.cpp', [
 ('''        renderer_.setReferenceTextured(batches);''', '''        renderer_.setReferenceTextured(batches);'''),
 ('''    if (instDirty_) {
        renderer_.setReferenceInstances(pendingInst_);''', '''    if (stockLmDirty_) {
        const auto& l = pendingStockLm_;
        if (l && l->width > 0) renderer_.setStockLightmap(l->width, l->height, l->a.data(), l->b.data(), l->d.data());
        else renderer_.setStockLightmap(0, 0, nullptr, nullptr, nullptr);
        pendingStockLm_.reset();
        stockLmDirty_ = false;
    }
    if (instDirty_) {
        renderer_.setReferenceInstances(pendingInst_);'''),
])

# ---- main window: request and load the lightmap with the reference
patch('src/qtapp/main_window.h', [
 ('''    void loadStockReference(const QString& zone);''', '''    void loadStockReference(const QString& zone);
    // The reference zone's own lightmap (world cache lightmap.iw8rlm) into the camera, read off the UI thread.
    void loadStockLightmap(const QString& zone);'''),
])
patch('src/qtapp/main_window.cpp', [
 ('''        connect(worldCache_, &WorldCache::ready, this, [this](const QString& z) {
            if (z == referenceZone_) loadStockReference(z);
        });''', '''        connect(worldCache_, &WorldCache::ready, this, [this](const QString& z) {
            if (z == referenceZone_) loadStockReference(z);
        });
        connect(worldCache_, &WorldCache::lightmapReady, this, [this](const QString& z) {
            if (z != referenceZone_) return;
            if (worldCache_->lightmapFile(z)) loadStockLightmap(z);
            else ctx_.setStatus("Stock lightmap of " + z + ": " + worldCache_->lightmapErrorOf(z));
        });'''),
 ('''            self->stockWorld_ = world;
            self->camera_->setReferenceTextured(std::move(*texBatches));
            self->camera_->setGameStockMap(zone.toStdString());''', '''            self->stockWorld_ = world;
            self->camera_->setReferenceTextured(std::move(*texBatches));
            self->camera_->setGameStockMap(zone.toStdString());
            // Game Lighting draws the stock world with its own lightmap (research 94): cached, or made now.
            self->camera_->setStockLightmap(nullptr);
            if (self->worldCache_->lightmapFile(zone)) self->loadStockLightmap(zone);
            else if (self->worldCache_->lightmapErrorOf(zone).isEmpty()) self->worldCache_->requestLightmap(zone);'''),
 ('''        camera_->setGameStockMap("");''', '''        camera_->setGameStockMap("");
        camera_->setStockLightmap(nullptr);'''),
 ('''void MainWindow::loadStockReference(const QString& zone) {''', '''void MainWindow::loadStockLightmap(const QString& zone) {
    const std::optional<QString> file = worldCache_ ? worldCache_->lightmapFile(zone) : std::nullopt;
    if (!file) return;
    QPointer<MainWindow> self(this);
    const std::string path = file->toStdString();
    std::thread([self, zone, path] {
        StockLightmap lm;
        std::string err;
        std::shared_ptr<const app::StockLightmapLayers> layers;
        if (readStockLightmapCache(path, lm, &err)) layers = app::stockLightmapLayers(lm);
        QMetaObject::invokeMethod(qApp, [self, zone, layers, err] {
            if (!self || self->referenceZone_ != zone) return;
            if (layers && layers->width > 0) {
                self->camera_->setStockLightmap(layers);
                self->ctx_.setStatus(QString("Stock lightmap of %1: %2 x %3 (View > Game Lighting uses it)").arg(zone).arg(layers->width).arg(layers->height));
            } else {
                self->ctx_.setStatus("Stock lightmap of " + zone + " not read: " + QString::fromStdString(err));
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::loadStockReference(const QString& zone) {'''),
])
s = io.open('src/qtapp/main_window.cpp', encoding='utf-8', newline='').read()
if '#include "game_lightmap.h"' not in s:
    i = s.index('#include "')
    s = s[:i] + '#include "game_lightmap.h"\n' + s[i:]
    io.open('src/qtapp/main_window.cpp', 'w', encoding='utf-8', newline='').write(s)
print('ok')
