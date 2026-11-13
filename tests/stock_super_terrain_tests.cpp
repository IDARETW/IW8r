// Stock super-terrain mesh tests (src/core/stock_super_terrain.cpp, docs/research/102_stock_super_terrain.md).
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "geometry.h"
#include "stock_super_terrain.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

// An ACTS-shaped export: two surfaces, maps table, union views, a quadtree with braces and quotes in its strings (the
// reader must skip it without parsing), a builtin ",$white" cutout on surface 1.
std::string testJson() {
    return R"({"format":"mw19-asset-json","pool":"stterrain","asset":{"type":"StTerrain","nodes":9,"fields":{
 "name":{"type":"const char","string":"mp_test"},
 "surfaceCount":2,
 "maps":{"type":"StTerrainMaps","count":2,"values":[{"mapResolution":5,"paddedResolution":8},{"mapResolution":3,"paddedResolution":4}]},
 "surfaces":{"type":"StDiskTerrainSurface","count":2,"values":[
  {"highLODIndices":{"bytes":"0000"},
   "grid":{"numLevels":1,"levels":{"values":[{"width":1,"height":1,"nodes":{"values":[{"mesh":{"note":"}]{\"[","x":[1,2,{"y":"}"}]}}]}}]}},
   "objToWld":{"quat":{"$union_views":true,"v":[0.0,0.0,0.0,1.0]},"origin":{"$union_views":true,"v":[100.0,200.0,10.0],"__s1":{"x":100.0,"y":200.0,"z":10.0}}},
   "wldBounds":{"midPoint":{"v":[100.0,200.0,0.0]},"halfSize":{"v":[32.0,32.0,50.0]}},
   "surfaceMapSetIndex":0,"surfaceMapWorldSize":16.0,"heightOffset":-5.0,"heightScale":65535.0,"halfWldSize":32.0,
   "layerMaterials":{"count":2,"values":[
     {"material":{"type":"Material","asset_reference":true,"name":",mc/dirt_soil_01"},
      "stFromXYZ":[{"$union_views":true,"v":[0.0078125,0.0,0.0,0.25]},{"$union_views":true,"v":[0.0,-0.0078125,0.0,0.0]}],
      "layerData":{"textureScale":{"v":[0.0078125,0.0078125]}}},
     {"material":{"name":",mc/grass_winter_01"},"stFromXYZ":[{"v":[0.01,0.0,0.0,0.0]},{"v":[0.0,-0.01,0.0,0.0]}]}]},
   "clutterLayers":{"values":[{"tileCount":[8,8]}]},
   "heightMap":{"type":"GfxImage","name":"mp_test_height_0"},"cutoutMap":{"name":"mp_test_cutout_0"},
   "combinedAlbedoMap":{"name":"mp_test_combined_albedo_0"},"combinedNogMap":{"name":"mp_test_combined_nog_0"}},
  {"grid":{"numLevels":0,"levels":null},
   "objToWld":{"quat":{"v":[0.0,0.0,0.0,1.0]},"origin":{"v":[0.0,0.0,0.0]}},
   "surfaceMapSetIndex":1,"surfaceMapWorldSize":32.0,"heightOffset":0.0,"heightScale":100.0,"halfWldSize":32.0,
   "layerMaterials":null,
   "heightMap":{"name":"mp_test_height_1"},"cutoutMap":{"name":",$white"},"combinedAlbedoMap":null,"combinedNogMap":null}
 ]},
 "lightmaps":null}}})";
}

void put32(std::string& b, size_t o, uint32_t v) {
    for (int k = 0; k < 4; ++k) b[o + size_t(k)] = char(uint8_t(v >> (8 * k)));
}

// A DX10 DDS header + raw texels.
std::string dds(int w, int h, uint32_t dxgi, size_t bytesPerTexel) {
    std::string b(148 + size_t(w) * size_t(h) * bytesPerTexel, '\0');
    b.replace(0, 4, "DDS ");
    put32(b, 4, 124);
    put32(b, 12, uint32_t(h));
    put32(b, 16, uint32_t(w));
    b.replace(84, 4, "DX10");
    put32(b, 128, dxgi);
    return b;
}

