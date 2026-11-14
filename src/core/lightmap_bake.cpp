#include "lightmap_bake.h"

#include "game_look.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <thread>
#include <unordered_map>

namespace iw8r {

namespace {

constexpr double kPi = 3.14159265358979323846;

double lum(const std::array<float, 3>& c) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; }
double lumV(const Vec3& c) { return 0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z; }

Vec3 safeNormalize(const Vec3& v, const Vec3& fallback) {
    const double l = length(v);
    return l > 1e-12 ? v * (1.0 / l) : fallback;
}

// Any unit vector perpendicular to n.
Vec3 perpendicular(const Vec3& n) {
    const Vec3 a = std::fabs(n.x) < 0.6 ? Vec3{1, 0, 0} : (std::fabs(n.y) < 0.6 ? Vec3{0, 1, 0} : Vec3{0, 0, 1});
    return safeNormalize(cross(n, a), Vec3{1, 0, 0});
}

uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

double radicalInverse(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return double(bits) * 2.3283064365386963e-10;
}

struct Chart {
    size_t surface = 0;
    std::vector<uint32_t> tris;  // triangle ordinals in the surface
    Vec3 normal, u, v;           // projection basis
    double minU = 0, minV = 0, maxU = 0, maxV = 0;  // in texels (projected / texelSize)
    int w = 0, h = 0;            // size with padding
    int x = 0, y = 0;            // atlas position
};

struct Texel {
    Vec3 pos, normal, tangent;
    int chart = -1;
};

// Shelf packing into a square of `side`; false when the charts do not fit.
bool pack(std::vector<Chart>& charts, int side) {
    std::vector<size_t> order(charts.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return charts[a].h > charts[b].h; });
    int x = 0, y = 0, shelf = 0;
    for (const size_t i : order) {
        Chart& c = charts[i];
        if (c.w > side || c.h > side) return false;
        if (x + c.w > side) {
            x = 0;
            y += shelf;
            shelf = 0;
        }
        if (y + c.h > side) return false;
        c.x = x;
        c.y = y;
        x += c.w;
        shelf = std::max(shelf, c.h);
    }
    return true;
}

}  // namespace

