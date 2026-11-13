// Stock lightmaps (src/core/stock_lightmap.h, docs/research/94_stock_lightmaps.md): texel decoders on hand-made
// blocks, the octahedral direction, the root search in a synthetic decompressed zone (also across chunk
// boundaries) and the cache file.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "stock_lightmap.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

namespace fs = std::filesystem;

bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

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

// A BC4 block: endpoints r0, r1 and 16 3-bit indices (texel t = row * 4 + column).
std::array<uint8_t, 8> bc4Block(uint8_t r0, uint8_t r1, const int (&idx)[16]) {
    std::array<uint8_t, 8> b{};
    b[0] = r0;
    b[1] = r1;
    uint64_t bits = 0;
    for (int t = 0; t < 16; ++t) bits |= uint64_t(idx[t] & 7) << (3 * t);
    for (int k = 0; k < 6; ++k) b[2 + k] = uint8_t(bits >> (8 * k));
    return b;
}

uint32_t packR11G11B10(uint32_t r, uint32_t g, uint32_t b) { return r | g << 11 | b << 22; }

// GfxImageRaw at `at`: format, flags 3, totalSize, w x h, depth 1, 1 element, 1 level, textureId 0, pixels -2.
void putRaw(std::vector<uint8_t>& b, size_t at, uint32_t format, uint32_t total, uint16_t w, uint16_t h,
            uint64_t pixels = 0xFFFFFFFFFFFFFFFEull) {
    put32(b, at, format);
    put32(b, at + 4, 3);
    put32(b, at + 8, total);
    put16(b, at + 12, w);
    put16(b, at + 14, h);
    put16(b, at + 16, 1);
    put16(b, at + 18, 1);
    b[at + 20] = 1;
    put64(b, at + 0x20, pixels);
}

// The synthetic lightmap: 8 x 4 texels.
//   plane 0 BC4: block 0 = gradient (r0 255, r1 0, index t % 8), block 1 = constant 51 (0.2)
//   plane 1 R11G11B10F: texel i = (1.0, 2.0, 0.5) except texel 5 = (1.5, 0.25, 3.0)
//   plane 2 BC5 or RGBA8: red 191 (0.75), green 127 everywhere ... decoded per research 94 section 1.3
constexpr int kW = 8, kH = 4;

std::vector<uint8_t> planeBC4() {
    int grad[16], zero[16] = {};
    for (int t = 0; t < 16; ++t) grad[t] = t % 8;
    std::vector<uint8_t> out;
    const auto b0 = bc4Block(255, 0, grad), b1 = bc4Block(51, 51, zero);
    out.insert(out.end(), b0.begin(), b0.end());
    out.insert(out.end(), b1.begin(), b1.end());
    return out;
}

std::vector<uint8_t> planeR11() {
    std::vector<uint8_t> out(kW * kH * 4);
    for (int i = 0; i < kW * kH; ++i) {
        // 1.0 = e15 m0; 2.0 = e16; 0.5 = e14 (5-bit mantissa for blue); 1.5 = e15 m32; 0.25 = e13; 3.0 = e16 m16 (5-bit).
        const uint32_t v = i == 5 ? packR11G11B10(15u << 6 | 32u, 13u << 6, 16u << 5 | 16u) : packR11G11B10(15u << 6, 16u << 6, 14u << 5);
        put32(out, size_t(i) * 4, v);
    }
    return out;
}

std::vector<uint8_t> planeDirection(bool rgba8, uint8_t x, uint8_t y) {
    if (rgba8) {
        std::vector<uint8_t> out(kW * kH * 4);
        for (int i = 0; i < kW * kH; ++i) {
            out[size_t(i) * 4] = x;
            out[size_t(i) * 4 + 1] = y;
            out[size_t(i) * 4 + 2] = 46;  // B: not decoded
            out[size_t(i) * 4 + 3] = 255;
        }
        return out;
    }
    int zero[16] = {};
    std::vector<uint8_t> out;
    for (int blk = 0; blk < 2; ++blk) {
        const auto r = bc4Block(x, x, zero), g = bc4Block(y, y, zero);
        out.insert(out.end(), r.begin(), r.end());
        out.insert(out.end(), g.begin(), g.end());
    }
    return out;
}

