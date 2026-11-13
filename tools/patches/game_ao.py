import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/viewport/render.h'
R = 'src/viewport/render.cpp'

# ---------------------------------------------------------------- header
patch(H, '''    ID3D11PixelShader* psShadowClip_ = nullptr;  // sun shadow pass: alpha-test clip of textured batches''',
      '''    ID3D11PixelShader* psShadowClip_ = nullptr;  // sun shadow pass: alpha-test clip of textured batches
    ID3D11PixelShader* psPrepass_ = nullptr;     // depth pre-pass: the shading pass's cut-outs and terrain cut
    // Game Lighting ambient occlusion (screen space, from the pre-pass depth; research 95 "GTAO"): the depth's shader
    // view, the raw and blurred AO targets, the full-screen shaders and their constants (b8).
    ID3D11ShaderResourceView* depthSrv_ = nullptr;
    ID3D11Texture2D* aoTex_[2] = {nullptr, nullptr};
    ID3D11RenderTargetView* aoRtv_[2] = {nullptr, nullptr};
    ID3D11ShaderResourceView* aoSrv_[2] = {nullptr, nullptr};
    ID3D11VertexShader* aoVs_ = nullptr;
    ID3D11PixelShader* aoPs_ = nullptr;
    ID3D11PixelShader* aoBlurPs_ = nullptr;
    ID3D11Buffer* cbAo_ = nullptr;
    bool aoTried_ = false;
    bool aoOn_ = true;
    bool aoValid_ = false;  // this frame's AO is in aoSrv_[1]
    bool createAoResources();
    void drawAo(const float* viewProj);''')
patch(H, '''    bool createMeterResources();''', '''    bool createMeterResources();
public:
    // View > Game Lighting: screen-space ambient occlusion on the indirect light (on by default).
    void setGameAo(bool on) { aoOn_ = on; }
    bool gameAo() const { return aoOn_; }
    bool gameAoDrawn() const { return aoValid_; }
private:''')

# ---------------------------------------------------------------- shaders
patch(R, '''Texture3D<uint4> probeGrid : register(t13);''', '''Texture3D<uint4> probeGrid : register(t13);
Texture2D<float> aoMap : register(t14);  // screen-space ambient occlusion (gameAo.x = 1 when drawn this frame)''')
patch(R, '''cbuffer Game : register(b4) { float4 game0; float4 gameSun; float4 gameSunCol; float4 gameSky; float4 gameFog[6]; float4 gameSkyScale; float4 gameTerrainCut; };''',
      '''cbuffer Game : register(b4) { float4 game0; float4 gameSun; float4 gameSunCol; float4 gameSky; float4 gameFog[6]; float4 gameSkyScale; float4 gameTerrainCut; float4 gameAo; };''')
patch(R, '''void ps_shadow_clip(PSIn i) { clip(tex0.Sample(samp0, i.uv).a - 0.3); }''',
      '''void ps_shadow_clip(PSIn i) { clip(tex0.Sample(samp0, i.uv).a - 0.3); }
// The depth pre-pass: the shading pass's own cut-out threshold and the coarse terrain cut, so the depths match.
void ps_prepass(PSIn i) {
    clip(tex0.Sample(samp0, i.uv).a - (game0.x > 0.5 && batchFlags2.z > 0.5 ? 0.5 : 0.3));
    if (gameTerrainCut.w > 0.5 && batchFlags2.z > 0.5 && i.col.a > 0.99 && i.col.a < 0.998 && distance(i.w.xy, gameTerrainCut.xy) < gameTerrainCut.z) discard;
}''')
# the AO in the lighting
patch(R, '''        const bool stockRef = batchFlags2.z > 0.5;
        float3 albedo = srgbToLinear3(t.rgb) * (stockRef ? float3(1, 1, 1) : i.col.rgb);  // i.col: selection and brush entity tints''',
      '''        const bool stockRef = batchFlags2.z > 0.5;
        // Screen-space ambient occlusion: the indirect diffuse (multi-bounce, as the game) and the specular occlusion.
        const float aoV = gameAo.x > 0.5 ? aoMap.Load(int3(i.pos.xy, 0)) : 1.0;
        float3 albedo = srgbToLinear3(t.rgb) * (stockRef ? float3(1, 1, 1) : i.col.rgb);  // i.col: selection and brush entity tints''')
