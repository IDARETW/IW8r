#include "stock_loot.h"
#include "test_harness.h"

#include <cstring>
#include <filesystem>
#include <fstream>

#include "map_text.h"
#include "stock_spawns.h"
#include "stock_zone_edit.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

std::string hexOf(const void* data, size_t size) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        out += digits[p[i] >> 4];
        out += digits[p[i] & 15];
    }
    return out;
}

std::string pointBytes(float x, float y, float z, float pitch, float yaw, float roll, uint16_t cluster, uint16_t zone) {
    const float f[6] = {x, y, z, pitch, yaw, roll};
    const uint16_t u[2] = {cluster, zone};
    return hexOf(f, sizeof(f)) + hexOf(u, sizeof(u));
}

// The loot block as ACTS writes it (the fields the reader uses), with the points as decoded values or as raw bytes.
std::string lootExport(bool bytes) {
    const std::string vec0 = "{\"$union_views\":true,\"v\":[100.5,200,36],\"__s1\":{\"x\":100.5,\"y\":200,\"z\":36}}";
    std::string points;
    if (bytes) {
        points = "{\"type\":\"SpawnGroupPoint\",\"stride\":28,\"count\":4,\"encoding\":\"hex-little-endian\",\"bytes\":\"" +
                 pointBytes(100.5f, 200, 36, 0, 90, 0, 0, 1) + pointBytes(120, 210, 36, 0, 0, 0, 0, 1) + pointBytes(900, 900, 0, 0, 180, 0, 0xFFFD, 0) +
                 pointBytes(5000, 5000, 10, 0, 270, 0, 0xFFFB, 1) + "\"}";
    } else {
        points = "{\"type\":\"SpawnGroupPoint\",\"stride\":28,\"count\":4,\"values\":["
                 "{\"origin\":" + vec0 + ",\"angles\":{\"v\":[0,90,0]},\"cluster\":0,\"zone\":1},"
                 "{\"origin\":{\"v\":[120,210,36]},\"angles\":{\"v\":[0,0,0]},\"cluster\":0,\"zone\":1},"
                 "{\"origin\":{\"v\":[900,900,0]},\"angles\":{\"v\":[0,180,0]},\"cluster\":65533,\"zone\":0},"
                 "{\"origin\":{\"v\":[5000,5000,10]},\"angles\":{\"v\":[0,270,0]},\"cluster\":65531,\"zone\":1}]}";
    }
    return "{\"format\":\"mw19-asset-json\",\"asset\":{\"fields\":{\"spawnGroupLoot\":{\"pointCount\":4,\"points\":" + points +
           ",\"clusterCount\":1,\"clusters\":{\"values\":[{\"zone\":1,\"pointCount\":2,\"points\":{\"values\":[0,1]},"
           "\"bounds\":{\"midPoint\":{\"v\":[110,205,36]},\"halfSize\":{\"v\":[16,16,8]}}}]},"
           "\"zoneCount\":1,\"zones\":{\"values\":[{\"name\":{\"value\":2},\"pointCount\":2,\"points\":{\"values\":[0,1]},\"clusterCount\":1,"
           "\"clusters\":{\"values\":[0]},\"bounds\":{\"midPoint\":{\"v\":[100,200,40]},\"halfSize\":{\"v\":[300,300,100]}}}]}}}}}";
}

std::string writeTmp(const std::string& name, const std::string& text) {
    const fs::path p = fs::path(iw8r::test::tmpDir()) / name;
    std::ofstream(p, std::ios::binary) << text;
    return p.string();
}

}  // namespace

