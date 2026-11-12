// Spawn analysis (6.18): parsing, sight lines over the collision world, spacing warnings and the density heat map.
#include <cmath>
#include <string>
#include <vector>

#include "collision_world.h"
#include "map_text.h"
#include "spawn_analysis.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

// Axis-aligned box brush in map text (same faces as auto_caulk_tests).
std::string brushBox(int x0, int y0, int z0, int x1, int y1, int z1) {
    const std::string mat = "tm2co/me_curb_stone_01";
    auto p = [](int x, int y, int z) { return std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z); };
    auto f = [&](const std::string& a, const std::string& b, const std::string& c) {
        return " ( " + a + " ) ( " + b + " ) ( " + c + " ) " + mat + " 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n";
    };
    std::string t = "{\n";
    t += f(p(x1, y1, z0), p(x0, y1, z0), p(x0, y0, z0));  // bottom
    t += f(p(x0, y0, z1), p(x0, y1, z1), p(x1, y1, z1));  // top
    t += f(p(x0, y0, z0), p(x0, y0, z1), p(x1, y0, z1));  // -y
    t += f(p(x1, y0, z0), p(x1, y0, z1), p(x1, y1, z1));  // +x
    t += f(p(x1, y1, z0), p(x1, y1, z1), p(x0, y1, z1));  // +y
    t += f(p(x0, y1, z0), p(x0, y1, z1), p(x0, y0, z1));  // -x
    return t + "}\n";
}

// A spawn entity block; origin is the feet.
std::string spawn(const std::string& cls, int x, int y, int z, const std::string& angles = "0 0 0") {
    return "{\n\"classname\" \"" + cls + "\"\n\"origin\" \"" + std::to_string(x) + " " + std::to_string(y) + " " +
           std::to_string(z) + "\"\n\"angles\" \"" + angles + "\"\n}\n";
}

MapFile makeMap(const std::string& body) {
    const std::string t = "iwmap 4\n" + body;
    Id next = 1;
    ParseResult r = parseMap(t, next);
    return r.map ? *r.map : MapFile{};
}

// Floor top at z = 0, spanning +-1024. Shared by the sight tests.
std::string floorWorld() {
    return "{\n\"classname\" \"worldspawn\"\n" + brushBox(-1024, -1024, -16, 1024, 1024, 0);
}

bool anyContains(const std::vector<std::string>& v, const std::string& s) {
    for (const std::string& x : v)
        if (x.find(s) != std::string::npos) return true;
    return false;
}

}  // namespace

TEST(spawn_collect_modes_and_teams) {
    const std::string body = spawn("mp_tdm_spawn_allies_start", 10, 20, 30, "5 90 0") +
                             spawn("mp_tdm_spawn_axis_start", 100, 0, 0) + spawn("mp_tdm_spawn", 0, 100, 0) +
                             spawn("mp_dm_spawn", 200, 0, 0) + spawn("mp_dom_spawn_allies_start", 0, 200, 0) +
                             spawn("mp_dom_spawn_axis_start", 0, 300, 0) + spawn("mp_koth_spawn", 0, 400, 0) +
                             spawn("mp_dd_spawn_defender_start", 0, 500, 0) + spawn("mp_global_intermission", 0, 0, 500);
    const MapFile m = makeMap("{\n\"classname\" \"worldspawn\"\n}\n" + body);
    const std::vector<SpawnPoint> sp = collectSpawns(m);
    CHECK(sp.size() == 8);  // intermission is not a spawn
    if (sp.size() != 8) return;

    CHECK(sp[0].classname == "mp_tdm_spawn_allies_start");
    CHECK(sp[0].mode == "tdm");
    CHECK(sp[0].team == 1);
    CHECK(sp[0].start);
    CHECK(sp[0].eye == (Vec3{10, 20, 30 + kSpawnEyeHeight}));
    CHECK(std::fabs(sp[0].yaw - 90.0) < 1e-9);  // second angles value
    CHECK(sp[0].entity != kNoId);

    CHECK(sp[1].mode == "tdm" && sp[1].team == 2 && sp[1].start);
    CHECK(sp[2].mode == "tdm" && sp[2].team == 0 && !sp[2].start);  // secondary / plain tdm spawn
    CHECK(sp[3].mode == "dm" && sp[3].team == 0 && !sp[3].start);
    CHECK(sp[4].mode == "dom" && sp[4].team == 1 && sp[4].start);
    CHECK(sp[5].mode == "dom" && sp[5].team == 2 && sp[5].start);
    CHECK(sp[6].mode == "koth" && sp[6].team == 0 && !sp[6].start);
    CHECK(sp[7].mode == "dd" && sp[7].team == 0 && sp[7].start);
    CHECK(std::fabs(sp[0].yaw - 90.0) < 1e-9);
    CHECK(sp[2].yaw == 0.0);  // no angles key
}

