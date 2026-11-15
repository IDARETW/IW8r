import io

# Research 136 section 1.2: a model layer with flags & 64 takes its tint through its tint mask (slot 59 + L):
# tint = lerp(1, tint, mask.r) at the layer UV. The image name comes from the cache entry's texture list and loads
# through the "\x05<image>" key; folded layers bind it at t50 / t51 (record rows unchanged: the flags are in row 0).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


CH = 'src/qtapp/camera_view.h'
patch(CH, '''        std::string layerMaskImage;''', '''        std::string layerMaskImage;
        std::vector<std::string> layerTintMaskImage;  // per layer L: slot 59 + L ("" none; used when flags & 64)''')
MW = 'src/qtapp/main_window.cpp'
patch(MW, '''            if (tk.mask126)
                for (const QString& t : e->textures)
                    if (t.startsWith("126=")) f.layerMaskImage = t.mid(4).toStdString();''', '''            if (tk.mask126)
                for (const QString& t : e->textures)
                    if (t.startsWith("126=")) f.layerMaskImage = t.mid(4).toStdString();
            f.layerTintMaskImage.assign(size_t(std::max(e->modelLayers, 1)), std::string());
            for (int L = 1; L < e->modelLayers; ++L)
                for (const QString& t : e->textures)
                    if (t.startsWith(QString::number(59 + L) + "=")) f.layerTintMaskImage[size_t(L)] = t.section('=', 1).toStdString();''')
H = 'src/viewport/render.h'
patch(H, '''    int normal = -1;        // the layer's normal + gloss + cavity map (folded layers only)''',
      '''    int normal = -1;        // the layer's normal + gloss + cavity map (folded layers only)
    int tintMask = -1;      // slot 59 + L (flags & 64; folded layers only), -1: none''')
patch(H, '''        int foldTex[6] = {-1, -1, -1, -1, -1, -1};  // colour 1, alpha / reveal 1, colour 2, alpha / reveal 2, normal 1, normal 2''',
      '''        int foldTex[8] = {-1, -1, -1, -1, -1, -1, -1, -1};  // colour, alpha / reveal (1, 2), normal 1, 2, tint mask 1, 2''')
patch(H, '''            g.foldTex[2 * f] = L.texture, g.foldTex[2 * f + 1] = L.reveal, g.foldTex[4 + f] = L.normal;''',
      '''            g.foldTex[2 * f] = L.texture, g.foldTex[2 * f + 1] = L.reveal, g.foldTex[4 + f] = L.normal, g.foldTex[6 + f] = L.tintMask;''')
CC = 'src/qtapp/camera_view.cpp'
patch(CC, '''        pl.normal = nid;''', '''        pl.normal = nid;
        if ((lc.flags & 64u) && facts && size_t(L) < facts->layerTintMaskImage.size() && !facts->layerTintMaskImage[size_t(L)].empty()) {
            const std::string tk = "\\x05" + facts->layerTintMaskImage[size_t(L)];
            const int tid = textureFor(tk);
            if (!texIds_.count(tk)) return false;  // still loading
            pl.tintMask = tid == checkerTex_ ? -1 : tid;
        }''')
R = 'src/viewport/render.cpp'
patch(R, '''Texture2D foldN2 : register(t49);''', '''Texture2D foldN2 : register(t49);
Texture2D foldT1 : register(t50);  // the folded layers' tint masks (slot 59 + L, flags & 64)
Texture2D foldT2 : register(t51);''')
patch(R, '''float3 foldPropLayers(float3 albedo, PSIn i) {
    if (foldL[0].x > 0.5) {
        const float2 u = foldUv(i, foldL[2], foldL[3]);
        albedo = foldLayer(albedo, i, foldL[0], foldL[1], foldL[2], foldL[3], foldC1.Sample(samp0, u), foldR1.Sample(samp0, u).rg);
    }
    if (foldL[4].x > 0.5) {
        const float2 u = foldUv(i, foldL[6], foldL[7]);
        albedo = foldLayer(albedo, i, foldL[4], foldL[5], foldL[6], foldL[7], foldC2.Sample(samp0, u), foldR2.Sample(samp0, u).rg);
    }
    return albedo;
}''', '''// The tint through the tint mask when flags & 64 (research 136 section 1.2): lerp(1, tint, mask.r) at the layer UV.
float4 foldTint(float4 P, float4 T, float mask) {
    return (((uint)(P.y + 0.5)) & 64u) ? float4(lerp(float3(1, 1, 1), T.rgb, mask), T.w) : T;
}
float3 foldPropLayers(float3 albedo, PSIn i) {
    if (foldL[0].x > 0.5) {
        const float2 u = foldUv(i, foldL[2], foldL[3]);
        const float4 T = foldTint(foldL[0], foldL[1], foldT1.Sample(samp0, u).r);
        albedo = foldLayer(albedo, i, foldL[0], T, foldL[2], foldL[3], foldC1.Sample(samp0, u), foldR1.Sample(samp0, u).rg);
    }
    if (foldL[4].x > 0.5) {
        const float2 u = foldUv(i, foldL[6], foldL[7]);
        const float4 T = foldTint(foldL[4], foldL[5], foldT2.Sample(samp0, u).r);
        albedo = foldLayer(albedo, i, foldL[4], T, foldL[6], foldL[7], foldC2.Sample(samp0, u), foldR2.Sample(samp0, u).rg);
    }
    return albedo;
}''')
patch(R, '''            ID3D11ShaderResourceView* fv[6] = {};
            for (int k = 0; k < 6; ++k)
                fv[k] = g.foldTex[k] >= 0 && size_t(g.foldTex[k]) < textures_.size() ? textures_[size_t(g.foldTex[k])]
                        : k < 4 && (k & 1) && whiteFullTex_ >= 0                   ? textures_[size_t(whiteFullTex_)]
                        : k >= 4 && flatNrmTex_ >= 0                               ? textures_[size_t(flatNrmTex_)]
                                                                                   : nullptr;
            ctx_->PSSetShaderResources(44, 6, fv);''', '''            ID3D11ShaderResourceView* fv[8] = {};
            for (int k = 0; k < 8; ++k)
                fv[k] = g.foldTex[k] >= 0 && size_t(g.foldTex[k]) < textures_.size() ? textures_[size_t(g.foldTex[k])]
                        : ((k < 4 && (k & 1)) || k >= 6) && whiteFullTex_ >= 0 ? textures_[size_t(whiteFullTex_)]
                        : k >= 4 && k < 6 && flatNrmTex_ >= 0                  ? textures_[size_t(flatNrmTex_)]
                                                                              : nullptr;
            ctx_->PSSetShaderResources(44, 8, fv);''')
print('ok')
