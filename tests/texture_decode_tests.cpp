// Tests for DDS -> RGBA8 decoding (material-browser thumbnails). DDS files are built in memory.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "../src/viewport/texture_decode.h"
#include "test_harness.h"

using namespace iw8r::app;
namespace fs = std::filesystem;

namespace {

void put32(std::string& s, size_t off, uint32_t v) {
    for (int i = 0; i < 4; ++i) s[off + i] = static_cast<char>((v >> (8 * i)) & 0xFF);
}

// 128-byte DDS header, plus the 20-byte DX10 extension when fourcc is "DX10".
std::string makeDds(uint32_t w, uint32_t h, const char* fourcc, uint32_t dxgi, const std::string& payload) {
    std::string s(128, '\0');
    s.replace(0, 4, "DDS ");
    put32(s, 4, 124);                    // dwSize
    put32(s, 8, 0x1 | 0x2 | 0x4 | 0x1000);  // flags: caps, height, width, pixelformat
    put32(s, 12, h);
    put32(s, 16, w);
    put32(s, 28, 1);                     // mip count
    put32(s, 76, 32);                    // DDS_PIXELFORMAT.dwSize
    put32(s, 80, 0x4);                   // DDPF_FOURCC
    s.replace(84, 4, fourcc);
    put32(s, 108, 0x1000);               // DDSCAPS_TEXTURE
    if (std::memcmp(fourcc, "DX10", 4) == 0) {
        s.append(20, '\0');
        put32(s, 128, dxgi);             // dxgiFormat
        put32(s, 132, 3);                // D3D10_RESOURCE_DIMENSION_TEXTURE2D
        put32(s, 144, 1);                // arraySize
    }
    s += payload;
    return s;
}

// LSB-first bit writer, the bit order BC7 uses.
struct BitWriter {
    uint8_t* out;
    int pos = 0;
    void put(uint32_t value, int bits) {
        for (int i = 0; i < bits; ++i, ++pos) {
            if ((value >> i) & 1u) out[pos / 8] |= static_cast<uint8_t>(1u << (pos % 8));
        }
    }
};

}  // namespace

TEST(dds_bc1_block_endpoints) {
    // Endpoints: c0 = pure red (0xF800), c1 = pure blue (0x001F). c0 > c1 selects the 4-colour mode.
    // Top two rows use index 0 (c0), bottom two rows index 1 (c1).
    uint8_t blk[8] = {0x00, 0xF8, 0x1F, 0x00, 0, 0, 0, 0};
    uint32_t indices = 0;
    for (int i = 8; i < 16; ++i) indices |= 1u << (2 * i);
    for (int k = 0; k < 4; ++k) blk[4 + k] = static_cast<uint8_t>(indices >> (8 * k));

    Rgba8Image img;
    std::string err;
    const std::string dds = makeDds(4, 4, "DX10", 71, std::string(reinterpret_cast<char*>(blk), 8));
    CHECK(decodeDds(dds, img, &err));
    CHECK(err.empty());
    CHECK(img.width == 4 && img.height == 4);
    CHECK(img.pixels.size() == 4 * 4 * 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const uint8_t* p = &img.pixels[(y * 4 + x) * 4];
            if (y < 2) {
                CHECK(p[0] == 255 && p[1] == 0 && p[2] == 0 && p[3] == 255);
            } else {
                CHECK(p[0] == 0 && p[1] == 0 && p[2] == 255 && p[3] == 255);
            }
        }
    }
}

TEST(dds_legacy_dxt1_matches_dx10_bc1) {
    uint8_t blk[8] = {0x00, 0xF8, 0x1F, 0x00, 0x55, 0x55, 0x55, 0x55};
    const std::string payload(reinterpret_cast<char*>(blk), 8);
    Rgba8Image a, b;
    CHECK(decodeDds(makeDds(4, 4, "DXT1", 0, payload), a));
    CHECK(decodeDds(makeDds(4, 4, "DX10", 71, payload), b));
    CHECK(a.pixels == b.pixels);
}

