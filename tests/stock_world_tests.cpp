// Stock world reader (src/core/stock_world.h) on a synthetic ACTS export in the layout of docs/research/30:
// gfx_map surfaces (tris.posOffset / vertexCount / triCount / baseIndex, material.name, transientZone) and
// gfx_map_trzone draw buffers (drawVerts.posData and indices as hex). Positions start at byte 4, as on mp_shipment.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "geometry.h"
#include "stock_world.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

std::string hexOf(const std::vector<uint8_t>& b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (uint8_t v : b) {
        s += d[v >> 4];
        s += d[v & 15];
    }
    return s;
}

void putF(std::vector<uint8_t>& b, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    for (int i = 0; i < 4; ++i) b.push_back(uint8_t(u >> (8 * i)));
}

void putU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}

// Transient zone 0: 4 bytes of padding, then 6 float3 vertices.
//   v0 (0,0,0)  v1 (100,0,0)  v2 (0,100,0)  v3 (100,100,0)  v4 (0,0,50)  v5 (100,0,50)
// Index stream: surface A uses (0,2,1)(1,3,2) on v0..v3 (ACTS clockwise: normal -z); surface B uses (0,1,2) on v1..v3
// starting at byte 16. Optional surface C points at an unexported transient zone; optional surface D reads
// the wall triangle with a vertexCount of 2 (index 2 is out of range).
std::string gfxJson(bool withC, bool withD) {
    std::string s = R"({"format":"mw19-asset-json-v1","asset":{"name":"maps/mp/test.d3dbsp","fields":{"surfaces":{"surfaces":{"values":[)";
    s += R"({"tris":{"posOffset":4,"vertexCount":4,"triCount":2,"baseIndex":0},"material":{"name":"tw/floor"},"transientZone":0})";
    s += R"(,{"tris":{"posOffset":16,"vertexCount":3,"triCount":1,"baseIndex":6},"material":{"name":"tw/wall"},"transientZone":0})";
    if (withC) s += R"(,{"tris":{"posOffset":4,"vertexCount":4,"triCount":1,"baseIndex":0},"material":{"name":",tw/ghost"},"transientZone":5})";
    // Index 2 of the wall triangle is outside a vertexCount of 2: that triangle is skipped with a warning.
    if (withD) s += R"(,{"tris":{"posOffset":4,"vertexCount":2,"triCount":1,"baseIndex":6},"material":{"name":"tw/floor"},"transientZone":0})";
    s += R"(]}}}}})";
    return s;
}

std::string trzJson(const std::vector<uint8_t>& pos, const std::vector<uint16_t>& idx, int zone = 0,
                    const std::vector<uint8_t>& aux = {}) {
    std::vector<uint8_t> ib;
    for (uint16_t v : idx) putU16(ib, v);
    std::string auxPart;  // drawVerts.auxData, only when the export has one
    if (!aux.empty())
        auxPart = R"("auxData":{"stride":1,"count":)" + std::to_string(aux.size()) +
                  R"(,"encoding":"hex-little-endian","bytes":")" + hexOf(aux) + R"("},)";
    return R"({"format":"mw19-asset-json-v1","asset":{"fields":{"name":"x","transientZoneIndex":)" + std::to_string(zone) +
           R"(,"drawVerts":{)" + auxPart + R"("posDataSize":)" + std::to_string(pos.size()) + R"(,"posData":{"stride":1,"count":)" +
           std::to_string(pos.size()) + R"(,"encoding":"hex-little-endian","bytes":")" + hexOf(pos) +
           R"("},"indexCount":)" + std::to_string(idx.size()) + R"(,"indices":{"stride":2,"count":)" + std::to_string(idx.size()) +
           R"(,"encoding":"hex-little-endian","bytes":")" + hexOf(ib) + R"("}}}}})";
}

struct Sample {
    std::vector<uint8_t> pos;
    std::vector<uint16_t> idx;
};

Sample sample() {
    Sample s;
    s.pos = {0, 0, 0, 0};  // padding before the first vertex
    const float v[6][3] = {{0, 0, 0}, {100, 0, 0}, {0, 100, 0}, {100, 100, 0}, {0, 0, 50}, {100, 0, 50}};
    for (const auto& p : v)
        for (float c : p) putF(s.pos, c);
    s.idx = {0, 2, 1, 1, 3, 2, 0, 1, 2};  // A: 2 triangles at baseIndex 0; B: 1 triangle at baseIndex 6
    return s;
}

// gfx_map with surfData (docs/research/30): the floor is surfData 0 (block at posOffset 4, one layer); the wall is
// surfData 1 (block at posOffset 16, vertices v1..v3, two layers). badIndex points the wall at a missing record.
std::string gfxJsonUv(bool badIndex) {
    std::string s = R"({"format":"mw19-asset-json-v1","asset":{"name":"maps/mp/test.d3dbsp","fields":{"surfaces":{"surfaces":{"values":[)";
    s += R"({"tris":{"posOffset":4,"vertexCount":4,"triCount":2,"baseIndex":0},"material":{"name":"tw/floor"},"transientZone":0,"surfDataIndex":0})";
    s += R"(,{"tris":{"posOffset":16,"vertexCount":3,"triCount":1,"baseIndex":6},"material":{"name":"tw/wall"},"transientZone":0,"surfDataIndex":)" +
         std::string(badIndex ? "7" : "1") + R"(})";
    s += R"(]},"surfData":{"values":[)";
    s += R"({"transientZone":0,"layerCount":1,"xyzOffset":4,"tangentFrameOffset":4,"lmapCoordOffset":20,"colorOffset":0,"texCoordOffset":52})";
    s += R"(,{"transientZone":0,"layerCount":2,"xyzOffset":16,"tangentFrameOffset":84,"lmapCoordOffset":96,"colorOffset":0,"texCoordOffset":120,"normalTransformOffset":[168,0,0,0,0,0,0]})";
    s += R"(]}}}}})";
    return s;
}

