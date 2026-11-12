#include "stock_spawns.h"

#include "stock_loot.h"
#include "stock_placed.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace iw8r {

namespace {

std::string lowerStr(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string num(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.9g", v);  // round-trips a float
    return buf;
}

std::string vec(const Vec3& v) { return num(v.x) + " " + num(v.y) + " " + num(v.z); }

}  // namespace

bool isSpawnListClass(const std::string& classname) {
    const std::string c = lowerStr(classname);
    return c.rfind("mp_", 0) == 0 && c.find("spawn") != std::string::npos;
}

std::vector<std::pair<std::string, std::string>> spawnEntityKeys(const Iw8SpawnRecord& s) {
    std::vector<std::pair<std::string, std::string>> kv = {{"classname", s.name}};
    if (!s.target.empty()) kv.push_back({"target", s.target});
    if (!s.noteworthy.empty()) kv.push_back({"script_noteworthy", s.noteworthy});
    kv.push_back({"origin", vec(s.origin)});
    kv.push_back({"angles", vec(s.angles)});
    return kv;
}

void splitStockDocument(const MapFile& map, std::string& entityString, std::vector<SpawnRecord>& spawns, std::string* err) {
    MapFile rest = map;
    rest.entities.clear();
    spawns.clear();
    size_t brushes = 0;
    for (const Entity& e : map.entities) {
        if (isLootPointClass(e.classname())) continue;  // loot points go to the edits file, never into the text (stock_loot.h)
        if (lowerStr(e.classname()) == kPlacedObjectClass) continue;  // placed objects too (stock_placed.h)
        if (!isSpawnListClass(e.classname())) {
            // Brushes and patches are not built into a stock map (its world is compiled), so they never enter the
            // entity text: the worldspawn loses them, any other entity that holds some is left out whole. (A helper
            // brush for Hide Stock Props that was not deleted used to be written into the text.)
            size_t prims = 0;
            for (const EntityItem& it : e.items) prims += it.isPrimitive() ? 1 : 0;
            if (prims) {
                brushes += prims;
                if (!e.isWorldspawn()) continue;
                Entity copy = e;
                copy.items.erase(std::remove_if(copy.items.begin(), copy.items.end(), [](const EntityItem& it) { return !it.isKeyValue(); }),
                                 copy.items.end());
                rest.entities.push_back(std::move(copy));
                continue;
            }
            rest.entities.push_back(e);
            continue;
        }
        const auto o = e.origin();
        if (!o) {
            if (err) *err += "spawn " + e.classname() + " has no origin; left out\n";
            continue;
        }
        SpawnRecord r;
        r.classname = e.classname();
        r.origin = *o;
        if (const std::string* a = e.value("angles")) std::sscanf(a->c_str(), "%lf %lf %lf", &r.angles.x, &r.angles.y, &r.angles.z);
        if (const std::string* t = e.value("target")) r.target = *t;
        if (const std::string* n = e.value("script_noteworthy")) r.script_noteworthy = *n;
        if (const std::string* tn = e.value("targetname")) r.targetname = *tn;
        r.index = uint16_t(spawns.size());
        spawns.push_back(r);
    }
    if (brushes && err) *err += std::to_string(brushes) + " brush(es) or patch(es) of the document are not built into a stock map; left out\n";
    entityString = rest.text();
}

}  // namespace iw8r
