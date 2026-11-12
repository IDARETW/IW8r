// Scatter rules tests (src/core/scatter_rules.cpp): slope and material filters, spacing, density, determinism,
// one-step placement, surface collection and JSON presets.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "document.h"
#include "geometry.h"
#include "map_text.h"
#include "scatter_rules.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

constexpr double kEps = 1e-6;
const double kTan60 = std::tan(60.0 * 3.14159265358979323846 / 180.0);

// Two triangles for the quad p0 p1 p2 p3 (in order), all sharing normal n.
void addQuad(std::vector<SurfaceTri>& out, const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, const Vec3& n,
             const std::string& mat) {
    SurfaceTri t1{p0, p1, p2, n, mat, kNoId, kNoId};
    SurfaceTri t2{p0, p2, p3, n, mat, kNoId, kNoId};
    out.push_back(t1);
    out.push_back(t2);
}

// Flat floor z = 0 over [x0, x1] x [0, y1].
void addFloor(std::vector<SurfaceTri>& out, double x0, double x1, double y1, const std::string& mat = "floor") {
    addQuad(out, {x0, 0, 0}, {x1, 0, 0}, {x1, y1, 0}, {x0, y1, 0}, {0, 0, 1}, mat);
}

// Ramp rising from x0 at 60 degrees (z = tan60 * (x - x0)) over [x0, x1] x [0, y1].
void addRamp60(std::vector<SurfaceTri>& out, double x0, double x1, double y1) {
    const double h = kTan60 * (x1 - x0);
    addQuad(out, {x0, 0, 0}, {x1, 0, h}, {x1, y1, h}, {x0, y1, 0}, normalize({-kTan60, 0, 1}), "ramp");
}

ScatterRule baseRule(const std::string& model = "props/rock") {
    ScatterRule r;
    r.models = {model};
    r.density = 4.0;
    r.minSpacing = 0.0;
    r.scaleMin = 1.0;
    r.scaleMax = 1.0;
    return r;
}

bool sameRule(const ScatterRule& a, const ScatterRule& b) {
    return a.models == b.models && a.weights == b.weights && a.density == b.density && a.minSpacing == b.minSpacing &&
           a.minSlopeDeg == b.minSlopeDeg && a.maxSlopeDeg == b.maxSlopeDeg && a.materialsAllow == b.materialsAllow &&
           a.materialsDeny == b.materialsDeny && a.scaleMin == b.scaleMin && a.scaleMax == b.scaleMax &&
           a.alignToNormal == b.alignToNormal && a.sinkDepth == b.sinkDepth && a.seed == b.seed;
}

size_t countClass(const MapFile& m, const std::string& cls) {
    size_t n = 0;
    for (const Entity& e : m.entities)
        if (e.classname() == cls) ++n;
    return n;
}

}  // namespace

TEST(scatter_slope_filter_keeps_ramp_empty) {
    std::vector<SurfaceTri> tris;
    addFloor(tris, 0, 2048, 2048);
    addRamp60(tris, 2048, 2560, 2048);

    ScatterRule r = baseRule();
    r.maxSlopeDeg = 30;
    r.density = 8;
    const auto pts = scatter(tris, r, 100000);
    CHECK(!pts.empty());
    for (const auto& p : pts) {
        CHECK(p.origin.x <= 2048.0 + kEps);
        CHECK(std::fabs(p.origin.z) < kEps);
    }

    // Sanity: with the limit lifted the same ramp does receive points.
    r.maxSlopeDeg = 90;
    const auto all = scatter(tris, r, 100000);
    bool onRamp = false;
    for (const auto& p : all)
        if (p.origin.x > 2048.0 + kEps) onRamp = true;
    CHECK(onRamp);
}

TEST(scatter_slope_min_filter) {
    std::vector<SurfaceTri> tris;
    addFloor(tris, 0, 2048, 2048);
    addRamp60(tris, 2048, 2560, 2048);
    ScatterRule r = baseRule();
    r.minSlopeDeg = 45;
    r.maxSlopeDeg = 90;
    const auto pts = scatter(tris, r, 100000);
    CHECK(!pts.empty());
    for (const auto& p : pts) CHECK(p.origin.x > 2048.0 + kEps);  // flat floor (0 deg) excluded
}