patch(R, '''            float3 S = Lr;
            if (probeCount.x > 0.5 && layerP.x < 0.5) {''', '''            const float so = min(aoV, baseMat ? nmCavity : 1.0);
            float3 S = lerp(0.18 * Lr, Lr, so);
            if (probeCount.x > 0.5 && layerP.x < 0.5) {''')
patch(R, '''                S = lerp(0.18 * mixed, P, baseMat ? nmCavity : 1.0);''', '''                S = lerp(0.18 * mixed, P, so);''')
patch(R, '''            c = diffAlbedo * indirect * kd * (1.0 - fmax) + Fenv * S;''',
      '''            const float3 aoMB = aoV / (1.0 - min(diffAlbedo, 0.85) * (1.0 - aoV));  // research 95: multi-bounce AO
            c = diffAlbedo * indirect * kd * (1.0 - fmax) * aoMB + Fenv * S;''')
patch(R, '''            float3 light = indirect + visibility * saturate(dot(N, gameSun.xyz)) * gameSunCol.rgb;''',
      '''            float3 light = indirect * aoV + visibility * saturate(dot(N, gameSun.xyz)) * gameSunCol.rgb;''')

patch(R, '''const char* kMeterShader = R"(''', '''// Game Lighting ambient occlusion: normal-oriented hemisphere samples in world space from the pre-pass depth, then a
// 4 x 4 depth-aware blur (the sample rotation repeats every 4 pixels, so the blur averages it out).
const char* kAoShader = R"(
Texture2D<float> depthTex : register(t0);
Texture2D<float> aoIn : register(t1);
cbuffer Ao : register(b8) { row_major float4x4 aoVp; row_major float4x4 aoIvp; float4 aoEye; float4 aoPrm; };  // radius, power, width, height
struct V { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
V vs_full(uint id : SV_VertexID) {
    V o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.uv = p;
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float3 worldAt(float2 uv, float d) {
    float4 w = mul(float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, d, 1.0), aoIvp);
    return w.xyz / w.w;
}
float depthAt(int2 p) { return depthTex.Load(int3(clamp(p, int2(0, 0), int2(aoPrm.zw) - 1), 0)); }
float ps_ao(V i) : SV_TARGET {
    int2 px = int2(i.pos.xy);
    float2 inv = 1.0 / aoPrm.zw;
    float2 uv = (float2(px) + 0.5) * inv;
    float d = depthAt(px);
    if (d >= 0.999999) return 1.0;
    float3 P = worldAt(uv, d);
    float dl = depthAt(px - int2(1, 0)), dr = depthAt(px + int2(1, 0)), du = depthAt(px - int2(0, 1)), dd = depthAt(px + int2(0, 1));
    float3 ex = abs(dr - d) < abs(d - dl) ? worldAt(uv + float2(inv.x, 0), dr) - P : P - worldAt(uv - float2(inv.x, 0), dl);
    float3 ey = abs(dd - d) < abs(d - du) ? worldAt(uv + float2(0, inv.y), dd) - P : P - worldAt(uv - float2(0, inv.y), du);
    float3 n = cross(ex, ey);
    if (dot(n, n) < 1e-20) return 1.0;
    n = normalize(n);
    float3 toEye = aoEye.xyz - P;
    if (dot(n, toEye) < 0) n = -n;
    float dist = length(toEye);
    uint2 q = uint2(px) & 3;
    float rot = (float((q.x * 4 + q.y) * 7 % 16) + 0.5) / 16.0 * 6.2831853;
    float3 t0 = normalize(abs(n.z) < 0.9 ? cross(n, float3(0, 0, 1)) : cross(n, float3(1, 0, 0)));
    float3 b0 = cross(n, t0);
    float3 T = t0 * cos(rot) + b0 * sin(rot), B = cross(n, T);
    float R = aoPrm.x;
    float occ = 0.0;
    [unroll] for (int k = 0; k < 12; ++k) {
        float f = (k + 0.5) / 12.0;
        float phi = k * 2.39996323;
        float rr = sqrt(f);
        float3 h = float3(cos(phi) * rr, sin(phi) * rr, sqrt(max(1.0 - f, 0.0)));
        float3 S = P + (T * h.x + B * h.y + n * h.z) * (R * lerp(0.2, 1.0, f * f)) + n * (0.02 * R);
        float4 c = mul(float4(S, 1.0), aoVp);
        if (c.w <= 1e-4) continue;
        float2 suv = float2(c.x / c.w * 0.5 + 0.5, 0.5 - c.y / c.w * 0.5);
        if (any(suv < 0.0) || any(suv > 1.0)) continue;
        float sd = depthAt(int2(suv * aoPrm.zw));
        if (sd >= 0.999999) continue;
        float3 Q = worldAt(suv, sd);
        if (length(Q - aoEye.xyz) < length(S - aoEye.xyz) - 0.03 * R) {
            float r = saturate(R / max(length(Q - P), 1e-3));
            occ += r * r * (3.0 - 2.0 * r);
        }
    }
    float ao = 1.0 - occ / 12.0;
    ao = lerp(ao, 1.0, saturate((dist - 6000.0) / 4000.0));
    return pow(saturate(ao), aoPrm.y);
}
float viewDist(int2 p) {
    int2 c = clamp(p, int2(0, 0), int2(aoPrm.zw) - 1);
    float d = depthTex.Load(int3(c, 0));
    return length(worldAt((float2(c) + 0.5) / aoPrm.zw, d) - aoEye.xyz);
}
float ps_ao_blur(V i) : SV_TARGET {
    int2 px = int2(i.pos.xy);
    float L0 = viewDist(px);
    float sum = 0.0, wsum = 0.0;
    [unroll] for (int y = -2; y < 2; ++y)
        [unroll] for (int x = -2; x < 2; ++x) {
            int2 p = clamp(px + int2(x, y), int2(0, 0), int2(aoPrm.zw) - 1);
            float w = saturate(1.0 - abs(viewDist(p) - L0) / (0.05 * L0 + 1.0)) + 1e-4;
            sum += aoIn.Load(int3(p, 0)) * w;
            wsum += w;
        }
    return sum / wsum;
}
)";

const char* kMeterShader = R"(''')

