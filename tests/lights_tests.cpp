// Light entities as IW8 primary lights (doc 24, src/core/compile_iw8.cpp localPrimaryLights): types, colours,
// radius, cone cosines and the primary-light count written to world.json and comworld.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "brush_ops.h"
#include "compile_iw8.h"
#include "json_lite.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

constexpr const char* kId = "mp_iw8r_lights";

std::string scratch(const std::string& name) {
    const fs::path p = fs::path(test::tmpDir()) / "lights" / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    return p.string();
}

std::string readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// One floor brush (so the map has geometry), then the given entity text.
std::string mapText(const std::string& entities) {
    const AABB floor{{-256, -256, -16}, {256, 256, 0}};
    std::string t = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n";
    const auto prim = makeBrushPrimitive(makeBox(floor, " room/grey 128 128 0 0 0 0"), "\n");
    if (prim) t += prim->text();
    t += "}\n";
    t += "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"-128 0 16\"\n\"angles\" \"0 0 0\"\n}\n";
    t += "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"128 0 16\"\n\"angles\" \"0 180 0\"\n}\n";
    return t + entities;
}

struct Compiled {
    bool ok = false;
    CompileReport rep;
    json::Value world, comworld;
    std::string ents;
    std::vector<std::string> warnings;
};

// Compiles map text into a fresh scratch folder and reads back the JSON documents and the .ents text.
Compiled compileText(const std::string& text, const std::string& name) {
    Compiled c;
    MapFile map;
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    if (!pr.map) return c;
    map = std::move(*pr.map);
    const std::string out = scratch(name);
    CompileOptions opt;
    opt.mapId = kId;
    std::string err;
    c.ok = compileIw8Extraction(map, out, opt, c.rep, &err);
    if (!c.ok) {
        std::printf("  compile error: %s\n", err.c_str());
        return c;
    }
    const fs::path m = fs::path(out) / "extracted" / "maps" / "mp";
    std::string jerr;
    auto w = json::parse(readAll((m / (std::string(kId) + ".d3dbsp.replay-world.json")).string()), &jerr);
    auto cw = json::parse(readAll((m / (std::string(kId) + ".d3dbsp.replay-comworld.json")).string()), &jerr);
    if (w) c.world = *w;
    if (cw) c.comworld = *cw;
    c.ents = readAll((m / (std::string(kId) + ".d3dbsp.ents")).string());
    c.warnings = c.rep.warnings;
    return c;
}

bool near(double a, double b, double eps = 1e-4) { return std::fabs(a - b) <= eps; }

// Component i of a JSON vector, or NaN when absent.
double at3(const json::Value* v, size_t i) {
    if (!v || v->arr.size() <= i) return std::nan("");
    return v->arr[i].number().value_or(std::nan(""));
}

}  // namespace

// One omni and one spot: four primary lights (empty, sun, omni, spot) with the converter's types and values.
TEST(lights_omni_and_spot_become_primary_lights) {
    const std::string ents =
        // omni: colour 0..1, radius 512, no target and no fov
        "{\n\"classname\" \"light\"\n\"origin\" \"0 0 128\"\n\"_color\" \"1 0.5 0.25\"\n\"radius\" \"512\"\n}\n"
        // spot: colour 0..255 with intensity 0.5, aimed by angles (pitch 60 down, yaw 90) with a 60/30 degree cone
        "{\n\"classname\" \"light\"\n\"origin\" \"0 -200 128\"\n\"angles\" \"60 90 0\"\n\"_color\" \"255 255 255\"\n"
        "\"intensity\" \"0.5\"\n\"fov_outer\" \"60\"\n\"fov_inner\" \"30\"\n}\n";
    const Compiled c = compileText(mapText(ents), "two");
    CHECK(c.ok);
    if (!c.ok) return;

    CHECK(c.world.get("primary_light_count") && c.world.get("primary_light_count")->number() == 4.0);
    CHECK(c.world.get("sun_primary_light_index") && c.world.get("sun_primary_light_index")->number() == 1.0);

    const json::Value* lights = c.comworld.get("primary_lights");
    CHECK(lights && lights->arr.size() == 4);
    if (!lights || lights->arr.size() != 4) return;
    CHECK(lights->arr[0].get("type")->number() == 0.0);
    CHECK(lights->arr[1].get("type")->number() == 1.0);
    CHECK(lights->arr[2].get("type")->number() == 3.0);  // omni
    CHECK(lights->arr[3].get("type")->number() == 2.0);  // spot

    // Omni: colour as written, radius 512, no cone.
    const json::Value& omni = lights->arr[2];
    CHECK(near(at3(omni.get("color"), 0), 1.0) && near(at3(omni.get("color"), 1), 0.5) && near(at3(omni.get("color"), 2), 0.25));
    CHECK(near(at3(omni.get("origin"), 2), 128.0));
    CHECK(omni.get("radius") && near(omni.get("radius")->number().value_or(0), 512.0));
    CHECK(omni.get("definition") && omni.get("definition")->str == "light_point_linear");

    // Spot: 255 white at intensity 0.5 is 0.5 grey; radius defaults to 400; cones cos(30) and cos(15).
    const json::Value& spot = lights->arr[3];
    CHECK(near(at3(spot.get("color"), 0), 0.5) && near(at3(spot.get("color"), 1), 0.5) && near(at3(spot.get("color"), 2), 0.5));
    CHECK(spot.get("radius") && near(spot.get("radius")->number().value_or(0), 400.0));
    const double cosOuter = spot.get("cos_half_fov_outer") ? spot.get("cos_half_fov_outer")->number().value_or(0) : 0;
    const double cosInner = spot.get("cos_half_fov_inner") ? spot.get("cos_half_fov_inner")->number().value_or(0) : 0;
    CHECK(near(cosOuter, std::cos(30.0 * 3.14159265358979323846 / 180.0), 1e-5));
    CHECK(near(cosInner, std::cos(15.0 * 3.14159265358979323846 / 180.0), 1e-5));
    // Stored direction points back toward the light: emission (0, 0.5, -0.866) negated.
    CHECK(near(at3(spot.get("direction"), 0), 0.0));
    CHECK(near(at3(spot.get("direction"), 1), -0.5, 1e-4));
    CHECK(near(at3(spot.get("direction"), 2), 0.8660254, 1e-4));

    // Light entities stay out of .ents (the converter does not accept the class, doc 24).
    CHECK(c.ents.find("\"light\"") == std::string::npos);
    CHECK(c.ents.find("mp_tdm_spawn") != std::string::npos);

    // The native falloff definition the converter requires for light_point_linear: 'b', "falloff_linear", NUL.
    const fs::path def = fs::path(test::tmpDir()) / "lights" / "two" / "extracted" / "lights" / "light_point_linear";
    CHECK(fs::is_regular_file(def));
    if (fs::is_regular_file(def)) CHECK(fs::file_size(def) == 16);
}

