// Asset usage (roadmap 12.9): map references (collectAssetUsage) and the mapper's own asset files (findOwnAssetFiles).
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "asset_usage.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

// One brush face using `mat`.
std::string face(const std::string& mat) {
    return " ( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) " + mat + " 0 0 0 1 1 0 0 0 0 0\n";
}

// A brush block with one face per material.
std::string brush(const std::vector<std::string>& mats) {
    std::string s = "{\n";
    for (const std::string& m : mats) s += face(m);
    return s + "}\n";
}

// A one-patch primitive using `mat`.
std::string patch(const std::string& mat) {
    return "{\n mesh\n {\n  " + mat +
           "\n  lightmap_gray\n  2 2 16 8\n  (\n\tv 0 0 0 t 0 0 0 0\n\tv 0 0 64 t 0 -64 0 4\n  )\n"
           "  (\n\tv 64 0 0 t 64 0 4 0\n\tv 64 0 64 t 64 -64 4 4\n  )\n }\n}\n";
}

// A key/value line.
std::string kv(const std::string& key, const std::string& value) { return "\"" + key + "\" \"" + value + "\"\n"; }

// An entity block holding the given lines and primitives.
std::string entity(const std::string& body) { return "{\n" + body + "}\n"; }

MapFile parse(const std::string& text) {
    Id next = 1;
    ParseResult pr = parseMap("iwmap 4\n" + text, next);
    CHECK(pr.map.has_value());
    return pr.map ? std::move(*pr.map) : MapFile{};
}

const AssetUse* find(const std::vector<AssetUse>& list, const std::string& name) {
    for (const AssetUse& u : list)
        if (u.name == name) return &u;
    return nullptr;
}

// Scratch folder under out/agent_usage_test, removed when the test ends.
struct Scratch {
    fs::path dir;
    explicit Scratch(const std::string& name) {
        fs::path p = fs::current_path();
        fs::path root = fs::current_path() / "out" / "agent_usage_test";
        for (int up = 0; up < 6; ++up, p = p.parent_path()) {
            if (fs::exists(p / "xmake.lua")) {
                root = p / "out" / "agent_usage_test";
                break;
            }
        }
        dir = root / name;
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::string sub(const std::string& rel) const {
        const fs::path p = dir / rel;
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        return p.string();
    }
};

void writeText(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary);
    f << text;
}

}  // namespace

TEST(asset_usage_counts_brush_faces_and_patches) {
    const MapFile map = parse(entity(kv("classname", "worldspawn") + brush({"mat_a", "mat_a", "mat_b"}) + brush({"mat_a"}) +
                                     patch("mat_p")));
    const MapAssetUsage u = collectAssetUsage(map);
    CHECK(u.materials.size() == 3);
    if (u.materials.size() != 3) return;
    CHECK(u.materials[0].name == "mat_a" && u.materials[0].count == 3);
    CHECK(u.materials[1].name == "mat_b" && u.materials[1].count == 1);
    CHECK(u.materials[2].name == "mat_p" && u.materials[2].count == 1);
}

TEST(asset_usage_counts_material_keys) {
    const MapFile map = parse(entity(kv("classname", "info_thing") + kv("iw8r_material", "mat_key") +
                                     kv("iw8r_mat:0", "mat_slot") + kv("iw8r_mat:1", "mat_slot") +
                                     kv("iw8r_mat:2", "")) +
                              entity(kv("classname", "info_other") + kv("iw8r_material", "mat_key")));
    const MapAssetUsage u = collectAssetUsage(map);
    const AssetUse* slot = find(u.materials, "mat_slot");
    const AssetUse* key = find(u.materials, "mat_key");
    CHECK(slot && slot->count == 2);
    CHECK(key && key->count == 2);
    CHECK(!find(u.materials, ""));  // an empty key value is not a name
}

TEST(asset_usage_names_are_case_insensitive) {
    const MapFile map = parse(entity(kv("classname", "worldspawn") + brush({"Mat_A", "mat_a", "MAT_A"})) +
                              entity(kv("classname", "misc_model") + kv("model", "Models/Rock")) +
                              entity(kv("classname", "misc_model") + kv("model", "models/rock")));
    const MapAssetUsage u = collectAssetUsage(map);
    CHECK(u.materials.size() == 1 && u.materials[0].name == "mat_a" && u.materials[0].count == 3);
    CHECK(u.models.size() == 1 && u.models[0].name == "models/rock" && u.models[0].count == 2);
}

TEST(asset_usage_models_and_prefabs) {
    const MapFile map = parse(entity(kv("classname", "worldspawn")) +
                              entity(kv("classname", "misc_model") + kv("model", "models/rock")) +
                              entity(kv("classname", "script_model") + kv("model", "models/rock")) +
                              entity(kv("classname", "dynamic_prop") + kv("model", "*0")) +
                              entity(kv("classname", "dynamic_prop") + kv("model", "models/crate")) +
                              entity(kv("classname", "misc_prefab") + kv("model", "prefabs/tower")) +
                              entity(kv("classname", "light") + kv("model", "models/ignored")));
    const MapAssetUsage u = collectAssetUsage(map);
    CHECK(u.models.size() == 2);
    CHECK(find(u.models, "models/rock") && find(u.models, "models/rock")->count == 2);
    CHECK(find(u.models, "models/crate") && find(u.models, "models/crate")->count == 1);
    CHECK(!find(u.models, "*0") && !find(u.models, "models/ignored"));
    CHECK(u.prefabs.size() == 1 && u.prefabs[0].name == "prefabs/tower" && u.prefabs[0].count == 1);
    CHECK(u.effects.empty());
}

