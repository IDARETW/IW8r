// Prefab bundles (docs/PREFAB_BUNDLES.md): manifest reading and writing, folder resolution, names per placement,
// prefab scripts in the generated map script, the build's expansion, save-selection, and the bundles shipped in
// data/prefabs/bundles.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "compile_iw8.h"
#include "document.h"
#include "map_script.h"
#include "prefab.h"
#include "prefab_bundle.h"
#include "map_package.h"
#include "script_check.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path p = fs::path(test::tmpDir()) / "prefab_bundle" / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}

void put(const fs::path& p, const std::string& t) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << t;
}

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string box(const AABB& b, const char* material) {
    const auto prim = makeBrushPrimitive(makeBox(b, std::string(" ") + material + " 128 128 0 0 0 0"), "\n");
    return prim ? prim->text() : std::string();
}

MapFile parse(const std::string& text) {
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    return pr.map ? *pr.map : MapFile{};
}

bool has(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }

size_t count(const std::string& text, const std::string& needle) {
    size_t n = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) ++n;
    return n;
}

bool hasLine(const std::vector<std::string>& lines, const std::string& needle) {
    for (const auto& l : lines)
        if (has(l, needle)) return true;
    return false;
}

uint64_t fnv64(const std::string& bytes) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : bytes) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    return h;
}

// Every file under a folder: path relative to it -> bytes.
std::map<std::string, std::string> folderBytes(const fs::path& root) {
    std::map<std::string, std::string> out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file()) out[it->path().lexically_relative(root).generic_string()] = slurp(it->path());
    return out;
}

// <repo>/<rel>, found by walking up from the working directory ("" when missing).
fs::path repoPath(const std::string& rel) {
    fs::path p = fs::current_path();
    for (int up = 0; up < 6; ++up, p = p.parent_path())
        if (fs::exists(p / rel)) return p / rel;
    return {};
}

// A small uncompressed 24-bit TGA.
std::string tgaBytes(int w, int h, unsigned char r, unsigned char g, unsigned char b) {
    std::string s(18, '\0');
    s[2] = 2;
    s[12] = static_cast<char>(w & 255);
    s[13] = static_cast<char>(w >> 8);
    s[14] = static_cast<char>(h & 255);
    s[15] = static_cast<char>(h >> 8);
    s[16] = 24;
    s[17] = 0x20;
    for (int i = 0; i < w * h; ++i) {
        s += static_cast<char>(b);
        s += static_cast<char>(g);
        s += static_cast<char>(r);
    }
    return s;
}

// A short 16-bit mono WAV (8 samples at 8000 Hz).
std::string wavBytes() {
    const auto u32 = [](uint32_t v) { return std::string{char(v & 255), char((v >> 8) & 255), char((v >> 16) & 255), char((v >> 24) & 255)}; };
    const auto u16 = [](uint32_t v) { return std::string{char(v & 255), char((v >> 8) & 255)}; };
    const std::string data(16, '\0');
    return "RIFF" + u32(36 + 16) + "WAVEfmt " + u32(16) + u16(1) + u16(1) + u32(8000) + u32(16000) + u16(2) + u16(16) + "data" + u32(16) + data;
}

// A floor and the two spawns the converter needs, then `entities`.
std::string hostText(const std::string& entities) {
    std::string t = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + box({{-512, -512, -16}, {512, 512, 0}}, "world/a") + "}\n";
    t += "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"-300 0 16\"\n\"angles\" \"0 0 0\"\n}\n";
    t += "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"300 0 16\"\n\"angles\" \"0 180 0\"\n}\n";
    return t + entities;
}

std::string placement(const std::string& model, const std::string& origin, const std::string& extra = {}) {
    return "{\n\"classname\" \"misc_prefab\"\n\"model\" \"" + model + "\"\n\"origin\" \"" + origin + "\"\n" + extra + "}\n";
}

bool compileMap(const std::string& text, const fs::path& out, const CompileOptions& base, const std::string& mapDir, CompileReport& rep,
                std::string* err = nullptr) {
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    if (!pr.map) return false;
    CompileOptions opt = base;
    if (opt.mapId.empty()) opt.mapId = "mp_bundle_t";
    opt.mapDir = mapDir;
    std::error_code ec;
    fs::remove_all(out, ec);
    std::string e;
    const bool ok = compileIw8Extraction(*pr.map, out.string(), opt, rep, &e);
    if (err) *err = e;
    else if (!ok) std::printf("  compile error: %s\n", e.c_str());
    return ok;
}

fs::path mapsDir(const fs::path& out) { return out / "extracted" / "maps" / "mp"; }

// The manifest of design section B.3, with fields this editor does not know at three levels.
const char* kExampleManifest = R"JSON({
  "format": "iw8r-prefab-1",
  "id": "stock_door_wood_single",
  "title": "Stock door, painted wood, single",
  "description": "The wooden door of Talsik Backlot.",
  "category": "doors",
  "tags": ["door", "interactive", "stock", "scriptable"],
  "author": "someone",
  "version": 2,
  "future_top": {"keep": [1, 2, 3]},
  "map": "prefab.map",
  "thumbnail": "thumb.png",
  "pivot": "hinge, floor level",
  "bounds": {"mins": [-4, -8, 0], "maxs": [56, 8, 96]},
  "placement": {"surface": "wall", "snap": 4, "yaw_step": 45, "future_placement": true},
  "names": {"local": ["door"], "global": ["flag_primary"], "keys": ["my_link"]},
  "parameters": [
    {"name": "angle", "label": "Swing", "type": "number", "default": 90, "min": 10, "max": 170,
     "apply": {"selector": "door", "key": "iw8r_door", "template": "hinge {angle} 0.8"}, "future_item": "x"},
    {"name": "wood", "type": "material", "default": "twc/wood", "apply": {"material": "twc/wood"}},
    {"name": "mode", "type": "choice", "choices": ["a", "b"], "apply": {"selector": "*", "key": "script_noteworthy", "template": "{mode}"}}
  ],
  "script": {"file": "script.gsc", "entry": "pf_main", "prefix": "pf_", "once": "pf_once"},
  "assets": {
    "materials": [{"name": "custom/brick", "file": "materials/brick.iw8mat.json"}],
    "models": [{"name": "crate", "file": "models/crate.obj"}],
    "sounds": [{"alias": "beep", "file": "sounds/beep.wav", "loop": true}],
    "effects": [{"name": "fire/small", "file": "effects/fire/small.iw8fx.json"}]
  },
  "requires": {
    "scriptable_defs": [{"name": "scriptable_door_wooden_panel_03_painted_mp", "zones": ["mp_backlot2", "mp_hideout"]}],
    "stock_models": [{"name": "tm/door_frame_wooden_panel_03_painted_mp", "zones": ["mp_backlot2"], "future_ref": 1}],
    "stock_materials": [{"name": "tm/wood_a"}],
    "stock_sounds": ["scrpt_door_wood_single_open"],
    "stock_effects": ["vfx/x"],
    "stock_strings": ["MP/DOOR_USE_OPEN"],
    "stock_scripts": ["scripts/engine/scriptable_door::system_init"],
    "build": ["scriptable_instances", "scriptable_def_carry", "stock_script_calls"]
  },
  "fallback": {"when_missing": ["scriptable_instances"], "map": "fallback.map", "script": "fallback.gsc", "note": "built as a scripted door"},
  "game_modes": ["koth"]
})JSON";

std::string errorOf(const std::string& json) {
    PrefabManifest m;
    std::string err;
    return parsePrefabManifest(json, m, &err) ? std::string("(accepted)") : err;
}

// A manifest with only `body` after the two required fields.
std::string with(const std::string& body) {
    return "{\"format\": \"iw8r-prefab-1\", \"id\": \"t\"" + (body.empty() ? std::string() : ", " + body) + "}";
}

// The fixture of the "bare prefab" proof: lamp.map (a pole and a light) and doorset.map (wall, door + trigger,
// teleporter + destination, effect, sound, a nested lamp), placed twice by host.map. Returns the host text.
std::string writeBareFixture(const fs::path& src) {
    put(src / "prefabs" / "lamp.map", "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + box({{-4, -4, 0}, {4, 4, 96}}, "world/a") +
                                          "}\n{\n\"classname\" \"light\"\n\"origin\" \"0 0 100\"\n}\n");
    put(src / "prefabs" / "doorset.map",
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + box({{-64, -8, 0}, {-32, 8, 96}}, "world/b") + "}\n" +
            "{\n\"classname\" \"script_brushmodel\"\n\"targetname\" \"door1\"\n\"iw8r_door\" \"hinge 90 0.8\"\n" +
            box({{-32, -2, 0}, {32, 2, 96}}, "world/a") + "}\n" +
            "{\n\"classname\" \"trigger_use\"\n\"targetname\" \"door1_trig\"\n\"target\" \"door1\"\n" +
            box({{-48, -48, 0}, {48, 48, 96}}, "trigger") + "}\n" +
            "{\n\"classname\" \"trigger_multiple\"\n\"targetname\" \"tp_in\"\n\"iw8r_teleport\" \"tp_out\"\n" +
            box({{100, -16, 0}, {132, 16, 64}}, "trigger") + "}\n" +
            "{\n\"classname\" \"script_origin\"\n\"targetname\" \"tp_out\"\n\"origin\" \"0 200 8\"\n\"angles\" \"0 90 0\"\n}\n" +
            "{\n\"classname\" \"fx_origin\"\n\"fx\" \"vfx/iw8/level/fire/vfx_fire_small\"\n\"origin\" \"0 64 8\"\n}\n" +
            "{\n\"classname\" \"sound_emitter\"\n\"soundalias\" \"emt_fire_small\"\n\"loop\" \"1\"\n\"origin\" \"0 64 8\"\n}\n" +
            "{\n\"classname\" \"misc_prefab\"\n\"model\" \"lamp.map\"\n\"origin\" \"64 64 0\"\n\"angles\" \"0 45 0\"\n}\n");
    std::string host = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + box({{-512, -512, -16}, {512, 512, 0}}, "world/a") + "}\n";
    host += "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"-300 0 16\"\n\"angles\" \"0 0 0\"\n}\n";
    host += "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"300 0 16\"\n\"angles\" \"0 180 0\"\n}\n";
    host += "{\n\"classname\" \"misc_prefab\"\n\"model\" \"prefabs/doorset.map\"\n\"origin\" \"-200 -200 0\"\n}\n";
    host += "{\n\"classname\" \"misc_prefab\"\n\"model\" \"prefabs/doorset.map\"\n\"origin\" \"200 200 0\"\n\"angles\" \"0 90 0\"\n"
            "\"iw8r_ovr:class=light:color\" \"1 0.5 0.2\"\n\"iw8r_mat:world/b\" \"world/a\"\n}\n";
    put(src / "host.map", host);
    return host;
}

// The contents of the door kit used by the name tests: a door with its trigger, a teleporter with its destination,
// a link to a name outside the prefab, and a name nothing inside refers to.
std::string doorKitMap() {
    return "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + box({{-64, -8, 0}, {-32, 8, 96}}, "world/b") + "}\n" +
           "{\n\"classname\" \"script_brushmodel\"\n\"targetname\" \"door\"\n\"iw8r_door\" \"hinge 90 0.8\"\n" +
           box({{-32, -2, 0}, {32, 2, 96}}, "world/a") + "}\n" +
           "{\n\"classname\" \"trigger_use\"\n\"targetname\" \"door_trig\"\n\"target\" \"door\"\n" + box({{-48, -48, 0}, {48, 48, 96}}, "trigger") +
           "}\n" +
           "{\n\"classname\" \"trigger_multiple\"\n\"targetname\" \"tp\"\n\"iw8r_teleport\" \"tp_dest\"\n\"target\" \"flag_primary\"\n" +
           box({{100, -16, 0}, {132, 16, 64}}, "trigger") + "}\n" +
           "{\n\"classname\" \"script_origin\"\n\"targetname\" \"tp_dest\"\n\"origin\" \"0 200 8\"\n}\n" +
           "{\n\"classname\" \"script_origin\"\n\"targetname\" \"hqloc\"\n\"origin\" \"0 0 8\"\n}\n" +
           "{\n\"classname\" \"script_origin\"\n\"script_linkname\" \"l1\"\n\"origin\" \"0 0 8\"\n}\n" +
           "{\n\"classname\" \"script_origin\"\n\"script_linkto\" \"l1 outside_link\"\n\"origin\" \"0 0 8\"\n}\n";
}

// Flattens host text that lives in `dir`; returns the flattened map text.
std::string flatten(const fs::path& dir, const std::string& host, std::vector<std::string>& warnings, PrefabFlattenInfo* info = nullptr,
                    const PrefabBuildSupport& support = {}, std::string* err = nullptr, bool* ok = nullptr) {
    MapFile out;
    PrefabFlattenInfo local;
    std::string e;
    const bool good = flattenPrefabBundles(parse(host), (dir / "host.map").string(), {}, support, out, info ? *info : local, &warnings, &e);
    if (ok) *ok = good;
    else CHECK(good);
    if (err) *err = e;
    return good ? out.text() : std::string();
}

// Key values of every entity of a map text that has `key`.
std::vector<std::string> valuesOf(const std::string& mapText, const std::string& key) {
    std::vector<std::string> out;
    for (const Entity& e : parse(mapText).entities)
        if (const std::string* v = e.value(key)) out.push_back(*v);
    return out;
}

bool contains(const std::vector<std::string>& list, const std::string& s) { return std::find(list.begin(), list.end(), s) != list.end(); }

}  // namespace

// ------------------------------------------------------------------ manifest