# compile the pre-pass shader with the textured ones
patch(R, '''            if (compileTex("ps_shadow_clip", "ps_5_0", sps)) {
                dev_->CreatePixelShader(sps->GetBufferPointer(), sps->GetBufferSize(), nullptr, &psShadowClip_);
                safeRelease(sps);
            }''', '''            if (compileTex("ps_shadow_clip", "ps_5_0", sps)) {
                dev_->CreatePixelShader(sps->GetBufferPointer(), sps->GetBufferSize(), nullptr, &psShadowClip_);
                safeRelease(sps);
            }
            ID3DBlob* pps = nullptr;
            if (compileTex("ps_prepass", "ps_5_0", pps)) {
                dev_->CreatePixelShader(pps->GetBufferPointer(), pps->GetBufferSize(), nullptr, &psPrepass_);
                safeRelease(pps);
            }''')
patch(R, '''    safeRelease(psShadowClip_);''', '''    safeRelease(psShadowClip_);
    safeRelease(psPrepass_);
    safeRelease(aoVs_), safeRelease(aoPs_), safeRelease(aoBlurPs_), safeRelease(cbAo_);''')

# targets: a readable depth buffer and the AO targets
patch(R, '''    td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    dev_->CreateTexture2D(&td, nullptr, &depth_);
    dev_->CreateDepthStencilView(depth_, nullptr, &dsv_);
}''', '''    // Depth, readable by the ambient occlusion pass (typeless: D24S8 to draw, R24 to read).
    td.Format = DXGI_FORMAT_R24G8_TYPELESS;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    if (SUCCEEDED(dev_->CreateTexture2D(&td, nullptr, &depth_))) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
        dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dev_->CreateDepthStencilView(depth_, &dd, &dsv_);
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        dev_->CreateShaderResourceView(depth_, &sd, &depthSrv_);
    } else {
        td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;  // no readable depth: no ambient occlusion
        td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        dev_->CreateTexture2D(&td, nullptr, &depth_);
        dev_->CreateDepthStencilView(depth_, nullptr, &dsv_);
    }
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    for (int k = 0; k < 2; ++k)
        if (SUCCEEDED(dev_->CreateTexture2D(&td, nullptr, &aoTex_[k]))) {
            dev_->CreateRenderTargetView(aoTex_[k], nullptr, &aoRtv_[k]);
            dev_->CreateShaderResourceView(aoTex_[k], nullptr, &aoSrv_[k]);
        }
}''')
patch(R, '''    safeRelease(dsv_);
    safeRelease(color_);
    safeRelease(depth_);
}''', '''    safeRelease(dsv_);
    safeRelease(color_);
    safeRelease(depthSrv_);
    safeRelease(depth_);
    for (int k = 0; k < 2; ++k) safeRelease(aoSrv_[k]), safeRelease(aoRtv_[k]), safeRelease(aoTex_[k]);
    aoValid_ = false;
}

bool SceneRenderer::createAoResources() {
    if (aoVs_ && aoPs_ && aoBlurPs_ && cbAo_) return true;
    if (aoTried_ || !dev_) return false;
    aoTried_ = true;
    ID3DBlob *vsb = nullptr, *psb = nullptr, *pbb = nullptr, *errb = nullptr;
    const auto compile = [&](const char* entry, const char* target, ID3DBlob*& out) {
        const bool ok = SUCCEEDED(D3DCompile(kAoShader, std::strlen(kAoShader), "iw8r_ao", nullptr, nullptr, entry, target, 0, 0, &out, &errb));
        safeRelease(errb);
        return ok;
    };
    if (compile("vs_full", "vs_5_0", vsb) && compile("ps_ao", "ps_5_0", psb) && compile("ps_ao_blur", "ps_5_0", pbb)) {
        dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &aoVs_);
        dev_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &aoPs_);
        dev_->CreatePixelShader(pbb->GetBufferPointer(), pbb->GetBufferSize(), nullptr, &aoBlurPs_);
    }
    safeRelease(vsb), safeRelease(psb), safeRelease(pbb);
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = 160;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    dev_->CreateBuffer(&bd, nullptr, &cbAo_);
    return aoVs_ && aoPs_ && aoBlurPs_ && cbAo_;
}

namespace {
// The inverse of a 4 x 4 matrix (row-major); false when singular.
bool invert4(const float* m, float* out) {
    double inv[16], a[16];
    for (int k = 0; k < 16; ++k) a[k] = m[k];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    const double det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (std::fabs(det) < 1e-30) return false;
    for (int k = 0; k < 16; ++k) out[k] = float(inv[k] / det);
    return true;
}
}  // namespace

void SceneRenderer::drawAo(const float* viewProj) {
    aoValid_ = false;
    if (!depthSrv_ || !aoRtv_[0] || !aoRtv_[1] || !createAoResources()) return;
    IW8R_PROF("d.ao");
    float cb[40];
    std::memcpy(cb, viewProj, 64);
    if (!invert4(viewProj, cb + 16)) return;
    static const float radius = std::getenv("IW8R_AO_RADIUS") ? float(std::atof(std::getenv("IW8R_AO_RADIUS"))) : 40.0f;
    static const float power = std::getenv("IW8R_AO_POWER") ? float(std::atof(std::getenv("IW8R_AO_POWER"))) : 1.0f;
    cb[32] = fog_[0], cb[33] = fog_[1], cb[34] = fog_[2], cb[35] = 0;
    cb[36] = std::max(radius, 1.0f), cb[37] = std::max(power, 0.1f), cb[38] = float(w_), cb[39] = float(h_);
    ctx_->UpdateSubresource(cbAo_, 0, nullptr, cb, 0, 0);
    ID3D11ShaderResourceView* none2[2] = {nullptr, nullptr};
    ctx_->PSSetShaderResources(14, 1, none2);  // last frame's AO is about to be written
    ctx_->OMSetRenderTargets(1, &aoRtv_[0], nullptr);
    ctx_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx_->OMSetDepthStencilState(dsOff_, 0);
    ctx_->RSSetState(rsTris_);
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(aoVs_, nullptr, 0);
    ctx_->PSSetShader(aoPs_, nullptr, 0);
    ctx_->PSSetConstantBuffers(8, 1, &cbAo_);
    ID3D11ShaderResourceView* in0[2] = {depthSrv_, nullptr};
    ctx_->PSSetShaderResources(0, 2, in0);
    ctx_->Draw(3, 0);
    ctx_->OMSetRenderTargets(1, &aoRtv_[1], nullptr);
    ctx_->PSSetShader(aoBlurPs_, nullptr, 0);
    ID3D11ShaderResourceView* in1[2] = {depthSrv_, aoSrv_[0]};
    ctx_->PSSetShaderResources(0, 2, in1);
    ctx_->Draw(3, 0);
    ctx_->PSSetShaderResources(0, 2, none2);
    ctx_->OMSetRenderTargets(1, &rtv_, dsv_);
    aoValid_ = true;
}''')

