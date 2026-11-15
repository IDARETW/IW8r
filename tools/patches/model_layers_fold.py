import io

# Research 136, the fold: a prop mesh's first two colour layers (lerp, multiply, overlay) are applied to the albedo in
# the mesh's own shading pass, as the game's pixel shader does (no extra shading pass: the separate lerp passes cost
# 6.7 ms on Aniyah Tac). The per-mesh record grows from 256 to 512 bytes (rows 16-23: two layers of 4 rows: (on, flags,
# k, kind), (tint, mask channel + 1), UV transform, (offset, second UV)); their colour and alpha / reveal images go to
# t32-t35 and the slot-126 mask to t31 with the mesh. Layers past the second keep the separate pass.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/viewport/render.h'
patch(H, '''        float wave[16] = {};        // setReferenceInstWave: constants 79-82 (research 133 section 6)''',
      '''        float wave[16] = {};        // setReferenceInstWave: constants 79-82 (research 133 section 6)
        // Research 136: the first two colour layers folded into the mesh's own pass (rows 16-23 of its record) and their
        // images (colour 1, alpha / reveal 1, colour 2, alpha / reveal 2; -1 none) and the slot-126 mask.
        float fold[32] = {};
        int foldTex[4] = {-1, -1, -1, -1};
        int foldMask = -1;''')
patch(H, '''    void setReferenceInstLayers(size_t mesh, std::vector<PropLayer> layers) {
        if (mesh < refTexInst_.size()) refTexInst_[mesh].layers = std::move(layers);
    }''', '''    // The first two are folded into the mesh's own pass (research 136); the rest are drawn as passes.
    void setReferenceInstLayers(size_t mesh, std::vector<PropLayer> layers) {
        if (mesh >= refTexInst_.size()) return;
        auto& g = refTexInst_[mesh];
        std::fill(std::begin(g.fold), std::end(g.fold), 0.0f);
        std::fill(std::begin(g.foldTex), std::end(g.foldTex), -1);
        g.foldMask = -1;
        static const bool noFold = std::getenv("IW8R_NO_LAYER_FOLD") != nullptr;  // A/B: every layer a pass
        size_t f = 0;
        std::vector<PropLayer> rest;
        for (PropLayer& L : layers) {
            if (noFold || f >= 2) {
                rest.push_back(std::move(L));
                continue;
            }
            float* r = &g.fold[16 * f];
            r[0] = 1.0f, r[1] = L.params[1], r[2] = L.params[2], r[3] = L.params[3];
            r[4] = L.params[4], r[5] = L.params[5], r[6] = L.params[6], r[7] = L.aux[1];
            for (int k = 0; k < 4; ++k) r[8 + k] = L.params[8 + k];
            r[12] = L.params[12], r[13] = L.params[13], r[14] = L.aux[0];
            g.foldTex[2 * f] = L.texture, g.foldTex[2 * f + 1] = L.reveal;
            if (L.mask >= 0) g.foldMask = L.mask;
            ++f;
        }
        g.layers = std::move(rest);
        instParamsDirty_ = true;
    }''')

R = 'src/viewport/render.cpp'
patch(R, '''                               float4 matDual0; float4 matDual1; };  // the water's constants 110 / 111 (research 133 section 5.3)''',
      '''                               float4 matDual0; float4 matDual1;  // the water's constants 110 / 111 (research 133 section 5.3)
                               float4 matRows14[2];
                               float4 foldL[8]; };  // rows 16-23: a prop mesh's two folded colour layers (research 136)''')
