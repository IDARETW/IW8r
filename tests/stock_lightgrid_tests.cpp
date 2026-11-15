// Stock static-model light grid volumes (src/core/stock_lightgrid.h, docs/research/105): R11G11B10F decode, the box
// table from a trzone export (values and hex forms), a synthetic atlas block (parse, texel-centre and trilinear
// samples, tile layout), E(n), the atlas search in a synthetic and a streamed "dec", and real mp_m_speedball data:
// tests/data/stock_lightgrid/speedball_v0_v994.iw8rlg (volumes 0 and 994 re-packed into a 2 x 2 x 1 atlas with their
// real texels; always present) and, when present on this machine, the full block in out/agent/smodel_lighting and the
// world cache's trzone export (skipped otherwise).
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "stock_lightgrid.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

namespace fs = std::filesystem;

void putU32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i)));
}
void putU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}
void putI64(std::vector<uint8_t>& b, int64_t v) {
    uint64_t u;
    std::memcpy(&u, &v, 8);
    for (int i = 0; i < 8; ++i) b.push_back(uint8_t(u >> (8 * i)));
}
void putF32(std::vector<uint8_t>& b, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    putU32(b, u);
}
void pad(std::vector<uint8_t>& b, size_t to) {
    while (b.size() < to) b.push_back(0);
}

std::string hexOf(const std::vector<uint8_t>& b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (uint8_t v : b) {
        s += d[v >> 4];
        s += d[v & 15];
    }
    return s;
}

// R11G11B10F from (exponent, mantissa) per channel: R, G 6-bit mantissa, B 5-bit.
uint32_t r11g11b10(uint32_t er, uint32_t mr, uint32_t eg, uint32_t mg, uint32_t eb, uint32_t mb) {
    return (mr & 63) | (er & 31) << 6 | (mg & 63) << 11 | (eg & 31) << 17 | (mb & 31) << 22 | (eb & 31) << 27;
}
// Encodes values of the form (1 + m / 2^bits) * 2^(e - 15) exactly (the test only uses such values).
uint32_t enc1(double v, int bits) {
    int e = 0;
    const double f = std::frexp(v, &e);  // v = f * 2^e, f in [0.5, 1)
    const uint32_t exp = uint32_t(e - 1 + 15);
    const uint32_t man = uint32_t(std::lround((f * 2 - 1) * double(1 << bits)));
    return exp << bits | man;
}
uint32_t encRgb(double r, double g, double b) {
    const uint32_t er = enc1(r, 6), eg = enc1(g, 6), eb = enc1(b, 5);
    return er | eg << 11 | eb << 22;
}

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
bool nearRel(double a, double b, double rel) { return std::fabs(a - b) <= rel * std::fabs(b) + 1e-9; }

// One 64-byte box record.
void putBox(std::vector<uint8_t>& b, uint32_t firstProbe, uint16_t sx, uint16_t sy, uint16_t sz, uint16_t flags,
            uint32_t singleProbe, const float rows[3][4]) {
    putU32(b, firstProbe);
    putU16(b, sx);
    putU16(b, sy);
    putU16(b, sz);
    putU16(b, flags);
    putU32(b, singleProbe);
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 4; ++k) putF32(b, rows[r][k]);
}

// A 240-byte atlas root as the zone stores it (research 105 section 1.3).
std::vector<uint8_t> makeRoot(uint32_t w, uint32_t d, uint32_t n) {
    std::vector<uint8_t> r;
    putU32(r, w);
    putU32(r, d);
    putU32(r, n);
    putU16(r, 0);  // m_worldTransient
    pad(r, 0x10);
    // texture 0: format 32, flags, bytes, w, h, d, elements, levels; then 0 and the pixel pointer -2
    putU32(r, 32);
    putU32(r, 0x810003);
    putU32(r, w * w * d * 4);
    putU16(r, uint16_t(w));
    putU16(r, uint16_t(w));
    putU16(r, uint16_t(d));
    putU16(r, 1);
    r.push_back(1);
    pad(r, 0x28);
    putI64(r, 0);
    putI64(r, -2);
    // texture 1 at 0x38
    putU32(r, 6);
    putU32(r, 0x810003);
    putU32(r, 2 * w * 3 * w * d * 4);
    putU16(r, uint16_t(2 * w));
    putU16(r, uint16_t(3 * w));
    putU16(r, uint16_t(d));
    putU16(r, 1);
    r.push_back(1);
    pad(r, 0x50);
    putI64(r, 0);
    putI64(r, -2);
    putI64(r, -2);  // 0x60 packing params pointer
    pad(r, 0xA8);
    putI64(r, -2);  // 0xA8 transform params pointer
    pad(r, 240);
    return r;
}

