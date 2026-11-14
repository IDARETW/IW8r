// IW8 Radiant Suite - the camera's geometry kept between frames (docs/research/85). See CameraGeometry in scene.h.
//
// The per-item code below makes exactly what buildCameraGeometry (scene.cpp) makes for one item; the GUI test
// compares the two (CameraGeometry::flatten against buildCameraGeometry) in every camera mode.
#include "game_mode_view.h"
#include "game_lightmap.h"
#include "scene.h"
#include "patch_covers.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <unordered_map>

#include "brush_ops.h"
#include "compile_iw8.h"
#include "prefab.h"
#include "texel_density.h"

namespace iw8r::app {

namespace {

std::atomic<uint32_t> g_streamId{1};

uint32_t scaleColor(uint32_t c, double s) {
    auto ch = [&](int shift) { return uint32_t(std::clamp(((c >> shift) & 0xff) * s, 0.0, 255.0)); };
    return ch(0) | (ch(8) << 8) | (ch(16) << 16) | (0xffu << 24);
}

uint32_t mixColor(uint32_t a, uint32_t b, double t) {
    auto ch = [&](int shift) { return uint32_t(std::clamp(((a >> shift) & 0xff) * (1 - t) + ((b >> shift) & 0xff) * t, 0.0, 255.0)); };
    return ch(0) | (ch(8) << 8) | (ch(16) << 16) | (ch(24) << 24);
}

GpuVertex gv(const Vec3& p, uint32_t c) { return {float(p.x), float(p.y), float(p.z), c}; }

uint32_t normalColour(const Vec3& n) {
    auto ch = [](double v) { return uint32_t(std::clamp(std::round((0.5 + 0.5 * v) * 255.0), 0.0, 255.0)); };
    return ch(n.x) | (ch(n.y) << 8) | (ch(n.z) << 16) | (0xffu << 24);
}

constexpr uint32_t kSelRed = 0xff3030ffu;
constexpr uint32_t kSelEdge = 0xff40e0ffu;
constexpr uint32_t kEdgeDark = 0x99101010u;
constexpr uint32_t kEdgeWire = 0xffb4b4b4u;

// What the view needs to know about a material, asked once per full build (the callbacks load textures).
struct MatInfo {
    int draw = 0, tex = -1, nrm = -1, blend = 0;
    bool sway = false, tool = false, sky = false;
    double side = 0;
    bool hasDraw = false, hasTex = false, hasNrm = false, hasBlend = false, hasSway = false, hasSide = false, hasTool = false, hasSky = false;
    uint64_t texAsked = 0;  // the update that last asked for the texture
};

enum { kShadowOnly = 0, kCasters = 1, kNoCast = 2, kLines = 3, kFlatStreams = 4 };
constexpr size_t kRunItems = 16;  // items per box of the scene box (see runBox)

// One item's vertices, by stream.
struct ItemOut {
    std::vector<GpuVertex> f[kFlatStreams];
    std::vector<GpuVertex> sel;  // outlines of a selected item (on top)
    struct Tex {
        int key = 0, tex = -1, nrm = -1, blend = 0;
        bool sway = false;
        const std::string* material = nullptr;
        std::vector<GpuTexVertex> v;
    };
    std::vector<Tex> tex;
    size_t texUsed = 0;
    bool linesHidden = false;  // f[kLines] is the place kept for a selected item's outlines (zero-length lines)
    void clear() {
        for (auto& s : f) s.clear();
        sel.clear();
        linesHidden = false;
        for (size_t k = 0; k < texUsed; ++k) tex[k].v.clear();
        texUsed = 0;
    }
    Tex& texFor(int key, int texId, int nrm, int blend, bool sway, const std::string& material) {
        for (size_t k = texUsed; k-- > 0;)
            if (tex[k].key == key) return tex[k];
        if (tex.size() <= texUsed) tex.emplace_back();
        Tex& t = tex[texUsed++];
        t.key = key, t.tex = texId, t.nrm = nrm, t.blend = blend, t.sway = sway, t.material = &material;
        return t;
    }
    size_t bytes() const {
        size_t n = sel.capacity() * sizeof(GpuVertex);
        for (const auto& s : f) n += s.capacity() * sizeof(GpuVertex);
        for (const auto& t : tex) n += t.v.capacity() * sizeof(GpuTexVertex);
        return n;
    }
};

struct Slice {
    uint32_t off = 0, cnt = 0;
};
struct TexSlice {
    SceneBatch* b = nullptr;
    uint32_t off = 0, cnt = 0;
};
struct ItemRec {
    const PrimGeom* g = nullptr;
    uint64_t stamp = 0;
    bool sel = false;
    // A selected item draws its outlines on top: its slice of the depth-tested lines is kept, drawn as zero-length
    // lines, so selecting and deselecting writes in place.
    bool linesHidden = false;
    Slice s[kFlatStreams];
    uint32_t texFirst = 0, texCount = 0;
};
struct SelEntry {
    uint32_t item = 0, off = 0, cnt = 0;
};
struct Plan {
    int32_t old = -1, gen = -1;
    bool sel = false;
};

template <class V>
bool sameBytes(const V* a, const V* b, size_t n) { return n == 0 || std::memcmp(a, b, n * sizeof(V)) == 0; }

uint64_t mixBits(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }
uint64_t mixDouble(uint64_t h, double v) {
    uint64_t b = 0;
    std::memcpy(&b, &v, sizeof(b));
    return mixBits(h, b);
}

}  // namespace

GeoStreamBase::GeoStreamBase() : id(g_streamId++) {}

struct CameraGeometry::Impl {
    std::vector<ItemRec> recs, newRecs;
    std::vector<TexSlice> texSlices, newTex;
    std::vector<SelEntry> selEntries, newSel;
    std::unordered_map<int, std::unique_ptr<SceneBatch>> byKey;
    std::unordered_map<std::string, MatInfo> mats;
    std::unordered_map<const PrimGeom*, uint32_t> where;
    std::vector<Plan> plan;
    std::vector<uint8_t> oldUsed;
    std::vector<SceneBatch*> newOrder, swayDirty;
    ItemOut out;
    std::vector<ItemOut> genPool;
    std::vector<GpuVertex> mSun, mFace, mOverlay, mLines;
    uint64_t globalKey = 0, serial = 0;
    bool built = false, staleMaterial = false, incremental = false;
    // The exact box of each run of kRunItems items: the scene box is their union, so a moved item costs its run
    // and the union of the runs, not a pass over every item.
    std::vector<AABB> runBox;

    // The view settings of the update in progress.
    const SceneCache* scene = nullptr;
    const Document* doc = nullptr;
    const TextureFn* textureFor = nullptr;
    const CollisionWorld* shadowWorld = nullptr;
    CameraMode mode = CameraMode::Textured;
    bool wire = false, normals = false, contentsMode = false, densityMode = false, collisionMode = false, texturesOn = false;
    Vec3 light;

