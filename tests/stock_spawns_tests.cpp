// Stock spawn editing (6.1): spawn records as document entities and the split back into entity string + spawnList.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "iw8_mapents.h"
#include "map_text.h"
#include "stock_spawns.h"
#include "test_harness.h"

using namespace iw8r;

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