TEST(spawn_sight_blocked_then_clear) {
    // Allies at x=-400 and axis at x=+400 on the floor, eyes at 60. A wall at x -16..16 blocks the eye-to-eye ray.
    const std::string spawns = spawn("mp_tdm_spawn_allies_start", -400, 0, 0) + spawn("mp_tdm_spawn_axis_start", 400, 0, 0);
    const std::string wall = brushBox(-16, -512, 0, 16, 512, 256);

    const MapFile blocked = makeMap(floorWorld() + wall + "}\n" + spawns);
    CollisionWorld wb;
    wb.build(blocked);
    const std::vector<SpawnPoint> spB = collectSpawns(blocked);
    const std::vector<SpawnExposure> exB = analyseSpawns(spB, wb);
    CHECK(exB.size() == 2);
    if (exB.size() != 2) return;
    CHECK(exB[0].visibleEnemies == 0 && exB[1].visibleEnemies == 0);
    CHECK(exB[0].visibleAny == 0 && exB[1].visibleAny == 0);
    CHECK(std::fabs(exB[0].nearestEnemy - 800) < 1e-6);  // distance still reported when blocked

    const MapFile open = makeMap(floorWorld() + "}\n" + spawns);
    CollisionWorld wo;
    wo.build(open);
    const std::vector<SpawnPoint> spO = collectSpawns(open);
    const std::vector<SpawnExposure> exO = analyseSpawns(spO, wo);
    CHECK(exO.size() == 2);
    if (exO.size() != 2) return;
    CHECK(exO[0].visibleEnemies == 1 && exO[1].visibleEnemies == 1);
    CHECK(exO[0].visibleAny == 1 && exO[1].visibleAny == 1);
    CHECK(std::fabs(exO[0].nearestEnemy - 800) < 1e-6);
    CHECK(std::isinf(exO[0].nearestFriendly));  // no friendly spawn

    // Beyond maxDistance nothing counts as visible.
    const std::vector<SpawnExposure> far = analyseSpawns(spO, wo, 500);
    CHECK(far.size() == 2 && far[0].visibleEnemies == 0);
}

TEST(spawn_too_close_warning) {
    const MapFile close = makeMap(spawn("mp_dm_spawn", 0, 0, 0) + spawn("mp_dm_spawn", 40, 0, 0));
    const std::vector<SpawnPoint> sp = collectSpawns(close);
    CollisionWorld w;
    const std::vector<std::string> warn = spawnWarnings(sp, analyseSpawns(sp, w));
    CHECK(warn.size() == 2);  // both spawns are reported
    CHECK(anyContains(warn, "is 40 units from another spawn (spawn trap risk)"));
    CHECK(anyContains(warn, "mp_dm_spawn (entity"));

    const MapFile far = makeMap(spawn("mp_dm_spawn", 0, 0, 0) + spawn("mp_dm_spawn", 600, 0, 0));
    const std::vector<SpawnPoint> sf = collectSpawns(far);
    CHECK(spawnWarnings(sf, analyseSpawns(sf, w)).empty());
}

TEST(spawn_warning_sight_camping_and_team_spacing) {
    // One allies start sees three axis starts (no walls). The axis starts are 300 apart from each other, so they are
    // not too close. The team pairs average ~1211 units, so no team-spacing warning.
    const MapFile m = makeMap(spawn("mp_tdm_spawn_allies_start", 0, 0, 0) + spawn("mp_tdm_spawn_axis_start", 1200, 0, 0) +
                              spawn("mp_tdm_spawn_axis_start", 1200, 300, 0) + spawn("mp_tdm_spawn_axis_start", 1200, -300, 0));
    CollisionWorld w;
    const std::vector<SpawnPoint> sp = collectSpawns(m);
    const std::vector<SpawnExposure> ex = analyseSpawns(sp, w);
    CHECK(ex.size() == 4);
    if (ex.size() != 4) return;
    CHECK(ex[0].visibleEnemies == 3);
    CHECK(ex[1].visibleEnemies == 1);  // each axis start sees the one allies start
    const std::vector<std::string> warn = spawnWarnings(sp, ex);
    CHECK(warn.size() == 1);
    CHECK(anyContains(warn, "mp_tdm_spawn_allies_start (entity"));
    CHECK(anyContains(warn, "sees 3 axis start spawns"));
    CHECK(!anyContains(warn, "mode tdm"));
}

TEST(spawn_warning_team_start_spacing) {
    // Allies and axis starts 300 apart: a spawn-trap style warning at the mode level.
    const MapFile m = makeMap(spawn("mp_tdm_spawn_allies_start", 0, 0, 0) + spawn("mp_tdm_spawn_axis_start", 300, 0, 0));
    CollisionWorld w;
    const std::vector<SpawnPoint> sp = collectSpawns(m);
    const std::vector<std::string> warn = spawnWarnings(sp, analyseSpawns(sp, w));
    CHECK(anyContains(warn, "mode tdm: allies starts are on average 300 units from axis starts"));
    CHECK(!anyContains(warn, "spawn trap"));  // 300 is above kMinSpawnSpacing
}

TEST(spawn_density_peaks_at_spawn_cell) {
    // Bounds -512..512 in 8x8 cells of 128: cell (3,3) is centred on (-64,-64), the spawn position.
    const MapFile m = makeMap(spawn("mp_dm_spawn", -64, -64, 0));
    const std::vector<SpawnPoint> sp = collectSpawns(m);
    AABB bounds;
    bounds.add(Vec3{-512, -512, -100});
    bounds.add(Vec3{512, 512, 100});
    const std::vector<double> grid = spawnDensity(sp, bounds, 8, 8, 200);
    CHECK(grid.size() == 64);
    if (grid.size() != 64) return;

    CHECK(std::fabs(grid[3 * 8 + 3] - 1.0) < 1e-9);  // the spawn's own cell
    size_t peak = 0;
    for (size_t i = 1; i < grid.size(); ++i)
        if (grid[i] > grid[peak]) peak = i;
    CHECK(peak == 3 * 8 + 3);
    CHECK(grid[3 * 8 + 4] > 0.0 && grid[3 * 8 + 4] < 1.0);  // neighbour at 128 units, inside the radius
    CHECK(grid[0] == 0.0);                                   // far corner, outside the 200 radius
    CHECK(grid[7 * 8 + 7] == 0.0);

    // Degenerate inputs give an all-zero grid of the right size, or nothing.
    CHECK(spawnDensity(sp, bounds, 8, 8, 0).size() == 64);
    CHECK(spawnDensity(sp, bounds, 0, 8, 200).empty());
}