// Surface 0's heights: 8 x 8 padded, H(i, j) = i + 10 j.
StockTerrainHeightGrid testHeights() {
    StockTerrainHeightGrid g;
    g.width = g.height = 8;
    g.data.resize(64);
    for (int j = 0; j < 8; ++j)
        for (int i = 0; i < 8; ++i) g.data[size_t(j * 8 + i)] = uint16_t(i + 10 * j);
    return g;
}

StockTerrainCutout allPresent(int wx = 2, int wy = 2) {
    StockTerrainCutout c;
    c.wordsX = wx;
    c.wordsY = wy;
    c.words.assign(size_t(wx * wy), 0xFFFFFFFFu);
    return c;
}

void clearCell(StockTerrainCutout& c, int cx, int cy) {
    c.words[size_t((cy >> 2) * c.wordsX + (cx >> 3))] &= ~(1u << ((cy & 3) * 8 + (cx & 7)));
}

}  // namespace

TEST(stock_super_terrain_parse) {
    StockTerrain t;
    std::string err;
    CHECK(parseStockTerrain(testJson(), t, &err));
    CHECK(err.empty());
    CHECK(t.name == "mp_test");
    CHECK(t.surfaces.size() == 2);
    if (t.surfaces.size() != 2) return;
    const StockTerrainSurface& s = t.surfaces[0];
    CHECK(s.index == 0 && s.origin == Vec3(100, 200, 10));
    CHECK(near(s.halfWldSize, 32) && near(s.texelSize, 16) && near(s.heightOffset, -5) && near(s.heightScale, 65535));
    CHECK(s.mapResolution == 5 && s.paddedResolution == 8 && s.cells() == 4);
    CHECK(s.heightImage == "mp_test_height_0" && s.cutoutImage == "mp_test_cutout_0");
    CHECK(s.combinedAlbedoImage == "mp_test_combined_albedo_0" && s.combinedNogImage == "mp_test_combined_nog_0");
    CHECK(s.layers.size() == 2);
    if (s.layers.size() == 2) {
        CHECK(s.layers[0].material == "mc/dirt_soil_01" && s.layers[1].material == "mc/grass_winter_01");
        CHECK(near(s.layers[0].stFromXYZ[0][0], 0.0078125) && near(s.layers[0].stFromXYZ[0][3], 0.25));
        CHECK(near(s.layers[0].stFromXYZ[1][1], -0.0078125) && near(s.layers[0].textureScale[1], 0.0078125));
        CHECK(near(s.layers[1].stFromXYZ[1][1], -0.01));
    }
    CHECK(s.wldBounds.valid() && near(s.wldBounds.mins.x, 68) && near(s.wldBounds.maxs.z, 50));
    const StockTerrainSurface& s1 = t.surfaces[1];
    CHECK(s1.mapResolution == 3 && s1.cutoutImage.empty() && s1.combinedAlbedoImage.empty() && s1.layers.empty());
    CHECK(t.warnings.empty());

    // The name list: heights and cut-outs by default, distinct, in surface order; the builtin $white is left out.
    const auto names = stockTerrainImageNames(t);
    CHECK(names == (std::vector<std::string>{"mp_test_height_0", "mp_test_cutout_0", "mp_test_height_1"}));
    CHECK(stockTerrainImageNames(t, kTerrainAlbedo) == std::vector<std::string>{"mp_test_combined_albedo_0"});

    // Malformed input fails.
    StockTerrain bad;
    CHECK(!parseStockTerrain("{\"asset\":{\"fields\":{\"name\":\"x\"}}}", bad, &err));
    CHECK(!parseStockTerrain("[1,2]", bad, &err));
    CHECK(!parseStockTerrain("{\"asset\":{\"fields\":{\"surfaces\":{\"values\":[{\"grid\":{\"a\":\"}\"}", bad, &err));
}

