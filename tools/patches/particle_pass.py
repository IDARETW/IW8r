import io

# Research 138: the effect preview draws particles through their own pass: per batch a sprite image, quads in world
# space with a float colour (HDR intensity survives), one premultiplied blend (ONE, INV_SRC_ALPHA) for lit particles
# (alpha) and unlit ones (output alpha 0 = additive), depth test without depth write, after the deferred blended scene
# (in the HDR scene with Game Lighting, on the frame otherwise).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if s.count(a) == 0 and b in s:
        return
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/viewport/render.h'
patch(H, '''struct GpuTexVertex {''', '''// An effect particle's corner (research 138): world position, atlas UV, linear colour x intensity (may exceed 1), alpha.
struct ParticleVertex {
    float x, y, z;
    float u, v;
    float r, g, b, a;
};
// One sprite image's particles, already sorted far to near. lit: the colour image's alpha blends and the scene's light
// scales it; else the emissive image adds (unlit effect sets: output alpha 0, research 138 section 6).
struct ParticleBatch {
    int texture = -1;
    bool lit = true;
    float emissive[3] = {1, 1, 1};  // an unlit material's emissive scale (constant 36) in Game Lighting
    std::vector<ParticleVertex> v;  // 6 per quad
};

struct GpuTexVertex {''')
patch(H, '''    void setGameAo(bool on) { aoOn_ = on; }''', '''    void setGameAo(bool on) { aoOn_ = on; }
    // The effect preview's particles of this frame (drawn after the scene's blended batches; empty = none).
    void setParticles(std::vector<ParticleBatch> batches) { particles_ = std::move(batches); }
    size_t particleQuads() const {
        size_t n = 0;
        for (const auto& b : particles_) n += b.v.size() / 6;
        return n;
    }''')
patch(H, '''    ID3D11BlendState* blendAdd_ = nullptr;  // additive (ONE, ONE) for blend 2 batches''',
      '''    ID3D11BlendState* blendAdd_ = nullptr;  // additive (ONE, ONE) for blend 2 batches
    // The particle pass (research 138).
    std::vector<ParticleBatch> particles_;
    ID3D11VertexShader* partVs_ = nullptr;
    ID3D11PixelShader* partPs_ = nullptr;
    ID3D11InputLayout* partLayout_ = nullptr;
    ID3D11Buffer* partVb_ = nullptr;
    size_t partVbBytes_ = 0;
    ID3D11Buffer* partCb_ = nullptr;
    ID3D11BlendState* blendPremul_ = nullptr;  // ONE, INV_SRC_ALPHA
    bool partTried_ = false;
    bool createParticleResources();
    void drawParticles(const float viewProj[16]);''')

R = 'src/viewport/render.cpp'
patch(R, '''const char* kAoShader = R"(''', '''// Effect particles (research 138): world-space quads, the sprite's texel x the particle colour. pMode.x = 1: linear
// output into the HDR scene (Game Lighting), else display space; pMode.y = 1: unlit (additive, output alpha 0).
const char* kParticleShader = R"(
cbuffer Part : register(b0) { row_major float4x4 pVp; float4 pLight; float4 pMode; };
Texture2D pTex : register(t0);
SamplerState pSamp : register(s0);
struct VI { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; };
struct PI { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; };
PI vs_part(VI i) { PI o; o.pos = mul(float4(i.pos, 1.0), pVp); o.uv = i.uv; o.col = i.col; return o; }
float3 linOf(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
float4 ps_part(PI i) : SV_Target {
    const float4 t = pTex.Sample(pSamp, i.uv);
    const bool hdr = pMode.x > 0.5;
    const float3 tex = hdr ? linOf(saturate(t.rgb)) : t.rgb;
    const float3 col = hdr ? i.col.rgb : pow(max(i.col.rgb, 0.0), 1.0 / 2.2);
    if (pMode.y > 0.5) return float4(tex * col * pLight.rgb, 0.0);  // unlit: adds (pLight = the emissive scale)
    const float a = saturate(t.a * i.col.a);
    return float4(tex * col * pLight.rgb * a, a);                  // lit: premultiplied
}
)";

const char* kAoShader = R"(''')
patch(R, '''    safeRelease(blendAdd_);''', '''    safeRelease(blendAdd_);
    safeRelease(partVs_), safeRelease(partPs_), safeRelease(partLayout_), safeRelease(partVb_), safeRelease(partCb_), safeRelease(blendPremul_);''')