// 13.5 (research 89): the loot block of a stock map, from decoded values and from raw bytes.
TEST(stock_loot_read_values_and_bytes) {
    const std::string strings = writeTmp("loot_strings.json", "[\"\",\"a\",\"map_quarry\"]");
    for (const bool bytes : {false, true}) {
        const std::string path = writeTmp(bytes ? "loot_bytes.json" : "loot_values.json", lootExport(bytes));
        StockLoot loot;
        std::string err;
        CHECK(readStockLoot(path, strings, loot, &err));
        CHECK(loot.points.size() == 4 && loot.clusters.size() == 1 && loot.zones.size() == 1 && loot.zoneNames.size() == 1);
        if (loot.points.size() != 4 || loot.clusters.size() != 1 || loot.zones.size() != 1) continue;
        CHECK(loot.points[0].origin == Vec3(100.5, 200, 36) && loot.points[0].angles == Vec3(0, 90, 0) && loot.points[0].cluster == 0 &&
              loot.points[0].zone == 1);
        CHECK(loot.points[2].cluster == kLootFreePoint && loot.points[2].zone == 0);
        CHECK(loot.points[3].cluster == kLootCachePoint && loot.points[3].zone == 1 && loot.points[3].origin == Vec3(5000, 5000, 10));
        CHECK(loot.clusters[0].known && loot.clusters[0].holds(loot.points[0].origin) && loot.clusters[0].holds(loot.points[1].origin) &&
              !loot.clusters[0].holds(loot.points[2].origin));
        CHECK(loot.zones[0].known && loot.zones[0].holds(loot.points[1].origin) && !loot.zones[0].holds(loot.points[3].origin));
        CHECK(loot.zoneNames[0] == "map_quarry");
    }
    // Without script strings the names stay empty; a map without the block has no loot; a bad count is an error.
    StockLoot loot;
    std::string err;
    CHECK(readStockLoot(writeTmp("loot_values.json", lootExport(false)), "", loot, &err) && loot.zoneNames.size() == 1 && loot.zoneNames[0].empty());
    CHECK(readStockLoot(writeTmp("loot_none.json", "{\"asset\":{\"fields\":{\"spawnGroupLoot\":{\"pointCount\":0,\"points\":null,"
                                                   "\"clusterCount\":0,\"clusters\":null,\"zoneCount\":0,\"zones\":null}}}}"),
                        "", loot, &err) &&
          loot.points.empty() && loot.clusters.empty());
    CHECK(readStockLoot(writeTmp("loot_absent.json", "{\"asset\":{\"fields\":{}}}"), "", loot, &err) && loot.points.empty());
    std::string wrong = lootExport(false);
    wrong.replace(wrong.find("\"pointCount\":4"), 14, "\"pointCount\":5");
    CHECK(!readStockLoot(writeTmp("loot_wrong.json", wrong), "", loot, &err) && err.find("pointCount") != std::string::npos);
    CHECK(!readStockLoot(writeTmp("loot_broken.json", "{ not json"), "", loot, &err));
    CHECK(!readStockLoot((fs::path(iw8r::test::tmpDir()) / "loot_missing.json").string(), "", loot, &err));
}

