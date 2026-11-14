#include "brush_flags.h"
#include "game_lightmap.h"
#include "scene.h"
#include "patch_covers.h"
#include "light_grid.h"
#include "lightmap_fields.h"
#include "texel_density.h"
#include "patch_ops.h"
#include "vertex_paint.h"

#include <map>
#include <set>

#include "patch_tess.h"
#include "brush_ops.h"
#include "map_script.h"
#include "compile_iw8.h"
#include "prefab.h"
#include "scriptable_model.h"
#include "tex_projection.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace iw8r::app {

namespace {

uint32_t packRGBA(double r, double g, double b, double a = 1.0) {
    auto c = [](double v) { return uint32_t(std::clamp(v, 0.0, 1.0) * 255.0 + 0.5); };
    return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
}

uint32_t fnv1a(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char ch : s) h = (h ^ ch) * 16777619u;
    return h;
}

void hsv(double h, double s, double v, double& r, double& g, double& b) {
    h = std::fmod(h, 1.0) * 6.0;
    const int i = int(h);
    const double f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
}

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// Tool volumes (triggers, clips, hint, skip) are see-through in Radiant: the camera draws them as wire boxes so the
// geometry behind stays visible. Matched on the last path component.
bool isVolumeMaterial(const std::string& material) {
    std::string m;
    for (char c : material) m += char(std::tolower(static_cast<unsigned char>(c)));
    const size_t slash = m.find_last_of('/');
    if (slash != std::string::npos) m = m.substr(slash + 1);
    return m.rfind("trigger", 0) == 0 || m.rfind("clip", 0) == 0 || m == "hint" || m == "skip";
}

void addBox(PrimGeom& g, const Vec3& mn, const Vec3& mx) {
    const Vec3 c[8] = {{mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
                       {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}};
    const int f[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {2, 3, 7, 6}, {1, 2, 6, 5}, {3, 0, 4, 7}};
    const Vec3 n[6] = {{0, 0, -1}, {0, 0, 1}, {0, -1, 0}, {0, 1, 0}, {1, 0, 0}, {-1, 0, 0}};
    for (int i = 0; i < 6; ++i) {
        g.polys.push_back({c[f[i][0]], c[f[i][1]], c[f[i][2]], c[f[i][3]]});
        g.faceNormals.push_back(n[i]);
    }
    g.bounds.add(mn);
    g.bounds.add(mx);
}

// Octahedron (lights, probes, effects, path nodes): 8 triangles wound counter-clockwise seen from outside.
void addOctahedron(PrimGeom& g, const Vec3& c, double rx, double ry, double rz) {
    const Vec3 px = c + Vec3{rx, 0, 0}, nx = c - Vec3{rx, 0, 0}, py = c + Vec3{0, ry, 0}, ny = c - Vec3{0, ry, 0};
    const Vec3 pz = c + Vec3{0, 0, rz}, nz = c - Vec3{0, 0, rz};
    const Vec3 tri[8][3] = {{px, py, pz}, {py, nx, pz}, {nx, ny, pz}, {ny, px, pz},
                            {py, px, nz}, {nx, py, nz}, {ny, nx, nz}, {px, ny, nz}};
    for (const auto& t : tri) {
        Vec3 n = normalize(cross(t[1] - t[0], t[2] - t[0]));
        if (dot(n, (t[0] + t[1] + t[2]) * (1.0 / 3.0) - c) < 0) {
            g.polys.push_back({t[0], t[2], t[1]});
            n = n * -1.0;
        } else {
            g.polys.push_back({t[0], t[1], t[2]});
        }
        g.faceNormals.push_back(n);
    }
    g.bounds.add(c - Vec3{rx, ry, rz});
    g.bounds.add(c + Vec3{rx, ry, rz});
}

std::optional<double> rayTriangle(const Ray& r, const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 e1 = b - a, e2 = c - a;
    const Vec3 p = cross(r.dir, e2);
    const double det = dot(e1, p);
    if (std::fabs(det) < 1e-12) return std::nullopt;
    const double inv = 1.0 / det;
    const Vec3 s = r.origin - a;
    const double u = dot(s, p) * inv;
    if (u < 0 || u > 1) return std::nullopt;
    const Vec3 q = cross(s, e1);
    const double v = dot(r.dir, q) * inv;
    if (v < 0 || u + v > 1) return std::nullopt;
    const double t = dot(e2, q) * inv;
    if (t < 0) return std::nullopt;
    return t;
}

PrimGeom buildPrim(const Entity& e, const EntityItem& it, const Primitive& p) {
    PrimGeom g;
    g.ref = {e.id, it.id};
    g.rev = it.rev;
    g.brushEntity = !e.isWorldspawn();
    if (p.kind == PrimKind::Brush) {
        g.kind = PrimGeom::Brush;
        g.planes = p.planes();
        {
            const BrushFlags bf = readBrushFlags(p);
            g.contentsColor = g.brushEntity ? 0xffd060c0u : bf.nonColliding ? 0xffe0a040u : bf.weaponClip ? 0xff40d0e0u
                              : bf.detail ? 0xff60c060u : 0xffa0a0a0u;
        }
        // One poly per face index (faceWindingsOf keeps Primitive::faces order) with its material and UVs.
        auto windings = faceWindingsOf(p);
        for (size_t i = 0; i < windings.size(); ++i) {
            if (windings[i].size() < 3) continue;
            const auto pl = facePlane(p, i);
            if (!pl) continue;
            const TexParams tp = texParamsOfFace(p, i).value_or(TexParams{});
            std::vector<std::array<float, 2>> uv;
            uv.reserve(windings[i].size());
            for (const auto& v : windings[i]) {
                g.bounds.add(v);
                const auto t = faceUV(pl->n, tp, v);
                uv.push_back({float(t[0]), float(t[1])});
            }
            g.faceNormals.push_back(pl->n);
            g.polys.push_back(std::move(windings[i]));
            g.polyMaterial.push_back(p.faces[i].material());
            g.polyUV.push_back(std::move(uv));
            g.polyRepeatsPerUnit.push_back(float(faceDensity(p, i, 1, 1, 0).texturePxPerUnit));
            g.polyLmDensity.resize(g.polys.size() - 1, 1.0f);
            g.polyLmDensity.push_back(float(lightmapDensityOfSuffix(p.faces[i].suffix)));
        }
        g.label = p.faces.empty() ? std::string() : p.faces.front().material();
        g.volume = !p.faces.empty() && std::all_of(p.faces.begin(), p.faces.end(), [](const auto& f) { return isVolumeMaterial(f.material()); });
        {
            const BrushFlags bf = readBrushFlags(p);
            const std::string cls = e.classname();
            const bool trigger = cls.rfind("trigger", 0) == 0;
            // A volume collides only when it is player clip (clip / playerclip materials); trigger, hint, skip do not.
            bool clipVolume = false;
            for (const auto& f : p.faces) {
                const std::string m = f.material();
                if (m.find("clip") != std::string::npos && m.find("missile") == std::string::npos) clipVolume = true;
            }
            g.collides = !bf.nonColliding && !trigger && (!g.volume || clipVolume);
        }
    } else {
        g.kind = PrimGeom::Patch;
        g.contentsColor = g.brushEntity ? 0xffd060c0u : 0xffc0c040u;
        g.collides = e.classname().rfind("trigger", 0) != 0;
        // Surface: biquadratic curve evaluation for "curve", the control grid for "mesh"/"terrain".
        const PatchMesh mesh = tessellatePrimitive(p, 6);
        const std::vector<Rgba8> paint = patchHasColours(p) ? tessellatePatchColours(p, 6) : std::vector<Rgba8>{};
        const std::vector<std::array<double, 2>> stUv = tessellatePatchUVs(p, 6);  // the patch's own UVs (repeats)
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const Vec3& a = mesh.points[mesh.indices[i]];
            const Vec3& b = mesh.points[mesh.indices[i + 1]];
            const Vec3& c = mesh.points[mesh.indices[i + 2]];
            g.polys.push_back({a, b, c});
            if (paint.size() == mesh.points.size()) {
                uint32_t sum[3] = {0, 0, 0};
                for (int k = 0; k < 3; ++k)
                    for (int ch = 0; ch < 3; ++ch) sum[ch] += paint[mesh.indices[i + size_t(k)]][size_t(ch)];
                g.polyTint.push_back(0xff000000u | (sum[0] / 3) | ((sum[1] / 3) << 8) | ((sum[2] / 3) << 16));
            }
            const Vec3 n = normalize(cross(b - a, c - a));
            g.faceNormals.push_back(n);
            // Patches: their control-point st tessellated like the positions; planar projection when they have none.
            std::vector<std::array<float, 2>> uv;
            if (stUv.size() == mesh.points.size()) {
                for (size_t k = 0; k < 3; ++k) {
                    const auto& t = stUv[mesh.indices[i + k]];
                    uv.push_back({float(t[0]), float(t[1])});
                }
            } else {
                const TexParams tp;
                for (const Vec3* q : {&a, &b, &c}) {
                    const auto t = faceUV(n, tp, *q);
                    uv.push_back({float(t[0]), float(t[1])});
                }
            }
            g.polyMaterial.push_back(p.patchMaterial);
            g.polyUV.push_back(std::move(uv));
        }
        // Wireframe: the control net (what the mapper edits).
        int rows = 0, cols = 0;
        for (const auto& v : p.verts) {
            rows = std::max(rows, v.row + 1);
            cols = std::max(cols, v.col + 1);
        }
        std::vector<const PatchVertex*> grid(size_t(rows * cols), nullptr);
        for (const auto& v : p.verts)
            if (v.row < rows && v.col < cols) grid[size_t(v.row * cols + v.col)] = &v;
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) {
                const PatchVertex* q = grid[size_t(r * cols + c)];
                if (!q) continue;
                if (c + 1 < cols && grid[size_t(r * cols + c + 1)]) g.edges.push_back({q->pos, grid[size_t(r * cols + c + 1)]->pos});
                if (r + 1 < rows && grid[size_t((r + 1) * cols + c)]) g.edges.push_back({q->pos, grid[size_t((r + 1) * cols + c)]->pos});
            }        for (const auto& v : p.verts) g.bounds.add(v.pos);
        g.label = p.patchMaterial;
    }
    g.color = g.brushEntity ? colorForClass(e.classname()) : colorForName(g.label);
    return g;
}

// A custom model drawn as its mesh, with the same axis change and scale the compiler writes (model_import.cpp).
// The entity as the map script would have it `t` seconds in: iw8r_spin "<seconds> [yaw|pitch|roll]" turns it,
// iw8r_bob "<height> [seconds]" lifts it and lets it down (map_script.cpp iw8r_spin_model / iw8r_bob_model).
Entity behaviourPose(const Entity& e, double t) {
    Entity out = e;
    const auto setKey = [&](const char* key, const std::string& value) {
        for (auto& it : out.items)
            if (KeyValueLine* kv = it.kv(); kv && kv->name() == key) {
                kv->set(value);
                return;
            }
        EntityItem item;
        item.v = makeKeyValue(key, value, std::string(1, char(10)), out.usesCanonicalKeys());
        out.items.push_back(std::move(item));
    };
    if (const std::string* sp = e.value("iw8r_spin")) {
        double seconds = 0;
        char axis[16] = "yaw";
        if (std::sscanf(sp->c_str(), "%lf %15s", &seconds, axis) >= 1 && seconds != 0) {
            double a[3] = {0, 0, 0};
            if (const std::string* av = e.value("angles")) std::sscanf(av->c_str(), "%lf %lf %lf", &a[0], &a[1], &a[2]);
            const std::string ax = axis;
            const int idx = ax == "pitch" ? 0 : ax == "roll" ? 2 : 1;
            a[idx] = std::fmod(a[idx] + 360.0 * t / std::fabs(seconds), 360.0);
            setKey("angles", formatNumber(a[0]) + " " + formatNumber(a[1]) + " " + formatNumber(a[2]));
        }
    }
    if (const std::string* bo = e.value("iw8r_bob")) {
        double height = 0, seconds = 2;
        if (std::sscanf(bo->c_str(), "%lf %lf", &height, &seconds) >= 1 && seconds > 0) {
            if (auto o = e.origin()) {
                const double z = o->z + height * (0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * t / seconds));
                setKey("origin", formatNumber(o->x) + " " + formatNumber(o->y) + " " + formatNumber(z));
            }
        }
    }
    return out;
}

