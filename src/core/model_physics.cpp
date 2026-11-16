#include "model_physics.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include "collision_world.h"
#include "convex_hull.h"
#include "map_text.h"
#include "stock_smodels.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace iw8r {

namespace {

std::string stripMarks(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ',' || s[a] == '~')) ++a;
    return s.substr(a);
}

std::string baseName(const std::string& s) {
    const size_t slash = s.find_last_of('/');
    return slash == std::string::npos ? s : s.substr(slash + 1);
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out;
    size_t at = 0;
    while (true) {
        const size_t tab = line.find('\t', at);
        out.push_back(line.substr(at, tab == std::string::npos ? std::string::npos : tab - at));
        if (tab == std::string::npos) break;
        at = tab + 1;
    }
    return out;
}

// "x,y,z x,y,z;x,y,z ..." -> pieces. A malformed piece is dropped.
std::vector<std::vector<Vec3>> parsePieces(const std::string& text) {
    std::vector<std::vector<Vec3>> out;
    const char* p = text.c_str();
    std::vector<Vec3> piece;
    bool bad = false;
    while (*p) {
        char* end = nullptr;
        double v[3];
        for (int k = 0; k < 3; ++k) {
            v[k] = std::strtod(p, &end);
            if (end == p) {
                bad = true;
                break;
            }
            p = end;
            if (k < 2) {
                if (*p != ',') {
                    bad = true;
                    break;
                }
                ++p;
            }
        }
        if (bad) break;
        piece.push_back(Vec3{v[0], v[1], v[2]});
        if (*p == ' ') {
            ++p;
        } else if (*p == ';' || *p == '\0') {
            if (piece.size() >= 4) out.push_back(std::move(piece));
            piece.clear();
            if (*p == ';') ++p;
        } else {
            bad = true;
            break;
        }
    }
    if (bad) return {};
    return out;
}

std::string exeDir() {
#ifdef _WIN32
    wchar_t buf[4096];
    const DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    if (n > 0 && n < std::size(buf)) return std::filesystem::path(std::wstring(buf, n)).parent_path().string();
#endif
    return std::string();
}

}  // namespace

void ModelPhysicsCatalog::loadText(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    // Shapes first resolve by name; the model records point at them once everything is read.
    std::vector<std::pair<std::string, std::string>> pending;  // model -> physics asset
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 3 || line[0] == '#' || line[1] != '\t') continue;
        const std::vector<std::string> f = splitTabs(line);
        if (line[0] == 'M' && f.size() >= 4) {
            const std::string name = stripMarks(f[1]);
            if (name.empty() || models_.count(name)) continue;
            ModelPhysicsInfo info;
            info.physicsAsset = f[2] == "-" ? std::string() : stripMarks(f[2]);
            info.detailCollision = f[3] == "-" ? std::string() : stripMarks(f[3]);
            models_.emplace(name, info);
            byBase_.emplace(baseName(name), name);
            if (!info.physicsAsset.empty()) pending.emplace_back(name, info.physicsAsset);
        } else if (line[0] == 'P' && f.size() >= 4) {
            const std::string name = stripMarks(f[1]);
            if (name.empty() || shapes_.count(name)) continue;
            ModelPhysicsShape s;
            s.contents = uint32_t(std::strtoul(f[2].c_str(), nullptr, 16));
            if (f[3] != "mesh" && f[3] != "-") s.pieces = parsePieces(f[3]);
            // Triangles: "<x,y,z ...>|<i,j,k ...>" (absent in the older four-column records, whose 5th field is a note).
            if (f.size() >= 5)
                if (const size_t bar = f[4].find('|'); bar != std::string::npos) {
                    std::vector<std::vector<Vec3>> verts = parsePieces(f[4].substr(0, bar));
                    if (verts.size() == 1) s.meshVerts = std::move(verts[0]);
                    const char* p = f[4].c_str() + bar + 1;
                    while (*p) {
                        char* end = nullptr;
                        std::array<uint32_t, 3> t{};
                        bool ok = true;
                        for (int k = 0; k < 3 && ok; ++k) {
                            const unsigned long v = std::strtoul(p, &end, 10);
                            ok = end != p && v < s.meshVerts.size();
                            t[size_t(k)] = uint32_t(v);
                            p = end;
                            if (k < 2) ok = ok && *p == ',', p += ok ? 1 : 0;
                        }
                        if (!ok) {
                            s.meshTris.clear();
                            break;
                        }
                        s.meshTris.push_back(t);
                        if (*p == ' ') ++p;
                        else if (*p) {
                            s.meshTris.clear();
                            break;
                        }
                    }
                    if (s.meshTris.empty()) s.meshVerts.clear();
                }
            shapes_.emplace(name, std::move(s));
        }
    }
    for (const auto& [model, pa] : pending)
        if (const auto s = shapes_.find(pa); s != shapes_.end()) models_[model].shape = &s->second;
}

