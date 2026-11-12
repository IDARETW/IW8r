#include "stock_placed.h"
#include "test_harness.h"

#include <filesystem>
#include <fstream>

#include "map_text.h"
#include "stock_spawns.h"
#include "stock_zone_edit.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

// The instance list as ACTS writes it (the fields the reader uses): two buy stations (name references), a door.
std::string placedExport() {
    const auto inst = [](const std::string& def, const std::string& origin, const std::string& angles) {
        return "{\"initialDef\":{\"type\":\"ScriptableDef\",\"asset_reference\":true,\"name\":\"" + def + "\"},\"initialOrigin\":{\"v\":[" + origin +
               "]},\"initialAngles\":{\"v\":[" + angles + "]},\"classname\":{\"value\":7},\"flags\":0}";
    };
    return "{\"format\":\"mw19-asset-json\",\"asset\":{\"fields\":{\"scriptableMapEnts\":{\"mapInstanceCount\":3,\"mapInstances\":{\"values\":[" +
           inst(",br_plunder_box", "100,200,36", "0,90,0") + "," + inst("scriptable_door_x", "0,0,0", "0,0,0") + "," +
           inst(",br_plunder_box", "-500.5,64,8", "0,213.198,0") + "]}}}}}";
}

std::string writeTmp(const std::string& name, const std::string& text) {
    const fs::path p = fs::path(iw8r::test::tmpDir()) / name;
    std::ofstream(p, std::ios::binary) << text;
    return p.string();
}

const char* kCatalog =
    "name\tzones\talways_loaded\tparts\tserver_instanced_parts\tusable_parts\tmax_dynent_parts\tphysics_dynents\tinstance_dynent\tsparse_occluder\tcollision_flags\tphysics_limit\tflags\tmodel\tsource\tverified\tphysics_limit_detail\n"
    "br_plunder_box\tcommon_br_mp\t0\t1\t1\t1\t16\t0\t0\t0\t0\t1\t0x43E266\tkiosk_rig\tobserved\t1\t1\n"
    "scriptable_door_x\tmp_backlot2\t0\t1\t1\t1\t2\t0\t1\t1\t1\t1\t0x48E2C7\tdoor_x\tobserved\t1\t1\n"
    "brloot_rock\tcommon_br_mp\t0\t1\t0\t0\t1\t0\t0\t0\t0\t0\t0x400046\trock\tobserved\t1\t0\n"
    "com_car_x\tmp_shipment\t0\t3\t1\t0\t5\t3\t1\t0\t0\t1\t0x406046\tcar\tobserved\t1\t0\n"
    "guess_thing\tcommon_br_mp\t0\t1\t0\t0\t1\t0\t0\t0\t0\t0\t0x400046\tthing\tderived\t0\t0\n";

std::string mapWith(const std::vector<std::vector<std::pair<std::string, std::string>>>& entities) {
    std::string text = "iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n}\n";
    for (const auto& kv : entities) {
        text += "{\n";
        for (const auto& [k, v] : kv) text += "\"" + k + "\" \"" + v + "\"\n";
        text += "}\n";
    }
    return text;
}

}  // namespace

// 13.5 (research 89 section 6): the placed objects of a stock map, and which of them can be edited.
TEST(stock_placed_read_and_entities) {
    std::vector<StockPlacedObject> stock;
    std::string err;
    CHECK(readStockPlacedObjects(writeTmp("placed.json", placedExport()), stock, &err));
    CHECK(stock.size() == 3);
    if (stock.size() != 3) return;
    CHECK(stock[0].index == 0 && stock[0].definition == "br_plunder_box" && stock[0].origin == Vec3(100, 200, 36) && stock[0].angles == Vec3(0, 90, 0));
    CHECK(stock[1].definition == "scriptable_door_x" && stock[2].index == 2 && stock[2].origin == Vec3(-500.5, 64, 8));
    const auto keys = placedObjectEntityKeys(stock[2]);
    CHECK(keys.size() == 5 && keys[0].second == "scriptable" && keys[1] == std::make_pair(std::string("scriptable"), std::string("br_plunder_box")) &&
          keys[2].second == "-500.5 64 8" && keys[3].second == "0 213.198 0" && keys[4] == std::make_pair(std::string("iw8r_stock_index"), std::string("2")));

    ScriptableCatalog catalog;
    CHECK(catalog.parse(kCatalog));
    // Verified definitions are editable whatever dynamic entities their instances have (the converter copies and drops
    // them: research 89 section 8); one whose numbers are not known is not.
    CHECK(editablePlacedDefinition(*catalog.find("br_plunder_box")) && editablePlacedDefinition(*catalog.find("brloot_rock")));
    CHECK(editablePlacedDefinition(*catalog.find("scriptable_door_x")) && editablePlacedDefinition(*catalog.find("com_car_x")));
    CHECK(!editablePlacedDefinition(*catalog.find("guess_thing")) && !movablePlacedDefinition(*catalog.find("guess_thing")));

    // A map without instances, a count that does not match, a broken file.
    CHECK(readStockPlacedObjects(writeTmp("placed_none.json", "{\"asset\":{\"fields\":{\"scriptableMapEnts\":{\"mapInstanceCount\":0,\"mapInstances\":null}}}}"),
                                 stock, &err) &&
          stock.empty());
    std::string wrong = placedExport();
    wrong.replace(wrong.find("\"mapInstanceCount\":3"), 20, "\"mapInstanceCount\":4");
    CHECK(!readStockPlacedObjects(writeTmp("placed_wrong.json", wrong), stock, &err) && err.find("mapInstanceCount") != std::string::npos);
    CHECK(!readStockPlacedObjects(writeTmp("placed_broken.json", "{ not json"), stock, &err));
}

