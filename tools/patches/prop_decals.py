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


# ------------------------------------------------------------------ renderer: a second vertex buffer of decal batches
patch('src/viewport/render.h', [
 ('''    void setReferenceDecal(size_t batch, int sortKey, int revealTexture, const float params[16]);''',
  '''    void setReferenceDecal(size_t batch, int sortKey, int revealTexture, const float params[16]);
    // Decal batches added after the textured reference (stock decal volumes projected on the props once they are
    // loaded): their own vertex buffer, batch indices from referenceTexturedBatches() on; each is a decal waiting for
    // setReferenceDecal. A second call replaces the previous ones; setReferenceTextured drops them.
    bool appendReferenceDecals(const std::vector<std::vector<GpuTexVertex>>& batches);'''),
 ('''    std::vector<size_t> refDecalOrder_;  // decal batches by sortKey, layer, batch''',
  '''    std::vector<size_t> refDecalOrder_;  // decal batches by sortKey, layer, batch
    ID3D11Buffer* vbRefTexExtra_ = nullptr;  // appendReferenceDecals
    size_t refTexExtraFrom_ = ~size_t(0);    // the first batch in vbRefTexExtra_'''),
])
patch('src/viewport/render.cpp', [
 ('''bool SceneRenderer::setReferenceTextured(const std::vector<std::vector<GpuTexVertex>>& batches, const std::vector<int>* layers) {''',
  '''bool SceneRenderer::appendReferenceDecals(const std::vector<std::vector<GpuTexVertex>>& batches) {
    // Drop the previous extras.
    if (refTexExtraFrom_ != ~size_t(0) && refTexExtraFrom_ <= refTexIds_.size()) {
        const size_t n = refTexExtraFrom_;
        refTexFirst_.resize(n), refTexCount_.resize(n), refTexIds_.resize(n), refTexChunks_.resize(n);
        refLayers_.resize(n), refNrmIds_.resize(n);
        refDecalDirty_ = true;
    }
    safeRelease(vbRefTexExtra_);
    refTexExtraFrom_ = refTexIds_.size();
    if (!dev_ || batches.empty()) return true;
    std::vector<GpuTexVertex> all;
    for (const auto& b : batches) {
        refTexFirst_.push_back(UINT(all.size()));
        refTexCount_.push_back(UINT(b.size()));
        all.insert(all.end(), b.begin(), b.end());
        refTexChunks_.emplace_back();  // small: drawn whole
        refTexIds_.push_back(-1);
        RefLayer L;
        L.decal = true;  // never drawn opaque; ready once setReferenceDecal gives its textures
        refLayers_.push_back(L);
        refNrmIds_.push_back(-1);
    }
    if (all.empty()) return true;
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = UINT(all.size() * sizeof(GpuTexVertex));
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init{all.data(), 0, 0};
    if (FAILED(dev_->CreateBuffer(&bd, &init, &vbRefTexExtra_))) return false;
    refDecalDirty_ = true;
    return true;
}

bool SceneRenderer::setReferenceTextured(const std::vector<std::vector<GpuTexVertex>>& batches, const std::vector<int>* layers) {
    safeRelease(vbRefTexExtra_);
    refTexExtraFrom_ = ~size_t(0);'''),
 # the decal pass: the buffer of each batch
 ('''                if (pi == firstDecal && rsDecal_) ctx_->RSSetState(rsDecal_);
                const RefLayer& L = refLayers_[k];''', '''                if (pi == firstDecal && rsDecal_) ctx_->RSSetState(rsDecal_);
                {
                    ID3D11Buffer* want = k >= refTexExtraFrom_ && vbRefTexExtra_ ? vbRefTexExtra_ : vbRefTex_;
                    if (want != boundRefVb) ctx_->IASetVertexBuffers(0, 1, &want, &tstride, &offset), boundRefVb = want;
                }
                const RefLayer& L = refLayers_[k];'''),
 ('''            // Opaque layers first, then the decals (their layers with them) with a small depth offset.''',
  '''            ID3D11Buffer* boundRefVb = vbRefTex_;
            // Opaque layers first, then the decals (their layers with them) with a small depth offset.'''),
 ('''            ctx_->RSSetState(rsTris_);
            ctx_->OMSetDepthStencilState(texOpaqueDs, 0);
        }''', '''            ctx_->RSSetState(rsTris_);
            ctx_->OMSetDepthStencilState(texOpaqueDs, 0);
            if (boundRefVb != vbRefTex_) ctx_->IASetVertexBuffers(0, 1, &vbRefTex_, &tstride, &offset);
        }'''),
 ('''    safeRelease(rsDecal_);''', '''    safeRelease(rsDecal_);
    safeRelease(vbRefTexExtra_);'''),
])

