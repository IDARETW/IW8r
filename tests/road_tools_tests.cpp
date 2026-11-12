// Roads along splines (src/core/road_tools.cpp): terrain height, road strips, terrain carving and the document helper.
#include <cmath>
#include <string>
#include <vector>

#include "document.h"
#include "geometry.h"
#include "patch_ops.h"
#include "patch_ops2.h"
#include "road_tools.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

constexpr double kEps = 1e-9;

// Height of the test terrain: a plane, so bilinear interpolation is exact inside every cell.
double planeZ(double x, double y) { return 0.25 * x + 0.1 * y; }

// 9 x 9 sloped terrain, 64 units apart, starting at the origin, in the layout Create Terrain writes.
PatchGrid slopedTerrain9() {
    PatchGrid g;
    g.rows = 9;
    g.cols = 9;
    g.keyword = "mesh";
    g.material = "terrain_m";
    for (int r = 0; r < 9; ++r)
        for (int c = 0; c < 9; ++c) {
            const double x = c * 64.0, y = r * 64.0;
            g.pos.push_back(Vec3{x, y, planeZ(x, y)});
            g.st.push_back({0.0, 0.0});
        }
    return g;
}

size_t at(const PatchGrid& g, int r, int c) {
    return static_cast<size_t>(r) * static_cast<size_t>(g.cols) + static_cast<size_t>(c);
}

// A map with one worldspawn holding `terrain` and one point entity per origin in `points`.
std::string mapWith(const PatchGrid& terrain, const std::vector<Vec3>& points) {
    std::string t = "iwmap 4\r\n\"000_Global\" flags active\r\n// entity 0\r\n{\r\n\"classname\" \"worldspawn\"\r\n";
    t += makePatchPrimitive(terrain, "\r\n", "").text();
    t += "}\r\n";
    for (const Vec3& p : points) {
        t += "{\r\n\"classname\" \"info_null\"\r\n\"origin\" \"" + std::to_string(static_cast<int>(p.x)) + " " +
             std::to_string(static_cast<int>(p.y)) + " " + std::to_string(static_cast<int>(p.z)) + "\"\r\n}\r\n";
    }
    return t;
}

// The point entity ids of a document in map order (the non-worldspawn entities).
std::vector<Id> pointIds(const Document& d) {
    std::vector<Id> ids;
    for (const Entity& e : d.map().entities)
        if (!e.isWorldspawn()) ids.push_back(e.id);
    return ids;
}

}  // namespace

TEST(road_terrain_height_is_bilinear_on_a_plane) {
    const std::vector<PatchGrid> terrains{slopedTerrain9()};
    bool hit = false;
    // Inside cells, on edges and at control points.
    const double pts[][2] = {{100, 37}, {0, 0}, {512, 512}, {64, 448}, {333.3, 200.7}};
    for (const auto& p : pts) {
        const double z = terrainHeightAt(terrains, p[0], p[1], &hit);
        CHECK(hit);
        CHECK(std::fabs(z - planeZ(p[0], p[1])) < kEps);
    }
}

TEST(road_terrain_height_outside_reports_no_hit) {
    const std::vector<PatchGrid> terrains{slopedTerrain9()};
    bool hit = true;
    const double z = terrainHeightAt(terrains, -10, 100, &hit);
    CHECK(!hit);
    CHECK(z == 0.0);
    terrainHeightAt({}, 10, 10, &hit);
    CHECK(!hit);
    // A non-finite query never hits.
    terrainHeightAt(terrains, std::nan(""), 10, &hit);
    CHECK(!hit);
}

TEST(road_terrain_height_irregular_grid_uses_triangles) {
    // Move one interior control point sideways: the grid is no longer a lattice, so the triangle fallback is used.
    // A query exactly at that moved point returns its own height.
    PatchGrid g = slopedTerrain9();
    const size_t k = at(g, 4, 4);
    g.pos[k].x += 10.0;
    g.pos[k].z = 77.0;
    bool hit = false;
    const double z = terrainHeightAt({g}, g.pos[k].x, g.pos[k].y, &hit);
    CHECK(hit);
    CHECK(std::fabs(z - 77.0) < 1e-6);
}

