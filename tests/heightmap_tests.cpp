// Height-map tests: patch round trip, PGM read/write, resampling, erosion and slope blend (src/core/heightmap.cpp).
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "heightmap.h"
#include "map_text.h"
#include "patch_ops.h"
#include "patch_ops2.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Fresh scratch folder in the system temp dir; removed by the caller.
std::filesystem::path scratchDir(const char* tag) {
    auto d = std::filesystem::temp_directory_path() / (std::string("iw8r_heightmap_") + tag);
    std::error_code ec;
    std::filesystem::remove_all(d, ec);
    std::filesystem::create_directories(d, ec);
    return d;
}

AABB boxXY(double w, double h) {
    AABB b;
    b.add(Vec3{0, 0, 0});
    b.add(Vec3{w, h, 0});
    return b;
}

double sumOf(const HeightMap& hm) {
    double s = 0;
    for (float v : hm.z) s += v;
    return s;
}

double sumAbsOf(const HeightMap& hm) {
    double s = 0;
    for (float v : hm.z) s += std::fabs(v);
    return s;
}

// Largest slope between a cell and any of its 8 neighbours, per unit of cell distance (same metric as erosion).
double maxSlope(const HeightMap& hm) {
    double m = 0;
    for (int r = 0; r < hm.h; ++r) {
        for (int c = 0; c < hm.w; ++c) {
            for (int dr = -1; dr <= 1; ++dr) {
                for (int dc = -1; dc <= 1; ++dc) {
                    if (dr == 0 && dc == 0) continue;
                    const int nr = r + dr, nc = c + dc;
                    if (nr < 0 || nr >= hm.h || nc < 0 || nc >= hm.w) continue;
                    const double dist = (dr != 0 && dc != 0) ? std::sqrt(2.0) : 1.0;
                    const double d = std::fabs(static_cast<double>(hm.z[r * hm.w + c]) - hm.z[nr * hm.w + nc]);
                    m = std::max(m, d / dist);
                }
            }
        }
    }
    return m;
}

}  // namespace

TEST(heightmap_patch_roundtrip) {
    PatchGrid g = makeTerrainPatch(boxXY(512, 256), 5, 7, "terrain/grass");
    CHECK(g.rows == 5 && g.cols == 7);
    for (size_t i = 0; i < g.pos.size(); ++i) g.pos[i].z = 40.0 * std::sin(0.7 * static_cast<double>(i)) + 0.3 * i;
    const PatchGrid orig = g;

    const HeightMap hm = heightMapFromPatch(g);
    CHECK(hm.w == 7 && hm.h == 5);
    CHECK(hm.z.size() == 35);
    // Row 0 of the map is the max-Y row of the patch (the last grid row).
    CHECK(std::fabs(hm.z[0] - g.pos[4 * 7 + 0].z) < 1e-4);
    CHECK(std::fabs(hm.z[4 * 7 + 6] - g.pos[0 * 7 + 6].z) < 1e-4);

    float lo = hm.z[0], hi = hm.z[0];
    for (float v : hm.z) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }

    // Flatten the patch, then rebuild it from the map over the same height range.
    PatchGrid back = g;
    for (Vec3& p : back.pos) p.z = 0;
    CHECK(applyHeightMap(back, hm, lo, hi));
    bool same = back.pos.size() == orig.pos.size();
    for (size_t i = 0; same && i < orig.pos.size(); ++i) {
        same = std::fabs(back.pos[i].x - orig.pos[i].x) < 1e-9 && std::fabs(back.pos[i].y - orig.pos[i].y) < 1e-9 &&
               std::fabs(back.pos[i].z - orig.pos[i].z) < 1e-4;
    }
    CHECK(same);

    // The rebuilt grid also survives the .map text path.
    Primitive prim = makePatchPrimitive(back, "\n", "");
    reparsePrimitive(prim);
    const auto viaText = patchGrid(prim);
    CHECK(viaText.has_value());
    if (viaText) {
        bool textSame = viaText->pos.size() == orig.pos.size();
        for (size_t i = 0; textSame && i < orig.pos.size(); ++i)
            textSame = std::fabs(viaText->pos[i].z - orig.pos[i].z) < 1e-4;
        CHECK(textSame);
    }

    // Inconsistent grids and bad ranges are refused and leave the grid alone.
    PatchGrid bad = g;
    bad.pos.pop_back();
    CHECK(heightMapFromPatch(bad).z.empty());
    PatchGrid keep = g;
    CHECK(!applyHeightMap(keep, hm, 5.0, 1.0));
    CHECK(keep.pos[3].z == g.pos[3].z);
}

