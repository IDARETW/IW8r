import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        if a != b and s.count(a) == 0 and s.count(b) >= 1:
            continue
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


# ---------------- renderer
patch('src/viewport/render.h', [
 ('''    void setEnvBrdfLut(int width, int height, const uint8_t* rg);''', '''    void setEnvBrdfLut(int width, int height, const uint8_t* rg);
    // The stock reference's reflection probes (core stock_probes, research 95 "Indirect specular"): every probe's
    // octahedral BC6H image as one array slice (all levels) and the influence boxes, drawn highest priority first.
    // null clears them.
    void setStockProbes(const struct StockProbeSet* set);
    size_t stockProbeCount() const { return stockProbeSlices_; }'''),
 ('''    ID3D11Texture2D* envBrdfTex_ = nullptr;''', '''    ID3D11Texture2D* envBrdfTex_ = nullptr;
    ID3D11Texture2D* probeArrTex_ = nullptr;
    ID3D11ShaderResourceView* probeArrSrv_ = nullptr;
    ID3D11Buffer* cbProbes_ = nullptr;
    size_t stockProbeSlices_ = 0;
    static constexpr int kMaxProbeBoxes = 128;'''),
])

probe_hlsl = '''Texture2D envBrdf : register(t10);
// The stock reference's reflection probes (setStockProbes): octahedral, world space, 4-texel gutter at every level.
Texture2DArray stockProbes : register(t11);
struct ProbeBox { float4 centerSlice; float4 axisX; float4 axisY; float4 axisZ; float4 originFeather; };
cbuffer Probes : register(b5) { float4 probeCount; ProbeBox probeBoxes[128]; };
float3 probeAt(float slice, float3 dir, float mip) {
    float3 a = abs(dir);
    float2 o = dir.xy / max(a.x + a.y + a.z, 1e-6);
    if (dir.z < 0) o = (1.0 - abs(o.yx)) * float2(o.x >= 0 ? 1 : -1, o.y >= 0 ? 1 : -1);
    float g = float(4u << uint(mip));
    float2 uv = (o * 0.5 + 0.5) * (1.0 - g / 128.0) + g / 256.0;
    return stockProbes.SampleLevel(lutSamp, float3(uv, slice), mip).rgb;
}
// The stock shader's probe term (research 95): the boxes front to back by priority, each with its box-parallax
// direction and a feathered fade, until the weights reach 1; two levels around 5 - 5 g'.
float3 stockProbeLight(float3 p, float3 R, float gp) {
    float mip = clamp(5.0 - 5.0 * gp, 0.0, 5.0);
    float m0 = floor(mip), mf = mip - m0, m1 = min(m0 + 1.0, 5.0);
    float3 sum = 0;
    float wsum = 0;
    int n = int(probeCount.x);
    for (int k = 0; k < n && wsum < 0.999; ++k) {
        ProbeBox b = probeBoxes[k];
        float3 d = p - b.centerSlice.xyz;
        float3 lp = float3(dot(d, b.axisX.xyz), dot(d, b.axisY.xyz), dot(d, b.axisZ.xyz));
        float3 h = float3(b.axisX.w, b.axisY.w, b.axisZ.w);
        float3 rem = h - abs(lp);
        if (rem.x < 0 || rem.y < 0 || rem.z < 0) continue;
        float fade = saturate(min(rem.x, min(rem.y, rem.z)) / max(b.originFeather.w, 1.0));
        // Box parallax: where the reflection ray leaves the box, seen from the probe's capture origin.
        float3 lr = float3(dot(R, b.axisX.xyz), dot(R, b.axisY.xyz), dot(R, b.axisZ.xyz));
        float3 t1 = (h - lp) / (abs(lr) > 1e-6 ? lr : 1e-6), t2 = (-h - lp) / (abs(lr) > 1e-6 ? lr : 1e-6);
        float3 tf = max(t1, t2);
        float t = min(tf.x, min(tf.y, tf.z));
        float3 dir = (p + R * max(t, 0.0)) - b.originFeather.xyz;
        if (dot(dir, dir) < 1e-6) dir = R;
        float w = fade * (1.0 - wsum);
        float3 c = lerp(probeAt(b.centerSlice.w, dir, m0), probeAt(b.centerSlice.w, dir, m1), mf);
        sum += c * w;
        wsum += w;
    }
    return wsum > 1e-4 ? sum / wsum : float3(0, 0, 0);
}'''

