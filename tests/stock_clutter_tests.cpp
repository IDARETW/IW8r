// Stock GPU ground clutter tests (src/core/stock_clutter.cpp, docs/research/100_stock_ground_clutter.md).
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "geometry.h"
#include "stock_clutter.h"
#include "stock_world.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }
bool nearV(const Vec3& a, const Vec3& b, double eps = 1e-6) { return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps); }

std::string hexOf(const std::vector<uint8_t>& b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (uint8_t c : b) {
        s += d[c >> 4];
        s += d[c & 15];
    }
    return s;
}

// A 2 x 2 tile layer over 1000 x 1000 inches with four set bits.
StockClutterLayer testLayer() {
    StockClutterLayer L;
    L.sampleOffset = 0;
    L.bitmaskOffset = 3;  // words before the layer's block are not read
    L.tileCount[0] = 2;
    L.tileCount[1] = 2;
    L.tileSizeUV[0] = 0.5f;
    L.tileSizeUV[1] = 0.5f;
    L.layerSize[0] = 1000;
    L.layerSize[1] = 1000;
    L.angleMin[2] = 0;
    L.angleMax[2] = 0;
    L.zOffsetMinMax[0] = -1;
    L.zOffsetMinMax[1] = 1;
    L.scaleMinMax[0] = 0.8f;
    L.scaleMinMax[1] = 1.2f;
    return L;
}

std::vector<std::array<float, 2>> testPoints() {
    std::vector<std::array<float, 2>> p;
    for (uint32_t s = 0; s < kClutterSamplesPerTile; ++s) p.push_back({float(s) / 2048.0f, float((s * 7) % 2048) / 2048.0f});
    return p;
}

std::vector<uint32_t> testBitmask(const StockClutterLayer& L) {
    std::vector<uint32_t> bm(L.bitmaskOffset + 4 * kClutterWordsPerTile, 0);
    auto set = [&](uint32_t tx, uint32_t ty, uint32_t s) {
        const uint32_t tile = ty * L.tileCount[0] + tx;
        bm[L.bitmaskOffset + tile * kClutterWordsPerTile + s / 32] |= 1u << (s % 32);
    };
    set(1, 0, 0);
    set(1, 0, 5);
    set(1, 0, 2047);
    set(0, 1, 40);
    return bm;
}

ClutterGroundFn flatGround(double h) {
    return [h](double, double, double, double, ClutterGroundSample& s) {
        s.height = h;
        s.normal = Vec3{0, 0, 1};
        return true;
    };
}

}  // namespace

TEST(stock_clutter_rng_matches_shader_reference) {
    // Values from an independent Python model of clutter_gen_pass.219 lines 103-149 (out/agent/clt/py/ref.py).
    CHECK(clutterSeed(0, 0, 0, 0.25f) == 0x7430f7b7u);
    CHECK(clutterSeed(3, 7, 100, 0.0078125f) == 0xab9e7cd1u);
    uint32_t h = clutterSeed(0, 0, 0, 0.25f);
    const uint32_t want[5] = {0x32027854u, 0xdc101cd0u, 0x2ec411b5u, 0x32ae1e4cu, 0xdd9b391fu};
    for (uint32_t w : want) {
        h = clutterXorshift(h);
        CHECK(h == w);
    }
    CHECK(near(clutterUnit(0x32027854u), 0.19535018503665924, 1e-7));
    CHECK(near(clutterUnit(0xdd9b391fu), 0.8656497597694397, 1e-7));
    h = clutterSeed(65535, 1, 2047, 1.0f / 64);
    CHECK(h == 0x5f933706u);
    CHECK(clutterUnit(0) == 0.0f);
    CHECK(clutterUnit(0xffffffffu) <= 1.0f);
}