TEST(dds_rgba8_roundtrip) {
    std::string payload(4 * 4 * 4, '\0');
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>((i * 37 + 11) & 0xFF);
    Rgba8Image img;
    std::string err;
    CHECK(decodeDds(makeDds(4, 4, "DX10", 28, payload), img, &err));
    CHECK(img.width == 4 && img.height == 4);
    CHECK(img.pixels.size() == payload.size());
    bool same = img.pixels.size() == payload.size();
    for (size_t i = 0; same && i < payload.size(); ++i) same = img.pixels[i] == static_cast<uint8_t>(payload[i]);
    CHECK(same);
}

TEST(dds_r8_expands_to_grey) {
    std::string payload;
    for (int i = 0; i < 16; ++i) payload.push_back(static_cast<char>(i * 16));
    Rgba8Image img;
    CHECK(decodeDds(makeDds(4, 4, "DX10", 61, payload), img));
    for (int i = 0; i < 16; ++i) {
        const uint8_t* p = &img.pixels[i * 4];
        CHECK(p[0] == i * 16 && p[1] == i * 16 && p[2] == i * 16 && p[3] == 255);
    }
}

TEST(dds_bc4_block_grey_levels) {
    // red0 = 200, red1 = 0 (red0 > red1 selects the 8-level mode). Pixel 0 uses index 0, pixel 1 index 1,
    // pixel 2 index 2, the rest index 0.
    uint8_t blk[8] = {200, 0, 0, 0, 0, 0, 0, 0};
    const uint64_t bits = (uint64_t(1) << 3) | (uint64_t(2) << 6);
    for (int k = 0; k < 6; ++k) blk[2 + k] = static_cast<uint8_t>(bits >> (8 * k));

    Rgba8Image img;
    CHECK(decodeDds(makeDds(4, 4, "DX10", 80, std::string(reinterpret_cast<char*>(blk), 8)), img));
    const uint8_t* p0 = &img.pixels[0];
    const uint8_t* p1 = &img.pixels[4];
    const uint8_t* p2 = &img.pixels[8];
    CHECK(p0[0] == 200 && p0[1] == 200 && p0[2] == 200 && p0[3] == 255);
    CHECK(p1[0] == 0 && p1[1] == 0 && p1[2] == 0 && p1[3] == 255);
    CHECK(p2[0] == p2[1] && p2[1] == p2[2]);
    CHECK(p2[0] > 160 && p2[0] < 180);  // (6 * 200 + 0) / 7 = 171
    // Every pixel must be grey.
    bool grey = true;
    for (int i = 0; i < 16; ++i) grey = grey && img.pixels[i * 4] == img.pixels[i * 4 + 1] && img.pixels[i * 4] == img.pixels[i * 4 + 2];
    CHECK(grey);
}

TEST(dds_bc5_normal_z_reconstructed) {
    // R = G = 128 (about zero in signed space), so Z is about 1 and B encodes to about 255.
    uint8_t blk[16] = {128, 128, 0, 0, 0, 0, 0, 0, 128, 128, 0, 0, 0, 0, 0, 0};
    Rgba8Image img;
    CHECK(decodeDds(makeDds(4, 4, "DX10", 83, std::string(reinterpret_cast<char*>(blk), 16)), img));
    const uint8_t* p = &img.pixels[0];
    CHECK(p[0] == 128 && p[1] == 128);
    CHECK(p[2] >= 254);
    CHECK(p[3] == 255);
}

