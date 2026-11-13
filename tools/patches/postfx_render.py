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
T = 'src/qtapp/qt_gui_test.cpp'

# ---------------- renderer interface and state
patch(RH, '''        float lutScale = 1.0f;''', '''        float lutScale = 1.0f;
        // The map's post effects (research 109): the bloom veil's strength and the vignette's constants
        // (core gameVignetteConstants); zero = none.
        float veil = 0;
        float vignette[12] = {};''')
patch(RH, '''    ID3D11VertexShader* postVs_ = nullptr;
    ID3D11PixelShader* postPs_ = nullptr;''', '''    ID3D11VertexShader* postVs_ = nullptr;
    ID3D11PixelShader* postPs_ = nullptr;
    // The bloom veil chain (research 109 section 1.2): scene mips 1..n+1 and the upsampled levels 2..n, R16G16B16A16F.
    struct VeilLevel {
        ID3D11Texture2D* tex = nullptr;
        ID3D11RenderTargetView* rtv = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        int w = 0, h = 0;
    };
    std::vector<VeilLevel> veilS_, veilV_;  // index = mip level
    int veilCount_ = 0, veilW_ = 0, veilH_ = 0;
    ID3D11PixelShader* veilDownPs_ = nullptr;
    ID3D11PixelShader* veilKarisPs_ = nullptr;
    ID3D11PixelShader* veilUpPs_ = nullptr;
    ID3D11Buffer* cbVeil_ = nullptr;
    bool postFxEnabled_ = true;
    bool createVeilResources();
    void releaseVeilResources();
    // Builds the veil (V2) from the HDR scene; false when the map has no veil or it is off.
    bool drawVeil();''')
patch(RH, '''    void setGameAoStrength(float s) { gameAoStrength_ = s; }''', '''    void setGameAoStrength(float s) { gameAoStrength_ = s; }
    // The map's bloom veil and vignette (research 109) in Game Lighting.
    void setPostFxEnabled(bool on) { postFxEnabled_ = on; }''')