    // A tool face of a brush that lies under flat patches (a paint mesh on a caulked face) is not drawn
    // (patch_covers.h). Found again at every update; the hash is part of the global key.
    PatchCovers covers;

    MatInfo& mat(const std::string& m) { return mats[m]; }
    void genItem(const PrimGeom* g, bool sel, ItemOut& out);
    SceneBatch* batchFor(const ItemOut::Tex& t, bool& created);
};

CameraGeometry::CameraGeometry() : d_(std::make_unique<Impl>()) {}
CameraGeometry::~CameraGeometry() = default;

void CameraGeometry::clear() {
    d_->built = false;
}

SceneBatch* CameraGeometry::Impl::batchFor(const ItemOut::Tex& t, bool& created) {
    created = false;
    auto it = byKey.find(t.key);
    if (it != byKey.end()) return it->second.get();
    auto b = std::make_unique<SceneBatch>();
    b->key = t.key;
    b->texture = t.tex;
    b->normalTexture = t.nrm;
    b->blend = t.blend;
    b->sway = t.sway;
    // Same texture id = same material, so the batch's emissive map is the material's.
    if (scene->emissiveFor && !scene->captureMode && t.material) b->emissiveTexture = scene->emissiveFor(*t.material);
    if (scene->specFor && !scene->captureMode && t.material) b->specTexture = scene->specFor(*t.material);
    if (scene->shaderFor && t.material) b->shader = scene->shaderFor(*t.material);
    created = true;
    return byKey.emplace(t.key, std::move(b)).first->second.get();
}

void CameraGeometry::Impl::genItem(const PrimGeom* g, bool sel, ItemOut& out) {
    out.clear();
    const SceneCache& sc = *scene;
    // Probe capture keeps only what the game draws: brushes, patches, models and prefab contents.
    if (sc.captureMode && ((g->kind == PrimGeom::PointEntity && !g->prefab) || g->volume)) return;
    if (collisionMode && !g->collides && !sel) return;  // only what the player collides with
    std::vector<GpuVertex>& lines = out.f[kLines];
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
        if (sc.captureMode) lines.clear();
        return;
    }
    std::vector<GpuVertex>& edgeDst = sel ? out.sel : lines;
    const std::string* lastMat = nullptr;
    MatInfo* lastInfo = nullptr;
    const bool lit = sc.litPreview && !sc.gameLookActive();
    const bool encoded = sc.litEncoded() && !sc.gameLookActive();
    const GameLightmap* gameLm = sc.gameLookActive() ? sc.gameLightmap.get() : nullptr;
    for (size_t i = 0; i < g->polys.size(); ++i) {
        const bool hasMat = i < g->polyMaterial.size();
        MatInfo* mi = nullptr;
        if (hasMat) {
            const std::string& m = g->polyMaterial[i];
            if (lastMat && *lastMat == m) {
                mi = lastInfo;
            } else {
                mi = &mat(m);
                lastMat = &m;
                lastInfo = mi;
            }
        }
        if (sc.skyActive && texturesOn && hasMat && !sel) {
            if (!mi->hasSky) mi->sky = g->polyMaterial[i].find("sky") != std::string::npos, mi->hasSky = true;
            if (mi->sky) continue;  // the sky box shows through, as in game
        }
        const bool markerPoly = g->markerPoly(i);  // a prefab's inner entity box: editor only
        if (markerPoly && sc.captureMode) continue;
        const Winding& w = g->polys[i];
        if (sc.drawClassFor && hasMat && !g->polyMaterial[i].empty()) {
            if (!mi->hasDraw) mi->draw = sc.drawClassFor(g->polyMaterial[i]), mi->hasDraw = true;
            if (mi->draw == 2) continue;  // never drawn by the game (info carriers, lighting proxy cards, distortion)
            if (mi->draw == 1) {          // shadow-only: casts the sun shadow, invisible on screen
                auto& so = out.f[kShadowOnly];
                for (size_t k = 1; k + 1 < w.size(); ++k) {
                    so.push_back(gv(w[0], 0xffffffffu));
                    so.push_back(gv(w[k], 0xffffffffu));
                    so.push_back(gv(w[k + 1], 0xffffffffu));
                }
                continue;
            }
        }
        const Vec3 n = i < g->faceNormals.size() ? g->faceNormals[i] : Vec3{0, 0, 1};
        double shade = mode == CameraMode::Fullbright ? 1.0 : 0.55 + 0.45 * std::fabs(dot(n, light));
        Vec3 litC{1, 1, 1};
        Vec3 sunLit;  // the sun's part of litC (the shadow map removes it where the sun is blocked)
        if (lit) {
            Vec3 c;
            for (const auto& v : w) c += v;
            c = c * (1.0 / double(std::max<size_t>(1, w.size())));
            double sunK = std::max(0.0, dot(n, sc.sunToward));
            if (sunK > 0 && sc.litShadows && shadowWorld && !sc.gpuShadows) {
                // Face centre lifted off the surface, then a ray toward the sun: blocked = in shadow.
                const Vec3 from = c + n * 1.0;
                if (shadowWorld->traceRay(from, from + sc.sunToward * 65536.0).fraction < 1.0) sunK = 0;
            }
            litC = Vec3{sc.ambient, sc.ambient, sc.ambient} + sc.sunColor * sunK * sc.sunGain;
            sunLit = sc.sunColor * sunK * sc.sunGain;
            // Textured faces get the first lights per pixel from the renderer; the rest (and flat faces) per face.
            const bool perPixel = sc.pixelLights && texturesOn && textureFor && hasMat && i < g->polyUV.size() && g->polyUV[i].size() == w.size();
            size_t li = 0;
            for (const auto& L : sc.lights) {
                if (perPixel && li++ < 16) continue;
                const Vec3 d = L.pos - c;
                const double dist = length(d);
                if (dist >= L.radius || dist < 1e-6) continue;
                const Vec3 l = d * (1.0 / dist);
                const double fall = 1.0 - dist / L.radius;
                double k = std::max(0.0, dot(n, l)) * fall * fall * sc.lightGain;
                if (L.cosOuter > -1.5) {
                    const double cs = dot(l * -1.0, L.dir);
                    if (cs <= L.cosOuter) continue;
                    if (cs < L.cosInner) k *= (cs - L.cosOuter) / std::max(1e-6, L.cosInner - L.cosOuter);
                }
                litC = litC + L.color * k;
            }
            shade = 1.0;
        }
        auto litColor = [&](uint32_t base) {
            if (!lit) return scaleColor(base, shade);
            if (!encoded) {
                auto c1 = [&](int shift, double s) { return uint32_t(std::clamp(((base >> shift) & 0xff) * s, 0.0, 255.0)); };
                return c1(0, litC.x) | (c1(8, litC.y) << 8) | (c1(16, litC.z) << 16) | (0xffu << 24);
            }
            // Light / 2 (headroom to 2x; the shader decodes) and the sun's share of it in alpha, 0.5..1.
            auto ch = [&](int shift, double s) { return uint32_t(std::clamp(((base >> shift) & 0xff) * s * 0.5, 0.0, 255.0)); };
            const double total = litC.x + litC.y + litC.z, sun = sunLit.x + sunLit.y + sunLit.z;
            const double share = total > 1e-6 ? std::clamp(sun / total, 0.0, 1.0) : 0.0;
            const uint32_t a = uint32_t(std::lround(128 + 127 * share));
            return ch(0, litC.x) | (ch(8, litC.y) << 8) | (ch(16, litC.z) << 16) | (a << 24);
        };
        int texId = -1;
        if (texturesOn && textureFor && hasMat && i < g->polyUV.size() && g->polyUV[i].size() == w.size()) {
            const std::string& m = g->polyMaterial[i];
            if (!mi->hasTex) {
                mi->tex = (*textureFor)(m), mi->hasTex = true, mi->texAsked = serial;
            } else if (mi->tex < 0 && incremental && mi->texAsked != serial) {
                // No texture when everything was last made: ask again, and make everything again when one came.
                mi->texAsked = serial;
                if ((*textureFor)(m) != mi->tex) staleMaterial = true;
            }
            texId = mi->tex;
        }
        if (texId >= 0) {
            const std::string& m = g->polyMaterial[i];
            if (sc.gameLookActive()) shade = 1.0;  // the renderer lights textured faces
            // Brush entities keep a hint of their class colour; selection tints red.
            uint32_t c = litColor(i < g->polyTint.size() ? g->polyTint[i] : 0xffffffffu);
            if (g->brushEntity) c = mixColor(c, g->color, 0.35);
            if (sel) c = mixColor(c, sc.selectionTint, sc.selectionTintAmount);
            // Normal map (3.2): batched by colour and normal texture together.
            int nrmId = -1;
            if (sc.normalFor && !sc.captureMode) {
                if (!mi->hasNrm) mi->nrm = sc.normalFor(m), mi->hasNrm = true;
                nrmId = mi->nrm;
            }
            int blendMode = 0;
            if (sc.blendFor && !sc.captureMode) {
                if (!mi->hasBlend) mi->blend = sc.blendFor(m), mi->hasBlend = true;
                blendMode = std::clamp(mi->blend, 0, 2);
            }
            bool sway = false;
            if (sc.foliageFor && !sc.captureMode) {
                if (!mi->hasSway) mi->sway = sc.foliageFor(m), mi->hasSway = true;
                sway = mi->sway;
            }
            const int batchKey = ((texId * 65536 + (nrmId + 1)) * 3 + blendMode) * 2 + (sway ? 1 : 0);
            auto& dst = out.texFor(batchKey, texId, nrmId, blendMode, sway, m).v;
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
            const bool lit = modelTriangleLight(sc, *g, i, n, modelLight);
            appendTexturedFace(*g, i, n, lit ? modelLight : tan, c, gameLm, centre, dst, lit);
        } else if (!wire) {
            uint32_t c = normals ? normalColour(n) : litColor(contentsMode || collisionMode ? g->contentsColor : g->color);
            if (!normals && !contentsMode && i < g->polyTint.size()) c = mixColor(c, g->polyTint[i] | 0xff000000u, 0.6);
            if (densityMode) {
                c = 0xff707070u;
                if (i < g->polyRepeatsPerUnit.size() && hasMat && g->polyRepeatsPerUnit[i] > 0) {
                    if (!mi->hasSide) {
                        std::pair<int, int> sz = sc.textureSizeFor ? sc.textureSizeFor(g->polyMaterial[i]) : std::pair<int, int>{0, 0};
                        if (sz.first <= 0 || sz.second <= 0) sz = {512, 512};
                        mi->side = std::sqrt(double(sz.first) * double(sz.second));
                        mi->hasSide = true;
                    }
                    const auto rgb = densityColor(g->polyRepeatsPerUnit[i] * mi->side, sc.densityTarget);
                    c = 0xff000000u | uint32_t(rgb[0] * 255) | (uint32_t(rgb[1] * 255) << 8) | (uint32_t(rgb[2] * 255) << 16);
                    c = scaleColor(c, shade);
                }
            }
            if (sel) c = mixColor(c, sc.selectionTint, std::min(1.0, sc.selectionTintAmount + 0.05));
            const bool marker = (g->kind == PrimGeom::PointEntity && !g->prefab && !g->mesh) || markerPoly;
            bool tool = false;
            if (hasMat) {
                if (!mi->hasTool) mi->tool = isIw8ToolMaterial(g->polyMaterial[i]), mi->hasTool = true;
                tool = mi->tool;
            }
            const bool noCast = marker || tool || g->volume;
            auto& dst = out.f[noCast ? kNoCast : kCasters];
            // Not in the game: no sun shadow falls on it either (sun share 0 in the lit encoding).
            if (noCast && encoded) c = (c & 0x00ffffffu) | 0x80000000u;
            // A tool face under a flat patch (a paint mesh on a caulked face) is left to the patch.
            const bool covered = tool && !sel && !wire && covers.any() && g->kind == PrimGeom::Brush && i < g->faceNormals.size() &&
                                 covers.covered(w, g->faceNormals[i]);
            for (size_t k = 1; !covered && k + 1 < w.size(); ++k) {
                dst.push_back(gv(w[0], c));
                dst.push_back(gv(w[k], c));
                dst.push_back(gv(w[k + 1], c));
            }
        }
        if (!g->edges.empty() || (g->meshPoly(i) && !sel && !wire)) continue;
        // Game Lighting: a textured face is drawn as the game draws it, without the editor's outline (seams between
        // the brushes of one wall would show as lines).
        if (!sel && !wire && g->kind == PrimGeom::Brush && !g->volume && sc.gameLookActive() && !sc.gameOutlines) continue;
        const uint32_t ec = sel ? kSelEdge : (wire ? kEdgeWire : kEdgeDark);
        for (size_t k = 0; k < w.size(); ++k) {
            edgeDst.push_back(gv(w[k], ec));
            edgeDst.push_back(gv(w[(k + 1) % w.size()], ec));
        }
    }
    if (!g->edges.empty()) {
        const uint32_t ec = sel ? kSelEdge : 0xcc8c60c0u;
        for (const auto& [a, b] : g->edges) {
            edgeDst.push_back(gv(a, ec));
            edgeDst.push_back(gv(b, ec));
        }
    }
    if (g->hasArrow) {
        edgeDst.push_back(gv(g->arrowFrom, 0xffffffffu));
        edgeDst.push_back(gv(g->arrowTo, 0xffffffffu));
    }
    if (sc.captureMode) {  // probe capture: only surfaces (no outlines, markers or helpers)
        lines.clear();
        out.sel.clear();
    } else if (sel && lines.empty() && !out.sel.empty() && !(g->hasMeshPolys() && !wire) && !collisionMode && !(sc.skyActive && texturesOn)) {
        // A selected item draws its outlines on top. Not selected, it has as many depth-tested line vertices (the same
        // edges): that place is kept as zero-length lines, so deselecting writes the outlines back in place instead of
        // moving every line after them. (Models, volumes and sky faces have other counts when not selected.)
        lines.assign(out.sel.size(), GpuVertex{out.sel[0].x, out.sel[0].y, out.sel[0].z, 0});
        out.linesHidden = true;
    }
}

