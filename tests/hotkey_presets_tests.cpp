// Hotkey presets (roadmap 2.30): the real data file, conflicts, command ids, apply/merge, and load errors.
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "hotkey_presets.h"
#include "test_harness.h"

namespace fs = std::filesystem;
using namespace iw8r;

namespace {

// Finds a repo-relative file by walking up from the working directory (tests run from the repo or from bin/<mode>).
std::string repoFile(const std::string& rel) {
    fs::path p = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        if (fs::exists(p / rel)) return (p / rel).string();
        if (!p.has_parent_path() || p.parent_path() == p) break;
        p = p.parent_path();
    }
    return rel;
}

std::string readText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The presets, loaded once from the real data file. Empty when the file is missing or invalid.
const std::vector<HotkeyPreset>& presets() {
    static std::vector<HotkeyPreset> p;
    static const bool ok = [] {
        std::string err;
        const bool read = loadHotkeyPresets(readText(repoFile("data/hotkey_presets.json")), p, &err);
        if (!read) std::printf("  hotkey_presets.json: %s\n", err.c_str());
        return read && !p.empty();
    }();
    (void)ok;
    return p;
}

bool loaded() {
    return !presets().empty();
}

const HotkeyPreset* byId(const std::string& id) {
    for (const HotkeyPreset& p : presets())
        if (p.id == id) return &p;
    return nullptr;
}

HotkeyPreset makePreset(const std::map<std::string, std::string>& keys) {
    HotkeyPreset p;
    p.id = "test";
    p.name = "test";
    p.keys = keys;
    return p;
}

}  // namespace

TEST(hotkey_presets_file_loads_all_three) {
    CHECK(loaded());
    CHECK(byId("iw8radiant") != nullptr);
    CHECK(byId("stock_radiant") != nullptr);
    CHECK(byId("iw3xo") != nullptr);
    CHECK(presets().size() == 3);
    const HotkeyPreset* def = byId("iw8radiant");
    CHECK(def && def->keys.size() > 200);  // every addCommand() in main_window.cpp, including the loop families
}

TEST(hotkey_presets_default_matches_main_window) {
    const HotkeyPreset* def = byId("iw8radiant");
    CHECK(def != nullptr);
    if (!def) return;
    // Spot checks against src/qtapp/main_window.cpp (a regenerated file must keep these).
    CHECK(def->keys.at("Save") == "Ctrl+S");
    CHECK(def->keys.at("CommandPalette") == "Ctrl+Shift+P");
    CHECK(def->keys.at("CenterCameraOnSelection") == "End");
    CHECK(def->keys.at("CameraBookmarkGo1") == "Ctrl+1");
    CHECK(def->keys.at("CameraBookmarkSet9") == "Ctrl+Shift+9");
    CHECK(def->keys.at("SetGridPointFive") == "`");
    CHECK(def->keys.at("SetGrid128") == "");  // listed, unbound
    CHECK(def->keys.at("RotateZ") == "");     // loop family, unbound
    CHECK(def->keys.at("AlignXMin") == "");
}

TEST(hotkey_presets_no_conflicts_inside_any_preset) {
    CHECK(loaded());
    for (const HotkeyPreset& p : presets()) {
        const auto c = presetConflicts(p);
        if (!c.empty()) std::printf("  conflict in %s: %s / %s\n", p.id.c_str(), c[0].first.c_str(), c[0].second.c_str());
        CHECK(c.empty());
    }
}

TEST(hotkey_presets_no_conflicts_after_applying_to_defaults) {
    // What the editor would bind after the preset is applied to the IW8 defaults (not just the preset's own keys).
    const HotkeyPreset* def = byId("iw8radiant");
    CHECK(def != nullptr);
    if (!def) return;
    CHECK(presetConflicts(*def).empty());
    for (const HotkeyPreset& p : presets()) {
        if (p.id == "iw8radiant") continue;
        const HotkeyPreset merged = makePreset(applyPreset(def->keys, p));
        const auto c = presetConflicts(merged);
        if (!c.empty()) std::printf("  %s over defaults: %s / %s\n", p.id.c_str(), c[0].first.c_str(), c[0].second.c_str());
        CHECK(c.empty());
    }
}

