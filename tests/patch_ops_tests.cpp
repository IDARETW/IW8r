// Patch and brush-vertex editing tests (src/core/patch_ops.cpp).
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "map_text.h"
#include "patch_ops.h"
#include "patch_tess.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

constexpr double kEps = 1e-6;

// Puts a primitive text into a worldspawn and reads it back.
std::optional<Primitive> parseOnePrim(const std::string& primText) {
    const std::string text = "iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n" + primText + "}\n";
    Id next = 1;
    const ParseResult r = parseMap(text, next);
    if (!r.map || r.map->entities.empty()) return std::nullopt;
    for (const auto& it : r.map->entities[0].items)
        if (it.isPrimitive()) return *it.prim();
    return std::nullopt;
}

Primitive patchPrim(const PatchGrid& g) {
    auto p = parseOnePrim(makePatchPrimitive(g, "\n", "// patch").text());
    return p ? *p : Primitive{};
}

bool samePos(const PatchGrid& a, const PatchGrid& b) {
    if (a.rows != b.rows || a.cols != b.cols || a.pos.size() != b.pos.size() || a.st.size() != b.st.size())
        return false;
    for (size_t i = 0; i < a.pos.size(); ++i) {
        if (length(a.pos[i] - b.pos[i]) > kEps) return false;
        if (std::fabs(a.st[i][0] - b.st[i][0]) > kEps || std::fabs(a.st[i][1] - b.st[i][1]) > kEps) return false;
    }
    return true;
}

bool finiteUnit(const PatchMesh& m) {
    for (const Vec3& n : m.normals) {
        if (!std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z)) return false;
        if (std::fabs(length(n) - 1.0) > 1e-6) return false;
    }
    return !m.points.empty() && !m.indices.empty();
}

bool insideBox(const PatchGrid& g, const AABB& box) {
    for (const Vec3& p : g.pos)
        for (int k = 0; k < 3; ++k)
            if (p[k] < box.mins[k] - kEps || p[k] > box.maxs[k] + kEps) return false;
    return true;
}

// Wraps a patch grid in a primitive, parses it, checks dimensions, and tessellates it.
void checkGenerator(const PatchGrid& g, int rows, int cols, const char* keyword) {
    const Primitive p = patchPrim(g);
    CHECK(p.kind == PrimKind::Patch);
    CHECK(p.patchKeyword == keyword);
    const auto back = patchGrid(p);
    CHECK(back.has_value());
    if (!back) return;
    CHECK(back->rows == rows && back->cols == cols);
    CHECK(samePos(*back, g));
    const PatchMesh m = tessellatePrimitive(p, 3);
    CHECK(finiteUnit(m));
}

}  // namespace

TEST(patch_ops_flat_generator_all_axes) {
    const AABB box = [] { AABB b; b.add({0, 0, 0}); b.add({64, 128, 32}); return b; }();
    for (int axis = 0; axis < 3; ++axis) {
        const PatchGrid g = makeFlatPatch(box, axis, 3, 4, "tm/floor");
        checkGenerator(g, 3, 4, "mesh");
        CHECK(insideBox(g, box));
        // Every point lies on the box's centre plane for the normal axis.
        for (const Vec3& p : g.pos) CHECK(std::fabs(p[axis] - box.center()[axis]) < kEps);
    }
}

TEST(patch_ops_flat_natural_matches_fit) {
    // A flat grid has equal edge lengths, so the natural rule gives the same st as the fit rule.
    AABB box;
    box.add({0, 0, 0});
    box.add({512, 256, 0});
    const PatchGrid g = makeFlatPatch(box, 2, 3, 3, "m");
    CHECK(std::fabs(g.st.back()[0] - 1.0) < kEps);
    CHECK(std::fabs(g.st.back()[1] - 0.5) < kEps);
    CHECK(samePos(naturalTexture(g), g));
}