TEST(stock_clutter_euler_quat_axes) {
    const double h = 1.5707963267948966;
    // Angle x alone turns about +Y: X goes to -Z.
    CHECK(nearV(quatRotate(clutterEulerQuat(h, 0, 0), Vec3{1, 0, 0}), Vec3{0, 0, -1}, 1e-9));
    // Angle y alone turns about -Z: X goes to -Y.
    CHECK(nearV(quatRotate(clutterEulerQuat(0, h, 0), Vec3{1, 0, 0}), Vec3{0, -1, 0}, 1e-9));
    // Angle z alone turns about -X: Y goes to -Z.
    CHECK(nearV(quatRotate(clutterEulerQuat(0, 0, h), Vec3{0, 1, 0}), Vec3{0, 0, -1}, 1e-9));
    // Unit length and identity at zero.
    const auto q = clutterEulerQuat(0.3, -1.1, 2.0);
    CHECK(near(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3], 1.0, 1e-12));
    const auto id = clutterEulerQuat(0, 0, 0);
    CHECK(near(id[3], 1.0) && near(id[0], 0) && near(id[1], 0) && near(id[2], 0));
    // Axes are the rotated unit vectors, orthonormal.
    const auto ax = quatAxes(q);
    CHECK(near(dot(ax[0], ax[1]), 0, 1e-12) && near(dot(ax[1], ax[2]), 0, 1e-12) && near(length(ax[2]), 1, 1e-12));
    CHECK(nearV(cross(ax[0], ax[1]), ax[2], 1e-12));
}

TEST(stock_clutter_align_quat) {
    const Vec3 up{0, 0, 1};
    const Vec3 targets[] = {normalize(Vec3{0.3, -0.2, 1}), normalize(Vec3{1, 0, 0.2}), normalize(Vec3{-0.5, 0.5, -0.1})};
    for (const Vec3& t : targets) CHECK(nearV(quatRotate(clutterAlignQuat(up, t), up), t, 1e-9));
    // Parallel: identity.
    const auto id = clutterAlignQuat(up, up);
    CHECK(id[3] == 1 && id[0] == 0 && id[1] == 0 && id[2] == 0);
    // Opposite: a half turn that maps up to -up.
    CHECK(nearV(quatRotate(clutterAlignQuat(up, Vec3{0, 0, -1}), up), Vec3{0, 0, -1}, 1e-9));
    const Vec3 x{1, 0, 0};
    CHECK(nearV(quatRotate(clutterAlignQuat(x, Vec3{-1, 0, 0}), x), Vec3{-1, 0, 0}, 1e-9));
    // Hamilton product: b first, then a.
    const auto a = clutterEulerQuat(0, 0.7, 0), b = clutterEulerQuat(0.4, 0, 0);
    const Vec3 v{0.2, 0.5, -0.3};
    CHECK(nearV(quatRotate(quatMultiply(a, b), v), quatRotate(a, quatRotate(b, v)), 1e-12));
}

TEST(stock_clutter_layer_record_round_trip) {
    StockClutterLayer L = testLayer();
    L.angleMin[0] = -0.1f;
    L.angleMax[1] = 6.2831853f;
    L.maxInstanceCount = 9295;
    L.tileInstanceCountOffset = 17;
    L.useSurfaceNormal = 1;
    uint8_t rec[kClutterLayerBytes] = {};
    encodeStockClutterLayer(L, rec);
    StockClutterLayer back;
    CHECK(decodeStockClutterLayer(rec, back));
    CHECK(back.bitmaskOffset == 3 && back.tileCount[0] == 2 && back.tileCount[1] == 2);
    CHECK(back.tileSizeUV[0] == 0.5f && back.layerSize[1] == 1000.0f);
    CHECK(back.angleMin[0] == -0.1f && back.angleMax[1] == 6.2831853f);
    CHECK(back.zOffsetMinMax[0] == -1.0f && back.scaleMinMax[1] == 1.2f);
    CHECK(back.maxInstanceCount == 9295 && back.tileInstanceCountOffset == 17 && back.useSurfaceNormal == 1);
    // Field offsets the shader reads (clutter_gen_pass.219): +8 tileCount.x, +16 tileSizeUV, +44 angleMax.x, +80 flag.
    CHECK(rec[8] == 2 && rec[80] == 1 && rec[72] == (9295 & 0xff));
    CHECK(!decodeStockClutterLayer(nullptr, back));
}

