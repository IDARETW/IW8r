// Stock world displacement: the per-vertex records and the CPU subdivision / displacement (src/core/stock_displacement.h,
// docs/research/101_stock_displacement_data.md).
//
// stock_displacement_real_data is an opt-in diagnostic on a real world cache (it does nothing unless
// IW8R_DSP_ZONE = <world_cache>/<map>, IW8R_DSP_MATS = an ACTS material export folder of the map's displaced materials and
// IW8R_DSP_IMAGES = a folder of <image name without ~hash>.pgm height images are set); it prints the statistics of
// research 101 section 5.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "json_lite.h"
#include "stock_decal.h"
#include "stock_displacement.h"
#include "stock_world.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

bool nearD(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

// Packs a tangent frame word: components a, b, c of the quaternion (without the dropped one) in [-1, 1] / sqrt(1/2).
uint32_t packFrame(double a, double b, double c, uint32_t dropped) {
    const auto q = [](double v, int bits) {
        const double m = double((1 << bits) - 1);
        return uint32_t(std::clamp(std::lround((v / 0.707107 + 1.0) * 0.5 * m), 0l, long(m)));
    };
    return q(a, 10) | q(b, 10) << 10 | q(c, 9) << 20 | dropped << 30;
}
constexpr uint32_t kUp = 512u | 512u << 10 | 256u << 20 | 3u << 30;  // identity quaternion: normal (0, 0, 1)

uint16_t floatToHalf(float f) {
    // Exact for the values used here (normal numbers in half range).
    uint32_t u;
    std::memcpy(&u, &f, 4);
    const uint32_t sign = (u >> 16) & 0x8000;
    const int exp = int((u >> 23) & 0xFF) - 127 + 15;
    const uint32_t man = (u >> 13) & 0x3FF;
    if (f == 0.0f) return uint16_t(sign);
    return uint16_t(sign | uint32_t(exp) << 10 | man);
}

// A flat square of `size` inches at z = 0: 4 vertices, 2 triangles (CCW seen from +z), 1 layer, layer UV = pos / size,
// lightmap UV = pos / size, colour byte 0 = 255 * x / size.
StockWorld flatQuad(double size, int layers = 1) {
    StockWorld w;
    ImportedMesh m;
    m.material = "tw/disp_test";
    m.pos = {{0, 0, 0}, {size, 0, 0}, {size, size, 0}, {0, size, 0}};
    m.normal.assign(4, Vec3{0, 0, 1});
    for (const Vec3& p : m.pos) m.uv.push_back({p.x / size, 1.0 - p.y / size});
    m.idx = {0, 1, 2, 0, 2, 3};
    w.meshes.push_back(m);
    w.lightmapUv.push_back({});
    w.lightmapIndex.push_back({});
    w.layerUv.push_back({});
    w.vertexColor.push_back({});
    w.vertexColorHigh.push_back({});
    w.layerNormalTransform.push_back({});
    w.layerCount.push_back(layers);
    for (const Vec3& p : m.pos) {
        w.lightmapUv[0].push_back({float(p.x / size), float(p.y / size)});
        w.lightmapIndex[0].push_back(0);
        for (int l = 0; l < layers; ++l) w.layerUv[0].push_back({float(p.x / size) * float(l + 1), 1.0f - float(p.y / size) * float(l + 1)});
        w.vertexColor[0].push_back(uint32_t(std::lround(255.0 * p.x / size)) | 0xFFFFFF00u);
        w.vertexColorHigh[0].push_back(0xFFFFFFFFu);
        for (int l = 1; l < layers; ++l) w.layerNormalTransform[0].push_back(0xFF8080FFu);
    }
    return w;
}

StockMeshDisplacement dispFor(const StockWorld& w, std::vector<float> lo, float hi, int layer = 0) {
    StockMeshDisplacement d;
    d.mesh = 0;
    d.material = w.meshes[0].material;
    d.layerCount = w.layerCount[0];
    d.displacedLayers = 1u << layer;
    for (size_t k = 0; k < w.meshes[0].pos.size(); ++k)
        for (int l = 0; l < d.layerCount; ++l) {
            StockDisplacementVertex v;
            if (l == layer) v.lo = lo[k], v.hi = hi;
            d.vertices.push_back(v);
        }
    d.normal = w.meshes[0].normal;
    return d;
}

StockHeightMap constantMap(uint8_t v, int size = 4) {
    std::vector<uint8_t> px(size_t(size) * size_t(size), v);
    return stockHeightMapFromR8(size, size, px.data());
}

// Each undirected edge of the mesh: how many triangles use it.
std::map<std::pair<uint32_t, uint32_t>, int> edgeUse(const ImportedMesh& m) {
    std::map<std::pair<uint32_t, uint32_t>, int> e;
    for (size_t i = 0; i + 2 < m.idx.size(); i += 3)
        for (int c = 0; c < 3; ++c) {
            uint32_t a = m.idx[i + size_t(c)], b = m.idx[i + size_t((c + 1) % 3)];
            if (a > b) std::swap(a, b);
            ++e[{a, b}];
        }
    return e;
}

double meshArea(const ImportedMesh& m) {
    double a = 0;
    for (size_t i = 0; i + 2 < m.idx.size(); i += 3)
        a += 0.5 * length(cross(m.pos[m.idx[i + 1]] - m.pos[m.idx[i]], m.pos[m.idx[i + 2]] - m.pos[m.idx[i]]));
    return a;
}

std::string hexOf(const std::vector<uint8_t>& b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (uint8_t x : b) s += d[x >> 4], s += d[x & 15];
    return s;
}
void putU32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    if (b.size() < at + 4) b.resize(at + 4);
    for (int i = 0; i < 4; ++i) b[at + size_t(i)] = uint8_t(v >> (8 * i));
}
void putF32(std::vector<uint8_t>& b, size_t at, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    putU32(b, at, u);
}

}  // namespace

