// Parity of a built map's lightmap with the editor's bake (plan 1.11, the offline proof): the planes the game will
// sample (C, f, D in the built zone, read by the stock lightmap reader from the decompressed zone) against the planes
// the editor decodes from its own bake (decodeLightmapTexel of the extraction's pair, which View > Game Lighting
// draws). Run by tools/lightmap_bake_build_test.py, which sets:
//   IW8R_TEST_BUILT_DEC            the decompressed built zone (<map>.ff.dec, ACTS fastfile -d)
//   IW8R_TEST_PAIR_PRIMARY / _SECONDARY   the extraction's pair files
//   IW8R_TEST_PAIR_W / _H / _LOGSCALE / _MAXIMUM   the pair's size and the world's lightmap_encoding
// Skipped without them.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "lightmap_bake.h"
#include "stock_lightmap.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

std::vector<uint8_t> readAll(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

double lumOf(const float c[3]) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; }

}  // namespace

TEST(built_lightmap_matches_the_bake) {
    const char* dec = std::getenv("IW8R_TEST_BUILT_DEC");
    const char* primary = std::getenv("IW8R_TEST_PAIR_PRIMARY");
    const char* secondary = std::getenv("IW8R_TEST_PAIR_SECONDARY");
    const char* ws = std::getenv("IW8R_TEST_PAIR_W");
    const char* hs = std::getenv("IW8R_TEST_PAIR_H");
    const char* ls = std::getenv("IW8R_TEST_PAIR_LOGSCALE");
    const char* ms = std::getenv("IW8R_TEST_PAIR_MAXIMUM");
    if (!dec || !*dec || !primary || !secondary || !ws || !hs || !ls || !ms) return;

    EncodedLightmap pair;
    pair.primary = readAll(primary);
    pair.secondary = readAll(secondary);
    pair.logScale = std::atof(ls);
    pair.maximum = std::atof(ms);
    const int w = std::atoi(ws), h = std::atoi(hs);
    CHECK(w > 0 && h > 0 && pair.primary.size() == size_t(w) * h && pair.secondary.size() == size_t(w) * h * 8);
    if (!(w > 0 && h > 0 && pair.secondary.size() == size_t(w) * h * 8)) return;

    std::vector<StockLightmap> built;
    std::string err;
    const bool read = extractStockLightmapsFromDecompressedZone(dec, built, &err);
    if (!read) std::printf("  built lightmap: %s\n", err.c_str());
    CHECK(read && built.size() == 1);
    if (!read || built.size() != 1) return;
    const StockLightmap& lm = built[0];
    std::printf("  built lightmap: %d x %d, type %d (the bake: %d x %d)\n", lm.width, lm.height, lm.type, w, h);
    CHECK(lm.width == w && lm.height == h);
    if (lm.width != w || lm.height != h) return;
    CHECK(lm.irradiance.size() == size_t(w) * h * 3 && lm.ambientFraction.size() == size_t(w) * h && lm.direction.size() == size_t(w) * h * 3);
    if (lm.irradiance.size() != size_t(w) * h * 3) return;

    // Texel by texel. Dark texels (below 2 % of the brightest) are compared by their absolute difference only: a
    // relative error means nothing there.
    double maxLum = 0;
    std::vector<float> expC(size_t(w) * h * 3), expF(size_t(w) * h), expD(size_t(w) * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * w + x;
            float f = 0;
            decodeLightmapTexel(pair, w, h, x, y, &expC[i * 3], f, &expD[i * 3]);
            expF[i] = f;
            maxLum = std::max(maxLum, lumOf(&expC[i * 3]));
        }
    CHECK(maxLum > 0);
    std::vector<double> rel;
    double sumRel = 0, sumAbsDark = 0, sumF = 0, sumAngle = 0, sumBuilt = 0, sumExp = 0;
    size_t bright = 0, dark = 0, lit = 0;
    for (size_t i = 0; i < size_t(w) * h; ++i) {
        const double e = lumOf(&expC[i * 3]), b = lumOf(&lm.irradiance[i * 3]);
        sumBuilt += b, sumExp += e;
        if (e >= 0.02 * maxLum) {
            const double r = std::fabs(b - e) / e;
            rel.push_back(r);
            sumRel += r;
            ++bright;
            sumF += std::fabs(lm.ambientFraction[i] - expF[i]);
            // The direction only matters where part of the light is directional.
            if (expF[i] < 0.9) {
                const double dotv = lm.direction[i * 3] * expD[i * 3] + lm.direction[i * 3 + 1] * expD[i * 3 + 1] + lm.direction[i * 3 + 2] * expD[i * 3 + 2];
                sumAngle += std::acos(std::clamp(dotv, -1.0, 1.0)) * 57.29577951;
                ++lit;
            }
        } else {
            sumAbsDark += std::fabs(b - e);
            ++dark;
        }
    }
    CHECK(bright > 100);
    if (rel.empty()) return;
    std::sort(rel.begin(), rel.end());
    const double meanRel = sumRel / double(bright), p99 = rel[size_t(double(rel.size() - 1) * 0.99)];
    const double meanF = sumF / double(bright), meanAngle = lit ? sumAngle / double(lit) : 0.0;
    const double darkAbs = dark ? sumAbsDark / double(dark) / maxLum : 0.0;
    std::printf("  built lightmap parity: %zu lit texels, light differs by %.2f %% on average (99 %% of them within %.1f %%), total light "
                "%.2f %% apart, ambient share %.3f apart, direction %.1f degrees apart on %zu texels, dark texels %.3f %% of the brightest apart\n",
                bright, meanRel * 100.0, p99 * 100.0, std::fabs(sumBuilt - sumExp) / sumExp * 100.0, meanF, meanAngle, lit, darkAbs * 100.0);
    // The plan's bar was +-10 % per region; texel by texel the stored formats (11 and 10 bit floats, block compressed
    // share and direction) stay well inside it.
    CHECK(meanRel < 0.03 && p99 < 0.10);
    CHECK(std::fabs(sumBuilt - sumExp) / sumExp < 0.02);
    CHECK(meanF < 0.05);
    CHECK(meanAngle < 6.0);
    CHECK(darkAbs < 0.01);
}
