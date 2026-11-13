import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


RH = 'src/viewport/render.h'
R = 'src/viewport/render.cpp'
CH = 'src/qtapp/camera_view.h'
CC = 'src/qtapp/camera_view.cpp'
MC = 'src/qtapp/main_window.cpp'

# ---------------- renderer interface
patch(RH, '''    void setStockLocalLights(std::vector<iw8r::StockGameLight> lights) { stockLights_ = std::move(lights); }''',
      '''    // shadows: per light (same order), its shadow camera (research 106 section 4); empty or cast = false: unshadowed.
    void setStockLocalLights(std::vector<iw8r::StockGameLight> lights, std::vector<iw8r::StockLightShadow> shadows = {}) {
        stockLights_ = std::move(lights);
        stockLightShadows_ = std::move(shadows);
        for (int& s : lightSlotOf_) s = -1;
        lightSlotOf_.assign(stockLights_.size(), -1);
        for (auto& sl : lightSlots_) sl = LightSlot{};
    }
    size_t stockLightShadowsDrawn() const { return lightShadowsDrawn_; }''')
patch(RH, '''    std::vector<iw8r::StockGameLight> stockLights_;
    size_t stockLightsDrawn_ = 0;''', '''    std::vector<iw8r::StockGameLight> stockLights_;
    std::vector<iw8r::StockLightShadow> stockLightShadows_;
    size_t stockLightsDrawn_ = 0;
    // Local light shadow maps: kLightSlots slices of kLightSlotSize^2 depth (t24), one light each; their matrices and
    // lookup constants (t23). A slot is drawn once (static casters), again when the casters change.
    static constexpr int kLightSlots = 64;
    static constexpr unsigned kLightSlotSize = 512;
    struct LightSlot {
        int light = -1;      // stockLights_ index, -1 = free
        bool drawn = false;  // its depth is current
        uint64_t lastUsed = 0;
        float vp[16] = {};
        float params[4] = {};  // near, far, k, bias
    };
    std::array<LightSlot, kLightSlots> lightSlots_{};
    std::vector<int> lightSlotOf_;  // per light: its slot or -1
    size_t lightShadowsDrawn_ = 0;  // slots with a current depth (bench)
    bool lightSlotsChanged_ = true;
    ID3D11Texture2D* lightShadowTex_ = nullptr;
    ID3D11ShaderResourceView* lightShadowSrv_ = nullptr;
    std::array<ID3D11DepthStencilView*, kLightSlots> lightShadowDsv_{};
    ID3D11Buffer* lightSlotBuf_ = nullptr;
    ID3D11ShaderResourceView* lightSlotSrv_ = nullptr;
    bool createLightShadowResources();''')

# ---------------- shader: the lookup
patch(R, '''StructuredBuffer<uint> lightTiles : register(t22);
cbuffer LocalLights : register(b11) { float4 localInfo; };''', '''StructuredBuffer<uint> lightTiles : register(t22);
cbuffer LocalLights : register(b11) { float4 localInfo; };
// Their shadow maps (research 106 section 4): per slot the light's view-projection and (near, far, k, bias); depth t24.
struct LightShadowSlot { row_major float4x4 vp; float4 p; };
StructuredBuffer<LightShadowSlot> lightShadowSlots : register(t23);
Texture2DArray<float> lightShadowMaps : register(t24);
float gameLocalShadow(uint slot, float3 P) {
    const LightShadowSlot s = lightShadowSlots[slot];
    const float4 c = mul(float4(P, 1.0), s.vp);
    if (c.w <= 1e-3) return 1.0;
    const float2 uv = c.xy / c.w * float2(0.5, -0.5) + 0.5;
    if (any(uv <= 0.0) || any(uv >= 1.0)) return 1.0;
    const float n = s.p.x, f = s.p.y, k = s.p.z, b = s.p.w;
    const float zr = (f - c.w) / (f - n);  // the receiver's reversed linear depth, as the stored one
    const float2 texel = 1.0 / 512.0;
    float sum = 0.0;
    [unroll] for (int q = 0; q < 4; ++q) {
        const float2 o = float2(q & 1 ? 0.5 : -0.5, q & 2 ? 0.5 : -0.5) * texel;
        const float d = lightShadowMaps.SampleLevel(lutSamp, float3(uv + o, slot), 0);
        const float z = n * f / max(f - d * (f - n), 1e-4);
        const float zs = (f - z) / (f - n);
        sum += saturate((exp(k * (zr - zs)) - b) / (1.0 - b));
    }
    return sum * 0.25;
}''')
patch(R, '''    const float att = prof * front * w * w * (3.0 - 2.0 * w);  // no shadow yet (research 106 section 4)
    if (att <= 0.0) return;''', '''    float att = prof * front * w * w * (3.0 - 2.0 * w);
    if (att <= 0.0) return;
    // Its shadow map (packed bits 0-7 = slot): spots over the whole cone, shadowed omnis inside their cone only.
    const uint slot = l.packed & 0xffu;
    if (slot != 0xffu) att *= lerp(1.0, gameLocalShadow(slot, P), isSpot ? 1.0 : spotF);
    if (att <= 0.0) return;''')

