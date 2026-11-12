// Light keys (5.14): the optional iw8r_* keys on light / light_spot entities reach their ComPrimaryLight fields in the
// comworld JSON. A valid value is written; an out-of-range one is warned and left out; a light without keys is unchanged.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "compile_iw8.h"
#include "json_lite.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

constexpr const char* kId = "mp_lightkeys_t";

std::string scratch(const std::string& name) {
    const fs::path p = fs::path(test::tmpDir()) / "light_keys" / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    return p.string();
}

std::string readText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

json::Value loadJson(const fs::path& p, bool& ok) {
    std::string err;
    auto v = json::parse(readText(p), &err);
    ok = v.has_value();
    if (!ok) std::printf("  json error in %s: %s\n", p.string().c_str(), err.c_str());
    return ok ? *v : json::Value{};
}

std::string boxBrush(const AABB& b, const char* material) {
    const auto prim = makeBrushPrimitive(makeBox(b, std::string(" ") + material + " 128 128 0 0 0 0"), "\n");
    return prim ? prim->text() : std::string();
}

// A floor box and the two spawns the converter needs, plus the given entities.
std::string worldText(const std::string& entities) {
    std::string t = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n";
    t += boxBrush({{0, 0, -16}, {256, 256, 0}}, "world/a");
    t += boxBrush({{900, 0, -16}, {1156, 256, 0}}, "world/b");
    t += "}\n";
    t += "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"64 128 16\"\n\"angles\" \"0 0 0\"\n}\n";
    t += "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"1000 128 16\"\n\"angles\" \"0 180 0\"\n}\n";
    t += entities;
    return t;
}

// Compiles map text into a fresh folder. Returns false (and prints why) on failure.
bool compileText(const std::string& text, const std::string& out, CompileReport& rep) {
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    if (!pr.map) {
        std::printf("  map parse failed\n");
        return false;
    }
    CompileOptions opt;
    opt.mapId = kId;
    std::string err;
    const bool ok = compileIw8Extraction(*pr.map, out, opt, rep, &err);
    if (!ok) std::printf("  compile error: %s\n", err.c_str());
    return ok;
}

bool hasWarning(const CompileReport& rep, const std::string& needle) {
    for (const auto& w : rep.warnings)
        if (w.find(needle) != std::string::npos) return true;
    return false;
}

int countWarnings(const CompileReport& rep, const std::string& needle) {
    int n = 0;
    for (const auto& w : rep.warnings)
        if (w.find(needle) != std::string::npos) ++n;
    return n;
}

fs::path comworldPath(const std::string& out) {
    return fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kId) + ".d3dbsp.replay-comworld.json");
}

// primary_lights[index] of the compiled comworld, or nullptr. Index 0 is the empty slot, 1 the sun, 2.. the lights.
const json::Value* primaryLight(const json::Value& comworld, size_t index) {
    const json::Value* lights = comworld.get("primary_lights");
    return lights ? lights->at(index) : nullptr;
}

bool numberIs(const json::Value* obj, const char* field, double want) {
    const json::Value* v = obj ? obj->get(field) : nullptr;
    return v && v->number() && std::fabs(*v->number() - want) < 1e-6;
}

bool hasField(const json::Value* obj, const char* field) { return obj && obj->get(field) != nullptr; }

// The fields a light written without keys carries (doc 24 section 3). Any other field means a key leaked into it.
const char* const kBaseFields[] = {"type", "exponent", "can_use_shadow_map", "color", "direction", "origin", "radius",
                                   "cos_half_fov_outer", "cos_half_fov_inner", "rotation_limit", "translation_limit",
                                   "definition"};

}  // namespace

