import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b, *c in pairs:
        n = c[0] if c else 1
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == n, (p, s.count(a), a[:100])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


# =============================================================== material cache: constants, tint, decal flag for all
patch('src/qtapp/material_cache.h', [
 ('''        int layerCount = 0;
        std::vector<LayerImages> layers;
        QByteArray psData;
        bool failed = false;''', '''        int layerCount = 0;
        std::vector<LayerImages> layers;
        QByteArray psData;  // the body's first pixel constant buffer (every material since tintRules 1)
        // The colour tint (material constant 9, RRGGBB; -1 = neutral or unknown) and whether a tint mask (texture slot
        // 59) limits it (research 95: albedo x lerp(1, tint, mask)).
        int tint = -1;
        bool tintMasked = false;
        bool failed = false;'''),
 ('''    std::optional<StockLayeredMaterial> layerConstants(const QString& material) const;''',
  '''    std::optional<StockLayeredMaterial> layerConstants(const QString& material) const;
    // The same for any stock material with a pixel constant buffer (single-layer world materials: layer 0 only), e.g.
    // a decal's flags and reveal sharpness (research 98).
    std::optional<StockLayeredMaterial> materialConstants(const QString& material) const;'''),
 ('''        QByteArray psData;      // constantBufferTable[0].psData of a layered body''',
  '''        QByteArray psData;      // the body's first pixel constant buffer
        int tint = -1;          // constant 9 (RRGGBB), -1 = neutral / none
        bool tintMasked = false;'''),
])
patch('src/qtapp/material_cache.cpp', [
 ('''std::vector<QString> MaterialCache::layerColorImages(const QString& material) const {''',
  '''std::optional<StockLayeredMaterial> MaterialCache::materialConstants(const QString& material) const {
    const auto e = lookup(material);
    if (!e || e->failed || e->psData.isEmpty()) return std::nullopt;
    const std::vector<uint8_t> bytes(e->psData.begin(), e->psData.end());
    StockLayeredMaterial m;
    if (!decodeStockLayeredMaterial(bytes, std::clamp(e->layerCount, 1, 8), m)) return std::nullopt;
    return m;
}

std::vector<QString> MaterialCache::layerColorImages(const QString& material) const {'''),
 ('''    work.psData.clear();
    if (layers >= 2) {
        for (int l = 0; l < layers; ++l) {
            work.layerColor << QString();
            work.layerNormal << QString();
            work.layerReveal << QString();
        }
        // The first buffer with pixel data: displacement layers (`..._129dn_...`, slot 96) have a vertex-only buffer 0
        // and the pixel constants in buffer 1.
        for (const QJsonValue& b : fields.value("constantBufferTable").toObject().value("values").toArray()) {
            work.psData = QByteArray::fromHex(b.toObject().value("psData").toObject().value("bytes").toString().toLatin1());
            if (!work.psData.isEmpty()) break;
        }
    }''', '''    work.psData.clear();
    if (layers >= 2) {
        for (int l = 0; l < layers; ++l) {
            work.layerColor << QString();
            work.layerNormal << QString();
            work.layerReveal << QString();
        }
    }
    // The first buffer with pixel data (every material: a decal's flags and ramp, research 98): displacement layers
    // (`..._129dn_...`, slot 96) have a vertex-only buffer 0 and the pixel constants in buffer 1.
    for (const QJsonValue& b : fields.value("constantBufferTable").toObject().value("values").toArray()) {
        work.psData = QByteArray::fromHex(b.toObject().value("psData").toObject().value("bytes").toString().toLatin1());
        if (!work.psData.isEmpty()) break;
    }
    // The colour tint: constant 9's literal, R G B bytes then 0 (tools/build_material_tints.py); white = neutral.
    work.tint = -1;
    work.tintMasked = false;
    for (const QJsonValue& cv : fields.value("constantTable").toObject().value("values").toArray()) {
        const QJsonObject c = cv.toObject();
        if (c.value("index").toInt(-1) != 9) continue;
        const QJsonArray v = c.value("literal").toObject().value("v").toArray();
        if (v.isEmpty() || !v[0].isDouble()) break;
        const float f = float(v[0].toDouble());
        uint8_t b[4];
        std::memcpy(b, &f, 4);
        if (b[3] == 0 && !(b[0] == 255 && b[1] == 255 && b[2] == 255)) work.tint = (b[0] << 16) | (b[1] << 8) | b[2];
        break;
    }'''),
 ('''        if (slot == 18 && work.emissive.isEmpty()) work.emissive = name;  // emissive slot''',
  '''        if (slot == 18 && work.emissive.isEmpty()) work.emissive = name;  // emissive slot
        if (slot == 59) work.tintMasked = true;                           // tint mask (research 95)'''),
 ('''            e.layerCount = w.layerCount;
            e.psData = w.psData;''', '''            e.layerCount = w.layerCount;
            e.psData = w.psData;
            e.tint = w.tint;
            e.tintMasked = w.tintMasked;'''),
 ('''        e.psData = QByteArray::fromHex(o.value("psData").toString().toLatin1());''',
  '''        e.psData = QByteArray::fromHex(o.value("psData").toString().toLatin1());
        e.tint = o.contains("tint") ? o.value("tint").toString().toInt(nullptr, 16) : -1;
        e.tintMasked = o.value("tintMasked").toBool();'''),
 ('''        // Images removed by the Cache Manager's size cap (4.13) or by hand: resolve again.''',
  '''        // Resolved before every body's constants and tint were kept (decals, painted materials): resolve again once.
        if (!e.failed && o.value("tintRules").toInt() < 1) continue;
        // Images removed by the Cache Manager's size cap (4.13) or by hand: resolve again.'''),
 ('''        if (!e.failed) o.insert("layerRules", 1);  // 1: layer images and psData of layered bodies (research 97)''',
  '''        if (!e.failed) o.insert("layerRules", 1);  // 1: layer images and psData of layered bodies (research 97)
        if (!e.failed) o.insert("tintRules", 1);   // 1: every body's psData, tint (constant 9) and tint mask flag
        if (e.tint >= 0) o.insert("tint", QString("%1").arg(e.tint, 6, 16, QChar('0')));
        if (e.tintMasked) o.insert("tintMasked", true);'''),
])
s = io.open('src/qtapp/material_cache.cpp', encoding='utf-8', newline='').read()
for inc in ('#include <cstring>', '#include <algorithm>'):
    if inc not in s:
        i = s.index('#include <')
        s = s[:i] + inc + '\n' + s[i:]
