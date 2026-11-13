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


# ---------------------------------------------------------------- core: SSR techsets (research 104 section 5)
patch('src/core/stock_decal.cpp', '''    t.ssr = (b & 0xF) == 2;''', '''    t.ssr = (b & 0x2) != 0;  // hexB bit 1: "...2" and the glass "...3" (research 104 section 5)''')
patch('src/core/stock_decal.h', '''    bool ssr = false;            // hexB last digit 2: screen-space reflection lerped over the probe reflection''',
      '''    bool ssr = false;            // hexB bit 1 (last digit 2 or 3): screen-space reflection over the probe (research 104)''')
patch('tests/stock_decal_tests.cpp', '''    t = parseStockTechsetName("tw/lit_3_lit_bad_ta1_815002_1002000000000012_0_1_1_0_0_113810015b_0_0_1_0_0");
    CHECK(t.valid && t.blended && t.ssr && !t.displacement);''', '''    t = parseStockTechsetName("tw/lit_3_lit_bad_ta1_815002_1002000000000012_0_1_1_0_0_113810015b_0_0_1_0_0");
    CHECK(t.valid && t.blended && t.ssr && !t.displacement);
    // The glass set ending in 3 traces too (research 104 section 5); 8 (displacement) does not.
    t = parseStockTechsetName("tw/lit_3_lit_bad_ta1_815002_1002000000000013_0_1_1_0_0_113810015b_0_0_1_0_0");
    CHECK(t.valid && t.ssr);
    t = parseStockTechsetName("tw/lit_3_lit_bad_ta1_815002_1002000000000018_0_1_1_0_0_113810015b_0_0_1_0_0");
    CHECK(t.valid && !t.ssr && t.displacement);''')

# ---------------------------------------------------------------- camera / main window: the flag per material
patch('src/qtapp/camera_view.h', '''        int decalSortKey = 0;
        int tint = -1;''', '''        int decalSortKey = 0;
        bool ssr = false;  // its techset traces screen-space reflections (research 104)
        int tint = -1;''')
patch('src/qtapp/main_window.cpp', '''        f.tint = e->tint;
        f.tintMasked = e->tintMasked;''', '''        f.ssr = parseStockTechsetName(e->techset.toStdString()).ssr;
        f.tint = e->tint;
        f.tintMasked = e->tintMasked;''')
C = 'src/qtapp/camera_view.cpp'
patch(C, '''    const auto f = factsLookup_(m);
    if (!f || f->constants.layer.empty()) return std::nullopt;
    const auto& c = f->constants.layer[0];''', '''    const auto f = factsLookup_(m);
    if (f && f->ssr && f->constants.layer.empty())  // no constants, but it traces SSR (layerP.z)
        return std::array<float, 16>{0.0f, 0.0f, 1.0f, 0.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    if (!f || f->constants.layer.empty()) return std::nullopt;
    const auto& c = f->constants.layer[0];''')
patch(C, '''    return std::array<float, 16>{0.0f, !masked && !blended ? 1.0f : 0.0f, 0.0f, 0.0f, c.specScale, c.specBias, c.glossScale, c.glossBias,''',
      '''    return std::array<float, 16>{0.0f, !masked && !blended ? 1.0f : 0.0f, f->ssr ? 1.0f : 0.0f, 0.0f, c.specScale, c.specBias, c.glossScale, c.glossBias,''')
# layers: layerTint.w = SSR
patch(C, '''    const float p[16] = {1.0f, float(lc.flags), lc.sharpness, lc.colorMode == 1 ? 1.0f : 0.0f, lc.tint[0], lc.tint[1], lc.tint[2], 0.0f,
                         lc.uvCos, lc.uvSin, lc.uvScale[0], lc.uvScale[1], lc.uvOffset[0], lc.uvOffset[1], 0.0f, 0.0f};
    renderer_.setReferenceTexture(k, cid);''', '''    const bool layerSsr = factsLookup_ && [&] { const auto f = factsLookup_(base); return f && f->ssr; }();
    const float p[16] = {1.0f, float(lc.flags), lc.sharpness, lc.colorMode == 1 ? 1.0f : 0.0f, lc.tint[0], lc.tint[1], lc.tint[2], layerSsr ? 1.0f : 0.0f,
                         lc.uvCos, lc.uvSin, lc.uvScale[0], lc.uvScale[1], lc.uvOffset[0], lc.uvOffset[1], 0.0f, 0.0f};
    renderer_.setReferenceTexture(k, cid);''')