TEST(prefab_bundle_manifest_reads_every_field) {
    PrefabManifest m;
    std::string err;
    CHECK(parsePrefabManifest(kExampleManifest, m, &err));
    if (!err.empty()) std::printf("  %s\n", err.c_str());
    CHECK(m.format == "iw8r-prefab-1" && m.id == "stock_door_wood_single");
    CHECK(m.title == "Stock door, painted wood, single" && m.category == "doors" && m.author == "someone");
    CHECK(m.tags.size() == 4 && m.tags[3] == "scriptable");
    CHECK(m.version && *m.version == 2);
    CHECK(m.map == "prefab.map" && m.thumbnail == "thumb.png" && m.pivot == "hinge, floor level");
    CHECK(m.bounds && m.bounds->mins.x == -4 && m.bounds->maxs.z == 96);
    CHECK(m.surface == "wall" && m.snap == 4 && m.yawStep == 45);
    CHECK(m.hasLocalNames && m.namesLocal == std::vector<std::string>{"door"});
    CHECK(m.namesGlobal == std::vector<std::string>{"flag_primary"} && m.nameKeys == std::vector<std::string>{"my_link"});
    CHECK(m.parameters.size() == 3);
    if (m.parameters.size() == 3) {
        const PrefabParameter& a = m.parameters[0];
        CHECK(a.name == "angle" && a.label == "Swing" && a.type == "number" && a.defaultValue == "90");
        CHECK(a.min && *a.min == 10 && a.max && *a.max == 170);
        CHECK(a.selector == "door" && a.key == "iw8r_door" && a.valueTemplate == "hinge {angle} 0.8");
        CHECK(m.parameters[1].material == "twc/wood" && m.parameters[1].defaultValue == "twc/wood");
        CHECK(m.parameters[2].choices.size() == 2);
        // What the inspector writes on the misc_prefab.
        CHECK(prefabParameterKey(a, "120") == (std::pair<std::string, std::string>{"iw8r_ovr:door:iw8r_door", "hinge 120 0.8"}));
        CHECK(prefabParameterKey(m.parameters[1], "twc/oak") == (std::pair<std::string, std::string>{"iw8r_mat:twc/wood", "twc/oak"}));
    }
    CHECK(m.scriptFile == "script.gsc" && m.scriptEntry == "pf_main" && m.scriptPrefix == "pf_" && m.scriptOnce == "pf_once");
    CHECK(m.materials.size() == 1 && m.materials[0].name == "custom/brick" && m.materials[0].file == "materials/brick.iw8mat.json");
    CHECK(m.models.size() == 1 && m.models[0].file == "models/crate.obj");
    CHECK(m.sounds.size() == 1 && m.sounds[0].alias == "beep" && m.sounds[0].loop);
    CHECK(m.effects.size() == 1 && m.effects[0].name == "fire/small");
    CHECK(m.scriptableDefs.size() == 1 && m.scriptableDefs[0].zones.size() == 2);
    CHECK(m.stockModels.size() == 1 && m.stockModels[0].zones == std::vector<std::string>{"mp_backlot2"});
    CHECK(m.stockMaterials.size() == 1 && m.stockMaterials[0].zones.empty());
    CHECK(m.stockSounds.size() == 1 && m.stockEffects.size() == 1 && m.stockStrings.size() == 1 && m.stockScripts.size() == 1);
    CHECK(m.buildFeatures.size() == 3);
    CHECK(m.fallbackWhenMissing == std::vector<std::string>{"scriptable_instances"});
    CHECK(m.fallbackMap == "fallback.map" && m.fallbackScript == "fallback.gsc" && m.fallbackNote == "built as a scripted door");
    CHECK(m.gameModes == std::vector<std::string>{"koth"});
}

TEST(prefab_bundle_manifest_round_trip_keeps_unknown_fields) {
    PrefabManifest m;
    CHECK(parsePrefabManifest(kExampleManifest, m, nullptr));
    m.title = "Renamed";
    m.tags.push_back("new");
    const std::string text = prefabManifestText(m);
    // Unknown fields at the top, in an object and in list items are still there.
    CHECK(has(text, "\"future_top\": {") && has(text, "\"keep\": [1, 2, 3]"));
    CHECK(has(text, "\"future_placement\": true"));
    CHECK(has(text, "\"future_item\": \"x\""));
    CHECK(has(text, "\"future_ref\": 1"));
    PrefabManifest again;
    std::string err;
    CHECK(parsePrefabManifest(text, again, &err));
    if (!err.empty()) std::printf("  %s\n", err.c_str());
    CHECK(again.title == "Renamed" && again.tags.size() == 5 && again.id == m.id);
    CHECK(again.surface == "wall" && again.snap == 4 && again.yawStep == 45);
    CHECK(again.parameters.size() == 3 && again.parameters[0].valueTemplate == "hinge {angle} 0.8" && again.parameters[1].material == "twc/wood");
    CHECK(again.parameters[0].defaultValue == "90" && again.parameters[0].min && *again.parameters[0].max == 170);
    CHECK(again.sounds.size() == 1 && again.sounds[0].loop && again.fallbackNote == m.fallbackNote);
    CHECK(again.scriptEntry == "pf_main" && again.scriptPrefix == "pf_" && again.scriptOnce == "pf_once");
    CHECK(again.stockModels[0].zones == m.stockModels[0].zones && again.buildFeatures == m.buildFeatures);
    CHECK(again.bounds && again.bounds->maxs.x == 56 && again.version && *again.version == 2);
    // Writing what was read changes nothing more.
    CHECK(prefabManifestText(again) == text);

    // A new manifest holds the two required fields and nothing that is only a default.
    PrefabManifest fresh;
    fresh.id = "crate_stack";
    const std::string small = prefabManifestText(fresh);
    CHECK(small == "{\n  \"format\": \"iw8r-prefab-1\",\n  \"id\": \"crate_stack\"\n}\n");
    PrefabManifest back;
    CHECK(parsePrefabManifest(small, back, nullptr));
    CHECK(back.surface == "floor" && back.yawStep == 90 && back.scriptEntry == "prefab_main" && back.scriptPrefix == "prefab_");
    CHECK(!back.hasLocalNames && back.map.empty());
    // An empty names.local list is a statement ("nothing is local") and is written.
    fresh.hasLocalNames = true;
    CHECK(has(prefabManifestText(fresh), "\"local\": []"));
}

TEST(prefab_bundle_manifest_validation_errors) {
    CHECK(has(errorOf("not json"), "prefab.json: not a JSON object"));
    CHECK(has(errorOf("[1]"), "not a JSON object"));
    CHECK(has(errorOf("{\"id\": \"a\"}"), "'format' is missing"));
    CHECK(has(errorOf("{\"format\": 1, \"id\": \"a\"}"), "'format' must be text"));
    CHECK(has(errorOf("{\"format\": \"iw8r-prefab-1\"}"), "'id' is missing"));
    CHECK(has(errorOf("{\"format\": \"iw8r-prefab-1\", \"id\": 7}"), "'id' must be text"));
    CHECK(has(errorOf("{\"format\": \"iw8r-prefab-1\", \"id\": \"Bad Id\"}"), "'id' \"Bad Id\" must be 1-40 characters of a-z 0-9 _"));
    CHECK(has(errorOf("{\"format\": \"iw8r-prefab-1\", \"id\": \"\"}"), "1-40 characters"));
    CHECK(has(errorOf("{\"format\": \"iw8r-prefab-1\", \"id\": \"" + std::string(41, 'a') + "\"}"), "1-40 characters"));
    CHECK(errorOf("{\"format\": \"iw8r-prefab-1\", \"id\": \"" + std::string(40, 'a') + "\"}") == "(accepted)");
    CHECK(errorOf(with("")) == "(accepted)");
    CHECK(has(errorOf(with("\"title\": 3")), "'title' must be text"));
    CHECK(has(errorOf(with("\"description\": []")), "'description' must be text"));
    CHECK(has(errorOf(with("\"category\": 1")), "'category' must be text"));
    CHECK(has(errorOf(with("\"tags\": \"door\"")), "'tags' must be a list of text"));
    CHECK(has(errorOf(with("\"tags\": [1]")), "'tags' must be a list of text"));
    CHECK(has(errorOf(with("\"author\": 1")), "'author' must be text"));
    CHECK(has(errorOf(with("\"version\": \"two\"")), "'version' must be a number"));
    CHECK(has(errorOf(with("\"map\": \"../other.map\"")), "'map' \"../other.map\" must be a file inside the bundle folder"));
    CHECK(has(errorOf(with("\"map\": \"C:/x.map\"")), "inside the bundle folder"));
    CHECK(has(errorOf(with("\"thumbnail\": 1")), "'thumbnail' must be a file name"));
    CHECK(has(errorOf(with("\"pivot\": 1")), "'pivot' must be text"));
    CHECK(has(errorOf(with("\"bounds\": {\"mins\": [0, 0], \"maxs\": [1, 1, 1]}")), "'bounds.mins' must be a list of three numbers"));
    CHECK(has(errorOf(with("\"bounds\": {\"mins\": [2, 0, 0], \"maxs\": [1, 1, 1]}")), "'bounds' has mins larger than maxs"));
    CHECK(has(errorOf(with("\"placement\": 3")), "'placement' must be an object"));
    CHECK(has(errorOf(with("\"placement\": {\"surface\": \"roof\"}")), "'placement.surface' must be one of floor, wall, ceiling, free"));
    CHECK(has(errorOf(with("\"placement\": {\"snap\": -1}")), "'placement.snap' must not be negative"));
    CHECK(has(errorOf(with("\"placement\": {\"yaw_step\": 0}")), "'placement.yaw_step' must be more than 0"));
    CHECK(has(errorOf(with("\"names\": {\"local\": \"door\"}")), "'names.local' must be a list of text"));
    CHECK(has(errorOf(with("\"names\": {\"local\": [\"a\"], \"global\": [\"a\"]}")), "both list \"a\""));
    CHECK(has(errorOf(with("\"names\": {\"keys\": [\"\"]}")), "'names.keys' must be a list of text"));
    CHECK(has(errorOf(with("\"parameters\": {}")), "'parameters' must be a list"));
    CHECK(has(errorOf(with("\"parameters\": [{\"name\": \"a b\", \"apply\": {\"material\": \"m\"}}]")), "'parameters[0].name' must be a name"));
    CHECK(has(errorOf(with("\"parameters\": [{\"name\": \"a\", \"type\": \"slider\", \"apply\": {\"material\": \"m\"}}]")),
              "'parameters[0].type' must be one of number, choice, material, text"));
    CHECK(has(errorOf(with("\"parameters\": [{\"name\": \"a\", \"type\": \"choice\", \"apply\": {\"material\": \"m\"}}]")),
              "'parameters[0].choices' is missing"));
    CHECK(has(errorOf(with("\"parameters\": [{\"name\": \"a\"}]")), "'parameters[0].apply' is missing"));
    CHECK(has(errorOf(with("\"parameters\": [{\"name\": \"a\", \"apply\": {\"selector\": \"x\"}}]")), "needs selector and key"));
    CHECK(has(errorOf(with("\"parameters\": [{\"name\": \"a\", \"apply\": {\"selector\": \"x\", \"key\": \"origin\"}}]")),
              "cannot be origin, angles or classname"));
    CHECK(has(errorOf(with("\"parameters\": [{\"name\": \"a\", \"min\": 5, \"max\": 1, \"apply\": {\"material\": \"m\"}}]")), "is larger than max"));
    CHECK(has(errorOf(with("\"parameters\": [{\"name\": \"a\", \"apply\": {\"material\": \"m\"}}, {\"name\": \"a\", \"apply\": {\"material\": \"n\"}}]")),
              "\"a\" is used twice"));
    CHECK(has(errorOf(with("\"script\": {\"prefix\": \"\"}")), "'script.prefix' must be letters, digits and _"));
    CHECK(has(errorOf(with("\"script\": {\"entry\": \"main\"}")), "'script.entry' \"main\" must be a function name that starts with the prefix \"prefab_\""));
    CHECK(has(errorOf(with("\"script\": {\"once\": \"setup\"}")), "'script.once' \"setup\" must be a function name"));
    CHECK(has(errorOf(with("\"script\": {\"file\": \"/abs.gsc\"}")), "'script.file'"));
    CHECK(has(errorOf(with("\"assets\": {\"materials\": [{\"name\": \"a\"}]}")), "'assets.materials[0].file' is missing"));
    CHECK(has(errorOf(with("\"assets\": {\"models\": [{\"file\": \"models/a.obj\"}]}")), "'assets.models[0].name' is missing"));
    CHECK(has(errorOf(with("\"assets\": {\"effects\": [3]}")), "'assets.effects[0]' must be an object"));
    CHECK(has(errorOf(with("\"assets\": {\"sounds\": [{\"alias\": \"a\", \"file\": \"sounds/a.wav\", \"loop\": 1}]}")),
              "'assets.sounds[0].loop' must be true or false"));
    CHECK(has(errorOf(with("\"assets\": {\"sounds\": [{\"file\": \"sounds/a.wav\"}]}")), "'assets.sounds[0].alias' is missing"));
    CHECK(has(errorOf(with("\"requires\": {\"stock_models\": [\"tm/x\"]}")), "'requires.stock_models[0]' must be an object {name, zones}"));
    CHECK(has(errorOf(with("\"requires\": {\"stock_materials\": [{\"zones\": []}]}")), "'requires.stock_materials[0].name' is missing"));
    CHECK(has(errorOf(with("\"requires\": {\"scriptable_defs\": [{\"name\": \"d\", \"zones\": \"mp_x\"}]}")),
              "'requires.scriptable_defs[0].zones' must be a list of text"));
    CHECK(has(errorOf(with("\"requires\": {\"stock_sounds\": \"a\"}")), "'requires.stock_sounds' must be a list of text"));
    CHECK(has(errorOf(with("\"requires\": {\"stock_effects\": [1]}")), "'requires.stock_effects'"));
    CHECK(has(errorOf(with("\"requires\": {\"stock_strings\": 1}")), "'requires.stock_strings'"));
    CHECK(has(errorOf(with("\"requires\": {\"stock_scripts\": 1}")), "'requires.stock_scripts'"));
    CHECK(has(errorOf(with("\"requires\": {\"build\": \"scriptable_instances\"}")), "'requires.build' must be a list of text"));
    CHECK(has(errorOf(with("\"fallback\": {\"when_missing\": [\"x\"]}")), "'fallback.map' is missing"));
    CHECK(has(errorOf(with("\"fallback\": {\"map\": \"fallback.map\"}")), "'fallback.when_missing' is missing"));
    CHECK(has(errorOf(with("\"fallback\": {\"map\": \"fallback.map\", \"when_missing\": [\"x\"], \"note\": 1}")), "'fallback.note' must be text"));
    CHECK(has(errorOf(with("\"game_modes\": \"koth\"")), "'game_modes' must be a list of text"));

    // Another format: read far enough to list it, and nothing else of it is judged.
    PrefabManifest m;
    std::string err;
    CHECK(parsePrefabManifest("{\"format\": \"iw8r-prefab-9\", \"id\": \"later\", \"title\": \"From the future\", \"tags\": 5}", m, &err));
    CHECK(m.format == "iw8r-prefab-9" && m.id == "later" && m.title == "From the future");

    CHECK(isValidPrefabId("a_1") && !isValidPrefabId("A") && !isValidPrefabId("a-b") && !isValidPrefabId(""));
    CHECK(prefabIdFromName("Jump Pad (v2)") == "jump_pad_v2" && prefabIdFromName("  ") == "prefab" && prefabIdFromName("__x__") == "x");
    CHECK(prefabIdFromName(std::string(60, 'A')).size() == 40);
}

// ------------------------------------------------------------------ folders

