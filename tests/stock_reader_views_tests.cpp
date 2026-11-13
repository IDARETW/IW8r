// The stock readers read only the members they need (json_lite View, memory pass 2026-11-13). These tests feed each
// reader the same export twice, once compact and once reformatted (whitespace everywhere, decoy members and strings
// that hold the member names, braces and escaped quotes), and require the same result; and they check that a
// malformed part the reader does not use still fails the read, with the error the whole-text parse gives.
#include "json_lite.h"
#include "stock_clutter.h"
#include "stock_displacement.h"
#include "stock_smodels.h"
#include "stock_world.h"
#include "test_harness.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

std::string hexOf(const std::vector<uint8_t>& b) {
    static const char* d = "0123456789abcdef";
    std::string out;
    for (uint8_t c : b) {
        out += d[c >> 4];
        out += d[c & 15];
    }
    return out;
}
void putU32(std::vector<uint8_t>& b, uint32_t v) {
    for (int k = 0; k < 4; ++k) b.push_back(uint8_t(v >> (8 * k)));
}
void putU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}
void putF32(std::vector<uint8_t>& b, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    putU32(b, u);
}
void setU32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int k = 0; k < 4; ++k) b[at + size_t(k)] = uint8_t(v >> (8 * k));
}
void setF32(std::vector<uint8_t>& b, size_t at, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    setU32(b, at, u);
}

struct Export {
    std::string gfx, trz;
};