TEST(dds_bc7_mode6_endpoints_plausible) {
    // Mode 6: 7-bit mode marker (bit 6 set), R0 R1 G0 G1 B0 B1 A0 A1 at 7 bits each, P0 P1, then 4-bit indices
    // (3 bits for the anchor pixel). R0 = 0, R1 = 127 with p-bits 0 gives 8-bit R endpoints 0 and 254.
    // G endpoints are both 64 (128), A endpoints both 127 (254).
    uint8_t blk[16] = {0};
    BitWriter bw{blk};
    bw.put(0x40, 7);                              // mode 6
    bw.put(0, 7);                                 // R0
    bw.put(127, 7);                               // R1
    bw.put(64, 7);                                // G0
    bw.put(64, 7);                                // G1
    bw.put(0, 7);                                 // B0
    bw.put(0, 7);                                 // B1
    bw.put(127, 7);                               // A0
    bw.put(127, 7);                               // A1
    bw.put(0, 1);                                 // P0
    bw.put(0, 1);                                 // P1
    // Indices: anchor pixel 0 uses 3 bits. Pixel 0 index 0 (R = 0), pixel 15 index 15 (R = 254), others 8.
    bw.put(0, 3);
    for (int i = 1; i < 16; ++i) bw.put(i == 15 ? 15 : 8, 4);

    Rgba8Image img;
    std::string err;
    CHECK(decodeDds(makeDds(4, 4, "DX10", 98, std::string(reinterpret_cast<char*>(blk), 16)), img, &err));
    CHECK(err.empty());
    bool inRange = true;
    for (int i = 0; i < 16; ++i) {
        const uint8_t* p = &img.pixels[i * 4];
        inRange = inRange && p[0] <= 254 && p[1] == 128 && p[3] == 254;
    }
    CHECK(inRange);
    CHECK(img.pixels[0] == 0);
    CHECK(img.pixels[15 * 4] == 254);
    CHECK(img.pixels[1 * 4] > 0 && img.pixels[1 * 4] < 254);
}

TEST(dds_truncated_file_fails_with_error) {
    // A valid 4x4 BC1 file cut one byte short.
    const std::string full = makeDds(4, 4, "DX10", 71, std::string(8, '\x11'));
    const std::string truncated = full.substr(0, full.size() - 1);
    Rgba8Image img;
    std::string err;
    CHECK(!decodeDds(truncated, img, &err));
    CHECK(!err.empty());
    CHECK(img.pixels.empty());

    std::string shortHeader = full.substr(0, 100);
    err.clear();
    CHECK(!decodeDds(shortHeader, img, &err));
    CHECK(!err.empty());

    std::string badMagic = full;
    badMagic[0] = 'X';
    err.clear();
    CHECK(!decodeDds(badMagic, img, &err));
    CHECK(!err.empty());
}

TEST(dds_unsupported_format_fails_with_error) {
    Rgba8Image img;
    std::string err;
    CHECK(!decodeDds(makeDds(4, 4, "DX10", 2, std::string(64, '\0')), img, &err));  // R32G32B32A32_FLOAT
    CHECK(err.find("unsupported") != std::string::npos);
}

TEST(downscale_8x8_to_4x4_averages) {
    // R = x * 32 (0..224). Each 2x2 box averages to 64 * ox + 16 exactly.
    Rgba8Image in;
    in.width = 8;
    in.height = 8;
    in.pixels.resize(8 * 8 * 4);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            uint8_t* p = &in.pixels[(y * 8 + x) * 4];
            p[0] = static_cast<uint8_t>(x * 32);
            p[1] = 0;
            p[2] = 255;
            p[3] = 255;
        }
    }
    const Rgba8Image out = downscale(in, 4);
    CHECK(out.width == 4 && out.height == 4);
    CHECK(out.pixels.size() == 4 * 4 * 4);
    bool ok = out.pixels.size() == 4 * 4 * 4;
    for (int y = 0; ok && y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const uint8_t* p = &out.pixels[(y * 4 + x) * 4];
            ok = ok && p[0] == 64 * x + 16 && p[1] == 0 && p[2] == 255 && p[3] == 255;
        }
    }
    CHECK(ok);
}

TEST(downscale_keeps_aspect_and_small_images) {
    Rgba8Image in;
    in.width = 8;
    in.height = 4;
    in.pixels.assign(8 * 4 * 4, 90);
    const Rgba8Image out = downscale(in, 4);
    CHECK(out.width == 4 && out.height == 2);
    CHECK(out.pixels.size() == 4 * 2 * 4);

    const Rgba8Image same = downscale(in, 64);
    CHECK(same.width == 8 && same.height == 4 && same.pixels == in.pixels);
}