TEST(prefab_bundle_load_checks_the_folder) {
    const fs::path dir = scratch("load");
    const std::string empty = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n";
    PrefabBundle b;
    std::string err;

    // No manifest.
    fs::create_directories(dir / "doors" / "none.iw8prefab");
    CHECK(!loadPrefabBundle((dir / "doors" / "none.iw8prefab").string(), b, &err));
    CHECK(has(err, "prefab 'none.iw8prefab': no prefab.json in the folder") && !b.placeable && b.problem == err);

    // A manifest that does not read.
    put(dir / "doors" / "bad.iw8prefab" / "prefab.json", "{\"format\": \"iw8r-prefab-1\", \"id\": \"No\"}");
    CHECK(!loadPrefabBundle((dir / "doors" / "bad.iw8prefab").string(), b, &err));
    CHECK(has(err, "prefab 'bad.iw8prefab': prefab.json: 'id'"));

    // The contents file is missing.
    const fs::path ok = dir / "doors" / "door_a.iw8prefab";
    put(ok / "prefab.json", "{\"format\": \"iw8r-prefab-1\", \"id\": \"door_a\"}");
    CHECK(!loadPrefabBundle(ok.string(), b, &err));
    CHECK(has(err, "contents file 'prefab.map' not found in the bundle"));

    // Complete: defaults from the folder, optional files found by their usual names.
    put(ok / "prefab.map", empty);
    put(ok / "script.gsc", "prefab_main(inst)\n{\n}\n");
    put(ok / "thumb.png", "x");
    fs::create_directories(ok / "materials");
    fs::create_directories(ok / "sounds");
    CHECK(loadPrefabBundle(ok.string(), b, &err));
    CHECK(b.placeable && b.problem.empty() && !b.bare);
    CHECK(b.title == "door_a" && b.category == "doors");
    CHECK(fs::path(b.mapFile).filename() == "prefab.map" && fs::path(b.scriptFile).filename() == "script.gsc");
    CHECK(fs::path(b.thumbFile).filename() == "thumb.png" && b.fallbackMapFile.empty());
    CHECK(!b.materialsDir.empty() && !b.soundsDir.empty() && b.modelsDir.empty() && b.effectsDir.empty());
    CHECK(isPrefabBundleFolder(ok.string()) && !isPrefabBundleFolder((dir / "doors").string()) && !isPrefabBundleFolder((ok / "prefab.map").string()));

    // Named files must exist; a listed asset that is missing is only a warning.
    put(ok / "prefab.json", "{\"format\": \"iw8r-prefab-1\", \"id\": \"door_a\", \"title\": \"Door A\", \"category\": \"portals\", "
                            "\"map\": \"inner/door.map\", \"script\": {\"file\": \"door.gsc\"}}");
    CHECK(!loadPrefabBundle(ok.string(), b, &err) && has(err, "contents file 'inner/door.map' not found"));
    put(ok / "inner" / "door.map", empty);
    CHECK(!loadPrefabBundle(ok.string(), b, &err) && has(err, "script.file 'door.gsc' not found in the bundle"));
    put(ok / "door.gsc", "prefab_main(inst)\n{\n}\n");
    CHECK(loadPrefabBundle(ok.string(), b, &err));
    CHECK(b.title == "Door A" && b.category == "portals" && fs::path(b.mapFile).filename() == "door.map");
    put(ok / "prefab.json", "{\"format\": \"iw8r-prefab-1\", \"id\": \"door_a\", \"thumbnail\": \"pic.png\", "
                            "\"assets\": {\"materials\": [{\"name\": \"m\", \"file\": \"materials/m.iw8mat.json\"}]}, "
                            "\"fallback\": {\"when_missing\": [\"x\"], \"map\": \"fallback.map\", \"script\": \"fallback.gsc\"}}");
    CHECK(!loadPrefabBundle(ok.string(), b, &err) && has(err, "fallback.map 'fallback.map' not found"));
    put(ok / "fallback.map", empty);
    CHECK(!loadPrefabBundle(ok.string(), b, &err) && has(err, "fallback.script 'fallback.gsc' not found"));
    put(ok / "fallback.gsc", "prefab_main(inst)\n{\n}\n");
    CHECK(loadPrefabBundle(ok.string(), b, &err));
    CHECK(b.warnings.size() == 2 && hasLine(b.warnings, "thumbnail 'pic.png' not found") && hasLine(b.warnings, "material 'm': file"));
    CHECK(b.thumbFile.empty() && !b.fallbackMapFile.empty() && !b.fallbackScriptFile.empty());

    // Another format: listed with its title, not placeable.
    const fs::path later = dir / "doors" / "later.iw8prefab";
    put(later / "prefab.json", "{\"format\": \"iw8r-prefab-9\", \"id\": \"later\", \"title\": \"From the future\"}");
    put(later / "prefab.map", empty);
    CHECK(!loadPrefabBundle(later.string(), b, &err));
    CHECK(!b.placeable && b.title == "From the future" && b.manifest.id == "later");
    CHECK(has(err, "format \"iw8r-prefab-9\" is not one this editor reads") && has(err, "listed but cannot be placed"));

    // The list: every bundle under the roots with the text for the model key; nothing inside a bundle is listed.
    put(ok / "inner" / "nested.iw8prefab" / "prefab.json", "{\"format\": \"iw8r-prefab-1\", \"id\": \"nested\"}");
    const auto list = listPrefabBundles({dir.string(), (dir / "missing").string(), dir.string()});
    CHECK(list.size() == 4);
    if (list.size() == 4) {
        CHECK(list[0].model == "doors/bad.iw8prefab" && !list[0].bundle.placeable);
        CHECK(list[1].model == "doors/door_a.iw8prefab" && list[1].bundle.placeable && list[1].root == dir.string());
        CHECK(list[2].model == "doors/later.iw8prefab" && !list[2].bundle.placeable && list[2].bundle.title == "From the future");
        CHECK(list[3].model == "doors/none.iw8prefab" && !list[3].bundle.placeable);
    }
}

TEST(prefab_bundle_reference_resolution) {
    const fs::path dir = scratch("resolve");
    const std::string empty = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n";
    const auto bundle = [&](const fs::path& folder, const std::string& id) {
        put(folder / "prefab.json", "{\"format\": \"iw8r-prefab-1\", \"id\": \"" + id + "\"}");
        put(folder / "prefab.map", empty);
    };
    bundle(dir / "maps" / "next.iw8prefab", "next");
    bundle(dir / "library" / "doors" / "deep" / "door.iw8prefab", "door");
    put(dir / "library" / "doors" / "deep" / "door.iw8prefab" / "other.map", empty);
    put(dir / "maps" / "prefabs" / "crate.map", empty);
    put(dir / "library" / "plain.map", empty);
    const std::string map = (dir / "maps" / "level.map").string();
    const std::vector<std::string> roots{(dir / "library").string()};
    const auto same = [](const std::string& got, const fs::path& want) {
        std::error_code e;
        return !got.empty() && fs::equivalent(got, want, e);
    };

    // A folder next to the map, in a root, and nested under a root.
    PrefabRef r = resolvePrefabReference(map, "next.iw8prefab", roots);
    CHECK(r.kind == PrefabRef::Kind::Bundle && same(r.path, dir / "maps" / "next.iw8prefab") && same(r.mapFile, dir / "maps" / "next.iw8prefab" / "prefab.map"));
    r = resolvePrefabReference(map, "doors/deep/door.iw8prefab", roots);
    CHECK(r.kind == PrefabRef::Kind::Bundle && same(r.path, dir / "library" / "doors" / "deep" / "door.iw8prefab"));
    CHECK(resolvePrefabReference(map, "doors\\deep\\door.iw8prefab/", roots).kind == PrefabRef::Kind::Bundle);
    CHECK(resolvePrefabReference("", (dir / "maps" / "next.iw8prefab").string(), {}).kind == PrefabRef::Kind::Bundle);
    // The contents file of a bundle names the bundle; another .map inside it is a plain prefab.
    r = resolvePrefabReference(map, "doors/deep/door.iw8prefab/prefab.map", roots);
    CHECK(r.kind == PrefabRef::Kind::Bundle && same(r.path, dir / "library" / "doors" / "deep" / "door.iw8prefab"));
    r = resolvePrefabReference(map, "doors/deep/door.iw8prefab/other.map", roots);
    CHECK(r.kind == PrefabRef::Kind::BareMap && same(r.mapFile, dir / "library" / "doors" / "deep" / "door.iw8prefab" / "other.map"));
    // Plain .map prefabs resolve as before.
    r = resolvePrefabReference(map, "prefabs/crate.map", roots);
    CHECK(r.kind == PrefabRef::Kind::BareMap && same(r.path, dir / "maps" / "prefabs" / "crate.map") && r.path == r.mapFile);
    CHECK(same(resolvePrefabPath(map, "prefabs/crate.map", roots), dir / "maps" / "prefabs" / "crate.map"));
    CHECK(same(resolvePrefabPath(map, "plain.map", roots), dir / "library" / "plain.map"));
    CHECK(resolvePrefabReference(map, "gone.iw8prefab", roots).kind == PrefabRef::Kind::Missing);
    CHECK(resolvePrefabReference(map, "", roots).kind == PrefabRef::Kind::Missing);
    CHECK(resolvePrefabPath(map, "gone.map", roots).empty());
    // A folder that only looks like a bundle from the outside is not one.
    fs::create_directories(dir / "maps" / "folder");
    CHECK(resolvePrefabReference(map, "folder", roots).kind == PrefabRef::Kind::Missing);

    // resolvePrefabPath gives the file a default build uses: the fallback when the bundle needs it.
    CHECK(same(resolvePrefabPath(map, "next.iw8prefab", roots), dir / "maps" / "next.iw8prefab" / "prefab.map"));
    put(dir / "maps" / "next.iw8prefab" / "prefab.json",
        "{\"format\": \"iw8r-prefab-1\", \"id\": \"next\", \"fallback\": {\"when_missing\": [\"scriptable_instances\"], \"map\": \"fb.map\"}}");
    put(dir / "maps" / "next.iw8prefab" / "fb.map", empty);
    CHECK(same(resolvePrefabPath(map, "next.iw8prefab", roots), dir / "maps" / "next.iw8prefab" / "fb.map"));
    CHECK(same(prefabBundleContentsFile((dir / "maps" / "next.iw8prefab").string()), dir / "maps" / "next.iw8prefab" / "prefab.map"));

    // The bundles a map places, also through plain prefabs, and their asset folders.
    fs::create_directories(dir / "library" / "doors" / "deep" / "door.iw8prefab" / "materials");
    fs::create_directories(dir / "library" / "doors" / "deep" / "door.iw8prefab" / "models");
    fs::create_directories(dir / "maps" / "next.iw8prefab" / "sounds");
    put(dir / "library" / "plain.map", empty + placement("doors/deep/door.iw8prefab", "0 0 0"));
    const MapFile host = parse(empty + placement("next.iw8prefab", "0 0 0") + placement("plain.map", "64 0 0") + placement("plain.map", "128 0 0"));
    const auto folders = prefabBundleFoldersOf(host, map, roots);
    CHECK(folders.size() == 2);
    if (folders.size() == 2) CHECK(same(folders[0], dir / "maps" / "next.iw8prefab") && same(folders[1], dir / "library" / "doors" / "deep" / "door.iw8prefab"));
    const PrefabAssetRoots ar = prefabBundleAssetRoots(host, map, roots);
    CHECK(ar.materials.size() == 1 && ar.models.size() == 1 && ar.sounds.size() == 1 && ar.effects.empty());
}

// ------------------------------------------------------------------ requires / fallback

TEST(prefab_bundle_requires_and_fallback_choice) {
    const fs::path dir = scratch("choice") / "d.iw8prefab";
    const std::string empty = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n";
    put(dir / "prefab.json",
        "{\"format\": \"iw8r-prefab-1\", \"id\": \"d\", "
        "\"requires\": {\"stock_models\": [{\"name\": \"TM/Known\"}, {\"name\": \"tm/unknown\"}], \"stock_sounds\": [\"known_alias\", \"nope_alias\"], "
        "\"stock_effects\": [\"vfx/whatever\"], \"scriptable_defs\": [{\"name\": \"def_a\"}], "
        "\"build\": [\"scriptable_instances\", \"scriptable_def_carry\", \"stock_script_calls\"]}, "
        "\"fallback\": {\"when_missing\": [\"scriptable_instances\", \"scriptable_def_carry\"], \"map\": \"fallback.map\", "
        "\"script\": \"fallback.gsc\", \"note\": \"built from stock parts\"}}");
    put(dir / "prefab.map", empty);
    put(dir / "script.gsc", "prefab_main(inst)\n{\n}\n");
    put(dir / "fallback.map", empty);
    put(dir / "fallback.gsc", "prefab_main(inst)\n{\n}\n");
    PrefabBundle b;
    CHECK(loadPrefabBundle(dir.string(), b));

    CHECK(defaultPrefabBuildFeatures() == std::set<std::string>{"custom_sounds"});
    // Today's build: both fallback features are missing.
    PrefabBuildSupport today;
    PrefabContentsChoice c = choosePrefabContents(b, today);
    CHECK(c.fallback && fs::path(c.mapFile).filename() == "fallback.map" && fs::path(c.scriptFile).filename() == "fallback.gsc");
    CHECK(c.missingFeatures.size() == 3 && c.missingAssets.empty());
    CHECK(c.reason == "fallback contents built: built from stock parts (missing build feature(s): scriptable_instances, scriptable_def_carry)");
    // One of the two is enough to need the fallback.
    PrefabBuildSupport half;
    half.features = {"scriptable_instances", "stock_script_calls"};
    c = choosePrefabContents(b, half);
    CHECK(c.fallback && has(c.reason, "(missing build feature(s): scriptable_def_carry)"));
    // Both there: main contents; the feature that is still missing has no fallback and is named.
    PrefabBuildSupport most;
    most.features = {"scriptable_instances", "scriptable_def_carry"};
    c = choosePrefabContents(b, most);
    CHECK(!c.fallback && fs::path(c.mapFile).filename() == "prefab.map" && fs::path(c.scriptFile).filename() == "script.gsc");
    CHECK(c.missingFeatures == std::vector<std::string>{"stock_script_calls"});
    CHECK(has(c.reason, "needs the build feature(s) stock_script_calls") && has(c.reason, "has no fallback for them"));
    PrefabBuildSupport all;
    all.features = {"scriptable_instances", "scriptable_def_carry", "stock_script_calls"};
    c = choosePrefabContents(b, all);
    CHECK(!c.fallback && c.missingFeatures.empty() && c.reason.empty());

    // Stock names are checked for the kinds the catalogue knows (case does not matter); other kinds are left alone.
    PrefabStockCatalog cat;
    cat.models["tm/known"] = {"mp_a"};
    cat.sounds["known_alias"] = {};
    all.stock = &cat;
    c = choosePrefabContents(b, all);
    CHECK(c.missingAssets == (std::vector<std::string>{"stock model 'tm/unknown'", "stock sound 'nope_alias'"}));

    // The catalogue files shipped with the editor.
    const fs::path data = repoPath("data/asset_catalog");
    CHECK(!data.empty());
    PrefabStockCatalog shipped;
    CHECK(shipped.load(data.string()) == 4);
    CHECK(shipped.sounds.count("scrpt_door_wood_single_open") && shipped.sounds["scrpt_door_wood_single_open"] == std::vector<std::string>{"common_mp"});
    CHECK(shipped.models.count("tm/door_frame_wooden_panel_03_painted_mp") && shipped.models["tm/door_frame_wooden_panel_03_painted_mp"].size() == 3);
    CHECK(shipped.effects.count("vfx/core/mp/core/vfx_marker_base_cyan_pulse") && shipped.materials.size() > 1000);
    CHECK(!shipped.models.count("# replay 1.20 stock models from x64-zt verifyzone traces of 76 zones"));
    PrefabStockCatalog none;
    CHECK(none.load((data / "missing").string()) == 0 && none.models.empty());
}

