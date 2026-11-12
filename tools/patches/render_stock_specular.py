import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/viewport/render.cpp', [
 ('''Texture3D gameLut : register(t9);''', '''Texture3D gameLut : register(t9);
// The stock world shader's ggx_env_brdf_lut (A, B by N.V across and gloss down; research 95), when the editor has it.
Texture2D envBrdf : register(t10);
float2 gameEnvBrdf(float nv, float g) {
    if (game0.w > 0.5) return envBrdf.SampleLevel(lutSamp, float2(nv, g) * (63.0 / 64.0) + 0.5 / 64.0, 0).rg;
    // Without the table: Karis's fit of the split-sum GGX table (roughness 1 - g).
    float4 r = float4(-1, -0.0275, -0.572, 0.022) * (1.0 - g) + float4(1, 0.0425, 1.04, -0.04);
    float a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}'''),
 ('''        float3 indirect = gameSky.rgb * (0.6 + 0.4 * saturate(N.z));
        float visibility = 1.0;''', '''        float3 indirect = gameSky.rgb * (0.6 + 0.4 * saturate(N.z));
        float3 stockDw = Nf, stockB = 0;  // the stock lightmap's directional part, for E(R) below
        float visibility = 1.0;'''),
 ('''                float3 Dw = normalize(T * s * d.x + B * s * d.y + Nf * d.z + 1e-6);
                indirect = a.rgb + b * saturate(dot(N, Dw));''', '''                float3 Dw = normalize(T * s * d.x + B * s * d.y + Nf * d.z + 1e-6);
                indirect = a.rgb + b * saturate(dot(N, Dw));
                stockDw = Dw, stockB = b;'''),
 ('''        if (litParams.y > 0.5) visibility = sunShadow(i.w);
        float3 light = indirect + visibility * saturate(dot(N, gameSun.xyz)) * gameSunCol.rgb;
        float3 c = albedo * light;''', '''        if (litParams.y > 0.5) visibility = sunShadow(i.w);
        float3 c;
        if (stockRef) {
            // The stock world PS (research 95 sections 2 "Ambient combine" and "Sun"): a dielectric (F0 .04) of the
            // material's default gloss (the reference has no gloss maps here), retro-reflective diffuse kd, the
            // environment term from the lightmap along the reflection (probes later) and the GGX sun.
            float3 Vs = normalize(eyeStart.xyz - i.w);
            const float3 f0 = 0.04;
            const float gp = 1.0 - sqrt(1.0 - 0.3);
            float a2 = 2.0 / (2.0 + exp2(20.0 * gp));
            float nv = saturate(dot(N, Vs));
            float2 ab = gameEnvBrdf(nv, gp);
            float3 Fenv = f0 * ab.x + ab.y;
            float3 R = reflect(-Vs, N);
            float3 Lr = (indirect - stockB * saturate(dot(N, stockDw))) + stockB * saturate(dot(R, stockDw));
            float tq = ((-0.442435 * gp + 1.7905) * gp - 2.24983) * gp + 0.948867;
            float cq = sqrt(0.5 * nv + 0.5);
            float fq = 1.05 * (1.0 - pow(1.0 - nv, 5.0));
            float kd = lerp(fq, cq * (cq + min(4.0 * tq * tq, 1.0)) * max(1.25 - 1.6 * tq, 0.55 - 0.2 * tq), tq) + 0.38 * tq * tq;
            float fmax = max(Fenv.r, max(Fenv.g, Fenv.b));
            c = albedo * indirect * kd * (1.0 - fmax) + Fenv * Lr;
            float3 Ls = gameSun.xyz;
            float nl = dot(N, Ls);
            if (nl > 0) {
                float3 H = normalize(Ls + Vs);
                float nh = saturate(dot(N, H));
                float dd = nh * nh * (a2 - 1.0) + 1.0;
                float Dg = a2 / max(dd * dd, 1e-8);
                float Vg = 1.0 / ((abs(nl) + sqrt(a2 + (1.0 - a2) * nl * nl)) * (nv + sqrt(a2 + (1.0 - a2) * nv * nv)));
                float3 F = f0 + (1.0 - f0) * pow(1.0 - abs(dot(Vs, H)), 5.0);
                float2 abl = gameEnvBrdf(saturate(nl), gp);
                float3 fl = f0 * abl.x + abl.y;
                c += gameSunCol.rgb * visibility * saturate(nl) *
                     (albedo * (1.0 - max(fl.r, max(fl.g, fl.b))) + F * Dg * Vg * 1.0390625);
            }
        } else {
            float3 light = indirect + visibility * saturate(dot(N, gameSun.xyz)) * gameSunCol.rgb;
            c = albedo * light;
        }'''),
 # game0.w = env BRDF bound; bind t10 in writeGameCb
 ('''    const float g[16] = {on ? 1.0f : 0.0f, game_.lutScale, on && gameLmSrv_[0] ? 1.0f : 0.0f, 0,''',
  '''    const float g[16] = {on ? 1.0f : 0.0f, game_.lutScale, on && gameLmSrv_[0] ? 1.0f : 0.0f, envBrdfSrv_ ? 1.0f : 0.0f,'''),
 ('''    ID3D11ShaderResourceView* gl = gameLutSrv_;
    ctx_->PSSetShaderResources(9, 1, &gl);''', '''    ID3D11ShaderResourceView* gl[2] = {gameLutSrv_, envBrdfSrv_};
    ctx_->PSSetShaderResources(9, 2, gl);'''),
 ('''    for (int k = 0; k < 3; ++k) safeRelease(stockLmSrv_[k]), safeRelease(stockLmTex_[k]);
    safeRelease(meterSrv_)''', '''    for (int k = 0; k < 3; ++k) safeRelease(stockLmSrv_[k]), safeRelease(stockLmTex_[k]);
    safeRelease(envBrdfSrv_), safeRelease(envBrdfTex_);
    safeRelease(meterSrv_)'''),
 ('''void SceneRenderer::writeGameCb() {''', '''void SceneRenderer::setEnvBrdfLut(int width, int height, const uint8_t* rg) {
    safeRelease(envBrdfSrv_), safeRelease(envBrdfTex_);
    if (!dev_ || width < 2 || height < 2 || !rg) return;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = UINT(width), td.Height = UINT(height);
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{rg, UINT(width) * 2, 0};
    if (FAILED(dev_->CreateTexture2D(&td, &init, &envBrdfTex_)) || FAILED(dev_->CreateShaderResourceView(envBrdfTex_, nullptr, &envBrdfSrv_)))
        safeRelease(envBrdfTex_);
}

void SceneRenderer::writeGameCb() {'''),
])
patch('src/viewport/render.h', [
 ('''    bool hasStockLightmap() const { return stockLmSrv_[0] != nullptr; }''', '''    bool hasStockLightmap() const { return stockLmSrv_[0] != nullptr; }
    // The stock world shader's environment BRDF table (64 x 64, A and B as R8G8; game_look_assets.h fetches it from the
    // owner's install). null = the analytic fit.
    void setEnvBrdfLut(int width, int height, const uint8_t* rg);
    bool hasEnvBrdfLut() const { return envBrdfSrv_ != nullptr; }'''),
 ('''    ID3D11ShaderResourceView* stockLmSrv_[3] = {nullptr, nullptr, nullptr};''', '''    ID3D11ShaderResourceView* stockLmSrv_[3] = {nullptr, nullptr, nullptr};
    ID3D11Texture2D* envBrdfTex_ = nullptr;
    ID3D11ShaderResourceView* envBrdfSrv_ = nullptr;'''),
])

