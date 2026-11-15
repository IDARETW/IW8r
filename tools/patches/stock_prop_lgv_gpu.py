import io

# Research 132 G7 / research 105 section 1.4: a probe-lit stock prop is lit per vertex from its light grid box, as the
# game's static-model vertex shader does (box coordinate from the vertex's world position, trilinear in the atlas, DC and
# eight SH slots, band weights 1, 4/9, 1/16). Before, each placement had one sample at its box centre with the first
# order only, so big props (buildings, containers, walls as models) were lit flat end to end.
# The atlas goes to the GPU once per zone (texture 0 R11G11B10F, texture 1 RGBA8, five float4 per box). An instance
# light record with [0] = -3 names its box in [1] and keeps the centre's DC and slots 0 / 2 in [2..10] (the fallback when
# the atlas is not bound).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/viewport/render.h'
patch(H, '''    bool hasGtsoLut() const { return gtsoSrv_ != nullptr; }''', '''    bool hasGtsoLut() const { return gtsoSrv_ != nullptr; }
    // The stock props' light grid atlas (research 105 sections 1.3-1.4, research 132 G7): texture 0 (R11G11B10F,
    // width x width x depth), texture 1 (RGBA8, 2 width x 3 width x depth, six tiles) and per box five float4 (the
    // world -> box rows, uvScale, uvBias). A prop instance whose light record starts with -3 samples its box ([1]) per
    // vertex in vs_tex_inst. width 0 clears it. False when the textures cannot be made (the records' fallback lights).
    bool setStockLgvAtlas(int width, int depth, const uint32_t* tex0, const uint32_t* tex1, const float* boxes, size_t boxCount);
    bool hasStockLgvAtlas() const { return lgvSrv_[2] != nullptr; }''')
patch(H, '''    ID3D11Texture3D* gtsoTex_ = nullptr;''', '''    ID3D11Texture3D* gtsoTex_ = nullptr;
    ID3D11Texture3D* lgvTex_[2] = {nullptr, nullptr};  // setStockLgvAtlas
    ID3D11Buffer* lgvBoxBuf_ = nullptr;
    ID3D11ShaderResourceView* lgvSrv_[3] = {nullptr, nullptr, nullptr};  // VS t40 / t41 / t42
    ID3D11SamplerState* lgvSampler_ = nullptr;                          // VS s4''')

C = 'src/viewport/render.cpp'
# HLSL: the atlas and the per-vertex evaluation.
patch(C, '''cbuffer PropWave : register(b8) { float4 pwRows[8]; float4 wave79; float4 wave80; float4 wave81; float4 wave82; };''',
      '''cbuffer PropWave : register(b8) { float4 pwRows[8]; float4 wave79; float4 wave80; float4 wave81; float4 wave82; };
// The stock props' light grid atlas (research 105 section 1.4; SceneRenderer::setStockLgvAtlas): texture 0 the DC
// (R11G11B10F), texture 1 six tiles of normalised SH bytes (2 x 3), five float4 per box (rows, uvScale, uvBias).
Texture3D<float3> lgvDc : register(t40);
Texture3D<float4> lgvTiles : register(t41);
Buffer<float4> lgvBoxes : register(t42);
SamplerState lgvSamp : register(s4);
// E(n) and E(-n) of a box at world point w (the static-model VS: saturate(M (w, 1)), trilinear, slot j channel c =
// q[3 j + c] x DC_c x sqrt3 | sqrt5; band weights 1, 4/9, 1/16). False when the box is not in the bound buffer.
bool lgvLight(uint box, float3 w, float3 n, out float3 e, out float3 back) {
    uint count = 0;
    lgvBoxes.GetDimensions(count);
    e = float3(0, 0, 0), back = float3(0, 0, 0);
    if (box * 5u + 4u >= count) return false;
    const float4 p = float4(w, 1.0);
    const float3 c = saturate(float3(dot(lgvBoxes[box * 5u], p), dot(lgvBoxes[box * 5u + 1u], p), dot(lgvBoxes[box * 5u + 2u], p)));
    const float3 uvw = c * lgvBoxes[box * 5u + 3u].xyz + lgvBoxes[box * 5u + 4u].xyz;
    const float3 dc = lgvDc.SampleLevel(lgvSamp, uvw, 0);
    float q[24];
    [unroll] for (int t = 0; t < 6; ++t) {
        const float4 v = lgvTiles.SampleLevel(lgvSamp, float3(uvw.x * 0.5 + 0.5 * float(t & 1), (uvw.y + float(t >> 1)) / 3.0, uvw.z), 0) * 2.0 - 1.0;
        q[4 * t] = v.x, q[4 * t + 1] = v.y, q[4 * t + 2] = v.z, q[4 * t + 3] = v.w;
    }
    const float r3 = 1.7320508, r5 = 2.2360680;
    const float3 s0 = float3(q[0], q[1], q[2]) * dc * r3, s1 = float3(q[3], q[4], q[5]) * dc * r5;
    const float3 s2 = float3(q[6], q[7], q[8]) * dc * r3, s3 = float3(q[9], q[10], q[11]) * dc * r3;
    const float3 s4 = float3(q[12], q[13], q[14]) * dc * r5, s5 = float3(q[15], q[16], q[17]) * dc * r5;
    const float3 s6 = float3(q[18], q[19], q[20]) * dc * r5, s7 = float3(q[21], q[22], q[23]) * dc * r5;
    const float3 band1 = 0.217157 * (s0 * n.z - s2 * n.y - s3 * n.x);  // 4/9 x 0.488603
    const float3 band2 = 0.0625 * (0.315392 * (3.0 * n.z * n.z - 1.0) * s1 - 1.092548 * n.x * n.z * s4 - 1.092548 * n.y * n.z * s5 +
                                   0.546274 * (n.x * n.x - n.y * n.y) * s6 + 1.092548 * n.x * n.y * s7);
    e = max(0.282095 * dc + band1 + band2, 0.0);
    back = max(0.282095 * dc - band1 + band2, 0.0);  // E(-n): the second band is even
    return true;
}''')
patch(C, '''    if (i.l0.x >= 0.0) {
        const float3 dc = i.l0.xyz, s0 = float3(i.l0.w, i.l1.x, i.l1.y), s2 = float3(i.l1.z, i.l1.w, i.l2.x), s3 = i.l2.yzw;''',
      '''    if (i.l0.x < -2.5) {
        // Per vertex from the box in the atlas (research 105 section 1.4); without the atlas, the box centre's DC and
        // slots 0 / 2 kept in the record.
        float3 e, back;
        if (!lgvLight(uint(i.l0.y + 0.5), w, o.n, e, back)) {
            const float3 dc = float3(i.l0.zw, i.l1.x), s0 = i.l1.yzw, s2 = i.l2.xyz;
            e = max(0.282095 * dc + 0.217157 * (s0 * o.n.z - s2 * o.n.y), 0.0);
            back = max(0.282095 * dc - 0.217157 * (s0 * o.n.z - s2 * o.n.y), 0.0);
        }
        o.t = e;
        o.lm = float2(-2, -2);
        o.lgvBack = back;
    } else if (i.l0.x >= 0.0) {
        const float3 dc = i.l0.xyz, s0 = float3(i.l0.w, i.l1.x, i.l1.y), s2 = float3(i.l1.z, i.l1.w, i.l2.x), s3 = i.l2.yzw;''')
