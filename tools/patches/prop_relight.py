import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


# --- renderer: new light for one textured prop mesh (its instance buffer is rebuilt; vertices stay) ---
RH = 'src/viewport/render.h'
patch(RH, '''    bool setReferenceTexInstances(const std::vector<InstancedTexMesh>& meshes);''',
      '''    bool setReferenceTexInstances(const std::vector<InstancedTexMesh>& meshes);
    // New per-placement light (InstancedTexMesh::light, 12 floats per instance) for mesh `mesh` of the last
    // setReferenceTexInstances: only its instance buffer is made again. False when the counts do not match.
    bool setReferenceTexInstanceLight(size_t mesh, const std::vector<float>& light);''')

R = 'src/viewport/render.cpp'
patch(R, '''bool SceneRenderer::setReferenceTexInstances(const std::vector<InstancedTexMesh>& meshes) {''',
      '''bool SceneRenderer::setReferenceTexInstanceLight(size_t mesh, const std::vector<float>& light) {
    if (!dev_ || mesh >= refTexInst_.size()) return false;
    GpuTexInstanced& g = refTexInst_[mesh];
    const size_t count = g.instances;
    if (!g.inst || count == 0 || light.size() != count * 12 || g.rows.size() != count * 12) return false;
    std::vector<float> inst24(count * 24);
    for (size_t i = 0; i < count; ++i) {
        std::memcpy(&inst24[i * 24], &g.rows[i * 12], 12 * sizeof(float));
        std::memcpy(&inst24[i * 24 + 12], &light[i * 12], 12 * sizeof(float));
    }
    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.ByteWidth = UINT(count * 24 * sizeof(float));
    D3D11_SUBRESOURCE_DATA init{inst24.data(), 0, 0};
    ID3D11Buffer* fresh = nullptr;
    if (FAILED(dev_->CreateBuffer(&bd, &init, &fresh))) return false;
    safeRelease(g.inst);
    g.inst = fresh;
    g.light = light;  // the culling copies it into the visible-placement buffers every frame
    return true;
}

bool SceneRenderer::setReferenceTexInstances(const std::vector<InstancedTexMesh>& meshes) {''')

# --- camera: pass it on (to the pending meshes when they are not uploaded yet) ---
CH = 'src/qtapp/camera_view.h'
patch(CH, '''    size_t referenceTexInstanceMeshes() const { return texInstMaterials_.size(); }''',
      '''    size_t referenceTexInstanceMeshes() const { return texInstMaterials_.size(); }
    // New per-placement light for every textured prop mesh (same order and counts as setReferenceTexInstances gave).
    void setReferenceTexInstanceLight(std::vector<std::vector<float>> light) {
        if (texInstDirty_) {
            for (size_t i = 0; i < light.size() && i < pendingTexInst_.size(); ++i) pendingTexInst_[i].light = std::move(light[i]);
        } else {
            pendingTexLight_ = std::move(light);
            texLightDirty_ = true;
        }
        update();
    }''')
patch(CH, '''    std::vector<app::InstancedTexMesh> pendingTexInst_;''', '''    std::vector<app::InstancedTexMesh> pendingTexInst_;
    std::vector<std::vector<float>> pendingTexLight_;  // setReferenceTexInstanceLight
    bool texLightDirty_ = false;''')
C = 'src/qtapp/camera_view.cpp'
patch(C, '''        texInstResolved_.assign(texInstMaterials_.size(), false);
        texInstDirty_ = false;
        texInstPending_ = refPollDue_ = true;
    }''', '''        texInstResolved_.assign(texInstMaterials_.size(), false);
        texInstDirty_ = false;
        texInstPending_ = refPollDue_ = true;
    }
    if (texLightDirty_) {
        IW8R_PROF("r.up_texlight");
        for (size_t i = 0; i < pendingTexLight_.size(); ++i)
            if (!pendingTexLight_[i].empty()) renderer_.setReferenceTexInstanceLight(i, pendingTexLight_[i]);
        pendingTexLight_.clear();
        pendingTexLight_.shrink_to_fit();
        texLightDirty_ = false;
    }''')

