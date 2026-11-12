// Gameplay checks (src/core/gameplay_checks.cpp): a clean base map with no issues, one scenario per rule
// (pillar overlap, spawn proximity, floating spawn, missing intermission, mode minimums, pitch/roll), and the
// IW8 sample room.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "gameplay_checks.h"
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

std::string entityText(const std::string& cls, const std::string& origin, const std::string& angles = "") {
    std::string s = "{\n\"classname\" \"" + cls + "\"\n\"origin\" \"" + origin + "\"\n";
    if (!angles.empty()) s += "\"angles\" \"" + angles + "\"\n";
    return s + "}\n";
}

// Floor of 1024 x 1024 and a complete TDM / DM / intermission set. Base has no issues.
// `extraBrushes` are worldspawn brushes; `extraEntities` follow the base entities.
struct BaseOptions {
    bool intermission = true;
    bool allies = true;
    int dmCount = 2;
    std::string tdmSpawnAngles;
};

std::string baseMap(const BaseOptions& o = {}, const std::string& extraBrushes = "", const std::string& extraEntities = "") {
    const AABB floorB{{-512, -512, -16}, {512, 512, 0}};
    std::string t = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + brushText(floorB, "room/floor") + extraBrushes + "}\n";
    t += entityText("mp_tdm_spawn", "-128 0 0", o.tdmSpawnAngles);
    t += entityText("mp_tdm_spawn", "128 0 0");
    if (o.allies) t += entityText("mp_tdm_spawn_allies_start", "-256 0 0");
    t += entityText("mp_tdm_spawn_axis_start", "256 0 0");
    if (o.dmCount >= 1) t += entityText("mp_dm_spawn", "-128 256 0");
    if (o.dmCount >= 2) t += entityText("mp_dm_spawn", "128 256 0");
    if (o.intermission) t += entityText("mp_global_intermission", "0 0 256");
    return t + extraEntities;
}

std::vector<GameplayIssue> issuesOf(const std::string& text) {
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return {};
    return gameplayIssues(*pr.map);
}

bool hasIssue(const std::vector<GameplayIssue>& v, size_t entity, const std::string& part) {
    for (const GameplayIssue& i : v)
        if (i.entity == entity && i.message.find(part) != std::string::npos) return true;
    return false;
}

bool anyIssue(const std::vector<GameplayIssue>& v, const std::string& part) {
    for (const GameplayIssue& i : v)
        if (i.message.find(part) != std::string::npos) return true;
    return false;
}

}  // namespace

// A complete map with no problems: no issues.
TEST(gameplay_base_map_has_no_issues) {
    const auto v = issuesOf(baseMap());
    if (!v.empty())
        for (const GameplayIssue& i : v) std::printf("    issue entity %zu %s: %s\n", i.entity, i.classname.c_str(), i.message.c_str());
    CHECK(v.empty());
}

// A spawn inside a solid pillar: the player box overlaps it. Entity 1 is the first TDM spawn.
TEST(gameplay_spawn_inside_pillar) {
    const AABB pillar{{-160, -40, 0}, {-96, 40, 128}};
    const auto v = issuesOf(baseMap({}, brushText(pillar, "tm/concrete_pillar")));
    CHECK(hasIssue(v, 1, "overlaps a solid brush"));
    CHECK(v.size() == 1);
}

// A detail pillar also blocks the player box.
TEST(gameplay_spawn_inside_detail_pillar) {
    const AABB pillar{{-160, -40, 0}, {-96, 40, 128}};
    const auto v = issuesOf(baseMap({}, brushText(pillar, "tm/concrete_pillar", "contents detail;")));
    CHECK(hasIssue(v, 1, "overlaps a solid brush"));
}

// A trigger brush does not block the player: no overlap issue.
TEST(gameplay_spawn_inside_trigger_is_fine) {
    const AABB pillar{{-160, -40, 0}, {-96, 40, 128}};
    const auto v = issuesOf(baseMap({}, brushText(pillar, "trigger")));
    CHECK(!anyIssue(v, "overlaps"));
}

// Two spawns 9.4 units apart: reported on the later entity.
TEST(gameplay_overlapping_spawns) {
    const auto v = issuesOf(baseMap({}, "", entityText("mp_tdm_spawn", "-120 5 0")));
    // Base entities: 0 worldspawn, 1 tdm, 2 tdm, 3 allies, 4 axis, 5 dm, 6 dm, 7 intermission; new one is 8.
    CHECK(hasIssue(v, 8, "within 32 units of entity 1"));
    CHECK(v.size() == 1);
}

// A spawn 200 units above the floor has no floor within 128 units below it.
TEST(gameplay_floating_spawn) {
    const auto v = issuesOf(baseMap({}, "", entityText("mp_dm_spawn", "0 400 200")));
    CHECK(hasIssue(v, 8, "no floor within 128 units"));
    CHECK(v.size() == 1);
}