TEST(stock_displacement_half_and_frame) {
    CHECK(stockHalfToFloat(0x3C00) == 1.0f);
    CHECK(stockHalfToFloat(0x2000) == 0.0078125f);  // 1/128, the most common hi on mp_village2
    CHECK(stockHalfToFloat(0x0001) == std::ldexp(1.0f, -24));
    CHECK(stockHalfToFloat(0xC000) == -2.0f);
    CHECK(std::isinf(stockHalfToFloat(0x7C00)));
    // Identity quaternion (dropped w): normal +z.
    const Vec3 up = stockTangentFrameNormal(kUp);
    CHECK(nearD(up.z, 1.0, 1e-5) && std::fabs(up.x) < 3e-3 && std::fabs(up.y) < 3e-3);
    // 90 degrees about X: q = (s, 0, 0, s), s = sqrt(1/2): normal (0, -1, 0). Dropped component w (index 3) or x (0).
    const double s = std::sqrt(0.5);
    const Vec3 n3 = stockTangentFrameNormal(packFrame(s, 0, 0, 3));
    CHECK(std::fabs(n3.x) < 3e-3 && nearD(n3.y, -1.0, 3e-3) && std::fabs(n3.z) < 3e-3);
    const Vec3 n0 = stockTangentFrameNormal(packFrame(0, 0, s, 0));
    CHECK(std::fabs(n0.x) < 3e-3 && nearD(n0.y, -1.0, 3e-3) && std::fabs(n0.z) < 3e-3);
    // 90 degrees about Y: q = (0, s, 0, s): normal (1, 0, 0) (dropped y, index 1: stored a = qx, b = qz, c = qw).
    const Vec3 n1 = stockTangentFrameNormal(packFrame(0, 0, s, 1));
    CHECK(nearD(n1.x, 1.0, 3e-3) && std::fabs(n1.y) < 3e-3 && std::fabs(n1.z) < 3e-3);
    // Unit length always.
    CHECK(nearD(length(stockTangentFrameNormal(0x12345678u)), 1.0, 1e-9));
}

TEST(stock_displacement_height_map_sampling) {
    // 2x2: 0, 255 / 255, 0 (row 0 = top).
    const uint8_t px[4] = {0, 255, 255, 0};
    const StockHeightMap m = stockHeightMapFromR8(2, 2, px);
    CHECK(m.levels.size() == 2);
    CHECK(m.levels[1].width == 1 && nearD(m.levels[1].texels[0], 0.5));
    // Texel centres return the texel; lod 1 the mean; wrap repeats.
    CHECK(nearD(sampleStockHeightMap(m, 0.25, 0.25, 0), 0.0));
    CHECK(nearD(sampleStockHeightMap(m, 0.75, 0.25, 0), 1.0));
    CHECK(nearD(sampleStockHeightMap(m, 0.25, 0.75, 0), 1.0));
    CHECK(nearD(sampleStockHeightMap(m, 1.75, -0.75, 0), 1.0, 1e-6));
    CHECK(nearD(sampleStockHeightMap(m, 0.5, 0.25, 0), 0.5));  // bilinear between two texels
    CHECK(nearD(sampleStockHeightMap(m, 0.0, 0.25, 0), 0.5));  // the wrap neighbour of texel 0 is texel 1
    CHECK(nearD(sampleStockHeightMap(m, 0.25, 0.25, 1), 0.5));
    CHECK(nearD(sampleStockHeightMap(m, 0.25, 0.25, 0.5), 0.25));  // trilinear
    CHECK(nearD(sampleStockHeightMap(m, 0.25, 0.25, 9), 0.5));     // clamped to the last mip
    // RGBA8 red, 16-bit.
    const uint8_t rgba[16] = {255, 1, 2, 3, 0, 9, 9, 9, 0, 9, 9, 9, 255, 9, 9, 9};
    const StockHeightMap r = stockHeightMapFromR8(2, 2, rgba, 4);
    CHECK(nearD(sampleStockHeightMap(r, 0.25, 0.25, 0), 1.0) && nearD(sampleStockHeightMap(r, 0.75, 0.25, 0), 0.0));
    const uint16_t w16[4] = {65535, 0, 0, 32768};
    const StockHeightMap h16 = stockHeightMapFromR16(2, 2, w16);
    CHECK(nearD(sampleStockHeightMap(h16, 0.75, 0.75, 0), 32768.0 / 65535.0, 1e-6));
    // Non-square, odd sizes reach 1x1.
    std::vector<uint8_t> big(size_t(6) * 3, 100);
    const StockHeightMap odd = stockHeightMapFromR8(6, 3, big.data());
    CHECK(odd.levels.back().width == 1 && odd.levels.back().height == 1);
    CHECK(nearD(sampleStockHeightMap(odd, 0.3, 0.7, 1.5), 100.0 / 255.0, 1e-6));
    CHECK(stockHeightMapFromR8(0, 0, nullptr).empty());
    CHECK(nearD(sampleStockHeightMap(StockHeightMap(), 0.1, 0.2, 0), 0.5));
}

TEST(stock_displacement_flat_quad_offsets) {
    // lo 1, hi 1/64, Replay param -1/256: amp = 1 / (256 / 64) = 0.25. h = 1: d = 0.25 * (40 - 20) = 5 inches.
    StockWorld w = flatQuad(64);
    const StockMeshDisplacement d = dispFor(w, {1, 1, 1, 1}, 1.0f / 64.0f);
    const StockHeightMap one = constantMap(255), mid = constantMap(128), zero = constantMap(0);
    StockDisplacementOptions o;
    o.targetEdge = 0;  // existing vertices only (plain VS)
    StockDisplacedMesh out;
    std::string err;
    CHECK(displaceStockMesh(w, d, {{0, 40.0f, -20.0f, 0.0f, &one}}, o, out, &err));
    CHECK(out.mesh.pos.size() == 4 && out.mesh.idx.size() == 6);
    for (const Vec3& p : out.mesh.pos) CHECK(nearD(p.z, 5.0, 1e-5));
    for (float f : out.offset) CHECK(nearD(f, 5.0, 1e-5));
    CHECK(nearD(out.mesh.normal[0].z, 1.0));
    // h = 0: -5; h = 128/255: 0.25 * (40 * 128/255 - 20) = +0.0196.
    CHECK(displaceStockMesh(w, d, {{0, 40.0f, -20.0f, 0.0f, &zero}}, o, out, &err));
    CHECK(nearD(out.mesh.pos[2].z, -5.0, 1e-5));
    CHECK(displaceStockMesh(w, d, {{0, 40.0f, -20.0f, 0.0f, &mid}}, o, out, &err));
    CHECK(nearD(out.mesh.pos[2].z, 0.25 * (40.0 * 128.0 / 255.0 - 20.0), 1e-5));
    // Without r_tessellationHeightAuto (param = +1): amp = lo = 1, d = 20.
    o.sceneParam = stockDisplacementSceneParam(1.0f, false);
    CHECK(displaceStockMesh(w, d, {{0, 40.0f, -20.0f, 0.0f, &one}}, o, out, &err));
    CHECK(nearD(out.mesh.pos[1].z, 20.0, 1e-5));
    // Per-vertex lo: 0, 0.5, 1, 1 -> 0, 2.5, 5, 5 (default param).
    o.sceneParam = -1.0f / 256.0f;
    const StockMeshDisplacement d2 = dispFor(w, {0, 0.5f, 1, 1}, 1.0f / 64.0f);
    CHECK(displaceStockMesh(w, d2, {{0, 40.0f, -20.0f, 0.0f, &one}}, o, out, &err));
    CHECK(nearD(out.mesh.pos[0].z, 0.0) && nearD(out.mesh.pos[1].z, 2.5, 1e-5) && nearD(out.mesh.pos[3].z, 5.0, 1e-5));
    // Along the vertex normal (here tilted to +x): the move follows it.
    StockMeshDisplacement d3 = d;
    d3.normal.assign(4, Vec3{1, 0, 0});
    CHECK(displaceStockMesh(w, d3, {{0, 40.0f, -20.0f, 0.0f, &one}}, o, out, &err));
    CHECK(nearD(out.mesh.pos[0].x, 5.0, 1e-5) && nearD(out.mesh.pos[0].z, 0.0));
    // Mismatched data fails.
    StockMeshDisplacement bad = d;
    bad.vertices.pop_back();
    CHECK(!displaceStockMesh(w, bad, {{0, 40.0f, -20.0f, 0.0f, &one}}, o, out, &err));
    // A layer without a map: h = 0.5, the mean surface (no move).
    CHECK(displaceStockMesh(w, d, {{0, 40.0f, -20.0f, 0.0f, nullptr}}, o, out, &err));
    CHECK(nearD(out.mesh.pos[2].z, 0.0, 1e-6));
}