// ------------------------------------------------------------------ names

TEST(prefab_bundle_name_scoping_rules) {
    const MapFile kit = parse(doorKitMap());
    // Linked: defined and referred to inside. Inferred local: those, the entities that refer to them, and the
    // entities the generated script looks up (behaviour keys).
    CHECK(prefabLinkedNames(kit) == (std::set<std::string>{"door", "l1", "tp_dest"}));
    CHECK(prefabInferredLocalNames(kit) == (std::set<std::string>{"door", "door_trig", "l1", "tp", "tp_dest"}));
    PrefabManifest m;
    CHECK(prefabLocalNames(kit, m) == prefabInferredLocalNames(kit));
    m.namesGlobal = {"tp_dest"};
    CHECK(!prefabLocalNames(kit, m).count("tp_dest") && prefabLocalNames(kit, m).count("door"));
    m.hasLocalNames = true;
    m.namesLocal = {"hqloc", "tp_dest"};
    CHECK(prefabLocalNames(kit, m) == std::set<std::string>{"hqloc"});  // the list decides; global still wins
    // An extra key from the manifest takes part in the inference.
    PrefabManifest extra;
    extra.nameKeys = {"my_link"};
    const MapFile linked = parse("iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"script_origin\"\n\"targetname\" \"a\"\n}\n"
                                 "{\n\"classname\" \"script_origin\"\n\"my_link\" \"a\"\n}\n");
    CHECK(prefabLocalNames(linked, PrefabManifest{}).empty() && prefabLocalNames(linked, extra) == std::set<std::string>{"a"});

    const PrefabNameScope scope = prefabNameScope(kit, PrefabManifest{}, "p7");
    CHECK(scope.pid == "p7" && contains(scope.keys, "targetname") && contains(scope.keys, "iw8r_teleport") && contains(scope.keys, "target8"));
    std::vector<std::pair<std::string, std::string>> keys = {{"classname", "door"},         {"targetname", "door"},      {"target", "flag_primary"},
                                                             {"iw8r_teleport", "tp_dest"},  {"script_linkto", "l1  outside_link"},
                                                             {"script_noteworthy", "door"}, {"target2", "door_trig"}};
    CHECK(scopePrefabNames(keys, scope) == 4);
    CHECK(keys[0].second == "door");                   // not a name key
    CHECK(keys[1].second == "p7_door");
    CHECK(keys[2].second == "flag_primary");           // a name outside the prefab stays
    CHECK(keys[3].second == "p7_tp_dest");
    CHECK(keys[4].second == "p7_l1  outside_link");    // each name of a list on its own; the spacing stays
    CHECK(keys[5].second == "door" && keys[6].second == "p7_door_trig");
    PrefabNameScope off = scope;
    off.pid.clear();
    CHECK(scopePrefabNames(keys, off) == 0 && keys[1].second == "p7_door");
    CHECK(scopedPrefabName("p7", "door") == "p7_door");

    CHECK(isValidPrefabPlacementId("p7") && isValidPrefabPlacementId("A_1") && !isValidPrefabPlacementId("") && !isValidPrefabPlacementId("p 7") &&
          !isValidPrefabPlacementId(std::string(25, 'p')));
    const MapFile host = parse("iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n" + placement("a.map", "0 0 0", "\"iw8r_pid\" \"p1\"\n") +
                               placement("a.map", "0 0 0", "\"iw8r_pid\" \"p3\"\n") + placement("a.map", "0 0 0") +
                               "{\n\"classname\" \"script_origin\"\n\"iw8r_pid\" \"p2\"\n}\n");
    CHECK(nextPrefabPlacementId(host) == "p2");  // only misc_prefab entities count
    CHECK(prefabInstance(host.entities[1])->pid == "p1" && prefabInstance(host.entities[3])->pid.empty());
    CHECK(prefabInstance(parse("iwmap 4\n" + placement("a.map", "0 0 0", "\"iw8r_pid\" \"p 1\"\n")).entities[0])->pid.empty());
}

TEST(prefab_bundle_two_placements_never_share_a_local_name) {
    const fs::path dir = scratch("scope");
    put(dir / "kit.iw8prefab" / "prefab.json", "{\"format\": \"iw8r-prefab-1\", \"id\": \"door_kit\"}");
    put(dir / "kit.iw8prefab" / "prefab.map", doorKitMap());
    const std::string host = hostText(placement("kit.iw8prefab", "-200 0 0", "\"iw8r_pid\" \"p1\"\n") +
                                      placement("kit.iw8prefab", "200 0 0", "\"iw8r_pid\" \"p2\"\n\"angles\" \"0 90 0\"\n"
                                                                            "\"iw8r_ovr:door:iw8r_door\" \"hinge 120 1\"\n"
                                                                            "\"iw8r_ovr:tp:target\" \"door\"\n") +
                                      "{\n\"classname\" \"script_origin\"\n\"targetname\" \"flag_primary\"\n\"origin\" \"0 0 8\"\n}\n");
    std::vector<std::string> warn;
    PrefabFlattenInfo info;
    const std::string flat = flatten(dir, host, warn, &info);
    const MapFile fm = parse(flat);
    CHECK(info.bundlePlacements == 2 && info.bundles.size() == 1 && info.bundles[0].id == "door_kit" && info.bundles[0].placements == 2);
    CHECK(!info.bundles[0].fallback && !info.bundles[0].script && info.scripts.empty());

    // Each placement has its own door, trigger, teleporter and destination.
    const std::vector<std::string> names = valuesOf(flat, "targetname");
    for (const char* n : {"p1_door", "p2_door", "p1_door_trig", "p2_door_trig", "p1_tp", "p2_tp", "p1_tp_dest", "p2_tp_dest"}) {
        CHECK(std::count(names.begin(), names.end(), std::string(n)) == 1);
    }
    CHECK(!contains(names, "door") && !contains(names, "door_trig") && !contains(names, "tp_dest"));
    // A name nothing inside refers to keeps its name (a stock script may look for it), and so does the host's.
    CHECK(std::count(names.begin(), names.end(), std::string("hqloc")) == 2);
    CHECK(std::count(names.begin(), names.end(), std::string("flag_primary")) == 1);
    // Links stay inside their placement; the link to the global name still reaches it.
    size_t doorLinks = 0, globalLinks = 0;
    for (const Entity& e : fm.entities) {
        const std::string* tn = e.value("targetname");
        const std::string* tg = e.value("target");
        if (tn && (*tn == "p1_door_trig" || *tn == "p2_door_trig")) {
            CHECK(tg && *tg == tn->substr(0, 2) + "_door");
            ++doorLinks;
        }
        if (tn && *tn == "p1_tp") {
            CHECK(tg && *tg == "flag_primary" && *e.value("iw8r_teleport") == "p1_tp_dest");
            ++globalLinks;
        }
        // The override selector names the entity as the prefab does; a name it writes is scoped like any other.
        if (tn && *tn == "p2_tp") CHECK(tg && *tg == "p2_door" && *e.value("iw8r_teleport") == "p2_tp_dest");
        if (tn && *tn == "p2_door") CHECK(*e.value("iw8r_door") == "hinge 120 1");
        if (tn && *tn == "p1_door") CHECK(*e.value("iw8r_door") == "hinge 90 0.8");
        if (const std::string* lt = e.value("script_linkto")) CHECK(*lt == "p1_l1 outside_link" || *lt == "p2_l1 outside_link");
    }
    CHECK(doorLinks == 2 && globalLinks == 1);
    CHECK(!hasLine(warn, "iw8r_pid") && hasLine(warn, "prefab bundle 'door_kit' (kit.iw8prefab): 2 placement(s), main contents"));

    // Flattening is stable: the same text again, and a flattened map flattens to itself.
    std::vector<std::string> warn2;
    CHECK(flatten(dir, host, warn2) == flat && warn2 == warn);
    std::vector<std::string> warn3;
    CHECK(flatten(dir, flat, warn3) == fm.text() && warn3.empty());

    // names.global keeps a name; names.local lists exactly what is renamed.
    put(dir / "kit.iw8prefab" / "prefab.json",
        "{\"format\": \"iw8r-prefab-1\", \"id\": \"door_kit\", \"names\": {\"local\": [\"door\"], \"global\": [\"hqloc\"]}}");
    warn.clear();
    const std::vector<std::string> listed = valuesOf(flatten(dir, host, warn), "targetname");
    CHECK(contains(listed, "p1_door") && contains(listed, "p2_door") && std::count(listed.begin(), listed.end(), std::string("hqloc")) == 2);
    CHECK(std::count(listed.begin(), listed.end(), std::string("door_trig")) == 2 && !contains(listed, "p1_door_trig"));

    // A placement without an id, and one that repeats an id, still get their own names; the build says so.
    put(dir / "kit.iw8prefab" / "prefab.json", "{\"format\": \"iw8r-prefab-1\", \"id\": \"door_kit\"}");
    const std::string loose = hostText(placement("kit.iw8prefab", "-200 0 0", "\"iw8r_pid\" \"a1\"\n") + placement("kit.iw8prefab", "0 0 0") +
                                       placement("kit.iw8prefab", "200 0 0", "\"iw8r_pid\" \"a1\"\n"));
    warn.clear();
    const std::vector<std::string> auto3 = valuesOf(flatten(dir, loose, warn), "targetname");
    CHECK(contains(auto3, "a1_door") && contains(auto3, "a2_door") && contains(auto3, "a3_door"));
    CHECK(hasLine(warn, "prefab 'kit.iw8prefab' at 0 0 0 has no iw8r_pid; its names are built with 'a2'"));
    CHECK(hasLine(warn, "prefab 'kit.iw8prefab' at 200 0 0 repeats the iw8r_pid 'a1'; its names are built with 'a3'"));

    // Explode with the scope gives the document the names the build uses.
    Document doc;
    CHECK(doc.loadFromText(hostText(placement("kit.iw8prefab", "64 0 0", "\"iw8r_pid\" \"p5\"\n")), (dir / "host.map").string(), nullptr));
    Id prefabId = kNoId;
    for (const Entity& e : doc.map().entities)
        if (e.classname() == "misc_prefab") prefabId = e.id;
    const MapFile contents = parse(doorKitMap());
    const PrefabNameScope scope = prefabNameScope(contents, PrefabManifest{}, "p5");
    std::string err;
    CHECK(explodePrefab(doc, prefabId, contents, &err, &scope));
    const std::vector<std::string> exploded = valuesOf(doc.map().text(), "targetname");
    CHECK(contains(exploded, "p5_door") && contains(exploded, "p5_door_trig") && contains(exploded, "hqloc") && !contains(exploded, "door"));
}

TEST(prefab_bundle_nested_names_chain) {
    const fs::path dir = scratch("nested");
    // inner: a door kit. room: a bundle with two inner placements and a name of its own. hall: a plain .map with a room.
    put(dir / "inner.iw8prefab" / "prefab.json", "{\"format\": \"iw8r-prefab-1\", \"id\": \"inner_kit\"}");
    put(dir / "inner.iw8prefab" / "prefab.map", doorKitMap());
    put(dir / "inner.iw8prefab" / "script.gsc", "prefab_main(inst)\n{\n    wait 0.05;\n}\n");
    put(dir / "room.iw8prefab" / "prefab.json", "{\"format\": \"iw8r-prefab-1\", \"id\": \"room\", \"names\": {\"local\": [\"lamp\"]}}");
    put(dir / "room.iw8prefab" / "prefab.map",
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"script_origin\"\n\"targetname\" \"lamp\"\n\"origin\" \"0 0 64\"\n}\n" +
            placement("../inner.iw8prefab", "100 0 0", "\"iw8r_pid\" \"p2\"\n") +
            placement("../inner.iw8prefab", "0 100 0", "\"iw8r_pid\" \"p3\"\n\"angles\" \"0 90 0\"\n"));
    put(dir / "hall.map", "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n" + placement("room.iw8prefab", "0 0 0", "\"iw8r_pid\" \"r1\"\n"));
    const std::string host = hostText(placement("room.iw8prefab", "1000 0 0", "\"iw8r_pid\" \"p7\"\n\"angles\" \"0 90 0\"\n") +
                                      placement("room.iw8prefab", "-1000 0 0", "\"iw8r_pid\" \"p8\"\n") + placement("hall.map", "0 2000 0") +
                                      placement("hall.map", "0 -2000 0", "\"iw8r_pid\" \"h9\"\n"));
    std::vector<std::string> warn;
    PrefabFlattenInfo info;
    const std::string flat = flatten(dir, host, warn, &info);
    const std::vector<std::string> names = valuesOf(flat, "targetname");
    // room in the map: p7_p2_door; room inside a plain prefab: the plain prefab's id comes first.
    for (const char* n : {"p7_lamp", "p8_lamp", "p7_p2_door", "p7_p3_door", "p8_p2_door", "p8_p3_door", "p7_p2_door_trig", "h9_r1_p2_door",
                          "h9_r1_lamp", "a1_r1_p3_door", "a1_r1_lamp"}) {
        CHECK(std::count(names.begin(), names.end(), std::string(n)) == 1);
    }
    // Every door name exists once: 4 rooms with 2 inner kits each.
    size_t doors = 0;
    for (const std::string& n : names) doors += n.size() > 5 && n.compare(n.size() - 5, 5, "_door") == 0;
    CHECK(doors == 8);
    std::vector<std::string> sorted = names;
    std::sort(sorted.begin(), sorted.end());
    for (size_t i = 1; i < sorted.size(); ++i)
        if (sorted[i] == sorted[i - 1]) CHECK(sorted[i] == "hqloc");
    for (const Entity& e : parse(flat).entities)
        if (const std::string* tn = e.value("targetname"); tn && *tn == "p7_p3_door_trig") CHECK(*e.value("target") == "p7_p3_door");
    // The plain prefab without an id got one because a bundle inside it needs it.
    CHECK(hasLine(warn, "prefab 'hall.map' at 0 2000 0 has no iw8r_pid; its names are built with 'a1'"));
    // Scripts: one entry for the inner kind, one start per placement with the chained id and the place in the map.
    CHECK(info.bundlePlacements == 12 && info.bundles.size() == 2 && info.scripts.size() == 1);
    if (info.scripts.size() == 1) {
        const auto& ps = info.scripts[0];
        CHECK(ps.id == "inner_kit" && ps.source == "inner.iw8prefab" && ps.placements.size() == 8);
        bool found = false;
        for (const auto& pl : ps.placements)
            if (pl.pid == "p7_p3") {
                // room at (1000 0 0) turned 90 degrees: the inner origin (0 100 0) lands at (900 0 0), yaw 90 + 90.
                found = true;
                CHECK(length(pl.origin - Vec3{900, 0, 0}) < 1e-6 && length(pl.angles - Vec3{0, 180, 0}) < 1e-6);
            }
        CHECK(found);
        for (const auto& pl : ps.placements)
            if (pl.pid == "p8_p2") CHECK(length(pl.origin - Vec3{-900, 0, 0}) < 1e-6 && length(pl.angles) < 1e-6);
    }
}

