// Stock super-terrain layer tests (src/core/stock_terrain_layers.cpp, docs/research/103_stock_terrain_layers.md).
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "geometry.h"
#include "stock_super_terrain.h"
#include "stock_terrain_layers.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

// An ACTS-shaped export: flattenedImages with builtins, one surface with three layers (index arrays as values and as
// hex bytes), a quadtree with braces in strings that must be skipped.
std::string layerJson() {
    return R"({"asset":{"type":"StTerrain","fields":{
 "name":{"string":"mp_test"},
 "flattenedImagesCount":8,
 "flattenedImages":{"type":"GfxImage *","count":8,"values":[
   {"name":",$white"},{"name":"mp_test_layer_mask_0_1"},{"name":",$black"},
   {"name":"dirt_c&defaultspec~11"},{"name":"dirt_n&dirt_o&dirt_g~12"},{"name":"grass_c&defaultspec~13"},
   {"name":"white&grass_r~14"},{"name":"rock_c&rock_r~15"}]},
 "surfaces":{"values":[
  {"grid":{"levels":{"values":[{"nodes":{"values":[{"mesh":{"s":"}]{\"["}}]}}]}},
   "layerCount":3,
   "layerMaterialsCount":3,
   "layerMaterials":{"count":3,"values":[
     {"material":{"name":",mc/dirt_soil_01"},"materialTextureIdxs":{"values":[3,4,65535,65535]},
      "stFromXYZ":[{"v":[0.0078125,0.0,0.0,0.0]},{"v":[0.0,-0.0078125,0.0,0.0]}],"himipRadiusInvSq":0.000154,"layer":0,
      "layerData":{"flags":579,"alphaRevealSoftEdge":1.0,"textureScale":{"v":[0.0078125,0.0078125]},"colorTint":{"v":[1.0,1.0,1.0]}}},
     {"material":{"name":",mc/grass_winter_01_rev_top"},"materialTextureIdxs":{"encoding":"hex-little-endian","bytes":"050004000600ffff"},
      "stFromXYZ":[{"v":[0.00390625,0.0,0.0,0.5]},{"v":[0.0,-0.00390625,0.0,0.0]}],"layer":1,
      "layerData":{"flags":66119,"alphaRevealSoftEdge":0.15000000596046448}},
     {"material":{"name":",mc/rock"},"materialTextureIdxs":{"values":[7,4,7,65535]},
      "stFromXYZ":[{"v":[0.0078125,0.0,0.0,0.0]},{"v":[0.0,-0.0078125,0.0,0.0]}],"layer":2,
      "layerData":{"flags":131079,"alphaRevealSoftEdge":0.4}}]},
   "layerMaskMapIdxsCount":3,"layerMaskMapIdxs":{"values":[0,1,2]},
   "combinedMapStreamRadiusInvSq":4.7e-09}
 ]}}}})";
}

void put32(std::string& b, size_t o, uint32_t v) {
    for (int k = 0; k < 4; ++k) b[o + size_t(k)] = char(uint8_t(v >> (8 * k)));
}

std::string ddsHeader(int w, int h, uint32_t dxgi, size_t payload) {
    std::string b(148 + payload, '\0');
    b.replace(0, 4, "DDS ");
    put32(b, 4, 124);
    put32(b, 12, uint32_t(h));
    put32(b, 16, uint32_t(w));
    b.replace(84, 4, "DX10");
    put32(b, 128, dxgi);
    return b;
}

StockTerrainLayerMask r8Mask(int w, int h, uint8_t (*f)(int, int)) {
    StockTerrainLayerMask m;
    m.width = w;
    m.height = h;
    m.data.resize(size_t(w) * size_t(h));
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) m.data[size_t(j * w + i)] = f(i, j);
    return m;
}

}  // namespace