// The points become entities and come back as the edits list: unchanged points keep their numbers, a point moved out
// of its box is picked by position again, bad numbers too, a cache point keeps the zone it is not inside.
TEST(stock_loot_entities_and_edits) {
    StockLoot loot;
    std::string err;
    CHECK(readStockLoot(writeTmp("loot_values.json", lootExport(false)), writeTmp("loot_strings.json", "[\"\",\"a\",\"map_quarry\"]"), loot, &err));
    if (loot.points.size() != 4) return;
    CHECK(isLootPointClass("iw8r_loot_point") && isLootPointClass("IW8R_Loot_Point") && !isLootPointClass("iw8r_loot") && !isLootPointClass(""));

    const auto keys = lootPointEntityKeys(loot.points[0], &loot);
    CHECK(keys.size() == 6 && keys[0] == std::make_pair(std::string("classname"), std::string("iw8r_loot_point")) &&
          keys[1].second == "100.5 200 36" && keys[2].second == "0 90 0" && keys[3] == std::make_pair(std::string("loot_cluster"), std::string("0")) &&
          keys[4] == std::make_pair(std::string("loot_zone"), std::string("1")) && keys[5].second == "map_quarry");
    CHECK(lootPointEntityKeys(loot.points[2]).size() == 5 && lootPointEntityKeys(loot.points[2])[3].second == "free");
    CHECK(lootPointEntityKeys(loot.points[3])[3].second == "cache");

    std::string text = "iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n}\n";
    const auto add = [&](const std::vector<std::pair<std::string, std::string>>& kv) {
        text += "{\n";
        for (const auto& [k, v] : kv) text += "\"" + k + "\" \"" + v + "\"\n";
        text += "}\n";
    };
    for (const LootPoint& p : loot.points) add(lootPointEntityKeys(p, &loot));
    text += "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"1 2 3\"\n}\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    CHECK(hasLootPoints(*pr.map));

    // Unchanged: the stock numbers come back, without a note.
    std::vector<std::string> notes;
    std::vector<LootPointEdit> edits = documentLootPoints(*pr.map, loot, &notes);
    CHECK(edits.size() == 4 && notes.empty());
    if (edits.size() != 4) return;
    CHECK(edits[0].origin == Vec3(100.5, 200, 36) && edits[0].angles == Vec3(0, 90, 0) && edits[0].cluster == "0" && edits[0].zone == "1");
    CHECK(edits[2].cluster == "free" && edits[2].zone == "0");
    CHECK(edits[3].cluster == "cache" && edits[3].zone == "1");  // outside the zone's box, as stock cache points are
    CHECK(lootPointsJson(edits) ==
          "[{\"origin\":[100.5,200,36],\"angles\":[0,90,0],\"cluster\":0,\"zone\":1},"
          "{\"origin\":[120,210,36],\"angles\":[0,0,0],\"cluster\":0,\"zone\":1},"
          "{\"origin\":[900,900,0],\"angles\":[0,180,0],\"cluster\":\"free\",\"zone\":0},"
          "{\"origin\":[5000,5000,10],\"angles\":[0,270,0],\"cluster\":\"cache\",\"zone\":1}]");
    CHECK(lootPointsJson({}) == "[]");

    // The entity text of the zone never holds a loot point; the spawns still split off.
    std::string ents;
    std::vector<SpawnRecord> spawns;
    splitStockDocument(*pr.map, ents, spawns);
    CHECK(ents.find("iw8r_loot_point") == std::string::npos && ents.find("worldspawn") != std::string::npos && spawns.size() == 1);
    const MapFile rest = withoutLootPoints(*pr.map);
    CHECK(rest.entities.size() == 2 && !hasLootPoints(rest));

    // Edited: point 0 moved out of its cluster (still in the zone), point 1 moved out of both, a new point without
    // keys, one with numbers the map does not have, one without an origin.
    std::string edited = "iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n}\n";
    const auto addTo = [&](const std::vector<std::pair<std::string, std::string>>& kv) {
        edited += "{\n";
        for (const auto& [k, v] : kv) edited += "\"" + k + "\" \"" + v + "\"\n";
        edited += "}\n";
    };
    addTo({{"classname", "iw8r_loot_point"}, {"origin", "300 300 36"}, {"angles", "0 90 0"}, {"loot_cluster", "0"}, {"loot_zone", "1"}});
    addTo({{"classname", "iw8r_loot_point"}, {"origin", "9000 0 0"}, {"loot_cluster", "0"}, {"loot_zone", "1"}});
    addTo({{"classname", "iw8r_loot_point"}, {"origin", "10 20 30"}});
    addTo({{"classname", "iw8r_loot_point"}, {"origin", "1 1 1"}, {"loot_cluster", "7"}, {"loot_zone", "9"}});
    addTo({{"classname", "iw8r_loot_point"}, {"angles", "0 0 0"}});
    addTo({{"classname", "iw8r_loot_point"}, {"origin", "2 2 2"}, {"loot_cluster", " Cache "}, {"loot_zone", "0"}});
    Id next2 = 1;
    ParseResult pr2 = parseMap(edited, next2);
    CHECK(pr2.map.has_value());
    if (!pr2.map) return;
    notes.clear();
    edits = documentLootPoints(*pr2.map, loot, &notes);
    CHECK(edits.size() == 5);
    if (edits.size() != 5) return;
    CHECK(edits[0].cluster == "auto" && edits[0].zone == "1");
    CHECK(edits[1].cluster == "auto" && edits[1].zone == "auto");
    CHECK(edits[2].cluster == "auto" && edits[2].zone == "auto" && edits[2].angles == Vec3(0, 0, 0));
    CHECK(edits[3].cluster == "auto" && edits[3].zone == "auto");
    CHECK(edits[4].cluster == "cache" && edits[4].zone == "0");
    CHECK(notes.size() == 5);
    const std::string json = lootPointsJson(edits);
    CHECK(json.find("{\"origin\":[300,300,36],\"angles\":[0,90,0],\"cluster\":\"auto\",\"zone\":1}") == 1);
    CHECK(json.find("\"cluster\":\"cache\",\"zone\":0}]") != std::string::npos);

    // The edits file carries the list; without it the key is absent.
    CHECK(stockEditJson("e.ents", {}, {}, &json) == "{\"entities\":\"e.ents\",\"loot_points\":" + json + "}");
    const std::string none = "[]";
    CHECK(stockEditJson("", {4}, {}, &none) == "{\"hide_static_models\":[4],\"loot_points\":[]}");
    CHECK(stockEditJson("e.ents", {}, {}).find("loot_points") == std::string::npos);
}