# ---------------- CPU: slots each frame, the records point at them
patch(R, '''    if (stockLightOrder_.empty()) return done();
    const size_t n = std::min<size_t>(stockLightOrder_.size(), kMaxLights);
    std::partial_sort(stockLightOrder_.begin(), stockLightOrder_.begin() + ptrdiff_t(n), stockLightOrder_.end());''',
      '''    if (stockLightOrder_.empty()) return done();
    const size_t n = std::min<size_t>(stockLightOrder_.size(), kMaxLights);
    std::partial_sort(stockLightOrder_.begin(), stockLightOrder_.begin() + ptrdiff_t(n), stockLightOrder_.end());
    // Shadow slots (research 106 section 4.1): the nearest drawn lights with a shadow map keep or get one; a slot not used
    // this frame goes to a nearer light that needs one. Lights without a slot draw unshadowed.
    static const bool noLightShadows = std::getenv("IW8R_NO_LIGHT_SHADOWS") != nullptr;
    if (lightSlotOf_.size() != stockLights_.size()) lightSlotOf_.assign(stockLights_.size(), -1);
    if (!noLightShadows && stockLightShadows_.size() == stockLights_.size() && createLightShadowResources()) {
        for (size_t k = 0; k < n; ++k) {
            const uint32_t li = stockLightOrder_[k].second;
            if (!stockLightShadows_[li].cast) continue;
            int s = lightSlotOf_[li];
            if (s < 0) {
                // a free slot, else the one used longest ago (not this frame)
                int best = -1;
                for (int j = 0; j < kLightSlots; ++j) {
                    if (lightSlots_[size_t(j)].light < 0) {
                        best = j;
                        break;
                    }
                    if (lightSlots_[size_t(j)].lastUsed < frame_ && (best < 0 || lightSlots_[size_t(j)].lastUsed < lightSlots_[size_t(best)].lastUsed))
                        best = j;
                }
                if (best < 0) continue;  // every slot is in use this frame
                LightSlot& sl = lightSlots_[size_t(best)];
                if (sl.light >= 0 && size_t(sl.light) < lightSlotOf_.size()) lightSlotOf_[size_t(sl.light)] = -1;
                sl = LightSlot{};
                sl.light = int(li);
                const iw8r::StockLightShadow& sh = stockLightShadows_[li];
                iw8r::stockLightShadowMatrix(sh, sl.vp);
                sl.params[0] = sh.zNear, sl.params[1] = sh.zFar, sl.params[2] = sh.k, sl.params[3] = sh.bias;
                lightSlotOf_[li] = best;
                lightSlotsChanged_ = true;
                s = best;
            }
            lightSlots_[size_t(s)].lastUsed = frame_;
        }
    }''')
patch(R, '''        const iw8r::StockGameLight& l = stockLights_[stockLightOrder_[k].second];
        dst[k] = l;''', '''        const iw8r::StockGameLight& l = stockLights_[stockLightOrder_[k].second];
        dst[k] = l;
        {
            const uint32_t li = stockLightOrder_[k].second;
            const int s = li < lightSlotOf_.size() ? lightSlotOf_[li] : -1;
            dst[k].packed = (l.packed & ~0xFFu) | (s >= 0 && lightSlots_[size_t(s)].drawn ? uint32_t(s) : 0xFFu);
        }''')