TEST(heightmap_pgm_roundtrip) {
    const auto dir = scratchDir("pgm");
    const std::string path16 = (dir / "terrain16.pgm").string();

    HeightMap hm;
    hm.w = 4;
    hm.h = 3;
    hm.z = {-2, -1, 0, 1, 2, 3, 4, 5, 6, 6, -2, 0};
    std::string err;
    CHECK(writeHeightMapPgm(path16, hm, -2.0, 6.0, &err));

    HeightMap back;
    CHECK(readHeightMapImage(path16, back, &err));
    CHECK(back.w == 4 && back.h == 3 && back.z.size() == 12);
    bool ok = back.z.size() == hm.z.size();
    for (size_t i = 0; ok && i < hm.z.size(); ++i) {
        // Read values are 0..1 (v / 65535); the expected value is the encoded fraction. One step is 1/65535.
        const double expect = (static_cast<double>(hm.z[i]) + 2.0) / 8.0;
        ok = std::fabs(back.z[i] - expect) <= 1.0 / 65535.0 + 1e-6;
    }
    CHECK(ok);

    // 8-bit binary PGM written by hand: the samples are read as v / 255.
    const std::string path8 = (dir / "terrain8.pgm").string();
    {
        std::ofstream f(path8, std::ios::binary);
        const std::string header = "P5\n# hand-written\n3 2\n255\n";
        f.write(header.data(), static_cast<std::streamsize>(header.size()));
        const unsigned char px[6] = {0, 51, 102, 153, 204, 255};
        f.write(reinterpret_cast<const char*>(px), 6);
    }
    HeightMap eight;
    CHECK(readHeightMapImage(path8, eight, &err));
    CHECK(eight.w == 3 && eight.h == 2 && eight.z.size() == 6);
    if (eight.z.size() == 6) {
        CHECK(std::fabs(eight.z[0] - 0.0) < 1e-6);
        CHECK(std::fabs(eight.z[2] - 0.4) < 1e-6);
        CHECK(std::fabs(eight.z[5] - 1.0) < 1e-6);
    }

    // Refusals: a missing file, a truncated PGM and an empty height range.
    HeightMap none;
    err.clear();
    CHECK(!readHeightMapImage((dir / "missing.pgm").string(), none, &err));
    CHECK(!err.empty());

    const std::string truncated = (dir / "short.pgm").string();
    {
        std::ofstream f(truncated, std::ios::binary);
        const std::string header = "P5\n4 4\n65535\n";
        f.write(header.data(), static_cast<std::streamsize>(header.size()));
        f.write("\0\0\0\0", 4);
    }
    CHECK(!readHeightMapImage(truncated, none, &err));
    CHECK(!writeHeightMapPgm((dir / "flat.pgm").string(), hm, 3.0, 3.0, &err));

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(heightmap_resample_sizes) {
    // 3 x 3 map to a 5 x 5 grid. Map values 0..2 land on heights 0..2 (the map's range is stretched onto 0..2).
    PatchGrid g = makeTerrainPatch(boxXY(256, 256), 5, 5, "terrain/grass");

    // Columns ramp 0,1,2 across X: grid column c samples map column c / 2, so the height is c / 2.
    HeightMap ramp;
    ramp.w = 3;
    ramp.h = 3;
    ramp.z = {0, 1, 2, 0, 1, 2, 0, 1, 2};
    CHECK(applyHeightMap(g, ramp, 0.0, 2.0));
    bool colOk = true;
    for (int r = 0; r < 5; ++r)
        for (int c = 0; c < 5; ++c) colOk = colOk && std::fabs(g.pos[r * 5 + c].z - c * 0.5) < 1e-6;
    CHECK(colOk);

    // Rows ramp too. Map row 0 is the north edge, so grid row 4 (max Y) gets 0 and grid row 0 (min Y) gets 2.
    HeightMap rows;
    rows.w = 3;
    rows.h = 3;
    rows.z = {0, 0, 0, 1, 1, 1, 2, 2, 2};
    CHECK(applyHeightMap(g, rows, 0.0, 2.0));
    bool rowOk = true;
    for (int r = 0; r < 5; ++r)
        for (int c = 0; c < 5; ++c) rowOk = rowOk && std::fabs(g.pos[r * 5 + c].z - (2.0 - 0.5 * r)) < 1e-6;
    CHECK(rowOk);

    // Downsampling a 9 x 9 map onto a 3 x 3 grid keeps the corners. Map value = c + 2 * (8 - r), so the map runs
    // from 0 at (row 8, col 0) to 24 at (row 0, col 8). Stretching 0..24 onto 0..32.
    HeightMap big;
    big.w = 9;
    big.h = 9;
    big.z.resize(81);
    for (int r = 0; r < 9; ++r)
        for (int c = 0; c < 9; ++c) big.z[r * 9 + c] = static_cast<float>(c + 2 * (8 - r));
    PatchGrid small = makeTerrainPatch(boxXY(64, 64), 3, 3, "m");
    CHECK(applyHeightMap(small, big, 0.0, 32.0));
    // Grid (row 0, col 0) is the min-X, min-Y corner: map (row 8, col 0) = 0 -> height 0.
    CHECK(std::fabs(small.pos[0].z - 0.0) < 1e-4);
    // Grid (row 2, col 2) is the max-X, max-Y corner: map (row 0, col 8) = 24 -> height 32.
    CHECK(std::fabs(small.pos[8].z - 32.0) < 1e-4);
    // Grid (row 2, col 0) is the max-Y, min-X corner: map (row 0, col 0) = 16 -> 16 / 24 * 32.
    CHECK(std::fabs(small.pos[6].z - 64.0 / 3.0) < 1e-4);

    // Bad input is refused.
    HeightMap empty;
    CHECK(!applyHeightMap(g, empty, 0.0, 1.0));
}

TEST(heightmap_thermal_erosion_reduces_slope) {
    // A single spike on a flat 9 x 9 map.
    HeightMap spike;
    spike.w = 9;
    spike.h = 9;
    spike.z.assign(81, 0.0f);
    spike.z[4 * 9 + 4] = 10.0f;
    const double before = maxSlope(spike);
    const double sum0 = sumOf(spike);
    thermalErosion(spike, 30, 0.5);
    CHECK(maxSlope(spike) < before);
    CHECK(std::fabs(sumOf(spike) - sum0) <= 1e-3 * sumAbsOf(spike) + 1e-9);

    // A rougher map with negative heights: same checks.
    HeightMap rough;
    rough.w = 12;
    rough.h = 10;
    rough.z.resize(120);
    for (int r = 0; r < 10; ++r)
        for (int c = 0; c < 12; ++c)
            rough.z[r * 12 + c] = static_cast<float>(5.0 * std::sin(1.3 * r) + 4.0 * std::cos(0.7 * c) + ((r * 7 + c * 3) % 4));
    const double slope0 = maxSlope(rough);
    const double sumAbs0 = sumAbsOf(rough);
    const double sum1 = sumOf(rough);
    thermalErosion(rough, 25, 0.8);
    CHECK(maxSlope(rough) < slope0);
    CHECK(std::fabs(sumOf(rough) - sum1) <= 1e-3 * sumAbs0);

    // Invalid input and zero iterations leave the map alone.
    HeightMap keep = spike;
    thermalErosion(keep, 0, 0.5);
    CHECK(keep.z == spike.z);
}

TEST(heightmap_hydraulic_deterministic) {
    HeightMap a;
    a.w = 16;
    a.h = 16;
    a.z.resize(256);
    for (int r = 0; r < 16; ++r)
        for (int c = 0; c < 16; ++c)
            a.z[r * 16 + c] = static_cast<float>(6.0 * std::sin(0.5 * r) + 5.0 * std::cos(0.4 * c) + 0.2 * ((r * 7 + c * 13) % 5));

    HeightMap b = a, c = a, d = a;
    hydraulicErosionSimple(b, 20, 0.05, 0.8, 1234u);
    hydraulicErosionSimple(c, 20, 0.05, 0.8, 1234u);
    hydraulicErosionSimple(d, 20, 0.05, 0.8, 99u);

    CHECK(b.z == c.z);  // same seed, same result, bit for bit
    CHECK(b.z != a.z);  // erosion did something
    CHECK(d.z != b.z);  // the seed changes the rain, so the result differs
    CHECK(std::fabs(sumOf(b) - sumOf(a)) <= 1e-3 * sumAbsOf(a));

    HeightMap keep = a;
    hydraulicErosionSimple(keep, 0, 0.05, 0.8, 1234u);
    CHECK(keep.z == a.z);
}

TEST(heightmap_slope_alpha) {
    const double slope60 = std::tan(60.0 * kPi / 180.0);
    const double slope30 = std::tan(30.0 * kPi / 180.0);

    // Flat: every control point is 0.
    PatchGrid flat = makeTerrainPatch(boxXY(256, 256), 5, 5, "m");
    const auto a0 = slopeBlendAlpha(flat, 20.0, 40.0);
    CHECK(a0.size() == 25);
    bool allZero = a0.size() == 25;
    for (float v : a0) allZero = allZero && std::fabs(v) < 1e-6f;
    CHECK(allZero);

    // Steep (60 degrees, past endDeg): every control point is 1.
    PatchGrid steep = makeTerrainPatch(boxXY(256, 256), 5, 5, "m");
    for (Vec3& p : steep.pos) p.z = slope60 * p.x;
    const auto a1 = slopeBlendAlpha(steep, 20.0, 40.0);
    bool allOne = a1.size() == 25;
    for (float v : a1) allOne = allOne && std::fabs(v - 1.0f) < 1e-6f;
    CHECK(allOne);

    // Halfway in the band (30 degrees with a 20..40 band): 0.5.
    PatchGrid mid = makeTerrainPatch(boxXY(256, 256), 5, 5, "m");
    for (Vec3& p : mid.pos) p.z = slope30 * p.x;
    const auto a2 = slopeBlendAlpha(mid, 20.0, 40.0);
    bool halfOk = a2.size() == 25;
    for (float v : a2) halfOk = halfOk && std::fabs(v - 0.5f) < 1e-4f;
    CHECK(halfOk);

    // Empty grid and a reversed band are refused.
    CHECK(slopeBlendAlpha(PatchGrid{}, 20.0, 40.0).empty());
    CHECK(slopeBlendAlpha(flat, 40.0, 20.0).empty());
}