patch(R, '''Texture2D layerMaskTex : register(t31);  // a prop layer's slot-126 mask (research 136)''',
      '''Texture2D layerMaskTex : register(t31);  // a prop layer's slot-126 mask (research 136)
Texture2D foldC1 : register(t32);  // the folded layers' colour and alpha / reveal images (research 136)
Texture2D foldR1 : register(t33);
Texture2D foldC2 : register(t34);
Texture2D foldR2 : register(t35);''')
patch(R, '''// vs_tex_inst: a prop lit from its light grid box carries that light in t (no tangent) and lm = (-2, -2).''',
      '''// A model layer folded into the prop's own pass (research 136 section 1.2-1.3): its weight from the vertex alpha
// (vc.w = 1 + a), the reveal ramp, the alpha and the slot-126 mask, then lerp (0), multiply (1) or soft-light overlay
// (2) of the linear albedo with the tinted layer colour.
float3 foldLayer(float3 albedo, PSIn i, float4 P, float4 T, float4 UT, float4 UO, float4 c, float2 ar) {
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
    if (b < 1.0 / 255.0) return albedo;
    const float3 cl = srgbToLinear3(c.rgb) * T.rgb;
    if (P.w > 1.5) {
        const float3 tt = 0.5 + b * (pow(saturate(cl), 1.0 / 2.2) - 0.5);
        return albedo * albedo * (1.0 - 2.0 * tt) + 2.0 * tt * albedo;
    }
    if (P.w > 0.5) return albedo * lerp(float3(1, 1, 1), cl, b);
    return lerp(albedo, cl, b);
}
float2 foldUv(PSIn i, float4 UT, float4 UO) {
    const float2 lu = UO.z > 0.5 ? i.uv1 : i.uv;
    return float2(UT.x * lu.x * UT.z + UT.y * lu.y * UT.w, -UT.y * lu.x * UT.z + UT.x * lu.y * UT.w) + UO.xy;
}
float3 foldPropLayers(float3 albedo, PSIn i) {
    if (foldL[0].x > 0.5) {
        const float2 u = foldUv(i, foldL[2], foldL[3]);
        albedo = foldLayer(albedo, i, foldL[0], foldL[1], foldL[2], foldL[3], foldC1.Sample(samp0, u), foldR1.Sample(samp0, u).rg);
    }
    if (foldL[4].x > 0.5) {
        const float2 u = foldUv(i, foldL[6], foldL[7]);
        albedo = foldLayer(albedo, i, foldL[4], foldL[5], foldL[6], foldL[7], foldC2.Sample(samp0, u), foldR2.Sample(samp0, u).rg);
    }
    return albedo;
}
// vs_tex_inst: a prop lit from its light grid box carries that light in t (no tangent) and lm = (-2, -2).''')
patch(R, '''        float3 albedo = srgbToLinear3(t.rgb) * (stockRef ? i.vc.rgb : i.col.rgb);  // i.col: selection and brush entity tints; vc: a prop's vertex colour''',
      '''        float3 albedo = srgbToLinear3(t.rgb);
        // A prop's folded model layers (research 136), before its vertex colour (the game multiplies it after them).
        if (stockRef && layerP.x < 0.5 && i.vc.w > 0.5 && foldL[0].x > 0.5) albedo = foldPropLayers(albedo, i);
        albedo *= stockRef ? i.vc.rgb : i.col.rgb;  // i.col: selection and brush entity tints; vc: a prop's vertex colour''')
# the per-mesh record: 512 bytes, 128 meshes per 64 KB
patch(R, '''bool SceneRenderer::uploadInstParams() {
    constexpr size_t kInstPerCb = 256;''', '''bool SceneRenderer::uploadInstParams() {
    constexpr size_t kInstPerCb = 128;  // 512-byte records (rows 16-23: the folded layers, research 136)''')
patch(R, '''            bd.ByteWidth = UINT(kInstPerCb * 256);
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            for (auto*& b : cbInstAll_)
                if (FAILED(dev_->CreateBuffer(&bd, nullptr, &b))) cbInstAllMeshes_ = 0;
        }
        std::vector<float> all(kInstPerCb * 64, 0.0f);''', '''            bd.ByteWidth = UINT(kInstPerCb * 512);
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            for (auto*& b : cbInstAll_)
                if (FAILED(dev_->CreateBuffer(&bd, nullptr, &b))) cbInstAllMeshes_ = 0;
        }
        std::vector<float> all(kInstPerCb * 128, 0.0f);''')