TEST(road_strip_points_sit_on_terrain_plus_lift) {
    const std::vector<PatchGrid> terrains{slopedTerrain9()};
    const std::vector<Vec3> path{{64, 256, 0}, {192, 256, 0}, {320, 256, 0}, {448, 256, 0}};
    const double lift = 0.5;
    auto road = roadStrip(path, 96, 4, terrains, lift, "road_m", 256);
    CHECK(road.has_value());
    if (!road) return;
    CHECK(road->rows == 4 && road->cols == 5);
    CHECK(road->keyword == "mesh");
    CHECK(road->material == "road_m");
    for (size_t k = 0; k < road->pos.size(); ++k) {
        const Vec3& p = road->pos[k];
        // The terrain is a plane, so the expected height is exact.
        CHECK(std::fabs(p.z - (planeZ(p.x, p.y) + lift)) < 0.5);
    }
}

TEST(road_strip_width_and_texture_coordinates) {
    const std::vector<PatchGrid> terrains{slopedTerrain9()};
    const std::vector<Vec3> path{{64, 256, 0}, {192, 256, 0}, {320, 256, 0}};
    const double width = 96, uvLength = 128;
    auto road = roadStrip(path, width, 4, terrains, 0.25, "road_m", uvLength);
    CHECK(road.has_value());
    if (!road) return;
    // Width: the first and last columns of every row are `width` apart in XY.
    for (int r = 0; r < road->rows; ++r) {
        const Vec3& a = road->pos[at(*road, r, 0)];
        const Vec3& b = road->pos[at(*road, r, road->cols - 1)];
        CHECK(std::fabs(std::hypot(b.x - a.x, b.y - a.y) - width) < kEps);
    }
    // s runs 0..1 across the road; t is 0 at the first row and grows along the path.
    CHECK(std::fabs(road->st[at(*road, 0, 0)][0]) < kEps);
    CHECK(std::fabs(road->st[at(*road, 0, road->cols - 1)][0] - 1.0) < kEps);
    CHECK(std::fabs(road->st[at(*road, 0, 0)][1]) < kEps);
    CHECK(road->st[at(*road, 1, 0)][1] > road->st[at(*road, 0, 0)][1]);
    CHECK(road->st[at(*road, 2, 0)][1] > road->st[at(*road, 1, 0)][1]);
    // t of the second row is its 3D centreline distance over uvLength (the centre column, on the terrain + lift).
    const double z0 = planeZ(64, 256) + 0.25, z1 = planeZ(192, 256) + 0.25;
    const double seg = std::sqrt(128.0 * 128.0 + (z1 - z0) * (z1 - z0));
    CHECK(std::fabs(road->st[at(*road, 1, 0)][1] - seg / uvLength) < 1e-6);
    // Every column of a row shares its t.
    CHECK(std::fabs(road->st[at(*road, 1, 2)][1] - road->st[at(*road, 1, 0)][1]) < kEps);
}

TEST(road_strip_rejects_bad_arguments) {
    const std::vector<PatchGrid> terrains{slopedTerrain9()};
    const std::vector<Vec3> path{{64, 256, 0}, {320, 256, 0}};
    CHECK(roadStrip(path, 96, 4, terrains, 0.25, "m", 256).has_value());
    CHECK(!roadStrip(path, 0, 4, terrains, 0.25, "m", 256).has_value());
    CHECK(!roadStrip(path, -8, 4, terrains, 0.25, "m", 256).has_value());
    CHECK(!roadStrip(path, 96, 0, terrains, 0.25, "m", 256).has_value());
    CHECK(!roadStrip(path, 96, 4, terrains, 0.25, "m", 0).has_value());
    CHECK(!roadStrip(path, 96, 4, terrains, std::nan(""), "m", 256).has_value());
    CHECK(!roadStrip({{0, 0, 0}}, 96, 4, terrains, 0.25, "m", 256).has_value());
    CHECK(!roadStrip(path, 96, 4, terrains, 0.25, "two words", 256).has_value());
    CHECK(!roadStrip(path, 96, 4, terrains, 0.25, "", 256).has_value());
    // Every point on one spot has no direction.
    CHECK(!roadStrip({{5, 5, 0}, {5, 5, 0}}, 96, 4, terrains, 0.25, "m", 256).has_value());
}