# ---------------- shaders
patch(R, '''float4 ps_post(VFull i) : SV_TARGET {
    float4 h = hdrScene.Load(int3(i.pos.xy, 0));
    if (h.a <= 0.0) discard;
    if (gameAo.y > 0.5 || gameAo.z > 0.5 || (gameSkyScale.z > 0.5 && gameSkyScale.z < 1.5)) return float4(h.rgb, 1.0);  // debug views
    return float4(gamePost(max(h.rgb, 0.0)), 1.0);
}''', '''// The bloom veil (research 109 section 1.2): its chain's source / add levels, the result (V2) and the post constants:
// veilDst = (dst w, dst h, 1 / w, 1 / h), veilW = (wIn, wOut); postFx0 = (veil, screen w, screen h); postVig0..2 = the
// vignette (core gameVignetteConstants).
Texture2D<float4> veilSrc : register(t25);
Texture2D<float4> veilAdd : register(t26);
Texture2D<float4> veilTex : register(t27);
cbuffer Veil : register(b12) { float4 veilDst; float4 veilW; float4 postFx0; float4 postVig0; float4 postVig1; float4 postVig2; };
static const float VR_OUT = 1.886476, VR_IN = 0.939981, VW_C = 0.479925, VW_OUT = 0.075948, VW_IN = 0.302964;
// Tap k at the fixed rotation n = 0.5 (the game rotates per pixel and frame under TAA; the editor has none).
float2 veilTap(int k, float stepA) { const float a = (k + 0.75) * stepA; return float2(sin(a), cos(a)) * ((k & 1) ? VR_IN : VR_OUT); }
float4 ps_veil_down(VFull i) : SV_TARGET {
    const float2 uv = i.pos.xy * veilDst.zw, d = veilDst.zw;
    float3 c = VW_C * veilSrc.SampleLevel(lutSamp, uv, 0).rgb;
    [unroll] for (int k = 0; k < 8; ++k) c += ((k & 1) ? VW_IN : VW_OUT) * veilSrc.SampleLevel(lutSamp, uv + veilTap(k, 0.78539816) * d, 0).rgb;
    return float4(clamp(c * 0.501110, -32255.0, 32255.0), 1.0);
}
float4 ps_veil_karis(VFull i) : SV_TARGET {  // mip 1: the firefly control (each tap weighted 1 / (1 + its max))
    const float2 uv = i.pos.xy * veilDst.zw, d = veilDst.zw;
    float4 s[5];
    s[4] = float4(VW_C * max(veilSrc.SampleLevel(lutSamp, uv, 0).rgb, 0.0), VW_C);
    [unroll] for (int k = 0; k < 4; ++k) {
        const float w = (k & 1) ? VW_IN : VW_OUT;
        s[k] = float4(w * max(veilSrc.SampleLevel(lutSamp, uv + veilTap(k, 1.5707963) * d, 0).rgb, 0.0), w);
    }
    [unroll] for (int j = 0; j < 5; ++j) s[j] *= 1.0 / (1.0 + max(s[j].r, max(s[j].g, s[j].b)));
    float3 r = 0;
    [unroll] for (int g = 0; g < 4; ++g) { const float4 a = s[4] + s[g] + s[(g + 1) & 3]; r += a.rgb / max(a.w, 1e-5); }
    return float4(clamp(0.25 * r, -32255.0, 32255.0), 1.0);
}
float4 ps_veil_up(VFull i) : SV_TARGET {
    const float2 uv = i.pos.xy * veilDst.zw, d = veilDst.zw * 2.0;  // offsets in source texels
    float3 b = VW_C * veilSrc.SampleLevel(lutSamp, uv, 0).rgb;
    [unroll] for (int k = 0; k < 4; ++k) b += ((k & 1) ? VW_IN : VW_OUT) * veilSrc.SampleLevel(lutSamp, uv + veilTap(k, 1.5707963) * d, 0).rgb;
    const float3 add = veilW.y > 0.0 ? veilAdd.SampleLevel(lutSamp, uv, 0).rgb : float3(0, 0, 0);
    return float4(b * 0.807919 * veilW.x + add * veilW.y, 1.0);
}
float4 ps_post(VFull i) : SV_TARGET {
    float4 h = hdrScene.Load(int3(i.pos.xy, 0));
    if (h.a <= 0.0) discard;
    if (gameAo.y > 0.5 || gameAo.z > 0.5 || (gameSkyScale.z > 0.5 && gameSkyScale.z < 1.5)) return float4(h.rgb, 1.0);  // debug views
    float3 c = max(h.rgb, 0.0);
    const float2 suv = i.pos.xy / max(postFx0.yz, 1.0);
    // The veil (research 109 section 1.3): a lerp toward the very wide blur, in linear HDR before the exposure.
    if (postFx0.x > 0.0) c = (1.0 - postFx0.x) * c + postFx0.x * clamp(veilTex.SampleLevel(lutSamp, suv, 0).rgb, 0.0, 32255.0);
    // The vignette (section 4.1), after the veil, in linear HDR.
    if (postVig0.z > 0.0) {
        const float2 p = (suv - 0.5) * postVig0.xy + postVig1.xy;
        const float r = length(p - clamp(p, -postVig1.zw, postVig1.zw));
        const float t = saturate(r * postVig2.x + postVig2.y);
        c *= 1.0 - (t + 0.6 * (t * t * (3.0 - 2.0 * t) - t)) * postVig0.z;
    }
    return float4(gamePost(c), 1.0);
}''')
patch(R, '''            if (compileTex("vs_post", "vs_5_0", pvs) && compileTex("ps_post", "ps_5_0", ppo)) {
                dev_->CreateVertexShader(pvs->GetBufferPointer(), pvs->GetBufferSize(), nullptr, &postVs_);
                dev_->CreatePixelShader(ppo->GetBufferPointer(), ppo->GetBufferSize(), nullptr, &postPs_);
            }
            safeRelease(pvs), safeRelease(ppo);''', '''            if (compileTex("vs_post", "vs_5_0", pvs) && compileTex("ps_post", "ps_5_0", ppo)) {
                dev_->CreateVertexShader(pvs->GetBufferPointer(), pvs->GetBufferSize(), nullptr, &postVs_);
                dev_->CreatePixelShader(ppo->GetBufferPointer(), ppo->GetBufferSize(), nullptr, &postPs_);
            }
            safeRelease(pvs), safeRelease(ppo);
            for (const auto& [entry, out] : {std::pair<const char*, ID3D11PixelShader**>{"ps_veil_down", &veilDownPs_},
                                             {"ps_veil_karis", &veilKarisPs_}, {"ps_veil_up", &veilUpPs_}}) {
                ID3DBlob* vb = nullptr;
                if (compileTex(entry, "ps_5_0", vb)) dev_->CreatePixelShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, out);
                safeRelease(vb);
            }''')
