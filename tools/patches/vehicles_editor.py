import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/core/stock_vehicles.cpp', [
 ('''        return a.type != b.type ? a.type > b.type : a.name < b.name;  // "xmodel" after "vehicle"
''', '''        return a.type != b.type ? a.type < b.type : a.name < b.name;  // "vehicle" before "xmodel"
'''),
])
patch('src/core/compile_iw8.h', [
 ('#include "collision_layout.h"\n', '#include "collision_layout.h"\n#include "stock_vehicles.h"\n'),
 ('''    CollisionLayout collisionLayout = CollisionLayout::Auto;
''', '''    CollisionLayout collisionLayout = CollisionLayout::Auto;
    // Stock vehicles (stock_vehicles.h): a vehicle spawn struct (script_struct_mp_atv, ...) makes the build carry the
    // vehicle definition and its model from the stock Warzone map that holds them. Without the catalogue the structs
    // are written and nothing is carried (warned). Not owned.
    const VehicleCatalog* vehicles = nullptr;
    const StockModelZones* stockModels = nullptr;
'''),
])
patch('src/core/compile_iw8.cpp', [
 ('''    return known.count(lcls) || startsWith(lcls, "trigger_multiple_bcs_");
''', '''    // script_struct_mp_*: the structs of the stock Warzone maps (vehicle spawns, spawn selection, ladder tops).
    return known.count(lcls) || startsWith(lcls, "trigger_multiple_bcs_") || isWarzoneStructClass(lcls);
'''),
 ('''    // Dynamic props (6.15, research 41): `dynamic_prop` point entities (model, origin, angles, mass in kg, material
''', '''    // Vehicles (13.4, research 89): vehicle spawn structs make the converter carry the stock vehicle definition and
    // its model (collision json carry_assets).
    std::string carryAssets;
    {
        std::vector<std::string> notes;
        size_t placed = 0;
        const std::vector<StockEditCarry> carry =
            opt.vehicles ? vehicleCarryList(map, *opt.vehicles, opt.stockModels, &notes, &placed) : std::vector<StockEditCarry>{};
        if (!opt.vehicles)
            for (const Entity* pe : points)
                if (const std::string* v = pe->value("vehicletype"); v && !v->empty()) ++placed;
        for (const std::string& n : notes) warn.push_back(n);
        if (placed) {
            bool brMap = worldspawn && worldspawn->value("iw8r_br");
            for (const Entity* pe : points)
                if (const std::string* tn = pe->value("targetname"); tn && *tn == "br_prematch_insertion_point") brMap = true;
            if (!opt.vehicles)
                warn.push_back(std::to_string(placed) + " vehicle spawn(s): the vehicle catalogue (data/asset_catalog/iw8_vehicles.tsv) was not "
                               "found, so no vehicle is carried into the map");
            else
                warn.push_back(std::to_string(placed) + " vehicle spawn(s); " + std::to_string(carry.size()) +
                               " stock asset(s) carried for them (definitions and models). Not seen in game");
            if (!brMap)
                warn.push_back("vehicle spawns are used by the Warzone (br) scripts only: make the map a Warzone map (Tools > Game Modes) or "
                               "the vehicles do not appear");
        }
        if (!carry.empty()) carryAssets = carryAssetsJson(carry);
    }
    // Dynamic props (6.15, research 41): `dynamic_prop` point entities (model, origin, angles, mass in kg, material
'''),
 ('''           (lootCount ? ",\\"loot_points\\":[" + lootJson + "]" : std::string()) + (collisionTiles ? ",\\"collision_tiles\\":true" : "") + "}";
''', '''           (lootCount ? ",\\"loot_points\\":[" + lootJson + "]" : std::string()) + (collisionTiles ? ",\\"collision_tiles\\":true" : "") +
           (carryAssets.empty() ? std::string() : ",\\"carry_assets\\":" + carryAssets) + "}";
'''),
])
patch('src/qtapp/compile_panel.cpp', [
 ('''        opt.collisionLayout = parseCollisionLayout(Settings::instance().value("build/collisionLayout", "auto").toString().toStdString());
''', '''        opt.collisionLayout = parseCollisionLayout(Settings::instance().value("build/collisionLayout", "auto").toString().toStdString());
        // 13.4: stock vehicles (and the stock model catalogue that says which vehicle models every match has).
        static VehicleCatalog vehicleCatalog;
        static StockModelZones modelZones;
        static bool catalogsLoaded = false;
        if (!catalogsLoaded) {
            catalogsLoaded = true;
            QDir d(QCoreApplication::applicationDirPath());
            for (int up = 0; up < 4; ++up, d.cdUp())
                if (QFileInfo::exists(d.filePath("data/asset_catalog/iw8_vehicles.tsv"))) {
                    vehicleCatalog.load(d.filePath("data/asset_catalog/iw8_vehicles.tsv").toStdString());
                    modelZones.load(d.filePath("data/asset_catalog/iw8_models.tsv").toStdString());
                    break;
                }
        }
        if (vehicleCatalog.size()) opt.vehicles = &vehicleCatalog;
        if (modelZones.size()) opt.stockModels = &modelZones;
'''),
])
print('ok')