BakeResult bakeLightmap(const BakeScene& scene, const BakeSettings& settings, const std::function<void(double)>& progress) {
    BakeResult r;
    r.surfaces = scene.surfaces;
    r.sourceVertex.resize(r.surfaces.size());
    for (size_t si = 0; si < r.surfaces.size(); ++si) {
        r.sourceVertex[si].resize(r.surfaces[si].verts.size());
        for (size_t v = 0; v < r.sourceVertex[si].size(); ++v) r.sourceVertex[si][v] = uint32_t(v);
    }
    // ---- charts: grow over shared edges while the normals stay close
    std::vector<Chart> charts;
    for (size_t si = 0; si < r.surfaces.size(); ++si) {
        BakeSurface& s = r.surfaces[si];
        if (!s.lit) continue;
        const size_t triCount = s.idx.size() / 3;
        std::vector<Vec3> fn(triCount);
        for (size_t t = 0; t < triCount; ++t) {
            const Vec3& a = s.verts[s.idx[t * 3]].pos;
            const Vec3& b = s.verts[s.idx[t * 3 + 1]].pos;
            const Vec3& c = s.verts[s.idx[t * 3 + 2]].pos;
            fn[t] = safeNormalize(cross(b - a, c - a), Vec3{0, 0, 0});
        }
        // Edges by their quantised end points (faces of a brush have their own vertices but meet at equal positions).
        const auto key = [](const Vec3& p) {
            return std::array<long long, 3>{std::llround(p.x * 64), std::llround(p.y * 64), std::llround(p.z * 64)};
        };
        std::map<std::pair<std::array<long long, 3>, std::array<long long, 3>>, std::vector<uint32_t>> edges;
        for (size_t t = 0; t < triCount; ++t)
            for (int e = 0; e < 3; ++e) {
                auto ka = key(s.verts[s.idx[t * 3 + e]].pos), kb = key(s.verts[s.idx[t * 3 + (e + 1) % 3]].pos);
                if (kb < ka) std::swap(ka, kb);
                edges[{ka, kb}].push_back(uint32_t(t));
            }
        std::vector<std::vector<uint32_t>> neighbours(triCount);
        for (const auto& [k, list] : edges)
            for (const uint32_t a : list)
                for (const uint32_t b : list)
                    if (a != b) neighbours[a].push_back(b);
        std::vector<int> chartOf(triCount, -1);
        for (size_t t = 0; t < triCount; ++t) {
            if (chartOf[t] >= 0 || length(fn[t]) < 0.5) continue;
            Chart c;
            c.surface = si;
            c.normal = fn[t];
            std::vector<uint32_t> queue{uint32_t(t)};
            chartOf[t] = int(charts.size());
            while (!queue.empty()) {
                const uint32_t q = queue.back();
                queue.pop_back();
                c.tris.push_back(q);
                for (const uint32_t n : neighbours[q])
                    if (chartOf[n] < 0 && length(fn[n]) > 0.5 && dot(fn[n], c.normal) >= settings.chartNormalDot) {
                        chartOf[n] = int(charts.size());
                        queue.push_back(n);
                    }
            }
            charts.push_back(std::move(c));
        }
        // A vertex used by triangles of two charts is split (each chart gets its own lightmap UV).
        std::unordered_map<uint32_t, int> vertexChart;
        std::map<std::pair<uint32_t, int>, uint32_t> copies;
        for (size_t t = 0; t < triCount; ++t) {
            if (chartOf[t] < 0) continue;
            for (int e = 0; e < 3; ++e) {
                uint32_t& vi = s.idx[t * 3 + e];
                const auto it = vertexChart.find(vi);
                if (it == vertexChart.end()) {
                    vertexChart.emplace(vi, chartOf[t]);
                } else if (it->second != chartOf[t]) {
                    const auto [ci, fresh] = copies.emplace(std::make_pair(vi, chartOf[t]), uint32_t(s.verts.size()));
                    if (fresh) {
                        s.verts.push_back(s.verts[vi]);
                        r.sourceVertex[si].push_back(r.sourceVertex[si][vi]);
                    }
                    vi = ci->second;
                }
            }
        }
    }
    r.charts = charts.size();
    for (Chart& c : charts) {
        c.u = perpendicular(c.normal);
        c.v = cross(c.normal, c.u);
    }
    // ---- sizes and packing; the texel size grows until everything fits
    double texel = std::max(1.0, settings.texelSize);
    int side = 0;
    for (int attempt = 0; attempt < 40 && side == 0; ++attempt, texel *= 1.25) {
        for (Chart& c : charts) {
            const BakeSurface& s = r.surfaces[c.surface];
            c.minU = c.minV = 1e30;
            c.maxU = c.maxV = -1e30;
            for (const uint32_t t : c.tris)
                for (int e = 0; e < 3; ++e) {
                    const Vec3& p = s.verts[s.idx[t * 3 + e]].pos;
                    const double pu = dot(p, c.u) / texel, pv = dot(p, c.v) / texel;
                    c.minU = std::min(c.minU, pu);
                    c.maxU = std::max(c.maxU, pu);
                    c.minV = std::min(c.minV, pv);
                    c.maxV = std::max(c.maxV, pv);
                }
            c.w = int(std::ceil(c.maxU - c.minU)) + 1 + 2 * settings.padding;
            c.h = int(std::ceil(c.maxV - c.minV)) + 1 + 2 * settings.padding;
        }
        for (int s = 64; s <= settings.maxSize; s *= 2)
            if (pack(charts, s)) {
                side = s;
                break;
            }
        if (side) break;
    }
    if (!side) {
        r.error = "the surfaces do not fit a " + std::to_string(settings.maxSize) + " lightmap";
        return r;
    }
    r.texelSize = texel;
    r.width = r.height = side;
    // ---- UVs
    r.uv.resize(r.surfaces.size());
    for (size_t si = 0; si < r.surfaces.size(); ++si) r.uv[si].assign(r.surfaces[si].verts.size(), {0.f, 0.f});
    for (const Chart& c : charts) {
        const BakeSurface& s = r.surfaces[c.surface];
        for (const uint32_t t : c.tris)
            for (int e = 0; e < 3; ++e) {
                const uint32_t vi = s.idx[t * 3 + e];
                const Vec3& p = s.verts[vi].pos;
                const double tu = c.x + settings.padding + (dot(p, c.u) / texel - c.minU);
                const double tv = c.y + settings.padding + (dot(p, c.v) / texel - c.minV);
                r.uv[c.surface][vi] = {float(tu / side), float(tv / side)};
            }
    }
    // ---- texels: the triangle under each texel centre
    const size_t n = size_t(side) * size_t(side);
    std::vector<Texel> texels(n);
    r.covered.assign(n, 0);
    for (size_t ci = 0; ci < charts.size(); ++ci) {
        const Chart& c = charts[ci];
        const BakeSurface& s = r.surfaces[c.surface];
        for (int ty = c.y; ty < c.y + c.h; ++ty)
            for (int tx = c.x; tx < c.x + c.w; ++tx) texels[size_t(ty) * side + tx].chart = int(ci);
        for (const uint32_t t : c.tris) {
            const BakeVertex* vtx[3] = {&s.verts[s.idx[t * 3]], &s.verts[s.idx[t * 3 + 1]], &s.verts[s.idx[t * 3 + 2]]};
            double px[3], py[3];
            for (int e = 0; e < 3; ++e) {
                px[e] = c.x + settings.padding + (dot(vtx[e]->pos, c.u) / texel - c.minU);
                py[e] = c.y + settings.padding + (dot(vtx[e]->pos, c.v) / texel - c.minV);
            }
            const double area = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);
            if (std::fabs(area) < 1e-12) continue;
            const int x0 = std::max(c.x, int(std::floor(std::min({px[0], px[1], px[2]})))), x1 = std::min(c.x + c.w - 1, int(std::ceil(std::max({px[0], px[1], px[2]}))));
            const int y0 = std::max(c.y, int(std::floor(std::min({py[0], py[1], py[2]})))), y1 = std::min(c.y + c.h - 1, int(std::ceil(std::max({py[0], py[1], py[2]}))));
            for (int ty = y0; ty <= y1; ++ty)
                for (int tx = x0; tx <= x1; ++tx) {
                    const double cx = tx + 0.5, cy = ty + 0.5;
                    const double w0 = ((px[1] - cx) * (py[2] - cy) - (px[2] - cx) * (py[1] - cy)) / area;
                    const double w1 = ((px[2] - cx) * (py[0] - cy) - (px[0] - cx) * (py[2] - cy)) / area;
                    const double w2 = 1.0 - w0 - w1;
                    const double eps = -1e-6;
                    if (w0 < eps || w1 < eps || w2 < eps) continue;
                    Texel& tx_ = texels[size_t(ty) * side + tx];
                    tx_.pos = vtx[0]->pos * w0 + vtx[1]->pos * w1 + vtx[2]->pos * w2;
                    tx_.normal = safeNormalize(vtx[0]->normal * w0 + vtx[1]->normal * w1 + vtx[2]->normal * w2, c.normal);
                    tx_.tangent = vtx[0]->tangent * w0 + vtx[1]->tangent * w1 + vtx[2]->tangent * w2;
                    r.covered[size_t(ty) * side + tx] = 1;
                }
        }
    }
    // ---- the ray scene: every surface (lit or not) and the extra occluders
    std::vector<RayBvh::Triangle> tris = scene.extraOccluders;
    for (const BakeSurface& s : r.surfaces)
        for (size_t t = 0; t + 2 < s.idx.size(); t += 3) tris.push_back({s.verts[s.idx[t]].pos, s.verts[s.idx[t + 1]].pos, s.verts[s.idx[t + 2]].pos, 0});
    RayBvh bvh;
    bvh.build(std::move(tris));
    // ---- light
    r.ambient.assign(n, {0, 0, 0});
    r.directional.assign(n, {0, 0, 0});
    r.direction.assign(n, {0, 0, 1});
    r.sunVisibility.assign(n, 1.0f);
    r.directionWorld.assign(n, {0, 0, 1});
    std::vector<size_t> work;
    for (size_t i = 0; i < n; ++i)
        if (r.covered[i]) work.push_back(i);
    r.texelsLit = work.size();
    const Vec3 sunDir = safeNormalize(scene.sunDir, Vec3{0, 0, 1});
    const bool sun = scene.sunIntensity > 0;
    const Vec3 sunE = scene.sunColor * scene.sunIntensity;
    const Vec3 skyE = scene.skyColor * scene.skyIntensity;
    // The game intensity of each light (convert.cpp): the reference is radius / 2, or for a spot the distance along its
    // axis to the first surface (at most radius / 2).
    std::vector<double> gameIntensity;
    for (const BakeLight& l : scene.lights) {
        double ref = l.reference;
        if (ref <= 0) {
            ref = l.radius * 0.5;
            RayBvh::Hit h;
            if (l.spot && bvh.intersect(l.pos, safeNormalize(l.dir, Vec3{0, 0, -1}), 1.0, ref, h)) ref = h.t;
        }
        gameIntensity.push_back(gameLightIntensity(l.radius, ref));
    }
    // A light's attenuation at distance dist toward L (unit, surface to light): the game falloff and the spot cone.
    const auto lightAtt = [&](size_t li, double dist, const Vec3& L) {
        const BakeLight& l = scene.lights[li];
        double spot = 1.0;
        if (l.spot) spot = std::clamp((dot(L * -1.0, l.dir) - l.cosOuter) / std::max(1e-4, l.cosInner - l.cosOuter), 0.0, 1.0);
        return gameLightAttenuation(gameIntensity[li], l.radius, dist, spot);
    };
    std::atomic<size_t> next{0}, done{0};
    const auto worker = [&] {
        for (;;) {
            const size_t begin = next.fetch_add(256);
            if (begin >= work.size()) return;
            const size_t end = std::min(work.size(), begin + 256);
            for (size_t w = begin; w < end; ++w) {
                const size_t i = work[w];
                const Texel& tx = texels[i];
                const Vec3 N = tx.normal;
                const Vec3 P = tx.pos + N * 0.5;
                Vec3 T = safeNormalize(tx.tangent - N * dot(tx.tangent, N), perpendicular(N));
                const Vec3 B = cross(N, T);
                // Direct light: the sun and the light entities.
                Vec3 E{0, 0, 0}, dirSum{0, 0, 0};
                if (sun) {
                    const double ndl = dot(N, sunDir);
                    const bool visible = ndl > 0 && !bvh.occluded(P, sunDir, 0.05, 1e6);
                    r.sunVisibility[i] = visible ? 1.0f : 0.0f;
                    if (visible && scene.sunDirect) {
                        const Vec3 e = sunE * ndl;
                        E = E + e;
                        dirSum = dirSum + sunDir * lumV(e);
                    }
                }
                for (size_t li = 0; li < scene.lights.size(); ++li) {
                    const BakeLight& l = scene.lights[li];
                    if (!l.direct) continue;
                    const Vec3 d = l.pos - P;
                    const double dist = length(d);
                    if (dist >= l.radius || dist < 1e-3) continue;
                    const Vec3 L = d * (1.0 / dist);
                    const double ndl = dot(N, L);
                    if (ndl <= 0) continue;
                    const double a = lightAtt(li, dist, L);
                    if (a <= 0 || bvh.occluded(P, L, 0.05, dist - 0.5)) continue;
                    const Vec3 e = l.color * (l.intensity * a * ndl);
                    E = E + e;
                    dirSum = dirSum + L * lumV(e);
                }
                // Direction in the tangent frame, clamped to the slopes the lightmap format holds; B = E / D.z.
                Vec3 Dw = safeNormalize(dirSum, N);
                double sx = dot(Dw, T), sy = dot(Dw, B), sz = dot(Dw, N);
                sz = std::max(sz, 1e-3);
                sx = std::clamp(sx / sz, -2.08, 2.0);
                sy = std::clamp(sy / sz, -2.0645, 2.0);
                const double dz = 1.0 / std::sqrt(1.0 + sx * sx + sy * sy);
                const Vec3 Bc = E * (1.0 / dz);
                r.directional[i] = {float(Bc.x), float(Bc.y), float(Bc.z)};
                r.direction[i] = {float(sx * dz), float(sy * dz), float(dz)};
                const Vec3 dw = safeNormalize(T * (sx * dz) + B * (sy * dz) + N * dz, N);
                r.directionWorld[i] = {float(dw.x), float(dw.y), float(dw.z)};
                // Ambient: given from outside for this texel, else the sky and one bounce of the sun, cosine-weighted.
                Vec3 A{0, 0, 0};
                if (scene.ambientAt && scene.ambientAt(tx.pos, N, A)) {
                    r.ambient[i] = {float(A.x), float(A.y), float(A.z)};
                    continue;
                }
                const int rays = std::max(1, settings.skyRays);
                const uint32_t seed = hash3(uint32_t(i), uint32_t(i >> 32), 7u);
                const double r1 = (seed & 0xFFFF) / 65536.0, r2 = (seed >> 16) / 65536.0;
                for (int k = 0; k < rays; ++k) {
                    double u1 = std::fmod((k + 0.5) / rays + r1, 1.0), u2 = std::fmod(radicalInverse(uint32_t(k)) + r2, 1.0);
                    const double rr = std::sqrt(u1), phi = 2 * kPi * u2;
                    const Vec3 dir = safeNormalize(T * (rr * std::cos(phi)) + B * (rr * std::sin(phi)) + N * std::sqrt(std::max(0.0, 1.0 - u1)), N);
                    RayBvh::Hit hit;
                    if (!bvh.intersect(P, dir, 0.05, 1e6, hit)) {
                        A = A + skyE;
                        continue;
                    }
                    const RayBvh::Triangle& ht = bvh.triangles()[hit.tri];
                    Vec3 Nh = safeNormalize(cross(ht.b - ht.a, ht.c - ht.a), N);
                    if (dot(Nh, dir) > 0) Nh = Nh * -1.0;
                    const Vec3 H = P + dir * hit.t + Nh * 0.5;
                    Vec3 Eh = skyE * 0.5;
                    if (sun) {
                        const double ndl = dot(Nh, sunDir);
                        if (ndl > 0 && !bvh.occluded(H, sunDir, 0.05, 1e6)) Eh = Eh + sunE * ndl;
                    }
                    // The light entities' direct light at the bounce point (real-time lights bounce too).
                    for (size_t li = 0; li < scene.lights.size(); ++li) {
                        const BakeLight& l = scene.lights[li];
                        const Vec3 d = l.pos - H;
                        const double dist = length(d);
                        if (dist >= l.radius || dist < 1e-3) continue;
                        const Vec3 L = d * (1.0 / dist);
                        const double ndl = dot(Nh, L);
                        if (ndl <= 0) continue;
                        const double a = lightAtt(li, dist, L);
                        if (a <= 0 || bvh.occluded(H, L, 0.05, dist - 0.5)) continue;
                        Eh = Eh + l.color * (l.intensity * a * ndl);
                    }
                    A = A + Eh * settings.bounceAlbedo;
                }
                A = A * (1.0 / rays);
                r.ambient[i] = {float(A.x), float(A.y), float(A.z)};
            }
            const size_t d = done.fetch_add(end - begin) + (end - begin);
            if (progress && work.size()) progress(double(d) / double(work.size()));
        }
    };
    const unsigned threads = settings.threads > 0 ? unsigned(settings.threads) : std::max(1u, std::thread::hardware_concurrency());
    if (progress) {
        // Progress callbacks from several threads: run one worker here and the others in the pool.
    }
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < threads; ++t) pool.emplace_back(worker);
    worker();
    for (auto& t : pool) t.join();
    // ---- fill the uncovered texels of each chart from covered neighbours (bilinear filtering reads them)
    std::vector<uint8_t> have = r.covered;
    for (int pass = 0; pass < settings.padding + 2; ++pass) {
        std::vector<uint8_t> next = have;
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x) {
                const size_t i = size_t(y) * side + x;
                if (have[i] || texels[i].chart < 0) continue;
                std::array<float, 3> a{0, 0, 0}, b{0, 0, 0}, d{0, 0, 0};
                float vis = 0;
                std::array<float, 3> dwSum{0, 0, 0};
                int count = 0;
                const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
                for (int k = 0; k < 4; ++k) {
                    const int nx = x + dx[k], ny = y + dy[k];
                    if (nx < 0 || ny < 0 || nx >= side || ny >= side) continue;
                    const size_t j = size_t(ny) * side + nx;
                    if (!have[j] || texels[j].chart != texels[i].chart) continue;
                    for (int c = 0; c < 3; ++c) {
                        a[c] += r.ambient[j][c];
                        b[c] += r.directional[j][c];
                        d[c] += r.direction[j][c];
                    }
                    vis += r.sunVisibility[j];
                    for (int c = 0; c < 3; ++c) dwSum[c] += r.directionWorld[j][c];
                    ++count;
                }
                if (!count) continue;
                for (int c = 0; c < 3; ++c) {
                    r.ambient[i][c] = a[c] / count;
                    r.directional[i][c] = b[c] / count;
                    d[c] /= count;
                }
                r.sunVisibility[i] = vis / count;
                const double dwl = std::sqrt(dwSum[0] * dwSum[0] + dwSum[1] * dwSum[1] + dwSum[2] * dwSum[2]);
                if (dwl > 1e-6) r.directionWorld[i] = {float(dwSum[0] / dwl), float(dwSum[1] / dwl), float(dwSum[2] / dwl)};
                const double dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                r.direction[i] = dl > 1e-6 ? std::array<float, 3>{float(d[0] / dl), float(d[1] / dl), float(d[2] / dl)} : std::array<float, 3>{0, 0, 1};
                next[i] = 1;
            }
        have.swap(next);
    }
    r.ok = true;
    return r;
}

