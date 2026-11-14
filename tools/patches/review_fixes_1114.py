import io

# Fixes for the defects an independent review found in the changes of 2026-11-14:
#  1. a prop outside a LOD group was culled by distance per material part (each part's own box): the model's box now;
#  2. a model with untextured parts keeps drawing as a whole (those parts are not culled); the sun and lamp shadow
#     passes leave out the cells whose placements are all past their cull distance;
#  3. a decode job that throws ends as a failed result instead of ending the program;
#  4. a change of the preview size (the texture budget) clears the decode pool, or its finished results of the old
#     size would fill it for seconds;
#  5. a tool face under a paint mesh is recognised by the patch triangles (shared viewport/patch_covers), so a face
#     of five or more corners is covered by its patches together, and a box that merely holds a face hides nothing;
#  6. a cancelled or failed entity export leaves a marker, and the next open exports again instead of reading what a
#     still running export may have half written; a second export cannot start inside the first;
#  7. the reference geometry builder hides the same covered faces as the kept geometry (the self-check compares them);
#  8. a capture's vignette uses the capture's shape, and a clean capture leaves out the props the view leaves out;
#  9. the white balance method and illuminant are range-checked before they become integers.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, n=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    write(p, s.replace(a, b))


def cut(p, start, end, replacement):
    # Replaces from the first `start` up to (not including) the first `end` after it.
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    start = start.replace('\n', nl); end = end.replace('\n', nl)
    assert s.count(start) == 1, (p, s.count(start), start[:60])
    i = s.index(start)
    j = s.index(end, i)
    write(p, s[:i] + replacement.replace('\n', nl) + s[j:])


# ---------------- 5 / 7: shared cover test
G = 'src/viewport/camera_geometry.cpp'
cut(G, '''    // Flat patches that touch a brush with tool faces (a paint mesh on a caulked face): a tool face wholly under one is''',
    '''    MatInfo& mat(const std::string& m) { return mats[m]; }''',
    '''    // A tool face of a brush that lies under flat patches (a paint mesh on a caulked face) is not drawn
    // (patch_covers.h). Found again at every update; the hash is part of the global key.
    PatchCovers covers;

''')
cut(G, '''uint64_t CameraGeometry::Impl::findCovers(const std::vector<const PrimGeom*>& items) {''',
    '''void CameraGeometry::Impl::genItem(const PrimGeom* g, bool sel, ItemOut& out) {''', '')
patch(G, '''            const bool covered = tool && !sel && !wire && !covers.empty() && g->kind == PrimGeom::Brush && i < g->faceNormals.size() &&
                                 toolFaceCovered(w, g->faceNormals[i]);''',
      '''            const bool covered = tool && !sel && !wire && covers.any() && g->kind == PrimGeom::Brush && i < g->faceNormals.size() &&
                                 covers.covered(w, g->faceNormals[i]);''')
patch(G, '''    key = mixBits(key, d.findCovers(items));''', '''    key = mixBits(key, d.covers.find(items));''')
patch(G, '''#include "scene.h"
''', '''#include "scene.h"
#include "patch_covers.h"
''')
S = 'src/viewport/scene.cpp'
patch(S, '''    const bool texturesOn = mode == CameraMode::Textured || mode == CameraMode::Fullbright;
    for (const PrimGeom* g : scene.items()) {
        // Probe capture keeps only what the game draws: brushes, patches, models and prefab contents.''',
      '''    const bool texturesOn = mode == CameraMode::Textured || mode == CameraMode::Fullbright;
    PatchCovers covers;  // as the kept geometry (camera_geometry.cpp): a tool face under a flat patch is not drawn
    covers.find(scene.items());
    for (const PrimGeom* g : scene.items()) {
        // Probe capture keeps only what the game draws: brushes, patches, models and prefab contents.''')
patch(S, '''                if (&dst == &noCast && encodedMode) c = (c & 0x00ffffffu) | 0x80000000u;
                for (size_t k = 1; k + 1 < w.size(); ++k) {''',
      '''                if (&dst == &noCast && encodedMode) c = (c & 0x00ffffffu) | 0x80000000u;
                const bool covered = tool && !sel && !wire && covers.any() && g->kind == PrimGeom::Brush && i < g->faceNormals.size() &&
                                     covers.covered(w, g->faceNormals[i]);
                for (size_t k = 1; !covered && k + 1 < w.size(); ++k) {''')
patch(S, '''#include "scene.h"
''', '''#include "scene.h"
#include "patch_covers.h"
''')

