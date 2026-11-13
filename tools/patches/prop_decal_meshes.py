import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


RH = 'src/viewport/render.h'
patch(RH, '''        int normal = -1;            // setReferenceInstNormal''', '''        int normal = -1;            // setReferenceInstNormal
        int decal = 0;              // setReferenceInstDecal: the material's decal sort key (0 = not a decal)''')
patch(RH, '''    void setReferenceInstNormal(size_t mesh, int textureId) {
        if (mesh < refTexInst_.size()) refTexInst_[mesh].normal = textureId;
    }''', '''    void setReferenceInstNormal(size_t mesh, int textureId) {
        if (mesh < refTexInst_.size()) refTexInst_[mesh].normal = textureId;
    }
    // A prop mesh whose material is a decal (paint bands, stencils and grime laid over the model's own surface): drawn
    // after the other props, pulled toward the camera, without writing depth, casting no shadow. Drawn as an opaque
    // mesh it fought the surface under it (owner 2026-11-13: a yellow band on mp_rust flickered with every move).
    void setReferenceInstDecal(size_t mesh, int sortKey) {
        if (mesh < refTexInst_.size()) refTexInst_[mesh].decal = sortKey;
    }''')

R = 'src/viewport/render.cpp'
patch(R, '''    const UINT strides[2] = {UINT(sizeof(PropVertex)), UINT(24 * sizeof(float))}, offsets[2] = {0, 0};
    for (const auto& g : refTexInst_) {
        if (!g.vb || !g.inst) continue;
        const int id = g.texture >= 0 ? g.texture : whiteTex_;
        if (id < 0 || size_t(id) >= textures_.size()) continue;
        const bool culled = instCull_ && g.visInst && g.visible <= g.instances;''', '''    const UINT strides[2] = {UINT(sizeof(PropVertex)), UINT(24 * sizeof(float))}, offsets[2] = {0, 0};
    // The props' decal meshes after the others (setReferenceInstDecal); none in the depth pre-pass.
    ID3D11RasterizerState* savedRs = nullptr;
    ID3D11DepthStencilState* savedDs = nullptr;
    UINT savedRef = 0;
    bool decalStateSet = false;
    for (int decalPass = 0; decalPass < 2; ++decalPass)
    for (const auto& g : refTexInst_) {
        if (!g.vb || !g.inst) continue;
        if ((g.decal > 0) != (decalPass == 1)) continue;
        if (decalPass == 1 && texPsOverride_) continue;
        if (decalPass == 1 && !decalStateSet && rsDecal_) {
            decalStateSet = true;
            ctx_->RSGetState(&savedRs);
            ctx_->OMGetDepthStencilState(&savedDs, &savedRef);
            ctx_->RSSetState(rsDecal_);
            ctx_->OMSetDepthStencilState(dsTest_, 0);
        }
        const int id = g.texture >= 0 ? g.texture : whiteTex_;
        if (id < 0 || size_t(id) >= textures_.size()) continue;
        const bool culled = instCull_ && g.visInst && g.visible <= g.instances;''')
patch(R, '''        ctx_->IASetIndexBuffer(g.ib, DXGI_FORMAT(g.ibFormat), 0);
        drawInstIdx(g.vertices, prepassBig ? g.big : culled ? g.visible : g.instances);
    }''', '''        ctx_->IASetIndexBuffer(g.ib, DXGI_FORMAT(g.ibFormat), 0);
        drawInstIdx(g.vertices, prepassBig ? g.big : culled ? g.visible : g.instances);
    }
    if (decalStateSet) {
        ctx_->RSSetState(savedRs);
        ctx_->OMSetDepthStencilState(savedDs, savedRef);
        if (savedRs) savedRs->Release();
        if (savedDs) savedDs->Release();
    }''')
# Shadows: decals cast nothing.
patch(R, '''    for (const auto& g : refTexInst_) {
        if (!g.vb || !g.inst) continue;
        if (g.lodGroup >= 0 && g.lodNear > 0) continue;  // the sun shadow of a LOD group: its LOD0''', '''    for (const auto& g : refTexInst_) {
        if (!g.vb || !g.inst) continue;
        if (g.decal > 0) continue;  // decals cast no shadow
        if (g.lodGroup >= 0 && g.lodNear > 0) continue;  // the sun shadow of a LOD group: its LOD0''')

CH = 'src/qtapp/camera_view.h'
patch(CH, '''        int decalSortKey = 0;''', '''        int decalSortKey = 0;
        int propDecalSortKey = 0;  // a model material that is a decal (blended `_bad_`, sort key up to 17); 0 = not''')
C = 'src/qtapp/camera_view.cpp'
patch(C, '''                    if (nid >= 0) renderer_.setReferenceInstNormal(k, nid);
                    if (const auto p = materialConstantsFor(texInstMaterials_[k])) renderer_.setReferenceInstParams(k, p->data());''', '''                    if (nid >= 0) renderer_.setReferenceInstNormal(k, nid);
                    if (const auto p = materialConstantsFor(texInstMaterials_[k])) renderer_.setReferenceInstParams(k, p->data());
                    if (factsLookup_)
                        if (const auto f = factsLookup_(texInstMaterials_[k])) renderer_.setReferenceInstDecal(k, f->propDecalSortKey);''')
M = 'src/qtapp/main_window.cpp'
patch(M, '''            f.decalSortKey = std::max(1, e->sortKey);
        f.ssr = parseStockTechsetName(e->techset.toStdString()).ssr;''', '''            f.decalSortKey = std::max(1, e->sortKey);
        // Model decals (research 98's blend token on model techsets; sort keys 10-17 seen): grenade box stencils, frame
        // decals, reveal layers. Glass and other blended surfaces sort later (25, 30) and are not decals.
        if (ts.contains("_bad_") && e->sortKey > 0 && e->sortKey <= 17 && !e->techset.startsWith("tw")) f.propDecalSortKey = e->sortKey;
        f.ssr = parseStockTechsetName(e->techset.toStdString()).ssr;''')
print('ok')