TEST(stock_super_terrain_dds_decode) {
    std::string h = dds(3, 2, 56, 2);
    for (int k = 0; k < 6; ++k) {
        h[148 + size_t(2 * k)] = char(k * 3);
        h[148 + size_t(2 * k) + 1] = char(k == 5 ? 0xFF : 0);
    }
    StockTerrainHeightGrid g;
    std::string err;
    CHECK(decodeStockTerrainHeightDds(h, g, &err));
    CHECK(g.width == 3 && g.height == 2 && g.data.size() == 6);
    CHECK(g.at(1, 0) == 3 && g.at(0, 1) == 9 && g.at(2, 1) == uint16_t(0xFF00 | 15));
    CHECK(!decodeStockTerrainHeightDds(dds(3, 2, 61, 2), g, &err));  // R8 is not a height map
    CHECK(!decodeStockTerrainHeightDds(h.substr(0, 150), g, &err));  // truncated
    CHECK(!decodeStockTerrainHeightDds("DDS nothing", g, &err));

    std::string c = dds(2, 1, 42, 4);
    put32(c, 148, 0xFFFFFFFEu);  // cell (0, 0) cut out
    put32(c, 152, 0x00000100u);  // word (1, 0): only bit 8 = cell (8, 1)
    StockTerrainCutout cut;
    CHECK(decodeStockTerrainCutoutDds(c, cut, &err));
    CHECK(cut.wordsX == 2 && cut.wordsY == 1);
    CHECK(!cut.present(0, 0) && cut.present(1, 0) && cut.present(7, 3) && cut.present(0, 1));
    CHECK(cut.present(8, 1) && !cut.present(8, 0) && !cut.present(9, 1) && !cut.present(15, 3));
    CHECK(cut.present(16, 0) && cut.present(0, 4));  // outside the words: drawn
    CHECK(!decodeStockTerrainCutoutDds(dds(2, 1, 56, 2), cut, &err));
}

TEST(stock_super_terrain_mesh_stride1) {
    StockTerrain t;
    CHECK(parseStockTerrain(testJson(), t));
    if (t.surfaces.size() != 2) return;
    const StockTerrainHeightGrid g = testHeights();
    StockTerrainMesh m;
    std::string err;
    CHECK(buildStockTerrainMesh(t, 0, g, nullptr, StockTerrainBuildOptions{}, m, &err));
    CHECK(m.surface == 0 && m.stride == 1 && m.cellsTotal == 16 && m.cellsKept == 16);
    CHECK(m.mesh.pos.size() == 25 && m.mesh.idx.size() == 16 * 6);
    CHECK(m.mesh.normal.size() == 25 && m.mesh.uv.size() == 25 && m.albedoUv.size() == 25);
    CHECK(m.mesh.material == "mc/dirt_soil_01" && m.albedoImage == "mp_test_combined_albedo_0");
    // Vertex k = j * 5 + i: x = 100 - 32 + 16 i, y = 200 - 32 + 16 j, z = 10 - 5 + (i + 10 j).
    CHECK(m.mesh.pos[0] == Vec3(68, 168, 5));
    CHECK(m.mesh.pos[7] == Vec3(68 + 32, 168 + 16, 5 + 2 + 10));
    CHECK(m.mesh.pos[24] == Vec3(132, 232, 5 + 4 + 40));
    CHECK(near(m.bounds.mins.z, 5) && near(m.bounds.maxs.z, 49) && near(m.bounds.maxs.x, 132));
    // First cell: (0,0) (1,0) (1,1) and (0,0) (1,1) (0,1), counter-clockwise from +Z.
    CHECK(m.mesh.idx[0] == 0 && m.mesh.idx[1] == 1 && m.mesh.idx[2] == 6 && m.mesh.idx[3] == 0 && m.mesh.idx[4] == 6 &&
          m.mesh.idx[5] == 5);
    for (size_t k = 0; k + 2 < m.mesh.idx.size(); k += 3) {
        const Vec3& a = m.mesh.pos[m.mesh.idx[k]];
        const Vec3 n = cross(m.mesh.pos[m.mesh.idx[k + 1]] - a, m.mesh.pos[m.mesh.idx[k + 2]] - a);
        CHECK(n.z > 0);
    }
    // The plane z = x / 16 + 10 y / 16 + c: every normal is (-1/16, -10/16, 1) normalised.
    const Vec3 want = normalize(Vec3{-1.0 / 16, -10.0 / 16, 1});
    CHECK(near(m.mesh.normal[0].x, want.x, 1e-12) && near(m.mesh.normal[12].y, want.y, 1e-12) && near(m.mesh.normal[24].z, want.z, 1e-12));
    // uv: layer 0 planar (s = x / 128 + 0.25, t = -y / 128), stored as (s, 1 - t).
    CHECK(near(m.mesh.uv[0][0], 68.0 / 128 + 0.25) && near(m.mesh.uv[0][1], 1 + 168.0 / 128));
    // albedo: the surface square mapped to [0, 1], v up.
    CHECK(near(m.albedoUv[0][0], 0) && near(m.albedoUv[0][1], 1) && near(m.albedoUv[24][0], 1) && near(m.albedoUv[24][1], 0));
    CHECK(near(m.albedoUv[7][0], 0.5) && near(m.albedoUv[7][1], 0.75));

    // Too small a grid and a bad index fail.
    StockTerrainHeightGrid small;
    small.width = small.height = 4;
    small.data.assign(16, 0);
    CHECK(!buildStockTerrainMesh(t, 0, small, nullptr, {}, m, &err));
    CHECK(!buildStockTerrainMesh(t, 5, g, nullptr, {}, m, &err));
}

