#include "stock_zone_edit.h"

#include <cstdio>
#include <cstdlib>

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
                          const std::vector<StockPatchOverride>* stockPatches, const std::vector<StockPropCollision>* removeCollision,
                          const std::string* lootBoxes, const std::string* sun, const std::string* replaceMaterials) {
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
                    ",\"name\":" + jsonText(carry[i].name) + (carry[i].folder.empty() ? std::string() : ",\"folder\":" + jsonText(carry[i].folder)) + "}";
        json += "]";
    }
    if (replaceMaterials && !replaceMaterials->empty()) {
        sep();
        json += "\"replace_materials\":" + *replaceMaterials;
    }
    if (sun && !sun->empty()) {
        sep();
        json += "\"sun\":" + *sun;
    }
    if (lootBoxes && !lootBoxes->empty()) {
        sep();
        json += "\"loot_boxes\":" + *lootBoxes;
    }
    if (lootPoints) {
        sep();
        json += "\"loot_points\":" + *lootPoints;
    }
    if (placedObjects && !placedObjects->empty()) {
        sep();
        json += "\"scriptable_instances\":" + *placedObjects;
    }
    if (removeCollision && !removeCollision->empty()) {
        sep();
        json += "\"remove_static_model_collision\":[";
        for (size_t i = 0; i < removeCollision->size(); ++i) {
            const StockPropCollision& c = (*removeCollision)[i];
            json += std::string(i ? "," : "") + "{\"model\":" + jsonText(c.model) + ",\"origin\":[" + formatNumber(c.origin[0]) + "," +
                    formatNumber(c.origin[1]) + "," + formatNumber(c.origin[2]) + "]}";
        }
        json += "]";
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

std::string stockSunJson(const MapFile& map, std::vector<std::string>* notes) {
    const Entity* world = nullptr;
    for (const Entity& e : map.entities)
        if (e.isWorldspawn()) {
            world = &e;
            break;
        }
    if (!world) return {};
    std::string json;
    const auto num = [](double v) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.6g", v);
        return std::string(buf);
    };
    if (const std::string* c = world->value("iw8r_sun_color"); c && !c->empty()) {
        double v[3] = {0, 0, 0};
        char tail = 0;
        if (std::sscanf(c->c_str(), "%lf %lf %lf %c", &v[0], &v[1], &v[2], &tail) == 3 && v[0] >= 0 && v[1] >= 0 && v[2] >= 0 &&
            v[0] <= 16 && v[1] <= 16 && v[2] <= 16)
            json += "\"color\":[" + num(v[0]) + "," + num(v[1]) + "," + num(v[2]) + "]";
        else if (notes)
            notes->push_back("iw8r_sun_color '" + *c + "' is not three numbers 0 to 16 (linear); the sun colour is not changed");
    }
    if (const std::string* i = world->value("iw8r_sun_intensity"); i && !i->empty()) {
        char* endp = nullptr;
        const double v = std::strtod(i->c_str(), &endp);
        if (endp && *endp == 0 && v >= 0 && v <= 1000000)
            json += std::string(json.empty() ? "" : ",") + "\"intensity\":" + num(v);
        else if (notes)
            notes->push_back("iw8r_sun_intensity '" + *i + "' is not a number 0 to 1000000; the sun intensity is not changed");
    }
    return json.empty() ? std::string() : "{" + json + "}";
}

bool stockSkyEdit(const MapFile& map, const std::string& thisZone, StockEditCarry& carry, std::string& replaceMaterials,
                  std::vector<std::string>* notes) {
    const Entity* world = nullptr;
    for (const Entity& e : map.entities)
        if (e.isWorldspawn()) {
            world = &e;
            break;
        }
    const std::string* v = world ? world->value("iw8r_sky_from") : nullptr;
    if (!v || v->empty()) return false;
    std::string id;
    for (const char c : *v) id += char(std::tolower(static_cast<unsigned char>(c)));
    const bool valid = id.size() > 3 && id.size() <= 40 && id.rfind("mp_", 0) == 0 &&
                       std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::islower(c) || std::isdigit(c) || c == '_'; });
    if (!valid) {
        if (notes) notes->push_back("iw8r_sky_from '" + *v + "' is not a stock map id (mp_...); the sky stays");
        return false;
    }
    if (id == thisZone) return false;
    carry = {"techsets_" + id, "material", "tw/sky_" + id, ""};
    replaceMaterials = "[{\"from\":\"@sky\",\"to\":\"tw/sky_" + id + "\"}]";
    if (notes) notes->push_back("the sky of " + id + " replaces this map's sky (tw/sky_" + id + " carried from techsets_" + id + ")");
    return true;
}

size_t stockRetextureEdits(const MapFile& map, const std::function<std::string(const std::string&)>& zoneOf,
                           std::vector<StockEditCarry>& carry, std::vector<std::string>& replaceEntries, std::vector<std::string>* notes) {
    const Entity* world = nullptr;
    for (const Entity& e : map.entities)
        if (e.isWorldspawn()) {
            world = &e;
            break;
        }
    const std::string* v = world ? world->value("iw8r_stock_retexture") : nullptr;
    if (!v || v->empty()) return 0;
    const auto trim = [](std::string t) {
        const auto a = t.find_first_not_of(" \t"), b = t.find_last_not_of(" \t");
        return a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
    };
    const auto validName = [](const std::string& n) {
        return !n.empty() && n.size() < 128 && std::all_of(n.begin(), n.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '_' || c == '/' || c == '-' || c == '.' || c == '~' || c == '&' || c == '#';
        });
    };
    size_t taken = 0;
    size_t at = 0;
    while (at <= v->size()) {
        const size_t semi = v->find(';', at);
        const std::string part = trim(v->substr(at, semi == std::string::npos ? std::string::npos : semi - at));
        at = semi == std::string::npos ? v->size() + 1 : semi + 1;
        if (part.empty()) continue;
        const size_t eq = part.find('=');
        const std::string from = eq == std::string::npos ? std::string() : trim(part.substr(0, eq));
        const std::string to = eq == std::string::npos ? std::string() : trim(part.substr(eq + 1));
        if (!validName(from) || !validName(to)) {
            if (notes) notes->push_back("iw8r_stock_retexture '" + part + "' is not 'from=to' (material names); left out");
            continue;
        }
        const std::string zone = zoneOf ? zoneOf(to) : std::string();
        std::string source;
        if (zone.rfind("techsets_", 0) == 0) source = zone;
        else if (zone.rfind("mp_", 0) == 0) source = "techsets_" + zone;
        if (source.empty()) {
            if (notes)
                notes->push_back("material '" + to + "' is " + (zone.empty() ? std::string("not in the material catalogue") : "held by " + zone + " only") +
                                 ": it cannot be carried into a stock map yet; '" + from + "' stays");
            continue;
        }
        carry.push_back({source, "material", to, ""});
        replaceEntries.push_back("{\"from\":\"" + from + "\",\"to\":\"" + to + "\"}");
        ++taken;
    }
    if (notes && taken) notes->push_back(std::to_string(taken) + " stock material(s) replaced on the world surfaces (iw8r_stock_retexture)");
    return taken;
}

}  // namespace iw8r