TEST(stock_displacement_subdivision_counts) {
    StockWorld w = flatQuad(64);
    const StockMeshDisplacement d = dispFor(w, {1, 1, 1, 1}, 1.0f / 64.0f);
    const StockHeightMap mid = constantMap(128);
    StockDisplacementOptions o;
    StockDisplacedMesh out;
    // One pass: sides 64 and diagonal 90.5 > 50: each triangle splits in 4; 4 + 5 midpoints.
    o.targetEdge = 50;
    CHECK(displaceStockMesh(w, d, {{0, 40.0f, -20.0f, 0.0f, &mid}}, o, out));
    CHECK(out.mesh.idx.size() / 3 == 8 && out.mesh.pos.size() == 9 && out.passes == 1);
    // Target 40: the second pass splits only the 45-inch hypotenuse of each of the 8 (red-green): 16 triangles, 4 more
    // vertices (each split edge is shared by two triangles), then every edge is <= 32 * sqrt(2) / ... <= 40.
    o.targetEdge = 40;
    CHECK(displaceStockMesh(w, d, {{0, 40.0f, -20.0f, 0.0f, &mid}}, o, out));
    CHECK(out.mesh.idx.size() / 3 == 16 && out.mesh.pos.size() == 13 && out.passes == 2);
    // Fine target: every edge <= target, watertight (every interior edge used twice, the 4 * n boundary edges once),
    // area kept (h = 0.5 + 1/510: offsets ~0.02 do not change a flat area), winding kept (+z normals).
    o.targetEdge = 5;
    CHECK(displaceStockMesh(w, d, {{0, 40.0f, -20.0f, 0.0f, &mid}}, o, out));
    const auto use = edgeUse(out.mesh);
    size_t boundary = 0, bad = 0;
    double longest = 0;
    for (const auto& [e, n] : use) {
        longest = std::max(longest, length(out.mesh.pos[e.first] - out.mesh.pos[e.second]));
        if (n == 1) ++boundary;
        else if (n != 2) ++bad;
    }
    CHECK(bad == 0);
    CHECK(longest <= 5.0 + 1e-6);
    CHECK(boundary == 4 * 16);  // each 64-inch side ends in 16 segments of 4
    CHECK(nearD(meshArea(out.mesh), 64.0 * 64.0, 1e-6));
    for (const Vec3& n : out.mesh.normal) CHECK(n.z > 0.999);
    CHECK(out.passes >= 5 && out.passes <= 8 && !out.truncated);
    // maxPasses and maxTriangles stop it.
    o.maxPasses = 1;
    CHECK(displaceStockMesh(w, d, {{0, 40.0f, -20.0f, 0.0f, &mid}}, o, out));
    CHECK(out.mesh.idx.size() / 3 == 8);
    o.maxPasses = 8;
    o.maxTriangles = 20;
    CHECK(displaceStockMesh(w, d, {{0, 40.0f, -20.0f, 0.0f, &mid}}, o, out));
    CHECK(out.truncated && out.mesh.idx.size() / 3 == 8);  // pass 2 would make 32
    // A texture stretched over a large area (hi 1/2048: 1/32 of a repeat is 64 inches): only the 90-inch diagonal
    // is longer than max(5, 64), so 4 triangles. And a relief below minRelief (lo tiny) is not subdivided at all.
    {
        const StockMeshDisplacement wide = dispFor(w, {1, 1, 1, 1}, 1.0f / 2048.0f);
        CHECK(displaceStockMesh(w, wide, {{0, 40.0f, -20.0f, 0.0f, &mid}}, o, out));
        CHECK(out.mesh.idx.size() / 3 == 4 && out.mesh.pos.size() == 5);
        const StockMeshDisplacement faint = dispFor(w, {0.001f, 0.001f, 0.001f, 0.001f}, 1.0f / 64.0f);  // relief 0.005 in
        CHECK(displaceStockMesh(w, faint, {{0, 40.0f, -20.0f, 0.0f, &mid}}, o, out));
        CHECK(out.mesh.idx.size() == 6);
        StockDisplacementOptions o2 = o;
        o2.minRelief = 0.001;
        CHECK(displaceStockMesh(w, faint, {{0, 40.0f, -20.0f, 0.0f, &mid}}, o2, out));
        CHECK(out.mesh.idx.size() > 6);
    }
    // Budget helper: 2 triangles of 2048 sq in count 9 A / E^2 = 36864 / E^2.
    {
        StockDisplacementData dd;
        dd.meshes.push_back(d);
        CHECK(nearD(stockDisplacementEdgeForBudget(w, dd, 2304), 4.0));
        CHECK(nearD(stockDisplacementEdgeForBudget(w, dd, 576), 8.0, 1e-3));
        CHECK(nearD(stockDisplacementEdgeForBudget(w, dd, 576, 10.0), 10.0));
        StockDisplacementData none;
        CHECK(nearD(stockDisplacementEdgeForBudget(w, none, 1), 4.0));
    }
    // lo = 0 everywhere: nothing is subdivided or moved.
    o.maxTriangles = 2'000'000;
    const StockMeshDisplacement none = dispFor(w, {0, 0, 0, 0}, 1.0f / 64.0f);
    CHECK(displaceStockMesh(w, none, {{0, 40.0f, -20.0f, 0.0f, &mid}}, o, out));
    CHECK(out.mesh.idx.size() == 6 && out.mesh.pos.size() == 4 && out.subdividedTriangles == 0);
}

