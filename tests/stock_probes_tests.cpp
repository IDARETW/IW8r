// Stock reflection probes (src/core/stock_probes.h): BC6H decode on hand-made blocks, record parsing on a small
// synthetic gfx_map JSON, the array image search in a synthetic decompressed zone (level-major slices, chunk
// boundaries) and the cache file round trip.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "stock_lightmap.h"
#include "stock_probes.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

namespace fs = std::filesystem;

void setBits(std::array<uint8_t, 16>& b, int at, int count, uint32_t v) {
    for (int i = 0; i < count; ++i)
        if (v >> i & 1u) b[size_t((at + i) / 8)] |= uint8_t(1u << ((at + i) % 8));
}

// BC6H mode 11 (5-bit mode 00011): one region, endpoints A = (r, g, b) and B as 10-bit values at bits 5..64,
// 4-bit indices from bit 65 (texel 0 has 3 bits, its top bit implied 0). `lastIndex` is texel 15's index; every
// other texel uses index 0.
std::array<uint8_t, 16> bc6hMode11(uint32_t a, uint32_t b, uint32_t lastIndex) {
    std::array<uint8_t, 16> blk{};
    setBits(blk, 0, 5, 3);
    for (int c = 0; c < 3; ++c) setBits(blk, 5 + 10 * c, 10, a);
    for (int c = 0; c < 3; ++c) setBits(blk, 35 + 10 * c, 10, b);
    setBits(blk, 65 + 3 + 14 * 4, 4, lastIndex);  // texel 15
    return blk;
}

// The BC6H unsigned decode of one 10-bit endpoint (no interpolation): unquantise, then * 31 / 64.
uint16_t bc6hUnsigned10(uint32_t v) {
    const uint32_t unq = v == 0 ? 0u : v == 1023 ? 0xFFFFu : ((v << 16) + 0x8000u) >> 10;
    return uint16_t((unq * 31u) >> 6);
}

void put16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
    b[at] = uint8_t(v);
    b[at + 1] = uint8_t(v >> 8);
}
void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = uint8_t(v >> (8 * i));
}
void put64(std::vector<uint8_t>& b, size_t at, uint64_t v) {
    put32(b, at, uint32_t(v));
    put32(b, at + 4, uint32_t(v >> 32));
}