// BC6H mode 11 (one region, 10-bit endpoints, unsigned): both endpoints (r, g, b), every index 0, so every texel
// decodes to the unquantised endpoint. Bits: mode 00011 at 0..4, rw 5..14, gw 15..24, bw 25..34, rx 35..44,
// gx 45..54, bx 55..64, indices 65..127.
std::array<uint8_t, 16> bc6hConstantBlock(uint32_t r, uint32_t g, uint32_t b) {
    std::array<uint8_t, 16> out{};
    const auto put = [&](int at, uint32_t v, int bits) {
        for (int i = 0; i < bits; ++i)
            if (v >> i & 1) out[size_t((at + i) / 8)] |= uint8_t(1u << ((at + i) % 8));
    };
    put(0, 3, 5);
    put(5, r, 10);
    put(15, g, 10);
    put(25, b, 10);
    put(35, r, 10);
    put(45, g, 10);
    put(55, b, 10);
    return out;
}

// The half float bits BC6H unsigned gives a 10-bit endpoint x (unquantise, then x 31 / 64; the D3D rules).
uint16_t bc6hHalfOf(uint32_t x) {
    const uint32_t unq = x == 0 ? 0 : x == 1023 ? 0xFFFF : ((x << 16) + 0x8000) >> 10;
    return uint16_t((unq * 31) >> 6);
}

// 8 x 4 BC6H plane: block 0 = (512, 256, 768), block 1 = (100, 900, 0).
std::vector<uint8_t> planeBC6H() {
    std::vector<uint8_t> out;
    const auto b0 = bc6hConstantBlock(512, 256, 768), b1 = bc6hConstantBlock(100, 900, 0);
    out.insert(out.end(), b0.begin(), b0.end());
    out.insert(out.end(), b1.begin(), b1.end());
    return out;
}

// The root (0xF10 bytes) and its pixels. numLightmaps > 1 repeats the descriptors and the planes. bc6h: plane 1 is
// BC6H (type 0, Warzone) instead of R11G11B10F.
std::vector<uint8_t> lightmapRecord(uint32_t type, uint32_t numLightmaps, bool rgba8, uint64_t pixelsPtr = 0xFFFFFFFFFFFFFFFEull,
                                    uint32_t first = 0, uint32_t transientZone = 0, bool bc6h = false) {
    const auto p0 = planeBC4(), p1 = bc6h ? planeBC6H() : planeR11(), p2 = planeDirection(rgba8, 191, 127);
    std::vector<uint8_t> r(0xF10, 0);
    put32(r, 0, transientZone);
    put32(r, 4, numLightmaps);
    put32(r, 8, first);         // firstLightmapIndex
    put32(r, 12, type);
    for (uint32_t li = 0; li < numLightmaps; ++li) {
        const size_t at = 0x10 + li * 0x78;
        putRaw(r, at, kLmFormatBC4, uint32_t(p0.size()), kW, kH, pixelsPtr);
        putRaw(r, at + 0x28, bc6h ? kLmFormatBC6H : kLmFormatR11G11B10F, uint32_t(p1.size()), kW, kH);
        putRaw(r, at + 0x50, rgba8 ? kLmFormatRGBA8 : kLmFormatBC5, uint32_t(p2.size()), kW, kH);
    }
    for (uint32_t li = 0; li < numLightmaps; ++li) {
        r.insert(r.end(), p0.begin(), p0.end());
        r.insert(r.end(), p1.begin(), p1.end());
        r.insert(r.end(), p2.begin(), p2.end());
    }
    return r;
}

// Filler bytes (an LCG) with `record` at `offset`, written to `path`.
void writeZone(const fs::path& path, size_t size, size_t offset, const std::vector<uint8_t>& record) {
    std::vector<uint8_t> b(size);
    uint32_t s = 12345;
    for (uint8_t& v : b) {
        s = s * 1664525u + 1013904223u;
        v = uint8_t(s >> 24);
    }
    std::memcpy(b.data() + offset, record.data(), record.size());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}