EncodedLightmap encodeLightmapPair(const BakeResult& r, double logScale) {
    EncodedLightmap e;
    e.logScale = logScale;
    const size_t n = size_t(r.width) * size_t(r.height);
    double maximum = 1e-3;
    for (size_t i = 0; i < n && i < r.ambient.size(); ++i)
        for (int c = 0; c < 3; ++c) maximum = std::max({maximum, double(r.ambient[i][c]), double(r.directional[i][c])});
    e.maximum = maximum;
    const double denom = std::log2(1.0 + logScale * maximum);
    const auto enc = [&](double v) {
        v = std::max(0.0, v);
        return uint8_t(std::clamp(std::lround(255.0 * std::log2(1.0 + logScale * v) / denom), 0L, 255L));
    };
    e.primary.assign(n, 255);
    for (size_t i = 0; i < n && i < r.sunVisibility.size(); ++i)
        e.primary[i] = uint8_t(std::clamp(std::lround(255.0 * r.sunVisibility[i]), 0L, 255L));
    e.secondary.assign(n * 8, 0);
    for (size_t i = 0; i < n; ++i) {
        const auto& a = r.ambient[i];
        const auto& b = r.directional[i];
        const auto& d = r.direction[i];
        const double dz = std::max(1e-3, double(d[2]));
        const double sx = d[0] / dz, sy = d[1] / dz;
        const uint8_t bx = uint8_t(std::clamp(std::lround((sx + 2.07999992) / 4.07999992 * 255.0), 0L, 255L));
        const uint8_t by = uint8_t(std::clamp(std::lround((sy + 2.06451607) / 4.06451607 * 255.0), 0L, 255L));
        uint8_t* pa = e.secondary.data() + i * 4;
        uint8_t* pb = e.secondary.data() + n * 4 + i * 4;
        pa[0] = enc(a[2]);
        pa[1] = enc(a[1]);
        pa[2] = enc(a[0]);
        pa[3] = bx;
        pb[0] = enc(b[2]);
        pb[1] = enc(b[1]);
        pb[2] = enc(b[0]);
        pb[3] = by;
    }
    return e;
}