TEST(stock_terrain_layers_parse) {
    StockTerrainLayerTable t;
    std::string err;
    CHECK(parseStockTerrainLayers(layerJson(), t, &err));
    CHECK(err.empty());
    CHECK(t.flattenedImages.size() == 8 && t.flattenedImages[0] == ",$white");
    CHECK(t.surfaces.size() == 1);
    if (t.surfaces.size() != 1 || t.surfaces[0].layers.size() != 3) {
        CHECK(false);
        return;
    }
    const StockTerrainSurfaceLayers& s = t.surfaces[0];
    CHECK(near(s.combinedMapStreamRadiusInvSq, 4.7e-9, 1e-15));
    const StockTerrainLayerInfo& l0 = s.layers[0];
    const StockTerrainLayerInfo& l1 = s.layers[1];
    const StockTerrainLayerInfo& l2 = s.layers[2];
    CHECK(l0.material == "mc/dirt_soil_01" && l0.layer == 0);
    CHECK(l0.maskKind == StockTerrainLayerInfo::Mask::White && l0.maskImage.empty());
    CHECK(l0.colorImage == "dirt_c&defaultspec~11" && l0.normalImage == "dirt_n&dirt_o&dirt_g~12");
    CHECK(l0.revealImage.empty() && l0.displacementImage.empty());
    CHECK(l0.flags == 0x243 && !l0.alphaReveal() && !l0.revealRamp() && l0.coversAtFullWeight());
    CHECK(near(l0.stFromXYZ[1][1], -0.0078125) && near(l0.textureScale[0], 0.0078125) && near(l0.himipRadiusInvSq, 0.000154));
    // Layer 1: hex-encoded indices, reveal ramp, soft edge 0.15 -> byte 38.
    CHECK(l1.material == "mc/grass_winter_01_rev_top" && l1.maskKind == StockTerrainLayerInfo::Mask::Image);
    CHECK(l1.maskImage == "mp_test_layer_mask_0_1");
    CHECK(l1.colorImage == "grass_c&defaultspec~13" && l1.normalImage == "dirt_n&dirt_o&dirt_g~12");
    CHECK(l1.revealImage == "white&grass_r~14" && l1.displacementImage.empty());
    CHECK(l1.flags == 0x10247 && l1.alphaReveal() && l1.revealRamp() && !l1.hardEdge());
    CHECK(l1.softEdgeByte() == 38 && near(l1.revealK(), 38.0 / 255));
    CHECK(l1.coversAtFullWeight());
    CHECK(near(l1.stFromXYZ[0][3], 0.5));
    // Layer 2: black mask, hard edge, alpha from a colour image (does not cover).
    CHECK(l2.maskKind == StockTerrainLayerInfo::Mask::Black);
    CHECK(l2.flags == 0x20007 && l2.hardEdge() && !l2.alphaReveal() && l2.coversAtFullWeight());
    CHECK(stockTerrainLayerMaskImageNames(t) == std::vector<std::string>{"mp_test_layer_mask_0_1"});
    CHECK(stockTerrainLayerMaskImageNames(t, 3).empty());
    CHECK(t.warnings.empty());

    StockTerrainLayerInfo alphaFromColour = l1;
    alphaFromColour.revealImage = "rock_c&rock_r~15";
    CHECK(!alphaFromColour.coversAtFullWeight());

    StockTerrainLayerTable bad;
    CHECK(!parseStockTerrainLayers("[1]", bad, &err));
    CHECK(!parseStockTerrainLayers("{\"asset\":{\"fields\":{\"name\":\"x\"}}}", bad, &err));
}