patch(C, '''            const float p[16] = {1.0f, float(lc.flags), lc.sharpness, lc.colorMode == 1 ? 1.0f : 0.0f, lc.tint[0], lc.tint[1], lc.tint[2], 0.0f,
                                 lc.uvCos, lc.uvSin, lc.uvScale[0], lc.uvScale[1], lc.uvOffset[0], lc.uvOffset[1], 0.0f, 0.0f};''',
      '''            const float p[16] = {1.0f, float(lc.flags), lc.sharpness, lc.colorMode == 1 ? 1.0f : 0.0f, lc.tint[0], lc.tint[1], lc.tint[2], f->ssr ? 1.0f : 0.0f,
                                 lc.uvCos, lc.uvSin, lc.uvScale[0], lc.uvScale[1], lc.uvOffset[0], lc.uvOffset[1], 0.0f, 0.0f};''')
patch(C, '''    const float p[16] = {1.0f, float(lc.flags), lc.sharpness, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f,
                         1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};  // layer 0: its tint is in the image already''',
      '''    const float p[16] = {1.0f, float(lc.flags), lc.sharpness, 0.0f, 1.0f, 1.0f, 1.0f, f->ssr ? 1.0f : 0.0f,
                         1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};  // layer 0: its tint is in the image already''')

# ---------------------------------------------------------------- renderer
H = 'src/viewport/render.h'
R = 'src/viewport/render.cpp'
patch(H, '''    bool hdrFrame_ = false;
    void drawPost();''', '''    bool hdrFrame_ = false;
    void drawPost();
    // Screen-space reflections (research 104 section 8): this frame's HDR scene (half size, 4 mips) and linear depth
    // (half size) kept for the next frame's trace in ps_tex, with the view they were drawn from (b10).
    ID3D11Texture2D* prevColorTex_ = nullptr;
    ID3D11RenderTargetView* prevColorRtv_ = nullptr;
    ID3D11ShaderResourceView* prevColorSrv_ = nullptr;
    ID3D11Texture2D* prevZTex_ = nullptr;
    ID3D11RenderTargetView* prevZRtv_ = nullptr;
    ID3D11ShaderResourceView* prevZSrv_ = nullptr;
    ID3D11PixelShader* prevColorPs_ = nullptr;
    ID3D11PixelShader* prevZPs_ = nullptr;
    ID3D11Buffer* cbPrev_ = nullptr;
    float prevViewProj_[16] = {};
    bool prevValid_ = false;   // the copies hold a usable previous frame
    bool ssrOn_ = true;
    void keepFrameForSsr(const float* viewProj);''')
patch(H, '''    bool gameHdrDrawn() const { return hdrFrame_; }''', '''    bool gameHdrDrawn() const { return hdrFrame_; }
    // View > Game Lighting: screen-space reflections on the stock materials that trace them (on by default).
    void setGameSsr(bool on) { ssrOn_ = on; }
    bool gameSsr() const { return ssrOn_; }''')
