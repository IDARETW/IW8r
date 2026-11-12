// Patch vertex colour and alpha painting tests (src/core/vertex_paint.cpp, roadmap 8.11).
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "map_text.h"
#include "patch_ops.h"
#include "test_harness.h"
#include "vertex_paint.h"

using namespace iw8r;

namespace {

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

// 3 x 3 mesh on the z = 0 plane. Control point i = row * 3 + col sits at (64 col, 64 row, 0).
// Distances from the origin: 0, 64, 128, 64, 90.5, 144, 128, 146.2, 181.0 (index 0..8).
Primitive meshPatch() {
    PatchGrid g;
    g.rows = 3;
    g.cols = 3;
    g.keyword = "mesh";
    g.material = "tm2co/test";
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
            g.pos.push_back({64.0 * c, 64.0 * r, 0.0});
            g.st.push_back({0.0, 0.0});
        }
    auto p = parseOnePrim(makePatchPrimitive(g, "\n", "// patch").text());
    return p ? *p : Primitive{};
}

// Same layout as samples/iw8/iw8_sample_room.map: one vertex of the first row carries a colour block.
const char* kSamplePatch =
    "// patch\n"
    "{\n"
    " mesh\n"
    " {\n"
    "  tm2co/x\n"
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

PaintBrush hardRed(double radius) {
    PaintBrush b;
    b.rgba[0] = 1.0f;
    b.rgba[1] = 0.0f;
    b.rgba[2] = 0.0f;
    b.rgba[3] = 1.0f;
    b.radius = radius;
    b.falloff = 0.0;
    b.strength = 1.0;
    b.channelMask = kPaintAll;
    return b;
}

bool sameColour(const Rgba8& a, const Rgba8& b) { return a == b; }

}  // namespace

TEST(vertex_paint_brush_weight_hard) {
    PaintBrush b;
    b.radius = 100;
    b.falloff = 0;
    CHECK(brushWeight(b, 0) == 1.0);
    CHECK(brushWeight(b, 99.9) == 1.0);
    CHECK(brushWeight(b, 100) == 0.0);  // at the radius: outside
    CHECK(brushWeight(b, 150) == 0.0);
}

TEST(vertex_paint_brush_weight_soft_and_partial) {
    PaintBrush soft;
    soft.radius = 100;
    soft.falloff = 1;
    CHECK(brushWeight(soft, 0) == 1.0);
    CHECK(std::fabs(brushWeight(soft, 50) - 0.5) < 1e-12);  // smoothstep midpoint
    CHECK(brushWeight(soft, 100) == 0.0);
    CHECK(brushWeight(soft, 25) > brushWeight(soft, 50));
    CHECK(brushWeight(soft, 50) > brushWeight(soft, 75));
    CHECK(brushWeight(soft, 25) < 1.0 && brushWeight(soft, 25) > 0.0);

    PaintBrush half;
    half.radius = 100;
    half.falloff = 0.5;  // full weight out to 50, then falls to 0 at 100
    CHECK(brushWeight(half, 50) == 1.0);
    CHECK(brushWeight(half, 25) == 1.0);
    CHECK(std::fabs(brushWeight(half, 75) - 0.5) < 1e-12);
    CHECK(brushWeight(half, 100) == 0.0);
}

TEST(vertex_paint_brush_weight_invalid_brush) {
    PaintBrush none;
    none.radius = 0;
    CHECK(brushWeight(none, 0) == 0.0);
    none.radius = std::numeric_limits<double>::quiet_NaN();
    CHECK(brushWeight(none, 0) == 0.0);
    PaintBrush ok;
    ok.radius = 100;
    CHECK(brushWeight(ok, std::numeric_limits<double>::quiet_NaN()) == 0.0);
}

TEST(vertex_paint_reads_default_white_without_block) {
    Primitive p = meshPatch();
    CHECK(p.kind == PrimKind::Patch);
    CHECK(p.verts.size() == 9);
    const std::vector<Rgba8> c = readPatchColours(p);
    CHECK(c.size() == 9);
    for (const Rgba8& v : c) CHECK(sameColour(v, kDefaultVertexRgba));
    CHECK(!hasPatchColour(p, 0));
    // Positions are read in row-major order.
    CHECK(length(p.verts[4].pos - Vec3{64, 64, 0}) < 1e-9);
    CHECK(length(p.verts[8].pos - Vec3{128, 128, 0}) < 1e-9);
}