TEST(stock_placed_edits) {
    std::vector<StockPlacedObject> stock;
    CHECK(readStockPlacedObjects(writeTmp("placed.json", placedExport()), stock));
    if (stock.size() != 3) return;
    ScriptableCatalog catalog;
    CHECK(catalog.parse(kCatalog));
    const auto parse = [](const std::string& text) {
        Id next = 1;
        return parseMap(text, next);
    };

    // No scriptable entity: nothing to do, no note.
    {
        ParseResult pr = parse(mapWith({}));
        CHECK(pr.map.has_value());
        const PlacedObjectEdits e = placedObjectEdits(*pr.map, stock, catalog);
        CHECK(e.json.empty() && e.notes.empty() && !hasPlacedObjects(*pr.map));
    }
    // The two buy stations as entities, unchanged: nothing to do.
    {
        ParseResult pr = parse(mapWith({placedObjectEntityKeys(stock[0]), placedObjectEntityKeys(stock[1]), placedObjectEntityKeys(stock[2])}));
        CHECK(pr.map.has_value());
        const PlacedObjectEdits e = placedObjectEdits(*pr.map, stock, catalog);
        CHECK(e.json.empty() && e.moved == 0 && e.added == 0 && e.notes.empty() && hasPlacedObjects(*pr.map));
        // Never part of the entity text.
        std::string ents;
        std::vector<SpawnRecord> spawns;
        splitStockDocument(*pr.map, ents, spawns);
        CHECK(ents.find("br_plunder_box") == std::string::npos && ents.find("worldspawn") != std::string::npos);
    }
    // One moved, a pasted copy (same index key: a new object), a new one without the key, the other stock one deleted,
    // a new door (copied with its dynamic entity) while the stock door is deleted, an unknown name, a kind the map does
    // not have, one without an origin.
    {
        auto moved = placedObjectEntityKeys(stock[0]);
        moved[2].second = "164 200 36";
        auto pasted = placedObjectEntityKeys(stock[0]);
        pasted[2].second = "100 328 36";
        pasted[3].second = "0 45 0";
        ParseResult pr = parse(mapWith({moved,
                                        pasted,
                                        {{"classname", "scriptable"}, {"scriptable", "BR_Plunder_Box"}, {"origin", "-156 200 44"}},
                                        {{"classname", "scriptable"}, {"scriptable", "scriptable_door_x"}, {"origin", "8 8 8"}},
                                        {{"classname", "scriptable"}, {"scriptable", "made_up"}, {"origin", "1 1 1"}},
                                        {{"classname", "scriptable"}, {"scriptable", "brloot_rock"}, {"origin", "2 2 2"}},
                                        {{"classname", "scriptable"}, {"scriptable", "br_plunder_box"}},
                                        {{"classname", "scriptable"}, {"origin", "3 3 3"}}}));
        CHECK(pr.map.has_value());
        const PlacedObjectEdits e = placedObjectEdits(*pr.map, stock, catalog);
        CHECK(e.moved == 1 && e.added == 3 && e.removed == 2);
        CHECK(e.json ==
              "{\"move\":[{\"index\":0,\"origin\":[164,200,36],\"angles\":[0,90,0]}],"
              "\"add\":[{\"like\":0,\"origin\":[100,328,36],\"angles\":[0,45,0],\"server_instanced_parts\":1,\"usable_parts\":1,"
              "\"physics_limit\":true,\"physics_limit_detail\":true},"
              "{\"like\":0,\"origin\":[-156,200,44],\"angles\":[0,0,0],\"server_instanced_parts\":1,\"usable_parts\":1,"
              "\"physics_limit\":true,\"physics_limit_detail\":true},"
              "{\"like\":1,\"origin\":[8,8,8],\"angles\":[0,0,0],\"server_instanced_parts\":1,\"usable_parts\":1,"
              "\"physics_limit\":true,\"physics_limit_detail\":true}],"
              "\"remove\":[{\"index\":1,\"server_instanced_parts\":1,\"usable_parts\":1,\"physics_limit\":true,\"physics_limit_detail\":true},"
              "{\"index\":2,\"server_instanced_parts\":1,\"usable_parts\":1,\"physics_limit\":true,\"physics_limit_detail\":true}]}");
        bool counted = false, deleted = false, door = true, unknown = false, none = false, noOrigin = false, noKey = false;
        for (const std::string& n : e.notes) {
            counted |= n == "1 placed object(s) moved, 3 added, 2 removed";
            deleted = true;
            door &= n.find("scriptable_door_x") == std::string::npos;  // the door is no longer left out
            unknown |= n.find("made_up") != std::string::npos;
            none |= n.find("brloot_rock") != std::string::npos && n.find("none to copy from") != std::string::npos;
            noOrigin |= n.find("without an origin") != std::string::npos;
            noKey |= n.find("no scriptable key") != std::string::npos;
        }
        CHECK(counted && deleted && door && unknown && none && noOrigin && noKey);
        CHECK(stockEditJson("e.ents", {}, {}, nullptr, &e.json) == "{\"entities\":\"e.ents\",\"scriptable_instances\":" + e.json + "}");
        const std::string empty;
        CHECK(stockEditJson("e.ents", {}, {}, nullptr, &empty) == "{\"entities\":\"e.ents\"}");
    }
    // A stock door moved in the document is written as a move (the converter moves its dynamic entity, inside its area).
    {
        auto door = placedObjectEntityKeys(stock[1]);
        door[2].second = "64 0 0";
        ParseResult pr = parse(mapWith({door, placedObjectEntityKeys(stock[0]), placedObjectEntityKeys(stock[2])}));
        CHECK(pr.map.has_value());
        const PlacedObjectEdits e = placedObjectEdits(*pr.map, stock, catalog);
        CHECK(e.moved == 1 && e.json.find("\"move\":[{\"index\":1,\"origin\":[64,0,0]") != std::string::npos);
    }
}

