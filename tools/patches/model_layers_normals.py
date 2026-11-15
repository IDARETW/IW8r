import io

# Research 136 sections 1.2-1.4, the rest of the fold: a folded layer's normal, cavity and gloss (flags 1 / 2 / 4, the
# normal mode: 0 lerp, 1 add, 2 none; overlays always add and pull cavity and gloss toward the lower value), so colour
# modes 2 / 3 (normal / gloss only) are drawn too. Rows 24-25 of the per-mesh record: per folded layer (normal mode + 1,
# normal strength, gloss scale, gloss bias); the layers' normal maps at t48 / t49. Mode 2 / 3 layers past the second
# are not drawn (a blended pass cannot change the normal).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/viewport/render.h'
patch(H, '''    int mask = -1;          // slot 126 (research 136), -1: none
    float aux[4] = {};      // row 8 of the layer buffer: the second UV set (1), the mask channel + 1
};''', '''    int mask = -1;          // slot 126 (research 136), -1: none
    float aux[4] = {};      // row 8 of the layer buffer: the second UV set (1), the mask channel + 1
    int normal = -1;        // the layer's normal + gloss + cavity map (folded layers only)
    float nrm[4] = {};      // normal mode + 1 (0: none), normal strength, gloss scale, gloss bias
};''')
patch(H, '''        float fold[32] = {};
        int foldTex[4] = {-1, -1, -1, -1};
        int foldMask = -1;''', '''        float fold[40] = {};  // rows 16-23 colour, rows 24-25 normal (one per folded layer)
        int foldTex[6] = {-1, -1, -1, -1, -1, -1};  // colour 1, alpha / reveal 1, colour 2, alpha / reveal 2, normal 1, normal 2
        int foldMask = -1;''')
patch(H, '''        for (PropLayer& L : layers) {
            if (noFold || f >= 2) {
                rest.push_back(std::move(L));
                continue;
            }''', '''        for (PropLayer& L : layers) {
            if (noFold || f >= 2) {
                if (L.params[3] < 2.5f) rest.push_back(std::move(L));  // kind 3 (normal / gloss only): not as a pass
                continue;
            }''')
patch(H, '''            g.foldTex[2 * f] = L.texture, g.foldTex[2 * f + 1] = L.reveal;
            if (L.mask >= 0) g.foldMask = L.mask;''', '''            g.foldTex[2 * f] = L.texture, g.foldTex[2 * f + 1] = L.reveal, g.foldTex[4 + f] = L.normal;
            for (int k = 0; k < 4; ++k) g.fold[32 + 4 * f + k] = L.nrm[k];
            if (L.mask >= 0) g.foldMask = L.mask;''')

R = 'src/viewport/render.cpp'
patch(R, '''                               float4 foldL[8]; };  // rows 16-23: a prop mesh's two folded colour layers (research 136)''',
      '''                               float4 foldL[10]; };  // rows 16-25: a prop mesh's two folded layers (research 136)''')
patch(R, '''Texture2D foldR2 : register(t47);''', '''Texture2D foldR2 : register(t47);
Texture2D foldN1 : register(t48);  // the folded layers' normal + gloss + cavity maps
Texture2D foldN2 : register(t49);''')
# the weight on its own, so the normal stage reuses it
patch(R, '''float3 foldLayer(float3 albedo, PSIn i, float4 P, float4 T, float4 UT, float4 UO, float4 c, float2 ar) {
    const float w = saturate(i.vc.w - 1.0), k = P.z;''', '''float foldWeight(PSIn i, float4 P, float4 T, float2 ar) {
    const float w = saturate(i.vc.w - 1.0), k = P.z;
    const uint fl = (uint)(P.y + 0.5);
    float b = w;
    if (fl & 16u) {
        const float lo = saturate(1.0 - w - k * sqrt(w)), hi = saturate(1.0 - w + k * sqrt(1.0 - w));
        b = saturate((ar.g - lo) / max(hi - lo, 1e-4));
    }
    if (fl & 8u) b *= ar.r;
    if (T.w > 0.5) {
        const float4 mk = layerMaskTex.Sample(samp0, i.uv);
        const int ch = int(T.w + 0.5) - 1;
        b *= ch == 0 ? mk.r : (ch == 1 ? mk.g : mk.b);
    }
    return b;
}
float3 foldLayer(float3 albedo, PSIn i, float4 P, float4 T, float4 UT, float4 UO, float4 c, float2 ar) {
    const float w = saturate(i.vc.w - 1.0), k = P.z;''')