bool ModelPhysicsCatalog::load(const std::string& path, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    loadText(ss.str());
    return true;
}

const ModelPhysicsInfo* ModelPhysicsCatalog::find(const std::string& model) const {
    const std::string name = stripMarks(model);
    if (const auto it = models_.find(name); it != models_.end()) return &it->second;
    if (const auto b = byBase_.find(baseName(name)); b != byBase_.end())
        if (const auto it = models_.find(b->second); it != models_.end()) return &it->second;
    return nullptr;
}

const ModelPhysicsCatalog& ModelPhysicsCatalog::shared(const std::string& startDir) {
    static const ModelPhysicsCatalog catalog = [&] {
        ModelPhysicsCatalog c;
        std::error_code ec;
        for (const std::string& from : {exeDir(), startDir}) {
            if (from.empty()) continue;
            std::filesystem::path d = std::filesystem::absolute(from, ec);
            for (int up = 0; up < 6 && !d.empty(); ++up, d = d.parent_path()) {
                const auto f = d / "data" / "asset_catalog" / "iw8_model_physics.tsv";
                if (std::filesystem::is_regular_file(f, ec)) {
                    c.load(f.string());
                    return c;
                }
                if (d == d.parent_path()) break;
            }
        }
        return c;
    }();
    return catalog;
}

ModelCollisionChoice chooseModelCollision(const std::string& model, const Entity* placement, bool customModel,
                                          const ModelPhysicsCatalog* catalog) {
    if (placement) {
        if (const std::string* v = placement->value("iw8r_collision")) {
            if (*v == "0") return {ModelCollisionSource::None, nullptr, "iw8r_collision 0"};
            if (*v == "1") return {ModelCollisionSource::RenderMesh, nullptr, "iw8r_collision 1"};
        }
        if (const std::string* v = placement->value("iw8r_no_collision"); v && *v == "1")
            return {ModelCollisionSource::None, nullptr, "iw8r_no_collision 1"};
    }
    if (customModel) return {ModelCollisionSource::RenderMesh, nullptr, "own model"};
    if (!catalog || catalog->size() == 0) return {ModelCollisionSource::RenderMesh, nullptr, "no physics catalogue"};
    const ModelPhysicsInfo* info = catalog->find(model);
    if (!info) return {ModelCollisionSource::RenderMesh, nullptr, "not in the physics catalogue"};
    if (info->physicsAsset.empty()) return {ModelCollisionSource::None, nullptr, "no stock physics asset"};
    if (info->shape && info->shape->readable()) return {ModelCollisionSource::PhysicsShape, info->shape, "stock physics shape"};
    return {ModelCollisionSource::RenderMesh, nullptr, "stock physics not read"};
}

std::vector<std::vector<Plane>> physicsShapePlanes(const ModelPhysicsShape& shape, size_t maxPlanes, int maxPieces, bool meshPieces) {
    std::vector<std::vector<Plane>> out;
    if (meshPieces && !shape.meshTris.empty()) {
        // The physics mesh as a model: convexPieces splits it like a render mesh (and keeps every piece closed).
        ImportedModel m;
        ImportedMesh mesh;
        mesh.pos = shape.meshVerts;
        for (const auto& t : shape.meshTris)
            for (const uint32_t i : t) mesh.idx.push_back(i);
        m.meshes.push_back(std::move(mesh));
        for (auto& pc : convexPieces(m, maxPieces, maxPlanes)) out.push_back(std::move(pc));
    }
    for (const auto& piece : shape.pieces) {
        AABB box;
        for (const Vec3& v : piece) box.add(v);
        if (!box.valid()) continue;
        std::vector<Plane> pl = convexHullPlanes(piece);
        if (!pl.empty() && maxPlanes >= 4 && pl.size() > maxPlanes) pl = simplifyHull(pl, maxPlanes);
        thickenHull(pl, piece, 0.5);  // a sliver at least half a unit thick (thinner ones become slabs in the world)
        // Never an open or oversized solid (a slab through the map traps the player): the piece's box instead, a little
        // thickened when flat.
        if (pl.empty() || !convexSolidWithin(pl, box)) {
            for (int a = 0; a < 3; ++a)
                if (box.maxs[a] - box.mins[a] < 0.5) box.mins[a] -= 0.25, box.maxs[a] += 0.25;
            pl = {Plane{Vec3{1, 0, 0}, box.maxs.x}, Plane{Vec3{-1, 0, 0}, -box.mins.x}, Plane{Vec3{0, 1, 0}, box.maxs.y},
                  Plane{Vec3{0, -1, 0}, -box.mins.y}, Plane{Vec3{0, 0, 1}, box.maxs.z}, Plane{Vec3{0, 0, -1}, -box.mins.z}};
        }
        out.push_back(std::move(pl));
    }
    return out;
}