// One gfx_map with everything the readers look at: surfaces (three in zone 0, one in streamed zone 7), surfData with a
// displaced surface, surfaceBounds, bakedLightScale (the first member of that name is not positive: the search goes
// on into its sub-object), smodels (hex collections and instances, value indirection, clutter collections).
Export makeExport() {
    std::vector<uint8_t> pos, idx, aux(128, 0);
    const auto vtx = [&](float x, float y, float z) { putF32(pos, x), putF32(pos, y), putF32(pos, z); };
    vtx(0, 0, 0), vtx(64, 0, 0), vtx(64, 64, 0), vtx(0, 64, 0);  // A: 0..47
    vtx(100, 0, 0), vtx(164, 0, 0), vtx(100, 64, 0);             // B: 48..83
    vtx(200, 0, 0), vtx(264, 0, 0), vtx(200, 64, 0);             // C: 84..119
    for (uint16_t i : {0, 1, 2, 0, 2, 3, 0, 1, 2, 0, 1, 2}) putU16(idx, i);
    const uint32_t up = 512u | 512u << 10 | 256u << 20 | 3u << 30;
    for (uint32_t j = 0; j < 4; ++j) {
        setU32(aux, 4 + 4 * j, up);
        setF32(aux, 20 + 8 * j, float(j)), setF32(aux, 24 + 8 * j, 0.5f);
        setU32(aux, 52 + 4 * j, 0x3800u | 0x2000u << 16);  // lo 0.5, hi 1/128
    }
    for (uint32_t j = 0; j < 3; ++j) {
        setU32(aux, 68 + 4 * j, up);
        setF32(aux, 80 + 8 * j, 0.1f * float(j));
        setF32(aux, 104 + 8 * j, 0.25f * float(j));
    }
    std::vector<uint8_t> bounds;
    for (float x : {32.0f, 232.0f, 132.0f, 5000.0f}) {
        putF32(bounds, x), putF32(bounds, 32), putF32(bounds, 0);
        putF32(bounds, 32), putF32(bounds, 32), putF32(bounds, 1);
        for (int k = 0; k < 8; ++k) putU32(bounds, 0);
    }
    std::vector<uint8_t> coll, inst;
    putU32(coll, 0), putU32(coll, 2), putU32(coll, 1), putU32(coll, 0);
    putU32(coll, 2), putU32(coll, 1), putU32(coll, 0), putU32(coll, 32);
    for (int i = 0; i < 3; ++i) {
        putU32(inst, uint32_t(4096 * (i + 1))), putU32(inst, uint32_t(-8192 * i)), putU32(inst, 40960);
        putU16(inst, 32768), putU16(inst, 32768), putU16(inst, uint16_t(32768 + 1000 * i)), putU16(inst, 65535);
        putF32(inst, 1.0f + 0.5f * float(i));
    }

    Export e;
    e.gfx =
        R"({"format":"mw19-asset-json","asset":{"type":"GfxWorld","fields":{"name":{"string":"maps/mp/mp_views.d3dbsp"},)"
        R"("surfaces":{"count":4,"surfaces":{"stride":40,"count":4,"values":[)"
        R"({"tris":{"posOffset":0,"vertexCount":4,"triCount":2,"baseIndex":0},"material":{"name":",tw/disp_test"},"surfDataIndex":0,"transientZone":0,"lightmapIndex":510},)"
        R"({"tris":{"posOffset":84,"vertexCount":3,"triCount":1,"baseIndex":9},"material":{"name":",tw/plain"},"surfDataIndex":2,"transientZone":0,"lightmapIndex":510},)"
        R"({"tris":{"posOffset":48,"vertexCount":3,"triCount":1,"baseIndex":6},"material":{"name":",tw/disp_test"},"surfDataIndex":1,"transientZone":0,"lightmapIndex":510},)"
        R"({"tris":{"posOffset":0,"vertexCount":3,"triCount":1,"baseIndex":0},"material":{"name":",tw/far"},"surfDataIndex":2,"transientZone":7,"lightmapIndex":0})"
        R"(]},"surfData":{"values":[)"
        R"({"transientZone":0,"layerCount":1,"xyzOffset":0,"tangentFrameOffset":4,"lmapCoordOffset":0,"colorOffset":0,"texCoordOffset":20,"normalTransformOffset":[0,0,0,0,0,0,0],"displacementOffset":[52,0,0,0,0,0,0,0]},)"
        R"({"transientZone":0,"layerCount":1,"xyzOffset":48,"tangentFrameOffset":68,"lmapCoordOffset":0,"colorOffset":0,"texCoordOffset":80,"normalTransformOffset":[0,0,0,0,0,0,0],"displacementOffset":[0,0,0,0,0,0,0,0]},)"
        R"({"transientZone":0,"layerCount":1,"xyzOffset":84,"tangentFrameOffset":0,"lmapCoordOffset":0,"colorOffset":0,"texCoordOffset":104,"normalTransformOffset":[0,0,0,0,0,0,0],"displacementOffset":[0,0,0,0,0,0,0,0]})"
        R"(]},"surfaceBounds":{"stride":56,"count":4,"bytes":")" + hexOf(bounds) + R"("}},)"
        R"("draw":{"lightA":{"bakedLightScale":-1},"bakedLightScale":0,"bakedLightScale":0.75,"sub":{"bakedLightScale":0.5}},)"
        R"("smodels":{"collectionCount":2,"models":{"count":2,"values":[)"
        R"({"model":{"type":"XModel","name":"tm/a"},"flags":{"value":1,"name":"LIGHTPROBE"}},{"model":{"name":"tm/b"},"flags":5}]},)"
        R"("collections":{"stride":16,"count":2,"bytes":")" + hexOf(coll) + R"("},)"
        R"("smodelInstanceData":{"stride":24,"count":3,"bytes":")" + hexOf(inst) + R"("},)"
        R"("modelStaticIndirection":{"stride":8,"count":3,"values":[)"
        R"({"lightingInstanceIndirection":11,"splinedModelInstanceIndex":0,"worldTransient":2},)"
        R"({"lightingInstanceIndirection":-1,"splinedModelInstanceIndex":0,"worldTransient":0},)"
        R"({"lightingInstanceIndirection":3,"splinedModelInstanceIndex":1,"worldTransient":9}]},)"
        R"("clutterCollections":{"values":[{"maxInstanceCount":100,"smodelCollectionIndex":1,"terrainSurfaceIndex":0,"clutterLayerIndex":2}]}}}}})";
    e.trz = R"({"format":"mw19-asset-json","asset":{"fields":{"name":"mp_views","transientZoneIndex":0,"drawVerts":{"posDataSize":120,)"
            R"("posData":{"bytes":")" + hexOf(pos) + R"("},"indexCount":12,"indices":{"bytes":")" + hexOf(idx) +
            R"("},"auxDataSize":128,"auxData":{"bytes":")" + hexOf(aux) + R"("}}}}})";
    return e;
}

