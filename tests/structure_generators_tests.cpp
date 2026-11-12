#include "structure_generators.h"
#include "test_harness.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace iw8r;

namespace {

AABB mkBox(Vec3 mn, Vec3 mx) {
    AABB b;
    b.mins = mn;
    b.maxs = mx;
    return b;
}

bool validBox(const AABB& b) {
    return b.valid() && b.maxs.x > b.mins.x && b.maxs.y > b.mins.y && b.maxs.z > b.mins.z;
}

bool insideBox(const AABB& outer, const AABB& in) {
    const double e = 1e-9;
    for (int i = 0; i < 3; ++i) {
        if (in.mins[i] < outer.mins[i] - e || in.maxs[i] > outer.maxs[i] + e) return false;
    }
    return true;
}

bool pointIn(const AABB& b, const Vec3& p) {
    for (int i = 0; i < 3; ++i) {
        if (p[i] < b.mins[i] || p[i] > b.maxs[i]) return false;
    }
    return true;
}

// Positive-volume intersection: touching faces do not count.
bool overlapPositive(const AABB& a, const AABB& b) {
    for (int i = 0; i < 3; ++i) {
        if (std::min(a.maxs[i], b.maxs[i]) - std::max(a.mins[i], b.mins[i]) <= 1e-9) return false;
    }
    return true;
}

bool anyPartContains(const std::vector<StructureBox>& boxes, StructurePart part, const Vec3& p) {
    for (const StructureBox& s : boxes) {
        if (s.part == part && pointIn(s.box, p)) return true;
    }
    return false;
}

}  // namespace

TEST(stairs_256x64x128_climb_plus_x) {
    const AABB box = mkBox(Vec3(0, 0, 0), Vec3(256, 64, 128));
    std::string err = "unset";
    const auto steps = generateStairs(box, StairOptions{}, &err);
    CHECK(err.empty());
    CHECK(steps.size() == 8);
    double prevTop = 0;
    for (const StructureBox& s : steps) {
        CHECK(validBox(s.box));
        CHECK(insideBox(box, s.box));
        CHECK(s.part == StructurePart::Stair);
        CHECK(s.box.mins.y == 0);
        CHECK(s.box.maxs.y == 64);
        const double top = s.box.maxs.z;
        CHECK(top > prevTop);
        CHECK(std::fabs((top - prevTop) - 16) < 1e-9);
        prevTop = top;
    }
    CHECK(std::fabs(prevTop - 128) < 1e-9);
    CHECK(steps.front().box.mins.x == 0);
    CHECK(steps.back().box.maxs.x == 256);
}

TEST(stairs_reverse_climbs_minus_x) {
    const AABB box = mkBox(Vec3(0, 0, 0), Vec3(256, 64, 128));
    StairOptions opt;
    opt.reverse = true;
    const auto steps = generateStairs(box, opt);
    CHECK(steps.size() == 8);
    // The first (lowest) step sits at the high X end and the top step at the low X end.
    CHECK(steps.front().box.maxs.x == 256);
    CHECK(steps.front().box.mins.x == 224);
    CHECK(steps.back().box.mins.x == 0);
    CHECK(std::fabs(steps.front().box.maxs.z - 16) < 1e-9);
    CHECK(std::fabs(steps.back().box.maxs.z - 128) < 1e-9);
    for (const StructureBox& s : steps) CHECK(insideBox(box, s.box));
}

TEST(stairs_axis_1_climbs_plus_y) {
    const AABB box = mkBox(Vec3(0, 0, 0), Vec3(64, 256, 128));
    StairOptions opt;
    opt.axis = 1;
    const auto steps = generateStairs(box, opt);
    CHECK(steps.size() == 8);
    double prevTop = 0;
    for (const StructureBox& s : steps) {
        CHECK(validBox(s.box));
        CHECK(insideBox(box, s.box));
        CHECK(s.box.mins.x == 0);
        CHECK(s.box.maxs.x == 64);
        CHECK(s.box.maxs.z > prevTop);
        prevTop = s.box.maxs.z;
    }
    CHECK(steps.front().box.mins.y == 0);
    CHECK(steps.back().box.maxs.y == 256);
    CHECK(std::fabs(prevTop - 128) < 1e-9);
}

TEST(stairs_too_short_is_empty_with_error) {
    const AABB box = mkBox(Vec3(0, 0, 0), Vec3(40, 64, 128));  // 8 steps of depth 5, below minRun 12
    std::string err;
    const auto steps = generateStairs(box, StairOptions{}, &err);
    CHECK(steps.empty());
    CHECK(!err.empty());
}