// Research 89 section 8: instances with dynamic entities (a door, a car with physics entries) are copied and removed like
// the buy stations; the converter rebuilds their dynamic entities. A definition whose numbers are not known is left out,
// and its deleted instance stays.
TEST(stock_placed_dynamic_entity_copy_and_remove) {
    const auto inst = [](const std::string& def, const std::string& origin) {
        return "{\"initialDef\":{\"name\":\"" + def + "\"},\"initialOrigin\":{\"v\":[" + origin + "]},\"initialAngles\":{\"v\":[0,0,0]}}";
    };
    const std::string text = "{\"asset\":{\"fields\":{\"scriptableMapEnts\":{\"mapInstanceCount\":4,\"mapInstances\":{\"values\":[" +
                             inst("scriptable_door_x", "0,0,0") + "," + inst("com_car_x", "100,0,0") + "," + inst("guess_thing", "200,0,0") + "," +
                             inst("com_car_x", "300,0,0") + "]}}}}}";
    std::vector<StockPlacedObject> stock;
    CHECK(readStockPlacedObjects(writeTmp("placed_dyn.json", text), stock));
    if (stock.size() != 4) return;
    ScriptableCatalog catalog;
    CHECK(catalog.parse(kCatalog));
    // The door and the first car kept, the second car and the unverified thing deleted, a door and a car copied (pasted).
    auto doorCopy = placedObjectEntityKeys(stock[0]);
    doorCopy[2].second = "0 96 0";
    auto carCopy = placedObjectEntityKeys(stock[1]);
    carCopy[2].second = "100 -200 0";
    carCopy[3].second = "0 90 0";
    Id next = 1;
    ParseResult pr = parseMap(mapWith({placedObjectEntityKeys(stock[0]), placedObjectEntityKeys(stock[1]), doorCopy, carCopy,
                                       {{"classname", "scriptable"}, {"scriptable", "guess_thing"}, {"origin", "5 5 5"}}}),
                              next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const PlacedObjectEdits e = placedObjectEdits(*pr.map, stock, catalog);
    CHECK(e.moved == 0 && e.added == 2 && e.removed == 1);
    CHECK(e.json ==
          "{\"add\":[{\"like\":0,\"origin\":[0,96,0],\"angles\":[0,0,0],\"server_instanced_parts\":1,\"usable_parts\":1,"
          "\"physics_limit\":true,\"physics_limit_detail\":true},"
          "{\"like\":1,\"origin\":[100,-200,0],\"angles\":[0,90,0],\"server_instanced_parts\":1,\"usable_parts\":0,"
          "\"physics_limit\":true,\"physics_limit_detail\":false}],"
          "\"remove\":[{\"index\":3,\"server_instanced_parts\":1,\"usable_parts\":0,\"physics_limit\":true,\"physics_limit_detail\":false}]}");
    bool unknownNumbers = false, counted = false;
    for (const std::string& n : e.notes) {
        unknownNumbers |= n.find("guess_thing") != std::string::npos && n.find("not known") != std::string::npos;
        counted |= n == "0 placed object(s) moved, 2 added, 1 removed";
    }
    CHECK(unknownNumbers && counted && e.notes.size() == 2);
}
