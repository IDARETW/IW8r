// IW8 Radiant - gameplay sanity checks (see gameplay_checks.h).
#include "gameplay_checks.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <optional>

#include "brush_ops.h"
#include "leak_check.h"
#include "patch_tess.h"
#include "prefab.h"

namespace iw8r {
namespace {

constexpr double kEps = 1e-6;

bool startsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

std::string lowerStr(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isSpawnClass(const std::string& c) {
    return c == "info_player_start" || (startsWith(c, "mp_") && c.find("spawn") != std::string::npos);
}

bool isPatchToolNonSolid(const std::string& mat) {
    const std::string low = lowerStr(mat);
    const size_t slash = low.find_last_of("/\\");
    const std::string last = slash == std::string::npos ? low : low.substr(slash + 1);
    auto matchWord = [&](const char* w) {
        const size_t n = std::strlen(w);
        return last.rfind(w, 0) == 0 && (last.size() == n || last[n] == '_' || std::isdigit(static_cast<unsigned char>(last[n])));
    };
    return matchWord("trigger") || matchWord("hint") || matchWord("skip") || matchWord("portal") || matchWord("origin");
}

struct PatchTri {
    size_t entity = 0;
    Vec3 a, b, c;
    AABB bounds;
    std::vector<Plane> planes;
};

std::vector<PatchTri> collidingPatchTriangles(const MapFile& map) {
    std::vector<PatchTri> out;
    for (size_t e = 0; e < map.entities.size(); ++e) {
        const Entity& ent = map.entities[e];
        const std::string cls = lowerStr(ent.classname());
        if (cls != "worldspawn" && cls != "func_group" && cls != "script_brushmodel") continue;
        for (const EntityItem& item : ent.items) {
            const Primitive* p = item.prim();
            if (!p || p->kind != PrimKind::Patch) continue;
            if (p->text().find("nonColliding") != std::string::npos) continue;
            if (isPatchToolNonSolid(p->patchMaterial)) continue;
            const PatchMesh mesh = tessellatePrimitive(*p, 4);
            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                const Vec3& a = mesh.points[mesh.indices[i]];
                const Vec3& b = mesh.points[mesh.indices[i + 1]];
                const Vec3& c = mesh.points[mesh.indices[i + 2]];
                const Vec3 cr = cross(b - a, c - a);
                if (length(cr) < 1e-6) continue;
                Vec3 n = normalize(cr);
                // Floor-like patches extrude thickness downward so a spawn resting on z = top does not overlap.
                if (n.z < -0.5) n = n * -1.0;
                PatchTri tri;
                tri.entity = e;
                tri.a = a, tri.b = b, tri.c = c;
                tri.planes.reserve(5);
                tri.planes.push_back({n, dot(n, a)});
                tri.planes.push_back({n * -1.0, -dot(n, a) + 1.0});
                const Vec3 pts[3] = {a, b, c};
                for (int k = 0; k < 3; ++k) {
                    const Vec3& p0 = pts[k];
                    const Vec3& p1 = pts[(k + 1) % 3];
                    Vec3 en = normalize(cross(p1 - p0, n));
                    if (dot(en, pts[(k + 2) % 3] - p0) > 0) en = en * -1.0;
                    tri.planes.push_back({en, dot(en, p0)});
                }
                for (const Vec3& v : pts) {
                    tri.bounds.add(v);
                    tri.bounds.add(v - n);
                }
                out.push_back(std::move(tri));
            }
        }
    }
    return out;
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

bool boxOverlapsPatch(const AABB& box, const PatchTri& t) {
    for (int a = 0; a < 3; ++a)
        if (box.mins[a] >= t.bounds.maxs[a] - kEps || box.maxs[a] <= t.bounds.mins[a] + kEps) return false;
    ConvexBrush cur = makeBox(box, "");
    for (const Plane& pl : t.planes) {
        std::optional<ConvexBrush> next = clipBrush(cur, pl, "");
        if (!next) return false;
        cur = std::move(*next);
    }
    return true;
}

// A brush top or patch surface lies within 128 units below the origin. The ray starts one unit above the origin.
bool hasFloor(const Vec3& origin, const std::vector<SolidBrush>& solids, const std::vector<PatchTri>& patches) {
    const Ray ray{{origin.x, origin.y, origin.z + 1.0}, {0, 0, -1}};
    for (const SolidBrush& b : solids) {
        std::vector<Plane> planes;
        planes.reserve(b.planes.size());
        for (const BrushFaceSpec& f : b.planes) planes.push_back(f.plane);
        const std::optional<double> t = rayConvex(ray, planes);
        if (t && *t <= 129.0) return true;
    }
    for (const PatchTri& pt : patches) {
        if (origin.x < pt.bounds.mins.x - kEps || origin.x > pt.bounds.maxs.x + kEps ||
            origin.y < pt.bounds.mins.y - kEps || origin.y > pt.bounds.maxs.y + kEps ||
            pt.bounds.maxs.z < origin.z - 128.0 - kEps || pt.bounds.mins.z > origin.z + 1.0 + kEps)
            continue;
        const double x0 = pt.a.x, y0 = pt.a.y;
        const double dx1 = pt.b.x - x0, dy1 = pt.b.y - y0;
        const double dx2 = pt.c.x - x0, dy2 = pt.c.y - y0;
        const double det = dx1 * dy2 - dy1 * dx2;
        if (std::fabs(det) < 1e-12) continue;
        const double px = origin.x - x0, py = origin.y - y0;
        const double u = (px * dy2 - py * dx2) / det;
        const double v = (dx1 * py - dy1 * px) / det;
        const double w = 1.0 - u - v;
        if (u < -1e-6 || v < -1e-6 || w < -1e-6) continue;
        const double z = w * pt.a.z + u * pt.b.z + v * pt.c.z;
        const double dist = (origin.z + 1.0) - z;
        if (dist >= -kEps && dist <= 129.0) return true;
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

    // Flatten placed prefabs for collision geometry, patch floors/overlaps and cross-prefab entity counts/targets.
    bool anyPrefab = false;
    for (const Entity& ent : map.entities)
        if (ent.classname() == "misc_prefab") {
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

    struct Spawn {
        size_t entity;
        std::string cls;
        Vec3 o;
    };
    std::vector<Spawn> spawns;
    for (size_t e = 0; e < map.entities.size(); ++e) {
        const Entity& ent = map.entities[e];
        const std::string cls = ent.classname();
        if (!isSpawnClass(cls)) continue;
        const std::optional<Vec3> o = ent.origin();
        if (!o) {
            out.push_back({e, cls, "spawn has no origin", {}});
            continue;
        }
        spawns.push_back({e, cls, *o});
    }
    std::map<std::string, size_t> counts, targetnames;
    bool anyDm = false, hasBrMarker = false;
    size_t bombzoneTriggers = 0, hardpointZones = 0;
    for (const Entity& ent : geomMap->entities) {
        const std::string cls = ent.classname();
        ++counts[cls];
        if (startsWith(cls, "mp_dm_spawn")) anyDm = true;
        if (ent.isWorldspawn()) {
            if (const std::string* br = ent.value("iw8r_br"); br && !br->empty()) hasBrMarker = true;
        }
        if (cls == "iw8r_loot_point" || cls == "iw8r_loot_cluster" || cls == "iw8r_loot_zone") hasBrMarker = true;
        if (const std::string* go = ent.value("script_gameobjectname"); go && *go == "br") hasBrMarker = true;
        if (const std::string* tn = ent.value("targetname"); tn && !tn->empty()) {
            ++targetnames[*tn];
            if (*tn == "bombzone" && cls == "trigger_use_touch") ++bombzoneTriggers;
            if (*tn == "hardpoint_zone" && cls == "trigger_multiple") ++hardpointZones;
            if (*tn == "br_prematch_insertion_point") hasBrMarker = true;
        }
    }
    const auto count = [&](const char* name) -> size_t {
        const auto it = counts.find(name);
        return it == counts.end() ? 0 : it->second;
    };
    const auto tnCount = [&](const char* name) -> size_t {
        const auto it = targetnames.find(name);
        return it == targetnames.end() ? 0 : it->second;
    };
    const Vec3 modeAt = spawns.empty() ? Vec3{} : spawns.front().o;

    // Solid brushes (detail included) and patches from the map and instantiated prefabs.
    const std::vector<SolidBrush> solids = collidingBrushes(*geomMap, true);
    const std::vector<PatchTri> patches = collidingPatchTriangles(*geomMap);

    for (const Spawn& s : spawns) {
        AABB box;
        box.add(s.o + Vec3{-16, -16, 0});
        box.add(s.o + Vec3{16, 16, 72});
        bool overlapped = false;
        for (const SolidBrush& b : solids)
            if (boxOverlaps(box, b)) {
                out.push_back({s.entity, s.cls,
                               "player box (32x32x72 at the feet) overlaps a solid brush of entity " +
                                   std::to_string(b.entity),
                               s.o});
                overlapped = true;
                break;
            }
        if (!overlapped)
            for (const PatchTri& pt : patches)
                if (boxOverlapsPatch(box, pt)) {
                    out.push_back({s.entity, s.cls,
                                   "player box (32x32x72 at the feet) overlaps a solid patch of entity " +
                                       std::to_string(pt.entity),
                                   s.o});
                    break;
                }
        if (!hasFloor(s.o, solids, patches))
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

    // SD: checked when any SD spawn or bomb object exists.
    if (count("mp_sd_spawn_attacker") > 0 || count("mp_sd_spawn_defender") > 0 || bombzoneTriggers > 0 ||
        tnCount("sd_bomb") > 0 || tnCount("sd_bomb_pickup_trig") > 0) {
        if (count("mp_sd_spawn_attacker") < 1) modeIssue("SD: no mp_sd_spawn_attacker");
        if (count("mp_sd_spawn_defender") < 1) modeIssue("SD: no mp_sd_spawn_defender");
        if (bombzoneTriggers < 2)
            modeIssue("SD: " + std::to_string(bombzoneTriggers) + " bombzone trigger_use_touch, at least 2 required");
        if (tnCount("sd_bomb") < 1) modeIssue("SD: no sd_bomb entity");
        if (tnCount("sd_bomb_pickup_trig") < 1) modeIssue("SD: no sd_bomb_pickup_trig entity");
    }

    // CTF: checked when any CTF spawn or flag object exists.
    const bool anyCtf = count("mp_ctf_spawn") > 0 || count("mp_ctf_spawn_allies_start") > 0 || count("mp_ctf_spawn_axis_start") > 0 ||
                        tnCount("ctf_flag_allies") > 0 || tnCount("ctf_flag_axis") > 0 || tnCount("ctf_trig_allies") > 0 ||
                        tnCount("ctf_trig_axis") > 0 || tnCount("ctf_zone_allies") > 0 || tnCount("ctf_zone_axis") > 0;
    if (anyCtf) {
        if (count("mp_ctf_spawn") < 2)
            modeIssue("CTF: " + std::to_string(count("mp_ctf_spawn")) + " mp_ctf_spawn, at least 2 required");
        if (count("mp_ctf_spawn_allies_start") < 1) modeIssue("CTF: no mp_ctf_spawn_allies_start");
        if (count("mp_ctf_spawn_axis_start") < 1) modeIssue("CTF: no mp_ctf_spawn_axis_start");
        for (const char* name : {"ctf_flag_allies", "ctf_flag_axis", "ctf_trig_allies", "ctf_trig_axis", "ctf_zone_allies", "ctf_zone_axis"})
            if (tnCount(name) < 1) modeIssue(std::string("CTF: no ") + name + " entity");
    }

    // Hardpoint (KOTH): checked when any KOTH spawn or hardpoint_zone trigger exists.
    if (count("mp_koth_spawn") > 0 || count("mp_koth_spawn_allies_start") > 0 || count("mp_koth_spawn_axis_start") > 0 ||
        hardpointZones > 0) {
        if (count("mp_koth_spawn") < 2)
            modeIssue("KOTH: " + std::to_string(count("mp_koth_spawn")) + " mp_koth_spawn, at least 2 required");
        if (count("mp_koth_spawn_allies_start") < 1) modeIssue("KOTH: no mp_koth_spawn_allies_start");
        if (count("mp_koth_spawn_axis_start") < 1) modeIssue("KOTH: no mp_koth_spawn_axis_start");
        if (hardpointZones < 1) modeIssue("KOTH: no hardpoint_zone trigger_multiple");
    }

    // Warzone (BR): checked when BR markers (iw8r_br, loot, prematch insertion points, or br gameobjects) exist.
    if (hasBrMarker) {
        if (count("mp_dm_spawn_start") < 1) modeIssue("BR: no mp_dm_spawn_start");
        if (tnCount("br_prematch_insertion_point") < 1) modeIssue("BR: no br_prematch_insertion_point entity");
    }

    // script_model semantics (doc 34): x64-zt keeps a script_model as a live entity when it is a gameobject,
    // exploder, targeted by another entity, or driven by an iw8r_* mover/animation/no-collision key.
    {
        std::set<std::string> targeted;
        for (const auto& ent : geomMap->entities)
            if (const std::string* t = ent.value("target")) targeted.insert(*t);
        for (size_t e = 0; e < map.entities.size(); ++e) {
            const Entity& ent = map.entities[e];
            if (ent.classname() != "script_model") continue;
            const std::string* tn = ent.value("targetname");
            const auto hasNonEmpty = [&](const char* k) {
                const std::string* v = ent.value(k);
                return v && !v->empty();
            };
            // Names the x64-zt compatibility layer drives (IW3 _destructables, interactive objects) also stay live.
            const bool special = tn && (*tn == "exploder" || *tn == "oldschool_pickup" || *tn == "destructible" ||
                                        *tn == "destructable" || *tn == "interactive_tv");
            const bool iw8rLive = hasNonEmpty("iw8r_spin") || hasNonEmpty("iw8r_bob") || hasNonEmpty("iw8r_lift") ||
                                  hasNonEmpty("iw8r_anim") ||
                                  (ent.value("iw8r_no_collision") && *ent.value("iw8r_no_collision") == "1");
            if (ent.value("script_gameobjectname") || ent.value("script_exploder") || (tn && targeted.count(*tn)) || special || iw8rLive)
                continue;
            out.push_back({e, "script_model",
                           "script_model " + std::string(ent.value("model") ? *ent.value("model") : std::string("(no model)")) +
                               " is built as a static prop (no gameobject, exploder or targeting entity); use misc_model for decor",
                           ent.origin().value_or(Vec3{})});
        }
    }
    return out;
}

}  // namespace iw8r