// Behaviour Preview for brush models (script_brushmodel): where the map script would have the brushes `t` seconds in.
// iw8r_lift "<dz> [time] [wait]" goes up and down, iw8r_bob bobs, iw8r_door "slide x y z [time] [hold]" or
// "rotate <yaw> [time] [hold]" opens, holds and closes in a loop, iw8r_spin turns about the vertical axis. Turning is
// about the entity's origin key (the map origin without one), as the game turns a brush model.
struct BrushPose {
    bool any = false;
    Vec3 delta{0, 0, 0};
    double yawDeg = 0;
    Vec3 pivot{0, 0, 0};
};

BrushPose brushBehaviourPose(const Entity& e, double t) {
    BrushPose p;
    if (const auto o = e.origin()) p.pivot = *o;
    const auto ease = [](double f) { return 0.5 - 0.5 * std::cos(3.14159265358979 * std::clamp(f, 0.0, 1.0)); };
    // 0 closed, 1 open: rest `rest`, move `move`, hold `hold`, move back `move`.
    const auto cycle = [&](double rest, double move, double hold) {
        const double period = rest + move + hold + move;
        const double x = std::fmod(t, period);
        if (x < rest) return 0.0;
        if (x < rest + move) return ease((x - rest) / move);
        if (x < rest + move + hold) return 1.0;
        return 1.0 - ease((x - rest - move - hold) / move);
    };
    if (const std::string* v = e.value("iw8r_lift")) {
        double dz = 0, time = 2, wait = 2;
        if (std::sscanf(v->c_str(), "%lf %lf %lf", &dz, &time, &wait) >= 1 && dz != 0) {
            p.delta.z += dz * cycle(std::max(0.0, wait), std::max(0.05, time), std::max(0.0, wait));
            p.any = true;
        }
    }
    if (const std::string* v = e.value("iw8r_bob")) {
        double h = 0, seconds = 2;
        if (std::sscanf(v->c_str(), "%lf %lf", &h, &seconds) >= 1 && seconds > 0) {
            p.delta.z += h * (0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * t / seconds));
            p.any = true;
        }
    }
    if (const std::string* v = e.value("iw8r_door")) {
        double a = 0, b = 0, c = 0, time = 1, hold = 3;
        if (std::sscanf(v->c_str(), "slide %lf %lf %lf %lf %lf", &a, &b, &c, &time, &hold) >= 3) {
            const double k = cycle(1.0, std::max(0.05, time), std::max(0.0, hold));
            p.delta = p.delta + Vec3{a, b, c} * k;
            p.any = true;
        } else if (std::sscanf(v->c_str(), "rotate %lf %lf %lf", &a, &time, &hold) >= 1 ||
                   std::sscanf(v->c_str(), "hinge %lf %lf", &a, &time) >= 1) {
            // Hinged: about the hinge at one end of the door (core doorHingeOf), opening to the +normal side.
            const DoorHinge hg = doorHingeOf(e);
            if (hg.ok) p.pivot = hg.pos;
            p.yawDeg += std::fabs(a) * hg.swing * cycle(1.0, std::max(0.05, time), std::max(0.0, hold));
            p.any = true;
        }
    }
    if (const std::string* v = e.value("iw8r_spin")) {
        double seconds = 0;
        char axis[16] = "yaw";
        if (std::sscanf(v->c_str(), "%lf %15s", &seconds, axis) >= 1 && seconds != 0 && std::string(axis) == "yaw") {
            p.yawDeg += std::fmod(360.0 * t / std::fabs(seconds), 360.0);
            p.any = true;
        }
    }
    return p;
}

void applyBrushPose(PrimGeom& g, const BrushPose& pose) {
    const double a = pose.yawDeg * 3.14159265358979 / 180.0, cs = std::cos(a), sn = std::sin(a);
    const auto turn = [&](const Vec3& v) { return Vec3{v.x * cs - v.y * sn, v.x * sn + v.y * cs, v.z}; };
    const auto move = [&](const Vec3& v) { return pose.pivot + turn(v - pose.pivot) + pose.delta; };
    AABB b;
    for (auto& poly : g.polys)
        for (Vec3& v : poly) {
            v = move(v);
            b.add(v);
        }
    for (auto& [x, y] : g.edges) {
        x = move(x);
        y = move(y);
    }
    for (Vec3& n : g.faceNormals) n = turn(n);
    for (Plane& pl : g.planes) {
        // dot(n, x) = d holds for the moved points: n turns, d follows a point of the plane.
        const Vec3 on = move(pl.n * pl.d);
        pl.n = turn(pl.n);
        pl.d = dot(pl.n, on);
    }
    if (b.valid()) g.bounds = b;
}

// The model's triangles placed at the entity's origin / angles / modelscale, appended to g (polys, normals, bounds,
// and materials with texture coordinates where the mesh has them).
void appendModelPolys(PrimGeom& g, const Entity& e, const ImportedModel& m, double scale) {
    const Vec3 o = e.origin().value_or(Vec3{});
    Vec3 ang;
    if (const std::string* a = e.value("angles")) std::sscanf(a->c_str(), "%lf %lf %lf", &ang.x, &ang.y, &ang.z);
    const Mat3 r = anglesToMatrix(ang);
    double ms = 1.0;
    if (const std::string* s = e.value("modelscale")) ms = std::max(0.001, std::atof(s->c_str()));
    auto xf = [&](const Vec3& p) { return o + r * (Vec3(p.x, -p.z, p.y) * (scale * ms)); };
    // iw8r_material: one material on every surface of this placement (the build makes the same copy).
    const std::string* overrideMat = e.value("iw8r_material");
    if (overrideMat && overrideMat->empty()) overrideMat = nullptr;
    // 12.7: "iw8r_mat:<model material>" "<replacement>" replaces one slot (it wins over iw8r_material for that slot).
    std::map<std::string, std::string> slotMats;
    for (const auto& it : e.items)
        if (const KeyValueLine* kv = it.kv()) {
            const std::string k = kv->name();
            if (k.rfind("iw8r_mat:", 0) == 0 && k.size() > 9 && !kv->value.empty()) {
                std::string from = k.substr(9);
                for (char& ch : from) ch = char(std::tolower(static_cast<unsigned char>(ch)));
                slotMats[from] = kv->value;
            }
        }
    const auto materialFor = [&](const std::string& meshMaterial) -> const std::string& {
        if (!slotMats.empty()) {
            std::string key = meshMaterial;
            for (char& ch : key) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            if (const auto s = slotMats.find(key); s != slotMats.end()) return s->second;
        }
        return overrideMat ? *overrideMat : meshMaterial;
    };
    for (const auto& mesh : m.meshes)
        for (size_t i = 0; i + 2 < mesh.idx.size(); i += 3) {
            const Vec3 a = xf(mesh.pos[mesh.idx[i]]), b = xf(mesh.pos[mesh.idx[i + 1]]), c = xf(mesh.pos[mesh.idx[i + 2]]);
            g.polys.push_back({a, b, c});
            g.faceNormals.push_back(normalize(cross(b - a, c - a)));
            if (!mesh.material.empty() && mesh.uv.size() == mesh.pos.size()) {
                // Stock models carry their xmodel material names (model cache): textured like brush faces.
                g.polyMaterial.resize(g.polys.size() - 1);
                g.polyUV.resize(g.polys.size() - 1);
                g.polyMaterial.push_back(materialFor(mesh.material));
                std::vector<std::array<float, 2>> uv;
                for (size_t k = 0; k < 3; ++k) {
                    const auto& t = mesh.uv[mesh.idx[i + k]];
                    uv.push_back({float(t[0]), float(t[1])});
                }
                g.polyUV.push_back(std::move(uv));
            }
            g.bounds.add(a);
            g.bounds.add(b);
            g.bounds.add(c);
        }
}

PrimGeom buildModelGeom(const Entity& e, const ImportedModel& m, double scale) {
    PrimGeom g;
    g.ref = {e.id, kNoId};
    g.kind = PrimGeom::PointEntity;
    g.prefab = true;  // drawn by its polys, picked by bounds
    g.mesh = true;
    for (const auto& it : e.items) g.rev = std::max(g.rev, it.rev + 1);
    g.label = "model:" + m.name;
    g.collides = true;  // the build gives placed models collision boxes or convex hulls (compile_iw8 modelCollisionBoxes)
    g.color = colorForClass(e.classname());
    appendModelPolys(g, e, m, scale);
    const Vec3 o = e.origin().value_or(Vec3{});
    if (!g.bounds.valid()) addBox(g, o - Vec3{8, 8, 8}, o + Vec3{8, 8, 8});
    return g;
}

// Classes the build places as models (compile_iw8: misc_model, script_model, dynamic_prop).
bool isModelClass(const std::string& cls) { return cls == "misc_model" || cls == "script_model" || cls == "dynamic_prop"; }

// An entity made of a prefab's key list (instantiatePrefab), carrying the id of the placement that draws it.
Entity entityFromKeys(const std::vector<std::pair<std::string, std::string>>& kvs, Id id) {
    Entity e;
    e.id = id;
    for (const auto& [k, v] : kvs) {
        EntityItem it;
        it.v = makeKeyValue(k, v, std::string(1, char(10)));
        e.items.push_back(std::move(it));
    }
    return e;
}

// The parts of a prefab's contents, in the order the prefab item keeps them (see PrimGeom::meshFrom).
struct PrefabParts {
    PrimGeom faces, meshes, markers;
    std::vector<PrimGeom::SourceDep> deps;
};

constexpr int kPrefabDepth = 8;  // nesting levels, as the build expands them (core flattenPrefabs)