// The aux stream for gfxJsonUv: 4 bytes of padding, then per surface the tangent (4 bytes/vertex), lmap (8/vertex),
// texCoord (8 bytes per vertex per layer, layers interleaved) and normalTransform (4/vertex per layer above 0) regions.
// Filler bytes mark the regions that must not be read as texture coordinates. 180 bytes.
std::vector<uint8_t> auxSample() {
    std::vector<uint8_t> a(4, 0);
    a.insert(a.end(), 16, 0xAB);  // block 0 tangentFrame, 4 vertices
    a.insert(a.end(), 32, 0xCD);  // block 0 lmapCoord, 4 vertices
    const float floorUv[4][2] = {{0, 0}, {0.5f, 0.25f}, {1, 0.5f}, {1.5f, 0.75f}};
    for (const auto& p : floorUv) {  // block 0 texCoord at 52, one layer
        putF(a, p[0]);
        putF(a, p[1]);
    }
    a.insert(a.end(), 12, 0xAB);  // block 1 tangentFrame at 84, 3 vertices
    a.insert(a.end(), 24, 0xCD);  // block 1 lmapCoord at 96, 3 vertices
    for (int j = 0; j < 3; ++j) {  // block 1 texCoord at 120: vertex j = layer 0, layer 1 (16 bytes per vertex)
        putF(a, float(2 + j));
        putF(a, float(3 + j));
        putF(a, float(50 + j));
        putF(a, float(60 + j));
    }
    a.insert(a.end(), 12, 0xEE);  // block 1 normalTransform for layer 1 at 168
    return a;
}

// The uv of the first vertex of mesh m at position p.
bool uvAt(const ImportedMesh& m, Vec3 p, std::array<double, 2>& uv) {
    for (size_t i = 0; i < m.pos.size(); ++i)
        if (m.pos[i] == p) {
            uv = m.uv[i];
            return true;
        }
    return false;
}

bool warnsWith(const StockWorld& w, const std::string& text) {
    for (const std::string& warn : w.warnings)
        if (warn.find(text) != std::string::npos) return true;
    return false;
}

}  // namespace

TEST(stock_world_synthetic_parse) {
    const Sample s = sample();
    StockWorld w;
    std::string err;
    CHECK(parseStockWorld(gfxJson(false, false), {trzJson(s.pos, s.idx)}, w, &err));
    CHECK(err.empty());
    CHECK(w.meshes.size() == 2);
    CHECK(w.warnings.empty());
    if (w.meshes.size() != 2) return;

    // Material order follows first use, and each mesh keeps its name.
    CHECK(w.meshes[0].material == "tw/floor");
    CHECK(w.meshes[1].material == "tw/wall");

    // Floor: v0..v3 (4 unique vertices), 2 triangles. Wall: v1..v3 (its own vertex list), 1 triangle.
    CHECK(w.meshes[0].pos.size() == 4);
    CHECK(w.meshes[0].idx.size() == 6);
    CHECK(w.meshes[1].pos.size() == 3);
    CHECK(w.meshes[1].idx.size() == 3);
    CHECK(w.meshes[0].normal.size() == w.meshes[0].pos.size());
    CHECK(w.meshes[0].uv.size() == w.meshes[0].pos.size());

    // Positions are Z-up inches, unchanged. Vertices are numbered in order of first use, so look them up by value.
    const auto hasPos = [](const ImportedMesh& m, Vec3 p) {
        for (const Vec3& q : m.pos)
            if (q == p) return true;
        return false;
    };
    CHECK(hasPos(w.meshes[0], Vec3{100, 0, 0}));
    CHECK(hasPos(w.meshes[0], Vec3{0, 100, 0}));
    CHECK(!hasPos(w.meshes[0], Vec3{0, 0, 50}));  // v4 is not used by the floor

    // The swap to counter-clockwise: the floor's first triangle, raw (0,2,1), has its geometric normal up (+z).
    const ImportedMesh& floor = w.meshes[0];
    const Vec3 a = floor.pos[floor.idx[0]], b = floor.pos[floor.idx[1]], c = floor.pos[floor.idx[2]];
    const Vec3 n = cross(b - a, c - a);
    CHECK(n.z > 0);
    // Vertex normals point up on the flat floor.
    CHECK(floor.normal[0].z > 0.99);

    // Wall: raw (0,1,2) on v1,v2,v3 has its normal down; after the swap it is up.
    const ImportedMesh& wall = w.meshes[1];
    const Vec3 wa = wall.pos[wall.idx[0]], wb = wall.pos[wall.idx[1]], wc = wall.pos[wall.idx[2]];
    CHECK(cross(wb - wa, wc - wa).z > 0);

    // Bounds cover the used vertices only (v4, v5 at z=50 are not used by any triangle).
    CHECK(w.bounds.valid());
    CHECK(w.bounds.mins.x == 0 && w.bounds.mins.y == 0 && w.bounds.mins.z == 0);
    CHECK(w.bounds.maxs.x == 100 && w.bounds.maxs.y == 100 && w.bounds.maxs.z == 0);
}

TEST(stock_world_skips_and_warnings) {
    const Sample s = sample();
    StockWorld w;
    std::string err;
    // Surface C points at transient zone 5 (not exported). Surface D has an index beyond vertexCount.
    CHECK(parseStockWorld(gfxJson(true, true), {trzJson(s.pos, s.idx)}, w, &err));
    CHECK(w.meshes.size() == 2);
    // The ghost surface's material has no mesh.
    for (const ImportedMesh& m : w.meshes) CHECK(m.material != "tw/ghost");
    bool sawZone = false, sawRange = false;
    for (const std::string& warn : w.warnings) {
        if (warn.find("transient zone 5") != std::string::npos) sawZone = true;
        if (warn.find("outside") != std::string::npos || warn.find("index") != std::string::npos) sawRange = true;
    }
    CHECK(sawZone);
    CHECK(sawRange);
}