// The synthetic atlas: W 4, D 2, two volumes.
//   volume 0: 2 x 2 x 2 probes at texels (0..1, 0..1, 0..1). DC R = 1 + x, G = 1 + 3 y, B = z ? 0.5 : 2. Every tile
//             byte is 0 at x = 0 and 255 at x = 1 (normalised -1 / +1).
//   volume 1: one probe at texel (3, 2, 1), DC (8, 0.25, 3), value k (tile k / 4, channel k % 4) byte 10 k.
//   Every other texel holds junk (DC 64, bytes 77) that a sample must never pick up.
constexpr uint32_t kW = 4, kD = 2;
std::string syntheticBlock() {
    std::vector<uint32_t> tex0(kW * kW * kD, encRgb(64, 64, 64));
    std::vector<uint32_t> tex1(6 * kW * kW * kD, 0x4D4D4D4Du);
    const auto t1 = [&](uint32_t t, uint32_t x, uint32_t y, uint32_t z) -> uint32_t& {
        return tex1[(x + kW * (t & 1)) + 2 * kW * (y + kW * (t >> 1)) + 2 * kW * 3 * kW * z];
    };
    for (uint32_t z = 0; z < 2; ++z)
        for (uint32_t y = 0; y < 2; ++y)
            for (uint32_t x = 0; x < 2; ++x) {
                tex0[x + kW * (y + kW * z)] = encRgb(1 + x, 1 + 3 * y, z ? 0.5 : 2);
                for (uint32_t t = 0; t < 6; ++t) t1(t, x, y, z) = x ? 0xFFFFFFFFu : 0u;
            }
    tex0[3 + kW * (2 + kW * 1)] = encRgb(8, 0.25, 3);
    for (uint32_t t = 0; t < 6; ++t) {
        uint32_t px = 0;
        for (uint32_t ch = 0; ch < 4; ++ch) px |= (10 * (4 * t + ch)) << (8 * ch);
        t1(t, 3, 2, 1) = px;
    }
    std::vector<uint8_t> b = makeRoot(kW, kD, 2);
    for (uint32_t v : tex0) putU32(b, v);
    for (uint32_t v : tex1) putU32(b, v);
    // packing: uvScale (3 f), size (3 u8), basisType, uvBias (3 f), firstStaticProbeIndex
    const float pk[2][6] = {{1.0f / kW, 1.0f / kW, 1.0f / kD, 0.5f / kW, 0.5f / kW, 0.5f / kD},
                            {0, 0, 0, 3.5f / kW, 2.5f / kW, 1.5f / kD}};
    for (int v = 0; v < 2; ++v) {
        for (int k = 0; k < 3; ++k) putF32(b, pk[v][k]);
        b.push_back(v ? 1 : 2);
        b.push_back(v ? 1 : 2);
        b.push_back(v ? 1 : 2);
        b.push_back(1);
        for (int k = 3; k < 6; ++k) putF32(b, pk[v][k]);
        putU32(b, 0);
    }
    for (int v = 0; v < 2; ++v)  // transform params: rows, flags, three zero words
        for (int k = 0; k < 16; ++k) putF32(b, k % 5 == 0 ? 1.0f : 0.0f);
    return std::string(b.begin(), b.end());
}

double factor(int slot) { return (slot == 0 || slot == 2 || slot == 3) ? std::sqrt(3.0) : std::sqrt(5.0); }

