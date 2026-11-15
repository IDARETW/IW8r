import io

# CameraGeometry: textures arriving no longer make everything again on every frame (a stock map's textures streaming
# in held the GUI thread 0.3-0.5 s a frame, stall watch 2026-11-15). update(..., recheckTextures) asks every known
# material's texture and normal map again, makes again only the items that use a changed one (in place or through the
# splice path), and updates the batches' emissive / spec / shader ids in place. Each item records its materials (ids).
# Also: GameLightmap::uvFor answers at once when the preview bake has no faces (a stock map without own brushes).

ROOT = 'E:/IW8-Radiant/'


def rw(p, pairs):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl)
        b = b.replace('\n', nl)
        if b in s and a not in s:
            continue
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


G = 'src/viewport/camera_geometry.cpp'
rw(G, [
('''struct MatInfo {
    int draw = 0, tex = -1, nrm = -1, blend = 0;''', '''struct MatInfo {
    uint32_t id = 0;  // the material's number in this build (ItemRec::mats)
    int draw = 0, tex = -1, nrm = -1, blend = 0;'''),
('''    std::vector<Tex> tex;
    size_t texUsed = 0;
    bool linesHidden = false;  // f[kLines] is the place kept for a selected item's outlines (zero-length lines)
    void clear() {
        for (auto& s : f) s.clear();
        sel.clear();''', '''    std::vector<Tex> tex;
    size_t texUsed = 0;
    std::vector<uint32_t> mats;  // the materials (MatInfo::id) the item's faces use
    bool linesHidden = false;  // f[kLines] is the place kept for a selected item's outlines (zero-length lines)
    void clear() {
        for (auto& s : f) s.clear();
        sel.clear();
        mats.clear();'''),
('''    Slice s[kFlatStreams];
    uint32_t texFirst = 0, texCount = 0;
};''', '''    Slice s[kFlatStreams];
    uint32_t texFirst = 0, texCount = 0;
    std::vector<uint32_t> mats;  // ItemOut::mats of the item as made (a changed material's texture makes it again)
};'''),
('''    uint64_t globalKey = 0, serial = 0;''', '''    uint64_t globalKey = 0, serial = 0;
    uint32_t nextMatId = 0;'''),
('''    MatInfo& mat(const std::string& m) { return mats[m]; }''', '''    MatInfo& mat(const std::string& m) {
        auto [it, fresh] = mats.try_emplace(m);
        if (fresh) it->second.id = nextMatId++;
        return it->second;
    }'''),
('''    b->sway = t.sway;
    // Same texture id = same material, so the batch's emissive map is the material's.''', '''    b->sway = t.sway;
    b->material = t.material ? *t.material : std::string();
    // Same texture id = same material, so the batch's emissive map is the material's.'''),
('''            } else {
                mi = &mat(m);
                lastMat = &m;
                lastInfo = mi;
            }''', '''            } else {
                mi = &mat(m);
                lastMat = &m;
                lastInfo = mi;
                if (std::find(out.mats.begin(), out.mats.end(), mi->id) == out.mats.end()) out.mats.push_back(mi->id);
            }'''),
('''bool CameraGeometry::update(const SceneCache& scene, const Document& doc, bool showSpawns, const TextureFn* textureFor, bool force) {''',
 '''bool CameraGeometry::update(const SceneCache& scene, const Document& doc, bool showSpawns, const TextureFn* textureFor, bool force,
                            bool recheckTextures) {'''),
('''    if (!full) {
        IW8R_PROF("g.items");
        d.incremental = true;''', '''    // Textures still arriving: every known material's texture and normal map are asked again; only the items that use
    // a changed one are made again below, and the batches' own maps are updated in place.
    std::vector<uint8_t> matChanged;
    if (!full && recheckTextures) {
        IW8R_PROF("g.recheck");
        for (auto& [name, mi] : d.mats) {
            bool ch = false;
            if (mi.hasTex && textureFor) {
                const int t = (*textureFor)(name);
                if (t != mi.tex) mi.tex = t, ch = true;
                mi.texAsked = d.serial;
            }
            if (mi.hasNrm && scene.normalFor && !scene.captureMode) {
                const int nn = scene.normalFor(name);
                if (nn != mi.nrm) mi.nrm = nn, ch = true;
            }
            if (ch) {
                if (matChanged.size() < d.nextMatId) matChanged.resize(d.nextMatId, 0);
                matChanged[mi.id] = 1;
            }
        }
        if (!scene.captureMode)
            for (auto& [k, b] : d.byKey) {
                if (b->material.empty()) continue;
                if (scene.emissiveFor) b->emissiveTexture = scene.emissiveFor(b->material);
                if (scene.specFor) b->specTexture = scene.specFor(b->material);
                if (scene.shaderFor) b->shader = scene.shaderFor(b->material);
            }
    }
    const auto usesChanged = [&matChanged](const ItemRec& r) {
        if (matChanged.empty()) return false;
        for (const uint32_t id : r.mats)
            if (id < matChanged.size() && matChanged[id]) return true;
        return false;
    };

    if (!full) {
        IW8R_PROF("g.items");
        d.incremental = true;'''),
('''            if (r.stamp == g->stamp && r.sel == sel) continue;
            d.genItem(g, sel, d.out);''', '''            if (r.stamp == g->stamp && r.sel == sel && !usesChanged(r)) continue;
            d.genItem(g, sel, d.out);'''),
('''            r.stamp = g->stamp;
            r.sel = sel;
            changed = true;
            ++inPlace_;''', '''            r.stamp = g->stamp;
            r.sel = sel;
            r.mats = out.mats;
            changed = true;
            ++inPlace_;'''),
('''                    if (r.stamp == g->stamp && r.sel == sel && int64_t(it->second) > lastOld && !d.oldUsed[it->second]) {''',
 '''                    if (r.stamp == g->stamp && r.sel == sel && !usesChanged(r) && int64_t(it->second) > lastOld && !d.oldUsed[it->second]) {'''),
('''                    if (d.plan[i].old >= 0) {
                        const ItemRec& o = d.recs[size_t(d.plan[i].old)];
                        nr.linesHidden = o.linesHidden;''', '''                    if (d.plan[i].old >= 0) {
                        const ItemRec& o = d.recs[size_t(d.plan[i].old)];
                        nr.linesHidden = o.linesHidden;
                        nr.mats = o.mats;'''),
('''                        const ItemOut& out = d.genPool[size_t(d.plan[i].gen)];
                        nr.linesHidden = out.linesHidden;''', '''                        const ItemOut& out = d.genPool[size_t(d.plan[i].gen)];
                        nr.linesHidden = out.linesHidden;
                        nr.mats = out.mats;'''),
('''        d.staleMaterial = false;
        d.mats.clear();''', '''        d.staleMaterial = false;
        d.mats.clear();
        d.nextMatId = 0;'''),
('''            r.g = g;
            r.stamp = g->stamp;
            r.sel = sel;
            r.linesHidden = out.linesHidden;''', '''            r.g = g;
            r.stamp = g->stamp;
            r.sel = sel;
            r.linesHidden = out.linesHidden;
            r.mats = out.mats;'''),
('''                    nb->sway = t.sway;
                    nb->baseZ = 0;''', '''                    nb->sway = t.sway;
                    nb->baseZ = 0;
                    nb->material = t.material ? *t.material : std::string();'''),
])