patch(R, '''bool SceneRenderer::createAoResources() {''', '''bool SceneRenderer::createParticleResources() {
    if (partVs_ && partPs_ && partLayout_ && partCb_ && blendPremul_) return true;
    if (partTried_ || !dev_) return false;
    partTried_ = true;
    ID3DBlob *vsb = nullptr, *psb = nullptr, *errb = nullptr;
    const bool ok = SUCCEEDED(D3DCompile(kParticleShader, std::strlen(kParticleShader), "iw8r_part", nullptr, nullptr, "vs_part", "vs_5_0", 0, 0, &vsb, &errb)) &&
                    SUCCEEDED(D3DCompile(kParticleShader, std::strlen(kParticleShader), "iw8r_part", nullptr, nullptr, "ps_part", "ps_5_0", 0, 0, &psb, &errb));
    if (errb) OutputDebugStringA(static_cast<const char*>(errb->GetBufferPointer()));
    safeRelease(errb);
    if (ok) {
        dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &partVs_);
        dev_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &partPs_);
        const D3D11_INPUT_ELEMENT_DESC il[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        dev_->CreateInputLayout(il, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &partLayout_);
    }
    safeRelease(vsb), safeRelease(psb);
    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = 96;
    cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    dev_->CreateBuffer(&cbd, nullptr, &partCb_);
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    dev_->CreateBlendState(&bd, &blendPremul_);
    return partVs_ && partPs_ && partLayout_ && partCb_ && blendPremul_;
}

void SceneRenderer::drawParticles(const float viewProj[16]) {
    if (particles_.empty() || !createParticleResources()) return;
    size_t verts = 0;
    for (const auto& b : particles_) verts += b.v.size();
    if (verts == 0) return;
    const size_t bytes = verts * sizeof(ParticleVertex);
    if (!partVb_ || partVbBytes_ < bytes) {
        safeRelease(partVb_);
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = UINT(std::max<size_t>(bytes, 64 * 1024));
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev_->CreateBuffer(&bd, nullptr, &partVb_))) return;
        partVbBytes_ = bd.ByteWidth;
    }
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(ctx_->Map(partVb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
    size_t at = 0;
    for (const auto& b : particles_) {
        std::memcpy(static_cast<char*>(ms.pData) + at * sizeof(ParticleVertex), b.v.data(), b.v.size() * sizeof(ParticleVertex));
        at += b.v.size();
    }
    ctx_->Unmap(partVb_, 0);
    const bool hdr = hdrFrame_;
    // A lit particle takes the scene's ambient and a share of the sun in Game Lighting (the game lights each particle
    // from the scene; research 138 section 6), 1 elsewhere.
    float lit[3] = {1, 1, 1};
    if (game_.on) {
        const float up = std::max(0.0f, game_.sunToward[2]);
        for (int c = 0; c < 3; ++c) lit[c] = game_.sky[c] + game_.sunColor[c] * (0.15f + 0.2f * up);
        if (!hdr) for (int c = 0; c < 3; ++c) lit[c] = std::min(1.5f, std::pow(std::max(lit[c], 0.0f), 1.0f / 2.2f));
    }
    ctx_->IASetInputLayout(partLayout_);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const UINT stride = sizeof(ParticleVertex), offset = 0;
    ctx_->IASetVertexBuffers(0, 1, &partVb_, &stride, &offset);
    ctx_->VSSetShader(partVs_, nullptr, 0);
    ctx_->PSSetShader(partPs_, nullptr, 0);
    ctx_->VSSetConstantBuffers(0, 1, &partCb_);
    ctx_->PSSetConstantBuffers(0, 1, &partCb_);
    ctx_->PSSetSamplers(0, 1, &sampler_);
    ctx_->RSSetState(rsTris_);
    ctx_->OMSetDepthStencilState(dsTest_, 0);
    const float bf[4] = {0, 0, 0, 0};
    ctx_->OMSetBlendState(blendPremul_, bf, 0xffffffff);
    UINT first = 0;
    for (const auto& b : particles_) {
        const UINT n = UINT(b.v.size());
        const int tex = b.texture >= 0 && size_t(b.texture) < textures_.size() ? b.texture : whiteTex_;
        if (n && tex >= 0 && size_t(tex) < textures_.size()) {
            float cb[24] = {};
            std::memcpy(cb, viewProj, 16 * sizeof(float));
            for (int c = 0; c < 3; ++c) cb[16 + c] = b.lit ? lit[c] : (game_.on ? b.emissive[c] : 1.0f);
            cb[19] = 1.0f;
            cb[20] = hdr ? 1.0f : 0.0f, cb[21] = b.lit ? 0.0f : 1.0f;
            ctx_->UpdateSubresource(partCb_, 0, nullptr, cb, 0, 0);
            ctx_->PSSetShaderResources(0, 1, &textures_[size_t(tex)]);
            ctx_->Draw(n, first);
            stats_.drawCalls++;
        }
        first += n;
    }
    ctx_->OMSetBlendState(blend_, bf, 0xffffffff);
    ctx_->OMSetDepthStencilState(dsWrite_, 0);
    ID3D11ShaderResourceView* none = nullptr;
    ctx_->PSSetShaderResources(0, 1, &none);
    ctx_->IASetInputLayout(layout_);
    ctx_->VSSetShader(vs_, nullptr, 0);
    ctx_->PSSetShader(ps_, nullptr, 0);
    ctx_->VSSetConstantBuffers(0, 1, &cb_);  // the scene's buffer again for what follows
    ctx_->PSSetConstantBuffers(0, 1, &cb_);
}

bool SceneRenderer::createAoResources() {''')
patch(R, '''    if (deferSceneBlended) drawSceneTex(firstBlendedScene, drawOrder_.size());  // the map's blended batches, last''',
      '''    if (deferSceneBlended) drawSceneTex(firstBlendedScene, drawOrder_.size());  // the map's blended batches, last
    if (!particles_.empty() && !in.clean) drawParticles(viewProj);                // the effect preview (research 138)''')
print('ok')
