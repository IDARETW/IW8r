// IW8 Radiant Suite - native IW8 map compiler front end. Contract: docs/research/15_iw8_extraction_contract.md.
#include "ies_profiles.h"
#include "compile_iw8.h"
#include "game_modes.h"
#include "collision_world.h"
#include "light_grid.h"
#include "patch_ops.h"
#include "vertex_paint.h"
#include "audio_zones.h"
#include "sound_settings.h"
#include "stock_loot.h"
#include "custom_sounds.h"
#include "fx_author.h"
#include "smodel_flags.h"
#include "sun_flare.h"
#include "atmosphere.h"
#include "map_script.h"
#include "iw8_contents.h"
#include "minimap.h"
#include "model_import.h"
#include "convex_hull.h"
#include "sky_image.h"
#include "tex_projection.h"
#include "prefab.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <utility>

#include "brush_ops.h"
#include "patch_tess.h"

namespace iw8r {
namespace {

namespace fs = std::filesystem;

constexpr double kPi = 3.14159265358979323846;
constexpr size_t kMaxVerticesPerSurface = 60000;  // task rule: split surfaces at 60000 vertices
constexpr size_t kMaxSurfaces = 65535;            // doc 15 §2.1: surfaces is 1..65535
constexpr double kBoundsPad = 16.0;               // task rule: world bounds padded by 16
constexpr double kDefaultTexSize = 128.0;         // texture size assumed when a face has no texture fields
constexpr int kProbeSize = 64;                    // doc 15 §7: 64x64 cube, 7 mips
constexpr int kProbeMips = 7;

// ---------------------------------------------------------------- small helpers

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool startsWith(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

bool isSkyMaterial(const std::string& mat) { return lower(mat).find("sky") != std::string::npos; }

// Task rule: brushes made only of trigger*, hint* or skip* faces are not collision.
bool isTriggerHintSkipMaterial(const std::string& mat) {
    const std::string m = lower(mat);
    return startsWith(m, "trigger") || startsWith(m, "hint") || startsWith(m, "skip");
}

std::string firstToken(const std::string& suffix) {
    const auto tok = splitTokens(suffix);
    return tok.empty() ? std::string() : tok[0];
}

bool parseNumber(const std::string& s, double& out) {
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0' || !std::isfinite(v)) return false;
    out = v;
    return true;
}

bool parseVec3Text(const std::string& s, double out[3]) {
    const auto tok = splitTokens(s);
    if (tok.size() < 3) return false;
    for (size_t k = 0; k < 3; ++k)
        if (!parseNumber(tok[k], out[k])) return false;
    return true;
}

bool allDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

bool finite3(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// Material names become materials/<name>.json, so keep them to a plain relative path.
bool isSafeMaterialName(const std::string& n) {
    if (n.empty() || n.front() == '/' || n.back() == '/' || n.find("..") != std::string::npos || n.find("//") != std::string::npos)
        return false;
    for (char c : n) {
        const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '/' || c == '.';
        if (!ok) return false;
    }
    return true;
}

std::string techniqueSetFor(const std::string& material) {
    std::string out;
    for (char c : material) out += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    return out;
}

// Sun direction toward the sun for Radiant "pitch yaw" (doc 15 §7: pitch -50, yaw 45 -> (0.4545, 0.4545, 0.766)).
Vec3 sunDirection(double pitchDeg, double yawDeg) {
    const double p = pitchDeg * kPi / 180.0, y = yawDeg * kPi / 180.0;
    return normalize({std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), -std::sin(p)});
}

// ---------------------------------------------------------------- JSON output

std::string num(double v) {
    if (!std::isfinite(v) || std::fabs(v) < 1e-9) return "0";
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

std::string vec3(const Vec3& v) { return "[" + num(v.x) + "," + num(v.y) + "," + num(v.z) + "]"; }

std::string vec3raw(double x, double y, double z) { return "[" + num(x) + "," + num(y) + "," + num(z) + "]"; }

std::string aabbJson(const AABB& b) { return "[" + vec3(b.mins) + "," + vec3(b.maxs) + "]"; }

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", unsigned(c));
                    out += buf;
                } else {
                    out += char(c);
                }
        }
    }
    return out + "\"";
}

// Indices 0..n-1 as a JSON list body.
std::string indexList(size_t n) {
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        if (i) out += ",";
        out += std::to_string(i);
    }
    return out;
}

// ---------------------------------------------------------------- texture projection (Radiant / Quake axes)

struct Axes {
    Vec3 s, t;
};

// Base axes for the dominant normal component (Z: x / -y, X: y / -z, Y: x / -z).
Axes dominantAxes(const Vec3& n) {
    const double ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
    if (az >= ax && az >= ay) return {{1, 0, 0}, {0, -1, 0}};
    if (ax >= ay) return {{0, 1, 0}, {0, 0, -1}};
    return {{1, 0, 0}, {0, 0, -1}};
}

struct TexProj {
    double width = kDefaultTexSize, height = kDefaultTexSize, xoff = 0, yoff = 0, rot = 0;
};

// The five numbers after the material: width, height, x offset, y offset, rotation (as Primitive::textureFields).
TexProj texProjection(const std::string& suffix) {
    TexProj tp;
    const auto tok = splitTokens(suffix);
    if (tok.size() >= 6) {
        double v[5];
        bool ok = true;
        for (size_t k = 0; k < 5 && ok; ++k) ok = parseNumber(tok[k + 1], v[k]);
        if (ok) {
            tp.width = v[0];
            tp.height = v[1];
            tp.xoff = v[2];
            tp.yoff = v[3];
            tp.rot = v[4];
        }
    }
    if (std::fabs(tp.width) < 1e-9) tp.width = kDefaultTexSize;
    if (std::fabs(tp.height) < 1e-9) tp.height = kDefaultTexSize;
    return tp;
}

struct Projected {
    double u = 0, v = 0;
    Vec3 tangent;
};

// uv = (s/width + xoff/width, t/height + yoff/height), where (s,t) is the axis projection rotated by rot degrees.
// Tangent = gradient of s over the position, projected into the face plane and normalized (binormal sign +1).
Projected projectPoint(const Vec3& p, const Vec3& n, const Axes& ax, const TexProj& tp) {
    const double rad = tp.rot * kPi / 180.0;
    const double c = std::cos(rad), sn = std::sin(rad);
    const double a = dot(p, ax.s), b = dot(p, ax.t);
    const double S = a * c - b * sn;
    const double T = a * sn + b * c;
    Projected out;
    out.u = S / tp.width + tp.xoff / tp.width;
    out.v = T / tp.height + tp.yoff / tp.height;
    const Vec3 g = ax.s * c - ax.t * sn;
    Vec3 tan = g - n * dot(g, n);
    if (length(tan) < 1e-9) {
        const Vec3 r = std::fabs(n.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        tan = r - n * dot(r, n);
    }
    out.tangent = normalize(tan);
    return out;
}

// ---------------------------------------------------------------- gathered geometry

struct RenderVertex {
    Vec3 pos, normal, tangent;
    double u = 0, v = 0;
    bool hasColor = false;  // 8.11: painted patch vertex colour (research 57); white when absent
    std::array<std::uint8_t, 4> color{255, 255, 255, 255};
};

// One brush face (a fan) or one patch triangle, with indices local to its own vertex list.
struct Chunk {
    size_t entity = 0;
    size_t group = 0;  // brush or patch primitive ordinal
    std::string editorMaterial;
    std::vector<RenderVertex> verts;
    std::vector<uint32_t> idx;
};

struct RenderSurface {
    std::string editorMaterial;
    std::vector<RenderVertex> verts;
    std::vector<uint32_t> idx;
};

struct CollisionBrush {
    std::vector<Plane> planes;  // dot(n, x) <= d, n outward (doc 15 §2.3)
    AABB bounds;
    uint32_t contents = 1;      // IW3-style source contents; the converter maps them (iw3_fastfile.cpp ConvertContents)
    int surfaceType = 0;        // Replay surface type for footsteps/impacts (brush "surface_type", iw3_fastfile.cpp:7194-7197)
    bool ladder = false;        // write ladder_planes: the converter keeps the widest vertical faces (iw3_fastfile.cpp:7250-7284)
};

// IW3 trigger material contents: the converter keeps bit 1 of 0x8000001 for trigger hulls
// (iw3_fastfile.cpp AddBrushlessTriggerBoxes comment, ConvertContents mask 0x33683).
constexpr uint32_t kTriggerContents = 0x8000001u;

// A brush entity compiled as brush model *N: collision submodel N (doc 15 §2.3) and brush_models[N] (§2.2).
struct BrushModelOut {
    size_t entity = 0;
    bool trigger = false;
    std::vector<Chunk> chunks;
    std::vector<size_t> brushes;  // indices into the collision brush list
    AABB bounds;
    size_t surfaceStart = 0, surfaceCount = 0;
};

struct Gather {
    int subdiv = 4;
    bool patchBackFaces = true;
    std::vector<Chunk> chunks;
    std::vector<CollisionBrush> brushes;
    std::set<std::string> editorMats;
    size_t skyFaces = 0;
    AABB bounds;
    std::vector<std::string>* warnings = nullptr;
    const MaterialLibrary* library = nullptr;
};

// IW3 surface type of an editor material: the library's for a custom material (doc 21), else by name.
int surfaceTypeOf(const std::string& material, const MaterialLibrary* library) {
    if (library)
        if (const CustomMaterial* cm = library->find(material)) return cm->surfaceType;
    return iw3SurfaceTypeForMaterial(material);
}

void gatherBrush(const Primitive& p, size_t entity, size_t group, Gather& g, bool forceCollision = false) {
    const std::string where = "entity " + std::to_string(entity);
    const ConvexBrush cb = brushFromPrimitive(p);
    if (!isValidBrush(cb)) {
        g.warnings->push_back(where + ": brush skipped (not a bounded convex solid)");
        return;
    }
    const std::vector<Winding> wins = brushWindings(cb);

    // Collision (doc 15 §2.3): contents from the face materials and the `contents` line (iw8_contents.h):
    // solid, player clip, item clip, AI no-sight; triggers, hints, skips and nonColliding brushes have none.
    const BrushContents bc = brushContents(p);
    if (bc.collides || forceCollision) {
        CollisionBrush cbr;
        cbr.contents = bc.collides ? bc.contents : 1u;
        cbr.ladder = bc.ladder;
        {
            // Surface type of the brush: the most common drawn face material (footsteps and impacts).
            std::map<std::string, int> uses;
            for (const auto& f : p.faces)
                if (!isIw8ToolMaterial(f.material())) ++uses[f.material()];
            const auto best = std::max_element(uses.begin(), uses.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
            if (best != uses.end()) cbr.surfaceType = replaySurfaceTypeForIw3(surfaceTypeOf(best->first, g.library));
        }
        for (size_t i = 0; i < cb.size() && i < wins.size(); ++i)
            if (!wins[i].empty()) cbr.planes.push_back(cb[i].plane);
        cbr.bounds = brushBounds(cb);
        g.bounds.add(cbr.bounds);
        g.brushes.push_back(std::move(cbr));
    }

    // Render (doc 15 §2.2): one fan per contributing face, indices [0,k,k+1] (CCW seen from outside).
    for (size_t fi = 0; fi < cb.size() && fi < wins.size(); ++fi) {
        const Winding& w = wins[fi];
        if (w.size() < 3) continue;
        const std::string mat = firstToken(cb[fi].suffix);
        if (mat.empty()) {
            g.warnings->push_back(where + ": face without a material skipped");
            continue;
        }
        if (isIw8ToolMaterial(mat)) {
            if (isSkyMaterial(mat)) ++g.skyFaces;
            continue;
        }
        g.editorMats.insert(mat);
        const Vec3 n = normalize(cb[fi].plane.n);
        // Brush faces use the shared projection (tex_projection.h), the same maths as the viewport and texture lock.
        const TexProj tpj = texProjection(cb[fi].suffix);
        const TexParams tp{tpj.width, tpj.height, tpj.xoff, tpj.yoff, tpj.rot};
        const Vec3 faceTan = faceTangent(n, tp);
        Chunk c;
        c.entity = entity;
        c.group = group;
        c.editorMaterial = mat;
        for (const Vec3& P : w) {
            const auto uv = faceUV(n, tp, P);
            c.verts.push_back({P, n, faceTan, uv[0], uv[1]});
            g.bounds.add(P);
        }
        for (size_t k = 1; k + 1 < w.size(); ++k) {
            c.idx.push_back(0);
            c.idx.push_back(uint32_t(k));
            c.idx.push_back(uint32_t(k + 1));
        }
        g.chunks.push_back(std::move(c));
    }
}

void gatherPatch(const Primitive& p, size_t entity, size_t group, Gather& g) {
    const std::string where = "entity " + std::to_string(entity);
    const std::string mat = p.patchMaterial;
    if (mat.empty()) {
        g.warnings->push_back(where + ": patch without a material skipped");
        return;
    }
    if (isIw8ToolMaterial(mat)) {
        if (isSkyMaterial(mat)) ++g.skyFaces;
        return;
    }
    const PatchMesh m = tessellatePrimitive(p, g.subdiv);
    // Painted vertex colours (8.11) follow the same tessellation; a patch without colour blocks writes none.
    const std::vector<Rgba8> colours = patchHasColours(p) ? tessellatePatchColours(p, g.subdiv) : std::vector<Rgba8>{};
    // The patch's own texture coordinates (file st / 1024, patch_ops kPatchStUnits), tessellated like the positions; a
    // patch without any keeps the planar projection below.
    const std::vector<std::array<double, 2>> stUv = tessellatePatchUVs(p, g.subdiv);
    const bool useSt = stUv.size() == m.points.size();
    if (m.points.empty() || m.indices.size() < 3 || m.normals.size() != m.points.size()) {
        g.warnings->push_back(where + ": patch skipped (invalid control grid)");
        return;
    }
    g.editorMats.insert(mat);

    // One projection axis per patch, from its averaged vertex normal; uv comes from position.
    Vec3 avg;
    for (const Vec3& nn : m.normals) avg += nn;
    const Vec3 pn = length(avg) > 1e-12 ? normalize(avg) : Vec3{0, 0, 1};
    const Axes ax = dominantAxes(pn);
    const TexProj tp{};

    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        uint32_t ia = m.indices[t], ib = m.indices[t + 1], ic = m.indices[t + 2];
        if (ia >= m.points.size() || ib >= m.points.size() || ic >= m.points.size()) continue;
        const Vec3 gn = cross(m.points[ib] - m.points[ia], m.points[ic] - m.points[ia]);
        if (length(gn) < 1e-12) continue;  // zero-area triangle
        const Vec3 navg = m.normals[ia] + m.normals[ib] + m.normals[ic];
        if (dot(gn, navg) < 0) std::swap(ib, ic);  // winding agrees with the vertex normals
        Chunk c;
        c.entity = entity;
        c.group = group;
        c.editorMaterial = mat;
        for (uint32_t i : {ia, ib, ic}) {
            Vec3 nn = normalize(m.normals[i]);
            if (length(nn) < 0.5) nn = normalize(gn);
            const Vec3& P = m.points[i];
            const Projected pr = projectPoint(P, nn, ax, tp);
            c.verts.push_back({P, nn, pr.tangent, useSt ? stUv[i][0] : pr.u, useSt ? stUv[i][1] : pr.v});
            if (colours.size() == m.points.size()) {
                c.verts.back().hasColor = true;
                c.verts.back().color = colours[i];
            }
            g.bounds.add(P);
        }
        if (useSt && c.verts.size() == 3) {
            const Vec3 e1 = c.verts[1].pos - c.verts[0].pos, e2 = c.verts[2].pos - c.verts[0].pos;
            const double du1 = c.verts[1].u - c.verts[0].u, dv1 = c.verts[1].v - c.verts[0].v;
            const double du2 = c.verts[2].u - c.verts[0].u, dv2 = c.verts[2].v - c.verts[0].v;
            const double det = du1 * dv2 - du2 * dv1;
            if (std::fabs(det) > 1e-12) {
                const Vec3 tan = (e1 * dv2 - e2 * dv1) * (1.0 / det);
                for (RenderVertex& rv : c.verts) {
                    const Vec3 t = tan - rv.normal * dot(tan, rv.normal);
                    if (length(t) > 1e-9) rv.tangent = normalize(t);
                }
            }
        }
        c.idx = {0, 1, 2};
        if (g.patchBackFaces) {
            // Owner playtest 2026-11-11: patches drew from one side only. The back face is the same triangle wound the
            // other way with its normals turned (worldspawn iw8r_patch_one_sided 1 turns this off).
            Chunk back = c;
            std::swap(back.verts[1], back.verts[2]);
            for (RenderVertex& rv : back.verts) rv.normal = rv.normal * -1.0;
            g.chunks.push_back(std::move(back));
        }
        g.chunks.push_back(std::move(c));
    }
}

// perBrush: one surface per brush and editor material. Otherwise one per entity and editor material.
// Either way a surface is closed before it would exceed 60000 vertices.
std::vector<RenderSurface> groupChunks(const std::vector<Chunk>& chunks, bool perBrush) {
    std::vector<RenderSurface> out;
    std::map<std::tuple<size_t, size_t, std::string>, size_t> current;
    for (const Chunk& c : chunks) {
        const auto key = std::make_tuple(c.entity, perBrush ? c.group : size_t(0), c.editorMaterial);
        auto it = current.find(key);
        if (it == current.end() || out[it->second].verts.size() + c.verts.size() > kMaxVerticesPerSurface) {
            RenderSurface s;
            s.editorMaterial = c.editorMaterial;
            out.push_back(std::move(s));
            current[key] = out.size() - 1;
        }
        RenderSurface& s = out[current[key]];
        const uint32_t base = uint32_t(s.verts.size());
        s.verts.insert(s.verts.end(), c.verts.begin(), c.verts.end());
        for (uint32_t i : c.idx) s.idx.push_back(base + i);
    }
    return out;
}

// The impact table the converter requires (iw3_fastfile.cpp:9714-9744, doc 17 §4.4): 12 rows of 29
// non-flesh and 4 flesh slots. Every slot is null, so no override is made and Replay's own stock impact
// effects stay in place: no IW3 data is involved.
std::string buildImpactTableJson() {
    std::string j = "{\"schema\":1,\"asset_type\":\"iw3_impact_fx\",\"layout\":{\"impact_count\":12,\"nonflesh_count\":29,"
                    "\"flesh_count\":4},\"entries\":[";
    for (int row = 0; row < 12; ++row) {
        if (row) j += ",";
        j += "{\"index\":" + std::to_string(row) + ",\"nonflesh\":[";
        for (int i = 0; i < 29; ++i) j += i ? ",null" : "null";
        j += "],\"flesh\":[null,null,null,null]}";
    }
    return j + "]}";
}

// ---------------------------------------------------------------- custom models (doc 22)

// xmodel names written by writeExtractionModel become file names; keep them to a safe set.
}  // namespace

std::string stockModelBuildName(const std::string& stockName) {
    std::string out = "iw8r_stock_";
    for (char c : stockName) {
        const char l = char(std::tolower(static_cast<unsigned char>(c)));
        out += (std::isalnum(static_cast<unsigned char>(l)) || l == '_' || l == '-') ? l : '_';
    }
    if (out.size() > 64) out.resize(64);
    return out;
}

namespace {

bool isCustomModelName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    for (char c : name)
        if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_' || c == '-')) return false;
    return true;
}