void decodeLightmapTexel(const EncodedLightmap& e, int width, int height, int x, int y, float c[3], float& f, float d[3]) {
    const size_t n = size_t(width) * size_t(height), i = size_t(y) * width + x;
    const uint8_t* a = e.secondary.data() + i * 4;
    const uint8_t* b = e.secondary.data() + n * 4 + i * 4;
    const double logMax = std::log2(1.0 + e.logScale * e.maximum);
    const auto dec = [&](uint8_t byte) { return (std::exp2(byte / 255.0 * logMax) - 1.0) / e.logScale; };
    const double sx = 4.07999992 * (a[3] / 255.0) - 2.07999992;
    const double sy = 4.06451607 * (b[3] / 255.0) - 2.06451607;
    const double dz = 1.0 / std::sqrt(1.0 + sx * sx + sy * sy);
    std::array<float, 3> amb{float(dec(a[2])), float(dec(a[1])), float(dec(a[0]))};
    std::array<float, 3> irr{};
    for (int k = 0; k < 3; ++k) irr[k] = float(amb[k] + dec(k == 0 ? b[2] : k == 1 ? b[1] : b[0]) * dz);
    for (int k = 0; k < 3; ++k) c[k] = irr[k];
    const double total = lum(irr);
    f = total > 0 ? float(std::clamp(lum(amb) / total, 0.0, 1.0)) : 1.0f;
    d[0] = float(sx * dz);
    d[1] = float(sy * dz);
    d[2] = float(dz);
}

}  // namespace iw8r
