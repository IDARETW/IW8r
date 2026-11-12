// DDS -> RGBA8 decoding. Self-contained on purpose: iw8r_tests includes this file directly
// (tests/texture_decode_tests.cpp) because it does not link iw8r_viewport.
#define BCDEC_IMPLEMENTATION
#include "../../third_party/bcdec/bcdec.h"

#include "texture_decode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace iw8r::app {
namespace {

// DXGI_FORMAT values we accept in the DX10 header.
enum DxgiFormat : uint32_t {
    kDxgiR16G16B16A16Float = 10,
    kDxgiR8G8B8A8Unorm = 28,
    kDxgiR8G8B8A8UnormSrgb = 29,
    kDxgiR8Unorm = 61,
    kDxgiBc1Unorm = 71,
    kDxgiBc1UnormSrgb = 72,
    kDxgiBc2Unorm = 74,
    kDxgiBc2UnormSrgb = 75,
    kDxgiBc3Unorm = 77,
    kDxgiBc3UnormSrgb = 78,
    kDxgiBc4Unorm = 80,
    kDxgiBc5Unorm = 83,
    kDxgiBc6hUF16 = 95,
    kDxgiBc6hSF16 = 96,
    kDxgiBc7Unorm = 98,
    kDxgiBc7UnormSrgb = 99,
};

enum class Codec { Bc1, Bc2, Bc3, Bc4, Bc5, Bc6hUF16, Bc6hSF16, Bc7, Rgba8, R8, Rgba16F };

bool isBlockCodec(Codec c) { return c <= Codec::Bc7; }

size_t blockBytes(Codec c) {
    switch (c) {
        case Codec::Bc1:
        case Codec::Bc4: return 8;
        default: return 16;
    }
}

size_t texelBytes(Codec c) {
    switch (c) {
        case Codec::Rgba8: return 4;
        case Codec::R8: return 1;
        case Codec::Rgba16F: return 8;
        default: return 0;
    }
}

bool codecFromDxgi(uint32_t f, Codec& c) {
    switch (f) {
        case kDxgiBc1Unorm:
        case kDxgiBc1UnormSrgb: c = Codec::Bc1; return true;
        case kDxgiBc2Unorm:
        case kDxgiBc2UnormSrgb: c = Codec::Bc2; return true;
        case kDxgiBc3Unorm:
        case kDxgiBc3UnormSrgb: c = Codec::Bc3; return true;
        case kDxgiBc4Unorm: c = Codec::Bc4; return true;
        case kDxgiBc5Unorm: c = Codec::Bc5; return true;
        case kDxgiBc6hUF16: c = Codec::Bc6hUF16; return true;
        case kDxgiBc6hSF16: c = Codec::Bc6hSF16; return true;
        case kDxgiBc7Unorm:
        case kDxgiBc7UnormSrgb: c = Codec::Bc7; return true;
        case kDxgiR8G8B8A8Unorm:
        case kDxgiR8G8B8A8UnormSrgb: c = Codec::Rgba8; return true;
        case kDxgiR8Unorm: c = Codec::R8; return true;
        case kDxgiR16G16B16A16Float: c = Codec::Rgba16F; return true;
        default: return false;
    }
}

bool codecFromFourCC(const char* cc, Codec& c) {
    if (std::memcmp(cc, "DXT1", 4) == 0) { c = Codec::Bc1; return true; }
    if (std::memcmp(cc, "DXT2", 4) == 0 || std::memcmp(cc, "DXT3", 4) == 0) { c = Codec::Bc2; return true; }
    if (std::memcmp(cc, "DXT4", 4) == 0 || std::memcmp(cc, "DXT5", 4) == 0) { c = Codec::Bc3; return true; }
    if (std::memcmp(cc, "ATI1", 4) == 0 || std::memcmp(cc, "BC4U", 4) == 0) { c = Codec::Bc4; return true; }
    if (std::memcmp(cc, "ATI2", 4) == 0 || std::memcmp(cc, "BC5U", 4) == 0) { c = Codec::Bc5; return true; }
    return false;
}

uint32_t readU32(const std::string& s, size_t off) {
    const auto* b = reinterpret_cast<const uint8_t*>(s.data() + off);
    return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
}

uint16_t readU16(const uint8_t* b) { return uint16_t(b[0] | (b[1] << 8)); }

float halfToFloat(uint16_t h) {
    const bool neg = (h & 0x8000u) != 0;
    const uint32_t e = (h >> 10) & 0x1Fu;
    const uint32_t m = h & 0x3FFu;
    float v;
    if (e == 0) {
        v = std::ldexp(static_cast<float>(m), -24);  // subnormal
    } else if (e == 31) {
        v = m ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
    } else {
        v = std::ldexp(static_cast<float>(m | 0x400u), static_cast<int>(e) - 25);
    }
    return neg ? -v : v;
}

// HDR -> LDR: x/(1+x) then gamma 1/2.2. Negative and NaN values clamp to black.
uint8_t hdrToByte(float x) {
    if (!(x > 0.0f)) return 0;
    const float v = std::isinf(x) ? 1.0f : x / (1.0f + x);
    const float g = std::pow(v, 1.0f / 2.2f);
    return static_cast<uint8_t>(std::lround(std::clamp(g, 0.0f, 1.0f) * 255.0f));
}

// Linear alpha-style channel: clamp to [0,1].
uint8_t linearToByte(float x) {
    if (!(x > 0.0f)) return 0;
    return static_cast<uint8_t>(std::lround(std::clamp(x, 0.0f, 1.0f) * 255.0f));
}

// Decodes one 4x4 block of a block-compressed format into 16 RGBA8 texels (row-major).
void decodeBlock(Codec c, const uint8_t* blk, uint8_t* tile) {
    switch (c) {
        case Codec::Bc1: bcdec_bc1(blk, tile, 16); break;
        case Codec::Bc2: bcdec_bc2(blk, tile, 16); break;
        case Codec::Bc3: bcdec_bc3(blk, tile, 16); break;
        case Codec::Bc7: bcdec_bc7(blk, tile, 16); break;
        case Codec::Bc4: {
            uint8_t r[16];
            bcdec_bc4(blk, r, 4);
            for (int i = 0; i < 16; ++i) {
                tile[i * 4 + 0] = tile[i * 4 + 1] = tile[i * 4 + 2] = r[i];
                tile[i * 4 + 3] = 255;
            }
            break;
        }
        case Codec::Bc5: {
            uint8_t rg[32];
            bcdec_bc5(blk, rg, 8);
            for (int i = 0; i < 16; ++i) {
                const float x = rg[i * 2 + 0] / 255.0f * 2.0f - 1.0f;
                const float y = rg[i * 2 + 1] / 255.0f * 2.0f - 1.0f;
                const float z2 = 1.0f - x * x - y * y;
                const float z = z2 > 0.0f ? std::sqrt(z2) : 0.0f;
                tile[i * 4 + 0] = rg[i * 2 + 0];
                tile[i * 4 + 1] = rg[i * 2 + 1];
                tile[i * 4 + 2] = static_cast<uint8_t>(std::lround((z * 0.5f + 0.5f) * 255.0f));
                tile[i * 4 + 3] = 255;
            }
            break;
        }
        case Codec::Bc6hUF16:
        case Codec::Bc6hSF16: {
            float rgb[48];  // 4 rows of 4 texels x 3 channels
            bcdec_bc6h_float(blk, rgb, 12, c == Codec::Bc6hSF16 ? 1 : 0);
            for (int i = 0; i < 16; ++i) {
                tile[i * 4 + 0] = hdrToByte(rgb[i * 3 + 0]);
                tile[i * 4 + 1] = hdrToByte(rgb[i * 3 + 1]);
                tile[i * 4 + 2] = hdrToByte(rgb[i * 3 + 2]);
                tile[i * 4 + 3] = 255;
            }
            break;
        }
        default: break;
    }
}

// What the header says about the pixel data.
struct DdsLayout {
    Codec codec{};
    uint32_t width = 0, height = 0;
    uint32_t mipCount = 1;  // levels stored for each slice (1 when the header gives none)
    size_t dataOffset = 128;
    bool volume = false;    // depth > 1: levels are not skipped (every level holds several slices)
};

// Parses the 128-byte header and the DX10 extension. `size` is the number of header bytes at hand.
bool parseHeader(const std::string& f, DdsLayout& l, std::string* err) {
    auto fail = [err](const std::string& msg) {
        if (err) *err = msg;
        return false;
    };

    if (f.size() < 128) return fail("file too small for a DDS header");
    if (f.compare(0, 4, "DDS ") != 0) return fail("missing DDS magic");
    if (readU32(f, 4) != 124) return fail("unexpected DDS header size");

    const uint32_t flags = readU32(f, 8);
    l.height = readU32(f, 12);
    l.width = readU32(f, 16);
    const uint32_t depth = readU32(f, 24);
    const uint32_t mips = readU32(f, 28);
    const uint32_t pfFlags = readU32(f, 80);
    const char* fourcc = f.data() + 84;

    constexpr uint32_t kDdpfFourCC = 0x4;
    if ((pfFlags & kDdpfFourCC) == 0) return fail("uncompressed legacy DDS pixel formats are not supported");

    l.dataOffset = 128;
    if (std::memcmp(fourcc, "DX10", 4) == 0) {
        if (f.size() < 148) return fail("truncated DX10 header");
        const uint32_t dxgi = readU32(f, 128);
        l.dataOffset = 148;
        if (!codecFromDxgi(dxgi, l.codec)) return fail("unsupported DXGI format " + std::to_string(dxgi));
        if (readU32(f, 132) == 4) l.volume = true;  // D3D10_RESOURCE_DIMENSION_TEXTURE3D
    } else if (!codecFromFourCC(fourcc, l.codec)) {
        return fail("unsupported FourCC '" + std::string(fourcc, 4) + "'");
    }

    if (l.width == 0 || l.height == 0) return fail("DDS has zero width or height");
    if (l.width > 16384 || l.height > 16384) return fail("DDS dimensions too large");
    constexpr uint32_t kDdsdDepth = 0x800000;
    if ((flags & kDdsdDepth) && depth > 1) l.volume = true;
    l.mipCount = std::clamp<uint32_t>(mips, 1, 15);
    return true;
}

size_t levelBytes(Codec codec, size_t w, size_t h) {
    return isBlockCodec(codec) ? ((w + 3) / 4) * ((h + 3) / 4) * blockBytes(codec) : w * h * texelBytes(codec);
}

// Decodes one level of w x h texels from src (levelBytes(codec, w, h) bytes).
void decodeLevel(Codec codec, const uint8_t* src, size_t w, size_t h, Rgba8Image& out) {
    out.width = static_cast<int>(w);
    out.height = static_cast<int>(h);
    out.pixels.assign(w * h * 4, 0);
    uint8_t* dst = out.pixels.data();

    if (isBlockCodec(codec)) {
        const size_t bw = (w + 3) / 4;
        const size_t bh = (h + 3) / 4;
        const size_t bb = blockBytes(codec);
        uint8_t tile[64];
        for (size_t by = 0; by < bh; ++by) {
            for (size_t bx = 0; bx < bw; ++bx) {
                decodeBlock(codec, src + (by * bw + bx) * bb, tile);
                if (by * 4 + 4 <= h && bx * 4 + 4 <= w) {  // whole tile inside the image: four row copies
                    for (size_t y = 0; y < 4; ++y) std::memcpy(dst + ((by * 4 + y) * w + bx * 4) * 4, tile + y * 16, 16);
                    continue;
                }
                for (size_t y = 0; y < 4; ++y) {
                    const size_t py = by * 4 + y;
                    if (py >= h) break;
                    for (size_t x = 0; x < 4; ++x) {
                        const size_t px = bx * 4 + x;
                        if (px >= w) break;
                        std::memcpy(dst + (py * w + px) * 4, tile + (y * 4 + x) * 4, 4);
                    }
                }
            }
        }
    } else if (codec == Codec::Rgba8) {
        std::memcpy(dst, src, w * h * 4);
    } else if (codec == Codec::R8) {
        for (size_t i = 0; i < w * h; ++i) {
            dst[i * 4 + 0] = dst[i * 4 + 1] = dst[i * 4 + 2] = src[i];
            dst[i * 4 + 3] = 255;
        }
    } else {  // Rgba16F: tone-map RGB, clamp alpha linearly
        for (size_t i = 0; i < w * h; ++i) {
            const uint8_t* t = src + i * 8;
            dst[i * 4 + 0] = hdrToByte(halfToFloat(readU16(t + 0)));
            dst[i * 4 + 1] = hdrToByte(halfToFloat(readU16(t + 2)));
            dst[i * 4 + 2] = hdrToByte(halfToFloat(readU16(t + 4)));
            dst[i * 4 + 3] = linearToByte(halfToFloat(readU16(t + 6)));
        }
    }
}

// The level a preview of maxSide should decode: the smallest one that is still at least maxSide on its larger
// side and lies completely inside the `available` data bytes. Level 0 when no other qualifies.
struct LevelPick {
    size_t offset = 0;  // from the start of the pixel data
    size_t bytes = 0;
    size_t width = 0, height = 0;
};

LevelPick pickLevel(const DdsLayout& l, int maxSide, size_t available) {
    LevelPick pick;
    pick.width = l.width;
    pick.height = l.height;
    pick.bytes = levelBytes(l.codec, l.width, l.height);
    if (maxSide <= 0 || l.volume) return pick;
    size_t w = l.width, h = l.height, offset = 0;
    for (uint32_t level = 0; level < l.mipCount; ++level) {
        const size_t bytes = levelBytes(l.codec, w, h);
        if (offset > available || bytes > available - offset) break;                // the file ends before this level
        if (level > 0 && std::max(w, h) < static_cast<size_t>(maxSide)) break;      // smaller than the preview
        pick = LevelPick{offset, bytes, w, h};
        if (w == 1 && h == 1) break;
        offset += bytes;
        w = std::max<size_t>(1, w / 2);
        h = std::max<size_t>(1, h / 2);
    }
    return pick;
}

bool failWith(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return false;
}

std::string truncatedMessage(size_t need, size_t have) {
    return "truncated DDS data: need " + std::to_string(need) + " bytes for mip 0, file has " + std::to_string(have);
}

}  // namespace