// First `<name>.obj`, else `<name>.glb`, else `<name>.gltf` (case-insensitive file name) under any root, recursively.
std::string findCustomModel(const std::vector<std::string>& roots, const std::string& name) {
  for (const char* ext : kCustomModelExtensions) {
    const std::string want = name + ext;
    for (const std::string& r : roots) {
        std::error_code ec;
        if (!fs::is_directory(r, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(r, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            std::string fn = it->path().filename().string();
            std::transform(fn.begin(), fn.end(), fn.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (fn == want) return it->path().string();
        }
    }
  }
    return {};
}

// ---------------------------------------------------------------- entity text (doc 15 §3)

// Classes the x64-zt converter accepts: its closed list isReplayEntity (x64-zt src/games/iw8/dump/maps.cpp:179-262,
// doc 15 §3.3). Anything else makes the build throw, so other classes are left out with a warning.
bool acceptedPointClass(const std::string& lcls) {
    static const std::set<std::string> known = {
        "worldspawn", "info_player_start", "mp_global_intermission", "mp_ctf_spawn", "mp_ctf_spawn_allies_start",
        "mp_ctf_spawn_axis_start", "mp_dm_spawn", "mp_dom_spawn", "mp_dom_spawn_allies_start", "mp_dom_spawn_axis_start",
        "mp_sd_spawn_attacker", "mp_sd_spawn_defender", "mp_tdm_spawn", "mp_tdm_spawn_allies_start", "mp_tdm_spawn_axis_start",
        "script_brushmodel", "script_model", "script_origin", "script_struct", "trigger_damage", "trigger_hurt",
        "trigger_multiple", "trigger_radius", "trigger_use", "trigger_use_touch",
        // shipped Replay map classes
        "mp_arena_spawn_allies_start", "mp_arena_spawn_axis_start", "mp_ctf_spawn_secondary", "mp_cyber_spawn_allies",
        "mp_cyber_spawn_axis", "mp_dd_spawn_attacker", "mp_dd_spawn_attacker_a", "mp_dd_spawn_attacker_b",
        "mp_dd_spawn_attacker_start", "mp_dd_spawn_defender", "mp_dd_spawn_defender_a", "mp_dd_spawn_defender_b",
        "mp_dd_spawn_defender_start", "mp_dm_spawn_secondary", "mp_dm_spawn_start", "mp_dom_spawn_secondary", "mp_koth_spawn",
        "mp_koth_spawn_allies_start", "mp_koth_spawn_axis_start", "mp_koth_spawn_secondary", "mp_rugby_spawn_allies",
        "mp_rugby_spawn_allies_secondary", "mp_rugby_spawn_allies_start", "mp_rugby_spawn_axis", "mp_rugby_spawn_axis_secondary",
        "mp_rugby_spawn_axis_start", "mp_tdm_spawn_secondary", "script_struct_heli", "triggermodeldummydefault",
        // spawns read by Replay's Uplink / Frontline scripts
        "mp_ball_spawn", "mp_ball_spawn_allies_start", "mp_ball_spawn_axis_start", "mp_ball_spawn_secondary",
        "mp_front_spawn_allies", "mp_front_spawn_axis",
        // source-only markers: x64-zt reads them (bot navigation pathnodes and traversal links, iw3_fastfile.cpp
        // ~12849-12867) and projects them out of the runtime entities without error (doc 15 section 3.3)
        "node_pathnode", "node_negotiation_begin", "node_negotiation_end", "fx_origin"};
    // script_struct_mp_*: the structs of the stock Warzone maps (vehicle spawns, spawn selection, ladder tops).
    return known.count(lcls) || startsWith(lcls, "trigger_multiple_bcs_") || isWarzoneStructClass(lcls);
}

// ---------------------------------------------------------------- reflection probes and static models (doc 29)

constexpr size_t kMaxReflectionProbes = 256;  // doc 15 §2.1: reflection_probes is 1..256

// AnglesToAxis of x64-zt (iw3_fastfile.cpp:11658-11666; degrees, pitch yaw roll). The rows are forward, left and up,
// which is the order world.json models[].axis takes (iw3_fastfile.cpp:3822-3825).
std::array<Vec3, 3> anglesToAxis(double pitchDeg, double yawDeg, double rollDeg) {
    const double d = kPi / 180.0;
    const double sp = std::sin(pitchDeg * d), cp = std::cos(pitchDeg * d);
    const double sy = std::sin(yawDeg * d), cy = std::cos(yawDeg * d);
    const double sr = std::sin(rollDeg * d), cr = std::cos(rollDeg * d);
    return {Vec3{cp * cy, cp * sy, -sp}, Vec3{sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, sr * cp},
            Vec3{cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp}};
}

}  // namespace

std::string findCustomModelFile(const std::vector<std::string>& roots, const std::string& name) { return findCustomModel(roots, name); }

std::array<double, 4> anglesToDynQuaternion(double pitchDeg, double yawDeg, double rollDeg) {
    // The basis rows (forward, left, up) are the columns of the rotation matrix R of the quaternion.
    const auto ax = anglesToAxis(pitchDeg, yawDeg, rollDeg);
    const double m00 = ax[0].x, m10 = ax[0].y, m20 = ax[0].z;
    const double m01 = ax[1].x, m11 = ax[1].y, m21 = ax[1].z;
    const double m02 = ax[2].x, m12 = ax[2].y, m22 = ax[2].z;
    double x, y, z, w;
    const double tr = m00 + m11 + m22;
    if (tr > 0) {
        const double s = std::sqrt(tr + 1.0) * 2.0;
        w = 0.25 * s, x = (m21 - m12) / s, y = (m02 - m20) / s, z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
        w = (m21 - m12) / s, x = 0.25 * s, y = (m01 + m10) / s, z = (m02 + m20) / s;
    } else if (m11 > m22) {
        const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
        w = (m02 - m20) / s, x = (m01 + m10) / s, y = 0.25 * s, z = (m12 + m21) / s;
    } else {
        const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
        w = (m10 - m01) / s, x = (m02 + m20) / s, y = (m12 + m21) / s, z = 0.25 * s;
    }
    const double n = std::sqrt(x * x + y * y + z * z + w * w);
    return {x / n, y / n, z / n, w / n};
}

namespace {

// Index of the probe whose origin is nearest to p (squared distance; the first probe wins a tie).
size_t nearestProbe(const std::vector<Vec3>& probes, const Vec3& p) {
    size_t best = 0;
    double bestD = 0;
    for (size_t i = 0; i < probes.size(); ++i) {
        const double dx = probes[i].x - p.x, dy = probes[i].y - p.y, dz = probes[i].z - p.z;
        const double d = dx * dx + dy * dy + dz * dz;
        if (i == 0 || d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

// Centre of a surface's vertex bounds: the point its reflection probe is picked by.
Vec3 surfaceCentre(const RenderSurface& s) {
    AABB b;
    for (const auto& v : s.verts) b.add(v.pos);
    return b.valid() ? b.center() : Vec3{};
}

using KeyValues = std::vector<std::pair<std::string, std::string>>;

void appendEntityText(std::string& out, const KeyValues& kvs) {
    out += "{\n";
    for (const auto& [k, v] : kvs) out += "\"" + k + "\" \"" + v + "\"\n";
    out += "}\n";
}

// ---------------------------------------------------------------- primary lights (doc 24)

// Replay's proxy slot count (x64-zt convert.cpp sortedLightLimit, doc 24 section 2): index 0 empty, index 1 the sun,
// then one slot per light entity. The converter pads a table below 256 and indexes proxies by sorted rank.
constexpr size_t kMaxPrimaryLights = 256;
constexpr size_t kFirstLocalLight = 2;
// IW3 Radiant's light template (reference/iw3xo-radiant/src/ggui/entity.cpp eclass_light_templates): radius 400,
// fov_outer 90, fov_inner 60 (full cone angles, degrees).
constexpr double kDefaultLightRadius = 400.0;
constexpr double kDefaultFovOuter = 90.0;
constexpr double kDefaultFovInner = 60.0;

bool isLightClass(const std::string& lcls) { return lcls == "light" || lcls == "light_spot" || lcls == "light_omni"; }

// Optional light keys (5.14) and the ComPrimaryLight field each one writes. An absent key writes nothing, so a light
// without them is unchanged. The ranges are the converter's own checks (x64-zt convert.cpp:983-1085): a value it would
// throw on is warned and left out here, so a light key never stops the build.
//   Finite / Positive / NonNegative: one number, checked for finite, > 0 or >= 0.
//   Pair: two numbers, "near far", written as a JSON array.
//   Switch: 0, 1, true or false. 1 sets can_use_shadow_map (spot lights only; see localPrimaryLights).
enum class LightKind { Finite, Positive, NonNegative, Pair, Switch };
struct LightKey {
    const char* key;    // Radiant key
    const char* field;  // ComPrimaryLight JSON field (convert.cpp reads each one by this name)
    LightKind kind;
    const char* rule;   // wording of the warning
};
constexpr LightKey kLightKeys[] = {
    {"iw8r_bulb_radius", "bulb_radius", LightKind::Positive, "must be a number above 0"},
    {"iw8r_shadow_bias", "shadow_bias", LightKind::Finite, "must be a number"},
    {"iw8r_falloff", "distance_falloff", LightKind::Positive, "must be a number above 0"},
    {"iw8r_fade", "fade_offset", LightKind::Pair, "must be two numbers: near far"},
    {"iw8r_shadows", "can_use_shadow_map", LightKind::Switch, "must be 0, 1, true or false"},
    {"iw8r_intensity_units", "intensity", LightKind::NonNegative, "must be a number of 0 or more"},
    {"iw8r_uv_intensity", "uv_intensity", LightKind::Finite, "must be a number"},
    {"iw8r_ir_intensity", "ir_intensity", LightKind::Finite, "must be a number"},
};

// One ComPrimaryLight object (doc 24 section 3). `direction` points back toward the light, as the sun's does.
// `extraFields` is a run of `,"field":value` pairs from the optional light keys (5.14); empty when none is set.
std::string primaryLightJson(int type, const double color[3], const Vec3& direction, const Vec3& origin, double radius,
                             double cosOuter, double cosInner, const std::string& definition = "",
                             bool canUseShadowMap = false, const std::string& extraFields = "") {
    return "{\"type\":" + std::to_string(type) + ",\"exponent\":0,\"can_use_shadow_map\":" +
           std::string(canUseShadowMap ? "true" : "false") + ",\"color\":" +
           vec3raw(color[0], color[1], color[2]) + ",\"direction\":" + vec3(direction) + ",\"origin\":" + vec3(origin) +
           ",\"radius\":" + num(radius) + ",\"cos_half_fov_outer\":" + num(cosOuter) +
           ",\"cos_half_fov_inner\":" + num(cosInner) + ",\"rotation_limit\":0,\"translation_limit\":0,\"definition\":\"" +
           (!definition.empty() ? definition : type >= 2 ? std::string("light_point_linear") : std::string()) + "\"" +
           extraFields + "}";
}

// The light entities as ComPrimaryLight objects, in map order (doc 24). A light is a spot (type 2) when it is
// `light_spot`, or a `light` with a `target` naming an entity or with an fov key; otherwise an omni (type 3).
// Keys: `_color` (or `color`) 0..1 or 0..255, `intensity` multiplies the colour (default 1), `radius`, `target`
// (aim at that entity's origin), `angles` (pitch positive down, yaw, roll; aims a spot without a target),
// `fov_outer` and `fov_inner` (full cone angles). Without `iw8r_intensity_units` the intensity is not written, so the
// converter calibrates each light from its radius (convert.cpp calibrateLocalLights). With it, the light is authored:
// the converter uses that value (times the light-unit scale) and skips the radius calibration.
// Light keys (5.14, kLightKeys): bulb radius, shadow bias, falloff, fade, shadow map (spots), units intensity, UV and IR.
// IES (5.13): a spot with `iw8r_ies` (an LM-63 .ies file, absolute or relative to the map folder) gets its own light
// definition `iw8r_ies_<stem>` whose profile is written to the comworld `ies_profiles` (x64-zt registers the LightDef,
// replay_ies.cpp). `iesProfiles` receives one JSON object per distinct file.
std::vector<std::string> localPrimaryLights(const MapFile& map, const std::vector<const Entity*>& points,
                                            std::vector<std::string>& warn, const std::string& mapDir,
                                            std::vector<std::string>& iesProfiles) {
    std::map<std::string, std::string> iesDefs;  // resolved file -> definition name ("" = unusable)
    std::map<std::string, Vec3> targets;  // targetname -> origin of the first entity that has it
    for (const Entity& e : map.entities) {
        const std::string* tn = e.value("targetname");
        const auto o = e.origin();
        if (tn && o && !tn->empty()) targets.emplace(*tn, *o);
    }
    std::vector<std::string> out;
    size_t dropped = 0;
    for (const Entity* pe : points) {
        const std::string lcls = lower(pe->classname());
        if (!isLightClass(lcls)) continue;
        const std::string where = "light entity " + std::to_string(pe - map.entities.data());
        if (out.size() + kFirstLocalLight >= kMaxPrimaryLights) {
            ++dropped;
            continue;
        }
        const Vec3 origin = pe->origin().value_or(Vec3{});

        double color[3] = {1, 1, 1};
        const std::string* cs = pe->value("_color");
        if (!cs) cs = pe->value("color");
        if (cs) {
            double v[3];
            if (parseVec3Text(*cs, v)) {
                if (std::max({v[0], v[1], v[2]}) > 1.0)
                    for (double& c : v) c /= 255.0;
                for (int k = 0; k < 3; ++k) color[k] = std::max(0.0, v[k]);
            } else {
                warn.push_back(where + ": colour '" + *cs + "' not parsed: using 1 1 1");
            }
        }
        double intensity = 1.0;
        if (const std::string* is = pe->value("intensity")) {
            double v = 0;
            if (parseNumber(*is, v) && v >= 0) intensity = v;
            else warn.push_back(where + ": intensity '" + *is + "' not parsed: using 1");
        }
        for (double& c : color) c *= intensity;

        double radius = kDefaultLightRadius;
        if (const std::string* rs = pe->value("radius")) {
            double v = 0;
            if (parseNumber(*rs, v) && v > 0) radius = v;
            else warn.push_back(where + ": radius '" + *rs + "' not positive: using 400");
        }

        // Aim at the target entity when it exists; otherwise the angles.
        std::optional<Vec3> aimAt;
        if (const std::string* tk = pe->value("target"); tk && !tk->empty()) {
            const auto found = targets.find(*tk);
            if (found != targets.end()) aimAt = found->second - origin;
            else warn.push_back(where + ": target '" + *tk + "' not found: aimed from angles");
        }
        const bool hasFov = pe->value("fov_outer") || pe->value("fov_inner");
        bool spot = lcls == "light_spot" || (lcls == "light" && (aimAt || hasFov));
        // Owner playtest 2026-11-11: a spot widened to 154 degrees did not show in game, while omni lights do. A cone of
        // 150 degrees or more is close to an omni light anyway, so it is built as one (with a note).
        if (spot)
            if (const std::string* fo = pe->value("fov_outer")) {
                double v = 0;
                if (parseNumber(*fo, v) && v >= 150.0) {
                    spot = false;
                    warn.push_back(where + ": fov_outer " + *fo + " is 150 degrees or more: built as an omni light (a spot that wide did not show in game)");
                }
            }

        Vec3 emission{1, 0, 0};  // unit emission axis of a spot
        double cosOuter = 0, cosInner = 0;
        if (spot) {
            if (aimAt && std::sqrt(aimAt->x * aimAt->x + aimAt->y * aimAt->y + aimAt->z * aimAt->z) > 1e-6) {
                emission = normalize(*aimAt);
            } else {
                double a[3] = {0, 0, 0};
                if (const std::string* as = pe->value("angles"); as && !parseVec3Text(*as, a))
                    warn.push_back(where + ": angles '" + *as + "' not parsed: aimed along +X");
                const double p = a[0] * kPi / 180.0, y = a[1] * kPi / 180.0;
                emission = {std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), -std::sin(p)};
            }
            double outer = kDefaultFovOuter, inner = kDefaultFovInner;
            if (const std::string* fo = pe->value("fov_outer")) {
                double v = 0;
                if (parseNumber(*fo, v) && v > 0) outer = v;
                else warn.push_back(where + ": fov_outer '" + *fo + "' not positive: using 90");
            }
            if (const std::string* fi = pe->value("fov_inner")) {
                double v = 0;
                if (parseNumber(*fi, v) && v >= 0) inner = v;
                else warn.push_back(where + ": fov_inner '" + *fi + "' not valid: using 60");
            }
            // The converter needs 0 < cos outer < cos inner <= 1, so the outer cone is 1..170 degrees and inner < outer.
            outer = std::clamp(outer, 1.0, 170.0);
            if (inner >= outer) {
                warn.push_back(where + ": fov_inner not below fov_outer: inner set to half of outer");
                inner = outer * 0.5;
            }
            cosOuter = std::cos(outer * 0.5 * kPi / 180.0);
            cosInner = std::cos(inner * 0.5 * kPi / 180.0);
        }
        const int type = spot ? 2 : 3;
        const Vec3 direction = spot ? -emission : Vec3{0, 0, 1};  // an omni's axis is unused; the converter wants non-zero
        std::string definition;
        if (const std::string* ies = pe->value("iw8r_ies"); ies && !ies->empty()) {
            fs::path p(*ies);
            if (p.is_relative() && !mapDir.empty()) p = fs::path(mapDir) / p;
            const std::string key = p.lexically_normal().string();
            auto found = iesDefs.find(key);
            if (found == iesDefs.end()) {
                std::string text, ierr;
                IesProfile prof;
                std::string def;
                if (!readFile(key, text)) {
                    warn.push_back(where + ": iw8r_ies '" + *ies + "' cannot be read; plain spot used");
                } else if (!parseIesFile(text, prof, &ierr)) {
                    warn.push_back(where + ": iw8r_ies '" + *ies + "': " + ierr + "; plain spot used");
                } else {
                    std::string stem;
                    for (const char c : p.stem().string()) stem += std::isalnum(static_cast<unsigned char>(c)) ? char(std::tolower(c)) : '_';
                    def = "iw8r_ies_" + stem.substr(0, 40) + "_" + std::to_string(iesDefs.size());
                    std::string texels;
                    for (const int t : iesReplayTexels(prof)) texels += (texels.empty() ? "" : ",") + std::to_string(t);
                    iesProfiles.push_back("{\"definition\":" + quote(def) + ",\"profile\":" + quote(def) + ",\"texels\":[" + texels + "]}");
                }
                found = iesDefs.emplace(key, def).first;
            }
            if (!spot && !found->second.empty())
                warn.push_back(where + ": iw8r_ies applies to spot lights; this omni light ignores it");
            else
                definition = found->second;
        }
        // Optional light keys (5.14, kLightKeys). A valid value adds its field; an invalid one is warned and left out.
        std::string extra;
        bool canShadow = false;
        for (const LightKey& lk : kLightKeys) {
            const std::string* s = pe->value(lk.key);
            if (!s) continue;
            if (lk.kind == LightKind::Switch) {
                const std::string t = lower(*s);
                if (t != "0" && t != "1" && t != "true" && t != "false") {
                    warn.push_back(where + ": " + lk.key + " '" + *s + "' " + lk.rule + ": left out");
                } else if (t == "1" || t == "true") {
                    if (spot) canShadow = true;
                    else warn.push_back(where + ": " + lk.key + " applies to spot lights; this omni light ignores it");
                }
                continue;
            }
            std::string json;
            if (lk.kind == LightKind::Pair) {
                const auto tok = splitTokens(*s);
                double a = 0, b = 0;
                if (tok.size() == 2 && parseNumber(tok[0], a) && parseNumber(tok[1], b))
                    json = "[" + num(a) + "," + num(b) + "]";
            } else {
                double v = 0;
                // num() writes |v| < 1e-9 as 0, so a "positive" value must stay at or above 1e-9 to remain positive.
                const bool ok = parseNumber(*s, v) && (lk.kind == LightKind::Finite || (lk.kind == LightKind::Positive ? v >= 1e-9 : v >= 0));
                if (ok) json = num(v);
            }
            if (json.empty()) warn.push_back(where + ": " + lk.key + " '" + *s + "' " + lk.rule + ": left out");
            else extra += std::string(",\"") + lk.field + "\":" + json;
        }
        out.push_back(primaryLightJson(type, color, direction, origin, radius, cosOuter, cosInner, definition, canShadow, extra));
    }
    if (dropped)
        warn.push_back(std::to_string(dropped) + " light entit" + (dropped == 1 ? "y" : "ies") +
                       " beyond the " + std::to_string(kMaxPrimaryLights - kFirstLocalLight) +
                       " local primary-light slots (doc 24) left out");
    return out;
}

// ---------------------------------------------------------------- output folder

// Writable when missing, empty, or a previous extraction: a `complete` file and nothing but
// complete / skeleton / extracted at the top level.
bool checkOutputFolder(const fs::path& dir, std::string& err) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) return true;
    if (!fs::is_directory(dir, ec)) {
        err = "output path is not a folder: " + dir.string();
        return false;
    }
    if (fs::is_empty(dir, ec)) return true;
    if (!fs::is_regular_file(dir / "complete", ec)) {
        err = "refusing to write into non-empty folder " + dir.string() + " (no 'complete' marker, so not a previous extraction)";
        return false;
    }
    for (fs::directory_iterator it(dir, ec), last; !ec && it != last; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name != "complete" && name != "skeleton" && name != "extracted") {
            err = "refusing to write into " + dir.string() + ": it holds '" + name + "', which is not part of an extraction";
            return false;
        }
    }
    if (ec) {
        err = "cannot read " + dir.string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool writeFile(const fs::path& p, const std::string& data, std::string& err) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    if (ec) {
        err = "cannot create " + p.parent_path().string() + ": " + ec.message();
        return false;
    }
    // Incremental builds (7.10): an unchanged file is left alone (no write, same timestamp).
    if (fs::is_regular_file(p, ec) && fs::file_size(p, ec) == data.size()) {
        std::string old;
        if (readFile(p.string(), old) && old == data) return true;
    }
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) {
        err = "cannot write " + p.string();
        return false;
    }
    f.write(data.data(), std::streamsize(data.size()));
    if (!f) {
        err = "write failed: " + p.string();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- DDS probe (doc 15 §7)

void put32(std::string& s, uint32_t v) {
    for (int i = 0; i < 4; ++i) s.push_back(char((v >> (8 * i)) & 0xFF));
}

// 64x64 cube map, B8G8R8A8, 7 mips: neutral grey (128) with opaque alpha, or the six captured RGBA faces (each
// 64*64*4 bytes, mips by 2x2 box filter). 128 + 6 * 21844 bytes.
std::string buildProbeDds(const std::vector<uint8_t>* faces = nullptr) {
    std::string out = "DDS ";
    put32(out, 124);                                       // dwSize
    put32(out, 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000);  // CAPS HEIGHT WIDTH PITCH PIXELFORMAT MIPMAPCOUNT
    put32(out, kProbeSize);                                // dwHeight
    put32(out, kProbeSize);                                // dwWidth
    put32(out, uint32_t(kProbeSize * 4));                  // dwPitchOrLinearSize (bytes per row)
    put32(out, 0);                                         // dwDepth
    put32(out, kProbeMips);                                // dwMipMapCount
    for (int i = 0; i < 11; ++i) put32(out, 0);            // dwReserved1
    put32(out, 32);                                        // DDS_PIXELFORMAT size
    put32(out, 0x41);                                      // DDPF_RGB | DDPF_ALPHAPIXELS
    put32(out, 0);                                         // fourCC
    put32(out, 32);                                        // RGB bit count
    put32(out, 0x00FF0000);                                // R mask
    put32(out, 0x0000FF00);                                // G mask
    put32(out, 0x000000FF);                                // B mask
    put32(out, 0xFF000000);                                // A mask
    put32(out, 0x8 | 0x1000 | 0x400000);                   // DDSCAPS_COMPLEX | TEXTURE | MIPMAP
    put32(out, 0xFE00);                                    // DDSCAPS2_CUBEMAP | all six faces
    put32(out, 0);                                         // dwCaps3
    put32(out, 0);                                         // dwCaps4
    put32(out, 0);                                         // dwReserved2
    const bool captured = faces && faces->size() == size_t(6 * kProbeSize * kProbeSize * 4);
    for (int face = 0; face < 6; ++face) {
        std::vector<uint8_t> level;
        if (captured) level.assign(faces->begin() + face * kProbeSize * kProbeSize * 4, faces->begin() + (face + 1) * kProbeSize * kProbeSize * 4);
        for (int mip = 0; mip < kProbeMips; ++mip) {
            const int size = std::max(1, kProbeSize >> mip);
            for (int px = 0; px < size * size; ++px) {
                if (captured) {
                    out.push_back(char(level[size_t(px) * 4 + 2]));  // B
                    out.push_back(char(level[size_t(px) * 4 + 1]));  // G
                    out.push_back(char(level[size_t(px) * 4 + 0]));  // R
                    out.push_back(char(0xFF));                      // A
                } else {
                    out.push_back(char(0x80));
                    out.push_back(char(0x80));
                    out.push_back(char(0x80));
                    out.push_back(char(0xFF));
                }
            }
            if (captured && size > 1) {  // next mip: 2x2 box filter
                const int half = size / 2;
                std::vector<uint8_t> next(size_t(half) * size_t(half) * 4);
                for (int y = 0; y < half; ++y)
                    for (int x = 0; x < half; ++x)
                        for (int c = 0; c < 4; ++c) {
                            const auto at = [&](int xx, int yy) { return int(level[(size_t(yy) * size_t(size) + size_t(xx)) * 4 + size_t(c)]); };
                            next[(size_t(y) * size_t(half) + size_t(x)) * 4 + size_t(c)] =
                                uint8_t((at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4);
                        }
                level = std::move(next);
            }
        }
    }
    return out;
}

// Stock-free grey material (doc 15 §4.2): techniqueSet plus two built-in textures.
std::string buildMaterialJson(const std::string& greyMaterial) {
    return "{\"techniqueSet\":" + quote(techniqueSetFor(greyMaterial)) +
           ",\"textures\":[{\"name\":\"colorMap\",\"semantic\":\"colorMap\",\"image\":\"$white\"},"
           "{\"name\":\"normalMap\",\"semantic\":\"normalMap\",\"image\":\"$identitynormalmap\"}]}";
}

// Image file stem of a custom material (doc 21 section 6): iw8rm_<name with non-alphanumerics as _>_<hash of name>.
// The hash keeps names that differ only by punctuation apart.
std::string customImageStem(const std::string& name) {
    std::string out = "iw8rm_";
    for (const char c : name) out += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    return out + "_" + customNameHash(name);
}

// Material JSON of a custom material (doc 21 section 6). The techniqueSet tag carries no underscore before a tag-like
// token (the hash), so ReadMaterial's name regex does not pick up alpha test or blend from it; "_a1" is added only
// for alpha test. Two-sided adds a state with cull none.
std::string customMaterialJson(const CustomMaterial& cm, const std::string& outName) {
    const std::string stem = customImageStem(outName);
    // The converter reads the blend mode from the techset name: "_b<n>" blended (x64-zt iw3_render_assets.cpp, the
    // (^|_)b[0-9] rule), "_a<n>" alpha tested.
    // Shader state (roadmap 12.2, research 68 section 7). Additive and multiply are read from the colour pass's state,
    // which the converter finds through stateBitsEntry (technique 7 = lit, 4 = unlit); "unlit" in the name takes no
    // light; colorTint and emissionScale are constants; sortKey above 8 layers decals; polygonOffset biases the depth.
    const bool stateBlend = cm.blendMode == 1 || cm.blendMode == 2;
    std::string json = "{\"techniqueSet\":" +
                       quote("iw8rcm" + customNameHash(outName) + (cm.unlit ? "_unlit" : "") +
                             (stateBlend ? "" : cm.blend ? "_b0" : cm.alphaTest ? "_a1" : "")) +
                       ",\"surfaceTypeBits\":" + std::to_string(iw3SurfaceTypeBits(cm.surfaceType));
    if (cm.twoSided || stateBlend || cm.polygonOffset > 0) {
        std::string st = "{\"colorWriteRgb\":true";
        if (cm.twoSided) st += ",\"cullFace\":\"none\"";
        if (cm.blendMode == 1) st += ",\"srcBlendRgb\":\"one\",\"dstBlendRgb\":\"one\",\"depthWrite\":false";
        if (cm.blendMode == 2) st += ",\"srcBlendRgb\":\"zero\",\"dstBlendRgb\":\"srccolor\",\"depthWrite\":false";
        if (cm.polygonOffset > 0) st += ",\"polygonOffset\":\"offset" + std::to_string(std::min(cm.polygonOffset, 2)) + "\"";
        json += ",\"stateBits\":[" + st + "}]";
        if (stateBlend || cm.polygonOffset > 0) {
            // 21 technique slots; the colour pass (7 lit, or 4 when unlit) names state 0.
            std::string entries;
            for (int t = 0; t < 21; ++t)
                entries += std::string(t ? "," : "") + (t == (cm.unlit ? 4 : 7) ? "0" : "-1");
            json += ",\"stateBitsEntry\":[" + entries + "]";
        }
    }
    if (cm.sortKey > 0) json += ",\"sortKey\":" + std::to_string(cm.sortKey);
    if (cm.hasTint() || cm.emission > 1) {
        std::string c;
        if (cm.hasTint())
            c += "{\"name\":\"colorTint\",\"literal\":[" + num(cm.tint[0]) + "," + num(cm.tint[1]) + "," + num(cm.tint[2]) + ",1]}";
        if (cm.emission > 1)
            c += std::string(c.empty() ? "" : ",") + "{\"name\":\"emissionScale\",\"literal\":[" + num(cm.emission) + ",0,0,0]}";
        json += ",\"constants\":[" + c + "]";
    }
    json += ",\"textures\":[{\"name\":\"colorMap\",\"semantic\":\"colorMap\",\"image\":" + quote(stem + "_c") + "}";
    if (!cm.normalImage.empty())
        json += ",{\"name\":\"normalMap\",\"semantic\":\"normalMap\",\"image\":" + quote(stem + "_n") + "}";
    if (!cm.specImage.empty())
        json += ",{\"name\":\"specularMap\",\"semantic\":\"specularMap\",\"image\":" + quote(stem + "_s") + "}";
    return json + "]}";
}

// Material JSON of a custom material used by a decal volume (5.8, research 82 section "How material is resolved").
// The converter's decal producer only takes the effect-decal form: metadata iw3 / material / 1, a technique set named
// [mw]c_l_sm_b0c0(n0)(s0), a 1 x 1 atlas, camera region "decal", state flags 52, one to three textures and a colorTint.
// It is written under its own name (<material>_dcl), so the same library material can also be painted on a face.
std::string customDecalMaterialJson(const CustomMaterial& cm, const std::string& outName) {
    const std::string stem = customImageStem(outName);
    std::string json = "{\"_game\":\"iw3\",\"_type\":\"material\",\"_version\":1,\"techniqueSet\":" +
                       quote(std::string("wc_l_sm_b0c0") + (cm.normalImage.empty() ? "" : "n0") + (cm.specImage.empty() ? "" : "s0")) +
                       ",\"textureAtlas\":{\"rows\":1,\"columns\":1},\"stateFlags\":52,\"cameraRegion\":\"decal\"";
    const double r = cm.hasTint() ? cm.tint[0] : 1, g = cm.hasTint() ? cm.tint[1] : 1, b = cm.hasTint() ? cm.tint[2] : 1;
    json += ",\"constants\":[{\"name\":\"colorTint\",\"literal\":[" + num(r) + "," + num(g) + "," + num(b) + ",1]}]";
    json += ",\"textures\":[{\"name\":\"colorMap\",\"semantic\":\"colorMap\",\"image\":" + quote(stem + "_c") + "}";
    if (!cm.normalImage.empty())
        json += ",{\"name\":\"normalMap\",\"semantic\":\"normalMap\",\"image\":" + quote(stem + "_n") + "}";
    if (!cm.specImage.empty())
        json += ",{\"name\":\"specularMap\",\"semantic\":\"specularMap\",\"image\":" + quote(stem + "_s") + "}";
    return json + "]}";
}

}  // namespace

bool isValidIw8MapId(const std::string& id) {
    // doc 03: ^mp_[a-z0-9_]{1,12}$ and at most 15 characters.
    if (id.size() < 4 || id.size() > 15 || !startsWith(id, "mp_")) return false;
    for (size_t i = 3; i < id.size(); ++i) {
        const char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

bool isIw8ToolMaterial(const std::string& material) {
    static const char* const kTool[] = {"caulk", "clip", "nodraw", "trigger", "hint", "skip", "portal", "origin", "sky"};
    const std::string m = lower(material);
    for (const char* t : kTool)
        if (m.find(t) != std::string::npos) return true;
    // The ladder tool texture is a climbable volume, never drawn (stock maps put it on an invisible brush); matched
    // exactly so stock surfaces such as "metal_ladder_01" still draw.
    const size_t slash = m.find_last_of('/');
    return (slash == std::string::npos ? m : m.substr(slash + 1)) == "ladder";
}

bool compileIw8ExtractionFlat(const MapFile& map, const std::string& outDir, const CompileOptions& opt, CompileReport& rep,
                              std::string* err);

// A model entity with iw8r_material "<material>" (owner request 2026-11-11: materials on models) is built from a copy
// of its stock model whose every surface uses that material: the copy gets its own name and the entity is pointed at
// it. Only models the editor exported (opt.stockModels) can be re-skinned; others keep their materials, with a warning.
bool compileIw8ExtractionVariants(const MapFile& mapIn, const std::string& outDir, const CompileOptions& opt, CompileReport& rep,
                                  std::string* err) {
    // 12.7: one slot can be replaced too: key "iw8r_mat:<material of the model>" "<replacement>". Slot keys win over
    // iw8r_material for their slot.
    const auto slotsOf = [](const Entity& e) {
        std::map<std::string, std::string> slots;  // lower-case model material -> replacement
        for (const auto& it : e.items)
            if (const KeyValueLine* kv = it.kv()) {
                const std::string k = kv->name();
                if (k.rfind("iw8r_mat:", 0) == 0 && k.size() > 9 && !kv->value.empty()) slots[lower(k.substr(9))] = kv->value;
            }
        return slots;
    };
    const auto isModel = [](const Entity& e) {
        const std::string c = lower(e.classname());
        return c == "misc_model" || c == "script_model" || c == "dynamic_prop";
    };
    static const std::string kNone;
    const auto wanted = [&](const Entity& e) -> const std::string* {
        if (!isModel(e)) return nullptr;
        const std::string* m = e.value("iw8r_material");
        if (m && !m->empty()) return m;
        return slotsOf(e).empty() ? nullptr : &kNone;
    };
    bool any = false;
    for (const auto& e : mapIn.entities) any |= wanted(e) != nullptr;
    if (!any) return compileIw8ExtractionFlat(mapIn, outDir, opt, rep, err);
    MapFile work = mapIn;
    std::map<std::string, ImportedModel> models;
    if (opt.stockModels) models = *opt.stockModels;
    std::map<std::string, CompileOptions::CustomModelVariant> customVariants;
    if (opt.customModelVariants) customVariants = *opt.customModelVariants;
    size_t skinned = 0;
    std::vector<std::string> notes;
    for (auto& e : work.entities) {
        const std::string* mat = wanted(e);
        if (!mat) continue;
        const std::string material = *mat;
        const std::string* mv = e.value("model");
        if (!mv || mv->empty()) continue;
        const std::string base = *mv;
        const auto found = opt.stockModels ? opt.stockModels->find(base) : models.end();
        const auto slots = slotsOf(e);
        std::string sig = material;
        for (const auto& [from, to] : slots) sig += "|" + from + ">" + to;
        if (!opt.stockModels || found == opt.stockModels->end() || found->second.meshes.empty()) {
            // The mapper's own model (a shape model or an imported file, owner report 2026-11-11: a staircase of
            // shape cubes): a copy read from the same file, with the materials replaced.
            const std::string hash = customNameHash(sig);
            std::string variant = base + "_m" + hash;
            if (variant.size() > 64) variant = base.substr(0, 64 - 2 - hash.size()) + "_m" + hash;
            if (!isCustomModelName(base) || findCustomModel(opt.modelRoots, base).empty() || !isCustomModelName(variant)) {
                notes.push_back("model '" + base + "' iw8r_material '" + material +
                                "': not a stock model from the model cache and not a model in the model folders; it keeps its own");
                continue;
            }
            customVariants[variant] = CompileOptions::CustomModelVariant{base, material, slots};
            for (auto& it : e.items)
                if (KeyValueLine* kv = it.kv(); kv && kv->name() == "model") kv->set(variant);
            ++skinned;
            continue;
        }
        const std::string variant = base + "_iw8rmat_" + customNameHash(sig);
        if (!models.count(variant)) {
            ImportedModel copy = found->second;
            copy.name = variant;
            for (auto& mesh : copy.meshes) {
                if (const auto s = slots.find(lower(mesh.material)); s != slots.end()) mesh.material = s->second;
                else if (!material.empty()) mesh.material = material;
            }
            models.emplace(variant, std::move(copy));
        }
        for (auto& it : e.items)
            if (KeyValueLine* kv = it.kv(); kv && kv->name() == "model") kv->set(variant);
        ++skinned;
    }
    CompileOptions opt2 = opt;
    if (skinned && !models.empty()) opt2.stockModels = &models;
    if (!customVariants.empty()) opt2.customModelVariants = &customVariants;
    const bool ok = compileIw8ExtractionFlat(work, outDir, opt2, rep, err);
    if (skinned)
        rep.warnings.insert(rep.warnings.begin(), std::to_string(skinned) + " model(s) built with their iw8r_material on every surface");
    rep.warnings.insert(rep.warnings.begin(), notes.begin(), notes.end());
    return ok;
}

// Moving brush models (owner playtest 2, 2026-11-11: a script_brushmodel door was invisible in game, although its
// surfaces were in the build; scripted models do draw and move). Each script_brushmodel that is not a glass pane gets
// a generated model of its faces (textured like the brushes), placed as a live script_model "<name>_vis" that the map
// script links to the brush model. The brush model keeps its collision and its mover; its own surfaces are left out.
bool compileIw8ExtractionBrushVisuals(const MapFile& mapIn, const std::string& outDir, const CompileOptions& opt, CompileReport& rep,
                                      std::string* err) {
    const auto isGlass = [&](const Entity& e) {
        for (const auto& it : e.items) {
            const Primitive* p = it.prim();
            if (!p) continue;
            for (const auto& f : p->faces) {
                const std::string m = f.material();
                if (lower(m) == "iw8r_glass") return true;
                if (opt.library)
                    if (const CustomMaterial* cm = opt.library->find(m); cm && cm->blend) return true;
            }
        }
        return false;
    };
    const auto wanted = [&](const Entity& e) {
        if (lower(e.classname()) != "script_brushmodel" || isGlass(e)) return false;
        if (const std::string* off = e.value("iw8r_brush_visual"); off && *off == "0") return false;
        for (const auto& it : e.items)
            if (it.prim()) return true;
        return false;
    };
    bool any = false;
    for (const auto& e : mapIn.entities) any |= wanted(e);
    if (!any) return compileIw8ExtractionVariants(mapIn, outDir, opt, rep, err);

    MapFile work = mapIn;
    std::map<std::string, ImportedModel> models;
    if (opt.stockModels) models = *opt.stockModels;
    std::set<std::string> names;
    for (const auto& e : work.entities)
        if (const std::string* t = e.value("targetname")) names.insert(*t);
    std::vector<std::string> warn;
    std::string added;
    size_t made = 0, n = 0;
    const size_t count = work.entities.size();
    for (size_t ei = 0; ei < count; ++ei) {
        Entity& e = work.entities[ei];
        if (!wanted(e)) continue;
        Gather g;
        g.subdiv = std::clamp(opt.patchSubdiv, 1, 32);
        g.warnings = &warn;
        g.library = opt.library;
        size_t group = 0;
        for (const auto& it : e.items) {
            const Primitive* p = it.prim();
            if (!p) continue;
            if (p->kind == PrimKind::Brush) gatherBrush(*p, ei, group++, g);
            else if (p->kind == PrimKind::Patch) gatherPatch(*p, ei, group++, g);
        }
        AABB box;
        for (const Chunk& c : g.chunks)
            for (const RenderVertex& v : c.verts) box.add(v.pos);
        if (g.chunks.empty() || !box.valid()) continue;
        const Vec3 centre = box.center();
        // The model writer turns a Y-up file into Z-up with (x, -z, y), so the file holds (x, z, -y).
        const auto toFile = [](const Vec3& v) { return Vec3{v.x, v.z, -v.y}; };
        ImportedModel im;
        std::map<std::string, size_t> meshOf;
        for (const Chunk& c : g.chunks) {
            auto found = meshOf.find(c.editorMaterial);
            if (found == meshOf.end()) {
                found = meshOf.emplace(c.editorMaterial, im.meshes.size()).first;
                im.meshes.emplace_back();
                im.meshes.back().material = c.editorMaterial;
            }
            ImportedMesh& mesh = im.meshes[found->second];
            const uint32_t base = uint32_t(mesh.pos.size());
            for (const RenderVertex& v : c.verts) {
                const Vec3 p = toFile(v.pos - centre);
                mesh.pos.push_back(p);
                mesh.normal.push_back(toFile(v.normal));
                mesh.uv.push_back({v.u, v.v});
                im.bounds.add(p);
            }
            for (const uint32_t i : c.idx) mesh.idx.push_back(base + i);
        }
        // Names: the brush model needs a targetname for the link; the model is "<name>_vis".
        std::string name;
        for (auto& it : e.items)
            if (KeyValueLine* kv = it.kv(); kv && kv->name() == "targetname") name = kv->value;
        if (name.empty()) {
            do name = "iw8r_bm_" + std::to_string(n++);
            while (names.count(name));
            EntityItem item;
            item.v = makeKeyValue("targetname", name, work.eol, e.usesCanonicalKeys());
            e.items.insert(e.items.begin(), std::move(item));
            names.insert(name);
        }
        std::string vis = name + "_vis";
        while (names.count(vis)) vis += "_";
        names.insert(vis);
        const std::string modelName = "iw8r_bmvis_" + customNameHash(name + "|" + std::to_string(ei));
        im.name = modelName;
        models.emplace(modelName, std::move(im));
        {
            EntityItem item;
            item.v = makeKeyValue("iw8r_no_draw", "1", work.eol, e.usesCanonicalKeys());
            e.items.insert(e.items.begin(), std::move(item));
        }
        {
            EntityItem item;
            item.v = makeKeyValue("iw8r_visual", vis, work.eol, e.usesCanonicalKeys());
            e.items.insert(e.items.begin(), std::move(item));
        }
        added += "{\n\"classname\" \"script_model\"\n\"model\" \"" + modelName + "\"\n\"targetname\" \"" + vis + "\"\n\"origin\" \"" +
                 formatNumber(centre.x) + " " + formatNumber(centre.y) + " " + formatNumber(centre.z) +
                 "\"\n\"iw8r_no_collision\" \"1\"\n}\n";
        ++made;
    }
    if (made) {
        // The new entities are parsed like any map text and appended.
        Id next = Id(1) << 40;
        ParseResult pr = parseMap("iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n" + added, next);
        if (pr.map)
            for (size_t i = 1; i < pr.map->entities.size(); ++i) work.entities.push_back(pr.map->entities[i]);
        else
            made = 0;
    }
    CompileOptions opt2 = opt;
    if (made) opt2.stockModels = &models;
    const bool ok = compileIw8ExtractionVariants(made ? work : mapIn, outDir, opt2, rep, err);
    if (made)
        rep.warnings.insert(rep.warnings.begin(), std::to_string(made) + " moving brush model(s) drawn by a generated model linked to them "
                                                                         "(their own surfaces did not show in game); iw8r_brush_visual 0 on one turns this off");
    return ok;
}

std::vector<StockOverlayModel> stockOverlayModels(const MapFile& map, int patchSubdiv, std::vector<std::string>* notes) {
    std::vector<StockOverlayModel> out;
    std::vector<std::string> warn;
    size_t otherEntities = 0, toolOnly = 0;
    for (size_t ei = 0; ei < map.entities.size(); ++ei) {
        const Entity& e = map.entities[ei];
        size_t prims = 0;
        for (const auto& it : e.items) prims += it.prim() ? 1 : 0;
        if (!prims) continue;
        const std::string cls = lower(e.classname());
        if (isLootBoxClass(cls)) continue;  // loot boxes go to the edits file (stock_loot.h)
        if (cls != "worldspawn" && cls != "func_group") {
            ++otherEntities;
            continue;
        }
        Gather g;
        g.subdiv = std::clamp(patchSubdiv, 1, 32);
        g.warnings = &warn;
        size_t group = 0;
        for (const auto& it : e.items) {
            const Primitive* p = it.prim();
            if (!p) continue;
            const size_t before = g.chunks.size();
            if (p->kind == PrimKind::Brush) gatherBrush(*p, ei, group++, g);
            else if (p->kind == PrimKind::Patch) gatherPatch(*p, ei, group++, g);
            if (g.chunks.size() == before) ++toolOnly;
        }
        AABB box;
        for (const Chunk& c : g.chunks)
            for (const RenderVertex& v : c.verts) box.add(v.pos);
        if (g.chunks.empty() || !box.valid()) continue;
        StockOverlayModel m;
        m.entity = ei;
        m.centre = box.center();
        std::string& o = m.obj;
        o = "# IW8 Radiant: brushes added to a stock map (entity " + std::to_string(ei) + ")\n";
        size_t base = 1;
        std::map<std::string, std::vector<const Chunk*>> byMaterial;
        for (const Chunk& c : g.chunks) byMaterial[c.editorMaterial].push_back(&c);
        for (const auto& [material, chunks] : byMaterial) {
            m.materials.insert(material);
            o += "g " + (material.empty() ? std::string("default") : material) + "\nusemtl " + material + "\n";
            for (const Chunk* c : chunks) {
                for (const RenderVertex& v : c->verts) {
                    const Vec3 p = v.pos - m.centre;
                    o += "v " + formatNumber(p.x) + " " + formatNumber(p.z) + " " + formatNumber(-p.y) + "\n";
                    o += "vn " + formatNumber(v.normal.x) + " " + formatNumber(v.normal.z) + " " + formatNumber(-v.normal.y) + "\n";
                    o += "vt " + formatNumber(v.u) + " " + formatNumber(v.v) + "\n";
                }
                for (size_t i = 0; i + 2 < c->idx.size(); i += 3) {
                    o += "f";
                    for (int k = 0; k < 3; ++k) {
                        const std::string n = std::to_string(base + c->idx[i + size_t(k)]);
                        o += " " + n + "/" + n + "/" + n;
                    }
                    o += "\n";
                    ++m.triangles;
                }
                base += c->verts.size();
            }
        }
        if (m.triangles) out.push_back(std::move(m));
    }
    if (notes) {
        if (toolOnly) notes->push_back(std::to_string(toolOnly) + " brush(es) of tool materials only (caulk, clip, nodraw, ...) draw nothing and are not built");
        if (otherEntities)
            notes->push_back(std::to_string(otherEntities) + " brush entit" + (otherEntities == 1 ? "y" : "ies") +
                             " other than the worldspawn and func_group (triggers, brush models) are not built into a stock map");
    }
    return out;
}

// Models that move without scripting (iw8r_spin / iw8r_bob / iw8r_lift, map_script.h) must be live script_models with
// a targetname: a misc_model with one of the keys is built as a script_model, and a nameless one gets a name.
bool isMoverModel(const Entity& e) {
    const std::string c = lower(e.classname());
    if (c != "misc_model" && c != "script_model") return false;
    if (const std::string* nc = e.value("iw8r_no_collision"); nc && *nc == "1") return true;
    return e.value("iw8r_spin") || e.value("iw8r_bob") || e.value("iw8r_lift");
}

bool compileIw8ExtractionMovers(const MapFile& mapIn, const std::string& outDir, const CompileOptions& opt, CompileReport& rep,
                                std::string* err) {
    bool edit = false;
    for (const auto& e : mapIn.entities)
        if (isMoverModel(e)) {
            const std::string* tn = e.value("targetname");
            edit |= lower(e.classname()) == "misc_model" || !tn || tn->empty();
        }
    if (!edit) return compileIw8ExtractionBrushVisuals(mapIn, outDir, opt, rep, err);
    MapFile work = mapIn;
    size_t promoted = 0, named = 0, n = 0;
    for (auto& e : work.entities) {
        if (!isMoverModel(e)) continue;
        bool hasName = false;
        for (auto& it : e.items) {
            KeyValueLine* kv = it.kv();
            if (!kv) continue;
            if (kv->name() == "classname" && lower(kv->value) == "misc_model") {
                kv->set("script_model");
                ++promoted;
            }
            if (kv->name() == "targetname") {
                if (kv->value.empty()) kv->set("iw8r_mover_" + std::to_string(n));
                hasName = true;
            }
        }
        if (!hasName) {
            EntityItem item;
            item.v = makeKeyValue("targetname", "iw8r_mover_" + std::to_string(n), work.eol, e.usesCanonicalKeys());
            e.items.insert(e.items.begin(), std::move(item));
            ++named;
        }
        ++n;
    }
    const bool ok = compileIw8ExtractionBrushVisuals(work, outDir, opt, rep, err);
    rep.warnings.insert(rep.warnings.begin(), std::to_string(n) + " moving model(s) (iw8r_spin / iw8r_bob / iw8r_lift): " +
                                                  std::to_string(promoted) + " misc_model(s) built as script_model, " +
                                                  std::to_string(named) + " given a targetname");
    return ok;
}

bool compileIw8Extraction(const MapFile& mapIn, const std::string& outDir, const CompileOptions& opt, CompileReport& rep,
                          std::string* err) {
    // Owner playtest 2026-11-11: traffic cones placed as misc_model did not move. A physics model (name ending _dyn)
    // only moves as a dynamic_prop; say so once per model.
    struct DynNote {
        const MapFile& map;
        CompileReport& rep;
        ~DynNote() {
            std::map<std::string, size_t> dyn;
            for (const auto& e : map.entities)
                if (lower(e.classname()) == "misc_model")
                    if (const std::string* m = e.value("model"); m && m->size() > 4 && m->compare(m->size() - 4, 4, "_dyn") == 0) ++dyn[*m];
            for (const auto& [m, n] : dyn)
                rep.warnings.push_back(std::to_string(n) + " misc_model placement(s) of the physics model '" + m +
                                       "' stand still: use the class dynamic_prop for props that move when shot or pushed");
        }
    } dynNote{mapIn, rep};
    // Prefabs are expanded first (misc_prefab is an editor-only entity): the build sees their brushes and entities.
    bool hasPrefab = false;
    for (const auto& e : mapIn.entities) hasPrefab |= e.classname() == "misc_prefab";
    if (!hasPrefab) return compileIw8ExtractionMovers(mapIn, outDir, opt, rep, err);
    MapFile flat;
    std::vector<std::string> prefabWarnings;
    const std::string mapPath = opt.mapDir.empty() ? std::string() : (fs::path(opt.mapDir) / "map.map").string();
    PrefabFlattenInfo bundles;
    if (!flattenPrefabBundles(mapIn, mapPath, opt.prefabRoots, opt.prefabSupport, flat, bundles, &prefabWarnings, err)) return false;
    // Prefab bundles bring their own asset folders and scripts (docs/PREFAB_BUNDLES.md). A map without a bundle
    // placement takes the build exactly as before: the options are passed on untouched.
    CompileOptions bundleOpt;
    MaterialLibrary bundleLibrary;
    const CompileOptions* useOpt = &opt;
    if (bundles.bundlePlacements > 0) {
        if (const std::string serr = prefabScriptsError(bundles.scripts); !serr.empty()) {
            if (err) *err = serr;
            return false;
        }
        bundleOpt = opt;
        useOpt = &bundleOpt;
        const auto addRoots = [](std::vector<std::string>& to, const std::vector<std::string>& from) {
            for (const std::string& r : from)
                if (std::find(to.begin(), to.end(), r) == to.end()) to.push_back(r);
        };
        addRoots(bundleOpt.modelRoots, bundles.assetRoots.models);
        addRoots(bundleOpt.soundRoots, bundles.assetRoots.sounds);
        addRoots(bundleOpt.fxRoots, bundles.assetRoots.effects);
        if (!bundles.scripts.empty()) bundleOpt.prefabScripts = &bundles.scripts;
        if (!bundles.assetRoots.materials.empty()) {
            // The bundles' materials must be in the one library the build looks names up in.
            std::string ignored;
            MaterialLibrary own;
            own.load(bundles.assetRoots.materials, &ignored);
            bool lacking = false;
            for (const std::string& name : own.names()) lacking |= !(opt.library && opt.library->find(name));
            if (lacking && opt.library && opt.materialRoots.empty()) {
                prefabWarnings.push_back("prefab bundle materials are not in the material library this build was given, and the "
                                         "library's folders are not known (CompileOptions::materialRoots): their surfaces build grey");
            } else if (lacking) {
                std::vector<std::string> roots = opt.materialRoots;
                addRoots(roots, bundles.assetRoots.materials);
                std::string lerr;
                bundleLibrary.load(roots, &lerr);
                for (size_t at = 0; at < lerr.size();) {
                    size_t end = lerr.find('\n', at);
                    if (end == std::string::npos) end = lerr.size();
                    if (end > at) prefabWarnings.push_back("prefab bundle materials: " + lerr.substr(at, end - at));
                    at = end + 1;
                }
                bundleOpt.library = &bundleLibrary;
            }
        }
    }
    const bool ok = compileIw8ExtractionMovers(flat, outDir, *useOpt, rep, err);
    size_t prefabs = 0;
    for (const auto& e : mapIn.entities) prefabs += e.classname() == "misc_prefab";
    rep.warnings.insert(rep.warnings.begin(), std::to_string(prefabs) + " prefab(s) expanded into the build");
    rep.warnings.insert(rep.warnings.begin() + 1, prefabWarnings.begin(), prefabWarnings.end());
    return ok;
}

bool compileIw8ExtractionFlat(const MapFile& map, const std::string& outDir, const CompileOptions& opt, CompileReport& rep,
                              std::string* err) {
    rep = CompileReport{};
    auto fail = [&](const std::string& msg) {
        if (err) *err = msg;
        return false;
    };
    if (!isValidIw8MapId(opt.mapId))
        return fail("invalid map id '" + opt.mapId + "': need mp_ followed by 1-12 characters of [a-z0-9_] (at most 15 in total)");
    if (!isSafeMaterialName(opt.greyMaterial)) return fail("invalid grey material name '" + opt.greyMaterial + "'");
    if (opt.library)
        for (const std::string& name : opt.library->names()) {
            if (lower(name) == lower(opt.greyMaterial))
                return fail("custom material '" + name + "' has the grey material's name; rename it");
        }
    if (outDir.empty()) return fail("empty output folder");
    const fs::path root(outDir);
    std::string dirErr;
    if (!checkOutputFolder(root, dirErr)) return fail(dirErr);

    std::vector<std::string>& warn = rep.warnings;
    const std::string id = opt.mapId;
    const std::string mapName = "maps/mp/" + id + ".d3dbsp";

    // ---- 1. Entities: world geometry from worldspawn and func_group, point entities for .ents (doc 15 §3).
    Gather g;
    g.subdiv = std::clamp(opt.patchSubdiv, 1, 32);
    for (const auto& we : map.entities)
        if (we.isWorldspawn())
            if (const std::string* os = we.value("iw8r_patch_one_sided"); os && *os == "1") g.patchBackFaces = false;
    g.warnings = &warn;
    g.library = opt.library;
    const Entity* worldspawn = nullptr;
    std::vector<const Entity*> points;
    std::map<std::string, size_t> skippedBrushEntities;
    size_t groupCounter = 0;
    std::vector<BrushModelOut> brushModels;    // *1, *2, ... in map order
    std::vector<CollisionBrush> entityBrushes;  // appended after the world brushes (indices fixed up below)

    for (size_t ei = 0; ei < map.entities.size(); ++ei) {
        const Entity& e = map.entities[ei];
        const std::string cls = e.classname();
        const std::string lcls = lower(cls);
        if (lcls == "worldspawn") {
            if (!worldspawn)
                worldspawn = &e;
            else
                warn.push_back("entity " + std::to_string(ei) + ": additional worldspawn, its keys are not written to .ents");
        }
        bool hasPrim = false;
        for (const auto& it : e.items)
            if (it.isPrimitive()) {
                hasPrim = true;
                break;
            }
        if (!hasPrim) {
            if (lcls == "worldspawn" || lcls == "func_group") continue;  // no geometry; worldspawn is written separately
            if (cls.empty()) {
                warn.push_back("entity " + std::to_string(ei) + " has no classname, skipped");
                continue;
            }
            points.push_back(&e);
            continue;
        }
        if (lcls != "worldspawn" && lcls != "func_group") {
            // Brush entities the converter accepts become brush model *N (doc 15 §3.5): triggers get collision
            // hulls with trigger contents and no render surfaces; script_brushmodel gets both.
            const bool trigger = startsWith(lcls, "trigger_") && acceptedPointClass(lcls);
            if (isLootBoxClass(lcls)) continue;  // loot clusters and zones: boxes of the collision json (below)
            if (!trigger && lcls != "script_brushmodel") {
                ++skippedBrushEntities[cls];
                continue;
            }
            Gather bg;
            bg.subdiv = g.subdiv;
            bg.patchBackFaces = g.patchBackFaces;
            bg.warnings = &warn;
            bg.library = opt.library;
            for (const auto& it : e.items) {
                const Primitive* p = it.prim();
                if (!p) continue;
                const size_t group = groupCounter++;
                if (p->kind == PrimKind::Brush)
                    gatherBrush(*p, ei, group, bg, trigger);
                else if (p->kind == PrimKind::Patch && !trigger)
                    gatherPatch(*p, ei, group, bg);
            }
            BrushModelOut bm;
            bm.entity = ei;
            bm.trigger = trigger;
            bm.chunks = std::move(bg.chunks);
            if (trigger) bm.chunks.clear();  // a trigger is never drawn
            // A generated model draws this brush model (compileIw8ExtractionBrushVisuals): no surfaces of its own.
            if (const std::string* nd = e.value("iw8r_no_draw"); nd && *nd == "1") bm.chunks.clear();
            for (auto& b : bg.brushes) {
                b.contents = trigger ? kTriggerContents : 1u;
                bm.brushes.push_back(entityBrushes.size());  // local index, offset after the world brushes are known
                entityBrushes.push_back(std::move(b));
            }
            bm.bounds = bg.bounds;
            g.bounds.add(bg.bounds);
            if (!trigger) g.editorMats.insert(bg.editorMats.begin(), bg.editorMats.end());
            g.skyFaces += bg.skyFaces;
            if (!bm.bounds.valid()) {
                warn.push_back("entity " + std::to_string(ei) + " (" + cls + "): no valid brushes, left out");
                continue;
            }
            brushModels.push_back(std::move(bm));
            continue;
        }
        for (const auto& it : e.items) {
            const Primitive* p = it.prim();
            if (!p) continue;
            const size_t group = groupCounter++;
            if (p->kind == PrimKind::Brush)
                gatherBrush(*p, ei, group, g);
            else if (p->kind == PrimKind::Patch)
                gatherPatch(*p, ei, group, g);
            else
                warn.push_back("entity " + std::to_string(ei) + ": unknown primitive skipped");
        }
    }
    // Placed models: a solid collision shape per model in the world collision (CompileOptions::modelCollisionBoxes):
    // one convex brush per convex piece (modelCollisionHulls, roadmap 4.20), or an oriented box when the mesh gives no hull.
    size_t modelBoxes = 0;
    size_t hullModels = 0, hullBrushCount = 0;
    if (opt.modelCollisionBoxes) {
        std::map<std::string, AABB> modelBounds;  // model name -> bounds in model space (Z-up, map units)
        std::map<std::string, std::vector<std::vector<Plane>>> modelPieces;  // model name -> convex pieces (Y-up, map units)
        // The surface a model's collision reads as (impacts, footsteps): the surface type most of its triangles have,
        // from its materials (owner playtest 2: hulls of placed models carried no type and read as concrete).
        std::map<std::string, int> modelSurface;
        const auto surfaceOfModel = [&](const std::string& name) -> int {
            if (auto it = modelSurface.find(name); it != modelSurface.end()) return it->second;
            int best = 0;
            if (opt.stockModels)
                if (auto sm = opt.stockModels->find(name); sm != opt.stockModels->end()) {
                    std::map<int, size_t> tris;
                    for (const auto& mesh : sm->second.meshes)
                        if (!mesh.material.empty()) tris[replaySurfaceTypeForIw3(surfaceTypeOf(mesh.material, opt.library))] += mesh.idx.size() / 3;
                    size_t most = 0;
                    for (const auto& [type, n] : tris)
                        if (type > 0 && n > most) {
                            most = n;
                            best = type;
                        }
                }
            return modelSurface[name] = best;
        };
        const auto boundsOf = [&](const std::string& name) -> AABB {
            if (auto it = modelBounds.find(name); it != modelBounds.end()) return it->second;
            AABB b;
            const auto addYUp = [&](const ImportedModel& m, double scale) {
                for (const auto& mesh : m.meshes)
                    for (const Vec3& p : mesh.pos) b.add(Vec3{p.x, -p.z, p.y} * scale);
            };
            // Convex pieces of the model in its file (Y-up) space. convexPieces works on the raw points; the plane offsets
            // d are scaled to map units here like the points (a unit normal does not change under a uniform scale).
            const auto piecesOf = [&](const ImportedModel& m, double scale) {
                std::vector<std::vector<Plane>> pcs;
                if (!opt.modelCollisionHulls) return pcs;
                pcs = convexPieces(m, opt.modelCollisionMaxPieces, size_t(std::max(0, opt.modelCollisionMaxPlanes)));
                for (auto& pc : pcs)
                    for (Plane& pl : pc) pl.d *= scale;
                return pcs;
            };
            std::vector<std::vector<Plane>> pcs;
            // A re-skinned copy of a custom model (iw8r_material) has its own name but the mesh of its base model: its
            // collision is the base model's (owner report 2026-11-11: re-skinned cubes stopped bullets, not the player).
            std::string fileName = name;
            if (opt.customModelVariants)
                if (const auto v = opt.customModelVariants->find(name); v != opt.customModelVariants->end()) fileName = v->second.base;
            const std::string obj = isCustomModelName(fileName) ? findCustomModel(opt.modelRoots, fileName) : std::string();
            if (!obj.empty()) {
                ImportedModel im;
                if (importModelFile(obj, im, nullptr)) {
                    const double s = opt.modelScale > 0 ? opt.modelScale : 1.0;
                    addYUp(im, s);
                    pcs = piecesOf(im, s);
                }
            } else if (opt.stockModels) {
                if (auto it = opt.stockModels->find(name); it != opt.stockModels->end()) {
                    addYUp(it->second, 1.0);
                    pcs = piecesOf(it->second, 1.0);
                }
            }
            modelBounds[name] = b;
            modelPieces[name] = std::move(pcs);
            return b;
        };
        // Which script_models x64-zt keeps as entities (TakeUnscriptedScriptModels, iw3_fastfile.cpp ~4339-4380): a
        // gameobject or exploder, or a targetname another entity targets or a special name. Every other one becomes a
        // static model, so it gets a box like misc_model. (The map script the editor generates names no model.)
        std::set<std::string> targeted;
        for (const Entity* pe : points)
            if (const std::string* t = pe->value("target")) targeted.insert(*t);
        const auto staysEntity = [&](const Entity& e) {
            if (e.value("script_gameobjectname") || e.value("script_exploder")) return true;
            const std::string* tn = e.value("targetname");
            // The generated script names a moving or animated model, so the converter keeps it an entity.
            if (tn && !tn->empty() && (e.value("iw8r_spin") || e.value("iw8r_bob") || e.value("iw8r_lift") || e.value("iw8r_anim"))) return true;
            // iw8r_no_collision 1: kept live (the script names it), so it has no player collision and no static box.
            if (const std::string* nc = e.value("iw8r_no_collision"); tn && !tn->empty() && nc && *nc == "1") return true;
            return tn && (targeted.count(*tn) || *tn == "exploder" || *tn == "oldschool_pickup" || *tn == "destructible" || *tn == "destructable" ||
                          *tn == "interactive_tv");
        };
        for (const Entity* pe : points) {
            const std::string cls = lower(pe->classname());
            if (cls != "misc_model" && cls != "script_model") continue;
            if (cls == "script_model" && staysEntity(*pe)) continue;  // a live entity (gameobject, targeted): no static box
            const std::string* mv = pe->value("model");
            if (!mv || mv->empty() || (*mv)[0] == '*') continue;
            const AABB mb = boundsOf(*mv);
            if (!mb.valid()) continue;
            double ang[3] = {0, 0, 0};
            if (const std::string* av = pe->value("angles"); av && !parseVec3Text(*av, ang)) ang[0] = ang[1] = ang[2] = 0;
            double ms = 1.0;
            if (const std::string* sv = pe->value("modelscale")) {
                double sc = 0;
                if (parseNumber(*sv, sc) && sc > 0) ms = sc;
            }
            const auto ax = anglesToAxis(ang[0], ang[1], ang[2]);  // forward, left, up
            const Vec3 o = pe->origin().value_or(Vec3{});
            // World bounds of the whole model, padded by 1. Every brush of the model takes them: a superset of each
            // hull piece, which is all the bounds have to be.
            const auto placedBounds = [&]() {
                AABB wb;
                for (int c = 0; c < 8; ++c) {
                    const Vec3 local{(c & 1 ? mb.maxs.x : mb.mins.x) * ms, (c & 2 ? mb.maxs.y : mb.mins.y) * ms,
                                     (c & 4 ? mb.maxs.z : mb.mins.z) * ms};
                    wb.add(o + ax[0] * local.x + ax[1] * local.y + ax[2] * local.z);
                }
                wb.mins = wb.mins - Vec3{1, 1, 1};
                wb.maxs = wb.maxs + Vec3{1, 1, 1};
                return wb;
            };
            // Hulls: a piece plane dot(n, p) <= d (file Y-up, map units) becomes, with nz = (n.x, -n.z, n.y) the Z-up
            // normal (the (x, -z, y) conversion above is a proper rotation, so outward stays outward) and the placement
            // x = o + R s with R's columns ax[0..2] (the box planes below use the same R), the world plane
            // dot(R nz, x) <= ms * d + dot(R nz, o).
            const std::vector<std::vector<Plane>>& pcs = modelPieces[*mv];
            if (!pcs.empty()) {
                const AABB wb = placedBounds();
                for (const auto& pc : pcs) {
                    CollisionBrush hb;
                    hb.contents = 1u;
                    hb.surfaceType = surfaceOfModel(*mv);
                    for (const Plane& pl : pc) {
                        const Vec3 nz{pl.n.x, -pl.n.z, pl.n.y};
                        const Vec3 n = ax[0] * nz.x + ax[1] * nz.y + ax[2] * nz.z;
                        hb.planes.push_back({n, ms * pl.d + dot(n, o)});
                    }
                    hb.bounds = wb;
                    g.bounds.add(hb.bounds);
                    g.brushes.push_back(std::move(hb));
                    ++hullBrushCount;
                }
                ++hullModels;
                continue;
            }
            // No hull (a flat or degenerate mesh, or hulls off): the oriented box.
            CollisionBrush cb;
            cb.contents = 1u;
            cb.surfaceType = surfaceOfModel(*mv);
            for (int i = 0; i < 3; ++i) {
                double lo = mb.mins[i] * ms, hi = mb.maxs[i] * ms;
                if (hi - lo < 2) lo -= 1, hi += 1;  // flat cards (fences) still get a 2-unit slab
                const Vec3 n = normalize(ax[size_t(i)]);
                cb.planes.push_back({n, dot(n, o) + hi});
                cb.planes.push_back({n * -1.0, -(dot(n, o) + lo)});
            }
            cb.bounds = placedBounds();
            g.bounds.add(cb.bounds);
            g.brushes.push_back(std::move(cb));
            ++modelBoxes;
        }
        if (opt.modelCollisionHulls) {
            if (hullModels || modelBoxes)
                warn.push_back("placed model collision: " + std::to_string(hullModels) + " model(s) as " + std::to_string(hullBrushCount) +
                               " convex hull brush(es), " + std::to_string(modelBoxes) + " as a solid box (no hull: flat or degenerate mesh)");
        } else if (modelBoxes) {
            warn.push_back(std::to_string(modelBoxes) + " placed model(s) got a solid collision box (hulls off; players and bullets)");
        }
    }
    // Collision brush order: world brushes (submodel 0) first, then each brush model's (doc 15 §2.3).
    const size_t worldBrushCount = g.brushes.size();
    for (auto& bm : brushModels)
        for (auto& i : bm.brushes) i += worldBrushCount;
    for (auto& b : entityBrushes) g.brushes.push_back(std::move(b));
    for (const auto& [cls, n] : skippedBrushEntities)
        warn.push_back(std::to_string(n) + " brush entit" + (n == 1 ? "y" : "ies") + " of class '" + cls +
                       "' not compiled (the converter accepts only triggers of its class list and script_brushmodel, doc 15 §3.3)");
    if (brushModels.size() + 1 > 65535) return fail("too many brush entities (the limit is 65534)");
    if (!worldspawn) warn.push_back("no worldspawn entity: default sun used");
    if (g.skyFaces) warn.push_back(std::to_string(g.skyFaces) + " sky face(s) not rendered: sky is not supported yet");

    // ---- 2. Render surfaces (doc 15 §2.2).
    std::vector<RenderSurface> surfaces = groupChunks(g.chunks, true);
    if (surfaces.size() > kMaxSurfaces) {
        warn.push_back("more than 65535 surfaces with one per brush: grouping one per entity and material instead");
        surfaces = groupChunks(g.chunks, false);
    }
    if (surfaces.empty()) return fail("no renderable surfaces: every face is a tool material or was skipped");
    const size_t worldSurfaceCount = surfaces.size();
    // Brush model surfaces follow the world's; brush_models[N] owns its range (iw3_fastfile.cpp ReadBrushModels).
    for (auto& bm : brushModels) {
        std::vector<RenderSurface> ms = groupChunks(bm.chunks, true);
        bm.surfaceStart = surfaces.size();
        bm.surfaceCount = ms.size();
        for (auto& m : ms) surfaces.push_back(std::move(m));
    }
    if (surfaces.size() > kMaxSurfaces)
        return fail("too many surfaces (" + std::to_string(surfaces.size()) + "), the limit is 65535 (doc 15 §2.1)");

    size_t triangles = 0;
    for (const auto& s : surfaces) {
        triangles += s.idx.size() / 3;
        for (const auto& v : s.verts)
            if (!finite3(v.pos) || !std::isfinite(v.u) || !std::isfinite(v.v)) return fail("non-finite vertex data in " + s.editorMaterial);
    }
    {
        std::string greyList, customList;
        size_t greyCount = 0, customCount = 0;
        for (const auto& m : g.editorMats) {
            if (opt.library && opt.library->find(m)) {
                customList += (customList.empty() ? "" : ", ") + m;
                ++customCount;
            } else {
                greyList += (greyList.empty() ? "" : ", ") + m;
                ++greyCount;
            }
        }
        if (greyCount)
            warn.push_back(std::to_string(greyCount) + " distinct editor material(s) compiled to '" + opt.greyMaterial +
                           "' (no custom material; stock IW8 names need material and image files, doc 15 §4): " + greyList);
        if (customCount)
            warn.push_back(std::to_string(customCount) + " editor material(s) compiled to custom materials (doc 21): " + customList);
    }

    // ---- 3. Bounds: union of geometry, collision and point origins, padded by 16.
    AABB geom = g.bounds;
    for (const Entity* pe : points)
        if (auto o = pe->origin()) geom.add(*o);
    AABB bounds;
    bounds.mins = geom.mins - Vec3{kBoundsPad, kBoundsPad, kBoundsPad};
    bounds.maxs = geom.maxs + Vec3{kBoundsPad, kBoundsPad, kBoundsPad};

    // ---- 4. Sun (doc 15 §2.4, §7).
    double sunPitch = -50, sunYaw = 45, sunRoll = 0;
    double sunColor[3] = {1, 1, 1};
    bool haveSunColor = false;
    if (worldspawn) {
        if (const std::string* sd = worldspawn->value("sundirection")) {
            double v[3];
            if (parseVec3Text(*sd, v)) {
                sunPitch = v[0];
                sunYaw = v[1];
                sunRoll = v[2];
            } else {
                warn.push_back("worldspawn sundirection '" + *sd + "' not parsed: using -50 45 0");
            }
        }
        if (const std::string* sc = worldspawn->value("suncolor")) {
            double v[3];
            if (parseVec3Text(*sc, v)) {
                haveSunColor = true;
                if (std::max({v[0], v[1], v[2]}) > 1.0) {
                    for (double& c : v) c /= 255.0;
                    warn.push_back("worldspawn suncolor above 1 read as 0-255 and scaled");
                }
                for (int k = 0; k < 3; ++k) sunColor[k] = std::max(0.0, v[k]);
            } else {
                warn.push_back("worldspawn suncolor '" + *sc + "' not parsed: using 1 1 1");
            }
        }
    }
    const Vec3 sunDir = sunDirection(sunPitch, sunYaw);

    // ---- 5. .ents text (doc 15 §3).
    std::string ents;
    size_t entityCount = 0;
    size_t spawns = 0, tdmSpawns = 0, droppedKeys = 0, quoteFixes = 0;
    bool haveAllies = false, haveAxis = false, haveScriptModel = false;
    std::map<std::string, size_t> unacceptedClasses, strippedModels;
    std::map<std::string, std::string> customModelPaths;  // model name -> .obj
    // Re-skinned copies of custom models (iw8r_material on a shape or imported model): the variant is read from its
    // base model's file; its materials are replaced when it is imported below.
    if (opt.customModelVariants)
        for (const auto& [variant, v] : *opt.customModelVariants)
            if (const std::string obj = findCustomModel(opt.modelRoots, v.base); !obj.empty()) customModelPaths[variant] = obj;
    std::map<std::string, const ImportedModel*> stockModelsUsed;  // build name -> exported stock mesh
    // A stock model the editor exported (opt.stockModels): its build name, or "" when there is none.
    const auto stockBuild = [&](const std::string& modelName) -> std::string {
        if (!opt.stockModels) return {};
        const auto it = opt.stockModels->find(modelName);
        if (it == opt.stockModels->end() || it->second.meshes.empty()) return {};
        const std::string bn = stockModelBuildName(modelName);
        stockModelsUsed[bn] = &it->second;
        return bn;
    };

    // ---- 4b. Reflection probes, static models and sky (doc 29).
    // reflection_probe entities become world.json probes in map order (at most 256). Without one, the default probe
    // (image <id>_probe, at the world centre) is the only probe, as before.
    std::vector<Vec3> probeOrigins;  // one per reflection_probe entity; empty = default probe only
    std::vector<std::optional<AABB>> probeVolumes;  // authored volume (keys mins / maxs, relative to the origin)
    // Authored blend (5.5): "priority" (higher wins where volumes overlap; stock default 10) and "feather" (blend
    // distance in units, one number or x y z; the converter takes 1 / distance). Written as JSON fragments.
    std::vector<std::string> probeBlendJson;
    size_t probeEntities = 0;
    for (const Entity* pe : points) {
        if (lower(pe->classname()) != "reflection_probe") continue;
        ++probeEntities;
        if (probeOrigins.size() >= kMaxReflectionProbes) continue;
        std::optional<AABB> volume;
        {
            double mn[3], mx[3];
            const std::string* sm = pe->value("mins");
            const std::string* sx = pe->value("maxs");
            if (sm && sx && parseVec3Text(*sm, mn) && parseVec3Text(*sx, mx)) {
                if (mx[0] > mn[0] && mx[1] > mn[1] && mx[2] > mn[2]) {
                    const Vec3 o = pe->origin().value_or(bounds.center());
                    AABB b;
                    b.add(o + Vec3{mn[0], mn[1], mn[2]});
                    b.add(o + Vec3{mx[0], mx[1], mx[2]});
                    volume = b;
                } else {
                    warn.push_back("reflection_probe entity " + std::to_string(pe - map.entities.data()) +
                                   ": mins/maxs have no extent; the converter derives its volume");
                }
            }
        }
        probeVolumes.push_back(volume);
        {
            std::string blend;
            if (const std::string* pr = pe->value("priority"); pr && !pr->empty()) {
                char* end = nullptr;
                const double v = std::strtod(pr->c_str(), &end);
                if (end && *end == '\0' && std::isfinite(v)) blend += ",\"priority\":" + num(v);
                else warn.push_back("reflection_probe priority '" + *pr + "' is not a number; ignored");
            }
            if (const std::string* fe = pe->value("feather"); fe && !fe->empty()) {
                double d[3];
                const int n = std::sscanf(fe->c_str(), "%lf %lf %lf", &d[0], &d[1], &d[2]);
                if (n == 1) d[1] = d[2] = d[0];
                if ((n == 1 || n == 3) && d[0] > 0 && d[1] > 0 && d[2] > 0)
                    blend += ",\"feather\":" + vec3raw(1.0 / d[0], 1.0 / d[1], 1.0 / d[2]);
                else
                    warn.push_back("reflection_probe feather '" + *fe + "' needs one or three positive distances; ignored");
            }
            probeBlendJson.push_back(blend);
        }
        if (const auto o = pe->origin()) {
            probeOrigins.push_back(*o);
        } else {
            probeOrigins.push_back(bounds.center());
            warn.push_back("reflection_probe entity " + std::to_string(pe - map.entities.data()) +
                           " has no origin: placed at the world centre");
        }
    }
    if (probeEntities > kMaxReflectionProbes)
        warn.push_back(std::to_string(probeEntities) + " reflection_probe entities: only the first " +
                       std::to_string(kMaxReflectionProbes) + " are written (the limit is 256, doc 15 §2.1)");
    // Probe 0 is always the default probe at the bounds centre: x64-zt treats source probe 0 as the default and drops
    // it from the output when there are two or more (iw3_render_assets.cpp:5520-5531), so entity probes start at 1.
    std::vector<Vec3> probes{bounds.center()};
    probes.insert(probes.end(), probeOrigins.begin(), probeOrigins.end());
    // Surfaces and models use the nearest entity probe when there is one (the default only otherwise).
    const auto probeFor = [&](const Vec3& p) -> size_t { return probeOrigins.empty() ? 0 : 1 + nearestProbe(probeOrigins, p); };

    // misc_model entities with a custom model (same lookup as script_model, doc 22) become world.models[] placements
    // (iw3_fastfile.cpp:3743-3825 reads model, origin, axis, scale and reflection_probe). Stock names are skipped.
    struct ModelPlacementOut {
        std::string model;
        Vec3 origin;
        std::array<Vec3, 3> axis;
        double scale;
        double lodScale = 1.0;  // 5.22 (research 54): Replay per-placement LOD distance scale, one of the 16 table values
        bool noShadow = false;  // 5.22: casts into no local light's shadow (shadow_lights: []); the sun part needs a fork
        bool groundLit = false;  // 4.8: one flat ground colour instead of the light grid (model_flags 1 + ground_lighting)
        std::uint32_t groundColor = 0;
    };
    std::vector<ModelPlacementOut> placements;
    std::map<std::string, size_t> strippedMiscModels;
    for (const Entity* pe : points) {
        if (lower(pe->classname()) != "misc_model") continue;
        const std::string* mv = pe->value("model");
        const std::string modelName = mv ? *mv : std::string();
        std::string objPath;
        if (customModelPaths.count(modelName))
            objPath = customModelPaths[modelName];
        else if (isCustomModelName(modelName))
            objPath = findCustomModel(opt.modelRoots, modelName);
        std::string placedName = modelName;
        if (objPath.empty()) {
            placedName = stockBuild(modelName);
            if (placedName.empty()) {
                ++strippedMiscModels[modelName.empty() ? std::string("(no model)") : modelName];
                continue;
            }
        } else {
            customModelPaths[modelName] = objPath;
        }
        double angles[3] = {0, 0, 0};
        if (const std::string* av = pe->value("angles"); av && !parseVec3Text(*av, angles)) {
            warn.push_back("misc_model '" + modelName + "' angles '" + *av + "' not parsed: using 0 0 0");
            angles[0] = angles[1] = angles[2] = 0;
        }
        double scale = 1.0;
        if (const std::string* sv = pe->value("modelscale")) {
            double s = 0;
            if (parseNumber(*sv, s) && s > 0)
                scale = s;
            else
                warn.push_back("misc_model '" + modelName + "' modelscale '" + *sv + "' is not a positive number: using 1");
        }
        ModelPlacementOut out{placedName, pe->origin().value_or(Vec3{}), anglesToAxis(angles[0], angles[1], angles[2]), scale};
        {
            std::map<std::string, std::string> keys;
            for (const char* k : {kSModelKeyLodScale, kSModelKeyNoShadow, kSModelKeyGroundLighting})
                if (const std::string* v = pe->value(k)) keys[k] = *v;
            std::vector<std::string> fw;
            const SModelFlags f = readSModelFlags(keys, &fw);
            for (const std::string& w : fw) warn.push_back("misc_model '" + modelName + "': " + w);
            out.lodScale = f.lodScale;
            out.noShadow = f.noShadow;
            out.groundLit = f.groundLighting;
            out.groundColor = f.groundColor;
        }
        placements.push_back(out);
    }
    // Scriptables (6.14, research 86): `scriptable` point entities (scriptable = the stock definition's name, origin,
    // angles, optional targetname / target / script_noteworthy / script_linkname / script_linkto) become the collision
    // json's scriptable_instances with the numbers of the definition from the catalogue. Only a definition that is
    // always loaded and fully known is written; any other is left out with a warning (a wrong count is a crash at load).
    std::string scriptablesJson;
    size_t scriptableCount = 0;
    bool stockDoorPlaced = false;
    {
        // A map made for the Warzone game type (worldspawn iw8r_br, or the prematch area the br script needs): the
        // game loads the battle royale zone (common_br_mp) for such a match, so its definitions (buy station, loot
        // caches) are placed by name. Whether it does so for an own map is not seen in game (research 89).
        bool brMap = worldspawn && worldspawn->value("iw8r_br");
        for (const Entity* pe : points)
            if (const std::string* tn = pe->value("targetname"); tn && *tn == "br_prematch_insertion_point") brMap = true;
        size_t brPlaced = 0;
        std::map<std::string, size_t> carried;  // stock zone -> instances
        std::map<std::string, size_t> leftOut;  // reason -> count
        for (const Entity* pe : points) {
            if (lower(pe->classname()) != "scriptable") continue;
            const std::string* dv = pe->value("scriptable");
            if (!dv || dv->empty()) dv = pe->value("definition");
            if (!dv || dv->empty()) {
                ++leftOut["no scriptable key (the definition's name)"];
                continue;
            }
            const ScriptableDefinition* def = opt.scriptables ? opt.scriptables->find(*dv) : nullptr;
            if (!opt.scriptables || opt.scriptables->size() == 0) {
                ++leftOut["the scriptable catalogue (data/asset_catalog/iw8_scriptables.tsv) was not found"];
                continue;
            }
            if (!def) {
                ++leftOut["'" + *dv + "' is not a stock scriptable definition"];
                continue;
            }
            if (!def->verified) {
                ++leftOut["'" + def->name + "': no stock map shows all the numbers a build must write for it"];
                continue;
            }
            std::string carryZone;
            const bool fromBrZone = !def->alwaysLoaded && def->brZone();
            if (fromBrZone && !brMap) {
                ++leftOut["'" + def->name + "' is in the Warzone zone (common_br_mp), which only a Warzone match loads; make the map a "
                          "Warzone map first (Tools > Game Modes > Carry in this map, Insert Warzone start entities)"];
                continue;
            }
            if (fromBrZone) ++brPlaced;
            if (fromBrZone) {
                // Research 93 (read from the game exe): the extra zone of a map is chosen by the map's name
                // (mp/levelZoneParents.csv), not by the game type, so a custom map gets common_core_mp and never
                // common_br_mp, even in a br match. The definition is carried from common_br_mp instead.
                carryZone = "common_br_mp";
                if (!opt.carryScriptables) {
                    ++leftOut["'" + def->name + "' lives in the Warzone zone (common_br_mp), which a custom map does not load; turn on "
                              "Edit > Preferences > Real stock scripted objects to carry it into the map"];
                    continue;
                }
            }
            if (!def->alwaysLoaded && !fromBrZone) {
                carryZone = def->carryZone();
                if (carryZone.empty()) {
                    ++leftOut["'" + def->name + "' is in no always-loaded zone and no stock map zone (" + def->zones + ")"];
                    continue;
                }
                if (!opt.carryScriptables) {
                    ++leftOut["'" + def->name + "' lives only in stock map zones (" + def->zones +
                              "); turn on Edit > Preferences > Real stock scripted objects to carry it into the map"];
                    continue;
                }
            }
            if (scriptableCount >= 2000) {
                ++leftOut["more than 2000 scriptables"];
                continue;
            }
            ScriptablePlacement place;
            place.def = def;
            const Vec3 o = pe->origin().value_or(Vec3{});
            place.origin[0] = o.x, place.origin[1] = o.y, place.origin[2] = o.z;
            if (const std::string* av = pe->value("angles"); av && !parseVec3Text(*av, place.angles))
                place.angles[0] = place.angles[1] = place.angles[2] = 0;
            const auto text = [&](const char* key) {
                const std::string* v = pe->value(key);
                std::string out = v ? *v : std::string();
                for (char& c : out)
                    if (static_cast<unsigned char>(c) < 0x20 || c == '"' || c == '\\') c = '_';
                return out;
            };
            place.targetname = text("targetname");
            place.target = text("target");
            place.noteworthy = text("script_noteworthy");
            place.linkname = text("script_linkname");
            place.linkto = text("script_linkto");
            place.carryZone = carryZone;
            if (!carryZone.empty()) ++carried[carryZone];
            stockDoorPlaced |= def->door();
            scriptablesJson += std::string(scriptableCount ? "," : "") + scriptableInstanceJson(place);
            ++scriptableCount;
        }
        for (const auto& [zone, n] : carried)
            warn.push_back(std::to_string(n) + " scriptable instance(s) carry their definition from the stock zone " + zone +
                           " (the converter copies the definition, its models, collision and materials; needs the game "
                           "folder; not seen in game)");
        if (brPlaced)
            warn.push_back(std::to_string(brPlaced) + " scriptable instance(s) use a definition of the Warzone zone (common_br_mp), carried into the map "
                           "(a custom map does not load that zone, research 93); they work only in a Warzone (br) match; not seen in game");
        for (const auto& [reason, n] : leftOut)
            warn.push_back(std::to_string(n) + " scriptable entit" + (n == 1 ? "y" : "ies") + " left out: " + reason);
        if (scriptableCount)
            warn.push_back(std::to_string(scriptableCount) + " scriptable instance(s) written (needs a converter with scriptable instances, " +
                           "research 86; not seen in game)");
    }
    // Loot points (13.5, research 89): `iw8r_loot_point` point entities become the collision json's loot_points, which
    // the converter writes as MapEnts.spawnGroupLoot (no clusters and no zones, as the small stock maps with loot).
    // loot_cluster "cache" makes a loot cache point; every other point is a free point.
    std::string lootJson, lootZone, lootBoxes;
    std::vector<StockEditCarry> lootCarry;  // the loot item definitions of the map's loot table (research 93)
    size_t lootCount = 0;
    {
        size_t tooMany = 0;
        for (const Entity* pe : points) {
            if (!isLootPointClass(pe->classname())) continue;
            if (lootCount >= kLootPointLimit) {
                ++tooMany;
                continue;
            }
            const Vec3 o = pe->origin().value_or(Vec3{});
            double a[3] = {0, 0, 0};
            if (const std::string* av = pe->value("angles"); av && !parseVec3Text(*av, a)) a[0] = a[1] = a[2] = 0;
            const std::string* cv = pe->value("loot_cluster");
            const bool cache = cv && lower(*cv) == "cache";
            lootJson += std::string(lootCount ? "," : "") + "{\"origin\":[" + num(o.x) + "," + num(o.y) + "," + num(o.z) + "],\"angles\":[" +
                        num(a[0]) + "," + num(a[1]) + "," + num(a[2]) + "],\"cache\":" + (cache ? "true" : "false") + "}";
            ++lootCount;
        }
        if (tooMany) warn.push_back(std::to_string(tooMany) + " loot point(s) left out: a map takes at most " + std::to_string(kLootPointLimit));
        if (lootCount)
            warn.push_back(std::to_string(lootCount) + " loot point(s) written for the battle royale game type (needs a converter with loot points, "
                           "research 89; not seen in game)");
        // The loot tables choose what lies on a point by its zone's name (research 87 section 2.4): one zone over
        // every point, named by worldspawn iw8r_loot_zone (default map_quarry, a zone of the stock Warzone tables).
        if (lootCount) {
            lootZone = "map_quarry";
            if (worldspawn)
                if (const std::string* z = worldspawn->value("iw8r_loot_zone"); z && !z->empty()) {
                    const std::string name = lower(*z);
                    if (std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::islower(c) || std::isdigit(c) || c == '_'; }))
                        lootZone = name;
                    else
                        warn.push_back("worldspawn iw8r_loot_zone '" + *z + "' is not a lower-case name; map_quarry is used");
                }
        }
        // The loot item definitions (research 93): the loot system spawns brloot_* scriptables of common_br_mp on the
        // points, and a custom map never loads that zone, so the items of the map's loot table are carried from it.
        if (lootCount) {
            std::string table = kDefaultLootItemTable;
            if (worldspawn)
                if (const std::string* dv = worldspawn->value("iw8r_mode_dvars"))
                    for (const auto& [name, value] : parseModeDvars(*dv))
                        if (lower(name) == "loot_table_name") table = lower(value);
            if (!opt.lootItems) {
                warn.push_back("loot points: the loot item table catalogue (data/asset_catalog/iw8_br_loot_items.tsv) was not found, so no "
                               "loot item is carried; a custom map does not load the Warzone zone that holds them (research 93)");
            } else if (!opt.carryScriptables) {
                warn.push_back("loot points: the loot items live in the Warzone zone (common_br_mp), which a custom map does not load "
                               "(research 93); turn on Edit > Preferences > Real stock scripted objects to carry them, or the points stay empty");
            } else if (const std::vector<std::string>* items = opt.lootItems->items(table)) {
                size_t skipped = 0;
                for (const std::string& item : *items) {
                    const ScriptableDefinition* def = opt.scriptables ? opt.scriptables->find(item) : nullptr;
                    if (def && def->brZone()) lootCarry.push_back({"common_br_mp", "scriptable", item, ""});
                    else ++skipped;
                }
                warn.push_back(std::to_string(lootCarry.size()) + " loot item definition(s) of " + table +
                               " carried from common_br_mp (a custom map does not load it, research 93)" +
                               (skipped ? "; " + std::to_string(skipped) + " item(s) of the table are not in that zone and are left out" : std::string()) +
                               "; not seen in game");
            } else {
                warn.push_back("loot points: loot_table_name '" + table + "' is not one of the stock loot item tables; no loot item is carried");
            }
        }
        // Authored loot clusters and zones (iw8r_loot_cluster / iw8r_loot_zone brush entities, research 89 section
        // 6d): with zones, each point takes the first box that holds it and iw8r_loot_zone only names unnamed zones.
        if (lootCount) {
            std::vector<std::string> boxNotes;
            const LootBoxEdits boxes = documentLootBoxes(map, lootZone, &boxNotes);
            for (const std::string& n : boxNotes) warn.push_back(n);
            if (!boxes.empty()) {
                lootBoxes = lootBoxesJson(boxes);
                warn.push_back(std::to_string(boxes.clusters.size()) + " loot cluster(s) and " + std::to_string(boxes.zones.size()) +
                               " loot zone(s) written (each point takes the first box that holds it; not seen in game)");
            }
        }
    }
    // Vehicles (13.4, research 89): vehicle spawn structs make the converter carry the stock vehicle definition and
    // its model (collision json carry_assets).
    std::string carryAssets;
    {
        std::vector<std::string> notes;
        size_t placed = 0;
        std::vector<StockEditCarry> carry =
            opt.vehicles ? vehicleCarryList(map, *opt.vehicles, opt.stockModelZones, &notes, &placed) : std::vector<StockEditCarry>{};
        if (!opt.vehicles)
            for (const Entity* pe : points)
                if (const std::string* v = pe->value("vehicletype"); v && !v->empty()) ++placed;
        for (const std::string& n : notes) warn.push_back(n);
        if (placed) {
            bool brMap = worldspawn && worldspawn->value("iw8r_br");
            for (const Entity* pe : points)
                if (const std::string* tn = pe->value("targetname"); tn && *tn == "br_prematch_insertion_point") brMap = true;
            if (!opt.vehicles)
                warn.push_back(std::to_string(placed) + " vehicle spawn(s): the vehicle catalogue (data/asset_catalog/iw8_vehicles.tsv) was not "
                               "found, so no vehicle is carried into the map");
            else
                warn.push_back(std::to_string(placed) + " vehicle spawn(s); " + std::to_string(carry.size()) +
                               " stock asset(s) carried for them (definitions and models). Not seen in game");
            if (!brMap)
                warn.push_back("vehicle spawns are used by the Warzone (br) scripts only: make the map a Warzone map (Tools > Game Modes) or "
                               "the vehicles do not appear");
        }
        carry.insert(carry.end(), lootCarry.begin(), lootCarry.end());
        if (!carry.empty()) carryAssets = carryAssetsJson(carry);
    }
    // Dynamic props (6.15, research 41): `dynamic_prop` point entities (model, origin, angles, mass in kg, material
    // word for the physics material: wood, metal, plastic, glass, paper, rubber, brick...) become the collision json's
    // dynamic_models (the converter's clutter route, iw3_fastfile.cpp ReadDynamicEntities) with a physic/<preset> file
    // each. The model is a custom model or an exported stock model, as for misc_model. In-game physics NOT VERIFIED.
    std::string dynModelsJson;
    size_t dynModelCount = 0;
    std::map<std::string, std::string> physicPresets;  // preset name -> file text
    for (const Entity* pe : points) {
        if (lower(pe->classname()) != "dynamic_prop") continue;
        const std::string* mv = pe->value("model");
        const std::string modelName = mv ? *mv : std::string();
        std::string objPath;
        if (customModelPaths.count(modelName)) objPath = customModelPaths[modelName];
        else if (isCustomModelName(modelName)) objPath = findCustomModel(opt.modelRoots, modelName);
        std::string placedName = modelName;
        if (objPath.empty()) {
            placedName = stockBuild(modelName);
            if (placedName.empty()) {
                warn.push_back("dynamic_prop '" + modelName + "': not a custom model or an exported stock model; skipped");
                continue;
            }
        } else {
            customModelPaths[modelName] = objPath;
        }
        double angles[3] = {0, 0, 0};
        if (const std::string* av = pe->value("angles"); av && !parseVec3Text(*av, angles)) angles[0] = angles[1] = angles[2] = 0;
        double mass = 10;
        if (const std::string* ms = pe->value("mass")) {
            double v = 0;
            if (parseNumber(*ms, v) && v > 0 && v < 100000) mass = v;
            else warn.push_back("dynamic_prop '" + modelName + "' mass '" + *ms + "' is not a positive number: using 10");
        }
        std::string material = "plastic";
        if (const std::string* mt = pe->value("material"); mt && !mt->empty()) {
            material.clear();
            for (const char c : *mt) material += std::isalnum(static_cast<unsigned char>(c)) ? char(std::tolower(c)) : '_';
        }
        const std::string preset = "iw8r_" + material + "_" + std::to_string(int(std::lround(mass * 10)));
        physicPresets[preset] = "PHYSIC\\mass\\" + num(mass) + "\\sndAliasPrefix\\" + material;
        const auto q = anglesToDynQuaternion(angles[0], angles[1], angles[2]);
        const Vec3 o = pe->origin().value_or(Vec3{});
        dynModelsJson += std::string(dynModelCount ? "," : "") + "{\"type\":1,\"model\":" + quote(placedName) +
                         ",\"quaternion\":[" + num(q[0]) + "," + num(q[1]) + "," + num(q[2]) + "," + num(q[3]) +
                         "],\"origin\":" + vec3(o) + ",\"physics_preset\":" + quote(preset) + "}";
        ++dynModelCount;
    }
    if (dynModelCount)
        warn.push_back(std::to_string(dynModelCount) + " dynamic_prop(s) written as physics clutter (in-game behaviour not verified)");
    for (const auto& [name, n] : strippedMiscModels)
        warn.push_back("misc_model entit" + std::string(n == 1 ? "y" : "ies") + " with model '" + name + "' (" +
                       std::to_string(n) + "): not a custom model in the model folders; stock xmodel export is not supported yet, skipped");

    // Sky (doc 29, doc 33): x64-zt reads world.sky and throws on a non-empty name without images/<name>.dds
    // (iw3_render_assets.cpp 6186, LoadImage 1285-1340). With opt.skyDdsPath the sky image is converted to the cube the
    // converter reads (sky_image.h) and named in world.sky; without it the sky stays "" and a named sky is reported.
    std::string skyValue, skyCube, skyNote;
    if (worldspawn) {
        for (const char* key : {"skyname", "sky"}) {
            const std::string* sk = worldspawn->value(key);
            if (!sk || sk->empty()) continue;
            const std::string name = skyImageName(*sk);
            std::string src, serr;
            SkyConvertOptions so;
            so.faceSize = opt.skyFaceSize;
            SkyConvertInfo si;
            if (opt.skyDdsPath.empty()) {
                warn.push_back("worldspawn " + std::string(key) + " '" + *sk +
                               "' not written: no sky image was provided (the converter needs images/" + name +
                               ".dds as a cube map); world sky left empty");
            } else if (name.empty() || !readFile(opt.skyDdsPath, src)) {
                warn.push_back("sky '" + *sk + "': cannot read " + opt.skyDdsPath + "; world sky left empty");
            } else if (!skyCubemapFromDds(src, skyCube, so, &si, &serr)) {
                warn.push_back("sky '" + *sk + "': " + serr + "; world sky left empty");
                skyCube.clear();
            } else {
                skyValue = name;
                char note[256];
                if (si.passedThrough)
                    std::snprintf(note, sizeof note, "cube map %dx%d copied as is", si.faceSize, si.faceSize);
                else
                    std::snprintf(note, sizeof note, "%dx%d %s (dxgi %u) -> %d cube faces, exposure %g (mean %g, p99.5 %g)",
                                  si.sourceWidth, si.sourceHeight, si.hdr ? "HDR" : "LDR", si.sourceDxgiFormat, si.faceSize,
                                  si.exposure, si.meanLuminance, si.highLuminance);
                skyNote = note;
            }
            break;
        }
    }
    {
        KeyValues wkv = {{"classname", "worldspawn"},
                         {"sundirection", formatNumber(sunPitch) + " " + formatNumber(sunYaw) + " " + formatNumber(sunRoll)}};
        // North (owner playtest 2026-11-11: the game looked turned a quarter against the editor): Replay reads worldspawn
        // northyaw for its compass, and a missing key is 0 (north = +X). The editor's north is +Y, the top of the XY view,
        // so the build writes 90 unless the map sets its own. The converter then leaves the minimap unrotated.
        {
            const std::string* ny = worldspawn ? worldspawn->value("northyaw") : nullptr;
            wkv.push_back({"northyaw", ny && !ny->empty() ? *ny : std::string("90")});
        }
        if (haveSunColor)
            wkv.push_back({"suncolor", formatNumber(sunColor[0]) + " " + formatNumber(sunColor[1]) + " " + formatNumber(sunColor[2])});
        appendEntityText(ents, wkv);
        ++entityCount;
    }
    auto entityKeys = [&](const Entity& e) {
        KeyValues kvs;
        for (const auto& it : e.items) {
            const KeyValueLine* kv = it.kv();
            if (!kv) continue;
            const std::string name = kv->name();
            if (name.empty() || allDigits(name) || startsWith(name, "_id_")) {
                ++droppedKeys;
                continue;
            }
            std::string value = kv->value;
            if (value.find('"') != std::string::npos) {
                std::replace(value.begin(), value.end(), '"', '\'');
                ++quoteFixes;
            }
            kvs.push_back({name, value});
        }
        return kvs;
    };
    for (const Entity* pe : points) {
        const std::string cls = pe->classname();
        const std::string lcls = lower(cls);
        if (isLightClass(lcls)) continue;  // light entities become primary lights in comworld, not .ents (doc 24)
        if (lcls == "reflection_probe" || lcls == "misc_model") continue;  // world.json probes and models (doc 29)
        if (lcls == "sound_emitter") continue;  // played from the map script (map_script.h), not an entity
        if (lcls == "dynamic_prop") continue;   // written as a dynamic model in the collision json (6.15)
        if (lcls == "iw8r_volume") continue;    // written as a world.json volumetric box (5.7)
        if (lcls == "iw8r_decal") continue;     // written as a world.json decal volume (5.8)
        if (lcls == "scriptable") continue;     // written as a collision json scriptable instance (6.14)
        if (lcls == kLootPointClass) continue;  // written as a collision json loot point (13.5)
        KeyValues kvs = entityKeys(*pe);
        if (!acceptedPointClass(lcls)) {
            // The converter throws on classes outside its list (doc 15 §3.3), so they are left out of the build.
            ++unacceptedClasses[cls];
            continue;
        }
        if (lcls == "script_model" || lcls == "trigger_radius") {
            haveScriptModel |= lcls == "script_model";
            // Every stock Replay script_model carries footprint_mask_group "default" (doc 34: 205 of 205 in six MP
            // maps); add it when the mapper left it out.
            if (lcls == "script_model" &&
                std::none_of(kvs.begin(), kvs.end(), [](const auto& kv) { return kv.first == "footprint_mask_group"; }))
                kvs.push_back({"footprint_mask_group", "default"});
            // Every model value needs xmodel/<name>.json in the extraction (doc 15 §3.6); stock Replay models
            // are not exported by this route yet, so the key is dropped and the entity kept (doc 17).
            const auto m = std::find_if(kvs.begin(), kvs.end(), [](const auto& kv) { return kv.first == "model"; });
            if (m != kvs.end() && !m->second.empty() && m->second[0] != '*') {
                // A custom model from the mapper's model folders keeps its key (doc 22).
                const std::string modelName = m->second;
                std::string objPath;
                if (customModelPaths.count(modelName)) objPath = customModelPaths[modelName];
                else if (isCustomModelName(modelName)) objPath = findCustomModel(opt.modelRoots, modelName);
                if (!objPath.empty()) {
                    customModelPaths[modelName] = objPath;
                } else if (const std::string bn = stockBuild(modelName); !bn.empty()) {
                    m->second = bn;  // the exported copy of the stock model
                } else {
                    ++strippedModels[m->second];
                    kvs.erase(m);
                }
            }
        }
        if (startsWith(lcls, "mp_") && lcls.find("spawn") != std::string::npos) {
            ++spawns;
            if (lcls == "mp_tdm_spawn") ++tdmSpawns;
            if (lcls == "mp_tdm_spawn_allies_start") haveAllies = true;
            if (lcls == "mp_tdm_spawn_axis_start") haveAxis = true;
        }
        appendEntityText(ents, kvs);
        ++entityCount;
    }
    // Brush models: the entity's keys plus "model" "*N" (doc 15 §3.5; maps.cpp brushModelIndex).
    for (size_t bi = 0; bi < brushModels.size(); ++bi) {
        KeyValues kvs = entityKeys(map.entities[brushModels[bi].entity]);
        kvs.erase(std::remove_if(kvs.begin(), kvs.end(), [](const auto& kv) { return kv.first == "model"; }), kvs.end());
        kvs.push_back({"model", "*" + std::to_string(bi + 1)});
        appendEntityText(ents, kvs);
        ++entityCount;
    }
    for (const auto& [cls, n] : unacceptedClasses)
        warn.push_back(std::to_string(n) + " entit" + (n == 1 ? "y" : "ies") + " of class '" + cls +
                       "' outside the IW8 accepted list (doc 15 §3.3): left out of the build" +
                       (cls == "reflection_probe" ? " (the reflection probe comes from world.json)" : ""));
    for (const auto& [model, n] : strippedModels)
        warn.push_back("model '" + model + "' on " + std::to_string(n) +
                       " entit" + (n == 1 ? "y" : "ies") + ": stock xmodels are not exported by the x64-zt route yet, key left out");
    if (droppedKeys) warn.push_back(std::to_string(droppedKeys) + " key(s) without a resolvable name dropped from .ents");
    if (quoteFixes) warn.push_back(std::to_string(quoteFixes) + " value(s) with a quote character had it replaced by an apostrophe");
    {
        // Minimap corners (docs/research/27): exactly two script_origin "minimap_corner" give the compass rectangle.
        // Mapper-placed corners are kept; otherwise two are added from the geometry bounds.
        size_t corners = 0;
        for (const Entity* pe : points)
            if (lower(pe->classname()) == "script_origin")
                if (const std::string* tn = pe->value("targetname"); tn && *tn == "minimap_corner") ++corners;
        if (corners == 0 && g.bounds.valid()) {
            appendEntityText(ents, {{"classname", "script_origin"}, {"targetname", "minimap_corner"},
                                    {"origin", formatNumber(g.bounds.mins.x) + " " + formatNumber(g.bounds.mins.y) + " 0"}});
            appendEntityText(ents, {{"classname", "script_origin"}, {"targetname", "minimap_corner"},
                                    {"origin", formatNumber(g.bounds.maxs.x) + " " + formatNumber(g.bounds.maxs.y) + " 0"}});
            entityCount += 2;
        } else if (corners != 2 && corners != 0) {
            warn.push_back(std::to_string(corners) + " minimap_corner script_origins (exactly two are needed for the compass)");
        }
    }
    if (!haveScriptModel) {
        // doc 15 §7: validateIw8EntityString requires a script_model; the placeholder names no model.
        appendEntityText(ents, {{"classname", "script_model"}, {"origin", "0 0 16"}, {"angles", "0 0 0"}});
        ++entityCount;
        warn.push_back("no script_model entity: placeholder added (validateIw8EntityString requires one, doc 15 §3.1)");
    }
    if (tdmSpawns < 2) warn.push_back("fewer than 2 mp_tdm_spawn entities (doc 15 §3.4 needs two for a playable match)");
    if (!haveAllies) warn.push_back("no mp_tdm_spawn_allies_start entity (doc 15 §3.4)");
    if (!haveAxis) warn.push_back("no mp_tdm_spawn_axis_start entity (doc 15 §3.4)");

    // ---- 6. JSON documents (doc 15 §2.1, §2.3, §2.4, §7).
    const std::string idxAll = indexList(worldSurfaceCount);  // brush model surfaces stay out of the cell
    // Custom materials used by surfaces, by library name (doc 21). Every other surface gets the grey material.
    std::map<std::string, const CustomMaterial*> customUsed;
    std::map<std::string, const CustomMaterial*> decalCustom;   // decal-form twins (<name>_dcl) of library materials
    const auto materialFor = [&](const std::string& editorMaterial) -> std::string {
        if (opt.library)
            if (const CustomMaterial* cm = opt.library->find(editorMaterial)) {
                const std::string outName = cm->buildName.empty() ? cm->name : cm->buildName;
                customUsed[outName] = cm;
                return outName;
            }
        return opt.greyMaterial;
    };
    // 8.2: custom models that authored effects spawn (model elements) join the extraction like placed ones.
    {
        size_t skipped = 0;
        for (const FxPlacement& p : readFxPlacements(map, &skipped)) {
            for (const std::string& r : opt.fxRoots) {
                std::string text;
                AuthoredFx afx;
                if (!readFile((fs::path(r) / (p.effect + ".iw8fx.json")).string(), text) || !parseAuthoredFx(text, afx)) continue;
                for (const AuthoredFxElement& e : afx.elements) {
                    if (e.kind != "model" || customModelPaths.count(e.model)) continue;
                    const std::string obj = findCustomModel(opt.modelRoots, e.model);
                    if (obj.empty())
                        warn.push_back("effect '" + p.effect + "': model '" + e.model + "' is not in the model folders; that element "
                                       "has no model");
                    else
                        customModelPaths[e.model] = obj;
                }
                break;
            }
        }
    }
    // Custom models: import now so their surface materials join the custom material set below.
    std::vector<ImportedModel> customModels;
    for (const auto& [modelName, objPath] : customModelPaths) {
        ImportedModel im;
        std::string merr;
        if (!importModelFile(objPath, im, &merr)) return fail("custom model " + modelName + ": " + merr);
        im.name = modelName;  // the xmodel name is the entity's model key
        if (opt.customModelVariants)
            if (const auto v = opt.customModelVariants->find(modelName); v != opt.customModelVariants->end())
                for (auto& mesh : im.meshes) {
                    if (const auto s = v->second.bySlot.find(lower(mesh.material)); s != v->second.bySlot.end()) mesh.material = s->second;
                    else if (!v->second.material.empty()) mesh.material = v->second.material;
                }
        for (auto& mesh : im.meshes) mesh.material = materialFor(mesh.material);
        for (const auto& w : im.warnings) warn.push_back("model " + modelName + ": " + w);
        customModels.push_back(std::move(im));
    }
    std::map<std::string, int> stockModelGreyMeshes;
    // Stock models exported by the editor: already in map units, so undo the custom-model scale applied on write.
    for (const auto& [buildName, src] : stockModelsUsed) {
        ImportedModel im = *src;
        im.name = buildName;
        const double inv = opt.modelScale > 0 ? 1.0 / opt.modelScale : 1.0;
        im.bounds = AABB{};
        for (auto& mesh : im.meshes) {
            for (auto& p : mesh.pos) {
                p = p * inv;
                im.bounds.add(p);
            }
            // The model writer flips v (OBJ, v = 0 at the bottom); ACTS/glTF UVs are already top-left like the game's.
            for (auto& t : mesh.uv) t[1] = 1.0 - t[1];
            const std::string built = materialFor(mesh.material);
            if (built == opt.greyMaterial && !mesh.material.empty()) ++stockModelGreyMeshes[mesh.material];
            mesh.material = built;
        }
        customModels.push_back(std::move(im));
    }
    if (!stockModelsUsed.empty())
        warn.push_back(std::to_string(stockModelsUsed.size()) + " stock model(s) built as copies from the ACTS export (iw8r_stock_*), textured with their stock colour images");
    for (const auto& [mat, n] : stockModelGreyMeshes)
        warn.push_back("stock model material '" + mat + "' has no cached colour image: grey this time (" + std::to_string(n) + " mesh(es))");
    std::string surfJson;
    for (size_t si = 0; si < surfaces.size(); ++si) {
        const RenderSurface& s = surfaces[si];
        if (si) surfJson += ",";
        surfJson += "{\"material\":" + quote(materialFor(s.editorMaterial)) + ",\"reflection_probe\":" +
                    std::to_string(probeFor(surfaceCentre(s))) + ",\"indices\":[";
        for (size_t i = 0; i < s.idx.size(); ++i) {
            if (i) surfJson += ",";
            surfJson += std::to_string(s.idx[i]);
        }
        surfJson += "],\"vertices\":[";
        for (size_t vi = 0; vi < s.verts.size(); ++vi) {
            const RenderVertex& v = s.verts[vi];
            if (vi) surfJson += ",";
            surfJson += "{\"position\":" + vec3(v.pos) + ",\"normal\":" + vec3(v.normal) + ",\"tangent\":" + vec3(v.tangent) +
                        ",\"binormal_sign\":1,\"uv\":[" + num(v.u) + "," + num(v.v) + "],\"lightmap_uv\":[0,0]" +
                        (v.hasColor ? ",\"color\":[" + std::to_string(v.color[0]) + "," + std::to_string(v.color[1]) + "," +
                                          std::to_string(v.color[2]) + "," + std::to_string(v.color[3]) + "]"
                                    : std::string()) + "}";
        }
        surfJson += "]}";
    }

    // Primary lights after the sun (doc 24): index 0 empty, index 1 the sun, then one slot per light entity.
    std::vector<std::string> iesProfiles;
    const std::vector<std::string> localLights = localPrimaryLights(map, points, warn, opt.mapDir, iesProfiles);
    if (!iesProfiles.empty()) warn.push_back(std::to_string(iesProfiles.size()) + " IES profile(s) written for spot lights (iw8r_ies)");
    const size_t primaryLightCount = kFirstLocalLight + localLights.size();

    const std::string boundsJson = aabbJson(bounds);
    // The one visibility cell reaches above and around the map (research 75, owner playtest 2026-11-11: the UAV model was
    // invisible and players were invisible from the Chopper Gunner; both are outside a cell that ends at the map's
    // geometry). worldspawn iw8r_airspace "<up> [out]" sets how far (default 6144 up, 2048 out; "0" keeps the tight
    // cell). The world bounds, the light grid and the surface tree keep the map's own size.
    AABB cellBounds = bounds;
    {
        double up = 6144, outward = 2048;
        if (worldspawn)
            if (const std::string* as = worldspawn->value("iw8r_airspace"); as && !as->empty()) {
                double a2 = 0, b2 = 0;
                const int got = std::sscanf(as->c_str(), "%lf %lf", &a2, &b2);
                if (got >= 1 && a2 >= 0) {
                    up = a2;
                    outward = got >= 2 && b2 >= 0 ? b2 : (a2 > 0 ? 2048 : 0);
                } else {
                    warn.push_back("worldspawn iw8r_airspace '" + *as + "' not understood (<up> [out]); using 6144 2048");
                }
            }
        cellBounds.maxs.z += up;
        cellBounds.mins.x -= outward;
        cellBounds.mins.y -= outward;
        cellBounds.maxs.x += outward;
        cellBounds.maxs.y += outward;
    }
    const std::string cellBoundsJson = aabbJson(cellBounds);
    // brush_models must own every surface and match the collision submodel count (iw3_fastfile.cpp
    // ReadBrushModels, AddReplayTemplateEntities); it is left out when the map has no brush entities.
    std::string brushModelsJson;
    if (!brushModels.empty()) {
        brushModelsJson = ",\"brush_models\":[{\"bounds\":" + boundsJson + ",\"start\":0,\"count\":" + std::to_string(worldSurfaceCount) + "}";
        for (const auto& bm : brushModels)
            brushModelsJson += ",{\"bounds\":" + aabbJson(bm.bounds) + ",\"start\":" + std::to_string(bm.surfaceCount ? bm.surfaceStart : 0) +
                               ",\"count\":" + std::to_string(bm.surfaceCount) + "}";
        brushModelsJson += "]";
    }
    // Probe and model lists (doc 29): one cell holds every probe and placement.
    std::string probeJson, probeIndexJson, modelJson, modelIndexJson;
    for (size_t pi = 0; pi < probes.size(); ++pi) {
        const std::string image = pi == 0 ? id + "_probe" : id + "_probe" + std::to_string(pi);
        // Probe pi >= 1 is entity probe pi - 1; an authored volume is read by the x64-zt fork (abf4bc6f+).
        std::string volumeJson;
        if (pi >= 1 && pi - 1 < probeVolumes.size() && probeVolumes[pi - 1])
            volumeJson = ",\"volume\":[" + vec3(probeVolumes[pi - 1]->mins) + "," + vec3(probeVolumes[pi - 1]->maxs) + "]";
        if (pi >= 1 && pi - 1 < probeBlendJson.size()) volumeJson += probeBlendJson[pi - 1];
        probeJson += std::string(pi ? "," : "") + "{\"image\":" + quote(image) + ",\"origin\":" + vec3(probes[pi]) + volumeJson + "}";
        probeIndexJson += std::string(pi ? "," : "") + std::to_string(pi);
    }
    for (size_t mi = 0; mi < placements.size(); ++mi) {
        const ModelPlacementOut& p = placements[mi];
        modelJson += std::string(mi ? "," : "") + "{\"model\":" + quote(p.model) + ",\"origin\":" + vec3(p.origin) +
                     ",\"axis\":[" + vec3(p.axis[0]) + "," + vec3(p.axis[1]) + "," + vec3(p.axis[2]) +
                     "],\"scale\":" + num(p.scale) + ",\"reflection_probe\":" + std::to_string(probeFor(p.origin)) +
                     (p.lodScale != 1.0 ? ",\"lod_scale\":" + num(p.lodScale) : std::string()) +
                     (p.noShadow ? std::string(",\"shadow_lights\":[],\"no_sun_shadow\":true") : std::string()) +
                     // The converter reads the lighting block only with mins / maxs (iw3_fastfile.cpp:3799); a ground-lit
                     // placement never samples the grid, so the origin as a zero box is enough.
                     (p.groundLit ? ",\"mins\":" + vec3(p.origin) + ",\"maxs\":" + vec3(p.origin) +
                                        ",\"model_flags\":1,\"ground_lighting\":" + std::to_string(p.groundColor)
                                  : std::string()) + "}";
        modelIndexJson += std::string(mi ? "," : "") + std::to_string(mi);
    }
    const std::string modelsKey = placements.empty() ? std::string() : ",\"models\":[" + modelJson + "]";
    // Colour grading LUT (doc 31 section 3.4): worldspawn iw8r_lut (.cube) -> extracted/grading/<id>_clut.rgba.
    std::string gradingKey;
    std::string lutBytes;
    if (worldspawn)
        if (const std::string* lp = worldspawn->value(kLutKey); lp && !lp->empty()) {
            fs::path lutPath(*lp);
            if (lutPath.is_relative() && !opt.mapDir.empty()) lutPath = fs::path(opt.mapDir) / lutPath;
            std::string text, lerr;
            int lutSize = 0;
            std::vector<uint8_t> lut;
            if (!readFile(lutPath.string(), text))
                warn.push_back("iw8r_lut '" + *lp + "': file not found; no grading LUT");
            else if (!parseCubeLut(text, lutSize, lut, &lerr))
                warn.push_back("iw8r_lut '" + *lp + "': " + lerr + "; no grading LUT");
            else {
                lutBytes.assign(lut.begin(), lut.end());
                gradingKey = ",\"grading_clut\":{\"size\":" + std::to_string(lutSize) + ",\"file\":\"grading/" + id + "_clut.rgba\"}";
                if (const std::string* fv = worldspawn->value(kFilmKey); fv && !fv->empty())
                    warn.push_back("iw8r_lut and iw8r_film are both set: the converter uses the film and ignores the LUT (doc 31)");
            }
        }
    // Volumetric boxes (5.7, research 46): `iw8r_volume` point entities (origin, mins / maxs relative to it, default
    // +-128; angles; density >= 0, default 0.2; falloff > 0, default 64; color; height_fade; anisotropy) become
    // world.json `volumetrics` (iw3_render_assets.cpp 5682, validated by replay_render.cpp 1406-1465). Bad values are
    // reported and the volume left out, so it cannot stop the build. Units and look NOT VERIFIED in game.
    std::string volumeJson;
    size_t volumeCount = 0;
    for (const Entity* pe : points) {
        if (lower(pe->classname()) != "iw8r_volume") continue;
        const std::string where = "iw8r_volume entity " + std::to_string(pe - map.entities.data());
        if (volumeCount >= 256) {
            warn.push_back(where + ": more than 256 volumes; left out");
            continue;
        }
        double mn[3] = {-128, -128, -128}, mx[3] = {128, 128, 128};
        if (const std::string* sm = pe->value("mins"); sm && !parseVec3Text(*sm, mn)) warn.push_back(where + ": mins not parsed; using -128");
        if (const std::string* sx = pe->value("maxs"); sx && !parseVec3Text(*sx, mx)) warn.push_back(where + ": maxs not parsed; using 128");
        if (!(mx[0] > mn[0] && mx[1] > mn[1] && mx[2] > mn[2])) {
            warn.push_back(where + ": mins / maxs have no extent; left out");
            continue;
        }
        const auto number = [&](const char* key, double def, double lo, bool strictLo) {
            const std::string* v = pe->value(key);
            double d = 0;
            if (!v || v->empty()) return def;
            if (!parseNumber(*v, d) || !std::isfinite(d) || (strictLo ? d <= lo : d < lo)) {
                warn.push_back(where + ": " + key + " '" + *v + "' out of range; using " + num(def));
                return def;
            }
            return d;
        };
        const double density = number("density", 0.2, 0, false), falloff = number("falloff", 64, 0, true);
        const double heightFade = number("height_fade", 0, -1e9, false), anisotropy = number("anisotropy", 0, -1, false);
        double color[3] = {1, 1, 1};
        if (const std::string* cv = pe->value("color"); cv && !parseVec3Text(*cv, color)) color[0] = color[1] = color[2] = 1;
        double ang[3] = {0, 0, 0};
        if (const std::string* av = pe->value("angles"); av && !parseVec3Text(*av, ang)) ang[0] = ang[1] = ang[2] = 0;
        const auto ax = anglesToAxis(ang[0], ang[1], ang[2]);
        const Vec3 o = pe->origin().value_or(Vec3{});
        const Vec3 localCenter{(mn[0] + mx[0]) / 2, (mn[1] + mx[1]) / 2, (mn[2] + mx[2]) / 2};
        const Vec3 center = o + ax[0] * localCenter.x + ax[1] * localCenter.y + ax[2] * localCenter.z;
        volumeJson += std::string(volumeCount ? "," : "") + "{\"flags\":0,\"center\":" + vec3(center) + ",\"axes\":[" +
                      vec3(ax[0]) + "," + vec3(ax[1]) + "," + vec3(ax[2]) + "],\"half_size\":" +
                      vec3raw((mx[0] - mn[0]) / 2, (mx[1] - mn[1]) / 2, (mx[2] - mn[2]) / 2) + ",\"color\":" +
                      vec3raw(std::max(0.0, color[0]), std::max(0.0, color[1]), std::max(0.0, color[2])) + ",\"density\":" + num(density) +
                      ",\"falloff\":" + num(falloff) + ",\"height_fade\":" + num(heightFade) + ",\"anisotropy\":" + num(anisotropy) + "}";
        ++volumeCount;
    }
    if (volumeCount) warn.push_back(std::to_string(volumeCount) + " volumetric box(es) written (iw8r_volume; in-game look not verified)");
    const std::string volumesKey = volumeCount ? ",\"volumetrics\":[" + volumeJson + "]" : std::string();
    // Decal volumes (5.8, research 80 section 6): `iw8r_decal` point entities (origin, angles: the forward axis is the
    // projection direction and points into the surface; size "depth width height"; material; optional opacity,
    // feather, draw_order, uv_rotate, uv_scale, uv_shift) become world.json `decal_volumes`. A bad entity is reported
    // and left out, so it cannot stop the build. A converter without the decal producer ignores the key.
    std::string decalJson;
    size_t decalCount = 0;
    std::set<std::string> decalMaterials;
    for (const Entity* pe : points) {
        if (lower(pe->classname()) != "iw8r_decal") continue;
        const std::string where = "iw8r_decal entity " + std::to_string(pe - map.entities.data());
        const std::string* mv = pe->value("material");
        if (!mv || mv->empty()) {
            warn.push_back(where + ": no material; left out");
            continue;
        }
        double size[3] = {8, 64, 64};
        if (const std::string* sv = pe->value("size"); sv && !parseVec3Text(*sv, size)) {
            warn.push_back(where + ": size '" + *sv + "' not parsed (depth width height); left out");
            continue;
        }
        if (!(size[0] > 0 && size[1] > 0 && size[2] > 0) || !std::isfinite(size[0] + size[1] + size[2])) {
            warn.push_back(where + ": size must be three numbers above 0; left out");
            continue;
        }
        const auto ranged = [&](const char* key, double def, double lo, double hi) {
            const std::string* v = pe->value(key);
            double d = 0;
            if (!v || v->empty()) return def;
            if (!parseNumber(*v, d) || !std::isfinite(d) || d < lo || d > hi) {
                warn.push_back(where + ": " + key + " '" + *v + "' out of range (" + num(lo) + " to " + num(hi) + "); using " + num(def));
                return def;
            }
            return d;
        };
        const double opacity = ranged("opacity", 1, 0, 1), feather = ranged("feather", 0, 0, 1);
        const int drawOrder = int(std::lround(ranged("draw_order", 0, -4, 3)));
        const double uvRotate = ranged("uv_rotate", 0, -360, 360);
        const auto pair = [&](const char* key, double a, double b, bool positive) {
            std::array<double, 2> out{a, b};
            const std::string* v = pe->value(key);
            if (!v || v->empty()) return out;
            double x = 0, y = 0;
            if (std::sscanf(v->c_str(), "%lf %lf", &x, &y) != 2 || !std::isfinite(x + y) || (positive && !(x > 0 && y > 0))) {
                warn.push_back(where + ": " + key + " '" + *v + "' not understood (two numbers); using " + num(a) + " " + num(b));
                return out;
            }
            return std::array<double, 2>{x, y};
        };
        const auto uvScale = pair("uv_scale", 1, 1, true), uvShift = pair("uv_shift", 0, 0, false);
        // A material of the custom library is built with the map (its picture's alpha is the decal's shape); any other
        // name is passed through as a stock material the game already has loaded.
        std::string material = *mv;
        if (opt.library)
            if (const CustomMaterial* cm = opt.library->find(material)) {
                material = (cm->buildName.empty() ? cm->name : cm->buildName) + "_dcl";
                decalCustom[material] = cm;
            }
        if (!decalMaterials.count(material) && decalMaterials.size() >= 2047) {
            warn.push_back(where + ": more than 2047 different decal materials; left out");
            continue;
        }
        decalMaterials.insert(material);
        double ang[3] = {0, 0, 0};
        if (const std::string* av = pe->value("angles"); av && !parseVec3Text(*av, ang)) ang[0] = ang[1] = ang[2] = 0;
        const auto ax = anglesToAxis(ang[0], ang[1], ang[2]);
        const Vec3 o = pe->origin().value_or(Vec3{});
        const double rad = uvRotate * 3.14159265358979323846 / 180.0, c = std::cos(rad), s = std::sin(rad);
        decalJson += std::string(decalCount ? "," : "") + "{\"center\":" + vec3(o) + ",\"axes\":[" + vec3(ax[0]) + "," + vec3(ax[1]) + "," +
                     vec3(ax[2]) + "],\"half_size\":" + vec3raw(size[0] / 2, size[1] / 2, size[2] / 2) + ",\"material\":" + quote(material) +
                     ",\"opacity\":" + num(opacity) + ",\"feather\":" + num(feather) + ",\"draw_order\":" + std::to_string(drawOrder) +
                     ",\"uv_matrix\":[" + num(c * uvScale[0]) + "," + num(-s * uvScale[1]) + "," + num(s * uvScale[0]) + "," +
                     num(c * uvScale[1]) + "],\"uv_offset\":[" + num(uvShift[0]) + "," + num(uvShift[1]) + "]}";
        ++decalCount;
    }
    if (decalCount)
        warn.push_back(std::to_string(decalCount) + " decal volume(s) written (iw8r_decal; needs a converter with the decal producer, " +
                       "research 80 and 82; in-game look not verified)");
    const std::string decalsKey = decalCount ? ",\"decal_volumes\":[" + decalJson + "]" : std::string();
    const std::string world =
        "{\"schema\":1,\"name\":" + quote(mapName) + ",\"sky\":" + quote(skyValue) + ",\"bounds\":" + boundsJson +
        ",\"sun\":{\"ambient\":[0.5,0.5,0.5],\"angles\":" + vec3raw(sunPitch, sunYaw, sunRoll) +
        ",\"color\":" + vec3raw(sunColor[0], sunColor[1], sunColor[2]) + ",\"intensity\":1}" +
        ",\"sun_primary_light_index\":1,\"primary_light_count\":" + std::to_string(primaryLightCount) +
        ",\"reflection_probes\":[" + probeJson + "]" + modelsKey + gradingKey + volumesKey + decalsKey +
        ",\"dpvs\":{\"planes\":[],\"nodes\":[1],\"cells\":[{\"bounds\":" + cellBoundsJson +
        ",\"portals\":[],\"reflection_probes\":[" + probeIndexJson + "],\"surfaces\":[" + idxAll + "],\"models\":[" +
        modelIndexJson + "],\"trees\":[{\"bounds\":" + boundsJson + ",\"surfaces\":[" + idxAll + "],\"models\":[" +
        modelIndexJson + "]}]}]}" + brushModelsJson + ",\"surfaces\":[" + surfJson + "]}";

    // Submodel 0 is the world (its brushes are those no leaf node claims); submodel N owns leaf node N, a
    // leaf listing its brushes (iw3_fastfile.cpp CollectLeafBrushes: count > 0 returns the list).
    std::string submodels = "{\"leaf\":0,\"mins\":" + vec3(bounds.mins) + ",\"maxs\":" + vec3(bounds.maxs) + "}";
    std::string leaves = "{\"brushes\":[],\"children\":[0,0],\"count\":0}";
    for (size_t bi = 0; bi < brushModels.size(); ++bi) {
        const BrushModelOut& bm = brushModels[bi];
        submodels += ",{\"leaf\":" + std::to_string(bi + 1) + ",\"mins\":" + vec3(bm.bounds.mins) + ",\"maxs\":" + vec3(bm.bounds.maxs) + "}";
        leaves += ",{\"brushes\":[";
        for (size_t k = 0; k < bm.brushes.size(); ++k) leaves += (k ? "," : "") + std::to_string(bm.brushes[k]);
        leaves += "],\"children\":[0,0],\"count\":" + std::to_string(bm.brushes.size()) + "}";
    }
    // The collision layout (14.1, research 90): one piece, or the game's streamed tiles for a big map. The converter
    // cuts the tiles itself; the editor only asks for them.
    bool collisionTiles = false;
    {
        CollisionLayout wanted = opt.collisionLayout;
        if (worldspawn)
            if (const std::string* key = worldspawn->value("iw8r_collision"); key && !key->empty()) {
                bool known = true;
                const CollisionLayout fromKey = parseCollisionLayout(*key, &known);
                if (known) wanted = fromKey;
                else warn.push_back("worldspawn iw8r_collision '" + *key + "' not understood (auto, one, tiles); using " + collisionLayoutName(wanted));
            }
        const CollisionLayoutChoice choice = chooseCollisionLayout(wanted, bounds);
        collisionTiles = choice.tiles;
        for (const std::string& n : choice.notes) warn.push_back(n);
    }
    std::string col = "{\"schema\":1,\"name\":" + quote(mapName) + ",\"submodel_count\":" + std::to_string(brushModels.size() + 1) +
                      ",\"submodels\":[" + submodels + "],\"leaf_brush_nodes\":[" + leaves + "],\"brushes\":[";
    for (size_t bi = 0; bi < g.brushes.size(); ++bi) {
        const CollisionBrush& b = g.brushes[bi];
        if (bi) col += ",";
        col += "{\"contents\":" + std::to_string(b.contents) + (b.surfaceType > 0 ? ",\"surface_type\":" + std::to_string(b.surfaceType) : std::string()) +
               ",\"mins\":" + vec3(b.bounds.mins) + ",\"maxs\":" + vec3(b.bounds.maxs) + ",\"planes\":[";
        for (size_t pi = 0; pi < b.planes.size(); ++pi) {
            const Plane& pl = b.planes[pi];
            if (pi) col += ",";
            col += "[" + num(pl.n.x) + "," + num(pl.n.y) + "," + num(pl.n.z) + "," + num(pl.d) + "]";
        }
        col += "]";
        if (b.ladder) {
            // Every brush plane: the converter keeps only the vertical faces that are (nearly) the widest.
            col += ",\"ladder_planes\":[";
            for (size_t pi = 0; pi < b.planes.size(); ++pi) {
                const Plane& pl = b.planes[pi];
                if (pi) col += ",";
                col += "[" + num(pl.n.x) + "," + num(pl.n.y) + "," + num(pl.n.z) + "," + num(pl.d) + "]";
            }
            col += "]";
        }
        col += "}";
    }
    // triangle_contents is read with at() (iw3_fastfile.cpp:11516) and its schema tag checked at :7351 (doc 17 §4).
    col += "],\"vertices\":[],\"triangles\":[],\"triangle_contents\":[],\"triangle_contents_schema\":\"material-partitions-v1\","
           "\"dynamic_model_count\":" + std::to_string(dynModelCount) + ",\"dynamic_brush_count\":0,\"dynamic_models\":[" +
           dynModelsJson + "],\"dynamic_brushes\":[]" +
           (scriptableCount ? ",\"scriptable_instances\":[" + scriptablesJson + "]" : std::string()) +
           (lootCount ? ",\"loot_points\":[" + lootJson + "],\"loot_zone\":\"" + lootZone + "\"" : std::string()) +
           (lootBoxes.empty() ? std::string() : ",\"loot_boxes\":" + lootBoxes) +
           (collisionTiles ? ",\"collision_tiles\":true" : "") +
           (carryAssets.empty() ? std::string() : ",\"carry_assets\":" + carryAssets) + "}";

    std::string comworld =
        "{\"schema\":1,\"name\":" + quote(mapName) +
        ",\"in_use\":true,\"primary_lights\":["
        "{\"type\":0,\"exponent\":0,\"can_use_shadow_map\":false,\"color\":[0,0,0],\"direction\":[0,0,0],\"origin\":[0,0,0],"
        "\"radius\":0,\"cos_half_fov_outer\":0,\"cos_half_fov_inner\":0,\"rotation_limit\":0,\"translation_limit\":0,"
        "\"definition\":\"\"},"
        "{\"type\":1,\"exponent\":0,\"can_use_shadow_map\":false,\"color\":" +
        vec3raw(sunColor[0], sunColor[1], sunColor[2]) + ",\"direction\":" + vec3(sunDir) +
        ",\"origin\":[0,0,0],\"radius\":0,\"cos_half_fov_outer\":0,\"cos_half_fov_inner\":0,\"rotation_limit\":0,"
        "\"translation_limit\":0,\"definition\":\"\"}";
    std::string localJson;
    for (const std::string& l : localLights) localJson += "," + l;
    comworld += localJson + "]";
    if (!iesProfiles.empty()) {
        comworld += ",\"ies_profiles\":[";
        for (size_t i = 0; i < iesProfiles.size(); ++i) comworld += (i ? "," : "") + iesProfiles[i];
        comworld += "]";
    }
    comworld += "}";

    // ---- 7. Write the folder. Nothing above touched the disk.
    const fs::path mapDir = root / "extracted" / "maps" / "mp";
    std::string werr;
    // Custom material files: every DDS is encoded before anything is written, so a failed conversion writes nothing.
    std::vector<std::pair<fs::path, std::string>> customFiles;
    for (const auto& [name, cm] : customUsed) {
        const std::string stem = customImageStem(name);
        struct ImageJob {
            const std::string* source;
            const char* suffix;
            bool normal;
            bool keepAlpha;
        };
        const ImageJob jobs[] = {{&cm->colorImage, "_c", false, cm->alphaTest || cm->blend},
                                 {&cm->normalImage, "_n", true, false},
                                 {&cm->specImage, "_s", false, true}};
        for (const ImageJob& job : jobs) {
            if (job.source->empty()) continue;
            std::vector<uint8_t> dds;
            std::string imageErr;
            if (!encodeDdsFromImage(*job.source, job.normal, job.keepAlpha, dds, &imageErr)) return fail(imageErr);
            customFiles.push_back({root / "extracted" / "images" / (stem + job.suffix + ".dds"),
                                   std::string(dds.begin(), dds.end())});
        }
        customFiles.push_back({root / "extracted" / "materials" / (name + ".json"), customMaterialJson(*cm, name) + "\n"});
    }
    for (const auto& [name, cm] : decalCustom) {
        // The picture's alpha is the decal's shape, so it is always kept.
        const std::string stem = customImageStem(name);
        const std::pair<const std::string*, const char*> jobs[] = {{&cm->colorImage, "_c"}, {&cm->normalImage, "_n"}, {&cm->specImage, "_s"}};
        for (const auto& [source, suffix] : jobs) {
            if (source->empty()) continue;
            std::vector<uint8_t> dds;
            std::string imageErr;
            if (!encodeDdsFromImage(*source, std::string(suffix) == "_n", true, dds, &imageErr)) return fail(imageErr);
            customFiles.push_back({root / "extracted" / "images" / (stem + suffix + ".dds"), std::string(dds.begin(), dds.end())});
        }
        customFiles.push_back({root / "extracted" / "materials" / (name + ".json"), customDecalMaterialJson(*cm, name) + "\n"});
    }
    if (!skyValue.empty()) customFiles.push_back({root / "extracted" / "images" / (skyValue + ".dds"), skyCube});
    if (!opt.loadscreenImagePath.empty()) {
        // 1.17: the converter makes the map-select preview from an image named loadscreen_<map> (opaque).
        std::vector<uint8_t> dds;
        std::string imageErr;
        if (encodeDdsFromImage(opt.loadscreenImagePath, false, false, dds, &imageErr)) {
            customFiles.push_back({root / "extracted" / "images" / ("loadscreen_" + id + ".dds"), std::string(dds.begin(), dds.end())});
            rep.loadscreen = true;
        } else {
            rep.warnings.push_back("loadscreen image " + opt.loadscreenImagePath + " not used: " + imageErr);
        }
    }
    for (const auto& [name, text] : physicPresets) customFiles.push_back({root / "extracted" / "physic" / name, text});
    // One cube per probe (doc 29), named <id>_probe<N> (the image of probe N): the editor's capture when it was taken at
    // this probe's position (5.5), else neutral grey.
    size_t capturedProbes = 0;
    const auto probeCube = [&](size_t pi) {
        if (!opt.probeCaptureDir.empty()) {
            const fs::path base = fs::path(opt.probeCaptureDir) / ("probe" + std::to_string(pi));
            // Probe 0 is the world default at the geometry centre, which the editor only approximates.
            const double tol = pi == 0 ? 512.0 : 1.0;
            std::string pos, rgba;
            double p[3];
            if (readFile(base.string() + ".pos", pos) && parseVec3Text(pos, p) && readFile(base.string() + ".rgba", rgba) &&
                rgba.size() == size_t(6 * kProbeSize * kProbeSize * 4) && std::fabs(p[0] - probes[pi].x) <= tol &&
                std::fabs(p[1] - probes[pi].y) <= tol && std::fabs(p[2] - probes[pi].z) <= tol) {
                const std::vector<uint8_t> faces(rgba.begin(), rgba.end());
                ++capturedProbes;
                return buildProbeDds(&faces);
            }
        }
        return buildProbeDds();
    };
    for (size_t pi = 1; pi < probes.size(); ++pi)
        customFiles.push_back({root / "extracted" / "images" / (id + "_probe" + std::to_string(pi) + ".dds"), probeCube(pi)});
    customFiles.push_back({root / "extracted" / "images" / (id + "_probe.dds"), probeCube(0)});
    if (!opt.probeCaptureDir.empty())
        warn.push_back(std::to_string(capturedProbes) + " of " + std::to_string(probes.size()) +
                       " reflection probe(s) use the editor capture (the rest are neutral grey; capture again after moving probes)");
    // A local light with definition light_point_linear needs the native falloff definition beside the extraction:
    // iw3_fastfile.cpp:11074-11090 (the converter's localLinearLightDef check) reads extracted/lights/light_point_linear and requires these exact bytes
    // (a type byte 'b', "falloff_linear", NUL). Written only when a light uses it (doc 24 section 3).
    // Atmosphere (doc 31): worldspawn iw8r_fog -> maps/mp/<id>.gsc setExpFog, iw8r_film -> vision/<id>.vision r_film*.
    if (worldspawn) {
        std::string awarn;
        const Atmosphere atmo = readAtmosphere(*worldspawn, &awarn);
        for (size_t a = 0, b; a < awarn.size(); a = b + 1) {
            b = awarn.find('\n', a);
            if (b == std::string::npos) b = awarn.size();
            if (b > a) warn.push_back(awarn.substr(a, b - a));
        }
        // Map script: fog + effect placements (map_script.h).
        size_t fxSkipped = 0;
        std::vector<FxPlacement> fx = readFxPlacements(map, &fxSkipped);
        if (fxSkipped) warn.push_back(std::to_string(fxSkipped) + " fx_origin entit" + (fxSkipped == 1 ? "y has" : "ies have") + " no usable fx key (a vfx/... name); skipped");
        if (!fx.empty()) warn.push_back(std::to_string(fx.size()) + " effect placement(s) in the map script (plays only when x64-zt compiles it: Tool Paths gsc_tool)");
        {
            // 8.2: authored effects. The map script loads them by name, so the converter turns the IW3 graph into a vfx.
            std::set<std::string> done;
            size_t authored = 0;
            std::map<std::string, double> authoredPeriod;
            struct PeriodApply {
                std::vector<FxPlacement>& fx;
                std::map<std::string, double>& periods;
                ~PeriodApply() {
                    for (FxPlacement& p : fx)
                        if (auto it = periods.find(p.effect); it != periods.end() && p.delay <= 0) p.delay = it->second;
                }
            } periodApply{fx, authoredPeriod};
            for (const FxPlacement& p : fx) {
                if (!done.insert(p.effect).second) continue;
                fs::path file;
                for (const std::string& r : opt.fxRoots) {
                    std::error_code fec;
                    const fs::path f = fs::path(r) / (p.effect + ".iw8fx.json");
                    if (fs::is_regular_file(f, fec)) {
                        file = f;
                        break;
                    }
                }
                if (file.empty()) continue;
                std::string text, perr;
                AuthoredFx afx;
                if (!isValidAuthoredFxName(p.effect)) {
                    warn.push_back("effect '" + p.effect + "': not a valid authored effect name (letters, digits, _ and /); skipped");
                    continue;
                }
                if (!readFile(file.string(), text) || !parseAuthoredFx(text, afx, &perr)) {
                    warn.push_back("effect " + file.string() + " not read: " + perr);
                    continue;
                }
                afx.name = p.effect;
                bool ok = true;
                for (size_t i = 0; i < afx.elements.size() && ok; ++i) {
                    if (afx.elements[i].kind != "sprite") continue;  // lights and models: no effect material or image
                    const std::string img = afx.elements[i].image.empty()
                                                ? opt.fxDefaultSprite
                                                : (file.parent_path() / afx.elements[i].image).string();
                    std::vector<uint8_t> dds;
                    std::string ierr;
                    if (img.empty() || !encodeDdsFromImage(img, false, true, dds, &ierr)) {
                        warn.push_back("effect '" + p.effect + "' element " + std::to_string(i) + ": image " + img + " not used (" + ierr +
                                       "); effect skipped");
                        ok = false;
                        break;
                    }
                    customFiles.push_back({root / "extracted" / "images" / (authoredFxImageName(afx, i) + ".dds"),
                                           std::string(dds.begin(), dds.end())});
                    customFiles.push_back({root / "extracted" / "materials" / (authoredFxMaterialName(afx, i) + ".json"),
                                           authoredFxMaterialJson(afx, i) + "\n"});
                }
                if (!ok) continue;
                customFiles.push_back({root / "extracted" / "fx" / (p.effect + ".iw3.json"), authoredFxIw3Json(afx) + "\n"});
                // A looping authored effect is replayed by the map script every period (fx_author.h authoredFxPeriodMs).
                if (const int period = authoredFxPeriodMs(afx); period > 0) authoredPeriod[p.effect] = period / 1000.0;
                ++authored;
            }
            if (authored)
                warn.push_back(std::to_string(authored) + " authored effect(s) written for the converter's IW3 effect route "
                               "(extracted/fx; research 67; in game NOT VERIFIED)");
        }
        size_t soundSkipped = 0;
        const std::vector<SoundPlacement> sounds = readSoundPlacements(map, &soundSkipped);
        if (soundSkipped)
            warn.push_back(std::to_string(soundSkipped) + " sound_emitter entit" + (soundSkipped == 1 ? "y has" : "ies have") +
                           " no usable soundalias key; skipped");
        // 6.22: the mapper's own sounds: an alias definition + the file per custom sound used (emitters, zone ambience).
        std::map<std::string, CustomSoundAlias> customSounds;
        std::map<std::string, std::string> customSoundFiles;  // alias -> source file (its settings sidecar sits next to it)
        const auto useCustomSound = [&](const std::string& name, bool looping, bool ambient2d) {
            const std::string file = findCustomSound(opt.soundRoots, name);
            if (file.empty()) return false;
            CustomSoundAlias& a = customSounds[name];
            customSoundFiles[name] = file;
            a.name = name;
            a.fileName = name + lower(fs::path(file).extension().string());
            a.looping = a.looping || looping;
            a.ambient2d = a.ambient2d || ambient2d;
            std::string bytes;
            if (readFile(file, bytes)) customFiles.push_back({root / "extracted" / "sound" / "iw8r" / a.fileName, bytes});
            return true;
        };
        std::vector<SoundPlacement> stockSounds;
        for (const SoundPlacement& p : sounds)
            if (!useCustomSound(p.alias, p.loop, false)) stockSounds.push_back(p);
        if (!sounds.empty()) {
            warn.push_back(std::to_string(sounds.size()) + " sound placement(s) in the map script (" +
                           std::to_string(sounds.size() - stockSounds.size()) +
                           " of the map's own sounds; plays only when x64-zt compiles it: Tool Paths gsc_tool)");
            if (!stockSounds.empty())
                customFiles.push_back({root / "extracted" / "sound" / (id + ".stock_sounds.txt"), stockSoundList(stockSounds)});
        }
        {
            // 6.22 (research 63): reverb and audio zones -> the converter's sound table and client audio triggers.
            AABB world = g.bounds;
            if (world.valid()) {
                world.mins = world.mins - Vec3{256, 256, 256};
                world.maxs = world.maxs + Vec3{256, 256, 1024};
            }
            std::vector<std::string> awarn;
            const AudioSetup audio = readAudioSetup(map, id, world, &awarn, [&](const std::string& n) {
                return !findCustomSound(opt.soundRoots, n).empty();
            });
            if (!audio.mapZone.ambient.empty()) useCustomSound(audio.mapZone.ambient, true, true);
            for (const AudioZone& z : audio.zones)
                if (!z.ambient.empty()) useCustomSound(z.ambient, true, true);
            for (const auto& [n, w] : audio.mapZone.oneShots) useCustomSound(n, false, true);
            for (const AudioZone& z : audio.zones)
                for (const auto& [n, w] : z.oneShots) useCustomSound(n, false, true);
            for (const std::string& w : awarn) warn.push_back("audio: " + w);
            if (audio.any() || !audio.mapZone.oneShots.empty()) {
                customFiles.push_back({root / "extracted" / "sound" / "soundtable.json", soundTableJson(audio) + "\n"});
                customFiles.push_back({root / "extracted" / "maps" / "clienttriggers.json", clientTriggersJson(audio) + "\n"});
                warn.push_back("audio: " + std::to_string(audio.zones.size() + 1) + " sound zone(s) (map reverb " +
                               (audio.mapZone.roomType.empty() ? std::string("none") : audio.mapZone.roomType) +
                               ") written to the sound table and client triggers (research 63; in game NOT VERIFIED)");
            }
        }
        for (auto& [name, alias] : customSounds) {
            // 12.8: settings next to the sound (<stem>.iw8snd.json: volume, distances, loop, 2D) override the defaults.
            if (const auto sf = customSoundFiles.find(name); sf != customSoundFiles.end()) {
                SoundSettings ss;
                std::string serr;
                if (!loadSoundSettingsFor(sf->second, ss, &serr))
                    warn.push_back("sound '" + name + "': its settings file is not valid (" + serr + "); defaults used");
                else
                    applySoundSettings(ss, alias);
            }
            customFiles.push_back({root / "extracted" / "soundaliases" / (name + ".iw3.json"), customSoundAliasJson(alias) + "\n"});
        }
        if (!customSounds.empty())
            warn.push_back("audio: " + std::to_string(customSounds.size()) +
                           " of the map's own sound(s) built into the map sound bank (sounds folder; in game NOT VERIFIED)");
        MapScriptExtras extras;
        extras.stockDoors = stockDoorPlaced;
        if (worldspawn) {
            // Game mode settings the map carries (game_modes.h).
            if (const std::string* dv = worldspawn->value("iw8r_mode_dvars")) {
                std::vector<std::string> notes;
                extras.modeDvars = parseModeDvars(*dv, &notes);
                for (const std::string& n : notes) warn.push_back("iw8r_mode_dvars: " + n);
                if (!extras.modeDvars.empty())
                    warn.push_back(std::to_string(extras.modeDvars.size()) + " game mode dvar(s) are set by the map script at start (" +
                                   modeDvarsText(extras.modeDvars) + ")");
            }
            if (const std::string* rules = worldspawn->value("iw8r_mode_rules")) {
                std::vector<std::string> notes;
                extras.modeRules = parseModeRules(*rules, &notes);
                for (const std::string& n : notes) warn.push_back("iw8r_mode_rules: " + n);
                if (!extras.modeRules.empty())
                    warn.push_back("game mode rules applied by the map script at every spawn (" + modeRulesText(extras.modeRules) +
                                   "; research 91; not seen in game)");
            }
            if (const std::string* br = worldspawn->value("iw8r_br")) {
                parseModeBr(*br, extras.brNoCircle, extras.brNoGulag, extras.brSkipDrop);
                if (extras.brNoCircle || extras.brNoGulag || extras.brSkipDrop)
                    warn.push_back(std::string("Warzone (br) switches set by the map script:") + (extras.brNoCircle ? " no circle" : "") +
                                   (extras.brNoGulag ? " no gulag" : "") + (extras.brSkipDrop ? " no drop from the plane" : "") +
                                   " (research 87; not seen in game)");
            }
        }
        if (worldspawn)
            if (const std::string* r = worldspawn->value("iw8r_sun_shadow_range")) {
                double v[2] = {0, 0};
                const int n = std::sscanf(r->c_str(), "%lf %lf", &v[0], &v[1]);
                if (n >= 1) extras.sunShadowRange[0] = std::clamp(v[0], 0.0, 8.0);
                extras.sunShadowRange[1] = n >= 2 ? std::clamp(v[1], 0.0, 8.0) : extras.sunShadowRange[0];
            }
        {
            // Live script_models the map's own logic owns (targeted, not gameobjects or exploders, whose stock scripts
            // handle them): solid for players, like stock scripts do (x64-zt's keyframed body only stops bullets).
            std::set<std::string> targeted, solid;
            for (const auto& e : map.entities)
                if (const std::string* t = e.value("target")) targeted.insert(*t);
            for (const auto& e : map.entities) {
                if (lower(e.classname()) != "script_model" || e.value("script_gameobjectname") || e.value("script_exploder")) continue;
                const std::string* tn = e.value("targetname");
                if (tn && (targeted.count(*tn) || (!tn->empty() && (e.value("iw8r_spin") || e.value("iw8r_bob") || e.value("iw8r_lift")))))
                    solid.insert(*tn);
            }
            extras.triggerVolumes = readTriggerVolumes(map);
            for (const auto& e : map.entities) {
                const std::string* vis = e.value("iw8r_visual");
                const std::string* tn = e.value("targetname");
                if (vis && tn && !vis->empty() && !tn->empty()) extras.visualLinks.push_back({*vis, *tn});
            }
            for (const auto& e : map.entities) {
                const std::string* nc = e.value("iw8r_no_collision");
                const std::string* tn = e.value("targetname");
                if (nc && *nc == "1" && tn && !tn->empty() && lower(e.classname()) == "script_model") {
                    extras.keepNames.push_back(*tn);
                    solid.erase(*tn);
                }
            }
            extras.solidTargetnames.assign(solid.begin(), solid.end());
            // 5.18: vision / fog volumes (brush triggers with iw8r_vision and a targetname).
            extras.baseVision = id;
            {
                int volume = 0;
                for (const auto& e : map.entities) {
                    const std::string* vt = e.value(kVisionKey);
                    if (!vt || vt->empty() || e.isWorldspawn() || lower(e.classname()).rfind("trigger_", 0) != 0) continue;
                    const std::string* tn = e.value("targetname");
                    if (!tn || tn->empty()) {
                        warn.push_back(e.classname() + " with iw8r_vision needs a targetname (the map script finds it by name); skipped");
                        continue;
                    }
                    std::string verr;
                    const std::string json = visionOverridesJson(*vt, &verr);
                    if (json.empty()) {
                        warn.push_back("trigger '" + *tn + "' iw8r_vision not parsed (" + verr + "); skipped");
                        continue;
                    }
                    if (volume >= 64) break;
                    customFiles.push_back({root / "extracted" / "vision" / (id + ".vol" + std::to_string(volume) + ".overrides.json"), json + "\n"});
                    extras.visionVolumes.push_back({*tn, id + "_v" + std::to_string(volume)});
                    ++volume;
                }
            }
            // 6.24: doors without scripting (script_brushmodel iw8r_door, opened by a trigger targeting it).
            for (const auto& e : map.entities) {
                const std::string* dk = e.value("iw8r_door");
                if (!dk || dk->empty()) continue;
                const std::string* tn = e.value("targetname");
                std::string trig;
                for (const auto& t : map.entities)
                    if (lower(t.classname()).rfind("trigger_", 0) == 0)
                        if (const std::string* tt = t.value("target"); tt && tn && *tt == *tn)
                            if (const std::string* ttn = t.value("targetname")) trig = *ttn;
                if (!tn || tn->empty()) {
                    warn.push_back(e.classname() + " with iw8r_door needs a targetname; not a door");
                    continue;
                }
                if (trig.empty()) {
                    // No trigger targets it: the door gets its own use volume (the script tests it, no entity is needed):
                    // the door's bounds grown by 40 units to every side, used with the Use button.
                    AABB box;
                    for (const auto& it : e.items)
                        if (const Primitive* p = it.prim(); p && p->kind == PrimKind::Brush) box.add(brushBounds(brushFromPrimitive(*p)));
                    if (!box.valid()) {
                        warn.push_back(e.classname() + " '" + *tn + "' with iw8r_door has no brushes; not a door");
                        continue;
                    }
                    trig = *tn + "_use";
                    MapScriptExtras::TriggerVolume uv;
                    uv.targetname = trig;
                    uv.use = true;
                    MapScriptExtras::TriggerVolume::Box ub;
                    ub.mins = Vec3{box.mins.x - 40, box.mins.y - 40, box.mins.z};
                    ub.maxs = Vec3{box.maxs.x + 40, box.maxs.y + 40, box.maxs.z};
                    uv.boxes.push_back(ub);
                    extras.triggerVolumes.push_back(std::move(uv));
                }
                MapScriptExtras::Door d;
                d.door = *tn;
                d.trigger = trig;
                char kind[16] = {};
                double v[5] = {0, 0, 0, 0, 0};
                const int n = std::sscanf(dk->c_str(), "%15s %lf %lf %lf %lf %lf", kind, &v[0], &v[1], &v[2], &v[3], &v[4]);
                if (std::string(kind) == "slide" && n >= 4) {
                    d.rotate = false;
                    for (int k = 0; k < 3; ++k) d.offset[k] = v[k];
                    if (n >= 5) d.moveTime = std::max(0.05, v[3]);
                    if (n >= 6) d.holdTime = std::max(0.0, v[4]);
                } else if ((std::string(kind) == "rotate" || std::string(kind) == "hinge") && n >= 2) {
                    // Both turn about the hinge at one end of the door, away from the player. "hinge" stays open until it
                    // is used again; "rotate" closes by itself after the hold time.
                    const DoorHinge hg = doorHingeOf(e);
                    if (!hg.ok) {
                        warn.push_back(e.classname() + " '" + *tn + "' with iw8r_door has no brushes; not a door");
                        continue;
                    }
                    d.hinged = true;
                    d.toggle = std::string(kind) == "hinge";
                    d.yaw = std::fabs(v[0]);
                    d.hinge[0] = hg.pos.x;
                    d.hinge[1] = hg.pos.y;
                    d.hinge[2] = hg.pos.z;
                    d.normal[0] = hg.normal.x;
                    d.normal[1] = hg.normal.y;
                    d.swing = hg.swing;
                    d.moveTime = n >= 3 ? std::max(0.05, v[1]) : 0.6;
                    if (n >= 4) d.holdTime = std::max(0.0, v[2]);
                } else {
                    warn.push_back("iw8r_door '" + *dk + "' not understood (hinge <angle> [time], rotate <angle> [time] [hold] or "
                                   "slide <x> <y> <z> [time] [hold])");
                    continue;
                }
                extras.doors.push_back(d);
            }
            // 6.12: inspector-only behaviours (teleporter, jump pad, damage zone on triggers; lift on script_brushmodels).
            for (const auto& e : map.entities) {
                const std::string cls = lower(e.classname());
                const std::string* tn = e.value("targetname");
                const bool trigger = cls.rfind("trigger_", 0) == 0;
                const auto needName = [&](const char* key) {
                    if (tn && !tn->empty()) return true;
                    warn.push_back(e.classname() + " with " + key + " needs a targetname (the map script finds it by name); skipped");
                    return false;
                };
                if (const std::string* v = e.value("iw8r_teleport"); v && !v->empty()) {
                    if (!trigger) warn.push_back(e.classname() + ": iw8r_teleport only works on a trigger; skipped");
                    else if (needName("iw8r_teleport")) {
                        bool found = false;
                        for (const auto& d : map.entities)
                            if (const std::string* dn = d.value("targetname"); dn && *dn == *v) found = true;
                        if (!found) warn.push_back("trigger '" + *tn + "' iw8r_teleport: no entity has targetname '" + *v +
                                                   "' (add a script_origin there); skipped");
                        else extras.teleports.push_back({*tn, *v});
                    }
                }
                if (const std::string* v = e.value("iw8r_jump"); v && !v->empty()) {
                    MapScriptExtras::JumpPad j;
                    if (!trigger) warn.push_back(e.classname() + ": iw8r_jump only works on a trigger; skipped");
                    else if (std::sscanf(v->c_str(), "%lf %lf %lf", &j.velocity[0], &j.velocity[1], &j.velocity[2]) != 3)
                        warn.push_back("iw8r_jump '" + *v + "' not understood (<vx> <vy> <vz>, units per second); skipped");
                    else if (needName("iw8r_jump")) {
                        j.trigger = *tn;
                        extras.jumpPads.push_back(j);
                    }
                }
                if (const std::string* v = e.value("iw8r_damage"); v && !v->empty()) {
                    MapScriptExtras::DamageZone h;
                    const int n = std::sscanf(v->c_str(), "%lf %lf", &h.amount, &h.interval);
                    if (cls == "fx_origin") {
                        // handled by readTriggerVolumes: a damage cylinder around the effect
                    } else if (!trigger) warn.push_back(e.classname() + ": iw8r_damage only works on a trigger or an fx_origin; skipped");
                    else if (n < 1 || h.amount <= 0)
                        warn.push_back("iw8r_damage '" + *v + "' not understood (<amount> [interval seconds]); skipped");
                    else if (needName("iw8r_damage")) {
                        h.trigger = *tn;
                        h.interval = std::max(0.05, h.interval);
                        extras.damageZones.push_back(h);
                    }
                }
                if (const std::string* v = e.value("iw8r_lift"); v && !v->empty()) {
                    MapScriptExtras::Lift l;
                    const int n = std::sscanf(v->c_str(), "%lf %lf %lf", &l.dz, &l.moveTime, &l.waitTime);
                    if (cls != "script_brushmodel" && cls != "script_model")
                        warn.push_back(e.classname() + ": iw8r_lift only works on a script_brushmodel; skipped");
                    else if (n < 1 || l.dz == 0)
                        warn.push_back("iw8r_lift '" + *v + "' not understood (<dz> [time] [wait]); skipped");
                    else if (needName("iw8r_lift")) {
                        l.targetname = *tn;
                        l.moveTime = std::max(0.05, l.moveTime);
                        l.waitTime = std::max(0.0, l.waitTime);
                        extras.lifts.push_back(l);
                    }
                }
            }
            for (const auto& e : map.entities) {
                const std::string cls = lower(e.classname());
                const std::string* tn = e.value("targetname");
                const bool mover = cls == "script_model" || cls == "script_brushmodel";
                if (const std::string* v = e.value("iw8r_spin"); v && !v->empty()) {
                    MapScriptExtras::Spinner sp;
                    char axis[16] = "yaw";
                    const int n = std::sscanf(v->c_str(), "%lf %15s", &sp.seconds, axis);
                    const std::string ax = lower(axis);
                    sp.axis = ax == "pitch" ? 1 : ax == "roll" ? 2 : 0;
                    if (!mover) warn.push_back(e.classname() + ": iw8r_spin only works on a model or a script_brushmodel; skipped");
                    else if (n < 1 || sp.seconds == 0 || (ax != "yaw" && ax != "pitch" && ax != "roll"))
                        warn.push_back("iw8r_spin '" + *v + "' not understood (<seconds per turn> [yaw|pitch|roll]); skipped");
                    else if (!tn || tn->empty()) warn.push_back(e.classname() + " with iw8r_spin needs a targetname; skipped");
                    else {
                        sp.targetname = *tn;
                        sp.seconds = std::fabs(sp.seconds);
                        extras.spinners.push_back(sp);
                    }
                }
                if (const std::string* v = e.value("iw8r_bob"); v && !v->empty()) {
                    MapScriptExtras::Bobber bo;
                    const int n = std::sscanf(v->c_str(), "%lf %lf", &bo.height, &bo.seconds);
                    if (!mover) warn.push_back(e.classname() + ": iw8r_bob only works on a model or a script_brushmodel; skipped");
                    else if (n < 1 || bo.height == 0 || bo.seconds <= 0)
                        warn.push_back("iw8r_bob '" + *v + "' not understood (<height> [seconds]); skipped");
                    else if (!tn || tn->empty()) warn.push_back(e.classname() + " with iw8r_bob needs a targetname; skipped");
                    else {
                        bo.targetname = *tn;
                        extras.bobbers.push_back(bo);
                    }
                }
            }
            // 4.21: stock animations on live script_models.
            for (const auto& e : map.entities) {
                if (lower(e.classname()) != "script_model") continue;
                const std::string* an = e.value("iw8r_anim");
                if (!an || an->empty()) continue;
                const std::string* tn = e.value("targetname");
                if (!tn || tn->empty() || !targeted.count(*tn)) {
                    warn.push_back("script_model with iw8r_anim '" + *an +
                                   "' is not live (it needs a targetname that something targets: Brush > Make Scripted); not animated");
                    continue;
                }
                MapScriptExtras::AnimPlacement ap;
                ap.targetname = *tn;
                ap.anim = *an;
                if (const std::string* lp = e.value("iw8r_anim_loop")) ap.loopSeconds = std::max(0.0, std::atof(lp->c_str()));
                extras.anims.push_back(ap);
            }
            // The map's own GSC (worldspawn iw8r_script, relative to the map folder): carried in the map zone.
            if (const std::string* sp = worldspawn->value("iw8r_script"); sp && !sp->empty()) {
                fs::path p(*sp);
                if (p.is_relative() && !opt.mapDir.empty()) p = fs::path(opt.mapDir) / p;
                std::string text;
                if (!readFile(p.string(), text))
                    warn.push_back("iw8r_script '" + *sp + "' cannot be read; the map script runs without it");
                else if (text.find("user_main") == std::string::npos)
                    warn.push_back("iw8r_script '" + *sp + "' has no user_main(); it is included but nothing starts it");
                else
                    extras.userScript = text;
                if (!extras.userScript.empty()) warn.push_back("map script includes " + p.filename().string() + " (user_main runs after the load script)");
            }
            // 13.6 (research 91 / 92): a new game type script the map carries (worldspawn iw8r_gametype_script, relative
            // to the map folder). Checked here; the converter compiles it into the map zone as it is.
            if (const std::string* gp = worldspawn->value(kGameTypeScriptKey); gp && !gp->empty()) {
                fs::path p(*gp);
                if (p.is_relative() && !opt.mapDir.empty()) p = fs::path(opt.mapDir) / p;
                const std::string name = gameTypeNameOfScript(p.filename().string());
                std::string text;
                if (!readFile(p.string(), text)) {
                    warn.push_back(std::string(kGameTypeScriptKey) + " '" + *gp + "' cannot be read; no game type script is built");
                } else if (!opt.stockScripts) {
                    warn.push_back("game type script " + p.filename().string() + " left out: the stock script index "
                                   "(data/generated/iw8_stock_scripts.tsv) was not found, so its calls cannot be checked");
                } else {
                    const std::vector<std::string> problems = checkGameTypeScript(text, name, *opt.stockScripts);
                    if (!problems.empty()) {
                        warn.push_back("game type script " + p.filename().string() + " left out (" + std::to_string(problems.size()) +
                                       " problem(s); a match with it would end at load):");
                        for (const std::string& pr : problems) warn.push_back("  " + pr);
                    } else {
                        customFiles.push_back({root / "extracted" / "scripts" / "mp" / "gametypes" / (name + ".gsc"), text});
                        warn.push_back("game type '" + name + "' built into the map (scripts/mp/gametypes/" + name +
                                       ".gsc; start a match with ui_gametype " + name + "; research 91 / 92; not seen in game)");
                    }
                }
            }
        }
        if (opt.prefabScripts) extras.prefabScripts = *opt.prefabScripts;  // prefab bundles (the prefab step fills it)
        if (const std::string gsc = mapScriptGsc(atmo.fog, fx, sounds, extras); !gsc.empty()) customFiles.push_back({mapDir / (id + ".gsc"), gsc});
        if (const std::string* vo = worldspawn->value(kVisionKey); vo && !vo->empty()) {
            std::string verr;
            const std::string json = visionOverridesJson(*vo, &verr);
            if (json.empty())
                warn.push_back(verr.empty() ? std::string("iw8r_vision is empty") : verr + "; no vision overrides");
            else
                customFiles.push_back({root / "extracted" / "vision" / (id + ".overrides.json"), json + "\n"});
        }
        if (const std::string* ss = worldspawn->value("iw8r_sun_sprite"); ss && !ss->empty()) {
            // 5.16 (research 39 section 3): sun sprite, flare, blind and glare -> extracted/sun/<id>.sun.
            std::string serr;
            if (const auto text = sunFileText(*ss, sunPitch, sunYaw, &serr))
                customFiles.push_back({root / "extracted" / "sun" / (id + ".sun"), *text});
            else
                warn.push_back(serr + "; no sun sprite");
        }
        if (atmo.film) {
            if (atmo.film->isIdentity())
                warn.push_back("iw8r_film is the identity grade; the converter drops it, vision file not written");
            else
                customFiles.push_back({root / "extracted" / "vision" / (id + ".vision"), filmVision(*atmo.film)});
        }
    }
    if (!lutBytes.empty()) customFiles.push_back({root / "extracted" / "grading" / (id + "_clut.rgba"), lutBytes});
    {
        // Light grid (research 58): ambient + sun visibility for viewmodels, players and models (without it the gun is
        // black in game). Sun visibility from a ray through the map's collision toward the sun.
        CollisionWorld cw;
        cw.build(map);
        LightGridInput lg;
        lg.mapId = id;
        lg.bounds = bounds;
        lg.sunToward = sunDir;
        for (int k = 0; k < 3; ++k) lg.ambient[k] = std::clamp(0.7 * sunColor[k], 0.0, 1.0);
        const double reach = 2.0 * length(bounds.maxs - bounds.mins) + 1024.0;
        lg.sunVisible = [&cw, sunDir, reach](const Vec3& p) {
            const CollisionWorld::Trace t = cw.traceRay(p + sunDir * 2.0, p + sunDir * reach);
            return !t.startSolid && t.fraction >= 1.0;
        };
        // Floors per column: repeated downward traces through the collision (top to bottom, at most 4 levels).
        const double topZ = bounds.maxs.z + 8, bottomZ = bounds.mins.z - 8;
        lg.floorsAt = [&cw, topZ, bottomZ](double x, double y) {
            std::vector<double> floors;
            double z = topZ;
            for (int level = 0; level < 4 && z > bottomZ; ++level) {
                const CollisionWorld::Trace t = cw.traceRay(Vec3{x, y, z}, Vec3{x, y, bottomZ});
                if (t.startSolid) {
                    z -= 16;  // inside a brush: step down out of it
                    --level;
                    continue;
                }
                if (t.fraction >= 1.0) break;
                if (t.plane.n.z > 0.5) floors.push_back(t.end.z);  // walkable-ish surface
                z = t.end.z - 1;
            }
            return floors;
        };
        LightGridFiles grid;
        std::string gerr;
        if (buildLightGrid(lg, grid, &gerr)) {
            const fs::path ex = root / "extracted";
            customFiles.push_back({ex / lightGridPartPath(id, "row_starts"), grid.rowStarts});
            customFiles.push_back({ex / lightGridPartPath(id, "row_data"), grid.rowData});
            customFiles.push_back({ex / lightGridPartPath(id, "entries"), grid.entries});
            customFiles.push_back({ex / lightGridPartPath(id, "colors"), grid.colors});
            customFiles.push_back({mapDir / (id + ".d3dbsp.lightgrid.json"), grid.json});
            warn.push_back("light grid: " + std::to_string(grid.entryCount) + " points (" + std::to_string(grid.sunlit) +
                           " sunlit) for viewmodel and model lighting");
        } else {
            warn.push_back("light grid not written (" + gerr + "): models and the viewmodel get no grid light");
        }
    }
    if (!localLights.empty()) {
        const std::string falloffDef = std::string("b") + std::string("falloff_linear", 14) + std::string(1, '\0');
        customFiles.push_back({root / "extracted" / "lights" / "light_point_linear", falloffDef});
    }
    bool ok = writeFile(root / "complete", "1\n", werr) && writeFile(root / "skeleton" / "complete", "1\n", werr) &&
                    writeFile(mapDir / (id + ".d3dbsp.replay-world.json"), world + "\n", werr) &&
                    writeFile(mapDir / (id + ".d3dbsp.replay-collision.json"), col + "\n", werr) &&
                    writeFile(mapDir / (id + ".d3dbsp.replay-comworld.json"), comworld + "\n", werr) &&
                    writeFile(mapDir / (id + ".d3dbsp.ents"), ents, werr) &&
                    writeFile(root / "extracted" / "materials" / (opt.greyMaterial + ".json"), buildMaterialJson(opt.greyMaterial) + "\n",
                              werr) &&
                    // Read by ReadPreparedRawFiles / ReadDeclaredFx / AuditSourceAssetDeclarations (doc 17 §4.1).
                    writeFile(root / "extracted" / "zone_source" / (id + ".zone"), "", werr) &&
                    writeFile(root / "extracted" / "impactfx" / "_default.iw3.json", buildImpactTableJson() + "\n", werr);
    for (size_t i = 0; ok && i < customFiles.size(); ++i) ok = writeFile(customFiles[i].first, customFiles[i].second, werr);
    if (!ok) return fail(werr);
    for (const ImportedModel& im : customModels) {
        ModelExportOptions mo;
        mo.scale = opt.modelScale > 0 ? opt.modelScale : 1.0;
        std::string merr;
        if (!writeExtractionModel(im, (root / "extracted").string(), mo, &merr)) return fail("custom model " + im.name + ": " + merr);
    }
    rep.customModels = customModels.size();
    rep.reflectionProbes = probes.size();
    rep.staticModels = placements.size();
    rep.sky = skyValue;
    rep.skyNote = skyNote;
    {
        // HUD compass image (docs/research/27): DXT5 with mips, the format the compass reader accepts.
        const MinimapImage mm = renderMinimap(map, 512);
        std::string merr;
        if (mm.width > 0 && !writeMinimapDds(mm, (root / "extracted" / "images" / ("compass_map_" + id + ".dds")).string(), &merr))
            warn.push_back("minimap not written: " + merr);
    }

    rep.surfaces = surfaces.size();
    rep.brushModels = brushModels.size();
    rep.triangles = triangles;
    rep.collisionBrushes = g.brushes.size();
    rep.entities = entityCount;
    rep.spawns = spawns;
    rep.bounds = bounds;
    return true;
}

}  // namespace iw8r