# ---------------- 1 / 2 / 8: prop culling
RH = 'src/viewport/render.h'
patch(RH, '''    // Per instance, the scale on lodOut (the placement's lodScale, research 119); 0 = this placement is never left out
    // by distance. Empty: 1 for every instance.
    std::vector<float> cullScale;''', '''    // Per instance, the scale on lodOut (the placement's lodScale, research 119); 0 = this placement is never left out
    // by distance. Empty: 1 for every instance.
    std::vector<float> cullScale;
    // The whole model's box in model space (mins, maxs): the distance for the LOD and the cull is measured to it, so
    // every material part of a placement goes at the same distance. hasModelBox false: this mesh's own box.
    bool hasModelBox = false;
    float modelBox[6] = {0, 0, 0, 0, 0, 0};''')
patch(RH, '''        struct InstCell {
            iw8r::AABB box;
            uint32_t first = 0, count = 0;
        };''', '''        struct InstCell {
            iw8r::AABB box;
            uint32_t first = 0, count = 0;
            float maxScale = 1;  // the largest cullScale of its placements; 0 = one of them is never left out
        };''')
patch(RH, '''    bool filmGrainEnabled_ = true;''', '''    bool filmGrainEnabled_ = true;
    // The view's distance cull as the shadow passes need it (drawReferenceTexInstancesShadow): on for this frame, the
    // capped field-of-view factor.
    bool cellCullOn_ = false;
    float cellCullFov_ = 1.0f;''')
RC = 'src/viewport/render.cpp'
patch(RC, '''                    for (int c = 0; c < 3; ++c) g.boxMid[c] = g.centre[c], g.boxHalf[c] = 0.5f * (hi[c] - lo[c]);''',
      '''                    for (int c = 0; c < 3; ++c) g.boxMid[c] = g.centre[c], g.boxHalf[c] = 0.5f * (hi[c] - lo[c]);
                    if (m.hasModelBox)  // the LOD and cull distance are the whole model's, not this material part's
                        for (int c = 0; c < 3; ++c)
                            g.boxMid[c] = 0.5f * (m.modelBox[c] + m.modelBox[c + 3]), g.boxHalf[c] = 0.5f * (m.modelBox[c + 3] - m.modelBox[c]);''')
patch(RC, '''                        const double rad = g.radius * scale * 1.0001 + 1.0;
                        c.box.add(iw8r::Vec3{wc[0] - rad, wc[1] - rad, wc[2] - rad});
                        c.box.add(iw8r::Vec3{wc[0] + rad, wc[1] + rad, wc[2] + rad});
                    }''', '''                        const double rad = g.radius * scale * 1.0001 + 1.0;
                        c.box.add(iw8r::Vec3{wc[0] - rad, wc[1] - rad, wc[2] - rad});
                        c.box.add(iw8r::Vec3{wc[0] + rad, wc[1] + rad, wc[2] + rad});
                        if (!g.cullScale.empty()) {
                            const float cs = g.cullScale[i];
                            if (c.count == 1) c.maxScale = cs;
                            else if (c.maxScale > 0) c.maxScale = cs > 0 ? std::max(c.maxScale, cs) : 0.0f;
                        }
                    }''')
patch(RC, '''    static const bool lodCentreDist = std::getenv("IW8R_LOD_CENTRE_DIST") != nullptr;''',
      '''    static const bool lodCentreDist = std::getenv("IW8R_LOD_CENTRE_DIST") != nullptr;
    cellCullOn_ = !noLodOut;
    cellCullFov_ = std::min(lodFov, 1.0f);''')
patch(RC, '''        // Runs of neighbouring cells in the view, one draw each.
        uint32_t runFirst = 0, runCount = 0;
        for (const auto& c : g.cells) {
            if (iw8r::frustumIntersectsAabb(*view, c.box)) {''', '''        // Runs of neighbouring cells in the view, one draw each. A cell whose placements are all past their cull
        // distance in the camera's view (research 119) casts nothing: the game leaves them out of its shadow passes too.
        const bool distCull = cellCullOn_ && g.lodOut > 0;
        uint32_t runFirst = 0, runCount = 0;
        for (const auto& c : g.cells) {
            if (distCull && c.maxScale > 0) {
                double d2 = 0;
                const double e[3] = {fog_[0], fog_[1], fog_[2]}, lo[3] = {c.box.mins.x, c.box.mins.y, c.box.mins.z},
                             hi[3] = {c.box.maxs.x, c.box.maxs.y, c.box.maxs.z};
                for (int k = 0; k < 3; ++k) {
                    const double d = std::max(std::max(lo[k] - e[k], e[k] - hi[k]), 0.0);
                    d2 += d * d;
                }
                if (iw8r::placementLodCulled(float(std::sqrt(d2)), cellCullFov_, c.maxScale, g.lodOut)) continue;
            }
            if (iw8r::frustumIntersectsAabb(*view, c.box)) {''')