// ------------------------------------------------------------------ scripts

TEST(prefab_bundle_script_prefix_and_starts) {
    const std::string snippet =
        "// prefab_main starts here, prefab_ stays in comments\n"
        "prefab_main(inst)\n{\n"
        "    level.prefab_count = 1; /* prefab_block */\n"
        "    self thread prefab_helper(\"prefab_ in a string \\\" prefab_x\", ::prefab_helper);\n"
        "    myprefab_value = 2prefab_;\n"
        "    prefab_ = 3;\n"
        "}\n\nprefab_helper(a, b)\n{\n}\n";
    const std::string renamed = prefabScriptRenamed(snippet, "prefab_", "jump_pad");
    CHECK(has(renamed, "// prefab_main starts here, prefab_ stays in comments\n"));
    CHECK(has(renamed, "\niw8rp_jump_pad_main(inst)\n"));
    CHECK(has(renamed, "level.iw8rp_jump_pad_count = 1; /* prefab_block */"));
    CHECK(has(renamed, "self thread iw8rp_jump_pad_helper(\"prefab_ in a string \\\" prefab_x\", ::iw8rp_jump_pad_helper);"));
    CHECK(has(renamed, "myprefab_value = 2prefab_;"));  // only whole identifiers that start with the prefix
    CHECK(has(renamed, "    prefab_ = 3;"));            // the bare prefix is not a name
    CHECK(count(renamed, "iw8rp_jump_pad_") == 5);
    CHECK(prefabScriptRenamed(snippet, "", "x") == snippet);
    CHECK(prefabScriptRenamed("pf_a(); prefab_b();", "pf_", "k") == "iw8rp_k_a(); prefab_b();");
    CHECK(prefabScriptFunctionName("jump_pad", "prefab_", "prefab_main") == "iw8rp_jump_pad_main");
    CHECK(prefabScriptFunctionName("jump_pad", "prefab_", "other") == "other");

    MapScriptExtras ex;
    MapScriptExtras::PrefabScript ps;
    ps.id = "jump_pad";
    ps.source = "jump_pad.iw8prefab";
    ps.text = snippet;
    ps.once = "prefab_helper";
    ps.placements = {{"p7", Vec3{83.5, 323.5, 69}, Vec3{0, 180, 0}}, {"p8_p2", Vec3{1, 2, 3}, Vec3{0, 0, 0}}, {"bad id", Vec3{}, Vec3{}}};
    ex.prefabScripts.push_back(ps);
    MapScriptExtras::PrefabScript other;
    other.id = "door_kit";
    other.text = "prefab_main(inst)\n{\n}\n";
    other.placements = {{"p1", Vec3{}, Vec3{}}};
    ex.prefabScripts.push_back(other);
    const std::string g = mapScriptGsc(std::nullopt, {}, {}, ex);
    // One start line per placement, in the form of the design, after the once function.
    CHECK(has(g, "    level thread iw8rp_jump_pad_helper();\n    level thread iw8rp_jump_pad_main(iw8r_prefab_inst(\"p7\", (83.5, 323.5, 69), (0, 180, 0)));\n"
                 "    level thread iw8rp_jump_pad_main(iw8r_prefab_inst(\"p8_p2\", (1, 2, 3), (0, 0, 0)));\n"
                 "    level thread iw8rp_door_kit_main(iw8r_prefab_inst(\"p1\", (0, 0, 0), (0, 0, 0)));\n}\n"));
    CHECK(!has(g, "bad id"));
    // The snippet is there once per kind, with the helpers and a trigger lookup for it.
    CHECK(count(g, "\niw8rp_jump_pad_main(inst)\n") == 1 && count(g, "\niw8rp_door_kit_main(inst)\n") == 1);
    CHECK(count(g, "\niw8r_prefab_inst(pid, origin, angles)\n") == 1 && count(g, "\niw8r_pname(inst, name)\n") == 1);
    CHECK(has(g, "return inst.pid + \"_\" + name;") && has(g, "\niw8r_trigger(name)\n") && has(g, "level.iw8r_trig = [];"));
    CHECK(has(g, "// ---- prefab script jump_pad (jump_pad.iw8prefab): prefab_* is iw8rp_jump_pad_* here ----\n"));
    const auto names = scriptFunctionNames(g);
    for (const char* n : {"main", "iw8r_prefab_inst", "iw8r_pname", "iw8rp_jump_pad_main", "iw8rp_jump_pad_helper", "iw8rp_door_kit_main"}) {
        CHECK(std::count(names.begin(), names.end(), std::string(n)) == 1);
    }
    CHECK(std::count(g.begin(), g.end(), '{') == std::count(g.begin(), g.end(), '}'));
    CHECK(std::count(g.begin(), g.end(), '(') == std::count(g.begin(), g.end(), ')'));
    CHECK(prefabScriptsError(ex.prefabScripts).empty());

    // A map without a prefab script gets the script it always got: entries that cannot be used change nothing.
    MapScriptExtras plain;
    plain.doors.push_back({"door1", "door1_trig"});
    plain.jumpPads.push_back({"pad", {0, 0, 600}});
    const std::string before = mapScriptGsc(std::nullopt, {}, {}, plain);
    CHECK(!has(before, "iw8r_prefab_inst") && !has(before, "iw8rp_") && !has(before, "prefab"));
    MapScriptExtras noise = plain;
    MapScriptExtras::PrefabScript bad;
    bad.id = "Bad Id";
    bad.text = "prefab_main(inst)\n{\n}\n";
    noise.prefabScripts.push_back(bad);
    bad.id = "empty";
    bad.text.clear();
    noise.prefabScripts.push_back(bad);
    CHECK(mapScriptGsc(std::nullopt, {}, {}, noise) == before);
    CHECK(has(prefabScriptsError(noise.prefabScripts), "prefab script id 'Bad Id'"));

    // Two bundles with one id: an error that names both; the generator keeps the first.
    MapScriptExtras twice = ex;
    MapScriptExtras::PrefabScript dup = other;
    dup.id = "jump_pad";
    dup.source = "copy_of_jump_pad.iw8prefab";
    twice.prefabScripts.push_back(dup);
    const std::string derr = prefabScriptsError(twice.prefabScripts);
    CHECK(has(derr, "two prefab bundles use the id 'jump_pad': jump_pad.iw8prefab and copy_of_jump_pad.iw8prefab"));
    CHECK(mapScriptGsc(std::nullopt, {}, {}, twice) == g);

    // The author's check before saving.
    PrefabManifest m;
    CHECK(checkPrefabScript("prefab_main(inst)\n{\n}\nprefab_other()\n{\n}\n", m).empty());
    auto problems = checkPrefabScript("#include maps\\mp\\_utility;\nstart(inst)\n{\n}\nprefab_a()\n{\n}\nprefab_a()\n{\n}\n", m);
    CHECK(hasLine(problems, "no prefab_main() function") && hasLine(problems, "function start() does not start with prefab_"));
    CHECK(hasLine(problems, "function prefab_a() is defined twice") && hasLine(problems, "line 1: #include and #using cannot be used"));
    m.scriptOnce = "prefab_setup";
    CHECK(hasLine(checkPrefabScript("prefab_main(inst)\n{\n}\n", m), "no prefab_setup() function (script.once)"));
}

// ------------------------------------------------------------------ the build

TEST(prefab_bundle_compile_expands_material_model_and_script) {
    const fs::path dir = scratch("compile_src");
    const fs::path kit = dir / "prefabs" / "kit.iw8prefab";
    put(kit / "prefab.json", "{\"format\": \"iw8r-prefab-1\", \"id\": \"test_kit\", \"names\": {\"local\": [\"kit_trig\"]}}");
    put(kit / "prefab.map", "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + box({{-32, -32, 0}, {32, 32, 8}}, "bundle/pad_metal") + "}\n" +
                                "{\n\"classname\" \"trigger_multiple\"\n\"targetname\" \"kit_trig\"\n\"iw8r_jump\" \"0 0 700\"\n" +
                                box({{-32, -32, 8}, {32, 32, 32}}, "trigger") + "}\n" +
                                "{\n\"classname\" \"misc_model\"\n\"model\" \"kit_wedge\"\n\"origin\" \"0 0 8\"\n}\n");
    put(kit / "script.gsc", "// the kit script; prefab_ is the prefix\nprefab_main(inst)\n{\n    wait 0.05;\n"
                            "    trig = iw8r_trigger(iw8r_pname(inst, \"kit_trig\"));\n    level.prefab_count = 1;\n    prefab_helper(\"prefab_ text\");\n}\n\n"
                            "prefab_helper(msg)\n{\n}\n");
    put(kit / "materials" / "pad_metal.tga", tgaBytes(8, 8, 90, 100, 110));
    put(kit / "materials" / "pad_metal.iw8mat.json", "{\"name\": \"bundle/pad_metal\", \"color\": \"pad_metal.tga\"}");
    put(kit / "models" / "kit_wedge.obj", "v 0 0 0\nv 16 0 0\nv 0 16 0\nv 0 0 16\nf 1 2 3\nf 1 2 4\nf 1 3 4\nf 2 3 4\n");
    const std::string host = hostText(placement("prefabs/kit.iw8prefab", "0 0 0", "\"iw8r_pid\" \"p1\"\n") +
                                      placement("prefabs/kit.iw8prefab", "300 0 0", "\"iw8r_pid\" \"p2\"\n\"angles\" \"0 90 0\"\n"));
    const fs::path out = scratch("compile_out");
    CompileReport rep;
    CHECK(compileMap(host, out, CompileOptions{}, dir.string(), rep));
    // The bundle's own material and model were found in its folders.
    CHECK(fs::exists(out / "extracted" / "materials" / "bundle" / "pad_metal.json"));
    CHECK(fs::exists(out / "extracted" / "xmodel" / "kit_wedge.json"));
    CHECK(rep.customModels == 1);
    // The script: included once with its own names, started once per placement at the placement.
    const std::string gsc = slurp(mapsDir(out) / "mp_bundle_t.gsc");
    CHECK(count(gsc, "\niw8rp_test_kit_main(inst)\n") == 1 && count(gsc, "\niw8rp_test_kit_helper(msg)\n") == 1);
    CHECK(has(gsc, "    level thread iw8rp_test_kit_main(iw8r_prefab_inst(\"p1\", (0, 0, 0), (0, 0, 0)));\n"));
    CHECK(has(gsc, "    level thread iw8rp_test_kit_main(iw8r_prefab_inst(\"p2\", (300, 0, 0), (0, 90, 0)));\n"));
    CHECK(has(gsc, "// the kit script; prefab_ is the prefix\n") && has(gsc, "iw8rp_test_kit_helper(\"prefab_ text\");") &&
          has(gsc, "level.iw8rp_test_kit_count = 1;"));
    CHECK(!has(gsc, "\nprefab_main(") && std::count(gsc.begin(), gsc.end(), '{') == std::count(gsc.begin(), gsc.end(), '}'));
    // The no-code behaviour inside the prefab works per placement: each jump pad waits on its own trigger.
    CHECK(has(gsc, "iw8r_trigger(\"p1_kit_trig\")") && has(gsc, "iw8r_trigger(\"p2_kit_trig\")") && !has(gsc, "(\"kit_trig\")"));
    CHECK(has(gsc, "iw8r_trigger_add(\"p1_kit_trig\"") && has(gsc, "iw8r_trigger_add(\"p2_kit_trig\""));
    // The log says what was done.
    CHECK(hasLine(rep.warnings, "2 prefab(s) expanded into the build"));
    CHECK(hasLine(rep.warnings, "prefab bundle 'test_kit' (kit.iw8prefab): 2 placement(s), main contents; its script is included once and "
                                "started 2 time(s); its own asset folders are used: materials, models"));
    CHECK(!hasLine(rep.warnings, "build grey"));
    for (const std::string& w : rep.warnings)
        if (has(w, "compiled to 'iw8r/grey'")) CHECK(!has(w, "bundle/pad_metal"));  // it is not one of the grey ones

    // The same map with the mapper's own material library: its folders must be known to merge the bundle's in.
    const fs::path mine = dir / "my_materials";
    put(mine / "wall.tga", tgaBytes(8, 8, 200, 180, 160));
    put(mine / "wall.iw8mat.json", "{\"name\": \"mine/wall\", \"color\": \"wall.tga\"}");
    MaterialLibrary lib;
    std::string lerr;
    CHECK(lib.load({mine.string()}, &lerr));
    CompileOptions withLib;
    withLib.library = &lib;
    CompileReport rep2;
    const fs::path out2 = scratch("compile_out_lib");
    CHECK(compileMap(host, out2, withLib, dir.string(), rep2));
    CHECK(hasLine(rep2.warnings, "prefab bundle materials are not in the material library this build was given"));
    CHECK(!fs::exists(out2 / "extracted" / "materials" / "bundle" / "pad_metal.json"));
    withLib.materialRoots = {mine.string()};
    CompileReport rep3;
    const fs::path out3 = scratch("compile_out_lib_roots");
    CHECK(compileMap(host, out3, withLib, dir.string(), rep3));
    CHECK(fs::exists(out3 / "extracted" / "materials" / "bundle" / "pad_metal.json") && !hasLine(rep3.warnings, "build grey"));
    // A library that already holds the bundle's materials (the caller added the folders) is used as it is.
    MaterialLibrary both;
    CHECK(both.load({mine.string(), (kit / "materials").string()}, &lerr));
    CompileOptions withBoth;
    withBoth.library = &both;
    CompileReport rep4;
    const fs::path out4 = scratch("compile_out_both");
    CHECK(compileMap(host, out4, withBoth, dir.string(), rep4));
    CHECK(fs::exists(out4 / "extracted" / "materials" / "bundle" / "pad_metal.json") && !hasLine(rep4.warnings, "build grey"));
    CHECK(folderBytes(out4) == folderBytes(out3));
}

