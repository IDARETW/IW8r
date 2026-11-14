import io

# Faces hidden inside or against an opaque brush are not built or drawn (core face_edges hiddenCovers): the sides
# between the slices of a wall, the parts of overlapping brushes inside each other. A brush hides only when it is
# opaque: every face is drawn with a material known to be opaque, or is caulk / nodraw (EdgeBrush::opaque, set by the
# hosts from the material facts: a techset's blend class, a custom material's keys). Unknown materials hide nothing.


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'E:/IW8-Radiant/src/core/face_edges.h'
patch(H, '''    AABB bounds;
    int set = 0;
};
// Fills bounds from the windings.''', '''    AABB bounds;
    int set = 0;
    // The brush hides what is inside it or flush against it: all its faces are drawn with opaque materials or are
    // caulk / nodraw, and at least one is drawn. The host sets it from what it knows of the materials (unknown: false).
    bool opaque = false;
};
// The opaque flag of a brush from its faces: `materials[i]` of face i, `isOpaque(material)` for a drawn material.
// Tool materials: caulk and nodraw are allowed, any other (clip, trigger, sky, ...) makes the brush hide nothing.
template <class Names, class IsTool, class IsOpaque>
bool edgeBrushOpaque(const EdgeBrush& b, const Names& materials, IsTool isTool, IsOpaque isOpaque) {
    bool drawn = false;
    for (size_t f = 0; f < b.wins.size() && f < materials.size(); ++f) {
        if (b.wins[f].size() < 3) continue;
        const auto& m = materials[f];
        if (isTool(m)) {
            std::string low;
            for (const char ch : m) low += char(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
            if (low.find("caulk") == std::string::npos && low.find("nodraw") == std::string::npos) return false;
            continue;
        }
        if (!isOpaque(m)) return false;
        drawn = true;
    }
    return drawn;
}
// Fills bounds from the windings.''')
patch(H, '''#include <cstdint>
#include <vector>
''', '''#include <cstdint>
#include <string>
#include <vector>
''')
patch(H, '''// Every face of brushes[self]: meshes[face] (empty for a face without a winding, or wholly under coplanar faces of
// other brushes: coplanarCovers).''', '''// The parts of face `face` of brushes[self] that no one can see: inside an opaque brush of its set, or flush against
// one (the open side of the face is in that brush). Convex polygons in the face's plane.
std::vector<Winding> hiddenCovers(const std::vector<EdgeBrush>& brushes, size_t self, size_t face,
                                  const std::vector<uint32_t>& neighbours);

// Every face of brushes[self]: meshes[face] (empty for a face without a winding, wholly under coplanar faces of
// other brushes (coplanarCovers), or wholly hidden (hiddenCovers)).''')