# camera view: fetch the table once in the background when Game Lighting is first on
patch('src/qtapp/camera_view.h', [
 ('''    bool hasStockLightmap() const { return renderer_.hasStockLightmap(); }''', '''    bool hasStockLightmap() const { return renderer_.hasStockLightmap(); }
    // Tool paths for game data Game Lighting reads from the install (game_look_assets.h); set by the main window.
    void setToolPathSource(std::function<QString(const QString&)> fn) { toolPathFn_ = std::move(fn); }
    bool hasEnvBrdfLut() const { return renderer_.hasEnvBrdfLut(); }'''),
 ('''    std::string gameStockMap_;''', '''    std::string gameStockMap_;
    std::function<QString(const QString&)> toolPathFn_;
    bool envBrdfAsked_ = false;
    std::shared_ptr<std::vector<uint8_t>> pendingEnvBrdf_;
    int pendingEnvBrdfW_ = 0, pendingEnvBrdfH_ = 0;'''),
])
patch('src/qtapp/camera_view.cpp', [
 ('''        if (!gameLutReady_) {
            const std::vector<uint8_t> lut = buildGameTonemapLut(64);
            renderer_.setGameTonemapLut(64, lut.data());
            gameLutReady_ = true;
        }''', '''        if (!gameLutReady_) {
            const std::vector<uint8_t> lut = buildGameTonemapLut(64);
            renderer_.setGameTonemapLut(64, lut.data());
            gameLutReady_ = true;
        }
        // The stock world shader's environment BRDF table, read from the install once (ACTS, a few seconds, cached).
        if (!envBrdfAsked_ && toolPathFn_) {
            envBrdfAsked_ = true;
            QPointer<CameraView> self(this);
            auto tools = toolPathFn_;
            std::thread([self, tools] {
                EnvBrdfLut lut = loadEnvBrdfLut(tools);
                QMetaObject::invokeMethod(qApp, [self, lut] {
                    if (!self) return;
                    if (lut.width > 0) {
                        self->pendingEnvBrdf_ = std::make_shared<std::vector<uint8_t>>(lut.rg);
                        self->pendingEnvBrdfW_ = lut.width, self->pendingEnvBrdfH_ = lut.height;
                        self->update();
                    } else {
                        emit self->ctx_->statusMessage("Game Lighting: " + lut.error + " (an approximation is used)");
                    }
                }, Qt::QueuedConnection);
            }).detach();
        }
        if (pendingEnvBrdf_) {
            renderer_.setEnvBrdfLut(pendingEnvBrdfW_, pendingEnvBrdfH_, pendingEnvBrdf_->data());
            pendingEnvBrdf_.reset();
        }'''),
 ('''#include "game_look.h"''', '''#include "game_look.h"
#include "game_look_assets.h"'''),
])
patch('src/qtapp/main_window.cpp', [
 ('''    connect(matCache_, &MaterialCache::materialReady, this, [this](const QString&) { camera_->materialImagesChanged(); });''',
  '''    connect(matCache_, &MaterialCache::materialReady, this, [this](const QString&) { camera_->materialImagesChanged(); });
    camera_->setToolPathSource([this](const QString& k) { return toolPath(k); });'''),
])
print('ok')