TEST(prefab_bundle_compile_requires_fallback_and_duplicate_id) {
    const fs::path dir = scratch("fallback_src");
    const fs::path door = dir / "door.iw8prefab";
    put(door / "prefab.json",
        "{\"format\": \"iw8r-prefab-1\", \"id\": \"native_door\", \"names\": {\"local\": [\"fb_marker\", \"main_marker\"]}, "
        "\"requires\": {\"stock_sounds\": [\"known_alias\", \"nope_alias\"], \"scriptable_defs\": [{\"name\": \"some_def\"}], "
        "\"build\": [\"scriptable_instances\", \"stock_script_calls\"]}, "
        "\"fallback\": {\"when_missing\": [\"scriptable_instances\"], \"map\": \"fallback.map\", \"script\": \"fallback.gsc\", "
        "\"note\": \"built as a scripted door\"}}");
    put(door / "prefab.map", "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n"
                             "{\n\"classname\" \"scriptable\"\n\"scriptable\" \"some_def\"\n\"origin\" \"0 0 0\"\n}\n"
                             "{\n\"classname\" \"script_origin\"\n\"targetname\" \"main_marker\"\n\"origin\" \"0 0 8\"\n}\n");
    put(door / "script.gsc", "prefab_main(inst)\n{\n    main_only = 1;\n}\n");
    put(door / "fallback.map", "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n"
                               "{\n\"classname\" \"script_origin\"\n\"targetname\" \"fb_marker\"\n\"origin\" \"0 0 8\"\n}\n");
    put(door / "fallback.gsc", "prefab_main(inst)\n{\n    fallback_only = 1;\n}\n");
    const std::string host = hostText(placement("door.iw8prefab", "0 0 0", "\"iw8r_pid\" \"p1\"\n"));
    PrefabStockCatalog cat;
    cat.sounds["known_alias"] = {};

    // Today's build: the fallback, said in the log with the note and the missing feature; the missing stock name is
    // named with the prefab.
    CompileOptions opt;
    opt.prefabSupport.stock = &cat;
    CompileReport rep;
    const fs::path out = scratch("fallback_out");
    CHECK(compileMap(host, out, opt, dir.string(), rep));
    CHECK(hasLine(rep.warnings, "prefab bundle 'native_door' (door.iw8prefab): 1 placement(s), fallback contents; its script is included once"));
    CHECK(hasLine(rep.warnings, "prefab bundle 'native_door' (door.iw8prefab): fallback contents built: built as a scripted door "
                                "(missing build feature(s): scriptable_instances)"));
    CHECK(hasLine(rep.warnings, "prefab bundle 'native_door' (door.iw8prefab) needs the stock sound 'nope_alias', which is not in the stock catalogue"));
    CHECK(!hasLine(rep.warnings, "known_alias") && !hasLine(rep.warnings, "scriptable object(s) left out"));
    const std::string ents = slurp(mapsDir(out) / "mp_bundle_t.d3dbsp.ents");
    const std::string gsc = slurp(mapsDir(out) / "mp_bundle_t.gsc");
    CHECK(has(ents, "p1_fb_marker") && !has(ents, "main_marker") && has(gsc, "fallback_only = 1;") && !has(gsc, "main_only"));

    // A build with the feature: the main contents and the main script.
    opt.prefabSupport.features.insert("scriptable_instances");
    CompileReport rep2;
    const fs::path out2 = scratch("fallback_out_main");
    CHECK(compileMap(host, out2, opt, dir.string(), rep2));
    CHECK(hasLine(rep2.warnings, "1 placement(s), main contents") && !hasLine(rep2.warnings, "fallback contents built"));
    CHECK(hasLine(rep2.warnings, "needs the build feature(s) stock_script_calls, which this build does not have, and has no fallback for them"));
    const std::string ents2 = slurp(mapsDir(out2) / "mp_bundle_t.d3dbsp.ents");
    CHECK(has(ents2, "p1_main_marker") && !has(ents2, "fb_marker") && has(slurp(mapsDir(out2) / "mp_bundle_t.gsc"), "main_only = 1;"));

    // No fallback and no scriptable support: the scriptable is left out and counted, the rest is built.
    put(door / "prefab.json", "{\"format\": \"iw8r-prefab-1\", \"id\": \"native_door\", \"requires\": {\"build\": [\"scriptable_instances\"]}}");
    CompileReport rep3;
    const fs::path out3 = scratch("fallback_out_none");
    CHECK(compileMap(host, out3, CompileOptions{}, dir.string(), rep3));
    CHECK(hasLine(rep3.warnings, "prefab bundle 'native_door' (door.iw8prefab): 1 scriptable object(s) left out"));
    CHECK(hasLine(rep3.warnings, "needs the build feature(s) scriptable_instances"));
    CHECK(has(slurp(mapsDir(out3) / "mp_bundle_t.d3dbsp.ents"), "main_marker"));

    // A bundle that cannot be read is left out with its reason; the map still builds.
    put(dir / "broken.iw8prefab" / "prefab.json", "{\"format\": \"iw8r-prefab-1\", \"id\": \"broken\", \"tags\": 3}");
    CompileReport rep4;
    CHECK(compileMap(hostText(placement("broken.iw8prefab", "0 0 0")), scratch("fallback_out_broken"), CompileOptions{}, dir.string(), rep4));
    CHECK(hasLine(rep4.warnings, "prefab 'broken.iw8prefab': prefab.json: 'tags' must be a list of text; left out of the build"));

    // Two folders with one id stop the build, and the message names both.
    for (const char* name : {"one.iw8prefab", "two.iw8prefab"}) {
        put(dir / name / "prefab.json", "{\"format\": \"iw8r-prefab-1\", \"id\": \"same_id\"}");
        put(dir / name / "prefab.map", "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n");
    }
    CompileReport rep5;
    std::string err;
    CHECK(!compileMap(hostText(placement("one.iw8prefab", "0 0 0") + placement("two.iw8prefab", "64 0 0")), scratch("fallback_out_dup"),
                      CompileOptions{}, dir.string(), rep5, &err));
    CHECK(has(err, "two prefab bundles use the id 'same_id'") && has(err, "one.iw8prefab") && has(err, "two.iw8prefab"));
}

// Maps without a bundle build exactly as before the bundle work. The digest below is the flattened text of this
// fixture made by the code before prefab bundles existed (FNV-1a 64); it changes only when the expansion of plain
// .map prefabs changes on purpose (then print the new value and replace it).
TEST(prefab_bundle_plain_prefabs_expand_and_build_byte_for_byte) {
    const fs::path src = scratch("bare_src");
    const std::string host = writeBareFixture(src);
    std::vector<std::string> warn;
    PrefabFlattenInfo info;
    const std::string flat = flatten(src, host, warn, &info);
    CHECK(fnv64(flat) == 0x7de012bbbfb96bf7ull);
    if (fnv64(flat) != 0x7de012bbbfb96bf7ull) std::printf("  flattened fixture digest is now %016llx\n", static_cast<unsigned long long>(fnv64(flat)));
    CHECK(flat.size() == 6570 && info.bundlePlacements == 0 && info.bundles.empty() && info.scripts.empty());
    // No name was touched, and the old entry point gives the same text.
    const std::vector<std::string> names = valuesOf(flat, "targetname");
    CHECK(std::count(names.begin(), names.end(), std::string("door1")) == 2 && std::count(names.begin(), names.end(), std::string("tp_out")) == 2);
    MapFile old;
    std::vector<std::string> oldWarn;
    CHECK(flattenPrefabs(parse(host), (src / "host.map").string(), {}, old, &oldWarn, nullptr) && old.text() == flat);
    // New: the build says that the two placements share linked names.
    CHECK(warn.size() == 1 && hasLine(warn, "prefab 'doorset.map' is placed 2 times and its placements share the linked name(s) 'door1', 'tp_out'"));

    // The build of the map equals the build of its flattened text (which never enters the prefab step), file by
    // file and byte by byte, and bundle options change nothing for it.
    CompileReport repA, repB, repC;
    const fs::path outA = scratch("bare_out_a"), outB = scratch("bare_out_b"), outC = scratch("bare_out_c");
    CHECK(compileMap(host, outA, CompileOptions{}, src.string(), repA));
    CHECK(compileMap(flat, outB, CompileOptions{}, src.string(), repB));
    CompileOptions bundleOptions;
    PrefabStockCatalog cat;
    cat.sounds["x"] = {};
    bundleOptions.prefabSupport.stock = &cat;
    bundleOptions.prefabSupport.features = {"scriptable_instances", "scriptable_def_carry", "stock_script_calls"};
    bundleOptions.materialRoots = {(src / "nowhere").string()};
    CHECK(compileMap(host, outC, bundleOptions, src.string(), repC));
    const auto a = folderBytes(outA), b = folderBytes(outB), c = folderBytes(outC);
    CHECK(a.size() > 15 && a == b && a == c);
    CHECK(a.count("extracted/maps/mp/mp_bundle_t.gsc") && !has(a.at("extracted/maps/mp/mp_bundle_t.gsc"), "prefab"));
    CHECK(repA.warnings.size() == repB.warnings.size() + 2);  // "2 prefab(s) expanded" and the shared-name note

    // The sample room (no prefab at all): the same extraction with and without bundle options.
    const fs::path sample = repoPath("tests/data/iw8_sample_room.map");
    CHECK(!sample.empty());
    const std::string room = slurp(sample);
    CompileReport repD, repE;
    const fs::path outD = scratch("sample_out_d"), outE = scratch("sample_out_e");
    CompileOptions sampleOpt;
    sampleOpt.mapId = "mp_sample_t";
    bundleOptions.mapId = "mp_sample_t";
    CHECK(compileMap(room, outD, sampleOpt, "", repD) && compileMap(room, outE, bundleOptions, "", repE));
    CHECK(folderBytes(outD).size() > 10 && folderBytes(outD) == folderBytes(outE) && repD.warnings == repE.warnings);
}

// ------------------------------------------------------------------ save selection

