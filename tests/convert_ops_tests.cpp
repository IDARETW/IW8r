// Brush -> patch and patch -> terrain conversions (src/core/convert_ops.cpp).
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "compile_iw8.h"
#include "convert_ops.h"
#include "document.h"
#include "geometry.h"
#include "map_text.h"
#include "patch_ops.h"
#include "patch_ops2.h"
#include "patch_tess.h"
#include "tex_projection.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

constexpr double kEps = 1e-6;
// Non-trivial texture fields: width 64, height 32, offsets 10 and 5, rotation 30.
const char* kSuffix = " ch_tile_floor05 64 32 10 5 30 0 0";
const char* kToolSuffix = " caulk 64 64 0 0 0 0";

AABB boxOf(const Vec3& mn, const Vec3& mx) {
    AABB b;
    b.mins = mn;
    b.maxs = mx;
    return b;
}

// Parses one primitive text inside a worldspawn, the same route the editor's files take.
std::optional<Primitive> parsePrim(const std::string& primText) {
    const std::string text = "iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n" + primText + "}\n";
    Id next = 1;
    const ParseResult r = parseMap(text, next);
    if (!r.map || r.map->entities.empty()) return std::nullopt;
    for (const auto& it : r.map->entities[0].items)
        if (it.isPrimitive()) return *it.prim();
    return std::nullopt;
}

Primitive brushPrim(const ConvexBrush& b) {
    const auto p = makeBrushPrimitive(b, "\n", "");
    if (!p) return Primitive{};
    const auto parsed = parsePrim(p->text());
    return parsed ? *parsed : Primitive{};
}

Primitive patchPrimOf(const PatchGrid& g) {
    const auto parsed = parsePrim(makePatchPrimitive(g, "\n", "").text());
    return parsed ? *parsed : Primitive{};
}

// Area of the mesh triangles, the number of zero-area triangles, and whether every other triangle faces `normal`.
struct MeshStats {
    double area = 0;
    int degenerate = 0;
    bool outward = true;
};

MeshStats meshStats(const PatchGrid& g, const Vec3& normal) {
    MeshStats s;
    const PatchMesh m = tessellatePatch(g.pos, g.rows, g.cols, g.keyword, 2);
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const Vec3 a = m.points[m.indices[i]], b = m.points[m.indices[i + 1]], c = m.points[m.indices[i + 2]];
        const Vec3 cr = cross(b - a, c - a);
        if (0.5 * length(cr) < 1e-9) {
            ++s.degenerate;
            continue;
        }
        s.area += 0.5 * length(cr);
        if (dot(cr, normal) <= 0) s.outward = false;
    }
    return s;
}

// Index of the brush face whose plane holds every control point of g (-1 when none does).
int faceOfPatch(const Primitive& brush, const PatchGrid& g) {
    for (size_t f = 0; f < brush.faces.size(); ++f) {
        const auto pl = facePlane(brush, f);
        if (!pl) continue;
        bool on = true;
        for (const Vec3& p : g.pos)
            if (std::fabs(dot(pl->n, p) - pl->d) > kEps) on = false;
        if (on) return static_cast<int>(f);
    }
    return -1;
}

double faceAreaOf(const Winding& w) {
    double area = 0;
    for (size_t k = 1; k + 1 < w.size(); ++k) area += 0.5 * length(cross(w[k] - w[0], w[k + 1] - w[0]));
    return area;
}

// Puts primitives into a worldspawn of a fresh document.
bool loadWorld(Document& doc, const std::vector<Primitive>& prims) {
    std::string text = "iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n";
    for (const auto& p : prims) text += p.text();
    text += "}\n";
    return doc.loadFromText(text, "convert_ops_test.map");
}

size_t countItems(Document& doc, bool patches) {
    const Entity* w = doc.findEntity(doc.worldspawn() ? doc.worldspawn()->id : kNoId);
    if (!w) return 0;
    size_t n = 0;
    for (const auto& it : w->items) {
        const Primitive* p = it.prim();
        if (p && (p->kind == PrimKind::Patch) == patches) ++n;
    }
    return n;
}

