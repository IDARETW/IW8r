import io

# Research 136 port (research 132 G9): props carry the second UV set and pass the vertex alpha as the layer weight
# (1 + a in vc.w); each model layer is classified from the technique set name (core parseModelLayerTokens): standard
# lerp layers (mode 0) are drawn with alpha = b, multiply (1) and overlay layers (soft light, a = 0.5: f = 1 + b (s -
# 0.5)) with the multiplying blend, code layers and modes 2 / 3 (normal / gloss only) are left out; a UV1 layer reads
# the second UV set; hexB 3... sets weight each layer by slot 126's channel L - 1 at the raw UV0. The prop layer pass
# has its own 256-byte constant buffer (row 8: UV1 flag, mask channel + 1). IW8R_NO_LERP_LAYERS: multiply layers only.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


RH = 'src/viewport/render.h'
patch(RH, '''    uint32_t lmapUv = 0;
};''', '''    uint32_t lmapUv = 0;
    // The second UV set (stock `2` families; research 136), as stored; 0 when the model has none.
    float u1 = 0, v1 = 0;
};''')
patch(RH, '''struct PropLayer {
    int texture = -1, reveal = -1;
    float params[16] = {};
};''', '''struct PropLayer {
    int texture = -1, reveal = -1;
    float params[16] = {};  // layerP (on, flags, k, kind: 0 lerp, 1 multiply, 2 overlay), tint, UV transform
    int mask = -1;          // slot 126 (research 136), -1: none
    float aux[4] = {};      // row 8 of the layer buffer: the second UV set (1), the mask channel + 1
};''')
patch(RH, '''    ID3D11Texture3D* lgvTex_[2] = {nullptr, nullptr};  // setStockLgvAtlas''', '''    ID3D11Texture3D* lgvTex_[2] = {nullptr, nullptr};  // setStockLgvAtlas
    ID3D11Buffer* cbPropLayer_ = nullptr;  // the prop layer pass's b7 (256 bytes: PropLayer params, row 8 aux)''')

R = 'src/viewport/render.cpp'
# HLSL inputs / outputs
patch(R, '''               float4 sv : TEXCOORD8; };  // a stock prop's self visibility (world bent normal x amount, cone; w < 0: none)''',
      '''               float4 sv : TEXCOORD8;  // a stock prop's self visibility (world bent normal x amount, cone; w < 0: none)
               float2 uv1 : TEXCOORD9; };  // a prop's second UV set (research 136)''')
patch(R, '''    o.sv = float4(0, 0, 0, -1);
    o.edge = i.edge.xyz;''', '''    o.sv = float4(0, 0, 0, -1);
    o.uv1 = float2(0, 0);
    o.edge = i.edge.xyz;''') if False else None