TEST(prefab_bundle_save_selection_keeps_entities_and_collects_assets) {
    const fs::path dir = scratch("save");
    // The mapper's own assets.
    put(dir / "mats" / "sub" / "plank.tga", tgaBytes(8, 8, 120, 80, 40));
    put(dir / "mats" / "sub" / "plank.iw8mat.json", "{\"name\": \"sub/plank\", \"color\": \"plank.tga\", \"keep_me\": 5}");
    put(dir / "mats" / "skin.tga", tgaBytes(8, 8, 10, 20, 30));
    put(dir / "mats" / "skin.iw8mat.json", "{\"name\": \"own/skin\", \"color\": \"skin.tga\"}");
    put(dir / "mats" / "unused.iw8mat.json", "{\"name\": \"own/unused\", \"color\": \"skin.tga\"}");
    put(dir / "models" / "own_crate.obj", "mtllib own_crate.mtl\nv 0 0 0\nv 16 0 0\nv 0 16 0\nusemtl own/skin\nf 1 2 3\n");
    put(dir / "models" / "own_crate.mtl", "newmtl own/skin\n");
    put(dir / "sounds" / "own_beep.wav", wavBytes());
    put(dir / "sounds" / "own_beep.json", "{}");
    put(dir / "fx" / "own" / "sparkle.iw8fx.json", "{}");

    const std::string text =
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + box({{0, 0, 0}, {64, 64, 16}}, "sub/plank") + box({{0, 0, 16}, {64, 8, 32}}, "tm/stock_wall") +
        box({{500, 500, 0}, {564, 564, 16}}, "world/elsewhere") + "}\n" +
        "{\n\"classname\" \"script_brushmodel\"\n\"targetname\" \"door1\"\n\"iw8r_door\" \"hinge 90 0.8\"\n\"origin\" \"32 4 0\"\n" +
        box({{0, 8, 0}, {64, 12, 96}}, "caulk") + box({{0, 12, 0}, {64, 14, 96}}, "sub/plank") + "}\n" +
        "{\n\"classname\" \"trigger_use\"\n\"targetname\" \"door1_trig\"\n\"target\" \"door1\"\n" + box({{0, -40, 0}, {64, 56, 96}}, "trigger") + "}\n" +
        "{\n\"classname\" \"misc_model\"\n\"model\" \"own_crate\"\n\"origin\" \"8 8 16\"\n}\n" +
        "{\n\"classname\" \"script_model\"\n\"model\" \"tm/stock_barrel\"\n\"targetname\" \"barrel\"\n\"target\" \"far_away\"\n\"origin\" \"40 40 16\"\n}\n" +
        "{\n\"classname\" \"fx_origin\"\n\"fx\" \"own/sparkle\"\n\"origin\" \"16 16 20\"\n}\n" +
        "{\n\"classname\" \"fx_origin\"\n\"fx\" \"vfx/stock_smoke\"\n\"soundalias\" \"stock_hum\"\n\"origin\" \"48 16 20\"\n}\n" +
        "{\n\"classname\" \"sound_emitter\"\n\"soundalias\" \"own_beep\"\n\"origin\" \"16 48 20\"\n}\n" +
        "{\n\"classname\" \"trigger_multiple\"\n\"targetname\" \"outside_trig\"\n\"target\" \"door1\"\n" + box({{200, 0, 0}, {232, 32, 64}}, "trigger") + "}\n" +
        "{\n\"classname\" \"script_origin\"\n\"targetname\" \"far_away\"\n\"origin\" \"900 0 0\"\n}\n" +
        "{\n\"classname\" \"script_origin\"\n\"targetname\" \"unrelated\"\n\"origin\" \"901 0 0\"\n}\n";
    Document doc;
    CHECK(doc.loadFromText(text, (dir / "level.map").string(), nullptr));
    // Select: the first two world brushes, the door, its trigger, and the five point entities next to them.
    const Entity& world = doc.map().entities[0];
    size_t n = 0;
    for (const auto& it : world.items)
        if (it.isPrimitive() && n++ < 2) doc.select({world.id, it.id}, true);
    for (const Entity& e : doc.map().entities) {
        const std::string* tn = e.value("targetname");
        if (e.isWorldspawn() || (tn && (*tn == "outside_trig" || *tn == "far_away" || *tn == "unrelated"))) continue;
        bool prims = false;
        for (const auto& it : e.items)
            if (it.isPrimitive() && !prims) {
                doc.select({e.id, it.id}, true);  // one brush selects the whole brush entity
                prims = true;
            }
        if (!prims) doc.select({e.id, kNoId}, true);
    }
    const std::string before = doc.map().text();

    PrefabStockCatalog cat;
    cat.materials["tm/stock_wall"] = {"mp_a", "mp_b"};
    cat.models["tm/stock_barrel"] = {"mp_c"};
    cat.sounds["stock_hum"] = {"common_mp"};
    cat.effects["vfx/other"] = {};
    PrefabSaveOptions opt;
    opt.name = "Door Set (wood)";
    opt.description = "A door with its trigger.";
    opt.category = "doors";
    opt.tags = {"door"};
    opt.materialRoots = {(dir / "mats").string()};
    opt.modelRoots = {(dir / "models").string()};
    opt.soundRoots = {(dir / "sounds").string()};
    opt.fxRoots = {(dir / "fx").string()};
    opt.stock = &cat;
    opt.scriptText = "prefab_main(inst)\n{\n    wait 0.05;\n}\n";
    const PrefabSaveResult r = prefabBundleFromSelection(doc, opt);
    CHECK(r.ok && r.error.empty() && doc.map().text() == before);

    // The pivot: bottom centre of the selection (x 0..64, y -40..64, z 0..96: centre 32 12 0), snapped to the grid of 8.
    CHECK(length(r.pivot - Vec3{32, 16, 0}) < 1e-9);
    const PrefabManifest& m = r.manifest;
    CHECK(m.id == "door_set_wood" && m.title == "Door Set (wood)" && m.category == "doors" && m.description == "A door with its trigger.");
    CHECK(m.bounds && length(m.bounds->mins - Vec3{-32, -56, 0}) < 1e-9 && length(m.bounds->maxs - Vec3{32, 48, 96}) < 1e-9);
    CHECK(m.scriptFile == "script.gsc" && r.scriptText == opt.scriptText);

    // prefab.map: world brushes in worldspawn, the brush entities still entities with their keys and brushes.
    const MapFile pm = parse(r.mapText);
    CHECK(pm.entities.size() == 8 && pm.entities[0].isWorldspawn());
    size_t worldPrims = 0;
    for (const auto& it : pm.entities[0].items) worldPrims += it.isPrimitive();
    CHECK(worldPrims == 2);
    const Entity* door = nullptr;
    const Entity* trig = nullptr;
    for (const Entity& e : pm.entities) {
        if (e.classname() == "script_brushmodel") door = &e;
        if (e.classname() == "trigger_use") trig = &e;
    }
    CHECK(door && trig);
    if (door && trig) {
        size_t doorPrims = 0;
        AABB doorBox;
        for (const auto& it : door->items)
            if (const Primitive* p = it.prim()) {
                ++doorPrims;
                doorBox.add(p->bounds());
            }
        CHECK(doorPrims == 2 && *door->value("targetname") == "door1" && *door->value("iw8r_door") == "hinge 90 0.8");
        CHECK(*door->value("origin") == "0 -12 0");  // moved with the brushes
        CHECK(length(doorBox.mins - Vec3{-32, -8, 0}) < 1e-6 && length(doorBox.maxs - Vec3{32, -2, 96}) < 1e-6);
        CHECK(*trig->value("target") == "door1" && *trig->value("targetname") == "door1_trig");
    }
    // Names: the door and its trigger are local; the name that points outside is global.
    CHECK(m.hasLocalNames && m.namesLocal == (std::vector<std::string>{"door1", "door1_trig"}));
    CHECK(m.namesGlobal == std::vector<std::string>{"far_away"});
    // The objects outside the selection that are linked to it.
    CHECK(r.linkedOutside.size() == 2);
    if (r.linkedOutside.size() == 2) {
        CHECK(r.linkedOutside[0].text == "trigger_multiple 'outside_trig' targets 'door1' in the selection and is not selected");
        CHECK(r.linkedOutside[1].text == "script_origin 'far_away' is targeted by the selection ('far_away') and is not selected");
        CHECK(doc.findEntity(r.linkedOutside[0].entity) && doc.findEntity(r.linkedOutside[0].entity)->classname() == "trigger_multiple");
    }

    // Own assets: listed in the manifest and in the files to copy; the model brings its material.
    CHECK(m.materials.size() == 2 && m.materials[0].name == "sub/plank" && m.materials[0].file == "materials/sub_plank.iw8mat.json");
    CHECK(m.materials[1].name == "own/skin" && m.materials[1].file == "materials/own_skin.iw8mat.json");
    CHECK(m.models.size() == 1 && m.models[0].name == "own_crate" && m.models[0].file == "models/own_crate.obj");
    CHECK(m.sounds.size() == 1 && m.sounds[0].alias == "own_beep" && m.sounds[0].file == "sounds/own_beep.wav");
    CHECK(m.effects.size() == 1 && m.effects[0].name == "own/sparkle" && m.effects[0].file == "effects/own/sparkle.iw8fx.json");
    std::vector<std::string> dests;
    for (const auto& f : r.files) dests.push_back(f.dest);
    std::sort(dests.begin(), dests.end());
    CHECK(dests == (std::vector<std::string>{"effects/own/sparkle.iw8fx.json", "materials/plank.tga", "materials/skin.tga", "models/own_crate.mtl",
                                             "models/own_crate.obj", "sounds/own_beep.json", "sounds/own_beep.wav"}));
    CHECK(r.generated.size() == 2);
    for (const auto& [dest, json] : r.generated)
        if (dest == "materials/sub_plank.iw8mat.json")
            CHECK(has(json, "\"name\": \"sub/plank\"") && has(json, "\"color\": \"plank.tga\"") && has(json, "\"keep_me\": 5"));
    // Stock assets by name, with the catalogue's zones; names that are neither are told to the mapper.
    CHECK(m.stockMaterials.size() == 1 && m.stockMaterials[0].name == "tm/stock_wall" && m.stockMaterials[0].zones.size() == 2);
    CHECK(m.stockModels.size() == 1 && m.stockModels[0].name == "tm/stock_barrel" && m.stockModels[0].zones == std::vector<std::string>{"mp_c"});
    CHECK(m.stockSounds == std::vector<std::string>{"stock_hum"} && m.stockEffects.empty());
    CHECK(hasLine(r.notes, "effect 'vfx/stock_smoke' is not one of your effects and not in the always-loaded stock effects"));
    CHECK(!hasLine(r.notes, "caulk") && !hasLine(r.notes, "trigger") && !hasLine(r.notes, "script:"));
    CHECK(contains(m.buildFeatures, "custom_sounds"));

    // Without a catalogue every name that is not the mapper's own is required by name.
    opt.stock = nullptr;
    const PrefabSaveResult plain = prefabBundleFromSelection(doc, opt);
    CHECK(plain.manifest.stockEffects == std::vector<std::string>{"vfx/stock_smoke"} && plain.manifest.stockMaterials.size() == 1 && plain.notes.empty());
    // A script without the entry function is told, not refused.
    opt.scriptText = "start()\n{\n}\n";
    CHECK(hasLine(prefabBundleFromSelection(doc, opt).notes, "script: no prefab_main() function"));
    // A picked pivot is used as it is.
    opt.pivot = Vec3{1, 2, 3};
    CHECK(length(prefabBundleFromSelection(doc, opt).pivot - Vec3{1, 2, 3}) < 1e-12);
    // Nothing selected.
    Document emptyDoc;
    CHECK(emptyDoc.loadFromText(text, "x.map", nullptr));
    const PrefabSaveResult none = prefabBundleFromSelection(emptyDoc, opt);
    CHECK(!none.ok && has(none.error, "nothing is selected"));
    std::string err;
    CHECK(!writePrefabBundleFolder((dir / "out" / "none.iw8prefab").string(), none, &err) && has(err, "nothing is selected"));

    // Written to disk it is a bundle that loads, lists its assets without a warning, and builds.
    const fs::path folder = dir / "out" / "prefabs" / "doors" / "door_set.iw8prefab";
    CHECK(!writePrefabBundleFolder((dir / "out" / "door_set").string(), r, &err) && has(err, "must end with .iw8prefab"));
    CHECK(writePrefabBundleFolder(folder.string(), r, &err));
    CHECK(!writePrefabBundleFolder(folder.string(), r, &err) && has(err, "already holds a prefab"));
    CHECK(writePrefabBundleFolder(folder.string(), r, &err, true));
    PrefabBundle b;
    CHECK(loadPrefabBundle(folder.string(), b, &err));
    CHECK(b.warnings.empty() && b.category == "doors" && !b.scriptFile.empty() && !b.materialsDir.empty() && !b.modelsDir.empty());
    CHECK(slurp(folder / "prefab.map") == r.mapText && fs::exists(folder / "materials" / "plank.tga") && fs::exists(folder / "sounds" / "own_beep.wav"));
    MaterialLibrary lib;
    std::string lerr;
    CHECK(lib.load({b.materialsDir}, &lerr) && lib.find("sub/plank") && lib.find("own/skin"));

    // Saved, placed twice, expanded: the door is still a door with its brushes, once per placement.
    const std::string host = hostText(placement("prefabs/doors/door_set.iw8prefab", "-200 0 0", "\"iw8r_pid\" \"p1\"\n") +
                                      placement("prefabs/doors/door_set.iw8prefab", "200 0 0", "\"iw8r_pid\" \"p2\"\n") +
                                      "{\n\"classname\" \"script_origin\"\n\"targetname\" \"far_away\"\n\"origin\" \"0 0 8\"\n}\n");
    std::vector<std::string> warn;
    const MapFile flat = parse(flatten(dir / "out", host, warn));
    size_t doors = 0;
    for (const Entity& e : flat.entities) {
        if (e.classname() != "script_brushmodel") continue;
        ++doors;
        size_t prims = 0;
        for (const auto& it : e.items) prims += it.isPrimitive();
        CHECK(prims == 2 && (*e.value("targetname") == "p1_door1" || *e.value("targetname") == "p2_door1"));
    }
    CHECK(doors == 2);
    for (const Entity& e : flat.entities)
        if (e.classname() == "script_model") CHECK(*e.value("target") == "far_away" && *e.value("targetname") == "barrel");
    CompileReport rep;
    const fs::path out = scratch("save_build");
    CHECK(compileMap(host, out, CompileOptions{}, (dir / "out").string(), rep));
    CHECK(fs::exists(out / "extracted" / "materials" / "sub" / "plank.json") && fs::exists(out / "extracted" / "xmodel" / "own_crate.json"));
    const std::string gsc = slurp(mapsDir(out) / "mp_bundle_t.gsc");
    CHECK(has(gsc, "getentarray(\"p1_door1\", \"targetname\")") && has(gsc, "iw8r_trigger(\"p1_door1_trig\")") && has(gsc, "iw8r_trigger(\"p2_door1_trig\")"));
    CHECK(count(gsc, "level thread iw8rp_door_set_wood_main(iw8r_prefab_inst(") == 2);
    CHECK(hasLine(rep.warnings, "prefab bundle 'door_set_wood' (door_set.iw8prefab): 2 placement(s), main contents"));
}

// The inspector reads a parameter back from the key it wrote on the placement.
TEST(prefab_bundle_parameter_value_is_read_back_from_the_placement) {
    PrefabParameter speed;
    speed.name = "speed";
    speed.type = "number";
    speed.selector = "pad";
    speed.key = "iw8r_jump";
    speed.valueTemplate = "0 0 {speed}";
    PrefabParameter swing = speed;
    swing.name = "angle";
    swing.key = "iw8r_door";
    swing.valueTemplate = "hinge {angle} 0.8";
    PrefabParameter twice = speed;
    twice.name = "s";
    twice.key = "modelscale_pair";
    twice.valueTemplate = "{s} x {s}";
    PrefabParameter wood;
    wood.name = "wood";
    wood.type = "material";
    wood.material = "twc/wood";
    PrefabParameter fixed = speed;
    fixed.key = "fixed";
    fixed.valueTemplate = "always";

    const auto entity = [&](const std::string& extra) {
        const MapFile m = parse("iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n" + placement("kit.iw8prefab", "0 0 0", extra));
        CHECK(m.entities.size() == 2);
        return m.entities.size() == 2 ? m.entities[1] : Entity{};
    };
    const auto line = [](const std::pair<std::string, std::string>& kv) { return "\"" + kv.first + "\" \"" + kv.second + "\"\n"; };

    // No key on the placement: nothing to read (the inspector shows the default).
    const Entity bare = entity("");
    CHECK(!prefabParameterValue(speed, bare) && !prefabParameterValue(wood, bare));
    // What prefabParameterKey wrote comes back, for a value at the end, in the middle, twice, and for a material.
    const Entity set = entity(line(prefabParameterKey(speed, "900")) + line(prefabParameterKey(swing, "120")) +
                              line(prefabParameterKey(twice, "2.5")) + line(prefabParameterKey(wood, "twc/oak")));
    CHECK(prefabParameterValue(speed, set) == std::optional<std::string>("900"));
    CHECK(prefabParameterValue(swing, set) == std::optional<std::string>("120"));
    CHECK(prefabParameterValue(twice, set) == std::optional<std::string>("2.5"));
    CHECK(prefabParameterValue(wood, set) == std::optional<std::string>("twc/oak"));
    // A value edited by hand that no longer fits the template is not guessed at.
    const Entity odd = entity("\"iw8r_ovr:pad:iw8r_jump\" \"100 0 900\"\n\"iw8r_ovr:pad:iw8r_door\" \"hinge 120\"\n"
                              "\"iw8r_ovr:pad:modelscale_pair\" \"2 x 3\"\n\"iw8r_ovr:pad:fixed\" \"always\"\n");
    CHECK(!prefabParameterValue(speed, odd) && !prefabParameterValue(swing, odd) && !prefabParameterValue(twice, odd));
    CHECK(!prefabParameterValue(fixed, odd));  // the template has no {name}
    // An empty value is a value.
    CHECK(prefabParameterValue(speed, entity("\"iw8r_ovr:pad:iw8r_jump\" \"0 0 \"\n")) == std::optional<std::string>(""));
}

// The old save keeps working for world brushes and point entities and now keeps brush entities.
TEST(prefab_bundle_selection_to_prefab_text_keeps_brush_entities) {
    const std::string text = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + box({{0, 0, 0}, {64, 64, 16}}, "world/a") + "}\n" +
                             "{\n\"classname\" \"script_brushmodel\"\n\"targetname\" \"lift\"\n\"iw8r_lift\" \"128 2 2\"\n" +
                             box({{0, 0, 16}, {64, 64, 24}}, "world/b") + box({{0, 0, 24}, {8, 8, 64}}, "world/b") + "}\n" +
                             "{\n\"classname\" \"light\"\n\"origin\" \"32 32 100\"\n}\n";
    Document doc;
    CHECK(doc.loadFromText(text, "x.map", nullptr));
    const Entity& world = doc.map().entities[0];
    for (const auto& it : world.items)
        if (it.isPrimitive()) doc.select({world.id, it.id}, true);
    const Entity& lift = doc.map().entities[1];
    for (const auto& it : lift.items)
        if (it.isPrimitive()) {
            doc.select({lift.id, it.id}, true);
            break;
        }
    doc.select({doc.map().entities[2].id, kNoId}, true);
    const MapFile pm = parse(selectionToPrefabText(doc, Vec3{32, 32, 0}));
    CHECK(pm.entities.size() == 3 && pm.entities[0].isWorldspawn());
    if (pm.entities.size() != 3) return;
    size_t worldPrims = 0, liftPrims = 0;
    for (const auto& it : pm.entities[0].items) worldPrims += it.isPrimitive();
    CHECK(worldPrims == 1);
    const Entity* saved = pm.entities[1].classname() == "script_brushmodel" ? &pm.entities[1] : &pm.entities[2];
    for (const auto& it : saved->items) liftPrims += it.isPrimitive();
    CHECK(saved->classname() == "script_brushmodel" && liftPrims == 2 && *saved->value("targetname") == "lift" && *saved->value("iw8r_lift") == "128 2 2");
    // Load, expand at the old place: the lift is back where it was, as an entity.
    PrefabInstance inst;
    inst.model = "x";
    inst.origin = Vec3{32, 32, 0};
    const PrefabContents c = instantiatePrefab(pm, inst, "\n");
    CHECK(c.worldPrims.size() == 1 && c.entities.size() == 2);
    for (const auto& [kvs, prims] : c.entities) {
        if (prims.empty()) continue;
        AABB b;
        for (const Primitive& p : prims) b.add(p.bounds());
        CHECK(prims.size() == 2 && length(b.mins - Vec3{0, 0, 16}) < 1e-6 && length(b.maxs - Vec3{64, 64, 64}) < 1e-6);
    }
}