// First brush or patch of the worldspawn. Key/value lines are items too, so items[0] is not necessarily a primitive.
std::optional<SelRef> firstPrimRef(Document& doc) {
    Entity* w = doc.worldspawn();
    if (!w) return std::nullopt;
    for (const auto& it : w->items)
        if (it.isPrimitive()) return SelRef{w->id, it.id};
    return std::nullopt;
}

bool sameBounds(const PatchGrid& a, const PatchGrid& b) {
    AABB ba, bb;
    for (const Vec3& p : a.pos) ba.add(p);
    for (const Vec3& p : b.pos) bb.add(p);
    return length(ba.mins - bb.mins) < kEps && length(ba.maxs - bb.maxs) < kEps;
}

}  // namespace

TEST(convert_ops_cube_six_patches_cover_faces_once) {
    const Primitive cube = brushPrim(makeBox(boxOf({0, 0, 0}, {64, 64, 64}), kSuffix));
    const auto ws = faceWindingsOf(cube);
    const auto patches = brushFacesToPatches(cube, true, 3);
    CHECK(patches.size() == 6);

    std::vector<double> areaByFace(ws.size(), 0.0);
    std::vector<int> patchesByFace(ws.size(), 0);
    double total = 0;
    for (const PatchGrid& g : patches) {
        const int f = faceOfPatch(cube, g);
        CHECK(f >= 0);
        if (f < 0) continue;
        const Vec3 n = normalize(facePlane(cube, static_cast<size_t>(f))->n);
        const MeshStats s = meshStats(g, n);
        CHECK(s.outward);
        CHECK(s.degenerate == 0);  // quads have no zero-area triangles
        areaByFace[static_cast<size_t>(f)] += s.area;
        patchesByFace[static_cast<size_t>(f)]++;
        total += s.area;
    }
    for (size_t f = 0; f < ws.size(); ++f) {
        CHECK(patchesByFace[f] == 1);
        CHECK(std::fabs(areaByFace[f] - faceAreaOf(ws[f])) < 1e-6 * faceAreaOf(ws[f]));
    }
    CHECK(std::fabs(total - 6.0 * 64.0 * 64.0) < 1e-6);
}

TEST(convert_ops_uv_matches_brush_faces) {
    const Primitive cube = brushPrim(makeBox(boxOf({0, 0, 0}, {64, 64, 64}), kSuffix));
    const auto ws = faceWindingsOf(cube);
    for (const PatchGrid& g : brushFacesToPatches(cube, true, 3)) {
        const int f = faceOfPatch(cube, g);
        CHECK(f >= 0);
        if (f < 0) continue;
        const Vec3 n = normalize(facePlane(cube, static_cast<size_t>(f))->n);
        const TexParams tp = texParamsOfFace(cube, static_cast<size_t>(f)).value_or(TexParams{});
        // Every control point carries the brush face's UV at that point.
        for (size_t i = 0; i < g.pos.size(); ++i) {
            const auto uv = faceUV(n, tp, g.pos[i]);
            CHECK(std::fabs(g.st[i][0] - uv[0]) < kEps && std::fabs(g.st[i][1] - uv[1]) < kEps);
        }
        // The four grid corners equal the face's winding corners with the brush UVs at those corners.
        const Winding& w = ws[static_cast<size_t>(f)];
        for (const Vec3& corner : w) {
            bool found = false;
            for (size_t i = 0; i < g.pos.size(); ++i) {
                if (length(g.pos[i] - corner) > kEps) continue;
                const auto uv = faceUV(n, tp, corner);
                found = std::fabs(g.st[i][0] - uv[0]) < kEps && std::fabs(g.st[i][1] - uv[1]) < kEps;
            }
            CHECK(found);
        }
    }
}