// One prefab placement's contents in world space, prefabs inside it included. Point entities with a model are drawn
// as the model at their (already placed) origin / angles and their own modelscale, which is what the build makes
// of them: the placement's modelscale scales the positions only (core transformKeys keeps modelscale as written).
void addPrefabContents(PrefabParts& out, const SceneCache& sc, Id owner, const PrefabInstance& inst, const MapFile& prefab, int depth) {
    const PrefabContents c = instantiatePrefab(prefab, inst, "\n");
    PrimGeom& g = out.faces;
    // Faces carry their material and texture coordinates like a brush or patch of the map itself (buildPrim), so a
    // placed prefab is textured in the camera view. The primitives are already in world space with the placement's
    // material swaps applied (instantiatePrefab), which is what the build textures.
    auto addPrim = [&](const Primitive& p) {
        if (p.kind == PrimKind::Brush) {
            auto windings = faceWindingsOf(p);
            for (size_t i = 0; i < windings.size(); ++i) {
                if (windings[i].size() < 3) continue;
                const auto pl = facePlane(p, i);
                if (!pl) continue;
                const TexParams tp = texParamsOfFace(p, i).value_or(TexParams{});
                std::vector<std::array<float, 2>> uv;
                uv.reserve(windings[i].size());
                for (const auto& v : windings[i]) {
                    g.bounds.add(v);
                    const auto t = faceUV(pl->n, tp, v);
                    uv.push_back({float(t[0]), float(t[1])});
                }
                g.faceNormals.push_back(pl->n);
                g.polys.push_back(std::move(windings[i]));
                g.polyMaterial.push_back(p.faces[i].material());
                g.polyUV.push_back(std::move(uv));
            }
        } else if (p.kind == PrimKind::Patch) {
            const PatchMesh mesh = tessellatePrimitive(p, 4);
            const std::vector<std::array<double, 2>> stUv = tessellatePatchUVs(p, 4);
            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                const Vec3& a = mesh.points[mesh.indices[i]];
                const Vec3& b = mesh.points[mesh.indices[i + 1]];
                const Vec3& cc = mesh.points[mesh.indices[i + 2]];
                const Vec3 n = normalize(cross(b - a, cc - a));
                std::vector<std::array<float, 2>> uv;
                if (stUv.size() == mesh.points.size()) {
                    for (size_t k = 0; k < 3; ++k) {
                        const auto& t = stUv[mesh.indices[i + k]];
                        uv.push_back({float(t[0]), float(t[1])});
                    }
                } else {
                    const TexParams tp;
                    for (const Vec3* q : {&a, &b, &cc}) {
                        const auto t = faceUV(n, tp, *q);
                        uv.push_back({float(t[0]), float(t[1])});
                    }
                }
                g.polys.push_back({a, b, cc});
                g.faceNormals.push_back(n);
                g.polyMaterial.push_back(p.patchMaterial);
                g.polyUV.push_back(std::move(uv));
                g.bounds.add(a);
                g.bounds.add(b);
                g.bounds.add(cc);
            }
        }
    };
    for (const auto& p : c.worldPrims) addPrim(p);
    for (const auto& [kvs, prims] : c.entities) {
        for (const auto& p : prims) addPrim(p);
        if (!prims.empty()) continue;
        Vec3 o;
        bool hasOrigin = false;
        for (const auto& [k, v] : kvs)
            if (k == "origin" && std::sscanf(v.c_str(), "%lf %lf %lf", &o.x, &o.y, &o.z) == 3) hasOrigin = true;
        if (!hasOrigin) continue;
        Entity ie = entityFromKeys(kvs, owner);
        const std::string cls = ie.classname();
        if (cls == "misc_prefab" && sc.prefabSource && depth < kPrefabDepth) {
            if (std::optional<PrefabInstance> innerInst = prefabInstance(ie)) {
                std::shared_ptr<const MapFile> inner = sc.prefabSource(ie);
                out.deps.push_back({std::make_shared<const Entity>(ie), inner, true});
                if (inner) {
                    // As the build: the inner prefab is expanded in this prefab's space and then placed with it, so
                    // its scale multiplies, this placement's material swaps apply after its own and its key
                    // overrides after the inner ones.
                    innerInst->scale *= inst.scale > 0 ? inst.scale : 1;
                    std::vector<std::pair<std::string, std::string>> swaps;
                    for (const auto& [from, to] : innerInst->materialSwaps) {
                        std::string dest = to;
                        for (const auto& [of, ot] : inst.materialSwaps)
                            if (of == dest) {
                                dest = ot;
                                break;
                            }
                        swaps.push_back({from, dest});
                    }
                    for (const auto& s : inst.materialSwaps) swaps.push_back(s);
                    innerInst->materialSwaps = std::move(swaps);
                    for (const auto& ov : inst.overrides) innerInst->overrides.push_back(ov);
                    addPrefabContents(out, sc, owner, *innerInst, *inner, depth + 1);
                    continue;
                }
            }
        }
        const std::string scriptable = cls == "scriptable" ? scriptableModelName(ie, sc.scriptableModelFor) : std::string();
        if (!scriptable.empty()) ie = withModelKey(ie, scriptable);
        const std::string* modelKey = ie.value("model");
        if (sc.modelSource && (isModelClass(cls) || !scriptable.empty()) && modelKey && !modelKey->empty()) {
            std::shared_ptr<const ImportedModel> m = sc.modelSource(ie);
            out.deps.push_back({std::make_shared<const Entity>(ie), m, false});
            if (m) {
                const size_t before = out.meshes.polys.size();
                appendModelPolys(out.meshes, ie, *m, sc.modelScale);
                if (out.meshes.polys.size() > before) continue;
            }
        }
        addBox(out.markers, o - Vec3{8, 8, 8}, o + Vec3{8, 8, 8});  // a marker until its model arrives
    }
}

// A misc_prefab drawn as its contents (brush windings, tessellated patches, inner models as their meshes, boxes for
// other point entities), transformed by the instance origin/angles/scale. Selected and picked as the one entity.
PrimGeom buildPrefab(const Entity& e, const PrefabInstance& inst, const std::shared_ptr<const MapFile>& prefab, const SceneCache& sc) {
    PrimGeom g;
    g.ref = {e.id, kNoId};
    g.kind = PrimGeom::PointEntity;
    g.prefab = true;
    for (const auto& it : e.items) g.rev = std::max(g.rev, it.rev + 1);
    g.label = "misc_prefab:" + inst.model;
    g.color = packRGBA(0.55, 0.85, 0.95);
    PrefabParts parts;
    parts.deps.push_back({nullptr, prefab, true});
    addPrefabContents(parts, sc, e.id, inst, *prefab, 1);
    g.polys = std::move(parts.faces.polys);
    g.faceNormals = std::move(parts.faces.faceNormals);
    g.bounds = parts.faces.bounds;
    // Materials and texture coordinates line up with the polys: the faces' own, then the models'.
    g.polyMaterial = std::move(parts.faces.polyMaterial);
    g.polyUV = std::move(parts.faces.polyUV);
    g.polyMaterial.resize(g.polys.size());
    g.polyUV.resize(g.polys.size());
    if (!parts.meshes.polys.empty()) {
        g.meshFrom = uint32_t(g.polys.size());
        if (!parts.meshes.polyMaterial.empty()) {
            for (auto& m : parts.meshes.polyMaterial) g.polyMaterial.push_back(std::move(m));
            for (auto& uv : parts.meshes.polyUV) g.polyUV.push_back(std::move(uv));
        }
        for (auto& w : parts.meshes.polys) g.polys.push_back(std::move(w));
        for (const Vec3& n : parts.meshes.faceNormals) g.faceNormals.push_back(n);
        if (parts.meshes.bounds.valid()) g.bounds.add(parts.meshes.bounds);
    }
    if (!parts.markers.polys.empty()) {
        g.markerFrom = uint32_t(g.polys.size());
        for (auto& w : parts.markers.polys) g.polys.push_back(std::move(w));
        for (const Vec3& n : parts.markers.faceNormals) g.faceNormals.push_back(n);
        g.bounds.add(parts.markers.bounds);
    }
    if (!g.bounds.valid()) {
        g.markerFrom = uint32_t(g.polys.size());
        addBox(g, inst.origin - Vec3{8, 8, 8}, inst.origin + Vec3{8, 8, 8});
    }
    g.deps = std::move(parts.deps);
    return g;
}

// True when one of a prefab item's sources gives something else now than when it was built.
bool prefabSourcesChanged(const PrimGeom& g, const SceneCache& sc, const MapFile* placed) {
    for (const PrimGeom::SourceDep& d : g.deps) {
        const void* now = nullptr;
        if (!d.entity) now = placed;
        else if (d.prefab) now = sc.prefabSource ? sc.prefabSource(*d.entity).get() : nullptr;
        else now = sc.modelSource ? sc.modelSource(*d.entity).get() : nullptr;
        if (now != d.resolved.get()) return true;
    }
    return false;
}

PrimGeom buildPointEntity(const Entity& e) {
    PrimGeom g;
    g.ref = {e.id, kNoId};
    g.kind = PrimGeom::PointEntity;
    for (const auto& it : e.items) g.rev = std::max(g.rev, it.rev + 1);
    const std::string cls = e.classname();
    g.label = cls;
    g.color = colorForClass(cls);
    const Vec3 o = e.origin().value_or(Vec3{});
    const bool playerSized = (startsWith(cls, "mp_") && cls.find("spawn") != std::string::npos) || cls == "info_player_start" ||
                             cls == "mp_global_intermission";
    // Shapes tell entity kinds apart in the camera (Radiant style): spawns are player boxes, lights and probes
    // octahedrons (lights in their _color), effects tall diamonds, path nodes flat diamonds, other points small cubes.
    if (playerSized) {
        addBox(g, o + Vec3{-16, -16, 0}, o + Vec3{16, 16, 72});
    } else if (startsWith(cls, "node_")) {
        addOctahedron(g, o + Vec3{0, 0, 4}, 12, 12, 4);
    } else if (startsWith(cls, "light")) {
        addOctahedron(g, o, 12, 12, 12);
        if (const std::string* c = e.value("_color")) {
            double r = 1, gg = 1, b = 1;
            if (std::sscanf(c->c_str(), "%lf %lf %lf", &r, &gg, &b) == 3) {
                const double m = std::max({r, gg, b, 1e-6});
                g.color = packRGBA(std::clamp(r / m, 0.0, 1.0), std::clamp(gg / m, 0.0, 1.0), std::clamp(b / m, 0.0, 1.0));
            }
        }
    } else if (cls == "reflection_probe") {
        addOctahedron(g, o, 16, 16, 16);
    } else if (cls == "fx_origin") {
        addOctahedron(g, o + Vec3{0, 0, 8}, 6, 6, 14);
        g.color = packRGBA(0.95, 0.45, 0.15);
    } else if (cls == "sound_emitter") {
        addOctahedron(g, o, 10, 10, 10);
        g.color = packRGBA(0.45, 0.95, 0.55);
    } else if (cls == "script_origin" || cls == "script_struct") {
        addBox(g, o - Vec3{4, 4, 4}, o + Vec3{4, 4, 4});
    } else {
        addBox(g, o - Vec3{8, 8, 8}, o + Vec3{8, 8, 8});
    }
    if (const std::string* a = e.value("angles")) {
        double pitch = 0, yaw = 0, roll = 0;
        if (std::sscanf(a->c_str(), "%lf %lf %lf", &pitch, &yaw, &roll) >= 2) {
            const double py = pitch * 3.14159265358979 / 180.0, yy = yaw * 3.14159265358979 / 180.0;
            const Vec3 fwd{std::cos(py) * std::cos(yy), std::cos(py) * std::sin(yy), -std::sin(py)};
            g.arrowFrom = g.bounds.center();
            g.arrowTo = g.arrowFrom + fwd * 40.0;
            g.hasArrow = true;
        }
    } else if (const std::string* yawText = e.value("angle")) {
        const double yy = std::atof(yawText->c_str()) * 3.14159265358979 / 180.0;
        g.arrowFrom = g.bounds.center();
        g.arrowTo = g.arrowFrom + Vec3{std::cos(yy), std::sin(yy), 0} * 40.0;
        g.hasArrow = true;
    }
    return g;
}

}  // namespace

uint32_t colorForName(const std::string& name) {
    const uint32_t h = fnv1a(name);
    double r, g, b;
    hsv((h & 0xffff) / 65535.0, 0.25 + ((h >> 16) & 0xff) / 255.0 * 0.25, 0.70 + ((h >> 24) & 0x3f) / 63.0 * 0.2, r, g, b);
    return packRGBA(r, g, b);
}

uint32_t colorForClass(const std::string& cls) {
    if (startsWith(cls, "mp_") && cls.find("spawn") != std::string::npos) return packRGBA(0.25, 0.85, 0.35);
    if (cls == "info_player_start") return packRGBA(0.35, 0.95, 0.75);
    if (cls == "mp_global_intermission") return packRGBA(0.95, 0.55, 0.95);
    if (startsWith(cls, "trigger")) return packRGBA(0.95, 0.55, 0.15);
    if (startsWith(cls, "script_model")) return packRGBA(0.95, 0.80, 0.25);
    if (startsWith(cls, "script_")) return packRGBA(0.35, 0.55, 0.95);
    if (startsWith(cls, "node_")) return packRGBA(0.55, 0.55, 0.60);
    if (cls == "reflection_probe") return packRGBA(0.30, 0.90, 0.95);
    return packRGBA(0.85, 0.35, 0.85);
}