TEST(road_carve_flattens_strip_and_keeps_far_points) {
    std::vector<PatchGrid> terrains{slopedTerrain9()};
    const PatchGrid before = terrains[0];
    // Road along y = 256 from x = 0 to 512 at height 40; width 128 (half 64), falloff 0 (hard edge), depth 8.
    const std::vector<Vec3> path{{0, 256, 40}, {128, 256, 40}, {256, 256, 40}, {384, 256, 40}, {512, 256, 40}};
    CHECK(carveTerrain(terrains, path, 128, 0, 8));
    const PatchGrid& g = terrains[0];
    // Rows 3, 4, 5 (y = 192, 256, 320) lie within half the width: flat at 40 - 8 = 32.
    for (int r = 3; r <= 5; ++r)
        for (int c = 0; c < 9; ++c) CHECK(std::fabs(g.pos[at(g, r, c)].z - 32.0) < kEps);
    // Rows 0, 1, 2, 6, 7, 8 are outside the band and do not move.
    for (int r : {0, 1, 2, 6, 7, 8})
        for (int c = 0; c < 9; ++c) CHECK(g.pos[at(g, r, c)].z == before.pos[at(before, r, c)].z);
    // Only z changes.
    for (size_t k = 0; k < g.pos.size(); ++k) {
        CHECK(g.pos[k].x == before.pos[k].x);
        CHECK(g.pos[k].y == before.pos[k].y);
    }
}

TEST(road_carve_falloff_is_smooth_and_bad_args_do_nothing) {
    std::vector<PatchGrid> terrains{slopedTerrain9()};
    const PatchGrid before = terrains[0];
    const std::vector<Vec3> path{{0, 256, 40}, {512, 256, 40}};
    // Half width 64, falloff 128: the row at y = 128 is 128 away, so its weight is smoothstep(0.5) complement = 0.5.
    CHECK(carveTerrain(terrains, path, 128, 128, 8));
    const PatchGrid& g = terrains[0];
    const double z0 = before.pos[at(before, 2, 4)].z;
    CHECK(std::fabs(g.pos[at(g, 2, 4)].z - (z0 + (32.0 - z0) * 0.5)) < 1e-6);
    // The row at y = 64 is 192 away, beyond half + falloff: unchanged.
    CHECK(g.pos[at(g, 1, 4)].z == before.pos[at(before, 1, 4)].z);

    std::vector<PatchGrid> same{slopedTerrain9()};
    CHECK(!carveTerrain(same, path, 0, 32, 8));
    CHECK(!carveTerrain(same, path, 128, -1, 8));
    CHECK(!carveTerrain(same, {{0, 0, 0}}, 128, 32, 8));
    CHECK(same[0].pos == slopedTerrain9().pos);
}