# ------------------------------------------------------------------ camera: append decal batches
patch('src/qtapp/camera_view.h', [
 ('''    std::vector<RefDecalVolume> refTexVolume_;  // parallel: stock decal volume batches (research 99)''',
  '''    std::vector<RefDecalVolume> refTexVolume_;  // parallel: stock decal volume batches (research 99)
    std::vector<app::ReferenceTexBatch> pendingDecalExtras_;
    bool decalExtrasDirty_ = false;
    size_t refTexExtraFrom_ = ~size_t(0);'''),
 ('''    void setMaterialLayers(LayerLookup lookup) { layerLookup_ = std::move(lookup); }''',
  '''    void setMaterialLayers(LayerLookup lookup) { layerLookup_ = std::move(lookup); }
    // Stock decal volumes projected on the props (decalVolume batches): added beside the textured reference, replacing
    // an earlier set.
    void setReferenceDecalExtras(std::vector<app::ReferenceTexBatch> batches) {
        pendingDecalExtras_ = std::move(batches);
        decalExtrasDirty_ = true;
        update();
    }'''),
])
patch('src/qtapp/camera_view.cpp', [
 ('''        renderer_.setReferenceTextured(batches, &refTexLayer_);''', '''        renderer_.setReferenceTextured(batches, &refTexLayer_);
        refTexExtraFrom_ = ~size_t(0);
        decalExtrasDirty_ = decalExtrasDirty_ || !pendingDecalExtras_.empty();'''),
 ('''    // Per-frame texture budget, reset before the reference loop so it gets loads after a busy frame too.''',
  '''    if (decalExtrasDirty_ && !refTexDirty_) {
        // Decal volumes on the props: after the world's batches (dropping an earlier set).
        decalExtrasDirty_ = false;
        if (refTexExtraFrom_ != ~size_t(0) && refTexExtraFrom_ <= refTexMaterials_.size()) {
            refTexMaterials_.resize(refTexExtraFrom_), refTexLayer_.resize(refTexExtraFrom_);
            refTexVolume_.resize(refTexExtraFrom_), refTexResolved_.resize(refTexExtraFrom_);
        }
        refTexExtraFrom_ = refTexMaterials_.size();
        std::vector<std::vector<app::GpuTexVertex>> vb;
        for (const auto& b : pendingDecalExtras_) {
            refTexMaterials_.push_back(b.material);
            refTexLayer_.push_back(0);
            RefDecalVolume dv;
            dv.on = true, dv.mask = b.decalMask, dv.a = b.decalA, dv.b = b.decalB, dv.order = b.decalOrder;
            for (int c = 0; c < 3; ++c) dv.tint[c] = b.decalTint[c];
            refTexVolume_.push_back(dv);
            refTexResolved_.push_back(false);
            vb.push_back(b.tris);
        }
        renderer_.appendReferenceDecals(vb);
        refTexPending_ = refPollDue_ = true;
    }
    // Per-frame texture budget, reset before the reference loop so it gets loads after a busy frame too.'''),
])