# writeGameCb: the AO flag and texture
patch(R, '''    float g[48] = {on ? 1.0f : 0.0f, game_.lutScale, on && gameLmSrv_[0] ? 1.0f : 0.0f, envBrdfSrv_ ? 1.0f : 0.0f,''',
      '''    float g[52] = {on ? 1.0f : 0.0f, game_.lutScale, on && gameLmSrv_[0] ? 1.0f : 0.0f, envBrdfSrv_ ? 1.0f : 0.0f,''')
patch(R, '''    g[44] = terrainCut_[0], g[45] = terrainCut_[1], g[46] = terrainCut_[2], g[47] = terrainCut_[2] > 0 ? 1.0f : 0.0f;''',
      '''    g[44] = terrainCut_[0], g[45] = terrainCut_[1], g[46] = terrainCut_[2], g[47] = terrainCut_[2] > 0 ? 1.0f : 0.0f;
    g[48] = on && aoValid_ ? 1.0f : 0.0f;''')
patch(R, '''    ctx_->PSSetShaderResources(13, 1, &probeGridSrv_);''', '''    ctx_->PSSetShaderResources(13, 1, &probeGridSrv_);
    ID3D11ShaderResourceView* ao = on && aoValid_ ? aoSrv_[1] : nullptr;
    ctx_->PSSetShaderResources(14, 1, &ao);''')