// <repo>/<rel>, found by walking up from the working directory ("" when not found).
std::string repoFile(const std::string& rel) {
    fs::path p = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        const fs::path c = p / fs::path(rel);
        std::error_code ec;
        if (fs::exists(c, ec)) return c.string();
        if (!p.has_parent_path() || p.parent_path() == p) break;
        p = p.parent_path();
    }
    return {};
}

bool readAll(const std::string& path, std::string& out) {
    std::ifstream in(fs::path(path), std::ios::binary);
    if (!in) return false;
    std::ostringstream s;
    s << in.rdbuf();
    out = s.str();
    return true;
}

// Pseudo-random junk bytes.
std::string junk(size_t n, uint32_t seed) {
    std::string s(n, '\0');
    uint32_t x = seed;
    for (char& c : s) {
        x = x * 1664525u + 1013904223u;
        c = char(x >> 24);
    }
    return s;
}

// Research 105 section 1.4's reference table (mp_m_speedball, box centre).
struct Ref {
    double dc[3], up[3], down[3];
};
const Ref kRef0 = {{13.5, 14.0, 14.25}, {4.833, 5.320, 6.234}, {2.961, 2.801, 2.072}};
const Ref kRef994 = {{27.5, 27.25, 26.0}, {8.595, 8.777, 8.929}, {7.054, 6.767, 5.938}};

void checkRef(const StockLgvAtlas& a, size_t volume, const Ref& ref) {
    const double centre[3] = {0.5, 0.5, 0.5}, up[3] = {0, 0, 1}, down[3] = {0, 0, -1};
    const StockLgvSh s = sampleStockLgv(a, volume, centre);
    double eu[3], ed[3];
    evalStockLgv(s, up, eu);
    evalStockLgv(s, down, ed);
    for (int c = 0; c < 3; ++c) {
        CHECK(nearRel(s.dc[c], ref.dc[c], 0.02));
        CHECK(nearRel(eu[c], ref.up[c], 0.02));
        CHECK(nearRel(ed[c], ref.down[c], 0.02));
    }
}

}  // namespace

TEST(stock_lgv_r11g11b10f_decode) {
    float rgb[3];
    decodeR11G11B10F(0x966584ACu, rgb);  // speedball volume 0 (research 105 section 1.4)
    CHECK(rgb[0] == 13.5f && rgb[1] == 14.0f && rgb[2] == 14.25f);
    decodeR11G11B10F(0x9D276CEEu, rgb);  // volume 994
    CHECK(rgb[0] == 27.5f && rgb[1] == 27.25f && rgb[2] == 26.0f);
    decodeR11G11B10F(r11g11b10(15, 0, 15, 0, 15, 0), rgb);
    CHECK(rgb[0] == 1.0f && rgb[1] == 1.0f && rgb[2] == 1.0f);
    decodeR11G11B10F(r11g11b10(15, 32, 16, 16, 14, 16), rgb);  // 1.5, 2.5, 0.75
    CHECK(rgb[0] == 1.5f && rgb[1] == 2.5f && rgb[2] == 0.75f);
    decodeR11G11B10F(0, rgb);
    CHECK(rgb[0] == 0.0f && rgb[1] == 0.0f && rgb[2] == 0.0f);
    decodeR11G11B10F(r11g11b10(0, 32, 0, 1, 0, 16), rgb);  // denormals: m / 2^bits * 2^-14
    CHECK(rgb[0] == std::ldexp(0.5f, -14) && rgb[1] == std::ldexp(1.0f / 64, -14) && rgb[2] == std::ldexp(0.5f, -14));
    decodeR11G11B10F(r11g11b10(31, 0, 31, 1, 30, 31), rgb);  // inf, NaN, the largest B
    CHECK(std::isinf(rgb[0]) && std::isnan(rgb[1]) && rgb[2] == float((1 + 31.0 / 32) * std::ldexp(1.0, 15)));
    CHECK(encRgb(13.5, 14.0, 14.25) == 0x966584ACu);  // the test's own encoder agrees with the stock word
}