# ------------------------------------------------------------------ main window: keep the decal set; project on props
patch('src/qtapp/main_window.h', [
 ('''    std::map<std::string, std::shared_ptr<const RefPropMesh>> refPropBuilt_;''',
  '''    std::map<std::string, std::shared_ptr<const RefPropMesh>> refPropBuilt_;
    // The stock map's decal volumes with their materials and masks (appendStockDecalVolumes), kept to project them on
    // the props once those are built (projectPropDecals; done once per map).
    std::shared_ptr<const struct StockDecalData> stockDecals_;
    QString propDecalsZone_;
    void projectPropDecals();'''),
])
patch('src/qtapp/main_window.cpp', [
 # the struct and the helper's new output parameter
 ('''struct StockDecalTools {
    QString acts, replay, oodle;
};
void appendStockDecalVolumes(const StockWorld& world, const QString& zone, const QString& zoneDir, const StockDecalTools& tools,
                             std::vector<app::ReferenceTexBatch>& out);''', '''struct StockDecalTools {
    QString acts, replay, oodle;
};
struct StockDecalData {
    StockDecalVolumeSet set;
    std::map<std::string, StockDecalMaterial> materials;
    std::map<std::string, std::string> maskImage;  // mask asset -> its blend map image
};
std::shared_ptr<StockDecalData> appendStockDecalVolumes(const StockWorld& world, const QString& zone, const QString& zoneDir,
                                                        const StockDecalTools& tools, std::vector<app::ReferenceTexBatch>& out);
// The reference batch of one projected decal volume (null material: none).
bool stockDecalBatch(const StockDecalData& d, const StockDecalBatch& b, int order, app::ReferenceTexBatch& rb);'''),
 ('''void appendStockDecalVolumes(const StockWorld& world, const QString& zone, const QString& zoneDir, const StockDecalTools& tools,
                             std::vector<app::ReferenceTexBatch>& out) {
    StockDecalVolumeSet set;
    std::string err;
    if (!readStockDecalVolumes(zoneDir.toStdString(), set, &err) || set.volumes.empty()) return;''',
  '''std::shared_ptr<StockDecalData> appendStockDecalVolumes(const StockWorld& world, const QString& zone, const QString& zoneDir,
                                                        const StockDecalTools& tools, std::vector<app::ReferenceTexBatch>& out) {
    auto data = std::make_shared<StockDecalData>();
    StockDecalVolumeSet& set = data->set;
    std::string err;
    if (!readStockDecalVolumes(zoneDir.toStdString(), set, &err) || set.volumes.empty()) return nullptr;'''),
 ('''    std::map<std::string, StockDecalMaterial> mats;
    for (const auto& [f, t] : matTexts) {''', '''    std::map<std::string, StockDecalMaterial>& mats = data->materials;
    for (const auto& [f, t] : matTexts) {'''),
 ('''    std::map<std::string, std::string> maskImage;
    for (const auto& [f, t] : maskTexts) {''', '''    std::map<std::string, std::string>& maskImage = data->maskImage;
    for (const auto& [f, t] : maskTexts) {'''),
 ('''    int order = 0;
    for (const StockDecalBatch& b : batches) {
        if (b.volume >= set.volumes.size() || b.idx.empty()) continue;
        const StockDecalVolume& vol = set.volumes[b.volume];
        const auto mi = mats.find(vol.material);
        if (mi == mats.end() || mi->second.channels[0].empty()) continue;
        const StockDecalShading sh = stockDecalShading(vol, mi->second);
        app::ReferenceTexBatch rb;''', '''    int order = 0;
    for (const StockDecalBatch& b : batches) {
        app::ReferenceTexBatch rb;
        if (stockDecalBatch(*data, b, order, rb)) out.push_back(std::move(rb)), ++order;
    }
    return data;
}

bool stockDecalBatch(const StockDecalData& d, const StockDecalBatch& b, int order, app::ReferenceTexBatch& rb) {
    {
        if (b.volume >= d.set.volumes.size() || b.idx.empty()) return false;
        const StockDecalVolume& vol = d.set.volumes[b.volume];
        const auto mi = d.materials.find(vol.material);
        if (mi == d.materials.end() || mi->second.channels[0].empty()) return false;
        const auto& maskImage = d.maskImage;
        const StockDecalShading sh = stockDecalShading(vol, mi->second);'''),
 ('''        rb.decalOrder = order++;
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
}''', '''        rb.decalOrder = order;
        for (uint32_t i : b.idx) {
            const StockDecalVertex& v = b.verts[i];
            app::GpuTexVertex g{float(v.pos.x), float(v.pos.y), float(v.pos.z), v.mu, v.mv, 0xFFFFFFFFu};
            g.nx = float(v.normal.x), g.ny = float(v.normal.y), g.nz = float(v.normal.z);
            g.tx = v.u, g.ty = v.v, g.tz = v.fade;
            if (v.lightmap >= 0 && v.lmU >= 0) g.lu = float(v.lightmap) + std::min(v.lmU, 0.99994f), g.lv = v.lmV;
            rb.tris.push_back(g);
        }
    }
    return true;
}

// Decal volumes on the stock props (stencils on crates, paint on containers; research 99: static models receive
// them): the converted prop meshes placed in world space, projected once the props are built.
void MainWindow::projectPropDecals() {
    if (!stockDecals_ || !refProps_ || referenceZone_.isEmpty() || propDecalsZone_ == referenceZone_ || refPropBuilt_.empty()) return;
    propDecalsZone_ = referenceZone_;
    const StockDecalData& d = *stockDecals_;
    // Only props near a volume become receivers.
    std::vector<AABB> boxes;
    for (const auto& v : d.set.volumes) boxes.push_back(stockDecalVolumeBounds(v));
    std::vector<ImportedMesh> meshes;  // world space; must outlive the receivers
    for (const StockModelInstance& in : *refProps_) {
        const auto it = refPropBuilt_.find(in.model);
        if (it == refPropBuilt_.end() || !it->second) continue;
        const auto place = [&](const app::GpuTexVertex& v) {
            const Vec3 p{v.x, v.y, v.z};
            return in.origin + (in.axis[0] * p.x + in.axis[1] * p.y + in.axis[2] * p.z) * in.scale;
        };
        AABB pb;
        for (const auto& [mat, tris] : it->second->tex)
            for (const auto& v : tris) pb.add(place(v));
        if (!pb.valid()) continue;
        bool near = false;
        for (const AABB& b : boxes)
            if (!(b.mins.x > pb.maxs.x || b.maxs.x < pb.mins.x || b.mins.y > pb.maxs.y || b.maxs.y < pb.mins.y ||
                  b.mins.z > pb.maxs.z || b.maxs.z < pb.mins.z)) {
                near = true;
                break;
            }
        if (!near) continue;
        ImportedMesh m;
        for (const auto& [mat, tris] : it->second->tex)
            for (size_t i = 0; i + 2 < tris.size(); i += 3) {
                const Vec3 a = place(tris[i]), b = place(tris[i + 1]), c = place(tris[i + 2]);
                Vec3 n = cross(b - a, c - a);
                if (length(n) < 1e-9) continue;
                n = normalize(n);
                for (const Vec3& p : {a, b, c}) {
                    m.idx.push_back(uint32_t(m.pos.size()));
                    m.pos.push_back(p);
                    m.normal.push_back(n);
                    m.uv.push_back({0, 0});
                }
            }
        if (!m.idx.empty()) meshes.push_back(std::move(m));
    }
    if (meshes.empty()) return;
    StockDecalReceivers receivers;
    for (const ImportedMesh& m : meshes) receivers.addMesh(m);
    receivers.build();
    std::vector<app::ReferenceTexBatch> out;
    int order = 100000;  // over the world's decals of the same order
    for (const StockDecalBatch& b : projectDecalVolumes(receivers, d.set)) {
        app::ReferenceTexBatch rb;
        if (stockDecalBatch(d, b, order, rb)) out.push_back(std::move(rb)), ++order;
    }
    if (!out.empty()) camera_->setReferenceDecalExtras(std::move(out));
}'''),
 # keep the data from the loader
 ('''        if (world) appendStockDecalVolumes(*world, zone, zoneDir, tools, *texBatches);  // projected decals (research 99)''',
  '''        std::shared_ptr<const StockDecalData> decals;
        if (world) decals = appendStockDecalVolumes(*world, zone, zoneDir, tools, *texBatches);  // projected decals (research 99)'''),
])
# hand the data to the UI thread (lambda captures) and project when the props are done
s = io.open('src/qtapp/main_window.cpp', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '        QMetaObject::invokeMethod(qApp, [self, zone, tris, n, err, props, plans, world, texBatches] {'
assert s.count(a) == 1
s = s.replace(a, '        QMetaObject::invokeMethod(qApp, [self, zone, tris, n, err, props, plans, world, texBatches, decals] {', 1)
a = '            self->stockWorld_ = world;'
assert s.count(a) == 1
s = s.replace(a, a + nl + '            self->stockDecals_ = decals;' + nl + '            self->propDecalsZone_.clear();', 1)
a = '''    } else if (step == PropFetchStep::Done || step == PropFetchStep::Stalled) {
        if (refPropClock_.isValid() && refPropFetchMs_ < 0) refPropFetchMs_ = refPropClock_.elapsed();'''.replace('\n', nl)
assert s.count(a) == 1
s = s.replace(a, a + nl + '        projectPropDecals();  // the decal volumes on the props, before their kept meshes are released', 1)
io.open('src/qtapp/main_window.cpp', 'w', encoding='utf-8', newline='').write(s)
print('ok')
