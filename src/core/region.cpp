// IW8 Radiant Suite - map regions and subset text (see region.h).
#include "region.h"

#include <utility>

namespace iw8r {

namespace {

bool overlaps(const AABB& a, const AABB& b) {
    for (int i = 0; i < 3; ++i)
        if (a.maxs[i] < b.mins[i] || a.mins[i] > b.maxs[i]) return false;
    return true;
}

bool isPointEntity(const Entity& e) {
    for (const auto& it : e.items)
        if (it.isPrimitive()) return false;
    return true;
}

}  // namespace

std::set<Id> itemsOutside(const MapFile& map, const AABB& region) {
    std::set<Id> out;
    if (!region.valid()) return out;
    for (const auto& e : map.entities) {
        if (isPointEntity(e)) {
            if (e.isWorldspawn()) continue;
            const auto o = e.origin();
            if (o && !region.contains(*o)) out.insert(e.id);
            continue;
        }
        for (const auto& it : e.items) {
            const Primitive* p = it.prim();
            if (!p || p->kind == PrimKind::Unknown) continue;
            const AABB b = p->bounds();
            if (b.valid() && !overlaps(b, region)) out.insert(it.id);
        }
    }
    return out;
}

std::string mapTextSubset(const MapFile& map, const KeepFn& keep) {
    MapFile out;
    out.header = map.header;
    out.trailer = map.trailer;
    out.eol = map.eol;
    for (const auto& src : map.entities) {
        Entity e;
        e.open = src.open;
        e.close = src.close;
        if (isPointEntity(src)) {
            // Worldspawn always stays (its keys are the map's global state); other point entities pass keep().
            if (!src.isWorldspawn() && !keep(src.id, kNoId)) continue;
            e.items = src.items;
        } else {
            size_t kept = 0;
            for (const auto& it : src.items) {
                if (!it.isPrimitive()) {
                    e.items.push_back(it);
                } else if (keep(src.id, it.id)) {
                    e.items.push_back(it);
                    ++kept;
                }
            }
            // A brush entity that lost all of its primitives is dropped; worldspawn stays.
            if (kept == 0 && !src.isWorldspawn()) continue;
        }
        e.prefix = "// entity " + std::to_string(out.entities.size()) + map.eol;
        out.entities.push_back(std::move(e));
    }
    return out.text();
}

std::string mapTextInRegion(const MapFile& map, const AABB& region) {
    const std::set<Id> out = itemsOutside(map, region);
    return mapTextSubset(map, [&out](Id entity, Id item) { return item == kNoId ? out.count(entity) == 0 : out.count(item) == 0; });
}

}  // namespace iw8r