// ------------------------------------------------------------------ the shipped bundles

TEST(prefab_bundle_shipped_bundles_load_validate_and_build) {
    const fs::path root = repoPath("data/prefabs");
    CHECK(!root.empty() && fs::exists(root / "bundles"));
    if (root.empty()) return;
    const auto list = listPrefabBundles({(root / "bundles").string()});
    CHECK(list.size() == 2);
    const PrefabBundle* door = nullptr;
    const PrefabBundle* pad = nullptr;
    for (const auto& e : list) {
        CHECK(e.bundle.placeable && e.bundle.warnings.empty());
        if (!e.bundle.problem.empty()) std::printf("  %s\n", e.bundle.problem.c_str());
        if (e.model == "doors/stock_door_wood_single.iw8prefab") door = &e.bundle;
        if (e.model == "gameplay/jump_pad.iw8prefab") pad = &e.bundle;
    }
    CHECK(door && pad);
    if (!door || !pad) return;
    PrefabStockCatalog cat;
    CHECK(cat.load(repoPath("data/asset_catalog").string()) == 4);
    PrefabBuildSupport today;
    today.stock = &cat;

    // Jump pad: model, behaviour, effect and sound in one object; possible today.
    CHECK(pad->manifest.id == "jump_pad" && pad->title == "Jump pad with effect and sound" && pad->category == "gameplay");
    CHECK(pad->manifest.parameters.size() == 1 && pad->manifest.namesLocal == std::vector<std::string>{"jump_pad_trigger"});
    PrefabContentsChoice c = choosePrefabContents(*pad, today);
    CHECK(!c.fallback && c.missingFeatures.empty() && c.missingAssets.empty() && c.reason.empty() && c.scriptFile.empty());
    std::string classes;
    for (const Entity& e : parse(slurp(pad->mapFile)).entities) classes += e.classname() + " ";
    CHECK(classes == "worldspawn misc_model trigger_multiple fx_origin sound_emitter ");

    // Stock door: the native door when the build can write scriptables, the stock-parts door until then. Every
    // stock name it needs is in the catalogue.
    CHECK(door->manifest.id == "stock_door_wood_single" && door->category == "doors" && !door->fallbackMapFile.empty());
    c = choosePrefabContents(*door, today);
    CHECK(c.fallback && c.missingAssets.empty() && fs::path(c.scriptFile).filename() == "fallback.gsc");
    CHECK(has(c.reason, "stock door built as a scripted door from stock parts"));
    PrefabBuildSupport later = today;
    later.features = {"scriptable_instances", "scriptable_def_carry", "stock_script_calls"};
    c = choosePrefabContents(*door, later);
    CHECK(!c.fallback && c.missingFeatures.empty() && c.reason.empty() && c.scriptFile.empty());
    bool scriptable = false;
    for (const Entity& e : parse(slurp(door->mapFile)).entities)
        scriptable |= e.classname() == "scriptable" && *e.value("scriptable") == "scriptable_door_wooden_panel_03_painted_mp";
    CHECK(scriptable);
    const std::string doorScript = slurp(door->fallbackScriptFile);
    CHECK(checkPrefabScript(doorScript, door->manifest).empty());
    CHECK(has(doorScript, "rotateyaw(90 * dir, 0.8, 0.1, 0.5)") && has(doorScript, "sethintstring(\"Open Door\")") &&
          has(doorScript, "playsound(\"scrpt_door_wood_single_open\")") && has(doorScript, "setuserange(125)"));
    // No game asset is in the repository: only text files of the editor.
    for (const auto& [name, bytes] : folderBytes(root / "bundles")) {
        const std::string ext = fs::path(name).extension().string();
        CHECK(ext == ".json" || ext == ".map" || ext == ".gsc");
    }

    // A small map with two of each: it builds, and the two placements of each kind are apart.
    const std::string host = hostText(placement("bundles/gameplay/jump_pad.iw8prefab", "-100 -200 0", "\"iw8r_pid\" \"p1\"\n") +
                                      placement("bundles/gameplay/jump_pad.iw8prefab", "100 -200 0",
                                                "\"iw8r_pid\" \"p2\"\n\"" + prefabParameterKey(pad->manifest.parameters[0], "900").first + "\" \"" +
                                                    prefabParameterKey(pad->manifest.parameters[0], "900").second + "\"\n") +
                                      placement("bundles/doors/stock_door_wood_single.iw8prefab", "-100 200 0", "\"iw8r_pid\" \"p3\"\n") +
                                      placement("bundles/doors/stock_door_wood_single.iw8prefab", "100 200 0", "\"iw8r_pid\" \"p4\"\n\"angles\" \"0 180 0\"\n"));
    CompileOptions opt;
    opt.prefabRoots = {root.string()};
    opt.prefabSupport = today;
    CompileReport rep;
    const fs::path out = scratch("shipped_out");
    CHECK(compileMap(host, out, opt, scratch("shipped_src").string(), rep));
    const std::string gsc = slurp(mapsDir(out) / "mp_bundle_t.gsc");
    const std::string ents = slurp(mapsDir(out) / "mp_bundle_t.d3dbsp.ents");
    // Two jump pads with their own triggers; the second one launches with the speed of its parameter.
    CHECK(has(gsc, "iw8r_trigger(\"p1_jump_pad_trigger\")") && has(gsc, "iw8r_trigger(\"p2_jump_pad_trigger\")"));
    CHECK(has(gsc, "setvelocity((0, 0, 700))") && has(gsc, "setvelocity((0, 0, 900))"));
    CHECK(count(gsc, "loadfx(\"vfx/core/mp/core/vfx_marker_base_cyan_pulse\")") == 1 && count(gsc, "iw8r_loop_sound(\"emt_light_fluorescent_hum1_int\"") == 2);
    // Two doors from one script, each started with its own placement.
    CHECK(count(gsc, "\niw8rp_stock_door_wood_single_main(inst)\n") == 1);
    CHECK(has(gsc, "    level thread iw8rp_stock_door_wood_single_main(iw8r_prefab_inst(\"p3\", (-100, 200, 0), (0, 0, 0)));\n"));
    CHECK(has(gsc, "    level thread iw8rp_stock_door_wood_single_main(iw8r_prefab_inst(\"p4\", (100, 200, 0), (0, 180, 0)));\n"));
    CHECK(has(gsc, "level thread iw8rp_stock_door_wood_single_watch_use(door);") && !has(gsc, "prefab_main"));
    CHECK(has(gsc, "iw8r_make_solid(\"p3_door\")") && has(gsc, "iw8r_make_solid(\"p4_door\")"));
    CHECK(has(ents, "p3_door") && has(ents, "p4_door") && has(ents, "p3_door_use") && has(ents, "p4_door_use"));
    CHECK(std::count(gsc.begin(), gsc.end(), '{') == std::count(gsc.begin(), gsc.end(), '}'));
    CHECK(std::count(gsc.begin(), gsc.end(), '(') == std::count(gsc.begin(), gsc.end(), ')'));
    // The log: what was built, and that the door is the stock-parts one.
    CHECK(hasLine(rep.warnings, "4 prefab(s) expanded into the build"));
    CHECK(hasLine(rep.warnings, "prefab bundle 'jump_pad' (jump_pad.iw8prefab): 2 placement(s), main contents"));
    CHECK(hasLine(rep.warnings, "prefab bundle 'stock_door_wood_single' (stock_door_wood_single.iw8prefab): 2 placement(s), fallback contents; "
                                "its script is included once and started 2 time(s)"));
    CHECK(hasLine(rep.warnings, "fallback contents built: stock door built as a scripted door from stock parts (Real stock scripted objects is off "
                                "in Preferences) (missing build feature(s): scriptable_instances, scriptable_def_carry)"));
    CHECK(!hasLine(rep.warnings, "which is not in the stock catalogue") && !hasLine(rep.warnings, "iw8r_pid") && !hasLine(rep.warnings, "script:"));
}

// ------------------------------------------------------------------ sharing and converting (8.5 rest)

namespace {
fs::path repoFile(const std::string& rel) {
    fs::path p = fs::current_path();
    for (int up = 0; up < 6; ++up, p = p.parent_path())
        if (fs::exists(p / rel)) return p / rel;
    return {};
}
}  // namespace

TEST(prefab_bundle_zip_round_trip_and_deflate) {
    const fs::path root = scratch("zip_share");
    // A bundle folder with a manifest, contents and an asset file in a subfolder.
    const fs::path b = root / "src" / "crate.iw8prefab";
    put(b / "prefab.json", "{\n  \"format\": \"iw8r-prefab-1\",\n  \"id\": \"crate\"\n}\n");
    put(b / "prefab.map", "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + box(AABB{{0, 0, 0}, {32, 32, 32}}, "caulk") + "}\n");
    put(b / "materials" / "crate.iw8mat.json", "{}\n");
    std::string err;
    const fs::path zip = root / "crate.zip";
    CHECK(exportPrefabBundleZip(b.string(), zip.string(), &err));
    // Import into another root: the bundle comes back whole; a second import does not replace it.
    std::vector<std::string> written;
    CHECK(importPrefabBundleZip(zip.string(), (root / "dest").string(), &written, &err));
    CHECK(written.size() == 1 && fs::exists(root / "dest" / "crate.iw8prefab" / "materials" / "crate.iw8mat.json"));
    CHECK(slurp(root / "dest" / "crate.iw8prefab" / "prefab.map") == slurp(b / "prefab.map"));
    CHECK(!importPrefabBundleZip(zip.string(), (root / "dest").string(), &written, &err) && has(err, "not replaced"));
    // Nothing of the scratch extraction is left behind.
    size_t leftovers = 0;
    for (const auto& e : fs::directory_iterator(root / "dest"))
        if (e.path().filename().string().rfind(".import-", 0) == 0) ++leftovers;
    CHECK(leftovers == 0);
    // A folder that is not a bundle is refused.
    CHECK(!exportPrefabBundleZip((root / "src").string(), (root / "x.zip").string(), &err));
    // A zip made by another tool with deflated entries (tests/data/deflated_bundle.zip, Python zipfile ZIP_DEFLATED).
    const fs::path deflated = repoFile("tests/data/deflated_bundle.zip");
    CHECK(!deflated.empty());
    if (!deflated.empty()) {
        CHECK(importPrefabBundleZip(deflated.string(), (root / "dest2").string(), &written, &err));
        CHECK(written.size() == 1 && has(slurp(root / "dest2" / "deflated_crate.iw8prefab" / "prefab.map"), "crate_pos"));
    }
    // A zip with one bundle's files at its top: the folder is named after the zip.
    std::vector<PackageFile> flat = {{(b / "prefab.json").string(), "prefab.json"}, {(b / "prefab.map").string(), "prefab.map"}};
    CHECK(writeZip((root / "flatbox.zip").string(), flat, &err));
    CHECK(importPrefabBundleZip((root / "flatbox.zip").string(), (root / "dest3").string(), &written, &err));
    CHECK(written.size() == 1 && fs::exists(root / "dest3" / "flatbox.iw8prefab" / "prefab.map"));
    // A zip without a bundle.
    std::vector<PackageFile> none = {{(b / "materials" / "crate.iw8mat.json").string(), "readme.txt"}};
    CHECK(writeZip((root / "none.zip").string(), none, &err));
    CHECK(!importPrefabBundleZip((root / "none.zip").string(), (root / "dest4").string(), &written, &err) && has(err, "no prefab bundle"));
}

TEST(prefab_bundle_from_plain_map_keeps_origin) {
    const fs::path root = scratch("plain_convert");
    const fs::path map = root / "Wooden Crate.map";
    put(map, "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + box(AABB{{16, 16, 0}, {48, 48, 32}}, "caulk") +
                 "}\n{\n\"classname\" \"script_origin\"\n\"origin\" \"32 32 40\"\n\"targetname\" \"lid\"\n}\n");
    PrefabSaveOptions opt;
    const PrefabSaveResult r = prefabBundleFromPlainMap(map.string(), opt);
    CHECK(r.ok);
    CHECK(r.manifest.id == "wooden_crate" && r.pivot == Vec3(0, 0, 0));
    // The contents keep their coordinates (pivot at the file's own origin).
    CHECK(has(r.mapText, "\"origin\" \"32 32 40\"") && has(r.mapText, "script_origin"));
    std::string err;
    CHECK(writePrefabBundleFolder((root / "wooden_crate.iw8prefab").string(), r, &err));
    PrefabBundle loaded;
    CHECK(loadPrefabBundle((root / "wooden_crate.iw8prefab").string(), loaded, &err) && loaded.manifest.id == "wooden_crate");
    // An empty or missing file.
    put(root / "empty.map", "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n");
    CHECK(!prefabBundleFromPlainMap((root / "empty.map").string(), opt).ok);
    CHECK(!prefabBundleFromPlainMap((root / "missing.map").string(), opt).ok);
}

TEST(prefab_bundle_placement_hints) {
    PrefabManifest wall;
    wall.surface = "wall";
    wall.yawStep = 90;
    // A wall facing +y: the prefab's +X turns to +y (yaw 90).
    PrefabPlacement p = prefabPlacementFor(wall, Vec3(10, 20, 30), Vec3(0, 1, 0));
    CHECK(p.yaw == 90 && p.origin == Vec3(10, 20, 30) && p.note.empty());
    // A slightly skewed wall rounds to the yaw step; without a step it is exact.
    p = prefabPlacementFor(wall, Vec3(0, 0, 0), Vec3(-0.996, 0.087, 0));
    CHECK(p.yaw == 180);
    wall.yawStep = 0;
    p = prefabPlacementFor(wall, Vec3(0, 0, 0), Vec3(0, -1, 0));
    CHECK(std::fabs(p.yaw - 270) < 1e-9);
    // On a floor a wall prefab keeps yaw 0 and says why.
    p = prefabPlacementFor(wall, Vec3(0, 0, 0), Vec3(0, 0, 1));
    CHECK(p.yaw == 0 && has(p.note, "walls"));
    // Snap: x, y (and the height on a wall); the depth stays on the wall.
    wall.snap = 16;
    wall.yawStep = 90;
    p = prefabPlacementFor(wall, Vec3(7, 33.5, 50), Vec3(-1, 0, 0));
    CHECK(p.origin == Vec3(7, 32, 48) && p.yaw == 180);
    PrefabManifest floor;
    floor.snap = 8;
    p = prefabPlacementFor(floor, Vec3(3, 13, 5), Vec3(0, 0, 1));
    CHECK(p.origin == Vec3(0, 16, 5) && p.yaw == 0 && p.note.empty());
    PrefabManifest ceiling;
    ceiling.surface = "ceiling";
    CHECK(prefabPlacementFor(ceiling, Vec3(0, 0, 0), Vec3(0, 0, -1)).note.empty());
    CHECK(has(prefabPlacementFor(ceiling, Vec3(0, 0, 0), Vec3(0, 0, 1)).note, "ceilings"));
    CHECK(prefabPlacementFor(ceiling, Vec3(1, 2, 3), std::nullopt).origin == Vec3(1, 2, 3));
}
