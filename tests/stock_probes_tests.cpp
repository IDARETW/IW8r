// Stock reflection probes (src/core/stock_probes.h): BC6H decode on hand-made blocks, record parsing on a small
// synthetic gfx_map JSON, the array image search in a synthetic decompressed zone (level-major slices, chunk
// boundaries) and the cache file round trip. Warzone (streamed) layout, research 108: indirection decode, records
// without an array image (hex SH, more than 4096 probes), transient zone exports, the bucket needle and level-major
// split in synthetic decompressed zones, shared-probe DDS, cache v2 (main and tile files, merge) and the camera-local
// subset; on real mp_donetsk data when this machine has it.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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

// ---------------------------------------------------------------------------------------------------------------
// Warzone (streamed) layout: docs/research/108_warzone_reflection_probes.md section 4.

namespace {

constexpr size_t kPer = 87360;  // one 256 x 256, 6-level BC6H probe image
constexpr size_t kLevel[6] = {65536, 16384, 4096, 1024, 256, 64};

std::string hexFloats(const std::vector<float>& v) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (float f : v) {
        uint32_t x;
        std::memcpy(&x, &f, 4);
        for (int i = 0; i < 4; ++i) {
            const uint8_t b = uint8_t(x >> (8 * i));
            s += d[b >> 4];
            s += d[b & 15];
        }
    }
    return s;
}

std::string fnum(double v) {
    char b[64];
    std::snprintf(b, sizeof b, "%.17g", v);
    return b;
}
std::string v3json(double x, double y, double z) { return "{\"$union_views\": true, \"v\": [" + fnum(x) + ", " + fnum(y) + ", " + fnum(z) + "]}"; }

struct SynthProbe {
    uint32_t ind;
    std::vector<int> boxes;
};
struct SynthBox {
    int owner;
    double cx, cy, cz, half, priority;
    int lod, flags;
};

// A streamed set (research 108 section 1.1): no array image, 2 shared probes (0, 7), zone 0 = probes 1-3 (buckets
// [2, 1]; probe 2 owns the fallback box 0), zone 7 = probes 4-6 (buckets [2, 1]).
const std::vector<SynthProbe> kSynthProbes = {
    {0xFFF00000u, {1}}, {0x00000000u, {2}}, {0x00000001u, {0}}, {0x00010000u, {3}},
    {0x00700000u, {4}}, {0x00700001u, {5, 6}}, {0x00710000u, {7}}, {0xFFF00001u, {8}}};
const std::vector<SynthBox> kSynthBoxes = {
    {2, 0, 0, 0, 524288, -3.4028234663852886e+38, 0, 1},
    {0, 1000, 0, 0, 100, 0, 0, 0},
    {1, 0, 1000, 0, 100, 1, 0, 0},
    {3, 0, 0, 0, 40000, 0, 2, 1},
    {4, 200, 0, 0, 50, -1, 0, 0},
    {5, -300, 0, 0, 50, 2, 0, 0},
    {5, 5000, 5000, 0, 50, 2, 0, 0},
    {6, 0, -250, 0, 50, 0.5, 0, 0},
    {7, 0, 0, 0, 700, 0, 1, 1}};

float synthSh(size_t probe, size_t k, size_t j) { return float(probe) * 100.0f + float(k) * 10.0f + float(j) + 0.25f; }

std::string streamedJson(const std::vector<SynthProbe>& probes, const std::vector<SynthBox>& boxes, int sharedCount, int names,
                         bool shHex, int countOverride = -1) {
    std::string s = "{\"asset\":{\"fields\":{\"draw\":{\"reflectionProbeData\":{\n \"reflectionProbeCount\": " +
                    std::to_string(countOverride >= 0 ? countOverride : int(probes.size())) +
                    ", \"sharedReflectionProbeCount\": " + std::to_string(sharedCount) +
                    ", \"reflectionProbeImages\": null,\n \"reflectionProbeImages_octahedron\": ";
    if (names <= 0) {
        s += "null";
    } else {
        s += "{\"type\": \"GfxImage *\", \"count\": " + std::to_string(names) + ", \"values\": [";
        int n = 0;
        for (size_t i = 0; i < probes.size() && n < names; ++i)
            if (probes[i].ind >> 20 == 0xFFF) {
                s += std::string(n ? ", " : "") + "{\"type\": \"GfxImage\", \"asset_reference\": true, \"name\": \"*reflection_probe_octahedron_" +
                     std::to_string(i) + "\"}";
                ++n;
            }
        for (; n < names; ++n) s += ", {\"name\": \"*extra\"}";
        s += "]}";
    }
    s += ",\n \"reflectionProbes\": {\"count\": " + std::to_string(probes.size()) + ", \"values\": [";
    for (size_t i = 0; i < probes.size(); ++i) {
        const SynthProbe& p = probes[i];
        const SynthBox& b = boxes[size_t(p.boxes[0])];
        s += std::string(i ? ",\n  " : "\n  ") + "{\"livePath\": {\"string\": \"_e" + std::to_string(i) + "\"}, \"origin\": " +
             v3json(b.cx, b.cy, b.cz) + ", \"angles\": " + v3json(0, 45, 0) + ", \"probeInstances\": {\"count\": " +
             std::to_string(p.boxes.size()) + ", \"values\": [";
        for (size_t k = 0; k < p.boxes.size(); ++k) s += (k ? ", " : "") + std::to_string(p.boxes[k]);
        s += "]}, \"probeInstanceCount\": " + std::to_string(p.boxes.size()) +
             ", \"probeRelightingIndex\": 65535, \"probeImageIndirection\": " + std::to_string(p.ind) + "}";
    }
    s += "]},\n \"reflectionProbeArrayImage\": null, \"reflectionProbeArrayImage_octahedron\": null,\n \"reflectionProbeInstanceCount\": " +
         std::to_string(boxes.size()) + ", \"reflectionProbeInstances\": {\"count\": " + std::to_string(boxes.size()) + ", \"values\": [";
    for (size_t i = 0; i < boxes.size(); ++i) {
        const SynthBox& b = boxes[i];
        s += std::string(i ? ",\n  " : "\n  ") + "{\"probePosition\": " + v3json(b.cx, b.cy, b.cz) +
             ", \"probeImageIndex\": " + std::to_string(b.owner) + ", \"lodLevel\": " + std::to_string(b.lod) +
             ", \"flags\": " + std::to_string(b.flags) + ", \"probeRotation\": {\"v\": [0.0, 0.0, 0.0, 1.0]}, \"volumeObb\": {\"center\": " +
             v3json(b.cx, b.cy, b.cz) + ", \"xAxis\": " + v3json(1, 0, 0) + ", \"yAxis\": " + v3json(0, 1, 0) +
             ", \"zAxis\": " + v3json(0, 0, 1) + ", \"halfSize\": " + v3json(b.half, b.half, b.half) + "}, \"priority\": " +
             fnum(b.priority) + ", \"feather\": " + v3json(3, 3, 3) + ", \"expandProjectionNeg\": " + v3json(0, 0, 0) +
             ", \"expandProjectionPos\": " + v3json(0, 0, 0) + "}";
    }
    s += "]},\n \"reflectionProbeSH\": ";
    if (shHex) {
        std::vector<float> f;
        for (size_t i = 0; i < probes.size(); ++i)
            for (size_t k = 0; k < 4; ++k)
                for (size_t j = 0; j < 9; ++j) f.push_back(synthSh(i, k, j));
        s += "{\"type\": \"GfxSH3Vec\", \"stride\": 36, \"count\": " + std::to_string(4 * probes.size()) +
             ", \"encoding\": \"hex-little-endian\", \"bytes\": \"" + hexFloats(f) + "\"}";
    } else {
        s += "null";
    }
    s += "\n}}}}}";
    return s;
}

