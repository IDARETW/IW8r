import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


# ---------------------------------------------------------------- render.h
patch('src/viewport/render.h', [
 ('''    float nx = 0, ny = 0, nz = 0;
    float tx = 0, ty = 0, tz = 0;
};''', '''    float nx = 0, ny = 0, nz = 0;
    float tx = 0, ty = 0, tz = 0;
    // View > Game Lighting: the vertex's lightmap UV in the preview bake (game_lightmap.h); negative = none.
    float lu = -1, lv = -1;
};'''),
 ('''    static constexpr int kMaxPixelLights = 16;''', '''    static constexpr int kMaxPixelLights = 64;'''),
 ('''    void setPixelLights(const std::vector<PixelLight>& lights) { pixelLights_ = lights; }''',
  '''    void setPixelLights(const std::vector<PixelLight>& lights) { pixelLights_ = lights; }
    // View > Game Lighting (research 95, "Editor maps in game"): textured surfaces shade as the converter's world
    // shader shades a built map: albedo (sRGB decoded) x (lightmap A N.z + B sat(N.D) + sun visibility x N.L x
    // suncolor) + albedo x local lights in the game's units (pixel lights: colour already x the game intensity,
    // falloff 1 / d^2 in a smooth window), then the game's exposure (lutScale) and tone map LUT. `sky`: the light of
    // surfaces without a lightmap texel (models; the game's light grid, approximated by the sky and sun).
    struct GameLook {
        bool on = false;
        float sunToward[3] = {0, 0, 1};
        float sunColor[3] = {1, 1, 1};
        float sky[3] = {0.2f, 0.2f, 0.2f};
        float lutScale = 1.0f;
    };
    void setGameLook(const GameLook& g) { game_ = g; }
    bool gameLookOn() const { return game_.on; }
    // The preview lightmap: width x height texels of four floats each (a, b, d as GameLightmap). null = none.
    void setGameLightmap(int width, int height, const float* a, const float* b, const float* d);
    bool hasGameLightmap() const { return gameLmSrv_[0] != nullptr; }
    // The game's tone map LUT (core game_look buildGameTonemapLut): size^3 RGBA8, red fastest.
    void setGameTonemapLut(int size, const uint8_t* rgba);'''),
 ('''    ID3D11ShaderResourceView* lutSrv_ = nullptr;''', '''    GameLook game_;
    ID3D11Texture2D* gameLmTex_[3] = {nullptr, nullptr, nullptr};
    ID3D11ShaderResourceView* gameLmSrv_[3] = {nullptr, nullptr, nullptr};
    ID3D11Texture3D* gameLutTex_ = nullptr;
    ID3D11ShaderResourceView* gameLutSrv_ = nullptr;
    ID3D11Buffer* cbGame_ = nullptr;
    ID3D11ShaderResourceView* lutSrv_ = nullptr;'''),
])

