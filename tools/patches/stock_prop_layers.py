import io

# Research 132 G9 / research 107 section 3: the extra layers of model-layered stock materials (tm2o, tm2co, m2o, ...:
# a second or third colour layer with its own UV transform, tint, reveal step and alpha; dirt, paint, wear) on props.
# Their material export says layerCount 0; the technique set name carries the count (`_0_<N>_1_`, N >= 2).
#   - material cache: Entry/Work::modelLayers (from the name) and layerPsData (the raw lit buffer the layered decoder
#     reads; the single-layer path is unchanged), the layer images of slots L / 9 + L / 27 + L; entries of such
#     technique sets resolve again once; layerImages / layerConstants serve them.
#   - camera: each prop mesh's layers 1.. (colour modes 0 and 1; modes 2 and 3 change only normal and gloss and are
#     left out as on the world) become PropLayer records (colour, reveal, the world layer constants).
#   - renderer: a pass after the props draws each record over the same buffers (test-only depth, alpha or multiply
#     blending, the layer constants in b7); vs_tex_inst marks prop vertices (vc.w = 1) so the pixel shader uses the
#     model weight 1 instead of the world's tangent-length weight.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


# ---------------- material cache
H = 'src/qtapp/material_cache.h'
patch(H, '''        int layerCount = 0;
        std::vector<LayerImages> layers;''', '''        int layerCount = 0;
        std::vector<LayerImages> layers;
        // Model-layered materials (research 107 section 3: tm2o, tm2co, m2o, ...; layerCount 0 in the export, the
        // technique set name's layer count N >= 2): N, and the raw lit buffer the layered decoder reads (psData above
        // keeps the single-layer rows of the base). 0 / empty for the others.
        int modelLayers = 0;
        QByteArray layerPsData;''')
patch(H, '''        int layerCount = 0;     // body layerCount (0 when absent)''', '''        int layerCount = 0;     // body layerCount (0 when absent)
        int modelLayers = 0;    // Entry::modelLayers
        QByteArray layerPsData; // Entry::layerPsData''')
C = 'src/qtapp/material_cache.cpp'
patch(C, '''#include "material_blend.h"''', '''#include "material_blend.h"
#include "stock_decal.h"''')
patch(C, '''std::vector<MaterialCache::LayerImages> MaterialCache::layerImages(const QString& material) const {
    const auto e = lookup(material);
    if (!e || e->failed || e->layerCount < 2) return {};''', '''std::vector<MaterialCache::LayerImages> MaterialCache::layerImages(const QString& material) const {
    const auto e = lookup(material);
    if (!e || e->failed || std::max(e->layerCount, e->modelLayers) < 2) return {};''')
patch(C, '''std::optional<StockLayeredMaterial> MaterialCache::layerConstants(const QString& material) const {
    const auto e = lookup(material);''', '''std::optional<StockLayeredMaterial> MaterialCache::layerConstants(const QString& material) const {
    const auto e = lookup(material);
    if (e && !e->failed && e->layerCount < 2 && e->modelLayers >= 2 && !e->layerPsData.isEmpty()) {
        // A model-layered material: its raw lit buffer, N layers on one UV set (research 107 section 3.3).
        const std::vector<uint8_t> raw(e->layerPsData.begin(), e->layerPsData.end());
        StockLayeredMaterial m;
        if (!decodeStockLayeredMaterial(raw, e->modelLayers, m)) return std::nullopt;
        return m;
    }''')
patch(C, '''        e.psData = QByteArray::fromHex(o.value("psData").toString().toLatin1());''', '''        e.psData = QByteArray::fromHex(o.value("psData").toString().toLatin1());
        e.modelLayers = o.value("modelLayers").toInt(0);
        e.layerPsData = QByteArray::fromHex(o.value("layerPsData").toString().toLatin1());''')
patch(C, '''        if (!e.psData.isEmpty()) o.insert("psData", QString::fromLatin1(e.psData.toHex()));''',
      '''        if (!e.psData.isEmpty()) o.insert("psData", QString::fromLatin1(e.psData.toHex()));
        if (!e.failed) o.insert("modelLayers", e.modelLayers);  // also 0: the name was read (research 107 section 3)
        if (!e.layerPsData.isEmpty()) o.insert("layerPsData", QString::fromLatin1(e.layerPsData.toHex()));''')
patch(C, '''        if (!e.failed && e.techset.section('/', -1).startsWith("iw8rf_")) continue;''', '''        if (!e.failed && e.techset.section('/', -1).startsWith("iw8rf_")) continue;
        // Resolved before model-layered materials kept their layers (research 132 G9): those resolve again once.
        if (!e.failed && !o.contains("modelLayers") && e.layerCount < 2) {
            const StockTechsetTraits tr = parseStockTechsetName(e.techset.toStdString());
            if (tr.valid && tr.layers >= 2 && (tr.family.rfind("m", 0) == 0 || tr.family.rfind("tm", 0) == 0)) continue;
        }''')
