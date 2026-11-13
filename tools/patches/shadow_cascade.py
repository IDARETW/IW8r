import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b, *cnt in pairs:
        n = cnt[0] if cnt else 1
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == n, (p, s.count(a), a[:90])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


OLD_CB = 'cbuffer CB : register(b0) { row_major float4x4 viewProj; float4 eyeStart; float4 fogColHalf; float4 litParams; row_major float4x4 sunViewProj; float4 grade0; float4 gradeLight; float4 gradeDark; };'
NEW_CB = 'cbuffer CB : register(b0) { row_major float4x4 viewProj; float4 eyeStart; float4 fogColHalf; float4 litParams; row_major float4x4 sunViewProj; float4 grade0; float4 gradeLight; float4 gradeDark; row_major float4x4 sunViewProjNear; float4 sunNear; };'

OLD_SUN = '''Texture2D<float> shadowMap : register(t2);
SamplerComparisonState shadowSamp : register(s1);
float sunShadow(float3 w) {
    if (litParams.y < 0.5) return 1.0;
    float4 lp = mul(float4(w, 1.0), sunViewProj);
    float2 uv = float2(lp.x * 0.5 + 0.5, 0.5 - lp.y * 0.5);
    if (uv.x <= 0 || uv.y <= 0 || uv.x >= 1 || uv.y >= 1 || lp.z >= 1) return 1.0;
    float s = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            s += shadowMap.SampleCmpLevelZero(shadowSamp, uv + float2(x, y) / 2048.0, lp.z - litParams.z);
    return s / 9.0;
}'''
NEW_SUN = '''// Two cascades (slice 1: the box around the camera, sunViewProjNear / sunNear = bias, on; slice 0: the whole scene),
// as the game's cascaded sun shadows: small props and thin poles keep their shadows near the camera.
Texture2DArray<float> shadowMap : register(t2);
SamplerComparisonState shadowSamp : register(s1);
float shadowPcf(float2 uv, float z, float slice) {
    float s = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            s += shadowMap.SampleCmpLevelZero(shadowSamp, float3(uv + float2(x, y) / 2048.0, slice), z);
    return s / 9.0;
}
float sunShadow(float3 w) {
    if (litParams.y < 0.5) return 1.0;
    if (sunNear.y > 0.5) {
        float4 ln = mul(float4(w, 1.0), sunViewProjNear);
        float2 un = float2(ln.x * 0.5 + 0.5, 0.5 - ln.y * 0.5);
        if (un.x > 0.01 && un.y > 0.01 && un.x < 0.99 && un.y < 0.99 && ln.z < 1) return shadowPcf(un, ln.z - sunNear.x, 1.0);
    }
    float4 lp = mul(float4(w, 1.0), sunViewProj);
    float2 uv = float2(lp.x * 0.5 + 0.5, 0.5 - lp.y * 0.5);
    if (uv.x <= 0 || uv.y <= 0 || uv.x >= 1 || uv.y >= 1 || lp.z >= 1) return 1.0;
    return shadowPcf(uv, lp.z - litParams.z, 0.0);
}'''