TEST(patch_ops_curved_generators_inside_box) {
    AABB box;
    box.add({-64, -32, 0});
    box.add({64, 32, 128});

    const PatchGrid cyl = makeCylinderPatch(box, "cyl");
    checkGenerator(cyl, 3, 9, "curve");
    CHECK(insideBox(cyl, box));

    const PatchGrid bev = makeBevelPatch(box, "bev");
    checkGenerator(bev, 3, 3, "curve");
    CHECK(insideBox(bev, box));

    const PatchGrid end = makeEndcapPatch(box, "end");
    checkGenerator(end, 3, 5, "curve");
    CHECK(insideBox(end, box));
}

TEST(patch_ops_cylinder_ring_on_box_edges) {
    // Even columns touch the box's ellipse extremes (+x, +y, -x, -y); the odd columns are the box corners.
    AABB box;
    box.add({-10, -20, 0});
    box.add({10, 20, 40});
    const PatchGrid cyl = makeCylinderPatch(box, "cyl");
    // Middle row (z = 20), columns 0 (+x), 1 (+x+y corner), 2 (+y).
    const Vec3 c0 = cyl.pos[1 * 9 + 0], c1 = cyl.pos[1 * 9 + 1], c2 = cyl.pos[1 * 9 + 2];
    CHECK(std::fabs(c0.x - 10) < kEps && std::fabs(c0.y) < kEps);
    CHECK(std::fabs(c1.x - 10) < kEps && std::fabs(c1.y - 20) < kEps);
    CHECK(std::fabs(c2.x) < kEps && std::fabs(c2.y - 20) < kEps);
    CHECK(std::fabs(c0.z - 20) < kEps);
}

TEST(patch_ops_invert_flips_normals) {
    AABB box;
    box.add({0, 0, 0});
    box.add({64, 64, 0});
    const Primitive p1 = patchPrim(makeFlatPatch(box, 2, 3, 3, "m"));
    const Primitive p2 = patchPrim(invertPatch(makeFlatPatch(box, 2, 3, 3, "m")));
    const PatchMesh m1 = tessellatePrimitive(p1, 1), m2 = tessellatePrimitive(p2, 1);
    CHECK(m1.normals.size() == m2.normals.size() && !m1.normals.empty());
    for (size_t i = 0; i < m1.normals.size() && i < m2.normals.size(); ++i) {
        CHECK(dot(m1.normals[i], m1.normals[i]) > 0.999);
        CHECK(dot(m1.normals[i], m2.normals[i]) < -0.999);
    }
}

TEST(patch_ops_transpose_twice_identity) {
    const PatchGrid cyl = makeCylinderPatch(AABB{{-1, -1, -1}, {1, 1, 1}}, "cyl");
    const PatchGrid t = transposePatch(cyl);
    CHECK(t.rows == 9 && t.cols == 3);
    CHECK(samePos(transposePatch(t), cyl));
    CHECK(samePos(transposePatch(transposePatch(cyl)), cyl));
}

TEST(patch_ops_insert_remove_identity) {
    AABB box;
    box.add({0, 0, 0});
    box.add({64, 64, 0});
    const PatchGrid g = makeFlatPatch(box, 2, 3, 5, "m");

    // Rows: insert around row 1, the kept row lands at index 2, so remove around 2.
    const PatchGrid ir = insertRowCol(g, true, 1);
    CHECK(ir.rows == 5 && ir.cols == 5);
    CHECK(samePos(removeRowCol(ir, true, 2), g));

    // Columns: insert around column 2, kept at index 3.
    const PatchGrid ic = insertRowCol(g, false, 2);
    CHECK(ic.rows == 3 && ic.cols == 7);
    CHECK(samePos(removeRowCol(ic, false, 3), g));

    // The inserted rows are midpoints of their neighbours.
    CHECK(length(ir.pos[1 * 5 + 0] - (g.pos[0] + g.pos[5]) * 0.5) < kEps);
    CHECK(length(ir.pos[3 * 5 + 0] - (g.pos[5] + g.pos[10]) * 0.5) < kEps);
}

