#include "game_lightmap.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <unordered_map>

#include "compile_iw8.h"
#include "scene.h"

namespace iw8r::app {

namespace {

uint64_t mixKey(uint64_t h, int64_t v) {
    h ^= uint64_t(v) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h * 0x100000001b3ull;
}

bool isSkyMaterial(const std::string& m) {
    std::string l = m;
    std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return l.find("sky") != std::string::npos;
}

}  // namespace

uint64_t GameLightmap::vertexKey(const Vec3& pos, const Vec3& normal, const Vec3& centroid) {
    uint64_t h = 1469598103934665603ull;
    for (const double v : {pos.x, pos.y, pos.z, centroid.x, centroid.y, centroid.z}) h = mixKey(h, std::llround(v * 16.0));
    for (const double v : {normal.x, normal.y, normal.z}) h = mixKey(h, std::llround(v * 256.0));
    return h;
}

bool GameLightmap::uvFor(const Vec3& pos, const Vec3& normal, const Vec3& centroid, float out[2]) const {
    if (uv.empty()) return false;  // nothing baked (a stock map without own brushes): no key to make
    const auto it = uv.find(vertexKey(pos, normal, centroid));
    if (it == uv.end()) return false;
    out[0] = it->second[0], out[1] = it->second[1];
    return true;
}

std::shared_ptr<StockLightmapLayers> stockLightmapLayers(const StockLightmap& lm) {
    auto out = std::make_shared<StockLightmapLayers>();
    const size_t n = size_t(std::max(lm.width, 0)) * size_t(std::max(lm.height, 0));
    if (!n || lm.irradiance.size() < n * 3 || lm.ambientFraction.size() < n || lm.direction.size() < n * 3) return out;
    out->width = lm.width, out->height = lm.height;
    out->a.resize(n * 4), out->b.resize(n * 4), out->d.resize(n * 4);
    for (size_t i = 0; i < n; ++i) {
        const float f = std::clamp(lm.ambientFraction[i], 0.0f, 1.0f);
        const float dz = std::max(lm.direction[i * 3 + 2], 0.001f);
        for (int c = 0; c < 3; ++c) {
            const float C = lm.irradiance[i * 3 + c];
            out->a[i * 4 + c] = f * C;
            out->b[i * 4 + c] = (1.0f - f) * C / dz;
            out->d[i * 4 + c] = lm.direction[i * 3 + c];
        }
        out->a[i * 4 + 3] = 1.0f;
        out->b[i * 4 + 3] = 0.0f;
        out->d[i * 4 + 3] = 0.0f;
    }
    out->rects.assign(size_t(std::max(lm.index, 0)) + 1, {0, 0, 0, 0});
    out->rects[size_t(std::max(lm.index, 0))] = {0, 0, 1, 1};
    double sum[3] = {0, 0, 0};
    size_t lit = 0;
    for (size_t i = 0; i < n; ++i) {
        const float* c = &lm.irradiance[i * 3];
        if (c[0] + c[1] + c[2] <= 1e-4f) continue;  // padding and unused texels
        for (int k = 0; k < 3; ++k) sum[k] += c[k];
        ++lit;
    }
    if (lit)
        for (int k = 0; k < 3; ++k) out->meanIrradiance[size_t(k)] = float(sum[k] / double(lit));
    return out;
}

std::shared_ptr<StockLightmapLayers> stockLightmapAtlas(const std::vector<StockLightmap>& lms) {
    std::vector<const StockLightmap*> list;
    for (const StockLightmap& l : lms)
        if (l.width > 0 && l.height > 0 && l.index >= 0) list.push_back(&l);
    if (list.size() == 1 && list[0]->index == 0) return stockLightmapLayers(*list[0]);
    auto out = std::make_shared<StockLightmapLayers>();
    if (list.empty()) return out;
    std::sort(list.begin(), list.end(), [](const StockLightmap* x, const StockLightmap* y) { return x->height > y->height; });
    // Shelf packing into the narrowest power-of-two width that keeps the atlas roughly square.
    size_t area = 0;
    int widest = 0, maxIndex = 0;
    for (const StockLightmap* l : list) area += size_t(l->width + 1) * size_t(l->height + 1), widest = std::max(widest, l->width + 1),
                                        maxIndex = std::max(maxIndex, l->index);
    int W = 256;
    while (W < widest || size_t(W) * size_t(W) < area) W *= 2;
    W = std::min(W, 16384);
    struct Place { const StockLightmap* l; int x, y; };
    double weight = 0;
    std::vector<Place> places;
    int x = 0, y = 0, shelf = 0;
    for (const StockLightmap* l : list) {
        if (x + l->width > W) x = 0, y += shelf + 1, shelf = 0;
        places.push_back({l, x, y});
        x += l->width + 1;
        shelf = std::max(shelf, l->height);
    }
    const int H = y + shelf;
    if (H > 16384) return out;  // too large for one texture: leave the lightmaps out
    out->width = W, out->height = H;
    const size_t n = size_t(W) * size_t(H);
    out->a.assign(n * 4, 0.0f), out->b.assign(n * 4, 0.0f), out->d.assign(n * 4, 0.0f);
    out->rects.assign(size_t(maxIndex) + 1, {0, 0, 0, 0});
    for (const Place& p : places) {
        const auto one = stockLightmapLayers(*p.l);
        if (!one || one->width <= 0) continue;
        for (int r = 0; r < one->height; ++r) {
            const size_t src = size_t(r) * size_t(one->width) * 4, dst = (size_t(p.y + r) * size_t(W) + size_t(p.x)) * 4;
            std::copy_n(one->a.begin() + src, size_t(one->width) * 4, out->a.begin() + dst);
            std::copy_n(one->b.begin() + src, size_t(one->width) * 4, out->b.begin() + dst);
            std::copy_n(one->d.begin() + src, size_t(one->width) * 4, out->d.begin() + dst);
        }
        out->rects[size_t(p.l->index)] = {float(p.x) / W, float(p.y) / H, float(p.l->width) / W, float(p.l->height) / H};
        const double texels = double(p.l->width) * double(p.l->height);
        for (int k = 0; k < 3; ++k) out->meanIrradiance[size_t(k)] += float(one->meanIrradiance[size_t(k)] * texels);
        weight += texels;
    }
    if (weight > 0)
        for (float& m : out->meanIrradiance) m = float(m / weight);
    return out;
}

uint64_t gameBakeGeometryKey(const SceneCache& scene) {
    uint64_t key = 1469598103934665603ull;
    for (const PrimGeom* g : scene.items()) key = mixKey(key, int64_t(g->stamp));
    return key;
}

GameBakeInput gatherGameBake(const SceneCache& scene, const std::function<int(const std::string&)>& drawClassFor,
                             const std::function<Vec3(const std::string&)>& albedoFor) {
    GameBakeInput in;
    uint64_t key = 1469598103934665603ull;
    // Per material, asked once per gather: the draw class (cache lookups and technique-set parsing) was asked for every
    // polygon, seconds on the GUI thread on a stock map each time more textures arrived (stall watch, 2026-11-15).
    struct MatInfo {
        int draw = 0;
        bool tool = false, sky = false, albedoKnown = false;
        Vec3 albedo;
    };
    std::unordered_map<std::string, MatInfo> infos;
    const std::string* lastName = nullptr;
    MatInfo* last = nullptr;
    const auto infoOf = [&](const std::string& m) -> MatInfo& {
        if (last && (lastName == &m || *lastName == m)) return *last;  // runs of one material
        auto [it, fresh] = infos.try_emplace(m);
        if (fresh) {
            it->second.draw = !m.empty() && drawClassFor ? drawClassFor(m) : 0;
            it->second.tool = isIw8ToolMaterial(m);
            it->second.sky = isSkyMaterial(m);
        }
        lastName = &it->first, last = &it->second;
        return it->second;
    };
    for (const PrimGeom* g : scene.items()) {
        key = mixKey(key, int64_t(g->stamp));
        if (g->volume || (g->kind == PrimGeom::PointEntity && !g->prefab && !g->mesh)) continue;  // editor markers
        for (size_t i = 0; i < g->polys.size(); ++i) {
            const Winding& w = g->polys[i];
            if (w.size() < 3 || g->markerPoly(i)) continue;
            const std::string* mat = i < g->polyMaterial.size() ? &g->polyMaterial[i] : nullptr;
            MatInfo* info = mat ? &infoOf(*mat) : nullptr;
            const int draw = info ? info->draw : 0;
            if (draw == 2) continue;
            const bool tool = info && info->tool;
            const bool sky = info && info->sky;
            const bool model = g->meshPoly(i) || g->kind == PrimGeom::PointEntity;
            // The game lightmaps brush faces and patches it draws; models, shadow-only meshes and the sky cast shadows.
            if (!model && !tool && !sky && draw == 0 && mat && !mat->empty()) {
                GameBakeInput::Poly p;
                p.pts = w;
                p.normal = i < g->faceNormals.size() ? g->faceNormals[i] : Vec3{0, 0, 1};
                Vec3 c;
                for (const Vec3& v : w) c += v;
                p.centroid = c * (1.0 / double(w.size()));
                if (albedoFor) {
                    if (!info->albedoKnown) info->albedo = albedoFor(*mat), info->albedoKnown = true;
                    p.albedo = info->albedo;
                }
                if (i < g->polyLmDensity.size()) p.density = g->polyLmDensity[i];
                in.polys.push_back(std::move(p));
            } else if (!tool || sky) {
                for (size_t k = 1; k + 1 < w.size(); ++k) in.occluders.push_back({w[0], w[k], w[k + 1], 0});
            }
        }
    }
    in.key = key;
    return in;
}

std::shared_ptr<GameLightmap> bakeGameLightmap(const GameBakeInput& in, const std::function<void(double)>& progress) {
    auto out = std::make_shared<GameLightmap>();
    out->key = in.key;
    const auto started = std::chrono::steady_clock::now();
    // One bake surface holds every lit polygon (fans); charts grow across polygons that meet at equal positions, as
    // the build's do. vertexOf maps each fan vertex back to its polygon for the UV table.
    BakeScene scene = in.scene;
    scene.surfaces.clear();
    scene.extraOccluders = in.occluders;
    BakeSurface s;
    struct Src {
        uint32_t poly, corner;
    };
    std::vector<Src> src;
    for (size_t pi = 0; pi < in.polys.size(); ++pi) {
        const auto& p = in.polys[pi];
        const Vec3 e = p.pts[1] - p.pts[0];
        const Vec3 t = length(e) > 1e-9 ? normalize(e) : Vec3{1, 0, 0};
        const uint32_t base = uint32_t(s.verts.size());
        for (size_t k = 0; k < p.pts.size(); ++k) {
            s.verts.push_back({p.pts[k], p.normal, t});
            src.push_back({uint32_t(pi), uint32_t(k)});
        }
        for (size_t k = 1; k + 1 < p.pts.size(); ++k) {
            s.idx.push_back(base);
            s.idx.push_back(base + uint32_t(k));
            s.idx.push_back(base + uint32_t(k + 1));
            s.triAlbedo.push_back(p.albedo);
            s.triDensity.push_back(p.density);
        }
    }
    out->polys = in.polys.size();
    if (s.idx.empty()) {
        out->error = "nothing to light: no drawn brush faces or patches";
        return out;
    }
    scene.surfaces.push_back(std::move(s));
    const BakeResult r = bakeLightmap(scene, in.settings, progress);
    if (!r.ok) {
        out->error = r.error;
        return out;
    }
    // Vertex UVs by polygon corner, from the triangles (each polygon has its own vertices and is planar, so it lies in
    // one chart and no corner is split).
    for (size_t t = 0; t + 2 < r.surfaces[0].idx.size(); t += 3)
        for (int k = 0; k < 3; ++k) {
            const uint32_t vi = r.surfaces[0].idx[t + k];
            const Src& sv = src[r.sourceVertex[0][vi]];
            const auto& p = in.polys[sv.poly];
            out->uv[GameLightmap::vertexKey(p.pts[sv.corner], p.normal, p.centroid)] = r.uv[0][vi];
        }
    // The layers as the game reads them: the IW3 pair's bytes decoded with the log encoding.
    const EncodedLightmap enc = encodeLightmapPair(r);
    const double denom = std::log2(1.0 + enc.logScale * enc.maximum);
    const auto dec = [&](uint8_t byte) { return float((std::exp2(byte / 255.0 * denom) - 1.0) / enc.logScale); };
    const size_t n = size_t(r.width) * size_t(r.height);
    out->width = r.width, out->height = r.height;
    out->a.resize(n * 4), out->b.resize(n * 4), out->d.resize(n * 4);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* pa = enc.secondary.data() + i * 4;
        const uint8_t* pb = enc.secondary.data() + n * 4 + i * 4;
        out->a[i * 4 + 0] = dec(pa[2]), out->a[i * 4 + 1] = dec(pa[1]), out->a[i * 4 + 2] = dec(pa[0]);
        out->a[i * 4 + 3] = enc.primary[i] / 255.0f;
        out->b[i * 4 + 0] = dec(pb[2]), out->b[i * 4 + 1] = dec(pb[1]), out->b[i * 4 + 2] = dec(pb[0]);
        out->b[i * 4 + 3] = 0;
        for (int c = 0; c < 3; ++c) out->d[i * 4 + c] = r.directionWorld[i][c];
        out->d[i * 4 + 3] = 0;
    }
    out->charts = r.charts;
    out->texels = r.texelsLit;
    out->texelSize = r.texelSize;
    out->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return out;
}

}  // namespace iw8r::app