TEST(dds_real_files_decode_when_present) {
    // Opportunistic: decode up to five real DDS exports if the stock ship image folder exists.
    const fs::path roots[] = {"E:\\mw124-conversion-builds\\_stock_ship_images", "E:\\IW8-Radiant\\data\\stock_dumps"};
    std::vector<fs::path> files;
    for (const fs::path& root : roots) {
        std::error_code ec;
        if (!fs::exists(root, ec)) continue;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end && files.size() < 5; it.increment(ec)) {
            if (it->is_regular_file(ec) && it->path().extension() == ".dds") files.push_back(it->path());
        }
        if (files.size() >= 5) break;
    }
    if (files.empty()) {
        iw8r::test::skipf("  note: no DDS files found under the stock folders; real-file check skipped\n");
        return;
    }
    for (const fs::path& p : files) {
        std::FILE* fp = std::fopen(p.string().c_str(), "rb");
        CHECK(fp != nullptr);
        if (!fp) continue;
        std::string bytes;
        char buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0) bytes.append(buf, n);
        std::fclose(fp);

        Rgba8Image img;
        std::string err;
        const bool ok = decodeDds(bytes, img, &err);
        if (!ok) std::printf("  note: %s: %s\n", p.string().c_str(), err.c_str());
        CHECK(ok);
        if (!ok) continue;
        CHECK(img.width > 0 && img.height > 0);
        CHECK(img.pixels.size() == static_cast<size_t>(img.width) * img.height * 4);
    }
}

namespace {

// A square DX10 RGBA8 DDS with `stored` mip levels in the file and `declared` levels in the header. Every byte of
// level i is 10 * (i + 1), so a decoded pixel tells which level it came from.
std::string makeMippedRgba8(uint32_t size, uint32_t declared, uint32_t stored) {
    std::string payload;
    for (uint32_t level = 0, side = size; level < stored; ++level, side = side > 1 ? side / 2 : 1)
        payload.append(size_t(side) * side * 4, static_cast<char>(10 * (level + 1)));
    std::string dds = makeDds(size, size, "DX10", 28, payload);
    put32(dds, 28, declared);
    return dds;
}

}  // namespace

TEST(dds_preview_decodes_the_smallest_level_that_covers_the_request) {
    const std::string dds = makeMippedRgba8(16, 5, 5);  // 16, 8, 4, 2, 1
    Rgba8Image img;
    int fullW = 0, fullH = 0;
    std::string err;

    CHECK(decodeDdsPreview(dds, 4, img, &fullW, &fullH, &err));
    CHECK(err.empty());
    CHECK(fullW == 16 && fullH == 16);
    CHECK(img.width == 4 && img.height == 4 && img.pixels.size() == 4 * 4 * 4);
    CHECK(!img.pixels.empty() && img.pixels[0] == 30);  // level 2 is 4x4

    CHECK(decodeDdsPreview(dds, 8, img));
    CHECK(img.width == 8 && img.pixels[0] == 20);  // level 1

    // 5 texels wanted: level 2 (4x4) is too small, so level 1 (8x8) is decoded and box-filtered to 5.
    CHECK(decodeDdsPreview(dds, 5, img));
    CHECK(img.width == 5 && img.height == 5 && img.pixels[0] == 20);

    // Larger than the image, and "no limit": the top level, unscaled.
    CHECK(decodeDdsPreview(dds, 64, img));
    CHECK(img.width == 16 && img.pixels[0] == 10);
    CHECK(decodeDdsPreview(dds, 0, img));
    CHECK(img.width == 16 && img.pixels[0] == 10);

    CHECK(decodeDdsPreview(dds, 1, img));
    CHECK(img.width == 1 && img.height == 1 && img.pixels[0] == 50);  // the 1x1 level
}

