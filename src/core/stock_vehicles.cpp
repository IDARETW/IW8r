#include "stock_vehicles.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace iw8r {

namespace {

std::string lowerOf(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string jsonText(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<unsigned char>(c) < 0x20) continue;
        out += c;
    }
    return out + "\"";
}

}  // namespace

std::string StockVehicle::carryZone() const {
    if (alwaysLoaded) return {};
    for (const std::string& z : zones)
        if (z.rfind("mp_", 0) == 0) return z;
    return {};
}

bool VehicleCatalog::parse(const std::string& text) {
    vehicles_.clear();
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f;
        for (size_t at = 0;;) {
            const size_t tab = line.find('\t', at);
            f.push_back(line.substr(at, tab == std::string::npos ? std::string::npos : tab - at));
            if (tab == std::string::npos) break;
            at = tab + 1;
        }
        if (f.size() < 3 || f[0].empty() || f[0] == "name") continue;
        StockVehicle v;
        v.name = f[0];
        for (size_t at = 0; at < f[1].size();) {
            const size_t comma = f[1].find(',', at);
            const std::string z = f[1].substr(at, comma == std::string::npos ? std::string::npos : comma - at);
            if (!z.empty()) v.zones.push_back(z);
            if (comma == std::string::npos) break;
            at = comma + 1;
        }
        v.alwaysLoaded = f[2] == "1";
        vehicles_.push_back(std::move(v));
    }
    return !vehicles_.empty();
}

bool VehicleCatalog::load(const std::string& tsvPath, std::string* err) {
    std::ifstream f(tsvPath, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot read " + tsvPath;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse(ss.str());
}

const StockVehicle* VehicleCatalog::find(const std::string& vehicleType) const {
    const std::string key = lowerOf(vehicleType);
    for (const std::string& candidate : {key, key + "_mp"})
        for (const StockVehicle& v : vehicles_)
            if (lowerOf(v.name) == candidate) return &v;
    return nullptr;
}

bool isWarzoneStructClass(const std::string& classname) { return lowerOf(classname).rfind("script_struct_mp_", 0) == 0; }

const std::vector<StockVehicleKind>& stockVehicleKinds() {
    // As mp_br_quarry and mp_donetsk place them (entity text, research 89).
    static const std::vector<StockVehicleKind> kinds = {
        {"script_struct_mp_atv", "atango_physics_mp", "veh8_mil_lnd_atango_physics_mp", "atv_spawn"},
        {"script_struct_mp_tacrover", "tromeo_physics_mp", "veh8_mil_lnd_tromeo_physics_mp", "tacrover_spawn"},
        {"script_struct_mp_jeep", "decho_physics_mp", "veh8_civ_lnd_decho_vm_dirty_blue_physics_mp", "jeep_spawn"},
        {"script_struct_mp_cargotruck", "mkilo_physics_mp", "veh8_mil_lnd_mkilo23_physics_mp", "cargotruck_spawn"},
        {"script_struct_mp_littlebird", "lbravo_physics", "veh8_mil_air_lbravo", "littlebird_spawn"},
    };
    return kinds;
}

std::vector<StockEditCarry> vehicleCarryList(const MapFile& map, const VehicleCatalog& vehicles, const StockModelZones* models,
                                             std::vector<std::string>* notes, size_t* placed) {
    std::set<std::tuple<std::string, std::string, std::string>> want;  // type, name, zone
    std::set<std::string> unknown;
    size_t count = 0, noModel = 0;
    for (const Entity& e : map.entities) {
        const std::string* type = e.value("vehicletype");
        if (!type || type->empty()) continue;
        ++count;
        const StockVehicle* v = vehicles.find(*type);
        if (!v) {
            unknown.insert(*type);
            continue;
        }
        const std::string zone = v->carryZone();
        if (!v->alwaysLoaded) {
            if (zone.empty()) {
                unknown.insert(*type);
                continue;
            }
            want.insert({"vehicle", v->name, zone});
        }
        const std::string* model = e.value("model");
        if (!model || model->empty() || (*model)[0] == '*') {
            ++noModel;
            continue;
        }
        bool held = false;
        if (models)
            if (const std::vector<std::string>* zones = models->zonesOf(*model))
                for (const std::string& z : *zones) held |= isAlwaysLoadedZone(z);
        if (!held) {
            // The model comes from the definition's zone; an always-loaded definition's model from the first stock map
            // zone the model catalogue names.
            std::string from = zone;
            if (from.empty() && models)
                if (const std::vector<std::string>* zones = models->zonesOf(*model))
                    for (const std::string& z : *zones)
                        if (lowerOf(z).rfind("mp_", 0) == 0) {
                            from = z;
                            break;
                        }
            if (from.empty()) {
                if (notes) notes->push_back("vehicle model '" + *model + "' is in no stock map zone the catalogues know; it would be missing in game");
                continue;
            }
            want.insert({"xmodel", *model, from});
        }
    }
    if (placed) *placed = count;
    if (notes) {
        for (const std::string& u : unknown)
            notes->push_back("vehicle type '" + u + "' is not a stock vehicle a map can carry (data/asset_catalog/iw8_vehicles.tsv)");
        if (noModel) notes->push_back(std::to_string(noModel) + " vehicle spawn(s) have no model key: the vehicle has no model to spawn");
    }
    std::vector<StockEditCarry> out;
    for (const auto& [type, name, zone] : want) out.push_back({zone, type, name});
    std::sort(out.begin(), out.end(), [](const StockEditCarry& a, const StockEditCarry& b) {
        return a.type != b.type ? a.type < b.type : a.name < b.name;  // "vehicle" before "xmodel"
    });
    return out;
}

std::string carryAssetsJson(const std::vector<StockEditCarry>& carry) {
    std::string json = "[";
    for (size_t i = 0; i < carry.size(); ++i)
        json += std::string(i ? "," : "") + "{\"zone\":" + jsonText(carry[i].zone) + ",\"type\":" + jsonText(carry[i].type) + ",\"name\":" +
                jsonText(carry[i].name) + "}";
    return json + "]";
}

}  // namespace iw8r
