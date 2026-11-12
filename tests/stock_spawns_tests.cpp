// Stock spawn editing (6.1): spawn records as document entities and the split back into entity string + spawnList.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

#include "compile_iw8.h"
#include "iw8_mapents.h"
#include "model_import.h"
#include "map_text.h"
#include "stock_spawns.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {
const std::string kActsJson =
    "E:\\IW8-Radiant\\out\\m2-script-check\\acts_ents\\mw19replay\\mp_shipment\\assets\\map_ents\\maps\\mp\\mp_shipment.d3dbsp.0.asset.json";
const std::string kActsStrings = "E:\\IW8-Radiant\\out\\m2-script-check\\acts_ents\\mw19replay\\mp_shipment\\script_strings.json";
}  // namespace

TEST(stock_spawns_classes) {
    CHECK(isSpawnListClass("mp_tdm_spawn") && isSpawnListClass("MP_DOM_SPAWN_AXIS_START") && isSpawnListClass("mp_dm_spawn_start"));
    CHECK(!isSpawnListClass("info_player_start") && !isSpawnListClass("mp_global_intermission") && !isSpawnListClass("script_origin"));
}

TEST(stock_spawns_split_text_map) {
    const std::string text =
        "{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"1 2 3\"\n\"angles\" \"0 90 0\"\n"
        "\"target\" \"t1\"\n}\n{\n\"classname\" \"script_origin\"\n\"origin\" \"0 0 0\"\n}\n{\n\"classname\" \"mp_dm_spawn\"\n}\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    std::string ents, err;
    std::vector<SpawnRecord> spawns;
    splitStockDocument(*pr.map, ents, spawns, &err);
    CHECK(spawns.size() == 1 && spawns[0].classname == "mp_tdm_spawn" && spawns[0].origin.z == 3 && spawns[0].angles.y == 90 &&
          spawns[0].target == "t1");
    CHECK(ents.find("mp_tdm_spawn") == std::string::npos && ents.find("mp_dm_spawn") == std::string::npos &&
          ents.find("script_origin") != std::string::npos);
    CHECK(err.find("mp_dm_spawn has no origin") != std::string::npos);
}

// The stock mp_shipment MapEnts (ACTS dump): spawns added as entities, then split, give back the stock entity string
// byte for byte and the same 611 spawns.
// Brushes are not built into a stock map: the entity text never holds them (a helper brush for Hide Stock Props that
// was left in the document used to be written into it).
TEST(stock_spawns_split_leaves_brushes_out) {
    const std::string brush =
        "// brush 0\n{\n"
        " ( 16 16 0 ) ( -16 16 0 ) ( -16 -16 0 ) caulk 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        " ( -16 -16 32 ) ( -16 16 32 ) ( 16 16 32 ) caulk 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        " ( -16 -16 32 ) ( 16 -16 32 ) ( 16 -16 0 ) caulk 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        " ( 16 16 32 ) ( -16 16 32 ) ( -16 16 0 ) caulk 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        " ( 16 -16 32 ) ( 16 16 32 ) ( 16 16 0 ) caulk 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        " ( -16 16 32 ) ( -16 -16 32 ) ( -16 -16 0 ) caulk 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        "}\n";
    const std::string text = "iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n\"fog\" \"1\"\n" + brush + "}\n"
                             "// entity 1\n{\n\"classname\" \"func_group\"\n" + brush + "}\n"
                             "// entity 2\n{\n\"classname\" \"script_model\"\n\"model\" \"crate\"\n\"origin\" \"1 2 3\"\n}\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    size_t prims = 0;
    for (const Entity& e : pr.map->entities)
        for (const EntityItem& it : e.items) prims += it.isPrimitive() ? 1 : 0;
    CHECK(prims == 2);
    std::string ents, err;
    std::vector<SpawnRecord> spawns;
    splitStockDocument(*pr.map, ents, spawns, &err);
    CHECK(ents.find("caulk") == std::string::npos && ents.find(" ( ") == std::string::npos && ents.find("func_group") == std::string::npos);
    CHECK(ents.find("worldspawn") != std::string::npos && ents.find("\"fog\" \"1\"") != std::string::npos && ents.find("crate") != std::string::npos);
    CHECK(err.find("2 brush(es)") != std::string::npos);
    CHECK(err.find("not part of the entity text") != std::string::npos);
    // The text parses again as two entities without primitives.
    Id next2 = 1;
    ParseResult again = parseMap(ents, next2);
    CHECK(again.map.has_value() && again.map->entities.size() == 2);
}

