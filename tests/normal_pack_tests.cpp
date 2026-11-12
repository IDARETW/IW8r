// IW8 packed normal maps (3.2 / 4.14): octahedral pair <-> slopes <-> the converter's slope map.
#include <cmath>
#include <vector>

#include "normal_pack.h"
#include "test_harness.h"

using namespace iw8r;

TEST(normal_pack_flat_is_neutral) {
    double sx = 9, sy = 9;
    replayNormalSlopes(128, 128, sx, sy);
    CHECK(std::fabs(sx) < 0.02 && std::fabs(sy) < 0.02);
    uint8_t g = 0, a = 0;
    replayNormalPack(0, 0, g, a);
    CHECK(g == 128 && a == 128);
    uint8_t r = 0, gg = 0;
    slopeMapBytes(0, 0, r, gg);
    // The converter reads 4.08 R/255 - 2.08: byte 130 is the closest to zero slope.
    CHECK(std::fabs(4.07999992 * r / 255.0 - 2.07999992) < 0.01);
    CHECK(std::fabs(4.06451607 * gg / 255.0 - 2.06451607) < 0.01);
}

TEST(normal_pack_round_trip) {
    // Slopes -> packed pair -> slopes, within the 8-bit step, over the range stock maps use.
    for (double sx = -1.5; sx <= 1.5; sx += 0.25)
        for (double sy = -1.5; sy <= 1.5; sy += 0.25) {
            uint8_t g, a;
            replayNormalPack(sx, sy, g, a);
            double bx, by;
            replayNormalSlopes(g, a, bx, by);
            const double tol = 0.02 * (1.0 + std::fabs(sx) + std::fabs(sy)) * (1.0 + std::fabs(sx) + std::fabs(sy));
            CHECK(std::fabs(bx - sx) <= tol);
            CHECK(std::fabs(by - sy) <= tol);
        }
}

TEST(normal_pack_slope_map_matches_converter) {
    // A packed stock texel turned into the slope map and read back with the converter's formula gives the same pair
    // again (what x64-zt's EncodeNativeWorldNormal does with the editor's image).
    const uint8_t packed[] = {200, 150, 255, 110,  40, 128, 255, 128,  90, 96, 200, 170};
    std::vector<uint8_t> slope(12), spec(12);
    replayNormalToSlopeMap(packed, 3, slope.data());
    for (int i = 0; i < 3; ++i) {
        const double sx = 4.07999992 * slope[i * 4] / 255.0 - 2.07999992;
        const double sy = 4.06451607 * slope[i * 4 + 1] / 255.0 - 2.06451607;
        uint8_t g, a;
        replayNormalPack(sx, sy, g, a);
        CHECK(std::abs(int(g) - int(packed[i * 4 + 1])) <= 2);
        CHECK(std::abs(int(a) - int(packed[i * 4 + 3])) <= 2);
        CHECK(slope[i * 4 + 2] == 255 && slope[i * 4 + 3] == 255);
    }
    const uint8_t colour[] = {10, 20, 30, 255,  200, 100, 50, 0,  156, 156, 156, 128};
    replaySpecularMap(colour, packed, 3, spec.data());
    CHECK(spec[0] == 56 && spec[1] == 56 && spec[2] == 56 && spec[3] == 200);  // alpha 255: dielectric; gloss from R
    CHECK(spec[4] == 200 && spec[5] == 100 && spec[6] == 50 && spec[7] == 40);  // alpha 0: metal, its own colour
    CHECK(std::abs(int(spec[8]) - 106) <= 1);                                   // half way
    replaySpecularMap(nullptr, packed, 3, spec.data());
    CHECK(spec[0] == 56 && spec[3] == 200);
}