TEST(stock_displacement_partial_and_crack_free) {
    // lo only at vertex 1: triangle 0-1-2 touches it, 0-2-3 does not. The diagonal 0-2 has no relief at either end, so
    // neither triangle splits it: 0-2-3 stays whole and the mesh stays conforming (no T-junction).
    StockWorld w = flatQuad(64);
    const StockMeshDisplacement d = dispFor(w, {0, 1, 0, 0}, 1.0f / 64.0f);
    const StockHeightMap one = constantMap(255);
    StockDisplacementOptions o;
    o.targetEdge = 20;
    StockDisplacedMesh out;
    CHECK(displaceStockMesh(w, d, {{0, 40.0f, -20.0f, 0.0f, &one}}, o, out));
    CHECK(out.subdividedTriangles == 1);
    // Every vertex on the diagonal x == y stays at z = 0 (lo interpolates 0..0 there).
    size_t onDiag = 0;
    for (size_t k = 0; k < out.mesh.pos.size(); ++k) {
        const double x = out.mesh.uv[k][0] * 64.0, y = (1.0 - out.mesh.uv[k][1]) * 64.0;  // flat position from uv
        if (std::fabs(x - y) < 1e-9) {
            ++onDiag;
            CHECK(std::fabs(out.mesh.pos[k].z) < 1e-9);
        }
    }
    CHECK(onDiag == 2);
    for (const auto& [e, n] : edgeUse(out.mesh)) CHECK(n <= 2);
    size_t whole = 0;
    for (size_t i = 0; i + 2 < out.mesh.idx.size(); i += 3)
        if (std::set<uint32_t>{out.mesh.idx[i], out.mesh.idx[i + 1], out.mesh.idx[i + 2]} == std::set<uint32_t>{0, 2, 3}) ++whole;
    CHECK(whole == 1);
    // Vertex 1 (corner, lo 1) moves the full 5 inches; with Phong on, a flat patch (all normals +z) stays flat in x / y.
    CHECK(nearD(out.mesh.pos[1].z, 5.0, 1e-5) && nearD(out.mesh.pos[1].x, 64.0) && nearD(out.mesh.pos[1].y, 0.0));
    // z grows linearly with lo (h = 1): a vertex at the midpoint of edge 0-1 has lo 0.5 -> 2.5.
    bool found = false;
    for (size_t k = 0; k < out.mesh.pos.size(); ++k)
        if (nearD(out.mesh.pos[k].x, 32.0, 1e-9) && nearD(out.mesh.pos[k].y, 0.0, 1e-9)) {
            found = true;
            CHECK(nearD(out.mesh.pos[k].z, 2.5, 1e-5));
        }
    CHECK(found);
}

TEST(stock_displacement_uv_interpolation) {
    StockWorld w = flatQuad(64, 2);
    const StockMeshDisplacement d = dispFor(w, {1, 1, 1, 1}, 1.0f / 64.0f, 1);
    const StockHeightMap mid = constantMap(128);
    StockDisplacementOptions o;
    o.targetEdge = 50;
    StockDisplacedMesh out;
    CHECK(displaceStockMesh(w, d, {{1, 40.0f, -20.0f, 0.0f, &mid}}, o, out));
    CHECK(out.layerCount == 2);
    CHECK(out.layerUv.size() == out.mesh.pos.size() * 2 && out.lightmapUv.size() == out.mesh.pos.size());
    CHECK(out.vertexColor.size() == out.mesh.pos.size() && out.layerNormalTransform.size() == out.mesh.pos.size());
    // The centre (midpoint of the diagonal 0-2): every attribute is the average of corners 0 and 2.
    bool centre = false;
    for (size_t k = 0; k < out.mesh.pos.size(); ++k) {
        if (!nearD(out.mesh.pos[k].x, 32.0, 1e-9) || !nearD(out.mesh.pos[k].y, 32.0, 1e-9)) continue;
        centre = true;
        CHECK(nearD(out.mesh.uv[k][0], 0.5) && nearD(out.mesh.uv[k][1], 0.5));
        CHECK(nearD(out.layerUv[k * 2][0], 0.5) && nearD(out.layerUv[k * 2][1], 0.5));
        CHECK(nearD(out.layerUv[k * 2 + 1][0], 1.0) && nearD(out.layerUv[k * 2 + 1][1], 0.0));  // layer 1 = 2x
        CHECK(nearD(out.lightmapUv[k][0], 0.5) && nearD(out.lightmapUv[k][1], 0.5) && out.lightmapIndex[k] == 0);
        CHECK((out.vertexColor[k] & 0xFF) == 128 && (out.vertexColor[k] >> 8) == 0xFFFFFFu);  // (0 + 255) / 2 rounded
        CHECK(out.layerNormalTransform[k] == 0xFF8080FFu);
    }
    CHECK(centre);
    // The midpoint of side 0-1 (y = 0): layer 0 uv (0.5, 1), lightmap (0.5, 0).
    for (size_t k = 0; k < out.mesh.pos.size(); ++k)
        if (nearD(out.mesh.pos[k].x, 32.0, 1e-9) && nearD(out.mesh.pos[k].y, 0.0, 1e-9))
            CHECK(nearD(out.layerUv[k * 2][0], 0.5) && nearD(out.layerUv[k * 2][1], 1.0) && nearD(out.lightmapUv[k][1], 0.0));
    // The height is read at the displaced layer's own UV (game v = 1 - stored): a map that is 1 in its left half and
    // 0 in the right. Layer 1 uv at vertex 1 is (2, 1 - 0) -> game (2, 1)... use layer 0 instead with a 2x1 map.
    const uint8_t lr[2] = {255, 0};
    const StockHeightMap half = stockHeightMapFromR8(2, 1, lr);
    StockWorld w1 = flatQuad(64);
    const StockMeshDisplacement d1 = dispFor(w1, {1, 1, 1, 1}, 1.0f / 64.0f);
    StockDisplacementOptions o1;
    o1.targetEdge = 0;
    o1.autoLod = false;
    CHECK(displaceStockMesh(w1, d1, {{0, 40.0f, -20.0f, 0.0f, &half}}, o1, out));
    // u = 0 is the edge between the two texels (wrap): h = 0.5 -> 0.
    CHECK(nearD(out.mesh.pos[0].z, 0.0, 1e-5));
    // A vertex at u = 0.25 (texel 0 centre) reads h = 1.
    StockWorld w2 = flatQuad(64);
    for (auto& uv : w2.layerUv[0]) uv[0] = uv[0] * 0.0f + 0.25f;
    CHECK(displaceStockMesh(w2, d1, {{0, 40.0f, -20.0f, 0.0f, &half}}, o1, out));
    CHECK(nearD(out.mesh.pos[2].z, 5.0, 1e-5));
}

