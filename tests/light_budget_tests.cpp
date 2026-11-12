// Light budget (5.15): compiler-matching light parsing, overlap heat map, spot cones and Check Map rules.
#include <cmath>
#include <string>
#include <vector>

#include "light_budget.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

bool close(double a, double b, double eps = 1e-5) { return std::fabs(a - b) < eps; }

MapFile parse(const std::string& text) {
    Id next = 1;
    ParseResult r = parseMap(text, next);
    CHECK(r.map.has_value());
    return r.map ? *r.map : MapFile{};
}

// Axis-aligned brush, as in auto_caulk_tests.cpp.
std::string box(int x0, int y0, int z0, int x1, int y1, int z1, const std::string& mat) {
    auto f = [&](const std::string& a, const std::string& b, const std::string& c) {
        return " ( " + a + " ) ( " + b + " ) ( " + c + " ) " + mat + " 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n";
    };
    const std::string X0 = std::to_string(x0), Y0 = std::to_string(y0), Z0 = std::to_string(z0), X1 = std::to_string(x1),
                      Y1 = std::to_string(y1), Z1 = std::to_string(z1);
    std::string t = "{\n";
    t += f(X1 + " " + Y1 + " " + Z0, X0 + " " + Y1 + " " + Z0, X0 + " " + Y0 + " " + Z0);
    t += f(X0 + " " + Y0 + " " + Z1, X0 + " " + Y1 + " " + Z1, X1 + " " + Y1 + " " + Z1);
    t += f(X0 + " " + Y0 + " " + Z0, X0 + " " + Y0 + " " + Z1, X1 + " " + Y0 + " " + Z1);
    t += f(X1 + " " + Y0 + " " + Z0, X1 + " " + Y0 + " " + Z1, X1 + " " + Y1 + " " + Z1);
    t += f(X1 + " " + Y1 + " " + Z0, X1 + " " + Y1 + " " + Z1, X0 + " " + Y1 + " " + Z1);
    t += f(X0 + " " + Y1 + " " + Z0, X0 + " " + Y1 + " " + Z1, X0 + " " + Y0 + " " + Z1);
    return t + "}\n";
}

LightInfo omni(Id id, Vec3 o, double radius = 400, bool shadows = false) {
    LightInfo L;
    L.entity = id;
    L.classname = "light";
    L.origin = o;
    L.radius = radius;
    L.shadows = shadows;
    return L;
}

LightInfo spot(Id id, Vec3 o, Vec3 axis, double fovOuterDeg, double radius = 500) {
    LightInfo L = omni(id, o, radius);
    L.classname = "light_spot";
    L.spot = true;
    L.dir = normalize(axis);
    L.cosOuter = std::cos(fovOuterDeg * 0.5 * 3.14159265358979323846 / 180.0);
    return L;
}

size_t countIssues(const std::vector<LightBudgetIssue>& issues, bool error) {
    size_t n = 0;
    for (const auto& i : issues) n += i.error == error;
    return n;
}

}  // namespace