patch(C, '''    work.layerCount = fields.value("layerCount").toInt(0);
    const int layers = std::clamp(work.layerCount, 0, 8);''', '''    work.layerCount = fields.value("layerCount").toInt(0);
    // Model-layered materials (research 107 section 3.3): layerCount 0, the count in the technique set name.
    work.modelLayers = 0;
    work.layerPsData.clear();
    if (work.layerCount < 2) {
        const StockTechsetTraits tr = parseStockTechsetName(work.techset.toStdString());
        if (tr.valid && tr.layers >= 2 && (tr.family.rfind("m", 0) == 0 || tr.family.rfind("tm", 0) == 0))
            work.modelLayers = std::min(tr.layers, 8);
    }
    const int layers = std::clamp(std::max(work.layerCount, work.modelLayers), 0, 8);''')
patch(C, '''    // Single-layer materials: the rows in the order the decoders read (row 0 c72, 1 c0, 2 c9, 3 c54). The lit''',
      '''    if (work.modelLayers >= 2) work.layerPsData = work.psData;  // the raw lit buffer, before the single-layer rows below
    // Single-layer materials: the rows in the order the decoders read (row 0 c72, 1 c0, 2 c9, 3 c54). The lit''')
patch(C, '''            e.layerCount = w.layerCount;''', '''            e.layerCount = w.layerCount;
            e.modelLayers = w.modelLayers;
            e.layerPsData = w.layerPsData;''')

# ---------------- renderer
RH = 'src/viewport/render.h'
patch(RH, '''struct PropMeshData {''', '''// One extra layer of a model-layered prop material (research 132 G9): its colour and reveal textures (createTexture
// ids; reveal -1 = white) and the world layer pass's constants (layerP, layerTint, layerUvT, layerUvO).
struct PropLayer {
    int texture = -1, reveal = -1;
    float params[16] = {};
};
struct PropMeshData {''')
patch(RH, '''        float wave[16] = {};        // setReferenceInstWave: constants 79-82 (research 133 section 6)''',
      '''        float wave[16] = {};        // setReferenceInstWave: constants 79-82 (research 133 section 6)
        std::vector<PropLayer> layers;  // setReferenceInstLayers: layers 1.. of a model-layered material''')
patch(RH, '''    void setReferenceInstWave(size_t mesh, const float wave[16]);''', '''    void setReferenceInstWave(size_t mesh, const float wave[16]);
    // A prop mesh's extra layers (model-layered materials, research 132 G9), drawn over it after the props.
    void setReferenceInstLayers(size_t mesh, std::vector<PropLayer> layers) {
        if (mesh < refTexInst_.size()) refTexInst_[mesh].layers = std::move(layers);
    }''')
R = 'src/viewport/render.cpp'
patch(R, '''float3 lgvBack : TEXCOORD5; float3 edge : TEXCOORD6; float3 vc : TEXCOORD7; };  // vc: a prop's vertex colour (linear; research 133)''',
      '''float3 lgvBack : TEXCOORD5; float3 edge : TEXCOORD6; float4 vc : TEXCOORD7; };  // vc: a prop's vertex colour (linear; research 133); w = 1 on props''')
patch(R, '''    o.vc = float3(1, 1, 1);''', '''    o.vc = float4(1, 1, 1, 0);''')
patch(R, '''    o.vc = i.vcol.rgb <= 0.04045 ? i.vcol.rgb / 12.92 : pow((i.vcol.rgb + 0.055) / 1.055, 2.4);''',
      '''    o.vc = float4(i.vcol.rgb <= 0.04045 ? i.vcol.rgb / 12.92 : pow((i.vcol.rgb + 0.055) / 1.055, 2.4), 1.0);  // w: a prop''')
patch(R, '''        float3 albedo = srgbToLinear3(t.rgb) * (stockRef ? i.vc : i.col.rgb);''', '''        float3 albedo = srgbToLinear3(t.rgb) * (stockRef ? i.vc.rgb : i.col.rgb);''')
patch(R, '''        float w = saturate(length(i.t) - 1.0), k = layerP.z;  // the tangent's length is 1 + the weight''',
      '''        // The tangent's length is 1 + the weight on the world; a model layer's weight is 1 (research 107 section 3.2).
        float w = i.vc.w > 0.5 ? 1.0 : saturate(length(i.t) - 1.0), k = layerP.z;''')