TEST(stock_clutter_heightmap_sample) {
    ClutterHeightmap m;
    m.width = 4;
    m.height = 4;
    m.texels.assign(16, 0.5f);
    m.heightScale = 100;
    m.heightBias = -10;
    m.texelWorldSize = 25;
    ClutterGroundSample s;
    CHECK(m.sample(0.3, 0.7, s));
    CHECK(near(s.height, 40.0, 1e-9));
    CHECK(nearV(s.normal, Vec3{0, 0, 1}, 1e-12));
    // Height rising with texel x: the normal leans to -X (h(-1,0) - h(+1,0) < 0).
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) m.texels[size_t(y) * 4 + size_t(x)] = 0.1f * float(x);
    CHECK(m.sample(0.5, 0.5, s));
    CHECK(s.normal.x < -0.1 && near(s.normal.y, 0, 1e-12) && s.normal.z > 0);
    // v is flipped: texture row 0 is layer v 1. Rising with texel y = falling with layer v = rising with local y.
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) m.texels[size_t(y) * 4 + size_t(x)] = 0.1f * float(y);
    ClutterGroundSample lo, hi;
    CHECK(m.sample(0.5, 0.9, lo) && m.sample(0.5, 0.1, hi));
    CHECK(lo.height < hi.height);
    CHECK(lo.normal.y < 0);  // h(0,-1) - h(0,+1) < 0: the ground rises towards local +y, so the normal leans to -y
    ClutterHeightmap empty;
    CHECK(!empty.sample(0.5, 0.5, s));
}

TEST(stock_clutter_scatter_layer_exact_placement) {
    const StockClutterLayer L = testLayer();
    const auto pts = testPoints();
    const auto bm = testBitmask(L);
    ClutterFrame f;
    f.origin = Vec3{100, 200, 50};
    StockClutterScatter out;
    scatterStockClutterLayer(L, "tm/grass", 7, pts, bm, f, flatGround(10), ClutterScatterOptions{}, out);
    CHECK(out.instances.size() == 4);
    CHECK(out.models.size() == 1 && out.models[0] == "tm/grass");
    CHECK(out.tilesVisited == 4 && !out.capped && out.samplesDropped == 0);
    bool sawTile10 = false;
    for (const auto& i : out.instances) {
        CHECK(i.collection == 7 && i.model == 0);
        const float pu = (pts[i.sample][0] + float(i.tileX)) * 0.5f, pv = (pts[i.sample][1] + float(i.tileY)) * 0.5f;
        const double lx = (double(pu) - 0.5) * 1000, ly = (0.5 - double(pv)) * 1000;
        uint32_t h = clutterSeed(i.tileX, i.tileY, i.sample, 0.5f);
        float r[5];
        for (float& x : r) x = clutterUnit(h = clutterXorshift(h));
        CHECK(near(i.origin.x, 100 + lx, 1e-6));
        CHECK(near(i.origin.y, 200 + ly, 1e-6));
        CHECK(near(i.origin.z, 50 + 10 + (r[4] * 2.0 - 1.0), 1e-5));
        CHECK(near(i.scale, 0.8 + r[0] * (1.2f - 0.8f), 1e-6));
        CHECK(i.scale >= 0.8 && i.scale <= 1.2);
        if (i.tileX == 1 && i.tileY == 0 && i.sample == 2047) sawTile10 = true;
    }
    CHECK(sawTile10);
    // Deterministic.
    StockClutterScatter again;
    scatterStockClutterLayer(L, "tm/grass", 7, pts, bm, f, flatGround(10), ClutterScatterOptions{}, again);
    CHECK(again.instances.size() == out.instances.size());
    bool same = again.instances.size() == out.instances.size();
    for (size_t k = 0; same && k < out.instances.size(); ++k)
        same = out.instances[k].origin == again.instances[k].origin && out.instances[k].scale == again.instances[k].scale;
    CHECK(same);
}

TEST(stock_clutter_scatter_density_cap_and_radius) {
    const StockClutterLayer L = testLayer();
    const auto pts = testPoints();
    const auto bm = testBitmask(L);
    ClutterFrame f;
    // Cap: stops at 2, nearest tiles first. Centre on tile (0, 1) (local -250, -250).
    ClutterScatterOptions o;
    o.centreX = -250;
    o.centreY = -250;
    o.maxInstances = 1;
    StockClutterScatter out;
    scatterStockClutterLayer(L, "m", 0, pts, bm, f, flatGround(0), o, out);
    CHECK(out.instances.size() == 1 && out.capped);
    CHECK(out.instances.size() == 1 && out.instances[0].tileX == 0 && out.instances[0].tileY == 1);
    // Radius: only tile (1, 0) (local +250, +250) is near; its three instances lie inside the tile.
    ClutterScatterOptions r;
    r.centreX = 250;
    r.centreY = 250;
    r.radius = 400;
    StockClutterScatter near1;
    scatterStockClutterLayer(L, "m", 0, pts, bm, f, flatGround(0), r, near1);
    CHECK(near1.instances.size() == 3);
    for (const auto& i : near1.instances) CHECK(i.tileX == 1 && i.tileY == 0);
    // keepEvery 5 keeps sample indices divisible by 5: 0, 5 and 40 (2047 is dropped).
    ClutterScatterOptions k;
    k.keepEvery = 5;
    StockClutterScatter thin;
    scatterStockClutterLayer(L, "m", 0, pts, bm, f, flatGround(0), k, thin);
    CHECK(thin.instances.size() == 3);
    // A ground that refuses samples drops them.
    StockClutterScatter none;
    scatterStockClutterLayer(L, "m", 0, pts, bm, f, [](double, double, double, double, ClutterGroundSample&) { return false; },
                             ClutterScatterOptions{}, none);
    CHECK(none.instances.empty() && none.samplesDropped == 4);
    // A bitmask too short for the layer visits no tile.
    StockClutterScatter shortMask;
    scatterStockClutterLayer(L, "m", 0, pts, std::vector<uint32_t>(10, ~0u), f, flatGround(0), ClutterScatterOptions{}, shortMask);
    CHECK(shortMask.instances.empty() && shortMask.tilesVisited == 0);
}