TEST(stock_lgv_boxes_values_and_hex) {
    const float rows0[3][4] = {{0.015625f, 0.0f, 0.0f, -2.5f}, {0.0f, 0.03125f, 0.0f, 0.1f}, {0.0f, 0.0f, 0.0625f, 7.0f}};
    const float rows1[3][4] = {{0.1f, -0.2f, 0.3f, 1e-4f}, {-0.0012051171f, 0.5f, 0.0f, -651.8f}, {0.0f, 0.0f, 1.0f, 0.0f}};
    std::vector<uint8_t> table;
    putBox(table, 0, 1, 1, 1, 0, 0, rows0);
    putBox(table, 1, 4, 2, 3, 4 | 2, 0xFFFFFFFFu, rows1);
    CHECK(table.size() == 128);

    const auto rowJson = [](const float r[4]) {
        std::string s = R"({"$union_views":true,"v":[)";
        for (int k = 0; k < 4; ++k) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%s%.17g", k ? "," : "", double(r[k]));
            s += buf;
        }
        return s + R"(],"__s1":{"x":0,"y":0,"z":0,"w":0}})";
    };
    // Other members around it, with braces and an escaped quote inside strings, to exercise the targeted scan.
    const std::string head = R"({"format":"mw19-asset-json","asset":{"type":"GfxWorldTransientZone","fields":{"name":{"string":"x { [ \" ] }"},)"
                             R"("gpuLightGrid":{"gpuLightGrid":{"probeCount":2,"probes":{"bytes":"00ff"}},"staticModelVolumeCount":2,)";
    const std::string tail = R"(,"SMLGppZone":null,"tempLightGridVolumeData":null}}}})";
    const std::string valuesJson =
        head + R"("staticModelVolumes":{"address":"0x1","type":"GfxStaticModelLightGridVolume","stride":64,"count":2,"values":[)" +
        R"({"firstProbe":0,"m_volumeSize":[1,1,1],"flags":{"value":0,"name":"LGV_SMODEL_FLAGS_EMTPY"},"singleProbe":0,"volumeTransform":[)" +
        rowJson(rows0[0]) + "," + rowJson(rows0[1]) + "," + rowJson(rows0[2]) + "]}," +
        R"({"firstProbe":1,"m_volumeSize":[4,2,3],"flags":{"value":6,"name":"X"},"singleProbe":4294967295,"volumeTransform":[)" +
        rowJson(rows1[0]) + "," + rowJson(rows1[1]) + "," + rowJson(rows1[2]) + "]}]}" + tail;
    const std::string hexJson = head + R"("staticModelVolumes" : {"stride":64,"count":2,"encoding":"hex-little-endian","bytes":")" +
                                hexOf(table) + R"("})" + tail;

    for (const std::string* text : {&valuesJson, &hexJson}) {
        std::vector<StockLgvBox> boxes;
        std::vector<uint8_t> raw;
        std::string err;
        CHECK(parseStockLgvBoxes(*text, boxes, &raw, &err));
        CHECK(err.empty());
        CHECK(raw == table);  // the values form rebuilds the zone's exact bytes
        CHECK(boxes.size() == 2);
        if (boxes.size() != 2) continue;
        CHECK(boxes[0].firstProbe == 0 && boxes[0].singleProbe == 0 && boxes[0].flags == 0);
        CHECK(boxes[0].size[0] == 1 && boxes[0].size[1] == 1 && boxes[0].size[2] == 1);
        CHECK(boxes[1].firstProbe == 1 && boxes[1].singleProbe == 0xFFFFFFFFu && boxes[1].flags == 6);
        CHECK(boxes[1].size[0] == 4 && boxes[1].size[1] == 2 && boxes[1].size[2] == 3);
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 4; ++k) {
                CHECK(boxes[0].row[r][k] == double(rows0[r][k]));
                CHECK(boxes[1].row[r][k] == double(rows1[r][k]));
            }
    }

    std::vector<StockLgvBox> boxes;
    std::string err;
    // No staticModelVolumes, a wrong count, a wrong stride, bad hex: failures.
    CHECK(!parseStockLgvBoxes(R"({"asset":{"fields":{"gpuLightGrid":{"staticModelVolumeCount":0}}}})", boxes, nullptr, &err));
    CHECK(!err.empty());
    std::string wrongCount = hexJson;
    wrongCount.replace(wrongCount.find(R"("staticModelVolumeCount":2)"), 26, R"("staticModelVolumeCount":3)");
    CHECK(!parseStockLgvBoxes(wrongCount, boxes, nullptr, &err));
    std::string wrongStride = hexJson;
    wrongStride.replace(wrongStride.find(R"("stride":64)"), 11, R"("stride":32)");
    CHECK(!parseStockLgvBoxes(wrongStride, boxes, nullptr, &err));
    std::string badHex = hexJson;
    badHex.replace(badHex.find(R"("bytes":")", badHex.find(R"("staticModelVolumes" :)")) + 9, 2, "zz");
    CHECK(!parseStockLgvBoxes(badHex, boxes, nullptr, &err));
    // A zone without volumes: null, count 0.
    std::vector<uint8_t> raw{1, 2, 3};
    CHECK(parseStockLgvBoxes(R"({"gpuLightGrid":{"staticModelVolumeCount":0,"staticModelVolumes":null}})", boxes, &raw, &err));
    CHECK(boxes.empty() && raw.empty());
}