C = 'E:/IW8-Radiant/src/core/face_edges.cpp'
patch(C, '''namespace {

// The pieces without the part under a convex polygon of their plane.''', '''std::vector<Winding> hiddenCovers(const std::vector<EdgeBrush>& brushes, size_t self, size_t face,
                                  const std::vector<uint32_t>& neighbours) {
    std::vector<Winding> out;
    if (self >= brushes.size()) return out;
    const EdgeBrush& B = brushes[self];
    if (face >= B.wins.size() || face >= B.planes.size() || B.wins[face].size() < 3) return out;
    const Vec3 nA = normalize(B.planes[face].n);
    // The face a little in front of itself: where that lies in an opaque brush, nothing can look at the face. (Well
    // clear of the cut tolerance: a coplanar face of the other brush, facing the same way, is not "in front".)
    constexpr double lift = 0.05;
    Winding lifted = B.wins[face];
    AABB ab;
    for (Vec3& p : lifted) {
        p = p + nA * lift;
        ab.add(p);
    }
    for (uint32_t ci : neighbours) {
        if (ci == self || ci >= brushes.size()) continue;
        const EdgeBrush& C = brushes[ci];
        if (C.set != B.set || !C.opaque) continue;
        if (C.bounds.mins.x > ab.maxs.x || C.bounds.maxs.x < ab.mins.x || C.bounds.mins.y > ab.maxs.y || C.bounds.maxs.y < ab.mins.y ||
            C.bounds.mins.z > ab.maxs.z || C.bounds.maxs.z < ab.mins.z)
            continue;
        Winding in = lifted;
        for (size_t g = 0; g < C.planes.size() && !in.empty(); ++g) {
            const double len = length(C.planes[g].n);
            if (len < 1e-12) continue;
            in = clipKeep(in, C.planes[g].n * (1.0 / len), C.planes[g].d / len);
        }
        if (in.empty() || polyArea(in) < 0.05) continue;
        for (Vec3& p : in) p = p - nA * lift;
        out.push_back(std::move(in));
    }
    return out;
}

namespace {

// The pieces without the part under a convex polygon of their plane.''')
patch(C, '''    const std::vector<Winding> covers = coplanarCovers(brushes, self, f, neighbours);
    for (const Winding& cover : covers) pieces = withoutCover(std::move(pieces), cover, n);
    if (coversOut) *coversOut = covers;
    return pieces;''', '''    std::vector<Winding> covers = coplanarCovers(brushes, self, f, neighbours);
    // ... and the parts no one can see (inside or against an opaque brush).
    for (Winding& hidden : hiddenCovers(brushes, self, f, neighbours)) covers.push_back(std::move(hidden));
    for (const Winding& cover : covers) pieces = withoutCover(std::move(pieces), cover, n);
    if (coversOut) *coversOut = covers;
    return pieces;''')

# ---- the build
K = 'E:/IW8-Radiant/src/core/compile_iw8.cpp'
patch(K, '''                finishEdgeBrush(b);
                edgeData.index.emplace(p, edgeData.brushes.size());''', '''                finishEdgeBrush(b);
                {
                    // Opaque: every drawn face a material of the library without cut-out or blending (the editor
                    // puts the stock materials a map uses into the library with their blend class).
                    std::vector<std::string> mats;
                    for (const auto& f : cb) mats.push_back(firstToken(f.suffix));
                    b.opaque = edgeBrushOpaque(b, mats, [](const std::string& m) { return isIw8ToolMaterial(m); }, [&](const std::string& m) {
                        const CustomMaterial* cm = opt.library ? opt.library->find(m) : nullptr;
                        return cm && !cm->alphaTest && !cm->blend && cm->blendMode == 0;
                    });
                }
                edgeData.index.emplace(p, edgeData.brushes.size());''')
patch(K, '''    size_t coveredFaces = 0;          // faces not written: wholly under coplanar faces of other brushes''',
      '''    size_t coveredFaces = 0;          // faces not written: wholly under coplanar faces of other brushes, or hidden''')
patch(K, '''                ++g.coveredFaces;  // wholly under coplanar faces of other brushes''',
      '''                ++g.coveredFaces;  // wholly under coplanar faces of other brushes, or inside / against an opaque brush''')
patch(K, '''        warn.push_back(std::to_string(g.coveredFaces) + " brush face(s) not built: each lies wholly under faces of other brushes in the "
                       "same plane (overlapping faces are drawn once: the smaller one where they overlap)");''',
      '''        warn.push_back(std::to_string(g.coveredFaces) + " brush face(s) not built: each is wholly hidden (inside or against an opaque "
                       "brush) or lies under faces of other brushes in the same plane (overlapping faces are drawn once)");''')

# ---- the viewport
SH = 'E:/IW8-Radiant/src/viewport/scene.h'
patch(SH, '''    bool faceEdges = false;
    std::function<bool()> faceEdgesWanted;''', '''    bool faceEdges = false;
    std::function<bool()> faceEdgesWanted;
    // Is a drawn material opaque for sure (no cut-out, no blending)? A brush of such materials hides the faces inside
    // or against it, which are then not drawn (core face_edges hiddenCovers). Null or false: hides nothing.
    std::function<bool(const std::string& material)> opaqueFor;''')