const char* kProbeJson = R"({"asset":{"fields":{"draw":{"reflectionProbeData":{
 "reflectionProbeCount": 2, "sharedReflectionProbeCount": 0,
 "reflectionProbeImages": null, "reflectionProbeImages_octahedron": null,
 "reflectionProbes": {"type": "GfxReflectionProbe", "stride": 48, "count": 2, "values": [
  {"livePath": {"string": "_e1_p2"}, "origin": {"$union_views": true, "v": [10.0, 20.0, 30.0]},
   "angles": {"v": [0.0, 90.0, 0.0]}, "probeInstances": {"count": 2, "values": [0, 1]}, "probeInstanceCount": 2,
   "probeRelightingIndex": 65535, "probeImageIndirection": 0},
  {"livePath": {"string": "_e2_p2 {brace}"}, "origin": {"v": [-5.0, 0.0, 100.0]}, "angles": {"v": [0.0, 0.0, 0.0]},
   "probeInstances": {"count": 1, "values": [2]}, "probeInstanceCount": 1, "probeRelightingIndex": 3,
   "probeImageIndirection": 0}]},
 "reflectionProbeArrayImage": null,
 "reflectionProbeArrayImage_octahedron": {"type": "GfxImage", "asset_reference": true, "name": "*reflection_probe_array_octahedron"},
 "reflectionProbeInstanceCount": 3,
 "reflectionProbeInstances": {"count": 3, "values": [
  {"probePosition": {"v": [10.0, 20.0, 30.0]}, "probeImageIndex": 0, "lodLevel": 0, "flags": 1,
   "probeRotation": {"v": [0.0, 0.0, 0.0, 1.0]},
   "volumeObb": {"center": {"v": [10.0, 20.0, 30.0]}, "xAxis": {"v": [1.0, 0.0, 0.0]}, "yAxis": {"v": [0.0, 1.0, 0.0]},
                 "zAxis": {"v": [0.0, 0.0, 1.0]}, "halfSize": {"v": [131072.0, 131072.0, 131072.0]}},
   "priority": -3.4028234663852886e+38, "feather": {"v": [8.0, 8.0, 8.0]},
   "expandProjectionNeg": {"v": [0.0, 0.0, 0.0]}, "expandProjectionPos": {"v": [0.0, 0.0, 0.0]}},
  {"probePosition": {"v": [10.0, 20.0, 30.0]}, "probeImageIndex": 0, "lodLevel": 0, "flags": 0,
   "probeRotation": {"v": [0.0, 0.0, 0.0, 1.0]},
   "volumeObb": {"center": {"v": [0.0, 0.0, 50.0]}, "xAxis": {"v": [0.0, -1.0, 0.0]}, "yAxis": {"v": [0.0, 0.0, -1.0]},
                 "zAxis": {"v": [1.0, 0.0, 0.0]}, "halfSize": {"v": [60.0, 176.0, 144.0]}},
   "priority": 10.0, "feather": {"v": [8.0, 8.0, 8.0]},
   "expandProjectionNeg": {"v": [1.0, 2.0, 3.0]}, "expandProjectionPos": {"v": [4.0, 5.0, 6.0]}},
  {"probePosition": {"v": [-5.0, 0.0, 100.0]}, "probeImageIndex": 1, "lodLevel": 0, "flags": 0,
   "probeRotation": {"v": [0.0, 0.0, 0.0, 1.0]},
   "volumeObb": {"center": {"v": [-5.0, 0.0, 100.0]}, "xAxis": {"v": [1.0, 0.0, 0.0]}, "yAxis": {"v": [0.0, 1.0, 0.0]},
                 "zAxis": {"v": [0.0, 0.0, 1.0]}, "halfSize": {"v": [10.0, 20.0, 30.0]}},
   "priority": 11.0, "feather": {"v": [8.0, 4.0, 2.0]},
   "expandProjectionNeg": {"v": [0.0, 0.0, 0.0]}, "expandProjectionPos": {"v": [0.0, 0.0, 0.0]}}]},
 "reflectionProbeSH": {"count": 8, "values": [
  {"coeffs": [1,2,3,4,5,6,7,8,9]}, {"coeffs": [0,0,0,0,0,0,0,0,0]}, {"coeffs": [0,0,0,0,0,0,0,0,0]}, {"coeffs": [0,0,0,0,0,0,0,0,0.5]},
  {"coeffs": [-1,0,0,0,0,0,0,0,0]}, {"coeffs": [0,0,0,0,0,0,0,0,0]}, {"coeffs": [0,0,0,0,0,0,0,0,0]}, {"coeffs": [0,0,0,0,0,0,0,0,2]}]}
},"other": {"x": 1}}}}})";