patch(R, '''    if (b < 1.0 / 255.0) return albedo;
    const float3 cl = srgbToLinear3(c.rgb) * T.rgb;''', '''    if (b < 1.0 / 255.0 || P.w > 2.5) return albedo;  // kind 3: normal / gloss only
    const float3 cl = srgbToLinear3(c.rgb) * T.rgb;''')
# the normal stage: after the base octahedral decode, before the tangent normal
patch(R, '''            const float3 nT = normalize(float3(nxy, 1.0 - 0.6 * dot(nxy, nxy)));''', '''            // A prop's folded layers (research 136 sections 1.2-1.3): normal (lerp / add), cavity and gloss by their weight.
            if (stockRef && baseMat && i.vc.w > 0.5 && (foldL[0].x > 0.5 || foldL[4].x > 0.5)) {
                float g = saturate(max(nmGloss, 0.0) * layerTint.z + layerTint.w);
                bool glossTouched = false;
                [unroll] for (int fi = 0; fi < 2; ++fi) {
                    const float4 P = foldL[4 * fi], T = foldL[4 * fi + 1], UT = foldL[4 * fi + 2], UO = foldL[4 * fi + 3], NR = foldL[8 + fi];
                    if (P.x < 0.5 || NR.x < 0.5) continue;
                    const float2 u = foldUv(i, UT, UO);
                    const float2 ar = fi == 0 ? foldR1.Sample(samp0, u).rg : foldR2.Sample(samp0, u).rg;
                    const float b = foldWeight(i, P, T, ar);
                    if (b < 1.0 / 255.0) continue;
                    float4 nl = fi == 0 ? foldN1.Sample(samp0, u) : foldN2.Sample(samp0, u);
                    const float gL = saturate(nl.r * NR.z + NR.w), cL = nl.b;
                    nl.ga = (nl.ga - 0.5) * NR.y + 0.5;
                    const float2 sl = nl.ga * 2.0 - 1.0;
                    const float2 ol = float2(sl.x + sl.y, sl.x - sl.y) * 0.5;
                    const float2 lxy = normalize(float3(ol, max(1.0 - abs(ol.x) - abs(ol.y), 0.0))).xy;
                    const uint fl = (uint)(P.y + 0.5);
                    if (P.w > 1.5 && P.w < 2.5) {  // overlay: the normal added, cavity and gloss toward the lower value
                        nxy += b * lxy;
                        nmCavity = lerp(nmCavity, min(nmCavity, cL), b);
                        g = lerp(g, min(g, gL), b), glossTouched = true;
                    } else {
                        const int nmode = int(NR.x + 0.5) - 1;
                        if (nmode != 2) {
                            const float bN = (fl & 1u) ? b : 0.0, bC = (fl & 2u) ? b : 0.0, bG = (fl & 4u) ? b : 0.0;
                            nxy = nmode == 0 ? nxy * (1.0 - bN) + bN * lxy : nxy + bN * lxy;
                            nmCavity = lerp(nmCavity, cL, bC);
                            if (bG > 0.0) g = lerp(g, gL, bG), glossTouched = true;
                        }
                    }
                }
                if (glossTouched) dualGloss = g;
            }
            const float3 nT = normalize(float3(nxy, 1.0 - 0.6 * dot(nxy, nxy)));''')
# upload rows 16-25 and bind the normal maps
patch(R, '''                std::memcpy(&all[j * 128 + 64], gi.fold, sizeof(gi.fold));  // rows 16-23''',
      '''                std::memcpy(&all[j * 128 + 64], gi.fold, sizeof(gi.fold));  // rows 16-25''')
