#include "stock_zone_edit.h"
#include "test_harness.h"

#include "map_text.h"

#include <set>

using namespace iw8r;

// 11.2 / 11.4 (research 88): the edits file for the converter's editzone command.
TEST(stock_zone_edit_carry_list_and_json) {
    StockModelZones catalogue;
    CHECK(catalogue.parse("# comment\n"
                          "door_a\tmp_backlot2,mp_hideout\n"
                          "cone_b\tmp_shipment,mp_hideout\n"
                          "crate_c\tcommon_mp,mp_rust\n"
                          "loot_d\tcommon_br_mp\n"
                          "Tree_E\tmp_rust\n"));
    CHECK(catalogue.size() == 5);
    CHECK(catalogue.zonesOf("DOOR_A") && catalogue.zonesOf("door_a")->size() == 2 && !catalogue.zonesOf("nothing"));
    CHECK(isAlwaysLoadedZone("common_mp") && isAlwaysLoadedZone("global_stream_mp") && isAlwaysLoadedZone("code_post_gfx"));
    CHECK(!isAlwaysLoadedZone("common_br_mp") && !isAlwaysLoadedZone("mp_shipment") && !isAlwaysLoadedZone("techsets_mp_rust"));

    const std::string text =
        "iwmap 4\n"
        "// entity 0\n{\n\"classname\" \"worldspawn\"\n\"iw8r_hide_smodels\" \"12 3 x 12 40\"\n}\n"
        "// entity 1\n{\n\"classname\" \"script_model\"\n\"model\" \"door_a\"\n\"origin\" \"0 0 0\"\n}\n"
        "// entity 2\n{\n\"classname\" \"script_model\"\n\"model\" \"cone_b\"\n\"origin\" \"0 0 0\"\n}\n"   // the zone's own
        "// entity 3\n{\n\"classname\" \"script_model\"\n\"model\" \"crate_c\"\n\"origin\" \"0 0 0\"\n}\n"  // always loaded
        "// entity 4\n{\n\"classname\" \"script_model\"\n\"model\" \"loot_d\"\n\"origin\" \"0 0 0\"\n}\n"   // battle royale only
        "// entity 5\n{\n\"classname\" \"script_model\"\n\"model\" \"tree_e\"\n\"origin\" \"0 0 0\"\n}\n"
        "// entity 6\n{\n\"classname\" \"script_model\"\n\"model\" \"door_a\"\n\"origin\" \"8 0 0\"\n}\n"   // twice: carried once
        "// entity 7\n{\n\"classname\" \"script_model\"\n\"model\" \"made_up\"\n\"origin\" \"0 0 0\"\n}\n"
        "// entity 8\n{\n\"classname\" \"script_brushmodel\"\n\"model\" \"*3\"\n\"origin\" \"0 0 0\"\n}\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    std::vector<std::string> notes;
    const std::vector<StockEditCarry> carry = stockEditCarryList(*pr.map, "mp_shipment", catalogue, &notes);
    CHECK(carry.size() == 2);
    if (carry.size() == 2) {
        CHECK((carry[0] == StockEditCarry{"mp_backlot2", "xmodel", "door_a"}));
        CHECK((carry[1] == StockEditCarry{"mp_rust", "xmodel", "tree_e"}));
    }
    CHECK(notes.size() == 2);  // loot_d (no map zone) and made_up (unknown)
    bool sawLoot = false, sawMadeUp = false;
    for (const auto& n : notes) {
        sawLoot |= n.find("loot_d") != std::string::npos;
        sawMadeUp |= n.find("made_up") != std::string::npos && n.find("no stock zone") != std::string::npos;
    }
    CHECK(sawLoot && sawMadeUp);

    // Models the stock map's own entities name are never carried and never noted (a stock map names models it does
    // not hold, and the catalogue does not list every map): only what was added counts.
    const std::set<std::string> names = entityModelNames(*pr.map);
    CHECK(names.size() == 6 && names.count("tree_e") && names.count("door_a") && !names.count("*3"));
    const std::set<std::string> stockOwn = {"door_a", "loot_d", "made_up"};
    notes.clear();
    const std::vector<StockEditCarry> added = stockEditCarryList(*pr.map, "mp_shipment", catalogue, &notes, &stockOwn);
    CHECK(added.size() == 1 && notes.empty());
    if (added.size() == 1) CHECK((added[0] == StockEditCarry{"mp_rust", "xmodel", "tree_e"}));
    notes.clear();
    CHECK(stockEditCarryList(*pr.map, "mp_shipment", catalogue, &notes, &names).empty() && notes.empty());

    CHECK((stockEditHiddenProps(*pr.map) == std::vector<uint32_t>{3, 12, 40}));
    CHECK(stockEditJson("", {}, {}) == "{}");
    CHECK(stockEditJson("C:\\work\\edit.ents", {3, 12}, carry) ==
          "{\"entities\":\"C:\\\\work\\\\edit.ents\",\"hide_static_models\":[3,12],\"carry\":["
          "{\"zone\":\"mp_backlot2\",\"type\":\"xmodel\",\"name\":\"door_a\"},{\"zone\":\"mp_rust\",\"type\":\"xmodel\",\"name\":\"tree_e\"}]}");
    CHECK(stockEditJson("e.ents", {}, {}) == "{\"entities\":\"e.ents\"}");
}