TEST(convert_ops_skips_tool_faces) {
    ConvexBrush b = makeBox(boxOf({0, 0, 0}, {64, 64, 64}), kSuffix);
    b[0].suffix = kToolSuffix;
    const Primitive p = brushPrim(b);
    CHECK(brushFacesToPatches(p, true, 2).size() == 5);
    CHECK(brushFacesToPatches(p, false, 2).size() == 6);
    for (const PatchGrid& g : brushFacesToPatches(p, true, 2)) CHECK(!isIw8ToolMaterial(g.material));
}

TEST(convert_ops_triangle_prism_faces_and_degenerate_count) {
    // Triangular prism: two triangular caps and three quad sides.
    const Primitive prism = brushPrim(makePrism(boxOf({0, 0, 0}, {64, 64, 64}), 3, 2, kSuffix));
    CHECK(prism.faces.size() == 5);
    const auto ws = faceWindingsOf(prism);
    const auto patches = brushFacesToPatches(prism, true, 3);
    CHECK(patches.size() == 5);

    std::vector<double> areaByFace(ws.size(), 0.0);
    int degenerate = 0;
    for (const PatchGrid& g : patches) {
        const int f = faceOfPatch(prism, g);
        CHECK(f >= 0);
        if (f < 0) continue;
        const Vec3 n = normalize(facePlane(prism, static_cast<size_t>(f))->n);
        const MeshStats s = meshStats(g, n);
        CHECK(s.outward);
        areaByFace[static_cast<size_t>(f)] += s.area;
        degenerate += s.degenerate;
    }
    for (size_t f = 0; f < ws.size(); ++f) CHECK(std::fabs(areaByFace[f] - faceAreaOf(ws[f])) < 1e-6 * faceAreaOf(ws[f]));
    // Each triangle patch with subdiv 3 has 2 zero-area triangles (its collapsed last row); the quads have none.
    CHECK(degenerate == 2 * 2);
}

TEST(convert_ops_hexagon_fans_into_triangle_patches) {
    // Hexagonal prism: two hexagonal caps (each fanned into 4 triangle patches) and six quad sides.
    const Primitive prism = brushPrim(makePrism(boxOf({0, 0, 0}, {64, 64, 64}), 6, 2, kSuffix));
    const auto ws = faceWindingsOf(prism);
    const auto patches = brushFacesToPatches(prism, true, 2);
    CHECK(patches.size() == 2 * 4 + 6);

    std::vector<double> areaByFace(ws.size(), 0.0);
    for (const PatchGrid& g : patches) {
        const int f = faceOfPatch(prism, g);
        CHECK(f >= 0);
        if (f < 0) continue;
        const Vec3 n = normalize(facePlane(prism, static_cast<size_t>(f))->n);
        const MeshStats s = meshStats(g, n);
        CHECK(s.outward);
        areaByFace[static_cast<size_t>(f)] += s.area;
    }
    for (size_t f = 0; f < ws.size(); ++f) CHECK(std::fabs(areaByFace[f] - faceAreaOf(ws[f])) < 1e-6 * faceAreaOf(ws[f]));
}

TEST(convert_ops_brush_to_patch_one_undo_step) {
    const Primitive cube = brushPrim(makeBox(boxOf({0, 0, 0}, {64, 64, 64}), kSuffix));
    Document doc;
    CHECK(loadWorld(doc, {cube}));
    const auto ref = firstPrimRef(doc);
    CHECK(ref.has_value() && countItems(doc, false) == 1);
    if (!ref) return;
    doc.select(*ref, false);

    const std::string before = doc.text();
    const size_t historyBefore = doc.history().size();
    CHECK(convertSelectedBrushesToPatches(doc, false, true));
    CHECK(doc.history().size() == historyBefore + 1);
    CHECK(countItems(doc, false) == 0);  // brush removed
    CHECK(countItems(doc, true) == 6);
    CHECK(doc.selection().size() == 6);  // the new patches are the selection
    for (const SelRef& r : doc.selection()) {
        const EntityItem* it = doc.findItem(r.entity, r.item);
        CHECK(it != nullptr && it->prim() != nullptr && it->prim()->kind == PrimKind::Patch);
    }

    CHECK(doc.undo());
    CHECK(doc.text() == before);
}