// The same JSON reformatted: whitespace after every structural character outside strings, and decoy members at the
// root that hold the names the readers look for (inside a nested object and inside a string with escaped quotes).
std::string decorate(const std::string& t) {
    std::string out;
    bool inStr = false;
    for (size_t i = 0; i < t.size(); ++i) {
        const char c = t[i];
        if (inStr) {
            out += c;
            if (c == '\\' && i + 1 < t.size()) out += t[++i];
            else if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') {
            inStr = true;
            out += c;
            continue;
        }
        if (c == '}' || c == ']') out += "\n ";
        out += c;
        if (c == '{' || c == '[' || c == ',' || c == ':') out += "\r\n\t ";
    }
    const size_t open = out.find('{');
    out.insert(open + 1,
               R"( "decoy" : { "asset" : { "fields" : { "surfaces" : { "surfaces" : { "values" : [ 1 ] } }, "drawVerts" : 5 } } } ,)"
               R"( "note" : "\"asset\":{\"fields\":{\"surfaces\":{} [{ ]} \\", )");
    return out;
}

// Everything a StockWorld holds, as bytes.
std::string worldKey(const StockWorld& w) {
    std::string k;
    const auto raw = [&](const void* p, size_t n) { k.append(static_cast<const char*>(p), n); };
    const auto str = [&](const std::string& s) {
        const size_t n = s.size();
        raw(&n, sizeof n);
        k += s;
    };
    for (const ImportedMesh& m : w.meshes) {
        str(m.material);
        raw(m.pos.data(), m.pos.size() * sizeof(Vec3));
        raw(m.normal.data(), m.normal.size() * sizeof(Vec3));
        raw(m.uv.data(), m.uv.size() * sizeof(m.uv[0]));
        raw(m.idx.data(), m.idx.size() * 4);
    }
    for (const auto& v : w.lightmapUv) raw(v.data(), v.size() * sizeof(v[0]));
    for (const auto& v : w.lightmapIndex) raw(v.data(), v.size() * sizeof(v[0]));
    raw(w.layerCount.data(), w.layerCount.size() * sizeof(int));
    for (const auto& v : w.layerUv) raw(v.data(), v.size() * sizeof(v[0]));
    for (const auto& v : w.vertexColor) raw(v.data(), v.size() * 4);
    for (const auto& v : w.vertexColorHigh) raw(v.data(), v.size() * 4);
    for (const auto& v : w.layerNormalTransform) raw(v.data(), v.size() * 4);
    raw(&w.bakedLightScale, sizeof(double));
    raw(&w.bounds, sizeof(AABB));
    for (const auto& s : w.warnings) str(s);
    if (w.surfaceTable) {
        for (const auto& s : w.surfaceTable->surfaces) {
            str(s.material);
            raw(&s.posOffset, sizeof(int64_t) * 7);
            k += char(s.hasTris);
        }
        for (const auto& d : w.surfaceTable->surfData) raw(&d, sizeof d);
        for (const auto& [m, n] : w.surfaceTable->materialLayers) str(m + "=" + std::to_string(n));
        raw(&w.surfaceTable->bakedLightScale, sizeof(double));
    }
    return k;
}

std::string displacementKey(const StockDisplacementData& d) {
    std::string k;
    for (const auto& m : d.meshes) {
        k += m.material + "|" + std::to_string(m.mesh) + "|" + std::to_string(m.layerCount) + "|" + std::to_string(m.displacedLayers) +
             "|" + std::to_string(m.surfaces) + "|" + std::to_string(m.displacedSurfaces) + "|" + std::to_string(m.triangles) +
             "|" + std::to_string(m.displacedTriangles);
        for (const auto& v : m.vertices) k.append(reinterpret_cast<const char*>(&v), sizeof v);
        k.append(reinterpret_cast<const char*>(m.normal.data()), m.normal.size() * sizeof(Vec3));
    }
    k += std::to_string(d.displacedSurfaces) + "|" + std::to_string(d.vertices) + "|" + std::to_string(d.triangles) + "|" +
         std::to_string(d.displacedTriangles);
    for (const auto& w : d.warnings) k += "|" + w;
    return k;
}