void SceneCache::clear() {
    cache_.clear();
    list_.clear();
    prevList_.clear();
    docRevision_ = 0;
    fingerprint_ = 0;
    doc_ = nullptr;
    spawns.clear();
}

bool modelTriangleLight(const SceneCache& scene, const PrimGeom& g, size_t i, const Vec3& n, Vec3& light) {
    if (scene.modelLights.empty() || !scene.gameLookActive() || !g.meshPoly(i)) return false;
    const auto it = scene.modelLights.find(g.ref.entity);
    if (it == scene.modelLights.end()) return false;
    light = modelLightFromSamples(it->second, n);
    return true;
}

void appendTexturedFace(const PrimGeom& g, size_t i, const Vec3& n, const Vec3& tan, uint32_t colour, const GameLightmap* gameLm,
                        const Vec3& centre, std::vector<GpuTexVertex>& out, bool propLight) {
    const Winding& w = g.polys[i];
    const auto& uv = g.polyUV[i];
    const EdgeFaceMesh* em = i < g.polyEdges.size() ? &g.polyEdges[i] : nullptr;
    if (em && em->idx.empty()) return;  // wholly under coplanar faces of other brushes: not drawn
    if (!em) {
        const auto tv = [&](size_t k) {
            GpuTexVertex v{float(w[k].x), float(w[k].y), float(w[k].z), uv[k][0], uv[k][1], colour};
            v.nx = float(n.x), v.ny = float(n.y), v.nz = float(n.z);
            v.tx = float(tan.x), v.ty = float(tan.y), v.tz = float(tan.z);
            if (gameLm) {
                float luv[2];
                if (gameLm->uvFor(w[k], n, centre, luv)) v.lu = luv[0], v.lv = luv[1];
            }
            if (propLight) v.lu = v.lv = -2.0f;
            return v;
        };
        for (size_t k = 1; k + 1 < w.size(); ++k) {
            out.push_back(tv(0));
            out.push_back(tv(k));
            out.push_back(tv(k + 1));
        }
        return;
    }
    // The face as its mesh: the texture coordinates are the face's own, an affine function of the position, taken
    // from the corner pair that spans the face best.
    size_t far = 2;
    double best = -1;
    for (size_t k = 2; k < w.size(); ++k)
        if (const double a = length(cross(w[1] - w[0], w[k] - w[0])); a > best) best = a, far = k;
    const Vec3 e1 = w[1] - w[0], e2 = w[far] - w[0];
    const double d11 = dot(e1, e1), d12 = dot(e1, e2), d22 = dot(e2, e2);
    const double det = d11 * d22 - d12 * d12;
    // The preview bake's lightmap UVs are kept per face corner: the same affine blend gives the others.
    float l0[2], l1[2], l2[2];
    const bool haveLm = gameLm && gameLm->uvFor(w[0], n, centre, l0) && gameLm->uvFor(w[1], n, centre, l1) && gameLm->uvFor(w[far], n, centre, l2);
    for (const uint32_t vi : em->idx) {
        const EdgeVertex& q = em->verts[vi];
        const Vec3 d = q.pos - w[0];
        const double b1 = dot(d, e1), b2 = dot(d, e2);
        const double s = det > 1e-18 ? (b1 * d22 - b2 * d12) / det : 0.0, t = det > 1e-18 ? (b2 * d11 - b1 * d12) / det : 0.0;
        GpuTexVertex v{float(q.pos.x), float(q.pos.y), float(q.pos.z), float(uv[0][0] + s * (uv[1][0] - uv[0][0]) + t * (uv[far][0] - uv[0][0])),
                       float(uv[0][1] + s * (uv[1][1] - uv[0][1]) + t * (uv[far][1] - uv[0][1])), colour};
        v.nx = float(n.x), v.ny = float(n.y), v.nz = float(n.z);
        v.tx = float(tan.x), v.ty = float(tan.y), v.tz = float(tan.z);
        if (haveLm) {
            v.lu = float(l0[0] + s * (l1[0] - l0[0]) + t * (l2[0] - l0[0]));
            v.lv = float(l0[1] + s * (l1[1] - l0[1]) + t * (l2[1] - l0[1]));
        }
        v.edge = packEdge(q);
        out.push_back(v);
    }
}

void SceneCache::updateFaceEdges() {
    // Brushes whose faces are drawn with a texture: the ones a surface shader can light.
    std::vector<PrimGeom*> items;
    for (const PrimGeom* cg : list_) {
        PrimGeom* g = const_cast<PrimGeom*>(cg);
        const bool brush = g->kind == PrimGeom::Brush && !g->volume && !g->prefab && g->polyMaterial.size() == g->polys.size() &&
                           g->faceNormals.size() == g->polys.size();
        if (brush) {
            items.push_back(g);
        } else if (!g->polyEdges.empty()) {
            g->polyEdges.clear();
            g->edgeKey = 0;
            g->stamp = ++stampCounter_;
        }
    }
    if (items.empty()) return;
    IW8R_PROF("s.face_edges");
    std::vector<EdgeBrush> brushes(items.size());
    std::unordered_map<std::string, bool> opaque;  // opaqueFor per material, asked once per sync
    for (size_t i = 0; i < items.size(); ++i) {
        const PrimGeom& g = *items[i];
        EdgeBrush& b = brushes[i];
        b.set = g.edgeSet;
        b.wins = g.polys;
        b.planes.resize(g.polys.size());
        b.drawn.resize(g.polys.size());
        for (size_t f = 0; f < g.polys.size(); ++f) {
            b.planes[f].n = g.faceNormals[f];
            b.planes[f].d = g.polys[f].empty() ? 0.0 : dot(g.faceNormals[f], g.polys[f][0]);
            b.drawn[f] = !isIw8ToolMaterial(g.polyMaterial[f]);
        }
        finishEdgeBrush(b);
        b.opaque = opaqueFor && edgeBrushOpaque(b, g.polyMaterial, [](const std::string& m) { return isIw8ToolMaterial(m); },
                                                [&](const std::string& m) {
                                                    auto it = opaque.find(m);
                                                    if (it == opaque.end()) it = opaque.emplace(m, opaqueFor(m)).first;
                                                    return it->second;
                                                });
    }
    const std::vector<std::vector<uint32_t>> near = edgeNeighbours(brushes);
    for (size_t i = 0; i < items.size(); ++i) {
        PrimGeom& g = *items[i];
        // The brush and the brushes touching it, as they are now: its edges change only when one of them does.
        uint64_t key = (1469598103934665603ull ^ g.rev) * 1099511628211ull;
        key = (key ^ uint64_t(g.edgeSet)) * 1099511628211ull;
        key = (key ^ uint64_t(faceEdges ? 2 : 1)) * 1099511628211ull;
        key = (key ^ uint64_t(brushes[i].opaque ? 5 : 3)) * 1099511628211ull;
        // ... and the neighbours' neighbours: they shape the neighbours' pieces, whose corners this brush's seams share.
        for (uint32_t n : near[i]) {
            key = (key ^ uint64_t(items[n]->ref.item)) * 1099511628211ull;
            key = (key ^ items[n]->rev) * 1099511628211ull;
            key = (key ^ uint64_t(brushes[n].opaque ? 5 : 3)) * 1099511628211ull;
            for (uint32_t m : near[n]) {
                key = (key ^ uint64_t(items[m]->ref.item)) * 1099511628211ull;
                key = (key ^ items[m]->rev) * 1099511628211ull;
                key = (key ^ uint64_t(brushes[m].opaque ? 5 : 3)) * 1099511628211ull;
            }
        }
        if (key == 0) key = 1;
        if (g.edgeKey == key && g.polyEdges.size() == g.polys.size()) continue;
        g.polyEdges = edgeBrushMeshes(brushes, i, near[i], kEdgeRange, faceEdges, &near);
        g.polyEdges.resize(g.polys.size());
        g.edgeKey = key;
        g.stamp = ++stampCounter_;
    }
}

