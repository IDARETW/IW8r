import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b and s.count(b) >= 1:
        return
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/viewport/render.h'
R = 'src/viewport/render.cpp'

# ------------------------------------------------------------------ header
patch(H, '''    bool appendReferenceDecals(const std::vector<std::vector<GpuTexVertex>>& batches);''', '''    bool appendReferenceDecals(const std::vector<std::vector<GpuTexVertex>>& batches);
    // The extras as keyed groups (terrain layer patch, prop decals, each streamed world tile), each in its own vertex
    // buffer: a group given again with the same key and version (batches null) keeps its buffer and every batch's
    // textures and constants; only new or changed groups are uploaded. World groups hold ordinary world batches (base
    // and layer batches: drawn, cast sun shadows); the others are decal-like (setReferenceDecal makes them ready).
    struct ExtraGroupInput {
        uint64_t key = 0, version = 0;
        const std::vector<std::vector<GpuTexVertex>>* batches = nullptr;  // null: reuse this key's group
        std::vector<int> layers;  // world groups: per batch, 0 base / L >= 1 a layer batch
        bool world = false;
        bool nearOnly = false;    // drawn only in view and within 4,000 units (prop decals)
    };
    bool setReferenceExtras(const std::vector<ExtraGroupInput>& groups);''')
patch(H, '''    ID3D11Buffer* vbRefTexExtra_ = nullptr;  // appendReferenceDecals''', '''    ID3D11Buffer* vbRefTexExtra_ = nullptr;  // unused (the extras are groups now: extraGroups_)
    struct ExtraGroupGpu {
        uint64_t key = 0, version = 0;
        ID3D11Buffer* vb = nullptr;
        std::vector<uint32_t> first, count;
        std::vector<iw8r::AABB> bounds;
        std::vector<int> layer;
        bool world = false, nearOnly = false;
    };
    std::vector<ExtraGroupGpu> extraGroups_;
    std::vector<ID3D11Buffer*> refTexVb_;     // per batch: its vertex buffer (null: vbRefTex_, the main reference)
    std::vector<uint8_t> refTexNearOnly_;     // per batch: drawn only within 4,000 units of the camera
    void releaseExtraGroups();
    ID3D11Buffer* refVbFor(size_t k) const { return k < refTexVb_.size() && refTexVb_[k] ? refTexVb_[k] : vbRefTex_; }
    // An extra batch whose bounds are outside the frustum (main batches use their chunks instead).
    bool refExtraOutside(size_t k, const iw8r::Frustum& f) const {
        if (refTexExtraFrom_ == ~size_t(0) || k < refTexExtraFrom_) return false;
        const size_t e = k - refTexExtraFrom_;
        if (e >= refTexExtraBounds_.size() || !refTexExtraBounds_[e].valid()) return false;
        const iw8r::AABB& bb = refTexExtraBounds_[e];
        const iw8r::Vec3 c = bb.center();
        const double r = iw8r::length(bb.maxs - bb.mins) * 0.5;
        for (const auto& pl : f.planes)
            if (pl.n.x * c.x + pl.n.y * c.y + pl.n.z * c.z - pl.d > r) return true;
        return false;
    }''')

# ------------------------------------------------------------------ releasing
patch(R, '''    safeRelease(rsDecal_);
    safeRelease(vbRefTexExtra_);''', '''    safeRelease(rsDecal_);
    safeRelease(vbRefTexExtra_);
    releaseExtraGroups();''')
