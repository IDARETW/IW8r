import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    pre = ''
    if p.endswith('scene.cpp'):
        at = s.index('size_t buildReferenceTexBatches(const StockWorld& world')
        pre, s = s[:at], s[at:]
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(pre + s)


# ---- reference vertices: lightmap index + u in lu
patch('src/viewport/scene.cpp', [
 ('''        const std::vector<std::array<float, 2>>* lmuv =
            mi < world.lightmapUv.size() && world.lightmapUv[mi].size() == m.pos.size() ? &world.lightmapUv[mi] : nullptr;''',
  '''        const std::vector<std::array<float, 2>>* lmuv =
            mi < world.lightmapUv.size() && world.lightmapUv[mi].size() == m.pos.size() ? &world.lightmapUv[mi] : nullptr;
        // Which lightmap (Warzone maps have many): the vertex carries index + u in its lightmap u (the shader splits it).
        const std::vector<int>* lmidx =
            mi < world.lightmapIndex.size() && world.lightmapIndex[mi].size() == m.pos.size() ? &world.lightmapIndex[mi] : nullptr;'''),
 ('''                if (lmuv && (*lmuv)[k][0] >= 0) v.lu = (*lmuv)[k][0], v.lv = (*lmuv)[k][1];''',
  '''                if (lmuv && (*lmuv)[k][0] >= 0) {
                    const int li = lmidx ? (*lmidx)[k] : 0;
                    if (li >= 0) v.lu = float(li) + std::min((*lmuv)[k][0], 0.99994f), v.lv = (*lmuv)[k][1];
                }'''),
])

# ---- layers: an atlas of several lightmaps with a rectangle per index
patch('src/viewport/game_lightmap.h', [
 ('''struct StockLightmapLayers {
    int width = 0, height = 0;
    std::vector<float> a, b, d;
};
std::shared_ptr<StockLightmapLayers> stockLightmapLayers(const StockLightmap& lm);''',
  '''struct StockLightmapLayers {
    int width = 0, height = 0;
    std::vector<float> a, b, d;
    // Per lightmap index: its rectangle in the layers (x, y, width, height, all 0..1); zero = not loaded.
    std::vector<std::array<float, 4>> rects;
};
std::shared_ptr<StockLightmapLayers> stockLightmapLayers(const StockLightmap& lm);
// Several lightmaps (a Warzone map's, by StockLightmap::index) packed into one atlas (shelves, 1-texel gaps).
std::shared_ptr<StockLightmapLayers> stockLightmapAtlas(const std::vector<StockLightmap>& lms);'''),
])
patch('src/viewport/game_lightmap.cpp', [
 ('''        out->a[i * 4 + 3] = 1.0f;
        out->b[i * 4 + 3] = 0.0f;
        out->d[i * 4 + 3] = 0.0f;
    }
    return out;
}''', '''        out->a[i * 4 + 3] = 1.0f;
        out->b[i * 4 + 3] = 0.0f;
        out->d[i * 4 + 3] = 0.0f;
    }
    out->rects.assign(size_t(std::max(lm.index, 0)) + 1, {0, 0, 0, 0});
    out->rects[size_t(std::max(lm.index, 0))] = {0, 0, 1, 1};
    return out;
}

std::shared_ptr<StockLightmapLayers> stockLightmapAtlas(const std::vector<StockLightmap>& lms) {
    std::vector<const StockLightmap*> list;
    for (const StockLightmap& l : lms)
        if (l.width > 0 && l.height > 0 && l.index >= 0) list.push_back(&l);
    if (list.size() == 1 && list[0]->index == 0) return stockLightmapLayers(*list[0]);
    auto out = std::make_shared<StockLightmapLayers>();
    if (list.empty()) return out;
    std::sort(list.begin(), list.end(), [](const StockLightmap* x, const StockLightmap* y) { return x->height > y->height; });
    // Shelf packing into the narrowest power-of-two width that keeps the atlas roughly square.
    size_t area = 0;
    int widest = 0, maxIndex = 0;
    for (const StockLightmap* l : list) area += size_t(l->width + 1) * size_t(l->height + 1), widest = std::max(widest, l->width + 1),
                                        maxIndex = std::max(maxIndex, l->index);
    int W = 256;
    while (W < widest || size_t(W) * size_t(W) < area) W *= 2;
    W = std::min(W, 16384);
    struct Place { const StockLightmap* l; int x, y; };
    std::vector<Place> places;
    int x = 0, y = 0, shelf = 0;
    for (const StockLightmap* l : list) {
        if (x + l->width > W) x = 0, y += shelf + 1, shelf = 0;
        places.push_back({l, x, y});
        x += l->width + 1;
        shelf = std::max(shelf, l->height);
    }
    const int H = y + shelf;
    if (H > 16384) return out;  // too large for one texture: leave the lightmaps out
    out->width = W, out->height = H;
    const size_t n = size_t(W) * size_t(H);
    out->a.assign(n * 4, 0.0f), out->b.assign(n * 4, 0.0f), out->d.assign(n * 4, 0.0f);
    out->rects.assign(size_t(maxIndex) + 1, {0, 0, 0, 0});
    for (const Place& p : places) {
        const auto one = stockLightmapLayers(*p.l);
        if (!one || one->width <= 0) continue;
        for (int r = 0; r < one->height; ++r) {
            const size_t src = size_t(r) * size_t(one->width) * 4, dst = (size_t(p.y + r) * size_t(W) + size_t(p.x)) * 4;
            std::copy_n(one->a.begin() + src, size_t(one->width) * 4, out->a.begin() + dst);
            std::copy_n(one->b.begin() + src, size_t(one->width) * 4, out->b.begin() + dst);
            std::copy_n(one->d.begin() + src, size_t(one->width) * 4, out->d.begin() + dst);
        }
        out->rects[size_t(p.l->index)] = {float(p.x) / W, float(p.y) / H, float(p.l->width) / W, float(p.l->height) / H};
    }
    return out;
}'''),
])