TEST(road_add_from_selection_is_one_undo_step) {
    const std::vector<Vec3> points{{64, 256, 0}, {256, 256, 0}, {448, 256, 0}};
    Document d;
    CHECK(d.loadFromText(mapWith(slopedTerrain9(), points), "road.map", nullptr));
    const std::string before = d.text();
    const size_t undoBefore = d.undoPosition();

    for (Id id : pointIds(d)) d.select(SelRef{id, kNoId}, true);
    RoadOptions opts;
    opts.crossSegments = 4;
    opts.lift = 0.5;
    opts.uvLength = 256;
    opts.material = "road_m";
    CHECK(addRoadFromSelection(d, 96, opts));
    CHECK(d.undoPosition() == undoBefore + 1);
    CHECK(d.text() != before);

    // The road is the last patch of worldspawn, a 3 x 5 mesh on the terrain + lift.
    const Entity* ws = d.worldspawn();
    CHECK(ws != nullptr);
    if (ws) {
        const EntityItem& last = ws->items.back();
        CHECK(last.isPrimitive());
        auto road = last.prim() ? patchGrid(*last.prim()) : std::nullopt;
        CHECK(road.has_value());
        if (road) {
            CHECK(road->rows == 3 && road->cols == 5);
            CHECK(road->material == "road_m");
            for (const Vec3& p : road->pos) CHECK(std::fabs(p.z - (planeZ(p.x, p.y) + 0.5)) < 0.5);
        }
    }

    // One undo restores the text exactly.
    CHECK(d.undo());
    CHECK(d.text() == before);
    CHECK(d.undoPosition() == undoBefore);
}

TEST(road_add_with_carve_is_one_undo_step) {
    const std::vector<Vec3> points{{0, 256, 40}, {256, 256, 40}, {512, 256, 40}};
    Document d;
    CHECK(d.loadFromText(mapWith(slopedTerrain9(), points), "road.map", nullptr));
    const std::string before = d.text();
    const size_t undoBefore = d.undoPosition();

    for (Id id : pointIds(d)) d.select(SelRef{id, kNoId}, true);
    RoadOptions opts;
    opts.carve = true;
    opts.depth = 8;
    opts.falloff = 0;
    CHECK(addRoadFromSelection(d, 128, opts));
    CHECK(d.undoPosition() == undoBefore + 1);

    // The terrain row on the road centreline was carved to 40 - 8 = 32 and the road sits on it + lift.
    const Entity* ws = d.worldspawn();
    CHECK(ws != nullptr);
    if (ws) {
        const EntityItem* terrainItem = nullptr;
        for (const EntityItem& it : ws->items)
            if (it.isPrimitive() && !terrainItem) terrainItem = &it;
        auto terrain = terrainItem && terrainItem->prim() ? patchGrid(*terrainItem->prim()) : std::nullopt;
        CHECK(terrain.has_value());
        if (terrain) CHECK(std::fabs(terrain->pos[at(*terrain, 4, 4)].z - 32.0) < 1e-6);
    }

    CHECK(d.undo());
    CHECK(d.text() == before);
}

TEST(road_add_needs_two_points_and_a_terrain) {
    const std::vector<Vec3> onePoint{{64, 256, 0}};
    Document one;
    CHECK(one.loadFromText(mapWith(slopedTerrain9(), onePoint), "one.map", nullptr));
    const std::string textOne = one.text();
    for (Id id : pointIds(one)) one.select(SelRef{id, kNoId}, true);
    CHECK(!addRoadFromSelection(one, 96));
    CHECK(one.text() == textOne);

    // Two points but no terrain patch in the map: nothing to drape the road onto.
    Document noTerrain;
    std::string text = "iwmap 4\r\n\"000_Global\" flags active\r\n// entity 0\r\n{\r\n\"classname\" \"worldspawn\"\r\n}\r\n";
    text += "{\r\n\"classname\" \"info_null\"\r\n\"origin\" \"64 256 0\"\r\n}\r\n";
    text += "{\r\n\"classname\" \"info_null\"\r\n\"origin\" \"320 256 0\"\r\n}\r\n";
    CHECK(noTerrain.loadFromText(text, "none.map", nullptr));
    for (Id id : pointIds(noTerrain)) noTerrain.select(SelRef{id, kNoId}, true);
    CHECK(!addRoadFromSelection(noTerrain, 96));
    CHECK(noTerrain.text() == text);
}
