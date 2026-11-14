// The game's distance rule for placed models (core smodel_lod.h, research 119) and the placement's own LOD scale
// read from a gfx_map export's smodels.instanceFlags (core stock_smodels).
#include "smodel_lod.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "smodel_flags.h"
#include "stock_smodels.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

std::string hexBytes(const std::vector<uint8_t>& b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (uint8_t v : b) s.push_back(d[v >> 4]), s.push_back(d[v & 15]);
    return s;
}

void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int k = 0; k < 4; ++k) b.push_back(uint8_t(v >> (8 * k)));
}

// GfxStaticModelCollection: u32 first, u32 count, u16 model, u16 transient, u16 clutter, u8 flags, u8 pad.
void putCollection(std::vector<uint8_t>& b, uint32_t first, uint32_t count, uint16_t model, uint8_t flags) {
    put32(b, first);
    put32(b, count);
    put32(b, model);
    put32(b, uint32_t(flags) << 16);
}

// GfxSModelInstanceData at the origin, identity orientation (x, y, z = 32768 -> 0; w = 65535 -> 1), scale 1.
void putInstance(std::vector<uint8_t>& b, int32_t xInches) {
    put32(b, uint32_t(xInches * 4096));
    put32(b, 0);
    put32(b, 0);
    for (int k = 0; k < 3; ++k) b.push_back(0x00), b.push_back(0x80);
    b.push_back(0xFF), b.push_back(0xFF);
    const float one = 1.0f;
    uint32_t bits;
    std::memcpy(&bits, &one, 4);
    put32(b, bits);
}

std::string exportJson(bool withFlags, bool flagsAsValues) {
    // collection 0: one instance (row 0), flags 0x80           -> its own scale (index 13 = 3)
    // collection 1: two instances (rows 1, 2)                  -> 1 whatever their bytes say
    // collection 2: one instance (row 3), clutter flag 0x20    -> 1
    // collection 3: one instance (row 4), high bits set        -> the low four bits count (index 2 = 0.5)
    std::vector<uint8_t> coll, inst;
    putCollection(coll, 0, 1, 0, 0x80);
    putCollection(coll, 1, 2, 0, 0);
    putCollection(coll, 3, 1, 0, 0x20);
    putCollection(coll, 4, 1, 0, 0);
    for (int i = 0; i < 5; ++i) putInstance(inst, i * 100);
    const std::vector<uint8_t> flags{0x0D, 0x0F, 0x0F, 0x0F, 0xF2};
    std::string f;
    if (withFlags && !flagsAsValues)
        f = R"(,"instanceFlags":{"type":"unsigned __int8","stride":1,"count":5,"bytes":")" + hexBytes(flags) + R"("})";
    if (withFlags && flagsAsValues) f = R"(,"instanceFlags":{"type":"unsigned __int8","stride":1,"count":5,"values":[13,15,15,15,242]})";
    return std::string(R"({"format":"mw19-asset-json","root_type":"GfxWorld","asset":{"type":"GfxWorld","fields":{)") +
           R"("smodels":{"models":{"values":[{"model":{"name":"tm/a"},"flags":{"value":1}}]},)" +
           R"("collections":{"stride":16,"count":4,"bytes":")" + hexBytes(coll) + R"("},)" +
           R"("smodelInstanceData":{"stride":24,"count":5,"bytes":")" + hexBytes(inst) + R"(")" + "}" + f + "}}}}";
}

std::vector<StockModelInstance> readExport(const std::string& name, const std::string& json) {
    const fs::path zone = fs::path(iw8r::test::tmpDir()) / name;
    fs::create_directories(zone / "assets" / "gfx_map");
    std::ofstream(zone / "assets" / "gfx_map" / "t.d3dbsp.1.asset.json", std::ios::binary) << json;
    std::vector<StockModelInstance> v;
    std::string err;
    CHECK(readStockModelInstances(zone.string(), v, &err));
    CHECK(err.empty());
    return v;
}

}  // namespace

TEST(smodel_lod_scale_table) {
    CHECK(smodelLodScaleOfIndex(6) == 1.0);
    CHECK(smodelLodScaleOfIndex(0) == 0.25 && smodelLodScaleOfIndex(15) == 4.0 && smodelLodScaleOfIndex(13) == 3.0);
    CHECK(smodelLodScaleOfIndex(16) == 1.0 && smodelLodScaleOfIndex(999) == 1.0);
    for (unsigned i = 1; i < 16; ++i) CHECK(smodelLodScaleOfIndex(i) > smodelLodScaleOfIndex(i - 1));
}