// A spawn 100 units above the floor is fine (floor within 128).
TEST(gameplay_spawn_above_floor_within_range_is_fine) {
    const auto v = issuesOf(baseMap({}, "", entityText("mp_dm_spawn", "0 400 100")));
    CHECK(!anyIssue(v, "floor"));
}

// No mp_global_intermission entity.
TEST(gameplay_missing_intermission) {
    BaseOptions o;
    o.intermission = false;
    const auto v = issuesOf(baseMap(o));
    CHECK(anyIssue(v, "missing mp_global_intermission"));
    CHECK(v.size() == 1);
}

// TDM: an allies start is required; a single mp_tdm_spawn is not enough.
TEST(gameplay_tdm_minimums) {
    BaseOptions o;
    o.allies = false;
    const auto v = issuesOf(baseMap(o));
    CHECK(anyIssue(v, "TDM: no mp_tdm_spawn_allies_start"));
    CHECK(v.size() == 1);

    const std::string oneTdm = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n" + entityText("mp_tdm_spawn", "-128 0 0") +
                               entityText("mp_tdm_spawn_allies_start", "-256 0 0") +
                               entityText("mp_tdm_spawn_axis_start", "256 0 0") + entityText("mp_global_intermission", "0 0 256");
    CHECK(anyIssue(issuesOf(oneTdm), "TDM: 1 mp_tdm_spawn, at least 2 required"));
}

// DM: when DM spawns exist there must be at least two mp_dm_spawn.
TEST(gameplay_dm_minimum) {
    BaseOptions o;
    o.dmCount = 1;
    const auto v = issuesOf(baseMap(o));
    CHECK(anyIssue(v, "DM: 1 mp_dm_spawn"));
    CHECK(v.size() == 1);

    BaseOptions none;
    none.dmCount = 0;
    CHECK(issuesOf(baseMap(none)).empty());  // no DM spawns at all: no DM rule
}

// DOM: mp_dom_spawn requires allies and axis starts.
TEST(gameplay_dom_starts_required_with_dom_spawn) {
    const auto v = issuesOf(baseMap({}, "", entityText("mp_dom_spawn", "0 -400 0")));
    CHECK(anyIssue(v, "DOM: no mp_dom_spawn_allies_start"));
    CHECK(anyIssue(v, "DOM: no mp_dom_spawn_axis_start"));
    CHECK(v.size() == 2);

    const auto ok = issuesOf(baseMap({}, "", entityText("mp_dom_spawn", "0 -400 0") +
                                                 entityText("mp_dom_spawn_allies_start", "0 -450 0") +
                                                 entityText("mp_dom_spawn_axis_start", "0 -350 0")));
    CHECK(ok.empty());
}

// Pitch or roll other than 0 on a spawn. angles is "pitch yaw roll".
TEST(gameplay_spawn_pitch_roll) {
    const auto v = issuesOf(baseMap(BaseOptions{true, true, 2, "10 90 0"}));
    CHECK(hasIssue(v, 1, "pitch and roll"));
    const auto roll = issuesOf(baseMap(BaseOptions{true, true, 2, "0 90 5"}));
    CHECK(hasIssue(roll, 1, "pitch and roll"));
    const auto yawOnly = issuesOf(baseMap(BaseOptions{true, true, 2, "0 170 0"}));
    CHECK(yawOnly.empty());
}

// The IW8 sample room: a TDM spawn count of 1, no mp_dm_spawn while mp_dm_spawn_start exists. Nothing else.
TEST(gameplay_iw8_sample_room_issues) {
    fs::path p = fs::current_path();
    std::string sample;
    for (int up = 0; up < 6 && sample.empty(); ++up, p = p.parent_path())
        if (fs::exists(p / "tests" / "data" / "iw8_sample_room.map")) sample = (p / "tests" / "data" / "iw8_sample_room.map").string();
    CHECK(!sample.empty());
    if (sample.empty()) return;
    std::string text;
    CHECK(readFile(sample, text));
    const auto v = issuesOf(text);
    for (const GameplayIssue& i : v) std::printf("    sample issue entity %zu %s: %s\n", i.entity, i.classname.c_str(), i.message.c_str());
    CHECK(anyIssue(v, "DM: 0 mp_dm_spawn"));
    CHECK(anyIssue(v, "TDM: 1 mp_tdm_spawn"));
    CHECK(anyIssue(v, "built as a static prop"));  // the ramp: a decorative script_model (doc 34)
    CHECK(v.size() == 3);
    CHECK(!anyIssue(v, "overlaps"));
    CHECK(!anyIssue(v, "floor"));
    CHECK(!anyIssue(v, "intermission"));
    CHECK(!anyIssue(v, "pitch"));
    CHECK(!anyIssue(v, "within 32"));
}