bool decodeDds(const std::string& f, Rgba8Image& out, std::string* err) {
    out = Rgba8Image{};
    DdsLayout l;
    if (!parseHeader(f, l, err)) return false;
    const size_t need = levelBytes(l.codec, l.width, l.height);
    if (f.size() < l.dataOffset || f.size() - l.dataOffset < need)
        return failWith(err, truncatedMessage(need, f.size() > l.dataOffset ? f.size() - l.dataOffset : 0));
    decodeLevel(l.codec, reinterpret_cast<const uint8_t*>(f.data() + l.dataOffset), l.width, l.height, out);
    return true;
}

bool decodeDdsPreview(const std::string& f, int maxSide, Rgba8Image& out, int* fullWidth, int* fullHeight, std::string* err) {
    out = Rgba8Image{};
    DdsLayout l;
    if (!parseHeader(f, l, err)) return false;
    if (fullWidth) *fullWidth = static_cast<int>(l.width);
    if (fullHeight) *fullHeight = static_cast<int>(l.height);
    const size_t available = f.size() > l.dataOffset ? f.size() - l.dataOffset : 0;
    const LevelPick pick = pickLevel(l, maxSide, available);
    if (pick.offset > available || pick.bytes > available - pick.offset) return failWith(err, truncatedMessage(pick.bytes, available));
    Rgba8Image level;
    decodeLevel(l.codec, reinterpret_cast<const uint8_t*>(f.data() + l.dataOffset + pick.offset), pick.width, pick.height, level);
    out = maxSide > 0 && std::max(level.width, level.height) > maxSide ? downscale(level, maxSide) : std::move(level);
    return true;
}

