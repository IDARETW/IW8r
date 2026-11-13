import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b, *cnt in pairs:
        n = cnt[0] if cnt else 1
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == n, (p, s.count(a), a[:100])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


# ---------------------------------------------------------------- scene: layer batches
patch('src/viewport/scene.h', [
 ('''struct ReferenceTexBatch {
    std::string material;
    std::vector<GpuTexVertex> tris;
};''', '''struct ReferenceTexBatch {
    std::string material;
    std::vector<GpuTexVertex> tris;
    // 0: the material's base (layer 0); L >= 1: layer L of a layered stock material (tw4 / twc4, research 97), drawn over
    // the layers below it with its own texture coordinates (uv) and its blend weight in the vertex tangent x (tx).
    int layer = 0;
};'''),
])
patch('src/viewport/scene.cpp', [
 ('''        auto [it, fresh] = byMaterial.emplace(m.material, out.size());
        if (fresh) out.push_back({m.material, {}});
        auto& dst = out[it->second].tris;''', '''        auto [it, fresh] = byMaterial.emplace(m.material, out.size());
        if (fresh) out.push_back({m.material, {}});
        auto& dst = out[it->second].tris;
        // Layers above 0 of a layered stock material: one batch per material and layer (research 97).
        const int layers = mi < world.layerCount.size() ? world.layerCount[mi] : 1;
        const bool layered = layers > 1 && mi < world.layerUv.size() && world.layerUv[mi].size() == m.pos.size() * size_t(layers);
        std::vector<std::vector<GpuTexVertex>*> layerDst;
        if (layered)
            for (int L = 1; L < layers; ++L) {
                auto [lit, lfresh] = byMaterial.emplace(m.material + '\\x01' + std::to_string(L), out.size());
                if (lfresh) {
                    out.push_back({m.material, {}});
                    out.back().layer = L;
                }
                layerDst.push_back(&out[lit->second].tris);
            }
        const std::vector<uint32_t>* vcol = layered && mi < world.vertexColor.size() && world.vertexColor[mi].size() == m.pos.size() ? &world.vertexColor[mi] : nullptr;
        const std::vector<uint32_t>* vcolHi =
            layered && mi < world.vertexColorHigh.size() && world.vertexColorHigh[mi].size() == m.pos.size() ? &world.vertexColorHigh[mi] : nullptr;'''),
 ('''                if (lmuv && (*lmuv)[k][0] >= 0) {
                    const int li = lmidx ? (*lmidx)[k] : 0;
                    if (li >= 0) v.lu = float(li) + std::min((*lmuv)[k][0], 0.99994f), v.lv = (*lmuv)[k][1];
                }
                dst.push_back(v);
            }''', '''                if (lmuv && (*lmuv)[k][0] >= 0) {
                    const int li = lmidx ? (*lmidx)[k] : 0;
                    if (li >= 0) v.lu = float(li) + std::min((*lmuv)[k][0], 0.99994f), v.lv = (*lmuv)[k][1];
                }
                dst.push_back(v);
                for (int L = 1; L <= int(layerDst.size()); ++L) {
                    GpuTexVertex lv = v;
                    const auto& luv = world.layerUv[mi][size_t(k) * size_t(layers) + size_t(L)];
                    lv.u = luv[0], lv.v = 1.0f - luv[1];
                    lv.tx = stockLayerWeight(vcol ? (*vcol)[k] : 0xFFFFFFFFu, vcolHi ? (*vcolHi)[k] : 0xFFFFFFFFu, L);
                    lv.ty = lv.tz = 0;
                    layerDst[size_t(L - 1)]->push_back(lv);
                }
            }'''),
])