TEST(stock_terrain_layers_bc4) {
    // r0 > r1: 8-value palette; index 0 -> r0, 1 -> r1, 2 -> (6 r0 + r1) / 7.
    const uint8_t blk[8] = {210, 70, 0, 0, 0, 0, 0, 0};
    uint8_t px[16];
    decodeStockTerrainBc4Block(blk, px);
    CHECK(px[0] == 210 && px[15] == 210);
    uint8_t blk2[8] = {210, 70, 0, 0, 0, 0, 0, 0};
    // texel 0 index 1, texel 1 index 2, texel 2 index 7 (bits 0-2, 3-5, 6-8).
    const uint64_t bits = 1ull | (2ull << 3) | (7ull << 6);
    for (int i = 0; i < 6; ++i) blk2[2 + i] = uint8_t(bits >> (8 * i));
    decodeStockTerrainBc4Block(blk2, px);
    CHECK(px[0] == 70 && px[1] == 190 && px[2] == 90);
    // r0 <= r1: 6-value palette plus 0 and 255.
    uint8_t blk3[8] = {40, 240, 0, 0, 0, 0, 0, 0};
    const uint64_t b3 = 6ull | (7ull << 3) | (2ull << 6);
    for (int i = 0; i < 6; ++i) blk3[2 + i] = uint8_t(b3 >> (8 * i));
    decodeStockTerrainBc4Block(blk3, px);
    CHECK(px[0] == 0 && px[1] == 255 && px[2] == uint8_t((4 * 40 + 1 * 240 + 2) / 5));

    // A 6 x 5 BC4 DDS (2 x 2 blocks, partial edge blocks).
    std::string dds = ddsHeader(6, 5, 80, 4 * 8);
    for (int b = 0; b < 4; ++b) {
        dds[148 + size_t(b) * 8] = char(uint8_t(10 + b * 50));
        dds[148 + size_t(b) * 8 + 1] = char(0);
    }
    StockTerrainLayerMask m;
    std::string err;
    CHECK(decodeStockTerrainLayerMaskDds(dds, m, &err));
    CHECK(m.width == 6 && m.height == 5 && m.data.size() == 30);
    CHECK(m.at(0, 0) == 10 && m.at(5, 0) == 60 && m.at(0, 4) == 110 && m.at(5, 4) == 160);
    CHECK(m.at(-3, 99) == 110);  // clamped
    // R8 and a wrong format.
    std::string r8 = ddsHeader(2, 2, 61, 4);
    r8[148] = char(0);
    r8[149] = char(255);
    r8[150] = char(255);
    r8[151] = char(255);
    CHECK(decodeStockTerrainLayerMaskDds(r8, m, &err));
    CHECK(m.width == 2 && near(m.sample(0, 0), 0) && near(m.sample(1, 1), 1) && near(m.sample(0.5, 0.5), 0.75));
    CHECK(!decodeStockTerrainLayerMaskDds(ddsHeader(2, 2, 56, 8), m, &err));
    CHECK(!decodeStockTerrainLayerMaskDds(ddsHeader(8, 8, 80, 8), m, &err));  // truncated
}

