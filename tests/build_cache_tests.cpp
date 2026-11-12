// Incremental builds (src/core/build_cache.*): writeIfChanged keeps unchanged files untouched, and the fingerprint
// classifies a map edit as none, entities-only, script-only or full.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "build_cache.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

using KV = std::vector<std::pair<std::string, std::string>>;

fs::path freshDir(const std::string& name) {
    const fs::path d = fs::path(test::tmpDir()) / "build_cache" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

// Axis-aligned box as Radiant brush text (same layout as the map diff tests).
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

// One entity block in the editor's layout, followed by its body (brushes).
std::string ent(const KV& kv, const std::string& body = "") {
    std::string t = "{\n";
    for (const auto& [k, v] : kv) t += "\"" + k + "\" \"" + v + "\"\n";
    return t + body + "}\n";
}

MapFile parseText(const std::string& text) {
    Id next = 1;
    ParseResult r = parseMap(text, next);
    CHECK(r.map.has_value());
    return r.map ? *r.map : MapFile{};
}

// A small map: worldspawn with one brush and a script key, a player start and a misc_model.
std::string baseText(int brushMaxX = 64, const std::string& script = "scripts/a.gsc", const std::string& spawnOrigin = "0 0 64",
                     const std::string& model = "crate_01", const std::string& mat = "tm/floor") {
    return "iwmap 4\n" +
           ent({{"classname", "worldspawn"}, {"iw8r_script", script}}, box(-64, -64, -16, brushMaxX, 64, 0, mat)) +
           ent({{"classname", "info_player_start"}, {"origin", spawnOrigin}}) +
           ent({{"classname", "misc_model"}, {"model", model}, {"origin", "100 0 0"}});
}

InputFingerprint fp(const std::string& text) { return fingerprintMap(parseText(text)); }

}  // namespace

TEST(build_cache_write_if_changed_identical_keeps_mtime) {
    const fs::path dir = freshDir("identical");
    const std::string path = (dir / "f.json").string();
    bool changed = true;
    CHECK(writeIfChanged(path, "{\"a\":1}\n", &changed));
    CHECK(changed);
    const auto old = fs::file_time_type::clock::now() - std::chrono::hours(48);
    fs::last_write_time(path, old);
    const auto before = fs::last_write_time(path);
    changed = true;
    CHECK(writeIfChanged(path, "{\"a\":1}\n", &changed));
    CHECK(!changed);
    CHECK(fs::last_write_time(path) == before);
}