TEST(convert_ops_keep_brush_and_all_tool_brush_untouched) {
    const Primitive cube = brushPrim(makeBox(boxOf({0, 0, 0}, {64, 64, 64}), kSuffix));
    Document doc;
    CHECK(loadWorld(doc, {cube}));
    const auto ref = firstPrimRef(doc);
    if (!ref) return;
    doc.select(*ref, false);
    CHECK(convertSelectedBrushesToPatches(doc, true, true));
    CHECK(countItems(doc, false) == 1);  // kept
    CHECK(countItems(doc, true) == 6);

    // A brush whose faces are all tool materials has nothing to show: it stays and nothing changes.
    ConvexBrush tools = makeBox(boxOf({0, 0, 0}, {64, 64, 64}), kToolSuffix);
    Document doc2;
    CHECK(loadWorld(doc2, {brushPrim(tools)}));
    const auto ref2 = firstPrimRef(doc2);
    if (!ref2) return;
    doc2.select(*ref2, false);
    const std::string before = doc2.text();
    CHECK(!convertSelectedBrushesToPatches(doc2, false, true));
    CHECK(doc2.text() == before);
}

TEST(convert_ops_invalid_inputs_give_nothing) {
    const Primitive cube = brushPrim(makeBox(boxOf({0, 0, 0}, {64, 64, 64}), kSuffix));
    // subdiv below 2 is treated as 2: the faces are still one 2 x 2 patch each.
    const auto small = brushFacesToPatches(cube, false, 1);
    CHECK(small.size() == 6);
    for (const PatchGrid& g : small) CHECK(g.rows == 2 && g.cols == 2);

    Primitive notBrush = patchPrimOf(makeFlatPatch(boxOf({0, 0, 0}, {64, 64, 0}), 2, 3, 3, "mat"));
    CHECK(brushFacesToPatches(notBrush, true, 2).empty());

    const PatchGrid flat = makeFlatPatch(boxOf({0, 0, 64}, {256, 256, 64}), 2, 3, 3, "ch_tile_floor05");
    CHECK(!patchToTerrain(flat, 1, 9).has_value());
    CHECK(!patchToTerrain(flat, 9, 1).has_value());
}

TEST(convert_ops_patch_to_terrain_size_and_bounds) {
    const PatchGrid flat = makeFlatPatch(boxOf({0, 0, 64}, {256, 256, 64}), 2, 3, 3, "ch_tile_floor05");
    const auto square = patchToTerrain(flat, 9, 9);
    CHECK(square.has_value());
    if (!square) return;
    CHECK(square->rows == 9 && square->cols == 9);
    CHECK(square->pos.size() == 81 && square->st.size() == 81);
    CHECK(square->keyword == "mesh");
    CHECK(square->material == "ch_tile_floor05");
    CHECK(sameBounds(flat, *square));
    // Corners keep their texture coordinates (flat patch: 256 / 512 = 0.5 at the far corner).
    CHECK(std::fabs(square->st[80][0] - 0.5) < kEps && std::fabs(square->st[80][1] - 0.5) < kEps);

    const auto rect = patchToTerrain(flat, 5, 9);  // cols 5, rows 9
    CHECK(rect.has_value());
    if (rect) {
        CHECK(rect->rows == 9 && rect->cols == 5);
        CHECK(rect->pos.size() == 45);
        CHECK(sameBounds(flat, *rect));
    }
}