std::string modelsKey(const std::vector<StockModelInstance>& v) {
    std::string k;
    for (const auto& s : v) {
        k += s.model + "|";
        k.append(reinterpret_cast<const char*>(&s.origin), sizeof(Vec3));
        k.append(reinterpret_cast<const char*>(s.axis.data()), sizeof(s.axis));
        k.append(reinterpret_cast<const char*>(&s.scale), sizeof(double));
        k += std::to_string(s.index) + "|" + std::to_string(s.lightingIndex) + "|" + std::to_string(s.worldTransient) + "|" +
             std::to_string(s.modelFlags) + ";";
    }
    return k;
}

std::string clutterKey(const std::vector<StockClutterCollection>& v) {
    std::string k;
    for (const auto& c : v)
        k += std::to_string(c.index) + "," + std::to_string(c.maxInstanceCount) + "," + std::to_string(c.smodelCollectionIndex) + "," +
             std::to_string(c.terrainSurfaceIndex) + "," + std::to_string(c.clutterLayerIndex) + "," + std::to_string(c.smodelIndex) +
             "," + c.model + ";";
    return k;
}

std::string zonesKey(const std::vector<StreamedWorldZone>& v) {
    std::string k;
    for (const auto& z : v) {
        k += std::to_string(z.index) + "," + z.name + "," + std::to_string(z.surfaces) + ",";
        k.append(reinterpret_cast<const char*>(&z.bounds), sizeof(AABB));
        k += ";";
    }
    return k;
}

void put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

// A zone folder as the world cache lays it out.
fs::path writeZone(const std::string& name, const Export& e) {
    const fs::path dir = fs::path(iw8r::test::tmpDir()) / "reader_views" / name / "mp_views";
    std::error_code ec;
    fs::remove_all(dir, ec);
    put(dir / "assets" / "gfx_map" / "maps" / "mp" / "mp_views.d3dbsp.asset.json", e.gfx);
    put(dir / "assets" / "gfx_map_trzone" / "mp_views.0.asset.json", e.trz);
    return dir;
}

}  // namespace

TEST(stock_readers_ignore_formatting_and_decoys) {
    const Export plain = makeExport();
    const Export fancy{decorate(plain.gfx), decorate(plain.trz)};
    CHECK(json::parse(fancy.gfx).has_value() && json::parse(fancy.trz).has_value());

    // World (text and folder readers).
    StockWorld a, b, c;
    std::string err;
    CHECK(parseStockWorld(plain.gfx, {plain.trz}, a, &err));
    CHECK(parseStockWorld(fancy.gfx, {fancy.trz}, b, &err));
    CHECK(a.meshes.size() == 2 && a.bakedLightScale == 0.5);  // the first bakedLightScale (0) is passed over
    CHECK(a.surfaceTable && a.surfaceTable->surfaces.size() == 4 && a.surfaceTable->surfData.size() == 3);
    CHECK(!a.warnings.empty());  // zone 7 is not in the export
    CHECK(worldKey(a) == worldKey(b));
    const fs::path zp = writeZone("plain", plain), zf = writeZone("fancy", fancy);
    CHECK(readStockWorld(zf.string(), c, &err));
    CHECK(worldKey(a) == worldKey(c));

    // Streamed zones: zone 7 (one surface around x 5000).
    std::vector<StreamedWorldZone> za, zb;
    CHECK(readStreamedWorldZones(zp.string(), za, &err));
    CHECK(readStreamedWorldZones(zf.string(), zb, &err));
    CHECK(za.size() == 1 && za[0].index == 7 && za[0].name == "mp_views_00007_tr" && za[0].surfaces == 1);
    CHECK(zonesKey(za) == zonesKey(zb));

    // Displacement.
    StockDisplacementData da, db, dc;
    CHECK(parseStockDisplacement(plain.gfx, {plain.trz}, a, da, &err));
    CHECK(parseStockDisplacement(fancy.gfx, {fancy.trz}, a, db, &err));
    CHECK(readStockDisplacement(zf.string(), a, dc, &err));
    CHECK(da.meshes.size() == 1 && da.displacedSurfaces == 1 && da.triangles == 3);
    CHECK(displacementKey(da) == displacementKey(db));
    CHECK(displacementKey(da) == displacementKey(dc));

    // Static models.
    std::vector<StockModelInstance> ma, mb;
    CHECK(readStockModelInstances(zp.string(), ma, &err));
    CHECK(readStockModelInstances(zf.string(), mb, &err));
    CHECK(ma.size() == 3 && ma[0].model == "tm/b" && ma[2].model == "tm/a" && ma[0].lightingIndex == 11 &&
          ma[1].lightingIndex == 0xFFFFFFFFu && ma[2].worldTransient == 9 && ma[0].modelFlags == 5 && ma[2].modelFlags == 1);
    CHECK(modelsKey(ma) == modelsKey(mb));

    // Clutter collections.
    std::vector<StockClutterCollection> ca, cb, cc;
    CHECK(parseStockClutterCollections(plain.gfx, ca, &err));
    CHECK(parseStockClutterCollections(fancy.gfx, cb, &err));
    CHECK(readStockClutterCollections(zf.string(), cc, &err));
    CHECK(ca.size() == 1 && ca[0].model == "tm/a" && ca[0].clutterLayerIndex == 2);
    CHECK(clutterKey(ca) == clutterKey(cb) && clutterKey(ca) == clutterKey(cc));

    std::error_code ec;
    fs::remove_all(fs::path(iw8r::test::tmpDir()) / "reader_views", ec);
}