patch('src/viewport/render.cpp', [
    (OLD_CB, NEW_CB, 2),
    (OLD_SUN, NEW_SUN, 2),
    ('''    cbd.ByteWidth = 224;  // viewProj, eye + fog start, fog colour + halfway, lit params, sun shadow viewProj, grade (3 x float4)''',
     '''    cbd.ByteWidth = 304;  // viewProj, eye + fog start, fog colour + halfway, lit params, sun shadow viewProj, grade (3 x float4),
                          // near cascade viewProj, near params'''),
    ('''        float cb[56];''', '''        float cb[76];'''),
    ('''        cb[47] = lit ? float(gradeFlags_) : 0.0f;  // lines (lit = false) keep their colours''',
     '''        cb[47] = lit ? float(gradeFlags_) : 0.0f;  // lines (lit = false) keep their colours
        std::memcpy(cb + 56, sunViewProjNear_, 64);
        cb[72] = shadowBiasNear_;
        cb[73] = nearEnabled_ ? 1.0f : 0.0f;
        cb[74] = cb[75] = 0;'''),
    # ---- the shadow pass: far and near cascades
    ('''    if (shadowOn && (shadowDirty_ || in.casterVersion != shadowCasterVersion_)) {
        // Sun shadow map (lit preview): depth of everything from the sun, drawn again only when a caster or the sun
        // changed (the camera moving and the selection do not need it).
        IW8R_PROF("d.shadow");
        ID3D11RenderTargetView* noRtv = nullptr;
        ctx_->OMSetRenderTargets(1, &noRtv, shadowDsv_);
        inShadowPass_ = true;
        ctx_->ClearDepthStencilView(shadowDsv_, D3D11_CLEAR_DEPTH, 1.0f, 0);
        D3D11_VIEWPORT svp{0, 0, float(kShadowSize), float(kShadowSize), 0, 1};
        ctx_->RSSetViewports(1, &svp);
        writeCbWith(sunViewProj_, false, false);''',
     '''    const bool farShadowDue = shadowOn && (shadowDirty_ || in.casterVersion != shadowCasterVersion_);
    const bool nearShadowDue = shadowOn && nearEnabled_ && shadowDsvNear_ && (farShadowDue || nearDirty_);
    if (farShadowDue || nearShadowDue) {
        // Sun shadow maps (lit preview, Game Lighting): depth of everything from the sun. The whole-scene cascade is drawn
        // again only when a caster or the sun changed; the near cascade also when the camera has moved its box.
        IW8R_PROF("d.shadow");
        ID3D11RenderTargetView* noRtv = nullptr;
        inShadowPass_ = true;
        D3D11_VIEWPORT svp{0, 0, float(kShadowSize), float(kShadowSize), 0, 1};
        ctx_->RSSetViewports(1, &svp);
        const auto drawCasters = [&](ID3D11DepthStencilView* sdsv, const float* sm, bool cull) {
        ctx_->OMSetRenderTargets(1, &noRtv, sdsv);
        ctx_->ClearDepthStencilView(sdsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
        writeCbWith(sm, false, false);'''),
    ('''        if (vbRef_ && refCount_ && refVisible_) {
            ctx_->IASetInputLayout(layout_);
            ctx_->VSSetShader(vs_, nullptr, 0);
            ctx_->IASetVertexBuffers(0, 1, &vbRef_, &stride, &offset);
            drawN(UINT(refCount_), 0);
        }
        if (refVisible_) drawReferenceInstances(ReferencePass::Shadow);
        shadowDirty_ = false;
        shadowCasterVersion_ = in.casterVersion;
        inShadowPass_ = false;
        ++stats_.shadowPasses;''',
     '''        if (vbRef_ && refCount_ && refVisible_) {
            ctx_->IASetInputLayout(layout_);
            ctx_->VSSetShader(vs_, nullptr, 0);
            ctx_->IASetVertexBuffers(0, 1, &vbRef_, &stride, &offset);
            if (cull && !refChunks_.empty())
                forVisibleRuns(iw8r::frustumFromViewProj(sm), refChunks_, [&](uint32_t first, uint32_t n) { drawN(n * 3, first * 3); });
            else
                drawN(UINT(refCount_), 0);
        }
        if (refVisible_) drawReferenceInstances(ReferencePass::Shadow);
        // The textured props (trees, crates, poles) cast too: their leaves through the cut-out test.
        if (refVisible_) drawReferenceTexInstancesShadow();
        };
        if (farShadowDue) drawCasters(shadowDsv_, sunViewProj_, false);
        if (nearShadowDue) drawCasters(shadowDsvNear_, sunViewProjNear_, true);
        if (farShadowDue) {
            shadowDirty_ = false;
            shadowCasterVersion_ = in.casterVersion;
            ++stats_.shadowPasses;
        }
        if (nearShadowDue) nearDirty_ = false;
        inShadowPass_ = false;'''),
    # ---- textured instances in the shadow pass
    ('''void SceneRenderer::drawReferenceInstances(ReferencePass pass) {''',
     '''void SceneRenderer::drawReferenceTexInstancesShadow() {
    if (refTexInst_.empty() || !vsTexInst_ || !layoutTexInst_) return;
    ctx_->IASetInputLayout(layoutTexInst_);
    ctx_->VSSetShader(vsTexInst_, nullptr, 0);
    ctx_->PSSetShader(psShadowClip_, nullptr, 0);  // none: solid
    if (psShadowClip_ && sampler_) ctx_->PSSetSamplers(0, 1, &sampler_);
    const UINT strides[2] = {UINT(sizeof(GpuTexVertex)), UINT(12 * sizeof(float))}, offsets[2] = {0, 0};
    for (const auto& g : refTexInst_) {
        if (!g.vb || !g.inst) continue;
        if (psShadowClip_) {
            const int id = g.texture >= 0 ? g.texture : whiteTex_;
            if (id < 0 || size_t(id) >= textures_.size()) continue;
            ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]);
        }
        ID3D11Buffer* bufs[2] = {g.vb, g.inst};
        ctx_->IASetVertexBuffers(0, 2, bufs, strides, offsets);
        drawInst(g.vertices, g.instances, 0, 0);
    }
    ID3D11Buffer* none[2] = {nullptr, nullptr};
    ctx_->IASetVertexBuffers(0, 2, none, strides, offsets);
    ID3D11ShaderResourceView* noTex[1] = {nullptr};
    ctx_->PSSetShaderResources(0, 1, noTex);
    ctx_->IASetInputLayout(layout_);
    ctx_->VSSetShader(vs_, nullptr, 0);
    ctx_->PSSetShader(nullptr, nullptr, 0);
}

void SceneRenderer::drawReferenceInstances(ReferencePass pass) {'''),
    # ---- resources: a two-slice array
    ('''    if (shadowSrv_ && shadowDsv_ && cmpSampler_) return true;''',
     '''    if (shadowSrv_ && shadowDsv_ && shadowDsvNear_ && cmpSampler_) return true;'''),
    ('''    td.Width = td.Height = kShadowSize;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32_TYPELESS;''', '''    td.Width = td.Height = kShadowSize;
    td.MipLevels = 1;
    td.ArraySize = 2;  // 0: the whole scene, 1: the near cascade
    td.Format = DXGI_FORMAT_R32_TYPELESS;'''),
    ('''    dd.Format = DXGI_FORMAT_D32_FLOAT;
    dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R32_FLOAT;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;''', '''    dd.Format = DXGI_FORMAT_D32_FLOAT;
    dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
    dd.Texture2DArray.FirstArraySlice = 0;
    dd.Texture2DArray.ArraySize = 1;
    D3D11_DEPTH_STENCIL_VIEW_DESC ddNear = dd;
    ddNear.Texture2DArray.FirstArraySlice = 1;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R32_FLOAT;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    sd.Texture2DArray.MipLevels = 1;
    sd.Texture2DArray.ArraySize = 2;'''),
    ('''    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &shadowTex_)) || FAILED(dev_->CreateDepthStencilView(shadowTex_, &dd, &shadowDsv_)) ||''',
     '''    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &shadowTex_)) || FAILED(dev_->CreateDepthStencilView(shadowTex_, &dd, &shadowDsv_)) ||
        FAILED(dev_->CreateDepthStencilView(shadowTex_, &ddNear, &shadowDsvNear_)) ||'''),
    ('''    safeRelease(shadowDsv_);''', '''    safeRelease(shadowDsv_);
    safeRelease(shadowDsvNear_);'''),
    # ---- setters and the cascade matrix
    ('''void sunShadowMatrix(const double toward[3], const double mins[3], const double maxs[3], float out[16]) {
    // Orthographic view from the sun over the box: x / y across, depth along the light, D3D z in [0, 1] (row vectors).
    const double c[3] = {(mins[0] + maxs[0]) / 2, (mins[1] + maxs[1]) / 2, (mins[2] + maxs[2]) / 2};
    const double ex[3] = {(maxs[0] - mins[0]) / 2, (maxs[1] - mins[1]) / 2, (maxs[2] - mins[2]) / 2};
    const double R = std::max(1.0, std::sqrt(ex[0] * ex[0] + ex[1] * ex[1] + ex[2] * ex[2]));''',
     '''void SceneRenderer::setSunShadowNear(bool enabled, const float sunViewProj[16], float bias) {
    if (enabled && (!nearEnabled_ || std::memcmp(sunViewProj, sunViewProjNear_, sizeof(sunViewProjNear_)) != 0)) nearDirty_ = true;
    nearEnabled_ = enabled;
    std::memcpy(sunViewProjNear_, sunViewProj, sizeof(sunViewProjNear_));
    shadowBiasNear_ = bias;
}

void sunShadowMatrix(const double toward[3], const double mins[3], const double maxs[3], float out[16]) {
    const double c[3] = {(mins[0] + maxs[0]) / 2, (mins[1] + maxs[1]) / 2, (mins[2] + maxs[2]) / 2};
    const double ex[3] = {(maxs[0] - mins[0]) / 2, (maxs[1] - mins[1]) / 2, (maxs[2] - mins[2]) / 2};
    const double R = std::max(1.0, std::sqrt(ex[0] * ex[0] + ex[1] * ex[1] + ex[2] * ex[2]));
    sunShadowMatrixAround(toward, c, R, R, out);
}

void sunShadowMatrixAround(const double toward[3], const double c[3], double R, double depthRadius, float out[16]) {
    // Orthographic view from the sun over a sphere: x / y across (radius R), depth along the light (2 x depthRadius
    // before and after the centre), D3D z in [0, 1] (row vectors).
    R = std::max(1.0, R);
    const double D = std::max(R, depthRadius);'''),
    ('''    const double eye[3] = {c[0] - f[0] * 2 * R, c[1] - f[1] * 2 * R, c[2] - f[2] * 2 * R};
    auto dot3 = [](const double* a, const double* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    const double depth = 4 * R;''', '''    const double eye[3] = {c[0] - f[0] * 2 * D, c[1] - f[1] * 2 * D, c[2] - f[2] * 2 * D};
    auto dot3 = [](const double* a, const double* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    const double depth = 4 * D;'''),
])