patch(RC, '''        instCull_ = !noCull && !in.capture && !in.clean;
        if (instCull_ && !refTexInst_.empty()) {''', '''        // A clean screenshot leaves out what the view leaves out (the game's distance rule); a probe capture draws all.
        instCull_ = !noCull && (!in.capture || in.clean);
        cellCullOn_ = false;  // set by cullTexInstances when it runs
        if (instCull_ && !refTexInst_.empty()) {''')
M = 'src/qtapp/main_window.cpp'
patch(M, '''                    fresh->lod0Far = fresh->lods.empty() ? fresh->lodOut : fresh->lods[0].nearD;
                }''', '''                    fresh->lod0Far = fresh->lods.empty() ? fresh->lodOut : fresh->lods[0].nearD;
                }
                // The model's box (every textured part of LOD0): the distance of its LOD and cull is measured to it.
                for (const auto& part : fresh->tex)
                    for (const app::PropVertex& v : part.second->vertices) {
                        const float p[3] = {v.x, v.y, v.z};
                        for (int c = 0; c < 3; ++c)
                            fresh->box[c] = std::min(fresh->box[c], p[c]), fresh->box[c + 3] = std::max(fresh->box[c + 3], p[c]);
                    }
                fresh->hasBox = !fresh->tex.empty();''')
patch(M, '''                    tm.mesh = tris;
                    tm.lodOut = float(built->lodOut);
                    tm.lodGroup = group;
                    tm.lodNear = 0, tm.lodFar = float(built->lod0Far);''', '''                    tm.mesh = tris;
                    tm.lodOut = float(built->lodOut);
                    tm.lodGroup = group;
                    tm.lodNear = 0, tm.lodFar = float(built->lod0Far);
                    tm.hasModelBox = built->hasBox;
                    std::copy(built->box, built->box + 6, tm.modelBox);''')
patch(M, '''                        tm.mesh = tris;
                        tm.lodOut = float(built->lodOut);
                        tm.lodGroup = group;
                        tm.lodNear = float(lod.nearD), tm.lodFar = float(lod.farD);''', '''                        tm.mesh = tris;
                        tm.lodOut = float(built->lodOut);
                        tm.lodGroup = group;
                        tm.lodNear = float(lod.nearD), tm.lodFar = float(lod.farD);
                        tm.hasModelBox = built->hasBox;
                        std::copy(built->box, built->box + 6, tm.modelBox);''')
MH = 'src/qtapp/main_window.h'
patch(MH, '''        double lod0Far = 0;  // LOD0's far (0 = no limit)
    };''', '''        double lod0Far = 0;  // LOD0's far (0 = no limit)
        // The box of LOD0's textured parts in model space (mins, maxs); hasBox false: no textured part.
        bool hasBox = false;
        float box[6] = {1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f};
    };''')

# ---------------- 3: the pool
patch('src/core/keyed_job_pool.h', '''            Result r = e->job();
            std::function<void()> cb;''', '''            // A job that throws (out of memory in a decoder, say) ends as an empty result: the entry must become ready,
            // or a waiter would wait for ever, and an exception must not leave the thread.
            Result r{};
            try {
                r = e->job();
            } catch (...) {
                r = Result{};
            }
            std::function<void()> cb;''')
patch('src/core/keyed_job_pool.h', '''        lock.unlock();
        out = job();
        return true;
    }

    void setReadyCallback''', '''        lock.unlock();
        try {
            out = job();
        } catch (...) {
            out = Result{};
        }
        return true;
    }

    void setReadyCallback''')

# ---------------- 4: the preview size
CV = 'src/qtapp/camera_view.cpp'
patch(CV, '''    const std::string key = std::to_string(mode) + ":" + std::to_string(side) + ":" + path;
    if (!decodePool_->take(key, job, out, wait)) return false;''', '''    // The preview size changed (the texture budget was reached): what was decoded at the old size will not be asked
    // for again, and would fill the pool until it went stale.
    if (side != decodeSide_) {
        if (decodeSide_ != 0) decodePool_->clear();
        decodeSide_ = side;
    }
    const std::string key = std::to_string(mode) + ":" + std::to_string(side) + ":" + path;
    if (!decodePool_->take(key, job, out, wait)) return false;''')
