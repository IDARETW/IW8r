#include "stock_lightmap.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace iw8r {
namespace {

namespace fs = std::filesystem;

constexpr uint64_t kRootBytes = 0xF10;      // sizeof(GfxLightmapAtlasDataTemp)
constexpr uint64_t kRawBytes = 0x28;        // sizeof(GfxImageRaw)
constexpr uint64_t kLightmapBytes = 0x78;   // sizeof(GfxLightmapRaw): textures[3]
constexpr uint64_t kSigBytes = 0x10 + kRawBytes;  // the four u32 and textures[0] of lightmap 0
constexpr uint64_t kPixelsFollow = 0xFFFFFFFFFFFFFFFEull;  // pointer -2: the pixels follow in the stream
constexpr size_t kDefaultChunk = size_t(16) << 20;
constexpr char kMagic[8] = {'I', 'W', '8', 'R', 'L', 'M', '\0', '\1'};
constexpr uint32_t kCacheVersion = 1;
constexpr uint32_t kHeaderBytes = 32;

uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint64_t rd64(const uint8_t* p) { return uint64_t(rd32(p)) | uint64_t(rd32(p + 4)) << 32; }
void wr32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}

// GfxImageRaw (GfxImageDesc + textureId + pixels).
struct ImageRaw {
    uint32_t format = 0, flags = 0, totalSize = 0;
    uint16_t width = 0, height = 0, depth = 0, numElements = 0;
    uint8_t levelCount = 0;
    uint64_t pixels = 0;
};

ImageRaw readRaw(const uint8_t* p) {
    ImageRaw r;
    r.format = rd32(p);
    r.flags = rd32(p + 4);
    r.totalSize = rd32(p + 8);
    r.width = rd16(p + 12);
    r.height = rd16(p + 14);
    r.depth = rd16(p + 16);
    r.numElements = rd16(p + 18);
    r.levelCount = p[20];
    r.pixels = rd64(p + 0x20);
    return r;
}

bool plausibleRaw(const ImageRaw& r) {
    return r.format >= 1 && r.format <= 49 && r.totalSize > 0 && r.width >= 1 && r.width <= 16384 && r.height >= 1 &&
           r.height <= 16384 && r.depth == 1 && r.numElements >= 1 && r.levelCount >= 1 && r.levelCount <= 16 &&
           r.pixels == kPixelsFollow;
}

// The root header and textures[0] of lightmap 0 (kSigBytes at p).
bool signatureAt(const uint8_t* p) {
    const uint32_t tz = rd32(p), num = rd32(p + 4), first = rd32(p + 8), type = rd32(p + 12);
    if (tz >= 1536 || num < 1 || num > 32 || first >= 512 || type > 2) return false;
    return plausibleRaw(readRaw(p + 16));
}

bool readAt(std::ifstream& f, uint64_t offset, uint8_t* dst, uint64_t bytes) {
    f.clear();
    f.seekg(std::streamoff(offset), std::ios::beg);
    if (!f) return false;
    f.read(reinterpret_cast<char*>(dst), std::streamsize(bytes));
    return uint64_t(f.gcount()) == bytes;
}

// The whole root at `offset`: every used texture plausible and the pixels inside the file.
bool rootValid(std::ifstream& f, uint64_t offset, uint64_t fileSize, std::vector<uint8_t>& root) {
    if (offset + kRootBytes > fileSize) return false;
    root.resize(kRootBytes);
    if (!readAt(f, offset, root.data(), kRootBytes)) return false;
    if (!signatureAt(root.data())) return false;
    const uint32_t num = rd32(root.data() + 4);
    uint64_t pixels = 0;
    for (uint32_t li = 0; li < num; ++li)
        for (int c = 0; c < 3; ++c) {
            const ImageRaw r = readRaw(root.data() + 0x10 + li * kLightmapBytes + c * kRawBytes);
            if (!plausibleRaw(r)) return false;
            pixels += r.totalSize;
        }
    return offset + kRootBytes + pixels <= fileSize;
}

uint64_t blocks4(int w, int h) { return uint64_t((w + 3) / 4) * uint64_t((h + 3) / 4); }