TEST(stock_clutter_scatter_surface_normal) {
    StockClutterLayer L = testLayer();
    L.useSurfaceNormal = 1;  // all angles 0: the instance's up axis is the ground normal
    const Vec3 n = normalize(Vec3{0.4, -0.3, 1});
    StockClutterScatter out;
    scatterStockClutterLayer(L, "m", 0, testPoints(), testBitmask(L), ClutterFrame{},
                             [n](double, double, double, double, ClutterGroundSample& s) {
                                 s.height = 0;
                                 s.normal = n;
                                 return true;
                             },
                             ClutterScatterOptions{}, out);
    CHECK(out.instances.size() == 4);
    for (const auto& i : out.instances) CHECK(nearV(i.axis[2], n, 1e-9));
    // Without the flag the up axis stays +Z.
    L.useSurfaceNormal = 0;
    StockClutterScatter flat;
    scatterStockClutterLayer(L, "m", 0, testPoints(), testBitmask(L), ClutterFrame{}, flatGround(0), ClutterScatterOptions{}, flat);
    for (const auto& i : flat.instances) CHECK(nearV(i.axis[2], Vec3{0, 0, 1}, 1e-12));
}

TEST(stock_clutter_reads_gfx_map_collections) {
    // values form (as the Verdansk export), with smodels members in the export's order.
    const std::string values = R"({"asset":{"fields":{"name":"x","smodels":{"surfaceCount":0,"modelCount":2,
        "surfaces":{"bytes":"00"},
        "models":{"values":[{"model":{"name":"tm/rock_a"},"flags":{"value":1}},{"model":{"name":"#tm/ee_foliage_grass_lm_short_01"},"flags":{"value":5}}]},
        "clutterCollections":{"stride":12,"count":2,"values":[
            {"maxInstanceCount":9295,"smodelCollectionIndex":1,"terrainSurfaceIndex":0,"clutterLayerIndex":0,"pad":0},
            {"maxInstanceCount":282,"smodelCollectionIndex":0,"terrainSurfaceIndex":3,"clutterLayerIndex":5,"pad":0}]},
        "collections":{"values":[{"firstInstance":0,"instanceCount":4,"smodelIndex":0,"flags":{"value":3}},
                                 {"firstInstance":0,"instanceCount":0,"smodelIndex":1,"flags":{"value":32}}]},
        "smodelInstanceData":{"bytes":""}}}}})";
    std::vector<StockClutterCollection> c;
    std::string err;
    CHECK(parseStockClutterCollections(values, c, &err));
    CHECK(c.size() == 2);
    if (c.size() == 2) {
        CHECK(c[0].model == "tm/ee_foliage_grass_lm_short_01" && c[0].maxInstanceCount == 9295 && c[0].smodelIndex == 1);
        CHECK(c[1].model == "tm/rock_a" && c[1].terrainSurfaceIndex == 3 && c[1].clutterLayerIndex == 5 && c[1].index == 1);
    }
    // bytes form: the same records packed (12 and 16 bytes).
    std::vector<uint8_t> cc = {0x4f, 0x24, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
    std::vector<uint8_t> co(32, 0);
    co[8] = 0;
    co[24] = 1;
    const std::string bytes = std::string(R"({"asset":{"fields":{"smodels":{"models":{"values":[{"model":{"name":",tm/a"}},{"model":{"name":"tm/b"}}]},"clutterCollections":{"bytes":")") +
                              hexOf(cc) + R"("},"collections":{"bytes":")" + hexOf(co) + R"("}}}}})";
    CHECK(parseStockClutterCollections(bytes, c, &err));
    CHECK(c.size() == 1 && c[0].model == "tm/b" && c[0].maxInstanceCount == 0x244f);
    // A map without clutter (every 6v6 map): null, no records, success.
    CHECK(parseStockClutterCollections(R"({"asset":{"fields":{"smodels":{"clutterCollectionCount":0,"clutterCollections":null,"collections":{"values":[]}}}}})", c, &err));
    CHECK(c.empty());
    // A record naming a missing collection fails.
    const std::string bad = R"({"smodels":{"models":{"values":[]},"clutterCollections":{"values":[{"maxInstanceCount":1,"smodelCollectionIndex":9,"terrainSurfaceIndex":0,"clutterLayerIndex":0}]},"collections":{"values":[]}}})";
    CHECK(!parseStockClutterCollections(bad, c, &err) && c.empty() && !err.empty());
    CHECK(!parseStockClutterCollections("{}", c, &err));
}