# ---------------------------------------------------------------- renderer
patch('src/viewport/render.h', [
 ('''    bool setReferenceTextured(const std::vector<std::vector<GpuTexVertex>>& batches);''',
  '''    // layers (optional, parallel to batches): 0 for a base batch, L >= 1 for layer L of a layered stock material, drawn
    // after every base batch (in layer order) with setReferenceLayer's blend; not drawn until then.
    bool setReferenceTextured(const std::vector<std::vector<GpuTexVertex>>& batches, const std::vector<int>* layers = nullptr);
    // A layer batch's blend (research 97): reveal = its alpha + reveal texture (-1: none, white), params = flags, reveal
    // sharpness k, tint RGB, UV transform (cos, sin, scale u, scale v, offset u, offset v); see the shader's cbuffer Layer.
    void setReferenceLayer(size_t batch, int revealTexture, const float params[16]);'''),
 ('''    ID3D11Buffer* cbBatch_ = nullptr;  // per textured batch: x = has a normal map''',
  '''    ID3D11Buffer* cbBatch_ = nullptr;  // per textured batch: x = has a normal map
    ID3D11Buffer* cbLayer_ = nullptr;  // b7: a stock layer batch's blend (zero = not a layer)
    struct RefLayer {
        int layer = 0;  // 0 = a base batch
        bool ready = false;
        int reveal = -1;
        float params[16] = {};
    };
    std::vector<RefLayer> refLayers_;
    std::vector<size_t> refLayerOrder_;  // layer batches by layer, then batch
    int whiteFullTex_ = -1;'''),
])

LAYER_SHADER = '''// A stock layered material's layer L >= 1 (research 97), drawn over the layers below with blending: layerP = (on, flags,
// reveal sharpness k, _), layerTint = tint RGB, layerUvT = (cos, sin, scale u, scale v), layerUvO = (offset u, v). The
// vertex blend weight is in the tangent x (the stock reference has no tangents). t12: the alpha (red) + reveal (green) map.
cbuffer Layer : register(b7) { float4 layerP; float4 layerTint; float4 layerUvT; float4 layerUvO; };
Texture2D revealTex : register(t12);
float4 ps_tex(PSIn i) : SV_TARGET {
    float layerB = -1.0;
    float2 uvS = i.uv;
    if (layerP.x > 0.5)
        uvS = float2(layerUvT.x * i.uv.x * layerUvT.z + layerUvT.y * i.uv.y * layerUvT.w,
                     -layerUvT.y * i.uv.x * layerUvT.z + layerUvT.x * i.uv.y * layerUvT.w) + layerUvO.xy;
    float4 t = tex0.Sample(samp0, uvS);
    if (layerP.x > 0.5) {
        float2 ar = revealTex.Sample(samp0, uvS).rg;
        float w = saturate(i.t.x), k = layerP.z;
        uint fl = (uint)(layerP.y + 0.5);
        float b = w;
        if (fl & 16u) {
            float lo = saturate(1.0 - w - k * sqrt(w)), hi = saturate(1.0 - w + k * sqrt(1.0 - w));
            b = saturate((ar.g - lo) / max(hi - lo, 1e-4));
        }
        if (fl & 8u) b *= ar.r;
        layerB = b;
        t.rgb *= pow(max(layerTint.rgb, 0.0), 1.0 / 2.2);  // the tint is linear; t is still sRGB here
    }'''