TEST(hotkey_presets_ids_exist_in_default) {
    const HotkeyPreset* def = byId("iw8radiant");
    CHECK(def != nullptr);
    if (!def) return;
    for (const HotkeyPreset& p : presets()) {
        for (const auto& [cmd, key] : p.keys) {
            const bool ok = def->keys.count(cmd) != 0;
            if (!ok) std::printf("  %s: unknown command id %s\n", p.id.c_str(), cmd.c_str());
            CHECK(ok);
        }
    }
}

TEST(hotkey_presets_sources_are_cited) {
    CHECK(loaded());
    for (const HotkeyPreset& p : presets()) {
        CHECK(!p.name.empty());
        CHECK(!p.source.empty());
    }
    const HotkeyPreset* stock = byId("stock_radiant");
    const HotkeyPreset* iw3xo = byId("iw3xo");
    CHECK(stock && stock->source.find("docs/research/07_editor_feel_reference.md") != std::string::npos);
    CHECK(iw3xo && iw3xo->source.find("hotkeys.ini") != std::string::npos);
}

TEST(hotkey_presets_stock_and_iw3xo_are_partial) {
    // Only documented bindings are listed; anything else keeps the default, so neither preset lists the whole table.
    const HotkeyPreset* def = byId("iw8radiant");
    const HotkeyPreset* stock = byId("stock_radiant");
    const HotkeyPreset* iw3xo = byId("iw3xo");
    CHECK(def && stock && iw3xo);
    if (!def || !stock || !iw3xo) return;
    CHECK(!stock->keys.empty() && stock->keys.size() < def->keys.size());
    CHECK(!iw3xo->keys.empty() && iw3xo->keys.size() < def->keys.size());
    CHECK(stock->keys.at("HideUnselected") == "Alt+H");
    CHECK(iw3xo->keys.at("GizmoLocal") == "-");
    CHECK(stock->keys.count("CameraBookmarkGo1") == 0);  // no stock row for camera bookmarks
}

TEST(hotkey_presets_canonical_key) {
    CHECK(canonicalKey("Shift+Ctrl+F") == "Ctrl+Shift+F");
    CHECK(canonicalKey("Ctrl+Shift+F") == "Ctrl+Shift+F");
    CHECK(canonicalKey("ctrl+s") == "Ctrl+S");
    CHECK(canonicalKey("Shift+Alt+Ctrl+H") == "Ctrl+Alt+Shift+H");
    CHECK(canonicalKey("Ctrl+=") == "Ctrl+=");
    CHECK(canonicalKey("Ctrl+++") == "Ctrl++");
    CHECK(canonicalKey("Ctrl++") == "Ctrl++");
    CHECK(canonicalKey("") == "");
    CHECK(canonicalKey("Foo+S") == "Foo+S");  // unknown modifier: returned unchanged
    CHECK(canonicalKey("Ctrl+Ctrl+S") == "Ctrl+Ctrl+S");
}

TEST(hotkey_presets_conflict_detection) {
    // Same key in another order is the same key; empty keys never conflict; each pair is reported once.
    HotkeyPreset p = makePreset({{"A", "Shift+Ctrl+F"}, {"B", "Ctrl+Shift+F"}, {"C", "F"}, {"D", ""}, {"E", ""}});
    const auto c = presetConflicts(p);
    CHECK(c.size() == 1);
    CHECK(c.size() == 1 && c[0] == std::make_pair(std::string("A"), std::string("B")));

    HotkeyPreset three = makePreset({{"X", "Alt+Q"}, {"Y", "Alt+Q"}, {"Z", "Alt+Q"}});
    CHECK(presetConflicts(three).size() == 3);  // X-Y, X-Z, Y-Z

    HotkeyPreset clean = makePreset({{"A", "Ctrl+A"}, {"B", "Ctrl+B"}});
    CHECK(presetConflicts(clean).empty());
}