s = io.open(ROOT + R, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
# vs_tex: the first "o.sv = float4(0, 0, 0, -1);" (in vs_tex) gets uv1 = 0 after it
first = s.find('    o.sv = float4(0, 0, 0, -1);' + nl)
assert first > 0
s = s[:first] + '    o.uv1 = float2(0, 0);' + nl + s[first:]
io.open(ROOT + R, 'w', encoding='utf-8', newline='').write(s)
patch(R, '''float4 sv : SELFVIS; float2 lmuv : LMAPUV; float4 r0 : INSTROW0;''', '''float4 sv : SELFVIS; float2 lmuv : LMAPUV; float2 uv1 : UVTWO; float4 r0 : INSTROW0;''')
patch(R, '''    o.vc = float4(i.vcol.rgb <= 0.04045 ? i.vcol.rgb / 12.92 : pow((i.vcol.rgb + 0.055) / 1.055, 2.4), 1.0);  // w: a prop''',
      '''    // w: 1 + the vertex alpha (a prop; the model layers' weight in the vertex-colour families, research 136).
    o.vc = float4(i.vcol.rgb <= 0.04045 ? i.vcol.rgb / 12.92 : pow((i.vcol.rgb + 0.055) / 1.055, 2.4), 1.0 + i.vcol.a);
    o.uv1 = i.uv1;''')
# PS: the layer UV from the second set; the weight; the mask; the overlay
patch(R, '''    float layerB = -1.0;
    float2 uvS = i.uv;
    if (layerP.x > 0.5)
        uvS = float2(layerUvT.x * i.uv.x * layerUvT.z + layerUvT.y * i.uv.y * layerUvT.w,
                     -layerUvT.y * i.uv.x * layerUvT.z + layerUvT.x * i.uv.y * layerUvT.w) + layerUvO.xy;''',
      '''    float layerB = -1.0;
    float2 uvS = i.uv;
    if (layerP.x > 0.5) {
        // A prop layer on the second UV set (research 136; matRows8[0].x, the prop layer buffer's row 8).
        const float2 lu = layerP.x < 1.5 && matRows8[0].x > 0.5 ? i.uv1 : i.uv;
        uvS = float2(layerUvT.x * lu.x * layerUvT.z + layerUvT.y * lu.y * layerUvT.w,
                     -layerUvT.y * lu.x * layerUvT.z + layerUvT.x * lu.y * layerUvT.w) + layerUvO.xy;
    }''')
patch(R, '''        // The tangent's length is 1 + the weight on the world; a model layer's weight is 1 (research 107 section 3.2).
        float w = i.vc.w > 0.5 ? 1.0 : saturate(length(i.t) - 1.0), k = layerP.z;''',
      '''        // The tangent's length is 1 + the weight on the world; a model layer's weight is the vertex alpha in the
        // vertex-colour families and 1 in the others, carried as vc.w = 1 + a (research 136; white: 1).
        float w = i.vc.w > 0.5 ? saturate(i.vc.w - 1.0) : saturate(length(i.t) - 1.0), k = layerP.z;''')
patch(R, '''        if (fl & 8u) b *= ar.r;
        layerB = b;''', '''        if (fl & 8u) b *= ar.r;
        // hexB 3... sets: slot 126's channel L - 1 at the raw UV0 weights the layer (research 136 section 1.2).
        if (matRows8[0].y > 0.5) {
            const float4 mk = layerMaskTex.Sample(samp0, i.uv);
            const int ch = int(matRows8[0].y + 0.5) - 1;
            b *= ch == 0 ? mk.r : (ch == 1 ? mk.g : mk.b);
        }
        layerB = b;''')
patch(R, '''Texture2D revealTex : register(t12);''', '''Texture2D revealTex : register(t12);
Texture2D layerMaskTex : register(t31);  // a prop layer's slot-126 mask (research 136)''')
patch(R, '''    if (layerP.x > 0.5 && layerP.w > 0.5) {
        const float3 mulLin = lerp(float3(1, 1, 1), srgbToLinear3(t.rgb), layerB);''',
      '''    if (layerP.x > 0.5 && layerP.w > 1.5) {
        // An overlay layer (research 136 section 1.3): soft light, t = 0.5 + b (s - 0.5) with s the tinted colour in sRGB;
        // the albedo below is taken as 0.5, which makes the factor f = 2t + a (1 - 2t) = 1 + b (s - 0.5).
        const float3 f = 1.0 + layerB * (saturate(t.rgb) - 0.5);
        return float4(game0.x > 0.5 && gameAo.w > 0.5 ? f : pow(max(f, 0.0), 1.0 / 2.2), 1.0);
    }
    if (layerP.x > 0.5 && layerP.w > 0.5) {
        const float3 mulLin = lerp(float3(1, 1, 1), srgbToLinear3(t.rgb), layerB);''')
# layout
patch(R, '''                    {"LMAPUV", 0, DXGI_FORMAT_R16G16_UNORM, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},''',
      '''                    {"LMAPUV", 0, DXGI_FORMAT_R16G16_UNORM, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"UVTWO", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},''')
patch(R, '''                // PropVertex (40 bytes): position, uv, colour, normal as signed bytes, the model's vertex colour, the self
                // visibility as signed bytes, the lightmap uv as unorm16.''', '''                // PropVertex (48 bytes): position, uv, colour, normal as signed bytes, the model's vertex colour, the self
                // visibility as signed bytes, the lightmap uv as unorm16, the second uv.''')
# the prop layer buffer
patch(R, '''                D3D11_BUFFER_DESC lb = bd;
                lb.ByteWidth = 64;
                const float zero[16] = {};
                D3D11_SUBRESOURCE_DATA lz{zero, 0, 0};
                dev_->CreateBuffer(&lb, &lz, &cbLayer_);''', '''                D3D11_BUFFER_DESC lb = bd;
                lb.ByteWidth = 64;
                const float zero[64] = {};
                D3D11_SUBRESOURCE_DATA lz{zero, 0, 0};
                dev_->CreateBuffer(&lb, &lz, &cbLayer_);
                lb.ByteWidth = 256;  // the prop layer pass: params, rows 4-7 zero, row 8 aux (research 136)
                dev_->CreateBuffer(&lb, &lz, &cbPropLayer_);''')
patch(R, '''    safeRelease(cbLayer_);''', '''    safeRelease(cbLayer_);
    safeRelease(cbPropLayer_);''')
patch(R, '''        if (texPsOverride_ || !cbLayer_ || noPropLayers) return;''', '''        if (texPsOverride_ || !cbLayer_ || !cbPropLayer_ || noPropLayers) return;''')
patch(R, '''                ctx_->PSSetShaderResources(1, 1, &noN);
                ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);
            }''', '''                ctx_->PSSetShaderResources(1, 1, &noN);
                ctx_->PSSetConstantBuffers(7, 1, &cbPropLayer_);
            }''')
patch(R, '''                ctx_->UpdateSubresource(cbLayer_, 0, nullptr, L.params, 0, 0);
                const float bfz[4] = {0, 0, 0, 0};
                ctx_->OMSetBlendState(L.params[3] > 0.5f && blendMul_ ? blendMul_ : blend_, bfz, 0xffffffff);
                ctx_->PSSetShaderResources(0, 1, &textures_[size_t(L.texture)]);
                ctx_->PSSetShaderResources(12, 1, &textures_[size_t(rv)]);''', '''                float all[64] = {};
                std::memcpy(all, L.params, sizeof(L.params));
                std::memcpy(all + 32, L.aux, sizeof(L.aux));  // row 8
                ctx_->UpdateSubresource(cbPropLayer_, 0, nullptr, all, 0, 0);
                const float bfz[4] = {0, 0, 0, 0};
                ctx_->OMSetBlendState(L.params[3] > 0.5f && blendMul_ ? blendMul_ : blend_, bfz, 0xffffffff);
                ctx_->PSSetShaderResources(0, 1, &textures_[size_t(L.texture)]);
                ctx_->PSSetShaderResources(12, 1, &textures_[size_t(rv)]);
                ID3D11ShaderResourceView* mk = L.mask >= 0 && size_t(L.mask) < textures_.size() ? textures_[size_t(L.mask)] : nullptr;
                ctx_->PSSetShaderResources(31, 1, &mk);''')
patch(R, '''        const float zero16[16] = {};
        ctx_->UpdateSubresource(cbLayer_, 0, nullptr, zero16, 0, 0);  // the plain layer buffer is zero again
        ID3D11ShaderResourceView* noR = nullptr;
        ctx_->PSSetShaderResources(12, 1, &noR);''', '''        ID3D11ShaderResourceView* noR = nullptr;
        ctx_->PSSetShaderResources(12, 1, &noR);
        ctx_->PSSetShaderResources(31, 1, &noR);
        ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);  // the plain (zero) layer buffer again''')

# ---- camera: facts and the layer resolver
CH = 'src/qtapp/camera_view.h'
patch(CH, '''        std::array<float, 8> dualNormal{};''', '''        std::array<float, 8> dualNormal{};
        // Model-layered materials (research 136): per layer L the kind (0 standard, 1 overlay, 2 code) and whether it
        // samples the second UV set; the slot-126 mask image when the technique set uses it (hexB 3...).
        std::vector<int> layerKind;
        std::vector<char> layerUv1;
        std::string layerMaskImage;''')
CC = 'src/qtapp/camera_view.cpp'
patch(CC, '''    std::vector<app::PropLayer> out;
    for (int L = 1; L < c->layers && size_t(L) < c->layer.size(); ++L) {
        const StockLayerConstants& lc = c->layer[size_t(L)];
        if (lc.colorMode >= 2) continue;  // modes 2 / 3: normal and gloss only (not drawn, as on the world)
        // Only multiply layers (mode 1: grime, wear) are drawn. Colour (lerp) layers need a weight or UV the model export
        // does not carry (UNVERIFIED: weight 1 makes a container wall all door and a sign's alphabet layer the whole
        // letter atlas; King, 2026-11-14 bench), so they are left out until that is decoded (research 132 G9).
        if (lc.colorMode != 1) continue;
        const std::string ck = base + '\\x01' + std::to_string(L), rk = base + '\\x02' + std::to_string(L);
        const int cid = textureFor(ck), rid = textureFor(rk);
        if (!texIds_.count(ck) || !texIds_.count(rk)) return false;  // still loading
        if (cid < 0 || cid == checkerTex_) continue;  // no colour image
        app::PropLayer pl;
        pl.texture = cid;
        pl.reveal = rid == checkerTex_ ? -1 : rid;
        const float p[16] = {1.0f, float(lc.flags), lc.sharpness, lc.colorMode == 1 ? 1.0f : 0.0f, lc.tint[0], lc.tint[1], lc.tint[2], 0.0f,
                             lc.uvCos, lc.uvSin, lc.uvScale[0], lc.uvScale[1], lc.uvOffset[0], lc.uvOffset[1], 0.0f, 0.0f};
        std::copy(p, p + 16, pl.params);
        out.push_back(pl);
    }''', '''    // Research 136: the layer kinds and UV sets from the technique set name, the slot-126 mask.
    const auto facts = factsLookup_ ? factsLookup_(base) : std::nullopt;
    static const bool noLerp = std::getenv("IW8R_NO_LERP_LAYERS") != nullptr;  // A/B: multiply layers only, as before
    int maskId = -1;
    if (facts && !facts->layerMaskImage.empty()) {
        const std::string mk = "\\x05" + facts->layerMaskImage;
        const int id = textureFor(mk);
        if (!texIds_.count(mk)) return false;  // still loading
        maskId = id == checkerTex_ ? -1 : id;
    }
    std::vector<app::PropLayer> out;
    for (int L = 1; L < c->layers && size_t(L) < c->layer.size(); ++L) {
        const StockLayerConstants& lc = c->layer[size_t(L)];
        const int kind = facts && size_t(L) < facts->layerKind.size() ? facts->layerKind[size_t(L)] : 0;
        if (kind == 2) continue;  // a code layer (vehicle dirt / rubble; not decoded)
        // Overlay layers (soft light) ignore the colour mode; standard layers: 0 lerp, 1 multiply, 2 / 3 normal and gloss
        // only (not drawn: a blended pass cannot change the normal under the shaded pixel).
        float drawKind = kind == 1 ? 2.0f : 0.0f;
        if (kind == 0) {
            if (lc.colorMode >= 2) continue;
            if (lc.colorMode == 1) drawKind = 1.0f;
            else if (noLerp) continue;
        }
        const std::string ck = base + '\\x01' + std::to_string(L), rk = base + '\\x02' + std::to_string(L);
        const int cid = textureFor(ck), rid = textureFor(rk);
        if (!texIds_.count(ck) || !texIds_.count(rk)) return false;  // still loading
        if (cid < 0 || cid == checkerTex_) continue;  // no colour image
        app::PropLayer pl;
        pl.texture = cid;
        pl.reveal = rid == checkerTex_ ? -1 : rid;
        const float p[16] = {1.0f, float(lc.flags), lc.sharpness, drawKind, lc.tint[0], lc.tint[1], lc.tint[2], 0.0f,
                             lc.uvCos, lc.uvSin, lc.uvScale[0], lc.uvScale[1], lc.uvOffset[0], lc.uvOffset[1], 0.0f, 0.0f};
        std::copy(p, p + 16, pl.params);
        pl.aux[0] = facts && size_t(L) < facts->layerUv1.size() && facts->layerUv1[size_t(L)] ? 1.0f : 0.0f;
        if (maskId >= 0 && L <= 3) pl.mask = maskId, pl.aux[1] = float(L);  // channel L - 1, stored + 1
        out.push_back(pl);
    }''')

# ---- MainWindow: facts and the mesh's second UV
MW = 'src/qtapp/main_window.cpp'
patch(MW, '''        if (const auto* c63 = cst(63)) f.uvAnim = *c63;  // UV animation (research 133 section 1)''',
      '''        if (const auto* c63 = cst(63)) f.uvAnim = *c63;  // UV animation (research 133 section 1)
        // Model-layered materials (research 136): layer kinds and UV sets from the technique set name, the mask.
        if (e->modelLayers >= 2) {
            const ModelLayerTokens tk = parseModelLayerTokens(e->techset.toStdString());
            for (const ModelLayerKind k : tk.kind) f.layerKind.push_back(k == ModelLayerKind::Overlay ? 1 : k == ModelLayerKind::Code ? 2 : 0);
            for (const bool u : tk.uv1) f.layerUv1.push_back(u ? 1 : 0);
            if (tk.mask126)
                for (const QString& t : e->textures)
                    if (t.startsWith("126=")) f.layerMaskImage = t.mid(4).toStdString();
        }''')
patch(MW, '''                            if (lmapUv) {
                                const auto u16 = [](float c) { return uint32_t(std::clamp(c, 0.0f, 1.0f) * 65535.0f + 0.5f); };''',
      '''                            if (ms.uv1.size() == nv) pv.u1 = float(ms.uv1[v][0]), pv.v1 = float(ms.uv1[v][1]);  // research 136
                            if (lmapUv) {
                                const auto u16 = [](float c) { return uint32_t(std::clamp(c, 0.0f, 1.0f) * 65535.0f + 0.5f); };''')
print('ok')