TEST(stock_terrain_layers_blend_math) {
    // Reveal ramp: w = 0 hides, w = 1 shows wherever reveal > 0, k = 0 is a hard step at 1 - w.
    CHECK(near(stockTerrainRevealRamp(0, 0.99, 0.2), 0));
    CHECK(near(stockTerrainRevealRamp(1, 0.01, 0.2), 1));
    CHECK(near(stockTerrainRevealRamp(1, 0.0, 0.2), 0));
    CHECK(near(stockTerrainRevealRamp(0.5, 0.6, 0), 1) && near(stockTerrainRevealRamp(0.5, 0.4, 0), 0));
    // k = 0.2, w = 0.25: lo = 0.75 - 0.1 = 0.65, hi = 0.75 + 0.2 * sqrt(0.75).
    const double hi = 0.75 + 0.2 * std::sqrt(0.75);
    CHECK(near(stockTerrainRevealRamp(0.25, 0.7, 0.2), (0.7 - 0.65) / (hi - 0.65), 1e-12));

    StockTerrainLayerInfo L;
    L.layer = 1;
    L.flags = 0x10247;
    L.alphaRevealSoftEdge = 0.2;
    const double k = L.revealK();
    CHECK(near(k, 51.0 / 255));
    CHECK(near(stockTerrainLayerBlendWeight(L, 0.25, 0.5, 0.7), 0.5 * stockTerrainRevealRamp(0.25, 0.7, k), 1e-12));
    L.flags = 0x10243;  // alpha only: mask * alpha
    CHECK(near(stockTerrainLayerBlendWeight(L, 0.25, 0.5, 0.7), 0.125));
    L.flags = 0x243;    // no reveal image: the mask
    CHECK(near(stockTerrainLayerBlendWeight(L, 0.25, 0.5, 0.7), 0.25));
    L.flags = 0x30243;  // hard edge on alpha * mask
    CHECK(near(stockTerrainLayerBlendWeight(L, 0.6, 1.0, 0.0), 1) && near(stockTerrainLayerBlendWeight(L, 0.6, 0.8, 0.0), 0));
    L.layer = 0;        // layer 0: the mask alone
    CHECK(near(stockTerrainLayerBlendWeight(L, 0.6, 0.0, 0.0), 0.6));

    // Contributions: lerp order, later layers on top.
    std::vector<double> c;
    CHECK(stockTerrainLayerContributions({1, 0.5, 1}, c) == 2);
    CHECK(near(c[0], 0) && near(c[1], 0) && near(c[2], 1));
    CHECK(stockTerrainLayerContributions({1, 0.25, 0.5}, c) == 2);
    CHECK(near(c[0], 0.375) && near(c[1], 0.125) && near(c[2], 0.5));
    CHECK(stockTerrainLayerContributions({1, 0.5, 0}, c) == 0);  // tie keeps the lower layer

    // UV: (s, 1 - t) from the world position.
    StockTerrainLayerInfo u;
    u.stFromXYZ[0][0] = 0.0078125;
    u.stFromXYZ[0][3] = 0.5;
    u.stFromXYZ[1][1] = -0.0078125;
    const auto uv = stockTerrainLayerUv(u, Vec3{256, 128, 40});
    CHECK(near(uv[0], 2.5) && near(uv[1], 2.0));
}