TEST(hotkey_preset_apply_merges_over_current) {
    const std::map<std::string, std::string> current = {{"Save", "Ctrl+S"}, {"Open", "Ctrl+O"}, {"Undo", "Ctrl+Z"}};
    const HotkeyPreset p = makePreset({{"Save", ""}, {"Open", "Ctrl+Alt+O"}, {"NewMap", "Ctrl+N"}});
    const auto out = applyPreset(current, p);
    CHECK(out.size() == 4);
    CHECK(out.at("Save").empty());             // "" unbinds
    CHECK(out.at("Open") == "Ctrl+Alt+O");     // overwritten
    CHECK(out.at("NewMap") == "Ctrl+N");       // added
    CHECK(out.at("Undo") == "Ctrl+Z");         // not listed: kept
    CHECK(current.at("Save") == "Ctrl+S");     // input unchanged

    const auto same = applyPreset(current, makePreset({}));
    CHECK(same == current);
}

TEST(hotkey_preset_apply_real_iw3xo_keeps_unlisted_defaults) {
    const HotkeyPreset* def = byId("iw8radiant");
    const HotkeyPreset* iw3xo = byId("iw3xo");
    CHECK(def && iw3xo);
    if (!def || !iw3xo) return;
    const auto out = applyPreset(def->keys, *iw3xo);
    CHECK(out.size() == def->keys.size());
    CHECK(out.at("HideUnselected") == "Alt+H");
    CHECK(out.at("CameraBookmarkGo2") == "Ctrl+2");  // no iw3xo row: default kept
    CHECK(out.at("CommandPalette") == "Ctrl+Shift+P");
}

TEST(hotkey_preset_load_small_document) {
    const std::string text = R"({"presets":[{"id":"a","name":"A","keys":{"Save":"Ctrl+S","Undo":""}},)"
                             R"({"id":"b","name":"B","source":"doc","keys":{}}]})";
    std::vector<HotkeyPreset> out;
    std::string err;
    CHECK(loadHotkeyPresets(text, out, &err));
    CHECK(err.empty());
    CHECK(out.size() == 2);
    if (out.size() != 2) return;
    CHECK(out[0].id == "a" && out[0].name == "A" && out[0].source.empty());
    CHECK(out[0].keys.size() == 2 && out[0].keys.at("Save") == "Ctrl+S" && out[0].keys.at("Undo").empty());
    CHECK(out[1].source == "doc" && out[1].keys.empty());
}

TEST(hotkey_preset_load_rejects_bad_documents) {
    struct Case {
        const char* name;
        const char* text;
    };
    const Case cases[] = {
        {"not json", "{presets"},
        {"no presets array", R"({"other":1})"},
        {"presets not an array", R"({"presets":{}})"},
        {"missing id", R"({"presets":[{"name":"A","keys":{}}]})"},
        {"missing name", R"({"presets":[{"id":"a","keys":{}}]})"},
        {"missing keys", R"({"presets":[{"id":"a","name":"A"}]})"},
        {"duplicate id", R"({"presets":[{"id":"a","name":"A","keys":{}},{"id":"a","name":"B","keys":{}}]})"},
        {"non-string key", R"({"presets":[{"id":"a","name":"A","keys":{"Save":3}}]})"},
        {"bad modifier", R"({"presets":[{"id":"a","name":"A","keys":{"Save":"Hyper+S"}}]})"},
        {"repeated modifier", R"({"presets":[{"id":"a","name":"A","keys":{"Save":"Ctrl+Ctrl+S"}}]})"},
        {"missing key name", R"({"presets":[{"id":"a","name":"A","keys":{"Save":"Ctrl+"}}]})"},
    };
    for (const Case& c : cases) {
        std::vector<HotkeyPreset> out;
        std::string err;
        const bool ok = loadHotkeyPresets(c.text, out, &err);
        if (ok) std::printf("  accepted bad document: %s\n", c.name);
        CHECK(!ok);
        CHECK(!err.empty());
    }
}