TEST(convert_ops_patch_to_terrain_one_undo_step) {
    const PatchGrid flat = makeFlatPatch(boxOf({0, 0, 64}, {256, 256, 64}), 2, 3, 3, "ch_tile_floor05");
    Document doc;
    CHECK(loadWorld(doc, {patchPrimOf(flat)}));
    const auto ref = firstPrimRef(doc);
    if (!ref) return;
    doc.select(*ref, false);

    const std::string before = doc.text();
    const size_t historyBefore = doc.history().size();
    CHECK(convertSelectedPatchesToTerrain(doc, 9, 9));
    CHECK(doc.history().size() == historyBefore + 1);
    CHECK(countItems(doc, true) == 1);
    CHECK(doc.selection().size() == 1);
    for (const SelRef& r : doc.selection()) {
        const EntityItem* it = doc.findItem(r.entity, r.item);
        const Primitive* p = it ? it->prim() : nullptr;
        CHECK(p != nullptr);
        if (!p) continue;
        const auto g = patchGrid(*p);
        CHECK(g.has_value() && g->rows == 9 && g->cols == 9);
    }
    CHECK(doc.undo());
    CHECK(doc.text() == before);
}

// ---- Paint meshes on brush faces (blend painting, ROADMAP 5.9 / 8.11)

TEST(convert_ops_paint_mesh_density_and_uv) {
    // A floor slab 256 x 128 x 16: the top face gets a control point every 32 units.
    const Primitive slab = brushPrim(makeBox(boxOf({0, 0, 0}, {256, 128, 16}), kSuffix));
    const auto ws = faceWindingsOf(slab);
    size_t top = ws.size();
    for (size_t f = 0; f < ws.size(); ++f)
        if (const auto pl = facePlane(slab, f); pl && normalize(pl->n).z > 0.99) top = f;
    CHECK(top < ws.size());
    if (top >= ws.size()) return;
    const auto meshes = brushFacePaintMeshes(slab, top, 32.0, true);
    CHECK(meshes.size() == 1);
    if (meshes.size() != 1) return;
    const PatchGrid& g = meshes[0];
    CHECK(std::max(g.cols, g.rows) == 9 && std::min(g.cols, g.rows) == 5);  // 256 / 32 + 1 and 128 / 32 + 1
    CHECK(g.material == "ch_tile_floor05" && g.keyword == "mesh");
    const Vec3 n = normalize(facePlane(slab, top)->n);
    const MeshStats s = meshStats(g, n);
    CHECK(s.outward && s.degenerate == 0);
    CHECK(std::fabs(s.area - 256.0 * 128.0) < 1e-6 * 256.0 * 128.0);
    // Every control point lies on the face and carries the face's own texture coordinates.
    const TexParams tp = texParamsOfFace(slab, top).value_or(TexParams{});
    bool onFace = true, sameUv = true;
    for (size_t i = 0; i < g.pos.size(); ++i) {
        onFace = onFace && std::fabs(g.pos[i].z - 16.0) < 1e-9;
        const auto uv = faceUV(n, tp, g.pos[i]);
        sameUv = sameUv && std::fabs(uv[0] - g.st[i][0]) < 1e-6 && std::fabs(uv[1] - g.st[i][1]) < 1e-6;
    }
    CHECK(onFace && sameUv);
    // The spacing rounds up to whole cells and is limited to 65 points a side; no spacing gives the corners.
    const auto fine = brushFacePaintMeshes(slab, top, 1.0, true);
    CHECK(fine.size() == 1 && fine[0].cols == 65 && fine[0].rows == 65);
    const auto odd = brushFacePaintMeshes(slab, top, 100.0, true);
    CHECK(odd.size() == 1 && std::max(odd[0].cols, odd[0].rows) == 4 && std::min(odd[0].cols, odd[0].rows) == 3);
    const auto corners = brushFacePaintMeshes(slab, top, 0.0, true);
    CHECK(corners.size() == 1 && corners[0].cols == 2 && corners[0].rows == 2);
    // A face that is not there, and a tool face when tool faces are skipped: nothing.
    CHECK(brushFacePaintMeshes(slab, 99, 32.0, true).empty());
    const Primitive caulked = brushPrim(makeBox(boxOf({0, 0, 0}, {64, 64, 64}), kToolSuffix));
    CHECK(brushFacePaintMeshes(caulked, 0, 32.0, true).empty());
    CHECK(brushFacePaintMeshes(caulked, 0, 32.0, false).size() == 1);
}

