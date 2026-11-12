// Build preflight (src/core/build_preflight.*): each check against a small fake extraction folder, plus the real showcase
// build when it is present (samples\dev\iw8build\mp_iw8r_dev_sho). The fake builds live under the test temp dir.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "build_preflight.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

const char* kMap = "mp_pf_test";

void writeText(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << text;
}

fs::path freshDir(const std::string& name) {
    const fs::path d = fs::path(test::tmpDir()) / "build_preflight" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

// Walks up from the working directory until `rel` exists (tests run from bin\release or the repo root).
std::string findUp(const std::string& rel) {
    fs::path p = fs::current_path();
    for (int up = 0; up < 6; ++up, p = p.parent_path())
        if (fs::exists(p / rel)) return (p / rel).string();
    return "";
}

// Names the fake scripts call. Real catalogs are read in the loader test and the showcase test.
PreflightCatalogs testCatalogs() {
    PreflightCatalogs c;
    c.functions = {"spawn", "loadfx", "getentarray", "playfx", "spawnfx", "playsoundatpos"};
    c.methods = {"solid", "playsound", "moveto"};
    c.effects = {"vfx/core/expl/spark_death"};
    c.sounds = {"snd_ok"};
    return c;
}

// A script that uses every kind of reference the checks look at, and is clean.
const char* kGoodGsc = R"G(// Map script for the preflight tests.
main()
{
    maps\mp\_load::main();
    setExpFog(128, 3000, 0.3, 0.3, 0.5);
    level.fx[0] = loadfx("vfx/core/expl/spark_death");
    level thread place();
    iw8r_make_solid("door");
}

place()
{
    wait 0.05;
    ent = spawn("script_origin", (0, 0, 0));
    ent playsound("snd_ok");
    playfx(level.fx[0], (0, 0, 0));
}

iw8r_make_solid(name)
{
    ents = getentarray(name, "targetname");
    foreach (ent in ents)
        ent solid();
}
)G";

const char* kGoodEnts = R"E({
"classname" "worldspawn"
"sundirection" "-45 60 0"
}
{
"classname" "script_model"
"model" "crate_test"
"targetname" "door"
"origin" "0 0 0"
}
{
"classname" "script_origin"
"model" "*1"
"origin" "0 0 0"
}
{
"classname" "fx_origin"
"fx" "vfx/core/expl/spark_death"
"origin" "0 0 0"
}
{
"classname" "sound_emitter"
"soundalias" "snd_ok"
"origin" "0 0 0"
}
)E";

const char* kGoodWorld = R"J({"schema":1,"name":"maps/mp/mp_pf_test.d3dbsp","sky":"sky_test","bounds":[[0,0,0],[1,1,1]],"reflection_probes":[{"image":"probe_test","origin":[0,0,0]}],"models":[{"model":"crate_test","origin":[0,0,0],"scale":1}],"brush_models":[{"surfaces":[{"material":"mat/wall","start":0,"count":3},{"material":"$white","start":3,"count":3}]}]})J";

const char* kWallMaterial = R"M({"techniqueSet":"iw8rcm_test","textures":[{"name":"colorMap","semantic":"colorMap","image":"wall_c"},{"name":"normalMap","semantic":"normalMap","image":"$identitynormalmap"}]})M";

const char* kCrateTable = R"T({"collLod":0,"flags":0,"lods":[{"distance":0}],"rootBoneName":"","type":"rigid"})T";

const char* kCrateGeometry =
    R"R({"schema":1,"name":"crate_test","bone_count":1,"lods":[{"distance":0,"surfaces":[{"material":"mat/wall","vertices":[],"indices":[]}]}]})R";

// A complete, clean extraction folder. Returns its `extracted` directory.
fs::path makeBuild(const std::string& name, const std::string& gsc = kGoodGsc) {
    const fs::path ex = freshDir(name) / "extracted";
    writeText(ex / "maps" / "mp" / (std::string(kMap) + ".gsc"), gsc);
    writeText(ex / "maps" / "mp" / (std::string(kMap) + ".d3dbsp.ents"), kGoodEnts);
    writeText(ex / "maps" / "mp" / (std::string(kMap) + ".d3dbsp.replay-world.json"), kGoodWorld);
    writeText(ex / "materials" / "mat" / "wall.json", kWallMaterial);
    writeText(ex / "images" / "wall_c.dds", "DDS ");
    writeText(ex / "images" / "sky_test.dds", "DDS ");
    writeText(ex / "images" / "probe_test.dds", "DDS ");
    writeText(ex / "xmodel" / "crate_test.json", kCrateTable);
    writeText(ex / "xmodel" / "crate_test.replay.json", kCrateGeometry);
    return ex;
}

