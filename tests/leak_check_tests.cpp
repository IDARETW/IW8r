// Leak (sealing) check (src/core/leak_check.cpp): sealed boxes, a removed wall, detail and trigger walls,
// cell size, grid limit, and the IW8 sample room (which has no ceiling and leaks).
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "leak_check.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

std::string brushText(const AABB& box, const std::string& mat, const std::string& directive = "") {
    auto prim = makeBrushPrimitive(makeBox(box, " " + mat + " 128 128 0 0 0 0"), "\n");
    if (!prim) return {};
    if (!directive.empty()) prim->lines.insert(prim->lines.begin() + 1, TextLine{" " + directive, "\n"});
    return prim->text();
}

std::string pointEntity(const std::string& cls, const std::string& origin) {
    return "{\n\"classname\" \"" + cls + "\"\n\"origin\" \"" + origin + "\"\n}\n";
}

MapFile parseText(const std::string& text) {
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    return pr.map ? std::move(*pr.map) : MapFile{};
}

// Room of 288 x 288 interior with 16-unit walls: floor z -16..0, ceiling z 256..272, walls to z 256.
// `east` is the east wall's material and directive; `omitEast` drops it entirely.
std::string roomMap(const std::string& eastMat, const std::string& eastDirective, bool omitEast, bool ceiling = true) {
    const AABB floorB{{-144, -144, -16}, {144, 144, 0}};
    const AABB ceilB{{-144, -144, 256}, {144, 144, 272}};
    const AABB westB{{-144, -144, 0}, {-128, 144, 256}};
    const AABB eastB{{128, -144, 0}, {144, 144, 256}};
    const AABB southB{{-128, -144, 0}, {128, -128, 256}};
    const AABB northB{{-128, 128, 0}, {128, 144, 256}};
    std::string t = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n";
    t += brushText(floorB, "room/floor");
    if (ceiling) t += brushText(ceilB, "room/ceiling");
    t += brushText(westB, "room/wall");
    if (!omitEast) t += brushText(eastB, eastMat, eastDirective);
    t += brushText(southB, "room/wall");
    t += brushText(northB, "room/wall");
    t += "}\n";
    t += pointEntity("mp_tdm_spawn", "0 0 40");
    return t;
}

bool insideAABB(const AABB& b, const Vec3& p) {
    return p.x >= b.mins.x && p.x <= b.maxs.x && p.y >= b.mins.y && p.y <= b.maxs.y && p.z >= b.mins.z && p.z <= b.maxs.z;
}

}  // namespace

// A closed six-brush room with a spawn inside is sealed.
TEST(leak_sealed_box_room) {
    const MapFile m = parseText(roomMap("room/wall", "", false));
    const LeakResult r = checkLeak(m);
    CHECK(r.sealed);
    CHECK(r.error.empty());
    CHECK(r.path.empty());
    CHECK(r.solidCells > 0);
    CHECK(r.cells > r.solidCells);
    // The same room is sealed on other cell sizes (walls are 16 thick).
    CHECK(checkLeak(m, 32).sealed);
    CHECK(checkLeak(m, 8).sealed);
}

// Removing the east wall opens the room. The path starts at the spawn and ends outside the brushes.
TEST(leak_removed_wall_gives_path) {
    const MapFile m = parseText(roomMap("", "", true));
    const LeakResult r = checkLeak(m);
    CHECK(!r.sealed);
    CHECK(r.error.empty());
    CHECK(r.leakingEntity == 1);
    CHECK(r.classname == "mp_tdm_spawn");
    CHECK(r.path.size() >= 3);
    if (r.path.size() < 2) return;
    CHECK(r.path.front() == Vec3(0, 0, 40));
    const AABB bounds{{-144, -144, -16}, {144, 144, 272}};
    CHECK(!insideAABB(bounds, r.path.back()));
    // Consecutive points are one cell apart (the first step is from the origin into its cell).
    for (size_t i = 1; i < r.path.size(); ++i) {
        const double d = length(r.path[i] - r.path[i - 1]);
        CHECK(d <= 16.0 + 1e-6);
        CHECK(d > 0.0);
    }
    // The path leaves through the gap in the east wall (x grows past the wall at 128..144).
    bool passedGap = false;
    for (const Vec3& p : r.path)
        if (p.x > 130 && p.x < 150 && std::fabs(p.y) < 140) passedGap = true;
    CHECK(passedGap);
}