std::string synthStreamedJson() { return streamedJson(kSynthProbes, kSynthBoxes, 2, 2, true); }

// The trzone export of a transient zone ("pixels" hex included, as ACTS writes it; the reader must not need it).
std::string trzoneJson(int zone, const std::vector<int>& instances, const std::vector<int>& buckets, int exclusive) {
    std::string s = "{\"format\":\"mw19-asset-json\",\"pool\":\"gfx_map_trzone\",\"asset\":{\"type\":\"GfxWorldTransientZone\","
                    "\"fields\":{\"name\":{\"string\":\"mp_x_" + std::to_string(zone) + "_tr\"},\"transientZoneIndex\":" +
                    std::to_string(zone) + ",\"drawVerts\":{\"posData\":{\"count\":4,\"bytes\":\"00112233\"}},\"reflectionProbes\":"
                    "{\"type\":\"GfxReflectionProbeTransientData\",\"stride\":24,\"count\":1,\"values\":[{\"reflectionProbeInstanceCount\":" +
                    std::to_string(instances.size()) + ",\"exclusiveProbeBucketCount\":" + std::to_string(buckets.size()) +
                    ",\"exclusiveProbeCount\":" + std::to_string(exclusive) + ",\"reflectionProbeInstances\":";
    if (instances.empty()) {
        s += "null";
    } else {
        s += "{\"type\":\"unsigned __int16\",\"count\":" + std::to_string(instances.size()) + ",\"values\":[";
        for (size_t k = 0; k < instances.size(); ++k) s += (k ? "," : "") + std::to_string(instances[k]);
        s += "]}";
    }
    s += ",\"exclusiveProbeBuckets\":";
    if (buckets.empty()) {
        s += "null";
    } else {
        s += "{\"type\":\"GfxImageReflectionProbe\",\"count\":" + std::to_string(buckets.size()) + ",\"values\":[";
        for (size_t b = 0; b < buckets.size(); ++b)
            s += std::string(b ? "," : "") + "{\"totalSize\":" + std::to_string(size_t(buckets[b]) * kPer) +
                 ",\"elementCount\":" + std::to_string(buckets[b]) +
                 ",\"textureId\":{\"value\":0,\"name\":\"NULLID\"},\"pixels\":{\"count\":8,\"bytes\":\"0011223344556677\"}}";
        s += "]}";
    }
    s += "}]},\"collisionData\":{\"x\":1}}}}";
    return s;
}

// Byte k of level m of slice e of bucket b of zone z in a synthetic decompressed zone.
uint8_t pixelTag(uint32_t z, size_t b, size_t e, size_t m, size_t k) { return uint8_t(z * 31 + b * 17 + e * 7 + m * 3 + k * 13 + (k >> 8)); }

// The needle of research 108 section 4.2, written independently of the reader.
std::vector<uint8_t> transientHeader(const StockProbeTransientZone& tz, uint64_t firstPtr = 0xFFFFFFFFFFFFFFFEull) {
    std::vector<uint8_t> h(24 + 2 * tz.instances.size() + 24 * tz.bucketCounts.size(), 0);
    put16(h, 0, uint16_t(tz.instances.size()));
    put16(h, 2, uint16_t(tz.bucketCounts.size()));
    put32(h, 4, tz.exclusiveCount);
    put64(h, 8, firstPtr);
    put64(h, 16, 0xFFFFFFFFFFFFFFFEull);
    for (size_t k = 0; k < tz.instances.size(); ++k) put16(h, 24 + 2 * k, tz.instances[k]);
    for (size_t b = 0; b < tz.bucketCounts.size(); ++b) {
        const size_t at = 24 + 2 * tz.instances.size() + 24 * b;
        put32(h, at, uint32_t(tz.bucketCounts[b] * kPer));
        put32(h, at + 4, tz.bucketCounts[b]);
        put64(h, at + 16, 0xFFFFFFFFFFFFFFFEull);
    }
    return h;
}

// A synthetic decompressed zone: junk, a decoy header (instances pointer 0), then the transient data and its
// buckets level-major (pixelTag), `copies` times, then junk.
std::vector<uint8_t> transientDec(const StockProbeTransientZone& tz, size_t junk, int copies, uint64_t* at) {
    std::vector<uint8_t> z(junk, 0x5A);
    const auto decoy = transientHeader(tz, 0);
    z.insert(z.end(), decoy.begin(), decoy.end());
    z.insert(z.end(), 33, 0x00);
    for (int c = 0; c < copies; ++c) {
        if (c == 0) *at = z.size();
        const auto h = transientHeader(tz);
        z.insert(z.end(), h.begin(), h.end());
        for (size_t b = 0; b < tz.bucketCounts.size(); ++b)
            for (size_t m = 0; m < 6; ++m)
                for (size_t e = 0; e < tz.bucketCounts[b]; ++e)
                    for (size_t k = 0; k < kLevel[m]; ++k) z.push_back(pixelTag(uint32_t(tz.zoneIndex), b, e, m, k));
    }
    z.insert(z.end(), 101, 0x11);
    return z;
}

// What a probe's image (level 0 first) should hold after the split.
std::vector<uint8_t> expectedImage(uint32_t indirection) {
    std::vector<uint8_t> v;
    for (size_t m = 0; m < 6; ++m)
        for (size_t k = 0; k < kLevel[m]; ++k)
            v.push_back(pixelTag(stockProbeZone(indirection), stockProbeBucket(indirection), stockProbeElement(indirection), m, k));
    return v;
}

void writeFile(const fs::path& p, const std::vector<uint8_t>& b) {
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    o.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}
void writeText(const fs::path& p, const std::string& s) {
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    o << s;
}
bool readFileBytes(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

StockProbeTransientZone synthZone0() {
    StockProbeTransientZone tz;
    tz.zoneIndex = 0;
    tz.hasProbes = true;
    tz.exclusiveCount = 3;
    tz.instances = {0, 2, 3};
    tz.bucketCounts = {2, 1};
    return tz;
}
StockProbeTransientZone synthZone7() {
    StockProbeTransientZone tz;
    tz.zoneIndex = 7;
    tz.hasProbes = true;
    tz.exclusiveCount = 3;
    tz.instances = {1, 4, 5, 6, 7};
    tz.bucketCounts = {2, 1};
    return tz;
}

// A DDS as ACTS writes a shared probe (research 108 section 1.2): DX10 header, DXGI 95, 256 x 256, 6 mips.
std::vector<uint8_t> probeDds(uint8_t fill) {
    std::vector<uint8_t> d(148 + kPer, fill);
    std::fill(d.begin(), d.begin() + 148, uint8_t(0));
    std::memcpy(d.data(), "DDS ", 4);
    put32(d, 4, 124);
    put32(d, 8, 0x000A1007);
    put32(d, 12, 256);
    put32(d, 16, 256);
    put32(d, 20, 65536);
    put32(d, 28, 6);
    put32(d, 76, 32);
    put32(d, 80, 4);
    std::memcpy(d.data() + 84, "DX10", 4);
    put32(d, 108, 0x401008);
    put32(d, 128, 95);
    put32(d, 132, 3);
    put32(d, 140, 1);
    return d;
}

// The octahedral gutter test (research 108 section 1.4): rows r < 4 repeat row 7 - r mirrored; relative error.
double gutterError(const std::vector<uint8_t>& bc6h) {
    std::vector<uint16_t> img;
    if (!decodeBC6H(bc6h.data(), kLevel[0], 256, 256, false, img)) return 1e9;
    double e = 0, m = 0;
    for (int r = 0; r < 4; ++r)
        for (int x = 4; x < 252; ++x)
            for (int c = 0; c < 3; ++c) {
                const double a = halfToFloat(img[(size_t(r) * 256 + size_t(x)) * 3 + size_t(c)]);
                const double b = halfToFloat(img[(size_t(7 - r) * 256 + size_t(255 - x)) * 3 + size_t(c)]);
                e += std::fabs(a - b);
                m += std::fabs(b);
            }
    return m > 0 ? e / m : 1e9;
}

// <repo>/<rel>, found by walking up from the working directory (empty when not found).
fs::path repoPath(const std::string& rel) {
    fs::path p = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        std::error_code ec;
        if (fs::exists(p / fs::path(rel), ec)) return p / fs::path(rel);
        if (!p.has_parent_path() || p.parent_path() == p) break;
        p = p.parent_path();
    }
    return {};
}