io.open('src/qtapp/material_cache.cpp', 'w', encoding='utf-8', newline='').write(s)

# =============================================================== scene: the base weight in the tangent length
patch('src/viewport/scene.cpp', [
 ('''        const std::vector<uint32_t>* vcol = layered && mi < world.vertexColor.size() && world.vertexColor[mi].size() == m.pos.size() ? &world.vertexColor[mi] : nullptr;''',
  '''        const std::vector<uint32_t>* vcol = mi < world.vertexColor.size() && world.vertexColor[mi].size() == m.pos.size() ? &world.vertexColor[mi] : nullptr;
        // Layer 0's weight (decals, research 98): twc4 byte 0, twc the vertex alpha, others 1.
        const bool twc4 = lm.rfind("twc4/", 0) == 0, twc = !twc4 && lm.rfind("twc/", 0) == 0;'''),
 ('''                v.tx = float(tan0.x), v.ty = float(tan0.y), v.tz = float(tan0.z);''',
  '''                {
                    const uint32_t col = vcol ? (*vcol)[k] : 0xFFFFFFFFu;
                    const double w0 = twc4 ? double(col & 0xFF) / 255.0 : twc ? double(col >> 24) / 255.0 : 1.0;
                    const Vec3 t0 = tan0 * (1.0 + w0);  // length 1 + weight, as the layers
                    v.tx = float(t0.x), v.ty = float(t0.y), v.tz = float(t0.z);
                }'''),
])

