// IW8 Radiant - drivable stock vehicles on a map (roadmap 13.4, docs/research/89_warzone_work.md "Vehicles").
//
// The stock Warzone maps place vehicles with structs in their entity text: script_struct_mp_atv, _tacrover, _jeep,
// _cargotruck and _littlebird, each with `model`, `vehicletype` and targetname `<kind>_spawn`; the battle royale
// scripts spawn the vehicle there. The drivable vehicle definitions live in the map zones of mp_br_quarry and
// mp_donetsk, not in a zone every match loads, and a vehicle definition does not name its model (script spawns the
// model by name with the type). So a map that places one carries both the definition and the model from the stock
// zone (the converter's collision JSON "carry_assets").
// Pure core code.
#pragma once

#include <string>
#include <vector>

#include "map_text.h"
#include "stock_zone_edit.h"

namespace iw8r {

struct StockVehicle {
    std::string name;         // vehicle definition, e.g. atango_physics_mp
    std::vector<std::string> zones;
    bool alwaysLoaded = false;  // in common_base_mp
    // The stock zone the definition is carried from ("" when it is always loaded or no map zone holds it).
    std::string carryZone() const;
};

// data/asset_catalog/iw8_vehicles.tsv: name, comma list of zones, always_loaded.
class VehicleCatalog {
public:
    bool parse(const std::string& text);
    bool load(const std::string& tsvPath, std::string* err = nullptr);
    // Exact name, else the name with "_mp" added (stock littlebird spawns say "lbravo_physics" for lbravo_physics_mp).
    // Case-insensitive. Null when neither is known.
    const StockVehicle* find(const std::string& vehicleType) const;
    size_t size() const { return vehicles_.size(); }

private:
    std::vector<StockVehicle> vehicles_;
};

// True for the struct classes of the stock Warzone maps (script_struct_mp_<anything>), which a build accepts.
bool isWarzoneStructClass(const std::string& classname);

// The stock vehicle kinds as the stock maps place them: class, vehicle type, model, spawn targetname.
struct StockVehicleKind {
    const char* classname;
    const char* vehicleType;
    const char* model;
    const char* targetname;
};
const std::vector<StockVehicleKind>& stockVehicleKinds();

// What a map that places vehicles must carry: for every entity with a `vehicletype` key, the definition (unless it is
// always loaded) and the entity's `model` (unless a zone every match loads holds it, by `models`), both from the
// definition's stock zone. Each asset once, sorted by type then name. `notes` gets one line per vehicle type that no
// stock zone holds, and per entity without a model.
std::vector<StockEditCarry> vehicleCarryList(const MapFile& map, const VehicleCatalog& vehicles, const StockModelZones* models,
                                             std::vector<std::string>* notes = nullptr, size_t* placed = nullptr);

// The collision JSON list: [{"zone":...,"type":...,"name":...}, ...].
std::string carryAssetsJson(const std::vector<StockEditCarry>& carry);

}  // namespace iw8r
