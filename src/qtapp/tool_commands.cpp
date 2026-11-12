#include "heightmap.h"
#include "tool_commands.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "brush_ops2.h"
#include "brush_tools.h"
#include "generators.h"
#include "patch_ops.h"
#include "patch_ops2.h"
#include "primitives.h"

namespace iw8r::qt {

namespace {

std::string vec(const Vec3& v) { return formatNumber(v.x) + " " + formatNumber(v.y) + " " + formatNumber(v.z); }

struct SelPatch {
    Id entity, item;
    PatchGrid grid;
};

std::vector<SelPatch> selectedPatchGrids(EditorContext& ctx) {
    std::vector<SelPatch> out;
    for (const auto& r : ctx.doc.selection()) {
        const EntityItem* it = r.item ? ctx.doc.findItem(r.entity, r.item) : nullptr;
        const Primitive* p = it ? it->prim() : nullptr;
        if (!p || p->kind != PrimKind::Patch) continue;
        if (auto g = patchGrid(*p)) out.push_back({r.entity, r.item, *g});
    }
    return out;
}

std::optional<Primitive> patchPrim(const PatchGrid& g, const std::string& eol) {
    Primitive p = makePatchPrimitive(g, eol, "");
    if (p.lines.empty()) return std::nullopt;
    reparsePrimitive(p);
    if (p.kind != PrimKind::Patch) return std::nullopt;
    return p;
}

// Box for a new object: the selection's bounds, or a grid-sized box at the 2D view centre resting on z = 0.
AABB placementBox(EditorContext& ctx, double size) {
    AABB b = ctx.doc.selectionBounds();
    if (b.valid() && b.maxs.x - b.mins.x > 1 && b.maxs.y - b.mins.y > 1 && b.maxs.z - b.mins.z > 1) return b;
    Vec3 c;
    c[ctx.axisH()] = snapToGrid(ctx.gridCenterH, ctx.doc.grid());
    c[ctx.axisV()] = snapToGrid(ctx.gridCenterV, ctx.doc.grid());
    AABB out;
    out.add(Vec3{c.x - size / 2, c.y - size / 2, 0});
    out.add(Vec3{c.x + size / 2, c.y + size / 2, size});
    return out;
}

bool replaceWithBrushes(EditorContext& ctx, const std::vector<ConvexBrush>& brushes, const std::string& name) {
    Entity* w = ctx.doc.worldspawn();
    if (!w || brushes.empty()) return false;
    std::vector<Document::PrimEdit> edits;
    for (const auto& sb : selectedBrushes(ctx)) edits.push_back({sb.entity, sb.item, std::nullopt});
    for (const auto& b : brushes)
        if (auto p = makeBrushPrimitive(b, ctx.doc.eol())) edits.push_back({w->id, kNoId, *p});
    return ctx.doc.editPrimitives(edits, name);
}

}  // namespace

bool mergeSelectedBrushes(EditorContext& ctx) {
    const auto sel = selectedBrushes(ctx);
    if (sel.size() < 2) return false;
    std::vector<ConvexBrush> in;
    for (const auto& sb : sel) in.push_back(sb.brush);
    const auto merged = mergeBrushes(in);
    if (!merged) return false;
    auto prim = rebuildBrush(*sel.front().source, *merged, ctx.doc.eol());
    if (!prim) return false;
    std::vector<Document::PrimEdit> edits{{sel.front().entity, sel.front().item, *prim}};
    for (size_t i = 1; i < sel.size(); ++i) edits.push_back({sel[i].entity, sel[i].item, std::nullopt});
    return ctx.doc.editPrimitives(edits, "CSG Merge");
}

bool extrudeSelectedFace(EditorContext& ctx, double distance) {
    if (ctx.doc.faceSelection().size() != 1) return false;
    const FaceRef f = *ctx.doc.faceSelection().begin();
    const EntityItem* it = ctx.doc.findItem(f.entity, f.item);
    if (!it || !it->prim()) return false;
    const ConvexBrush cb = brushFromPrimitive(*it->prim());
    if (cb.size() != it->prim()->faces.size() || f.face >= cb.size()) return false;  // degenerate faces: indices differ
    const auto ext = extrudeFace(cb, f.face, distance);
    if (!ext) return false;
    auto p = makeBrushPrimitive(*ext, ctx.doc.eol());
    if (!p) return false;
    if (!ctx.doc.editPrimitives({{f.entity, kNoId, *p}}, "Extrude")) return false;
    return true;
}

bool createPrimitive(EditorContext& ctx, PrimitiveKind kind) {
    const AABB box = placementBox(ctx, std::max(64.0, ctx.doc.grid() * 8));
    const std::string suffix = faceSuffix(ctx);
    std::vector<ConvexBrush> out;
    const char* name = "Create Primitive";
    switch (kind) {
        case PrimitiveKind::Cone: out = {makeCone(box, 8, 2, suffix)}; name = "Create Cone"; break;
        case PrimitiveKind::Sphere: out = makeSphere(box, 8, suffix); name = "Create Sphere"; break;
        case PrimitiveKind::Stairs: out = makeStairs(box, 8, 0, suffix); name = "Create Stairs"; break;
        case PrimitiveKind::Arch:
            out = makeArchBlocks(box, 8, std::max(8.0, ctx.doc.grid()), ctx.axisD() == 2 ? 1 : ctx.axisD(), suffix);
            name = "Create Arch";
            break;
        case PrimitiveKind::Ramp: out = {makeRamp(box, 0, suffix)}; name = "Create Ramp"; break;
    }
    out.erase(std::remove_if(out.begin(), out.end(), [](const ConvexBrush& b) { return !isValidBrush(b); }), out.end());
    return replaceWithBrushes(ctx, out, name);
}

bool importHeightMapToSelectedPatch(EditorContext& ctx, const QString& file, double zMin, double zMax, QString* err) {
    const auto sel = selectedPatchGrids(ctx);
    if (sel.empty()) {
        if (err) *err = "Select a terrain patch first (Patch > Create Terrain makes one)";
        return false;
    }
    HeightMap hm;
    std::string e;
    if (!readHeightMapImage(file.toStdString(), hm, &e)) {
        if (err) *err = QString::fromStdString(e);
        return false;
    }
    PatchGrid g = sel.front().grid;
    if (!applyHeightMap(g, hm, zMin, zMax)) {
        if (err) *err = "The height map does not fit the patch";
        return false;
    }
    auto p = patchPrim(g, ctx.doc.eol());
    return p && ctx.doc.editPrimitives({{sel.front().entity, sel.front().item, *p}}, "Import Height Map");
}

bool exportSelectedPatchHeightMap(EditorContext& ctx, const QString& file, QString* err) {
    const auto sel = selectedPatchGrids(ctx);
    if (sel.empty()) {
        if (err) *err = "Select a terrain patch first";
        return false;
    }
    const HeightMap hm = heightMapFromPatch(sel.front().grid);
    if (hm.z.empty()) {
        if (err) *err = "The patch has no control points";
        return false;
    }
    const auto [lo, hi] = std::minmax_element(hm.z.begin(), hm.z.end());
    std::string e;
    // A flat patch still gets a valid range (it exports as black).
    if (!writeHeightMapPgm(file.toStdString(), hm, *lo, std::max(double(*hi), double(*lo) + 1.0), &e)) {
        if (err) *err = QString::fromStdString(e);
        return false;
    }
    return true;
}

bool erodeSelectedPatches(EditorContext& ctx, bool hydraulic, int iterations) {
    std::vector<Document::PrimEdit> edits;
    for (const auto& sp : selectedPatchGrids(ctx)) {
        HeightMap hm = heightMapFromPatch(sp.grid);
        if (hm.z.empty()) continue;
        const auto [lo, hi] = std::minmax_element(hm.z.begin(), hm.z.end());
        const double zMin = *lo, zMax = *hi;
        if (zMax - zMin < 1e-6) continue;
        // Talus in height per cell: about a 30 degree slope at the patch's control-point spacing.
        const double cell = sp.grid.cols > 1 ? std::max(1.0, length(sp.grid.pos[size_t(sp.grid.cols - 1)] - sp.grid.pos[0]) / (sp.grid.cols - 1)) : 64.0;
        if (hydraulic) hydraulicErosionSimple(hm, iterations, 0.01 * (zMax - zMin), 0.1, 1);
        else thermalErosion(hm, iterations, cell * 0.577);
        PatchGrid g = sp.grid;
        const auto [nlo, nhi] = std::minmax_element(hm.z.begin(), hm.z.end());
        if (!applyHeightMap(g, hm, *nlo, *nhi)) continue;
        if (auto p = patchPrim(g, ctx.doc.eol())) edits.push_back({sp.entity, sp.item, *p});
    }
    return !edits.empty() && ctx.doc.editPrimitives(edits, hydraulic ? "Hydraulic Erosion" : "Thermal Erosion");
}

bool capSelectedPatches(EditorContext& ctx) {
    std::vector<Document::PrimEdit> edits;
    for (const auto& sp : selectedPatchGrids(ctx))
        for (const PatchGrid& cap : capPatch(sp.grid))
            if (auto p = patchPrim(cap, ctx.doc.eol())) edits.push_back({sp.entity, kNoId, *p});
    return ctx.doc.editPrimitives(edits, "Cap Patch");
}

bool weldSelectedPatches(EditorContext& ctx) {
    const auto sel = selectedPatchGrids(ctx);
    if (sel.size() != 2) return false;
    const auto w = weldPatches(sel[0].grid, sel[1].grid);
    if (!w) return false;
    auto p = patchPrim(*w, ctx.doc.eol());
    if (!p) return false;
    return ctx.doc.editPrimitives({{sel[0].entity, sel[0].item, *p}, {sel[1].entity, sel[1].item, std::nullopt}}, "Weld Patches");
}

bool redisperseSelectedPatches(EditorContext& ctx) {
    std::vector<Document::PrimEdit> edits;
    for (const auto& sp : selectedPatchGrids(ctx))
        if (auto p = patchPrim(redisperse(sp.grid, true, true), ctx.doc.eol())) edits.push_back({sp.entity, sp.item, *p});
    return ctx.doc.editPrimitives(edits, "Redisperse Patch");
}

bool splitSelectedPatches(EditorContext& ctx) {
    std::vector<Document::PrimEdit> edits;
    for (const auto& sp : selectedPatchGrids(ctx)) {
        const bool rows = sp.grid.rows >= sp.grid.cols;
        const int n = rows ? sp.grid.rows : sp.grid.cols;
        int at = n / 2;
        if (at % 2 == 1) ++at;  // split at an even (shared) control index; n is odd
        const auto parts = splitPatch(sp.grid, rows, std::min(at, n - 3));
        if (parts.size() != 2) continue;
        edits.push_back({sp.entity, sp.item, std::nullopt});
        for (const auto& g : parts)
            if (auto p = patchPrim(g, ctx.doc.eol())) edits.push_back({sp.entity, kNoId, *p});
    }
    return ctx.doc.editPrimitives(edits, "Split Patch");
}

bool createTerrain(EditorContext& ctx) {
    Entity* w = ctx.doc.worldspawn();
    if (!w) return false;
    const AABB box = placementBox(ctx, 512);
    std::vector<Document::PrimEdit> edits;
    for (const auto& sb : selectedBrushes(ctx)) edits.push_back({sb.entity, sb.item, std::nullopt});
    auto p = patchPrim(makeTerrainPatch(box, 9, 9, ctx.currentMaterial), ctx.doc.eol());
    if (!p) return false;
    edits.push_back({w->id, kNoId, *p});
    return ctx.doc.editPrimitives(edits, "Create Terrain");
}

bool noiseSelectedPatches(EditorContext& ctx, double amplitude, uint32_t seed) {
    std::vector<Document::PrimEdit> edits;
    for (const auto& sp : selectedPatchGrids(ctx))
        if (auto p = patchPrim(applyNoise(sp.grid, amplitude, seed), ctx.doc.eol())) edits.push_back({sp.entity, sp.item, *p});
    return ctx.doc.editPrimitives(edits, "Patch Noise");
}

bool smoothSelectedPatches(EditorContext& ctx, int iterations) {
    std::vector<Document::PrimEdit> edits;
    for (const auto& sp : selectedPatchGrids(ctx))
        if (auto p = patchPrim(smoothPatch(sp.grid, iterations), ctx.doc.eol())) edits.push_back({sp.entity, sp.item, *p});
    return ctx.doc.editPrimitives(edits, "Smooth Patch");
}

bool createRope(EditorContext& ctx) {
    std::vector<Vec3> pts;
    for (const auto& r : ctx.doc.selection()) {
        if (r.item == kNoId) {
            if (const Entity* e = ctx.doc.findEntity(r.entity))
                if (auto o = e->origin()) pts.push_back(*o);
        } else if (const EntityItem* it = ctx.doc.findItem(r.entity, r.item); it && it->prim()) {
            pts.push_back(it->prim()->bounds().center());
        }
        if (pts.size() == 2) break;
    }
    Entity* w = ctx.doc.worldspawn();
    if (pts.size() < 2 || !w) return false;
    const double span = length(pts[1] - pts[0]);
    auto p = patchPrim(ropePatch(pts[0], pts[1], std::max(8.0, span * 0.08), 2.0, 8, ctx.currentMaterial), ctx.doc.eol());
    if (!p) return false;
    return ctx.doc.editPrimitives({{w->id, kNoId, *p}}, "Create Rope");
}

size_t scatterModels(EditorContext& ctx, const QString& model, int count, double radius, uint32_t seed) {
    if (model.isEmpty() || count <= 0) return 0;
    Vec3 c;
    c[ctx.axisH()] = ctx.gridCenterH;
    c[ctx.axisV()] = ctx.gridCenterV;
    ScatterOptions opt;
    opt.radius = radius;
    opt.count = count;
    opt.seed = seed;
    opt.minSpacing = 16;
    const auto places = scatterOnBrushes(ctx.doc.map(), c, opt);
    if (places.empty()) return 0;
    ctx.doc.beginGroup("Scatter Models");
    for (const auto& pl : places) {
        // Decor is a static model (misc_model), as in stock maps (doc 34).
        std::vector<std::pair<std::string, std::string>> kv = {{"classname", "misc_model"}, {"model", model.toStdString()},
                                                               {"origin", vec(pl.origin)}, {"angles", vec(pl.angles)}};
        if (std::fabs(pl.scale - 1.0) > 1e-3) kv.push_back({"modelscale", formatNumber(pl.scale)});
        ctx.doc.addEntity(kv, {}, "Scatter Models");
    }
    ctx.doc.endGroup();
    return places.size();
}

size_t paintModelsAt(EditorContext& ctx, const QString& model, const Vec3& centre, int count, double radius, uint32_t seed) {
    if (model.isEmpty() || count <= 0) return 0;
    ScatterOptions opt;
    opt.radius = std::max(1.0, radius);
    opt.count = count;
    opt.seed = seed;
    opt.minSpacing = std::min(32.0, radius / 4);
    // Only surfaces near the click: the scatter picks brush tops in the XY circle; keep those within reach vertically.
    auto places = scatterOnBrushes(ctx.doc.map(), centre, opt);
    std::erase_if(places, [&](const auto& pl) { return std::fabs(pl.origin.z - centre.z) > std::max(64.0, radius); });
    if (places.empty()) return 0;
    ctx.doc.beginGroup("Paint Models");
    for (const auto& pl : places) {
        std::vector<std::pair<std::string, std::string>> kv = {{"classname", "misc_model"}, {"model", model.toStdString()},
                                                               {"origin", vec(pl.origin)}, {"angles", vec(pl.angles)}};
        if (std::fabs(pl.scale - 1.0) > 1e-3) kv.push_back({"modelscale", formatNumber(pl.scale)});
        ctx.doc.addEntity(kv, {}, "Paint Models");
    }
    ctx.doc.endGroup();
    return places.size();
}

size_t dropSelectionToFloor(EditorContext& ctx) {
    std::vector<std::pair<Id, Vec3>> moves;
    for (const auto& r : ctx.doc.selection()) {
        if (r.item != kNoId) continue;
        const Entity* e = ctx.doc.findEntity(r.entity);
        if (!e) continue;
        const auto o = e->origin();
        if (!o) continue;
        if (auto f = dropToFloor(ctx.doc.map(), *o + Vec3{0, 0, 1}); f && std::fabs(f->z - o->z) > 1e-6) moves.push_back({r.entity, *f});
    }
    if (moves.empty()) return 0;
    ctx.doc.beginGroup("Drop To Floor");
    for (const auto& [id, at] : moves) ctx.doc.setKeyValue(id, "origin", vec(at));
    ctx.doc.endGroup();
    return moves.size();
}

}  // namespace iw8r::qt