// Every key on one spot: each field is written with its value, and the build reports no warning for them.
TEST(light_keys_spot_writes_every_field) {
    const std::string out = scratch("every_field");
    const std::string spot =
        "{\n\"classname\" \"light_spot\"\n\"origin\" \"200 0 200\"\n\"angles\" \"70 180 0\"\n\"_color\" \"1 1 1\"\n"
        "\"iw8r_bulb_radius\" \"3\"\n\"iw8r_shadow_bias\" \"0.5\"\n\"iw8r_falloff\" \"0.4\"\n\"iw8r_fade\" \"10 200\"\n"
        "\"iw8r_shadows\" \"1\"\n\"iw8r_intensity_units\" \"2.5\"\n\"iw8r_uv_intensity\" \"0.3\"\n"
        "\"iw8r_ir_intensity\" \"0.6\"\n}\n";
    CompileReport rep;
    if (!compileText(worldText(spot), out, rep)) {
        CHECK(false);
        return;
    }
    CHECK(!hasWarning(rep, "iw8r_"));
    bool ok = false;
    const json::Value comworld = loadJson(comworldPath(out), ok);
    CHECK(ok);
    if (!ok) return;
    const json::Value* light = primaryLight(comworld, 2);
    CHECK(light != nullptr);
    if (!light) return;
    CHECK(numberIs(light, "type", 2));
    CHECK(numberIs(light, "bulb_radius", 3));
    CHECK(numberIs(light, "shadow_bias", 0.5));
    CHECK(numberIs(light, "distance_falloff", 0.4));
    CHECK(numberIs(light, "intensity", 2.5));
    CHECK(numberIs(light, "uv_intensity", 0.3));
    CHECK(numberIs(light, "ir_intensity", 0.6));
    const json::Value* shadow = light->get("can_use_shadow_map");
    CHECK(shadow && shadow->type == json::Value::Type::Bool && shadow->b);
    const json::Value* fade = light->get("fade_offset");
    CHECK(fade && fade->arr.size() == 2 && fade->arr[0].number() == 10.0 && fade->arr[1].number() == 200.0);
}

// Out-of-range and unparsable values write no field and each one is warned. A valid key on the same light still lands.
TEST(light_keys_out_of_range_skipped_with_warning) {
    const std::string out = scratch("bad_values");
    const std::string bad =
        "{\n\"classname\" \"light_spot\"\n\"origin\" \"200 0 200\"\n\"angles\" \"70 180 0\"\n"
        "\"iw8r_bulb_radius\" \"-1\"\n\"iw8r_falloff\" \"0\"\n\"iw8r_fade\" \"1\"\n\"iw8r_shadows\" \"maybe\"\n"
        "\"iw8r_intensity_units\" \"-2\"\n\"iw8r_shadow_bias\" \"nan\"\n\"iw8r_uv_intensity\" \"abc\"\n"
        "\"iw8r_ir_intensity\" \"0.25\"\n}\n";
    const std::string tiny =
        "{\n\"classname\" \"light_spot\"\n\"origin\" \"600 0 200\"\n\"angles\" \"70 180 0\"\n\"iw8r_bulb_radius\" \"1e-12\"\n}\n";
    CompileReport rep;
    if (!compileText(worldText(bad + tiny), out, rep)) {
        CHECK(false);
        return;
    }
    CHECK(countWarnings(rep, "left out") == 8);  // 7 bad values on the first light, bulb_radius 1e-12 on the second
    CHECK(hasWarning(rep, "iw8r_bulb_radius '-1'"));
    CHECK(hasWarning(rep, "iw8r_falloff '0'"));
    CHECK(hasWarning(rep, "iw8r_fade '1'"));
    CHECK(hasWarning(rep, "iw8r_shadows 'maybe'"));
    CHECK(hasWarning(rep, "iw8r_intensity_units '-2'"));
    CHECK(hasWarning(rep, "iw8r_shadow_bias 'nan'"));
    CHECK(hasWarning(rep, "iw8r_uv_intensity 'abc'"));
    CHECK(hasWarning(rep, "iw8r_bulb_radius '1e-12'"));

    bool ok = false;
    const json::Value comworld = loadJson(comworldPath(out), ok);
    CHECK(ok);
    if (!ok) return;
    const json::Value* first = primaryLight(comworld, 2);
    CHECK(first && first->get("type") && first->get("type")->number() == 2.0);  // the light is still written
    CHECK(!hasField(first, "bulb_radius"));
    CHECK(!hasField(first, "distance_falloff"));
    CHECK(!hasField(first, "fade_offset"));
    CHECK(!hasField(first, "shadow_bias"));
    CHECK(!hasField(first, "intensity"));
    CHECK(!hasField(first, "uv_intensity"));
    CHECK(numberIs(first, "ir_intensity", 0.25));  // the one valid key on the light is written
    const json::Value* shadow = first ? first->get("can_use_shadow_map") : nullptr;
    CHECK(shadow && shadow->type == json::Value::Type::Bool && !shadow->b);
    const json::Value* second = primaryLight(comworld, 3);
    CHECK(second && !hasField(second, "bulb_radius"));
}

