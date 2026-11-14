#include "lightmap_bake.h"
#include "stock_lightgrid.h"
#include "test_harness.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

using namespace iw8r;

namespace {
// A probe record with the given DC (values the R11G11B10F form holds exactly) and one L1 coefficient byte.
std::vector<uint8_t> probe(uint32_t dcPacked, int coefIndex = 0, uint8_t coefByte = 128) {
    std::vector<uint8_t> r(32, 0);
    std::memcpy(r.data(), &dcPacked, 4);
    for (int c = 0; c < 3; ++c)
        for (int i = 1; i <= 8; ++i) r[size_t(4 + 8 * c + (i - 1))] = i == coefIndex ? coefByte : uint8_t(128);
    r[29] = 255;
    return r;
}
void putF(std::vector<uint8_t>& v, float f) {
    uint8_t b[4];
    std::memcpy(b, &f, 4);
    v.insert(v.end(), b, b + 4);
}
void putU(std::vector<uint8_t>& v, uint32_t u) {
    uint8_t b[4];
    std::memcpy(b, &u, 4);
    v.insert(v.end(), b, b + 4);
}
// R11G11B10F of (1, 1, 1): exponent 15, mantissa 0 in each field.
constexpr uint32_t kOne = (15u << 6) | ((15u << 6) << 11) | ((15u << 5) << 22);
constexpr uint32_t kTwo = (16u << 6) | ((16u << 6) << 11) | ((16u << 5) << 22);
}  // namespace

// Plan 1.9 (research 105 section 2.1): the stock tetrahedral light grid, sampled by barycentric weights.
TEST(stock_gpugrid_decode_build_and_sample) {
    float one[3], two[3];
    decodeR11G11B10F(kOne, one);
    decodeR11G11B10F(kTwo, two);
    CHECK(one[0] == 1.0f && one[2] == 1.0f && two[1] == 2.0f);
    // A probe: the DC, and coefficient 2 (the +z one) at byte 255 = +1.0039 x DC x sqrt3 in the LGV's slot 0.
    const std::vector<uint8_t> rec = probe(kTwo, 2, 255);
    const StockLgvSh p = decodeStockGpuProbe(rec.data());
    CHECK(p.dc[0] == 2.0f && p.dc[1] == 2.0f && p.dc[2] == 2.0f);
    CHECK(std::fabs(p.sh[0][0] - float((255 - 127.5) / 127.0 * 2.0 * std::sqrt(3.0))) < 1e-5);
    CHECK(std::fabs(p.sh[2][0] - float(0.5 / 127.0 * 2.0 * std::sqrt(3.0))) < 1e-5);  // byte 128: nearly 0
    // Lit from above, a face that looks up gets more light than one that looks down.
    double up[3], down[3];
    const double nUp[3] = {0, 0, 1}, nDown[3] = {0, 0, -1};
    evalStockLgv(p, nUp, up);
    evalStockLgv(p, nDown, down);
    CHECK(up[0] > down[0] && down[0] >= 0);

    // Two tetrahedra sharing a face: probes 0..3 at light 1, probe 4 at light 2.
    std::vector<uint8_t> probes, positions, tets;
    const float pts[5][3] = {{0, 0, 0}, {128, 0, 0}, {0, 128, 0}, {0, 0, 128}, {128, 128, 128}};
    for (int i = 0; i < 5; ++i) {
        const std::vector<uint8_t> r = probe(i == 4 ? kTwo : kOne);
        probes.insert(probes.end(), r.begin(), r.end());
        for (float f : pts[i]) putF(positions, f);
    }
    for (uint32_t i : {0u, 1u, 2u, 3u}) putU(tets, i);
    for (uint32_t i : {1u, 2u, 3u, 4u | 0x80000000u}) putU(tets, i);  // bit 31 is a flag, not part of the index
    StockGpuGrid g;
    std::string err;
    CHECK(buildStockGpuGrid(probes, positions, tets, g, &err));
    CHECK(g.pos.size() == 5 && g.tets.size() == 2 && g.tets[1][3] == 4 && g.maxs[0] == 128.0f && g.nx >= 1 && !g.cellTets.empty());
    StockLgvSh s;
    const double atVertex[3] = {0, 0, 0}, inFirst[3] = {16, 16, 16}, atFar[3] = {128, 128, 128}, outside[3] = {127, 127, 1}, away[3] = {900, 0, 0};
    CHECK(sampleStockGpuGrid(g, atVertex, s) && std::fabs(s.dc[0] - 1.0f) < 1e-5);
    CHECK(sampleStockGpuGrid(g, inFirst, s) && std::fabs(s.dc[1] - 1.0f) < 1e-5);
    CHECK(sampleStockGpuGrid(g, atFar, s) && std::fabs(s.dc[2] - 2.0f) < 1e-4);
    // Halfway between the shared face's centre and probe 4: between the two lights.
    const double mid[3] = {(128.0 / 3 + 128) / 2, (128.0 / 3 + 128) / 2, (128.0 / 3 + 128) / 2};
    CHECK(sampleStockGpuGrid(g, mid, s) && std::fabs(s.dc[0] - 1.5f) < 1e-4);
    CHECK(!sampleStockGpuGrid(g, outside, s));  // inside the box, in no tetrahedron
    CHECK(!sampleStockGpuGrid(g, away, s));

    // Refusals.
    StockGpuGrid bad;
    std::vector<uint8_t> shortPos(positions.begin(), positions.end() - 12);
    CHECK(!buildStockGpuGrid(probes, shortPos, tets, bad, &err) && err.find("positions") != std::string::npos);
    std::vector<uint8_t> wild = tets;
    const uint32_t big = 99;
    std::memcpy(wild.data(), &big, 4);
    CHECK(!buildStockGpuGrid(probes, positions, wild, bad, &err) && err.find("names probe 99") != std::string::npos);
    CHECK(!buildStockGpuGrid({}, {}, {}, bad, &err));
    CHECK(!parseStockGpuGrid("{}", bad, &err) && err.find("gpuLightGrid") != std::string::npos);

    // The export's text form.
    const auto hex = [](const std::vector<uint8_t>& v) {
        static const char* d = "0123456789abcdef";
        std::string o;
        for (uint8_t b : v) o += d[b >> 4], o += d[b & 15];
        return o;
    };
    const std::string json = "{\"asset\":{\"fields\":{\"probes\":{\"bytes\":\"00\"},\"gpuLightGrid\":{\"gpuLightGrid\":{\"values\":[{\"probeCount\":5,"
                             "\"probes\":{\"stride\":32,\"bytes\":\"" + hex(probes) + "\"},\"probePositions\":{\"bytes\":\"" + hex(positions) +
                             "\"},\"tetrahedrons\":{\"bytes\":\"" + hex(tets) + "\"}}]}}}}}";
    StockGpuGrid parsed;
    CHECK(parseStockGpuGrid(json, parsed, &err));
    CHECK(parsed.pos.size() == 5 && parsed.tets.size() == 2 && sampleStockGpuGrid(parsed, mid, s) && std::fabs(s.dc[0] - 1.5f) < 1e-4);
}