patch(R, '''    safeRelease(postVs_), safeRelease(postPs_), safeRelease(meterHdrPs_);''', '''    safeRelease(postVs_), safeRelease(postPs_), safeRelease(meterHdrPs_);
    safeRelease(veilDownPs_), safeRelease(veilKarisPs_), safeRelease(veilUpPs_), safeRelease(cbVeil_);
    releaseVeilResources();''')

# ---------------- the chain
patch(R, '''void SceneRenderer::drawPost() {
    IW8R_PROF("d.post");
    writeGameCb();  // b4 (exposure), the LUT on t9 and its sampler''', '''void SceneRenderer::releaseVeilResources() {
    for (auto* list : {&veilS_, &veilV_})
        for (VeilLevel& l : *list) safeRelease(l.srv), safeRelease(l.rtv), safeRelease(l.tex);
    veilS_.clear(), veilV_.clear();
    veilCount_ = veilW_ = veilH_ = 0;
}

bool SceneRenderer::createVeilResources() {
    if (veilW_ == w_ && veilH_ == h_ && veilCount_ > 0) return true;
    releaseVeilResources();
    if (!dev_ || w_ < 8 || h_ < 8) return false;
    // research 109 section 1.2: veilMip 2, veilCount = min(min(floor(log2(min(W, H))), 9) - 2, 7), scene mips 1..count+1.
    const int lg = int(std::floor(std::log2(double(std::min(w_, h_)))));
    const int count = std::clamp(std::min(lg, 9) - 2, 1, 7);
    const auto make = [&](int mip, VeilLevel& l) {
        l.w = std::max(1, w_ >> mip), l.h = std::max(1, h_ >> mip);
        D3D11_TEXTURE2D_DESC td{};
        td.Width = UINT(l.w), td.Height = UINT(l.h);
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(dev_->CreateTexture2D(&td, nullptr, &l.tex)) && SUCCEEDED(dev_->CreateRenderTargetView(l.tex, nullptr, &l.rtv)) &&
               SUCCEEDED(dev_->CreateShaderResourceView(l.tex, nullptr, &l.srv));
    };
    veilS_.resize(size_t(count + 2));
    veilV_.resize(size_t(count + 1));
    bool ok = true;
    for (int m = 1; m <= count + 1 && ok; ++m) ok = make(m, veilS_[size_t(m)]);
    for (int m = 2; m <= count && ok; ++m) ok = make(m, veilV_[size_t(m)]);
    if (!cbVeil_ && ok) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 96;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ok = SUCCEEDED(dev_->CreateBuffer(&bd, nullptr, &cbVeil_));
    }
    if (!ok) {
        releaseVeilResources();
        return false;
    }
    veilCount_ = count, veilW_ = w_, veilH_ = h_;
    return true;
}

bool SceneRenderer::drawVeil() {
    if (!postFxEnabled_ || game_.veil <= 0 || !hdrSrv_ || !postVs_ || !veilDownPs_ || !veilKarisPs_ || !veilUpPs_ || !createVeilResources())
        return false;
    IW8R_PROF("d.veil");
    static const float kWeights[7] = {0, 0.025f, 0.075f, 0.15f, 0.2f, 0.25f, 0.3f};  // s_veilWeights (Replay .rdata)
    ctx_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx_->OMSetDepthStencilState(dsOff_, 0);
    ctx_->RSSetState(rsTris_);
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(postVs_, nullptr, 0);
    if (lutSampler_) ctx_->PSSetSamplers(2, 1, &lutSampler_);
    ID3D11ShaderResourceView* none2[2] = {nullptr, nullptr};
    const auto pass = [&](ID3D11PixelShader* ps, const VeilLevel& dst, ID3D11ShaderResourceView* src, ID3D11ShaderResourceView* add,
                          float wIn, float wOut) {
        float cb[24] = {float(dst.w), float(dst.h), 1.0f / float(dst.w), 1.0f / float(dst.h), wIn, wOut, 0, 0};
        ctx_->UpdateSubresource(cbVeil_, 0, nullptr, cb, 0, 0);
        ctx_->PSSetShaderResources(25, 2, none2);
        ctx_->OMSetRenderTargets(1, &dst.rtv, nullptr);
        D3D11_VIEWPORT vp{0, 0, float(dst.w), float(dst.h), 0, 1};
        ctx_->RSSetViewports(1, &vp);
        ID3D11ShaderResourceView* in[2] = {src, add};
        ctx_->PSSetShaderResources(25, 2, in);
        ctx_->PSSetConstantBuffers(12, 1, &cbVeil_);
        ctx_->PSSetShader(ps, nullptr, 0);
        ctx_->Draw(3, 0);
    };
    const int c = veilCount_;
    pass(veilKarisPs_, veilS_[1], hdrSrv_, nullptr, 0, 0);
    for (int m = 2; m <= c + 1; ++m) pass(veilDownPs_, veilS_[size_t(m)], veilS_[size_t(m - 1)].srv, nullptr, 0, 0);
    // V_m = Up(V_{m+1} or the smallest mip) x wIn + S_m x weights[m - 2]; the first wIn is weights[count - 1].
    for (int m = c; m >= 2; --m) {
        ID3D11ShaderResourceView* src = m == c ? veilS_[size_t(c + 1)].srv : veilV_[size_t(m + 1)].srv;
        const float wIn = m == c ? kWeights[std::min(c - 1, 6)] : 1.0f, wOut = kWeights[std::min(m - 2, 6)];
        pass(veilUpPs_, veilV_[size_t(m)], src, wOut > 0 ? veilS_[size_t(m)].srv : nullptr, wIn, wOut);
    }
    ctx_->PSSetShaderResources(25, 2, none2);
    D3D11_VIEWPORT full{0, 0, float(w_), float(h_), 0, 1};
    ctx_->RSSetViewports(1, &full);
    return c >= 2;
}

void SceneRenderer::drawPost() {
    IW8R_PROF("d.post");
    const bool veil = drawVeil();
    {
        // The post constants (b12): veil strength and screen size, the vignette.
        float cb[24] = {};
        cb[8] = veil ? game_.veil : 0.0f, cb[9] = float(w_), cb[10] = float(h_);
        if (postFxEnabled_) std::memcpy(cb + 12, game_.vignette, sizeof(game_.vignette));
        if (cbVeil_ || createVeilResources()) {
            ctx_->UpdateSubresource(cbVeil_, 0, nullptr, cb, 0, 0);
            ctx_->PSSetConstantBuffers(12, 1, &cbVeil_);
        }
        ID3D11ShaderResourceView* vt = veil ? veilV_[2].srv : nullptr;
        ctx_->PSSetShaderResources(27, 1, &vt);
    }
    writeGameCb();  // b4 (exposure), the LUT on t9 and its sampler''')