patch(R, '''bool SceneRenderer::setReferenceTextured(const std::vector<std::vector<GpuTexVertex>>& batches, const std::vector<int>* layers) {
    safeRelease(vbRefTexExtra_);
    refTexExtraFrom_ = ~size_t(0);
    refTexExtraBounds_.clear();
    refLayers_.assign(batches.size(), RefLayer{});
    refNrmIds_.assign(batches.size(), -1);''', '''void SceneRenderer::releaseExtraGroups() {
    for (auto& g : extraGroups_) safeRelease(g.vb);
    extraGroups_.clear();
}

bool SceneRenderer::setReferenceExtras(const std::vector<ExtraGroupInput>& groups) {
    const size_t mainN = refTexExtraFrom_ != ~size_t(0) && refTexExtraFrom_ <= refTexIds_.size() ? refTexExtraFrom_ : refTexIds_.size();
    // The extras' per-batch state now, by group key (kept for the groups given again).
    struct Slice {
        size_t first = 0;
        size_t index = 0;
    };
    std::map<uint64_t, Slice> old;
    {
        size_t at = mainN;
        for (size_t i = 0; i < extraGroups_.size(); ++i) old[extraGroups_[i].key] = {at, i}, at += extraGroups_[i].first.size();
    }
    const std::vector<int> oTex(refTexIds_.begin() + std::ptrdiff_t(std::min(mainN, refTexIds_.size())), refTexIds_.end());
    const std::vector<int> oNrm(refNrmIds_.begin() + std::ptrdiff_t(std::min(mainN, refNrmIds_.size())), refNrmIds_.end());
    const std::vector<RefLayer> oLay(refLayers_.begin() + std::ptrdiff_t(std::min(mainN, refLayers_.size())), refLayers_.end());
    std::vector<ExtraGroupGpu> next;
    std::vector<int> reusedFrom;  // per next group: the old group's first extra index, -1 = new
    std::vector<uint8_t> taken(extraGroups_.size(), 0);
    bool worldChanged = false;
    for (const ExtraGroupInput& in : groups) {
        const auto it = old.find(in.key);
        if (it != old.end() && !taken[it->second.index] && extraGroups_[it->second.index].version == in.version &&
            (!in.batches || in.batches->size() == extraGroups_[it->second.index].first.size())) {
            taken[it->second.index] = 1;
            next.push_back(extraGroups_[it->second.index]);
            extraGroups_[it->second.index].vb = nullptr;  // moved
            reusedFrom.push_back(int(it->second.first - mainN));
            continue;
        }
        if (!in.batches) continue;  // asked to reuse a group that is not there
        ExtraGroupGpu g;
        g.key = in.key, g.version = in.version, g.world = in.world, g.nearOnly = in.nearOnly;
        std::vector<GpuTexVertex> all;
        for (size_t j = 0; j < in.batches->size(); ++j) {
            const auto& b = (*in.batches)[j];
            g.first.push_back(UINT(all.size()));
            g.count.push_back(UINT(b.size()));
            all.insert(all.end(), b.begin(), b.end());
            iw8r::AABB bb;
            for (const GpuTexVertex& v : b) bb.add(iw8r::Vec3{v.x, v.y, v.z});
            g.bounds.push_back(bb);
            g.layer.push_back(in.world && j < in.layers.size() ? in.layers[j] : 0);
        }
        if (!all.empty() && dev_) {
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = UINT(all.size() * sizeof(GpuTexVertex));
            bd.Usage = D3D11_USAGE_IMMUTABLE;
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA init{all.data(), 0, 0};
            if (FAILED(dev_->CreateBuffer(&bd, &init, &g.vb))) g.vb = nullptr;
        }
        worldChanged |= g.world;
        next.push_back(std::move(g));
        reusedFrom.push_back(-1);
    }
    for (size_t i = 0; i < extraGroups_.size(); ++i)
        if (!taken[i]) worldChanged |= extraGroups_[i].world, safeRelease(extraGroups_[i].vb);
    extraGroups_ = std::move(next);
    // The per-batch arrays: the main reference, then every group in order.
    refTexFirst_.resize(mainN), refTexCount_.resize(mainN), refTexIds_.resize(mainN), refTexChunks_.resize(mainN);
    refLayers_.resize(mainN), refNrmIds_.resize(mainN), refTexVb_.resize(mainN, nullptr), refTexNearOnly_.resize(mainN, 0);
    refTexExtraFrom_ = mainN;
    refTexExtraBounds_.clear();
    for (size_t gi = 0; gi < extraGroups_.size(); ++gi) {
        const ExtraGroupGpu& g = extraGroups_[gi];
        for (size_t j = 0; j < g.first.size(); ++j) {
            refTexFirst_.push_back(g.first[j]);
            refTexCount_.push_back(g.count[j]);
            refTexChunks_.emplace_back();  // drawn whole (frustum test on the bounds)
            refTexExtraBounds_.push_back(g.bounds[j]);
            refTexVb_.push_back(g.vb);
            refTexNearOnly_.push_back(g.nearOnly ? 1 : 0);
            if (reusedFrom[gi] >= 0) {
                const size_t o = size_t(reusedFrom[gi]) + j;
                refTexIds_.push_back(o < oTex.size() ? oTex[o] : -1);
                refNrmIds_.push_back(o < oNrm.size() ? oNrm[o] : -1);
                refLayers_.push_back(o < oLay.size() ? oLay[o] : RefLayer{});
            } else {
                refTexIds_.push_back(-1);
                refNrmIds_.push_back(-1);
                RefLayer L;
                L.layer = g.layer[j];
                L.decal = !g.world;  // decal-like extras are never drawn opaque; ready once setReferenceDecal gives textures
                refLayers_.push_back(L);
            }
        }
    }
    // Layer order: the main reference's layer batches, then the world groups' (by layer).
    refLayerOrder_.erase(std::remove_if(refLayerOrder_.begin(), refLayerOrder_.end(), [&](size_t k) { return k >= mainN; }), refLayerOrder_.end());
    for (size_t k = mainN; k < refLayers_.size(); ++k)
        if (refLayers_[k].layer > 0 && !refLayers_[k].decal) refLayerOrder_.push_back(k);
    std::stable_sort(refLayerOrder_.begin(), refLayerOrder_.end(), [&](size_t a, size_t b) { return refLayers_[a].layer < refLayers_[b].layer; });
    refDecalDirty_ = true;
    layerParamsDirty_ = true;
    if (worldChanged) shadowDirty_ = nearDirty_ = true;
    return true;
}

bool SceneRenderer::setReferenceTextured(const std::vector<std::vector<GpuTexVertex>>& batches, const std::vector<int>* layers) {
    safeRelease(vbRefTexExtra_);
    releaseExtraGroups();
    refTexExtraFrom_ = ~size_t(0);
    refTexExtraBounds_.clear();
    refLayers_.assign(batches.size(), RefLayer{});
    refNrmIds_.assign(batches.size(), -1);
    refTexVb_.assign(batches.size(), nullptr);
    refTexNearOnly_.assign(batches.size(), 0);''')