fs::path worldCacheDir() {
    const char* local = std::getenv("LOCALAPPDATA");
    return local ? fs::path(local) / "IW8 Radiant" / "IW8 Radiant" / "world_cache" : fs::path();
}

}  // namespace

TEST(stock_probes_indirection) {
    CHECK(stockProbeZone(0x1) == 0 && stockProbeBucket(0x1) == 0 && stockProbeElement(0x1) == 1);
    CHECK(stockProbeIsShared(0xFFF00739u) && stockProbeElement(0xFFF00739u) == 0x739 && stockProbeBucket(0xFFF00739u) == 0);
    CHECK(stockProbeZone(0x00A31002u) == 10 && stockProbeBucket(0x00A31002u) == 3 && stockProbeElement(0x00A31002u) == 0x1002);
    CHECK(stockProbeZone(0x12840005u) == 0x128 && stockProbeBucket(0x12840005u) == 4 && !stockProbeIsShared(0x12840005u));
    CHECK(stockProbeZone(0) == 0 && !stockProbeIsShared(0));
}

TEST(stock_probes_streamed_records) {
    StockProbeSet set;
    std::string err;
    CHECK(parseStockProbeRecords(synthStreamedJson(), set, &err));
    CHECK(err.empty());
    CHECK(set.streamed && set.arrayImageName.empty() && set.sharedCount == 2);
    CHECK(set.probes.size() == 8 && set.boxes.size() == 9);
    CHECK(set.sharedImageNames == (std::vector<std::string>{"*reflection_probe_octahedron_0", "*reflection_probe_octahedron_7"}));
    if (set.probes.size() == 8 && set.boxes.size() == 9) {
        // Indirections past 2^24 survive exactly (they are not read through a float).
        for (size_t i = 0; i < 8; ++i) CHECK(set.probes[i].imageIndirection == kSynthProbes[i].ind);
        CHECK(set.probes[7].imageIndirection == 0xFFF00001u);
        // SH from hex bytes.
        bool shOk = true;
        for (size_t i = 0; i < 8; ++i)
            for (size_t k = 0; k < 4; ++k)
                for (size_t j = 0; j < 9; ++j) shOk = shOk && set.probes[i].sh[k][j] == synthSh(i, k, j);
        CHECK(shOk);
        CHECK(set.probes[5].boxes == (std::vector<int>{5, 6}));
        CHECK(stockProbeBoxIsFallback(set.boxes[0]) && set.boxes[0].imageIndex == 2);
        CHECK(set.boxes[3].lodLevel == 2 && set.boxes[8].lodLevel == 1 && set.boxes[8].flags == 1);
        CHECK(set.probes[0].bc6h.empty() && set.size == 0);
        CHECK(stockProbeSharedImageName(set, 7) == "*reflection_probe_octahedron_7");
        CHECK(stockProbeSharedImageName(set, 1).empty() && stockProbeSharedImageName(set, 99).empty());
        StockProbeSet noNames = set;
        noNames.sharedImageNames.clear();
        CHECK(stockProbeSharedImageName(noNames, 7) == "*reflection_probe_octahedron_7");
        // The MP image search refuses a streamed set with a message that says why.
        CHECK(!extractStockProbeImages("none.ff.dec", set, &err) && err.find("streamed") != std::string::npos);
    }
    // SH as values is still read (Quarry's form); null SH leaves zeros.
    StockProbeSet noSh;
    CHECK(parseStockProbeRecords(streamedJson(kSynthProbes, kSynthBoxes, 2, 2, false), noSh, &err));
    CHECK(noSh.probes.size() == 8 && noSh.probes[3].sh[0][0] == 0.0f);
    // Streamed without shared probes: the indirections alone make it streamed.
    std::vector<SynthProbe> noShared = kSynthProbes;
    noShared[0].ind = 0x00710001u;  // a second slice in zone 7 bucket 1
    noShared[7].ind = 0x00720000u;
    StockProbeSet ns;
    CHECK(parseStockProbeRecords(streamedJson(noShared, kSynthBoxes, 0, 0, true), ns, &err) && ns.streamed && ns.sharedCount == 0);
    // No array image and nothing indirect: refused, as before.
    std::vector<SynthProbe> flat = kSynthProbes;
    for (SynthProbe& p : flat) p.ind = 0;
    CHECK(!parseStockProbeRecords(streamedJson(flat, kSynthBoxes, 0, 0, true), set, &err));
    CHECK(err.find("no reflectionProbeArrayImage_octahedron") != std::string::npos);
    // Names that do not match the shared count.
    CHECK(!parseStockProbeRecords(streamedJson(kSynthProbes, kSynthBoxes, 2, 3, true), set, &err));
    CHECK(err.find("names") != std::string::npos);
    // "Too many" and "does not match" are different errors.
    CHECK(!parseStockProbeRecords(streamedJson(kSynthProbes, kSynthBoxes, 2, 2, true, 70000), set, &err));
    CHECK(err.find("too many") != std::string::npos);
    CHECK(!parseStockProbeRecords(streamedJson(kSynthProbes, kSynthBoxes, 2, 2, true, 9), set, &err));
    CHECK(err.find("does not match") != std::string::npos);
    // 4096 < count <= 65535 is accepted: 5000 probes, one box each.
    std::vector<SynthProbe> many;
    std::vector<SynthBox> manyBoxes;
    for (int i = 0; i < 5000; ++i) {
        many.push_back({0xFFF00000u | uint32_t(i), {i}});
        manyBoxes.push_back({i, double(i), 0, 0, 10, double(i % 3), 0, 0});
    }
    StockProbeSet big;
    CHECK(parseStockProbeRecords(streamedJson(many, manyBoxes, 5000, 5000, false), big, &err));
    CHECK(big.probes.size() == 5000 && big.boxes.size() == 5000 && big.probes[4999].imageIndirection == 0xFFF01387u);
}

