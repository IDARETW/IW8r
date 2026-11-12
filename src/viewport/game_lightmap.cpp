#include "game_lightmap.h"

#include <algorithm>
#include <chrono>
#include <cmath>

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
    return out;
}

GameBakeInput gatherGameBake(const SceneCache& scene, const std::function<int(const std::string&)>& drawClassFor) {
    GameBakeInput in;
    uint64_t key = 1469598103934665603ull;
    for (const PrimGeom* g : scene.items()) {
        key = mixKey(key, int64_t(g->stamp));
        if (g->volume || (g->kind == PrimGeom::PointEntity && !g->prefab && !g->mesh)) continue;  // editor markers
        for (size_t i = 0; i < g->polys.size(); ++i) {
            const Winding& w = g->polys[i];
            if (w.size() < 3 || g->markerPoly(i)) continue;
            const std::string* mat = i < g->polyMaterial.size() ? &g->polyMaterial[i] : nullptr;
            int draw = 0;
            if (mat && !mat->empty() && drawClassFor) draw = drawClassFor(*mat);
            if (draw == 2) continue;
            const bool tool = mat && isIw8ToolMaterial(*mat);
            const bool sky = mat && isSkyMaterial(*mat);
            const bool model = g->meshPoly(i) || g->kind == PrimGeom::PointEntity;
            // The game lightmaps brush faces and patches it draws; models, shadow-only meshes and the sky cast shadows.
            if (!model && !tool && !sky && draw == 0 && mat && !mat->empty()) {
                GameBakeInput::Poly p;
                p.pts = w;
                p.normal = i < g->faceNormals.size() ? g->faceNormals[i] : Vec3{0, 0, 1};
                Vec3 c;
                for (const Vec3& v : w) c += v;
                p.centroid = c * (1.0 / double(w.size()));
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