# --- main window: remember each placement's light key per mesh; relight instead of rebuilding ---
MH = 'src/qtapp/main_window.h'
patch(MH, '''    size_t refPropLit_ = 0, refPropLitOf_ = 0;  // propLightStats''', '''    size_t refPropLit_ = 0, refPropLitOf_ = 0;  // propLightStats
    // Per textured prop mesh (the camera's mesh order), each placement's light grid key in draw order: a light grid
    // that arrives later (a streamed tile's, one after another on Warzone) relights the props without rebuilding them
    // (relightReferenceProps; a rebuild reads every model again). `first`: the placement's first textured mesh.
    struct PropLightKey {
        uint32_t lightingIndex = 0xFFFFFFFF;
        uint16_t transient = 0;
        bool probe = false, first = false;
    };
    std::vector<std::vector<PropLightKey>> refPropLightKeys_;
    bool propLightFor(const PropLightKey& k, float out[12]) const;''')
patch(MH, '''    void rebuildReferenceProps();''', '''    void rebuildReferenceProps();
    void relightReferenceProps();  // new light grids: only the props' light (falls back to a rebuild)''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''        // Research 105: a LIGHTPROBE prop of the main transient zone takes its light grid box (12 floats, DC first);
        // every other placement gets DC r = -1 (the probes' fallback in the shader).
        float lit[12] = {-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        static const bool noPropLgv = std::getenv("IW8R_NO_PROP_LGV") != nullptr;  // A/B: the probes' light as before
        const std::vector<float>* grid = nullptr;  // the prop's transient zone's grid: the main zone's, or its tile's
        if (in.worldTransient == 0) {
            grid = refPropVolLight_.get();
        } else if (const auto t = refPropTileLight_.find(int(in.worldTransient)); t != refPropTileLight_.end()) {
            grid = t->second.get();
        }
        if (!noPropLgv && grid && (in.modelFlags & 1u) && size_t(in.lightingIndex) < grid->size() / 12) {
            std::memcpy(lit, &(*grid)[size_t(in.lightingIndex) * 12], sizeof(lit));
            if (!it->second.tex.empty()) ++refPropLit_;
        }
        if (!it->second.tex.empty()) ++refPropLitOf_;
        for (const int ti : it->second.tex) {
            auto& dst = texInstanced[size_t(ti)].rows;
            dst.insert(dst.end(), rows, rows + 12);
            auto& ld = texInstanced[size_t(ti)].light;
            ld.insert(ld.end(), lit, lit + 12);
        }''', '''        // Research 105: a LIGHTPROBE prop of the main transient zone takes its light grid box (12 floats, DC first);
        // every other placement gets DC r = -1 (the probes' fallback in the shader).
        PropLightKey key;
        key.lightingIndex = in.lightingIndex;
        key.transient = in.worldTransient;
        key.probe = (in.modelFlags & 1u) != 0;
        float lit[12];
        if (propLightFor(key, lit) && !it->second.tex.empty()) ++refPropLit_;
        if (!it->second.tex.empty()) ++refPropLitOf_;
        if (lightKeys.size() < texInstanced.size()) lightKeys.resize(texInstanced.size());
        key.first = true;
        for (const int ti : it->second.tex) {
            auto& dst = texInstanced[size_t(ti)].rows;
            dst.insert(dst.end(), rows, rows + 12);
            auto& ld = texInstanced[size_t(ti)].light;
            ld.insert(ld.end(), lit, lit + 12);
            lightKeys[size_t(ti)].push_back(key);
            key.first = false;
        }''')