patch(CV, '''    if (!decodePool_) {
        if (wait) {
            out = job();
            return true;
        }''', '''    if (!decodePool_) {
        if (wait) {
            try {
                out = job();
            } catch (...) {
                out = DecodedTexture{};
            }
            return true;
        }''')
patch('src/qtapp/camera_view.h', '''    std::unique_ptr<KeyedJobPool<DecodedTexture>> decodePool_;''', '''    std::unique_ptr<KeyedJobPool<DecodedTexture>> decodePool_;
    int decodeSide_ = 0;  // the preview size of the pool's entries''')

# ---------------- 8: the capture's vignette
patch(CV, '''            const double aspect = height() > 0 ? double(width()) / double(height()) : 16.0 / 9.0;
            gameVignetteConstants(stockPostFx_, aspect, 1.0, g.vignette);''', '''            // The shape of what is being drawn: a capture's own, else the widget's.
            double aspect = height() > 0 ? double(width()) / double(height()) : 16.0 / 9.0;
            if (captureW_ > 0 && captureH_ > 0) aspect = captureAspect_ > 0 ? captureAspect_ : double(captureW_) / double(captureH_);
            gameVignetteConstants(stockPostFx_, aspect, 1.0, g.vignette);''')

# ---------------- 9: white balance integers
patch('src/core/white_balance.cpp', '''    wb.method = int(method);
    wb.illuminant = int(illuminant);''', '''    // Out of range (or not a number) becomes -1, which the matrix answers with the identity.
    wb.method = method >= 0 && method <= 6 ? int(method) : -1;
    wb.illuminant = illuminant >= 0 && illuminant <= 21 ? int(illuminant) : -1;''')

# ---------------- 6: the entity export
patch(M, '''    QString json = findMapEntsJson(cache);
    if (json.isEmpty()) json = exportStockMapEnts(zone, cache, err);''', '''    // An export that was cancelled or failed left its marker: what is in the folder may be half written.
    if (QFileInfo::exists(cache + "/export.incomplete")) QDir(cache + "/mw19replay").removeRecursively();
    QString json = QFileInfo::exists(cache + "/export.incomplete") ? QString() : findMapEntsJson(cache);
    if (json.isEmpty()) json = exportStockMapEnts(zone, cache, err);''')
patch(M, '''    if (viaRunner) *viaRunner = false;
    const QString acts = toolPath("acts"), replay = toolPath("replay"), oodle = toolPath("oodle");
    const QString name = QFileInfo(zone).completeBaseName();
    QDir().mkpath(cache);''', '''    if (viaRunner) *viaRunner = false;
    // One at a time: the wait below runs the event loop, and a second "Open Stock Map" could start inside this one.
    if (exportingMapEnts_) {
        if (err) *err = "A stock map is being read already; wait for it to finish.";
        return {};
    }
    exportingMapEnts_ = true;
    struct Done {
        bool& flag;
        ~Done() { flag = false; }
    } exportDone{exportingMapEnts_};
    const QString acts = toolPath("acts"), replay = toolPath("replay"), oodle = toolPath("oodle");
    const QString name = QFileInfo(zone).completeBaseName();
    QDir().mkpath(cache);
    // The marker stays unless the export ends well: a cancelled run may still be writing (openStockZone looks for it).
    const QString marker = cache + "/export.incomplete";
    {
        QFile mf(marker);
        if (mf.open(QIODevice::WriteOnly)) mf.write("the map_ents export into this folder did not finish\\n");
    }''')
patch(M, '''    if ((exitCode != 0 && exitCode != 127) || json.isEmpty()) {
        if (err) *err = "ACTS did not produce map_ents for " + name + " (exit " + QString::number(exitCode) + ").\\n\\n" + log.right(1500);
        return {};
    }
    return json;''', '''    if ((exitCode != 0 && exitCode != 127) || json.isEmpty()) {
        if (err) *err = "ACTS did not produce map_ents for " + name + " (exit " + QString::number(exitCode) + ").\\n\\n" + log.right(1500);
        return {};
    }
    QFile::remove(marker);
    return json;''')
patch(MH, '''    QTimer* streamTimer_ = nullptr;''', '''    QTimer* streamTimer_ = nullptr;
    bool exportingMapEnts_ = false;  // exportStockMapEnts is running (it waits in the event loop)''')
print('ok')