TEST(stock_world_rejects_bad_input) {
    const Sample s = sample();
    StockWorld w;
    std::string err;
    CHECK(!parseStockWorld("{not json", {trzJson(s.pos, s.idx)}, w, &err));
    CHECK(!err.empty());
    err.clear();
    CHECK(!parseStockWorld(gfxJson(false, false), {R"({"asset":{"fields":{"transientZoneIndex":0}}})"}, w, &err));
    CHECK(!err.empty());
    err.clear();
    // Every surface points past the buffers: nothing to read.
    CHECK(!parseStockWorld(R"({"asset":{"fields":{"surfaces":{"surfaces":{"values":[{"tris":{"posOffset":4000,"vertexCount":4,"triCount":2,"baseIndex":0},"material":{"name":"m"},"transientZone":0}]}}}}})",
                           {trzJson(s.pos, s.idx)}, w, &err));
    CHECK(!err.empty());
}

TEST(stock_world_folder_layout) {
    namespace fs = std::filesystem;
    const Sample s = sample();
    const fs::path zone = fs::path(iw8r::test::tmpDir()) / "stock_world_zone" / "mw19replay" / "test";
    fs::remove_all(zone.parent_path());
    fs::create_directories(zone / "assets" / "gfx_map" / "maps" / "mp");
    fs::create_directories(zone / "assets" / "gfx_map_trzone");
    {
        std::ofstream(zone / "assets" / "gfx_map" / "maps" / "mp" / "test.d3dbsp.0.asset.json") << gfxJson(false, false);
        std::ofstream(zone / "assets" / "gfx_map_trzone" / "test.0.asset.json") << trzJson(s.pos, s.idx);
    }
    StockWorld w;
    std::string err;
    CHECK(readStockWorld(zone.string(), w, &err));
    CHECK(err.empty());
    CHECK(w.meshes.size() == 2);

    // A missing gfx_map folder fails with a message that names it.
    StockWorld none;
    CHECK(!readStockWorld((fs::path(iw8r::test::tmpDir()) / "stock_world_missing").string(), none, &err));
    CHECK(err.find("gfx_map") != std::string::npos);
}

TEST(stock_world_texcoords_from_surface_records) {
    // Each surface reads its own record: the floor one float2 per vertex, the wall layer 0 of two interleaved layers.
    const Sample s = sample();
    StockWorld w;
    std::string err;
    CHECK(parseStockWorld(gfxJsonUv(false), {trzJson(s.pos, s.idx, 0, auxSample())}, w, &err));
    CHECK(err.empty());
    CHECK(w.warnings.empty());
    CHECK(w.meshes.size() == 2);
    if (w.meshes.size() != 2) return;
    for (const ImportedMesh& m : w.meshes) CHECK(m.uv.size() == m.pos.size());

    // ImportedMesh uv has v = 0 at the bottom: the stored v is flipped (1 - v).
    std::array<double, 2> uv{};
    CHECK(uvAt(w.meshes[0], Vec3{100, 0, 0}, uv));
    CHECK(uv[0] == 0.5 && uv[1] == 0.75);
    CHECK(uvAt(w.meshes[0], Vec3{0, 100, 0}, uv));
    CHECK(uv[0] == 1.0 && uv[1] == 0.5);
    CHECK(uvAt(w.meshes[0], Vec3{100, 100, 0}, uv));
    CHECK(uv[0] == 1.5 && uv[1] == 0.25);

    // The wall's v1 is also a floor vertex, with its own record: the same position gets the wall's coordinates.
    // Layer 0 only: the layer 1 values (50, 60) are not read.
    CHECK(uvAt(w.meshes[1], Vec3{100, 0, 0}, uv));
    CHECK(uv[0] == 2.0 && uv[1] == -2.0);
    CHECK(uvAt(w.meshes[1], Vec3{100, 100, 0}, uv));
    CHECK(uv[0] == 4.0 && uv[1] == -4.0);
}

TEST(stock_world_texcoords_missing_records) {
    const Sample s = sample();
    const auto aux = auxSample();
    StockWorld w;
    std::string err;
    std::array<double, 2> uv{};

    // The wall points at a surfData record that does not exist: its uv stays zero, the floor is unaffected.
    CHECK(parseStockWorld(gfxJsonUv(true), {trzJson(s.pos, s.idx, 0, aux)}, w, &err));
    CHECK(w.meshes.size() == 2);
    if (w.meshes.size() != 2) return;
    CHECK(uvAt(w.meshes[1], Vec3{100, 0, 0}, uv));
    CHECK(uv[0] == 0.0 && uv[1] == 0.0);
    CHECK(uvAt(w.meshes[0], Vec3{100, 0, 0}, uv));
    CHECK(uv[0] == 0.5 && uv[1] == 0.75);
    CHECK(warnsWith(w, "surfData missing or out of range"));

    // The export has no auxData: the geometry reads as before, uv zero, with a warning.
    StockWorld n;
    CHECK(parseStockWorld(gfxJsonUv(false), {trzJson(s.pos, s.idx)}, n, &err));
    CHECK(n.meshes.size() == 2);
    if (n.meshes.size() != 2) return;
    CHECK(uvAt(n.meshes[0], Vec3{100, 0, 0}, uv));
    CHECK(uv[0] == 0.0 && uv[1] == 0.0);
    CHECK(warnsWith(n, "no auxData"));

    // An aux stream that ends before the wall's records: the wall's uv stays zero, the floor's (within 84 bytes) is read.
    StockWorld t;
    const std::vector<uint8_t> shortAux(aux.begin(), aux.begin() + 100);
    CHECK(parseStockWorld(gfxJsonUv(false), {trzJson(s.pos, s.idx, 0, shortAux)}, t, &err));
    CHECK(t.meshes.size() == 2);
    if (t.meshes.size() != 2) return;
    CHECK(uvAt(t.meshes[1], Vec3{100, 0, 0}, uv));
    CHECK(uv[0] == 0.0 && uv[1] == 0.0);
    CHECK(uvAt(t.meshes[0], Vec3{100, 0, 0}, uv));
    CHECK(uv[0] == 0.5 && uv[1] == 0.75);
    CHECK(warnsWith(t, "surfData missing or out of range"));
}