TEST(stock_probes_transient_zone) {
    StockProbeTransientZone tz;
    std::string err;
    CHECK(parseStockProbeTransientZone(trzoneJson(7, {1, 4, 5, 6, 7}, {2, 1}, 3), tz, &err));
    CHECK(tz.zoneIndex == 7 && tz.hasProbes && tz.exclusiveCount == 3);
    CHECK(tz.instances == (std::vector<uint16_t>{1, 4, 5, 6, 7}) && tz.bucketCounts == (std::vector<uint32_t>{2, 1}));
    // Shared-probe boxes only, no buckets.
    CHECK(parseStockProbeTransientZone(trzoneJson(66, {10, 11}, {}, 0), tz, &err));
    CHECK(tz.zoneIndex == 66 && tz.hasProbes && tz.exclusiveCount == 0 && tz.instances.size() == 2 && tz.bucketCounts.empty());
    // reflectionProbes null (the stub zones): parsed, no probes.
    const std::string stub = "{\"asset\":{\"fields\":{\"transientZoneIndex\":587,\"reflectionProbes\":null,\"collisionData\":null}}}";
    CHECK(parseStockProbeTransientZone(stub, tz, &err));
    CHECK(tz.zoneIndex == 587 && !tz.hasProbes && tz.instances.empty() && tz.exclusiveCount == 0);
    // Counts that disagree, a bucket size that is not count * 87360, no key at all.
    CHECK(!parseStockProbeTransientZone(trzoneJson(7, {1, 4, 5, 6, 7}, {2, 1}, 4), tz, &err));
    std::string badSize = trzoneJson(7, {1}, {1}, 1);
    badSize.replace(badSize.find("\"totalSize\":87360"), 17, "\"totalSize\":87361");
    CHECK(!parseStockProbeTransientZone(badSize, tz, &err));
    CHECK(!parseStockProbeTransientZone("{\"transientZoneIndex\":1}", tz, &err));
    // From a file, behind junk.
    const fs::path dir = fs::path(iw8r::test::tmpDir()) / "stock_probes";
    fs::create_directories(dir);
    const fs::path f = dir / "mp_x_00007_tr.0.asset.json";
    std::string text = trzoneJson(7, {1, 4, 5, 6, 7}, {2, 1}, 3);
    text.insert(text.find("\"reflectionProbes\""), "\"junk\":\"" + std::string(5000, 'a') + "\",");
    writeText(f, text);
    StockProbeTransientZone fromFile;
    CHECK(readStockProbeTransientZone(f.string(), fromFile, &err));
    CHECK(fromFile.zoneIndex == 7 && fromFile.instances.size() == 5 && fromFile.bucketCounts == (std::vector<uint32_t>{2, 1}));
    writeText(f, stub);
    CHECK(readStockProbeTransientZone(f.string(), fromFile, &err) && !fromFile.hasProbes && fromFile.zoneIndex == 587);
    writeText(f, text.substr(0, text.size() - 40));  // cut inside reflectionProbes
    CHECK(!readStockProbeTransientZone(f.string(), fromFile, &err));
    CHECK(!readStockProbeTransientZone((dir / "missing.json").string(), fromFile, &err));
}

TEST(stock_probes_zone_images) {
    const fs::path dir = fs::path(iw8r::test::tmpDir()) / "stock_probes";
    fs::create_directories(dir);
    StockProbeSet set;
    std::string err;
    CHECK(parseStockProbeRecords(synthStreamedJson(), set, &err));
    if (set.probes.size() != 8) return;
    for (const StockProbeTransientZone& tz : {synthZone0(), synthZone7()}) {
        uint64_t at = 0;
        const auto dec = transientDec(tz, tz.zoneIndex ? 1001 : 0, 1, &at);
        const fs::path decPath = dir / ("zone" + std::to_string(tz.zoneIndex) + ".ff.dec");
        writeFile(decPath, dec);
        for (size_t chunk : {size_t(0), size_t(64), size_t(4093), size_t(100000)}) {
            StockProbeImages img;
            uint64_t found = 0;
            CHECK(extractStockProbeZoneImages(decPath.string(), tz, set, img, &err, &found, chunk));
            CHECK(found == at);
            CHECK(img.zone == uint32_t(tz.zoneIndex) && img.size == 256 && img.mipCount == 6);
            CHECK(img.boxes == std::vector<int>(tz.instances.begin(), tz.instances.end()));
            const std::vector<int> want = tz.zoneIndex == 0 ? std::vector<int>{1, 2, 3} : std::vector<int>{4, 5, 6};
            CHECK(img.probes == want && img.bc6h.size() == 3);
            if (img.probes == want && img.bc6h.size() == 3)
                for (size_t k = 0; k < 3; ++k) CHECK(img.bc6h[k] == expectedImage(set.probes[size_t(want[k])].imageIndirection));
        }
        // Twice in the file: refused.
        writeFile(decPath, transientDec(tz, 10, 2, &at));
        StockProbeImages twice;
        CHECK(!extractStockProbeZoneImages(decPath.string(), tz, set, twice, &err) && err.find("more than once") != std::string::npos);
    }
    // Not in the file.
    StockProbeImages none;
    uint64_t at = 0;
    writeFile(dir / "zone7.ff.dec", transientDec(synthZone0(), 10, 1, &at));
    CHECK(!extractStockProbeZoneImages((dir / "zone7.ff.dec").string(), synthZone7(), set, none, &err));
    CHECK(err.find("not found") != std::string::npos);
    // The records and the export disagree.
    StockProbeTransientZone wrong = synthZone7();
    wrong.bucketCounts = {1, 1, 1};
    CHECK(!extractStockProbeZoneImages((dir / "zone7.ff.dec").string(), wrong, set, none, &err));
    wrong = synthZone7();
    wrong.bucketCounts = {2, 2};
    wrong.exclusiveCount = 4;
    CHECK(!extractStockProbeZoneImages((dir / "zone7.ff.dec").string(), wrong, set, none, &err));
    CHECK(err.find("records name 3") != std::string::npos);
    wrong.bucketCounts = {4};
    CHECK(!extractStockProbeZoneImages((dir / "zone7.ff.dec").string(), wrong, set, none, &err));
    CHECK(err.find("names bucket 1") != std::string::npos);
    // A zone without exclusive images needs no file.
    StockProbeTransientZone sharedOnly;
    sharedOnly.zoneIndex = 66;
    sharedOnly.hasProbes = true;
    sharedOnly.instances = {1, 8};
    StockProbeImages empty;
    CHECK(extractStockProbeZoneImages((dir / "no_such.ff.dec").string(), sharedOnly, set, empty, &err));
    CHECK(empty.probes.empty() && empty.zone == 66 && empty.boxes == (std::vector<int>{1, 8}));
    StockProbeTransientZone noIndex = synthZone7();
    noIndex.zoneIndex = -1;
    CHECK(!extractStockProbeZoneImages((dir / "zone7.ff.dec").string(), noIndex, set, none, &err));
}

TEST(stock_probes_dds) {
    std::vector<uint8_t> out;
    std::string err;
    auto d = probeDds(0x42);
    CHECK(parseStockProbeDds(d.data(), d.size(), out, &err));
    CHECK(out.size() == kPer && out.front() == 0x42 && out.back() == 0x42);
    const auto refused = [&](std::vector<uint8_t> x) {
        std::vector<uint8_t> o = {1};
        return !parseStockProbeDds(x.data(), x.size(), o, &err) && o == std::vector<uint8_t>{1};
    };
    auto x = d;
    put32(x, 128, 96);  // BC6H_SF16
    CHECK(refused(x));
    x = d;
    put32(x, 28, 5);  // 5 mips
    CHECK(refused(x));
    x = d;
    put32(x, 16, 128);  // 128 wide
    CHECK(refused(x));
    x = d;
    put32(x, 140, 2);  // an array of 2
    CHECK(refused(x));
    x = d;
    put32(x, 136, 4);  // a cube map
    CHECK(refused(x));
    x = d;
    std::memcpy(x.data() + 84, "DXT1", 4);  // no DX10 header
    CHECK(refused(x));
    x = d;
    x.push_back(0);  // a trailing byte
    CHECK(refused(x));
    x = d;
    x.pop_back();  // a byte short
    CHECK(refused(x));
    x = d;
    x[0] = 'X';
    CHECK(refused(x));
    CHECK(refused(std::vector<uint8_t>(20, 0)));
    const fs::path dir = fs::path(iw8r::test::tmpDir()) / "stock_probes";
    fs::create_directories(dir);
    writeFile(dir / "0.0.dds", d);
    CHECK(readStockProbeDds((dir / "0.0.dds").string(), out, &err) && out.size() == kPer);
    CHECK(!readStockProbeDds((dir / "missing.dds").string(), out, &err));
}

