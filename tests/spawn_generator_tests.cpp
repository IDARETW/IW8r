// Spawn generator (6.x): walkable sampling over the collision world, FFA and team starts, warnings, determinism.
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "collision_world.h"
#include "map_text.h"
#include "spawn_generator.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

// Brush text from convex face triples. Each face is three points on its plane; the winding is chosen so the normal
// points away from `inside` (a point strictly inside the brush).
std::string brushText(std::vector<std::array<Vec3, 3>> faces, const Vec3& inside) {
    auto pt = [](const Vec3& p) {
        return "( " + formatNumber(p.x) + " " + formatNumber(p.y) + " " + formatNumber(p.z) + " )";
    };
    std::string t = "{\n";
    for (auto& f : faces) {
        Vec3 a = f[0], b = f[1], c = f[2];
        if (dot(cross(a - b, c - b), a - inside) < 0) std::swap(b, c);
        t += " " + pt(a) + " " + pt(b) + " " + pt(c) + " floor 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n";
    }
    return t + "}\n";
}

std::string boxBrush(const Vec3& lo, const Vec3& hi) {
    auto P = [](double x, double y, double z) { return Vec3{x, y, z}; };
    std::vector<std::array<Vec3, 3>> f = {
        {P(lo.x, lo.y, lo.z), P(hi.x, lo.y, lo.z), P(hi.x, hi.y, lo.z)},  // bottom
        {P(lo.x, lo.y, hi.z), P(hi.x, lo.y, hi.z), P(hi.x, hi.y, hi.z)},  // top
        {P(lo.x, lo.y, lo.z), P(hi.x, lo.y, lo.z), P(hi.x, lo.y, hi.z)},  // -y
        {P(lo.x, hi.y, lo.z), P(hi.x, hi.y, lo.z), P(hi.x, hi.y, hi.z)},  // +y
        {P(lo.x, lo.y, lo.z), P(lo.x, hi.y, lo.z), P(lo.x, hi.y, hi.z)},  // -x
        {P(hi.x, lo.y, lo.z), P(hi.x, hi.y, lo.z), P(hi.x, hi.y, hi.z)},  // +x
    };
    return brushText(f, (lo + hi) * 0.5);
}

// A triangular prism ramp rising along +x: 200 units of run, 346 of rise (about 60 degrees), 400 wide in Y.
std::string ramp60() {
    const Vec3 p0{0, -200, 0}, p1{200, -200, 0}, p2{200, -200, 346};
    const Vec3 p3{0, 200, 0}, p4{200, 200, 0}, p5{200, 200, 346};
    std::vector<std::array<Vec3, 3>> f = {
        {p0, p1, p2},  // -y end
        {p3, p4, p5},  // +y end
        {p0, p1, p4},  // bottom
        {p0, p3, p2},  // sloped top
        {p1, p2, p5},  // vertical back
    };
    return brushText(f, Vec3{66, 0, 115});
}

MapFile mapOf(const std::string& body) {
    const std::string t = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + body + "}\n";
    Id next = 1;
    ParseResult r = parseMap(t, next);
    return r.map ? *r.map : MapFile{};
}

// Floor 2048x1024 (z -16..0) with four walls to z 128. The walkable area is the floor top inside the walls.
std::string flatRoom() {
    return boxBrush({-1024, -512, -16}, {1024, 512, 0}) + boxBrush({-1040, -528, 0}, {-1024, 528, 128}) +
           boxBrush({1024, -528, 0}, {1040, 528, 128}) + boxBrush({-1024, 512, 0}, {1024, 528, 128}) +
           boxBrush({-1024, -528, 0}, {1024, -512, 128});
}

std::vector<GeneratedSpawn> ofClass(const SpawnGenResult& r, const std::string& cls) {
    std::vector<GeneratedSpawn> out;
    for (const GeneratedSpawn& s : r.spawns)
        if (s.classname == cls) out.push_back(s);
    return out;
}