patch('src/viewport/render.h', [
    ('''void sunShadowMatrix(const double toward[3], const double mins[3], const double maxs[3], float out[16]);''',
     '''void sunShadowMatrix(const double toward[3], const double mins[3], const double maxs[3], float out[16]);
// The same around a centre: radius R across the light, depth +-2 x depthRadius along it (the near shadow cascade covers a
// small box but takes casters from the whole scene).
void sunShadowMatrixAround(const double toward[3], const double centre[3], double R, double depthRadius, float out[16]);'''),
    ('''    void setSunShadow(bool enabled, const float sunViewProj[16], float bias);''',
     '''    void setSunShadow(bool enabled, const float sunViewProj[16], float bias);
    // The near cascade (a box around the camera at the same 2048 texels): sharp shadows of small props and poles.
    void setSunShadowNear(bool enabled, const float sunViewProj[16], float bias);'''),
    ('''    ID3D11DepthStencilView* shadowDsv_ = nullptr;''', '''    ID3D11DepthStencilView* shadowDsv_ = nullptr;
    ID3D11DepthStencilView* shadowDsvNear_ = nullptr;
    float sunViewProjNear_[16] = {};
    float shadowBiasNear_ = 0.0005f;
    bool nearEnabled_ = false, nearDirty_ = true;
    void drawReferenceTexInstancesShadow();'''),
])

