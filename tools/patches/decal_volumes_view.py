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


# ------------------------------------------------------------------ scene: decal volume batch fields
patch('src/viewport/scene.h', [
 ('''    int layer = 0;
};''', '''    int layer = 0;
    // A stock decal volume's projection (research 99): material = "\\x05<colour image>"; the vertex uv is the material
    // UV, tangent (x, y, z) = box UV and angle fade, lu / lv the receiver's lightmap UV. Drawn blended after the world.
    bool decalVolume = false;
    std::string decalMask;  // the mask image ("<mask>~atlasbc4"), "" when none
    float decalA = 1, decalB = 0;  // the blend-map remap: op = m * saturate(m * A + B)
    float decalTint[3] = {1, 1, 1};
    int decalOrder = 0;'''),
])

# ------------------------------------------------------------------ renderer: the volume shading mode
patch('src/viewport/render.cpp', [
 ('''    float4 t = tex0.Sample(samp0, uvS);
    if (layerP.x > 0.5) {
        float2 ar = revealTex.Sample(samp0, uvS).rg;''', '''    float4 t = tex0.Sample(samp0, uvS);
    if (layerP.x > 1.5) {
        // A stock decal volume (research 99): the mask at the box UV (clamped), the edge fade, the blend-map remap
        // op = m x saturate(m A + B), times the angle fade; the colour image tinted (layerTint).
        float2 bu = saturate(i.t.xy);
        float m = revealTex.SampleLevel(lutSamp, bu, 0).r;
        m *= 1.0 - saturate(256.0 * (max(abs(2.0 * bu.x - 1.0), abs(2.0 * bu.y - 1.0)) - (1.0 - 0.0078125)));
        layerB = m * saturate(m * layerP.y + layerP.z) * saturate(i.t.z);
        if (layerB < 1.0 / 255.0) discard;
        t.rgb *= pow(max(layerTint.rgb, 0.0), 1.0 / 2.2);
    } else if (layerP.x > 0.5) {
        float2 ar = revealTex.Sample(samp0, uvS).rg;'''),
 # the decal pass: volume batches without normal maps (their tangent slot carries the box UV)
 ('''                const RefLayer& L = refLayers_[k];
                if (!L.ready || refTexIds_[k] < 0 || size_t(refTexIds_[k]) >= textures_.size()) continue;
                const int rv = L.reveal >= 0 && size_t(L.reveal) < textures_.size() ? L.reveal : whiteFullTex_;''',
  '''                const RefLayer& L = refLayers_[k];
                if (!L.ready || refTexIds_[k] < 0 || size_t(refTexIds_[k]) >= textures_.size()) continue;
                if (cbBatch_) {
                    const bool volume = L.params[0] > 1.5f;  // decal volumes carry their box UV in the tangent: no normal map
                    const float flags[8] = {refNormals && !volume ? 1.0f : 0.0f, 1.0f, 0, 0, 0, 0, 1.0f, stockLmSrv_[0] ? 1.0f : 0.0f};
                    ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);
                }
                const int rv = L.reveal >= 0 && size_t(L.reveal) < textures_.size() ? L.reveal : whiteFullTex_;'''),
])

# ------------------------------------------------------------------ camera: resolve volume textures
patch('src/qtapp/camera_view.h', [
 ('''    std::vector<int> refTexLayer_;  // parallel: 0 base, L >= 1 a layer batch of refTexMaterials_[k]''',
  '''    std::vector<int> refTexLayer_;  // parallel: 0 base, L >= 1 a layer batch of refTexMaterials_[k]
    struct RefDecalVolume {
        bool on = false;
        std::string mask;
        float a = 1, b = 0, tint[3] = {1, 1, 1};
        int order = 0;
    };
    std::vector<RefDecalVolume> refTexVolume_;  // parallel: stock decal volume batches (research 99)'''),
])
patch('src/qtapp/camera_view.cpp', [
 ('''        refTexLayer_.clear();
        for (auto& b : pendingRefTex_) {
            refTexMaterials_.push_back(b.material);
            refTexLayer_.push_back(b.layer);''', '''        refTexLayer_.clear();
        refTexVolume_.clear();
        for (auto& b : pendingRefTex_) {
            refTexMaterials_.push_back(b.material);
            refTexLayer_.push_back(b.layer);
            RefDecalVolume dv;
            dv.on = b.decalVolume, dv.mask = b.decalMask, dv.a = b.decalA, dv.b = b.decalB, dv.order = b.decalOrder;
            for (int c = 0; c < 3; ++c) dv.tint[c] = b.decalTint[c];
            refTexVolume_.push_back(dv);'''),
 ('''                if (k < refTexLayer_.size() && refTexLayer_[k] > 0) {
                    if (resolveReferenceLayer(k)) refTexResolved_[k] = true;
                    else pending = true;
                    continue;
                }''', '''                if (k < refTexLayer_.size() && refTexLayer_[k] > 0) {
                    if (resolveReferenceLayer(k)) refTexResolved_[k] = true;
                    else pending = true;
                    continue;
                }
                if (k < refTexVolume_.size() && refTexVolume_[k].on) {
                    // A decal volume: its colour and mask images, then drawn with the decals (no normal map).
                    const RefDecalVolume& dv = refTexVolume_[k];
                    const int cid = textureFor(refTexMaterials_[k]);
                    const std::string mk = dv.mask.empty() ? std::string() : "\\x05" + dv.mask;
                    const int mid = mk.empty() ? -1 : textureFor(mk);
                    if (!texIds_.count(refTexMaterials_[k]) || (!mk.empty() && !texIds_.count(mk))) {
                        pending = true;
                        continue;
                    }
                    refTexResolved_[k] = true;
                    if (cid < 0 || cid == checkerTex_) continue;  // no colour image: left out
                    const float p[16] = {2.0f, dv.a, dv.b, 0.0f, dv.tint[0], dv.tint[1], dv.tint[2], 0.0f,
                                         1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
                    renderer_.setReferenceTexture(k, cid);
                    renderer_.setReferenceDecal(k, 1000 + dv.order, mid == checkerTex_ ? -1 : mid, p);
                    continue;
                }'''),
])