# =============================================================== renderer: decal pass
patch('src/viewport/render.h', [
 ('''    struct RefLayer {
        int layer = 0;  // 0 = a base batch
        bool ready = false;''', '''    struct RefLayer {
        int layer = 0;  // 0 = a base batch
        bool ready = false;
        bool decal = false;  // a decal material's batch (research 98): drawn after every opaque batch and layer
        int sortKey = 0;'''),
 ('''    void setReferenceLayer(size_t batch, int revealTexture, const float params[16]);''',
  '''    void setReferenceLayer(size_t batch, int revealTexture, const float params[16]);
    // A batch of a stock decal material (sortKey 10-17, techset blend `_bad_`; research 98): blended over the opaque
    // world (premultiplied by its coverage B, depth tested with a small offset, no depth write), in sortKey order.
    // Layer 0 (layer == 0) takes params / reveal as a layer would (its weight is in the tangent length too).
    void setReferenceDecal(size_t batch, int sortKey, int revealTexture, const float params[16]);'''),
 ('''    std::vector<size_t> refLayerOrder_;  // layer batches by layer, then batch''',
  '''    std::vector<size_t> refLayerOrder_;  // layer batches by layer, then batch
    std::vector<size_t> refDecalOrder_;  // decal batches by sortKey, layer, batch
    bool refDecalDirty_ = false;
    ID3D11RasterizerState* rsDecal_ = nullptr;'''),
])
patch('src/viewport/render.cpp', [
 ('''void SceneRenderer::setReferenceLayer(size_t batch, int revealTexture, const float params[16]) {''',
  '''void SceneRenderer::setReferenceDecal(size_t batch, int sortKey, int revealTexture, const float params[16]) {
    if (batch >= refLayers_.size()) return;
    RefLayer& L = refLayers_[batch];
    L.decal = true;
    L.sortKey = sortKey;
    L.ready = true;
    L.reveal = revealTexture;
    std::memcpy(L.params, params, sizeof(L.params));
    refDecalDirty_ = true;
}

void SceneRenderer::setReferenceLayer(size_t batch, int revealTexture, const float params[16]) {'''),
 # opaque loops skip decal bases
 ('''                if (k < refLayers_.size() && refLayers_[k].layer > 0) continue;  // layers: drawn blended after the bases''',
  '''                if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal)) continue;  // layers, decals: later'''),
 ('''            if (k < refLayers_.size() && refLayers_[k].layer > 0) continue;  // layers: drawn blended below''',
  '''            if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal)) continue;  // layers, decals: below'''),
 ('''            for (size_t k : refLayerOrder_) {
                const RefLayer& L = refLayers_[k];
                if (!L.ready || refTexIds_[k] < 0 || size_t(refTexIds_[k]) >= textures_.size()) continue;''',
  '''            if (refDecalDirty_) {
                refDecalDirty_ = false;
                refDecalOrder_.clear();
                for (size_t k = 0; k < refLayers_.size(); ++k)
                    if (refLayers_[k].decal) refDecalOrder_.push_back(k);
                std::stable_sort(refDecalOrder_.begin(), refDecalOrder_.end(), [&](size_t a, size_t b) {
                    const RefLayer &x = refLayers_[a], &y = refLayers_[b];
                    return x.sortKey != y.sortKey ? x.sortKey < y.sortKey : x.layer < y.layer;
                });
            }
            // Opaque layers first, then the decals (their layers with them) with a small depth offset.
            std::vector<size_t> passOrder;
            passOrder.reserve(refLayerOrder_.size() + refDecalOrder_.size());
            for (size_t k : refLayerOrder_)
                if (!refLayers_[k].decal) passOrder.push_back(k);
            const size_t firstDecal = passOrder.size();
            passOrder.insert(passOrder.end(), refDecalOrder_.begin(), refDecalOrder_.end());
            for (size_t pi = 0; pi < passOrder.size(); ++pi) {
                const size_t k = passOrder[pi];
                if (pi == firstDecal && rsDecal_) ctx_->RSSetState(rsDecal_);
                const RefLayer& L = refLayers_[k];
                if (!L.ready || refTexIds_[k] < 0 || size_t(refTexIds_[k]) >= textures_.size()) continue;'''),
 ('''        if (!refLayerOrder_.empty() && cbLayer_) {''', '''        if ((!refLayerOrder_.empty() || refDecalDirty_ || !refDecalOrder_.empty()) && cbLayer_) {'''),
 ('''            ctx_->PSSetShaderResources(12, 1, &noRev);
            ctx_->PSSetShaderResources(1, 1, &noRev);''', '''            ctx_->PSSetShaderResources(12, 1, &noRev);
            ctx_->PSSetShaderResources(1, 1, &noRev);
            ctx_->RSSetState(rsTris_);'''),
 ('''    refLayers_.assign(batches.size(), RefLayer{});
    refNrmIds_.assign(batches.size(), -1);
    refLayerOrder_.clear();''', '''    refLayers_.assign(batches.size(), RefLayer{});
    refNrmIds_.assign(batches.size(), -1);
    refLayerOrder_.clear();
    refDecalOrder_.clear();
    refDecalDirty_ = false;
    if (!rsDecal_ && dev_) {
        // Decals lie on the surface below: pulled toward the camera a little (research 98: polygon offset).
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        rd.DepthBias = -16;
        rd.SlopeScaledDepthBias = -1.0f;
        dev_->CreateRasterizerState(&rd, &rsDecal_);
    }'''),
 ('''    safeRelease(cbLayer_);''', '''    safeRelease(cbLayer_);
    safeRelease(rsDecal_);'''),
])