patch(R, '''Texture2D<float4> hdrScene : register(t15);''', '''Texture2D<float4> hdrScene : register(t15);
// Screen-space reflections (research 104 section 8): the previous frame's HDR scene (half size, mips) and its linear
// depth, with the view they were drawn from.
Texture2D<float4> prevColor : register(t16);
Texture2D<float> prevZ : register(t17);
cbuffer Prev : register(b10) { row_major float4x4 prevVP; float4 prevFwd; float4 prevSize; };  // prevSize: w, h, valid''')
# the trace and the combine in the stock specular
patch(R, '''            const float3 aoMB = aoV / (1.0 - min(diffAlbedo, 0.85) * (1.0 - aoV));  // research 95: multi-bounce AO
            c = diffAlbedo * indirect * kd * (1.0 - fmax) * aoMB + Fenv * S;''', '''            const float3 aoMB = aoV / (1.0 - min(diffAlbedo, 0.85) * (1.0 - aoV));  // research 95: multi-bounce AO
            // Screen-space reflections (research 104): materials that trace them (base: layerP.z, layers: layerTint.w),
            // glossy pixels, the previous frame's scene marched along the reflection of the vertex normal, the hit
            // read on the normal-mapped ray with a roughness cone; S -> lerp(S, ssr, w).
            const bool ssrMat = layerP.x < 0.5 ? layerP.z > 0.5 : (layerP.x < 1.5 && layerTint.w > 0.5);
            if (ssrMat && prevSize.z > 0.5 && probeCount.x > 0.5 && gp >= 0.3) {
                const float3 X = i.w, Ng = Nf, Vv = Vs;
                const float3 Rg = reflect(-Vv, Ng);
                const float wF = saturate(4.0 * dot(prevFwd.xyz, R));
                if (wF > 0.0 && dot(Rg, -Vv) > 0.0 && dot(prevFwd.xyz, Rg) > 0.0) {
                    const float4 ca = mul(float4(X, 1.0), prevVP), ce = mul(float4(X + 65500.0 * Rg, 1.0), prevVP);
                    float3 A = float3(ca.xy / ca.w * float2(0.5, -0.5) + 0.5, 1.0 / ca.w);
                    const float3 E = float3(ce.xy / ce.w * float2(0.5, -0.5) + 0.5, 1.0 / ce.w);
                    const float3 D = E - A;
                    const float len = length(D.xy * prevSize.xy);
                    A.xy += D.xy * (1.0 / max(len, 1e-3));  // one pixel out
                    float zPrev = 1.0 / A.z, tHit = 1.0;
                    bool hit = false;
                    [loop] for (int k = 0; k < 24; ++k) {
                        float tk = (k + 0.5) / 24.0;
                        tk *= tk;
                        const float3 Q = A + tk * D;
                        if (any(abs(Q.xy - 0.5) >= 0.5)) break;
                        const float zr = 1.0 / Q.z;
                        const float zs = prevZ.Load(int3(int2(Q.xy * prevSize.xy), 0));
                        if (zs * 1.002 < zr && abs(zr - zs) < max(abs(zr - zPrev), 48.0) && tk > 1.0 / len) {
                            hit = true, tHit = tk;
                            break;
                        }
                        zPrev = zr;
                    }
                    if (hit && tHit < 0.9999) {
                        const float rr = 1.0 - gp;
                        const float cone = max(0.0, min(1.01737 * rr * rr * rr + 0.070128, 1.0) * tHit + 1.48707 * rr - 0.284052);
                        const float4 cr = mul(float4(X + 65500.0 * R, 1.0), prevVP);
                        const float2 uvR = cr.w > 0.0 ? cr.xy / cr.w * float2(0.5, -0.5) + 0.5 : A.xy;
                        const float2 uvH = lerp(A.xy, uvR, tHit);
                        const float2 e2 = uvH * 2.0 - 1.0;
                        const float edge = saturate((1.0 - e2.x * e2.x) * (1.0 - e2.y * e2.y) * 2.0 * prevSize.x / max(prevSize.y, 1.0));
                        const float4 sc = prevColor.SampleLevel(lutSamp, uvH, 3.0 * min(cone, 1.0));
                        if (sc.a > 0.0) S = lerp(S, sc.rgb, wF * edge);
                    }
                }
            }
            c = diffAlbedo * indirect * kd * (1.0 - fmax) * aoMB + Fenv * S;''')
# the copy passes (in the AO shader source: they read the depth and the HDR scene)
patch(R, '''float viewDist(int2 p) {''', '''// Kept for the next frame's screen-space reflections: the linear view depth (clip w; 1e9 for the sky) at half size,
// the farther of each 2 x 2 (the game's floatZ max chain), and the HDR scene averaged over 2 x 2.
Texture2D<float4> hdrIn : register(t2);
float ps_prevz(V i) : SV_TARGET {
    int2 p = int2(i.pos.xy) * 2;
    float w = 0.0;
    [unroll] for (int y = 0; y < 2; ++y)
        [unroll] for (int x = 0; x < 2; ++x) {
            const int2 q = clamp(p + int2(x, y), int2(0, 0), int2(aoPrm.zw) - 1);
            const float d = depthTex.Load(int3(q, 0));
            float wl = 1e9;
            if (d < 0.999999) wl = mul(float4(worldAt((float2(q) + 0.5) / aoPrm.zw, d), 1.0), aoVp).w;
            w = max(w, wl);
        }
    return w;
}
float4 ps_prevcolor(V i) : SV_TARGET {
    int2 p = int2(i.pos.xy) * 2;
    float4 sum = 0.0;
    float n = 0.0;
    [unroll] for (int y = 0; y < 2; ++y)
        [unroll] for (int x = 0; x < 2; ++x) {
            const float4 c = hdrIn.Load(int3(clamp(p + int2(x, y), int2(0, 0), int2(aoPrm.zw) - 1), 0));
            if (c.a > 0.0) sum += float4(c.rgb, 1.0), n += 1.0;
        }
    return n > 0.0 ? float4(sum.rgb / n, 1.0) : float4(0, 0, 0, 0);
}
float viewDist(int2 p) {''')
patch(R, '''    if (compile("vs_full", "vs_5_0", vsb) && compile("ps_ao", "ps_5_0", psb) && compile("ps_ao_blur", "ps_5_0", pbb)) {
        dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &aoVs_);
        dev_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &aoPs_);
        dev_->CreatePixelShader(pbb->GetBufferPointer(), pbb->GetBufferSize(), nullptr, &aoBlurPs_);
    }''', '''    if (compile("vs_full", "vs_5_0", vsb) && compile("ps_ao", "ps_5_0", psb) && compile("ps_ao_blur", "ps_5_0", pbb)) {
        dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &aoVs_);
        dev_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &aoPs_);
        dev_->CreatePixelShader(pbb->GetBufferPointer(), pbb->GetBufferSize(), nullptr, &aoBlurPs_);
        ID3DBlob *zb = nullptr, *cb = nullptr;
        if (compile("ps_prevz", "ps_5_0", zb)) dev_->CreatePixelShader(zb->GetBufferPointer(), zb->GetBufferSize(), nullptr, &prevZPs_);
        if (compile("ps_prevcolor", "ps_5_0", cb)) dev_->CreatePixelShader(cb->GetBufferPointer(), cb->GetBufferSize(), nullptr, &prevColorPs_);
        safeRelease(zb), safeRelease(cb);
    }''')
