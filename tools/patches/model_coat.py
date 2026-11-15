import io

# Research 137: a model layer of kind Code is a clear coat: a second specular lobe (its own normal / gloss map or the
# vertex normal and gloss' 0.193774, F0 0.035614) weighted like a layer (alpha, slot-126 mask); it dims the ambient
# part by (1 - b Ec)^2 and adds b Ec x its environment reflection; the sun and lamps ignore it. Its constants sit in
# constant table rows (L, 9 + L, 54 + L), so code-layer materials get canonical layer rows (constRules 6; they resolve
# again once). The coat has its own block in the prop record (rows 26-29) and textures t52 (map) / t53 (alpha).
# Left out: the emissive kind (lit glass). IW8R_NO_COAT: none (A/B).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


# ---- core: the token hex per layer
patch('src/core/stock_layered_material.h', '''    std::vector<bool> uv1;                // the layer samples the second UV set (hex bit 0x80000; a `2` family only)''',
      '''    std::vector<bool> uv1;                // the layer samples the second UV set (hex bit 0x80000; a `2` family only)
    std::vector<uint32_t> hex;            // the layer token's hex (code layers, research 137: 0x10 map, 0x08 alpha, 0x20 emissive)''')
patch('src/core/stock_layered_material.cpp', '''    t.uv1.assign(size_t(n), false);''', '''    t.uv1.assign(size_t(n), false);
    t.hex.assign(size_t(n), 0u);''')
patch('src/core/stock_layered_material.cpp', '''        t.uv1[size_t(L)] = secondUvFamily && (hex & 0x80000ull) != 0;''', '''        t.uv1[size_t(L)] = secondUvFamily && (hex & 0x80000ull) != 0;
        t.hex[size_t(L)] = uint32_t(hex);''')
patch('tests/stock_layered_material_tests.cpp', '''    CHECK(c.layers == 3 && !c.uv1[1] && c.kind[2] == ModelLayerKind::Code);''', '''    CHECK(c.layers == 3 && !c.uv1[1] && c.kind[2] == ModelLayerKind::Code);
    CHECK(c.hex.size() == 3 && c.hex[2] == 0x180153u && (c.hex[2] & 0x10u) && c.hex[1] == 0x3818015fu);  // research 137: 0x10 map''')

# ---- material cache: canonical layer rows for code-layer materials
MC = 'src/qtapp/material_cache.cpp'
patch(MC, '''    if (work.modelLayers >= 2) work.layerPsData = work.psData;  // the raw lit buffer, before the single-layer rows below''',
      '''    if (work.modelLayers >= 2) work.layerPsData = work.psData;  // the raw lit buffer, before the single-layer rows below
    // Research 137: in technique sets with a code layer (a clear coat) the layer constants do not sit on rows 1 + 3 L
    // .. 3 + 3 L: rebuild them from the constant table (row 1 + 3 L <- L, 2 + 3 L <- 9 + L, 3 + 3 L <- 54 + L).
    if (work.modelLayers >= 2 && !constBytes.empty()) {
        const ModelLayerTokens tk = parseModelLayerTokens(work.techset.toStdString());
        bool code = false;
        for (const ModelLayerKind k : tk.kind) code = code || k == ModelLayerKind::Code;
        if (code) {
            QByteArray canon = work.layerPsData;
            while (canon.size() < 16 * (1 + 3 * work.modelLayers)) canon.append(char(0));
            for (int L = 0; L < work.modelLayers; ++L) {
                const int src[3] = {L, 9 + L, 54 + L};
                for (int r = 0; r < 3; ++r)
                    if (const auto it = constBytes.find(src[r]); it != constBytes.end()) canon.replace(16 * (1 + 3 * L + r), 16, it->second);
            }
            work.layerPsData = canon;
        }
    }''')
patch(MC, '''            o.insert("constRules", 5);  // 1: the lit buffer, canonical rows, constants 18 / 36 / 103 / 104, slot 68; 2: bits order; 3: + 63; 4: + 79-82; 5: + 110 / 111''',
      '''            o.insert("constRules", 6);  // 1: the lit buffer, canonical rows, constants 18 / 36 / 103 / 104, slot 68; 2: bits order; 3: + 63; 4: + 79-82; 5: + 110 / 111; 6: code-layer rows''')
patch(MC, '''        if (!e.failed && o.value("constRules").toInt() < 5 && e.material.startsWith("tw") && e.material.toLower().contains("water")) continue;''',
      '''        if (!e.failed && o.value("constRules").toInt() < 5 && e.material.startsWith("tw") && e.material.toLower().contains("water")) continue;
        // Resolved before code layers (clear coats, research 137) got canonical layer rows: those resolve again once.
        if (!e.failed && o.value("constRules").toInt() < 6 && o.value("modelLayers").toInt() >= 2) {
            const ModelLayerTokens tk = parseModelLayerTokens(e.techset.toStdString());
            bool code = false;
            for (const ModelLayerKind k : tk.kind) code = code || k == ModelLayerKind::Code;
            if (code) continue;
        }''')