TEST(stock_world_texcoord_packing) {
    // PackedTexCoords: u = binary16 in the low half, v = binary16 in the high half (x64-zt Vec2UnpackTexCoords).
    double u = 0, v = 0;
    unpackStockTexCoords((uint32_t(0x3800) << 16) | 0x3C00, u, v);  // 1.0, 0.5
    CHECK(u == 1.0 && v == 0.5);
    unpackStockTexCoords((uint32_t(0x0000) << 16) | 0xC000, u, v);  // -2.0, 0
    CHECK(u == -2.0 && v == 0.0);
    unpackStockTexCoords((uint32_t(0x4900) << 16) | 0x0001, u, v);  // smallest subnormal, 10.0
    CHECK(u == std::ldexp(1.0, -24) && v == 10.0);
    unpackStockTexCoords((uint32_t(0x7C00) << 16) | 0xFC00, u, v);  // u = -inf (low half), v = +inf (high half)
    CHECK(std::isinf(u) && u < 0 && std::isinf(v) && v > 0);
    unpackStockTexCoords(0, u, v);
    CHECK(u == 0.0 && v == 0.0);
}

TEST(stock_world_skips_streamed_trzone_stubs) {
    // mp_donetsk: the main zone holds a size-0 stub for each transient zone streamed from its own _tr fastfile.
    const Sample s = sample();
    StockWorld w;
    std::string err;
    const std::string stub =
        R"({"asset":{"fields":{"transientZoneIndex":587,"drawVerts":{"posDataSize":0,"auxDataSize":0,"posData":null,"auxData":null,"indexCount":0,"indices":null}}}})";
    CHECK(parseStockWorld(gfxJson(false, false), {trzJson(s.pos, s.idx), stub}, w, &err));
    CHECK(err.empty());
    CHECK(w.meshes.size() == 2);
}

namespace {

// gfxJsonUv plus lightmapIndex: the floor (lightmapIndex 0) reads lightmap UVs from surfData 0 (lmapCoordOffset 20),
// the wall has lightmapIndex 510 (none). A third surface draws the floor's block again through surfData 2, which has
// the same texCoordOffset (52) but its own lightmap UVs at 180: its vertices must not merge with the floor's.
std::string gfxJsonLm() {
    std::string s = R"({"format":"mw19-asset-json-v1","asset":{"name":"maps/mp/test.d3dbsp","fields":{"surfaces":{"surfaces":{"values":[)";
    s += R"({"tris":{"posOffset":4,"vertexCount":4,"triCount":2,"baseIndex":0},"material":{"name":"tw/floor"},"transientZone":0,"surfDataIndex":0,"lightmapIndex":0})";
    s += R"(,{"tris":{"posOffset":16,"vertexCount":3,"triCount":1,"baseIndex":6},"material":{"name":"tw/wall"},"transientZone":0,"surfDataIndex":1,"lightmapIndex":510})";
    s += R"(,{"tris":{"posOffset":4,"vertexCount":4,"triCount":2,"baseIndex":0},"material":{"name":"tw/floor"},"transientZone":0,"surfDataIndex":2,"lightmapIndex":0})";
    s += R"(]},"surfData":{"values":[)";
    s += R"({"transientZone":0,"layerCount":1,"xyzOffset":4,"tangentFrameOffset":4,"lmapCoordOffset":20,"colorOffset":0,"texCoordOffset":52})";
    s += R"(,{"transientZone":0,"layerCount":2,"xyzOffset":16,"tangentFrameOffset":84,"lmapCoordOffset":96,"colorOffset":0,"texCoordOffset":120,"normalTransformOffset":[168,0,0,0,0,0,0]})";
    s += R"(,{"transientZone":0,"layerCount":1,"xyzOffset":4,"tangentFrameOffset":4,"lmapCoordOffset":180,"colorOffset":0,"texCoordOffset":52})";
    s += R"(]}}}}})";
    return s;
}

// auxSample with real lightmap UVs: block 0 at 20 (vertex j: (0.1 + 0.2 j, 0.2 + 0.2 j)) and surfData 2's at 180
// (vertex j: (0.9 - 0.1 j, 0.05 j)).
std::vector<uint8_t> auxSampleLm() {
    std::vector<uint8_t> a = auxSample();
    std::vector<uint8_t> lm;
    for (int j = 0; j < 4; ++j) {
        putF(lm, 0.1f + 0.2f * float(j));
        putF(lm, 0.2f + 0.2f * float(j));
    }
    for (size_t i = 0; i < lm.size(); ++i) a[20 + i] = lm[i];
    for (int j = 0; j < 4; ++j) {
        putF(a, 0.9f - 0.1f * float(j));
        putF(a, 0.05f * float(j));
    }
    return a;
}

}  // namespace