TEST(vertex_paint_hard_brush_hits_only_inside_radius) {
    Primitive p = meshPatch();
    const int changed = paintPatchVertices(p, {0, 0, 0}, hardRed(100));
    // Inside 100 from the origin: indices 0, 1, 3, 4 (distances 0, 64, 64, 90.5).
    CHECK(changed == 4);
    const std::vector<Rgba8> c = readPatchColours(p);
    for (size_t i : {size_t(0), size_t(1), size_t(3), size_t(4)})
        CHECK(sameColour(c[i], Rgba8{255, 0, 0, 255}));
    // Outside the radius: untouched, no block written.
    for (size_t i : {size_t(2), size_t(5), size_t(6), size_t(7), size_t(8)}) {
        CHECK(sameColour(c[i], kDefaultVertexRgba));
        CHECK(!hasPatchColour(p, i));
    }
    // Positions are unchanged.
    CHECK(length(p.verts[4].pos - Vec3{64, 64, 0}) < 1e-9);
}

TEST(vertex_paint_only_changed_line_is_rewritten) {
    Primitive p = meshPatch();
    std::vector<std::string> before;
    for (const auto& l : p.lines) before.push_back(l.text);
    const size_t line0 = p.verts[0].line;
    PaintBrush small = hardRed(10);  // only control point 0
    CHECK(paintPatchVertices(p, {0, 0, 0}, small) == 1);
    CHECK(p.lines.size() == before.size());
    for (size_t i = 0; i < before.size(); ++i) {
        if (i == line0) continue;
        CHECK(p.lines[i].text == before[i]);
    }
    CHECK(p.dirty);
}

TEST(vertex_paint_insert_keeps_xyz_and_texture_tokens) {
    Primitive p = meshPatch();
    const std::string original = p.lines[p.verts[0].line].text;
    CHECK(setPatchVertexColour(p, 0, Rgba8{255, 0, 0, 255}));
    const std::string painted = p.lines[p.verts[0].line].text;
    CHECK(painted.find(" c 255 0 0 255") != std::string::npos);
    std::string stripped = painted;
    const std::string block = " c 255 0 0 255";
    stripped.erase(stripped.find(block), block.size());
    CHECK(stripped == original);  // the only change is the inserted block
    CHECK(length(p.verts[0].pos - Vec3{0, 0, 0}) < 1e-9);
    CHECK(readPatchColours(p)[0] == (Rgba8{255, 0, 0, 255}));
}

TEST(vertex_paint_strength_blends_toward_target) {
    Primitive p = meshPatch();
    PaintBrush b = hardRed(100);
    b.rgba[0] = 0.0f;  // black
    b.rgba[1] = 0.0f;
    b.rgba[2] = 0.0f;
    b.rgba[3] = 1.0f;
    b.strength = 0.5;
    b.channelMask = kPaintR | kPaintG | kPaintB;
    CHECK(paintPatchVertices(p, {0, 0, 0}, b) == 4);
    const Rgba8 c0 = readPatchColours(p)[0];
    // 255 + (0 - 255) * 0.5 = 127.5, rounded half away from zero.
    CHECK(c0[0] == 128 && c0[1] == 128 && c0[2] == 128);
    CHECK(c0[3] == 255);  // alpha not in the mask
}

TEST(vertex_paint_soft_falloff_is_darker_at_centre) {
    Primitive p = meshPatch();
    PaintBrush b = hardRed(100);
    b.rgba[0] = b.rgba[1] = b.rgba[2] = 0.0f;
    b.falloff = 1.0;
    b.channelMask = kPaintR | kPaintG | kPaintB;
    CHECK(paintPatchVertices(p, {0, 0, 0}, b) == 4);
    const std::vector<Rgba8> c = readPatchColours(p);
    CHECK(c[0][0] == 0);                  // centre: full weight
    CHECK(c[1][0] > 0 && c[1][0] < 255);  // 64 away: partial
    CHECK(c[1][0] == c[3][0]);            // symmetric distances
    CHECK(c[4][0] > c[1][0]);             // 90.5 away: less than 64 away
    CHECK(c[5][0] == 255);                // outside: untouched
}

TEST(vertex_paint_channel_mask_limits_channels) {
    Primitive p = meshPatch();
    PaintBrush b = hardRed(100);
    b.rgba[0] = b.rgba[1] = b.rgba[2] = b.rgba[3] = 0.0f;
    b.channelMask = kPaintR;  // red only
    CHECK(paintPatchVertices(p, {0, 0, 0}, b) == 4);
    CHECK(readPatchColours(p)[0] == (Rgba8{0, 255, 255, 255}));
}

TEST(vertex_paint_alpha_channel_only) {
    Primitive p = meshPatch();
    PaintBrush b = hardRed(100);
    b.rgba[0] = 1.0f;
    b.rgba[1] = 0.0f;
    b.rgba[2] = 0.0f;
    b.rgba[3] = 0.0f;  // alpha 0
    b.channelMask = kPaintA;
    CHECK(paintPatchVertices(p, {0, 0, 0}, b) == 4);
    CHECK(readPatchColours(p)[0] == (Rgba8{255, 255, 255, 0}));
    CHECK(readPatchColours(p)[1] == (Rgba8{255, 255, 255, 0}));
    CHECK(readPatchColours(p)[2] == kDefaultVertexRgba);
}