rw('src/viewport/scene.h', [
('''    bool update(const SceneCache& scene, const Document& doc, bool showSpawns, const TextureFn* textureFor, bool force);''',
 '''    // recheckTextures: textures may have arrived since the last update; the materials are asked again and only the
    // items that use a changed one are made again (force makes everything again).
    bool update(const SceneCache& scene, const Document& doc, bool showSpawns, const TextureFn* textureFor, bool force,
                bool recheckTextures = false);'''),
])
rw('src/viewport/render.h', [
('''    int shader = -1;  // SceneRenderer::createCustomShader id of the material's custom shader (Game Lighting), -1 none
    int blend = 0;''', '''    int shader = -1;  // SceneRenderer::createCustomShader id of the material's custom shader (Game Lighting), -1 none
    std::string material;  // the material it draws (its emissive / spec maps are asked again by name)
    int blend = 0;'''),
])
rw('src/viewport/game_lightmap.cpp', [
('''bool GameLightmap::uvFor(const Vec3& pos, const Vec3& normal, const Vec3& centroid, float out[2]) const {
    const auto it''', '''bool GameLightmap::uvFor(const Vec3& pos, const Vec3& normal, const Vec3& centroid, float out[2]) const {
    if (uv.empty()) return false;  // nothing baked (a stock map without own brushes): no key to make
    const auto it'''),
])

C = 'src/qtapp/camera_view.cpp'
rw(C, [
('''        geo_.update(ctx_->scene, ctx_->doc, ctx_->showSpawns && !ctx_->playing, ctx_->texturedView && imageLookup_ ? &texFn_ : nullptr,
                    geomDirty_ || noInPlace);''', '''        geo_.update(ctx_->scene, ctx_->doc, ctx_->showSpawns && !ctx_->playing, ctx_->texturedView && imageLookup_ ? &texFn_ : nullptr,
                    geomDirty_ || noInPlace, texRecheck_);'''),
('''        geomDirty_ = moreToLoad_;  // textures still arriving: build again next frame''',
 '''        // Textures still arriving: the next frame asks the materials again and makes again only the items whose
        // texture came (a full build per frame held the GUI thread 0.3-0.5 s a frame on a stock map).
        geomDirty_ = false;
        texRecheck_ = moreToLoad_;'''),
('''    const bool rebuild = key != geomKey_ || geomDirty_;''', '''    const bool rebuild = key != geomKey_ || geomDirty_ || texRecheck_;'''),
])
rw('src/qtapp/camera_view.h', [
('''    bool moreToLoad_ = false;''', '''    bool moreToLoad_ = false;
    bool texRecheck_ = false;  // textures were still arriving at the last geometry update: ask the materials again'''),
])
print('ok')