patch(R, '''    ctx_->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    ctx_->PSSetShaderResources(15, 1, &none);
    sceneRtv_ = rtv_;''', '''    ctx_->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    ctx_->PSSetShaderResources(15, 1, &none);
    ctx_->PSSetShaderResources(27, 1, &none);
    sceneRtv_ = rtv_;''')

# ---------------- camera
patch(CH, '''    void setGameStockVision(const std::string& visionText) {
        stockFog_ = gameFogFromVision(visionText);''', '''    void setGameStockVision(const std::string& visionText) {
        stockFog_ = gameFogFromVision(visionText);
        stockPostFx_ = gamePostFxFromVision(visionText);''')
patch(CH, '''    GameFog stockFog_;''', '''    GameFog stockFog_;
    GamePostFx stockPostFx_;  // the stock map's veil and vignette (research 109)''')
patch(CH, '''    void setGameAoStrength(double s) { renderer_.setGameAoStrength(float(s)); update(); }''', '''    void setGameAoStrength(double s) { renderer_.setGameAoStrength(float(s)); update(); }
    void setPostFxEnabled(bool on) { renderer_.setPostFxEnabled(on); update(); }''')
patch(CC, '''        if (stock && stockFog_.on) {''', '''        if (stock) {  // the stock map's bloom veil and vignette (research 109)
            g.veil = float(stockPostFx_.veil);
            const double aspect = height() > 0 ? double(width()) / double(height()) : 16.0 / 9.0;
            gameVignetteConstants(stockPostFx_, aspect, 1.0, g.vignette);
        }
        if (stock && stockFog_.on) {''')

# ---------------- menu toggle and its test
patch(MC, '''        toggle("GamePropLods", "Prop Levels of Detail (as in game)", "game/propLods", [this](bool on) {''',
      '''        toggle("GamePostFx", "Bloom and Vignette", "game/postFx", [this](bool on) { camera_->setPostFxEnabled(on); });
        toggle("GamePropLods", "Prop Levels of Detail (as in game)", "game/propLods", [this](bool on) {''')
patch(T, '''                                                               {"GameLightShadows", "game/lightShadows"},''',
      '''                                                               {"GameLightShadows", "game/lightShadows"},
                                                               {"GamePostFx", "game/postFx"},''')
print('ok')