patch(R, '''                std::memcpy(&all[j * 64], gi.params, sizeof(gi.params));
                std::memcpy(&all[j * 64 + 16], gi.extra, sizeof(gi.extra));
                std::memcpy(&all[j * 64 + 32], gi.wave, sizeof(gi.wave));  // PropWave (b8) in vs_tex_inst
                maskExtraTerms(&all[j * 64 + 16]);
                if (textureRawAlpha(gi.texture)) all[j * 64 + 1] = 2.0f;  // opaque: alpha is the specular code''',
      '''                std::memcpy(&all[j * 128], gi.params, sizeof(gi.params));
                std::memcpy(&all[j * 128 + 16], gi.extra, sizeof(gi.extra));
                std::memcpy(&all[j * 128 + 32], gi.wave, sizeof(gi.wave));  // PropWave (b8) in vs_tex_inst
                std::memcpy(&all[j * 128 + 64], gi.fold, sizeof(gi.fold));  // rows 16-23
                maskExtraTerms(&all[j * 128 + 16]);
                if (textureRawAlpha(gi.texture)) all[j * 128 + 1] = 2.0f;  // opaque: alpha is the specular code''')
s = io.open(ROOT + R, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
# drawReferenceTexInstances' constant
a = '    constexpr size_t kInstPerCb = 256;' + nl
assert s.count(a) == 1, s.count(a)
s = s.replace(a, '    constexpr size_t kInstPerCb = 128;  // uploadInstParams: 512-byte records' + nl)
# every bind of a record: 32 constants at (mi % kInstPerCb) * 32
n1 = s.count('const UINT first = UINT((mi % kInstPerCb) * 16), num = 16;')
assert n1 == 2, n1
s = s.replace('const UINT first = UINT((mi % kInstPerCb) * 16), num = 16;', 'const UINT first = UINT((mi % kInstPerCb) * 32), num = 32;')
a2 = '''            if (cbInstAll_[mi / 256]) {
                const UINT first = UINT((mi % 256) * 16), num = 16;
                ctx1_->PSSetConstantBuffers1(7, 1, &cbInstAll_[mi / 256], &first, &num);
                ctx1_->VSSetConstantBuffers1(8, 1, &cbInstAll_[mi / 256], &first, &num);  // PropWave'''.replace('\n', nl)
assert s.count(a2) == 1
s = s.replace(a2, '''            if (cbInstAll_[mi / 128]) {
                const UINT first = UINT((mi % 128) * 32), num = 32;
                ctx1_->PSSetConstantBuffers1(7, 1, &cbInstAll_[mi / 128], &first, &num);
                ctx1_->VSSetConstantBuffers1(8, 1, &cbInstAll_[mi / 128], &first, &num);  // PropWave'''.replace('\n', nl))
io.open(ROOT + R, 'w', encoding='utf-8', newline='').write(s)
# the main pass binds the folded layers' images with the mesh
patch(R, '''            ctx_->PSSetShaderResources(3, 1, &e);
            ctx_->PSSetShaderResources(19, 1, &t);
        }''', '''            ctx_->PSSetShaderResources(3, 1, &e);
            ctx_->PSSetShaderResources(19, 1, &t);
            // Research 136: the folded layers' images (t32-t35) and the slot-126 mask (t31).
            ID3D11ShaderResourceView* fv[4] = {};
            for (int k = 0; k < 4; ++k)
                fv[k] = g.foldTex[k] >= 0 && size_t(g.foldTex[k]) < textures_.size() ? textures_[size_t(g.foldTex[k])]
                                                                                       : (k & 1) && whiteFullTex_ >= 0 ? textures_[size_t(whiteFullTex_)] : nullptr;
            ctx_->PSSetShaderResources(32, 4, fv);
            ID3D11ShaderResourceView* fm = g.foldMask >= 0 && size_t(g.foldMask) < textures_.size() ? textures_[size_t(g.foldMask)] : nullptr;
            ctx_->PSSetShaderResources(31, 1, &fm);
        }''')
print('ok')
