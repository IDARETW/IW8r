#include "stock_placed.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>

#include "json_lite.h"

namespace iw8r {

namespace {

std::string lowerStr(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string num(double v) {
    if (!std::isfinite(v) || std::fabs(v) < 1e-9) return "0";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.9g", v);  // round-trips a float
    return buf;
}

std::string vecText(const Vec3& v) { return num(v.x) + " " + num(v.y) + " " + num(v.z); }

std::string vecJson(const Vec3& v) { return "[" + num(v.x) + "," + num(v.y) + "," + num(v.z) + "]"; }

// A vec3 dumped as {"v": [x, y, z], ...} or [x, y, z].
bool vec3(const json::Value* v, Vec3& out) {
    if (!v) return false;
    const json::Value* arr = v->type == json::Value::Type::Array ? v : v->get("v");
    if (!arr || arr->arr.size() < 3) return false;
    for (int a = 0; a < 3; ++a) {
        const auto n = arr->arr[size_t(a)].number();
        if (!n || !std::isfinite(*n)) return false;
        out[a] = *n;
    }
    return true;
}

bool near(const Vec3& a, const Vec3& b) {
    for (int k = 0; k < 3; ++k)
        if (std::fabs(a[k] - b[k]) > 1e-3 + 1e-6 * std::fabs(b[k])) return false;
    return true;
}

bool parseVec(const std::string& text, Vec3& out) {
    Vec3 v;
    if (std::sscanf(text.c_str(), "%lf %lf %lf", &v.x, &v.y, &v.z) != 3) return false;
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return false;
    out = v;
    return true;
}

}  // namespace

bool readStockPlacedObjects(const std::string& assetJson, std::vector<StockPlacedObject>& out, std::string* err) {
    out.clear();
    const auto fail = [&](const std::string& m) {
        if (err) *err = assetJson + ": " + m;
        return false;
    };
    std::string text;
    if (!readFile(assetJson, text)) return fail("cannot read file");
    std::string perr;
    const auto root = json::parse(text, &perr);
    if (!root) return fail("JSON: " + perr);
    const json::Value* fields = root->get("asset") ? root->get("asset")->get("fields") : nullptr;
    if (!fields) return fail("no asset fields");
    const json::Value* block = fields->get("scriptableMapEnts");
    const json::Value* list = block ? block->get("mapInstances") : nullptr;
    if (!list || list->isNull()) return true;
    const json::Value* values = list->get("values");
    if (!values || values->type != json::Value::Type::Array) return fail("the scriptable instances are not decoded values");
    for (size_t i = 0; i < values->arr.size(); ++i) {
        const json::Value& r = values->arr[i];
        StockPlacedObject o;
        o.index = uint32_t(i);
        if (const json::Value* def = r.get("initialDef"); def && !def->isNull()) {
            const json::Value* name = def->get("name");
            if (!name || !name->string()) name = def->get("string");
            if (name && name->string()) o.definition = *name->string();
        }
        while (!o.definition.empty() && o.definition[0] == ',') o.definition.erase(0, 1);  // a name reference
        if (!vec3(r.get("initialOrigin"), o.origin) || !vec3(r.get("initialAngles"), o.angles))
            return fail("scriptable instance " + std::to_string(i) + " has no origin or angles");
        out.push_back(std::move(o));
    }
    if (const json::Value* count = block->get("mapInstanceCount")) {
        std::optional<double> n = count->number();
        if (!n && count->get("value")) n = count->get("value")->number();
        if (n && *n != double(out.size())) return fail("mapInstanceCount does not match the decoded instances");
    }
    return true;
}

bool editablePlacedDefinition(const ScriptableDefinition& def) { return def.verified && !def.instanceDynent && def.physicsDynents == 0; }

std::vector<std::pair<std::string, std::string>> placedObjectEntityKeys(const StockPlacedObject& o) {
    return {{"classname", kPlacedObjectClass},
            {"scriptable", o.definition},
            {"origin", vecText(o.origin)},
            {"angles", vecText(o.angles)},
            {kPlacedObjectIndexKey, std::to_string(o.index)}};
}

bool hasPlacedObjects(const MapFile& map) {
    for (const Entity& e : map.entities)
        if (lowerStr(e.classname()) == kPlacedObjectClass) return true;
    return false;
}

PlacedObjectEdits placedObjectEdits(const MapFile& map, const std::vector<StockPlacedObject>& stock, const ScriptableCatalog& catalog) {
    PlacedObjectEdits out;
    std::map<std::string, uint32_t> firstOf;  // definition (lower case) -> its first stock instance
    for (const StockPlacedObject& o : stock) firstOf.emplace(lowerStr(o.definition), o.index);
    std::set<uint32_t> seen;
    std::string moves, adds;
    std::map<std::string, size_t> leftOut;  // reason -> count
    bool any = false;
    for (const Entity& e : map.entities) {
        if (lowerStr(e.classname()) != kPlacedObjectClass) continue;
        any = true;
        const std::string* dv = e.value("scriptable");
        if (!dv || dv->empty()) {
            ++leftOut["no scriptable key (the definition's name)"];
            continue;
        }
        const std::string key = lowerStr(*dv);
        const auto origin = e.origin();
        if (!origin || !std::isfinite(origin->x) || !std::isfinite(origin->y) || !std::isfinite(origin->z)) {
            ++leftOut["'" + *dv + "' without an origin"];
            continue;
        }
        Vec3 angles;
        if (const std::string* a = e.value("angles")) parseVec(*a, angles);

        // The stock instance this entity stands for, when its index and definition still match and no other entity
        // took it (a pasted copy keeps the index key of its source: the copy is a new object).
        const StockPlacedObject* own = nullptr;
        if (const std::string* iv = e.value(kPlacedObjectIndexKey)) {
            char* end = nullptr;
            const unsigned long n = std::strtoul(iv->c_str(), &end, 10);
            if (end && *end == 0 && !iv->empty() && n < stock.size() && lowerStr(stock[n].definition) == key && !seen.count(uint32_t(n)))
                own = &stock[n];
        }
        const ScriptableDefinition* def = catalog.find(*dv);
        if (own) {
            seen.insert(own->index);
            if (near(*origin, own->origin) && near(angles, own->angles)) continue;  // unchanged
            if (!def || !editablePlacedDefinition(*def)) {
                ++leftOut["'" + *dv + "' was moved, but it has physics entries of its own (or is not in the catalogue): it stays where it was"];
                continue;
            }
            moves += std::string(out.moved ? "," : "") + "{\"index\":" + std::to_string(own->index) + ",\"origin\":" + vecJson(*origin) +
                     ",\"angles\":" + vecJson(angles) + "}";
            ++out.moved;
            continue;
        }
        if (!def) {
            ++leftOut["'" + *dv + "' is not a stock scriptable definition"];
            continue;
        }
        if (!editablePlacedDefinition(*def)) {
            ++leftOut["'" + def->name + "' has physics entries of its own: a stock map cannot take a new one yet"];
            continue;
        }
        const auto like = firstOf.find(key);
        if (like == firstOf.end()) {
            ++leftOut["'" + def->name + "': this map has none to copy from (a new object copies an existing one of its kind)"];
            continue;
        }
        adds += std::string(out.added ? "," : "") + "{\"like\":" + std::to_string(like->second) + ",\"origin\":" + vecJson(*origin) + ",\"angles\":" +
                vecJson(angles) + ",\"server_instanced_parts\":" + std::to_string(def->serverInstancedParts) + ",\"usable_parts\":" +
                std::to_string(def->usableParts) + ",\"physics_limit\":" + (def->physicsLimit ? "true" : "false") + ",\"physics_limit_detail\":" +
                (def->physicsLimitDetail ? "true" : "false") + "}";
        ++out.added;
    }
    if (!any) return out;
    // Stock objects of an editable kind whose entity is gone: removed (with the definition's numbers for the counts).
    std::string removes;
    for (const StockPlacedObject& o : stock) {
        if (seen.count(o.index)) continue;
        const ScriptableDefinition* def = catalog.find(o.definition);
        if (!def || !editablePlacedDefinition(*def)) continue;
        removes += std::string(out.removed ? "," : "") + "{\"index\":" + std::to_string(o.index) + ",\"server_instanced_parts\":" +
                   std::to_string(def->serverInstancedParts) + ",\"usable_parts\":" + std::to_string(def->usableParts) +
                   ",\"physics_limit\":" + (def->physicsLimit ? "true" : "false") + ",\"physics_limit_detail\":" +
                   (def->physicsLimitDetail ? "true" : "false") + "}";
        ++out.removed;
    }
    for (const auto& [reason, n] : leftOut) out.notes.push_back(std::to_string(n) + " placed object(s) left out: " + reason);
    if (out.moved || out.added || out.removed) {
        out.json = "{";
        if (out.moved) out.json += "\"move\":[" + moves + "]";
        if (out.added) out.json += std::string(out.moved ? "," : "") + "\"add\":[" + adds + "]";
        if (out.removed) out.json += std::string(out.moved || out.added ? "," : "") + "\"remove\":[" + removes + "]";
        out.json += "}";
        out.notes.insert(out.notes.begin(), std::to_string(out.moved) + " placed object(s) moved, " + std::to_string(out.added) + " added" +
                                                (out.removed ? ", " + std::to_string(out.removed) + " removed" : std::string()));
    }
    return out;
}

}  // namespace iw8r