// A synthetic decompressed zone: junk, then the GfxImage (0xE8 bytes), its name and the level-major pixels of
// `count` probes of size 8 with 2 levels (64 + 16 bytes per probe). Probe i level m is filled with the mode-11
// block of endpoint 100 * (i + 1) + 10 * m.
std::vector<uint8_t> syntheticZone(int count, size_t junk, uint64_t* imageAt) {
    std::vector<uint8_t> z(junk, 0x5A);
    // A decoy: the name without a valid header in front of it.
    const std::string name = "*reflection_probe_array_octahedron";
    z.insert(z.end(), name.begin(), name.end());
    z.push_back(0);
    *imageAt = z.size();
    std::vector<uint8_t> h(0xE8, 0);
    put64(h, 0, 0xFFFFFFFFFFFFFFFEull);
    put32(h, 0x14, kProbeFormatBC6H);
    put32(h, 0x18, 0x20001);
    put32(h, 0x1C, uint32_t(80 * count));
    put16(h, 0x24, 8);
    put16(h, 0x26, 8);
    put16(h, 0x28, 1);
    put16(h, 0x2A, uint16_t(count));
    h[0x2E] = 1;
    h[0x2F] = 2;
    h[0x30] = 2;
    put64(h, 0xE0, 0xFFFFFFFFFFFFFFFEull);
    z.insert(z.end(), h.begin(), h.end());
    z.insert(z.end(), name.begin(), name.end());
    z.push_back(0);
    for (int m = 0; m < 2; ++m)
        for (int i = 0; i < count; ++i) {
            const auto blk = bc6hMode11(uint32_t(100 * (i + 1) + 10 * m), 1023, 15);
            for (int k = 0; k < (m == 0 ? 4 : 1); ++k) z.insert(z.end(), blk.begin(), blk.end());
        }
    z.insert(z.end(), 300, 0x11);
    return z;
}

}  // namespace

TEST(stock_probes_bc6h_decode) {
    const uint16_t a = bc6hUnsigned10(512), b = bc6hUnsigned10(1023);
    CHECK(a == 0x3E0F);  // 512: unquantised 32800, * 31 / 64 = 15887
    CHECK(b == 0x7BFF);  // 1023: 0xFFFF * 31 / 64 = 31743, the largest finite half (65504)
    const auto blk = bc6hMode11(512, 1023, 15);
    std::vector<uint16_t> out;
    CHECK(decodeBC6H(blk.data(), blk.size(), 4, 4, false, out));
    CHECK(out.size() == 48);
    if (out.size() == 48) {
        for (int c = 0; c < 3; ++c) CHECK(out[size_t(c)] == a);           // texel 0: index 0 = endpoint A
        CHECK(out[3 * 14] == a);                                           // texel 14
        for (int c = 0; c < 3; ++c) CHECK(out[size_t(45 + c)] == b);      // texel 15: index 15 = endpoint B
        CHECK(std::fabs(halfToFloat(out[0]) - 1.5146484f) < 1e-6f);
        CHECK(halfToFloat(out[45]) == 65504.0f);
    }
    // Index 8 of 15: weight 34 / 64 between A = 0 and B = 1023 (unquantised 0 and 0xFFFF).
    const auto mid = bc6hMode11(0, 1023, 8);
    CHECK(decodeBC6H(mid.data(), mid.size(), 4, 4, false, out));
    CHECK(out[0] == 0);
    CHECK(out[45] == uint16_t(((uint32_t(0xFFFF) * 34u + 32u) >> 6) * 31u >> 6));
    // Partial blocks and short input.
    CHECK(decodeBC6H(blk.data(), blk.size(), 3, 2, false, out) && out.size() == 18 && out[0] == a);
    CHECK(!decodeBC6H(blk.data(), 15, 4, 4, false, out));
    CHECK(bc6hBytesPerProbe(256, 6) == 87360);
    CHECK(bc6hBytesPerProbe(8, 2) == 80);
}