TEST(stock_probes_cache_v2) {
    const fs::path dir = fs::path(iw8r::test::tmpDir()) / "stock_probes";
    fs::create_directories(dir);
    StockProbeSet set;
    std::string err;
    CHECK(parseStockProbeRecords(synthStreamedJson(), set, &err));
    if (set.probes.size() != 8) return;
    // Zone 0 from its "main .dec", the shared probes from DDS files: the main file's images.
    uint64_t at = 0;
    writeFile(dir / "main.ff.dec", transientDec(synthZone0(), 77, 1, &at));
    StockProbeImages z0;
    CHECK(extractStockProbeZoneImages((dir / "main.ff.dec").string(), synthZone0(), set, z0, &err));
    CHECK(mergeStockProbeImages(set, z0, &err));
    CHECK(set.size == 256 && set.mipCount == 6 && set.probes[2].bc6h.size() == kPer && set.probes[4].bc6h.empty());
    StockProbeImages shared;
    shared.size = 256;
    shared.mipCount = 6;
    for (int i : {0, 7}) {
        const auto dds = probeDds(uint8_t(0x30 + i));
        std::vector<uint8_t> px;
        CHECK(parseStockProbeDds(dds.data(), dds.size(), px, &err));
        shared.probes.push_back(i);
        shared.bc6h.push_back(px);
    }
    CHECK(mergeStockProbeImages(set, shared, &err));
    const fs::path mainFile = dir / "probes_v2.iw8rprb";
    CHECK(writeStockProbeCache(mainFile.string(), set, &err));
    // 40 + P x 208 + 9 box indexes x 4 + B x 140 + K x 4 + K x 87360 (K = 2 shared + 3 zone 0).
    CHECK(fs::file_size(mainFile) == 40 + 8 * 208 + 9 * 4 + 9 * 140 + 5 * 4 + 5 * kPer);
    StockProbeSet back;
    CHECK(readStockProbeCache(mainFile.string(), back, &err));  // decode = true: ignored for v2
    CHECK(back.streamed && back.size == 256 && back.mipCount == 6 && back.probes.size() == 8 && back.boxes.size() == 9);
    CHECK(back.sharedCount == 2);
    if (back.probes.size() == 8 && back.boxes.size() == 9) {
        for (size_t i = 0; i < 8; ++i) {
            CHECK(back.probes[i].imageIndirection == set.probes[i].imageIndirection && back.probes[i].sh == set.probes[i].sh);
            CHECK(back.probes[i].boxes == set.probes[i].boxes && back.probes[i].bc6h == set.probes[i].bc6h);
            CHECK(back.probes[i].mips.empty());
        }
        CHECK(back.probes[4].bc6h.empty() && back.probes[0].bc6h.size() == kPer && back.probes[0].bc6h[5] == 0x30);
        for (size_t b = 0; b < 9; ++b)
            CHECK(back.boxes[b].center == set.boxes[b].center && back.boxes[b].priority == set.boxes[b].priority &&
                  back.boxes[b].lodLevel == set.boxes[b].lodLevel && back.boxes[b].imageIndex == set.boxes[b].imageIndex);
    }
    StockProbeSet recs;
    CHECK(readStockProbeCacheRecords(mainFile.string(), recs, &err));
    CHECK(recs.probes.size() == 8 && recs.boxes.size() == 9 && recs.probes[2].bc6h.empty() && recs.streamed);

    // The tile file of zone 7: its 3 exclusive images and its box list.
    writeFile(dir / "tile7.ff.dec", transientDec(synthZone7(), 5, 1, &at));
    StockProbeImages z7;
    CHECK(extractStockProbeZoneImages((dir / "tile7.ff.dec").string(), synthZone7(), recs, z7, &err));
    const fs::path tileFile = dir / "tile7.iw8rprb";
    CHECK(writeStockProbeTileCache(tileFile.string(), z7, &err));
    CHECK(fs::file_size(tileFile) == 40 + 3 * 4 + 4 + 5 * 4 + 3 * kPer);
    StockProbeImages t;
    CHECK(readStockProbeTileCache(tileFile.string(), t, &err));
    CHECK(t.zone == 7 && t.size == 256 && t.mipCount == 6 && t.probes == (std::vector<int>{4, 5, 6}) && t.boxes == z7.boxes);
    CHECK(t.bc6h == z7.bc6h);
    // Merged into the main set: every probe now has its image.
    CHECK(mergeStockProbeImages(back, t, &err));
    bool all = true;
    for (const StockProbe& p : back.probes) all = all && p.bc6h.size() == kPer;
    CHECK(all && back.probes[5].bc6h == expectedImage(0x00700001u));
    // A tile file with no images (a tile that only switches on shared-probe boxes).
    StockProbeImages boxesOnly;
    boxesOnly.zone = 66;
    boxesOnly.boxes = {1, 8};
    const fs::path tile66 = dir / "tile66.iw8rprb";
    CHECK(writeStockProbeTileCache(tile66.string(), boxesOnly, &err));
    CHECK(readStockProbeTileCache(tile66.string(), t, &err) && t.zone == 66 && t.probes.empty() && t.boxes == (std::vector<int>{1, 8}));
    // Each kind is refused by the other reader.
    StockProbeSet wrongKind;
    CHECK(!readStockProbeCache(tileFile.string(), wrongKind, &err) && err.find("tile") != std::string::npos);
    CHECK(!readStockProbeCacheRecords(tileFile.string(), wrongKind, &err));
    CHECK(!readStockProbeTileCache(mainFile.string(), t, &err));
    // A tile file needs a zone; images must be ascending and the right size.
    StockProbeImages bad = z7;
    bad.zone = kStockProbeMainFile;
    CHECK(!writeStockProbeTileCache((dir / "bad.iw8rprb").string(), bad, &err));
    bad = z7;
    std::swap(bad.probes[0], bad.probes[1]);
    CHECK(!writeStockProbeTileCache((dir / "bad.iw8rprb").string(), bad, &err));
    bad = z7;
    bad.bc6h[1].pop_back();
    CHECK(!writeStockProbeTileCache((dir / "bad.iw8rprb").string(), bad, &err));
    // Merge refuses an index out of range or another image size, and leaves the set alone.
    StockProbeSet keep = back;
    bad = z7;
    bad.probes[2] = 8;
    CHECK(!mergeStockProbeImages(keep, bad, &err) && keep.probes[6].bc6h == back.probes[6].bc6h);
    bad = z7;
    bad.size = 128;
    CHECK(!mergeStockProbeImages(keep, bad, &err));
    // A streamed set with a wrong-sized image is not written.
    StockProbeSet wrongImg = set;
    wrongImg.probes[1].bc6h.resize(10);
    CHECK(!writeStockProbeCache((dir / "bad.iw8rprb").string(), wrongImg, &err));
    // Damaged v2 files.
    std::vector<uint8_t> bytes;
    CHECK(readFileBytes(mainFile, bytes));
    const fs::path damaged = dir / "damaged.iw8rprb";
    auto cut = bytes;
    cut.pop_back();
    writeFile(damaged, cut);
    CHECK(!readStockProbeCache(damaged.string(), back, &err));
    cut = bytes;
    cut.push_back(0);
    writeFile(damaged, cut);
    CHECK(!readStockProbeCache(damaged.string(), back, &err));
    cut = bytes;
    const size_t idxAt = 40 + 8 * 208 + 9 * 4 + 9 * 140;
    put32(cut, idxAt + 4, 0);  // the index list is no longer ascending
    writeFile(damaged, cut);
    CHECK(!readStockProbeCache(damaged.string(), back, &err));
    cut = bytes;
    put32(cut, 32, 9);  // K > P
    writeFile(damaged, cut);
    CHECK(!readStockProbeCache(damaged.string(), back, &err));
    cut = bytes;
    put32(cut, 8, 3);  // version 3
    writeFile(damaged, cut);
    CHECK(!readStockProbeCache(damaged.string(), back, &err) && err.find("version") != std::string::npos);
}