patch(R, '''    safeRelease(aoVs_), safeRelease(aoPs_), safeRelease(aoBlurPs_), safeRelease(cbAo_);''',
      '''    safeRelease(aoVs_), safeRelease(aoPs_), safeRelease(aoBlurPs_), safeRelease(cbAo_);
    safeRelease(prevZPs_), safeRelease(prevColorPs_), safeRelease(cbPrev_);''')
# targets at half size
patch(R, '''    td.Format = DXGI_FORMAT_R8_UNORM;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    for (int k = 0; k < 2; ++k)''', '''    {
        D3D11_TEXTURE2D_DESC hd = td;
        hd.Width = std::max(1u, td.Width / 2), hd.Height = std::max(1u, td.Height / 2);
        hd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        hd.MipLevels = 4;
        hd.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        hd.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        if (SUCCEEDED(dev_->CreateTexture2D(&hd, nullptr, &prevColorTex_))) {
            D3D11_RENDER_TARGET_VIEW_DESC rd{};
            rd.Format = hd.Format;
            rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            dev_->CreateRenderTargetView(prevColorTex_, &rd, &prevColorRtv_);
            dev_->CreateShaderResourceView(prevColorTex_, nullptr, &prevColorSrv_);
        }
        hd.Format = DXGI_FORMAT_R32_FLOAT;
        hd.MipLevels = 1;
        hd.MiscFlags = 0;
        if (SUCCEEDED(dev_->CreateTexture2D(&hd, nullptr, &prevZTex_))) {
            dev_->CreateRenderTargetView(prevZTex_, nullptr, &prevZRtv_);
            dev_->CreateShaderResourceView(prevZTex_, nullptr, &prevZSrv_);
        }
    }
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    for (int k = 0; k < 2; ++k)''')
patch(R, '''    safeRelease(hdrSrv_), safeRelease(hdrRtv_), safeRelease(hdrTex_);''', '''    safeRelease(hdrSrv_), safeRelease(hdrRtv_), safeRelease(hdrTex_);
    safeRelease(prevColorSrv_), safeRelease(prevColorRtv_), safeRelease(prevColorTex_);
    safeRelease(prevZSrv_), safeRelease(prevZRtv_), safeRelease(prevZTex_);
    prevValid_ = false;''')
# this frame's constants for the trace (b10) and the textures, bound before the lit passes
patch(R, '''    sceneRtv_ = hdrFrame_ ? hdrRtv_ : rtv_;''', '''    sceneRtv_ = hdrFrame_ ? hdrRtv_ : rtv_;
    {
        // The previous frame for the reflections: only an HDR frame of the same size, with SSR on.
        static const bool noSsr = std::getenv("IW8R_NO_SSR") != nullptr;
        const bool ssrNow = hdrFrame_ && prevValid_ && ssrOn_ && !noSsr && prevColorSrv_ && prevZSrv_;
        if (!cbPrev_ && dev_) {
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = 96;
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            dev_->CreateBuffer(&bd, nullptr, &cbPrev_);
        }
        if (cbPrev_) {
            float pc[24] = {};
            std::memcpy(pc, prevViewProj_, 64);
            // The previous camera's forward axis: the w column of its view-projection (row-vector convention).
            float fx = prevViewProj_[3], fy = prevViewProj_[7], fz = prevViewProj_[11];
            const float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
            if (fl > 0) fx /= fl, fy /= fl, fz /= fl;
            pc[16] = fx, pc[17] = fy, pc[18] = fz;
            pc[20] = float(std::max(1, w_ / 2)), pc[21] = float(std::max(1, h_ / 2)), pc[22] = ssrNow ? 1.0f : 0.0f;
            ctx_->UpdateSubresource(cbPrev_, 0, nullptr, pc, 0, 0);
            ctx_->PSSetConstantBuffers(10, 1, &cbPrev_);
        }
        ID3D11ShaderResourceView* ps[2] = {ssrNow ? prevColorSrv_ : nullptr, ssrNow ? prevZSrv_ : nullptr};
        ctx_->PSSetShaderResources(16, 2, ps);
    }''')
