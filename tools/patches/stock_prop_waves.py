import io

# Research 133 sections 5.2 and 6: the flag and foliage vertex waves of stock static models (constants 79-82 of the
# lit vertex shader): three sines over the game clock and the UV, weighted by the vertex colour alpha, along the vertex
# normal; each placement gets its own speed from its origin (the static-model VS: (x + y + z) / 4096). The viewport's
# made-up sway (foliage_sway) stays for own maps.
#   - material cache: constants 79-82 kept; the vertex-colour model families (the ones that carry wave weights)
#     resolve again once (constRules 4);
#   - facts: wave[16] (c79..c82); prop meshes: GpuTexInstanced::wave, uploaded at floats 32..47 of the per-mesh record;
#   - vs_tex_inst reads them through cbuffer PropWave (b8, the same per-mesh range) and the clock (Game b4, now also
#     bound to the vertex shaders); the vertex colour alpha reaches the prop vertex.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


# --- material cache
M = 'src/qtapp/material_cache.cpp'
patch(M, '''        if (finite && (index == 18 || index == 36 || index == 63 || index == 103 || index == 104)) work.consts[index] = f;''',
      '''        if (finite && (index == 18 || index == 36 || index == 63 || (index >= 79 && index <= 82) || index == 103 || index == 104))
            work.consts[index] = f;''')
patch(M, '''        if (!e.failed && e.techset.section('/', -1).startsWith("iw8rf_")) continue;''',
      '''        if (!e.failed && e.techset.section('/', -1).startsWith("iw8rf_")) continue;
        // Resolved before the wave constants 79-82 were kept (research 133 section 6): the vertex-colour model families
        // (tmco, tm2co, mco, ...: the vertex alpha is the wave weight) resolve again once.
        if (!e.failed && o.value("constRules").toInt() < 4) {
            const QString prefix = e.material.section('/', 0, 0);
            if (prefix.contains('c') && (prefix.startsWith('m') || prefix.startsWith("tm"))) continue;
        }''')
patch(M, '''            o.insert("constRules", 3);  // 1: the lit buffer, canonical rows, constants 18 / 36 / 103 / 104, slot 68; 2: bits order; 3: + 63''',
      '''            o.insert("constRules", 4);  // 1: the lit buffer, canonical rows, constants 18 / 36 / 103 / 104, slot 68; 2: bits order; 3: + 63; 4: + 79-82''')

# --- facts
patch('src/qtapp/camera_view.h', '''        std::array<float, 4> uvAnim{0, 0, 0, 0};''', '''        std::array<float, 4> uvAnim{0, 0, 0, 0};
        // Research 133 section 6: the flag / foliage waves, constants 79, 80, 81, 82 (zero = none).
        std::array<float, 16> wave{};''')
patch('src/qtapp/main_window.cpp', '''        if (const auto* c63 = cst(63)) f.uvAnim = *c63;  // UV animation (research 133 section 1)''',
      '''        if (const auto* c63 = cst(63)) f.uvAnim = *c63;  // UV animation (research 133 section 1)
        for (int k = 0; k < 4; ++k)  // the flag / foliage waves (research 133 section 6)
            if (const auto* c = cst(79 + k)) std::copy(c->begin(), c->end(), f.wave.begin() + 4 * k);''')
patch('src/qtapp/main_window.cpp', '''                                pv.color = byte(ms.color[v][0]) | (byte(ms.color[v][1]) << 8) | (byte(ms.color[v][2]) << 16) | 0xff000000u;''',
      '''                                // Alpha: the weight of the flag / foliage waves (research 133 section 4).
                                pv.color = byte(ms.color[v][0]) | (byte(ms.color[v][1]) << 8) | (byte(ms.color[v][2]) << 16) |
                                           (byte(ms.color[v][3]) << 24);''')
C = 'src/qtapp/camera_view.cpp'
patch(C, '''    const bool uvAnim = f && (f->uvAnim[0] != 0 || f->uvAnim[1] != 0 || f->uvAnim[2] != 0);
    if (!f || (f->emissiveScale[3] <= 0 && f->transTint[3] <= 0 && f->wrap[3] <= 0 && !f->unlit && !uvAnim)) return true;
    if (uvAnim) uvAnimSeen_ = true;''', '''    const bool uvAnim = f && (f->uvAnim[0] != 0 || f->uvAnim[1] != 0 || f->uvAnim[2] != 0);
    const bool waves = f && propMesh && (f->wave[7] != 0 || f->wave[11] != 0 || f->wave[15] != 0);  // c80..c82 amplitudes
    if (waves) {
        renderer_.setReferenceInstWave(index, f->wave.data());
        uvAnimSeen_ = true;  // the view keeps redrawing while it moves
    }
    if (!f || (f->emissiveScale[3] <= 0 && f->transTint[3] <= 0 && f->wrap[3] <= 0 && !f->unlit && !uvAnim)) return true;
    if (uvAnim) uvAnimSeen_ = true;''')

# --- renderer
H = 'src/viewport/render.h'
patch(H, '''        float extra[16] = {};       // setReferenceInstExtra (RefLayer::extra)''',
      '''        float extra[16] = {};       // setReferenceInstExtra (RefLayer::extra)
        float wave[16] = {};        // setReferenceInstWave: constants 79-82 (research 133 section 6)''')