// A shadow key on an omni is warned and ignored; the other keys on the same omni are still written.
TEST(light_keys_omni_ignores_shadows_keeps_others) {
    const std::string out = scratch("omni");
    const std::string omni =
        "{\n\"classname\" \"light\"\n\"origin\" \"-200 0 200\"\n\"_color\" \"1 0.8 0.6\"\n\"radius\" \"600\"\n"
        "\"iw8r_shadows\" \"1\"\n\"iw8r_bulb_radius\" \"3\"\n}\n";
    CompileReport rep;
    if (!compileText(worldText(omni), out, rep)) {
        CHECK(false);
        return;
    }
    CHECK(hasWarning(rep, "iw8r_shadows applies to spot lights"));
    bool ok = false;
    const json::Value comworld = loadJson(comworldPath(out), ok);
    CHECK(ok);
    if (!ok) return;
    const json::Value* light = primaryLight(comworld, 2);
    CHECK(light && numberIs(light, "type", 3));
    const json::Value* shadow = light ? light->get("can_use_shadow_map") : nullptr;
    CHECK(shadow && shadow->type == json::Value::Type::Bool && !shadow->b);
    CHECK(numberIs(light, "bulb_radius", 3));
}

// 0 and false leave the shadow flag off without a warning; true sets it on a spot.
TEST(light_keys_shadow_switch_forms) {
    const std::string out = scratch("switch");
    const std::string text =
        "{\n\"classname\" \"light_spot\"\n\"origin\" \"200 0 200\"\n\"angles\" \"70 180 0\"\n\"iw8r_shadows\" \"true\"\n}\n"
        "{\n\"classname\" \"light_spot\"\n\"origin\" \"600 0 200\"\n\"angles\" \"70 180 0\"\n\"iw8r_shadows\" \"false\"\n}\n";
    CompileReport rep;
    if (!compileText(worldText(text), out, rep)) {
        CHECK(false);
        return;
    }
    CHECK(!hasWarning(rep, "iw8r_"));
    bool ok = false;
    const json::Value comworld = loadJson(comworldPath(out), ok);
    CHECK(ok);
    if (!ok) return;
    const json::Value* on = primaryLight(comworld, 2);
    const json::Value* off = primaryLight(comworld, 3);
    CHECK(on && on->get("can_use_shadow_map") && on->get("can_use_shadow_map")->b);
    CHECK(off && off->get("can_use_shadow_map") && !off->get("can_use_shadow_map")->b);
}

// A spot with no light keys carries exactly the base fields, and the build reports nothing about keys.
TEST(light_keys_none_leaves_light_unchanged) {
    const std::string out = scratch("none");
    const std::string spot = "{\n\"classname\" \"light_spot\"\n\"origin\" \"200 0 200\"\n\"angles\" \"70 180 0\"\n}\n";
    CompileReport rep;
    if (!compileText(worldText(spot), out, rep)) {
        CHECK(false);
        return;
    }
    CHECK(!hasWarning(rep, "iw8r_"));
    bool ok = false;
    const json::Value comworld = loadJson(comworldPath(out), ok);
    CHECK(ok);
    if (!ok) return;
    const json::Value* light = primaryLight(comworld, 2);
    CHECK(light && light->obj.size() == sizeof(kBaseFields) / sizeof(kBaseFields[0]));
    if (!light) return;
    for (const char* f : kBaseFields) CHECK(light->get(f) != nullptr);
    CHECK(numberIs(light, "exponent", 0));
    const json::Value* shadow = light->get("can_use_shadow_map");
    CHECK(shadow && shadow->type == json::Value::Type::Bool && !shadow->b);
    const json::Value* def = light->get("definition");
    CHECK(def && def->string() && *def->string() == "light_point_linear");
}
