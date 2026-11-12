#include "stock_zone_edit.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>

namespace iw8r {

namespace {

std::string lowerOf(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
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

bool StockModelZones::parse(const std::string& text) {
    zones_.clear();
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0) continue;
        // The zones are the last column.
        const size_t last = line.rfind('\t');
        std::vector<std::string> zones;
        const std::string list = line.substr(last + 1);
        for (size_t at = 0; at <= list.size();) {
            const size_t comma = list.find(',', at);
            const std::string zone = list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
            if (!zone.empty()) zones.push_back(zone);
            if (comma == std::string::npos) break;
            at = comma + 1;
        }
        zones_[lowerOf(line.substr(0, tab))] = std::move(zones);
    }
    return true;
}

bool StockModelZones::load(const std::string& tsvPath, std::string* err) {
    std::ifstream f(tsvPath, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot read " + tsvPath;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse(ss.str());
}

const std::vector<std::string>* StockModelZones::zonesOf(const std::string& model) const {
    const auto it = zones_.find(lowerOf(model));
    return it == zones_.end() ? nullptr : &it->second;
}

bool isAlwaysLoadedZone(const std::string& zone) {
    const std::string z = lowerOf(zone);
    if (z == "common_br_mp") return false;
    return z.rfind("common", 0) == 0 || z.rfind("global", 0) == 0 || z.rfind("code_", 0) == 0;
}

std::set<std::string> entityModelNames(const MapFile& map) {
    std::set<std::string> models;
    for (const Entity& e : map.entities) {
        const std::string* m = e.value("model");
        if (!m || m->empty() || (*m)[0] == '*') continue;
        models.insert(lowerOf(*m));
    }
    return models;
}

std::vector<StockEditCarry> stockEditCarryList(const MapFile& map, const std::string& zone, const StockModelZones& catalogue,
                                               std::vector<std::string>* notes, const std::set<std::string>* stockModels) {
    std::set<std::string> models;
    for (const Entity& e : map.entities) {
        const std::string* m = e.value("model");
        if (!m || m->empty() || (*m)[0] == '*') continue;
        if (stockModels && stockModels->count(lowerOf(*m))) continue;  // the stock map's own: as the stock map has it
        models.insert(*m);
    }
    const std::string own = lowerOf(zone);
    std::vector<StockEditCarry> out;
    for (const std::string& model : models) {
        const std::vector<std::string>* zones = catalogue.zonesOf(model);
        if (!zones) {
            if (notes) notes->push_back("model '" + model + "' is in no stock zone the catalogue knows; it would be missing in game");
            continue;
        }
        bool held = false;
        std::string from;
        for (const std::string& z : *zones) {
            const std::string lz = lowerOf(z);
            if (lz == own || isAlwaysLoadedZone(lz)) held = true;
            else if (from.empty() && lz.rfind("mp_", 0) == 0) from = z;
        }
        if (held) continue;
        if (from.empty()) {
            if (notes) notes->push_back("model '" + model + "' is held only by zones that cannot be read as a map zone; it would be missing in game");
            continue;
        }
        out.push_back({from, "xmodel", model});
    }
    return out;
}

std::vector<uint32_t> stockEditHiddenProps(const MapFile& map) {
    std::set<uint32_t> found;
    for (const Entity& e : map.entities) {
        if (!e.isWorldspawn()) continue;
        const std::string* v = e.value("iw8r_hide_smodels");
        if (!v) continue;
        std::istringstream in(*v);
        for (std::string token; in >> token;) {
            if (token.empty() || token.size() > 9 || !std::all_of(token.begin(), token.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
                continue;
            found.insert(static_cast<uint32_t>(std::stoul(token)));
        }
    }
    return {found.begin(), found.end()};
}

std::string stockEditJson(const std::string& entitiesPath, const std::vector<uint32_t>& hiddenProps,
                          const std::vector<StockEditCarry>& carry, const std::string* lootPoints, const std::string* placedObjects,
                          const std::vector<StockPatchOverride>* stockPatches) {
    std::string json = "{";
    bool first = true;
    const auto sep = [&] {
        if (!first) json += ",";
        first = false;
    };
    if (!entitiesPath.empty()) {
        sep();
        json += "\"entities\":" + jsonText(entitiesPath);
    }
    if (!hiddenProps.empty()) {
        sep();
        json += "\"hide_static_models\":[";
        for (size_t i = 0; i < hiddenProps.size(); ++i) json += (i ? "," : "") + std::to_string(hiddenProps[i]);
        json += "]";
    }
    if (!carry.empty()) {
        sep();
        json += "\"carry\":[";
        for (size_t i = 0; i < carry.size(); ++i)
            json += std::string(i ? "," : "") + "{\"zone\":" + jsonText(carry[i].zone) + ",\"type\":" + jsonText(carry[i].type) +
                    ",\"name\":" + jsonText(carry[i].name) + "}";
        json += "]";
    }
    if (lootPoints) {
        sep();
        json += "\"loot_points\":" + *lootPoints;
    }
    if (placedObjects && !placedObjects->empty()) {
        sep();
        json += "\"scriptable_instances\":" + *placedObjects;
    }
    if (stockPatches && !stockPatches->empty()) {
        sep();
        json += "\"stock_patches\":{";
        for (size_t i = 0; i < stockPatches->size(); ++i)
            json += std::string(i ? "," : "") + jsonText((*stockPatches)[i].name) + ":" + jsonText((*stockPatches)[i].stockFile);
        json += "}";
    }
    return json + "}";
}

}  // namespace iw8r
