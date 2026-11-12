// Decal tool tests: planar decal patches, conforming decals over brush faces and placeDecal's undo step
// (src/core/decal_tools.cpp).
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "decal_tools.h"
#include "document.h"
#include "geometry.h"
#include "map_text.h"
#include "patch_ops.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

constexpr double kEps = 1e-6;

bool near(const Vec3& a, const Vec3& b, double eps = kEps) { return length(a - b) < eps; }

// Front normal of a 2 x 2 grid, as the editor faces it: cross(column direction, row direction).
Vec3 frontOf(const PatchGrid& g) { return normalize(cross(g.pos[1] - g.pos[0], g.pos[2] - g.pos[0])); }

// Area of a planar quad or degenerate (triangle) 2 x 2 patch, from its diagonals.
double patchArea(const PatchGrid& g) { return 0.5 * length(cross(g.pos[3] - g.pos[0], g.pos[2] - g.pos[1])); }

AABB box(const Vec3& lo, const Vec3& hi) {
    AABB b;
    b.add(lo);
    b.add(hi);
    return b;
}

// Worldspawn text with one solid brush per box (visible floor texture, so the decal tool accepts the faces).
std::string worldText(const std::vector<AABB>& boxes) {
    std::string t = "iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n";
    for (const AABB& b : boxes) {
        const auto p = makeBrushPrimitive(makeBox(b, " test_floor 64 64 0 0 0 0 0"), "\n", "");
        if (p) t += p->text();
    }
    return t + "}\n";
}

MapFile mapOf(const std::string& text) {
    Id next = 1;
    const ParseResult r = parseMap(text, next);
    return r.map ? *r.map : MapFile{};
}

// Patches (items with kind Patch) on worldspawn.
std::vector<PatchGrid> worldPatches(const Document& doc) {
    std::vector<PatchGrid> out;
    const Entity* ws = doc.map().entities.empty() ? nullptr : &doc.map().entities[0];
    if (!ws) return out;
    for (const auto& it : ws->items) {
        const Primitive* p = it.prim();
        if (p && p->kind == PrimKind::Patch)
            if (auto g = patchGrid(*p)) out.push_back(*g);
    }
    return out;
}

}  // namespace

TEST(decal_floor_lies_at_offset_facing_up) {
    const auto g = makeDecalPatch({10, 20, 0}, {0, 0, 1}, 64, 32, 0, 0.5, "decal_test");
    CHECK(g.has_value());
    if (!g) return;
    CHECK(g->rows == 2 && g->cols == 2 && g->pos.size() == 4 && g->st.size() == 4);
    CHECK(g->material == "decal_test");
    for (const Vec3& p : g->pos) CHECK(std::fabs(p.z - 0.5) < kEps);
    // Corners: centre (10, 20) +/- (32, 16), row 0 at -y, column 0 at -x.
    CHECK(near(g->pos[0], {-22, 4, 0.5}));
    CHECK(near(g->pos[1], {42, 4, 0.5}));
    CHECK(near(g->pos[2], {-22, 36, 0.5}));
    CHECK(near(g->pos[3], {42, 36, 0.5}));
    CHECK(near(frontOf(*g), {0, 0, 1}));
    // st 0..1 over the decal: s along columns, t along rows.
    CHECK(g->st[0][0] == 0 && g->st[0][1] == 0);
    CHECK(g->st[1][0] == 1 && g->st[1][1] == 0);
    CHECK(g->st[2][0] == 0 && g->st[2][1] == 1);
    CHECK(g->st[3][0] == 1 && g->st[3][1] == 1);
    CHECK(std::fabs(patchArea(*g) - 64.0 * 32.0) < kEps);
}

TEST(decal_rotation_quarter_turn_moves_corners) {
    const auto g = makeDecalPatch({10, 20, 0}, {0, 0, 1}, 64, 32, 90, 0.5, "decal_test");
    CHECK(g.has_value());
    if (!g) return;
    // Rotated 90 degrees about +Z: the width runs along +Y and the height along -X.
    CHECK(near(g->pos[0], {26, -12, 0.5}));
    CHECK(near(g->pos[1], {26, 52, 0.5}));
    CHECK(near(g->pos[2], {-6, -12, 0.5}));
    CHECK(near(g->pos[3], {-6, 52, 0.5}));
    CHECK(near(frontOf(*g), {0, 0, 1}));
}