# ---------------------------------------------------------------- render.cpp
game_ps = '''float3 srgbToLinear3(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
// The game's post chain: exposure x the +1.5 EV base (game0.y), the 20-stop log shaper, the universal colour LUT.
float3 gamePost(float3 c) {
    float3 x = max(c, 0) * game0.y;
    float3 u = saturate((log2(x + 1.0 / 1024.0) + 10.0) / 20.0) * (63.0 / 64.0) + 0.5 / 64.0;
    return gameLut.SampleLevel(lutSamp, u, 0).rgb;
}
float4 ps_tex(PSIn i) : SV_TARGET {
    float4 t = tex0.Sample(samp0, i.uv);
    // Blend 0: alpha-tested foliage and grates. Blended materials keep their coverage; without a mask (alpha 1) a
    // blended surface (glass, a decal sheet) shows at 60 percent so what is behind it stays visible.
    float outA = 1.0;
    if (batchFlags.y < 0.5) clip(t.a - 0.3);
    else outA = t.a > 0.99 ? 0.6 : t.a;
    if (game0.x > 0.5) {
        // View > Game Lighting: map_surface_realtime.hlsl (the converter's world shader, iw3 style) and the post chain.
        float3 albedo = srgbToLinear3(t.rgb) * i.col.rgb;  // i.col: selection and brush entity tints (white otherwise)
        if (i.col.a < 0.5) return float4(gamePost(albedo), 1.0);  // sky box: unlit
        float3 Nf = dot(i.n, i.n) > 0.5 ? normalize(i.n) : normalize(cross(ddx(i.w), ddy(i.w)));
        float3 N = Nf;
        if (batchFlags.x > 0.5 && dot(i.n, i.n) > 0.5) {
            float4 nm = nrm0.Sample(samp0, i.uv);
            float2 sd = nm.ga * 2.0 - 1.0;
            float2 oct = float2(sd.x + sd.y, sd.x - sd.y) * 0.5;
            float2 slope = oct / max(0.02, 1.0 - abs(oct.x) - abs(oct.y));
            float3 T = normalize(i.t - Nf * dot(i.t, Nf)), B = cross(Nf, T);
            N = normalize(T * slope.x + B * slope.y + Nf);
        }
        float3 indirect = gameSky.rgb * (0.6 + 0.4 * saturate(N.z));
        float visibility = 1.0;
        if (game0.z > 0.5 && i.lm.x >= 0) {
            float4 a = gameLmA.SampleLevel(lutSamp, i.lm, 0);
            float3 b = gameLmB.SampleLevel(lutSamp, i.lm, 0).rgb;
            float3 d = gameLmD.SampleLevel(lutSamp, i.lm, 0).xyz;
            indirect = a.rgb * saturate(dot(N, Nf)) + b * saturate(dot(normalize(d + 1e-6), N));
            visibility = a.a;
        }
        // The sun: the baked sun depth where it covers the point (here the sun shadow map), else the lightmap's mask.
        if (litParams.y > 0.5) visibility = sunShadow(i.w);
        float3 light = indirect + visibility * saturate(dot(N, gameSun.xyz)) * gameSunCol.rgb;
        float3 c = albedo * light;
        // Local lights (LocalLighting): colour x game intensity / d^2 x w^2 (3 - 2w), w = (1 - d / r) x spot cone;
        // specular as the iw3 style's (F0 .04, gloss .3).
        float3 V = normalize(eyeStart.xyz - i.w);
        int count = int(lightCount.x);
        for (int k = 0; k < count; ++k) {
            float3 dl = lights[k].posRadius.xyz - i.w;
            float dist = length(dl);
            if (dist >= lights[k].posRadius.w || dist < 1e-4) continue;
            float3 l = dl / dist;
            float spot = 1.0;
            if (lights[k].colorOuter.w > -1.5)
                spot = saturate((dot(-l, lights[k].dirInner.xyz) - lights[k].colorOuter.w) /
                                max(1e-4, lights[k].dirInner.w - lights[k].colorOuter.w));
            float w = saturate(1.0 - dist / lights[k].posRadius.w) * spot;
            float att = w * w * (3.0 - 2.0 * w) / max(dist * dist, 1.0);
            float3 irr = lights[k].colorOuter.rgb * att * saturate(dot(N, l));
            float3 h = normalize(V + l);
            float power = exp2(13.0 * 0.3);
            float dist2 = (power + 2.0) * 0.5 * pow(saturate(dot(N, h)), power);
            float fres = 0.04 + 0.96 * pow(1.0 - saturate(dot(V, h)), 5.0);
            c += albedo * irr + fres * dist2 * 0.25 * irr;
        }
        if (batchFlags2.x > 0.5) c += srgbToLinear3(emi0.Sample(samp0, i.uv).rgb);
        c = applyFog(c, i.w);
        c = gamePost(c);
        if (batchFlags.y > 1.5) return float4(c * outA, 1.0);
        return float4(c, outA);
    }'''