TEST(stock_readers_still_reject_malformed_exports) {
    const Export plain = makeExport();
    // A broken member the readers never use: the read fails as the whole-text parse did, with its error.
    Export bad = plain;
    bad.gfx.insert(bad.gfx.find("\"surfaces\""), "\"junk\":[1,2,],");
    std::string parseErr;
    CHECK(!json::parse(bad.gfx, &parseErr).has_value());
    const std::string want = "gfx_map JSON: " + parseErr;

    StockWorld w;
    std::string err;
    CHECK(!parseStockWorld(bad.gfx, {plain.trz}, w, &err));
    CHECK(err == want);
    StockWorld good;
    CHECK(parseStockWorld(plain.gfx, {plain.trz}, good, &err));
    StockDisplacementData d;
    err.clear();
    CHECK(!parseStockDisplacement(bad.gfx, {plain.trz}, good, d, &err));
    CHECK(err == want);
    const fs::path zb = writeZone("bad", bad);
    std::vector<StockModelInstance> m;
    err.clear();
    CHECK(!readStockModelInstances(zb.string(), m, &err));
    CHECK(err == want);
    std::vector<StreamedWorldZone> z;
    err.clear();
    CHECK(!readStreamedWorldZones(zb.string(), z, &err));
    CHECK(err == want);
    err.clear();
    CHECK(!readStockWorld(zb.string(), w, &err));
    CHECK(err == want);

    // Trailing data after the export and a truncated transient zone.
    err.clear();
    CHECK(!parseStockWorld(plain.gfx + " x", {plain.trz}, w, &err));
    CHECK(err.rfind("gfx_map JSON: trailing data at byte ", 0) == 0);
    std::string trzErr;
    const std::string cut = plain.trz.substr(0, plain.trz.size() / 2);
    CHECK(!json::parse(cut, &trzErr).has_value());
    err.clear();
    CHECK(!parseStockWorld(plain.gfx, {cut}, w, &err));
    CHECK(err == "gfx_map_trzone JSON: " + trzErr);
    err.clear();
    CHECK(!parseStockDisplacement(plain.gfx, {cut}, good, d, &err));
    CHECK(err == "gfx_map_trzone JSON: " + trzErr);
    // An empty transient zone file (an interrupted export) fails the world read as before.
    const fs::path ze = writeZone("empty_trz", plain);
    put(ze / "assets" / "gfx_map_trzone" / "mp_views.0.asset.json", "");
    err.clear();
    CHECK(!readStockWorld(ze.string(), w, &err));
    CHECK(err == "gfx_map_trzone JSON: unexpected end at byte 0");

    std::error_code ec;
    fs::remove_all(fs::path(iw8r::test::tmpDir()) / "reader_views", ec);
}