TEST(stock_displacement_phong) {
    // A triangle whose corner normals lean outward along x: Phong lifts the midpoint of edge 0-1 to
    // (1 - a) p + a avg(pi_0, pi_1) with a = 0.75 lo; for P0 (0,0,0) N0 (-1,0,1)/sqrt2, P1 (10,0,0) N1 (1,0,1)/sqrt2 the
    // projections are (2.5, 0, 2.5) and (7.5, 0, 2.5): z = 0.75 * 2.5. h = 0.5 and bias -20, scale 40: d = 0.
    StockWorld w;
    ImportedMesh m;
    m.material = "tw/disp_test";
    m.pos = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
    m.normal.assign(3, Vec3{0, 0, 1});
    m.uv.assign(3, {0, 0});
    m.idx = {0, 1, 2};
    w.meshes.push_back(m);
    w.layerCount.push_back(1);
    w.layerUv.push_back({{0, 1}, {0, 1}, {0, 1}});
    StockMeshDisplacement d;
    d.mesh = 0;
    d.layerCount = 1;
    d.displacedLayers = 1;
    d.vertices.assign(3, StockDisplacementVertex{1.0f, 1.0f / 64.0f});
    const double s = std::sqrt(0.5);
    d.normal = {{-s, 0, s}, {s, 0, s}, {0, 0, 1}};
    const StockHeightMap half = [] {
        const uint16_t v[1] = {32768};
        return stockHeightMapFromR16(1, 1, v);
    }();
    StockDisplacementOptions o;
    o.targetEdge = 6;
    o.maxPasses = 1;
    // Exactly h = 0.5: scale 40 bias -20 with h = 32768 / 65535 is not 0.5; use bias -scale * h instead.
    const float hv = 32768.0f / 65535.0f;
    StockDisplacedMesh out;
    CHECK(displaceStockMesh(w, d, {{0, 40.0f, -40.0f * hv, 0.0f, &half}}, o, out));
    bool found = false;
    for (size_t k = 3; k < out.mesh.pos.size(); ++k) {
        // The midpoint of edge 0-1 is the one with flat y = 0 (uv carries no position here: find by x).
        if (nearD(out.mesh.pos[k].x, 5.0, 1e-6) && nearD(out.mesh.pos[k].y, 0.0, 1e-6)) {
            found = true;
            CHECK(nearD(out.mesh.pos[k].z, 0.75 * 2.5, 1e-6));
        }
    }
    CHECK(found);
    // Corners do not move (unit barycentric), Phong off leaves the midpoint flat.
    CHECK(nearD(out.mesh.pos[0].z, 0.0, 1e-6) && nearD(out.mesh.pos[1].z, 0.0, 1e-6));
    o.phong = false;
    CHECK(displaceStockMesh(w, d, {{0, 40.0f, -40.0f * hv, 0.0f, &half}}, o, out));
    for (const Vec3& p : out.mesh.pos) CHECK(std::fabs(p.z) < 1e-6);
}