patch(M, '''    size_t meshTris = 0, meshed = 0;
    int lodGroups = 0;  // plan 1.13: LOD groups numbered per rebuild''', '''    size_t meshTris = 0, meshed = 0;
    std::vector<std::vector<PropLightKey>> lightKeys;  // refPropLightKeys_ of this rebuild
    refPropLightKeys_.clear();
    int lodGroups = 0;  // plan 1.13: LOD groups numbered per rebuild''')
patch(M, '''    camera_->setReferenceInstances(std::move(instanced));
    camera_->setReferenceTexInstances(std::move(texInstanced), std::move(texMaterials));
    referencePropMeshes_ = meshed;''', '''    lightKeys.resize(texInstanced.size());
    refPropLightKeys_ = std::move(lightKeys);
    camera_->setReferenceInstances(std::move(instanced));
    camera_->setReferenceTexInstances(std::move(texInstanced), std::move(texMaterials));
    referencePropMeshes_ = meshed;''')
patch(M, '''void MainWindow::rebuildReferencePropsUnguarded() {''', '''bool MainWindow::propLightFor(const PropLightKey& k, float out[12]) const {
    static const float none[12] = {-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(out, none, sizeof(none));
    static const bool noPropLgv = std::getenv("IW8R_NO_PROP_LGV") != nullptr;  // A/B: the probes' light as before
    if (noPropLgv || !k.probe) return false;
    const std::vector<float>* grid = nullptr;  // the prop's transient zone's grid: the main zone's, or its tile's
    if (k.transient == 0) {
        grid = refPropVolLight_.get();
    } else if (const auto t = refPropTileLight_.find(int(k.transient)); t != refPropTileLight_.end()) {
        grid = t->second.get();
    }
    if (!grid || size_t(k.lightingIndex) >= grid->size() / 12) return false;
    std::memcpy(out, &(*grid)[size_t(k.lightingIndex) * 12], 12 * sizeof(float));
    return true;
}

void MainWindow::relightReferenceProps() {
    // The camera must hold the meshes these keys were made for; otherwise (none yet, another rebuild since) rebuild.
    if (refPropLightKeys_.empty() || !camera_ || camera_->referenceTexInstanceMeshes() != refPropLightKeys_.size()) {
        rebuildReferenceProps();
        return;
    }
    std::vector<std::vector<float>> light(refPropLightKeys_.size());
    size_t lit = 0, of = 0;
    for (size_t m = 0; m < refPropLightKeys_.size(); ++m) {
        const auto& keys = refPropLightKeys_[m];
        light[m].resize(keys.size() * 12);
        for (size_t i = 0; i < keys.size(); ++i) {
            const bool got = propLightFor(keys[i], &light[m][i * 12]);
            if (keys[i].first) lit += got ? 1 : 0, ++of;
        }
    }
    refPropLit_ = lit, refPropLitOf_ = of;
    camera_->setReferenceTexInstanceLight(std::move(light));
}

void MainWindow::rebuildReferencePropsUnguarded() {''')
patch(M, '''        refPropBuilt_.clear();
        refPropBuiltTris_ = 0;
        ctx_.setStatus("Not enough memory to rebuild the stock map's props''', '''        refPropBuilt_.clear();
        refPropBuiltTris_ = 0;
        refPropLightKeys_.clear();
        ctx_.setStatus("Not enough memory to rebuild the stock map's props''')
# Light grids arriving: relight only.
patch(M, '''                QTimer::singleShot(1500, self, [self] {
                    if (!self) return;
                    self->refPropRebuildQueued_ = false;
                    self->rebuildReferenceProps();
                });''', '''                QTimer::singleShot(1500, self, [self] {
                    if (!self) return;
                    self->refPropRebuildQueued_ = false;
                    self->relightReferenceProps();
                });''')
patch(M, '''            self->refPropVolLight_ = vol;
            self->refPropVolLightZone_ = zone;
            self->rebuildReferenceProps();''', '''            self->refPropVolLight_ = vol;
            self->refPropVolLightZone_ = zone;
            self->relightReferenceProps();''')
print('ok')