# =============================================================== camera: decals and tints
patch('src/qtapp/camera_view.h', [
 ('''    void setMaterialLayers(LayerLookup lookup) { layerLookup_ = std::move(lookup); }''',
  '''    void setMaterialLayers(LayerLookup lookup) { layerLookup_ = std::move(lookup); }
    // Stock material facts the reference needs: a decal's sort key (0 = not a decal) and the colour tint (RRGGBB, -1
    // none; applied when no tint mask limits it). nullopt while the material is not resolved.
    struct StockMaterialFacts {
        int decalSortKey = 0;
        int tint = -1;
        bool tintMasked = false;
        StockLayeredMaterial constants;  // layer 0 at least when the body has a pixel constant buffer
    };
    using FactsLookup = std::function<std::optional<StockMaterialFacts>(const std::string& material)>;
    void setMaterialFacts(FactsLookup lookup) { factsLookup_ = std::move(lookup); }'''),
 ('''    LayerLookup layerLookup_;''', '''    LayerLookup layerLookup_;
    FactsLookup factsLookup_;
    bool resolveReferenceDecal(size_t k, int textureId);  // true when settled'''),
])
patch('src/qtapp/camera_view.cpp', [
 ('''                    if (nid >= 0) renderer_.setReferenceNormal(k, nid);
                    refTexResolved_[k] = true;''', '''                    if (nid >= 0) renderer_.setReferenceNormal(k, nid);
                    if (id >= 0 && id != checkerTex_ && !resolveReferenceDecal(k, id)) {
                        pending = true;
                        continue;
                    }
                    refTexResolved_[k] = true;'''),
 ('''    if (cid < 0 || cid == checkerTex_) return true;  // no colour image: left out rather than drawn as the checker
    const int nid = normalFor(ck);
    if (!nrmIds_.count(ck)) return false;  // waits for the per-frame budget
    if (nid >= 0) renderer_.setReferenceNormal(k, nid);''', '''    if (cid < 0 || cid == checkerTex_) return true;  // no colour image: left out rather than drawn as the checker
    const int nid = normalFor(ck);
    if (!nrmIds_.count(ck)) return false;  // waits for the per-frame budget
    if (nid >= 0) renderer_.setReferenceNormal(k, nid);
    // A decal material's layers draw with the decal, in its sort order (research 98).
    if (factsLookup_)
        if (const auto f = factsLookup_(base); f && f->decalSortKey > 0) {
            const float p[16] = {1.0f, float(lc.flags), lc.sharpness, lc.colorMode == 1 ? 1.0f : 0.0f, lc.tint[0], lc.tint[1], lc.tint[2], 0.0f,
                                 lc.uvCos, lc.uvSin, lc.uvScale[0], lc.uvScale[1], lc.uvOffset[0], lc.uvOffset[1], 0.0f, 0.0f};
            renderer_.setReferenceTexture(k, cid);
            renderer_.setReferenceDecal(k, f->decalSortKey, rid == checkerTex_ ? -1 : rid, p);
            return true;
        }'''),
 ('''void CameraView::setMaterialImages(ImageLookup lookup, ImageRequest request) {''',
  '''bool CameraView::resolveReferenceDecal(size_t k, int textureId) {
    if (!factsLookup_ || k >= refTexMaterials_.size()) return true;
    const std::string& base = refTexMaterials_[k];
    const auto f = factsLookup_(base);
    if (!f) return true;  // the colour resolved, so the material is known: no facts = not a decal
    if (f->decalSortKey <= 0) return true;
    // Layer 0's alpha + reveal map (slot 27) and its blend constants.
    const std::string rk = base + '\\x02' + "0";
    const int rid = textureFor(rk);
    if (!texIds_.count(rk)) return false;
    StockLayerConstants lc;
    if (!f->constants.layer.empty()) lc = f->constants.layer[0];
    const float p[16] = {1.0f, float(lc.flags), lc.sharpness, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f,
                         1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};  // layer 0: its tint is in the image already
    (void)textureId;
    renderer_.setReferenceDecal(k, f->decalSortKey, rid == checkerTex_ ? -1 : rid, p);
    return true;
}

void CameraView::setMaterialImages(ImageLookup lookup, ImageRequest request) {'''),
 ('''        if (const auto tint = MaterialTints::shared(QCoreApplication::applicationDirPath().toStdString()).find(material);
            tint && img.pixels.size() >= size_t(img.width) * size_t(img.height) * 4)
            applyMaterialTint(img.pixels.data(), size_t(img.width) * size_t(img.height), *tint);''',
  '''        if (const auto tint = MaterialTints::shared(QCoreApplication::applicationDirPath().toStdString()).find(material);
            tint && img.pixels.size() >= size_t(img.width) * size_t(img.height) * 4) {
            applyMaterialTint(img.pixels.data(), size_t(img.width) * size_t(img.height), *tint);
        } else if (factsLookup_ && material.find_first_of("\\x01\\x02") == std::string::npos &&
                   img.pixels.size() >= size_t(img.width) * size_t(img.height) * 4) {
            // The material's own tint (constant 9) when no tint mask limits it: painted walls and roofs (owner
            // comparison 2026-11-12: Speedball's REMONT building came out white instead of green wood and a red roof).
            if (const auto f = factsLookup_(material); f && f->tint >= 0 && !f->tintMasked) {
                const std::array<uint8_t, 3> t{uint8_t(f->tint >> 16), uint8_t(f->tint >> 8), uint8_t(f->tint)};
                applyMaterialTint(img.pixels.data(), size_t(img.width) * size_t(img.height), t);
            }
        }'''),
])