void SceneCache::sync(const Document& doc) {
    if (doc_ == &doc && docRevision_ == doc.revision()) return;
    // The revision also moves when only the selection changed. Every mutation of an item gives it a new rev, so the
    // ids and revs of all items tell whether anything drawn changed: when they are what the last sync saw, the cache
    // is already right (docs/research/85). invalidate() (hidden sets, prefab and model sources) skips this.
    uint64_t print = 1469598103934665603ull;
    for (const auto& e : doc.map().entities) {
        print = (print ^ e.id) * 1099511628211ull;
        for (const auto& it : e.items) print = ((print ^ it.id) * 1099511628211ull ^ it.rev) * 1099511628211ull;
    }
    static const bool noFastSync = perfOff("syncfast");  // benchmark switch
    if (!noFastSync && doc_ == &doc && docRevision_ != 0 && print == fingerprint_ && behaviourTime < 0) {
        docRevision_ = doc.revision();
        return;
    }
    IW8R_PROF("s.sync_full");
    fingerprint_ = print;
    doc_ = &doc;
    docRevision_ = doc.revision();
    ++generation_;
    ++visit_;
    // The items mostly come in the order of the last sync: the last list is tried first, before the hash lookup.
    prevList_.swap(list_);
    list_.clear();
    const auto hinted = [&](Id entity, Id item) -> PrimGeom* {
        if (list_.size() >= prevList_.size()) return nullptr;
        PrimGeom* c = const_cast<PrimGeom*>(prevList_[list_.size()]);
        return c->ref.entity == entity && c->ref.item == item ? c : nullptr;
    };
    connections_.clear();
    std::unordered_map<std::string, Vec3> named;  // targetname -> centre
    std::vector<std::pair<Vec3, std::string>> targets;
    auto classHidden = [&](const std::string& cls) {
        for (const auto& prefix : hiddenClassPrefixes)
            if (cls.rfind(prefix, 0) == 0) return true;
        return false;
    };
    for (const auto& e : doc.map().entities) {
        const std::string cls = e.classname();
        const bool filtered = !e.isWorldspawn() && classHidden(cls);
        bool hasPrim = false;
        AABB entBounds;
        BrushPose brushPose;
        if (behaviourTime >= 0 && cls == "script_brushmodel") brushPose = brushBehaviourPose(e, behaviourTime);
        for (const auto& it : e.items) {
            const Primitive* p = it.prim();
            if (!p || p->kind == PrimKind::Unknown) continue;
            hasPrim = true;
            if (filtered || hidden.count(it.id) || hidden.count(e.id) || hiddenByLayer.count(it.id) || hiddenByRegion.count(it.id) ||
                hiddenByMode.count(e.id))
                continue;
            PrimGeom* pg = hinted(e.id, it.id);
            if (!pg)
                if (auto found = cache_.find(it.id); found != cache_.end()) pg = &found->second;
            if (brushPose.any) {
                // Behaviour Preview: rebuilt at its pose every tick; the odd revision brings the resting brush back after.
                pg = &cache_.insert_or_assign(it.id, buildPrim(e, it, *p)).first->second;
                applyBrushPose(*pg, brushPose);
                pg->rev = ~uint64_t(0);
                pg->stamp = ++stampCounter_;
            } else if (!pg || pg->rev != it.rev || pg->ref.entity != e.id) {
                pg = &cache_.insert_or_assign(it.id, buildPrim(e, it, *p)).first->second;
                pg->stamp = ++stampCounter_;
            }
            pg->seen = visit_;
            pg->edgeSet = e.isWorldspawn() || cls == "func_group" ? 0 : int(e.id % 0x7fffffff) + 1;
            entBounds.add(pg->bounds);
            list_.push_back(pg);
        }
        if (!hasPrim && !e.isWorldspawn() && e.origin()) {
            if (filtered || hidden.count(e.id) || hiddenByRegion.count(e.id) || hiddenByMode.count(e.id)) continue;
            uint64_t rev = 0;
            for (const auto& it : e.items) rev = std::max(rev, it.rev + 1);
            PrimGeom* pg = hinted(e.id, kNoId);
            if (!pg)
                if (auto found = cache_.find(e.id); found != cache_.end()) pg = &found->second;
            std::shared_ptr<const MapFile> prefab;
            std::optional<PrefabInstance> inst;
            if (prefabSource && cls == "misc_prefab" && (inst = prefabInstance(e))) prefab = prefabSource(e);
            // A scriptable without a model key draws its definition's model, as a misc_model with that key would.
            std::optional<Entity> asModel;
            if (!prefab && cls == "scriptable" && scriptableModelFor)
                if (const std::string m = scriptableModelName(e, scriptableModelFor); !m.empty()) asModel = withModelKey(e, m);
            const Entity& me = asModel ? *asModel : e;
            std::shared_ptr<const ImportedModel> model;
            if (!prefab && modelSource && (isModelClass(cls) || asModel)) model = modelSource(me);
            const std::string label = prefab ? "misc_prefab:" + inst->model : model ? "model:" + model->name : cls;
            if (behaviourTime >= 0 && model && (e.value("iw8r_spin") || e.value("iw8r_bob"))) {
                // Rebuilt every tick while the preview runs; the label differs, so the resting pose comes back after it.
                pg = &cache_.insert_or_assign(e.id, buildModelGeom(behaviourPose(me, behaviourTime), *model, modelScale)).first->second;
                pg->label += "@behaviour";
                pg->stamp = ++stampCounter_;
            } else if (!pg || pg->rev != rev || pg->label != label || (prefab && prefabSourcesChanged(*pg, *this, prefab.get()))) {
                pg = &cache_.insert_or_assign(e.id, prefab ? buildPrefab(e, *inst, prefab, *this)
                                                    : model ? buildModelGeom(me, *model, modelScale)
                                                            : buildPointEntity(e)).first->second;
                pg->stamp = ++stampCounter_;
            }
            pg->seen = visit_;
            entBounds.add(pg->bounds);
            list_.push_back(pg);
        }
        if (entBounds.valid()) {
            if (const std::string* tn = e.value("targetname")) named[*tn] = entBounds.center();
            if (const std::string* t = e.value("target")) targets.push_back({entBounds.center(), *t});
        }
    }
    for (const auto& [from, name] : targets)
        if (auto it = named.find(name); it != named.end()) connections_.push_back({from, it->second});
    if (faceEdgesWanted) faceEdges = faceEdgesWanted();
    updateFaceEdges();
    IW8R_PROF("s.lights_erase");
    // Lights for the lit preview (same keys the compiler reads, docs/research/24).
    lights.clear();
    auto parse3 = [](const std::string* s, Vec3& out) {
        return s && std::sscanf(s->c_str(), "%lf %lf %lf", &out.x, &out.y, &out.z) == 3;
    };
    auto colour = [&](const Entity& e) {
        Vec3 c{1, 1, 1};
        if (!parse3(e.value("_color"), c)) parse3(e.value("color"), c);
        if (c.x > 1 || c.y > 1 || c.z > 1) c = c * (1.0 / 255.0);
        if (const std::string* in = e.value("intensity")) c = c * std::max(0.0, std::atof(in->c_str()));
        return c;
    };
    for (const auto& e : doc.map().entities) {
        const std::string cls = e.classname();
        if (e.isWorldspawn()) {
            Vec3 a;
            if (parse3(e.value("sundirection"), a)) {
                const double p = a.x * 3.14159265358979 / 180.0, y = a.y * 3.14159265358979 / 180.0;
                sunToward = normalize(Vec3{std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), -std::sin(p)});
            }
            Vec3 sc;
            if (parse3(e.value("suncolor"), sc)) sunColor = (sc.x > 1 || sc.y > 1 || sc.z > 1) ? sc * (1.0 / 255.0) : sc;
            continue;
        }
        if (cls != "light" && cls != "light_spot" && cls != "light_omni") continue;
        const auto o = e.origin();
        if (!o) continue;
        PreviewLight L;
        L.pos = *o;
        L.color = colour(e);
        if (const std::string* r = e.value("radius")) L.radius = std::max(1.0, std::atof(r->c_str()));
        bool spot = cls == "light_spot" || e.value("fov_outer");
        Vec3 dir{0, 0, -1};
        if (const std::string* t = e.value("target"))
            for (const auto& t2 : doc.map().entities)
                if (const std::string* tn = t2.value("targetname"); tn && *tn == *t)
                    if (auto to = t2.origin()) {
                        dir = normalize(*to - *o);
                        spot = true;
                    }
        if (spot) {
            Vec3 ang;
            if (!e.value("target") && parse3(e.value("angles"), ang)) {
                const double p = ang.x * 3.14159265358979 / 180.0, y = ang.y * 3.14159265358979 / 180.0;
                dir = normalize(Vec3{std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), -std::sin(p)});
            }
            const double outer = e.value("fov_outer") ? std::atof(e.value("fov_outer")->c_str()) : 90.0;
            const double inner = e.value("fov_inner") ? std::atof(e.value("fov_inner")->c_str()) : 60.0;
            L.dir = dir;
            L.cosOuter = std::cos(outer * 0.5 * 3.14159265358979 / 180.0);
            L.cosInner = std::cos(std::min(inner, outer) * 0.5 * 3.14159265358979 / 180.0);
        }
        lights.push_back(L);
    }
    prevList_.clear();
    // Every listed item was seen once: when the cache holds no more than those, nothing is left to drop.
    if (cache_.size() != list_.size())
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (it->second.seen != visit_) it = cache_.erase(it);
            else ++it;
        }
}
std::optional<FaceRef> SceneCache::pickFace(const Ray& ray, double* tOut) const {
    if (!doc_) return std::nullopt;
    std::optional<FaceRef> best;
    double bestT = 1e300;
    for (const PrimGeom* g : list_) {
        if (g->kind != PrimGeom::Brush || !rayAABB(ray, g->bounds)) continue;
        const EntityItem* it = doc_->findItem(g->ref.entity, g->ref.item);
        const Primitive* p = it ? it->prim() : nullptr;
        if (!p) continue;
        // Slab test over the face planes; the entering plane with the largest t is the face hit.
        double tEnter = -1e300, tExit = 1e300;
        int enterFace = -1;
        bool miss = false;
        for (size_t f = 0; f < p->faces.size() && !miss; ++f) {
            const auto pl = facePlane(*p, f);
            if (!pl) continue;
            const double denom = dot(pl->n, ray.dir);
            const double dist = dot(pl->n, ray.origin) - pl->d;
            if (std::fabs(denom) < 1e-12) {
                if (dist > 0) miss = true;
                continue;
            }
            const double t = -dist / denom;
            if (denom < 0) {
                if (t > tEnter) {
                    tEnter = t;
                    enterFace = int(f);
                }
            } else if (t < tExit) {
                tExit = t;
            }
        }
        if (miss || enterFace < 0 || tEnter > tExit || tEnter < 0) continue;
        if (tEnter < bestT) {
            bestT = tEnter;
            best = FaceRef{g->ref.entity, g->ref.item, uint32_t(enterFace)};
        }
    }
    if (tOut && best) *tOut = bestT;
    return best;
}

std::vector<SelRef> SceneCache::pickAll(const Ray& ray) const {
    std::vector<std::pair<double, SelRef>> hits;
    for (const PrimGeom* g : list_) {
        std::optional<double> t;
        if (g->kind == PrimGeom::Brush) {
            if (!rayAABB(ray, g->bounds)) continue;
            t = rayConvex(ray, g->planes);
        } else if (g->kind == PrimGeom::PointEntity) {
            t = rayAABB(ray, g->bounds);
        } else {
            for (const auto& w : g->polys)
                for (size_t i = 1; i + 1 < w.size(); ++i)
                    if (auto h = rayTriangle(ray, w[0], w[i], w[i + 1]); h && (!t || *h < *t)) t = h;
        }
        if (t) hits.push_back({*t, g->ref});
    }
    std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<SelRef> out;
    for (const auto& h : hits) out.push_back(h.second);
    return out;
}

std::optional<SelRef> SceneCache::pick(const Ray& ray, double* tOut) const {
    std::optional<SelRef> best;
    double bestT = 1e300;
    for (const PrimGeom* g : list_) {
        std::optional<double> t;
        if (g->kind == PrimGeom::Brush) {
            if (!rayAABB(ray, g->bounds)) continue;
            t = rayConvex(ray, g->planes);
        } else if (g->kind == PrimGeom::PointEntity) {
            t = rayAABB(ray, g->bounds);
        } else {
            for (const auto& w : g->polys) {
                for (size_t i = 1; i + 1 < w.size(); ++i)
                    if (auto h = rayTriangle(ray, w[0], w[i], w[i + 1]); h && (!t || *h < *t)) t = h;
            }
        }
        if (t && *t < bestT) {
            bestT = *t;
            best = g->ref;
        }
    }
    if (tOut && best) *tOut = bestT;
    return best;
}

namespace {

uint32_t scaleColor(uint32_t c, double s) {
    auto ch = [&](int shift) { return uint32_t(std::clamp(((c >> shift) & 0xff) * s, 0.0, 255.0)); };
    return ch(0) | (ch(8) << 8) | (ch(16) << 16) | (0xffu << 24);
}

uint32_t mixColor(uint32_t a, uint32_t b, double t) {
    auto ch = [&](int shift) { return uint32_t(std::clamp(((a >> shift) & 0xff) * (1 - t) + ((b >> shift) & 0xff) * t, 0.0, 255.0)); };
    return ch(0) | (ch(8) << 8) | (ch(16) << 16) | (ch(24) << 24);
}

GpuVertex gv(const Vec3& p, uint32_t c) { return {float(p.x), float(p.y), float(p.z), c}; }

// Normals mode: face normal mapped to RGB (0.5 + 0.5 * n); +Z floor = (128, 128, 255).
uint32_t normalColour(const Vec3& n) {
    auto ch = [](double v) { return uint32_t(std::clamp(std::round((0.5 + 0.5 * v) * 255.0), 0.0, 255.0)); };
    return ch(n.x) | (ch(n.y) << 8) | (ch(n.z) << 16) | (0xffu << 24);
}

constexpr uint32_t kSelRed = 0xff3030ffu;
constexpr uint32_t kSelEdge = 0xff40e0ffu;
constexpr uint32_t kEdgeDark = 0x99101010u;
constexpr uint32_t kEdgeWire = 0xffb4b4b4u;  // Wireframe mode: edges are the only geometry, so they are lighter

}  // namespace

namespace {
void buildCameraGeometryImpl(const SceneCache& scene, const Document& doc, bool showSpawns, std::vector<GpuVertex>& tris,
                             std::vector<GpuVertex>& lines, std::vector<GpuVertex>& overlay,
                             const std::function<int(const std::string&)>* textureFor, std::vector<TexturedBatch>* textured);
}  // namespace

void buildCameraGeometry(const SceneCache& scene, const Document& doc, bool showSpawns, std::vector<GpuVertex>& tris,
                         std::vector<GpuVertex>& lines, std::vector<GpuVertex>& overlay) {
    buildCameraGeometryImpl(scene, doc, showSpawns, tris, lines, overlay, nullptr, nullptr);
}

void buildCameraGeometry(const SceneCache& scene, const Document& doc, bool showSpawns, std::vector<GpuVertex>& tris,
                         std::vector<GpuVertex>& lines, std::vector<GpuVertex>& overlay,
                         const std::function<int(const std::string&)>& textureFor, std::vector<TexturedBatch>& textured) {
    buildCameraGeometryImpl(scene, doc, showSpawns, tris, lines, overlay, &textureFor, &textured);
}

