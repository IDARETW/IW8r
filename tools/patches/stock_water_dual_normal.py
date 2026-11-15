import io

# Research 133 section 5.3: Shipment's periphery water (twc/uk_periph_water_01, the only lit techset binding constants
# 110 / 111) samples its normal map twice with two scrolling UVs (uv x c.y + time x c.zw), fades each layer as its mip
# nears the last, sums the decoded normals weighted by c.x, sums the gloss (saturated: near-mirror) and the cavity. The
# material cache keeps 110 / 111 (constRules 5; water materials resolve again once), the facts carry them, the world
# batch's 256-byte record holds them in rows 12-13 (free in base and prop records), and the Game Lighting PS decodes
# the two layers for a stock base batch with matDual0.x > 0.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


MC = 'src/qtapp/material_cache.cpp'
patch(MC, '''        if (finite && (index == 18 || index == 36 || index == 63 || (index >= 79 && index <= 82) || index == 103 || index == 104))''',
      '''        if (finite && (index == 18 || index == 36 || index == 63 || (index >= 79 && index <= 82) || index == 103 || index == 104 ||
                       index == 110 || index == 111))''')
patch(MC, '''            o.insert("constRules", 4);  // 1: the lit buffer, canonical rows, constants 18 / 36 / 103 / 104, slot 68; 2: bits order; 3: + 63; 4: + 79-82''',
      '''            o.insert("constRules", 5);  // 1: the lit buffer, canonical rows, constants 18 / 36 / 103 / 104, slot 68; 2: bits order; 3: + 63; 4: + 79-82; 5: + 110 / 111''')
patch(MC, '''        // Layered materials (tw4 / twc4) resolved before their layer images and constants were kept (research 97), or''',
      '''        // Resolved before the water's dual normal constants 110 / 111 were kept (research 133 section 5.3): world water
        // materials resolve again once.
        if (!e.failed && o.value("constRules").toInt() < 5 && e.material.startsWith("tw") && e.material.toLower().contains("water")) continue;
        // Layered materials (tw4 / twc4) resolved before their layer images and constants were kept (research 97), or''')

CH = 'src/qtapp/camera_view.h'
patch(CH, '''        std::array<float, 4> uvAnim{0, 0, 0, 0};''', '''        std::array<float, 4> uvAnim{0, 0, 0, 0};
        // The water's dual scrolling normal (research 133 section 5.3): constants 110 and 111 (weight, UV scale, scroll
        // per second u, v); zero = none.
        std::array<float, 8> dualNormal{};''')
MW = 'src/qtapp/main_window.cpp'
patch(MW, '''        if (const auto* c63 = cst(63)) f.uvAnim = *c63;  // UV animation (research 133 section 1)''',
      '''        if (const auto* c63 = cst(63)) f.uvAnim = *c63;  // UV animation (research 133 section 1)
        if (const auto* c110 = cst(110))  // the water's two scrolling normal layers (research 133 section 5.3)
            if (const auto* c111 = cst(111)) {
                std::copy(c110->begin(), c110->end(), f.dualNormal.begin());
                std::copy(c111->begin(), c111->end(), f.dualNormal.begin() + 4);
            }''')
CC = 'src/qtapp/camera_view.cpp'
patch(CC, '''    if (waves) {
        renderer_.setReferenceInstWave(index, f->wave.data());
        uvAnimSeen_ = true;  // the view keeps redrawing while it moves
    }''', '''    if (waves) {
        renderer_.setReferenceInstWave(index, f->wave.data());
        uvAnimSeen_ = true;  // the view keeps redrawing while it moves
    }
    if (f && !propMesh && (f->dualNormal[0] > 0 || f->dualNormal[4] > 0)) {
        renderer_.setReferenceBaseDualNormal(index, f->dualNormal.data());
        uvAnimSeen_ = true;  // the layers scroll
    }''')

H = 'src/viewport/render.h'
patch(H, '''    void setReferenceBaseExtra(size_t batch, const float extra[16], int emissiveTexture, int transmissionTexture);''',
      '''    void setReferenceBaseExtra(size_t batch, const float extra[16], int emissiveTexture, int transmissionTexture);
    // A base batch's dual scrolling normal (research 133 section 5.3): constants 110 and 111; rows 12-13 of its record.
    void setReferenceBaseDualNormal(size_t batch, const float dual[8]);''')
patch(H, '''        float extra[16] = {};
        int emissive = -1, transmission = -1;
    };
    std::vector<RefLayer> refLayers_;''', '''        float extra[16] = {};
        float dual[8] = {};  // setReferenceBaseDualNormal: rows 12-13 (matDual0 / matDual1)
        int emissive = -1, transmission = -1;
    };
    std::vector<RefLayer> refLayers_;''')

C = 'src/viewport/render.cpp'
patch(C, '''                               float4 matE; float4 matTr; float4 matW; float4 matX; };  // matE/Tr/W: research 107 (RefLayer::extra)''',
      '''                               float4 matE; float4 matTr; float4 matW; float4 matX;  // matE/Tr/W: research 107 (RefLayer::extra)
                               float4 matRows8[4];  // rows 8-11: a prop's waves (PropWave, VS)
                               float4 matDual0; float4 matDual1; };  // the water's constants 110 / 111 (research 133 section 5.3)''')
