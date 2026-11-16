// IW8 Radiant - gameplay sanity checks (see gameplay_checks.h).
#include "gameplay_checks.h"

#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <optional>

#include "brush_ops.h"
#include "leak_check.h"

namespace iw8r {
namespace {

constexpr double kEps = 1e-6;

bool startsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

bool isSpawnClass(const std::string& c) {
    return c == "info_player_start" || (startsWith(c, "mp_") && c.find("spawn") != std::string::npos);
}

// Player box (x and y half extent 16, height 72) against one brush. The box is clipped by every brush
// plane; it overlaps only when a piece of positive volume survives. Touching faces do not overlap.
bool boxOverlaps(const AABB& box, const SolidBrush& b) {
    for (int a = 0; a < 3; ++a)
        if (box.mins[a] >= b.bounds.maxs[a] - kEps || box.maxs[a] <= b.bounds.mins[a] + kEps) return false;
    ConvexBrush cur = makeBox(box, "");
    for (const BrushFaceSpec& f : b.planes) {
        std::optional<ConvexBrush> next = clipBrush(cur, f.plane, "");
        if (!next) return false;
        cur = std::move(*next);
    }
    return true;
}

// A brush top lies within 128 units below the origin. The ray starts one unit above the origin.
bool hasFloor(const Vec3& origin, const std::vector<SolidBrush>& solids) {
    const Ray ray{{origin.x, origin.y, origin.z + 1.0}, {0, 0, -1}};
    for (const SolidBrush& b : solids) {
        std::vector<Plane> planes;
        planes.reserve(b.planes.size());
        for (const BrushFaceSpec& f : b.planes) planes.push_back(f.plane);
        const std::optional<double> t = rayConvex(ray, planes);
        if (t && *t <= 129.0) return true;
    }
    return false;
}

}  // namespace

std::vector<std::string> killstreakNotes(const MapFile& map) {
    size_t entrances = 0, goals = 0, airstrike = 0, corners = 0;
    for (const Entity& e : map.entities) {
        const std::string cls = e.classname();
        const std::string* tn = e.value("targetname");
        const std::string name = tn ? *tn : std::string();
        if (cls == "script_struct_heli") {
            entrances += name == "ks_heli_entrance";
            goals += name == "ks_heli_goal";
        } else if (cls == "script_origin") {
            airstrike += name == "airstrikeheight";
            corners += name == "minimap_corner";
        }
    }
    std::vector<std::string> out;
    if (!entrances || !goals)
        out.push_back("no helicopter path (script_struct_heli ks_heli_entrance -> ks_heli_goal, linked with script_linkto / "
                      "script_linkname, radius 256): stock maps have 4 entrances and 4 goals");
    if (!airstrike) out.push_back("no script_origin airstrikeheight (stock maps put it about 2500-2600 units up)");
    if (corners != 0 && corners != 2) out.push_back(std::to_string(corners) + " minimap_corner script_origins (0 or 2 expected)");
    return out;
}

std::vector<GameplayIssue> gameplayIssues(const MapFile& map) {
    std::vector<GameplayIssue> out;
    if (map.entities.empty()) return out;

    struct Spawn {
        size_t entity;
        std::string cls;
        Vec3 o;
    };
    std::vector<Spawn> spawns;
    std::map<std::string, size_t> counts;
    bool anyDm = false;
    for (size_t e = 0; e < map.entities.size(); ++e) {
        const Entity& ent = map.entities[e];
        const std::string cls = ent.classname();
        ++counts[cls];
        if (startsWith(cls, "mp_dm_spawn")) anyDm = true;
        if (!isSpawnClass(cls)) continue;
        const std::optional<Vec3> o = ent.origin();
        if (!o) {
            out.push_back({e, cls, "spawn has no origin", {}});
            continue;
        }
        spawns.push_back({e, cls, *o});
    }
    const auto count = [&](const char* name) -> size_t {
        const auto it = counts.find(name);
        return it == counts.end() ? 0 : it->second;
    };
    const Vec3 modeAt = spawns.empty() ? Vec3{} : spawns.front().o;

    // Solid brushes, detail included: they block the player box, the floor ray and the spawn overlap.
    const std::vector<SolidBrush> solids = collidingBrushes(map, true);

    for (const Spawn& s : spawns) {
        AABB box;
        box.add(s.o + Vec3{-16, -16, 0});
        box.add(s.o + Vec3{16, 16, 72});
        for (const SolidBrush& b : solids)
            if (boxOverlaps(box, b)) {
                out.push_back({s.entity, s.cls,
                               "player box (32x32x72 at the feet) overlaps a solid brush of entity " +
                                   std::to_string(b.entity),
                               s.o});
                break;
            }
        if (!hasFloor(s.o, solids))
            out.push_back({s.entity, s.cls, "no floor within 128 units below the spawn", s.o});

        if (const std::string* angles = map.entities[s.entity].value("angles")) {
            const std::vector<std::string> tok = splitTokens(*angles);
            const double pitch = tok.size() >= 1 ? std::strtod(tok[0].c_str(), nullptr) : 0.0;
            const double roll = tok.size() >= 3 ? std::strtod(tok[2].c_str(), nullptr) : 0.0;
            if (std::fabs(pitch) > kEps || std::fabs(roll) > kEps)
                out.push_back({s.entity, s.cls,
                               "spawn angles pitch and roll must be 0 (angles \"" + *angles + "\")", s.o});
        }
    }

    for (size_t j = 0; j < spawns.size(); ++j)
        for (size_t i = 0; i < j; ++i) {
            const Vec3 d = spawns[j].o - spawns[i].o;
            if (length(d) < 32.0)
                out.push_back({spawns[j].entity, spawns[j].cls,
                               "spawn is within 32 units of entity " + std::to_string(spawns[i].entity) + " (" +
                                   spawns[i].cls + ")",
                               spawns[j].o});
        }

    const size_t mainEntity = 0;
    const std::string mainClass = map.entities[0].classname();
    const auto modeIssue = [&](const std::string& msg) { out.push_back({mainEntity, mainClass, msg, modeAt}); };

    if (count("mp_global_intermission") == 0) modeIssue("missing mp_global_intermission");

    // TDM is always required (doc 15 §3.4).
    if (count("mp_tdm_spawn") < 2)
        modeIssue("TDM: " + std::to_string(count("mp_tdm_spawn")) + " mp_tdm_spawn, at least 2 required");
    if (count("mp_tdm_spawn_allies_start") < 1) modeIssue("TDM: no mp_tdm_spawn_allies_start");
    if (count("mp_tdm_spawn_axis_start") < 1) modeIssue("TDM: no mp_tdm_spawn_axis_start");

    if (anyDm && count("mp_dm_spawn") < 2)
        modeIssue("DM: " + std::to_string(count("mp_dm_spawn")) + " mp_dm_spawn, at least 2 required when DM spawns exist");

    if (count("mp_dom_spawn") > 0) {
        if (count("mp_dom_spawn_allies_start") < 1) modeIssue("DOM: no mp_dom_spawn_allies_start");
        if (count("mp_dom_spawn_axis_start") < 1) modeIssue("DOM: no mp_dom_spawn_axis_start");
    }

    // script_model semantics (doc 34): x64-zt keeps a script_model as a live entity only when it is a gameobject or
    // exploder, or another entity targets its targetname; every other one becomes a static prop at build time.
    {
        std::set<std::string> targeted;
        for (const auto& ent : map.entities)
            if (const std::string* t = ent.value("target")) targeted.insert(*t);
        for (size_t e = 0; e < map.entities.size(); ++e) {
            const Entity& ent = map.entities[e];
            if (ent.classname() != "script_model") continue;
            const std::string* tn = ent.value("targetname");
            // Names the x64-zt compatibility layer drives (IW3 _destructables, interactive objects) also stay live.
            const bool special = tn && (*tn == "exploder" || *tn == "oldschool_pickup" || *tn == "destructible" ||
                                        *tn == "destructable" || *tn == "interactive_tv");
            if (ent.value("script_gameobjectname") || ent.value("script_exploder") || (tn && targeted.count(*tn)) || special) continue;
            out.push_back({e, "script_model",
                           "script_model " + std::string(ent.value("model") ? *ent.value("model") : std::string("(no model)")) +
                               " is built as a static prop (no gameobject, exploder or targeting entity); use misc_model for decor",
                           ent.origin().value_or(Vec3{})});
        }
    }
    return out;
}

}  // namespace iw8r