TEST(asset_usage_effects_and_sounds) {
    const MapFile map = parse(entity(kv("classname", "fx_origin") + kv("fx", "fx/sparks") + kv("soundalias", "snd_spark")) +
                              entity(kv("classname", "fx_origin") + kv("fx", "fx/sparks") + kv("soundalias", "snd_spark")) +
                              entity(kv("classname", "sound_emitter") + kv("soundalias", "snd_hum")) +
                              entity(kv("classname", "info_other") + kv("soundalias", "snd_not_counted")));
    const MapAssetUsage u = collectAssetUsage(map);
    CHECK(u.effects.size() == 1 && u.effects[0].name == "fx/sparks" && u.effects[0].count == 2);
    CHECK(find(u.sounds, "snd_spark") && find(u.sounds, "snd_spark")->count == 2);
    CHECK(find(u.sounds, "snd_hum") && find(u.sounds, "snd_hum")->count == 1);
    CHECK(!find(u.sounds, "snd_not_counted"));
}

TEST(asset_usage_worldspawn_ambient_sounds) {
    const MapFile map = parse(entity(kv("classname", "worldspawn") + kv("iw8r_ambient", "amb_loop") +
                                     kv("iw8r_ambient_oneshots", "5 10 drip_a:2 drip_b drip_c:0.5")));
    const MapAssetUsage u = collectAssetUsage(map);
    CHECK(find(u.sounds, "amb_loop") && find(u.sounds, "amb_loop")->count == 1);
    CHECK(find(u.sounds, "drip_a") && find(u.sounds, "drip_a")->count == 1);
    CHECK(find(u.sounds, "drip_b") && find(u.sounds, "drip_c"));
    CHECK(!find(u.sounds, "5") && !find(u.sounds, "10"));  // the min and max seconds are skipped
    CHECK(!find(u.sounds, "drip_a:2"));                     // weights are stripped
    CHECK(u.sounds.size() == 4);
}

TEST(asset_usage_sorts_by_count_then_name) {
    const MapFile map = parse(entity(kv("classname", "worldspawn") +
                                     brush({"b", "b", "a", "a", "c", "c", "c"})));
    const MapAssetUsage u = collectAssetUsage(map);
    CHECK(u.materials.size() == 3);
    if (u.materials.size() != 3) return;
    CHECK(u.materials[0].name == "c" && u.materials[0].count == 3);
    CHECK(u.materials[1].name == "a" && u.materials[1].count == 2);
    CHECK(u.materials[2].name == "b" && u.materials[2].count == 2);
}

TEST(asset_usage_finds_own_materials) {
    Scratch s("materials");
    writeText(s.sub("mats/stone.iw8mat.json"), "{\"name\": \"Stone_A\", \"surface\": \"stone\"}");
    writeText(s.sub("mats/deep/metal.iw8mat.json"), "{\"surface\": \"metal\"}");  // no name: file stem used
    writeText(s.sub("mats/notes.json"), "{\"name\": \"not_a_material\"}");
    MapAssetUsage usage;
    usage.materials.push_back({"stone_a", 2});

    const std::vector<OwnAssetFile> files = findOwnAssetFiles(usage, {s.sub("mats")}, {}, {}, {});
    CHECK(files.size() == 2);
    if (files.size() != 2) return;
    for (const OwnAssetFile& f : files) {
        CHECK(f.kind == "material");
        if (f.name == "Stone_A") CHECK(f.used);
        if (f.name == "metal") CHECK(!f.used);
    }
}

TEST(asset_usage_finds_own_models_sounds_and_effects) {
    Scratch s("assets");
    writeText(s.sub("models/rock.obj"), "");
    writeText(s.sub("models/crate.GLB"), "");
    writeText(s.sub("models/readme.txt"), "");
    writeText(s.sub("sounds/snd_hum.wav"), "");
    writeText(s.sub("sounds/other.mp3"), "");
    writeText(s.sub("fx/sub/sparks.iw8fx.json"), "{}");
    writeText(s.sub("fx/plain.json"), "{}");

    MapAssetUsage usage;
    usage.models.push_back({"rock", 1});
    usage.sounds.push_back({"snd_hum", 1});
    usage.effects.push_back({"sub/sparks", 1});

    const std::vector<OwnAssetFile> files =
        findOwnAssetFiles(usage, {}, {s.sub("models")}, {s.sub("sounds")}, {s.sub("fx")});
    int models = 0, sounds = 0, effects = 0;
    for (const OwnAssetFile& f : files) {
        if (f.kind == "model") {
            ++models;
            CHECK(f.used == (f.name == "rock"));
        } else if (f.kind == "sound") {
            ++sounds;
            CHECK(f.name == "snd_hum" && f.used);
        } else if (f.kind == "effect") {
            ++effects;
            CHECK(f.name == "sub/sparks" && f.used);
        }
    }
    CHECK(models == 2);  // rock.obj and crate.GLB, not the .txt
    CHECK(sounds == 1);  // the .mp3 is ignored
    CHECK(effects == 1); // fx/plain.json is not an .iw8fx.json
}

TEST(asset_usage_missing_roots_are_empty) {
    Scratch s("missing");
    const MapAssetUsage usage;
    const std::vector<OwnAssetFile> files = findOwnAssetFiles(usage, {s.sub("nope")}, {s.sub("nope")}, {}, {});
    CHECK(files.empty());
}