TEST(patch_ops_insert_keeps_odd_counts) {
    const PatchGrid cyl = makeCylinderPatch(AABB{{-1, -1, -1}, {1, 1, 1}}, "cyl");
    const PatchGrid more = insertRowCol(cyl, true, 1);
    CHECK(more.rows == 5 && more.rows % 2 == 1);
    checkGenerator(more, 5, 9, "curve");
    CHECK(insertRowCol(cyl, true, 0).rows == 3);  // out of range: unchanged
    CHECK(removeRowCol(cyl, true, 1).rows == 3);  // would leave fewer than 3 rows: unchanged
}

TEST(patch_ops_thicken_offsets_and_seams) {
    AABB box;
    box.add({0, 0, 0});
    box.add({64, 64, 0});
    const PatchGrid g = makeFlatPatch(box, 2, 3, 3, "m");
    std::vector<PatchGrid> sides;
    const PatchGrid t = thickenPatch(g, 4, &sides);
    CHECK(t.pos.size() == g.pos.size());
    for (size_t i = 0; i < t.pos.size(); ++i) CHECK(std::fabs(t.pos[i].z - 4.0) < kEps);
    CHECK(sides.size() == 4);
    for (const auto& s : sides) {
        CHECK(s.rows == 2 && s.cols == 3);
        CHECK(std::fabs(s.pos[0].z) < kEps && std::fabs(s.pos[s.pos.size() - 1].z - 4.0) < kEps);
    }
}

TEST(patch_ops_reads_sample_vertex_layout) {
    // Vertex layout of samples/iw8/iw8_sample_room.map: "v x y z [c r g b a] t s t ls lt".
    const std::string text =
        "// brush 6\n"
        "{\n"
        " mesh\n"
        " {\n"
        "  toolFlags splitGeo;\n"
        "  tm2co/ee_trim_beam_airport_concrete\n"
        "  lightmap_gray\n"
        "  2 2 16 8\n"
        "  (\n"
        "\tv 64 128 0 t 0 0 0 0\n"
        "\tv 64 128 64 t 0 -64 0 4\n"
        "  )\n"
        "  (\n"
        "\tv 128 128 0 c 255 255 255 255 t 64 0 4 0\n"
        "\tv 128 128 64 t 64 -64 4 4\n"
        "  )\n"
        " }\n"
        "}\n";
    const auto p = parseOnePrim(text);
    CHECK(p.has_value());
    if (!p) return;
    const auto g = patchGrid(*p);
    CHECK(g.has_value());
    if (!g) return;
    CHECK(g->rows == 2 && g->cols == 2);
    CHECK(g->material == "tm2co/ee_trim_beam_airport_concrete");
    // File units are repeats x 1024 (kPatchStUnits): t 64 is 1/16 of a repeat.
    CHECK(std::fabs(g->st[2][0] - 64 / kPatchStUnits) < kEps && std::fabs(g->st[2][1]) < kEps);
    CHECK(std::fabs(g->st[3][1] + 64 / kPatchStUnits) < kEps);

    // Moving the colour-bearing vertex rewrites only its xyz; the colour and st stay as written.
    Primitive moved = *p;
    CHECK(movePatchVerts(moved, {2}, {0, 0, 8}));
    const size_t vl = p->verts[2].line;
    CHECK(moved.lines[vl].text == "\tv 128 128 8 c 255 255 255 255 t 64 0 4 0");
    CHECK(moved.lines.size() == p->lines.size());
    for (size_t li = 0; li < p->lines.size(); ++li)
        if (li != vl) CHECK(moved.lines[li].text == p->lines[li].text);
}

TEST(patch_ops_move_verts_zero_is_identical) {
    AABB box;
    box.add({0, 0, 0});
    box.add({64, 64, 0});
    const Primitive p = patchPrim(makeFlatPatch(box, 2, 3, 3, "m"));
    Primitive copy = p;
    CHECK(movePatchVerts(copy, {0, 1, 2, 3, 4, 5, 6, 7, 8}, {0, 0, 0}));
    CHECK(copy.text() == p.text());
}

