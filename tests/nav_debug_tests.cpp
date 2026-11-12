// Navigation debug dump reader (src/core/nav_debug.*): a hand-written dump in the converter's schema
// parses, overlay lines and summary come out right, and malformed dumps are rejected.
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <string>

#include "nav_debug.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

// Same shape as games/iw8/common/replay_navigation.cpp (the IW8_ZONETOOL_NAV_DEBUG block): a 2x2 height
// grid (index iy * nx + ix), one square area, two nodes linked 0 -> 1, one spawn.
const char* kSmallDump = R"JSON({"minX":0,"minY":0,"cell":8,"nx":2,"ny":2,"top":[10,10,-99999,12],)JSON"
                         R"JSON("areas":[[[0,0,10,-1],[64,0,10,-1],[64,64,10,-1],[0,64,10,-1]]],)JSON"
                         R"JSON("nodes":[[32,32,10,1,[1]],[48,40,10,1,[0]]],"fixed":2,"zones":1,"spawns":[[8,8,10]]})JSON";

bool contains(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

bool parses(const std::string& text) {
    NavDebug d;
    std::string err;
    return readNavDebug(text, d, &err);
}

}  // namespace

TEST(nav_debug_parses_small_dump) {
    NavDebug d;
    std::string err;
    const bool ok = readNavDebug(kSmallDump, d, &err);
    CHECK(ok);
    if (!ok) std::printf("  error: %s\n", err.c_str());
    if (!ok) return;

    CHECK(d.minX == 0.0 && d.minY == 0.0);
    CHECK(d.cell == 8.0);
    CHECK(d.nx == 2 && d.ny == 2);
    CHECK(d.top.size() == 4);
    CHECK(d.top[0] == 10.0 && d.top[3] == 12.0);
    CHECK(d.top[2] == -99999.0);

    CHECK(d.areaPolys.size() == 1);
    CHECK(d.areaPolys[0].size() == 4);
    CHECK(d.areaPolys[0][2] == (Vec3{64, 64, 10}));
    CHECK(d.areaAdjacent.size() == 1);
    CHECK(d.areaAdjacent[0] == (std::vector<int>{-1, -1, -1, -1}));

    CHECK(d.nodes.size() == 2);
    CHECK(d.nodes[1] == (Vec3{48, 40, 10}));
    CHECK(d.nodeZones.size() == 2 && d.nodeZones[0] == 1 && d.nodeZones[1] == 1);
    // Node 0 lists 1 and node 1 lists 0, so the dump has two directed links.
    CHECK(d.links.size() == 2);
    if (d.links.size() == 2) {
        CHECK(d.links[0].first == (Vec3{32, 32, 10}));
        CHECK(d.links[0].second == (Vec3{48, 40, 10}));
        CHECK(d.links[1].first == (Vec3{48, 40, 10}));
        CHECK(d.links[1].second == (Vec3{32, 32, 10}));
    }

    CHECK(d.spawns.size() == 1 && d.spawns[0] == (Vec3{8, 8, 10}));
    CHECK(d.fixedCount == 2);
    CHECK(d.zoneCount == 1);
}

TEST(nav_debug_overlay_lines_are_lifted) {
    NavDebug d;
    CHECK(readNavDebug(kSmallDump, d, nullptr));

    const auto lines = navDebugLines(d, 2.0);
    // 4 area edges + 2 nodes x (2 cross arms + 1 tick) + 2 links.
    CHECK(lines.size() == 12);
    if (lines.size() != 12) return;

    bool allLifted = true;
    for (const auto& [a, b] : lines) allLifted = allLifted && a.z >= 12.0 - 1e-9 && b.z >= 12.0 - 1e-9;
    CHECK(allLifted);
    CHECK(lines[0].first == (Vec3{0, 0, 12}));
    CHECK(lines[0].second == (Vec3{64, 0, 12}));
    // Area edges close the polygon: last edge goes from corner 3 back to corner 0.
    CHECK(lines[3].first == (Vec3{0, 64, 12}));
    CHECK(lines[3].second == (Vec3{0, 0, 12}));
    // Links are the last segments and keep their endpoints (lifted).
    CHECK(lines[10].first == (Vec3{32, 32, 12}));
    CHECK(lines[10].second == (Vec3{48, 40, 12}));
    CHECK(lines[11].first == (Vec3{48, 40, 12}));
    CHECK(lines[11].second == (Vec3{32, 32, 12}));
}

TEST(nav_debug_overlay_zero_lift_keeps_z) {
    NavDebug d;
    CHECK(readNavDebug(kSmallDump, d, nullptr));
    const auto lines = navDebugLines(d, 0.0);
    CHECK(lines.size() == 12);
    if (!lines.empty()) CHECK(lines[0].first == (Vec3{0, 0, 10}));
}

TEST(nav_debug_summary_counts_and_bounds) {
    NavDebug d;
    CHECK(readNavDebug(kSmallDump, d, nullptr));
    const std::string s = navDebugSummary(d);
    std::printf("  summary: %s\n", s.c_str());
    CHECK(contains(s, "1 areas (4 corners)"));
    CHECK(contains(s, "2 nodes in 1 zones (2 fixed)"));
    CHECK(contains(s, "2 links"));
    CHECK(contains(s, "1 spawns"));
    CHECK(contains(s, "grid 2x2 cell 8.0"));
    CHECK(contains(s, "walkable columns 3/4"));
    CHECK(contains(s, "bounds (0.0, 0.0, 10.0) to (64.0, 64.0, 10.0)"));
}