TEST(stock_lgv_block_parse_and_sample) {
    const std::string block = syntheticBlock();
    CHECK(block.size() == 240 + 4 * 4 * 2 * 4 + 8 * 12 * 2 * 4 + 2 * 96);
    StockLgvAtlas a;
    std::string err;
    CHECK(parseStockLgvBlock(block, a, &err));
    CHECK(a.width == 4 && a.depth == 2);
    CHECK(a.tex0.size() == 32 && a.tex1.size() == 192 && a.pack.size() == 2);
    if (a.pack.size() != 2) return;
    CHECK(a.pack[0].uvScale[0] == 0.25f && a.pack[0].uvScale[2] == 0.5f && a.pack[0].uvBias[1] == 0.125f);
    CHECK(a.pack[1].uvScale[0] == 0.0f && a.pack[1].uvBias[0] == 0.875f && a.pack[1].uvBias[2] == 0.75f);
    CHECK(a.xform.size() == 2);
    if (a.xform.size() == 2) {
        CHECK(a.xform[1].row[0][0] == 1.0f && a.xform[1].row[1][1] == 1.0f && a.xform[1].row[2][2] == 1.0f);
        CHECK(a.xform[1].row[0][3] == 0.0f && a.xform[1].row[2][1] == 0.0f && a.xform[1].flags == 0);
    }

    // At a texel centre the sample is that texel: coord 0 = the first probe, coord 1 = the last.
    const auto expectAt = [&](size_t vol, double cx, double cy, double cz, const double dc[3], double q) {
        const double c[3] = {cx, cy, cz};
        const StockLgvSh s = sampleStockLgv(a, vol, c);
        for (int ch = 0; ch < 3; ++ch) {
            CHECK(near(s.dc[ch], dc[ch], 1e-5));
            for (int j = 0; j < 8; ++j) CHECK(near(s.sh[j][ch], q * dc[ch] * factor(j), 1e-4));
        }
    };
    const double dc000[3] = {1, 1, 2}, dc111[3] = {2, 4, 0.5};
    expectAt(0, 0, 0, 0, dc000, -1);
    expectAt(0, 1, 1, 1, dc111, 1);
    expectAt(0, -3, -0.5, -1e9, dc000, -1);  // clamped to [0, 1]
    expectAt(0, 2, 1.5, 1e9, dc111, 1);
    // Between texels: trilinear, DC and the normalised bytes filtered separately, then multiplied.
    const double dcA[3] = {1.25, 1, 2};
    expectAt(0, 0.25, 0, 0, dcA, -0.5);
    const double dcB[3] = {1.5, 2.5, 1.25};
    expectAt(0, 0.5, 0.5, 0.5, dcB, 0);
    const double dcC[3] = {1.75, 1.75, 0.875};
    expectAt(0, 0.75, 0.25, 0.75, dcC, 0.5);

    // The single probe: any coordinate gives its texel; the six tiles and channels map to slot j, channel c.
    for (double c : {0.0, 0.3, 1.0}) {
        const double coord[3] = {c, 1 - c, c};
        const StockLgvSh s = sampleStockLgv(a, 1, coord);
        CHECK(near(s.dc[0], 8, 1e-5) && near(s.dc[1], 0.25, 1e-6) && near(s.dc[2], 3, 1e-5));
        for (int k = 0; k < 24; ++k) {
            const int ch = k % 3, j = k / 3;
            const double q = (10.0 * k) / 255.0 * 2 - 1;
            CHECK(near(s.sh[j][ch], q * s.dc[ch] * factor(j), 1e-4));
        }
    }

    // Out of range volume: zeros.
    const double c0[3] = {0.5, 0.5, 0.5};
    const StockLgvSh none = sampleStockLgv(a, 2, c0);
    CHECK(none.dc[0] == 0 && none.sh[7][2] == 0);

    // Rejections: truncated, a wrong texture format, an extra byte.
    StockLgvAtlas bad;
    CHECK(!parseStockLgvBlock(block.substr(0, block.size() - 1), bad, &err));
    CHECK(!parseStockLgvBlock(block + std::string(1, '\0'), bad, &err));
    std::string wrongFormat = block;
    wrongFormat[0x10] = 31;
    CHECK(!parseStockLgvBlock(wrongFormat, bad, &err));
    CHECK(!parseStockLgvBlock(std::string(100, '\0'), bad, &err));
    CHECK(bad.pack.empty() && bad.tex0.empty());
}