TEST(convert_ops_paint_mesh_on_selected_faces_one_undo_step) {
    const Primitive slab = brushPrim(makeBox(boxOf({0, 0, 0}, {256, 128, 16}), kSuffix));
    Document doc;
    CHECK(loadWorld(doc, {slab}));
    const auto ref = firstPrimRef(doc);
    CHECK(ref.has_value());
    if (!ref) return;
    const Primitive* brush = doc.findItem(ref->entity, ref->item)->prim();
    uint32_t top = 0;
    for (size_t f = 0; f < brush->faces.size(); ++f)
        if (const auto pl = facePlane(*brush, f); pl && normalize(pl->n).z > 0.99) top = uint32_t(f);
    doc.selectFace(FaceRef{ref->entity, ref->item, top}, false);

    const std::string before = doc.text();
    const size_t historyBefore = doc.history().size();
    CHECK(addPaintMeshesToBrushFaces(doc, 32.0) == 1);
    CHECK(doc.history().size() == historyBefore + 1);
    CHECK(countItems(doc, false) == 1 && countItems(doc, true) == 1);  // the brush stays, one patch on its top
    // The covered face is caulk now, the others keep their material, and the brush has not moved.
    const Primitive* after = doc.findItem(ref->entity, ref->item) ? doc.findItem(ref->entity, ref->item)->prim() : nullptr;
    CHECK(after != nullptr);
    if (!after) return;
    size_t caulk = 0, tile = 0;
    for (size_t f = 0; f < after->faces.size(); ++f) {
        if (after->faces[f].material() == "caulk") {
            ++caulk;
            CHECK(f == top);
        }
        if (after->faces[f].material() == "ch_tile_floor05") ++tile;
    }
    CHECK(caulk == 1 && tile == after->faces.size() - 1);
    CHECK(faceWindingsOf(*after).size() == faceWindingsOf(slab).size());
    // The patch is the selection (it is what gets painted) and no face is selected any more.
    CHECK(doc.selection().size() == 1 && doc.faceSelection().empty());
    for (const SelRef& r : doc.selection()) {
        const EntityItem* it = doc.findItem(r.entity, r.item);
        CHECK(it && it->prim() && it->prim()->kind == PrimKind::Patch);
    }
    CHECK(doc.undo());
    CHECK(doc.text() == before);
}

TEST(convert_ops_paint_mesh_on_whole_brushes_skips_tool_faces) {
    // No face selection: every drawn face of the selected brush is covered; a brush of tool faces is left alone.
    const Primitive cube = brushPrim(makeBox(boxOf({0, 0, 0}, {64, 64, 64}), kSuffix));
    const Primitive tool = brushPrim(makeBox(boxOf({128, 0, 0}, {192, 64, 64}), kToolSuffix));
    Document doc;
    CHECK(loadWorld(doc, {cube, tool}));
    Entity* w = doc.worldspawn();
    CHECK(w != nullptr);
    if (!w) return;
    bool first = true;
    for (const auto& it : w->items)
        if (it.isPrimitive()) doc.select(SelRef{w->id, it.id}, !first), first = false;
    CHECK(doc.selection().size() == 2);
    const std::string before = doc.text();
    CHECK(addPaintMeshesToBrushFaces(doc, 32.0) == 6);
    CHECK(countItems(doc, false) == 2 && countItems(doc, true) == 6);
    CHECK(doc.selection().size() == 6);
    // Nothing selected, or only patches: nothing happens and nothing is recorded.
    const size_t history = doc.history().size();
    CHECK(addPaintMeshesToBrushFaces(doc, 32.0) == 0);
    doc.deselectAll();
    CHECK(addPaintMeshesToBrushFaces(doc, 32.0) == 0);
    CHECK(doc.history().size() == history);
    CHECK(doc.undo());
    CHECK(doc.text() == before);
}
