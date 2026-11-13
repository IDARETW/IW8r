// Light grid for builds (src/core/light_grid.h): the bytes follow x64-zt iw3_lightgrid.cpp's reader (rows, runs, z
// order, padding), checked here by replaying those rules.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "../src/core/light_grid.h"
#include "test_harness.h"

using namespace iw8r;

namespace {
uint32_t rd16(const std::string& s, size_t o) { return uint8_t(s[o]) | (uint32_t(uint8_t(s[o + 1])) << 8); }
uint32_t rd32(const std::string& s, size_t o) { return rd16(s, o) | (rd16(s, o + 2) << 16); }

// The converter's row walk (iw3_lightgrid.cpp ~200-265): returns the cells consumed, or -1 on a rule violation.
long replay(const LightGridFiles& f) {
    size_t expected = 0, previousEnd = 0;
    const size_t rows = f.rowStarts.size() / 2;
    for (size_t row = 0; row < rows; ++row) {
        const uint32_t start = rd16(f.rowStarts, row * 2);
        if (start == 0xFFFF) continue;
        size_t cur = size_t(start) * 4;
        if (cur < previousEnd || cur + 12 > f.rowData.size()) return -1;
        const uint32_t width = rd16(f.rowData, cur + 2), height = rd16(f.rowData, cur + 6), first = rd32(f.rowData, cur + 8);
        cur += 12;
        if (first != expected || !width || !height) return -1;
        size_t consumed = 0;
        while (consumed < width) {
            const uint8_t run = uint8_t(f.rowData[cur++]), count = uint8_t(f.rowData[cur++]);
            if (!run || consumed + run > width) return -1;
            uint8_t startZ = 0;
            if (count) startZ = uint8_t(f.rowData[cur++]);
            if (startZ + count > height) return -1;
            expected += size_t(run) * count;
            consumed += run;
        }
        previousEnd = (cur + 3) & ~size_t(3);
        for (size_t k = cur; k < previousEnd; ++k)
            if (f.rowData[k] != 0) return -1;
    }
    if (previousEnd != f.rowData.size() || expected * 4 != f.entries.size()) return -1;
    return long(expected);
}
}  // namespace

TEST(light_grid_follows_the_converter_reader) {
    for (const double width : {1000.0, 9000.0}) {  // 9000 units: more than 255 columns, so rows split into runs
        LightGridInput in;
        in.mapId = "mp_test";
        in.bounds.add(Vec3{-width / 2, -width / 2, -64});
        in.bounds.add(Vec3{width / 2, width / 2, 512});
        in.sunToward = normalize(Vec3{0.3, 0.2, 0.8});
        in.sunVisible = [](const Vec3& p) { return p.x > 0; };  // half the map in shadow
        LightGridFiles f;
        std::string err;
        CHECK(buildLightGrid(in, f, &err));
        CHECK(replay(f) == long(f.entryCount));
        CHECK(f.dims[1] > (width > 8000 ? 255 : 0));
        CHECK(f.sunlit > 0 && f.sunlit < f.entryCount);
        CHECK(f.colors.size() == 168 && f.json.find("\"entry_count\": " + std::to_string(f.entryCount)) != std::string::npos);
        CHECK(f.json.find("maps/mp/mp_test.d3dbsp.lightgrid.entries.bin") != std::string::npos);
    }
    // Sun below the horizon: nothing sunlit; no bounds: refused.
    LightGridInput low;
    low.mapId = "mp_test";
    low.bounds.add(Vec3{0, 0, 0});
    low.bounds.add(Vec3{256, 256, 128});
    low.sunToward = Vec3{0, 0, -1};
    LightGridFiles f;
    CHECK(buildLightGrid(low, f) && f.sunlit == 0);
    LightGridInput none;
    none.mapId = "mp_test";
    CHECK(!buildLightGrid(none, f));
}

TEST(light_grid_floor_bands_stay_valid_and_small) {
    LightGridInput in;
    in.mapId = "mp_test";
    in.bounds.add(Vec3{-2000, -2000, -64});
    in.bounds.add(Vec3{2000, 2000, 1500});
    in.sunToward = normalize(Vec3{0.3, 0.2, 0.8});
    // A floor at z 0 everywhere except a raised block (x > 0) with a second level at 512, and an empty corner.
    in.floorsAt = [](double x, double y) -> std::vector<double> {
        if (x < -1500 && y < -1500) return {};
        if (x > 0) return {512, 0};
        return {0};
    };
    LightGridFiles full, band;
    LightGridInput fullIn = in;
    fullIn.floorsAt = nullptr;
    CHECK(buildLightGrid(fullIn, full));
    CHECK(buildLightGrid(in, band));
    CHECK(replay(band) == long(band.entryCount));
    CHECK(band.entryCount > 0 && band.entryCount * 2 < full.entryCount);
}

TEST(light_grid_palette_from_samples) {
    // ROADMAP 5.20: per-cell colours from samplesAt (here: dark for x < 0, bright and blue-ish upward for x >= 0).
    const auto& dirs = lightGridSampleDirections();
    CHECK(dirs.size() == 56);
    for (const Vec3& d : dirs) CHECK(std::fabs(length(d) - 1.0) < 1e-9);
    CHECK(std::fabs(dirs[0].x - dirs[0].y) < 1e-12 && dirs[0].x < 0 && dirs[0].z < 0);  // texel (0,0,0): (-1,-1,-1)
    CHECK(dirs[55].x > 0 && dirs[55].y > 0 && dirs[55].z > 0);                         // texel (3,3,3)
    LightGridInput in;
    in.mapId = "mp_test";
    in.bounds.add(Vec3{-1000, -800, 0});
    in.bounds.add(Vec3{1000, 800, 384});
    in.sunToward = normalize(Vec3{0.3, 0.2, 0.8});
    in.threads = 3;
    in.samplesAt = [&dirs](const Vec3& p, std::array<std::array<float, 3>, 56>& s) {
        for (int i = 0; i < 56; ++i) {
            const double up = std::max(0.0, dirs[size_t(i)].z);
            const double base = p.x < 0 ? 10.0 : 300.0;
            s[size_t(i)] = {float(base * (0.5 + 0.5 * up)), float(base * (0.6 + 0.5 * up)), float(base * (0.8 + 0.6 * up))};
        }
    };
    LightGridFiles f;
    std::string err;
    CHECK(buildLightGrid(in, f, &err));
    CHECK(replay(f) == long(f.entryCount));
    CHECK(f.colorCount == 2);  // two distinct cell colours, deduplicated
    CHECK(f.colors.size() == 168 * f.colorCount);
    CHECK(f.json.find("\"color_count\": 2") != std::string::npos);
    // Every entry points into the palette, and both colours are used.
    bool used[2] = {false, false};
    for (size_t e = 0; e < f.entryCount; ++e) {
        const uint32_t c = rd16(f.entries, e * 4);
        CHECK(c < f.colorCount);
        if (c < 2) used[c] = true;
    }
    CHECK(used[0] && used[1]);
    // The samples are stored in direction order, clamped to a byte: the bright colour's upward sample is 255 (blue).
    const size_t bright = uint8_t(f.colors[0]) > 50 ? 0 : 1;
    CHECK(uint8_t(f.colors[bright * 168 + 55 * 3 + 2]) == 255);
    // Without samplesAt: the single flat colour, as before.
    in.samplesAt = nullptr;
    CHECK(buildLightGrid(in, f) && f.colorCount == 1 && f.colors.size() == 168);
}