TEST(decal_rotation_keeps_frame_orthogonal) {
    const Vec3 n = normalize({1, 2, 3});
    const auto g = makeDecalPatch({-40, 5, 7}, n, 96, 48, 37, 0.5, "decal_test");
    CHECK(g.has_value());
    if (!g) return;
    const Vec3 along = g->pos[1] - g->pos[0];
    const Vec3 down = g->pos[2] - g->pos[0];
    CHECK(std::fabs(length(along) - 96) < kEps);
    CHECK(std::fabs(length(down) - 48) < kEps);
    CHECK(std::fabs(dot(along, down)) < kEps);
    CHECK(std::fabs(dot(along, n)) < kEps && std::fabs(dot(down, n)) < kEps);  // in the plane
    CHECK(dot(frontOf(*g), n) > 0.999999);
}

TEST(decal_wall_faces_wall_normal) {
    // West-facing wall (normal -X) at x = 100: the decal sits 0.5 in front of it.
    const auto g = makeDecalPatch({100, 0, 50}, {-1, 0, 0}, 64, 32, 0, 0.5, "decal_test");
    CHECK(g.has_value());
    if (!g) return;
    for (const Vec3& p : g->pos) CHECK(std::fabs(p.x - 99.5) < kEps);
    CHECK(near(g->pos[0], {99.5, -16, 18}));  // width runs along z, height along y
    CHECK(near(g->pos[1], {99.5, -16, 82}));
    CHECK(near(frontOf(*g), {-1, 0, 0}));

    // Same for a normal that is not axis aligned: the front still follows the normal.
    const Vec3 n = normalize({0.3, -0.8, 0.5});
    const auto h = makeDecalPatch({0, 0, 0}, n, 40, 40, 15, 0.5, "decal_test");
    CHECK(h.has_value());
    if (h) CHECK(dot(frontOf(*h), n) > 0.999999);
}

TEST(decal_invalid_arguments_rejected) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK(!makeDecalPatch({0, 0, 0}, {0, 0, 1}, 0, 32, 0, 0.5, "m").has_value());
    CHECK(!makeDecalPatch({0, 0, 0}, {0, 0, 1}, 64, -1, 0, 0.5, "m").has_value());
    CHECK(!makeDecalPatch({0, 0, 0}, {0, 0, 0}, 64, 32, 0, 0.5, "m").has_value());
    CHECK(!makeDecalPatch({0, 0, 0}, {0, 0, 1}, 64, 32, 0, 0.5, "").has_value());
    CHECK(!makeDecalPatch({nan, 0, 0}, {0, 0, 1}, 64, 32, 0, 0.5, "m").has_value());
    CHECK(makeDecalPatch({0, 0, 0}, {0, 0, 1}, 64, 32, 0, 0.5, "m").has_value());
}

TEST(conforming_straddle_step_gives_two_patches) {
    // Floor A (top z = 64) and a lower floor B (top z = 32) that meets it at x = 0. The decal is centred on the
    // edge of A, so its left half lies on A and its right half on B.
    const MapFile m = mapOf(worldText({box({-256, -128, 0}, {0, 128, 64}), box({0, -128, -256}, {256, 128, 32})}));
    const auto out = makeConformingDecal(m, {0, 0, 64}, {0, 0, 1}, 64, 64, 0, 0.5, "decal_test");
    CHECK(out.size() == 2);
    if (out.size() != 2) return;

    double total = 0;
    for (const PatchGrid& g : out) total += patchArea(g);
    CHECK(std::fabs(total - 64.0 * 64.0) < 1e-4);  // the two pieces cover the whole decal once

    const PatchGrid* high = nullptr;
    const PatchGrid* low = nullptr;
    for (const PatchGrid& g : out) {
        if (std::fabs(g.pos[0].z - 64.5) < kEps) high = &g;
        if (std::fabs(g.pos[0].z - 32.5) < kEps) low = &g;
    }
    CHECK(high != nullptr && low != nullptr);
    if (!high || !low) return;
    CHECK(std::fabs(patchArea(*high) - 32.0 * 64.0) < 1e-4);
    CHECK(std::fabs(patchArea(*low) - 32.0 * 64.0) < 1e-4);
    for (const Vec3& p : high->pos) {
        CHECK(p.x > -32 - kEps && p.x < kEps);
        CHECK(std::fabs(p.z - 64.5) < kEps);
    }
    for (const Vec3& p : low->pos) {
        CHECK(p.x > -kEps && p.x < 32 + kEps);
        CHECK(std::fabs(p.z - 32.5) < kEps);
    }
    // Both pieces face +Z and carry continuous decal st (0..1 across the whole rectangle).
    CHECK(near(frontOf(*high), {0, 0, 1}));
    CHECK(near(frontOf(*low), {0, 0, 1}));
    double maxS = 0, minS = 1;
    for (const PatchGrid& g : out)
        for (const auto& st : g.st) {
            maxS = std::max(maxS, st[0]);
            minS = std::min(minS, st[0]);
        }
    CHECK(std::fabs(minS - 0.0) < kEps && std::fabs(maxS - 1.0) < kEps);
}

