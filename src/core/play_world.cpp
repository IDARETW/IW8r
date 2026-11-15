#include "play_world.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

#include "convex_hull.h"
#include "prefab.h"

namespace iw8r {

namespace {

std::string lowered(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const std::string* nonEmpty(const Entity& e, const char* key) {
    const std::string* v = e.value(key);
    return v && !v->empty() ? v : nullptr;
}

}  // namespace

bool playModelCollides(const Entity& e, const MapFile& map) {
    const std::string cls = lowered(e.classname());
    if (cls == "misc_model") return true;
    if (cls != "script_model") return false;
    if (e.value("script_gameobjectname") || e.value("script_exploder")) return false;
    const std::string* tn = nonEmpty(e, "targetname");
    if (const std::string* nc = e.value("iw8r_no_collision"); nc && *nc == "1") return false;
    if (!tn) return true;
    if (e.value("iw8r_spin") || e.value("iw8r_bob") || e.value("iw8r_lift") || e.value("iw8r_anim")) return false;
    if (*tn == "exploder" || *tn == "oldschool_pickup" || *tn == "destructible" || *tn == "destructable" || *tn == "interactive_tv")
        return false;
    for (const Entity& other : map.entities)
        if (const std::string* t = other.value("target"); t && *t == *tn) return false;
    return true;
}

void buildPlayWorld(const MapFile& map, const PlayWorldOptions& opt, CollisionWorld& out, PlayWorldStats* stats) {
    PlayWorldStats st;
    // Prefabs: their contents as the build has them.
    bool anyPrefab = false;
    for (const Entity& e : map.entities) anyPrefab = anyPrefab || lowered(e.classname()) == "misc_prefab";
    MapFile flat;
    const MapFile* use = &map;
    if (anyPrefab) {
        std::string err;
        if (flattenPrefabs(map, opt.mapPath, opt.prefabRoots, flat, &st.warnings, &err)) {
            use = &flat;
            for (const Entity& e : map.entities) st.prefabs += lowered(e.classname()) == "misc_prefab";
        } else {
            st.warnings.push_back("prefabs not expanded for the play preview: " + err);
        }
    }
    out.build(*use);

    // Placed models: convex pieces (file Y-up coordinates) per model name, placed like the build does.
    std::map<const ImportedModel*, std::vector<std::vector<Plane>>> pieces;
    for (const Entity& e : use->entities) {
        if (!playModelCollides(e, *use)) continue;
        const std::string* mv = nonEmpty(e, "model");
        if (!mv || (*mv)[0] == '*' || !opt.modelOf) continue;
        const std::shared_ptr<const ImportedModel> m = opt.modelOf(e);
        if (!m || !m->bounds.valid()) continue;
        auto it = pieces.find(m.get());
        if (it == pieces.end()) {
            // The render meshes, as compile_iw8.cpp's piecesOf (it does not read a glTF's collision nodes either).
            it = pieces.emplace(m.get(), convexPieces(*m, opt.maxPieces, size_t(std::max(0, opt.maxPlanes)))).first;
        }
        Vec3 ang;
        if (const std::string* a = e.value("angles")) std::sscanf(a->c_str(), "%lf %lf %lf", &ang.x, &ang.y, &ang.z);
        const Mat3 r = anglesToMatrix(ang);
        double ms = 1.0;
        if (const std::string* s = e.value("modelscale")) ms = std::max(0.001, std::atof(s->c_str()));
        const double scale = (opt.modelScale > 0 ? opt.modelScale : 1.0) * ms;
        const Vec3 o = e.origin().value_or(Vec3{});
        ++st.models;
        if (!it->second.empty()) {
            for (const auto& piece : it->second) {
                // File (Y-up) normal n -> map normal (x, -z, y), a proper rotation; then placed: n' = R nz,
                // d' = dot(n', o) + scale * d.
                std::vector<Plane> planes;
                for (const Plane& pl : piece) {
                    const Vec3 n = r * Vec3{pl.n.x, -pl.n.z, pl.n.y};
                    planes.push_back({n, dot(n, o) + scale * pl.d});
                }
                if (out.addConvex(std::move(planes))) ++st.hullPieces;
            }
            continue;
        }
        // No hull (a flat or degenerate mesh): the oriented box of the model, at least 2 units thick, as the build.
        const AABB& b = m->bounds;
        double lo[3] = {b.mins.x, -b.maxs.z, b.mins.y}, hi[3] = {b.maxs.x, -b.mins.z, b.maxs.y};
        std::vector<Plane> planes;
        for (int k = 0; k < 3; ++k) {
            double a = lo[k] * scale, c = hi[k] * scale;
            if (c - a < 2) a -= 1, c += 1;
            Vec3 axis;
            axis[k] = 1;
            const Vec3 n = r * axis;
            planes.push_back({n, dot(n, o) + c});
            planes.push_back({n * -1.0, -(dot(n, o) + a)});
        }
        if (out.addConvex(std::move(planes))) ++st.boxes;
    }
    if (stats) *stats = std::move(st);
}

}  // namespace iw8r