fs::path tmpPath(const char* name) {
    const fs::path dir = fs::path(iw8r::test::tmpDir()) / "stock_lightmap";
    fs::create_directories(dir);
    return dir / name;
}

}  // namespace

TEST(stock_lightmap_bc4_bc5_decode) {
    // r0 > r1: 8-value palette. Index t % 8 over the block.
    int grad[16];
    for (int t = 0; t < 16; ++t) grad[t] = t % 8;
    const auto b = bc4Block(200, 100, grad);
    std::vector<float> v;
    CHECK(decodeBC4(b.data(), b.size(), 4, 4, v));
    CHECK(v.size() == 16);
    CHECK(near(v[0], 200 / 255.0f));
    CHECK(near(v[1], 100 / 255.0f));
    CHECK(near(v[2], (6 * 200 + 1 * 100) / 7.0f / 255.0f));
    CHECK(near(v[7], (1 * 200 + 6 * 100) / 7.0f / 255.0f));
    CHECK(near(v[8], v[0]));  // texel (0, 2)

    // r0 <= r1: 6-value palette, index 6 = 0, index 7 = 255.
    int six[16] = {0, 1, 2, 5, 6, 7};
    const auto c = bc4Block(50, 150, six);
    CHECK(decodeBC4(c.data(), c.size(), 4, 4, v));
    CHECK(near(v[0], 50 / 255.0f) && near(v[1], 150 / 255.0f));
    CHECK(near(v[2], (4 * 50 + 1 * 150) / 5.0f / 255.0f));
    CHECK(near(v[3], (1 * 50 + 4 * 150) / 5.0f / 255.0f));
    CHECK(v[4] == 0.0f && v[5] == 1.0f);

    // A 5 x 3 image needs 2 x 1 blocks; the texels outside the image are dropped. Too few bytes fail.
    std::vector<uint8_t> two(b.begin(), b.end());
    int none[16] = {};
    const auto k = bc4Block(10, 10, none);
    two.insert(two.end(), k.begin(), k.end());
    CHECK(decodeBC4(two.data(), two.size(), 5, 3, v));
    CHECK(v.size() == 15);
    CHECK(near(v[4], 10 / 255.0f));  // (4, 0): block 1
    CHECK(near(v[5], (4 * 200 + 3 * 100) / 7.0f / 255.0f));  // (0, 1): block 0, texel 4, index 4
    CHECK(near(v[5 + 4], 10 / 255.0f));  // (4, 1): block 1
    CHECK(!decodeBC4(two.data(), 8, 5, 3, v));

    // BC5: red block then green block per 4x4.
    std::vector<uint8_t> bc5;
    int zero[16] = {};
    const auto r = bc4Block(20, 20, zero), g = bc4Block(240, 240, zero);
    bc5.insert(bc5.end(), r.begin(), r.end());
    bc5.insert(bc5.end(), g.begin(), g.end());
    CHECK(decodeBC5(bc5.data(), bc5.size(), 4, 4, v));
    CHECK(v.size() == 32);
    CHECK(near(v[0], 20 / 255.0f) && near(v[1], 240 / 255.0f) && near(v[30], 20 / 255.0f) && near(v[31], 240 / 255.0f));
}

TEST(stock_lightmap_r11g11b10f_and_rgba8) {
    CHECK(unpackUnsignedSmallFloat(15u << 6, 6) == 1.0f);
    CHECK(unpackUnsignedSmallFloat(15u << 6 | 32u, 6) == 1.5f);
    CHECK(unpackUnsignedSmallFloat(16u << 5 | 16u, 5) == 3.0f);
    CHECK(unpackUnsignedSmallFloat(1u, 6) == std::ldexp(1.0f, -20));  // subnormal: 1/64 * 2^-14
    CHECK(unpackUnsignedSmallFloat(0u, 6) == 0.0f);
    CHECK(unpackUnsignedSmallFloat(31u << 6, 6) == 0.0f);  // infinity is not light
    CHECK(unpackUnsignedSmallFloat(30u << 6 | 63u, 6) == 65024.0f);  // the largest finite value

    const auto p = planeR11();
    std::vector<float> v;
    CHECK(decodeR11G11B10F(p.data(), p.size(), kW, kH, v));
    CHECK(v.size() == size_t(kW * kH * 3));
    CHECK(v[0] == 1.0f && v[1] == 2.0f && v[2] == 0.5f);
    CHECK(v[15] == 1.5f && v[16] == 0.25f && v[17] == 3.0f);
    CHECK(!decodeR11G11B10F(p.data(), p.size() - 1, kW, kH, v));

    const uint8_t px[8] = {0, 51, 255, 128, 1, 2, 3, 4};
    CHECK(decodeRGBA8(px, 8, 2, 1, v));
    CHECK(v.size() == 8 && v[0] == 0.0f && near(v[1], 0.2f) && v[2] == 1.0f);
    CHECK(!decodeRGBA8(px, 7, 2, 1, v));
}