TEST(stock_world_lightmap_uvs) {
    const Sample s = sample();
    StockWorld w;
    std::string err;
    CHECK(parseStockWorld(gfxJsonLm(), {trzJson(s.pos, s.idx, 0, auxSampleLm())}, w, &err));
    CHECK(err.empty());
    CHECK(w.meshes.size() == 2);
    CHECK(w.lightmapUv.size() == w.meshes.size());
    if (w.meshes.size() != 2 || w.lightmapUv.size() != 2) return;
    for (size_t m = 0; m < 2; ++m) CHECK(w.lightmapUv[m].size() == w.meshes[m].pos.size());
    CHECK(w.lightmapSurfaces == 2);

    // The floor mesh: 4 vertices from surfData 0 and 4 more from surfData 2 (same position and texture records,
    // other lightmap records), so no lightmap UV is lost to the vertex sharing.
    const ImportedMesh& floor = w.meshes[0];
    CHECK(floor.pos.size() == 8);
    CHECK(floor.idx.size() == 12);
    const auto lmAt = [&](const Vec3& p, float u, float v) {
        for (size_t k = 0; k < floor.pos.size(); ++k)
            if (floor.pos[k] == p && std::fabs(w.lightmapUv[0][k][0] - u) < 1e-6f && std::fabs(w.lightmapUv[0][k][1] - v) < 1e-6f)
                return true;
        return false;
    };
    // Stored as is (no V flip): vertex 1 (100,0,0) of block 0 is (0.3, 0.4); of surfData 2 it is (0.8, 0.05).
    CHECK(lmAt(Vec3{0, 0, 0}, 0.1f, 0.2f));
    CHECK(lmAt(Vec3{100, 0, 0}, 0.3f, 0.4f));
    CHECK(lmAt(Vec3{100, 100, 0}, 0.7f, 0.8f));
    CHECK(lmAt(Vec3{100, 0, 0}, 0.8f, 0.05f));
    CHECK(lmAt(Vec3{100, 100, 0}, 0.6f, 0.15f));
    // The texture coordinates are unchanged by the extra key.
    std::array<double, 2> uv{};
    CHECK(uvAt(floor, Vec3{100, 0, 0}, uv));
    CHECK(uv[0] == 0.5 && uv[1] == 0.75);

    // The wall has lightmapIndex 510: no lightmap UV.
    for (const auto& l : w.lightmapUv[1]) CHECK(l[0] == -1.0f && l[1] == -1.0f);
    // The lightmap index per vertex, parallel to lightmapUv: 0 on the floor, -1 on the wall.
    CHECK(w.lightmapIndex.size() == 2 && w.lightmapIndex[0].size() == floor.pos.size() &&
          w.lightmapIndex[1].size() == w.meshes[1].pos.size());
    if (w.lightmapIndex.size() == 2) {
        for (int li : w.lightmapIndex[0]) CHECK(li == 0);
        for (int li : w.lightmapIndex[1]) CHECK(li == -1);
    }

    // No aux stream: every vertex has (-1, -1), and the vertex count is the old one.
    StockWorld n;
    CHECK(parseStockWorld(gfxJsonUv(false), {trzJson(s.pos, s.idx)}, n, &err));
    CHECK(n.lightmapUv.size() == n.meshes.size());
    CHECK(n.lightmapIndex.size() == n.meshes.size());
    CHECK(n.lightmapSurfaces == 0);
    for (size_t m = 0; m < n.lightmapUv.size() && m < n.meshes.size(); ++m) {
        CHECK(n.lightmapUv[m].size() == n.meshes[m].pos.size());
        for (const auto& l : n.lightmapUv[m]) CHECK(l[0] == -1.0f && l[1] == -1.0f);
    }
    // gfx_map without surfData records at all (the oldest export shape): lists still parallel.
    StockWorld o;
    CHECK(parseStockWorld(gfxJson(false, true), {trzJson(s.pos, s.idx)}, o, &err));
    CHECK(o.lightmapUv.size() == o.meshes.size());
    for (size_t m = 0; m < o.lightmapUv.size() && m < o.meshes.size(); ++m) CHECK(o.lightmapUv[m].size() == o.meshes[m].pos.size());
}

// Warzone: lightmap i belongs to transient zone lightmapTransientIndex[i] (research 94 "Warzone (type 0)"). Two
// transient zones (0 and 30, like a streamed tile) whose streams use the SAME byte offsets for different vertices,
// with the same material: nothing may merge across the zones, and each vertex keeps its surface's lightmap index.
// A third surface in zone 0 draws the floor block again with another lightmap index (3): its own vertices.
TEST(stock_world_lightmap_index_and_zones) {
    const Sample s = sample();
    // Zone 30: the same layout, every position moved by +1000 on x.
    Sample t = s;
    t.pos = {0, 0, 0, 0};
    const float v[6][3] = {{0, 0, 0}, {100, 0, 0}, {0, 100, 0}, {100, 100, 0}, {0, 0, 50}, {100, 0, 50}};
    for (const auto& p : v) {
        putF(t.pos, p[0] + 1000.0f);
        putF(t.pos, p[1]);
        putF(t.pos, p[2]);
    }
    std::string g = R"({"format":"mw19-asset-json-v1","asset":{"name":"maps/mp/test.d3dbsp","fields":{"surfaces":{"surfaces":{"values":[)";
    g += R"({"tris":{"posOffset":4,"vertexCount":4,"triCount":2,"baseIndex":0},"material":{"name":"tw/floor"},"transientZone":0,"surfDataIndex":0,"lightmapIndex":0})";
    g += R"(,{"tris":{"posOffset":4,"vertexCount":4,"triCount":2,"baseIndex":0},"material":{"name":"tw/floor"},"transientZone":30,"surfDataIndex":0,"lightmapIndex":17})";
    g += R"(,{"tris":{"posOffset":4,"vertexCount":4,"triCount":2,"baseIndex":0},"material":{"name":"tw/floor"},"transientZone":0,"surfDataIndex":0,"lightmapIndex":3})";
    g += R"(]},"surfData":{"values":[)";
    g += R"({"transientZone":0,"layerCount":1,"xyzOffset":4,"tangentFrameOffset":4,"lmapCoordOffset":20,"colorOffset":0,"texCoordOffset":52})";
    g += R"(]}}}}})";
    const auto aux = auxSampleLm();
    StockWorld w;
    std::string err;
    CHECK(parseStockWorld(g, {trzJson(s.pos, s.idx, 0, aux), trzJson(t.pos, t.idx, 30, aux)}, w, &err));
    CHECK(err.empty());
    CHECK(w.meshes.size() == 1);
    if (w.meshes.size() != 1 || w.lightmapIndex.size() != 1 || w.lightmapUv.size() != 1) return;
    const ImportedMesh& m = w.meshes[0];
    CHECK(m.pos.size() == 12);  // 4 per surface: zone 0 / lightmap 0, zone 30 / lightmap 17, zone 0 / lightmap 3
    CHECK(m.idx.size() == 18);
    CHECK(w.lightmapIndex[0].size() == m.pos.size() && w.lightmapUv[0].size() == m.pos.size());
    CHECK(w.lightmapSurfaces == 3);
    if (w.lightmapIndex[0].size() != m.pos.size()) return;
    int perIndex[18] = {};
    bool placed = true;
    for (size_t k = 0; k < m.pos.size(); ++k) {
        const int li = w.lightmapIndex[0][k];
        if (li < 0 || li > 17) {
            placed = false;
            continue;
        }
        ++perIndex[li];
        // Zone 30's vertices (lightmap 17) are the moved ones; zone 0's are not.
        placed &= (li == 17) == (m.pos[k].x >= 1000.0);
    }
    CHECK(placed);
    CHECK(perIndex[0] == 4 && perIndex[17] == 4 && perIndex[3] == 4);
    // Every triangle uses one lightmap (its corners agree).
    bool oneLightmap = true;
    for (size_t i = 0; i + 2 < m.idx.size(); i += 3)
        oneLightmap &= w.lightmapIndex[0][m.idx[i]] == w.lightmapIndex[0][m.idx[i + 1]] &&
                       w.lightmapIndex[0][m.idx[i]] == w.lightmapIndex[0][m.idx[i + 2]];
    CHECK(oneLightmap);
    // The same UVs in both zones (the same aux bytes): vertex (1100, 0, 0) of zone 30 is (0.3, 0.4).
    bool found = false;
    for (size_t k = 0; k < m.pos.size(); ++k)
        if (m.pos[k] == Vec3{1100, 0, 0} && std::fabs(w.lightmapUv[0][k][0] - 0.3f) < 1e-6f && std::fabs(w.lightmapUv[0][k][1] - 0.4f) < 1e-6f)
            found = true;
    CHECK(found);
}