TEST(smodel_lod_placement_scale_from_export) {
    for (const bool asValues : {false, true}) {
        const auto v = readExport(asValues ? "smodel_lod_values" : "smodel_lod_bytes", exportJson(true, asValues));
        CHECK(v.size() == 5);
        if (v.size() != 5) return;
        CHECK(v[0].lodCullScale == 3.0f);                             // a collection of one: the placement's own
        CHECK(v[1].lodCullScale == 1.0f && v[2].lodCullScale == 1.0f);  // several instances: the game uses 1
        CHECK(v[3].lodCullScale == 1.0f);                             // clutter: 1
        CHECK(v[4].lodCullScale == 0.5f);                             // only the low four bits
        CHECK(v[0].collection == 0 && v[1].collection == 1 && v[2].collection == 1 && v[4].collection == 3);
    }
    // An export without the flags: every placement at 1.
    const auto plain = readExport("smodel_lod_noflags", exportJson(false, false));
    CHECK(plain.size() == 5);
    for (const auto& s : plain) CHECK(s.lodCullScale == 1.0f);
}

TEST(smodel_lod_fov_factor) {
    // 1 at the game's own view: 80 degrees wide at 16:9 is 50.53 degrees high.
    const float tanGame = std::tan(0.5f * 80.0f * 3.14159265f / 180.0f) * 9.0f / 16.0f;
    CHECK(std::fabs(lodFovFactor(tanGame) - 1.0f) < 2e-3f);
    CHECK(lodFovFactor(2.0f * tanGame) > 1.99f && lodFovFactor(2.0f * tanGame) < 2.01f);
    CHECK(lodFovFactor(0.0f) == 1.0f && lodFovFactor(-1.0f) == 1.0f);
    CHECK(lodFovFactor(1e-4f) == 0.05f && lodFovFactor(100.0f) == 8.0f);
}

TEST(smodel_lod_box_distance) {
    // A wall 256 long: model box x -128..128, y -8..8, z 0..96, placed at (1000, 0, 0).
    const float mid[3] = {0, 0, 48}, half[3] = {128, 8, 48};
    const float rows[12] = {1, 0, 0, 1000, 0, 1, 0, 0, 0, 0, 1, 0};
    const float inside[3] = {1000, 0, 10}, along[3] = {0, 0, 10}, side[3] = {1000, 508, 48}, diag[3] = {1000 + 128 + 30, 8 + 40, 48};
    CHECK(placementBoxDistance(rows, mid, half, inside) == 0.0f);
    CHECK(std::fabs(placementBoxDistance(rows, mid, half, along) - 872.0f) < 1e-3f);  // to the near end, not the centre
    CHECK(std::fabs(placementBoxDistance(rows, mid, half, side) - 500.0f) < 1e-3f);
    CHECK(std::fabs(placementBoxDistance(rows, mid, half, diag) - 50.0f) < 1e-3f);
    // Turned 90 degrees about Z and scaled 2: the long side lies along Y (512 long), 16 thick each way, 192 high.
    const float turned[12] = {0, -2, 0, 1000, 2, 0, 0, 0, 0, 0, 2, 0};
    const float e1[3] = {1000, 1256, 10}, e2[3] = {1116, 0, 10};
    CHECK(std::fabs(placementBoxDistance(turned, mid, half, e1) - 1000.0f) < 1e-3f);
    CHECK(std::fabs(placementBoxDistance(turned, mid, half, e2) - 100.0f) < 1e-3f);
}

TEST(smodel_lod_cull_rule) {
    // Last LOD distance 10000.
    CHECK(!placementLodCulled(9999.0f, 1.0f, 1.0f, 10000.0f));
    CHECK(placementLodCulled(10000.0f, 1.0f, 1.0f, 10000.0f));
    // The placement's scale moves the distance: 3 keeps it to 30000, 0.5 drops it at 5000.
    CHECK(!placementLodCulled(29000.0f, 1.0f, 3.0f, 10000.0f) && placementLodCulled(30000.0f, 1.0f, 3.0f, 10000.0f));
    CHECK(!placementLodCulled(4999.0f, 1.0f, 0.5f, 10000.0f) && placementLodCulled(5000.0f, 1.0f, 0.5f, 10000.0f));
    // A wide view never brings the cull nearer; a zoomed view pushes it out.
    CHECK(!placementLodCulled(9999.0f, 2.0f, 1.0f, 10000.0f) && placementLodCulled(10000.0f, 2.0f, 1.0f, 10000.0f));
    CHECK(!placementLodCulled(19999.0f, 0.5f, 1.0f, 10000.0f) && placementLodCulled(20000.0f, 0.5f, 1.0f, 10000.0f));
    // No last distance, or a placement marked never (scale 0): not left out.
    CHECK(!placementLodCulled(1e9f, 1.0f, 1.0f, 0.0f) && !placementLodCulled(1e9f, 1.0f, 0.0f, 10000.0f));
}