TEST(stock_lightmap_octahedral) {
    // (0.5, 0.5) is straight up; research 94 section 1.3's formulas on a hand case.
    auto d = decodeOctahedralDirection(0.5f, 0.5f);
    CHECK(near(d[0], 0) && near(d[1], 0) && near(d[2], 1));
    // x = 0.75, y = 0.75: a = b = 0.5, o = (0.5, 0, 0.5) -> (0.7071, 0, 0.7071).
    d = decodeOctahedralDirection(0.75f, 0.75f);
    CHECK(near(d[0], 0.70710678f) && near(d[1], 0) && near(d[2], 0.70710678f));
    // x = 0.75, y = 0.25: a = 0.5, b = -0.5, o = (0, 0.5, 0.5).
    d = decodeOctahedralDirection(0.75f, 0.25f);
    CHECK(near(d[0], 0) && near(d[1], 0.70710678f) && near(d[2], 0.70710678f));
    // Every byte pair decodes to a unit vector with z >= 0 and encodes back to the same bytes.
    bool unit = true, roundTrip = true;
    for (int x = 0; x < 256; x += 3)
        for (int y = 0; y < 256; y += 3) {
            const auto e = decodeOctahedralDirection(x / 255.0f, y / 255.0f);
            unit &= near(e[0] * e[0] + e[1] * e[1] + e[2] * e[2], 1.0f, 1e-5f) && e[2] >= 0.0f;
            const auto b = encodeOctahedralDirection(e[0], e[1], e[2]);
            roundTrip &= int(std::lround(b[0] * 255.0f)) == x && int(std::lround(b[1] * 255.0f)) == y;
        }
    CHECK(unit);
    CHECK(roundTrip);
    // Direction -> encode -> decode on the upper hemisphere.
    bool dirTrip = true;
    for (int i = 0; i < 50; ++i) {
        const float th = 0.03f * float(i), ph = 0.7f * float(i);
        const float x = std::sin(th) * std::cos(ph), y = std::sin(th) * std::sin(ph), z = std::cos(th);
        const auto e = encodeOctahedralDirection(x, y, z);
        const auto back = decodeOctahedralDirection(e[0], e[1]);
        dirTrip &= near(back[0], x, 1e-5f) && near(back[1], y, 1e-5f) && near(back[2], z, 1e-5f);
    }
    CHECK(dirTrip);
    // Below the horizon: put on the horizon.
    const auto low = encodeOctahedralDirection(1, 0, -1);
    d = decodeOctahedralDirection(low[0], low[1]);
    CHECK(near(d[0], 1) && near(d[2], 0));
}