patch(R, '''void SceneRenderer::setGtsoLut(int width, int height, int depth, const uint8_t* r) {''', '''bool SceneRenderer::createLightShadowResources() {
    if (lightShadowSrv_ && lightSlotSrv_) return true;
    if (!dev_) return false;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = kLightSlotSize;
    td.MipLevels = 1;
    td.ArraySize = kLightSlots;
    td.Format = DXGI_FORMAT_R32_TYPELESS;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &lightShadowTex_))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_R32_FLOAT;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    sv.Texture2DArray.MipLevels = 1;
    sv.Texture2DArray.ArraySize = kLightSlots;
    bool ok = SUCCEEDED(dev_->CreateShaderResourceView(lightShadowTex_, &sv, &lightShadowSrv_));
    for (int j = 0; j < kLightSlots && ok; ++j) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dv{};
        dv.Format = DXGI_FORMAT_D32_FLOAT;
        dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        dv.Texture2DArray.FirstArraySlice = UINT(j);
        dv.Texture2DArray.ArraySize = 1;
        ok = SUCCEEDED(dev_->CreateDepthStencilView(lightShadowTex_, &dv, &lightShadowDsv_[size_t(j)]));
    }
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = UINT(kLightSlots * 80);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = 80;
    ok = ok && SUCCEEDED(dev_->CreateBuffer(&bd, nullptr, &lightSlotBuf_)) &&
         SUCCEEDED(dev_->CreateShaderResourceView(lightSlotBuf_, nullptr, &lightSlotSrv_));
    if (!ok) {
        for (auto*& d : lightShadowDsv_) safeRelease(d);
        safeRelease(lightShadowSrv_), safeRelease(lightShadowTex_), safeRelease(lightSlotSrv_), safeRelease(lightSlotBuf_);
    }
    return ok;
}

void SceneRenderer::setGtsoLut(int width, int height, int depth, const uint8_t* r) {''')
patch(R, '''    safeRelease(cbLocal_), safeRelease(iesSrv_), safeRelease(iesTex_);
    stockTileCap_ = 0;''', '''    safeRelease(cbLocal_), safeRelease(iesSrv_), safeRelease(iesTex_);
    stockTileCap_ = 0;
    for (auto*& d : lightShadowDsv_) safeRelease(d);
    safeRelease(lightShadowSrv_), safeRelease(lightShadowTex_), safeRelease(lightSlotSrv_), safeRelease(lightSlotBuf_);''')
# bound with the lights
patch(R, '''        ID3D11ShaderResourceView* ll[3] = {stockLightsOn_ ? stockLightSrv_ : nullptr, iesSrv_, stockLightsOn_ ? stockTileSrv_ : nullptr};
        ctx_->PSSetShaderResources(20, 3, ll);''', '''        ID3D11ShaderResourceView* ll[5] = {stockLightsOn_ ? stockLightSrv_ : nullptr, iesSrv_, stockLightsOn_ ? stockTileSrv_ : nullptr,
                                           stockLightsOn_ ? lightSlotSrv_ : nullptr, stockLightsOn_ ? lightShadowSrv_ : nullptr};
        ctx_->PSSetShaderResources(20, 5, ll);''')

# ---------------- drawing the slots with the caster pass
patch(R, '''    const bool nearShadowDue = shadowOn && nearEnabled_ && shadowDsvNear_ && (farShadowDue || nearDirty_);
    if (farShadowDue || nearShadowDue) {''', '''    const bool nearShadowDue = shadowOn && nearEnabled_ && shadowDsvNear_ && (farShadowDue || nearDirty_);
    // Local light shadow maps: every slot again when the casters changed; each frame a few of those not current.
    if (farShadowDue)
        for (LightSlot& sl : lightSlots_) sl.drawn = false;
    bool lightShadowsDue = false;
    if (stockLightsOn_ && lightShadowSrv_)
        for (const LightSlot& sl : lightSlots_) lightShadowsDue |= sl.light >= 0 && !sl.drawn;
    if (farShadowDue || nearShadowDue || lightShadowsDue) {''')
patch(R, '''        if (farShadowDue) drawCasters(shadowDsv_, sunViewProj_, false);
        if (nearShadowDue) drawCasters(shadowDsvNear_, sunViewProjNear_, true);''', '''        if (farShadowDue) drawCasters(shadowDsv_, sunViewProj_, false);
        if (nearShadowDue) drawCasters(shadowDsvNear_, sunViewProjNear_, true);
        if (lightShadowsDue) {
            // Up to six local light maps a frame (static world and props; the game also draws at most a few a frame).
            D3D11_VIEWPORT lvp{0, 0, float(kLightSlotSize), float(kLightSlotSize), 0, 1};
            ctx_->RSSetViewports(1, &lvp);
            int budget = 6;
            for (int j = 0; j < kLightSlots && budget > 0; ++j) {
                LightSlot& sl = lightSlots_[size_t(j)];
                if (sl.light < 0 || sl.drawn || !lightShadowDsv_[size_t(j)]) continue;
                drawCasters(lightShadowDsv_[size_t(j)], sl.vp, true);
                sl.drawn = true;
                lightSlotsChanged_ = true;
                --budget;
            }
            ctx_->RSSetViewports(1, &svp);
        }''')
print('ok part 1')