TEST(stock_super_terrain_mesh_stride_and_cutout) {
    StockTerrain t;
    CHECK(parseStockTerrain(testJson(), t));
    if (t.surfaces.size() != 2) return;
    const StockTerrainHeightGrid g = testHeights();
    StockTerrainMesh m;
    StockTerrainBuildOptions o;
    o.stride = 2;
    CHECK(buildStockTerrainMesh(t, 0, g, nullptr, o, m));
    CHECK(m.mesh.pos.size() == 9 && m.mesh.idx.size() == 4 * 6 && m.cellsTotal == 4);
    CHECK(m.mesh.pos[4] == Vec3(100, 200, 5 + 2 + 20));  // texel (2, 2)
    // Stride 3 on 4 cells: texels 0, 3, 4 (the last column is always included).
    o.stride = 3;
    CHECK(buildStockTerrainMesh(t, 0, g, nullptr, o, m));
    CHECK(m.mesh.pos.size() == 9 && m.mesh.idx.size() == 24);
    CHECK(m.mesh.pos[1] == Vec3(68 + 48, 168, 5 + 3) && m.mesh.pos[2] == Vec3(132, 168, 5 + 4));
    CHECK(stockTerrainSamples(4, 3) == 3 && stockTerrainSamples(2048, 8) == 257 && stockTerrainSamples(2048, 5) == 411);
    // Stride larger than the surface: one cell.
    o.stride = 64;
    CHECK(buildStockTerrainMesh(t, 0, g, nullptr, o, m));
    CHECK(m.mesh.pos.size() == 4 && m.mesh.idx.size() == 6);

    // Cut-outs at stride 1: the corner cell (0, 0) and the inner cell (2, 1) are cleared.
    StockTerrainCutout c = allPresent();
    clearCell(c, 0, 0);
    clearCell(c, 2, 1);
    o.stride = 1;
    CHECK(buildStockTerrainMesh(t, 0, g, &c, o, m));
    CHECK(m.cellsKept == 14 && m.mesh.idx.size() == 14 * 6);
    CHECK(m.mesh.pos.size() == 24);  // texel (0, 0) is used by no kept cell
    CHECK(m.mesh.pos[0] == Vec3(84, 168, 6));
    o.applyCutout = false;
    CHECK(buildStockTerrainMesh(t, 0, g, &c, o, m));
    CHECK(m.cellsKept == 16 && m.mesh.pos.size() == 25);
    o.applyCutout = true;
    // Stride 2: a coarse cell is kept when at least half of its 4 fine cells are present.
    clearCell(c, 1, 0);  // coarse (0, 0) now has 2 of 4 -> kept
    o.stride = 2;
    CHECK(buildStockTerrainMesh(t, 0, g, &c, o, m));
    CHECK(m.cellsKept == 4);
    clearCell(c, 1, 1);  // 1 of 4 -> dropped
    CHECK(buildStockTerrainMesh(t, 0, g, &c, o, m));
    CHECK(m.cellsKept == 3 && m.mesh.pos.size() == 8);
    // A fine mesh that also keeps what the coarse mesh keeps (cutoutStride): the cleared fine cells (2, 1) and (3, 3)
    // come back, as their coarse cells are kept; cells (0..1, 0..1) follow the fine map (coarse (0, 0) is dropped).
    clearCell(c, 3, 3);
    o.stride = 1;
    CHECK(buildStockTerrainMesh(t, 0, g, &c, o, m));
    CHECK(m.cellsKept == 11);
    o.cutoutStride = 2;
    CHECK(buildStockTerrainMesh(t, 0, g, &c, o, m));
    CHECK(m.cellsKept == 13);  // 16 - (0,0), (1,0), (1,1)
    o.cutoutStride = 0;
}