patch('src/viewport/render.cpp', [
 ('''float4 ps_tex(PSIn i) : SV_TARGET {
    float4 t = tex0.Sample(samp0, i.uv);''', LAYER_SHADER),
 ('''    if (batchFlags.y < 0.5) clip(t.a - 0.3);
    else outA = t.a > 0.99 ? 0.6 : t.a;
    if (game0.x > 0.5) {''', '''    if (batchFlags.y < 0.5) clip(t.a - 0.3);
    else outA = t.a > 0.99 ? 0.6 : t.a;
    if (layerB >= 0.0) outA = layerB;
    if (game0.x > 0.5) {'''),
 ('''            dev_->CreateBuffer(&bd, nullptr, &cbBatch_);''', '''            dev_->CreateBuffer(&bd, nullptr, &cbBatch_);
            {
                D3D11_BUFFER_DESC lb = bd;
                lb.ByteWidth = 64;
                const float zero[16] = {};
                D3D11_SUBRESOURCE_DATA lz{zero, 0, 0};
                dev_->CreateBuffer(&lb, &lz, &cbLayer_);
            }'''),
 ('''    safeRelease(cbBatch_);''', '''    safeRelease(cbBatch_);
    safeRelease(cbLayer_);'''),
 # base draw loops skip the layer batches
 ('''            for (size_t k = 0; k < refTexIds_.size(); ++k) {
                const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
                if (id < 0 || size_t(id) >= textures_.size()) continue;
                ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]);''', '''            for (size_t k = 0; k < refTexIds_.size(); ++k) {
                if (k < refLayers_.size() && refLayers_[k].layer > 0) continue;  // layers: drawn blended after the bases
                const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
                if (id < 0 || size_t(id) >= textures_.size()) continue;
                ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]);'''),
 ('''        for (size_t k = 0; k < refTexIds_.size(); ++k) {
            const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
            if (id < 0 || size_t(id) >= textures_.size()) continue;
            const auto bind = [&] {''', '''        for (size_t k = 0; k < refTexIds_.size(); ++k) {
            if (k < refLayers_.size() && refLayers_[k].layer > 0) continue;  // layers: drawn blended below
            const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
            if (id < 0 || size_t(id) >= textures_.size()) continue;
            const auto bind = [&] {'''),
 ('''            bind();
            drawN(refTexCount_[k], refTexFirst_[k]);
        }
        ID3D11ShaderResourceView* none2[1] = {nullptr};
        ctx_->PSSetShaderResources(0, 1, none2);''', '''            bind();
            drawN(refTexCount_[k], refTexFirst_[k]);
        }
        // The layers of layered stock materials (terrain): over their base, in layer order, blended by the layer's
        // weight and reveal map, depth-tested against the base (same vertices, same depth) without writing it.
        if (!refLayerOrder_.empty() && cbLayer_) {
            if (cbBatch_) {
                const float flags[8] = {0, 1.0f, 0, 0, 0, 0, 1.0f, stockLmSrv_[0] ? 1.0f : 0.0f};  // y = 1: blended, no clip
                ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);
            }
            ctx_->OMSetDepthStencilState(dsTest_, 0);
            ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);
            for (size_t k : refLayerOrder_) {
                const RefLayer& L = refLayers_[k];
                if (!L.ready || refTexIds_[k] < 0 || size_t(refTexIds_[k]) >= textures_.size()) continue;
                const int rv = L.reveal >= 0 && size_t(L.reveal) < textures_.size() ? L.reveal : whiteFullTex_;
                if (rv < 0 || size_t(rv) >= textures_.size()) continue;
                ctx_->UpdateSubresource(cbLayer_, 0, nullptr, L.params, 0, 0);
                ctx_->PSSetShaderResources(0, 1, &textures_[size_t(refTexIds_[k])]);
                ctx_->PSSetShaderResources(12, 1, &textures_[size_t(rv)]);
                if (refCulling_ && k < refTexChunks_.size() && !refTexChunks_[k].empty())
                    refCulledLast_ += forVisibleRuns(frustum, refTexChunks_[k], [&](uint32_t first, uint32_t n) { drawN(n * 3, refTexFirst_[k] + first * 3); });
                else
                    drawN(refTexCount_[k], refTexFirst_[k]);
            }
            const float zero[16] = {};
            ctx_->UpdateSubresource(cbLayer_, 0, nullptr, zero, 0, 0);
            ID3D11ShaderResourceView* noRev = nullptr;
            ctx_->PSSetShaderResources(12, 1, &noRev);
            ctx_->OMSetDepthStencilState(texOpaqueDs, 0);
        }
        ID3D11ShaderResourceView* none2[1] = {nullptr};
        ctx_->PSSetShaderResources(0, 1, none2);'''),
 # upload: the layer flags
 ('''bool SceneRenderer::setReferenceTextured(const std::vector<std::vector<GpuTexVertex>>& batches) {''',
  '''void SceneRenderer::setReferenceLayer(size_t batch, int revealTexture, const float params[16]) {
    if (batch >= refLayers_.size() || refLayers_[batch].layer <= 0) return;
    RefLayer& L = refLayers_[batch];
    L.ready = true;
    L.reveal = revealTexture;
    std::memcpy(L.params, params, sizeof(L.params));
}

bool SceneRenderer::setReferenceTextured(const std::vector<std::vector<GpuTexVertex>>& batches, const std::vector<int>* layers) {
    refLayers_.assign(batches.size(), RefLayer{});
    refLayerOrder_.clear();
    if (layers && layers->size() == batches.size()) {
        for (size_t k = 0; k < batches.size(); ++k)
            if ((*layers)[k] > 0) refLayers_[k].layer = (*layers)[k], refLayerOrder_.push_back(k);
        std::stable_sort(refLayerOrder_.begin(), refLayerOrder_.end(), [&](size_t a, size_t b) { return refLayers_[a].layer < refLayers_[b].layer; });
    }
    if (whiteFullTex_ < 0 && dev_) {
        const uint8_t white[16] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
        whiteFullTex_ = createTexture(white, 2, 2);
    }'''),
])

