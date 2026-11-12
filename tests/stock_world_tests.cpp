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