# ------------------------------------------------------------------ main window: read, export, project
patch('src/qtapp/main_window.cpp', [
 ('''        if (world) app::buildReferenceTexBatches(*world, *texBatches);  // textured camera reference (3.19)''',
  '''        if (world) app::buildReferenceTexBatches(*world, *texBatches);  // textured camera reference (3.19)
        if (world) appendStockDecalVolumes(*world, zone, zoneDir, tools, *texBatches);  // projected decals (research 99)'''),
 ('''    std::thread([self, cache, zone, zoneDir] {
        QString err;
        std::optional<StockWorld> loaded = cache->load(zone, &err);''', '''    const StockDecalTools tools{toolPath("acts"), toolPath("replay"), toolPath("oodle")};
    std::thread([self, cache, zone, zoneDir, tools] {
        QString err;
        std::optional<StockWorld> loaded = cache->load(zone, &err);'''),
 # the image lookup: decal volume images through the Images browser's exporter
 ('''            // A layer image of a layered stock material: "<material>\\x01<L>" colour, "<material>\\x02<L>" alpha + reveal.''',
  '''            // A stock decal volume image ("\\x05<image>": colour or mask, research 99): any catalogued image.
            if (!m.empty() && m[0] == '\\x05') {
                const QString image = qs(m.substr(1));
                const QString f = imageCache_->cachedDds(image);
                if (!f.isEmpty()) return f;
                if (!imageCache_->lastError(image).isEmpty()) return QString();  // could not be exported
                imageCache_->request(image);
                return std::nullopt;
            }
            // A layer image of a layered stock material: "<material>\\x01<L>" colour, "<material>\\x02<L>" alpha + reveal.'''),
 ('''        [this](const std::string& m) { matCache_->request({qs(m.substr(0, m.find_first_of("\\x01\\x02")))}); });''',
  '''        [this](const std::string& m) {
            if (!m.empty() && m[0] == '\\x05') return;  // decal volume images: requested by the lookup
            matCache_->request({qs(m.substr(0, m.find_first_of("\\x01\\x02")))});
        });'''),
])

