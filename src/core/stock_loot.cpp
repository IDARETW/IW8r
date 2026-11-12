#include "stock_loot.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>

#include "brush_ops.h"
#include "json_lite.h"

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

std::string vecText(const Vec3& v) { return num(v.x) + " " + num(v.y) + " " + num(v.z); }

// {"value": N} or a bare number.
std::optional<double> scalar(const json::Value* v) {
    if (!v) return std::nullopt;
    if (auto n = v->number()) return n;
    if (const json::Value* inner = v->get("value")) return inner->number();
    return std::nullopt;
}

std::optional<uint32_t> toU32(const json::Value* v, uint32_t max) {
    const auto n = scalar(v);
    if (!n || !std::isfinite(*n) || *n < 0 || *n != std::floor(*n) || *n > double(max)) return std::nullopt;
    return uint32_t(*n);
}

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

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool hexBytes(const std::string& hex, std::string& out) {
    if (hex.size() % 2) return false;
    out.resize(hex.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        const int hi = hexNibble(hex[2 * i]), lo = hexNibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = char((hi << 4) | lo);
    }
    return true;
}

bool box(const json::Value* bounds, LootBox& out) {
    if (!bounds) return false;
    out.known = vec3(bounds->get("midPoint"), out.mid) && vec3(bounds->get("halfSize"), out.half);
    return out.known;
}

// True when `s` is a whole number of at most 5 digits; `value` gets it.
bool wholeNumber(const std::string& s, uint32_t& value) {
    if (s.empty() || s.size() > 5) return false;
    value = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + uint32_t(c - '0');
    }
    return true;
}

std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

}  // namespace

bool LootBox::holds(const Vec3& p, double tolerance) const {
    if (!known) return false;
    for (int a = 0; a < 3; ++a)
        if (std::abs(p[a] - mid[a]) > half[a] + tolerance) return false;
    return true;
}

bool readStockLoot(const std::string& assetJson, const std::string& scriptStringsJson, StockLoot& out, std::string* err) {
    out = StockLoot{};
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
    const json::Value* loot = fields->get("spawnGroupLoot");
    if (!loot || loot->isNull()) return true;  // an export without the block: no loot

    std::vector<std::string> strings;
    if (!scriptStringsJson.empty()) {
        std::string st;
        if (readFile(scriptStringsJson, st))
            if (const auto sj = json::parse(st, &perr); sj && sj->type == json::Value::Type::Array)
                for (const auto& v : sj->arr) strings.push_back(v.string() ? *v.string() : std::string());
    }

    const auto pointCount = toU32(loot->get("pointCount"), 0xFFFF);
    if (!pointCount) return fail("spawnGroupLoot.pointCount is not a count");
    if (const json::Value* points = loot->get("points"); points && !points->isNull()) {
        if (const json::Value* values = points->get("values"); values && values->type == json::Value::Type::Array) {
            for (const auto& r : values->arr) {
                LootPoint p;
                const auto cluster = toU32(r.get("cluster"), 0xFFFF), zone = toU32(r.get("zone"), 0xFFFF);
                if (!vec3(r.get("origin"), p.origin) || !vec3(r.get("angles"), p.angles) || !cluster || !zone)
                    return fail("a loot point is not origin, angles, cluster, zone");
                p.cluster = uint16_t(*cluster);
                p.zone = uint16_t(*zone);
                out.points.push_back(p);
            }
        } else if (const json::Value* bytes = points->get("bytes"); bytes && bytes->string()) {
            // SpawnGroupPoint, 28 bytes: float origin[3], float angles[3], u16 cluster, u16 zone (little endian).
            std::string raw;
            if (!hexBytes(*bytes->string(), raw) || raw.size() % 28) return fail("the loot point bytes are not 28-byte records");
            for (size_t at = 0; at < raw.size(); at += 28) {
                float f[6];
                uint16_t u[2];
                std::memcpy(f, raw.data() + at, sizeof(f));
                std::memcpy(u, raw.data() + at + 24, sizeof(u));
                LootPoint p;
                for (int a = 0; a < 3; ++a) {
                    if (!std::isfinite(f[a]) || !std::isfinite(f[3 + a])) return fail("a loot point is not finite");
                    p.origin[a] = double(f[a]);
                    p.angles[a] = double(f[3 + a]);
                }
                p.cluster = u[0];
                p.zone = u[1];
                out.points.push_back(p);
            }
        } else {
            return fail("the loot points are neither decoded values nor bytes");
        }
    }
    if (out.points.size() != *pointCount) return fail("spawnGroupLoot.pointCount does not match the decoded points");

    const auto clusterCount = toU32(loot->get("clusterCount"), 0xFFFF);
    const auto zoneCount = toU32(loot->get("zoneCount"), 0xFFFF);
    if (!clusterCount || !zoneCount) return fail("spawnGroupLoot cluster or zone count is not a count");
    out.clusters.resize(*clusterCount);
    out.zones.resize(*zoneCount);
    out.zoneNames.resize(*zoneCount);
    if (const json::Value* c = loot->get("clusters"); c && !c->isNull())
        if (const json::Value* values = c->get("values"); values && values->type == json::Value::Type::Array) {
            if (values->arr.size() != *clusterCount) return fail("spawnGroupLoot.clusterCount does not match the decoded clusters");
            for (size_t i = 0; i < values->arr.size(); ++i) box(values->arr[i].get("bounds"), out.clusters[i]);
        }
    if (const json::Value* z = loot->get("zones"); z && !z->isNull())
        if (const json::Value* values = z->get("values"); values && values->type == json::Value::Type::Array) {
            if (values->arr.size() != *zoneCount) return fail("spawnGroupLoot.zoneCount does not match the decoded zones");
            for (size_t i = 0; i < values->arr.size(); ++i) {
                box(values->arr[i].get("bounds"), out.zones[i]);
                if (const auto name = toU32(values->arr[i].get("name"), std::numeric_limits<uint32_t>::max()); name && *name < strings.size())
                    out.zoneNames[i] = strings[*name];
            }
        }
    return true;
}