# ---- camera: a code layer becomes the coat (kind 4)
CH = 'src/qtapp/camera_view.h'
patch(CH, '''        std::vector<char> layerUv1;''', '''        std::vector<char> layerUv1;
        std::vector<uint32_t> layerHex;  // the layer token's hex (research 137: 0x10 map, 0x20 emissive)''')
MW = 'src/qtapp/main_window.cpp'
patch(MW, '''            for (const bool u : tk.uv1) f.layerUv1.push_back(u ? 1 : 0);''', '''            for (const bool u : tk.uv1) f.layerUv1.push_back(u ? 1 : 0);
            f.layerHex = tk.hex;''')
CC = 'src/qtapp/camera_view.cpp'
patch(CC, '''        const int kind = facts && size_t(L) < facts->layerKind.size() ? facts->layerKind[size_t(L)] : 0;
        if (kind == 2) continue;  // a code layer (vehicle dirt / rubble; not decoded)''', '''        const int kind = facts && size_t(L) < facts->layerKind.size() ? facts->layerKind[size_t(L)] : 0;
        const uint32_t hex = facts && size_t(L) < facts->layerHex.size() ? facts->layerHex[size_t(L)] : 0u;
        // A code layer is a clear coat (research 137): its own block, no colour; the emissive kind is left out.
        if (kind == 2 && (hex & 0x20u)) continue;''')
patch(CC, '''        float drawKind = kind == 1 ? 2.0f : 0.0f;''', '''        float drawKind = kind == 1 ? 2.0f : kind == 2 ? 4.0f : 0.0f;''')
patch(CC, '''        if (noColour && drawKind != 3.0f) continue;  // no colour image
        // The layer's normal + gloss + cavity map (research 136): only a folded layer reads it.
        const int nid = normalFor(ck);
        if (!nrmIds_.count(ck)) return false;  // still loading''', '''        if (noColour && drawKind < 2.5f) continue;  // no colour image
        // The layer's normal + gloss + cavity map (research 136): only a folded layer reads it. A coat has one only with
        // hex bit 0x10 (research 137); without it the coat takes the vertex normal and a fixed gloss.
        int nid = -1;
        if (drawKind != 4.0f || (hex & 0x10u)) {
            nid = normalFor(ck);
            if (!nrmIds_.count(ck)) return false;  // still loading
        }''')

# ---- renderer: the coat block
H = 'src/viewport/render.h'
patch(H, '''        int foldMask = -1;''', '''        int foldMask = -1;
        // Research 137: the clear coat (rows 26-29: (on, flags, mask channel + 1, map), UV transform, (offset, second
        // UV, map), (normal strength, gloss scale, gloss bias, 0)) and its map / alpha images.
        float coat[16] = {};
        int coatTex[2] = {-1, -1};''')
patch(H, '''        std::fill(std::begin(g.foldTex), std::end(g.foldTex), -1);
        g.foldMask = -1;''', '''        std::fill(std::begin(g.foldTex), std::end(g.foldTex), -1);
        g.foldMask = -1;
        std::fill(std::begin(g.coat), std::end(g.coat), 0.0f);
        g.coatTex[0] = g.coatTex[1] = -1;
        static const bool noCoat = std::getenv("IW8R_NO_COAT") != nullptr;''')
patch(H, '''        for (PropLayer& L : layers) {
            if (noFold || f >= 2) {''', '''        for (PropLayer& L : layers) {
            if (L.params[3] > 3.5f) {  // the clear coat: its own block (research 137)
                if (noCoat || g.coat[0] > 0.5f) continue;
                const bool map = L.normal >= 0;
                const float c[16] = {1.0f, L.params[1], L.aux[1], map ? 1.0f : 0.0f, L.params[8], L.params[9], L.params[10], L.params[11],
                                     L.params[12], L.params[13], L.aux[0], map ? 1.0f : 0.0f, L.nrm[1], L.nrm[2], L.nrm[3], 0.0f};
                std::copy(c, c + 16, g.coat);
                g.coatTex[0] = L.normal, g.coatTex[1] = L.reveal;
                if (L.mask >= 0) g.foldMask = L.mask;
                continue;
            }
            if (noFold || f >= 2) {''')

R = 'src/viewport/render.cpp'
patch(R, '''                               float4 foldL[10]; };  // rows 16-25: a prop mesh's two folded layers (research 136)''',
      '''                               float4 foldL[10];  // rows 16-25: a prop mesh's two folded layers (research 136)
                               float4 coatL[4]; };  // rows 26-29: its clear coat (research 137)''')