patch('src/viewport/render.cpp', [
 ('''cbuffer Lights : register(b2) { float4 lightCount; PixelLight lights[16]; };''',
  '''cbuffer Lights : register(b2) { float4 lightCount; PixelLight lights[64]; };
// View > Game Lighting: game0 = (on, LUT input scale, lightmap bound, 0), sun toward, sun colour, unbaked light.
cbuffer Game : register(b4) { float4 game0; float4 gameSun; float4 gameSunCol; float4 gameSky; };
Texture2D gameLmA : register(t6);
Texture2D gameLmB : register(t7);
Texture2D gameLmD : register(t8);
Texture3D gameLut : register(t9);'''),
 ('''struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float3 t : TANGENT; };
struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 w : TEXCOORD1; float3 n : TEXCOORD2; float3 t : TEXCOORD3; };''',
  '''struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float3 t : TANGENT; float2 lm : TEXCOORD4; };
struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 w : TEXCOORD1; float3 n : TEXCOORD2; float3 t : TEXCOORD3; float2 lm : TEXCOORD4; };'''),
 ('''    o.pos = mul(float4(p, 1.0), viewProj); o.uv = i.uv; o.col = i.col; o.w = p; o.n = i.n; o.t = i.t; return o;''',
  '''    o.pos = mul(float4(p, 1.0), viewProj); o.uv = i.uv; o.col = i.col; o.w = p; o.n = i.n; o.t = i.t; o.lm = i.lm; return o;'''),
 ('''struct VSInInst { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float3 t : TANGENT; float4 r0 : INSTROW0; float4 r1 : INSTROW1; float4 r2 : INSTROW2; };''',
  '''struct VSInInst { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float3 t : TANGENT; float2 lm : TEXCOORD4; float4 r0 : INSTROW0; float4 r1 : INSTROW1; float4 r2 : INSTROW2; };'''),
 ('''    PSIn o; o.pos = mul(float4(w, 1.0), viewProj); o.uv = i.uv; o.col = i.col; o.w = w; o.n = float3(0, 0, 0); o.t = float3(0, 0, 0); return o;''',
  '''    PSIn o; o.pos = mul(float4(w, 1.0), viewProj); o.uv = i.uv; o.col = i.col; o.w = w; o.n = float3(0, 0, 0); o.t = float3(0, 0, 0); o.lm = float2(-1, -1); return o;'''),
 ('''float4 ps_tex(PSIn i) : SV_TARGET {
    float4 t = tex0.Sample(samp0, i.uv);
    // Blend 0: alpha-tested foliage and grates. Blended materials keep their coverage; without a mask (alpha 1) a
    // blended surface (glass, a decal sheet) shows at 60 percent so what is behind it stays visible.
    float outA = 1.0;
    if (batchFlags.y < 0.5) clip(t.a - 0.3);
    else outA = t.a > 0.99 ? 0.6 : t.a;''', game_ps),
 ('''                {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };
            dev_->CreateInputLayout(til, 5, tvs->GetBufferPointer(), tvs->GetBufferSize(), &layoutTex_);''',
  '''                {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 4, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };
            dev_->CreateInputLayout(til, 6, tvs->GetBufferPointer(), tvs->GetBufferSize(), &layoutTex_);'''),
 ('''                    {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"INSTROW", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},''',
  '''                    {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"TEXCOORD", 4, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"INSTROW", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},'''),
 ('''                dev_->CreateInputLayout(tiil, 8, tivs->GetBufferPointer(), tivs->GetBufferSize(), &layoutTexInst_);''',
  '''                dev_->CreateInputLayout(tiil, 9, tivs->GetBufferPointer(), tivs->GetBufferSize(), &layoutTexInst_);'''),
 ('''            bd.ByteWidth = 16 + UINT(sizeof(PixelLight)) * kMaxPixelLights;
            dev_->CreateBuffer(&bd, nullptr, &cbLights_);''', '''            bd.ByteWidth = 16 + UINT(sizeof(PixelLight)) * kMaxPixelLights;
            dev_->CreateBuffer(&bd, nullptr, &cbLights_);
            bd.ByteWidth = 64;  // game0, sun toward, sun colour, unbaked light
            dev_->CreateBuffer(&bd, nullptr, &cbGame_);'''),
 ('''    safeRelease(cbLights_);
    releaseStreams();''', '''    safeRelease(cbLights_);
    safeRelease(cbGame_);
    for (int k = 0; k < 3; ++k) safeRelease(gameLmSrv_[k]), safeRelease(gameLmTex_[k]);
    safeRelease(gameLutSrv_);
    safeRelease(gameLutTex_);
    releaseStreams();'''),
 ('''void SceneRenderer::resize(int w, int h) {''', '''void SceneRenderer::setGameLightmap(int width, int height, const float* a, const float* b, const float* d) {
    for (int k = 0; k < 3; ++k) safeRelease(gameLmSrv_[k]), safeRelease(gameLmTex_[k]);
    if (!dev_ || width < 1 || height < 1 || !a || !b || !d) return;
    const float* src[3] = {a, b, d};
    for (int k = 0; k < 3; ++k) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = UINT(width), td.Height = UINT(height);
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{src[k], UINT(width) * 16, 0};
        if (FAILED(dev_->CreateTexture2D(&td, &init, &gameLmTex_[k])) ||
            FAILED(dev_->CreateShaderResourceView(gameLmTex_[k], nullptr, &gameLmSrv_[k]))) {
            for (int j = 0; j < 3; ++j) safeRelease(gameLmSrv_[j]), safeRelease(gameLmTex_[j]);
            return;
        }
    }
}

void SceneRenderer::setGameTonemapLut(int size, const uint8_t* rgba) {
    safeRelease(gameLutSrv_);
    safeRelease(gameLutTex_);
    if (size < 2 || !rgba || !dev_) return;
    D3D11_TEXTURE3D_DESC td{};
    td.Width = td.Height = td.Depth = UINT(size);
    td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{rgba, UINT(size) * 4, UINT(size) * UINT(size) * 4};
    if (FAILED(dev_->CreateTexture3D(&td, &init, &gameLutTex_)) || FAILED(dev_->CreateShaderResourceView(gameLutTex_, nullptr, &gameLutSrv_))) {
        safeRelease(gameLutTex_);
        return;
    }
    if (!lutSampler_) {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        dev_->CreateSamplerState(&sd, &lutSampler_);
    }
}

void SceneRenderer::resize(int w, int h) {'''),
 ('''        if (cbLights_) {
            struct {
                float count[4];''', '''        if (cbGame_) {
            const bool on = game_.on && gameLutSrv_ != nullptr;
            const float g[16] = {on ? 1.0f : 0.0f, game_.lutScale, on && gameLmSrv_[0] ? 1.0f : 0.0f, 0,
                                 game_.sunToward[0], game_.sunToward[1], game_.sunToward[2], 0,
                                 game_.sunColor[0], game_.sunColor[1], game_.sunColor[2], 0,
                                 game_.sky[0], game_.sky[1], game_.sky[2], 0};
            ctx_->UpdateSubresource(cbGame_, 0, nullptr, g, 0, 0);
            ctx_->PSSetConstantBuffers(4, 1, &cbGame_);
            ID3D11ShaderResourceView* gs[4] = {gameLmSrv_[0], gameLmSrv_[1], gameLmSrv_[2], gameLutSrv_};
            ctx_->PSSetShaderResources(6, 4, gs);
            if (lutSampler_) ctx_->PSSetSamplers(2, 1, &lutSampler_);
        }
        if (cbLights_) {
            struct {
                float count[4];'''),
])
print('ok')