patch(H, '''    void setReferenceInstExtra(size_t mesh, const float extra[16], int emissiveTexture, int transmissionTexture);''',
      '''    void setReferenceInstExtra(size_t mesh, const float extra[16], int emissiveTexture, int transmissionTexture);
    // A prop mesh's flag / foliage waves (research 133 section 6): constants 79, 80, 81, 82.
    void setReferenceInstWave(size_t mesh, const float wave[16]);''')
R = 'src/viewport/render.cpp'
patch(R, '''void SceneRenderer::setReferenceInstExtra(size_t mesh, const float extra[16], int emissiveTexture, int transmissionTexture) {''',
      '''void SceneRenderer::setReferenceInstWave(size_t mesh, const float wave[16]) {
    if (mesh >= refTexInst_.size()) return;
    std::memcpy(refTexInst_[mesh].wave, wave, sizeof(refTexInst_[mesh].wave));
    instParamsDirty_ = true;
}

void SceneRenderer::setReferenceInstExtra(size_t mesh, const float extra[16], int emissiveTexture, int transmissionTexture) {''')
patch(R, '''                std::memcpy(&all[j * 64 + 16], gi.extra, sizeof(gi.extra));''', '''                std::memcpy(&all[j * 64 + 16], gi.extra, sizeof(gi.extra));
                std::memcpy(&all[j * 64 + 32], gi.wave, sizeof(gi.wave));  // PropWave (b8) in vs_tex_inst''')
patch(R, '''    ctx_->PSSetConstantBuffers(4, 1, &cbGame_);''', '''    ctx_->PSSetConstantBuffers(4, 1, &cbGame_);
    ctx_->VSSetConstantBuffers(4, 1, &cbGame_);  // the clock of the prop waves (vs_tex_inst)''')
# Both per-mesh bindings (shading / pre-pass and the sun shadow pass) give the vertex shader the same range on b8.
patch(R, '''                ctx1_->PSSetConstantBuffers1(7, 1, &cbInstAll_[mi / kInstPerCb], &first, &num);''',
      '''                ctx1_->PSSetConstantBuffers1(7, 1, &cbInstAll_[mi / kInstPerCb], &first, &num);
                ctx1_->VSSetConstantBuffers1(8, 1, &cbInstAll_[mi / kInstPerCb], &first, &num);  // PropWave''')
patch(R, '''                ctx1_->PSSetConstantBuffers1(7, 1, &cbInstAll_[mi / 256], &first, &num);''',
      '''                ctx1_->PSSetConstantBuffers1(7, 1, &cbInstAll_[mi / 256], &first, &num);
                ctx1_->VSSetConstantBuffers1(8, 1, &cbInstAll_[mi / 256], &first, &num);  // PropWave''')
patch(R, '''    if (instCb && cbLayer_) ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);  // the plain (zero) layer buffer''',
      '''    if (instCb && cbLayer_) ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);  // the plain (zero) layer buffer
    if (instCb) {
        ID3D11Buffer* none = nullptr;  // no waves for what is drawn next
        ctx_->VSSetConstantBuffers(8, 1, &none);
    }''')
patch(R, '''    if (instCb && cbLayer_) ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);
    ctx_->IASetInputLayout(layout_);''', '''    if (instCb && cbLayer_) ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);
    if (instCb) {
        ID3D11Buffer* none = nullptr;
        ctx_->VSSetConstantBuffers(8, 1, &none);
    }
    ctx_->IASetInputLayout(layout_);''')
patch(R, '''struct VSInInst { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float4 vcol : COLOR1; float4 r0 : INSTROW0; float4 r1 : INSTROW1; float4 r2 : INSTROW2;''',
      '''// A prop mesh's flag / foliage waves (research 133 section 6): constants 79-82 at rows 8..11 of its per-mesh record
// (the same range as cbuffer Layer, bound to b8 for the vertex shader; unbound = zero = no waves).
cbuffer PropWave : register(b8) { float4 pwRows[8]; float4 wave79; float4 wave80; float4 wave81; float4 wave82; };
struct VSInInst { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float4 vcol : COLOR1; float4 r0 : INSTROW0; float4 r1 : INSTROW1; float4 r2 : INSTROW2;''')
patch(R, '''PSIn vs_tex_inst(VSInInst i) {
    float4 p = float4(i.pos, 1.0);''', '''PSIn vs_tex_inst(VSInInst i) {
    float4 p = float4(i.pos, 1.0);
    if (wave80.w != 0 || wave81.w != 0 || wave82.w != 0) {
        // The static-model VS (research 133 section 5.2): speed from the placement's origin, three sines over the clock
        // and the UV, weighted by the vertex colour alpha, along the vertex normal.
        const float s = (i.r0.w + i.r1.w + i.r2.w) / 4096.0;
        const float tri = abs(1.0 - 2.0 * frac(wave79.z * s)) * 2.0 - 1.0;
        const float phase = (wave79.x + wave79.w * tri) * (gameTime.x + wave79.y);
        const float2 q = 1.0 - i.uv;
        const float h = sin(phase * wave80.z + dot(q, wave80.xy)) * wave80.w + sin(phase * wave81.z + dot(q, wave81.xy)) * wave81.w +
                        sin(phase * wave82.z + dot(q, wave82.xy)) * wave82.w;
        p.xyz += h * i.vcol.a * i.n;
    }''')
print('ok')