TEST(build_cache_write_if_changed_rewrites_different_content) {
    const fs::path dir = freshDir("different");
    const std::string path = (dir / "f.json").string();
    bool changed = false;
    CHECK(writeIfChanged(path, "{\"a\":1}\n", &changed));
    const auto old = fs::file_time_type::clock::now() - std::chrono::hours(48);
    fs::last_write_time(path, old);
    // Same size, different bytes: the content comparison must catch it, not only the size.
    changed = false;
    CHECK(writeIfChanged(path, "{\"a\":2}\n", &changed));
    CHECK(changed);
    CHECK(fs::last_write_time(path) != old);
    {
        // Scoped: an open reader would block the replace of the file on Windows.
        std::ifstream f(path, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        CHECK(text == "{\"a\":2}\n");
    }
    // A longer file is rewritten too.
    changed = false;
    CHECK(writeIfChanged(path, "{\"a\":2,\"b\":3}\n", &changed));
    CHECK(changed);
}

TEST(build_cache_write_if_changed_creates_parents_and_leaves_no_temp) {
    const fs::path dir = freshDir("parents");
    const fs::path sub = dir / "extracted" / "materials";
    bool changed = false;
    CHECK(writeIfChanged((sub / "iw8r_grey.json").string(), "{}\n", &changed));
    CHECK(changed);
    CHECK(fs::is_regular_file(sub / "iw8r_grey.json"));
    int temps = 0;
    for (const auto& e : fs::directory_iterator(sub))
        if (e.path().filename().string().find("iw8r-tmp") != std::string::npos) ++temps;
    CHECK(temps == 0);
}

TEST(build_cache_fingerprint_no_change_is_none) {
    const InputFingerprint a = fp(baseText());
    const InputFingerprint b = fp(baseText());
    CHECK(a == b);
    CHECK(classifyChange(a, b) == "none");
}

TEST(build_cache_fingerprint_moved_point_entity_is_entities_only) {
    const InputFingerprint a = fp(baseText());
    const InputFingerprint b = fp(baseText(64, "scripts/a.gsc", "0 0 128"));
    CHECK(a.entities != b.entities);
    CHECK(a.world == b.world);
    CHECK(a.materials == b.materials);
    CHECK(a.models == b.models);
    CHECK(a.script == b.script);
    CHECK(classifyChange(a, b) == "entities-only");
}

TEST(build_cache_fingerprint_edited_brush_is_full) {
    const InputFingerprint a = fp(baseText());
    const InputFingerprint b = fp(baseText(96));
    CHECK(a.world != b.world);
    CHECK(a.entities == b.entities);
    CHECK(classifyChange(a, b) == "full");
}

TEST(build_cache_fingerprint_changed_script_key_is_script_only) {
    const InputFingerprint a = fp(baseText());
    const InputFingerprint b = fp(baseText(64, "scripts/b.gsc"));
    CHECK(a.script != b.script);
    CHECK(a.world == b.world);
    CHECK(a.entities == b.entities);
    CHECK(classifyChange(a, b) == "script-only");
}

TEST(build_cache_fingerprint_model_and_material_changes_are_full) {
    const InputFingerprint a = fp(baseText());
    const InputFingerprint model = fp(baseText(64, "scripts/a.gsc", "0 0 64", "crate_02"));
    CHECK(a.models != model.models);
    CHECK(classifyChange(a, model) == "full");
    const InputFingerprint mat = fp(baseText(64, "scripts/a.gsc", "0 0 64", "crate_01", "tm/wall"));
    CHECK(a.materials != mat.materials);
    CHECK(classifyChange(a, mat) == "full");
}

TEST(build_cache_fingerprint_entities_and_script_together_are_full) {
    const InputFingerprint a = fp(baseText());
    const InputFingerprint b = fp(baseText(64, "scripts/b.gsc", "0 0 128"));
    CHECK(classifyChange(a, b) == "full");
}

TEST(build_cache_fingerprint_worldspawn_key_other_than_script_is_full) {
    const std::string withFog = "iwmap 4\n" +
                                ent({{"classname", "worldspawn"}, {"iw8r_script", "scripts/a.gsc"}, {"iw8r_fog", "1"}},
                                    box(-64, -64, -16, 64, 64, 0, "tm/floor")) +
                                ent({{"classname", "info_player_start"}, {"origin", "0 0 64"}}) +
                                ent({{"classname", "misc_model"}, {"model", "crate_01"}, {"origin", "100 0 0"}});
    const std::string withoutFog = "iwmap 4\n" +
                                   ent({{"classname", "worldspawn"}, {"iw8r_script", "scripts/a.gsc"}},
                                       box(-64, -64, -16, 64, 64, 0, "tm/floor")) +
                                   ent({{"classname", "info_player_start"}, {"origin", "0 0 64"}}) +
                                   ent({{"classname", "misc_model"}, {"model", "crate_01"}, {"origin", "100 0 0"}});
    CHECK(classifyChange(fp(withoutFog), fp(withFog)) == "full");
}

TEST(build_cache_fingerprint_save_load_round_trip) {
    const fs::path dir = freshDir("fingerprint");
    const InputFingerprint a = fp(baseText());
    std::string err;
    CHECK(saveFingerprint((dir / "last.fingerprint").string(), a, &err));
    InputFingerprint back;
    CHECK(loadFingerprint((dir / "last.fingerprint").string(), back, &err));
    CHECK(back == a);
    // A damaged file is refused, not read as zeros.
    {
        std::ofstream f(dir / "bad.fingerprint", std::ios::binary);
        f << "iw8r-fingerprint 1\nworld 0000000000000001\n";
    }
    InputFingerprint junk;
    CHECK(!loadFingerprint((dir / "bad.fingerprint").string(), junk, &err));
    CHECK(!loadFingerprint((dir / "missing.fingerprint").string(), junk, &err));
}

// 7.10: the extraction folder's hash decides whether the zones of the last build can be reused.
TEST(build_cache_hash_directory) {
    namespace fs = std::filesystem;
    const fs::path a = fs::path(test::tmpDir()) / "hashdir" / "a";
    const fs::path b = fs::path(test::tmpDir()) / "hashdir" / "b";
    std::error_code ec;
    fs::remove_all(a.parent_path(), ec);
    for (const fs::path& d : {a, b}) {
        fs::create_directories(d / "sub", ec);
        std::ofstream(d / "x.json", std::ios::binary) << "{\"k\":1}";
        std::ofstream(d / "sub" / "y.dds", std::ios::binary) << "DDS data";
    }
    CHECK(hashDirectory(a.string()) != 0 && hashDirectory(a.string()) == hashDirectory(b.string()));
    std::ofstream(b / "x.json", std::ios::binary) << "{\"k\":2}";
    CHECK(hashDirectory(a.string()) != hashDirectory(b.string()));
    std::ofstream(b / "x.json", std::ios::binary) << "{\"k\":1}";
    CHECK(hashDirectory(a.string()) == hashDirectory(b.string()));
    fs::rename(b / "sub" / "y.dds", b / "sub" / "z.dds", ec);
    CHECK(hashDirectory(a.string()) != hashDirectory(b.string()));
    CHECK(hashDirectory((a / "missing").string()) == 0);
}
