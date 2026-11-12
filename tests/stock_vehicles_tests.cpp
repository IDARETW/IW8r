#include "stock_vehicles.h"
#include "test_harness.h"

#include "map_text.h"

using namespace iw8r;

namespace {
const char* kVehicles =
    "# comment\n"
    "name\tzones\talways_loaded\n"
    "atango_physics_mp\tmp_br_quarry\t0\n"
    "lbravo_physics_mp\tmp_br_quarry\t0\n"
    "veh_apache_mp\tcommon_base_mp\t1\n"
    "veh_apache_plunder_mp\tcommon_br_mp\t0\n";
const char* kModels =
    "veh8_mil_air_lbravo\tcommon_base_mp,mp_backlot2\n"
    "veh8_mil_lnd_atango_physics_mp\tmp_backlot2,mp_shipment\n";
}  // namespace

// 13.4 (research 89): vehicle spawn structs carry the stock vehicle definition and its model.
TEST(stock_vehicles_catalog_and_carry) {
    VehicleCatalog vehicles;
    CHECK(vehicles.parse(kVehicles) && vehicles.size() == 4);
    const StockVehicle* atv = vehicles.find("ATANGO_physics_mp");
    CHECK(atv && atv->carryZone() == "mp_br_quarry" && !atv->alwaysLoaded);
    const StockVehicle* bird = vehicles.find("lbravo_physics");  // the stock spawns name it without _mp
    CHECK(bird && bird->name == "lbravo_physics_mp");
    CHECK(vehicles.find("veh_apache_mp") && vehicles.find("veh_apache_mp")->carryZone().empty());
    CHECK(vehicles.find("veh_apache_plunder_mp") && vehicles.find("veh_apache_plunder_mp")->carryZone().empty());  // no map zone
    CHECK(!vehicles.find("made_up"));
    CHECK(isWarzoneStructClass("script_struct_mp_atv") && isWarzoneStructClass("SCRIPT_STRUCT_MP_ladder_top") && !isWarzoneStructClass("script_struct"));
    CHECK(stockVehicleKinds().size() == 5 && std::string(stockVehicleKinds()[0].targetname) == "atv_spawn");

    StockModelZones models;
    CHECK(models.parse(kModels));
    const std::string text =
        "iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n}\n"
        "{\n\"classname\" \"script_struct_mp_atv\"\n\"vehicletype\" \"atango_physics_mp\"\n\"model\" \"veh8_mil_lnd_atango_physics_mp\"\n\"origin\" \"0 0 0\"\n}\n"
        "{\n\"classname\" \"script_struct_mp_atv\"\n\"vehicletype\" \"atango_physics_mp\"\n\"model\" \"veh8_mil_lnd_atango_physics_mp\"\n\"origin\" \"9 0 0\"\n}\n"
        "{\n\"classname\" \"script_struct_mp_littlebird\"\n\"vehicletype\" \"lbravo_physics\"\n\"model\" \"veh8_mil_air_lbravo\"\n\"origin\" \"0 0 0\"\n}\n"
        "{\n\"classname\" \"script_struct_mp_x\"\n\"vehicletype\" \"veh_apache_mp\"\n\"model\" \"veh8_mil_air_lbravo\"\n\"origin\" \"0 0 0\"\n}\n"
        "{\n\"classname\" \"script_struct_mp_y\"\n\"vehicletype\" \"made_up\"\n\"origin\" \"0 0 0\"\n}\n"
        "{\n\"classname\" \"script_struct_mp_z\"\n\"vehicletype\" \"atango_physics_mp\"\n\"origin\" \"0 0 0\"\n}\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    std::vector<std::string> notes;
    size_t placed = 0;
    const std::vector<StockEditCarry> carry = vehicleCarryList(*pr.map, vehicles, &models, &notes, &placed);
    CHECK(placed == 6);
    // The ATV definition and model once each; the littlebird definition (its model is always loaded); nothing for the
    // always-loaded apache; the unknown type is noted, and so is the spawn without a model.
    CHECK(carry.size() == 3);
    if (carry.size() == 3) {
        CHECK((carry[0] == StockEditCarry{"mp_br_quarry", "vehicle", "atango_physics_mp"}));
        CHECK((carry[1] == StockEditCarry{"mp_br_quarry", "vehicle", "lbravo_physics_mp"}));
        CHECK((carry[2] == StockEditCarry{"mp_br_quarry", "xmodel", "veh8_mil_lnd_atango_physics_mp"}));
    }
    CHECK(notes.size() == 2);
    CHECK(carryAssetsJson(carry) ==
          "[{\"zone\":\"mp_br_quarry\",\"type\":\"vehicle\",\"name\":\"atango_physics_mp\"},"
          "{\"zone\":\"mp_br_quarry\",\"type\":\"vehicle\",\"name\":\"lbravo_physics_mp\"},"
          "{\"zone\":\"mp_br_quarry\",\"type\":\"xmodel\",\"name\":\"veh8_mil_lnd_atango_physics_mp\"}]");
    CHECK(carryAssetsJson({}) == "[]");
    // Without the model catalogue the littlebird's model is carried too (nothing says it is always loaded).
    notes.clear();
    CHECK(vehicleCarryList(*pr.map, vehicles, nullptr, &notes).size() == 4);
}
