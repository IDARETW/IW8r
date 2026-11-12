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