TEST(vertex_paint_zero_strength_mask_or_radius_changes_nothing) {
    Primitive p = meshPatch();
    PaintBrush b = hardRed(100);
    b.strength = 0;
    CHECK(paintPatchVertices(p, {0, 0, 0}, b) == 0);
    b = hardRed(100);
    b.channelMask = 0;
    CHECK(paintPatchVertices(p, {0, 0, 0}, b) == 0);
    b = hardRed(0);
    CHECK(paintPatchVertices(p, {0, 0, 0}, b) == 0);
    b = hardRed(std::numeric_limits<double>::infinity());
    CHECK(paintPatchVertices(p, {0, 0, 0}, b) == 0);
    for (const Rgba8& v : readPatchColours(p)) CHECK(sameColour(v, kDefaultVertexRgba));
}

TEST(vertex_paint_repeat_is_idempotent) {
    Primitive p = meshPatch();
    CHECK(paintPatchVertices(p, {0, 0, 0}, hardRed(100)) == 4);
    CHECK(paintPatchVertices(p, {0, 0, 0}, hardRed(100)) == 0);
}

TEST(vertex_paint_non_patch_and_bad_index) {
    Primitive empty;  // kind Unknown
    CHECK(paintPatchVertices(empty, {0, 0, 0}, hardRed(100)) == 0);
    CHECK(readPatchColours(empty).empty());
    CHECK(!setPatchVertexColour(empty, 0, Rgba8{1, 2, 3, 4}));

    Primitive p = meshPatch();
    CHECK(!setPatchVertexColour(p, 9, Rgba8{1, 2, 3, 4}));  // only 9 control points
    CHECK(!hasPatchColour(p, 9));
}

TEST(vertex_paint_reads_and_overwrites_sample_block) {
    auto parsed = parseOnePrim(kSamplePatch);
    CHECK(parsed.has_value());
    if (!parsed) return;
    Primitive p = *parsed;
    CHECK(p.verts.size() == 4);
    CHECK(hasPatchColour(p, 2));
    CHECK(!hasPatchColour(p, 0));
    std::vector<Rgba8> c = readPatchColours(p);
    CHECK(sameColour(c[2], kDefaultVertexRgba));  // 255 255 255 255 is the same as white
    CHECK(sameColour(c[0], kDefaultVertexRgba));

    // Insert on a line without a block, then overwrite the existing block (two lines, both rewritten).
    CHECK(setPatchVertexColour(p, 0, Rgba8{1, 2, 3, 4}));
    CHECK(p.lines[p.verts[0].line].text == "\tv 64 128 0 c 1 2 3 4 t 0 0 0 0");
    CHECK(setPatchVertexColour(p, 2, Rgba8{10, 20, 30, 40}));
    CHECK(p.lines[p.verts[2].line].text == "\tv 128 128 0 c 10 20 30 40 t 64 0 4 0");
    c = readPatchColours(p);
    CHECK(sameColour(c[0], Rgba8{1, 2, 3, 4}));
    CHECK(sameColour(c[2], Rgba8{10, 20, 30, 40}));
    CHECK(sameColour(c[1], kDefaultVertexRgba));
    CHECK(sameColour(c[3], kDefaultVertexRgba));
    CHECK(length(p.verts[3].pos - Vec3{128, 128, 64}) < 1e-9);
}

TEST(vertex_paint_round_trip_through_map_text) {
    // Paint a mesh, write the map text, parse it again: colours and positions survive, and the unpainted parts
    // of the file stay byte-exact.
    Primitive p = meshPatch();
    PaintBrush b = hardRed(100);
    b.rgba[1] = 0.5f;  // (1, 0.5, 0, 1) -> (255, 128, 0, 255) after one full-strength stamp
    CHECK(paintPatchVertices(p, {0, 0, 0}, b) == 4);
    const std::vector<Rgba8> painted = readPatchColours(p);

    const std::string text =
        "iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n\"origin\" \"0 0 0\"\n" + p.text() + "}\n";
    Id next = 1;
    const ParseResult r = parseMap(text, next);
    CHECK(r.map.has_value());
    if (!r.map) return;
    CHECK(r.map->text() == text);  // lossless serialisation of the parsed map
    const Primitive* back = nullptr;
    for (const auto& it : r.map->entities[0].items)
        if (it.isPrimitive()) back = it.prim();
    CHECK(back != nullptr);
    if (!back) return;
    CHECK(back->kind == PrimKind::Patch);
    CHECK(readPatchColours(*back) == painted);
    CHECK(sameColour(painted[0], (Rgba8{255, 128, 0, 255})));
    CHECK(sameColour(painted[5], kDefaultVertexRgba));
    CHECK(length(back->verts[7].pos - Vec3{64, 128, 0}) < 1e-9);
}