TEST(stock_loot_new_boxes) {
    // A cluster from two brushes (their union), a zone with a messy name, a zone without a name, a box entity without
    // brushes, and an ordinary loot point beside them.
    const auto brush = [](int x0, int y0, int z0, int x1, int y1, int z1) {
        const auto f = [](const std::string& a, const std::string& b, const std::string& c) {
            return " ( " + a + " ) ( " + b + " ) ( " + c + " ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n";
        };
        const auto v = [](int x, int y, int z) { return std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z); };
        return "{\n" + f(v(x1, y1, z0), v(x0, y1, z0), v(x0, y0, z0)) + f(v(x0, y0, z1), v(x0, y1, z1), v(x1, y1, z1)) +
               f(v(x0, y0, z0), v(x0, y0, z1), v(x1, y0, z1)) + f(v(x1, y0, z0), v(x1, y0, z1), v(x1, y1, z1)) +
               f(v(x1, y1, z0), v(x1, y1, z1), v(x0, y1, z1)) + f(v(x0, y1, z0), v(x0, y1, z1), v(x0, y0, z1)) + "}\n";
    };
    const std::string box = brush(0, 0, 0, 256, 128, 64), far = brush(1000, 0, 0, 1064, 64, 32);
    std::string text = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n";
    text += "{\n\"classname\" \"iw8r_loot_cluster\"\n" + box + far + "}\n";
    text += "{\n\"classname\" \"iw8r_loot_zone\"\n\"name\" \"Map Hospital\"\n" + box + "}\n";
    text += "{\n\"classname\" \"IW8R_LOOT_ZONE\"\n" + far + "}\n";
    text += "{\n\"classname\" \"iw8r_loot_zone\"\n\"name\" \"x\"\n}\n";
    text += "{\n\"classname\" \"iw8r_loot_point\"\n\"origin\" \"10 10 0\"\n}\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    CHECK(isLootBoxClass("iw8r_loot_cluster") && isLootBoxClass("IW8R_Loot_Zone") && !isLootBoxClass("iw8r_loot_point"));
    std::vector<std::string> notes;
    const LootBoxEdits boxes = documentLootBoxes(*pr.map, "map_quarry", &notes);
    CHECK(boxes.clusters.size() == 1 && boxes.zones.size() == 2);
    if (boxes.clusters.size() != 1 || boxes.zones.size() != 2) return;
    CHECK(boxes.clusters[0].mid == Vec3(532, 64, 32) && boxes.clusters[0].half == Vec3(532, 64, 32));  // both brushes
    CHECK(boxes.zones[0].name == "maphospital" && boxes.zones[0].mid == Vec3(128, 64, 32) && boxes.zones[0].half == Vec3(128, 64, 32));
    CHECK(boxes.zones[1].name == "map_quarry" && boxes.zones[1].mid == Vec3(1032, 32, 16));
    CHECK(notes.size() == 3);  // no brush, unnamed, renamed
    CHECK(lootBoxesJson(boxes) ==
          "{\"clusters\":[{\"mid\":[532,64,32],\"half\":[532,64,32]}],\"zones\":[{\"name\":\"maphospital\",\"mid\":[128,64,32],"
          "\"half\":[128,64,32]},{\"name\":\"map_quarry\",\"mid\":[1032,32,16],\"half\":[32,32,16]}]}");
    CHECK(lootBoxesJson({}) == "{\"clusters\":[],\"zones\":[]}");

    // Never in the entity text, never counted as brushes of the document, gone with the loot points.
    std::string ents;
    std::vector<SpawnRecord> spawns;
    std::string err;
    splitStockDocument(*pr.map, ents, spawns, &err);
    CHECK(ents.find("iw8r_loot") == std::string::npos && ents.find("IW8R_LOOT") == std::string::npos && err.empty());
    CHECK(withoutLootPoints(*pr.map).entities.size() == 1);

    // The edits file carries the boxes before the points (the converter adds boxes first either way).
    const std::string json = lootBoxesJson(boxes), pts = "[]";
    const std::string edits = stockEditJson("", {}, {}, &pts, nullptr, nullptr, nullptr, &json);
    CHECK(edits == "{\"loot_boxes\":" + json + ",\"loot_points\":[]}");
    const std::string empty;
    CHECK(stockEditJson("", {}, {}, nullptr, nullptr, nullptr, nullptr, &empty) == "{}");
}

// 11.6: the stock sun's colour and intensity from worldspawn keys, into the edits file.
TEST(stock_sun_edit_json) {
    const auto mapOf = [](const std::string& keys) {
        Id next = 1;
        ParseResult pr = parseMap("iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + keys + "}\n", next);
        return pr.map ? *pr.map : MapFile{};
    };
    std::vector<std::string> notes;
    CHECK(stockSunJson(mapOf(""), &notes).empty() && notes.empty());
    CHECK(stockSunJson(mapOf("\"iw8r_sun_color\" \"1 0.55 0.35\"\n\"iw8r_sun_intensity\" \"7.5\"\n"), &notes) ==
          "{\"color\":[1,0.55,0.35],\"intensity\":7.5}");
    CHECK(stockSunJson(mapOf("\"iw8r_sun_intensity\" \"12\"\n")) == "{\"intensity\":12}");
    notes.clear();
    CHECK(stockSunJson(mapOf("\"iw8r_sun_color\" \"red\"\n\"iw8r_sun_intensity\" \"-1\"\n"), &notes).empty() && notes.size() == 2);
    const std::string sun = "{\"intensity\":12}";
    CHECK(stockEditJson("", {}, {}, nullptr, nullptr, nullptr, nullptr, nullptr, &sun) == "{\"sun\":{\"intensity\":12}}");
}
