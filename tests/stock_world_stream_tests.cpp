#include "stock_world.h"
#include "test_harness.h"

#include <cstring>
#include <filesystem>
#include <fstream>

using namespace iw8r;
namespace fs = std::filesystem;

namespace {
std::string hexOf(const void* data, size_t size) {
    static const char* d = "0123456789abcdef";
    std::string out;
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        out += d[p[i] >> 4];
        out += d[p[i] & 15];
    }
    return out;
}
void put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}
std::string stub(int index, const std::string& name) {
    return "{\"asset\":{\"fields\":{\"name\":\"" + name + "\",\"transientZoneIndex\":" + std::to_string(index) +
           ",\"drawVerts\":{\"posDataSize\":0,\"indexCount\":0}}}}";
}
}  // namespace

// Big maps keep their world in streamed tiles: the stubs of the main zone name them, and the surface bounds place them.
TEST(stock_world_streamed_zones) {
    const fs::path root = fs::path(iw8r::test::tmpDir()) / "streamed_world" / "mp_big";
    std::error_code ec;
    fs::remove_all(root, ec);
    // Surfaces: two in zone 3 (around x 1000), one in zone 5 (around x 50000), one in zone 0 (resident, not streamed).
    const float b[4][14] = {{1000, 0, 0, 100, 100, 50}, {1400, 200, 0, 50, 50, 50}, {50000, 0, 0, 500, 500, 100}, {0, 0, 0, 10, 10, 10}};
    std::string bytes;
    for (const auto& r : b) bytes += hexOf(r, 56);
    const std::string gfx = "{\"asset\":{\"fields\":{\"surfaces\":{\"surfaces\":{\"values\":[{\"transientZone\":3},{\"transientZone\":3},"
                            "{\"transientZone\":5},{\"transientZone\":0}]},\"surfaceBounds\":{\"bytes\":\"" + bytes + "\"}}}}}";
    put(root / "assets" / "gfx_map" / "maps" / "mp" / "mp_big.d3dbsp.0.asset.json", gfx);
    put(root / "assets" / "gfx_map_trzone" / "mp_big_00003_tr.1.asset.json", stub(3, "mp_big_00003_tr"));
    put(root / "assets" / "gfx_map_trzone" / "mp_big_00005_tr.2.asset.json", stub(5, "mp_big_00005_tr"));
    put(root / "assets" / "gfx_map_trzone" / "mp_big_00009_tr.3.asset.json", stub(9, "mp_big_00009_tr"));  // no surface uses it
    // The map's own transient zone has its buffers here: not streamed.
    put(root / "assets" / "gfx_map_trzone" / "mp_big.0.asset.json",
        "{\"asset\":{\"fields\":{\"name\":\"mp_big\",\"transientZoneIndex\":0,\"drawVerts\":{\"posDataSize\":12,\"indexCount\":3}}}}");
    std::vector<StreamedWorldZone> zones;
    std::string err;
    CHECK(readStreamedWorldZones(root.string(), zones, &err));
    CHECK(zones.size() == 2);
    if (zones.size() != 2) return;
    CHECK(zones[0].index == 3 && zones[0].name == "mp_big_00003_tr" && zones[0].surfaces == 2);
    CHECK(zones[0].bounds.mins.x == 900 && zones[0].bounds.maxs.x == 1450 && zones[0].bounds.maxs.y == 250);
    CHECK(zones[1].index == 5 && zones[1].bounds.mins.x == 49500);

    // Near a point: by distance on X and Y, nearest first, capped.
    auto near = streamedZonesNear(zones, 0, 0, 2000, 10);
    CHECK(near.size() == 1 && near[0].index == 3);
    near = streamedZonesNear(zones, 30000, 0, 30000, 10);
    CHECK(near.size() == 2 && near[0].index == 5 && near[1].index == 3);
    CHECK(streamedZonesNear(zones, 30000, 0, 30000, 1).size() == 1);
    CHECK(streamedZonesNear(zones, 0, 90000, 1000, 10).empty());
    fs::remove_all(root.parent_path(), ec);
}