TEST(stock_lgv_eval_known_sh) {
    const double up[3] = {0, 0, 1}, down[3] = {0, 0, -1}, px[3] = {1, 0, 0}, py[3] = {0, 1, 0};
    const double s2 = std::sqrt(0.5), diag[3] = {s2, s2, 0};
    double e[3];

    StockLgvSh s;  // DC only: E = 0.282095 DC in every direction
    s.dc[0] = 1;
    s.dc[1] = 2;
    s.dc[2] = 3;
    for (const double* n : {up, down, px, diag}) {
        evalStockLgv(s, n, e);
        CHECK(near(e[0], 0.282095, 1e-9) && near(e[1], 0.56419, 1e-9) && near(e[2], 0.846285, 1e-9));
    }

    const double l1 = 4.0 / 9.0 * 0.488603;
    s.sh[0][0] = 1;  // slot 0: + z
    evalStockLgv(s, up, e);
    CHECK(near(e[0], 0.282095 + l1, 1e-9));
    evalStockLgv(s, down, e);
    CHECK(near(e[0], 0.282095 - l1, 1e-9));
    s.sh[0][0] = 0;
    s.sh[2][1] = 1;  // slot 2: - y
    evalStockLgv(s, py, e);
    CHECK(near(e[1], 0.56419 - l1, 1e-9) && near(e[0], 0.282095, 1e-9));
    s.sh[2][1] = 0;
    s.sh[3][2] = 1;  // slot 3: - x
    evalStockLgv(s, px, e);
    CHECK(near(e[2], 0.846285 - l1, 1e-9));
    s.sh[3][2] = 0;

    s.sh[1][0] = 2;  // slot 1: 3 z^2 - 1 (band weight 1/16)
    evalStockLgv(s, up, e);
    CHECK(near(e[0], 0.282095 + 0.0625 * 0.315392 * 2 * 2, 1e-9));
    evalStockLgv(s, px, e);
    CHECK(near(e[0], 0.282095 - 0.0625 * 0.315392 * 2, 1e-9));
    s.sh[1][0] = 0;
    s.sh[7][0] = 1;  // slot 7: x y
    s.sh[6][0] = 1;  // slot 6: x^2 - y^2 (0 on the diagonal)
    evalStockLgv(s, diag, e);
    CHECK(near(e[0], 0.282095 + 0.0625 * 1.092548 * 0.5, 1e-9));
    evalStockLgv(s, px, e);
    CHECK(near(e[0], 0.282095 + 0.0625 * 0.546274, 1e-9));
    s.sh[6][0] = s.sh[7][0] = 0;
    const double xz[3] = {s2, 0, s2};
    s.sh[4][0] = 1;  // slot 4: - x z
    evalStockLgv(s, xz, e);
    CHECK(near(e[0], 0.282095 - 0.0625 * 1.092548 * 0.5, 1e-9));
    s.sh[4][0] = 0;

    // Negative irradiance is clamped to 0.
    s.sh[0][1] = -10;
    evalStockLgv(s, up, e);
    CHECK(e[1] == 0.0 && near(e[0], 0.282095, 1e-9));
}