# ---- renderer: rectangles in a constant buffer; the shader splits index + u
patch('src/viewport/render.h', [
 ('''    void setStockLightmap(int width, int height, const float* a, const float* b, const float* d);''',
  '''    void setStockLightmap(int width, int height, const float* a, const float* b, const float* d,
                          const std::vector<std::array<float, 4>>& rects = {});'''),
 ('''    ID3D11ShaderResourceView* stockLmSrv_[3] = {nullptr, nullptr, nullptr};''',
  '''    ID3D11ShaderResourceView* stockLmSrv_[3] = {nullptr, nullptr, nullptr};
    ID3D11Buffer* cbLmRects_ = nullptr;
    static constexpr int kMaxLightmapRects = 1024;'''),
])
patch('src/viewport/render.cpp', [
 ('''Texture2D gameLmA : register(t6);''', '''Texture2D gameLmA : register(t6);
// The stock reference's lightmap rectangles in its atlas (setStockLightmap): x, y, width, height per lightmap index,
// and the atlas size in .x of lmInfo. A reference vertex carries index + u in its lightmap u.
cbuffer LightmapRects : register(b6) { float4 lmInfo; float4 lmRects[1024]; };'''),
 ('''        if ((stockRef ? batchFlags2.w > 0.5 : game0.z > 0.5) && i.lm.x >= 0) {
            float4 a = gameLmA.SampleLevel(lutSamp, i.lm, 0);
            float3 b = gameLmB.SampleLevel(lutSamp, i.lm, 0).rgb;
            float3 d = gameLmD.SampleLevel(lutSamp, i.lm, 0).xyz;''', '''        float2 lmUv = i.lm;
        bool lmOk = (stockRef ? batchFlags2.w > 0.5 : game0.z > 0.5) && i.lm.x >= 0;
        if (lmOk && stockRef) {
            float idx = floor(i.lm.x);
            float4 rc = idx < 1024 ? lmRects[int(idx)] : float4(0, 0, 0, 0);
            if (rc.z <= 0) lmOk = false;
            float2 half = 0.5 / max(lmInfo.xy, 1.0) / max(rc.zw, 1e-6);
            lmUv = rc.xy + clamp(float2(i.lm.x - idx, i.lm.y), half, 1.0 - half) * rc.zw;
        }
        if (lmOk) {
            float4 a = gameLmA.SampleLevel(lutSamp, lmUv, 0);
            float3 b = gameLmB.SampleLevel(lutSamp, lmUv, 0).rgb;
            float3 d = gameLmD.SampleLevel(lutSamp, lmUv, 0).xyz;'''),
 ('''void SceneRenderer::setStockLightmap(int width, int height, const float* a, const float* b, const float* d) {
    for (int k = 0; k < 3; ++k) safeRelease(stockLmSrv_[k]), safeRelease(stockLmTex_[k]);
    if (!dev_ || width < 1 || height < 1 || !a || !b || !d) return;''', '''void SceneRenderer::setStockLightmap(int width, int height, const float* a, const float* b, const float* d,
                                     const std::vector<std::array<float, 4>>& rects) {
    for (int k = 0; k < 3; ++k) safeRelease(stockLmSrv_[k]), safeRelease(stockLmTex_[k]);
    if (dev_ && !cbLmRects_) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 16 + 16 * kMaxLightmapRects;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        dev_->CreateBuffer(&bd, nullptr, &cbLmRects_);
    }
    if (cbLmRects_) {
        std::vector<float> cb(size_t(4 + 4 * kMaxLightmapRects), 0.0f);
        cb[0] = float(width), cb[1] = float(height);
        // A single lightmap without rectangles is index 0 over the whole texture.
        if (rects.empty()) cb[4] = 0, cb[5] = 0, cb[6] = 1, cb[7] = 1;
        for (size_t k = 0; k < rects.size() && k < size_t(kMaxLightmapRects); ++k)
            for (int c = 0; c < 4; ++c) cb[4 + 4 * k + size_t(c)] = rects[k][size_t(c)];
        ctx_->UpdateSubresource(cbLmRects_, 0, nullptr, cb.data(), 0, 0);
    }
    if (!dev_ || width < 1 || height < 1 || !a || !b || !d) return;'''),
 ('''    if (cbProbes_) ctx_->PSSetConstantBuffers(5, 1, &cbProbes_);''', '''    if (cbProbes_) ctx_->PSSetConstantBuffers(5, 1, &cbProbes_);
    if (cbLmRects_) ctx_->PSSetConstantBuffers(6, 1, &cbLmRects_);'''),
 ('''    safeRelease(probeArrSrv_), safeRelease(probeArrTex_), safeRelease(cbProbes_);''',
  '''    safeRelease(probeArrSrv_), safeRelease(probeArrTex_), safeRelease(cbProbes_), safeRelease(cbLmRects_);'''),
])
patch('src/qtapp/camera_view.cpp', [
 ('''        if (l && l->width > 0) renderer_.setStockLightmap(l->width, l->height, l->a.data(), l->b.data(), l->d.data());''',
  '''        if (l && l->width > 0) renderer_.setStockLightmap(l->width, l->height, l->a.data(), l->b.data(), l->d.data(), l->rects);'''),
])