# =============================================================== main window: facts; layer 0 reveal of a decal
patch('src/qtapp/main_window.cpp', [
 ('''                const auto imgs = matCache_->layerImages(base);
                const int L = std::atoi(m.c_str() + sep + 1);
                if (L < 0 || size_t(L) >= imgs.size()) return QString();''', '''                const auto imgs = matCache_->layerImages(base);
                const int L = std::atoi(m.c_str() + sep + 1);
                if (L == 0 && m[sep] == '\\x02') return e->maskDds;  // a decal's layer 0 alpha + reveal: slot 27
                if (L < 0 || size_t(L) >= imgs.size()) return QString();'''),
 ('''    camera_->setMaterialLayers([this](const std::string& m) -> std::optional<StockLayeredMaterial> {''',
  '''    camera_->setMaterialFacts([this](const std::string& m) -> std::optional<CameraView::StockMaterialFacts> {
        const auto e = matCache_->lookup(qs(m));
        if (!e || e->failed) return std::nullopt;
        CameraView::StockMaterialFacts f;
        // Decals: the blended world techsets (`_bad_` blend token; research 98), sortKey 10-17 on Speedball.
        const QString ts = e->techset.section('/', -1);
        if ((ts.contains("_bad_") || (e->sortKey >= 10 && e->sortKey <= 17 && (ts.startsWith("lit") || e->techset.startsWith("tw")))) &&
            (e->techset.startsWith("tw/") || e->techset.startsWith("twc/") || e->techset.startsWith("tw4/") || e->techset.startsWith("twc4/")))
            f.decalSortKey = std::max(1, e->sortKey);
        f.tint = e->tint;
        f.tintMasked = e->tintMasked;
        if (auto c = matCache_->materialConstants(qs(m))) f.constants = *c;
        return f;
    });
    camera_->setMaterialLayers([this](const std::string& m) -> std::optional<StockLayeredMaterial> {'''),
])
print('ok')