// ACTS writes a large surfData array as hex "bytes" (mp_donetsk): the 88-byte GfxWorldSurfData records give the same
// texture and lightmap coordinates as the decoded "values" form.
TEST(stock_world_surfdata_hex_bytes) {
    const Sample s = sample();
    const std::string valuesForm = gfxJsonLm();
    // The three records of gfxJsonLm: (tz, layers, xyz, tangent, lmap, colour, tex).
    const uint32_t rec[3][7] = {{0, 1, 4, 4, 20, 0, 52}, {0, 2, 16, 84, 96, 0, 120}, {0, 1, 4, 4, 180, 0, 52}};
    std::vector<uint8_t> raw;
    for (const auto& r : rec) {
        std::vector<uint8_t> one(88, 0);
        for (int k = 0; k < 7; ++k)
            for (int b = 0; b < 4; ++b) one[size_t(4 * k + b)] = uint8_t(r[k] >> (8 * b));
        one[0x1C] = 168;  // normalTransformOffset[0] (set on every record; read only for layers above 0: record 1)
        raw.insert(raw.end(), one.begin(), one.end());
    }
    const size_t a = valuesForm.find(R"("surfData":{"values":[)");
    const size_t b = valuesForm.rfind("]}}}}}");  // the end of the values array (records hold "]}" too)
    CHECK(a != std::string::npos && b != std::string::npos);
    if (a == std::string::npos || b == std::string::npos) return;
    const std::string hexForm = valuesForm.substr(0, a) + R"("surfData":{"type":"GfxWorldSurfData","stride":88,"count":3,"encoding":"hex-little-endian","bytes":")" +
                                hexOf(raw) + R"("})" + valuesForm.substr(b + 2);
    StockWorld v, h;
    std::string err;
    CHECK(parseStockWorld(valuesForm, {trzJson(s.pos, s.idx, 0, auxSampleLm())}, v, &err));
    CHECK(parseStockWorld(hexForm, {trzJson(s.pos, s.idx, 0, auxSampleLm())}, h, &err));
    CHECK(h.lightmapSurfaces == 2 && h.lightmapSurfaces == v.lightmapSurfaces);
    CHECK(h.meshes.size() == v.meshes.size());
    for (size_t m = 0; m < h.meshes.size() && m < v.meshes.size(); ++m) {
        CHECK(h.meshes[m].pos == v.meshes[m].pos);
        CHECK(h.meshes[m].uv == v.meshes[m].uv);
        CHECK(h.lightmapUv[m] == v.lightmapUv[m]);
        CHECK(h.lightmapIndex[m] == v.lightmapIndex[m]);
        // The layer records too: normalTransformOffset[0] (0x1C) of record 1 gives the wall's layer-1 transforms.
        CHECK(h.layerCount[m] == v.layerCount[m]);
        CHECK(h.layerUv[m] == v.layerUv[m]);
        CHECK(h.vertexColor[m] == v.vertexColor[m]);
        CHECK(h.layerNormalTransform[m] == v.layerNormalTransform[m]);
    }
    CHECK(h.layerCount.size() == 2 && h.layerCount[1] == 2 && h.layerNormalTransform[1].size() == h.meshes[1].pos.size());
    if (h.layerNormalTransform.size() == 2)
        for (uint32_t t : h.layerNormalTransform[1]) CHECK(t == 0xEEEEEEEEu);
    CHECK(h.warnings == v.warnings);
}