TEST(stock_clutter_reads_stterrain) {
    // Empty terrain, as every 6v6 map exports it.
    StockClutterTerrain t;
    std::string err;
    CHECK(parseStockClutterTerrain(R"({"asset":{"type":"StTerrain","fields":{"name":{"string":"mp_m_speedball"},"surfaceCount":0,"surfaces":null,
        "clutterSamplePoints":{"sampleCount":0,"samplePoints":null}}}})", t, &err));
    CHECK(t.name == "mp_m_speedball" && t.surfaces.empty() && t.samplePoints.empty());
    // One surface, layers as values, bitmask as bytes, points as values.
    StockClutterLayer L = testLayer();
    const std::string json = R"({"asset":{"fields":{"name":"t","surfaces":{"values":[{"objToWld":{"quat":{"v":[0,0,0,1]},"origin":{"v":[10,20,30]}},
        "heightOffset":-5,"heightScale":300,
        "clutterLayers":{"values":[{"sampleOffset":4,"bitmaskOffset":3,"tileCount":{"v":[2,2]},"tileSizeUV":{"v":[0.5,0.5]},
            "layerSize":{"v":[1000,1000]},"angleMin":{"v":[0,0,0]},"angleMax":{"v":[0,6.25,0]},"zOffsetMinMax":{"v":[-1,1]},
            "scaleMinMax":{"v":[0.8,1.2]},"maxInstanceCount":100,"tileInstanceCountOffset":2,"useSurfaceNormal":1}]},
        "clutterSampleBitmask":{"wordCount":2,"bitmask":{"bytes":"0100000080000000"}}}]},
        "clutterSamplePoints":{"sampleCount":2,"samplePoints":{"values":[{"v":[0.25,0.5]},[0.75,0.125]]}}}}})";
    CHECK(parseStockClutterTerrain(json, t, &err));
    CHECK(t.surfaces.size() == 1 && t.samplePoints.size() == 2);
    if (t.surfaces.size() == 1 && t.samplePoints.size() == 2) {
        const auto& s = t.surfaces[0];
        CHECK(s.objToWld.origin == Vec3(10, 20, 30) && s.heightScale == 300 && s.heightOffset == -5);
        CHECK(s.layers.size() == 1 && s.bitmask.size() == 2 && s.bitmask[0] == 1 && s.bitmask[1] == 0x80);
        if (!s.layers.empty()) {
            CHECK(s.layers[0].sampleOffset == 4 && s.layers[0].tileCount[1] == 2 && s.layers[0].angleMax[1] == 6.25f);
            CHECK(s.layers[0].useSurfaceNormal == 1 && s.layers[0].maxInstanceCount == 100);
        }
        CHECK(t.samplePoints[1][0] == 0.75f && t.samplePoints[1][1] == 0.125f);
    }
    // Layers as hex bytes decode to the same record.
    uint8_t rec[kClutterLayerBytes] = {};
    encodeStockClutterLayer(L, rec);
    const std::string hexJson = std::string(R"({"fields":{"surfaces":[{"clutterLayers":{"bytes":")") +
                                hexOf(std::vector<uint8_t>(rec, rec + kClutterLayerBytes)) + R"("}}]}})";
    CHECK(parseStockClutterTerrain(hexJson, t, &err));
    CHECK(t.surfaces.size() == 1 && t.surfaces[0].layers.size() == 1 && t.surfaces[0].layers[0].layerSize[0] == 1000.0f);
    CHECK(!parseStockClutterTerrain("not json", t, &err));
}