# ---------------------------------------------------------------- camera
patch('src/qtapp/camera_view.h', [
 ('''    void setMaterialImages(ImageLookup lookup, ImageRequest request);''',
  '''    void setMaterialImages(ImageLookup lookup, ImageRequest request);
    // Layered stock materials (research 97): a material's per-layer constants; nullopt while the material is not
    // resolved, an empty StockLayeredMaterial (layers 0) when it has none. Layer images are looked up through
    // ImageLookup as "<material>\\x01<L>" (colour) and "<material>\\x02<L>" (alpha + reveal).
    using LayerLookup = std::function<std::optional<StockLayeredMaterial>(const std::string& material)>;
    void setMaterialLayers(LayerLookup lookup) { layerLookup_ = std::move(lookup); }'''),
 ('''    std::vector<std::string> refTexMaterials_;''', '''    std::vector<std::string> refTexMaterials_;
    std::vector<int> refTexLayer_;  // parallel: 0 base, L >= 1 a layer batch of refTexMaterials_[k]
    LayerLookup layerLookup_;
    bool resolveReferenceLayer(size_t k);  // true when settled (drawn or left out)'''),
])
s = io.open('src/qtapp/camera_view.h', encoding='utf-8', newline='').read()
if '#include "stock_layered_material.h"' not in s:
    s = s.replace('#include "stock_probes.h"', '#include "stock_probes.h"\n#include "stock_layered_material.h"', 1)
    io.open('src/qtapp/camera_view.h', 'w', encoding='utf-8', newline='').write(s)