// The lightmap bake takes its ambient light from outside where a function gives it (the stock map's grid).
TEST(lightmap_bake_ambient_from_outside) {
    BakeScene scene;
    BakeSurface floor;
    floor.verts = {{Vec3{0, 0, 0}, Vec3{0, 0, 1}, Vec3{1, 0, 0}}, {Vec3{128, 0, 0}, Vec3{0, 0, 1}, Vec3{1, 0, 0}},
                   {Vec3{128, 128, 0}, Vec3{0, 0, 1}, Vec3{1, 0, 0}}, {Vec3{0, 128, 0}, Vec3{0, 0, 1}, Vec3{1, 0, 0}}};
    floor.idx = {0, 1, 2, 0, 2, 3};
    scene.surfaces.push_back(floor);
    scene.sunIntensity = 0;
    BakeSettings settings;
    settings.threads = 1;
    const BakeResult sky = bakeLightmap(scene, settings);
    // Left half: light 3 from outside; right half: the bake's own sky.
    scene.ambientAt = [](const Vec3& pos, const Vec3& normal, Vec3& ambient) {
        if (pos.x >= 64 || normal.z < 0.5) return false;
        ambient = Vec3{3, 2, 1};
        return true;
    };
    const BakeResult mixed = bakeLightmap(scene, settings);
    CHECK(sky.ok && mixed.ok && sky.width == mixed.width);
    size_t given = 0, own = 0;
    bool same = true;
    for (size_t i = 0; i < mixed.ambient.size() && i < sky.ambient.size(); ++i) {
        if (!mixed.covered[i]) continue;
        if (mixed.ambient[i][0] == 3.0f && mixed.ambient[i][1] == 2.0f && mixed.ambient[i][2] == 1.0f) ++given;
        else if (sky.covered[i]) ++own, same = same && std::fabs(mixed.ambient[i][0] - sky.ambient[i][0]) < 0.5f;
    }
    CHECK(given > 4 && own > 4 && same);
}

// On a real export (set IW8R_TEST_TRZONE to a gfx_map_trzone asset JSON; skipped without it): the grid parses, a
// probe's own position gives that probe's light back, and the light is in the range research 105 measured.
TEST(stock_gpugrid_real_export) {
    const char* path = std::getenv("IW8R_TEST_TRZONE");
    if (!path || !*path) return;
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    StockGpuGrid g;
    std::string err;
    CHECK(parseStockGpuGrid(ss.str(), g, &err));
    if (g.empty()) return;
    size_t found = 0, close = 0, bright = 0;
    double sum = 0;
    const size_t step = std::max<size_t>(1, g.pos.size() / 2000);
    for (size_t i = 0; i < g.pos.size(); i += step) {
        const double p[3] = {g.pos[i][0], g.pos[i][1], g.pos[i][2]};
        StockLgvSh s;
        if (!sampleStockGpuGrid(g, p, s)) continue;
        ++found;
        if (std::fabs(s.dc[1] - g.sh[i].dc[1]) <= 0.02f * std::max(1.0f, g.sh[i].dc[1])) ++close;
        double e[3];
        const double up[3] = {0, 0, 1};
        evalStockLgv(s, up, e);
        sum += e[1];
        if (e[1] > 0.05) ++bright;
    }
    std::printf("  gpu grid: %zu probes, %zu tetrahedra, %d x %d x %d cells of %.0f, %zu cell entries; %zu of %zu sampled probes found, "
                "%zu equal their own light, mean E(up) green %.3f\n",
                g.pos.size(), g.tets.size(), g.nx, g.ny, g.nz, g.cell, g.cellTets.size(), found, (g.pos.size() + step - 1) / step, close,
                found ? sum / double(found) : 0.0);
    CHECK(found > 100 && close * 10 >= found * 9 && bright * 2 > found);
}