TEST(conforming_does_not_wrap_onto_walls) {
    // Only floor A: the decal runs past its east edge, onto the side wall. The wall is steeper than 45 degrees, so
    // only the part on the floor is painted.
    const MapFile m = mapOf(worldText({box({-256, -128, 0}, {0, 128, 64})}));
    const auto out = makeConformingDecal(m, {0, 0, 64}, {0, 0, 1}, 64, 64, 0, 0.5, "decal_test");
    CHECK(out.size() == 1);
    if (out.size() != 1) return;
    CHECK(std::fabs(patchArea(out[0]) - 32.0 * 64.0) < 1e-4);
    for (const Vec3& p : out[0].pos) CHECK(p.x < kEps);
}

TEST(conforming_no_geometry_falls_back_to_planar) {
    const MapFile empty = mapOf(worldText({}));
    const auto out = makeConformingDecal(empty, {10, 20, 0}, {0, 0, 1}, 64, 32, 0, 0.5, "decal_test");
    const auto planar = makeDecalPatch({10, 20, 0}, {0, 0, 1}, 64, 32, 0, 0.5, "decal_test");
    CHECK(out.size() == 1 && planar.has_value());
    if (out.size() == 1 && planar) {
        for (size_t i = 0; i < 4; ++i) CHECK(near(out[0].pos[i], planar->pos[i]));
    }
    CHECK(makeConformingDecal(empty, {0, 0, 0}, {0, 0, 0}, 64, 32, 0, 0.5, "m").empty());
}

TEST(place_decal_one_undo_step_restores_text) {
    Document doc;
    CHECK(doc.loadFromText(worldText({box({-256, -256, -16}, {256, 256, 0})}), "decal_test.map", nullptr));
    const std::string before = doc.text();
    const size_t historyBefore = doc.history().size();

    const auto a = makeDecalPatch({0, 0, 0}, {0, 0, 1}, 64, 64, 0, 0.5, "decal_test");
    const auto b = makeDecalPatch({100, 0, 0}, {0, 0, 1}, 32, 32, 45, 0.5, "decal_test");
    CHECK(a.has_value() && b.has_value());
    if (!a || !b) return;

    CHECK(placeDecal(doc, {*a, *b}, true));
    CHECK(doc.history().size() == historyBefore + 1);  // both patches in one step
    CHECK(doc.history().back().name == "Place Decal");
    CHECK(doc.selection().size() == 2);
    CHECK(doc.text() != before);
    const auto placed = worldPatches(doc);
    CHECK(placed.size() == 2);
    if (placed.size() == 2) {
        CHECK(near(placed[0].pos[0], a->pos[0]) && near(placed[0].pos[3], a->pos[3]));
        CHECK(near(placed[1].pos[1], b->pos[1]) && near(placed[1].pos[2], b->pos[2]));
    }

    CHECK(doc.undo());
    CHECK(doc.text() == before);
    CHECK(worldPatches(doc).empty());

    CHECK(!placeDecal(doc, {}, true));
    CHECK(doc.text() == before);
}

TEST(place_decal_non_colliding_flag_written_on_patch_body) {
    const auto g = makeDecalPatch({0, 0, 0}, {0, 0, 1}, 64, 64, 0, 0.5, "decal_test");
    CHECK(g.has_value());
    if (!g) return;

    Document solid;
    CHECK(solid.loadFromText(worldText({}), "solid.map", nullptr));
    CHECK(placeDecal(solid, {*g}, false));
    CHECK(solid.text().find("nonColliding") == std::string::npos);
    CHECK(worldPatches(solid).size() == 1);

    Document nc;
    CHECK(nc.loadFromText(worldText({}), "nc.map", nullptr));
    CHECK(placeDecal(nc, {*g}, true));
    const std::string text = nc.text();
    CHECK(text.find("contents nonColliding;") != std::string::npos);
    // The flag line is a body line: the grid, material and vertices read back exactly as built.
    const auto back = worldPatches(nc);
    CHECK(back.size() == 1);
    if (back.size() == 1) {
        CHECK(back[0].material == "decal_test");
        for (size_t i = 0; i < 4; ++i) CHECK(near(back[0].pos[i], g->pos[i]));
    }
}