# the helper, before MainWindow::loadStockSun
patch('src/qtapp/main_window.cpp', [
 ('''void MainWindow::loadStockSun(const QString& zone) {''', '''// A stock map's decal volumes (research 99): read from its world export, their materials and masks exported once with
// ACTS (beside the world cache), projected on the world and appended as blended reference batches.
void appendStockDecalVolumes(const StockWorld& world, const QString& zone, const QString& zoneDir, const StockDecalTools& tools,
                             std::vector<app::ReferenceTexBatch>& out) {
    StockDecalVolumeSet set;
    std::string err;
    if (!readStockDecalVolumes(zoneDir.toStdString(), set, &err) || set.volumes.empty()) return;
    // Materials and masks (pools decalvolumematerial / decalvolumemask), cached in <zoneDir>/decalvol.
    const QString dvDir = zoneDir + "/decalvol";
    const auto readAssets = [&](const char* pool, std::map<std::string, std::string>& texts) {
        for (QDirIterator it(dvDir, {"*.json"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
            const QString f = it.next();
            if (!f.contains(QString("/assets/") + pool + "/")) continue;
            std::string t;
            if (readFile(f.toStdString(), t)) texts[f.toStdString()] = t;
        }
    };
    std::map<std::string, std::string> matTexts, maskTexts;
    readAssets("decalvolumematerial", matTexts);
    readAssets("decalvolumemask", maskTexts);
    if (matTexts.empty() && QFileInfo::exists(tools.acts)) {
        QStringList names;
        for (const auto& m : set.materials)
            if (!m.empty()) names << QString::fromStdString(m);
        for (const auto& m : set.masks)
            if (!m.empty()) names << QString::fromStdString(m);
        const QString zoneFolder = QFileInfo(tools.replay).absolutePath() + "/zone/";
        for (const QString& ff : {zoneFolder + "techsets_" + zone + ".ff", zoneFolder + zone + ".ff"}) {
            if (!QFileInfo::exists(ff)) continue;
            QProcess p;
            p.start(tools.acts, {"--noUpdater", "fastfile", "-r", "mw19replay", "-g", tools.replay, "--oodle", tools.oodle, "-a",
                                 "decalvolumematerial,decalvolumemask", "-n", names.join(','), "-l", "-o", dvDir, ff});
            p.waitForFinished(180000);
        }
        readAssets("decalvolumematerial", matTexts);
        readAssets("decalvolumemask", maskTexts);
    }
    std::map<std::string, StockDecalMaterial> mats;
    for (const auto& [f, t] : matTexts) {
        StockDecalMaterial m;
        if (parseStockDecalMaterial(t, m)) mats[m.name] = m;
    }
    std::map<std::string, std::string> maskImage;
    for (const auto& [f, t] : maskTexts) {
        std::string name, image;
        if (parseStockDecalMask(t, name, image)) maskImage[name] = image;
    }
    // Receivers: the world without its sky and shadow-only caulk.
    std::vector<bool> keep(world.meshes.size(), true);
    for (size_t m = 0; m < world.meshes.size(); ++m) {
        std::string lm = world.meshes[m].material;
        for (char& c : lm) c = char(std::tolower(static_cast<unsigned char>(c)));
        const std::string leaf = lm.substr(lm.rfind('/') == std::string::npos ? 0 : lm.rfind('/') + 1);
        if (leaf == "sky" || leaf.rfind("sky_", 0) == 0 || lm.find("caulk") != std::string::npos) keep[m] = false;
    }
    StockDecalReceivers receivers;
    receivers.addWorld(world, keep);
    receivers.build();
    const std::vector<StockDecalBatch> batches = projectDecalVolumes(receivers, set);
    int order = 0;
    for (const StockDecalBatch& b : batches) {
        if (b.volume >= set.volumes.size() || b.idx.empty()) continue;
        const StockDecalVolume& vol = set.volumes[b.volume];
        const auto mi = mats.find(vol.material);
        if (mi == mats.end() || mi->second.channels[0].empty()) continue;
        const StockDecalShading sh = stockDecalShading(vol, mi->second);
        app::ReferenceTexBatch rb;
        rb.material = "\\x05" + mi->second.channels[0];
        rb.decalVolume = true;
        if (const auto mk = maskImage.find(vol.mask); mk != maskImage.end()) rb.decalMask = mk->second;
        rb.decalA = float(sh.opacityScale), rb.decalB = float(sh.opacityBias);
        rb.decalTint[0] = float(sh.tint.x), rb.decalTint[1] = float(sh.tint.y), rb.decalTint[2] = float(sh.tint.z);
        rb.decalOrder = order++;
        for (uint32_t i : b.idx) {
            const StockDecalVertex& v = b.verts[i];
            app::GpuTexVertex g{float(v.pos.x), float(v.pos.y), float(v.pos.z), v.mu, v.mv, 0xFFFFFFFFu};
            g.nx = float(v.normal.x), g.ny = float(v.normal.y), g.nz = float(v.normal.z);
            g.tx = v.u, g.ty = v.v, g.tz = v.fade;
            if (v.lightmap >= 0 && v.lmU >= 0) g.lu = float(v.lightmap) + std::min(v.lmU, 0.99994f), g.lv = v.lmV;
            rb.tris.push_back(g);
        }
        out.push_back(std::move(rb));
    }
}

void MainWindow::loadStockSun(const QString& zone) {'''),
])
s = io.open('src/qtapp/main_window.cpp', encoding='utf-8', newline='').read()
if '#include "stock_decal_volumes.h"' not in s:
    i = s.index('#include "')
    s = s[:i] + '#include "stock_decal_volumes.h"\n' + s[i:]
io.open('src/qtapp/main_window.cpp', 'w', encoding='utf-8', newline='').write(s)
# the tools struct + declaration near the top of main_window.cpp (file scope, before use)
s = io.open('src/qtapp/main_window.cpp', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
anchor = 'namespace iw8r::qt {'
assert s.count(anchor) >= 1
decl = (anchor + nl + nl + '// Tool paths a worker thread needs to export stock decal volume assets (appendStockDecalVolumes).' + nl +
        'struct StockDecalTools {' + nl + '    QString acts, replay, oodle;' + nl + '};' + nl +
        'void appendStockDecalVolumes(const StockWorld& world, const QString& zone, const QString& zoneDir, const StockDecalTools& tools,' + nl +
        '                             std::vector<app::ReferenceTexBatch>& out);' + nl)
s = s.replace(anchor, decl, 1)
io.open('src/qtapp/main_window.cpp', 'w', encoding='utf-8', newline='').write(s)
print('ok')