patch(R, '''Texture2D foldT2 : register(t51);''', '''Texture2D foldT2 : register(t51);
Texture2D coatMap : register(t52);    // the clear coat's normal + gloss + cavity map (research 137)
Texture2D coatAlpha : register(t53);  // its alpha (R)''')
patch(R, '''            c = diffAlbedo * indirect * kd * (1.0 - fmax) * aoMB + Fenv * S;''', '''            c = diffAlbedo * indirect * kd * (1.0 - fmax) * aoMB + Fenv * S;
            // A prop's clear coat (research 137 section 1.2): a second environment lobe over the finished ambient part,
            // c x (1 - b Ec)^2 + b Ec x its reflection; the sun and lamps below ignore it.
            if (i.vc.w > 0.5 && coatL[0].x > 0.5 && layerP.x < 0.5) {
                const float2 cu = foldUv(i, coatL[1], coatL[2]);
                const float bC = foldWeight(i, float4(1.0, coatL[0].y, 0.0, 0.0), float4(1.0, 1.0, 1.0, coatL[0].z), coatAlpha.Sample(samp0, cu).rg);
                if (bC >= 1.0 / 255.0) {
                    float3 NC = Nf;
                    float gpC = 0.193774, cavC = 1.0;
                    if (coatL[2].w > 0.5) {
                        float4 m = coatMap.Sample(samp0, cu);
                        gpC = 1.0 - sqrt(1.0 - saturate(m.r * coatL[3].y + coatL[3].z));
                        cavC = m.b;
                        m.ga = (m.ga - 0.5) * coatL[3].x + 0.5;
                        const float2 sdC = m.ga * 2.0 - 1.0;
                        const float2 oC = float2(sdC.x + sdC.y, sdC.x - sdC.y) * 0.5;
                        const float2 xyC = normalize(float3(oC, max(1.0 - abs(oC.x) - abs(oC.y), 0.0))).xy;
                        const float3 nTC = normalize(float3(xyC, 1.0 - 0.6 * dot(xyC, xyC)));
                        const float2 slC = nTC.xy / max(nTC.z, 0.02);
                        const float3 dp1 = ddx(i.w), dp2 = ddy(i.w);
                        const float2 du1 = ddx(cu), du2 = ddy(cu);
                        const float3 p2 = cross(dp2, Nf), p1 = cross(Nf, dp1);
                        float3 TC = p2 * du1.x + p1 * du2.x, BC = p2 * du1.y + p1 * du2.y;
                        const float scC = rsqrt(max(max(dot(TC, TC), dot(BC, BC)), 1e-20));
                        NC = normalize(TC * scC * slC.x + BC * scC * slC.y + Nf);
                    }
                    const float2 abC = gameEnvBrdf(saturate(dot(NC, Vs)), gpC);
                    const float Ec = 0.035614 * abC.x + abC.y;
                    const float3 RC = reflect(-Vs, NC);
                    const float3 LrC = (indirect - stockDirN) + stockB * saturate(dot(RC, stockDw));
                    const float a2C = 2.0 / (2.0 + exp2(20.0 * gpC));
                    const float soC = gameSun.w > 0.5
                        ? gtsoLut.SampleLevel(lutSamp, float3(max(sqrt(saturate(1.0 - aoV)), sqrt(saturate(1.0 - cavC))), a2C, saturate(dot(Nf, RC))), 0).r
                        : min(aoV, cavC);
                    float3 SC = lerp(0.18 * LrC, LrC, soC);
                    const float fbC = saturate(2.0 - 10.0 * gpC);
                    if (fbC < 1.0 && probeCount.x > 0.5) {
                        float3 PC = stockProbeLight(i.w, RC, gpC) * dot(LrC, float3(0.2126, 0.7152, 0.0722));
                        PC = lerp(PC, LrC, fbC);
                        SC = lerp(0.18 * lerp(PC, LrC, 0.85), PC, soC);
                    }
                    const float tC = 1.0 - bC * Ec;
                    c = c * tC * tC + bC * Ec * SC;
                }
            }''')
patch(R, '''                std::memcpy(&all[j * 128 + 64], gi.fold, sizeof(gi.fold));  // rows 16-25''',
      '''                std::memcpy(&all[j * 128 + 64], gi.fold, sizeof(gi.fold));  // rows 16-25
                std::memcpy(&all[j * 128 + 104], gi.coat, sizeof(gi.coat));  // rows 26-29 (research 137)''')
patch(R, '''            ctx_->PSSetShaderResources(44, 8, fv);''', '''            ctx_->PSSetShaderResources(44, 8, fv);
            ID3D11ShaderResourceView* cv[2] = {g.coatTex[0] >= 0 && size_t(g.coatTex[0]) < textures_.size() ? textures_[size_t(g.coatTex[0])]
                                                : flatNrmTex_ >= 0 ? textures_[size_t(flatNrmTex_)] : nullptr,
                                               g.coatTex[1] >= 0 && size_t(g.coatTex[1]) < textures_.size() ? textures_[size_t(g.coatTex[1])]
                                                : whiteFullTex_ >= 0 ? textures_[size_t(whiteFullTex_)] : nullptr};
            ctx_->PSSetShaderResources(52, 2, cv);''')
print('ok')