TEST(light_budget_parse_defaults_and_spots) {
    const MapFile map = parse(
        "iwmap 4\n"
        "{\n\"classname\" \"worldspawn\"\n}\n"
        "{\n\"classname\" \"light\"\n\"origin\" \"10 20 30\"\n}\n"                       // 0: omni, radius default
        "{\n\"classname\" \"light_omni\"\n\"origin\" \"0 0 0\"\n\"radius\" \"0\"\n}\n"    // 1: radius 0 -> 400
        "{\n\"classname\" \"light\"\n}\n"                                                // 2: no origin -> 0 0 0
        "{\n\"classname\" \"light_spot\"\n\"origin\" \"100 0 0\"\n\"target\" \"aim\"\n}\n"  // 3: aimed at target
        "{\n\"classname\" \"info_null\"\n\"targetname\" \"aim\"\n\"origin\" \"0 0 0\"\n}\n"
        "{\n\"classname\" \"light_spot\"\n\"origin\" \"0 0 0\"\n\"angles\" \"0 90 0\"\n\"fov_outer\" \"0.2\"\n"
        "\"shadows\" \"1\"\n}\n"                                                         // 4: yaw 90, fov clamped to 1
        "{\n\"classname\" \"light\"\n\"origin\" \"0 0 0\"\n\"fov_outer\" \"60\"\n}\n"       // 5: fov key makes a spot
        "{\n\"classname\" \"light\"\n" + box(0, 0, 0, 8, 8, 8, "caulk") + "}\n");  // brush entity: skipped
    const auto L = collectLights(map);
    CHECK(L.size() == 6);
    if (L.size() != 6) return;
    CHECK(L[0].classname == "light" && !L[0].spot && !L[0].shadows);
    CHECK(close(L[0].origin.x, 10) && close(L[0].origin.y, 20) && close(L[0].origin.z, 30));
    CHECK(close(L[0].radius, 400));
    CHECK(L[0].dir == Vec3{});
    CHECK(L[1].classname == "light_omni" && close(L[1].radius, 400));
    CHECK(L[2].origin == Vec3{} && close(L[2].radius, 400) && !L[2].spot);
    CHECK(L[3].spot && close(L[3].dir.x, -1) && close(L[3].dir.y, 0) && close(L[3].dir.z, 0));
    CHECK(close(L[3].cosOuter, std::cos(45.0 * 3.14159265358979323846 / 180.0)));  // default fov_outer 90
    CHECK(L[4].spot && close(L[4].dir.y, 1) && close(L[4].dir.x, 0, 1e-6));
    CHECK(close(L[4].cosOuter, std::cos(0.5 * 3.14159265358979323846 / 180.0)));  // 0.2 clamped to 1 degree
    CHECK(L[4].shadows);
    CHECK(L[5].spot && close(L[5].dir.x, 1) && close(L[5].cosOuter, std::cos(30.0 * 3.14159265358979323846 / 180.0)));
}

TEST(light_budget_overlap_two_and_three_lights) {
    // Four cells across x = 0..200 at y = 0: centres 25, 75, 125, 175.
    const AABB bounds{{0, -10, 0}, {200, 10, 0}};
    std::vector<LightInfo> two = {omni(1, {0, 0, 0}, 100), omni(2, {150, 0, 0}, 100)};
    CHECK((lightOverlap(two, bounds, 4, 1, 0) == std::vector<int>{1, 2, 1, 1}));

    std::vector<LightInfo> three = two;
    three.push_back(omni(3, {75, 0, 0}, 10));  // reaches only the centre at 75
    CHECK((lightOverlap(three, bounds, 4, 1, 0) == std::vector<int>{1, 3, 1, 1}));

    // Same lights, sampled 1000 units above: nothing reaches.
    CHECK((lightOverlap(three, bounds, 4, 1, 1000) == std::vector<int>{0, 0, 0, 0}));
    // Invalid grids and bounds give an empty result.
    CHECK(lightOverlap(three, bounds, 0, 1, 0).empty());
    CHECK(lightOverlap(three, AABB{}, 2, 2, 0).empty());
}

TEST(light_budget_overlap_row_major) {
    // 2x2 grid over 0..100: centres (25,25), (75,25), (25,75), (75,75). Index = row * 2 + column, row = y.
    const AABB bounds{{0, 0, 0}, {100, 100, 0}};
    const std::vector<LightInfo> lights = {omni(1, {25, 75, 0}, 1)};
    CHECK((lightOverlap(lights, bounds, 2, 2, 0) == std::vector<int>{0, 0, 1, 0}));
}