size_t countIssues(const std::vector<PreflightIssue>& v, bool error, const std::string& category, const std::string& needle) {
    return size_t(std::count_if(v.begin(), v.end(), [&](const PreflightIssue& i) {
        return i.error == error && i.category == category && i.message.find(needle) != std::string::npos;
    }));
}

size_t errorCount(const std::vector<PreflightIssue>& v) {
    return size_t(std::count_if(v.begin(), v.end(), [](const PreflightIssue& i) { return i.error; }));
}

void printIssues(const char* label, const std::vector<PreflightIssue>& v) {
    for (const PreflightIssue& i : v) std::printf("    [%s] %s %s: %s\n", label, i.error ? "error" : "warning", i.category.c_str(), i.message.c_str());
}

// Runs the check on a build and prints what it found (helps when a check fails).
std::vector<PreflightIssue> run(const fs::path& ex) {
    std::vector<PreflightIssue> v = preflightExtraction(ex.string(), kMap, testCatalogs());
    printIssues("run", v);
    return v;
}

}  // namespace

TEST(preflight_clean_build_has_no_issues) {
    const auto v = run(makeBuild("clean"));
    CHECK(v.empty());
}

TEST(preflight_missing_material_is_error) {
    const fs::path ex = makeBuild("missing_material");
    fs::remove(ex / "materials" / "mat" / "wall.json");
    const auto v = run(ex);
    CHECK(errorCount(v) == 1);
    CHECK(countIssues(v, true, "material", "mat/wall") == 1);
}

TEST(preflight_missing_image_of_material_is_error) {
    const fs::path ex = makeBuild("missing_image");
    fs::remove(ex / "images" / "wall_c.dds");
    const auto v = run(ex);
    CHECK(errorCount(v) == 1);
    CHECK(countIssues(v, true, "image", "wall_c") == 1);
}

TEST(preflight_material_texture_naming_missing_image_is_error) {
    const fs::path ex = makeBuild("material_texture");
    writeText(ex / "materials" / "mat" / "wall.json",
              R"M({"techniqueSet":"t","textures":[{"name":"colorMap","image":"absent_tex"}]})M");
    const auto v = run(ex);
    CHECK(countIssues(v, true, "image", "absent_tex") == 1);
}

TEST(preflight_sky_and_probe_images_are_checked) {
    const fs::path ex = makeBuild("sky_probe");
    fs::remove(ex / "images" / "sky_test.dds");
    fs::remove(ex / "images" / "probe_test.dds");
    const auto v = run(ex);
    CHECK(errorCount(v) == 2);
    CHECK(countIssues(v, true, "image", "sky_test") == 1);
    CHECK(countIssues(v, true, "image", "probe_test") == 1);
}

TEST(preflight_missing_world_model_is_error) {
    const fs::path ex = makeBuild("missing_model");
    fs::remove(ex / "xmodel" / "crate_test.json");
    const auto v = run(ex);
    CHECK(errorCount(v) == 1);
    CHECK(countIssues(v, true, "model", "crate_test") == 1);
}

TEST(preflight_missing_entity_model_is_error) {
    const fs::path ex = makeBuild("missing_ents_model");
    writeText(ex / "maps" / "mp" / (std::string(kMap) + ".d3dbsp.ents"),
              std::string(kGoodEnts) + "{\n\"classname\" \"script_model\"\n\"model\" \"ghost_model\"\n}\n");
    const auto v = run(ex);
    // Both the table and the geometry are missing: one error each.
    CHECK(countIssues(v, true, "model", "ghost_model") == 2);
    CHECK(errorCount(v) == 2);
}

TEST(preflight_xmodel_surface_material_is_checked) {
    const fs::path ex = makeBuild("xmodel_material");
    writeText(ex / "xmodel" / "crate_test.replay.json",
              R"R({"schema":1,"name":"crate_test","lods":[{"distance":0,"surfaces":[{"material":"mat/gone"}]}]})R");
    const auto v = run(ex);
    CHECK(countIssues(v, true, "material", "mat/gone") == 1);
    CHECK(errorCount(v) == 1);
}

TEST(preflight_unknown_function_is_error_with_line) {
    std::string gsc = kGoodGsc;
    const size_t at = gsc.find("    wait 0.05;");
    gsc.insert(at, "    frobnicate(1);\n");
    const size_t line = size_t(std::count(gsc.begin(), gsc.begin() + long(at), '\n')) + 1;
    const fs::path ex = makeBuild("unknown_function", gsc);
    const auto v = run(ex);
    CHECK(errorCount(v) == 1);
    CHECK(countIssues(v, true, "script", ".gsc:" + std::to_string(line) + ": unknown function 'frobnicate()'") == 1);
}