bool CameraGeometry::update(const SceneCache& scene, const Document& doc, bool showSpawns, const TextureFn* textureFor, bool force) {
    Impl& d = *d_;
    ++d.serial;
    d.scene = &scene;
    d.doc = &doc;
    d.textureFor = textureFor;
    d.mode = scene.cameraMode;
    d.wire = d.mode == CameraMode::Wireframe;
    d.normals = d.mode == CameraMode::Normals;
    d.contentsMode = d.mode == CameraMode::Contents;
    d.densityMode = d.mode == CameraMode::Density;
    d.collisionMode = d.mode == CameraMode::Collision;
    d.texturesOn = d.mode == CameraMode::Textured || d.mode == CameraMode::Fullbright;
    d.light = normalize(Vec3{0.35, 0.55, 0.75});
    d.staleMaterial = false;
    // Per-face ray shadows (lit preview in a mode the shadow map does not cover) depend on every other item.
    const bool rayShadows = scene.litPreview && scene.litShadows && !scene.gpuShadows;
    d.shadowWorld = rayShadows ? scene.shadowWorld(doc) : nullptr;

    // Everything the vertices of all items depend on.
    uint64_t key = 1469598103934665603ull;
    key = mixBits(key, uint64_t(d.mode) | (uint64_t(scene.litPreview) << 8) | (uint64_t(scene.litShadows) << 9) | (uint64_t(scene.gpuShadows) << 10) |
                           (uint64_t(scene.pixelLights) << 11) | (uint64_t(scene.captureMode) << 12) | (uint64_t(scene.skyActive) << 13) |
                           (uint64_t(textureFor != nullptr) << 14) | (uint64_t(bool(scene.normalFor)) << 15) | (uint64_t(bool(scene.emissiveFor)) << 16) |
                           (uint64_t(bool(scene.blendFor)) << 17) | (uint64_t(bool(scene.foliageFor)) << 18) | (uint64_t(bool(scene.drawClassFor)) << 19) |
                           (uint64_t(bool(scene.textureSizeFor)) << 20) | (uint64_t(bool(scene.specFor)) << 21));
    key = mixBits(key, scene.modelLightsRev);
    key = mixBits(key, scene.selectionTint);
    key = mixDouble(key, scene.selectionTintAmount);
    key = mixDouble(key, scene.densityTarget);
    if (scene.litPreview) {
        for (double v : {scene.ambient, scene.sunGain, scene.lightGain, scene.sunToward.x, scene.sunToward.y, scene.sunToward.z, scene.sunColor.x,
                         scene.sunColor.y, scene.sunColor.z})
            key = mixDouble(key, v);
        for (const auto& L : scene.lights)
            for (double v : {L.pos.x, L.pos.y, L.pos.z, L.color.x, L.color.y, L.color.z, L.dir.x, L.dir.y, L.dir.z, L.radius, L.cosOuter, L.cosInner})
                key = mixDouble(key, v);
        if (rayShadows) key = mixBits(key, doc.revision());
    }

    const std::vector<const PrimGeom*>& items = scene.items();
    const size_t n = items.size();
    key = mixBits(key, d.covers.find(items));
    GeoStream<GpuVertex>* fs[kFlatStreams] = {&shadowOnly, &casters, &noCast, &lines};
    bool changed = false, layoutChanged = false, moved = false;
    bool full = force || !d.built || key != d.globalKey;
    d.swayDirty.clear();

    if (!full) {
        IW8R_PROF("g.items");
        d.incremental = true;
        bool sameList = d.recs.size() == n;
        for (size_t i = 0; i < n && sameList; ++i) sameList = d.recs[i].g == items[i];
        bool structural = !sameList;
        const bool anySel = !doc.selection().empty();
        for (size_t i = 0; i < n && sameList && !structural; ++i) {
            const PrimGeom* g = items[i];
            ItemRec& r = d.recs[i];
            const bool sel = anySel && doc.isSelected(g->ref);
            if (r.stamp == g->stamp && r.sel == sel) continue;
            d.genItem(g, sel, d.out);
            if (d.staleMaterial) break;
            ItemOut& out = d.out;
            // The same vertex counts in the same streams: written over the old vertices.
            bool same = out.texUsed == r.texCount;
            for (int s = 0; s < kLines && same; ++s) same = out.f[s].size() == r.s[s].cnt;
            const bool hide = sel && out.f[kLines].empty() && r.s[kLines].cnt > 0;
            if (same && !hide) same = out.f[kLines].size() == r.s[kLines].cnt;
            for (size_t k = 0; k < out.texUsed && same; ++k) {
                const TexSlice& ts = d.texSlices[r.texFirst + k];
                same = ts.b->key == out.tex[k].key && ts.cnt == out.tex[k].v.size();
            }
            if (!same) {
                structural = true;
                break;
            }
            const bool stampChanged = r.stamp != g->stamp;
            for (int s = 0; s < kFlatStreams; ++s) {
                const Slice sl = r.s[s];
                if (!sl.cnt) continue;
                GpuVertex* dst = fs[s]->v.data() + sl.off;
                if (s == kLines && hide) {
                    if (!r.linesHidden) {
                        for (uint32_t k = 1; k < sl.cnt; ++k) dst[k].x = dst[0].x, dst[k].y = dst[0].y, dst[k].z = dst[0].z;
                        fs[s]->mark(sl.off, sl.off + sl.cnt);
                        r.linesHidden = true;
                    }
                    continue;
                }
                if (!sameBytes(dst, out.f[s].data(), sl.cnt)) {
                    std::memcpy(dst, out.f[s].data(), sl.cnt * sizeof(GpuVertex));
                    fs[s]->mark(sl.off, sl.off + sl.cnt);
                }
                if (s == kLines) r.linesHidden = out.linesHidden;
            }
            for (size_t k = 0; k < out.texUsed; ++k) {
                const TexSlice& ts = d.texSlices[r.texFirst + k];
                GpuTexVertex* dst = ts.b->v.data() + ts.off;
                if (!sameBytes(dst, out.tex[k].v.data(), ts.cnt)) {
                    std::memcpy(dst, out.tex[k].v.data(), ts.cnt * sizeof(GpuTexVertex));
                    ts.b->mark(ts.off, ts.off + ts.cnt);
                    if (stampChanged) ts.b->moved = true;
                    if (ts.b->sway && stampChanged) d.swayDirty.push_back(ts.b);
                }
            }
            if (stampChanged) {
                const size_t run = i / kRunItems;
                if (run < d.runBox.size()) {
                    AABB exact;
                    for (size_t k = run * kRunItems; k < std::min(n, (run + 1) * kRunItems); ++k)
                        if (items[k]->bounds.valid()) exact.add(items[k]->bounds);
                    d.runBox[run] = exact;
                }
            }
            // The selected item's on-top outlines: a small stream, spliced.
            {
                auto it = std::lower_bound(d.selEntries.begin(), d.selEntries.end(), uint32_t(i), [](const SelEntry& e, uint32_t v) { return e.item < v; });
                const bool has = it != d.selEntries.end() && it->item == uint32_t(i);
                auto& sv = selLines.v;
                if (has && it->cnt == out.sel.size() && !out.sel.empty()) {
                    if (!sameBytes(sv.data() + it->off, out.sel.data(), it->cnt)) {
                        std::memcpy(sv.data() + it->off, out.sel.data(), it->cnt * sizeof(GpuVertex));
                        selLines.mark(it->off, it->off + it->cnt);
                    }
                } else if (has || !out.sel.empty()) {
                    uint32_t off = has ? it->off : (it == d.selEntries.begin() ? 0u : (it - 1)->off + (it - 1)->cnt);
                    const int64_t delta = int64_t(out.sel.size()) - (has ? int64_t(it->cnt) : 0);
                    if (has) sv.erase(sv.begin() + off, sv.begin() + off + it->cnt);
                    sv.insert(sv.begin() + off, out.sel.begin(), out.sel.end());
                    if (has && out.sel.empty()) {
                        it = d.selEntries.erase(it);
                    } else if (has) {
                        it->cnt = uint32_t(out.sel.size());
                        ++it;
                    } else {
                        it = d.selEntries.insert(it, SelEntry{uint32_t(i), off, uint32_t(out.sel.size())});
                        ++it;
                    }
                    for (; it != d.selEntries.end(); ++it) it->off = uint32_t(int64_t(it->off) + delta);
                    selLines.mark(off, sv.size());
                }
            }
            if (stampChanged) moved = true;
            r.stamp = g->stamp;
            r.sel = sel;
            changed = true;
            ++inPlace_;
            ++generated_;
        }
        if (d.staleMaterial) {
            full = true;
        } else if (structural) {
            // Items added, removed or with other vertex counts: the streams they are in are put together again from
            // the old slices and the newly made items.
            const size_t oldN = d.recs.size();
            d.where.clear();
            d.where.reserve(oldN);
            for (size_t i = 0; i < oldN; ++i) d.where.emplace(d.recs[i].g, uint32_t(i));
            d.plan.assign(n, Plan{});
            d.oldUsed.assign(oldN, 0);
            size_t genUsed = 0;
            const size_t limit = std::max<size_t>(64, n / 4);
            int64_t lastOld = -1;
            const bool anySel2 = !doc.selection().empty();
            bool ok = true;
            for (size_t i = 0; i < n; ++i) {
                const PrimGeom* g = items[i];
                const bool sel = anySel2 && doc.isSelected(g->ref);
                d.plan[i].sel = sel;
                if (auto it = d.where.find(g); it != d.where.end()) {
                    const ItemRec& r = d.recs[it->second];
                    if (r.stamp == g->stamp && r.sel == sel && int64_t(it->second) > lastOld && !d.oldUsed[it->second]) {
                        d.plan[i].old = int32_t(it->second);
                        lastOld = it->second;
                        d.oldUsed[it->second] = 1;
                        continue;
                    }
                }
                if (genUsed >= limit) {
                    ok = false;
                    break;
                }
                if (d.genPool.size() <= genUsed) d.genPool.emplace_back();
                d.genItem(g, sel, d.genPool[genUsed]);
                if (d.staleMaterial) {
                    ok = false;
                    break;
                }
                d.plan[i].gen = int32_t(genUsed++);
            }
            if (!ok) {
                full = true;
            } else {
                bool aff[kFlatStreams] = {false, false, false, false};
                size_t firstChange[kFlatStreams] = {SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
                for (auto& [k, b] : d.byKey) b->affected = false, b->seen = false, b->firstChange = SIZE_MAX, b->scratch.clear();
                for (size_t o = 0; o < oldN; ++o) {
                    if (d.oldUsed[o]) continue;
                    const ItemRec& r = d.recs[o];
                    for (int s = 0; s < kFlatStreams; ++s) aff[s] = aff[s] || r.s[s].cnt > 0;
                    for (uint32_t k = 0; k < r.texCount; ++k) d.texSlices[r.texFirst + k].b->affected = true;
                }
                for (size_t gI = 0; gI < genUsed; ++gI) {
                    const ItemOut& out = d.genPool[gI];
                    for (int s = 0; s < kFlatStreams; ++s) aff[s] = aff[s] || !out.f[s].empty();
                    for (size_t k = 0; k < out.texUsed; ++k)
                        if (auto it = d.byKey.find(out.tex[k].key); it != d.byKey.end()) it->second->affected = true;
                }
                for (int s = 0; s < kFlatStreams; ++s)
                    if (aff[s]) {
                        fs[s]->scratch.clear();
                        fs[s]->scratch.reserve(fs[s]->v.size() + 4096);
                    }
                selLines.scratch.clear();
                d.newRecs.assign(n, ItemRec{});
                d.newTex.clear();
                d.newSel.clear();
                d.newOrder.clear();
                for (size_t i = 0; i < n; ++i) {
                    const PrimGeom* g = items[i];
                    ItemRec& nr = d.newRecs[i];
                    nr.g = g;
                    nr.stamp = g->stamp;
                    nr.sel = d.plan[i].sel;
                    nr.texFirst = uint32_t(d.newTex.size());
                    if (d.plan[i].old >= 0) {
                        const ItemRec& o = d.recs[size_t(d.plan[i].old)];
                        nr.linesHidden = o.linesHidden;
                        for (int s = 0; s < kFlatStreams; ++s) {
                            if (!aff[s]) {
                                nr.s[s] = o.s[s];
                                continue;
                            }
                            auto& sc2 = fs[s]->scratch;
                            const uint32_t noff = uint32_t(sc2.size());
                            if (o.s[s].cnt) {
                                sc2.insert(sc2.end(), fs[s]->v.begin() + o.s[s].off, fs[s]->v.begin() + o.s[s].off + o.s[s].cnt);
                                if (noff != o.s[s].off) firstChange[s] = std::min(firstChange[s], size_t(noff));
                            }
                            nr.s[s] = {noff, o.s[s].cnt};
                        }
                        for (uint32_t k = 0; k < o.texCount; ++k) {
                            const TexSlice& ts = d.texSlices[o.texFirst + k];
                            SceneBatch* b = ts.b;
                            if (!b->seen) b->seen = true, d.newOrder.push_back(b);
                            uint32_t noff = ts.off;
                            if (b->affected) {
                                noff = uint32_t(b->scratch.size());
                                b->scratch.insert(b->scratch.end(), b->v.begin() + ts.off, b->v.begin() + ts.off + ts.cnt);
                                if (noff != ts.off) b->firstChange = std::min(b->firstChange, size_t(noff));
                            }
                            d.newTex.push_back({b, noff, ts.cnt});
                        }
                        if (o.sel) {
                            const uint32_t oi = uint32_t(d.plan[i].old);
                            auto it = std::lower_bound(d.selEntries.begin(), d.selEntries.end(), oi, [](const SelEntry& e, uint32_t v) { return e.item < v; });
                            if (it != d.selEntries.end() && it->item == oi) {
                                const uint32_t noff = uint32_t(selLines.scratch.size());
                                selLines.scratch.insert(selLines.scratch.end(), selLines.v.begin() + it->off, selLines.v.begin() + it->off + it->cnt);
                                d.newSel.push_back({uint32_t(i), noff, it->cnt});
                            }
                        }
                    } else {
                        const ItemOut& out = d.genPool[size_t(d.plan[i].gen)];
                        nr.linesHidden = out.linesHidden;
                        for (int s = 0; s < kFlatStreams; ++s) {
                            if (out.f[s].empty()) continue;
                            auto& sc2 = fs[s]->scratch;
                            const uint32_t noff = uint32_t(sc2.size());
                            sc2.insert(sc2.end(), out.f[s].begin(), out.f[s].end());
                            nr.s[s] = {noff, uint32_t(out.f[s].size())};
                            firstChange[s] = std::min(firstChange[s], size_t(noff));
                        }
                        for (size_t k = 0; k < out.texUsed; ++k) {
                            bool created = false;
                            SceneBatch* b = d.batchFor(out.tex[k], created);
                            if (created) b->affected = true, b->seen = false, b->firstChange = 0, b->scratch.clear();
                            if (!b->seen) b->seen = true, d.newOrder.push_back(b);
                            const uint32_t noff = uint32_t(b->scratch.size());
                            b->scratch.insert(b->scratch.end(), out.tex[k].v.begin(), out.tex[k].v.end());
                            b->firstChange = std::min(b->firstChange, size_t(noff));
                            d.newTex.push_back({b, noff, uint32_t(out.tex[k].v.size())});
                        }
                        if (!out.sel.empty()) {
                            const uint32_t noff = uint32_t(selLines.scratch.size());
                            selLines.scratch.insert(selLines.scratch.end(), out.sel.begin(), out.sel.end());
                            d.newSel.push_back({uint32_t(i), noff, uint32_t(out.sel.size())});
                        }
                        ++generated_;
                    }
                    nr.texCount = uint32_t(d.newTex.size()) - nr.texFirst;
                }
                // The old vector of a stream stays as the work vector of the next time, unless it is huge.
                constexpr size_t kKeepScratchBytes = size_t(64) << 20;
                for (int s = 0; s < kFlatStreams; ++s) {
                    if (!aff[s]) continue;
                    fs[s]->v.swap(fs[s]->scratch);
                    if (firstChange[s] != SIZE_MAX) fs[s]->mark(firstChange[s], fs[s]->v.size());
                    if (fs[s]->scratch.capacity() * sizeof(GpuVertex) > kKeepScratchBytes) std::vector<GpuVertex>().swap(fs[s]->scratch);
                }
                for (SceneBatch* b : d.newOrder) {
                    if (!b->affected) continue;
                    b->moved = true;
                    b->v.swap(b->scratch);
                    b->scratch.clear();
                    if (b->scratch.capacity() * sizeof(GpuTexVertex) > kKeepScratchBytes) std::vector<GpuTexVertex>().swap(b->scratch);
                    if (b->firstChange != SIZE_MAX) b->mark(b->firstChange, b->v.size());
                    b->structural = true;
                    if (b->sway) d.swayDirty.push_back(b);
                }
                for (auto it = d.byKey.begin(); it != d.byKey.end();) {
                    if (!it->second->seen) it = d.byKey.erase(it);
                    else ++it;
                }
                order_ = d.newOrder;
                if (selLines.v.size() != selLines.scratch.size() || !sameBytes(selLines.v.data(), selLines.scratch.data(), selLines.v.size())) {
                    selLines.v.swap(selLines.scratch);
                    selLines.markAll();
                }
                d.recs.swap(d.newRecs);
                d.texSlices.swap(d.newTex);
                d.selEntries.swap(d.newSel);
                // Keep a few item buffers for the next time, not the memory of a big paste.
                if (d.genPool.size() > 8) d.genPool.resize(8);
                changed = layoutChanged = moved = true;
                ++relayouts_;
            }
        }
    }

    if (full) {
        // Everything again, in item order (what buildCameraGeometry makes).
        IW8R_PROF("g.full");
        d.incremental = false;
        d.staleMaterial = false;
        d.mats.clear();
        for (auto& [k, b] : d.byKey) b->seen = false;
        // Batches are made again (their texture ids and emissive maps are asked again); the objects of keys that
        // come back are reused so the renderer keeps their buffers.
        std::unordered_map<int, std::unique_ptr<SceneBatch>> pool;
        pool.swap(d.byKey);
        order_.clear();
        for (auto* s : fs) s->v.clear();
        selLines.v.clear();
        d.selEntries.clear();
        d.texSlices.clear();
        d.recs.assign(n, ItemRec{});
        const bool anySel = !doc.selection().empty();
        for (size_t i = 0; i < n; ++i) {
            const PrimGeom* g = items[i];
            const bool sel = anySel && doc.isSelected(g->ref);
            ItemOut& out = d.out;
            d.genItem(g, sel, out);
            ItemRec& r = d.recs[i];
            r.g = g;
            r.stamp = g->stamp;
            r.sel = sel;
            r.linesHidden = out.linesHidden;
            for (int s = 0; s < kFlatStreams; ++s) {
                if (out.f[s].empty()) continue;
                r.s[s] = {uint32_t(fs[s]->v.size()), uint32_t(out.f[s].size())};
                fs[s]->v.insert(fs[s]->v.end(), out.f[s].begin(), out.f[s].end());
            }
            r.texFirst = uint32_t(d.texSlices.size());
            for (size_t k = 0; k < out.texUsed; ++k) {
                const ItemOut::Tex& t = out.tex[k];
                SceneBatch* b = nullptr;
                if (auto it = d.byKey.find(t.key); it != d.byKey.end()) {
                    b = it->second.get();
                } else {
                    std::unique_ptr<SceneBatch> nb;
                    if (auto p = pool.find(t.key); p != pool.end()) {
                        nb = std::move(p->second);
                        pool.erase(p);
                        nb->v.clear();
                    } else {
                        nb = std::make_unique<SceneBatch>();
                    }
                    nb->key = t.key;
                    nb->texture = t.tex;
                    nb->normalTexture = t.nrm;
                    nb->blend = t.blend;
                    nb->sway = t.sway;
                    nb->baseZ = 0;
                    nb->emissiveTexture = scene.emissiveFor && !scene.captureMode && t.material ? scene.emissiveFor(*t.material) : -1;
                    nb->specTexture = scene.specFor && !scene.captureMode && t.material ? scene.specFor(*t.material) : -1;
                    nb->shader = scene.shaderFor && t.material ? scene.shaderFor(*t.material) : -1;
                    b = d.byKey.emplace(t.key, std::move(nb)).first->second.get();
                    order_.push_back(b);
                }
                d.texSlices.push_back({b, uint32_t(b->v.size()), uint32_t(t.v.size())});
                b->v.insert(b->v.end(), t.v.begin(), t.v.end());
            }
            r.texCount = uint32_t(d.texSlices.size()) - r.texFirst;
            if (!out.sel.empty()) {
                d.selEntries.push_back({uint32_t(i), uint32_t(selLines.v.size()), uint32_t(out.sel.size())});
                selLines.v.insert(selLines.v.end(), out.sel.begin(), out.sel.end());
            }
        }
        // Memory of a bigger earlier build (another view mode) is given back.
        const auto fit = [](auto& v) {
            if (v.capacity() > v.size() * 2 + 65536) v.shrink_to_fit();
        };
        for (auto* s : fs) {
            s->markAll();
            fit(s->v);
            if (!s->scratch.empty() || s->scratch.capacity() > 65536) std::vector<GpuVertex>().swap(s->scratch);
        }
        selLines.markAll();
        fit(selLines.v);
        for (SceneBatch* b : order_) {
            b->markAll();
            b->structural = true;
            b->moved = true;  // (the renderer still keeps its chunks when a hash of the positions is unchanged)
            fit(b->v);
            if (b->scratch.capacity() > 65536) std::vector<GpuTexVertex>().swap(b->scratch);
            if (b->sway) d.swayDirty.push_back(b);
        }
        generated_ += n;
        changed = layoutChanged = moved = true;
        ++fullBuilds_;
    }
    d.globalKey = key;
    d.built = true;
    d.incremental = false;

    // Foliage batches sway above their lowest point.
    for (SceneBatch* b : d.swayDirty) {
        if (b->v.empty()) continue;
        float z = b->v.front().z;
        for (const auto& v : b->v) z = std::min(z, v.z);
        b->baseZ = z;
    }
    d.swayDirty.clear();

    // The box around everything, from the boxes of runs of items (made again when slices moved; an item written in
    // place made its run again above).
    if (layoutChanged || d.runBox.size() != (n + kRunItems - 1) / kRunItems) {
        d.runBox.assign((n + kRunItems - 1) / kRunItems, AABB{});
        for (size_t i = 0; i < n; ++i)
            if (items[i]->bounds.valid()) d.runBox[i / kRunItems].add(items[i]->bounds);
    }
    if (moved || !bounds_.valid()) {
        AABB all;
        for (const AABB& b : d.runBox)
            if (b.valid()) all.add(b);
        bounds_ = all;
    }

    // Helpers and on-top lines that do not belong to one item: small, made again on every change of the document
    // (which includes the selection) and replaced only when they differ.
    {
        IW8R_PROF("g.misc");
        d.mSun.clear(), d.mFace.clear(), d.mOverlay.clear(), d.mLines.clear();
        if (scene.litPreview && bounds_.valid()) {
            // Sun direction (Sun & Sky): a yellow arrow from the middle of the map toward the sun.
            const Vec3 c = bounds_.center();
            const double len = std::max(128.0, length(bounds_.maxs - bounds_.mins) * 0.4);
            const Vec3 tip = c + scene.sunToward * len;
            const uint32_t yellow = 0xff20e0ffu;
            auto seg = [&](const Vec3& a, const Vec3& b) {
                d.mSun.push_back({float(a.x), float(a.y), float(a.z), yellow});
                d.mSun.push_back({float(b.x), float(b.y), float(b.z), yellow});
            };
            seg(c, tip);
            const double r = len * 0.06;
            seg(tip - Vec3{r, 0, 0}, tip + Vec3{r, 0, 0});
            seg(tip - Vec3{0, r, 0}, tip + Vec3{0, r, 0});
            seg(tip - Vec3{0, 0, r}, tip + Vec3{0, 0, r});
        }
        // Selected faces: tinted fill drawn on top plus an outline and a cross.
        for (const FaceRef& f : doc.faceSelection()) {
            const EntityItem* it = doc.findItem(f.entity, f.item);
            const Primitive* p = it ? it->prim() : nullptr;
            if (!p || f.face >= p->faces.size()) continue;
            const auto wins = faceWindingsOf(*p);
            const Winding& w = wins[f.face];
            if (w.size() < 3) continue;
            const auto pl = facePlane(*p, f.face);
            const Vec3 lift = pl ? pl->n * 0.05 : Vec3{};
            for (size_t k = 1; !d.wire && k + 1 < w.size(); ++k) {
                d.mFace.push_back(gv(w[0] + lift, kSelRed));
                d.mFace.push_back(gv(w[k] + lift, kSelRed));
                d.mFace.push_back(gv(w[k + 1] + lift, kSelRed));
            }
            Vec3 c;
            for (const auto& v : w) c += v;
            c = c * (1.0 / double(w.size()));
            for (size_t k = 0; k < w.size(); ++k) {
                d.mOverlay.push_back(gv(w[k], kSelEdge));
                d.mOverlay.push_back(gv(w[(k + 1) % w.size()], kSelEdge));
                d.mOverlay.push_back(gv(w[k], kSelEdge));
                d.mOverlay.push_back(gv(c, kSelEdge));
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
                d.mLines.push_back(gv(p, teal));
                d.mLines.push_back(gv(q, teal));
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
                    for (const Vec3& dd : {Vec3{k, 0, 0}, Vec3{0, k, 0}, Vec3{0, 0, k}}) {
                        d.mOverlay.push_back(gv(p - dd, tc));
                        d.mOverlay.push_back(gv(p + dd, tc));
                    }
                }
            }
        }
        for (const UmbraLine& l : scene.tomeLines) {  // Umbra cells and portals (M2.2)
            d.mLines.push_back(gv(l.a, l.rgba));
            d.mLines.push_back(gv(l.b, l.rgba));
        }
        for (const auto& [a, b] : scene.regionLines) {  // the active Region's box, amber
            d.mLines.push_back(gv(a, 0xff30b4ffu));
            d.mLines.push_back(gv(b, 0xff30b4ffu));
        }
        for (const auto& [a, b] : scene.navLines) {  // the build's navmesh (6.23), green
            d.mLines.push_back(gv(a, 0xff30e060u));
            d.mLines.push_back(gv(b, 0xff30e060u));
        }
        for (const auto& [a, b] : scene.compiledWorld) {  // the build's render surfaces, magenta
            d.mLines.push_back(gv(a, 0xffff50e0u));
            d.mLines.push_back(gv(b, 0xffff50e0u));
        }
        for (const auto& h : scene.collisionPreview) {
            // Solid cyan, player clip orange, ladder green, anything else magenta (R in the low byte).
            const uint32_t c = h.ladder ? 0xff40ff40u : (h.contents & 0x10000u) && !(h.contents & 1u) ? 0xff2090ffu
                               : (h.contents & 1u) ? 0xffffe040u : 0xffff40ffu;
            for (const auto& [a, b] : h.edges) {
                d.mLines.push_back(gv(a, c));
                d.mLines.push_back(gv(b, c));
            }
        }
        for (size_t i = 0; i + 1 < scene.pointfile.size(); ++i) {
            d.mOverlay.push_back(gv(scene.pointfile[i], 0xff20d0ffu));
            d.mOverlay.push_back(gv(scene.pointfile[i + 1], 0xff20d0ffu));
        }
        if (showSpawns) {
            const GameModeView* mode = findGameModeView(scene.gameMode);
            for (const auto& s : scene.spawns) {
                if (mode && !gameModeKeepsSpawn(*mode, s.name)) continue;
                const uint32_t c = 0xff40ff40u;
                const Vec3 o = s.origin;
                const Vec3 mn = o + Vec3{-16, -16, 0}, mx = o + Vec3{16, 16, 72};
                const Vec3 b[8] = {{mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
                                   {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}};
                const int e[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
                for (const auto& ed : e) {
                    d.mLines.push_back(gv(b[ed[0]], c));
                    d.mLines.push_back(gv(b[ed[1]], c));
                }
                const double y = s.angles.y * 3.14159265358979 / 180.0;
                d.mLines.push_back(gv(o + Vec3{0, 0, 36}, c));
                d.mLines.push_back(gv(o + Vec3{std::cos(y) * 40, std::sin(y) * 40, 36}, c));
            }
        }
        // Entity connections (target -> targetname).
        for (const auto& [a, b] : scene.connections()) {
            d.mOverlay.push_back(gv(a, 0xff30d0ffu));
            d.mOverlay.push_back(gv(b, 0xff30d0ffu));
        }
        // World axes at the origin (X red, Y green, Z blue).
        d.mLines.push_back(gv({0, 0, 0}, 0xff3030ffu));
        d.mLines.push_back(gv({64, 0, 0}, 0xff3030ffu));
        d.mLines.push_back(gv({0, 0, 0}, 0xff30ff30u));
        d.mLines.push_back(gv({0, 64, 0}, 0xff30ff30u));
        d.mLines.push_back(gv({0, 0, 0}, 0xffff3030u));
        d.mLines.push_back(gv({0, 0, 64}, 0xffff3030u));
        if (scene.captureMode) d.mLines.clear(), d.mOverlay.clear(), d.mSun.clear();
        const auto put = [&](GeoStream<GpuVertex>& s, std::vector<GpuVertex>& v) {
            if (s.v.size() == v.size() && sameBytes(s.v.data(), v.data(), v.size())) return false;
            s.v.swap(v);
            s.markAll();
            return true;
        };
        if (put(faceSel, d.mFace)) changed = moved = true;  // the fill is drawn in the shadow pass too
        if (put(sunArrow, d.mSun)) changed = true;
        if (put(overlayMisc, d.mOverlay)) changed = true;
        if (put(linesMisc, d.mLines)) changed = true;
    }
    if (moved) ++casterVersion_;
    scene.shadowOnlyVertices = shadowOnly.v.size();
    scene.shadowCasterVertices = shadowOnly.v.size() + casters.v.size() + faceSel.v.size();
    return changed;
}

void CameraGeometry::flatten(std::vector<GpuVertex>& tris, std::vector<GpuVertex>& linesOut, std::vector<GpuVertex>& overlay,
                             std::vector<TexturedBatch>& textured) const {
    const Impl& d = *d_;
    tris.clear();
    tris.insert(tris.end(), shadowOnly.v.begin(), shadowOnly.v.end());
    tris.insert(tris.end(), casters.v.begin(), casters.v.end());
    tris.insert(tris.end(), faceSel.v.begin(), faceSel.v.end());
    tris.insert(tris.end(), noCast.v.begin(), noCast.v.end());
    linesOut.clear();
    for (const ItemRec& r : d.recs) {
        if (r.linesHidden || !r.s[kLines].cnt) continue;
        linesOut.insert(linesOut.end(), lines.v.begin() + r.s[kLines].off, lines.v.begin() + r.s[kLines].off + r.s[kLines].cnt);
    }
    linesOut.insert(linesOut.end(), linesMisc.v.begin(), linesMisc.v.end());
    overlay.clear();
    overlay.insert(overlay.end(), sunArrow.v.begin(), sunArrow.v.end());
    overlay.insert(overlay.end(), selLines.v.begin(), selLines.v.end());
    overlay.insert(overlay.end(), overlayMisc.v.begin(), overlayMisc.v.end());
    textured.clear();
    for (const SceneBatch* b : order_) {
        TexturedBatch t;
        t.texture = b->texture;
        t.tris = b->v;
        t.normalTexture = b->normalTexture;
        t.emissiveTexture = b->emissiveTexture;
        t.specTexture = b->specTexture;
        t.blend = b->blend;
        t.sway = b->sway;
        t.baseZ = b->baseZ;
        textured.push_back(std::move(t));
    }
}

}  // namespace iw8r::app