TEST(patch_ops_move_verts_changes_only_moved_tokens) {
    AABB box;
    box.add({0, 0, 0});
    box.add({64, 64, 0});
    const Primitive p = patchPrim(makeFlatPatch(box, 2, 3, 3, "m"));
    Primitive q = p;
    CHECK(movePatchVerts(q, {4}, {1, 2, 3}));

    // Same line count; only the moved vertex's line differs, and only in its xyz tokens.
    CHECK(q.lines.size() == p.lines.size());
    const size_t moved = p.verts[4].line;
    const size_t tok = p.verts[4].token;
    for (size_t li = 0; li < p.lines.size() && li < q.lines.size(); ++li) {
        if (li != moved) {
            CHECK(q.lines[li].text == p.lines[li].text);
            continue;
        }
        const auto a = splitTokens(p.lines[li].text), b = splitTokens(q.lines[li].text);
        CHECK(a.size() == b.size());
        for (size_t k = 0; k < a.size() && k < b.size(); ++k) {
            const bool inXyz = k >= tok + 1 && k <= tok + 3;
            if (!inXyz) CHECK(a[k] == b[k]);
        }
    }
    CHECK(length(q.verts[4].pos - (p.verts[4].pos + Vec3{1, 2, 3})) < 1e-5);
    CHECK(length(q.verts[3].pos - p.verts[3].pos) < kEps);
}

TEST(patch_ops_brush_vertices_and_move_top_up) {
    ConvexBrush b = makeBox(AABB{{0, 0, 0}, {64, 32, 48}}, "");
    const char* mats[6] = {"m_px", "m_nx", "m_py", "m_ny", "m_pz", "m_nz"};
    for (size_t i = 0; i < b.size(); ++i) b[i].suffix = std::string(" ") + mats[i] + " 64 64 0 0 0 0";
    const auto built = makeBrushPrimitive(b, "\n", "// brush 0");
    CHECK(built.has_value());
    if (!built) return;
    const auto parsed = parseOnePrim(built->text());
    CHECK(parsed.has_value());
    if (!parsed) return;
    const Primitive brush = *parsed;
    CHECK(brush.kind == PrimKind::Brush);
    CHECK(brush.faces.size() == 6);
    CHECK(brushVertices(brush).size() == 8);

    std::vector<Vec3> top;
    for (const Vec3& v : brushVertices(brush))
        if (std::fabs(v.z - 48) < kEps) top.push_back(v);
    CHECK(top.size() == 4);

    const auto moved = moveBrushVerts(brush, top, {0, 0, 16}, "\n");
    CHECK(moved.has_value());
    if (!moved) return;
    CHECK(moved->kind == PrimKind::Brush);
    const AABB bb = moved->bounds();
    CHECK(std::fabs((bb.maxs.z - bb.mins.z) - 64) < kEps);
    CHECK(std::fabs(bb.mins.z) < kEps);
    CHECK(moved->faces.size() == brush.faces.size());
    for (size_t i = 0; i < moved->faces.size() && i < brush.faces.size(); ++i)
        CHECK(moved->faces[i].suffix == brush.faces[i].suffix);
    CHECK(moved->faces[0].suffix == " m_px 64 64 0 0 0 0");
    CHECK(moved->text().find("// brush 0") == 0);
}

TEST(patch_ops_brush_nonplanar_move_is_valid_or_nullopt) {
    ConvexBrush b = makeBox(AABB{{0, 0, 0}, {64, 64, 64}}, " caulk 64 64 0 0 0 0");
    const auto built = makeBrushPrimitive(b, "\r\n");
    CHECK(built.has_value());
    if (!built) return;
    const auto parsed = parseOnePrim(built->text());
    CHECK(parsed.has_value());
    if (!parsed) return;
    const Primitive brush = *parsed;

    // Lifting one top corner tilts the top face out of its plane.
    const std::vector<Vec3> one = {Vec3{64, 64, 64}};
    const auto r = moveBrushVerts(brush, one, {0, 0, 16}, "\r\n");
    if (r) {
        const ConvexBrush rb = brushFromPrimitive(*r);
        CHECK(isValidBrush(rb));
        CHECK(brushVolume(rb) > 0);
    }
    CHECK(!r.has_value());  // the top face is no longer planar, so this must be rejected

    // A corner that is not in the set is rejected.
    CHECK(!moveBrushVerts(brush, {Vec3{1, 2, 3}}, {0, 0, 1}, "\n").has_value());
}