TEST(stock_super_terrain_meshes_and_budget) {
    StockTerrain t;
    CHECK(parseStockTerrain(testJson(), t));
    if (t.surfaces.size() != 2) return;
    // Surface 1: 3 x 3 (2 cells), heights 0 .. 65535 -> z 0 .. 100.
    StockTerrainHeightGrid g1;
    g1.width = g1.height = 4;
    g1.data.assign(16, 65535);
    std::vector<StockTerrainHeightGrid> heights{testHeights(), g1};
    std::vector<StockTerrainCutout> cutouts(1, allPresent());
    clearCell(cutouts[0], 3, 3);
    StockTerrainMeshes all;
    std::string err;
    CHECK(buildStockTerrainMeshes(t, heights, cutouts, {}, all, &err));
    CHECK(all.meshes.size() == 2 && all.triangles == 30 + 8 && all.vertices == 24 + 9);
    CHECK(near(all.bounds.maxs.z, 100) && near(all.bounds.mins.x, -32) && near(all.bounds.maxs.x, 132));
    CHECK(all.meshes[1].mesh.material.empty() && all.meshes[1].mesh.pos[0] == Vec3(-32, -32, 100));
    CHECK(all.warnings.empty());
    // A missing height grid is skipped with a warning; a missing cutout too.
    std::vector<StockTerrainHeightGrid> only1{StockTerrainHeightGrid{}, g1};
    CHECK(buildStockTerrainMeshes(t, only1, {}, {}, all, &err));
    CHECK(all.meshes.size() == 1 && all.meshes[0].surface == 1 && all.warnings.size() == 1);
    CHECK(!buildStockTerrainMeshes(t, {}, {}, {}, all, &err));

    // Budget: stride 1 = 2 * (16 + 4) = 40 triangles (no cut-outs), stride 2 = 2 * (4 + 1) = 10, stride 4 = 2 * (1 + 1).
    CHECK(stockTerrainTriangleCount(t, 1) == 40 && stockTerrainTriangleCount(t, 2) == 10 && stockTerrainTriangleCount(t, 4) == 4);
    CHECK(stockTerrainTriangleCount(t, 1, &cutouts) == 38);
    CHECK(stockTerrainStrideForBudget(t, 1000) == 1 && stockTerrainStrideForBudget(t, 40) == 1);
    CHECK(stockTerrainStrideForBudget(t, 39) == 2 && stockTerrainStrideForBudget(t, 10) == 2 && stockTerrainStrideForBudget(t, 9) == 4);
    CHECK(stockTerrainStrideForBudget(t, 38, &cutouts) == 1 && stockTerrainStrideForBudget(t, 37, &cutouts) == 2);
    CHECK(stockTerrainStrideForBudget(t, 1, nullptr, 8) == 8);
}