TEST(preflight_unknown_method_is_error) {
    std::string gsc = kGoodGsc;
    gsc.insert(gsc.find("    playfx("), "    ent frobnicate();\n");
    const auto v = run(makeBuild("unknown_method", gsc));
    CHECK(errorCount(v) == 1);
    CHECK(countIssues(v, true, "script", "unknown method 'frobnicate()'") == 1);
}

// self / level are entities: a builtin method called on them is fine, an unknown one is an unknown method.
TEST(preflight_methods_on_self_and_level) {
    std::string gsc = kGoodGsc;
    gsc.insert(gsc.find("    playfx("), "    self solid();\n    self moveto((0, 0, 8), 1);\n    level frobnicate();\n");
    const auto v = run(makeBuild("self_methods", gsc));
    CHECK(errorCount(v) == 1);
    CHECK(countIssues(v, true, "script", "unknown method 'frobnicate()' called on 'level'") == 1);
}

TEST(preflight_comments_and_strings_are_not_calls) {
    std::string gsc = kGoodGsc;
    gsc.insert(gsc.find("    playfx("),
               "    // frobnicate(2);\n    /* nothing(3); */\n    level.label = \"frobnicate(4) bogus(5)\";\n");
    const auto v = run(makeBuild("comments_strings", gsc));
    CHECK(v.empty());
}

TEST(preflight_missing_main_is_error) {
    const auto v = run(makeBuild("no_main", "place()\n{\n    wait 1;\n}\n"));
    CHECK(countIssues(v, true, "script", "main() is missing") == 1);
}

TEST(preflight_unbalanced_brackets_is_error) {
    std::string gsc = kGoodGsc;
    gsc.erase(gsc.rfind('}'), 1);  // iw8r_make_solid loses its closing brace
    const auto v = run(makeBuild("brackets", gsc));
    CHECK(countIssues(v, true, "script", "'{' is never closed") == 1);
}

TEST(preflight_own_script_qualified_call_is_checked) {
    std::string gsc = kGoodGsc;
    gsc.insert(gsc.find("    playfx("), "    maps\\mp\\mp_pf_test::nothere();\n    maps\\mp\\mp_pf_test::place();\n");
    const auto v = run(makeBuild("own_qualified", gsc));
    CHECK(errorCount(v) == 1);
    CHECK(countIssues(v, true, "script", "nothere() is not defined in this map script") == 1);
}

TEST(preflight_bad_qualified_path_is_warning) {
    std::string gsc = kGoodGsc;
    gsc.insert(gsc.find("    playfx("), "    scripts/mp/utility::fn();\n");
    const auto v = run(makeBuild("bad_path", gsc));
    CHECK(errorCount(v) == 0);  // a call into another script is not checked for its name
    CHECK(countIssues(v, false, "script", "GSC paths use backslashes") == 1);
}

TEST(preflight_other_qualified_calls_are_allowed) {
    std::string gsc = kGoodGsc;
    gsc.insert(gsc.find("    playfx("), "    maps\\mp\\_utility::somefn();\n    scripts\\mp\\utility::fn2();\n");
    const auto v = run(makeBuild("other_qualified", gsc));
    CHECK(v.empty());
}

TEST(preflight_effect_outside_catalog_is_warning) {
    std::string gsc = kGoodGsc;
    gsc.insert(gsc.find("    playfx("), "    level.fx[1] = loadfx(\"vfx/unknown/not_always_loaded\");\n");
    const auto v = run(makeBuild("effect_warning", gsc));
    CHECK(errorCount(v) == 0);
    CHECK(countIssues(v, false, "effect", "vfx/unknown/not_always_loaded") == 1);
}

TEST(preflight_effect_carried_by_build_is_accepted) {
    std::string gsc = kGoodGsc;
    gsc.insert(gsc.find("    playfx("), "    level.fx[1] = loadfx(\"vfx/unknown/carried\");\n");
    const fs::path ex = makeBuild("effect_carried", gsc);
    writeText(ex / "fx" / "vfx" / "unknown" / "carried.json", "{}");
    const auto v = run(ex);
    CHECK(v.empty());
}

// 8.2: an authored effect's IW3 graph (fx/<name>.iw3.json) counts as carried.
TEST(preflight_authored_effect_is_accepted) {
    std::string gsc = kGoodGsc;
    gsc.insert(gsc.find("    playfx("), "    level.fx[1] = loadfx(\"iw8r/campfire\");\n");
    const fs::path ex = makeBuild("effect_authored", gsc);
    writeText(ex / "fx" / "iw8r" / "campfire.iw3.json", "{}");
    CHECK(run(ex).empty());
}