TEST(stock_displacement_reader_matches_world) {
    // Two surfaces of one displaced material sharing nothing, one plain surface of another material.
    // Aux layout: [0..4) pad, surface A: tangent frames 4 x 4 at 4, texcoords 4 x 8 at 20, displacement 4 x 4 at 52;
    // surface B (same material, no displacement record): tangent 3 x 4 at 68, texcoords 3 x 8 at 80;
    // surface C (other material): texcoords 3 x 8 at 104.
    std::vector<uint8_t> pos, aux(128, 0), idx;
    const auto vtx = [&](float x, float y, float z) {
        const size_t at = pos.size();
        putF32(pos, at, x), putF32(pos, at + 4, y), putF32(pos, at + 8, z);
    };
    vtx(0, 0, 0), vtx(64, 0, 0), vtx(64, 64, 0), vtx(0, 64, 0);  // A: 0..47
    vtx(100, 0, 0), vtx(164, 0, 0), vtx(100, 64, 0);             // B: 48..83
    vtx(200, 0, 0), vtx(264, 0, 0), vtx(200, 64, 0);             // C: 84..119
    const auto tri = [&](uint16_t a, uint16_t b, uint16_t c) {
        for (uint16_t i : {a, b, c}) idx.push_back(uint8_t(i)), idx.push_back(uint8_t(i >> 8));
    };
    tri(0, 1, 2), tri(0, 2, 3);  // A: baseIndex 0
    tri(0, 1, 2);                // B: baseIndex 6
    tri(0, 1, 2);                // C: baseIndex 9
    const float lo[4] = {0.0f, 0.25f, 0.5f, 1.0f};
    for (uint32_t j = 0; j < 4; ++j) {
        putU32(aux, 4 + 4 * j, j == 3 ? packFrame(std::sqrt(0.5), 0, 0, 3) : kUp);
        putF32(aux, 20 + 8 * j, float(j)), putF32(aux, 24 + 8 * j, 0.5f);
        putU32(aux, 52 + 4 * j, uint32_t(floatToHalf(lo[j])) | uint32_t(floatToHalf(1.0f / 128.0f)) << 16);
    }
    for (uint32_t j = 0; j < 3; ++j) {
        putU32(aux, 68 + 4 * j, kUp);
        putF32(aux, 80 + 8 * j, 0.1f * float(j)), putF32(aux, 84 + 8 * j, 0.0f);
        putF32(aux, 104 + 8 * j, 0.0f), putF32(aux, 108 + 8 * j, 0.0f);
    }
    const std::string gfx =
        R"({"asset":{"fields":{"surfaces":{"surfaces":{"values":[)"
        R"({"tris":{"posOffset":0,"vertexCount":4,"triCount":2,"baseIndex":0},"material":{"name":",tw/disp_test"},"surfDataIndex":0,"transientZone":0,"lightmapIndex":510},)"
        R"({"tris":{"posOffset":84,"vertexCount":3,"triCount":1,"baseIndex":9},"material":{"name":",tw/plain"},"surfDataIndex":2,"transientZone":0,"lightmapIndex":510},)"
        R"({"tris":{"posOffset":48,"vertexCount":3,"triCount":1,"baseIndex":6},"material":{"name":",tw/disp_test"},"surfDataIndex":1,"transientZone":0,"lightmapIndex":510})"
        R"(]},"surfData":{"values":[)"
        R"({"transientZone":0,"layerCount":1,"xyzOffset":0,"tangentFrameOffset":4,"lmapCoordOffset":0,"colorOffset":0,"texCoordOffset":20,"normalTransformOffset":[0,0,0,0,0,0,0],"displacementOffset":[52,0,0,0,0,0,0,0]},)"
        R"({"transientZone":0,"layerCount":1,"xyzOffset":48,"tangentFrameOffset":68,"lmapCoordOffset":0,"colorOffset":0,"texCoordOffset":80,"normalTransformOffset":[0,0,0,0,0,0,0],"displacementOffset":[0,0,0,0,0,0,0,0]},)"
        R"({"transientZone":0,"layerCount":1,"xyzOffset":84,"tangentFrameOffset":0,"lmapCoordOffset":0,"colorOffset":0,"texCoordOffset":104,"normalTransformOffset":[0,0,0,0,0,0,0],"displacementOffset":[0,0,0,0,0,0,0,0]})"
        R"(]}}}}})";
    const std::string trz = R"({"asset":{"fields":{"transientZoneIndex":0,"drawVerts":{"posData":{"bytes":")" + hexOf(pos) +
                            R"("},"indices":{"bytes":")" + hexOf(idx) + R"("},"auxData":{"bytes":")" + hexOf(aux) + R"("}}}}})";
    StockWorld world;
    std::string err;
    CHECK(parseStockWorld(gfx, {trz}, world, &err));
    CHECK(world.meshes.size() == 2);
    StockDisplacementData data;
    CHECK(parseStockDisplacement(gfx, {trz}, world, data, &err));
    CHECK(data.meshes.size() == 1 && data.warnings.empty());
    if (data.meshes.size() != 1) return;
    const StockMeshDisplacement& md = data.meshes[0];
    CHECK(md.material == "tw/disp_test" && md.mesh < world.meshes.size() && world.meshes[md.mesh].material == "tw/disp_test");
    CHECK(md.surfaces == 2 && md.displacedSurfaces == 1 && md.triangles == 3 && md.displacedTriangles == 2);
    CHECK(md.displacedLayers == 1u && md.layerCount == 1);
    const ImportedMesh& wm = world.meshes[md.mesh];
    CHECK(md.vertices.size() == wm.pos.size() && md.normal.size() == wm.pos.size());
    CHECK(data.vertices == wm.pos.size() && data.displacedSurfaces == 1 && data.triangles == 3);
    // Vertex for vertex: find the StockWorld vertex at each source position.
    for (uint32_t j = 0; j < 4; ++j) {
        const Vec3 p{j == 1 || j == 2 ? 64.0 : 0.0, j >= 2 ? 64.0 : 0.0, 0.0};
        for (size_t k = 0; k < wm.pos.size(); ++k) {
            if (wm.pos[k] != p) continue;
            CHECK(md.vertices[k].lo == lo[j] && md.vertices[k].hi == 1.0f / 128.0f);
            if (j == 3) CHECK(nearD(md.normal[k].y, -1.0, 3e-3));
            else CHECK(nearD(md.normal[k].z, 1.0, 1e-5));
        }
    }
    // Surface B vertices: the shader default (lo 0, hi 1).
    for (size_t k = 0; k < wm.pos.size(); ++k)
        if (wm.pos[k].x >= 100) CHECK(md.vertices[k].lo == 0.0f && md.vertices[k].hi == 1.0f);
    // A world from another export (positions changed) is refused with a warning.
    StockWorld other = world;
    other.meshes[md.mesh].pos[0].z += 1;
    CHECK(parseStockDisplacement(gfx, {trz}, other, data, &err));
    CHECK(data.meshes.empty() && data.warnings.size() == 1);
    // Displace it and put it back into the world: the parallel arrays follow.
    CHECK(parseStockDisplacement(gfx, {trz}, world, data, &err));
    const StockHeightMap one = constantMap(255);
    StockDisplacedMesh out;
    StockDisplacementOptions o;
    o.targetEdge = 16;
    CHECK(displaceStockMesh(world, data.meshes[0], {{0, 40.0f, -20.0f, 0.0f, &one}}, o, out, &err));
    const size_t nv = out.mesh.pos.size();
    CHECK(nv > wm.pos.size());
    replaceStockWorldMesh(world, data.meshes[0].mesh, std::move(out));
    const size_t mi = data.meshes[0].mesh;
    CHECK(world.meshes[mi].pos.size() == nv && world.lightmapUv[mi].size() == nv && world.layerUv[mi].size() == nv &&
          world.vertexColor[mi].size() == nv && world.lightmapIndex[mi].size() == nv && world.meshes[mi].uv.size() == nv);
    // lo 1 at (0, 64) with hi 1/128: amp 0.5, h = 1: +10 inches along that vertex's normal (0, -1, 0).
    bool moved = false;
    for (const Vec3& p : world.meshes[mi].pos)
        if (nearD(p.x, 0.0, 0.05) && nearD(p.y, 54.0, 0.05) && std::fabs(p.z) < 0.05) moved = true;
    CHECK(moved);
    // No displaced surface: an empty result, no error.
    std::string plainGfx = gfx;
    const std::string from = R"("displacementOffset":[52,)";
    plainGfx.replace(plainGfx.find(from), from.size(), R"("displacementOffset":[0,)");
    CHECK(parseStockDisplacement(plainGfx, {trz}, world, data, &err) && data.meshes.empty());
}

// ---------------------------------------------------------------------------------------------------------------------
// Opt-in diagnostic on a real world cache (see the file comment).

namespace {

bool readPgm(const std::string& path, int& w, int& h, std::vector<uint8_t>& px) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string magic;
    int maxv = 0;
    f >> magic >> w >> h >> maxv;
    f.get();
    if (magic != "P5" || w <= 0 || h <= 0 || maxv != 255) return false;
    px.resize(size_t(w) * size_t(h));
    f.read(reinterpret_cast<char*>(px.data()), std::streamsize(px.size()));
    return bool(f);
}

struct MatDisp {
    std::map<int, std::string> image;                  // layer -> slot 95 + L image (without ~hash)
    std::map<int, std::array<float, 4>> constant;      // layer -> constant 45 + L
};