TEST(stock_probes_record_parse) {
    StockProbeSet set;
    std::string err;
    CHECK(parseStockProbeRecords(kProbeJson, set, &err));
    CHECK(err.empty());
    CHECK(set.probes.size() == 2);
    CHECK(set.boxes.size() == 3);
    CHECK(set.arrayImageName == "*reflection_probe_array_octahedron");
    if (set.probes.size() == 2 && set.boxes.size() == 3) {
        const StockProbe& p0 = set.probes[0];
        CHECK(p0.origin == (ProbeVec3{10, 20, 30}));
        CHECK(p0.angles == (ProbeVec3{0, 90, 0}));
        CHECK(p0.relightingIndex == -1);
        CHECK(p0.boxes == (std::vector<int>{0, 1}));
        CHECK(p0.sh[0][8] == 9.0f && p0.sh[3][8] == 0.5f);
        // Its AABB skips the fallback box: box 1 rotated, x axis = -Y (60), y axis = -Z (176), z axis = +X (144).
        CHECK(p0.mins == (ProbeVec3{-144, -60, -126}));
        CHECK(p0.maxs == (ProbeVec3{144, 60, 226}));
        const StockProbe& p1 = set.probes[1];
        CHECK(p1.relightingIndex == 3);
        CHECK(p1.boxes == (std::vector<int>{2}));
        CHECK(p1.sh[0][0] == -1.0f && p1.sh[3][8] == 2.0f);
        CHECK(p1.mins == (ProbeVec3{-15, -20, 70}));
        CHECK(stockProbeBoxIsFallback(set.boxes[0]));
        CHECK(!stockProbeBoxIsFallback(set.boxes[1]));
        CHECK(set.boxes[0].priority == -std::numeric_limits<float>::max());
        CHECK(set.boxes[1].axes[0] == (ProbeVec3{0, -1, 0}));
        CHECK(set.boxes[1].expandNeg == (ProbeVec3{1, 2, 3}) && set.boxes[1].expandPos == (ProbeVec3{4, 5, 6}));
        CHECK(set.boxes[2].feather == (ProbeVec3{8, 4, 2}) && set.boxes[2].imageIndex == 1 && set.boxes[2].priority == 11.0f);
    }
    // Streamed from a file, with the object past a small chunk.
    const fs::path dir = fs::path(iw8r::test::tmpDir()) / "stock_probes";
    fs::create_directories(dir);
    const fs::path jf = dir / "gfx_map.json";
    {
        std::ofstream o(jf, std::ios::binary);
        o << std::string(100, ' ') << kProbeJson;
    }
    StockProbeSet fromFile;
    CHECK(readStockProbeRecords(jf.string(), fromFile, &err));
    CHECK(fromFile.probes.size() == 2 && fromFile.boxes.size() == 3);
    // Bad inputs.
    CHECK(!parseStockProbeRecords("{\"x\":1}", set, &err));
    std::string bad = kProbeJson;
    bad.replace(bad.find("\"reflectionProbeCount\": 2"), 25, "\"reflectionProbeCount\": 3");
    CHECK(!parseStockProbeRecords(bad, set, &err));
    std::string badIdx = kProbeJson;
    badIdx.replace(badIdx.find("\"values\": [2]"), 13, "\"values\": [9]");
    CHECK(!parseStockProbeRecords(badIdx, set, &err));
}