patch('src/viewport/render.cpp', [
 ('''Texture2D envBrdf : register(t10);''', probe_hlsl),
 ('''            c = albedo * indirect * kd * (1.0 - fmax) + Fenv * Lr;''', '''            float3 S = Lr;
            if (probeCount.x > 0.5) {
                // Probes are luminance-normalised: relit by the lightmap along the reflection (research 95).
                float3 P = stockProbeLight(i.w, R, gp) * dot(Lr, float3(0.2126, 0.7152, 0.0722));
                float3 mixed = lerp(P, Lr, 0.85);
                S = lerp(0.18 * mixed, P, 1.0);  // specular occlusion 1: no GTAO here
            }
            c = albedo * indirect * kd * (1.0 - fmax) + Fenv * S;'''),
 ('''    safeRelease(envBrdfSrv_), safeRelease(envBrdfTex_);
    safeRelease(meterSrv_)''', '''    safeRelease(envBrdfSrv_), safeRelease(envBrdfTex_);
    safeRelease(probeArrSrv_), safeRelease(probeArrTex_), safeRelease(cbProbes_);
    safeRelease(meterSrv_)'''),
 ('''void SceneRenderer::writeGameCb() {''', '''void SceneRenderer::setStockProbes(const StockProbeSet* set) {
    safeRelease(probeArrSrv_), safeRelease(probeArrTex_);
    stockProbeSlices_ = 0;
    if (!dev_) return;
    if (!cbProbes_) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 16 + 80 * kMaxProbeBoxes;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(dev_->CreateBuffer(&bd, nullptr, &cbProbes_))) return;
    }
    std::vector<float> cb(size_t(4 + 20 * kMaxProbeBoxes), 0.0f);
    const auto upload = [&] { ctx_->UpdateSubresource(cbProbes_, 0, nullptr, cb.data(), 0, 0); };
    if (!set || set->probes.empty() || set->size < 16 || set->mipCount < 1) {
        upload();
        return;
    }
    const size_t perProbe = bc6hBytesPerProbe(set->size, set->mipCount);
    for (const auto& p : set->probes)
        if (p.bc6h.size() < perProbe) {
            upload();
            return;
        }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = UINT(set->size);
    td.MipLevels = UINT(set->mipCount);
    td.ArraySize = UINT(set->probes.size());
    td.Format = DXGI_FORMAT_BC6H_UF16;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> init;
    for (const auto& p : set->probes) {
        size_t off = 0;
        for (int m = 0; m < set->mipCount; ++m) {
            const int s = std::max(set->size >> m, 4);
            const size_t blocksRow = size_t(s + 3) / 4;
            init.push_back({p.bc6h.data() + off, UINT(blocksRow * 16), 0});
            off += blocksRow * blocksRow * 16;
        }
    }
    if (FAILED(dev_->CreateTexture2D(&td, init.data(), &probeArrTex_)) || FAILED(dev_->CreateShaderResourceView(probeArrTex_, nullptr, &probeArrSrv_))) {
        safeRelease(probeArrTex_);
        upload();
        return;
    }
    stockProbeSlices_ = set->probes.size();
    // Boxes, highest priority first (the fallback box, priority -FLT_MAX, last).
    std::vector<size_t> order(set->boxes.size());
    for (size_t k = 0; k < order.size(); ++k) order[k] = k;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return set->boxes[a].priority > set->boxes[b].priority; });
    size_t n = 0;
    for (size_t k : order) {
        const StockProbeBox& b = set->boxes[k];
        if (b.imageIndex < 0 || size_t(b.imageIndex) >= set->probes.size() || n >= size_t(kMaxProbeBoxes)) continue;
        float* e = cb.data() + 4 + 20 * n;
        e[0] = b.center[0], e[1] = b.center[1], e[2] = b.center[2], e[3] = float(b.imageIndex);
        for (int a = 0; a < 3; ++a) {
            e[4 + 4 * a] = b.axes[a][0], e[5 + 4 * a] = b.axes[a][1], e[6 + 4 * a] = b.axes[a][2];
            e[7 + 4 * a] = b.halfSize[a];
        }
        e[16] = b.position[0], e[17] = b.position[1], e[18] = b.position[2];
        e[19] = std::max({b.feather[0], b.feather[1], b.feather[2], 1.0f});
        ++n;
    }
    cb[0] = float(n);
    upload();
}

void SceneRenderer::writeGameCb() {'''),
 ('''    ID3D11ShaderResourceView* gl[2] = {gameLutSrv_, envBrdfSrv_};
    ctx_->PSSetShaderResources(9, 2, gl);''', '''    ID3D11ShaderResourceView* gl[3] = {gameLutSrv_, envBrdfSrv_, probeArrSrv_};
    ctx_->PSSetShaderResources(9, 3, gl);
    if (cbProbes_) ctx_->PSSetConstantBuffers(5, 1, &cbProbes_);'''),
])
s = io.open('src/viewport/render.cpp', encoding='utf-8', newline='').read()
if '#include "stock_probes.h"' not in s:
    s = s.replace('#include "render.h"', '#include "render.h"\n#include "stock_probes.h"', 1)
    assert '#include "stock_probes.h"' in s
    io.open('src/viewport/render.cpp', 'w', encoding='utf-8', newline='').write(s)