// The shipped model catalogue names the zones of a stock door model.
TEST(stock_zone_edit_shipped_model_catalogue) {
    StockModelZones catalogue;
    std::string err;
    if (!catalogue.load("data/asset_catalog/iw8_models.tsv", &err)) {
        test::skipf("data/asset_catalog/iw8_models.tsv not found");
        return;
    }
    CHECK(catalogue.size() > 10000);
    const auto* zones = catalogue.zonesOf("door_wooden_panel_03_painted_left_mp_nophysics");
    CHECK(zones && !zones->empty());
}

// 11.5 (research 96): iw8r_remove_world brush entities become the edits file's "remove_world_surfaces" boxes.
TEST(stock_zone_edit_shared_look) {
    // The first-person look: only the techset zone, no patch file, the names kept, only the matching materials.
    const std::string json = stockSharedLookEditJson("C:\\w\\f.cso", "C:\\w\\s.cso");
    CHECK(json.rfind("{\"techsets_only\":true,\"patch\":false,\"lit_filter\":{\"program\":\"C:\\\\w\\\\f.cso\",\"screen_program\":\"C:\\\\w\\\\s.cso\",", 0) == 0);
    CHECK(json.find("\"keep_names\":true,\"materials\":\"(_vm_|/vm_|_vm$|_arm_[lr]_)\"}}") != std::string::npos);
    CHECK(json.find("\"shared\":[\"techsets_common_mp\"") != std::string::npos);
    CHECK(json.find("stock_zones") == std::string::npos);
    CHECK(json.back() == '}');
    // Without a screen program; an installed edit's kept stock file is named.
    const std::vector<StockPatchOverride> kept{{"techsets_global_stream_mp.ff", "D:\\g\\iw8r_stock\\techsets_global_stream_mp.ff"}};
    const std::string two = stockSharedLookEditJson("f.cso", "", "_vm_", &kept);
    CHECK(two.find("screen_program") == std::string::npos);
    CHECK(two.find("\"materials\":\"_vm_\"}") != std::string::npos);
    CHECK(two.find(",\"stock_zones\":{\"techsets_global_stream_mp.ff\":\"D:\\\\g\\\\iw8r_stock\\\\techsets_global_stream_mp.ff\"}}") != std::string::npos);
    CHECK(std::string(kViewmodelLookZone) == "global_stream_mp");
}