TEST(scatter_material_allow_and_deny) {
    std::vector<SurfaceTri> tris;
    addFloor(tris, 0, 1024, 2048, "Grass_Lawn");
    addFloor(tris, 1024, 2048, 2048, "rock_cliff");

    ScatterRule allow = baseRule();
    allow.materialsAllow = {"grass"};  // case-insensitive substring
    const auto a = scatter(tris, allow, 100000);
    CHECK(!a.empty());
    for (const auto& p : a) CHECK(p.origin.x <= 1024.0 + kEps);

    ScatterRule deny = baseRule();
    deny.materialsDeny = {"ROCK"};
    const auto d = scatter(tris, deny, 100000);
    CHECK(!d.empty());
    for (const auto& p : d) CHECK(p.origin.x <= 1024.0 + kEps);

    // Allow and deny together: deny wins. "Grass_Lawn" matches both lists and is rejected; rock stays.
    ScatterRule both = baseRule();
    both.materialsAllow = {"grass", "rock"};
    both.materialsDeny = {"lawn"};
    const auto b = scatter(tris, both, 100000);
    CHECK(!b.empty());
    for (const auto& p : b) CHECK(p.origin.x >= 1024.0 - kEps);
    // Allow list that matches nothing yields nothing.
    ScatterRule none = baseRule();
    none.materialsAllow = {"water"};
    CHECK(scatter(tris, none, 100000).empty());
}

TEST(scatter_min_spacing_respected) {
    std::vector<SurfaceTri> tris;
    addFloor(tris, 0, 2048, 2048);
    ScatterRule r = baseRule();
    r.density = 4;
    r.minSpacing = 64;
    const auto pts = scatter(tris, r, 5000);
    CHECK(pts.size() > 50);
    for (size_t i = 0; i < pts.size(); ++i)
        for (size_t j = i + 1; j < pts.size(); ++j) {
            const double d = length(pts[i].origin - pts[j].origin);
            CHECK(d >= r.minSpacing - kEps);
        }
}

TEST(scatter_density_roughly_proportional_to_area) {
    // density 4 per 256 x 256: 2048^2 -> 256 expected, 1024^2 -> 64 expected. Mean over 4 seeds, within 30%.
    auto meanCount = [](double side) {
        std::vector<SurfaceTri> tris;
        addFloor(tris, 0, side, side);
        size_t sum = 0;
        for (unsigned seed = 1; seed <= 4; ++seed) {
            ScatterRule r = baseRule();
            r.density = 4;
            r.seed = seed;
            sum += scatter(tris, r, 100000).size();
        }
        return double(sum) / 4.0;
    };
    const double big = meanCount(2048), small = meanCount(1024);
    CHECK(std::fabs(big - 256.0) <= 0.3 * 256.0);
    CHECK(std::fabs(small - 64.0) <= 0.3 * 64.0);
    CHECK(big > small * 2.5 && big < small * 5.5);
}

TEST(scatter_deterministic_for_seed) {
    std::vector<SurfaceTri> tris;
    addFloor(tris, 0, 2048, 2048);
    addRamp60(tris, 2048, 2560, 2048);
    ScatterRule r = baseRule();
    r.models = {"a", "b", "c"};
    r.weights = {1, 2, 3};
    r.minSpacing = 32;
    r.scaleMin = 0.5;
    r.scaleMax = 1.5;
    r.alignToNormal = true;
    r.sinkDepth = 2;
    r.seed = 7;
    const auto p1 = scatter(tris, r);
    const auto p2 = scatter(tris, r);
    CHECK(!p1.empty());
    CHECK(p1.size() == p2.size());
    bool same = p1.size() == p2.size();
    for (size_t i = 0; same && i < p1.size(); ++i)
        same = p1[i].model == p2[i].model && p1[i].origin == p2[i].origin && p1[i].angles == p2[i].angles &&
               p1[i].scale == p2[i].scale;
    CHECK(same);

    r.seed = 8;
    const auto p3 = scatter(tris, r);
    CHECK(!p3.empty() && (p3.size() != p1.size() || p3[0].origin != p1[0].origin));
}