bool isLootPointClass(const std::string& classname) { return lowerStr(classname) == kLootPointClass; }

std::vector<std::pair<std::string, std::string>> lootPointEntityKeys(const LootPoint& p, const StockLoot* block) {
    std::vector<std::pair<std::string, std::string>> kv = {{"classname", kLootPointClass}, {"origin", vecText(p.origin)}, {"angles", vecText(p.angles)}};
    kv.push_back({"loot_cluster", p.cluster == kLootFreePoint ? "free" : p.cluster == kLootCachePoint ? "cache" : std::to_string(p.cluster)});
    kv.push_back({"loot_zone", std::to_string(p.zone)});
    if (block && p.zone && size_t(p.zone - 1) < block->zoneNames.size() && !block->zoneNames[size_t(p.zone - 1)].empty())
        kv.push_back({"loot_zone_name", block->zoneNames[size_t(p.zone - 1)]});
    return kv;
}

bool isLootBoxClass(const std::string& classname) {
    std::string c;
    for (const char ch : classname) c += char(std::tolower(static_cast<unsigned char>(ch)));
    return c == kLootClusterClass || c == kLootZoneClass;
}

LootBoxEdits documentLootBoxes(const MapFile& map, const std::string& defaultZoneName, std::vector<std::string>* notes) {
    LootBoxEdits out;
    size_t empty = 0, unnamed = 0, renamed = 0;
    for (const Entity& e : map.entities) {
        if (!isLootBoxClass(e.classname())) continue;
        AABB box;
        for (const EntityItem& it : e.items) {
            const Primitive* p = it.prim();
            if (!p) continue;
            if (p->kind == PrimKind::Brush) {
                const AABB b = brushBounds(brushFromPrimitive(*p));
                if (b.valid()) box.add(b);
            } else {
                for (const PatchVertex& v : p->verts) box.add(v.pos);
            }
        }
        if (!box.valid() || box.maxs.x <= box.mins.x || box.maxs.y <= box.mins.y || box.maxs.z <= box.mins.z) {
            ++empty;
            continue;
        }
        LootBoxEdit edit;
        edit.mid = (box.mins + box.maxs) * 0.5;
        edit.half = (box.maxs - box.mins) * 0.5;
        std::string c;
        for (const char ch : e.classname()) c += char(std::tolower(static_cast<unsigned char>(ch)));
        if (c == kLootClusterClass) {
            out.clusters.push_back(edit);
            continue;
        }
        const std::string* name = e.value("name");
        std::string clean;
        for (const char ch : name ? *name : std::string()) {
            const char l = char(std::tolower(static_cast<unsigned char>(ch)));
            if (std::islower(static_cast<unsigned char>(l)) || std::isdigit(static_cast<unsigned char>(l)) || l == '_') clean += l;
        }
        if (clean.empty()) {
            ++unnamed;
            clean = defaultZoneName;
        } else if (clean != *name) {
            ++renamed;
        }
        edit.name = clean;
        out.zones.push_back(edit);
    }
    if (notes) {
        if (empty) notes->push_back(std::to_string(empty) + " loot box entit" + (empty == 1 ? "y has" : "ies have") + " no brush and are left out");
        if (unnamed) notes->push_back(std::to_string(unnamed) + " loot zone(s) without a name are named " + defaultZoneName);
        if (renamed) notes->push_back(std::to_string(renamed) + " loot zone name(s) were lowered to letters, digits and '_'");
    }
    return out;
}