// One BC4 block: 16 values 0..255 (row-major in the 4x4 block).
void bc4Block(const uint8_t* b, float out[16]) {
    const float r0 = b[0], r1 = b[1];
    float pal[8];
    pal[0] = r0;
    pal[1] = r1;
    if (b[0] > b[1]) {
        for (int i = 1; i <= 6; ++i) pal[1 + i] = (float(7 - i) * r0 + float(i) * r1) / 7.0f;
    } else {
        for (int i = 1; i <= 4; ++i) pal[1 + i] = (float(5 - i) * r0 + float(i) * r1) / 5.0f;
        pal[6] = 0.0f;
        pal[7] = 255.0f;
    }
    uint64_t bits = 0;
    for (int k = 0; k < 6; ++k) bits |= uint64_t(b[2 + k]) << (8 * k);
    for (int t = 0; t < 16; ++t) out[t] = pal[(bits >> (3 * t)) & 7];
}

// BC4 (channels 1, 8-byte blocks) or BC5 (channels 2, two BC4 blocks of 8 bytes: red then green).
bool decodeBcChannels(const uint8_t* data, size_t bytes, int w, int h, int channels, std::vector<float>& out) {
    if (w <= 0 || h <= 0) return false;
    const int bw = (w + 3) / 4, bh = (h + 3) / 4;
    const uint64_t blockBytes = 8ull * uint64_t(channels);
    if (uint64_t(bw) * uint64_t(bh) * blockBytes > bytes) return false;
    out.assign(size_t(w) * size_t(h) * size_t(channels), 0.0f);
    float v[16];
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            const uint8_t* blk = data + (uint64_t(by) * uint64_t(bw) + uint64_t(bx)) * blockBytes;
            for (int c = 0; c < channels; ++c) {
                bc4Block(blk + 8 * c, v);
                for (int t = 0; t < 16; ++t) {
                    const int x = bx * 4 + (t & 3), y = by * 4 + (t >> 2);
                    if (x < w && y < h) out[(size_t(y) * size_t(w) + size_t(x)) * size_t(channels) + size_t(c)] = v[t] / 255.0f;
                }
            }
        }
    return true;
}