namespace {

// Layered surfaces (docs/research/97): the floor (surfData 0, one layer, no colour), the wall (surfData 1, two layers,
// a colour record and a layer-1 normal transform), the wall again through surfData 3 (same texture records, its own
// colour record: its vertices must not merge with the first wall's), and a five-layer surface over the floor block
// (surfData 2: 8 colour bytes per vertex, transforms for layers 1 and 3 only, no lightmap UV).
std::string gfxJsonLayers() {
    std::string s = R"({"format":"mw19-asset-json-v1","asset":{"name":"maps/mp/test.d3dbsp","fields":{"surfaces":{"surfaces":{"values":[)";
    s += R"({"tris":{"posOffset":4,"vertexCount":4,"triCount":2,"baseIndex":0},"material":{"name":"tw/floor"},"transientZone":0,"surfDataIndex":0})";
    s += R"(,{"tris":{"posOffset":16,"vertexCount":3,"triCount":1,"baseIndex":6},"material":{"name":",twc4/wall"},"transientZone":0,"surfDataIndex":1})";
    s += R"(,{"tris":{"posOffset":16,"vertexCount":3,"triCount":1,"baseIndex":6},"material":{"name":",twc4/wall"},"transientZone":0,"surfDataIndex":3})";
    s += R"(,{"tris":{"posOffset":4,"vertexCount":4,"triCount":2,"baseIndex":0},"material":{"name":",twc4/five"},"transientZone":0,"surfDataIndex":2})";
    s += R"(]},"surfData":{"values":[)";
    s += R"({"transientZone":0,"layerCount":1,"xyzOffset":4,"tangentFrameOffset":4,"lmapCoordOffset":20,"colorOffset":0,"texCoordOffset":52})";
    s += R"(,{"transientZone":0,"layerCount":2,"xyzOffset":16,"tangentFrameOffset":84,"lmapCoordOffset":96,"colorOffset":120,"texCoordOffset":132,"normalTransformOffset":[180,0,0,0,0,0,0]})";
    s += R"(,{"transientZone":0,"layerCount":5,"xyzOffset":4,"tangentFrameOffset":4,"lmapCoordOffset":0,"colorOffset":192,"texCoordOffset":224,"normalTransformOffset":[384,0,400,0,0,0,0]})";
    s += R"(,{"transientZone":0,"layerCount":2,"xyzOffset":16,"tangentFrameOffset":84,"lmapCoordOffset":96,"colorOffset":416,"texCoordOffset":132,"normalTransformOffset":[180,0,0,0,0,0,0]})";
    s += R"(]}}}}})";
    return s;
}

void putU32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i)));
}

// The aux stream for gfxJsonLayers, 428 bytes:
//   0 pad | 4 floor tangent | 20 floor lmap | 52 floor tex (1 layer) | 84 wall tangent | 96 wall lmap
//   120 wall colour (vertex j: R 0xFF, G 0x40 j, B 0x11, A 0x22) | 132 wall tex (vertex j: (2 + j, 3 + j), (50 + j, 60 + j))
//   180 wall layer-1 transform (vertex j: 0xFF808000 | j) | 192 five colour (8 bytes/vertex: FF, 10j+1 .. 10j+4, 0, 0, 0)
//   224 five tex (5 layers: layer l of vertex j = (10 l + j, 100 + 10 l + j)) | 384 five layer-1 transform
//   400 five layer-3 transform | 416 second wall colour (vertex j: 0x0A0B0C0D + j)
std::vector<uint8_t> auxSampleLayers() {
    std::vector<uint8_t> a(4, 0);
    a.insert(a.end(), 16, 0xAB);
    a.insert(a.end(), 32, 0xCD);
    const float floorUv[4][2] = {{0, 0}, {0.5f, 0.25f}, {1, 0.5f}, {1.5f, 0.75f}};
    for (const auto& p : floorUv) {
        putF(a, p[0]);
        putF(a, p[1]);
    }
    a.insert(a.end(), 12, 0xAB);                                                 // 84
    a.insert(a.end(), 24, 0xCD);                                                 // 96
    for (uint32_t j = 0; j < 3; ++j) putU32(a, 0x221100FFu | (0x40u * j) << 8);  // 120
    for (int j = 0; j < 3; ++j) {                                                // 132
        putF(a, float(2 + j));
        putF(a, float(3 + j));
        putF(a, float(50 + j));
        putF(a, float(60 + j));
    }
    for (uint32_t j = 0; j < 3; ++j) putU32(a, 0xFF808000u | j);  // 180
    for (uint32_t j = 0; j < 4; ++j) {                            // 192
        putU32(a, 0xFFu | (10 * j + 1) << 8 | (10 * j + 2) << 16 | (10 * j + 3) << 24);
        putU32(a, 10 * j + 4);
    }
    for (int j = 0; j < 4; ++j)  // 224
        for (int l = 0; l < 5; ++l) {
            putF(a, float(10 * l + j));
            putF(a, float(100 + 10 * l + j));
        }
    for (uint32_t j = 0; j < 4; ++j) putU32(a, 0x11111100u | j);  // 384
    for (uint32_t j = 0; j < 4; ++j) putU32(a, 0x33333300u | j);  // 400
    for (uint32_t j = 0; j < 3; ++j) putU32(a, 0x0A0B0C0Du + j);  // 416
    return a;
}

// The first vertex of mesh m at position p, or -1.
int vertexAt(const StockWorld& w, size_t m, Vec3 p) {
    const ImportedMesh& mesh = w.meshes[m];
    for (size_t k = 0; k < mesh.pos.size(); ++k)
        if (mesh.pos[k] == p) return int(k);
    return -1;
}

}  // namespace