TEST(stock_lightmap_half_floats) {
    CHECK(floatToHalf(1.0f) == 0x3C00);
    CHECK(floatToHalf(0.0f) == 0);
    CHECK(floatToHalf(-2.0f) == 0xC000);
    CHECK(floatToHalf(65504.0f) == 0x7BFF);
    CHECK(floatToHalf(1e9f) == 0x7BFF);
    CHECK(floatToHalf(std::nanf("")) == 0);
    CHECK(floatToHalf(std::ldexp(1.0f, -24)) == 1);       // smallest subnormal
    CHECK(floatToHalf(std::ldexp(1.0f, -14)) == 0x0400);  // smallest normal
    CHECK(halfToFloat(0x3C00) == 1.0f && halfToFloat(0x7BFF) == 65504.0f && halfToFloat(1) == std::ldexp(1.0f, -24));
    bool ok = true;
    for (int i = 0; i < 2000; ++i) {
        const float v = std::ldexp(1.0f + float(i % 97) / 97.0f, (i % 30) - 14);
        const float back = halfToFloat(floatToHalf(v));
        ok &= std::fabs(back - v) <= v * (1.0f / 2048.0f) * 1.0001f;
    }
    CHECK(ok);
    // Every half that is finite converts back exactly.
    bool exact = true;
    for (uint32_t h = 0; h < 0x7C00; ++h) exact &= floatToHalf(halfToFloat(uint16_t(h))) == h;
    CHECK(exact);
}

TEST(stock_lightmap_root_search) {
    const auto rec = lightmapRecord(1, 1, false);
    const fs::path zone = tmpPath("synthetic.ff.dec");
    const size_t offset = 100003;  // not 4-aligned, like mp_backlot2's root
    writeZone(zone, 200000, offset, rec);

    std::vector<uint64_t> roots;
    std::string err;
    CHECK(findStockLightmapRoots(zone.string(), roots, &err));
    CHECK(roots.size() == 1 && roots[0] == offset);

    // Chunk sizes that cut the 56-byte signature (and the 0xF10 root) at every place near the boundary.
    bool all = true;
    for (size_t chunk : {size_t(57), size_t(64), size_t(1000), size_t(4096), size_t(offset + 1), size_t(offset + 30),
                         size_t(offset + 55), size_t(offset + 56), size_t(offset - 1), size_t(65536)}) {
        CHECK(findStockLightmapRoots(zone.string(), roots, &err, chunk));
        const bool one = roots.size() == 1 && roots[0] == offset;
        if (!one) std::printf("    chunk %zu: %zu roots\n", chunk, roots.size());
        all &= one;
    }
    CHECK(all);

    // A record whose pixel pointer is not -2 is not a root; nor is one whose pixels run past the end of the file.
    const fs::path bad = tmpPath("bad.ff.dec");
    writeZone(bad, 20000, 5000, lightmapRecord(1, 1, false, 0x1234));
    CHECK(findStockLightmapRoots(bad.string(), roots, &err));
    CHECK(roots.empty());
    auto cut = lightmapRecord(1, 1, false);
    writeZone(bad, 0xF10 + 100 + 64, 100, std::vector<uint8_t>(cut.begin(), cut.begin() + 0xF10 + 64));
    CHECK(findStockLightmapRoots(bad.string(), roots, &err));
    CHECK(roots.empty());
    StockLightmap none;
    CHECK(!extractStockLightmapFromDecompressedZone(bad.string(), none, &err));
    CHECK(err.find("no lightmap root") != std::string::npos);

    CHECK(!findStockLightmapRoots(tmpPath("missing.ff.dec").string(), roots, &err));
}