TEST(stock_probes_subset) {
    StockProbeSet full;
    std::string err;
    CHECK(parseStockProbeRecords(synthStreamedJson(), full, &err));
    if (full.probes.size() != 8) return;
    full.size = 8;  // small images: 64 + 16 bytes
    full.mipCount = 2;
    for (size_t i = 0; i < 8; ++i)
        if (i != 6) full.probes[i].bc6h.assign(80, uint8_t(i));  // probe 6 (box 7) has no image yet
    // Camera at the origin, 4 boxes: box 3 (lod 2, contains it), box 8 (lod 1, contains it), box 4 (150 away),
    // box 5 (250 away); box 7 (200 away) has no image. Order: lod 0 by priority (5: 2, 4: -1), 8, 3, the fallback 0.
    StockProbeSet sub = buildStockProbeSubset(full, {0, 0, 0}, 4);
    CHECK(sub.size == 8 && sub.mipCount == 2 && sub.streamed);
    CHECK(sub.boxes.size() == 5 && sub.probes.size() == 5);
    if (sub.boxes.size() == 5 && sub.probes.size() == 5) {
        const int owners[5] = {5, 4, 7, 3, 2};
        const ProbeVec3 centers[5] = {{-300, 0, 0}, {200, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
        for (size_t k = 0; k < 5; ++k) {
            CHECK(sub.boxes[k].imageIndex == int(k));  // probes re-indexed in box order
            CHECK(sub.boxes[k].center == centers[k]);
            CHECK(sub.probes[k].bc6h == full.probes[size_t(owners[k])].bc6h);
            CHECK(sub.probes[k].boxes == (std::vector<int>{int(k)}));  // probe 5's far box 6 is not in the subset
            CHECK(sub.probes[k].imageIndirection == full.probes[size_t(owners[k])].imageIndirection);
        }
        CHECK(sub.boxes[2].lodLevel == 1 && sub.boxes[3].lodLevel == 2 && stockProbeBoxIsFallback(sub.boxes[4]));
    }
    // Every box with an image (clamped to 127): boxes 1-6 and 8, then the fallback; probe 5 keeps both its boxes.
    sub = buildStockProbeSubset(full, {0, 0, 0}, 1000);
    CHECK(sub.boxes.size() == 8 && sub.probes.size() == 7 && stockProbeBoxIsFallback(sub.boxes.back()));
    int twoBoxes = 0;
    for (const StockProbe& p : sub.probes) twoBoxes += p.boxes.size() == 2 ? 1 : 0;
    CHECK(twoBoxes == 1);
    for (size_t k = 0; k < sub.boxes.size(); ++k) {
        const int s = sub.boxes[k].imageIndex;
        CHECK(s >= 0 && size_t(s) < sub.probes.size());
        if (s >= 0 && size_t(s) < sub.probes.size())
            CHECK(std::find(sub.probes[size_t(s)].boxes.begin(), sub.probes[size_t(s)].boxes.end(), int(k)) != sub.probes[size_t(s)].boxes.end());
    }
    // At (5000, 5000, 0): inside box 6 and the map-sized box 3; box 6 (lod 0) first.
    sub = buildStockProbeSubset(full, {5000, 5000, 0}, 2);
    CHECK(sub.boxes.size() == 3 && sub.boxes[0].center == (ProbeVec3{5000, 5000, 0}) && sub.boxes[1].lodLevel == 2 &&
          stockProbeBoxIsFallback(sub.boxes[2]));
    CHECK(sub.probes.size() == 3 && sub.probes[0].boxes == (std::vector<int>{0}));  // probe 5 without its near box 5
    // Without the fallback's image: no fallback box.
    StockProbeSet noFallback = full;
    noFallback.probes[2].bc6h.clear();
    sub = buildStockProbeSubset(noFallback, {0, 0, 0}, 3);
    CHECK(sub.boxes.size() == 3 && !stockProbeBoxIsFallback(sub.boxes.back()));
    // No images at all, or no image size: empty.
    StockProbeSet bare;
    CHECK(parseStockProbeRecords(synthStreamedJson(), bare, &err));
    CHECK(buildStockProbeSubset(bare, {0, 0, 0}).boxes.empty());
    // Many boxes: at most 128 boxes and 128 slices, the fallback last.
    StockProbeSet many;
    many.size = 8;
    many.mipCount = 2;
    for (int i = 0; i < 300; ++i) {
        StockProbe p;
        p.bc6h.assign(80, uint8_t(i));
        p.boxes = {i};
        many.probes.push_back(p);
        StockProbeBox b;
        b.imageIndex = i;
        b.center = {float(i * 100), 0, 0};
        b.halfSize = {10, 10, 10};
        b.priority = i == 150 ? -std::numeric_limits<float>::max() : 0.0f;
        many.boxes.push_back(b);
    }
    sub = buildStockProbeSubset(many, {0, 0, 0});
    CHECK(sub.boxes.size() == 128 && sub.probes.size() == 128 && stockProbeBoxIsFallback(sub.boxes.back()));
    CHECK(sub.boxes[0].center[0] == 0.0f && sub.boxes[126].center[0] == 12600.0f);  // the 127 nearest, in order
    // MP set (kProbeJson): the same boxes, priority order, the fallback last.
    StockProbeSet mp;
    CHECK(parseStockProbeRecords(kProbeJson, mp, &err));
    mp.size = 8;
    mp.mipCount = 2;
    for (StockProbe& p : mp.probes) p.bc6h.assign(80, 7);
    sub = buildStockProbeSubset(mp, {0, 0, 0});
    CHECK(sub.boxes.size() == 3 && sub.boxes[0].priority == 11.0f && sub.boxes[1].priority == 10.0f && stockProbeBoxIsFallback(sub.boxes[2]));
    CHECK(sub.probes.size() == 2 && sub.boxes[1].imageIndex == sub.boxes[2].imageIndex && !sub.streamed);
}

// Real data, when this machine has it: the mp_donetsk world cache (research 108 sections 1-2) and the research cuts
// in out/agent/wzprobe (zone-0 buckets read from the main .ff.dec, three ACTS-exported shared DDS).
TEST(stock_probes_warzone_real) {
    const fs::path wc = worldCacheDir() / "mp_donetsk";
    const fs::path gfx = wc / "assets" / "gfx_map" / "maps" / "mp" / "mp_donetsk.d3dbsp.282.asset.json";
    std::error_code ec;
    if (wc.empty() || !fs::exists(gfx, ec)) {
        iw8r::test::skipf("  SKIP mp_donetsk gfx_map export not in the world cache\n");
        return;
    }
    StockProbeSet set;
    std::string err;
    CHECK(readStockProbeRecords(gfx.string(), set, &err));
    CHECK(set.probes.size() == 5620 && set.boxes.size() == 6052 && set.sharedCount == 1849 && set.streamed);
    if (set.probes.size() != 5620 || set.boxes.size() != 6052) return;
    CHECK(set.sharedImageNames.size() == 1849 && set.sharedImageNames[0] == "*reflection_probe_octahedron_19");
    std::vector<int> zoneCount(0x1000, 0);
    for (const StockProbe& p : set.probes) ++zoneCount[stockProbeZone(p.imageIndirection)];
    int tiles = 0, exclusive = 0;
    for (size_t z = 1; z < 0xFFF; ++z)
        if (zoneCount[z]) ++tiles, exclusive += zoneCount[z];
    CHECK(zoneCount[0] == 3 && zoneCount[0xFFF] == 1849 && exclusive == 3768 && tiles == 180);
    // Shared probe i: element = its rank, slot named after the probe (research 108 section 1.1).
    CHECK(stockProbeSharedImageName(set, 19) == "*reflection_probe_octahedron_19" && stockProbeElement(set.probes[19].imageIndirection) == 0);
    CHECK(set.boxes[0].imageIndex == 5617 && stockProbeBoxIsFallback(set.boxes[0]) && set.probes[5617].imageIndirection == 1);
    CHECK(set.boxes[6049].lodLevel == 2 && set.boxes[6050].lodLevel == 2);
    CHECK(std::fabs(set.probes[0].sh[0][0] - 4.79998398f) < 1e-6f && std::fabs(set.probes[0].sh[0][8] - 5.16909599f) < 1e-6f);

    // Zone 0 (main trzone export) and tile 1 (its own export) against the records.
    StockProbeTransientZone tz0, tz1;
    const fs::path main0 = wc / "assets" / "gfx_map_trzone" / "mp_donetsk.0.asset.json";
    if (fs::exists(main0, ec)) {
        CHECK(readStockProbeTransientZone(main0.string(), tz0, &err));
        CHECK(tz0.zoneIndex == 0 && tz0.exclusiveCount == 3 && tz0.instances == (std::vector<uint16_t>{0, 6049, 6050}) &&
              tz0.bucketCounts == (std::vector<uint32_t>{2, 1}));
    } else {
        iw8r::test::skipf("  SKIP mp_donetsk main trzone export not in the world cache\n");
    }
    const fs::path t1 = wc / "streamed" / "mp_donetsk_00001_tr" / "assets" / "gfx_map_trzone" / "mp_donetsk_00001_tr.0.asset.json";
    if (fs::exists(t1, ec)) {
        CHECK(readStockProbeTransientZone(t1.string(), tz1, &err));
        CHECK(tz1.zoneIndex == 1 && tz1.exclusiveCount == 25 && tz1.instances.size() == 35 &&
              tz1.bucketCounts == (std::vector<uint32_t>{16, 8, 1}) && zoneCount[1] == 25);
    } else {
        iw8r::test::skipf("  SKIP mp_donetsk_00001_tr trzone export not in the world cache\n");
    }

    // Zone-0 pixels as research 108 cut them from the main .ff.dec, put back behind their needle: the split gives
    // three images that pass the octahedral gutter test (a wrong layout does not).
    const fs::path cuts = repoPath("out/agent/wzprobe");
    std::vector<uint8_t> b0, b1;
    if (tz0.exclusiveCount == 3 && readFileBytes(cuts / "m0_b0.bin", b0) && readFileBytes(cuts / "m0_b1.bin", b1) &&
        b0.size() == 2 * kPer && b1.size() == kPer) {
        std::vector<uint8_t> dec(4096, 0x33);
        const auto h = transientHeader(tz0);
        dec.insert(dec.end(), h.begin(), h.end());
        dec.insert(dec.end(), b0.begin(), b0.end());
        dec.insert(dec.end(), b1.begin(), b1.end());
        dec.insert(dec.end(), 64, 0x44);
        const fs::path dir = fs::path(iw8r::test::tmpDir()) / "stock_probes";
        fs::create_directories(dir);
        writeFile(dir / "donetsk_zone0.ff.dec", dec);
        StockProbeImages z0;
        uint64_t at = 0;
        CHECK(extractStockProbeZoneImages((dir / "donetsk_zone0.ff.dec").string(), tz0, set, z0, &err, &at));
        CHECK(at == 4096 && z0.probes == (std::vector<int>{5616, 5617, 5618}));
        for (const auto& img : z0.bc6h) CHECK(gutterError(img) < 0.03);
        // Slice 1 of bucket 0 read slice-major (the wrong layout) fails it.
        std::vector<uint8_t> wrongLayout(b0.begin() + std::ptrdiff_t(kPer), b0.begin() + std::ptrdiff_t(2 * kPer));
        CHECK(gutterError(wrongLayout) > 0.1);
        // Into a renderable subset next to the fallback's probe.
        StockProbeSet withZ0 = set;
        CHECK(mergeStockProbeImages(withZ0, z0, &err));
        const StockProbeSet sub = buildStockProbeSubset(withZ0, set.probes[5617].origin);
        CHECK(sub.boxes.size() == 3 && sub.probes.size() == 3 && stockProbeBoxIsFallback(sub.boxes.back()));
        CHECK(sub.boxes.size() == 3 && sub.boxes[0].lodLevel == 2 && sub.boxes[1].lodLevel == 2);
    } else {
        iw8r::test::skipf("  SKIP out/agent/wzprobe m0_b*.bin (research 108 zone-0 cut) not found\n");
    }

    // Shared images as ACTS exported them (research 108 section 1.2).
    std::vector<uint8_t> dds, px, ref;
    if (readFileBytes(cuts / "img" / "mw19replay" / "mp_donetsk" / "assets" / "image" / "_encoded" / "0.0.dds", dds) &&
        readFileBytes(cuts / "shared0.bin", ref)) {
        CHECK(parseStockProbeDds(dds.data(), dds.size(), px, &err) && px == ref);
        CHECK(gutterError(px) < 0.03);
    } else {
        iw8r::test::skipf("  SKIP out/agent/wzprobe shared DDS not found\n");
    }

    // mp_br_quarry: the same layout, SH as values (research 108 section 1.1).
    const fs::path quarry = worldCacheDir() / "mp_br_quarry" / "assets" / "gfx_map" / "maps" / "mp" / "mp_br_quarry.d3dbsp.282.asset.json";
    if (fs::exists(quarry, ec)) {
        StockProbeSet q;
        CHECK(readStockProbeRecords(quarry.string(), q, &err));
        CHECK(q.probes.size() == 653 && q.boxes.size() == 733 && q.sharedCount == 206 && q.streamed);
        if (q.probes.size() == 653 && q.boxes.size() == 733) {
            int z0 = 0, shared = 0, excl = 0;
            for (const StockProbe& p : q.probes) {
                const uint32_t z = stockProbeZone(p.imageIndirection);
                (z == 0 ? z0 : z == kStockProbeSharedZone ? shared : excl)++;
            }
            CHECK(z0 == 3 && shared == 206 && excl == 444);
            CHECK(q.boxes[0].imageIndex == 650 && stockProbeBoxIsFallback(q.boxes[0]) && q.probes[650].imageIndirection == 1);
            bool anySh = false;
            for (const StockProbe& p : q.probes) anySh = anySh || p.sh[0][0] != 0.0f;
            CHECK(anySh);
        }
    } else {
        iw8r::test::skipf("  SKIP mp_br_quarry gfx_map export not in the world cache\n");
    }

    // A camera-local subset over the whole map (every probe given a tiny stand-in image): 128 boxes, the fallback
    // last, lod order, the map-sized zone-0 boxes always in.
    StockProbeSet tiny = set;
    tiny.size = 4;
    tiny.mipCount = 1;
    for (StockProbe& p : tiny.probes) p.bc6h.assign(16, 0);
    // At probe 19 (north of both zone-0 boxes) and at a point inside both (their Y ranges overlap at -7156..-6156).
    for (const ProbeVec3& cam : {set.probes[19].origin, ProbeVec3{28400, -6656, 1348}}) {
        const StockProbeSet sub = buildStockProbeSubset(tiny, cam);
        CHECK(sub.boxes.size() == 128 && sub.probes.size() <= 128 && stockProbeBoxIsFallback(sub.boxes.back()));
        bool sorted = true;
        int lod2 = 0;
        for (size_t k = 0; k + 2 < sub.boxes.size(); ++k) {
            const StockProbeBox &a = sub.boxes[k], &b = sub.boxes[k + 1];
            sorted = sorted && (a.lodLevel < b.lodLevel || (a.lodLevel == b.lodLevel && a.priority >= b.priority));
        }
        for (const StockProbeBox& b : sub.boxes) lod2 += b.lodLevel == 2 ? 1 : 0;
        CHECK(sorted);
        if (cam[1] < 0) CHECK(lod2 == 2 && sub.boxes[125].lodLevel == 2 && sub.boxes[126].lodLevel == 2);
    }
}

// Opt-in: set IW8R_PROBE_DEC_DIR to a folder of decompressed Verdansk zones (mp_donetsk.ff.dec and / or
// mp_donetsk_NNNNN_tr.ff.dec, ACTS fastfile -d) to check the real needle, split and gutter on each (not a fixture: the
// zones are 1-600 MB and made on demand, so nothing is counted as skipped when it is not set).
TEST(stock_probes_warzone_dec) {
    const char* decDir = std::getenv("IW8R_PROBE_DEC_DIR");
    if (!decDir || !*decDir) return;
    const fs::path wc = worldCacheDir() / "mp_donetsk";
    StockProbeSet set;
    std::string err;
    CHECK(readStockProbeRecords((wc / "assets" / "gfx_map" / "maps" / "mp" / "mp_donetsk.d3dbsp.282.asset.json").string(), set, &err));
    if (set.probes.size() != 5620) return;
    std::error_code ec;
    for (fs::directory_iterator it(decDir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string file = it->path().filename().string();
        if (file.rfind("mp_donetsk", 0) != 0 || file.size() < 8 || file.substr(file.size() - 7) != ".ff.dec") continue;
        const std::string zone = file.substr(0, file.size() - 7);
        const fs::path exportPath = zone == "mp_donetsk"
                                        ? wc / "assets" / "gfx_map_trzone" / "mp_donetsk.0.asset.json"
                                        : wc / "streamed" / zone / "assets" / "gfx_map_trzone" / (zone + ".0.asset.json");
        StockProbeTransientZone tz;
        if (!readStockProbeTransientZone(exportPath.string(), tz, &err)) {
            std::printf("  %s: no trzone export (%s)\n", zone.c_str(), err.c_str());
            continue;
        }
        StockProbeImages img;
        uint64_t at = 0;
        const bool ok = extractStockProbeZoneImages(it->path().string(), tz, set, img, &err, &at);
        CHECK(ok);
        double worst = 0;
        for (const auto& b : img.bc6h) worst = std::max(worst, gutterError(b));
        std::printf("  %s: zone %d, header at 0x%llX, %zu boxes, buckets %zu, %zu images, worst gutter error %.4f%s%s\n", zone.c_str(),
                    tz.zoneIndex, (unsigned long long)at, img.boxes.size(), tz.bucketCounts.size(), img.probes.size(), worst,
                    ok ? "" : ", ERROR ", ok ? "" : err.c_str());
        CHECK(img.probes.size() == tz.exclusiveCount && worst < 0.05);  // research 108: at most 0.028 on the tiles read
        // Zone 0: the main file path (merge, v2 write / read, a subset the renderer can take, decoded).
        if (ok && tz.zoneIndex == 0) {
            StockProbeSet main = set;
            CHECK(mergeStockProbeImages(main, img, &err));
            const fs::path mf = fs::path(iw8r::test::tmpDir()) / "stock_probes" / "donetsk_main.iw8rprb";
            fs::create_directories(mf.parent_path());
            StockProbeSet back;
            CHECK(writeStockProbeCache(mf.string(), main, &err) && readStockProbeCache(mf.string(), back, &err));
            CHECK(back.probes.size() == 5620 && back.boxes.size() == 6052 && back.probes[5617].bc6h == main.probes[5617].bc6h);
            StockProbeSet sub = buildStockProbeSubset(back, back.probes[5617].origin);
            CHECK(sub.probes.size() == 3 && sub.boxes.size() == 3 && decodeStockProbeImages(sub, &err));
            if (sub.probes.size() == 3 && !sub.probes[2].mips.empty())
                std::printf("  mp_donetsk main file %llu bytes; fallback probe level-0 mean luminance %.3f\n",
                            (unsigned long long)fs::file_size(mf, ec), stockProbeMeanLuminance(sub.probes[2], 256, 0, true));
            fs::remove(mf, ec);
        }
        // The tile file round trip.
        if (ok && tz.zoneIndex > 0) {
            const fs::path tf = fs::path(iw8r::test::tmpDir()) / "stock_probes" / (zone + ".iw8rprb");
            fs::create_directories(tf.parent_path());
            StockProbeImages back;
            CHECK(writeStockProbeTileCache(tf.string(), img, &err) && readStockProbeTileCache(tf.string(), back, &err));
            CHECK(back.probes == img.probes && back.bc6h == img.bc6h && back.boxes == img.boxes && back.zone == img.zone);
            fs::remove(tf, ec);
        }
    }
}

// What a zone's reflection probes hold (research 123): set IW8R_TEST_PROBE_JSON (the zone's gfx_map export) and
// IW8R_TEST_PROBE_DEC (the decompressed zone, ACTS -d), or IW8R_TEST_PROBE_CACHE (an editor probe cache file).
// Prints per probe the mean luminance of level 0, its brightest texel and how coloured it is (the mean of
// (max - min) / max over the texels: 0 = grey). A report; with IW8R_TEST_PROBE_EXPECT="<min mean> <max mean> <min colour>"
// it checks that every probe's mean luminance lies in that range and the set's colour reaches that value.
TEST(stock_probes_zone_report) {
    const char* json = std::getenv("IW8R_TEST_PROBE_JSON");
    const char* dec = std::getenv("IW8R_TEST_PROBE_DEC");
    const char* cache = std::getenv("IW8R_TEST_PROBE_CACHE");
    StockProbeSet set;
    std::string err;
    if (cache && *cache) {
        if (!readStockProbeCache(cache, set, &err, true)) {
            std::printf("  probereport: %s\n", err.c_str());
            return;
        }
    } else if (json && *json && dec && *dec) {
        if (!extractStockProbes(json, dec, set, &err) || !decodeStockProbeImages(set, &err)) {
            std::printf("  probereport: %s\n", err.c_str());
            return;
        }
    } else {
        return;
    }
    double lo = 1e30, hi = 0, sum = 0, colourSum = 0, peak = 0;
    size_t n = 0;
    for (size_t k = 0; k < set.probes.size(); ++k) {
        const StockProbe& p = set.probes[k];
        if (p.mips.empty()) continue;
        const double mean = stockProbeMeanLuminance(p, set.size, 0, true);
        double colour = 0, brightest = 0;
        size_t texels = 0;
        for (int y = 4; y < set.size - 4; ++y)
            for (int x = 4; x < set.size - 4; ++x) {
                const ProbeVec3 t = stockProbeTexel(p, set.size, 0, x, y);
                const double mx = std::max({double(t[0]), double(t[1]), double(t[2])}), mn = std::min({double(t[0]), double(t[1]), double(t[2])});
                brightest = std::max(brightest, mx);
                if (mx > 1e-4) colour += (mx - mn) / mx;
                ++texels;
            }
        colour = texels ? colour / double(texels) : 0;
        if (n < 6) std::printf("  probereport: probe %zu mean luminance %.3f, brightest %.2f, colour %.3f\n", k, mean, brightest, colour);
        lo = std::min(lo, mean), hi = std::max(hi, mean), sum += mean, colourSum += colour, peak = std::max(peak, brightest);
        ++n;
    }
    if (!n) {
        std::printf("  probereport: no probe image\n");
        return;
    }
    std::printf("  probereport: %zu probe(s), %d px, mean luminance %.3f .. %.3f (average %.3f), brightest texel %.2f, colour %.3f\n", n, set.size, lo,
                hi, sum / double(n), peak, colourSum / double(n));
    if (const char* expect = std::getenv("IW8R_TEST_PROBE_EXPECT")) {
        double minMean = 0, maxMean = 0, minColour = 0;
        if (std::sscanf(expect, "%lf %lf %lf", &minMean, &maxMean, &minColour) == 3) {
            CHECK(lo >= minMean);
            CHECK(hi <= maxMean);
            CHECK(colourSum / double(n) >= minColour);
        }
    }
}