patch('src/qtapp/camera_view.cpp', [
 ('''        for (auto& b : pendingRefTex_) {
            refTexMaterials_.push_back(b.material);
            batches.push_back(std::move(b.tris));
        }
        renderer_.setReferenceTextured(batches);''', '''        refTexLayer_.clear();
        for (auto& b : pendingRefTex_) {
            refTexMaterials_.push_back(b.material);
            refTexLayer_.push_back(b.layer);
            batches.push_back(std::move(b.tris));
        }
        renderer_.setReferenceTextured(batches, &refTexLayer_);'''),
 ('''            for (size_t k = 0; k < refTexMaterials_.size(); ++k) {
                if (refTexResolved_[k]) continue;
                const int id = textureFor(refTexMaterials_[k]);''', '''            for (size_t k = 0; k < refTexMaterials_.size(); ++k) {
                if (refTexResolved_[k]) continue;
                if (k < refTexLayer_.size() && refTexLayer_[k] > 0) {
                    if (resolveReferenceLayer(k)) refTexResolved_[k] = true;
                    else pending = true;
                    continue;
                }
                const int id = textureFor(refTexMaterials_[k]);'''),
 ('''void CameraView::setMaterialImages(ImageLookup lookup, ImageRequest request) {''',
  '''bool CameraView::resolveReferenceLayer(size_t k) {
    const std::string& base = refTexMaterials_[k];
    const int L = refTexLayer_[k];
    if (!layerLookup_) return true;  // no layer data: the layer is left out
    const std::optional<StockLayeredMaterial> c = layerLookup_(base);
    if (!c) {
        if (imageRequest_ && requested_.insert(base).second) imageRequest_(base);
        return false;
    }
    StockLayerConstants lc;
    if (c->layers > L && size_t(L) < c->layer.size()) lc = c->layer[size_t(L)];
    // Colour modes 2 and 3 leave the colour below; multiply (1) is not drawn yet (its normal / gloss blend only).
    if (lc.colorMode != 0) return true;
    const std::string ck = base + '\\x01' + std::to_string(L), rk = base + '\\x02' + std::to_string(L);
    const int cid = textureFor(ck), rid = textureFor(rk);
    if (!texIds_.count(ck) || !texIds_.count(rk)) return false;
    if (cid < 0 || cid == checkerTex_) return true;  // no colour image: left out rather than drawn as the checker
    const float p[16] = {1.0f, float(lc.flags), lc.sharpness, 0.0f, lc.tint[0], lc.tint[1], lc.tint[2], 0.0f,
                         lc.uvCos, lc.uvSin, lc.uvScale[0], lc.uvScale[1], lc.uvOffset[0], lc.uvOffset[1], 0.0f, 0.0f};
    renderer_.setReferenceTexture(k, cid);
    renderer_.setReferenceLayer(k, rid == checkerTex_ ? -1 : rid, p);
    return true;
}

void CameraView::setMaterialImages(ImageLookup lookup, ImageRequest request) {'''),
])

# ---------------------------------------------------------------- main window: layer images and constants
patch('src/qtapp/main_window.cpp', [
 ('''    camera_->setMaterialImages(
        [this](const std::string& m) -> std::optional<QString> {
            if (const CustomMaterial* cm''', '''    camera_->setMaterialImages(
        [this](const std::string& m) -> std::optional<QString> {
            // A layer image of a layered stock material: "<material>\\x01<L>" colour, "<material>\\x02<L>" alpha + reveal.
            if (const size_t sep = m.find_first_of("\\x01\\x02"); sep != std::string::npos && sep > 0) {
                const QString base = qs(m.substr(0, sep));
                const auto e = matCache_->lookup(base);
                if (!e) return std::nullopt;
                if (e->failed) return QString();
                const auto imgs = matCache_->layerImages(base);
                const int L = std::atoi(m.c_str() + sep + 1);
                if (L < 0 || size_t(L) >= imgs.size()) return QString();
                return m[sep] == '\\x01' ? imgs[size_t(L)].colorDds : imgs[size_t(L)].revealDds;
            }
            if (const CustomMaterial* cm'''),
 ('''        [this](const std::string& m) { matCache_->request({qs(m)}); });''',
  '''        [this](const std::string& m) { matCache_->request({qs(m.substr(0, m.find_first_of("\\x01\\x02")))}); });
    camera_->setMaterialLayers([this](const std::string& m) -> std::optional<StockLayeredMaterial> {
        const auto e = matCache_->lookup(qs(m));
        if (!e) return std::nullopt;
        if (auto c = matCache_->layerConstants(qs(m))) return *c;
        return StockLayeredMaterial{};
    });'''),
])
print('ok')