std::map<std::string, MatDisp> readMaterials(const std::string& dir) {
    namespace fs = std::filesystem;
    std::map<std::string, MatDisp> out;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != ".json") continue;
        const std::string fn = e.path().filename().string();
        if (fn == "manifest.json" || fn == "script_strings.json") continue;
        std::ifstream f(e.path(), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const auto root = json::parse(text);
        const json::Value* a = root ? root->get("asset") : nullptr;
        const json::Value* fl = a ? a->get("fields") : nullptr;
        if (!fl || !fl->get("name") || !fl->get("name")->get("string")) continue;
        MatDisp md;
        if (const json::Value* tt = fl->get("textureTable") ? fl->get("textureTable")->get("values") : nullptr)
            for (const json::Value& t : tt->arr) {
                const int slot = int(t.get("index") ? t.get("index")->num : -1);
                const json::Value* nm = t.get("image") ? t.get("image")->get("name") : nullptr;
                if (slot >= 95 && slot < 95 + kStockMaxLayers && nm && nm->string()) {
                    std::string s = *nm->string();
                    if (!s.empty() && s[0] == ',') s.erase(0, 1);
                    md.image[slot - 95] = s.substr(0, s.find('~'));
                }
            }
        if (const json::Value* ct = fl->get("constantTable") ? fl->get("constantTable")->get("values") : nullptr)
            for (const json::Value& c : ct->arr) {
                const int idx = int(c.get("index") ? c.get("index")->num : -1);
                const json::Value* v = c.get("literal") ? c.get("literal")->get("v") : nullptr;
                if (idx >= 45 && idx < 45 + kStockMaxLayers && v && v->arr.size() == 4) {
                    std::array<float, 4> k{};
                    for (int i = 0; i < 4; ++i) k[size_t(i)] = v->arr[size_t(i)].type == json::Value::Type::Number ? float(v->arr[size_t(i)].num) : 0.0f;
                    md.constant[idx - 45] = k;
                }
            }
        out[*fl->get("name")->get("string")->string()] = md;
    }
    return out;
}

}  // namespace