# the old single-buffer entry point becomes one decal-like group
patch(R, '''bool SceneRenderer::appendReferenceDecals(const std::vector<std::vector<GpuTexVertex>>& batches) {
    // Drop the previous extras.''', '''bool SceneRenderer::appendReferenceDecals(const std::vector<std::vector<GpuTexVertex>>& batches) {
    static uint64_t version = 0;
    ExtraGroupInput g;
    g.key = 1, g.version = ++version, g.batches = &batches, g.nearOnly = true;
    return setReferenceExtras({g});
}

bool appendReferenceDecalsOld_unused(const std::vector<std::vector<GpuTexVertex>>& batches);
bool SceneRenderer_appendReferenceDecalsOld_unused() { return false; }
#if 0
bool SceneRenderer::appendReferenceDecals(const std::vector<std::vector<GpuTexVertex>>& batches) {
    // Drop the previous extras.''')
patch(R, '''    if (FAILED(dev_->CreateBuffer(&bd, &init, &vbRefTexExtra_))) return false;
    refDecalDirty_ = true;
    return true;
}''', '''    if (FAILED(dev_->CreateBuffer(&bd, &init, &vbRefTexExtra_))) return false;
    refDecalDirty_ = true;
    return true;
}
#endif''')

# ------------------------------------------------------------------ draw loops: each batch's own vertex buffer
# shading pass, base batches
patch(R, '''        const bool baseCb = uploadLayerParams();
        constexpr size_t kBasePerCb = 256;
        for (size_t k = 0; k < refTexIds_.size(); ++k) {
            if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal)) continue;  // layers, decals: below
            const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
            if (id < 0 || size_t(id) >= textures_.size()) continue;
            const auto bind = [&] {''', '''        const bool baseCb = uploadLayerParams();
        constexpr size_t kBasePerCb = 256;
        ID3D11Buffer* baseVb = vbRefTex_;
        for (size_t k = 0; k < refTexIds_.size(); ++k) {
            if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal)) continue;  // layers, decals: below
            const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
            if (id < 0 || size_t(id) >= textures_.size()) continue;
            if (refExtraOutside(k, frustum)) continue;  // a streamed world tile's batch out of view
            if (ID3D11Buffer* v = refVbFor(k); v != baseVb) ctx_->IASetVertexBuffers(0, 1, &v, &tstride, &offset), baseVb = v;
            const auto bind = [&] {''')