bool anyContains(const std::vector<std::string>& v, const std::string& s) {
    for (const std::string& x : v)
        if (x.find(s) != std::string::npos) return true;
    return false;
}

}  // namespace

TEST(spawn_gen_flat_floor_spawns) {
    CollisionWorld w;
    w.build(mapOf(flatRoom()));
    const AABB area = [] {
        AABB a;
        a.add(Vec3{-1024, -512, -16});
        a.add(Vec3{1024, 512, 128});
        return a;
    }();
    const auto t0 = std::chrono::steady_clock::now();
    const SpawnGenResult res = generateSpawns(w, area);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  generateSpawns 2048x1024 flat floor: %.2f ms, %zu candidates, %zu spawns\n", ms, res.candidates,
                res.spawns.size());

    CHECK(res.candidates > 16);
    CHECK(res.warnings.empty());
    const auto dm = ofClass(res, "mp_dm_spawn");
    const auto tdm = ofClass(res, "mp_tdm_spawn");
    const auto dom = ofClass(res, "mp_dom_spawn");
    CHECK(dm.size() == 16);
    CHECK(tdm.size() == 16);
    CHECK(dom.size() == 16);
    bool sameTdm = tdm.size() == dm.size();
    for (size_t i = 0; sameTdm && i < dm.size(); ++i) sameTdm = tdm[i].origin == dm[i].origin;
    CHECK(sameTdm);

    bool spaced = true;
    for (size_t i = 0; i < dm.size(); ++i)
        for (size_t j = i + 1; j < dm.size(); ++j)
            if (length(dm[i].origin - dm[j].origin) < 256 - 1e-6) spaced = false;
    CHECK(spaced);

    bool onFloor = true;
    for (const GeneratedSpawn& s : res.spawns) onFloor &= std::fabs(s.origin.z) < 1e-6;
    CHECK(onFloor);

    const auto allies = ofClass(res, "mp_tdm_spawn_allies_start");
    const auto axis = ofClass(res, "mp_tdm_spawn_axis_start");
    CHECK(allies.size() == 4);
    CHECK(axis.size() == 4);
    bool halves = true;
    for (const GeneratedSpawn& s : allies) halves &= s.origin.x < 0;
    for (const GeneratedSpawn& s : axis) halves &= s.origin.x > 0;
    CHECK(halves);
    CHECK(ofClass(res, "mp_dom_spawn_allies_start").size() == 4);
    CHECK(ofClass(res, "mp_dom_spawn_axis_start").size() == 4);

    // Yaw faces the centre (the origin): within 10 degrees for spawns away from the middle.
    bool facing = true;
    for (const GeneratedSpawn& s : dm) {
        if (std::hypot(s.origin.x, s.origin.y) < 100) continue;
        const double expect = std::atan2(-s.origin.y, -s.origin.x) / (std::acos(-1.0) / 180.0);
        const double diff = std::fmod(s.yaw - expect + 540.0, 360.0) - 180.0;
        facing &= std::fabs(diff) < 10;
    }
    CHECK(facing);
}

TEST(spawn_gen_low_ceiling_has_no_candidates) {
    // Floor at z 0 and a roof at z 40: 40 units of headroom. The roof top (z 400) is above the sky trace start.
    CollisionWorld w;
    w.build(mapOf(boxBrush({-512, -512, -16}, {512, 512, 0}) + boxBrush({-512, -512, 40}, {512, 512, 400})));
    AABB area;
    area.add(Vec3{-512, -512, -16});
    area.add(Vec3{512, 512, 128});
    const SpawnGenResult res = generateSpawns(w, area);
    CHECK(res.candidates == 0);
    CHECK(res.spawns.empty());
    CHECK(anyContains(res.warnings, "no walkable"));
}