patch(R, '''            bd.ByteWidth = 192;  // game0, sun toward, sun colour, unbaked light, fog[6], sky scale, terrain cut''',
      '''            bd.ByteWidth = 208;  // game0, sun toward, sun colour, unbaked light, fog[6], sky scale, terrain cut, ao''')

# the pre-pass: on whenever the AO is drawn; then the AO
patch(R, '''    const bool prepass = usePrepass && game_.on && psShadowClip_ && blendNoColor_ && vsTex_ && layoutTex_ && sampler_;
    if (prepass) {
        IW8R_PROF("d.prepass");''', '''    static const bool noAo = std::getenv("IW8R_NO_AO") != nullptr;
    aoValid_ = false;
    const bool aoWanted = game_.on && aoOn_ && !noAo && depthSrv_ && aoRtv_[1] && psPrepass_ && createAoResources();
    const bool prepass = (usePrepass || aoWanted) && game_.on && psPrepass_ && blendNoColor_ && vsTex_ && layoutTex_ && sampler_;
    if (prepass) {
        IW8R_PROF("d.prepass");
        writeGameCb();  // the cut-out threshold and the terrain cut (b4)''')
patch(R, '''        ctx_->PSSetShader(psShadowClip_, nullptr, 0);
        ctx_->PSSetSamplers(0, 1, &sampler_);
        if (cbBatch_) ctx_->VSSetConstantBuffers(1, 1, &cbBatch_);''', '''        ctx_->PSSetShader(psPrepass_, nullptr, 0);
        ctx_->PSSetSamplers(0, 1, &sampler_);
        if (cbBatch_) ctx_->VSSetConstantBuffers(1, 1, &cbBatch_), ctx_->PSSetConstantBuffers(1, 1, &cbBatch_);''')
patch(R, '''        if (refVisible_ && !refTexInst_.empty()) {
            texPsOverride_ = psShadowClip_;
            drawReferenceTexInstances();
            texPsOverride_ = nullptr;
        }
        ID3D11ShaderResourceView* noTex[1] = {nullptr};
        ctx_->PSSetShaderResources(0, 1, noTex);
        ctx_->OMSetBlendState(blend_, bf, 0xffffffff);
        ctx_->IASetInputLayout(layout_);
        ctx_->VSSetShader(vs_, nullptr, 0);
        ctx_->PSSetShader(ps_, nullptr, 0);
    }''', '''        if (refVisible_ && !refTexInst_.empty()) {
            texPsOverride_ = psPrepass_;
            drawReferenceTexInstances();
            texPsOverride_ = nullptr;
        }
        ID3D11ShaderResourceView* noTex[1] = {nullptr};
        ctx_->PSSetShaderResources(0, 1, noTex);
        if (aoWanted) {
            drawAo(viewProj);
            ctx_->RSSetViewports(1, &vp);
        }
        ctx_->OMSetBlendState(blend_, bf, 0xffffffff);
        ctx_->IASetInputLayout(layout_);
        ctx_->VSSetShader(vs_, nullptr, 0);
        ctx_->PSSetShader(ps_, nullptr, 0);
    }''')
print('ok')