TEST(preflight_sound_outside_catalog_is_warning) {
    std::string gsc = kGoodGsc;
    gsc.insert(gsc.find("    playfx("), "    playsoundatpos((0, 0, 0), \"snd_missing\");\n");
    const fs::path ex = makeBuild("sound_warning", gsc);
    writeText(ex / "sound" / (std::string(kMap) + ".stock_sounds.txt"), "snd_listed_missing 0\nsnd_ok 0\n");
    const auto v = run(ex);
    CHECK(errorCount(v) == 0);
    CHECK(countIssues(v, false, "sound", "snd_missing") == 1);
    CHECK(countIssues(v, false, "sound", "snd_listed_missing") == 1);
    CHECK(countIssues(v, false, "sound", "snd_ok") == 0);
}

TEST(preflight_user_script_include_is_error) {
    std::string gsc = kGoodGsc;
    gsc += "\n// ---- map script (worldspawn iw8r_script) ----\n#include scripts\\mp\\utility;\nuser_main()\n{\n}\n";
    const auto v = run(makeBuild("user_include", gsc));
    CHECK(countIssues(v, true, "script", "cannot be used") == 1);
}

TEST(preflight_user_script_redefining_generated_function_is_error) {
    std::string gsc = kGoodGsc;
    gsc += "\n// ---- map script (worldspawn iw8r_script) ----\niw8r_make_solid(name)\n{\n}\nuser_main()\n{\n}\n";
    const auto v = run(makeBuild("user_redefine", gsc));
    CHECK(countIssues(v, true, "script", "iw8r_make_solid() is defined twice") == 1);
}

TEST(preflight_user_script_calls_are_checked) {
    std::string gsc = kGoodGsc;
    gsc += "\n// ---- map script (worldspawn iw8r_script) ----\nuser_main()\n{\n    level thread user_helper();\n    level thread helper();\n}\n"
           "helper()\n{\n    wait 1;\n}\n";
    const auto v = run(makeBuild("user_calls", gsc));
    CHECK(errorCount(v) == 1);
    CHECK(countIssues(v, true, "script", "unknown function 'user_helper()'") == 1);
}

TEST(preflight_missing_map_files_are_errors) {
    const fs::path ex = freshDir("no_map_files") / "extracted";
    fs::create_directories(ex);
    const auto v = run(ex);
    CHECK(countIssues(v, true, "build", ".gsc") == 1);
    CHECK(countIssues(v, true, "build", ".d3dbsp.ents") == 1);
    CHECK(countIssues(v, true, "build", "replay-world.json") == 1);
}

TEST(preflight_bad_map_id_is_error) {
    const auto v = preflightExtraction(makeBuild("bad_id").string(), "../escape", testCatalogs());
    CHECK(countIssues(v, true, "build", "not a plain name") == 1);
}

TEST(preflight_catalog_loader_reads_data_folder) {
    const std::string gen = findUp("data/generated/iw8_gsc_builtins.txt");
    if (gen.empty()) {
        test::skipf("  SKIP data folder not found\n");
        return;
    }
    PreflightCatalogs cat;
    std::string err;
    const std::string data = fs::path(gen).parent_path().parent_path().string();
    CHECK(loadPreflightCatalogs(data, cat, &err));
    CHECK(cat.functions.count("loadfx") == 1);
    CHECK(cat.functions.count("getentarray") == 1);
    CHECK(cat.methods.count("solid") == 1);
    CHECK(cat.effects.count("vfx/core/expl/spark_death") == 1);
    CHECK(cat.sounds.count("2pop") == 1);
    CHECK(std::none_of(cat.effects.begin(), cat.effects.end(), [](const std::string& s) { return !s.empty() && s[0] == '#'; }));
    PreflightCatalogs none;
    CHECK(!loadPreflightCatalogs((fs::path(test::tmpDir()) / "no_such_data").string(), none, &err));
}

TEST(preflight_real_showcase_build) {
    const std::string ex = findUp("samples/dev/iw8build/mp_iw8r_dev_sho/extraction/extracted");
    const std::string gen = findUp("data/generated/iw8_gsc_builtins.txt");
    if (ex.empty() || gen.empty()) {
        test::skipf("  SKIP showcase build or data folder not found\n");
        return;
    }
    PreflightCatalogs cat;
    std::string err;
    CHECK(loadPreflightCatalogs(fs::path(gen).parent_path().parent_path().string(), cat, &err));
    const auto v = preflightExtraction(ex, "mp_iw8r_dev_sho", cat);
    printIssues("showcase", v);
    CHECK(errorCount(v) == 0);
}