namespace {
void buildCameraGeometryImpl(const SceneCache& scene, const Document& doc, bool showSpawns, std::vector<GpuVertex>& tris,
                             std::vector<GpuVertex>& lines, std::vector<GpuVertex>& overlay,
                             const std::function<int(const std::string&)>* textureFor, std::vector<TexturedBatch>* textured) {
    tris.clear();
    // Flat triangles of things the game does not draw (entity markers, tool-material faces, trigger / clip volumes):
    // appended after the shadow casters so the lit preview's sun shadow pass can leave them out.
    std::vector<GpuVertex> noCast;
    lines.clear();
    overlay.clear();
    if (textured) textured->clear();
    std::unordered_map<int, size_t> batchOf;  // texture id -> index in *textured
    std::unordered_map<std::string, int> texCache;
    std::unordered_map<std::string, int> normalCache;
    std::unordered_map<std::string, int> blendCache;
    std::unordered_map<std::string, bool> foliageCache;
    std::unordered_map<std::string, int> drawCache;
    std::vector<GpuVertex> shadowOnly;  // nodraw stock materials: sun shadow pass only (placed first in tris)
    if (scene.litPreview) {
        // Sun direction (Sun & Sky): a yellow arrow from the middle of the map toward the sun.
        AABB all;
        for (const PrimGeom* g : scene.items())
            if (g->bounds.valid()) all.add(g->bounds);
        if (all.valid()) {
            const Vec3 c = all.center();
            const double len = std::max(128.0, length(all.maxs - all.mins) * 0.4);
            const Vec3 tip = c + scene.sunToward * len;
            const uint32_t yellow = 0xff20e0ffu;
            auto seg = [&](const Vec3& a, const Vec3& b) {
                overlay.push_back({float(a.x), float(a.y), float(a.z), yellow});
                overlay.push_back({float(b.x), float(b.y), float(b.z), yellow});
            };
            seg(c, tip);
            const double r = len * 0.06;
            seg(tip - Vec3{r, 0, 0}, tip + Vec3{r, 0, 0});
            seg(tip - Vec3{0, r, 0}, tip + Vec3{0, r, 0});
            seg(tip - Vec3{0, 0, r}, tip + Vec3{0, 0, r});
        }
    }
    const CollisionWorld* shadowWorld = scene.litPreview && scene.litShadows ? scene.shadowWorld(doc) : nullptr;
    const Vec3 light = normalize(Vec3{0.35, 0.55, 0.75});
    const CameraMode mode = scene.cameraMode;
    const bool wire = mode == CameraMode::Wireframe;
    const bool normals = mode == CameraMode::Normals;
    const bool contentsMode = mode == CameraMode::Contents;
    const bool densityMode = mode == CameraMode::Density;
    const bool collisionMode = mode == CameraMode::Collision;
    std::unordered_map<std::string, double> texSide;  // material -> sqrt(width * height) in pixels
    const bool texturesOn = mode == CameraMode::Textured || mode == CameraMode::Fullbright;
    PatchCovers covers;  // as the kept geometry (camera_geometry.cpp): a tool face under a flat patch is not drawn
    covers.find(scene.items());
    for (const PrimGeom* g : scene.items()) {
        // Probe capture keeps only what the game draws: brushes, patches, models and prefab contents.
        if (scene.captureMode && ((g->kind == PrimGeom::PointEntity && !g->prefab) || g->volume)) continue;
        const bool sel = doc.isSelected(g->ref);
        if (collisionMode && !g->collides && !sel) continue;  // only what the player collides with
        if (g->volume && !sel && !wire && !(collisionMode && g->collides)) {
            // Wire box with face diagonals in the volume's colour; the fill would hide what is inside.
            const uint32_t vc = g->color | 0xff000000u;
            for (const Winding& w : g->polys) {
                for (size_t k = 0; k < w.size(); ++k) {
                    lines.push_back(gv(w[k], vc));
                    lines.push_back(gv(w[(k + 1) % w.size()], vc));
                }
                if (w.size() >= 4) {
                    lines.push_back(gv(w[0], vc));
                    lines.push_back(gv(w[w.size() / 2], vc));
                }
            }
            continue;
        }
        for (size_t i = 0; i < g->polys.size(); ++i) {
            if (scene.skyActive && texturesOn && i < g->polyMaterial.size() &&
                g->polyMaterial[i].find("sky") != std::string::npos && !doc.isSelected(g->ref))
                continue;  // the sky box shows through, as in game
            const bool markerPoly = g->markerPoly(i);  // a prefab's inner entity box: editor only
            if (markerPoly && scene.captureMode) continue;
            const Winding& w = g->polys[i];
            if (scene.drawClassFor && i < g->polyMaterial.size() && !g->polyMaterial[i].empty()) {
                const std::string& m = g->polyMaterial[i];
                auto dc = drawCache.find(m);
                if (dc == drawCache.end()) dc = drawCache.emplace(m, scene.drawClassFor(m)).first;
                if (dc->second == 2) continue;  // never drawn by the game (info carriers, lighting proxy cards, distortion)
                if (dc->second == 1) {          // shadow-only: casts the sun shadow, invisible on screen
                    for (size_t k = 1; k + 1 < w.size(); ++k) {
                        shadowOnly.push_back(gv(w[0], 0xffffffffu));
                        shadowOnly.push_back(gv(w[k], 0xffffffffu));
                        shadowOnly.push_back(gv(w[k + 1], 0xffffffffu));
                    }
                    continue;
                }
            }
            const Vec3 n = i < g->faceNormals.size() ? g->faceNormals[i] : Vec3{0, 0, 1};
            double shade = mode == CameraMode::Fullbright ? 1.0 : 0.55 + 0.45 * std::fabs(dot(n, light));
            Vec3 lit{1, 1, 1};
            Vec3 sunLit;  // the sun's part of lit (the shadow map removes it where the sun is blocked)
            // View > Game Lighting: the renderer lights textured faces; colours carry only tints (camera_geometry.cpp).
            const bool litMode = scene.litPreview && !scene.gameLookActive();
            const bool encodedMode = scene.litEncoded() && !scene.gameLookActive();
            const GameLightmap* gameLm = scene.gameLookActive() ? scene.gameLightmap.get() : nullptr;
            if (litMode) {
                Vec3 c;
                for (const auto& v : w) c += v;
                c = c * (1.0 / double(std::max<size_t>(1, w.size())));
                double sunK = std::max(0.0, dot(n, scene.sunToward));
                if (sunK > 0 && scene.litShadows && shadowWorld && !scene.gpuShadows) {
                    // Face centre lifted off the surface, then a ray toward the sun: blocked = in shadow.
                    const Vec3 from = c + n * 1.0;
                    if (shadowWorld->traceRay(from, from + scene.sunToward * 65536.0).fraction < 1.0) sunK = 0;
                }
                lit = Vec3{scene.ambient, scene.ambient, scene.ambient} + scene.sunColor * sunK * scene.sunGain;
                sunLit = scene.sunColor * sunK * scene.sunGain;
                // Textured faces get the first lights per pixel from the renderer; the rest (and flat faces) per face.
                const bool perPixel = scene.pixelLights && texturesOn && textureFor && textured && i < g->polyMaterial.size() &&
                                      i < g->polyUV.size() && g->polyUV[i].size() == w.size();
                size_t li = 0;
                for (const auto& L : scene.lights) {
                    if (perPixel && li++ < 16) continue;
                    const Vec3 d = L.pos - c;
                    const double dist = length(d);
                    if (dist >= L.radius || dist < 1e-6) continue;
                    const Vec3 l = d * (1.0 / dist);
                    const double fall = 1.0 - dist / L.radius;
                    double k = std::max(0.0, dot(n, l)) * fall * fall * scene.lightGain;
                    if (L.cosOuter > -1.5) {
                        const double cs = dot(l * -1.0, L.dir);
                        if (cs <= L.cosOuter) continue;
                        if (cs < L.cosInner) k *= (cs - L.cosOuter) / std::max(1e-6, L.cosInner - L.cosOuter);
                    }
                    lit = lit + L.color * k;
                }
                shade = 1.0;
            }
            auto litColor = [&](uint32_t base) {
                if (!litMode) return scaleColor(base, shade);
                if (!encodedMode) {
                    auto c1 = [&](int shift, double s) { return uint32_t(std::clamp(((base >> shift) & 0xff) * s, 0.0, 255.0)); };
                    return c1(0, lit.x) | (c1(8, lit.y) << 8) | (c1(16, lit.z) << 16) | (0xffu << 24);
                }
                // Light / 2 (headroom to 2x; the shader decodes) and the sun's share of it in alpha, 0.5..1.
                auto ch = [&](int shift, double s) { return uint32_t(std::clamp(((base >> shift) & 0xff) * s * 0.5, 0.0, 255.0)); };
                const double total = lit.x + lit.y + lit.z, sun = sunLit.x + sunLit.y + sunLit.z;
                const double share = total > 1e-6 ? std::clamp(sun / total, 0.0, 1.0) : 0.0;
                const uint32_t a = uint32_t(std::lround(128 + 127 * share));
                return ch(0, lit.x) | (ch(8, lit.y) << 8) | (ch(16, lit.z) << 16) | (a << 24);
            };
            int texId = -1;
            if (texturesOn && textureFor && textured && i < g->polyMaterial.size() && i < g->polyUV.size() && g->polyUV[i].size() == w.size()) {
                const std::string& m = g->polyMaterial[i];
                auto tc = texCache.find(m);
                if (tc == texCache.end()) tc = texCache.emplace(m, (*textureFor)(m)).first;
                texId = tc->second;
            }
            if (texId >= 0) {
                if (scene.gameLookActive()) shade = 1.0;
                // Brush entities keep a hint of their class colour; selection tints red.
                uint32_t c = litColor(i < g->polyTint.size() ? g->polyTint[i] : 0xffffffffu);
                if (g->brushEntity) c = mixColor(c, g->color, 0.35);
                if (sel) c = mixColor(c, scene.selectionTint, scene.selectionTintAmount);
                // Normal map (3.2): batched by colour and normal texture together.
                int nrmId = -1;
                if (scene.normalFor && !scene.captureMode) {
                    const std::string& m = g->polyMaterial[i];
                    auto nc = normalCache.find(m);
                    if (nc == normalCache.end()) nc = normalCache.emplace(m, scene.normalFor(m)).first;
                    nrmId = nc->second;
                }
                int blendMode = 0;
                if (scene.blendFor && !scene.captureMode) {
                    const std::string& m = g->polyMaterial[i];
                    auto bc = blendCache.find(m);
                    if (bc == blendCache.end()) bc = blendCache.emplace(m, scene.blendFor(m)).first;
                    blendMode = std::clamp(bc->second, 0, 2);
                }
                bool sway = false;
                if (scene.foliageFor && !scene.captureMode) {
                    const std::string& m = g->polyMaterial[i];
                    auto fc = foliageCache.find(m);
                    if (fc == foliageCache.end()) fc = foliageCache.emplace(m, scene.foliageFor(m)).first;
                    sway = fc->second;
                }
                const int batchKey = ((texId * 65536 + (nrmId + 1)) * 3 + blendMode) * 2 + (sway ? 1 : 0);
                auto b = batchOf.find(batchKey);
                if (b == batchOf.end()) {
                    b = batchOf.emplace(batchKey, textured->size()).first;
                    textured->push_back({texId, {}, nrmId, -1, blendMode, sway, 0.0f});
                    // Same texture id = same material, so the batch's emissive map is the material's.
                    if (scene.emissiveFor && !scene.captureMode) (*textured)[b->second].emissiveTexture = scene.emissiveFor(g->polyMaterial[i]);
                    if (scene.specFor && !scene.captureMode) (*textured)[b->second].specTexture = scene.specFor(g->polyMaterial[i]);
                }
                auto& out = (*textured)[b->second].tris;
                const auto& uv = g->polyUV[i];
                // Tangent: the world direction of increasing u, from the first triangle's positions and texture coords.
                Vec3 tan;
                if (w.size() >= 3) {
                    const Vec3 e1 = w[1] - w[0], e2 = w[2] - w[0];
                    const double du1 = uv[1][0] - uv[0][0], dv1 = uv[1][1] - uv[0][1], du2 = uv[2][0] - uv[0][0], dv2 = uv[2][1] - uv[0][1];
                    const double det = du1 * dv2 - du2 * dv1;
                    if (std::fabs(det) > 1e-12) tan = normalize((e1 * dv2 - e2 * dv1) * (1.0 / det));
                }
                Vec3 centre;
                if (gameLm) {
                    for (const auto& p : w) centre += p;
                    centre = centre * (1.0 / double(std::max<size_t>(1, w.size())));
                }
                Vec3 modelLight;
                const bool lit = modelTriangleLight(scene, *g, i, n, modelLight);
                appendTexturedFace(*g, i, n, lit ? modelLight : tan, c, gameLm, centre, out, lit);
            } else if (!wire) {
                uint32_t c = normals ? normalColour(n) : litColor(contentsMode || collisionMode ? g->contentsColor : g->color);
                if (!normals && !contentsMode && i < g->polyTint.size()) c = mixColor(c, g->polyTint[i] | 0xff000000u, 0.6);
                if (densityMode) {
                    c = 0xff707070u;
                    if (i < g->polyRepeatsPerUnit.size() && i < g->polyMaterial.size() && g->polyRepeatsPerUnit[i] > 0) {
                        const std::string& m = g->polyMaterial[i];
                        auto it = texSide.find(m);
                        if (it == texSide.end()) {
                            std::pair<int, int> sz = scene.textureSizeFor ? scene.textureSizeFor(m) : std::pair<int, int>{0, 0};
                            if (sz.first <= 0 || sz.second <= 0) sz = {512, 512};
                            it = texSide.emplace(m, std::sqrt(double(sz.first) * double(sz.second))).first;
                        }
                        const auto rgb = densityColor(g->polyRepeatsPerUnit[i] * it->second, scene.densityTarget);
                        c = 0xff000000u | uint32_t(rgb[0] * 255) | (uint32_t(rgb[1] * 255) << 8) | (uint32_t(rgb[2] * 255) << 16);
                        c = scaleColor(c, shade);
                    }
                }
                if (sel) c = mixColor(c, scene.selectionTint, std::min(1.0, scene.selectionTintAmount + 0.05));
                const bool marker = (g->kind == PrimGeom::PointEntity && !g->prefab && !g->mesh) || markerPoly;
                const bool tool = i < g->polyMaterial.size() && isIw8ToolMaterial(g->polyMaterial[i]);
                auto& dst = marker || tool || g->volume ? noCast : tris;
                // Not in the game: no sun shadow falls on it either (sun share 0 in the lit encoding).
                if (&dst == &noCast && encodedMode) c = (c & 0x00ffffffu) | 0x80000000u;
                const bool covered = tool && !sel && !wire && covers.any() && g->kind == PrimGeom::Brush && i < g->faceNormals.size() &&
                                     covers.covered(w, g->faceNormals[i]);
                for (size_t k = 1; !covered && k + 1 < w.size(); ++k) {
                    dst.push_back(gv(w[0], c));
                    dst.push_back(gv(w[k], c));
                    dst.push_back(gv(w[k + 1], c));
                }
            }
            if (!g->edges.empty() || (g->meshPoly(i) && !sel && !wire)) continue;
            // Game Lighting: no editor outlines on brush faces (camera_geometry.cpp has the same rule).
            if (!sel && !wire && g->kind == PrimGeom::Brush && !g->volume && scene.gameLookActive() && !scene.gameOutlines) continue;
            auto& dst = sel ? overlay : lines;
            const uint32_t ec = sel ? kSelEdge : (wire ? kEdgeWire : kEdgeDark);
            for (size_t k = 0; k < w.size(); ++k) {
                dst.push_back(gv(w[k], ec));
                dst.push_back(gv(w[(k + 1) % w.size()], ec));
            }
        }
        if (!g->edges.empty()) {
            auto& dst = sel ? overlay : lines;
            const uint32_t ec = sel ? kSelEdge : 0xcc8c60c0u;
            for (const auto& [a, b] : g->edges) {
                dst.push_back(gv(a, ec));
                dst.push_back(gv(b, ec));
            }
        }
        if (g->hasArrow) {
            auto& dst = sel ? overlay : lines;
            dst.push_back(gv(g->arrowFrom, 0xffffffffu));
            dst.push_back(gv(g->arrowTo, 0xffffffffu));
        }
    }
    // Selected faces: tinted fill drawn on top plus an outline and a cross, so a face reads as selected
    // even when its brush is not.
    for (const FaceRef& f : doc.faceSelection()) {
        const EntityItem* it = doc.findItem(f.entity, f.item);
        const Primitive* p = it ? it->prim() : nullptr;
        if (!p || f.face >= p->faces.size()) continue;
        const auto wins = faceWindingsOf(*p);
        const Winding& w = wins[f.face];
        if (w.size() < 3) continue;
        const auto pl = facePlane(*p, f.face);
        const Vec3 lift = pl ? pl->n * 0.05 : Vec3{};
        for (size_t k = 1; !wire && k + 1 < w.size(); ++k) {
            tris.push_back(gv(w[0] + lift, kSelRed));
            tris.push_back(gv(w[k] + lift, kSelRed));
            tris.push_back(gv(w[k + 1] + lift, kSelRed));
        }
        Vec3 c;
        for (const auto& v : w) c += v;
        c = c * (1.0 / double(w.size()));
        for (size_t k = 0; k < w.size(); ++k) {
            overlay.push_back(gv(w[k], kSelEdge));
            overlay.push_back(gv(w[(k + 1) % w.size()], kSelEdge));
            overlay.push_back(gv(w[k], kSelEdge));
            overlay.push_back(gv(c, kSelEdge));
        }
    }
    // Reflection probe volumes (5.5): mins/maxs keys relative to the origin, drawn as a teal wire box.
    for (const auto& e : doc.map().entities) {
        if (e.classname() != "reflection_probe") continue;
        const std::string* sm = e.value("mins");
        const std::string* sx = e.value("maxs");
        Vec3 mn, mx;
        if (!sm || !sx || std::sscanf(sm->c_str(), "%lf %lf %lf", &mn.x, &mn.y, &mn.z) != 3 ||
            std::sscanf(sx->c_str(), "%lf %lf %lf", &mx.x, &mx.y, &mx.z) != 3)
            continue;
        const Vec3 o = e.origin().value_or(Vec3{});
        const Vec3 a = o + mn, b = o + mx;
        const uint32_t teal = 0xffc8c840u;
        auto seg = [&](const Vec3& p, const Vec3& q) {
            lines.push_back(gv(p, teal));
            lines.push_back(gv(q, teal));
        };
        const Vec3 c[8] = {{a.x, a.y, a.z}, {b.x, a.y, a.z}, {b.x, b.y, a.z}, {a.x, b.y, a.z},
                           {a.x, a.y, b.z}, {b.x, a.y, b.z}, {b.x, b.y, b.z}, {a.x, b.y, b.z}};
        for (int k = 0; k < 4; ++k) {
            seg(c[k], c[(k + 1) % 4]);
            seg(c[4 + k], c[4 + (k + 1) % 4]);
            seg(c[k], c[4 + k]);
        }
    }
    if (scene.tagsFor && !scene.captureMode) {
        // 3.18: the tags of each selected model (attach points: tag_origin, tag_fx, tag_light ...), on top.
        std::set<Id> done;
        for (const SelRef& r : doc.selection()) {
            if (!done.insert(r.entity).second) continue;
            const Entity* e = doc.findEntity(r.entity);
            const std::string* m = e ? e->value("model") : nullptr;
            if (!m || m->empty()) continue;
            const std::vector<Vec3> tags = scene.tagsFor(*m);
            if (tags.empty()) continue;
            const Vec3 o = e->origin().value_or(Vec3{});
            Vec3 ang;
            if (const std::string* a = e->value("angles")) std::sscanf(a->c_str(), "%lf %lf %lf", &ang.x, &ang.y, &ang.z);
            const Mat3 rot = anglesToMatrix(ang);
            double ms = 1.0;
            if (const std::string* s = e->value("modelscale")) ms = std::max(0.001, std::atof(s->c_str()));
            const uint32_t tc = 0xff30e0ffu;  // yellow (R in the low byte)
            for (const Vec3& t : tags) {
                const Vec3 p = o + rot * (t * ms);
                const double k = 2.0;
                for (const Vec3& d : {Vec3{k, 0, 0}, Vec3{0, k, 0}, Vec3{0, 0, k}}) {
                    overlay.push_back(gv(p - d, tc));
                    overlay.push_back(gv(p + d, tc));
                }
            }
        }
    }
    if (textured)
        for (auto& b : *textured)
            if (b.sway && !b.tris.empty()) {
                float z = b.tris.front().z;
                for (const auto& v : b.tris) z = std::min(z, v.z);
                b.baseZ = z;
            }
    for (const UmbraLine& l : scene.tomeLines) {  // Umbra cells and portals (M2.2)
        lines.push_back(gv(l.a, l.rgba));
        lines.push_back(gv(l.b, l.rgba));
    }
    for (const auto& [a, b] : scene.navLines) {  // the build's navmesh (6.23), green
        lines.push_back(gv(a, 0xff30e060u));
        lines.push_back(gv(b, 0xff30e060u));
    }
    for (const auto& [a, b] : scene.compiledWorld) {  // the build's render surfaces, magenta
        lines.push_back(gv(a, 0xffff50e0u));
        lines.push_back(gv(b, 0xffff50e0u));
    }
    for (const auto& h : scene.collisionPreview) {
        // Solid cyan, player clip orange, ladder green, anything else magenta (R in the low byte).
        const uint32_t c = h.ladder ? 0xff40ff40u : (h.contents & 0x10000u) && !(h.contents & 1u) ? 0xff2090ffu
                           : (h.contents & 1u) ? 0xffffe040u : 0xffff40ffu;
        for (const auto& [a, b] : h.edges) {
            lines.push_back(gv(a, c));
            lines.push_back(gv(b, c));
        }
    }
    for (size_t i = 0; i + 1 < scene.pointfile.size(); ++i) {
        overlay.push_back(gv(scene.pointfile[i], 0xff20d0ffu));
        overlay.push_back(gv(scene.pointfile[i + 1], 0xff20d0ffu));
    }
    if (showSpawns) {
        for (const auto& s : scene.spawns) {
            const uint32_t c = 0xff40ff40u;
            const Vec3 o = s.origin;
            const Vec3 mn = o + Vec3{-16, -16, 0}, mx = o + Vec3{16, 16, 72};
            const Vec3 b[8] = {{mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
                               {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}};
            const int e[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (const auto& ed : e) {
                lines.push_back(gv(b[ed[0]], c));
                lines.push_back(gv(b[ed[1]], c));
            }
            const double y = s.angles.y * 3.14159265358979 / 180.0;
            lines.push_back(gv(o + Vec3{0, 0, 36}, c));
            lines.push_back(gv(o + Vec3{std::cos(y) * 40, std::sin(y) * 40, 36}, c));
        }
    }
    // Entity connections (target -> targetname).
    for (const auto& [a, b] : scene.connections()) {
        overlay.push_back(gv(a, 0xff30d0ffu));
        overlay.push_back(gv(b, 0xff30d0ffu));
    }
    // World axes at the origin (X red, Y green, Z blue).
    lines.push_back(gv({0, 0, 0}, 0xff3030ffu));
    lines.push_back(gv({64, 0, 0}, 0xff3030ffu));
    lines.push_back(gv({0, 0, 0}, 0xff30ff30u));
    lines.push_back(gv({0, 64, 0}, 0xff30ff30u));
    lines.push_back(gv({0, 0, 0}, 0xffff3030u));
    lines.push_back(gv({0, 0, 64}, 0xffff3030u));
    if (scene.captureMode) {  // probe capture: only surfaces (no outlines, markers or helpers)
        lines.clear();
        overlay.clear();
    }
    tris.insert(tris.begin(), shadowOnly.begin(), shadowOnly.end());
    scene.shadowOnlyVertices = shadowOnly.size();
    scene.shadowCasterVertices = tris.size();
    tris.insert(tris.end(), noCast.begin(), noCast.end());
}
}  // namespace

}  // namespace iw8r::app
namespace iw8r::app {

size_t buildReferenceTris(const StockWorld& world, std::vector<GpuVertex>& out, bool skipSky) {
    out.clear();
    size_t total = 0;
    for (const auto& m : world.meshes) total += m.idx.size();
    out.reserve(total);
    const Vec3 light = normalize(Vec3{0.35, 0.55, 0.75});
    for (const auto& m : world.meshes) {
        std::string lm = m.material;
        for (char& c : lm) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (skipSky) {  // the sky box (tw/sky_<map>), not skyline / skyscraper surfaces (mp_shipment's city)
            const std::string leaf = lm.substr(lm.rfind('/') == std::string::npos ? 0 : lm.rfind('/') + 1);
            if (leaf == "sky" || leaf.rfind("sky_", 0) == 0) continue;
        }
        for (size_t i = 0; i + 2 < m.idx.size(); i += 3) {
            const uint32_t a = m.idx[i], b = m.idx[i + 1], c = m.idx[i + 2];
            if (a >= m.pos.size() || b >= m.pos.size() || c >= m.pos.size()) continue;
            const Vec3 n = normalize(cross(m.pos[b] - m.pos[a], m.pos[c] - m.pos[a]));
            const double shade = 0.5 + 0.4 * std::fabs(dot(n, light));
            const uint32_t g = uint32_t(std::clamp(176.0 * shade, 0.0, 255.0));
            const uint32_t rgba = g | (g << 8) | (uint32_t(std::min(255.0, g * 1.05)) << 16) | (0xffu << 24);
            for (uint32_t k : {a, b, c}) out.push_back({float(m.pos[k].x), float(m.pos[k].y), float(m.pos[k].z), rgba});
        }
    }
    return out.size() / 3;
}

}  // namespace iw8r::app

namespace iw8r::app {

size_t buildReferenceTexBatches(const StockWorld& world, std::vector<ReferenceTexBatch>& out, bool skipSky, size_t maxTriangles) {
    out.clear();
    size_t total = 0, withUv = 0;
    for (const auto& m : world.meshes) {
        total += m.idx.size() / 3;
        if (m.uv.size() == m.pos.size() && !m.uv.empty()) ++withUv;
    }
    if (total > maxTriangles || withUv == 0) return 0;
    const Vec3 light = normalize(Vec3{0.35, 0.55, 0.75});
    std::map<std::string, size_t> byMaterial;
    size_t tris = 0;
    for (size_t mi = 0; mi < world.meshes.size(); ++mi) {
        const auto& m = world.meshes[mi];
        // Its stock lightmap UVs (research 94; (-1, -1) where the surface has none).
        const std::vector<std::array<float, 2>>* lmuv =
            mi < world.lightmapUv.size() && world.lightmapUv[mi].size() == m.pos.size() ? &world.lightmapUv[mi] : nullptr;
        // Which lightmap (Warzone maps have many): the vertex carries index + u in its lightmap u (the shader splits it).
        const std::vector<int>* lmidx =
            mi < world.lightmapIndex.size() && world.lightmapIndex[mi].size() == m.pos.size() ? &world.lightmapIndex[mi] : nullptr;
        std::string lm = m.material;
        for (char& c : lm) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (skipSky) {  // the sky box (tw/sky_<map>), not skyline / skyscraper surfaces (mp_shipment's city)
            const std::string leaf = lm.substr(lm.rfind('/') == std::string::npos ? 0 : lm.rfind('/') + 1);
            if (leaf == "sky" || leaf.rfind("sky_", 0) == 0) continue;
        }
        if (m.uv.size() != m.pos.size()) continue;
        auto [it, fresh] = byMaterial.emplace(m.material, out.size());
        if (fresh) out.push_back({m.material, {}});
        const size_t baseBatch = it->second;  // an index: adding the layer batches below may move `out`
        // Layers above 0 of a layered stock material: one batch per material and layer (research 97).
        const int layers = mi < world.layerCount.size() ? world.layerCount[mi] : 1;
        const bool layered = layers > 1 && mi < world.layerUv.size() && world.layerUv[mi].size() == m.pos.size() * size_t(layers);
        std::vector<size_t> layerBatch;
        if (layered)
            for (int L = 1; L < layers; ++L) {
                auto [lit, lfresh] = byMaterial.emplace(m.material + '\x01' + std::to_string(L), out.size());
                if (lfresh) {
                    out.push_back({m.material, {}});
                    out.back().layer = L;
                }
                layerBatch.push_back(lit->second);
            }
        // References only now that `out` holds every batch of this mesh (a push_back above can move its vectors).
        auto& dst = out[baseBatch].tris;
        std::vector<std::vector<GpuTexVertex>*> layerDst;
        for (size_t b : layerBatch) layerDst.push_back(&out[b].tris);
        const std::vector<uint32_t>* vcol = mi < world.vertexColor.size() && world.vertexColor[mi].size() == m.pos.size() ? &world.vertexColor[mi] : nullptr;
        // Layer 0's weight (decals, research 98): twc4 byte 0, twc the vertex alpha, others 1.
        const bool twc4 = lm.rfind("twc4/", 0) == 0, twc = !twc4 && lm.rfind("twc/", 0) == 0;
        const std::vector<uint32_t>* vcolHi =
            layered && mi < world.vertexColorHigh.size() && world.vertexColorHigh[mi].size() == m.pos.size() ? &world.vertexColorHigh[mi] : nullptr;
        // The triangle's GfxWorld surface, for the Umbra tome cull (referenceSurfaceTag; 0 = of no known surface).
        const std::vector<uint32_t>* triSurf =
            mi < world.triSurface.size() && world.triSurface[mi].size() * 3 == m.idx.size() ? &world.triSurface[mi] : nullptr;
        for (size_t i = 0; i + 2 < m.idx.size(); i += 3) {
            const uint32_t a = m.idx[i], b = m.idx[i + 1], c = m.idx[i + 2];
            if (a >= m.pos.size() || b >= m.pos.size() || c >= m.pos.size()) continue;
            const Vec3 n = normalize(cross(m.pos[b] - m.pos[a], m.pos[c] - m.pos[a]));
            const double shade = 0.55 + 0.45 * std::fabs(dot(n, light));
            const uint32_t g = uint32_t(std::clamp(255.0 * shade, 0.0, 255.0));
            const uint32_t rgba = g | (g << 8) | (g << 16) | (0xffu << 24);
            // The texture tangent of the triangle (the direction of +u in the game's uv), for the stock normal maps: the
            // world's own tangent frames are not read. Any direction across the face where the uv is degenerate.
            const auto tangentOf = [&](float u0, float v0, float u1, float v1, float u2, float v2) {
                const Vec3 e1 = m.pos[b] - m.pos[a], e2 = m.pos[c] - m.pos[a];
                const double du1 = u1 - u0, dv1 = v1 - v0, du2 = u2 - u0, dv2 = v2 - v0;
                const double det = du1 * dv2 - du2 * dv1;
                Vec3 t = std::fabs(det) > 1e-12 ? (e1 * dv2 - e2 * dv1) * (1.0 / det) : e1;
                t = t - n * dot(t, n);
                if (length(t) < 1e-9) t = std::fabs(n.z) < 0.9 ? cross(n, Vec3{0, 0, 1}) : cross(n, Vec3{1, 0, 0});
                return normalize(t);
            };
            const Vec3 tan0 = tangentOf(float(m.uv[a][0]), 1.0f - float(m.uv[a][1]), float(m.uv[b][0]), 1.0f - float(m.uv[b][1]),
                                        float(m.uv[c][0]), 1.0f - float(m.uv[c][1]));
            std::vector<Vec3> tanL;
            for (int L = 1; L <= int(layerDst.size()); ++L) {
                const auto& ua = world.layerUv[mi][size_t(a) * size_t(layers) + size_t(L)];
                const auto& ub = world.layerUv[mi][size_t(b) * size_t(layers) + size_t(L)];
                const auto& uc = world.layerUv[mi][size_t(c) * size_t(layers) + size_t(L)];
                tanL.push_back(tangentOf(ua[0], 1.0f - ua[1], ub[0], 1.0f - ub[1], uc[0], 1.0f - uc[1]));
            }
            // A layer whose weight is 0 at the three corners adds nothing to this triangle (the reveal ramp and the
            // plain weight both give b = 0 at w = 0): it is left out (Hideout drew 21 M triangles a frame with every
            // layer covering the whole terrain).
            bool layerOn[kStockMaxLayers] = {};
            for (int L = 1; L <= int(layerDst.size()); ++L)
                for (uint32_t k : {a, b, c})
                    if (stockLayerWeight(vcol ? (*vcol)[k] : 0xFFFFFFFFu, vcolHi ? (*vcolHi)[k] : 0xFFFFFFFFu, L) > 0.5f / 255.0f)
                        layerOn[L - 1] = true;
            for (uint32_t k : {a, b, c}) {
                // ImportedMesh::uv is OBJ convention (v flipped); the textured camera uses the game's v.
                GpuTexVertex v{float(m.pos[k].x), float(m.pos[k].y), float(m.pos[k].z), m.uv[k][0], 1.0f - m.uv[k][1], rgba};
                v.nx = float(n.x), v.ny = float(n.y), v.nz = float(n.z);
                v.edge = referenceSurfaceTag(triSurf ? int64_t((*triSurf)[i / 3]) : -1);
                {
                    const uint32_t col = vcol ? (*vcol)[k] : 0xFFFFFFFFu;
                    const double w0 = twc4 ? double(col & 0xFF) / 255.0 : twc ? double(col >> 24) / 255.0 : 1.0;
                    const Vec3 t0 = tan0 * (1.0 + w0);  // length 1 + weight, as the layers
                    v.tx = float(t0.x), v.ty = float(t0.y), v.tz = float(t0.z);
                }
                if (lmuv && (*lmuv)[k][0] >= 0) {
                    const int li = lmidx ? (*lmidx)[k] : 0;
                    if (li >= 0) v.lu = float(li) + std::min((*lmuv)[k][0], 0.99994f), v.lv = (*lmuv)[k][1];
                }
                dst.push_back(v);
                for (int L = 1; L <= int(layerDst.size()); ++L) {
                    if (!layerOn[L - 1]) continue;
                    GpuTexVertex lv = v;
                    const auto& luv = world.layerUv[mi][size_t(k) * size_t(layers) + size_t(L)];
                    lv.u = luv[0], lv.v = 1.0f - luv[1];
                    // The layer's own tangent (its uv), its length 1 + the blend weight (the shader takes length - 1).
                    const float w = stockLayerWeight(vcol ? (*vcol)[k] : 0xFFFFFFFFu, vcolHi ? (*vcolHi)[k] : 0xFFFFFFFFu, L);
                    const Vec3 tl = tanL[size_t(L - 1)] * (1.0 + double(w));
                    lv.tx = float(tl.x), lv.ty = float(tl.y), lv.tz = float(tl.z);
                    layerDst[size_t(L - 1)]->push_back(lv);
                }
            }
            ++tris;
        }
    }
    return tris;
}

}  // namespace iw8r::app

namespace iw8r::app {

const CollisionWorld* SceneCache::shadowWorld(const Document& doc) const {
    if (!shadowWorld_ || shadowRevision_ != doc.revision()) {
        auto w = std::make_shared<CollisionWorld>();
        w->build(doc.map());
        shadowWorld_ = std::move(w);
        shadowRevision_ = doc.revision();
    }
    return shadowWorld_.get();
}

}  // namespace iw8r::app