std::vector<Plane> placePlanes(const std::vector<Plane>& piece, const Vec3& origin, const Vec3 (&axis)[3], double scale) {
    std::vector<Plane> out;
    out.reserve(piece.size());
    for (const Plane& pl : piece) {
        // A unit normal again if the axes are not exactly unit (axes k R: the solid scales by k too); a lost direction
        // drops the piece.
        const Vec3 r = axis[0] * pl.n.x + axis[1] * pl.n.y + axis[2] * pl.n.z;
        const double len = length(r);
        if (!(len > 1e-6)) return {};
        const Vec3 n = r * (1.0 / len);
        out.push_back({n, scale * pl.d * len + dot(n, origin)});
    }
    return out;
}

StockPropCollisionStats addStockPropCollision(
    const std::vector<StockModelInstance>& props, CollisionWorld& world, const Vec3& around, double radius,
    const ModelPhysicsCatalog& catalog, size_t maxPlanes,
    const std::function<const std::vector<std::vector<Plane>>*(const std::string& model)>& renderPieces,
    const std::function<bool(const StockModelInstance&)>& skip) {
    StockPropCollisionStats st;
    // Per physics asset: its planes and the radius of its pieces about the model origin (model space).
    struct Built {
        std::vector<std::vector<Plane>> planes;
        double reach = 0;
    };
    std::unordered_map<const ModelPhysicsShape*, Built> built;
    std::unordered_map<std::string, ModelCollisionChoice> choice;
    for (const StockModelInstance& p : props) {
        if (p.scale <= 0 || (skip && skip(p))) continue;  // hidden (scale 0, or by the document): no collision either
        auto ci = choice.find(p.model);
        if (ci == choice.end()) ci = choice.emplace(p.model, chooseModelCollision(p.model, nullptr, false, &catalog)).first;
        const ModelCollisionChoice& c = ci->second;
        const std::vector<std::vector<Plane>>* pieces = nullptr;
        double reach = 0;
        if (c.source == ModelCollisionSource::PhysicsShape) {
            auto bi = built.find(c.shape);
            if (bi == built.end()) {
                Built b;
                b.planes = physicsShapePlanes(*c.shape, maxPlanes, 4, false);  // the triangles become facets below
                for (const auto& piece : c.shape->pieces)
                    for (const Vec3& v : piece) b.reach = std::max(b.reach, std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z));
                for (const Vec3& v : c.shape->meshVerts) b.reach = std::max(b.reach, std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z));
                bi = built.emplace(c.shape, std::move(b)).first;
            }
            pieces = &bi->second.planes;
            reach = bi->second.reach;
        }
        if (radius > 0) {
            // Range test before any mesh work: the physics pieces' reach, else a generous 512 units for a prop.
            const double r = radius + (pieces ? reach : 512.0) * p.scale;
            const double dx = p.origin.x - around.x, dy = p.origin.y - around.y;
            if (dx * dx + dy * dy > r * r) continue;
        }
        ++st.considered;
        if (c.source == ModelCollisionSource::None) {
            ++st.walkThrough;
            continue;
        }
        if (c.source == ModelCollisionSource::RenderMesh) {
            pieces = renderPieces ? renderPieces(p.model) : nullptr;
            if (!pieces) {
                ++st.pending;
                continue;
            }
            ++st.renderMesh;
        } else {
            ++st.physics;
        }
        const Vec3 axis[3] = {p.axis[0], p.axis[1], p.axis[2]};
        for (const auto& piece : *pieces)
            if (world.addConvex(placePlanes(piece, p.origin, axis, p.scale))) ++st.solids;
        if (c.source == ModelCollisionSource::PhysicsShape && !c.shape->meshTris.empty()) {
            // The physics mesh as the game has it: one facet per triangle, placed like the model.
            const auto place = [&](const Vec3& v) { return p.origin + (axis[0] * v.x + axis[1] * v.y + axis[2] * v.z) * p.scale; };
            for (const auto& t : c.shape->meshTris)
                if (world.addTriangle(place(c.shape->meshVerts[t[0]]), place(c.shape->meshVerts[t[1]]), place(c.shape->meshVerts[t[2]]), 1.0))
                    ++st.facets;
        }
    }
    return st;
}

}  // namespace iw8r