TEST(stock_spawns_shipment_round_trip) {
    if (!std::filesystem::exists(kActsJson)) {
        test::skipf("  skip (no %s)\n", kActsJson.c_str());
        return;
    }
    Iw8MapEntsDump dump;
    std::string err;
    CHECK(loadActsMapEnts(kActsJson, kActsStrings, dump, &err));
    if (dump.spawns.empty()) return;
    Id next = 1;
    ParseResult pr = parseMap(dump.entityString, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    MapFile edited = *pr.map;
    std::string withSpawns = edited.text();
    for (const auto& s : dump.spawns) {
        std::string e = "{\n";
        for (const auto& [k, v] : spawnEntityKeys(s)) e += "\"" + k + "\" \"" + v + "\"\n";
        e += "}\n";
        withSpawns.insert(withSpawns.size() - (withSpawns.back() == '\0' ? 1 : 0), e);
    }
    ParseResult pr2 = parseMap(withSpawns, next);
    CHECK(pr2.map.has_value());
    if (!pr2.map) return;
    std::string ents;
    std::vector<SpawnRecord> spawns;
    splitStockDocument(*pr2.map, ents, spawns, &err);
    CHECK(ents == dump.entityString);
    CHECK(spawns.size() == dump.spawns.size());
    size_t mismatched = 0;
    for (size_t i = 0; i < spawns.size() && i < dump.spawns.size(); ++i) {
        const auto& a = spawns[i];
        const auto& b = dump.spawns[i];
        if (a.classname != b.name || a.target != b.target || a.script_noteworthy != b.noteworthy || float(a.origin.x) != float(b.origin.x) ||
            float(a.origin.y) != float(b.origin.y) || float(a.origin.z) != float(b.origin.z) || float(a.angles.y) != float(b.angles.y))
        {
            if (!mismatched)
                std::printf("  first mismatch %zu: '%s'/'%s' t '%s'/'%s' n '%s'/'%s' o %.9g %.9g %.9g / %.9g %.9g %.9g a %.9g/%.9g\n", i,
                            a.classname.c_str(), b.name.c_str(), a.target.c_str(), b.target.c_str(), a.script_noteworthy.c_str(),
                            b.noteworthy.c_str(), a.origin.x, a.origin.y, a.origin.z, b.origin.x, b.origin.y, b.origin.z, a.angles.y,
                            b.angles.y);
            ++mismatched;
        }
    }
    CHECK(mismatched == 0);
}

// 11.4 (research 88): the brushes added to a stock map as model files: one per worldspawn / func_group, centred,
// Y-up OBJ that the model importer reads back with the same triangles; tool-only brushes and other brush entities
// are left out with a note.
TEST(stock_overlay_models_from_brushes) {
    const auto box = [](int x0, int y0, int z0, int x1, int y1, int z1, const std::string& m) {
        const auto v = [](int a, int b, int c) { return "( " + std::to_string(a) + " " + std::to_string(b) + " " + std::to_string(c) + " )"; };
        const std::string s = " " + m + " 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n";
        return "{\n " + v(x1, y1, z0) + " " + v(x0, y1, z0) + " " + v(x0, y0, z0) + s + " " + v(x0, y0, z1) + " " + v(x0, y1, z1) + " " + v(x1, y1, z1) + s +
               " " + v(x0, y0, z1) + " " + v(x1, y0, z1) + " " + v(x1, y0, z0) + s + " " + v(x1, y1, z1) + " " + v(x0, y1, z1) + " " + v(x0, y1, z0) + s +
               " " + v(x1, y0, z1) + " " + v(x1, y1, z1) + " " + v(x1, y1, z0) + s + " " + v(x0, y1, z1) + " " + v(x0, y0, z1) + " " + v(x0, y0, z0) + s + "}\n";
    };
    const std::string text = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + box(100, 0, 0, 164, 64, 64, "tm2co/me_curb_stone_01") +
                             box(0, 0, 0, 16, 16, 16, "caulk") + "}\n" + "{\n\"classname\" \"func_group\"\n" + box(-64, -64, 0, 0, 0, 32, "pfm_wall") + "}\n" +
                             "{\n\"classname\" \"trigger_multiple\"\n" + box(0, 0, 0, 8, 8, 8, "trigger") + "}\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    std::vector<std::string> notes;
    const std::vector<StockOverlayModel> models = stockOverlayModels(*pr.map, 4, &notes);
    CHECK(models.size() == 2);
    if (models.size() != 2) return;
    CHECK(models[0].entity == 0 && models[0].centre == Vec3(132, 32, 32) && models[0].triangles == 12 && models[0].materials.size() == 1 &&
          models[0].materials.count("tm2co/me_curb_stone_01"));
    CHECK(models[1].entity == 1 && models[1].centre == Vec3(-32, -32, 16) && models[1].triangles == 12);
    CHECK(notes.size() == 2);
    // The importer reads it back: 12 triangles, centred (the box spans -32..32 on X and, Y-up, -32..32 on Z).
    const fs::path file = fs::path(iw8r::test::tmpDir()) / "overlay_box.obj";
    std::ofstream(file, std::ios::binary) << models[0].obj;
    ImportedModel im;
    std::string err;
    CHECK(importModelFile(file.string(), im, &err));
    size_t tris = 0;
    for (const auto& mesh : im.meshes) tris += mesh.idx.size() / 3;
    CHECK(tris == 12 && im.meshes.size() == 1 && im.meshes[0].material == "tm2co/me_curb_stone_01");
    CHECK(im.bounds.valid() && std::abs(im.bounds.mins.x + 32) < 1e-6 && std::abs(im.bounds.maxs.x - 32) < 1e-6 && std::abs(im.bounds.maxs.y - 32) < 1e-6);
}