TEST(stock_clutter_ground_grid_and_map_scatter) {
    StockGroundGrid g;
    g.setCellSize(64);
    // Ground quad at z 5 over [-100, 100]^2, and a roof at z 80 over [0, 50]^2.
    g.addTriangle({-100, -100, 5}, {100, -100, 5}, {100, 100, 5});
    g.addTriangle({-100, -100, 5}, {100, 100, 5}, {-100, 100, 5});
    g.addTriangle({0, 0, 80}, {50, 50, 80}, {50, 0, 80});  // clockwise: still faces up in the grid
    g.addTriangle({0, 0, 0}, {0, 10, 0}, {0, 0, 10});     // vertical: ignored
    g.finish();
    CHECK(g.triangleCount() == 3);
    double z = 0;
    Vec3 n;
    CHECK(g.sample(-50, 20, 1e9, z, n) && near(z, 5) && nearV(n, Vec3{0, 0, 1}));
    CHECK(g.sample(40, 10, 1e9, z, n) && near(z, 80));
    CHECK(g.sample(40, 10, 50, z, n) && near(z, 5));  // under the roof
    CHECK(!g.sample(500, 0, 1e9, z, n));

    // The whole-map scatter: one collection over the grid, origin at z 0 (local height = world z).
    StockClutterTerrain t;
    StockClutterSurface sf;
    StockClutterLayer L = testLayer();
    L.layerSize[0] = 200;
    L.layerSize[1] = 200;
    sf.layers.push_back(L);
    sf.bitmask = testBitmask(L);
    t.surfaces.push_back(sf);
    t.samplePoints = testPoints();
    StockClutterCollection c;
    c.index = 4;
    c.model = "tm/grass";
    std::vector<StockClutterCollection> cols{c};
    StockClutterScatter out;
    scatterStockClutter(cols, t, [&](uint32_t s) { return s == 0 ? meshGround(g, t.surfaces[0].objToWld, 60) : ClutterGroundFn{}; },
                        ClutterScatterOptions{}, out);
    CHECK(out.instances.size() == 4);
    for (const auto& i : out.instances) {
        CHECK(i.collection == 4 && out.models[i.model] == "tm/grass");
        CHECK(i.origin.z >= 4 - 1e-9 && i.origin.z <= 6 + 1e-9);  // ground 5 +- zOffset 1 (the roof is above zMax 60)
    }
    // A collection naming a missing surface or layer is skipped.
    c.terrainSurfaceIndex = 2;
    StockClutterScatter none;
    scatterStockClutter({c}, t, [&](uint32_t) { return flatGround(0); }, ClutterScatterOptions{}, none);
    CHECK(none.instances.empty());

    // build() from a StockWorld keeps up-facing triangles of the kept materials.
    StockWorld w;
    ImportedMesh m;
    m.material = "tw/grass";
    m.pos = {{0, 0, 1}, {10, 0, 1}, {0, 10, 1}, {0, 0, 9}, {0, 10, 9}, {10, 0, 9}};
    m.idx = {0, 1, 2, 3, 4, 5};  // second triangle faces down
    w.meshes.push_back(m);
    StockGroundGrid gw;
    gw.build(w, 32);
    CHECK(gw.triangleCount() == 1 && gw.sample(2, 2, 1e9, z, n) && near(z, 1));
    gw.build(w, 32, [](const std::string& mat) { return mat != "tw/grass"; });
    CHECK(gw.triangleCount() == 0);
}

TEST(stock_clutter_real_speedball_has_none) {
    // Speedball's grass is placed static models: its gfx_map has clutterCollections null (research 100 section 1).
    const char* local = std::getenv("LOCALAPPDATA");
    const fs::path dir = local ? fs::path(local) / "IW8 Radiant Suite" / "IW8 Radiant Suite" / "world_cache" / "mp_m_speedball" : fs::path();
    std::error_code ec;
    if (!local || !fs::exists(dir, ec)) {
        test::skipf("  SKIP world_cache/mp_m_speedball not found\n");
        return;
    }
    std::vector<StockClutterCollection> c;
    std::string err;
    CHECK(readStockClutterCollections(dir.string(), c, &err));
    CHECK(c.empty());
}