patch('src/qtapp/camera_view.cpp', [
    ('''            const double texel = 2.0 * R / 2048.0;
            renderer_.setSunShadow(true, m, float((2.0 + 1.5 * texel) / (4.0 * R)));''',
     '''            const double texel = 2.0 * R / 2048.0;
            renderer_.setSunShadow(true, m, float((2.0 + 1.5 * texel) / (4.0 * R)));
            // The near cascade: a box of kNearRadius around the view ahead of the camera, moved in steps of an eighth of
            // it (so it is not drawn again on every small move), when the scene is much bigger than that box.
            constexpr double kNearRadius = 1400.0;
            if (R > 2.5 * kNearRadius) {
                const Vec3 fwd = ctx_->cam.forward();
                Vec3 flat{fwd.x, fwd.y, 0};
                flat = length(flat) > 1e-6 ? normalize(flat) : Vec3{1, 0, 0};
                Vec3 c = ctx_->cam.pos + flat * (0.55 * kNearRadius);
                const double step = kNearRadius / 8;
                c = Vec3{std::round(c.x / step) * step, std::round(c.y / step) * step, std::round(c.z / step) * step};
                const double cc[3] = {c.x, c.y, c.z};
                const double depthR = std::max(kNearRadius, R + length(c - sb.center()));
                float mn[16];
                app::sunShadowMatrixAround(t, cc, kNearRadius, depthR, mn);
                const double texelN = 2.0 * kNearRadius / 2048.0;
                renderer_.setSunShadowNear(true, mn, float((1.0 + 1.5 * texelN) / (4.0 * depthR)));
            } else {
                renderer_.setSunShadowNear(false, m, 0);
            }'''),
])
print('ok')