patch(R, '''    if (decalStateSet) {
        ctx_->RSSetState(savedRs);
        ctx_->OMSetDepthStencilState(savedDs, savedRef);
        if (savedRs) savedRs->Release();
        if (savedDs) savedDs->Release();
    }
    if (instCb && cbLayer_) ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);  // the plain (zero) layer buffer''',
      '''    // Pass 3: the extra layers of model-layered materials (research 132 G9) over their meshes, as the world's layer
    // pass: test-only depth, blended by the layer's reveal step and alpha (or multiplied), its constants in b7.
    if (!texPsOverride_ && cbLayer_) {
        bool layerStateSet = false;
        for (const auto& g : refTexInst_) {
            if (g.layers.empty() || !g.vb || !g.inst || g.drawClass != 0) continue;
            if (!layerStateSet) {
                layerStateSet = true;
                if (!decalStateSet) {
                    decalStateSet = true;
                    ctx_->RSGetState(&savedRs);
                    ctx_->OMGetDepthStencilState(&savedDs, &savedRef);
                }
                if (savedRs) ctx_->RSSetState(savedRs);
                ctx_->OMSetDepthStencilState(dsTest_, 0);
                if (cbBatch_) {
                    const float flags[8] = {0, 1.0f, 0, 0, 0, 0, game_.skyIsStock ? 1.0f : 0.0f, 0};  // no normal map; blended
                    writeBatchCb(flags);
                }
                ID3D11ShaderResourceView* noN = nullptr;
                ctx_->PSSetShaderResources(1, 1, &noN);
                ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);
            }
            const size_t mi = size_t(&g - refTexInst_.data());
            if (instCb && cbInstAll_[mi / kInstPerCb]) {  // the mesh's waves, so the layer stays on the moving surface
                const UINT first = UINT((mi % kInstPerCb) * 16), num = 16;
                ctx1_->VSSetConstantBuffers1(8, 1, &cbInstAll_[mi / kInstPerCb], &first, &num);
            }
            ID3D11Buffer* bufs[2] = {g.vb, g.inst};
            ctx_->IASetVertexBuffers(0, 2, bufs, strides, offsets);
            ctx_->IASetIndexBuffer(g.ib, DXGI_FORMAT(g.ibFormat), 0);
            for (const PropLayer& L : g.layers) {
                if (L.texture < 0 || size_t(L.texture) >= textures_.size()) continue;
                const int rv = L.reveal >= 0 && size_t(L.reveal) < textures_.size() ? L.reveal : whiteFullTex_;
                if (rv < 0 || size_t(rv) >= textures_.size()) continue;
                ctx_->UpdateSubresource(cbLayer_, 0, nullptr, L.params, 0, 0);
                const float bfz[4] = {0, 0, 0, 0};
                ctx_->OMSetBlendState(L.params[3] > 0.5f && blendMul_ ? blendMul_ : blend_, bfz, 0xffffffff);
                ctx_->PSSetShaderResources(0, 1, &textures_[size_t(L.texture)]);
                ctx_->PSSetShaderResources(12, 1, &textures_[size_t(rv)]);
                drawInstIdx(g.vertices, g.instances);
            }
        }
        if (layerStateSet) {
            const float zero16[16] = {};
            ctx_->UpdateSubresource(cbLayer_, 0, nullptr, zero16, 0, 0);  // the plain layer buffer is zero again
            ID3D11ShaderResourceView* noR = nullptr;
            ctx_->PSSetShaderResources(12, 1, &noR);
        }
    }
    if (decalStateSet) {
        ctx_->RSSetState(savedRs);
        ctx_->OMSetDepthStencilState(savedDs, savedRef);
        if (savedRs) savedRs->Release();
        if (savedDs) savedDs->Release();
    }
    if (instCb && cbLayer_) ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);  // the plain (zero) layer buffer''')

# ---------------- camera: the layers of each prop mesh
CV = 'src/qtapp/camera_view.cpp'
patch(CV, '''                    if (!applyMaterialExtras(texInstMaterials_[k], k, true)) {
                        pending = true;
                        continue;
                    }
                    texInstResolved_[k] = true;''', '''                    if (!applyMaterialExtras(texInstMaterials_[k], k, true)) {
                        pending = true;
                        continue;
                    }
                    if (!resolvePropLayers(k)) {  // model-layered: its layer images (research 132 G9)
                        pending = true;
                        continue;
                    }
                    texInstResolved_[k] = true;''')
patch(CV, '''bool CameraView::resolveReferenceLayer(size_t k) {''', '''bool CameraView::resolvePropLayers(size_t k) {
    if (!layerLookup_ || k >= texInstMaterials_.size()) return true;
    const std::string& base = texInstMaterials_[k];
    if (base.empty() || base.find_first_of("\\x01\\x02\\x05\\x06") != std::string::npos) return true;
    const std::optional<StockLayeredMaterial> c = layerLookup_(base);
    if (!c || c->layers < 2) return true;
    std::vector<app::PropLayer> out;
    for (int L = 1; L < c->layers && size_t(L) < c->layer.size(); ++L) {
        const StockLayerConstants& lc = c->layer[size_t(L)];
        if (lc.colorMode >= 2) continue;  // modes 2 / 3: normal and gloss only (not drawn, as on the world)
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
    }
    renderer_.setReferenceInstLayers(k, std::move(out));
    return true;
}

bool CameraView::resolveReferenceLayer(size_t k) {''')
patch('src/qtapp/camera_view.h', '''    bool resolveReferenceLayer(size_t k);  // true when settled (drawn or left out)''', '''    bool resolveReferenceLayer(size_t k);  // true when settled (drawn or left out)
    bool resolvePropLayers(size_t k);  // a prop mesh's extra layers (model-layered materials); false while loading''')
print('ok')