// A spot aimed at a target entity: its direction is the normalized vector from the light to the target.
TEST(lights_spot_aims_at_target_entity) {
    const std::string ents =
        "{\n\"classname\" \"light\"\n\"origin\" \"0 -200 128\"\n\"target\" \"aim_here\"\n}\n"
        "{\n\"classname\" \"script_origin\"\n\"targetname\" \"aim_here\"\n\"origin\" \"0 200 0\"\n}\n";
    const Compiled c = compileText(mapText(ents), "target");
    CHECK(c.ok);
    if (!c.ok) return;
    const json::Value* lights = c.comworld.get("primary_lights");
    CHECK(lights && lights->arr.size() == 3);
    if (!lights || lights->arr.size() != 3) return;
    const json::Value& spot = lights->arr[2];
    CHECK(spot.get("type") && spot.get("type")->number() == 2.0);
    // From (0,-200,128) to (0,200,0): (0,400,-128), negated for the stored direction.
    const double len = std::sqrt(400.0 * 400.0 + 128.0 * 128.0);
    CHECK(near(at3(spot.get("direction"), 1), -400.0 / len, 1e-5));
    CHECK(near(at3(spot.get("direction"), 2), 128.0 / len, 1e-5));
    // No fov keys: the Radiant template cones, 90 outer and 60 inner.
    CHECK(near(at3(spot.get("color"), 0), 1.0));
    const double cosOuter = spot.get("cos_half_fov_outer") ? spot.get("cos_half_fov_outer")->number().value_or(0) : 0;
    CHECK(near(cosOuter, std::cos(45.0 * 3.14159265358979323846 / 180.0), 1e-5));
}

// No light entities: the table stays at today's two lights and world.json says 2.
TEST(lights_none_keeps_two_primary_lights) {
    const Compiled c = compileText(mapText(""), "none");
    CHECK(c.ok);
    if (!c.ok) return;
    CHECK(c.world.get("primary_light_count") && c.world.get("primary_light_count")->number() == 2.0);
    const json::Value* lights = c.comworld.get("primary_lights");
    CHECK(lights && lights->arr.size() == 2);
    if (lights && lights->arr.size() == 2) CHECK(lights->arr[1].get("type")->number() == 1.0);
    CHECK(!fs::exists(fs::path(test::tmpDir()) / "lights" / "none" / "extracted" / "lights" / "light_point_linear"));
}

// 300 lights: 254 fit the 256 slots (empty and sun included), the rest are dropped with a warning.
TEST(lights_beyond_slot_limit_are_dropped) {
    std::string ents;
    for (int i = 0; i < 300; ++i)
        ents += "{\n\"classname\" \"light_omni\"\n\"origin\" \"" + std::to_string(i % 200 - 100) + " 0 128\"\n\"radius\" \"64\"\n}\n";
    const Compiled c = compileText(mapText(ents), "limit");
    CHECK(c.ok);
    if (!c.ok) return;
    CHECK(c.world.get("primary_light_count") && c.world.get("primary_light_count")->number() == 256.0);
    const json::Value* lights = c.comworld.get("primary_lights");
    CHECK(lights && lights->arr.size() == 256);
    bool warned = false;
    for (const auto& w : c.warnings)
        if (w.find("left out") != std::string::npos && w.find("46 light") != std::string::npos) warned = true;
    CHECK(warned);
}
