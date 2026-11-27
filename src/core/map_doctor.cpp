#include "map_doctor.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>

#include "collision_world.h"
#include "prefab.h"

namespace iw8r {

namespace {

bool isSpawn(const std::string& c) {
    return c == "info_player_start" || (c.rfind("mp_", 0) == 0 && c.find("spawn") != std::string::npos);
}

bool degeneratePatch(const Primitive& p) {
    if (p.verts.empty()) return true;
    const AABB b = p.bounds();
    const Vec3 ext = b.maxs - b.mins;
    int flat = 0;
    for (int a = 0; a < 3; ++a) flat += ext[a] <= 1e-9;
    return flat >= 2;
}

std::string vecText(const Vec3& v) { return formatNumber(v.x) + " " + formatNumber(v.y) + " " + formatNumber(v.z); }

std::string where(size_t index, const std::string& cls) { return "entity " + std::to_string(index) + " (" + cls + ")"; }

const Vec3 kBoxMins{-16, -16, 0}, kBoxMaxs{16, 16, 72};

bool boxFree(const CollisionWorld& w, const Vec3& at) {
    const auto t = w.traceBox(at, at + Vec3{0, 0, 0.01}, kBoxMins, kBoxMaxs);
    return !t.startSolid && !t.allSolid;
}

// The floor straight below `from` (box trace down `range` units); nullopt when none, or not walkable.
std::optional<Vec3> floorBelow(const CollisionWorld& w, const Vec3& from, double range) {
    const auto t = w.traceBox(from, from - Vec3{0, 0, range}, kBoxMins, kBoxMaxs);
    if (t.startSolid || t.allSolid || t.fraction >= 1.0) return std::nullopt;
    if (t.plane.n.z < 0.7) return std::nullopt;  // steeper than about 45 degrees
    Vec3 p = from + (Vec3{0, 0, -range}) * t.fraction;
    p.z = std::round(p.z * 8.0) / 8.0;  // keep the written value tidy (the trace stops a hair above the surface)
    return p;
}

}  // namespace

std::vector<DoctorFix> diagnoseMap(const MapFile& map) {
    std::vector<DoctorFix> out;
    // Flatten placed prefabs so prefab geometry (brushes and patches) and targetnames/linknames are visible.
    bool anyPrefab = false;
    for (const Entity& e : map.entities)
        if (e.classname() == "misc_prefab") {
            anyPrefab = true;
            break;
        }
    MapFile flat;
    const MapFile* geomMap = &map;
    if (anyPrefab) {
        std::error_code ec;
        std::vector<std::string> roots;
        if (const auto cwd = std::filesystem::current_path(ec); !ec && !cwd.empty()) roots.push_back(cwd.string());
        if (flattenPrefabs(map, "", roots, flat, nullptr, nullptr)) geomMap = &flat;
    }

    std::set<std::string> targetnames, linknames;
    for (const Entity& e : geomMap->entities) {
        if (const std::string* t = e.value("targetname"); t && !t->empty()) targetnames.insert(*t);
        if (const std::string* l = e.value("script_linkname"); l && !l->empty()) linknames.insert(*l);
    }
    CollisionWorld world;
    bool worldBuilt = false;
    for (size_t i = 0; i < map.entities.size(); ++i) {
        const Entity& e = map.entities[i];
        const std::string cls = e.classname();
        // Dangling targets (target, target2..8, iw8r_teleport, script_linkto -> script_linkname).
        for (const auto& it : e.items)
            if (const KeyValueLine* kv = it.kv()) {
                const std::string k = kv->name();
                const bool targetKey = k == "target" || (k.size() == 7 && k.rfind("target", 0) == 0 && k[6] >= '2' && k[6] <= '8') ||
                                       k == "iw8r_teleport";
                if (targetKey && !kv->value.empty() && !targetnames.count(kv->value)) {
                    out.push_back({DoctorFixKind::DanglingTarget, e.id, kNoId, k, "",
                                   where(i, cls) + ": remove " + k + " \"" + kv->value + "\" (no entity has that targetname)"});
                } else if (k == "script_linkto" && !kv->value.empty()) {
                    const std::vector<std::string> toks = splitTokens(kv->value);
                    std::string kept;
                    bool anyMissing = false;
                    for (const std::string& tok : toks) {
                        if (linknames.count(tok))
                            kept += (kept.empty() ? "" : " ") + tok;
                        else
                            anyMissing = true;
                    }
                    if (anyMissing) {
                        out.push_back({DoctorFixKind::DanglingTarget, e.id, kNoId, k, kept,
                                       kept.empty()
                                           ? where(i, cls) + ": remove script_linkto \"" + kv->value +
                                                 "\" (no entity has that script_linkname)"
                                           : where(i, cls) + ": trim script_linkto \"" + kv->value + "\" -> \"" + kept +
                                                 "\" (missing script_linkname)"});
                    }
                }
            }
        // Broken primitives.
        size_t k = 0;
        for (const auto& it : e.items)
            if (const Primitive* p = it.prim()) {
                const size_t idx = k++;
                const bool bad = (p->kind == PrimKind::Brush && p->planes().size() < 4) || (p->kind == PrimKind::Patch && degeneratePatch(*p));
                if (bad)
                    out.push_back({DoctorFixKind::BadPrimitive, e.id, it.id, "", "",
                                   where(i, cls) + ": delete " + (p->kind == PrimKind::Brush ? "brush " : "patch ") + std::to_string(idx) +
                                       (p->kind == PrimKind::Brush ? " (fewer than 4 valid planes)" : " (degenerate)")});
            }
        if (!isSpawn(cls)) continue;
        const auto o = e.origin();
        if (!o) continue;
        // Angles with pitch or roll.
        if (const std::string* a = e.value("angles")) {
            double p = 0, y = 0, r = 0;
            const int n = std::sscanf(a->c_str(), "%lf %lf %lf", &p, &y, &r);
            if (n >= 1 && (std::fabs(p) > 1e-6 || (n >= 3 && std::fabs(r) > 1e-6)))
                out.push_back({DoctorFixKind::SpawnAngles, e.id, kNoId, "angles", "0 " + formatNumber(y) + " 0",
                               where(i, cls) + ": angles \"" + *a + "\" -> \"0 " + formatNumber(y) + " 0\""});
        }
        // Floor: inside a solid (brush, patch, or prefab), or nothing to stand on within 128 units.
        if (!worldBuilt) {
            world.build(*geomMap);
            worldBuilt = true;
        }
        // Clearance half a unit up: a box resting exactly on a floor counts as touching, not inside.
        const bool inside = !boxFree(world, *o + Vec3{0, 0, 0.5});
        const auto below = inside ? std::nullopt : floorBelow(world, *o + Vec3{0, 0, 1}, 129);
        if (!inside && below) continue;  // stands on something within 128
        std::optional<Vec3> to;
        if (inside) {
            for (double dz = 8; dz <= 128 && !to; dz += 8)
                if (boxFree(world, *o + Vec3{0, 0, dz})) to = floorBelow(world, *o + Vec3{0, 0, dz}, dz + 1);
        } else {
            to = floorBelow(world, *o + Vec3{0, 0, 1}, 1024);
        }
        if (!to) continue;
        const double dz = to->z - o->z;
        if (std::fabs(dz) < 0.5) continue;  // already there
        out.push_back({DoctorFixKind::SpawnFloor, e.id, kNoId, "origin", vecText(*to),
                       where(i, cls) + (inside ? ": out of the solid, onto the top " : ": down onto the floor ") +
                           formatNumber(std::fabs(dz)) + (dz >= 0 ? " up" : " below")});
    }
    return out;
}

size_t applyDoctorFixes(Document& doc, const std::vector<DoctorFix>& fixes, const std::string& name) {
    size_t n = 0;
    doc.beginGroup(name);
    std::vector<Document::PrimEdit> deletes;
    for (const DoctorFix& f : fixes) {
        if (!doc.findEntity(f.entity)) continue;
        switch (f.kind) {
            case DoctorFixKind::DanglingTarget:
                n += (f.value.empty() ? doc.removeKey(f.entity, f.key) : doc.setKeyValue(f.entity, f.key, f.value)) ? 1 : 0;
                break;
            case DoctorFixKind::SpawnAngles:
            case DoctorFixKind::SpawnFloor:
                n += doc.setKeyValue(f.entity, f.key, f.value) ? 1 : 0;
                break;
            case DoctorFixKind::BadPrimitive:
                if (doc.findItem(f.entity, f.item)) {
                    deletes.push_back({f.entity, f.item, std::nullopt});
                    ++n;
                }
                break;
        }
    }
    if (!deletes.empty() && !doc.editPrimitives(deletes, name, false)) n -= deletes.size();
    doc.endGroup();
    return n;
}

}  // namespace iw8r