TEST(stock_lightmap_extract_and_cache) {
    const fs::path zone = tmpPath("extract.ff.dec");
    writeZone(zone, 50000, 777, lightmapRecord(1, 1, false));
    StockLightmap lm;
    std::string err;
    uint64_t at = 0;
    CHECK(extractStockLightmapFromDecompressedZone(zone.string(), lm, &err, &at));
    CHECK(err.empty());
    CHECK(at == 777);
    CHECK(lm.type == 1 && lm.width == kW && lm.height == kH);
    CHECK(lm.irradiance.size() == size_t(kW * kH * 3) && lm.ambientFraction.size() == size_t(kW * kH) &&
          lm.direction.size() == size_t(kW * kH * 3));
    if (lm.irradiance.size() != size_t(kW * kH * 3) || lm.direction.size() != size_t(kW * kH * 3)) return;
    // f: block 0 (x 0..3) is the 255 -> 0 gradient (texel (1, 0) is index 1 = 0), block 1 (x 4..7) 0.2.
    CHECK(near(lm.ambientFraction[0], 1.0f) && lm.ambientFraction[1] == 0.0f && near(lm.ambientFraction[2], 6.0f / 7.0f));
    CHECK(near(lm.ambientFraction[4], 0.2f) && near(lm.ambientFraction[kW + 7], 0.2f));
    // C: texel 5 = (1.5, 0.25, 3.0), others (1, 2, 0.5).
    CHECK(lm.irradiance[0] == 1.0f && lm.irradiance[1] == 2.0f && lm.irradiance[2] == 0.5f);
    CHECK(lm.irradiance[15] == 1.5f && lm.irradiance[16] == 0.25f && lm.irradiance[17] == 3.0f);
    // D from bytes (191, 127).
    const auto d = decodeOctahedralDirection(191 / 255.0f, 127 / 255.0f);
    CHECK(near(lm.direction[0], d[0]) && near(lm.direction[1], d[1]) && near(lm.direction[2], d[2]) && d[0] > 0.3f);

    // Cache round trip: C as binary16 (exact for these values), f as a byte, D as the two octahedral bytes.
    const fs::path cache = tmpPath("lightmap.iw8rlm");
    CHECK(writeStockLightmapCache(cache.string(), lm, &err));
    CHECK(fs::file_size(cache) == 32 + size_t(kW * kH) * 9);
    CHECK(!fs::exists(fs::path(cache.string() + ".tmp")));
    StockLightmap back;
    CHECK(readStockLightmapCache(cache.string(), back, &err));
    CHECK(back.type == 1 && back.width == kW && back.height == kH);
    CHECK(back.irradiance == lm.irradiance);
    bool fOk = back.ambientFraction.size() == lm.ambientFraction.size(), dOk = back.direction.size() == lm.direction.size();
    for (size_t i = 0; fOk && i < lm.ambientFraction.size(); ++i) fOk = near(back.ambientFraction[i], lm.ambientFraction[i], 0.5f / 255.0f + 1e-6f);
    for (size_t i = 0; dOk && i < lm.direction.size(); ++i) dOk = near(back.direction[i], lm.direction[i], 1e-5f);
    CHECK(fOk);
    CHECK(dOk);

    // A lightmap with only C: the other planes stay empty.
    StockLightmap onlyC;
    onlyC.type = 2;
    onlyC.width = 2;
    onlyC.height = 1;
    onlyC.irradiance = {0.1f, 1.0f, 100.0f, 7.5f, 0.0f, 1e6f};
    CHECK(writeStockLightmapCache(cache.string(), onlyC, &err));
    CHECK(readStockLightmapCache(cache.string(), back, &err));
    CHECK(back.type == 2 && back.ambientFraction.empty() && back.direction.empty() && back.irradiance.size() == 6);
    CHECK(near(back.irradiance[0], 0.1f, 1e-4f) && back.irradiance[1] == 1.0f && back.irradiance[2] == 100.0f &&
          back.irradiance[5] == 65504.0f);
    // Wrong plane sizes are refused.
    onlyC.irradiance.pop_back();
    CHECK(!writeStockLightmapCache(cache.string(), onlyC, &err));

    // Damaged files fail.
    {
        std::ofstream(cache, std::ios::binary | std::ios::trunc) << "IW8RLM";
    }
    CHECK(!readStockLightmapCache(cache.string(), back, &err));
    CHECK(writeStockLightmapCache(cache.string(), lm, &err));
    fs::resize_file(cache, fs::file_size(cache) - 1);
    CHECK(!readStockLightmapCache(cache.string(), back, &err));
    CHECK(!readStockLightmapCache(tmpPath("nope.iw8rlm").string(), back, &err));
}