std::string lootBoxesJson(const LootBoxEdits& boxes) {
    const auto vec = [](const Vec3& v) { return "[" + num(v.x) + "," + num(v.y) + "," + num(v.z) + "]"; };
    std::string json = "{\"clusters\":[";
    for (size_t i = 0; i < boxes.clusters.size(); ++i)
        json += std::string(i ? "," : "") + "{\"mid\":" + vec(boxes.clusters[i].mid) + ",\"half\":" + vec(boxes.clusters[i].half) + "}";
    json += "],\"zones\":[";
    for (size_t i = 0; i < boxes.zones.size(); ++i)
        json += std::string(i ? "," : "") + "{\"name\":\"" + boxes.zones[i].name + "\",\"mid\":" + vec(boxes.zones[i].mid) +
                ",\"half\":" + vec(boxes.zones[i].half) + "}";
    return json + "]}";
}

bool hasLootPoints(const MapFile& map) {
    for (const Entity& e : map.entities)
        if (isLootPointClass(e.classname())) return true;
    return false;
}

MapFile withoutLootPoints(const MapFile& map) {
    MapFile rest = map;
    rest.entities.clear();
    for (const Entity& e : map.entities)
        if (!isLootPointClass(e.classname()) && !isLootBoxClass(e.classname())) rest.entities.push_back(e);
    return rest;
}

std::vector<LootPointEdit> documentLootPoints(const MapFile& map, const StockLoot& block, std::vector<std::string>* notes) {
    std::vector<LootPointEdit> out;
    size_t noOrigin = 0, badCluster = 0, badZone = 0, leftCluster = 0, leftZone = 0;
    for (const Entity& e : map.entities) {
        if (!isLootPointClass(e.classname())) continue;
        const auto origin = e.origin();
        if (!origin || !std::isfinite(origin->x) || !std::isfinite(origin->y) || !std::isfinite(origin->z)) {
            ++noOrigin;
            continue;
        }
        LootPointEdit p;
        p.origin = *origin;
        if (const std::string* a = e.value("angles")) {
            Vec3 v;
            if (std::sscanf(a->c_str(), "%lf %lf %lf", &v.x, &v.y, &v.z) == 3 && std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z))
                p.angles = v;
        }
        bool cache = false;
        if (const std::string* c = e.value("loot_cluster")) {
            const std::string text = lowerStr(trimmed(*c));
            uint32_t n = 0;
            if (text == "free" || text == "cache" || text == "auto" || text.empty()) {
                p.cluster = text.empty() ? "auto" : text;
                cache = text == "cache";
            } else if (wholeNumber(text, n) && (n < block.clusters.size() || n == kLootFreePoint || n == kLootCachePoint || n == kLootNoCluster)) {
                if (n == kLootFreePoint) p.cluster = "free";
                else if (n == kLootCachePoint) p.cluster = "cache", cache = true;
                else if (n < block.clusters.size() && block.clusters[n].known && !block.clusters[n].holds(p.origin)) {
                    p.cluster = "auto";  // moved out of its cluster's box
                    ++leftCluster;
                } else p.cluster = std::to_string(n);
            } else {
                ++badCluster;
            }
        }
        if (const std::string* z = e.value("loot_zone")) {
            const std::string text = lowerStr(trimmed(*z));
            uint32_t n = 0;
            if (text == "auto" || text.empty()) {
                p.zone = "auto";
            } else if (wholeNumber(text, n) && n <= block.zones.size()) {
                if (n && !cache && block.zones[n - 1].known && !block.zones[n - 1].holds(p.origin)) {
                    p.zone = "auto";  // moved out of its zone's box
                    ++leftZone;
                } else p.zone = std::to_string(n);
            } else {
                ++badZone;
            }
        }
        out.push_back(p);
    }
    if (notes) {
        const auto note = [&](size_t n, const char* what) {
            if (n) notes->push_back(std::to_string(n) + " " + what);
        };
        note(noOrigin, "loot point(s) without an origin are left out");
        note(badCluster, "loot point(s) name a cluster the map does not have: the converter picks the cluster by position");
        note(badZone, "loot point(s) name a zone the map does not have: the converter picks the zone by position");
        note(leftCluster, "loot point(s) were moved out of their cluster's box: the converter picks the cluster by position");
        note(leftZone, "loot point(s) were moved out of their zone's box: the converter picks the zone by position");
    }
    return out;
}

std::string lootPointsJson(const std::vector<LootPointEdit>& points) {
    std::string json = "[";
    const auto field = [](const std::string& v) {
        uint32_t n = 0;
        return wholeNumber(v, n) ? v : "\"" + v + "\"";  // only free / cache / auto reach here as text
    };
    for (size_t i = 0; i < points.size(); ++i) {
        const LootPointEdit& p = points[i];
        json += std::string(i ? "," : "") + "{\"origin\":[" + num(p.origin.x) + "," + num(p.origin.y) + "," + num(p.origin.z) + "],\"angles\":[" +
                num(p.angles.x) + "," + num(p.angles.y) + "," + num(p.angles.z) + "],\"cluster\":" + field(p.cluster) + ",\"zone\":" + field(p.zone) + "}";
    }
    return json + "]";
}

}  // namespace iw8r