TEST(stock_terrain_layers_patch) {
    // One 8 x 8-cell surface (9 texels a side, 16 inches), origin (1000, 2000), flat at z 0.
    StockTerrain terrain;
    StockTerrainSurface s;
    s.index = 0;
    s.origin = Vec3{1000, 2000, 0};
    s.halfWldSize = 64;
    s.texelSize = 16;
    s.heightOffset = 0;
    s.heightScale = 65535;
    s.mapResolution = 9;
    s.paddedResolution = 12;
    StockTerrainLayer tl;
    tl.material = "mc/base";
    tl.stFromXYZ[0][0] = 1.0 / 128;
    tl.stFromXYZ[1][1] = -1.0 / 128;
    s.layers.push_back(tl);
    terrain.surfaces.push_back(s);
    StockTerrainHeightGrid h;
    h.width = h.height = 12;
    h.data.assign(144, 0);
    StockTerrainMesh mesh;
    std::string err;
    CHECK(buildStockTerrainMesh(terrain, 0, h, nullptr, StockTerrainBuildOptions{}, mesh, &err));
    CHECK(mesh.mesh.idx.size() == 8 * 8 * 6);

    StockTerrainLayerTable table;
    StockTerrainSurfaceLayers sl;
    sl.surface = 0;
    auto layer = [](int i, const char* mat, StockTerrainLayerInfo::Mask kind, uint32_t flags) {
        StockTerrainLayerInfo L;
        L.layer = i;
        L.material = mat;
        L.maskKind = kind;
        L.flags = flags;
        L.stFromXYZ[0][0] = 1.0 / 256;
        L.stFromXYZ[1][1] = -1.0 / 256;
        L.alphaRevealSoftEdge = 0.5;
        if (kind == StockTerrainLayerInfo::Mask::Image) L.maskImage = std::string("mask_") + char('0' + i);
        if (flags & kStLayerAlphaReveal) L.revealImage = "white&r~1";
        return L;
    };
    sl.layers.push_back(layer(0, "mc/base", StockTerrainLayerInfo::Mask::White, 0x243));
    sl.layers.push_back(layer(1, "mc/left", StockTerrainLayerInfo::Mask::Image, 0x10247));    // columns 0..3 at 1
    sl.layers.push_back(layer(2, "mc/none", StockTerrainLayerInfo::Mask::Black, 0x10247));
    sl.layers.push_back(layer(3, "mc/top", StockTerrainLayerInfo::Mask::Image, 0x10247));     // rows 0..1 at 1
    sl.layers.push_back(layer(4, "mc/half", StockTerrainLayerInfo::Mask::Image, 0x243));      // 0.5 on column 8
    table.surfaces.push_back(sl);
    std::vector<StockTerrainLayerMask> masks(5);
    masks[1] = r8Mask(12, 12, [](int i, int) -> uint8_t { return i <= 3 ? 255 : 0; });
    masks[3] = r8Mask(12, 12, [](int, int j) -> uint8_t { return j <= 1 ? 255 : 0; });
    masks[4] = r8Mask(12, 12, [](int i, int) -> uint8_t { return i >= 8 ? 128 : 0; });

    StockTerrainLayerPatchOptions opt;
    opt.radius = 0;  // whole surface
    StockTerrainLayerPatch p;
    CHECK(buildStockTerrainLayerPatch(terrain, table, mesh, masks, opt, p, &err));
    CHECK(p.surface == 0 && p.baseMaterial == "mc/base" && p.base.idx.size() == mesh.mesh.idx.size());
    CHECK(p.base.pos.size() == 81 && p.albedoUv.size() == 81 && p.sourceVertex.size() == 81);
    CHECK(p.warnings.empty());
    // Sets in ascending order: 1, 3, 4 (2 is black, 0 is the base).
    CHECK(p.layers.size() == 3);
    if (p.layers.size() == 3) {
        const StockTerrainLayerSet& a = p.layers[0];
        const StockTerrainLayerSet& b = p.layers[1];
        const StockTerrainLayerSet& c = p.layers[2];
        CHECK(a.layer == 1 && b.layer == 3 && c.layer == 4);
        // Layer 1 has weight on columns 0..3 (triangles touching texel columns <= 3: cells 0..3, 64 triangles), but
        // layer 3 covers rows 0..1: the 8 triangles of row 0 cells have all 3 vertices covered, and in cell (3, 1) the
        // triangle (3,1) (4,1) (4,2) has no vertex where layer 1 is both weighted and uncovered. 64 - 9 = 55.
        CHECK(a.mesh.idx.size() / 3 == 55);
        CHECK(a.revealRamp && near(a.revealK, 127.0 / 255) && a.material == "mc/left" && a.revealImage == "white&r~1");
        CHECK(a.weight.size() == a.mesh.pos.size() && a.mesh.uv.size() == a.mesh.pos.size());
        bool weightsOk = true, uvOk = true;
        for (size_t v = 0; v < a.weight.size(); ++v) {
            const double tx = (a.mesh.pos[v].x - 1000 + 64) / 16;
            weightsOk &= near(a.weight[v], tx <= 3.0 + 1e-9 ? 1.0 : 0.0, 1e-6);
            uvOk &= near(a.mesh.uv[v][0], a.mesh.pos[v].x / 256, 1e-9) && near(a.mesh.uv[v][1], 1.0 + a.mesh.pos[v].y / 256, 1e-9);
        }
        CHECK(weightsOk && uvOk);
        // Layer 3: rows 0..1 -> cells in rows 0..1 = 16 cells, 32 triangles.
        CHECK(b.mesh.idx.size() / 3 == 32);
        // Layer 4: column 8 at 0.5 -> cells in column 7 = 8 cells, 16 triangles; mean weight over its vertices 0.25.
        CHECK(c.mesh.idx.size() / 3 == 16 && near(c.meanWeight, 128.0 / 255 / 2, 1e-6) && !c.revealRamp);
        CHECK(p.layerTriangles == 55 + 32 + 16);
    }
    // Dominant: column 8 -> layer 4 (128/255 beats the 127/255 left of what is under it; 9 vertices), rows 0..1 of
    // columns 0..7 -> layer 3 (16), columns 0..3 of rows 2..8 -> layer 1 (28), the rest the base.
    CHECK(p.dominantCount.size() == 5);
    if (p.dominantCount.size() == 5) {
        CHECK(p.dominantCount[4] == 9 && p.dominantCount[3] == 16 && p.dominantCount[1] == 28);
        CHECK(p.dominantCount[0] == 81 - 9 - 16 - 28 && p.dominantCount[2] == 0);
    }

    // Cover culling only (minContribution 0): the 8 fully covered triangles go.
    opt.minContribution = 0;
    CHECK(buildStockTerrainLayerPatch(terrain, table, mesh, masks, opt, p, &err));
    CHECK(!p.layers.empty() && p.layers[0].mesh.idx.size() / 3 == 56);
    // No culling: layer 1 gets all 64 triangles.
    opt.cullHidden = false;
    CHECK(buildStockTerrainLayerPatch(terrain, table, mesh, masks, opt, p, &err));
    CHECK(!p.layers.empty() && p.layers[0].mesh.idx.size() / 3 == 64);
    opt.minContribution = 4.0 / 255;

    // Radius: centre on texel (2, 2) (world 968, 1968), 20 inches -> the 8 triangles around it with centroids inside.
    opt.cullHidden = true;
    opt.centerX = 968;
    opt.centerY = 1968;
    opt.radius = 12;
    CHECK(buildStockTerrainLayerPatch(terrain, table, mesh, masks, opt, p, &err));
    CHECK(p.base.idx.size() / 3 > 0 && p.base.idx.size() / 3 <= 8);
    for (const Vec3& v : p.base.pos) CHECK(std::fabs(v.x - 968) <= 16 + 1e-9 && std::fabs(v.y - 1968) <= 16 + 1e-9);

    // A missing image mask is a warning and the layer is skipped.
    std::vector<StockTerrainLayerMask> few(2);
    few[1] = masks[1];
    opt.radius = 0;
    CHECK(buildStockTerrainLayerPatch(terrain, table, mesh, few, opt, p, &err));
    CHECK(p.warnings.size() == 2);
    CHECK(p.layers.size() == 1 && p.layers[0].layer == 1);

    // Weights at a point, surfaces near a point.
    std::vector<double> w;
    stockTerrainLayerWeightsAt(terrain.surfaces[0], sl, masks, 1000 - 64 + 3.5 * 16, 2000, w);
    CHECK(w.size() == 5 && near(w[0], 1) && near(w[1], 0.5) && near(w[2], 0) && near(w[3], 0));
    CHECK(stockTerrainSurfacesNear(terrain, 1000, 2000, 1) == std::vector<int>{0});
    CHECK(stockTerrainSurfacesNear(terrain, 1200, 2000, 100).empty());
    CHECK(stockTerrainSurfacesNear(terrain, 1100, 2000, 40) == std::vector<int>{0});

    // Errors.
    StockTerrainMesh other = mesh;
    other.surface = 3;
    CHECK(!buildStockTerrainLayerPatch(terrain, table, other, masks, opt, p, &err));
    StockTerrainLayerTable empty;
    CHECK(!buildStockTerrainLayerPatch(terrain, empty, mesh, masks, opt, p, &err));
}