TEST(stock_lightmap_ab_and_unsupported) {
    // AB_LIGHTING (type 2): plane 2 is R8G8B8A8, R and G the octahedral direction.
    const fs::path zone = tmpPath("ab.ff.dec");
    writeZone(zone, 30000, 4242, lightmapRecord(2, 1, true));
    StockLightmap lm;
    std::string err;
    CHECK(extractStockLightmapFromDecompressedZone(zone.string(), lm, &err));
    CHECK(lm.type == 2 && lm.width == kW && lm.direction.size() == size_t(kW * kH * 3));
    if (lm.direction.size() == size_t(kW * kH * 3)) {
        const auto d = decodeOctahedralDirection(191 / 255.0f, 127 / 255.0f);
        CHECK(near(lm.direction[3 * 9], d[0]) && near(lm.direction[3 * 9 + 2], d[2]));
    }

    // Two lightmaps in one root: the single-lightmap reader sends the caller to the many-lightmap one.
    writeZone(zone, 30000, 1000, lightmapRecord(1, 2, false));
    CHECK(!extractStockLightmapFromDecompressedZone(zone.string(), lm, &err));
    CHECK(err.find("2 lightmaps in one root") != std::string::npos);

    // An unknown irradiance format (BC7, 44) is refused with the formats in the message.
    {
        auto rec = lightmapRecord(0, 1, false);
        put32(rec, 0x10 + 0x28, 44);
        writeZone(zone, 30000, 1000, rec);
    }
    CHECK(!extractStockLightmapFromDecompressedZone(zone.string(), lm, &err));
    CHECK(err.find("plane formats 39, 44, 40") != std::string::npos);

    // Two roots in one file: refused.
    {
        const auto rec = lightmapRecord(1, 1, false);
        std::vector<uint8_t> both = rec;
        both.resize(rec.size() + 500, 0);
        both.insert(both.end(), rec.begin(), rec.end());
        writeZone(zone, 40000, 100, both);
    }
    std::vector<uint64_t> roots;
    CHECK(findStockLightmapRoots(zone.string(), roots, &err) && roots.size() == 2);
    CHECK(!extractStockLightmapFromDecompressedZone(zone.string(), lm, &err));
    CHECK(err.find("2 lightmap roots") != std::string::npos);
}

// Warzone (type 0): plane 1 is BC6H. One lightmap per transient zone; the root's firstLightmapIndex is the global
// index (mp_br_quarry_00026_tr: transient zone 26, lightmap 17).
TEST(stock_lightmap_type0_bc6h) {
    const fs::path zone = tmpPath("wz_tile.ff.dec");
    writeZone(zone, 60000, 3333, lightmapRecord(0, 1, false, 0xFFFFFFFFFFFFFFFEull, 17, 26, true));
    std::vector<StockLightmap> all;
    std::vector<uint64_t> roots;
    std::string err;
    CHECK(extractStockLightmapsFromDecompressedZone(zone.string(), all, &err, &roots));
    CHECK(err.empty());
    CHECK(roots.size() == 1 && roots[0] == 3333);
    CHECK(all.size() == 1);
    if (all.size() != 1) return;
    const StockLightmap& lm = all[0];
    CHECK(lm.index == 17 && lm.transientZone == 26 && lm.type == 0 && lm.width == kW && lm.height == kH);
    CHECK(lm.irradiance.size() == size_t(kW * kH * 3) && lm.ambientFraction.size() == size_t(kW * kH) &&
          lm.direction.size() == size_t(kW * kH * 3));
    if (lm.irradiance.size() != size_t(kW * kH * 3)) return;
    // Block 0 (x 0..3) is (512, 256, 768) unquantised, block 1 (x 4..7) (100, 900, 0); every row alike.
    const float r0 = halfToFloat(bc6hHalfOf(512)), g0 = halfToFloat(bc6hHalfOf(256)), b0 = halfToFloat(bc6hHalfOf(768));
    const float r1 = halfToFloat(bc6hHalfOf(100)), g1 = halfToFloat(bc6hHalfOf(900));
    CHECK(near(r0, 1.5146484f, 1e-6f) && b0 > 300.0f);
    bool ok = true;
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const float* c = &lm.irradiance[size_t(y * kW + x) * 3];
            ok &= x < 4 ? (c[0] == r0 && c[1] == g0 && c[2] == b0) : (c[0] == r1 && c[1] == g1 && c[2] == 0.0f);
        }
    CHECK(ok);
    // f and D decode as on the MP maps.
    CHECK(near(lm.ambientFraction[0], 1.0f) && near(lm.ambientFraction[4], 0.2f));
    const auto d = decodeOctahedralDirection(191 / 255.0f, 127 / 255.0f);
    CHECK(near(lm.direction[0], d[0]) && near(lm.direction[2], d[2]));

    // The single-lightmap reader takes type 0 too.
    StockLightmap one;
    CHECK(extractStockLightmapFromDecompressedZone(zone.string(), one, &err));
    CHECK(one.index == 17 && one.type == 0 && one.irradiance == lm.irradiance);

    // Cache: the index is kept (header offset 28); the header-only read gives it without the planes.
    const fs::path cache = tmpPath("17.iw8rlm");
    CHECK(writeStockLightmapCache(cache.string(), lm, &err));
    StockLightmap back, head;
    CHECK(readStockLightmapCache(cache.string(), back, &err));
    CHECK(back.index == 17 && back.type == 0 && back.width == kW && back.height == kH && back.transientZone == -1);
    CHECK(back.irradiance.size() == lm.irradiance.size());
    bool close = back.irradiance.size() == lm.irradiance.size();
    for (size_t i = 0; close && i < lm.irradiance.size(); ++i)
        close = std::fabs(back.irradiance[i] - lm.irradiance[i]) <= std::fabs(lm.irradiance[i]) * 1e-3f + 1e-6f;
    CHECK(close);
    CHECK(readStockLightmapCacheHeader(cache.string(), head, &err));
    CHECK(head.index == 17 && head.width == kW && head.height == kH && head.irradiance.empty() && head.direction.empty());
    CHECK(!readStockLightmapCacheHeader(tmpPath("nope.iw8rlm").string(), head, &err));
}