patch(C, '''        if (batchFlags.x > 0.5 && dot(i.n, i.n) > 0.5) {
            float4 nm = nrm0.Sample(samp0, uvS);
            if (baseMat) {
                nmGloss = nm.r, nmCavity = nm.b;
                nm.ga = (nm.ga - 0.5) * layerUvO.x + 0.5;
            }
            float2 sd = nm.ga * 2.0 - 1.0;
            float2 oct = float2(sd.x + sd.y, sd.x - sd.y) * 0.5;
            // The stock decode (research 95, W:425-428): the octahedral direction normalised, its z replaced by
            // 1 - 0.6 |xy|^2 (the normal map's relief is a little flatter than the plain decode).
            const float2 nxy = normalize(float3(oct, max(1.0 - abs(oct.x) - abs(oct.y), 0.0))).xy;
            const float3 nT = normalize(float3(nxy, 1.0 - 0.6 * dot(nxy, nxy)));''', '''        if (batchFlags.x > 0.5 && dot(i.n, i.n) > 0.5) {
            float4 nm = nrm0.Sample(samp0, uvS);
            if (baseMat) {
                nmGloss = nm.r, nmCavity = nm.b;
                nm.ga = (nm.ga - 0.5) * layerUvO.x + 0.5;
            }
            float2 sd = nm.ga * 2.0 - 1.0;
            float2 oct = float2(sd.x + sd.y, sd.x - sd.y) * 0.5;
            // The stock decode (research 95, W:425-428): the octahedral direction normalised, its z replaced by
            // 1 - 0.6 |xy|^2 (the normal map's relief is a little flatter than the plain decode).
            float2 nxy = normalize(float3(oct, max(1.0 - abs(oct.x) - abs(oct.y), 0.0))).xy;
            if (stockRef && baseMat && (matDual0.x > 0.0 || matDual1.x > 0.0)) {
                // The water's two scrolling layers (research 133 section 5.3, PS_9786960c:138-187): uv x scale + time x
                // scroll each, decoded alike, faded as the mip nears the last, summed by weight; gloss and cavity summed.
                uint nw, nh, levels;
                nrm0.GetDimensions(0, nw, nh, levels);
                float2 sum = 0;
                nmGloss = 0, nmCavity = 0;
                [unroll] for (int k = 0; k < 2; ++k) {
                    const float4 c = k == 0 ? matDual0 : matDual1;
                    const float2 uvK = uvS * c.y + gameTime.x * c.zw;
                    float4 nk = nrm0.Sample(samp0, uvK);
                    const float fade = saturate((float(levels) - 1.0 - nrm0.CalculateLevelOfDetail(samp0, uvK)) * 0.5);
                    nmGloss += nk.r * c.x, nmCavity += nk.b * c.x;
                    nk.ga = (nk.ga - 0.5) * layerUvO.x + 0.5;
                    const float2 sk = nk.ga * 2.0 - 1.0;
                    const float2 ok = float2(sk.x + sk.y, sk.x - sk.y) * 0.5;
                    sum += normalize(float3(ok, max(1.0 - abs(ok.x) - abs(ok.y), 0.0))).xy * fade * c.x;
                }
                nxy = sum;
                // Each layer's gloss remapped, then summed by weight and saturated (the water is near-mirror).
                dualGloss = saturate(nmGloss * layerTint.z + layerTint.w * (matDual0.x + matDual1.x));
            }
            const float3 nT = normalize(float3(nxy, 1.0 - 0.6 * dot(nxy, nxy)));''')
patch(C, '''        float nmGloss = -1.0, nmCavity = 1.0;
        if (batchFlags.x > 0.5 && dot(i.n, i.n) > 0.5) {''', '''        float nmGloss = -1.0, nmCavity = 1.0, dualGloss = -1.0;
        if (batchFlags.x > 0.5 && dot(i.n, i.n) > 0.5) {''')
patch(C, '''                if (nmGloss >= 0.0) glossM = saturate(nmGloss * layerTint.z + layerTint.w);''',
      '''                if (nmGloss >= 0.0) glossM = saturate(nmGloss * layerTint.z + layerTint.w);
                if (dualGloss >= 0.0) glossM = dualGloss;''')
patch(C, '''void SceneRenderer::setReferenceInstWave(size_t mesh, const float wave[16]) {''', '''void SceneRenderer::setReferenceBaseDualNormal(size_t batch, const float dual[8]) {
    if (batch >= refLayers_.size() || refLayers_[batch].layer > 0 || refLayers_[batch].decal) return;
    std::memcpy(refLayers_[batch].dual, dual, sizeof(refLayers_[batch].dual));
    layerParamsDirty_ = true;
}

void SceneRenderer::setReferenceInstWave(size_t mesh, const float wave[16]) {''')
patch(C, '''                std::memcpy(&all[j * 64 + 16], refLayers_[k].extra, sizeof(refLayers_[0].extra));
                maskExtraTerms(&all[j * 64 + 16]);''', '''                std::memcpy(&all[j * 64 + 16], refLayers_[k].extra, sizeof(refLayers_[0].extra));
                maskExtraTerms(&all[j * 64 + 16]);
                std::memcpy(&all[j * 64 + 48], refLayers_[k].dual, sizeof(refLayers_[0].dual));  // rows 12-13''')
print('ok')