TEST(patch_st_units_and_legacy_repeats) {
    // A 2x2 patch 512 units wide: file units (one repeat across = 1024) and an older editor's repeats (1.0) read the same.
    auto patch = [](const char* s1) {
        return std::string("{\n mesh\n {\n  m/x\n  lightmap_gray\n  2 2 16 8\n  (\n\tv 0 0 0 t 0 0 0 0\n\tv 0 512 0 t 0 ") + s1 +
               " 0 0\n  )\n  (\n\tv 512 0 0 t " + s1 + " 0 0 0\n\tv 512 512 0 t " + s1 + " " + s1 + " 0 0\n  )\n }\n}\n";
    };
    for (const char* v : {"1024", "1"}) {
        const auto p = parseOnePrim(patch(v));
        CHECK(p.has_value());
        if (!p) continue;
        const auto g = patchGrid(*p);
        CHECK(g && std::fabs(g->st.back()[0] - 1.0) < kEps && std::fabs(g->st.back()[1] - 1.0) < kEps);
        // Tessellated UVs: the corners keep their UVs, the middle is halfway.
        const auto uv = tessellatePatchUVs(*p, 4);
        CHECK(!uv.empty() && std::fabs(uv.front()[0]) < kEps && std::fabs(uv.back()[0] - 1.0) < kEps);
    }
    // Written back in file units.
    const auto p = parseOnePrim(patch("1"));
    if (p) {
        const Primitive w = makePatchPrimitive(*patchGrid(*p), "\n", "");
        CHECK(w.text().find("t 1024 1024") != std::string::npos);
    }
}


TEST(patch_st_world_unit_patches_keep_their_look) {
    // tools/make_dev_showcase.py (before 2026-11-10): t in world units, lightmap 1 per 64: a 128-unit repeat.
    const std::string text = "{\n mesh\n {\n  m/x\n  lightmap_gray\n  2 2 16 8\n  (\n\tv 0 0 0 t 0 0 0 0\n\tv 0 64 0 t 0 64 0 1\n"
                             "  )\n  (\n\tv 64 0 0 t 64 0 1 0\n\tv 64 64 0 t 64 64 1 1\n  )\n }\n}\n";
    const auto p = parseOnePrim(text);
    CHECK(p.has_value());
    if (!p) return;
    const auto g = patchGrid(*p);
    CHECK(g && std::fabs(g->st.back()[0] - 0.5) < kEps && std::fabs(g->st.back()[1] - 0.5) < kEps);
}


TEST(patch_set_st_rewrites_only_the_texture_pair) {
    const std::string text = "{\n mesh\n {\n  m/x\n  lightmap_gray\n  2 2 16 8\n  (\n\tv 0 0 0 c 255 0 0 255 t 0 0 0 0\n\tv 0 64 0 t 0 64 0 1\n"
                             "  )\n  (\n\tv 64 0 0 t 64 0 1 0\n\tv 64 64 0 t 64 64 1 1\n  )\n }\n}\n";
    auto p = parseOnePrim(text);
    CHECK(p.has_value());
    if (!p) return;
    auto g = patchGrid(*p);
    CHECK(g.has_value());
    if (!g) return;
    auto st = g->st;
    st[3] = {2.0, -1.5};
    CHECK(setPatchSt(*p, st));
    const auto g2 = patchGrid(*p);
    CHECK(g2 && std::fabs(g2->st[3][0] - 2.0) < kEps && std::fabs(g2->st[3][1] + 1.5) < kEps);
    CHECK(g2 && std::fabs(g2->st[0][0] - g->st[0][0]) < kEps);  // the others keep their UVs (converted to file units)
    const std::string out = p->text();
    CHECK(out.find("c 255 0 0 255 t 0 0") != std::string::npos);  // colour block kept
    CHECK(out.find("t 2048 -1536 1 1") != std::string::npos);      // file units, lightmap pair kept
}