# release
patch(C, '''    safeRelease(gtsoSrv_), safeRelease(gtsoTex_);
    safeRelease(stockLightSrv_), safeRelease(stockLightBuf_), safeRelease(stockTileSrv_), safeRelease(stockTileBuf_);''',
      '''    safeRelease(gtsoSrv_), safeRelease(gtsoTex_);
    for (auto*& s : lgvSrv_) safeRelease(s);
    for (auto*& t : lgvTex_) safeRelease(t);
    safeRelease(lgvBoxBuf_), safeRelease(lgvSampler_);
    safeRelease(stockLightSrv_), safeRelease(stockLightBuf_), safeRelease(stockTileSrv_), safeRelease(stockTileBuf_);''')
# upload
patch(C, '''void SceneRenderer::setEnvBrdfLut(int width, int height, const uint8_t* rg) {''', '''bool SceneRenderer::setStockLgvAtlas(int width, int depth, const uint32_t* tex0, const uint32_t* tex1, const float* boxes, size_t boxCount) {
    for (auto*& s : lgvSrv_) safeRelease(s);
    for (auto*& t : lgvTex_) safeRelease(t);
    safeRelease(lgvBoxBuf_);
    if (!dev_ || width < 1 || depth < 1 || !tex0 || !tex1 || !boxes || boxCount == 0) return false;
    const UINT w = UINT(width), d = UINT(depth);
    D3D11_TEXTURE3D_DESC td{};
    td.MipLevels = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.Width = w, td.Height = w, td.Depth = d;
    td.Format = DXGI_FORMAT_R11G11B10_FLOAT;  // the game's texture 0 bits as they are (research 105 section 1.3)
    D3D11_SUBRESOURCE_DATA i0{tex0, 4 * w, 4 * w * w};
    bool ok = SUCCEEDED(dev_->CreateTexture3D(&td, &i0, &lgvTex_[0])) && SUCCEEDED(dev_->CreateShaderResourceView(lgvTex_[0], nullptr, &lgvSrv_[0]));
    td.Width = 2 * w, td.Height = 3 * w;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    D3D11_SUBRESOURCE_DATA i1{tex1, 8 * w, 8 * w * 3 * w};
    ok = ok && SUCCEEDED(dev_->CreateTexture3D(&td, &i1, &lgvTex_[1])) && SUCCEEDED(dev_->CreateShaderResourceView(lgvTex_[1], nullptr, &lgvSrv_[1]));
    if (ok) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = UINT(boxCount * 5 * 16);
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA ib{boxes, 0, 0};
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sd.Buffer.FirstElement = 0;
        sd.Buffer.NumElements = UINT(boxCount * 5);
        ok = SUCCEEDED(dev_->CreateBuffer(&bd, &ib, &lgvBoxBuf_)) && SUCCEEDED(dev_->CreateShaderResourceView(lgvBoxBuf_, &sd, &lgvSrv_[2]));
    }
    if (ok && !lgvSampler_) {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;  // the VS's trilinear fetch (texel centres are the probes)
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        ok = SUCCEEDED(dev_->CreateSamplerState(&sd, &lgvSampler_));
    }
    if (!ok) {
        for (auto*& s : lgvSrv_) safeRelease(s);
        for (auto*& t : lgvTex_) safeRelease(t);
        safeRelease(lgvBoxBuf_);
    }
    return ok;
}

void SceneRenderer::setEnvBrdfLut(int width, int height, const uint8_t* rg) {''')
# bind for the vertex shader with the game constants
patch(C, '''    ctx_->VSSetConstantBuffers(4, 1, &cbGame_);  // the clock of the prop waves (vs_tex_inst)''',
      '''    ctx_->VSSetConstantBuffers(4, 1, &cbGame_);  // the clock of the prop waves (vs_tex_inst)
    ctx_->VSSetShaderResources(40, 3, lgvSrv_);   // the props' light grid atlas (vs_tex_inst)
    if (lgvSampler_) ctx_->VSSetSamplers(4, 1, &lgvSampler_);''')
print('ok')