TEST(light_budget_spot_cone_excludes_behind) {
    // Spot at the origin aimed along +X, half-angle 45 degrees, radius 500.
    const LightInfo s = spot(1, {0, 0, 0}, {1, 0, 0}, 90);
    CHECK(lightReaches(s, {100, 0, 0}));     // in front
    CHECK(!lightReaches(s, {-100, 0, 0}));   // behind
    CHECK(!lightReaches(s, {0, 100, 0}));    // 90 degrees off axis
    CHECK(!lightReaches(s, {600, 0, 0}));    // beyond the radius
    CHECK(lightReaches(s, {0, 0, 0}));       // at its own origin

    // Heat map: 4 cells along x, all on the axis at y = 0. The cell behind (x = -50) is excluded.
    const LightInfo narrow = spot(2, {0, 0, 0}, {1, 0, 0}, 60);  // half-angle 30 degrees
    const AABB bounds{{-200, -50, 0}, {200, 50, 0}};
    CHECK((lightOverlap({narrow}, bounds, 4, 1, 0) == std::vector<int>{0, 0, 1, 1}));
}

TEST(light_budget_primary_light_limit) {
    auto row = [](size_t n) {
        std::vector<LightInfo> v;
        for (size_t i = 0; i < n; ++i) v.push_back(omni(static_cast<Id>(i + 1), {double(i) * 1000.0, 0, 0}));
        return v;
    };
    CHECK(checkLightBudget(row(lightbudget::kMaxPrimaryLocalLights)).empty());
    const auto over = checkLightBudget(row(lightbudget::kMaxPrimaryLocalLights + 1));
    CHECK(over.size() == 1);
    CHECK(over.size() == 1 && over[0].error && over[0].entity == kNoId);
}

TEST(light_budget_radius_rules) {
    std::vector<LightInfo> lights = {omni(5, {0, 0, 0}, 0), omni(7, {5000, 0, 0}, 5000)};
    const auto issues = checkLightBudget(lights);
    CHECK(countIssues(issues, true) == 1);
    CHECK(countIssues(issues, false) == 1);
    for (const auto& i : issues) {
        if (i.error) CHECK(i.entity == 5);
        else CHECK(i.entity == 7);
    }
    // A light with radius 0 lights nothing, so it does not count towards another light's overlap.
    std::vector<LightInfo> nine(9, omni(0, {0, 0, 0}));
    for (size_t i = 0; i < nine.size(); ++i) nine[i].entity = static_cast<Id>(i + 1);
    nine[8].radius = 0;
    const auto r = checkLightBudget(nine);
    CHECK(countIssues(r, true) == 1 && countIssues(r, false) == 0);
}

TEST(light_budget_zero_cone_from_map) {
    // fov_outer 0.2 is clamped to 1 degree by the compiler: a cone that lights almost nothing.
    const MapFile map = parse(
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n"
        "{\n\"classname\" \"light_spot\"\n\"origin\" \"0 0 0\"\n\"fov_outer\" \"0.2\"\n}\n"
        "{\n\"classname\" \"light_spot\"\n\"origin\" \"500 0 0\"\n\"fov_outer\" \"90\"\n}\n");
    const auto L = collectLights(map);
    CHECK(L.size() == 2);
    if (L.size() != 2) return;
    const auto issues = checkLightBudget(L);
    CHECK(countIssues(issues, true) == 1);
    for (const auto& i : issues)
        if (i.error) CHECK(i.entity == L[0].entity);
}

TEST(light_budget_overlap_guideline) {
    std::vector<LightInfo> eight, nine;
    for (Id i = 1; i <= 9; ++i) {
        nine.push_back(omni(i, {0, 0, 0}));
        if (i <= 8) eight.push_back(omni(i, {0, 0, 0}));
    }
    CHECK(checkLightBudget(eight).empty());  // 8 overlapping is at the guideline, not above it
    const auto issues = checkLightBudget(nine);
    CHECK(countIssues(issues, true) == 0);
    CHECK(issues.size() == 9);  // one warning per light at the shared origin
}

TEST(light_budget_shadow_overlap_guideline) {
    std::vector<LightInfo> two, three;
    for (Id i = 1; i <= 3; ++i) {
        three.push_back(omni(i, {0, 0, 0}, 400, true));
        if (i <= 2) two.push_back(omni(i, {0, 0, 0}, 400, true));
    }
    CHECK(checkLightBudget(two).empty());
    const auto issues = checkLightBudget(three);
    CHECK(issues.size() == 3);
    CHECK(countIssues(issues, true) == 0);
}