TEST(stock_lgv_extract_synthetic) {
    const std::string block = syntheticBlock();
    const float rows[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
    std::vector<uint8_t> table;
    putBox(table, 0, 2, 2, 2, 0, 0xFFFFFFFFu, rows);
    putBox(table, 8, 1, 1, 1, 0, 8, rows);
    const std::string tableStr(table.begin(), table.end());
    std::string out, err;

    // junk + table + block + junk: found right after the table.
    const std::string dec = junk(1000, 1) + tableStr + block + junk(500, 2);
    CHECK(extractStockLgvBlock(dec, table, 2, out, &err));
    CHECK(out == block);
    // Something between the table and the root (a non-null SMLGppZone): found by the signature.
    const std::string gap = junk(1000, 3) + tableStr + junk(24, 4) + block + junk(500, 5);
    CHECK(extractStockLgvBlock(gap, table, 2, out, &err));
    CHECK(out == block);
    // No table given: the signature alone. A decoy root (same counts, pointers not -2) before it is skipped.
    std::string decoyBlock = block;
    decoyBlock[0xA8] = 0;
    const std::string decoy = junk(300, 6) + decoyBlock.substr(0, 240) + junk(64, 7) + block;
    CHECK(extractStockLgvBlock(decoy, {}, 2, out, &err));
    CHECK(out == block);
    // Failures: wrong count, no volumes, table size mismatch, no atlas, a truncated atlas.
    CHECK(!extractStockLgvBlock(dec, {}, 3, out, &err));
    CHECK(out.empty() && !err.empty());
    CHECK(!extractStockLgvBlock(dec, table, 0, out, &err));
    CHECK(!extractStockLgvBlock(dec, table, 1, out, &err));
    CHECK(!extractStockLgvBlock(junk(5000, 8) + tableStr + junk(5000, 9), table, 2, out, &err));
    CHECK(!extractStockLgvBlock(junk(100, 10) + tableStr + block.substr(0, block.size() - 1), table, 2, out, &err));

    // Streamed from a file, with the table and (second file) the root across the 4 MB window boundary.
    const size_t chunk = size_t(4) << 20;
    const fs::path f1 = fs::path(iw8r::test::tmpDir()) / "stock_lgv_dec1.bin";
    const fs::path f2 = fs::path(iw8r::test::tmpDir()) / "stock_lgv_dec2.bin";
    {
        std::ofstream o(f1, std::ios::binary);
        const std::string s = junk(chunk - 50, 11) + tableStr + block + junk(100, 12);
        o.write(s.data(), std::streamsize(s.size()));
    }
    {
        std::ofstream o(f2, std::ios::binary);
        const std::string s = junk(chunk - 100, 13) + block + junk(100, 14);
        o.write(s.data(), std::streamsize(s.size()));
    }
    CHECK(extractStockLgvBlockFromFile(f1.string(), table, 2, out, &err));
    CHECK(out == block);
    CHECK(extractStockLgvBlockFromFile(f2.string(), table, 2, out, &err));  // table absent: signature
    CHECK(out == block);
    CHECK(extractStockLgvBlockFromFile(f2.string(), {}, 2, out, &err));
    CHECK(out == block);
    CHECK(!extractStockLgvBlockFromFile((fs::path(iw8r::test::tmpDir()) / "stock_lgv_missing.bin").string(), table, 2, out, &err));
    std::error_code ec;
    fs::remove(f1, ec);
    fs::remove(f2, ec);
}

TEST(stock_lgv_speedball_fixture) {
    const std::string path = repoFile("tests/data/stock_lightgrid/speedball_v0_v994.iw8rlg");
    std::string block;
    if (path.empty() || !readAll(path, block)) {
        iw8r::test::skipf("  SKIP tests/data/stock_lightgrid/speedball_v0_v994.iw8rlg not found\n");
        return;
    }
    StockLgvAtlas a;
    std::string err;
    CHECK(parseStockLgvBlock(block, a, &err));
    CHECK(a.width == 2 && a.depth == 1 && a.pack.size() == 2);
    if (a.pack.size() != 2) return;
    CHECK(a.tex0[0] == 0x966584ACu && a.tex0[1] == 0x9D276CEEu);  // the raw DC words of research 105's table
    checkRef(a, 0, kRef0);
    checkRef(a, 1, kRef994);  // speedball volume 994 sits in slot 1 of the cut
}

TEST(stock_lgv_speedball_real) {
    const std::string lgv = repoFile("out/agent/smodel_lighting/speedball_lgv.bin");
    std::string file;
    if (lgv.empty() || !readAll(lgv, file)) {
        iw8r::test::skipf("  SKIP out/agent/smodel_lighting/speedball_lgv.bin not found\n");
        return;
    }
    // The research cut is the box table (3,476 x 64 bytes) followed by the block: a small "dec".
    const size_t n = 3476;
    CHECK(file.size() > n * 64);
    if (file.size() <= n * 64) return;
    std::vector<uint8_t> table(file.begin(), file.begin() + std::ptrdiff_t(n * 64));

    const char* local = std::getenv("LOCALAPPDATA");
    const fs::path tz = local ? fs::path(local) / "IW8 Radiant Suite" / "IW8 Radiant Suite" / "world_cache" / "mp_m_speedball" /
                                    "assets" / "gfx_map_trzone" / "mp_m_speedball.0.asset.json"
                              : fs::path();
    std::string tzText;
    std::error_code ec;
    if (!tz.empty() && fs::exists(tz, ec) && readAll(tz.string(), tzText)) {
        std::vector<StockLgvBox> boxes;
        std::vector<uint8_t> raw;
        std::string err;
        CHECK(parseStockLgvBoxes(tzText, boxes, &raw, &err));
        CHECK(boxes.size() == n);
        CHECK(raw == table);  // the values-form export rebuilds the zone's bytes exactly
        if (boxes.size() == n) {
            CHECK(boxes[0].firstProbe == 0 && boxes[0].singleProbe == 0 && boxes[0].size[0] == 1);
            CHECK(boxes[994].firstProbe == 1272 && boxes[994].singleProbe == 1272);
            CHECK(near(boxes[0].row[0][3], 6.9775495529174805, 1e-6));
        }
    } else {
        iw8r::test::skipf("  SKIP mp_m_speedball trzone export not in the world cache (box table JSON part)\n");
    }

    std::string block, err;
    CHECK(extractStockLgvBlock(file, table, n, block, &err));
    CHECK(block.size() == 240 + 64 * 64 * 8 * 4 + 128 * 192 * 8 * 4 + 96 * n);
    CHECK(block == file.substr(n * 64));
    std::string viaFile;
    CHECK(extractStockLgvBlockFromFile(lgv, table, n, viaFile, &err));
    CHECK(viaFile == block);
    CHECK(extractStockLgvBlockFromFile(lgv, {}, n, viaFile, &err));  // by signature
    CHECK(viaFile == block);

    StockLgvAtlas a;
    CHECK(parseStockLgvBlock(block, a, &err));
    CHECK(a.width == 64 && a.depth == 8 && a.pack.size() == n);
    if (a.pack.size() != n) return;
    CHECK(near(a.pack[0].uvBias[0] * 64, 62.5, 1e-4) && near(a.pack[0].uvBias[2] * 8, 6.5, 1e-4));
    CHECK(a.xform.size() == n);
    if (a.xform.size() == n) CHECK(near(a.xform[0].row[0][3], 6.9775495529174805, 1e-5));  // = the exported box rows
    checkRef(a, 0, kRef0);
    checkRef(a, 994, kRef994);
}