SC = 'E:/IW8-Radiant/src/viewport/scene.cpp'
patch(SC, '''            b.drawn[f] = !isIw8ToolMaterial(g.polyMaterial[f]);
        }
        finishEdgeBrush(b);''', '''            b.drawn[f] = !isIw8ToolMaterial(g.polyMaterial[f]);
        }
        finishEdgeBrush(b);
        b.opaque = opaqueFor && edgeBrushOpaque(b, g.polyMaterial, [](const std::string& m) { return isIw8ToolMaterial(m); },
                                                [&](const std::string& m) {
                                                    auto it = opaque.find(m);
                                                    if (it == opaque.end()) it = opaque.emplace(m, opaqueFor(m)).first;
                                                    return it->second;
                                                });''')
patch(SC, '''    std::vector<EdgeBrush> brushes(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        const PrimGeom& g = *items[i];''', '''    std::vector<EdgeBrush> brushes(items.size());
    std::unordered_map<std::string, bool> opaque;  // opaqueFor per material, asked once per sync
    for (size_t i = 0; i < items.size(); ++i) {
        const PrimGeom& g = *items[i];''')
patch(SC, '''        key = (key ^ uint64_t(faceEdges ? 2 : 1)) * 1099511628211ull;''', '''        key = (key ^ uint64_t(faceEdges ? 2 : 1)) * 1099511628211ull;
        key = (key ^ uint64_t(brushes[i].opaque ? 5 : 3)) * 1099511628211ull;''')
patch(SC, '''        for (uint32_t n : near[i]) {
            key = (key ^ uint64_t(items[n]->ref.item)) * 1099511628211ull;
            key = (key ^ items[n]->rev) * 1099511628211ull;
            for (uint32_t m : near[n]) {''', '''        for (uint32_t n : near[i]) {
            key = (key ^ uint64_t(items[n]->ref.item)) * 1099511628211ull;
            key = (key ^ items[n]->rev) * 1099511628211ull;
            key = (key ^ uint64_t(brushes[n].opaque ? 5 : 3)) * 1099511628211ull;
            for (uint32_t m : near[n]) {''')
patch(SC, '''                key = (key ^ uint64_t(items[m]->ref.item)) * 1099511628211ull;
                key = (key ^ items[m]->rev) * 1099511628211ull;
            }''', '''                key = (key ^ uint64_t(items[m]->ref.item)) * 1099511628211ull;
                key = (key ^ items[m]->rev) * 1099511628211ull;
                key = (key ^ uint64_t(brushes[m].opaque ? 5 : 3)) * 1099511628211ull;
            }''')

M = 'E:/IW8-Radiant/src/qtapp/main_window.cpp'
patch(M, '''    connect(matCache_, &MaterialCache::materialReady, this, [this](const QString&) { camera_->materialImagesChanged(); });''',
      '''    connect(matCache_, &MaterialCache::materialReady, this, [this](const QString&) {
        camera_->materialImagesChanged();
        invalidateSceneSoon();  // a material's blend class is known now: brushes of it may hide faces (opaqueFor)
    });''')
patch(M, '''    camera_->setEffectSource([this](const std::string& fx) -> std::shared_ptr<const VfxSimDef> {''', '''    // A material that is opaque for sure: a custom one without cut-out or blending, a stock one whose techset says so.
    // A brush made of such materials hides the faces inside or against it (core face_edges); unknown hides nothing.
    ctx_.scene.opaqueFor = [this](const std::string& m) -> bool {
        if (const CustomMaterial* cm = customMaterials_ ? customMaterials_->find(m) : nullptr)
            return !cm->alphaTest && !cm->blend && cm->blendMode == 0;
        if (qs(m).compare("iw8r_glass", Qt::CaseInsensitive) == 0) return false;
        const auto e = matCache_->lookup(qs(m));
        if (!e || e->failed || e->techset.isEmpty()) return false;
        return materialBlendFromTechset(e->techset.toStdString()) == MaterialBlend::Opaque;
    };
    camera_->setEffectSource([this](const std::string& fx) -> std::shared_ptr<const VfxSimDef> {''')
print('ok')