// Several lightmaps: one root with three (indices 5, 6, 7) and a second root with one (9), pixels after each root.
TEST(stock_lightmap_many) {
    std::vector<uint8_t> both = lightmapRecord(0, 3, false, 0xFFFFFFFFFFFFFFFEull, 5, 40, true);
    both.resize(both.size() + 300, 0);
    const auto second = lightmapRecord(1, 1, false, 0xFFFFFFFFFFFFFFFEull, 9, 41, false);
    both.insert(both.end(), second.begin(), second.end());
    const fs::path zone = tmpPath("many.ff.dec");
    writeZone(zone, 80000, 1001, both);
    std::vector<StockLightmap> all;
    std::string err;
    CHECK(extractStockLightmapsFromDecompressedZone(zone.string(), all, &err));
    CHECK(err.empty());
    CHECK(all.size() == 4);
    if (all.size() != 4) return;
    CHECK(all[0].index == 5 && all[1].index == 6 && all[2].index == 7 && all[3].index == 9);
    CHECK(all[0].transientZone == 40 && all[3].transientZone == 41 && all[0].type == 0 && all[3].type == 1);
    // Each lightmap's planes are read from its own place: the three BC6H lightmaps are equal, the fourth is R11G11B10F.
    CHECK(all[1].irradiance == all[0].irradiance && all[2].irradiance == all[0].irradiance);
    CHECK(all[3].irradiance.size() == size_t(kW * kH * 3) && all[3].irradiance[0] == 1.0f && all[3].irradiance[1] == 2.0f);
    CHECK(all[2].ambientFraction == all[3].ambientFraction && all[2].direction == all[3].direction);

    // The single-lightmap reader refuses two roots.
    StockLightmap one;
    CHECK(!extractStockLightmapFromDecompressedZone(zone.string(), one, &err));

    // Two lightmaps with one index: refused.
    std::vector<uint8_t> dup = lightmapRecord(0, 1, false, 0xFFFFFFFFFFFFFFFEull, 9, 40, true);
    dup.resize(dup.size() + 64, 0);
    dup.insert(dup.end(), second.begin(), second.end());
    writeZone(zone, 80000, 500, dup);
    CHECK(!extractStockLightmapsFromDecompressedZone(zone.string(), all, &err));
    CHECK(err.find("two lightmaps with index 9") != std::string::npos);

    // No root: an error, out untouched.
    writeZone(zone, 10000, 0, std::vector<uint8_t>(1, 0));
    all.assign(2, StockLightmap());
    CHECK(!extractStockLightmapsFromDecompressedZone(zone.string(), all, &err));
    CHECK(all.size() == 2 && err.find("no lightmap root") != std::string::npos);
}