TEST(scatter_model_weights_and_limits) {
    std::vector<SurfaceTri> tris;
    addFloor(tris, 0, 2048, 2048);
    ScatterRule r = baseRule();
    r.models = {"only_zero", "only_one"};
    r.weights = {0, 1};  // zero weight is never chosen
    r.density = 8;
    const auto pts = scatter(tris, r, 100000);
    CHECK(!pts.empty());
    for (const auto& p : pts) CHECK(p.model == "only_one");

    // maxCount caps the output; scales stay inside the rule range.
    ScatterRule s = baseRule();
    s.density = 100;
    s.scaleMin = 0.8;
    s.scaleMax = 1.2;
    const auto capped = scatter(tris, s, 17);
    CHECK(capped.size() == 17);
    for (const auto& p : capped) CHECK(p.scale >= 0.8 - kEps && p.scale <= 1.2 + kEps);

    // A zero density or no models produce nothing.
    ScatterRule none = baseRule();
    none.density = 0;
    CHECK(scatter(tris, none).empty());
    ScatterRule nomodel = baseRule();
    nomodel.models.clear();
    CHECK(scatter(tris, nomodel).empty());
}

TEST(scatter_align_to_normal_tilts_on_ramp) {
    std::vector<SurfaceTri> tris;
    addRamp60(tris, 0, 512, 512);
    ScatterRule r = baseRule();
    r.maxSlopeDeg = 90;
    r.alignToNormal = true;
    const auto pts = scatter(tris, r, 200);
    CHECK(!pts.empty());
    for (const auto& p : pts) {
        CHECK(std::fabs(std::fabs(p.angles.x) - 60.0) < 1e-6);  // pitch follows the 60 degree slope
        CHECK(p.angles.y >= 0.0 && p.angles.y < 360.0);         // random yaw
    }
}

TEST(place_scatter_one_undo_step) {
    Document d;
    CHECK(d.loadFromText("iwmap 4\n", "scatter_undo.map", nullptr));
    const size_t before = d.undoPosition();

    std::vector<ScatterPlacement> pl;
    for (int i = 0; i < 5; ++i) {
        ScatterPlacement p;
        p.model = "props/rock";
        p.origin = {100.0 * i, 0, 0};
        p.angles = {0, 90, 0};
        p.scale = i == 0 ? 1.0 : 1.5;
        pl.push_back(p);
    }
    CHECK(placeScatter(d, pl) == 5);
    CHECK(d.undoPosition() == before + 1);
    CHECK(countClass(d.map(), "misc_model") == 5);
    CHECK(d.selection().size() == 5);

    const Entity* first = nullptr;
    for (const Entity& e : d.map().entities)
        if (e.classname() == "misc_model") {
            first = &e;
            break;
        }
    CHECK(first && first->value("model") && *first->value("model") == "props/rock");
    CHECK(first && first->value("modelscale") == nullptr);  // scale 1 writes no key

    CHECK(d.undo());
    CHECK(countClass(d.map(), "misc_model") == 0);
    CHECK(d.redo());
    CHECK(countClass(d.map(), "misc_model") == 5);

    CHECK(placeScatter(d, {}) == 0);
    CHECK(d.undoPosition() == before + 1);
}