patch(R, '''            bind();
            drawN(refTexCount_[k], refTexFirst_[k]);
        }
        if (baseCb && cbLayer_) ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);  // the plain (zero) layer buffer''', '''            bind();
            drawN(refTexCount_[k], refTexFirst_[k]);
        }
        if (baseVb != vbRefTex_) ctx_->IASetVertexBuffers(0, 1, &vbRefTex_, &tstride, &offset);
        if (baseCb && cbLayer_) ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);  // the plain (zero) layer buffer''')
# pre-pass
patch(R, '''            const bool preCb = uploadLayerParams();
            for (size_t k = 0; k < refTexIds_.size(); ++k) {
                if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal)) continue;  // layers, decals: later
                const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
                if (id < 0 || size_t(id) >= textures_.size()) continue;''', '''            const bool preCb = uploadLayerParams();
            ID3D11Buffer* preVb = vbRefTex_;
            for (size_t k = 0; k < refTexIds_.size(); ++k) {
                if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal)) continue;  // layers, decals: later
                const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
                if (id < 0 || size_t(id) >= textures_.size()) continue;
                if (refExtraOutside(k, frustum)) continue;
                if (ID3D11Buffer* v = refVbFor(k); v != preVb) ctx_->IASetVertexBuffers(0, 1, &v, &tstride, &offset), preVb = v;''')
# shadow pass: every world batch (the main reference and the world groups)
patch(R, '''            for (size_t k = 0; k < refTexIds_.size() && (refTexExtraFrom_ == ~size_t(0) || k < refTexExtraFrom_); ++k) {
                if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal)) continue;
                const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
                if (id < 0 || size_t(id) >= textures_.size()) continue;''', '''            ID3D11Buffer* shVb = vbRefTex_;
            for (size_t k = 0; k < refTexIds_.size(); ++k) {
                if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal)) continue;
                const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
                if (id < 0 || size_t(id) >= textures_.size()) continue;
                if (cull && refExtraOutside(k, sf)) continue;
                if (ID3D11Buffer* v = refVbFor(k); v != shVb) ctx_->IASetVertexBuffers(0, 1, &v, &tstride, &offset), shVb = v;''')
# layer and decal pass
patch(R, '''                    ID3D11Buffer* want = k >= refTexExtraFrom_ && vbRefTexExtra_ ? vbRefTexExtra_ : vbRefTex_;''',
      '''                    ID3D11Buffer* want = refVbFor(k);''')
patch(R, '''                        const bool terrainLayer = L.params[0] > 0.5f && L.params[0] < 1.5f;
                        if (!terrainLayer && std::sqrt(dx * dx + dy * dy + dz * dz) - r > 4000.0) continue;''', '''                        const bool terrainLayer = L.params[0] > 0.5f && L.params[0] < 1.5f;
                        const bool nearOnly = k < refTexNearOnly_.size() && refTexNearOnly_[k];  // prop decals (not tiles)
                        if (nearOnly && !terrainLayer && std::sqrt(dx * dx + dy * dy + dz * dz) - r > 4000.0) continue;''')
print('ok')