TEST(nav_debug_summary_empty_dump) {
    NavDebug d;
    CHECK(readNavDebug(R"({"minX":0,"minY":0,"cell":8,"nx":0,"ny":0,"top":[],"areas":[],"nodes":[],)"
                       R"("fixed":0,"zones":0,"spawns":[]})",
                       d, nullptr));
    CHECK(d.areaPolys.empty() && d.nodes.empty() && d.links.empty());
    CHECK(navDebugLines(d, 2.0).empty());
    const std::string s = navDebugSummary(d);
    CHECK(contains(s, "0 areas (0 corners)"));
    CHECK(!contains(s, "bounds"));
}

TEST(nav_debug_rejects_bad_json) {
    CHECK(!parses(""));
    CHECK(!parses("{"));
    CHECK(!parses("[]"));
    CHECK(!parses(std::string(kSmallDump) + " trailing"));
    // The converter prints NaN as "nan", which is not JSON.
    CHECK(!parses(R"({"minX":nan,"minY":0,"cell":8,"nx":0,"ny":0,"top":[],"areas":[],"nodes":[],"fixed":0,"zones":0,"spawns":[]})"));
}

TEST(nav_debug_rejects_missing_or_bad_fields) {
    NavDebug d;
    std::string err;
    CHECK(!readNavDebug(R"({})", d, &err));
    CHECK(!err.empty());
    CHECK(!parses(R"({"minX":0,"minY":0,"nx":0,"ny":0,"top":[],"areas":[],"nodes":[],"fixed":0,"zones":0,"spawns":[]})"));  // no cell
    CHECK(!parses(R"({"minX":0,"minY":0,"cell":0,"nx":0,"ny":0,"top":[],"areas":[],"nodes":[],"fixed":0,"zones":0,"spawns":[]})"));
    CHECK(!parses(R"({"minX":0,"minY":0,"cell":8,"nx":1.5,"ny":1,"top":[1],"areas":[],"nodes":[],"fixed":0,"zones":0,"spawns":[]})"));
    CHECK(!parses(R"({"minX":0,"minY":0,"cell":8,"nx":0,"ny":0,"top":[],"areas":[],"nodes":[],"fixed":0,"zones":0})"));  // no spawns
}

TEST(nav_debug_rejects_inconsistent_arrays) {
    // top length must be nx * ny.
    CHECK(!parses(R"({"minX":0,"minY":0,"cell":8,"nx":2,"ny":2,"top":[1,2,3],"areas":[],"nodes":[],"fixed":0,"zones":0,"spawns":[]})"));
    // An area needs at least 3 corners, each [x, y, z, adjacent].
    CHECK(!parses(R"({"minX":0,"minY":0,"cell":8,"nx":0,"ny":0,"top":[],"areas":[[[0,0,0,-1],[1,0,0,-1]]],"nodes":[],"fixed":0,"zones":0,"spawns":[]})"));
    CHECK(!parses(R"({"minX":0,"minY":0,"cell":8,"nx":0,"ny":0,"top":[],"areas":[[[0,0,0],[1,0,0],[1,1,0]]],"nodes":[],"fixed":0,"zones":0,"spawns":[]})"));
    // Adjacency must name an existing area or -1.
    CHECK(!parses(R"({"minX":0,"minY":0,"cell":8,"nx":0,"ny":0,"top":[],"areas":[[[0,0,0,1],[1,0,0,-1],[1,1,0,-1]]],"nodes":[],"fixed":0,"zones":0,"spawns":[]})"));
    // Node: [x, y, z, zone, [links]], zone >= 0, link indices inside the node list.
    CHECK(!parses(R"({"minX":0,"minY":0,"cell":8,"nx":0,"ny":0,"top":[],"areas":[],"nodes":[[0,0,0,-1,[]]],"fixed":0,"zones":0,"spawns":[]})"));
    CHECK(!parses(R"({"minX":0,"minY":0,"cell":8,"nx":0,"ny":0,"top":[],"areas":[],"nodes":[[0,0,0,0,[5]]],"fixed":0,"zones":0,"spawns":[]})"));
    CHECK(!parses(R"({"minX":0,"minY":0,"cell":8,"nx":0,"ny":0,"top":[],"areas":[],"nodes":[[0,0,0,0]],"fixed":0,"zones":0,"spawns":[]})"));
    // Spawns are [x, y, z].
    CHECK(!parses(R"({"minX":0,"minY":0,"cell":8,"nx":0,"ny":0,"top":[],"areas":[],"nodes":[],"fixed":0,"zones":0,"spawns":[[1,2]]})"));
}

TEST(nav_debug_failure_clears_output) {
    NavDebug d;
    CHECK(readNavDebug(kSmallDump, d, nullptr));
    CHECK(!d.nodes.empty());
    std::string err;
    CHECK(!readNavDebug("{", d, &err));
    CHECK(d.nodes.empty() && d.areaPolys.empty() && d.top.empty());
    CHECK(!err.empty());
}

TEST(nav_debug_reads_a_real_converter_dump_when_present) {
    // Written by x64-zt when the Compile panel sets IW8_ZONETOOL_NAV_DEBUG (6.23); absent on a fresh checkout.
    namespace fs = std::filesystem;
    fs::path p = fs::current_path();
    for (int up = 0; up < 4 && !fs::exists(p / "samples" / "dev"); ++up) p = p.parent_path();
    const fs::path dump = p / "samples" / "dev" / "iw8build" / "mp_iw8r_dev_sho" / "nav_debug.json";
    if (!fs::exists(dump)) return;
    std::string text;
    {
        std::FILE* f = std::fopen(dump.string().c_str(), "rb");
        CHECK(f != nullptr);
        if (!f) return;
        char buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
        std::fclose(f);
    }
    NavDebug nav;
    std::string err;
    CHECK(readNavDebug(text, nav, &err));
    CHECK(!navDebugLines(nav, 2.0).empty());
    CHECK(!navDebugSummary(nav).empty());
}