TEST(spawn_gen_steep_ramp_is_not_walkable) {
    // The ramp alone: no floor at all, so nothing is walkable.
    CollisionWorld alone;
    alone.build(mapOf(ramp60()));
    AABB area;
    area.add(Vec3{-50, -250, -16});
    area.add(Vec3{250, 250, 400});
    const SpawnGenResult none = generateSpawns(alone, area);
    CHECK(none.candidates == 0);
    CHECK(anyContains(none.warnings, "no walkable"));

    // The ramp on a floor: the floor counts, the 60 degree slope never does, so every spawn stays at floor level.
    CollisionWorld withFloor;
    withFloor.build(mapOf(boxBrush({-200, -300, -16}, {400, 300, 0}) + ramp60()));
    AABB wide;
    wide.add(Vec3{-200, -300, -16});
    wide.add(Vec3{400, 300, 400});
    const SpawnGenResult res = generateSpawns(withFloor, wide);
    CHECK(res.candidates > 0);
    CHECK(!res.spawns.empty());
    bool floorLevel = true;
    for (const GeneratedSpawn& s : res.spawns) floorLevel &= std::fabs(s.origin.z) < 1e-6;
    CHECK(floorLevel);
}

TEST(spawn_gen_two_storeys) {
    // Floor at z 0 and a platform with its top at z 256 over x 0..512 (240 to 256, so the floor under it has headroom).
    CollisionWorld w;
    w.build(mapOf(boxBrush({-512, -256, -16}, {512, 256, 0}) + boxBrush({0, -256, 240}, {512, 256, 256})));
    AABB area;
    area.add(Vec3{-512, -256, -16});
    area.add(Vec3{512, 256, 256});
    const SpawnGenResult res = generateSpawns(w, area);
    bool upper = false, lower = false;
    for (const GeneratedSpawn& s : res.spawns) {
        if (std::fabs(s.origin.z - 256) < 1e-6) upper = true;
        if (std::fabs(s.origin.z) < 1e-6) lower = true;
    }
    CHECK(upper);
    CHECK(lower);
    CHECK(res.candidates > 16);
}

TEST(spawn_gen_asks_for_more_than_fit) {
    // A 512x512 floor fits far fewer than 16 points 256 apart.
    CollisionWorld w;
    w.build(mapOf(boxBrush({-256, -256, -16}, {256, 256, 0})));
    AABB area;
    area.add(Vec3{-256, -256, -16});
    area.add(Vec3{256, 256, 128});
    const SpawnGenResult res = generateSpawns(w, area);
    const size_t n = ofClass(res, "mp_dm_spawn").size();
    CHECK(n >= 1);
    CHECK(n < 16);
    CHECK(anyContains(res.warnings, "asked"));
}

TEST(spawn_gen_is_deterministic) {
    CollisionWorld w;
    w.build(mapOf(flatRoom()));
    AABB area;
    area.add(Vec3{-1024, -512, -16});
    area.add(Vec3{1024, 512, 128});
    const SpawnGenResult a = generateSpawns(w, area);
    const SpawnGenResult b = generateSpawns(w, area);
    CHECK(a.candidates == b.candidates);
    CHECK(a.spawns.size() == b.spawns.size());
    std::string ta, tb;
    for (const GeneratedSpawn& s : a.spawns) ta += spawnEntityText(s);
    for (const GeneratedSpawn& s : b.spawns) tb += spawnEntityText(s);
    CHECK(!ta.empty());
    CHECK(ta == tb);
}

TEST(spawn_gen_entity_text_format) {
    GeneratedSpawn s;
    s.classname = "mp_dm_spawn";
    s.origin = Vec3{100, -200.5, 256};
    s.yaw = 90;
    CHECK(spawnEntityText(s) == "{\n\"classname\" \"mp_dm_spawn\"\n\"origin\" \"100 -200.5 256\"\n\"angles\" \"0 90 0\"\n}\n");
    s.origin = Vec3{0, 0, 0.0};
    s.yaw = 0;
    CHECK(spawnEntityText(s) == "{\n\"classname\" \"mp_dm_spawn\"\n\"origin\" \"0 0 0\"\n\"angles\" \"0 0 0\"\n}\n");
}