TEST(stock_displacement_real_data) {
    const char* zone = std::getenv("IW8R_DSP_ZONE");
    const char* mats = std::getenv("IW8R_DSP_MATS");
    const char* imgs = std::getenv("IW8R_DSP_IMAGES");
    if (!zone || !mats || !imgs) return;  // opt-in diagnostic, not a fixture test
    using clk = std::chrono::steady_clock;
    const auto t0 = clk::now();
    StockWorld world;
    std::string err;
    CHECK(readStockWorld(zone, world, &err));
    const auto t1 = clk::now();
    StockDisplacementData data;
    CHECK(readStockDisplacement(zone, world, data, &err));
    const auto t2 = clk::now();
    const auto ms = [](auto a, auto b) { return long(std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count()); };
    size_t worldTris = 0;
    for (const auto& m : world.meshes) worldTris += m.idx.size() / 3;
    std::printf("  [dsp] world %zu meshes %zu tris (read %ld ms); displacement read %ld ms\n", world.meshes.size(), worldTris, ms(t0, t1), ms(t1, t2));
    std::printf("  [dsp] displaced meshes %zu, surfaces %zu, vertices %zu, triangles %zu (%.2f%% of world), with lo > 0: %zu\n",
                data.meshes.size(), data.displacedSurfaces, data.vertices, data.triangles, 100.0 * double(data.triangles) / double(worldTris),
                data.displacedTriangles);
    for (const auto& w : data.warnings) std::printf("  [dsp] warning: %s\n", w.c_str());
    const auto matInfo = readMaterials(mats);
    std::map<std::string, StockHeightMap> maps;
    std::set<std::string> missing;
    const float param = stockDisplacementSceneParam(1.0f, true);
    // Statistics.
    double normAngleSum = 0, normAngleMax = 0;
    size_t normN = 0, normOver10 = 0, normFlipped = 0;
    double ampMin = 1e30, ampMax = 0, dMaxPossible = 0, ampMaxNoAuto = 0;
    std::vector<double> ratio;  // hi / measured layer uv per inch
    struct Run {
        const char* name;
        StockDisplacementOptions o;
        size_t tris = 0, verts = 0, moved = 0;
        double sumAbs = 0, sumSq = 0, maxUp = 0, maxDown = 0;
        long msec = 0;
        size_t truncated = 0;
    };
    std::vector<Run> runs;
    struct Big {
        size_t outTris = 0, srcTris = 0;
        double area = 0, hiMin = 1e9, hiMax = 0, relief = 0;
        std::string material;
    };
    std::vector<Big> big;  // per mesh, the 8-inch run
    {
        Run r{"VS only (no subdivision, mip at 8-inch edges)"};
        r.o.targetEdge = 0;
        r.o.lodEdge = 8;
        runs.push_back(r);
    }
    for (double e : {8.0, 4.0, 2.0}) {
        Run r{e == 8.0 ? "subdivided, 8-inch edges" : e == 4.0 ? "subdivided, 4-inch edges" : "subdivided, 2-inch edges"};
        r.o.targetEdge = e;
        r.o.maxTriangles = 4'000'000;
        r.o.maxPasses = 10;
        runs.push_back(r);
    }
    {
        Run r{"subdivided, edge for a 1.5M-triangle budget"};
        r.o.targetEdge = stockDisplacementEdgeForBudget(world, data, 1'500'000);
        r.o.maxTriangles = 4'000'000;
        r.o.maxPasses = 10;
        std::printf("  [dsp] edge for a 1.5M-triangle budget: %.2f in\n", r.o.targetEdge);
        runs.push_back(r);
    }
    for (const StockMeshDisplacement& md : data.meshes) {
        const ImportedMesh& m = world.meshes[md.mesh];
        const auto it = matInfo.find(md.material);
        std::vector<StockDisplacementLayer> layers;
        for (int L = 0; L < md.layerCount; ++L) {
            if (!(md.displacedLayers & (1u << L))) continue;
            StockDisplacementLayer ly;
            ly.layer = L;
            if (it != matInfo.end() && it->second.constant.count(L)) {
                const auto& k = it->second.constant.at(L);
                ly.scale = k[0], ly.bias = k[1], ly.w = k[3];
            }
            if (it != matInfo.end() && it->second.image.count(L)) {
                const std::string img = it->second.image.at(L);
                if (!maps.count(img)) {
                    int w = 0, h = 0;
                    std::vector<uint8_t> px;
                    if (readPgm(std::string(imgs) + "/" + img + ".pgm", w, h, px)) maps[img] = stockHeightMapFromR8(w, h, px.data());
                    else missing.insert(img);
                }
                if (maps.count(img)) ly.height = &maps[img];
            }
            layers.push_back(ly);
            for (size_t k = 0; k < m.pos.size(); ++k) {
                const StockDisplacementVertex& v = md.vertices[k * size_t(md.layerCount) + size_t(L)];
                if (v.lo <= 0) continue;
                const double a = stockDisplacementAmplitude(v.lo, v.hi, param);
                ampMin = std::min(ampMin, a), ampMax = std::max(ampMax, a);
                ampMaxNoAuto = std::max(ampMaxNoAuto, double(stockDisplacementAmplitude(v.lo, v.hi, 1.0f)) * std::fabs(ly.bias));
                dMaxPossible = std::max(dMaxPossible, a * std::fabs(double(ly.bias)));
            }
            // hi vs the measured layer UV density (per triangle, sqrt(uv area / world area)).
            for (size_t i = 0; i + 2 < m.idx.size(); i += 3) {
                const uint32_t a = m.idx[i], b = m.idx[i + 1], c = m.idx[i + 2];
                const double wa = 0.5 * length(cross(m.pos[b] - m.pos[a], m.pos[c] - m.pos[a]));
                const auto& ua = world.layerUv[md.mesh][size_t(a) * size_t(md.layerCount) + size_t(L)];
                const auto& ub = world.layerUv[md.mesh][size_t(b) * size_t(md.layerCount) + size_t(L)];
                const auto& uc = world.layerUv[md.mesh][size_t(c) * size_t(md.layerCount) + size_t(L)];
                const double uvA = 0.5 * std::fabs(double(ub[0] - ua[0]) * double(uc[1] - ua[1]) - double(uc[0] - ua[0]) * double(ub[1] - ua[1]));
                const float hiAvg = (md.vertices[size_t(a) * size_t(md.layerCount) + size_t(L)].hi + md.vertices[size_t(b) * size_t(md.layerCount) + size_t(L)].hi +
                                     md.vertices[size_t(c) * size_t(md.layerCount) + size_t(L)].hi) / 3.0f;
                if (wa > 1.0 && uvA > 0) ratio.push_back(double(hiAvg) / std::sqrt(uvA / wa));
            }
        }
        // Tangent-frame normal vs StockWorld's area-weighted normal.
        if (!md.normal.empty())
            for (size_t k = 0; k < m.pos.size(); ++k) {
                const double c = std::clamp(dot(md.normal[k], m.normal[k]), -1.0, 1.0);
                const double ang = std::acos(c) * 180.0 / 3.14159265358979;
                normAngleSum += ang, normAngleMax = std::max(normAngleMax, ang), ++normN;
                if (ang > 10) ++normOver10;
                if (c < 0) ++normFlipped;
            }
        for (Run& r : runs) {
            StockDisplacedMesh out;
            const auto a = clk::now();
            CHECK(displaceStockMesh(world, md, layers, r.o, out, &err));
            r.msec += ms(a, clk::now());
            r.tris += out.mesh.idx.size() / 3, r.verts += out.mesh.pos.size();
            if (&r == &runs[1]) {
                Big b;
                b.outTris = out.mesh.idx.size() / 3, b.srcTris = m.idx.size() / 3, b.area = 0, b.material = md.material;
                for (size_t i = 0; i + 2 < m.idx.size(); i += 3)
                    b.area += 0.5 * length(cross(m.pos[m.idx[i + 1]] - m.pos[m.idx[i]], m.pos[m.idx[i + 2]] - m.pos[m.idx[i]]));
                for (const auto& ly : layers)
                    for (size_t k = 0; k < m.pos.size(); ++k) {
                        const auto& v = md.vertices[k * size_t(md.layerCount) + size_t(ly.layer)];
                        if (v.lo <= 0) continue;
                        b.hiMin = std::min(b.hiMin, double(v.hi)), b.hiMax = std::max(b.hiMax, double(v.hi));
                        b.relief = std::max(b.relief, double(stockDisplacementAmplitude(v.lo, v.hi, param)) *
                                                          std::max(std::fabs(ly.scale + ly.bias), std::fabs(ly.bias)));
                    }
                big.push_back(b);
            }
            if (out.truncated) ++r.truncated;
            for (float f : out.offset) {
                if (f == 0.0f) continue;
                ++r.moved;
                r.sumAbs += std::fabs(f), r.sumSq += double(f) * f;
                r.maxUp = std::max(r.maxUp, double(f)), r.maxDown = std::min(r.maxDown, double(f));
            }
        }
    }
    for (const auto& s : missing) std::printf("  [dsp] height image not found: %s.pgm\n", s.c_str());
    std::printf("  [dsp] height images used: %zu\n", maps.size());
    for (const auto& [n, mp] : maps) std::printf("  [dsp]   %s %dx%d, %zu mips\n", n.c_str(), mp.width(), mp.levels[0].height, mp.levels.size());
    std::printf("  [dsp] amp (lo > 0, heightAuto): %.4f .. %.4f; |d| bound amp*|bias| %.3f in (without heightAuto %.3f in)\n", ampMin, ampMax,
                dMaxPossible, ampMaxNoAuto);
    std::sort(ratio.begin(), ratio.end());
    if (!ratio.empty())
        std::printf("  [dsp] hi / measured layer uv per inch over %zu triangles: p10 %.3f median %.3f p90 %.3f\n", ratio.size(),
                    ratio[ratio.size() / 10], ratio[ratio.size() / 2], ratio[ratio.size() * 9 / 10]);
    if (normN)
        std::printf("  [dsp] tangent-frame normal vs area normal: mean %.2f deg, max %.1f deg, > 10 deg %zu of %zu, opposite %zu\n",
                    normAngleSum / double(normN), normAngleMax, normOver10, normN, normFlipped);
    std::sort(big.begin(), big.end(), [](const Big& a, const Big& b) { return a.outTris > b.outTris; });
    for (size_t i = 0; i < big.size() && i < 6; ++i)
        std::printf("  [dsp] 8-inch run, mesh %-34s %8zu tris from %6zu, area %.0f sq ft, hi %.5f..%.5f, relief bound %.2f in\n",
                    big[i].material.c_str(), big[i].outTris, big[i].srcTris, big[i].area / 144.0, big[i].hiMin, big[i].hiMax, big[i].relief);
    for (const Run& r : runs)
        std::printf("  [dsp] %-46s tris %8zu verts %8zu moved %8zu  mean|d| %.3f rms %.3f  max +%.3f / %.3f in  %ld ms%s\n", r.name, r.tris,
                    r.verts, r.moved, r.moved ? r.sumAbs / double(r.moved) : 0.0, r.moved ? std::sqrt(r.sumSq / double(r.moved)) : 0.0,
                    r.maxUp, r.maxDown, r.msec, r.truncated ? " (some truncated)" : "");
}