TEST(stairs_thin_treads_have_thickness) {
    const AABB box = mkBox(Vec3(0, 0, 0), Vec3(256, 64, 128));
    StairOptions opt;
    opt.solidUnderneath = false;
    opt.treadThickness = 8;
    const auto treads = generateStairs(box, opt);
    CHECK(treads.size() == 8);
    for (const StructureBox& s : treads) {
        CHECK(validBox(s.box));
        CHECK(insideBox(box, s.box));
        CHECK(std::fabs((s.box.maxs.z - s.box.mins.z) - 8) < 1e-9);
    }
    // The lowest tread sits 8 units above the box bottom, so its bottom stays inside the box.
    CHECK(std::fabs(treads.front().box.mins.z - 8) < 1e-9);
}

TEST(building_512x384_two_floors) {
    const AABB box = mkBox(Vec3(0, 0, 0), Vec3(512, 384, 264));
    BuildingOptions opt;
    opt.floors = 2;
    std::string err = "unset";
    const auto b = generateBuilding(box, opt, &err);
    CHECK(err.empty());
    CHECK(!b.empty());

    bool hasFloor = false, hasWall = false, hasRoof = false, hasStair = false;
    for (const StructureBox& s : b) {
        CHECK(validBox(s.box));
        CHECK(insideBox(box, s.box));
        hasFloor |= s.part == StructurePart::Floor;
        hasWall |= s.part == StructurePart::Wall;
        hasRoof |= s.part == StructurePart::Roof;
        hasStair |= s.part == StructurePart::Stair;
    }
    CHECK(hasFloor);
    CHECK(hasWall);
    CHECK(hasRoof);
    CHECK(hasStair);

    for (size_t i = 0; i < b.size(); ++i) {
        for (size_t j = i + 1; j < b.size(); ++j) {
            if (overlapPositive(b[i].box, b[j].box)) CHECK(!"boxes overlap with positive volume");
        }
    }

    // Door: the doorway on the -Y wall (centre X 256, 112 tall above the ground floor at z 8) is clear of walls.
    CHECK(!anyPartContains(b, StructurePart::Wall, Vec3(256, 4, 64)));
    CHECK(anyPartContains(b, StructurePart::Wall, Vec3(100, 4, 64)));  // solid wall beside the door

    // Window: the -X wall window (centred on Y 192, sill 48 above the ground floor) is an opening.
    CHECK(!anyPartContains(b, StructurePart::Wall, Vec3(4, 192, 80)));
    CHECK(anyPartContains(b, StructurePart::Wall, Vec3(4, 300, 80)));

    // The first upper slab (z 128 to 136) has an opening above the staircase, at X 408 to 504 and Y 8 to 376.
    CHECK(!anyPartContains(b, StructurePart::Floor, Vec3(456, 192, 132)));
    CHECK(anyPartContains(b, StructurePart::Floor, Vec3(100, 192, 132)));
}

TEST(building_too_small_footprint_is_empty_with_error) {
    const AABB box = mkBox(Vec3(0, 0, 0), Vec3(100, 300, 264));
    std::string err;
    const auto b = generateBuilding(box, BuildingOptions{}, &err);
    CHECK(b.empty());
    CHECK(!err.empty());
}

TEST(building_and_stairs_are_deterministic) {
    const AABB box = mkBox(Vec3(-256, 128, 0), Vec3(256, 512, 264));
    BuildingOptions opt;
    opt.floors = 2;
    const auto a = generateBuilding(box, opt);
    const auto b = generateBuilding(box, opt);
    CHECK(!a.empty());
    CHECK(a.size() == b.size());
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        CHECK(a[i].part == b[i].part);
        CHECK(a[i].box.mins == b[i].box.mins);
        CHECK(a[i].box.maxs == b[i].box.maxs);
    }
    const auto s1 = generateStairs(mkBox(Vec3(0, 0, 0), Vec3(256, 64, 128)), StairOptions{});
    const auto s2 = generateStairs(mkBox(Vec3(0, 0, 0), Vec3(256, 64, 128)), StairOptions{});
    CHECK(s1.size() == s2.size());
    for (size_t i = 0; i < s1.size() && i < s2.size(); ++i) CHECK(s1[i].box.maxs == s2[i].box.maxs);
}

TEST(structure_part_names) {
    CHECK(std::string(structurePartName(StructurePart::Floor)) == "floor");
    CHECK(std::string(structurePartName(StructurePart::Wall)) == "wall");
    CHECK(std::string(structurePartName(StructurePart::Stair)) == "stair");
    CHECK(std::string(structurePartName(StructurePart::Roof)) == "roof");
}