patch(R, '''            ID3D11ShaderResourceView* fv[4] = {};
            for (int k = 0; k < 4; ++k)
                fv[k] = g.foldTex[k] >= 0 && size_t(g.foldTex[k]) < textures_.size() ? textures_[size_t(g.foldTex[k])]
                                                                                       : (k & 1) && whiteFullTex_ >= 0 ? textures_[size_t(whiteFullTex_)] : nullptr;
            ctx_->PSSetShaderResources(44, 4, fv);''', '''            ID3D11ShaderResourceView* fv[6] = {};
            for (int k = 0; k < 6; ++k)
                fv[k] = g.foldTex[k] >= 0 && size_t(g.foldTex[k]) < textures_.size() ? textures_[size_t(g.foldTex[k])]
                        : k < 4 && (k & 1) && whiteFullTex_ >= 0                   ? textures_[size_t(whiteFullTex_)]
                        : k >= 4 && flatNrmTex_ >= 0                               ? textures_[size_t(flatNrmTex_)]
                                                                                   : nullptr;
            ctx_->PSSetShaderResources(44, 6, fv);''')

C = 'src/qtapp/camera_view.cpp'
patch(C, '''        float drawKind = kind == 1 ? 2.0f : 0.0f;
        if (kind == 0) {
            if (lc.colorMode >= 2) continue;
            if (lc.colorMode == 1) drawKind = 1.0f;
            else if (noLerp) continue;
        }
        const std::string ck = base + '\\x01' + std::to_string(L), rk = base + '\\x02' + std::to_string(L);
        const int cid = textureFor(ck), rid = textureFor(rk);
        if (!texIds_.count(ck) || !texIds_.count(rk)) return false;  // still loading
        if (cid < 0 || cid == checkerTex_) continue;  // no colour image''', '''        // kind 3: modes 2 / 3, normal / cavity / gloss only (folded into the mesh's pass; never a pass of its own).
        float drawKind = kind == 1 ? 2.0f : 0.0f;
        if (kind == 0) {
            if (lc.colorMode >= 2) drawKind = 3.0f;
            else if (lc.colorMode == 1) drawKind = 1.0f;
            else if (noLerp) continue;
        }
        const std::string ck = base + '\\x01' + std::to_string(L), rk = base + '\\x02' + std::to_string(L);
        const int cid = textureFor(ck), rid = textureFor(rk);
        if (!texIds_.count(ck) || !texIds_.count(rk)) return false;  // still loading
        const bool noColour = cid < 0 || cid == checkerTex_;
        if (noColour && drawKind != 3.0f) continue;  // no colour image
        // The layer's normal + gloss + cavity map (research 136): only a folded layer reads it.
        const int nid = normalFor(ck);
        if (!nrmIds_.count(ck)) return false;  // still loading''')
patch(C, '''        app::PropLayer pl;
        pl.texture = cid;
        pl.reveal = rid == checkerTex_ ? -1 : rid;
        const float p[16] = {1.0f, float(lc.flags), lc.sharpness, drawKind,''', '''        app::PropLayer pl;
        pl.texture = noColour ? -1 : cid;
        pl.reveal = rid == checkerTex_ ? -1 : rid;
        pl.normal = nid;
        pl.nrm[0] = nid >= 0 ? float(lc.normalMode + 1) : 0.0f;
        pl.nrm[1] = lc.normalStrength, pl.nrm[2] = lc.glossScale, pl.nrm[3] = lc.glossBias;
        const float p[16] = {1.0f, float(lc.flags), lc.sharpness, drawKind,''')
# normalFor: a layer key (mode 3 layers have no colour image) does not wait for a colour image
patch(C, '''    // Only once the colour image is up: the material is then known to the cache, and colour loads go first.
    if (auto t = texIds_.find(material); t == texIds_.end() || t->second < 0 || t->second == checkerTex_) return -1;''',
      '''    // Only once the colour image is up: the material is then known to the cache, and colour loads go first. A model
    // layer's key ("<material>\\x01<L>") only waits for that layer's colour lookup (a mode 3 layer has no colour image).
    if (auto t = texIds_.find(material);
        t == texIds_.end() || (material.find('\\x01') == std::string::npos && (t->second < 0 || t->second == checkerTex_)))
        return -1;''')
print('ok')