# ---------------- camera view
patch('src/qtapp/camera_view.h', [
 ('''    bool hasEnvBrdfLut() const { return renderer_.hasEnvBrdfLut(); }''', '''    bool hasEnvBrdfLut() const { return renderer_.hasEnvBrdfLut(); }
    // The stock reference's reflection probes (null clears), sent to the renderer on the next frame.
    void setStockProbes(std::shared_ptr<const StockProbeSet> set) {
        pendingProbes_ = std::move(set);
        probesDirty_ = true;
        update();
    }
    size_t stockProbeCount() const { return renderer_.stockProbeCount(); }'''),
 ('''    bool stockLmDirty_ = false;''', '''    bool stockLmDirty_ = false;
    std::shared_ptr<const StockProbeSet> pendingProbes_;
    bool probesDirty_ = false;'''),
])
patch('src/qtapp/camera_view.cpp', [
 ('''    if (stockLmDirty_) {''', '''    if (probesDirty_) {
        renderer_.setStockProbes(pendingProbes_.get());
        pendingProbes_.reset();
        probesDirty_ = false;
    }
    if (stockLmDirty_) {'''),
])
s = io.open('src/qtapp/camera_view.h', encoding='utf-8', newline='').read()
if '#include "stock_probes.h"' not in s:
    i = s.index('#include "')
    s = s[:i] + '#include "stock_probes.h"\n' + s[i:]
    io.open('src/qtapp/camera_view.h', 'w', encoding='utf-8', newline='').write(s)

# ---------------- main window: load probes with the lightmap
patch('src/qtapp/main_window.h', [
 ('''    void loadStockLightmap(const QString& zone);''', '''    void loadStockLightmap(const QString& zone);
    // The reference zone's reflection probes (world cache probes.iw8rprb) into the camera, read off the UI thread.
    void loadStockProbes(const QString& zone);'''),
])
patch('src/qtapp/main_window.cpp', [
 ('''        connect(worldCache_, &WorldCache::lightmapReady, this, [this](const QString& z) {''', '''        connect(worldCache_, &WorldCache::probesReady, this, [this](const QString& z) {
            if (z != referenceZone_) return;
            if (worldCache_->probesFile(z)) loadStockProbes(z);
            else if (!worldCache_->probesErrorOf(z).isEmpty()) ctx_.setStatus("Stock reflection probes of " + z + ": " + worldCache_->probesErrorOf(z));
        });
        connect(worldCache_, &WorldCache::lightmapReady, this, [this](const QString& z) {'''),
 ('''            self->camera_->setStockLightmap(nullptr);
            if (self->worldCache_->lightmapFile(zone)) self->loadStockLightmap(zone);
            else if (self->worldCache_->lightmapErrorOf(zone).isEmpty()) self->worldCache_->requestLightmap(zone);''',
  '''            self->camera_->setStockLightmap(nullptr);
            self->camera_->setStockProbes(nullptr);
            if (self->worldCache_->lightmapFile(zone)) self->loadStockLightmap(zone);
            if (self->worldCache_->probesFile(zone)) self->loadStockProbes(zone);
            const bool needLm = !self->worldCache_->lightmapFile(zone) && self->worldCache_->lightmapErrorOf(zone).isEmpty();
            const bool needProbes = !self->worldCache_->probesFile(zone) && self->worldCache_->probesErrorOf(zone).isEmpty();
            if (needLm || needProbes) self->worldCache_->requestLightmap(zone);'''),
 ('''        camera_->setStockLightmap(nullptr);
        ctx_.referencePlans.clear();''', '''        camera_->setStockLightmap(nullptr);
        camera_->setStockProbes(nullptr);
        ctx_.referencePlans.clear();'''),
 ('''void MainWindow::loadStockReference(const QString& zone) {''', '''void MainWindow::loadStockProbes(const QString& zone) {
    const std::optional<QString> file = worldCache_ ? worldCache_->probesFile(zone) : std::nullopt;
    if (!file) return;
    QPointer<MainWindow> self(this);
    const std::string path = file->toStdString();
    std::thread([self, zone, path] {
        auto set = std::make_shared<StockProbeSet>();
        std::string err;
        const bool ok = readStockProbeCache(path, *set, &err, false);  // the GPU takes the BC6H blocks as they are
        QMetaObject::invokeMethod(qApp, [self, zone, set, ok, err] {
            if (!self || self->referenceZone_ != zone) return;
            if (ok) self->camera_->setStockProbes(set);
            else self->ctx_.setStatus("Stock reflection probes of " + zone + " not read: " + QString::fromStdString(err));
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::loadStockReference(const QString& zone) {'''),
])
print('ok')