int clampByte(float v) {
    const float r = std::round(v * 255.0f);
    return r < 0 ? 0 : r > 255 ? 255 : int(r);
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Decoders

bool decodeBC4(const uint8_t* data, size_t bytes, int width, int height, std::vector<float>& out) {
    return decodeBcChannels(data, bytes, width, height, 1, out);
}

bool decodeBC5(const uint8_t* data, size_t bytes, int width, int height, std::vector<float>& out) {
    return decodeBcChannels(data, bytes, width, height, 2, out);
}

float unpackUnsignedSmallFloat(uint32_t bits, int mantissaBits) {
    const uint32_t e = (bits >> mantissaBits) & 0x1F;
    const uint32_t m = bits & ((1u << mantissaBits) - 1);
    const float scale = float(1u << mantissaBits);
    if (e == 31) return 0.0f;  // inf / NaN: not valid light
    if (e == 0) return std::ldexp(float(m) / scale, -14);
    return std::ldexp(1.0f + float(m) / scale, int(e) - 15);
}

bool decodeR11G11B10F(const uint8_t* data, size_t bytes, int width, int height, std::vector<float>& out) {
    if (width <= 0 || height <= 0) return false;
    const size_t n = size_t(width) * size_t(height);
    if (uint64_t(n) * 4 > bytes) return false;
    out.resize(n * 3);
    for (size_t i = 0; i < n; ++i) {
        const uint32_t v = rd32(data + 4 * i);
        out[3 * i] = unpackUnsignedSmallFloat(v & 0x7FF, 6);
        out[3 * i + 1] = unpackUnsignedSmallFloat((v >> 11) & 0x7FF, 6);
        out[3 * i + 2] = unpackUnsignedSmallFloat((v >> 22) & 0x3FF, 5);
    }
    return true;
}

bool decodeRGBA8(const uint8_t* data, size_t bytes, int width, int height, std::vector<float>& out) {
    if (width <= 0 || height <= 0) return false;
    const size_t n = size_t(width) * size_t(height) * 4;
    if (n > bytes) return false;
    out.resize(n);
    for (size_t i = 0; i < n; ++i) out[i] = float(data[i]) / 255.0f;
    return true;
}

std::array<float, 3> decodeOctahedralDirection(float x, float y) {
    const float a = x * 2.0f - 1.0f, b = y * 2.0f - 1.0f;
    const float ox = (a + b) * 0.5f, oy = (a - b) * 0.5f;
    const float oz = std::max(0.0f, 1.0f - std::fabs(ox) - std::fabs(oy));
    const float len = std::sqrt(ox * ox + oy * oy + oz * oz);
    if (len < 1e-12f) return {0.0f, 0.0f, 1.0f};
    return {ox / len, oy / len, oz / len};
}

std::array<float, 2> encodeOctahedralDirection(float dx, float dy, float dz) {
    dz = std::max(dz, 0.0f);
    const float s = std::fabs(dx) + std::fabs(dy) + dz;
    if (s < 1e-12f) return {0.5f, 0.5f};  // straight up
    const float ox = dx / s, oy = dy / s;
    return {(1.0f + ox + oy) * 0.5f, (1.0f + ox - oy) * 0.5f};
}

// ---------------------------------------------------------------------------------------------------------------
// Root search and extraction

bool findStockLightmapRoots(const std::string& decPath, std::vector<uint64_t>& roots, std::string* err, size_t chunkBytes) {
    roots.clear();
    std::error_code ec;
    const uint64_t size = fs::file_size(fs::path(decPath), ec);
    std::ifstream f(fs::path(decPath), std::ios::binary);
    if (ec || !f) {
        if (err) *err = "cannot read " + decPath;
        return false;
    }
    const uint64_t chunk = chunkBytes ? chunkBytes : kDefaultChunk;
    std::vector<uint8_t> buf;
    std::vector<uint8_t> root;
    for (uint64_t start = 0; start + kSigBytes <= size; start += chunk) {
        // This chunk's candidates are its first `chunk` offsets; the read runs kSigBytes - 1 further so a signature
        // that starts near the end is whole (the next chunk starts after it, so nothing is found twice).
        const uint64_t len = std::min<uint64_t>(chunk + kSigBytes - 1, size - start);
        buf.resize(size_t(len));
        if (!readAt(f, start, buf.data(), len)) {
            if (err) *err = "read error in " + decPath;
            return false;
        }
        const uint8_t* p = buf.data();
        const uint64_t last = std::min<uint64_t>(chunk, len - kSigBytes + 1);
        for (uint64_t i = 0; i < last; ++i) {
            // Fast reject: numLightmaps 1..32 and lightmapType 0..2, both little-endian u32.
            if (p[i + 4] == 0 || p[i + 4] > 32 || p[i + 5] || p[i + 6] || p[i + 7]) continue;
            if (p[i + 12] > 2 || p[i + 13] || p[i + 14] || p[i + 15]) continue;
            if (!signatureAt(p + i)) continue;
            const uint64_t at = start + i;
            if (rootValid(f, at, size, root)) roots.push_back(at);
        }
    }
    return true;
}

bool extractStockLightmapFromDecompressedZone(const std::string& decPath, StockLightmap& out, std::string* err,
                                              uint64_t* rootOffset) {
    std::vector<uint64_t> roots;
    if (!findStockLightmapRoots(decPath, roots, err)) return false;
    if (roots.empty()) {
        if (err) *err = "no lightmap root (GfxLightmapAtlasDataTemp) in " + fs::path(decPath).filename().string();
        return false;
    }
    if (roots.size() > 1) {
        if (err) *err = std::to_string(roots.size()) + " lightmap roots found: not supported yet";
        return false;
    }
    const uint64_t at = roots.front();
    if (rootOffset) *rootOffset = at;
    std::ifstream f(fs::path(decPath), std::ios::binary);
    std::vector<uint8_t> root(kRootBytes);
    if (!f || !readAt(f, at, root.data(), kRootBytes)) {
        if (err) *err = "cannot read the lightmap root of " + decPath;
        return false;
    }
    const uint32_t num = rd32(root.data() + 4);
    const uint32_t type = rd32(root.data() + 12);
    if (num != 1) {
        if (err) *err = std::to_string(num) + " lightmaps: not supported yet";
        return false;
    }
    if (type != 1 && type != 2) {
        // mp_donetsk: type 0 REGULAR, BC6H irradiance (format 42); the main zone holds lightmap 0 of 258, the others
        // are in the streamed transient zones.
        if (err) *err = "lightmap type " + std::to_string(type) + " (REGULAR, Warzone: BC6H, lightmaps in streamed zones): not supported yet";
        return false;
    }
    ImageRaw planes[3];
    for (int c = 0; c < 3; ++c) planes[c] = readRaw(root.data() + 0x10 + c * kRawBytes);
    const int w = planes[0].width, h = planes[0].height;
    for (const ImageRaw& r : planes)
        if (r.width != w || r.height != h) {
            if (err) *err = "lightmap planes differ in size";
            return false;
        }
    if (planes[0].format != kLmFormatBC4 || planes[1].format != kLmFormatR11G11B10F ||
        (planes[2].format != kLmFormatBC5 && planes[2].format != kLmFormatRGBA8 && planes[2].format != kLmFormatRGBA8Srgb)) {
        if (err)
            *err = "lightmap plane formats " + std::to_string(planes[0].format) + ", " + std::to_string(planes[1].format) + ", " +
                   std::to_string(planes[2].format) + ": not supported yet (expected 39, 32, 40 or 6)";
        return false;
    }

    StockLightmap lm;
    lm.type = int(type);
    lm.width = w;
    lm.height = h;
    uint64_t offset = at + kRootBytes;
    std::vector<uint8_t> px;
    std::vector<float> tmp;
    for (int c = 0; c < 3; ++c) {
        px.resize(planes[c].totalSize);
        if (!readAt(f, offset, px.data(), px.size())) {
            if (err) *err = "lightmap plane " + std::to_string(c) + " runs past the end of the file";
            return false;
        }
        offset += planes[c].totalSize;
        bool ok = false;
        if (c == 0) {
            ok = decodeBC4(px.data(), px.size(), w, h, lm.ambientFraction);
        } else if (c == 1) {
            ok = decodeR11G11B10F(px.data(), px.size(), w, h, lm.irradiance);
        } else {
            const bool bc5 = planes[c].format == kLmFormatBC5;
            ok = bc5 ? decodeBC5(px.data(), px.size(), w, h, tmp) : decodeRGBA8(px.data(), px.size(), w, h, tmp);
            if (ok) {
                const size_t n = size_t(w) * size_t(h), stride = bc5 ? 2 : 4;
                lm.direction.resize(n * 3);
                for (size_t i = 0; i < n; ++i) {
                    const auto d = decodeOctahedralDirection(tmp[i * stride], tmp[i * stride + 1]);
                    lm.direction[3 * i] = d[0];
                    lm.direction[3 * i + 1] = d[1];
                    lm.direction[3 * i + 2] = d[2];
                }
            }
        }
        if (!ok) {
            if (err) *err = "lightmap plane " + std::to_string(c) + " is smaller than its size needs";
            return false;
        }
    }
    out = std::move(lm);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Cache file

uint16_t floatToHalf(float f) {
    if (std::isnan(f)) return 0;
    uint32_t x;
    std::memcpy(&x, &f, 4);
    const uint16_t sign = uint16_t((x >> 16) & 0x8000);
    const float a = std::fabs(f);
    if (a >= 65504.0f) return uint16_t(sign | 0x7BFF);  // saturate (also infinity)
    if (a < std::ldexp(1.0f, -24) * 0.5f) return sign;  // rounds to zero
    int e;
    const float m = std::frexp(a, &e);  // a = m * 2^e, m in [0.5, 1)
    // binary16 normal: a = (1 + man / 1024) * 2^(E - 15), E = e - 1 + 15
    int E = e - 1 + 15;
    if (E <= 0) {
        // subnormal: a = man * 2^-24
        const float man = std::nearbyint(std::ldexp(a, 24));  // round half to even (default mode)
        return uint16_t(sign | uint16_t(man));  // man == 1024 lands on the smallest normal, which is the right bits
    }
    float man = std::nearbyint((m * 2.0f - 1.0f) * 1024.0f);
    if (man >= 1024.0f) {
        man = 0;
        ++E;
    }
    if (E >= 31) return uint16_t(sign | 0x7BFF);
    return uint16_t(sign | uint16_t(E) << 10 | uint16_t(man));
}

float halfToFloat(uint16_t h) {
    const bool neg = (h >> 15) != 0;
    const int e = (h >> 10) & 31;
    const int m = h & 1023;
    float v;
    if (e == 0)
        v = std::ldexp(float(m), -24);
    else if (e == 31)
        v = m ? 0.0f : 65504.0f;  // never written by floatToHalf
    else
        v = std::ldexp(float(1024 + m), e - 25);
    return neg ? -v : v;
}

bool writeStockLightmapCache(const std::string& path, const StockLightmap& lm, std::string* err) {
    if (lm.width <= 0 || lm.height <= 0) {
        if (err) *err = "lightmap has no size";
        return false;
    }
    const size_t n = size_t(lm.width) * size_t(lm.height);
    uint32_t planes = 0;
    if (!lm.irradiance.empty()) planes |= 1;
    if (!lm.ambientFraction.empty()) planes |= 2;
    if (!lm.direction.empty()) planes |= 4;
    if (((planes & 1) && lm.irradiance.size() != n * 3) || ((planes & 2) && lm.ambientFraction.size() != n) ||
        ((planes & 4) && lm.direction.size() != n * 3)) {
        if (err) *err = "lightmap plane sizes do not match " + std::to_string(lm.width) + "x" + std::to_string(lm.height);
        return false;
    }
    std::vector<uint8_t> body(kHeaderBytes);
    std::memcpy(body.data(), kMagic, 8);
    wr32(body.data() + 8, kCacheVersion);
    wr32(body.data() + 12, uint32_t(lm.type));
    wr32(body.data() + 16, uint32_t(lm.width));
    wr32(body.data() + 20, uint32_t(lm.height));
    wr32(body.data() + 24, planes);
    wr32(body.data() + 28, 0);
    body.reserve(kHeaderBytes + n * (((planes & 1) ? 6 : 0) + ((planes & 2) ? 1 : 0) + ((planes & 4) ? 2 : 0)));
    if (planes & 1)
        for (float v : lm.irradiance) {
            const uint16_t hv = floatToHalf(v);
            body.push_back(uint8_t(hv));
            body.push_back(uint8_t(hv >> 8));
        }
    if (planes & 2)
        for (float v : lm.ambientFraction) body.push_back(uint8_t(clampByte(v)));
    if (planes & 4)
        for (size_t i = 0; i < n; ++i) {
            const auto e = encodeOctahedralDirection(lm.direction[3 * i], lm.direction[3 * i + 1], lm.direction[3 * i + 2]);
            body.push_back(uint8_t(clampByte(e[0])));
            body.push_back(uint8_t(clampByte(e[1])));
        }
    // Written beside the target, then renamed over it: a reader never sees half a file.
    const fs::path target = fs::path(path);
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) {
            if (err) *err = "cannot write " + tmp.string();
            return false;
        }
        o.write(reinterpret_cast<const char*>(body.data()), std::streamsize(body.size()));
        if (!o) {
            o.close();
            std::error_code ec;
            fs::remove(tmp, ec);
            if (err) *err = "write error in " + tmp.string();
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        if (err) *err = "cannot rename " + tmp.string() + " to " + target.string();
        return false;
    }
    return true;
}

bool readStockLightmapCache(const std::string& path, StockLightmap& out, std::string* err) {
    std::ifstream f(fs::path(path), std::ios::binary);
    if (!f) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    uint8_t h[kHeaderBytes];
    f.read(reinterpret_cast<char*>(h), kHeaderBytes);
    if (f.gcount() != std::streamsize(kHeaderBytes) || std::memcmp(h, kMagic, 8) != 0) {
        if (err) *err = path + " is not a lightmap cache file";
        return false;
    }
    if (rd32(h + 8) != kCacheVersion) {
        if (err) *err = path + ": lightmap cache version " + std::to_string(rd32(h + 8)) + " is not supported";
        return false;
    }
    const uint32_t type = rd32(h + 12), w = rd32(h + 16), hh = rd32(h + 20), planes = rd32(h + 24);
    if (w == 0 || hh == 0 || w > 16384 || hh > 16384 || (planes & ~7u)) {
        if (err) *err = path + ": bad lightmap cache header";
        return false;
    }
    const size_t n = size_t(w) * size_t(hh);
    const size_t bytes = n * (((planes & 1) ? 6 : 0) + ((planes & 2) ? 1 : 0) + ((planes & 4) ? 2 : 0));
    std::vector<uint8_t> body(bytes);
    f.read(reinterpret_cast<char*>(body.data()), std::streamsize(bytes));
    if (size_t(f.gcount()) != bytes || f.peek() != std::char_traits<char>::eof()) {
        if (err) *err = path + ": lightmap cache size does not match its header";
        return false;
    }
    StockLightmap lm;
    lm.type = int(type);
    lm.width = int(w);
    lm.height = int(hh);
    const uint8_t* p = body.data();
    if (planes & 1) {
        lm.irradiance.resize(n * 3);
        for (size_t i = 0; i < n * 3; ++i, p += 2) lm.irradiance[i] = halfToFloat(rd16(p));
    }
    if (planes & 2) {
        lm.ambientFraction.resize(n);
        for (size_t i = 0; i < n; ++i) lm.ambientFraction[i] = float(*p++) / 255.0f;
    }
    if (planes & 4) {
        lm.direction.resize(n * 3);
        for (size_t i = 0; i < n; ++i, p += 2) {
            const auto d = decodeOctahedralDirection(float(p[0]) / 255.0f, float(p[1]) / 255.0f);
            lm.direction[3 * i] = d[0];
            lm.direction[3 * i + 1] = d[1];
            lm.direction[3 * i + 2] = d[2];
        }
    }
    out = std::move(lm);
    return true;
}

}  // namespace iw8r