TEST(collect_surfaces_brushes_groups_and_tools) {
    Document d;
    CHECK(d.loadFromText("iwmap 4\n", "collect.map", nullptr));
    const AABB floorBox = [] {
        AABB b;
        b.add({0, 0, -16});
        b.add({256, 256, 0});
        return b;
    }();
    auto brush = [&](const std::string& suffix) {
        auto p = makeBrushPrimitive(makeBox(floorBox, suffix), d.eol());
        return p ? std::vector<Primitive>{*p} : std::vector<Primitive>{};
    };
    const Id world = d.addEntity({{"classname", "worldspawn"}}, brush(" floor 64 64 0 0 0 0 0"), "world");
    d.addEntity({{"classname", "worldspawn"}}, brush(" caulk 64 64 0 0 0 0 0"), "tool");
    const Id group = d.addEntity({{"classname", "func_group"}}, brush(" floor 64 64 0 0 0 0 0"), "group");
    d.addEntity({{"classname", "trigger_multiple"}}, brush(" floor 64 64 0 0 0 0 0"), "trigger");

    const auto tris = collectSurfaces(d.map());
    // Two floor boxes, six faces each, two triangles per quad face: 24 triangles. Caulk and trigger are skipped.
    CHECK(tris.size() == 24);
    size_t fromWorld = 0, fromGroup = 0;
    bool sawCaulk = false;
    for (const auto& t : tris) {
        if (t.entity == world) ++fromWorld;
        if (t.entity == group) ++fromGroup;
        if (t.material == "caulk") sawCaulk = true;
        CHECK(std::fabs(length(t.n) - 1.0) < 1e-9);
    }
    CHECK(fromWorld == 12 && fromGroup == 12 && !sawCaulk);

    // Upward faces of the box top: normals point up.
    size_t up = 0;
    for (const auto& t : tris)
        if (t.n.z > 0.99) ++up;
    CHECK(up == 4);

    AABB far;
    far.add({10000, 10000, 10000});
    far.add({10001, 10001, 10001});
    CHECK(collectSurfaces(d.map(), &far).empty());
}

TEST(scatter_rules_json_round_trip) {
    ScatterRule a = baseRule("props/rock_a");
    a.models = {"props/rock_a", "props/\"quoted\"\\name"};
    a.weights = {1.0, 0.25};
    a.density = 3.5;
    a.minSpacing = 48;
    a.minSlopeDeg = 2;
    a.maxSlopeDeg = 37.5;
    a.materialsAllow = {"grass", "dirt"};
    a.materialsDeny = {"lawn_edge"};
    a.scaleMin = 0.73;
    a.scaleMax = 1.31;
    a.alignToNormal = true;
    a.sinkDepth = 1.5;
    a.seed = 4000000000u;

    ScatterRule b;  // all defaults
    b.models = {"tree"};

    const std::string json = scatterRulesToJson({a, b});
    std::vector<ScatterRule> back;
    std::string err;
    CHECK(scatterRulesFromJson(json, back, &err));
    CHECK(err.empty());
    CHECK(back.size() == 2);
    if (back.size() == 2) {
        CHECK(sameRule(a, back[0]));
        CHECK(sameRule(b, back[1]));
    }

    // Same rules after a second trip: the text is stable.
    CHECK(scatterRulesToJson(back) == json);

    // Empty list round trips too.
    std::vector<ScatterRule> none;
    CHECK(scatterRulesFromJson(scatterRulesToJson({}), none));
    CHECK(none.empty());
}

TEST(scatter_rules_json_rejects_bad_input) {
    std::vector<ScatterRule> out;
    std::string err;
    CHECK(!scatterRulesFromJson("[]", out, &err));
    CHECK(!err.empty());
    CHECK(!scatterRulesFromJson("{\"format\":\"other\",\"rules\":[]}", out));
    CHECK(!scatterRulesFromJson("{\"format\":\"iw8r-scatter-rules\",\"rules\":[{\"density\":\"four\"}]}", out, &err));
    CHECK(!scatterRulesFromJson("{\"format\":\"iw8r-scatter-rules\",\"rules\":[{\"seed\":-1}]}", out));
    CHECK(!scatterRulesFromJson("{\"format\":\"iw8r-scatter-rules\",\"rules\":[{\"models\":[1]}]}", out));
    CHECK(!scatterRulesFromJson("{ not json", out));
    // Missing members keep their defaults.
    CHECK(scatterRulesFromJson("{\"format\":\"iw8r-scatter-rules\",\"rules\":[{\"models\":[\"x\"]}]}", out));
    CHECK(out.size() == 1 && out[0].density == ScatterRule{}.density && out[0].seed == 1);
}