// Real data (opt-in): IW8R_STL_VERIFY=<dir with mp_donetsk_layer_mask_<s>_<L>.*.dds>, the stterrain export and height /
// cutout images from the editor's world cache (or IW8R_STL_JSON / IW8R_STL_IMAGES). Prints the layer sets around
// IW8R_STL_POINT (default -24900 -1700) and, with IW8R_STL_OUT=<file>, writes the dominant layer per 16-inch point of
// a 12,000-inch square around it (u16 width, height, then per point u8 surface, u8 layer; 255 = none).
TEST(stock_terrain_layers_real_data) {
    const char* maskDir = std::getenv("IW8R_STL_VERIFY");
    if (!maskDir) return;
    namespace fs = std::filesystem;
    std::string json, images;
    if (const char* j = std::getenv("IW8R_STL_JSON")) json = j;
    if (const char* im = std::getenv("IW8R_STL_IMAGES")) images = im;
    if (json.empty() || images.empty()) {
        const char* local = std::getenv("LOCALAPPDATA");
        if (!local) return;
        const fs::path z = fs::path(local) / "IW8 Radiant" / "IW8 Radiant" / "world_cache" / "mp_donetsk" / "terrain" /
                           "mw19replay" / "mp_donetsk" / "assets";
        if (json.empty()) json = (z / "stterrain" / "mp_donetsk.0.asset.json").string();
        if (images.empty()) images = (z / "image").string();
    }
    double px = -24900, py = -1700, radius = 6000;
    if (const char* p = std::getenv("IW8R_STL_POINT")) std::sscanf(p, "%lf %lf %lf", &px, &py, &radius);
    std::ifstream f(json, std::ios::binary);
    if (!f) {
        std::printf("  stl: no %s, skipped\n", json.c_str());
        return;
    }
    const auto t0 = std::chrono::steady_clock::now();
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    StockTerrain terrain;
    StockTerrainLayerTable table;
    std::string err;
    CHECK(parseStockTerrain(text, terrain, &err));
    CHECK(parseStockTerrainLayers(text, table, &err));
    text.clear();
    const double parseMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  stl: %zu surfaces, %zu flattened images, %zu layer masks, parsed in %.0f ms, %zu warnings\n",
                table.surfaces.size(), table.flattenedImages.size(), stockTerrainLayerMaskImageNames(table).size(), parseMs,
                table.warnings.size());
    CHECK(table.surfaces.size() == terrain.surfaces.size());

    const std::vector<int> near = stockTerrainSurfacesNear(terrain, px, py, radius);
    std::vector<std::vector<StockTerrainLayerMask>> allMasks(terrain.surfaces.size());
    std::vector<int> used;
    for (int si : near) {
        const StockTerrainSurface& s = terrain.surfaces[size_t(si)];
        const StockTerrainSurfaceLayers& sl = table.surfaces[size_t(si)];
        StockTerrainHeightGrid hg;
        StockTerrainCutout cut;
        const std::string hp = findStockTerrainImage(images, s.heightImage);
        if (hp.empty() || !readStockTerrainHeightDds(hp, hg, &err)) {
            std::printf("  stl: s%d no height image, skipped\n", si);
            continue;
        }
        const std::string cp = s.cutoutImage.empty() ? std::string() : findStockTerrainImage(images, s.cutoutImage);
        const bool haveCut = !cp.empty() && readStockTerrainCutoutDds(cp, cut, &err);
        std::vector<StockTerrainLayerMask>& masks = allMasks[size_t(si)];
        masks.resize(sl.layers.size());
        size_t missing = 0;
        for (size_t L = 0; L < sl.layers.size(); ++L) {
            if (sl.layers[L].maskKind != StockTerrainLayerInfo::Mask::Image) continue;
            const std::string mp = findStockTerrainImage(maskDir, sl.layers[L].maskImage);
            if (mp.empty() || !readStockTerrainLayerMaskDds(mp, masks[L], &err)) ++missing;
        }
        if (missing) {
            std::printf("  stl: s%d %zu layer masks missing, skipped\n", si, missing);
            continue;
        }
        used.push_back(si);
        StockTerrainMesh mesh;
        StockTerrainBuildOptions bo;
        CHECK(buildStockTerrainMesh(terrain, si, hg, haveCut ? &cut : nullptr, bo, mesh, &err));
        for (int stride : {1, 2, 0, -1}) {  // 0: stride 1 with cover culling only, -1: stride 1, 16 / 255
            StockTerrainMesh m2;
            const StockTerrainMesh* use = &mesh;
            if (stride > 1) {
                bo.stride = stride;
                CHECK(buildStockTerrainMesh(terrain, si, hg, haveCut ? &cut : nullptr, bo, m2, &err));
                use = &m2;
            }
            StockTerrainLayerPatchOptions opt;
            opt.centerX = px;
            opt.centerY = py;
            opt.radius = radius;
            if (stride == 0) opt.minContribution = 0;
            if (stride == -1) opt.minContribution = 16.0 / 255;
            StockTerrainLayerPatch p;
            const auto t1 = std::chrono::steady_clock::now();
            CHECK(buildStockTerrainLayerPatch(terrain, table, *use, masks, opt, p, &err));
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
            std::printf("  stl: s%d stride %d: base %zu vertices %zu triangles (%s), %zu layer sets, %zu layer triangles, %.0f ms\n",
                        si, stride, p.base.pos.size(), p.base.idx.size() / 3, p.baseMaterial.c_str(), p.layers.size(),
                        p.layerTriangles, ms);
            CHECK(p.warnings.empty());
            if (stride != 1) continue;
            for (const StockTerrainLayerSet& set : p.layers) {
                float mx = 0;
                size_t full = 0;
                for (float w : set.weight) {
                    mx = std::max(mx, w);
                    full += w >= 254.5f / 255 ? 1 : 0;
                }
                std::printf("    L%-2d %-46s tris %7zu verts %7zu mean w %.3f max %.3f at1 %5.1f%% k %.3f %s dominant %zu\n",
                            set.layer, set.material.c_str(), set.mesh.idx.size() / 3, set.weight.size(), set.meanWeight,
                            double(mx), 100.0 * double(full) / double(std::max<size_t>(1, set.weight.size())), set.revealK,
                            set.revealRamp ? "ramp" : (set.alphaReveal ? "alpha" : "mask"), p.dominantCount[size_t(set.layer)]);
            }
            std::printf("    L0  %-46s base, dominant %zu\n", p.baseMaterial.c_str(), p.dominantCount[0]);
        }
    }
    if (const char* outPath = std::getenv("IW8R_STL_OUT")) {
        const double step = 16, half = radius;
        const int n = int(2 * half / step);
        std::string buf;
        buf.push_back(char(n & 255));
        buf.push_back(char(n >> 8));
        buf.push_back(char(n & 255));
        buf.push_back(char(n >> 8));
        std::vector<double> w, c;
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                const double x = px - half + (i + 0.5) * step, y = py + half - (j + 0.5) * step;  // row 0 = north
                int surf = 255, lay = 255;
                for (int si : used) {
                    const StockTerrainSurface& s = terrain.surfaces[size_t(si)];
                    if (std::fabs(x - s.origin.x) > s.halfWldSize || std::fabs(y - s.origin.y) > s.halfWldSize) continue;
                    stockTerrainLayerWeightsAt(s, table.surfaces[size_t(si)], allMasks[size_t(si)], x, y, w);
                    lay = stockTerrainLayerContributions(w, c);
                    surf = si;
                    break;  // smallest surface first
                }
                buf.push_back(char(surf));
                buf.push_back(char(lay));
            }
        std::ofstream o(outPath, std::ios::binary);
        o.write(buf.data(), std::streamsize(buf.size()));
        std::printf("  stl: wrote %s (%d x %d)\n", outPath, n, n);
    }
}