TEST(dds_preview_without_mips_matches_decode_then_downscale) {
    std::string payload(8 * 8 * 4, '\0');
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>((i * 53 + 7) & 0xFF);
    const std::string dds = makeDds(8, 8, "DX10", 28, payload);
    Rgba8Image full, preview;
    CHECK(decodeDds(dds, full));
    CHECK(decodeDdsPreview(dds, 4, preview));
    const Rgba8Image expected = downscale(full, 4);
    CHECK(preview.width == expected.width && preview.height == expected.height && preview.pixels == expected.pixels);
}

TEST(dds_preview_uses_only_levels_the_file_holds) {
    // The header promises five levels, the file ends after two: level 1 is the smallest usable one.
    const std::string dds = makeMippedRgba8(16, 5, 2);
    Rgba8Image img;
    std::string err;
    CHECK(decodeDdsPreview(dds, 2, img, nullptr, nullptr, &err));
    CHECK(err.empty());
    CHECK(img.width == 2 && img.height == 2 && img.pixels[0] == 20);

    // Not even the top level is complete: the same error as a full decode.
    const std::string cut = dds.substr(0, 148 + 16 * 16 * 4 - 1);
    CHECK(!decodeDdsPreview(cut, 2, img, nullptr, nullptr, &err));
    CHECK(!err.empty());
    CHECK(img.pixels.empty());
}

TEST(dds_preview_bc1_reads_the_level_block) {
    // 8x8 BC1 with levels 8, 4, 2, 1: four blocks, then one block per smaller level. The 4x4 level is solid blue,
    // the top level solid red.
    const uint8_t red[8] = {0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0};
    const uint8_t blue[8] = {0x1F, 0x00, 0x1F, 0x00, 0, 0, 0, 0};
    std::string payload;
    for (int i = 0; i < 4; ++i) payload.append(reinterpret_cast<const char*>(red), 8);
    for (int i = 0; i < 3; ++i) payload.append(reinterpret_cast<const char*>(blue), 8);
    std::string dds = makeDds(8, 8, "DX10", 71, payload);
    put32(dds, 28, 4);
    Rgba8Image img;
    CHECK(decodeDdsPreview(dds, 4, img));
    CHECK(img.width == 4 && img.height == 4);
    CHECK(img.pixels.size() == 64 && img.pixels[0] == 0 && img.pixels[2] == 255);
    CHECK(decodeDdsPreview(dds, 8, img));
    CHECK(img.width == 8 && img.pixels[0] == 255 && img.pixels[2] == 0);
}

TEST(dds_preview_from_file_reads_the_same_pixels) {
    const std::string dds = makeMippedRgba8(16, 5, 5);
    const fs::path file = fs::temp_directory_path() / "iw8r_texture_decode_preview_test.dds";
    {
        std::FILE* fp = std::fopen(file.string().c_str(), "wb");
        CHECK(fp != nullptr);
        if (!fp) return;
        std::fwrite(dds.data(), 1, dds.size(), fp);
        std::fclose(fp);
    }
    Rgba8Image fromMemory, fromFile;
    int fullW = 0, fullH = 0;
    std::string err;
    CHECK(decodeDdsPreview(dds, 4, fromMemory));
    CHECK(loadDdsPreview(file.string(), 4, fromFile, &fullW, &fullH, &err));
    CHECK(err.empty());
    CHECK(fullW == 16 && fullH == 16);
    CHECK(fromFile.width == fromMemory.width && fromFile.height == fromMemory.height && fromFile.pixels == fromMemory.pixels);

    std::error_code ec;
    fs::remove(file, ec);
    CHECK(!loadDdsPreview(file.string(), 4, fromFile, nullptr, nullptr, &err));
    CHECK(!err.empty());
    CHECK(fromFile.pixels.empty());
}

// The viewport target compiles texture_decode.cpp on its own; this test binary does not link iw8r_viewport,
// so the implementation is compiled into this translation unit too.
#include "../src/viewport/texture_decode.cpp"