// A missing ceiling is a leak through the top.
TEST(leak_missing_ceiling) {
    const MapFile m = parseText(roomMap("room/wall", "", false, false));
    const LeakResult r = checkLeak(m);
    CHECK(!r.sealed);
    CHECK(r.leakingEntity == 1);
    CHECK(!r.path.empty());
}

// `contents detail;` walls do not seal.
TEST(leak_detail_wall_does_not_seal) {
    const MapFile m = parseText(roomMap("room/wall", "contents detail;", false));
    const LeakResult r = checkLeak(m);
    CHECK(!r.sealed);
    CHECK(r.leakingEntity == 1);
    CHECK(r.path.size() >= 2);
}

// Trigger and nonColliding walls never seal.
TEST(leak_trigger_and_noncolliding_walls_do_not_seal) {
    CHECK(!checkLeak(parseText(roomMap("trigger", "", false))).sealed);
    CHECK(!checkLeak(parseText(roomMap("room/wall", "contents nonColliding;", false))).sealed);
    CHECK(!checkLeak(parseText(roomMap("hint", "", false))).sealed);
    // A clip wall does seal (collides).
    CHECK(checkLeak(parseText(roomMap("clip", "", false))).sealed);
}

// A func_group wall seals like worldspawn; a brush entity that is not worldspawn or func_group does not.
TEST(leak_func_group_seals_other_brush_entity_does_not) {
    const AABB floorB{{-144, -144, -16}, {144, 144, 0}};
    const AABB wallB{{-144, -144, 0}, {-128, 144, 256}};
    std::string t = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + brushText(floorB, "room/floor") + "}\n";
    t += "{\n\"classname\" \"func_group\"\n" + brushText(wallB, "room/wall") + "}\n";
    t += pointEntity("mp_tdm_spawn", "0 0 40");
    const LeakResult r = checkLeak(parseText(t));
    CHECK(!r.sealed);      // three walls are missing: the spawn leaks
    CHECK(r.leakingEntity == 2);

    std::string t2 = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + brushText(floorB, "room/floor") + "}\n";
    t2 += "{\n\"classname\" \"script_brushmodel\"\n" + brushText(wallB, "room/wall") + "}\n";
    // The grid is the floor's bounds padded by two cells (z up to 32), so the spawn must sit inside it.
    t2 += pointEntity("mp_tdm_spawn", "0 0 8");
    CHECK(checkLeak(parseText(t2)).leakingEntity == 2);
    CHECK(checkLeak(parseText(t2)).sealed == false);
}

// Grid limit and invalid cell size are reported as errors (sealed stays false).
TEST(leak_grid_limit_and_bad_cell) {
    const MapFile m = parseText(roomMap("room/wall", "", false));
    const LeakResult tooBig = checkLeak(m, 1.0, 1000);
    CHECK(!tooBig.sealed);
    CHECK(!tooBig.error.empty());
    CHECK(!checkLeak(m, 0.0).error.empty());
    CHECK(!checkLeak(m, -4.0).sealed);
}

// No sealing brushes at all: nothing to check, reported as an error.
TEST(leak_no_brushes_is_error) {
    const MapFile m = parseText("iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n" + pointEntity("mp_tdm_spawn", "0 0 0"));
    const LeakResult r = checkLeak(m);
    CHECK(!r.sealed);
    CHECK(!r.error.empty());
}

// The IW8 sample room has no ceiling brush, so its spawn leaks through the top. The south wall is detail and
// does not seal either. The first point entity (info_player_start, entity 1) is reported.
TEST(leak_iw8_sample_room_leaks) {
    fs::path p = fs::current_path();
    std::string sample;
    for (int up = 0; up < 6 && sample.empty(); ++up, p = p.parent_path())
        if (fs::exists(p / "tests" / "data" / "iw8_sample_room.map")) sample = (p / "tests" / "data" / "iw8_sample_room.map").string();
    CHECK(!sample.empty());
    if (sample.empty()) return;
    std::string text;
    CHECK(readFile(sample, text));
    const MapFile m = parseText(text);
    const LeakResult r = checkLeak(m);
    CHECK(!r.sealed);
    CHECK(r.error.empty());
    CHECK(r.leakingEntity == 1);
    CHECK(r.classname == "info_player_start");
    CHECK(r.path.size() >= 3);
    if (!r.path.empty()) CHECK(r.path.front() == Vec3(0, -192, 16));
}