bool loadDdsPreview(const std::string& path, int maxSide, Rgba8Image& out, int* fullWidth, int* fullHeight, std::string* err) {
    out = Rgba8Image{};
    const std::filesystem::path file(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    std::ifstream in(file, std::ios::binary);
    if (!in) return failWith(err, "cannot open " + path);
    in.seekg(0, std::ios::end);
    const std::streamoff fileSize = in.tellg();
    if (fileSize < 0) return failWith(err, "cannot read " + path);
    std::string header(static_cast<size_t>(std::min<std::streamoff>(fileSize, 148)), '\0');
    in.seekg(0);
    in.read(header.data(), static_cast<std::streamsize>(header.size()));
    if (!in) return failWith(err, "cannot read " + path);

    DdsLayout l;
    if (!parseHeader(header, l, err)) return false;
    if (fullWidth) *fullWidth = static_cast<int>(l.width);
    if (fullHeight) *fullHeight = static_cast<int>(l.height);
    const size_t size = static_cast<size_t>(fileSize);
    const size_t available = size > l.dataOffset ? size - l.dataOffset : 0;
    const LevelPick pick = pickLevel(l, maxSide, available);
    if (pick.offset > available || pick.bytes > available - pick.offset) return failWith(err, truncatedMessage(pick.bytes, available));
    std::string bytes(pick.bytes, '\0');
    in.seekg(static_cast<std::streamoff>(l.dataOffset + pick.offset));
    in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!in) return failWith(err, "cannot read " + path);
    Rgba8Image level;
    decodeLevel(l.codec, reinterpret_cast<const uint8_t*>(bytes.data()), pick.width, pick.height, level);
    out = maxSide > 0 && std::max(level.width, level.height) > maxSide ? downscale(level, maxSide) : std::move(level);
    return true;
}