TEST(stock_zone_edit_world_removal) {
    const auto brush = [](int x0, int y0, int z0, int x1, int y1, int z1) {
        const auto f = [](const std::string& a, const std::string& b, const std::string& c) {
            return " ( " + a + " ) ( " + b + " ) ( " + c + " ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n";
        };
        const auto v = [](int x, int y, int z) { return std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z); };
        return "{\n" + f(v(x1, y1, z0), v(x0, y1, z0), v(x0, y0, z0)) + f(v(x0, y0, z1), v(x0, y1, z1), v(x1, y1, z1)) +
               f(v(x0, y0, z0), v(x0, y0, z1), v(x1, y0, z1)) + f(v(x1, y0, z0), v(x1, y0, z1), v(x1, y1, z1)) +
               f(v(x1, y1, z0), v(x1, y1, z1), v(x0, y1, z1)) + f(v(x0, y1, z0), v(x0, y1, z1), v(x0, y0, z1)) + "}\n";
    };
    // Two brushes in one entity (two boxes), an entity that keeps the collision, one that keeps everything, one
    // without brushes, and a func_group that is not a removal.
    std::string text = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n";
    text += "{\n\"classname\" \"iw8r_remove_world\"\n" + brush(-960, 1815, 18, -820, 2140, 125) + brush(0, 0, 0, 64, 32, 16) + "}\n";
    text += "{\n\"classname\" \"IW8R_Remove_World\"\n\"collision\" \"0\"\n" + brush(100, 100, 0, 200, 200, 50) + "}\n";
    text += "{\n\"classname\" \"iw8r_remove_world\"\n\"render\" \"0\"\n\"collision\" \"0\"\n" + brush(0, 0, 0, 8, 8, 8) + "}\n";
    text += "{\n\"classname\" \"iw8r_remove_world\"\n}\n";
    text += "{\n\"classname\" \"func_group\"\n" + brush(0, 0, 0, 8, 8, 8) + "}\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    CHECK(isRemoveWorldClass("iw8r_remove_world") && isRemoveWorldClass("IW8R_REMOVE_WORLD") && !isRemoveWorldClass("iw8r_loot_zone"));
    std::vector<std::string> notes;
    const std::vector<WorldRemovalBox> boxes = documentWorldRemovals(*pr.map, &notes);
    CHECK(boxes.size() == 3);
    if (boxes.size() == 3) {
        CHECK(boxes[0].mins == Vec3(-960, 1815, 18) && boxes[0].maxs == Vec3(-820, 2140, 125) && boxes[0].render && boxes[0].collision);
        CHECK(boxes[1].mins == Vec3(0, 0, 0) && boxes[1].maxs == Vec3(64, 32, 16));
        CHECK(boxes[2].render && !boxes[2].collision);
    }
    CHECK(notes.size() == 2);  // the entity without brushes, the one that keeps both

    CHECK(stockWorldRemovalRebakesSun(*pr.map));
    const std::string json = stockWorldRemovalJson(boxes, true, "C:\\acts\\acts.exe");
    CHECK(json ==
          "{\"boxes\":[{\"mins\":[-960,1815,18],\"maxs\":[-820,2140,125]},{\"mins\":[0,0,0],\"maxs\":[64,32,16]},"
          "{\"mins\":[100,100,0],\"maxs\":[200,200,50],\"collision\":false}],\"rebake_sun_shadow\":true,\"acts\":\"C:\\\\acts\\\\acts.exe\"}");
    CHECK(stockWorldRemovalJson({}, true).empty());
    // Only collision removed: the sun shadow has nothing to lose.
    WorldRemovalBox clipOnly;
    clipOnly.maxs = Vec3(1, 1, 1);
    clipOnly.render = false;
    CHECK(stockWorldRemovalJson({clipOnly}, true) == "{\"boxes\":[{\"mins\":[0,0,0],\"maxs\":[1,1,1],\"render\":false}]}");
    CHECK(stockWorldRemovalJson({boxes[0]}, false) == "{\"boxes\":[{\"mins\":[-960,1815,18],\"maxs\":[-820,2140,125]}]}");
    CHECK(stockEditJson("", {}, {}, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &json).find("\"remove_world_surfaces\":{\"boxes\"") == 1);

    const std::string keep = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n\"iw8r_remove_world_sun\" \"Keep\"\n}\n";
    ParseResult kp = parseMap(keep, next);
    CHECK(kp.map.has_value() && !stockWorldRemovalRebakesSun(*kp.map));
}