# keep this frame for the next one: after the lit scene, before the post pass
patch(R, '''    if (hdrFrame_) {
        // The lit scene is done: the post chain into the frame, then the unlit pieces and the editor's overlays.
        drawPost();''', '''    if (hdrFrame_) {
        // The lit scene is done: kept for the next frame's reflections, the post chain into the frame, then the unlit
        // pieces and the editor's overlays.
        keepFrameForSsr(viewProj);
        drawPost();''')
patch(R, '''void SceneRenderer::drawPost() {''', '''void SceneRenderer::keepFrameForSsr(const float* viewProj) {
    if (!prevColorRtv_ || !prevZRtv_ || !prevZPs_ || !prevColorPs_ || !depthSrv_ || !hdrSrv_ || !createAoResources()) {
        prevValid_ = false;
        return;
    }
    IW8R_PROF("d.ssr_keep");
    float cb[40];
    std::memcpy(cb, viewProj, 64);
    if (!invert4(viewProj, cb + 16)) {
        prevValid_ = false;
        return;
    }
    cb[32] = fog_[0], cb[33] = fog_[1], cb[34] = fog_[2], cb[35] = 0;
    cb[36] = 40.0f, cb[37] = 1.0f, cb[38] = float(w_), cb[39] = float(h_);
    ctx_->UpdateSubresource(cbAo_, 0, nullptr, cb, 0, 0);
    ID3D11ShaderResourceView* none4[4] = {nullptr, nullptr, nullptr, nullptr};
    ctx_->PSSetShaderResources(16, 2, none4);  // this frame's inputs become the outputs
    D3D11_VIEWPORT hv{0, 0, float(std::max(1, w_ / 2)), float(std::max(1, h_ / 2)), 0, 1};
    ctx_->RSSetViewports(1, &hv);
    ctx_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx_->OMSetDepthStencilState(dsOff_, 0);
    ctx_->RSSetState(rsTris_);
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(aoVs_, nullptr, 0);
    ctx_->PSSetConstantBuffers(8, 1, &cbAo_);
    // linear depth (reads the depth buffer: no depth bound)
    ctx_->OMSetRenderTargets(1, &prevZRtv_, nullptr);
    ctx_->PSSetShader(prevZPs_, nullptr, 0);
    ID3D11ShaderResourceView* zin[3] = {depthSrv_, nullptr, nullptr};
    ctx_->PSSetShaderResources(0, 3, zin);
    ctx_->Draw(3, 0);
    // the scene, then its mips
    ctx_->OMSetRenderTargets(1, &prevColorRtv_, nullptr);
    ctx_->PSSetShader(prevColorPs_, nullptr, 0);
    ID3D11ShaderResourceView* cin[3] = {depthSrv_, nullptr, hdrSrv_};
    ctx_->PSSetShaderResources(0, 3, cin);
    ctx_->Draw(3, 0);
    ctx_->PSSetShaderResources(0, 3, none4);
    ctx_->OMSetRenderTargets(1, &sceneRtv_, nullptr);
    ctx_->GenerateMips(prevColorSrv_);
    D3D11_VIEWPORT fv{0, 0, float(w_), float(h_), 0, 1};
    ctx_->RSSetViewports(1, &fv);
    std::memcpy(prevViewProj_, viewProj, 64);
    prevValid_ = true;
}

void SceneRenderer::drawPost() {''')
# a frame that is not HDR breaks the chain
patch(R, '''    hdrFrame_ = game_.on && gameLutSrv_ && hdrRtv_ && postVs_ && postPs_ && !noHdr && !in.capture;''',
      '''    hdrFrame_ = game_.on && gameLutSrv_ && hdrRtv_ && postVs_ && postPs_ && !noHdr && !in.capture;
    if (!hdrFrame_) prevValid_ = false;''')
print('ok')