TEST(stock_world_layers_and_vertex_colour) {
    const Sample s = sample();
    const auto aux = auxSampleLayers();
    CHECK(aux.size() == 428);
    StockWorld w;
    std::string err;
    CHECK(parseStockWorld(gfxJsonLayers(), {trzJson(s.pos, s.idx, 0, aux)}, w, &err));
    CHECK(err.empty());
    CHECK(w.warnings.empty());
    CHECK(w.meshes.size() == 3);
    if (w.meshes.size() != 3) return;
    CHECK(w.meshes[0].material == "tw/floor" && w.meshes[1].material == "twc4/wall" && w.meshes[2].material == "twc4/five");
    CHECK(w.layerCount.size() == 3 && w.layerUv.size() == 3 && w.vertexColor.size() == 3 && w.vertexColorHigh.size() == 3 &&
          w.layerNormalTransform.size() == 3);
    if (w.layerCount.size() != 3) return;
    CHECK(w.layerCount[0] == 1 && w.layerCount[1] == 2 && w.layerCount[2] == 5);
    CHECK(w.layeredSurfaces == 3);
    CHECK(w.colorSurfaces == 3);
    for (size_t m = 0; m < 3; ++m) {
        const size_t n = w.meshes[m].pos.size();
        const size_t layers = size_t(w.layerCount[m]);
        CHECK(w.layerUv[m].size() == n * layers);
        CHECK(w.vertexColor[m].size() == n && w.vertexColorHigh[m].size() == n);
        CHECK(w.layerNormalTransform[m].size() == n * (layers - 1));
        if (w.layerUv[m].size() != n * layers) continue;
        // Layer 0 is the uv the reader always gave (v flipped).
        for (size_t k = 0; k < n; ++k) {
            CHECK(double(w.layerUv[m][k * layers][0]) == w.meshes[m].uv[k][0]);
            CHECK(double(w.layerUv[m][k * layers][1]) == w.meshes[m].uv[k][1]);
        }
    }

    // The floor: one layer, no colour record (white), no transforms.
    CHECK(w.meshes[0].pos.size() == 4);
    for (uint32_t c : w.vertexColor[0]) CHECK(c == 0xFFFFFFFFu);
    for (uint32_t c : w.vertexColorHigh[0]) CHECK(c == 0xFFFFFFFFu);

    // The wall: two surfaces over the same position and texture records with their own colour records, so 6 vertices
    // (3 + 3), not 3. Vertex j of the block is v(1 + j).
    CHECK(w.meshes[1].pos.size() == 6);
    CHECK(w.meshes[1].idx.size() == 6);
    const Vec3 wallPos[3] = {{100, 0, 0}, {0, 100, 0}, {100, 100, 0}};
    for (uint32_t j = 0; j < 3; ++j) {
        int first = -1, second = -1;
        for (size_t q = 0; q < w.meshes[1].pos.size(); ++q) {
            if (!(w.meshes[1].pos[q] == wallPos[j])) continue;
            // Both copies read the same texture records: layer 1 UV (50 + j, 1 - (60 + j)) and its transform.
            CHECK(w.layerUv[1][q * 2 + 1][0] == float(50 + j));
            CHECK(w.layerUv[1][q * 2 + 1][1] == 1.0f - float(60 + j));
            CHECK(w.layerNormalTransform[1][q] == (0xFF808000u | j));
            if (w.vertexColor[1][q] == (0x221100FFu | (0x40u * j) << 8)) first = int(q);
            if (w.vertexColor[1][q] == 0x0A0B0C0Du + j) second = int(q);
        }
        CHECK(first >= 0 && second >= 0 && first != second);
        if (first < 0) continue;
        CHECK(w.vertexColorHigh[1][size_t(first)] == 0xFFFFFFFFu);  // a 4-byte record
        // Layer 1's weight is G (byte 1).
        CHECK(stockLayerWeight(w.vertexColor[1][size_t(first)], w.vertexColorHigh[1][size_t(first)], 1) == float(0x40 * j) / 255.0f);
    }

    // The five-layer surface: 8 colour bytes per vertex, every layer's UV, transforms for layers 1 and 3, identity
    // (0xFF8080FF) for layers 2 and 4. Vertex j of the block is v(j).
    CHECK(w.meshes[2].pos.size() == 4);
    const Vec3 fivePos[4] = {{0, 0, 0}, {100, 0, 0}, {0, 100, 0}, {100, 100, 0}};
    for (uint32_t j = 0; j < 4; ++j) {
        const int k = vertexAt(w, 2, fivePos[j]);
        CHECK(k >= 0);
        if (k < 0) continue;
        for (int l = 0; l < 5; ++l) {
            CHECK(w.layerUv[2][size_t(k) * 5 + size_t(l)][0] == float(10 * l + int(j)));
            CHECK(w.layerUv[2][size_t(k) * 5 + size_t(l)][1] == 1.0f - float(100 + 10 * l + int(j)));
        }
        const uint32_t lo = w.vertexColor[2][size_t(k)], hi = w.vertexColorHigh[2][size_t(k)];
        CHECK(lo == (0xFFu | (10 * j + 1) << 8 | (10 * j + 2) << 16 | (10 * j + 3) << 24));
        CHECK(hi == 10 * j + 4);
        CHECK(stockLayerWeight(lo, hi, 0) == 1.0f);
        for (int l = 1; l <= 4; ++l) CHECK(stockLayerWeight(lo, hi, l) == float(10 * j + uint32_t(l)) / 255.0f);
        CHECK(stockLayerWeight(lo, hi, 5) == 0.0f);
        const uint32_t* nt = &w.layerNormalTransform[2][size_t(k) * 4];
        CHECK(nt[0] == (0x11111100u | j) && nt[1] == 0xFF8080FFu && nt[2] == (0x33333300u | j) && nt[3] == 0xFF8080FFu);
    }

    // Without an aux stream: every list still parallel, colours white, layer UVs zero, transforms identity, and the two
    // wall surfaces share their vertices again (3, as before colours were keyed).
    StockWorld n;
    CHECK(parseStockWorld(gfxJsonLayers(), {trzJson(s.pos, s.idx)}, n, &err));
    CHECK(n.meshes.size() == 3 && n.layerCount.size() == 3);
    CHECK(n.colorSurfaces == 0);
    for (size_t m = 0; m < n.meshes.size() && m < n.layerCount.size(); ++m) {
        CHECK(n.layerUv[m].size() == n.meshes[m].pos.size() * size_t(n.layerCount[m]));
        for (const auto& uv : n.layerUv[m]) CHECK(uv[0] == 0.0f && uv[1] == 0.0f);
        for (uint32_t c : n.vertexColor[m]) CHECK(c == 0xFFFFFFFFu);
        for (uint32_t t : n.layerNormalTransform[m]) CHECK(t == 0xFF8080FFu);
    }
    CHECK(n.meshes.size() == 3 && n.meshes[1].pos.size() == 3);

    // The oldest export shape (no surfData at all): one layer per mesh, lists parallel.
    StockWorld o;
    CHECK(parseStockWorld(gfxJson(false, false), {trzJson(s.pos, s.idx)}, o, &err));
    CHECK(o.layerCount.size() == o.meshes.size());
    for (size_t m = 0; m < o.meshes.size() && m < o.layerCount.size(); ++m) {
        CHECK(o.layerCount[m] == 1);
        CHECK(o.layerUv[m].size() == o.meshes[m].pos.size());
        CHECK(o.vertexColor[m].size() == o.meshes[m].pos.size());
        CHECK(o.layerNormalTransform[m].empty());
    }
}