TEST(stock_probes_images_and_cache) {
    const fs::path dir = fs::path(iw8r::test::tmpDir()) / "stock_probes";
    fs::create_directories(dir);
    StockProbeSet set;
    std::string err;
    CHECK(parseStockProbeRecords(kProbeJson, set, &err));
    for (size_t junk : {size_t(0), size_t(37), size_t(1000)}) {
        uint64_t at = 0, found = 0;
        const auto zone = syntheticZone(2, junk, &at);
        const fs::path dec = dir / "zone.ff.dec";
        {
            std::ofstream o(dec, std::ios::binary);
            o.write(reinterpret_cast<const char*>(zone.data()), std::streamsize(zone.size()));
        }
        for (size_t chunk : {size_t(0), size_t(64), size_t(97), size_t(4096)}) {
            StockProbeSet s = set;
            CHECK(extractStockProbeImages(dec.string(), s, &err, &found, chunk));
            CHECK(found == at);
            CHECK(s.size == 8 && s.mipCount == 2);
            CHECK(decodeStockProbeImages(s, &err));
            // Level-major in the zone: probe i level m has endpoint 100 (i + 1) + 10 m.
            for (int i = 0; i < 2; ++i)
                for (int m = 0; m < 2; ++m) {
                    const ProbeVec3 c = stockProbeTexel(s.probes[size_t(i)], 8, m, 0, 0);
                    CHECK(c[0] == halfToFloat(bc6hUnsigned10(uint32_t(100 * (i + 1) + 10 * m))));
                    CHECK(stockProbeTexel(s.probes[size_t(i)], 8, m, 3, 3)[1] == 65504.0f);
                }
            CHECK(s.probes[1].mips.size() == 2 && s.probes[1].mips[1].size() == 4 * 4 * 3);
        }
        // A probe count that does not match the slices is refused.
        StockProbeSet one = set;
        one.probes.pop_back();
        CHECK(!extractStockProbeImages(dec.string(), one, &err));
        CHECK(err.find("slices") != std::string::npos);
    }

    // Cache round trip.
    uint64_t at = 0;
    const auto zone = syntheticZone(2, 5, &at);
    const fs::path dec = dir / "zone2.ff.dec";
    {
        std::ofstream o(dec, std::ios::binary);
        o.write(reinterpret_cast<const char*>(zone.data()), std::streamsize(zone.size()));
    }
    StockProbeSet s = set;
    CHECK(extractStockProbeImages(dec.string(), s, &err));
    CHECK(decodeStockProbeImages(s, &err));
    const fs::path cache = dir / "probes.iw8rprb";
    CHECK(writeStockProbeCache(cache.string(), s, &err));
    CHECK(fs::file_size(cache) == 32 + 2 * 208 + 3 * 4 + 3 * 140 + 2 * 80);
    StockProbeSet back;
    CHECK(readStockProbeCache(cache.string(), back, &err));
    CHECK(back.size == 8 && back.mipCount == 2 && back.probes.size() == 2 && back.boxes.size() == 3);
    if (back.probes.size() == 2 && back.boxes.size() == 3) {
        for (size_t i = 0; i < 2; ++i) {
            CHECK(back.probes[i].origin == s.probes[i].origin && back.probes[i].angles == s.probes[i].angles);
            CHECK(back.probes[i].mins == s.probes[i].mins && back.probes[i].maxs == s.probes[i].maxs);
            CHECK(back.probes[i].relightingIndex == s.probes[i].relightingIndex);
            CHECK(back.probes[i].sh == s.probes[i].sh);
            CHECK(back.probes[i].boxes == s.probes[i].boxes);
            CHECK(back.probes[i].bc6h == s.probes[i].bc6h);
            CHECK(back.probes[i].mips == s.probes[i].mips);
        }
        for (size_t b = 0; b < 3; ++b) {
            const StockProbeBox &x = back.boxes[b], &y = s.boxes[b];
            CHECK(x.center == y.center && x.axes == y.axes && x.halfSize == y.halfSize && x.priority == y.priority &&
                  x.feather == y.feather && x.expandNeg == y.expandNeg && x.expandPos == y.expandPos &&
                  x.position == y.position && x.rotation == y.rotation && x.imageIndex == y.imageIndex && x.flags == y.flags);
        }
    }
    StockProbeSet raw;
    CHECK(readStockProbeCache(cache.string(), raw, &err, false));
    CHECK(raw.probes.size() == 2 && raw.probes[0].mips.empty() && !raw.probes[0].bc6h.empty());
    // Damaged files are refused.
    {
        std::ofstream o(cache, std::ios::binary | std::ios::app);
        o.put('x');
    }
    CHECK(!readStockProbeCache(cache.string(), back, &err));
    {
        std::ofstream o(cache, std::ios::binary | std::ios::trunc);
        o << "IW8RLM";
    }
    CHECK(!readStockProbeCache(cache.string(), back, &err));
    // A set without images cannot be written.
    StockProbeSet empty = set;
    CHECK(!writeStockProbeCache((dir / "none.iw8rprb").string(), empty, &err));
}