# ---- main window: every lightmap of the zone (one file on MP maps, lightmaps/<index> on Warzone)
patch('src/qtapp/main_window.cpp', [
 ('''void MainWindow::loadStockLightmap(const QString& zone) {
    const std::optional<QString> file = worldCache_ ? worldCache_->lightmapFile(zone) : std::nullopt;
    if (!file) return;
    QPointer<MainWindow> self(this);
    const std::string path = file->toStdString();
    std::thread([self, zone, path] {
        StockLightmap lm;
        std::string err;
        std::shared_ptr<const app::StockLightmapLayers> layers;
        if (readStockLightmapCache(path, lm, &err)) layers = app::stockLightmapLayers(lm);''',
  '''void MainWindow::loadStockLightmap(const QString& zone) {
    if (!worldCache_) return;
    // MP maps: one lightmap (lightmap.iw8rlm); Warzone maps: lightmaps/<index>.iw8rlm, more as streamed tiles arrive.
    std::vector<std::string> paths;
    if (const std::optional<QString> file = worldCache_->lightmapFile(zone)) paths.push_back(file->toStdString());
    for (const int idx : worldCache_->lightmapIndices(zone))
        if (const std::optional<QString> f = worldCache_->lightmapFile(zone, idx)) paths.push_back(f->toStdString());
    if (paths.empty()) return;
    QPointer<MainWindow> self(this);
    std::thread([self, zone, paths] {
        std::vector<StockLightmap> lms;
        std::string err;
        for (const std::string& path : paths) {
            StockLightmap lm;
            std::string e;
            if (readStockLightmapCache(path, lm, &e)) lms.push_back(std::move(lm));
            else err = e;
        }
        std::shared_ptr<const app::StockLightmapLayers> layers;
        if (!lms.empty()) layers = app::stockLightmapAtlas(lms);'''),
 ('''            if (worldCache_->lightmapFile(z)) loadStockLightmap(z);
            else ctx_.setStatus("Stock lightmap of " + z + ": " + worldCache_->lightmapErrorOf(z));''',
  '''            if (worldCache_->lightmapFile(z) || !worldCache_->lightmapIndices(z).isEmpty()) loadStockLightmap(z);
            else ctx_.setStatus("Stock lightmap of " + z + ": " + worldCache_->lightmapErrorOf(z));'''),
 ('''            if (self->worldCache_->lightmapFile(zone)) self->loadStockLightmap(zone);
            if (self->worldCache_->probesFile(zone)) self->loadStockProbes(zone);
            const bool needLm = !self->worldCache_->lightmapFile(zone) && self->worldCache_->lightmapErrorOf(zone).isEmpty();''',
  '''            const bool haveLm = self->worldCache_->lightmapFile(zone) || !self->worldCache_->lightmapIndices(zone).isEmpty();
            if (haveLm) self->loadStockLightmap(zone);
            if (self->worldCache_->probesFile(zone)) self->loadStockProbes(zone);
            const bool needLm = !haveLm && self->worldCache_->lightmapErrorOf(zone).isEmpty();'''),
])
print('ok')