Rgba8Image downscale(const Rgba8Image& in, int maxSide) {
    if (in.width <= 0 || in.height <= 0 || maxSide <= 0 || std::max(in.width, in.height) <= maxSide) return in;

    const double scale = static_cast<double>(maxSide) / std::max(in.width, in.height);
    const int ow = std::max(1, static_cast<int>(std::lround(in.width * scale)));
    const int oh = std::max(1, static_cast<int>(std::lround(in.height * scale)));

    Rgba8Image out;
    out.width = ow;
    out.height = oh;
    out.pixels.assign(static_cast<size_t>(ow) * oh * 4, 0);

    for (int oy = 0; oy < oh; ++oy) {
        const int y0 = static_cast<int>(static_cast<int64_t>(oy) * in.height / oh);
        const int y1 = std::max(y0 + 1, static_cast<int>(static_cast<int64_t>(oy + 1) * in.height / oh));
        for (int ox = 0; ox < ow; ++ox) {
            const int x0 = static_cast<int>(static_cast<int64_t>(ox) * in.width / ow);
            const int x1 = std::max(x0 + 1, static_cast<int>(static_cast<int64_t>(ox + 1) * in.width / ow));
            double sum[4] = {0, 0, 0, 0};
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    const uint8_t* p = &in.pixels[(static_cast<size_t>(y) * in.width + x) * 4];
                    for (int c = 0; c < 4; ++c) sum[c] += p[c];
                }
            }
            const double n = static_cast<double>((y1 - y0) * (x1 - x0));
            uint8_t* q = &out.pixels[(static_cast<size_t>(oy) * ow + ox) * 4];
            for (int c = 0; c < 4; ++c) q[c] = static_cast<uint8_t>(std::lround(sum[c] / n));
        }
    }
    return out;
}

}  // namespace iw8r::app
