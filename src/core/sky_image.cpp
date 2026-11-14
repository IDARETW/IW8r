// Sky cube maps for the x64-zt extraction (docs/research/33_sky_route.md). See sky_image.h.
#define BCDEC_STATIC
#define BCDEC_IMPLEMENTATION
#include "../../third_party/bcdec/bcdec.h"

#include "sky_image.h"

#include <algorithm>
#include <cctype>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace iw8r {
namespace {

constexpr uint32_t kFourCcDxt1 = 0x31545844u, kFourCcDxt3 = 0x33545844u, kFourCcDxt5 = 0x35545844u,
                   kFourCcDx10 = 0x30315844u;

uint32_t rd32(const std::string& s, size_t off) {
    uint32_t v = 0;
    if (off + 4 <= s.size()) std::memcpy(&v, s.data() + off, 4);
    return v;
}

void put32(std::string& s, uint32_t v) {
    for (int i = 0; i < 4; ++i) s.push_back(char((v >> (8 * i)) & 0xFF));
}

bool setErr(std::string* err, const std::string& m) {
    if (err) *err = m;
    return false;
}

size_t levelBytes(unsigned w, unsigned h, uint32_t fourCC, unsigned bits) {
    if (fourCC == kFourCcDxt1) return size_t((w + 3) / 4) * ((h + 3) / 4) * 8;
    if (fourCC == kFourCcDxt3 || fourCC == kFourCcDxt5) return size_t((w + 3) / 4) * ((h + 3) / 4) * 16;
    return (size_t(w) * h * bits + 7) / 8;
}

float srgbToLinear(float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); }
float linearToSrgb(float v) { return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f; }

// Mip 0 / slice 0 of a 2D source as RGB floats. LDR sources keep their (sRGB) values in 0..1; HDR ones are linear.
struct FloatImage {
    int w = 0, h = 0;
    bool hdr = false;
    std::vector<float> rgb;  // w * h * 3
};

int extractChannel(uint32_t v, uint32_t mask) {
    if (!mask) return -1;
    int shift = 0;
    while (!((mask >> shift) & 1u)) ++shift;
    const uint32_t m = mask >> shift;
    return int(std::lround(double((v >> shift) & m) * 255.0 / double(m)));
}

bool decodeSource(const std::string& dds, FloatImage& out, unsigned& dxgiOut, std::string* err) {
    if (dds.size() < 128 || std::memcmp(dds.data(), "DDS ", 4) != 0 || rd32(dds, 4) != 124)
        return setErr(err, "not a DDS file");
    const unsigned h = rd32(dds, 12), w = rd32(dds, 16);
    const uint32_t pfFlags = rd32(dds, 80), fourCC = rd32(dds, 84);
    if (!w || !h || w > 16384 || h > 16384) return setErr(err, "bad DDS size");
    out.w = int(w);
    out.h = int(h);
    out.rgb.assign(size_t(w) * h * 3, 0.0f);
    size_t offset = 128;

    enum class Kind { Bc1, Bc2, Bc3, Bc4, Bc6u, Bc6s, Bc7, Rgba8, Bgra8, Rgba16f, Rgba32f, Masked } kind;
    unsigned dxgi = 0;
    if ((pfFlags & 0x4) && fourCC == kFourCcDx10) {
        if (dds.size() < 148) return setErr(err, "truncated DX10 header");
        dxgi = rd32(dds, 128);
        const uint32_t dimension = rd32(dds, 132), arraySize = rd32(dds, 140);
        if (dimension != 3 || arraySize < 1) return setErr(err, "DX10 sky is not a 2D texture (dimension " + std::to_string(dimension) + ")");
        if (rd32(dds, 136) & 0x4) return setErr(err, "DX10 cube map sky is not supported (needs a legacy-header cube or a 2D hemi-octahedral image)");
        offset = 148;
        switch (dxgi) {
            case 70: case 71: case 72: kind = Kind::Bc1; break;
            case 73: case 74: case 75: kind = Kind::Bc2; break;
            case 76: case 77: case 78: kind = Kind::Bc3; break;
            case 79: case 80: kind = Kind::Bc4; break;
            case 94: case 95: kind = Kind::Bc6u; break;
            case 96: kind = Kind::Bc6s; break;
            case 97: case 98: case 99: kind = Kind::Bc7; break;
            case 27: case 28: case 29: kind = Kind::Rgba8; break;
            case 87: case 90: case 91: kind = Kind::Bgra8; break;
            case 10: kind = Kind::Rgba16f; break;
            case 2: kind = Kind::Rgba32f; break;
            default: return setErr(err, "unsupported DX10 sky format " + std::to_string(dxgi));
        }
    } else if (pfFlags & 0x4) {
        if (fourCC == kFourCcDxt1) kind = Kind::Bc1;
        else if (fourCC == kFourCcDxt3) kind = Kind::Bc2;
        else if (fourCC == kFourCcDxt5) kind = Kind::Bc3;
        else return setErr(err, "unsupported DDS fourCC");
    } else {
        if (rd32(dds, 88) != 32) return setErr(err, "unsupported uncompressed DDS (only 32-bit)");
        kind = Kind::Masked;
    }
    dxgiOut = dxgi;
    out.hdr = kind == Kind::Bc6u || kind == Kind::Bc6s || kind == Kind::Rgba16f || kind == Kind::Rgba32f;

    const auto need = [&](size_t bytes) { return offset <= dds.size() && bytes <= dds.size() - offset; };
    const uint8_t* src = reinterpret_cast<const uint8_t*>(dds.data()) + offset;
    const size_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    const auto store = [&](unsigned x, unsigned y, float r, float g, float b) {
        if (x >= w || y >= h) return;
        float* p = &out.rgb[(size_t(y) * w + x) * 3];
        p[0] = r;
        p[1] = g;
        p[2] = b;
    };
    switch (kind) {
        case Kind::Bc1: case Kind::Bc2: case Kind::Bc3: case Kind::Bc4: case Kind::Bc7: {
            const size_t bs = (kind == Kind::Bc1 || kind == Kind::Bc4) ? 8 : 16;
            if (!need(bw * bh * bs)) return setErr(err, "truncated DDS pixels");
            uint8_t tile[16 * 4];
            uint8_t grey[16];
            for (size_t by = 0; by < bh; ++by)
                for (size_t bx = 0; bx < bw; ++bx) {
                    const uint8_t* blk = src + (by * bw + bx) * bs;
                    if (kind == Kind::Bc4) {
                        bcdec_bc4(blk, grey, 4);
                        for (int i = 0; i < 16; ++i) tile[i * 4] = tile[i * 4 + 1] = tile[i * 4 + 2] = grey[i];
                    } else if (kind == Kind::Bc1) bcdec_bc1(blk, tile, 16);
                    else if (kind == Kind::Bc2) bcdec_bc2(blk, tile, 16);
                    else if (kind == Kind::Bc3) bcdec_bc3(blk, tile, 16);
                    else bcdec_bc7(blk, tile, 16);
                    for (int i = 0; i < 16; ++i)
                        store(unsigned(bx * 4 + i % 4), unsigned(by * 4 + i / 4), tile[i * 4] / 255.0f, tile[i * 4 + 1] / 255.0f,
                              tile[i * 4 + 2] / 255.0f);
                }
            break;
        }
        case Kind::Bc6u: case Kind::Bc6s: {
            if (!need(bw * bh * 16)) return setErr(err, "truncated DDS pixels");
            float tile[16 * 3];
            for (size_t by = 0; by < bh; ++by)
                for (size_t bx = 0; bx < bw; ++bx) {
                    bcdec_bc6h_float(src + (by * bw + bx) * 16, tile, 12, kind == Kind::Bc6s ? 1 : 0);
                    for (int i = 0; i < 16; ++i)
                        store(unsigned(bx * 4 + i % 4), unsigned(by * 4 + i / 4), std::max(0.0f, tile[i * 3]),
                              std::max(0.0f, tile[i * 3 + 1]), std::max(0.0f, tile[i * 3 + 2]));
                }
            break;
        }
        case Kind::Rgba8: case Kind::Bgra8: case Kind::Masked: {
            if (!need(size_t(w) * h * 4)) return setErr(err, "truncated DDS pixels");
            const uint32_t rm = rd32(dds, 92), gm = rd32(dds, 96), bm = rd32(dds, 100);
            for (size_t i = 0; i < size_t(w) * h; ++i) {
                const uint8_t* p = src + i * 4;
                float r, g, b;
                if (kind == Kind::Masked) {
                    uint32_t v;
                    std::memcpy(&v, p, 4);
                    r = std::max(0, extractChannel(v, rm)) / 255.0f;
                    g = std::max(0, extractChannel(v, gm)) / 255.0f;
                    b = std::max(0, extractChannel(v, bm)) / 255.0f;
                } else if (kind == Kind::Rgba8) {
                    r = p[0] / 255.0f, g = p[1] / 255.0f, b = p[2] / 255.0f;
                } else {
                    r = p[2] / 255.0f, g = p[1] / 255.0f, b = p[0] / 255.0f;
                }
                out.rgb[i * 3] = r, out.rgb[i * 3 + 1] = g, out.rgb[i * 3 + 2] = b;
            }
            break;
        }
        case Kind::Rgba16f: {
            if (!need(size_t(w) * h * 8)) return setErr(err, "truncated DDS pixels");
            for (size_t i = 0; i < size_t(w) * h; ++i)
                for (int c = 0; c < 3; ++c) {
                    uint16_t hv;
                    std::memcpy(&hv, src + i * 8 + c * 2, 2);
                    out.rgb[i * 3 + c] = std::max(0.0f, bcdec__half_to_float_quick(hv));
                }
            break;
        }
        case Kind::Rgba32f: {
            if (!need(size_t(w) * h * 16)) return setErr(err, "truncated DDS pixels");
            for (size_t i = 0; i < size_t(w) * h; ++i)
                for (int c = 0; c < 3; ++c) {
                    float f;
                    std::memcpy(&f, src + i * 16 + c * 4, 4);
                    out.rgb[i * 3 + c] = std::isfinite(f) ? std::max(0.0f, f) : 0.0f;
                }
            break;
        }
    }
    return true;
}

// The cube faces in D3D order: the texel at (a, b) in [-1, 1] of face f looks along w + a * u + b * v, in IW world axes.
// These are the axes of x64-zt's sky box (iw3_fastfile.cpp:6259-6264) and of its IW7 sky export (export_sky.cpp:44-52).
struct CubeFace {
    float w[3], u[3], v[3];
};
constexpr std::array<CubeFace, 6> kCubeFaces{{
    {{1, 0, 0}, {0, 0, -1}, {0, -1, 0}},
    {{-1, 0, 0}, {0, 0, 1}, {0, -1, 0}},
    {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},
    {{0, -1, 0}, {1, 0, 0}, {0, 0, -1}},
    {{0, 0, 1}, {1, 0, 0}, {0, -1, 0}},
    {{0, 0, -1}, {-1, 0, 0}, {0, -1, 0}},
}};

// Hemi-octahedral lookup of a direction (Z up). Below the horizon the horizon texel is used. Returns texel coordinates
// (continuous, texel centres at +0.5) of the source. The game's stock sky shader (PS 8a61dcf2, hemi mode: cb3[0].y bit 0;
// out/agent/post109/asm/SKY_8a61dcf2.asm lines 18-34) reads u = (oy - ox + 1) / 2, v = (ox + oy + 1) / 2 with
// (ox, oy) = (x, y) / (|x| + |y| + |z|). VERIFIED 2026-11-13; the editor had u and v transposed and mirrored (owner:
// the clouds did not match the game).
void hemiOctTexel(float x, float y, float z, int w, int h, float& px, float& py) {
    z = std::max(z, 0.0f);
    const float n = std::fabs(x) + std::fabs(y) + z;
    float ox = 0.0f, oy = 0.0f;
    if (n > 0.0f) {
        ox = x / n;
        oy = y / n;
    }
    const float u = oy - ox, v = ox + oy;
    px = (u + 1.0f) * 0.5f * float(w);
    py = (v + 1.0f) * 0.5f * float(h);
}

void sampleBilinear(const FloatImage& img, float px, float py, float* rgb) {
    const float x = px - 0.5f, y = py - 0.5f;
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float fx = x - float(x0), fy = y - float(y0);
    rgb[0] = rgb[1] = rgb[2] = 0.0f;
    for (int k = 0; k < 4; ++k) {
        const int tx = std::clamp(x0 + (k & 1), 0, img.w - 1), ty = std::clamp(y0 + (k >> 1), 0, img.h - 1);
        const float wgt = ((k & 1) ? fx : 1.0f - fx) * ((k >> 1) ? fy : 1.0f - fy);
        const float* p = &img.rgb[(size_t(ty) * img.w + tx) * 3];
        for (int c = 0; c < 3; ++c) rgb[c] += p[c] * wgt;
    }
}

std::string cubeHeader(int size) {
    std::string out = "DDS ";
    put32(out, 124);
    put32(out, 0x1 | 0x2 | 0x4 | 0x8 | 0x1000);  // CAPS HEIGHT WIDTH PITCH PIXELFORMAT
    put32(out, uint32_t(size));
    put32(out, uint32_t(size));
    put32(out, uint32_t(size * 4));
    put32(out, 0);
    put32(out, 1);  // one mip
    for (int i = 0; i < 11; ++i) put32(out, 0);
    put32(out, 32);
    put32(out, 0x41);  // DDPF_RGB | DDPF_ALPHAPIXELS
    put32(out, 0);
    put32(out, 32);
    put32(out, 0x00FF0000);
    put32(out, 0x0000FF00);
    put32(out, 0x000000FF);
    put32(out, 0xFF000000);
    put32(out, 0x8 | 0x1000);  // COMPLEX | TEXTURE
    put32(out, 0xFE00);        // CUBEMAP | all six faces
    put32(out, 0);
    put32(out, 0);
    put32(out, 0);
    return out;
}

}  // namespace

bool isConverterSkyCubemap(const std::string& dds, std::string* why) {
    if (dds.size() < 128 || std::memcmp(dds.data(), "DDS ", 4) != 0 || rd32(dds, 4) != 124) return setErr(why, "not a DDS file");
    const unsigned h = rd32(dds, 12), w = rd32(dds, 16);
    const unsigned mips = std::max(1u, rd32(dds, 28));
    const uint32_t fourCC = rd32(dds, 84), bits = rd32(dds, 88), caps2 = rd32(dds, 112);
    if (fourCC == kFourCcDx10) return setErr(why, "DX10 header (the converter reads a DX10 header only for a 2D BC7 image)");
    if (fourCC && fourCC != kFourCcDxt1 && fourCC != kFourCcDxt3 && fourCC != kFourCcDxt5) return setErr(why, "unsupported fourCC");
    if (!fourCC && bits != 8 && bits != 16 && bits != 24 && bits != 32) return setErr(why, "unsupported bit count");
    if ((caps2 & 0xFE00u) != 0xFE00u) return setErr(why, "not a complete cube map");
    if (!w || w != h || w > 8192 || mips > 14) return setErr(why, "faces are not square or too large");
    size_t total = 0;
    for (unsigned m = 0, s = w; m < mips; ++m, s = std::max(1u, s / 2)) total += levelBytes(s, s, fourCC, bits);
    if (dds.size() - 128 < total * 6) return setErr(why, "truncated cube map");
    return true;
}

float parseSkyBrightness(const std::string* text, bool* bad) {
    if (bad) *bad = false;
    if (!text || text->empty()) return 1.0f;
    char* end = nullptr;
    const double v = std::strtod(text->c_str(), &end);
    while (end && (*end == ' ' || *end == '\t')) ++end;
    if (end == text->c_str() || (end && *end) || !(v >= 0.1 && v <= 16.0)) {
        if (bad) *bad = true;
        return 1.0f;
    }
    return float(v);
}

bool skyCubemapFromDds(const std::string& dds, std::string& cube, const SkyConvertOptions& opt, SkyConvertInfo* info,
                       std::string* err) {
    SkyConvertInfo local;
    SkyConvertInfo& inf = info ? *info : local;
    inf = SkyConvertInfo{};
    if (isConverterSkyCubemap(dds)) {
        cube = dds;
        inf.passedThrough = true;
        inf.sourceWidth = inf.sourceHeight = inf.faceSize = int(rd32(dds, 16));
        return true;
    }
    if (dds.size() >= 128 && (rd32(dds, 112) & 0x200u) && rd32(dds, 84) != kFourCcDx10)
        return setErr(err, "sky cube map in a format the converter does not read (needs a legacy DXT1/3/5 or 8-32 bit cube)");
    FloatImage img;
    unsigned dxgi = 0;
    if (!decodeSource(dds, img, dxgi, err)) return false;
    inf.hdr = img.hdr;
    inf.sourceWidth = img.w;
    inf.sourceHeight = img.h;
    inf.sourceDxgiFormat = dxgi;

    // Luminance statistics of the source (linear; LDR sources are decoded from sRGB for this).
    {
        std::vector<float> lum(size_t(img.w) * img.h);
        double sum = 0.0;
        for (size_t i = 0; i < lum.size(); ++i) {
            float c[3];
            for (int k = 0; k < 3; ++k) c[k] = img.hdr ? img.rgb[i * 3 + k] : srgbToLinear(img.rgb[i * 3 + k]);
            lum[i] = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
            sum += lum[i];
        }
        inf.meanLuminance = lum.empty() ? 0.0f : float(sum / double(lum.size()));
        if (!lum.empty()) {
            const auto high = lum.begin() + std::ptrdiff_t(lum.size() * 995 / 1000);
            std::nth_element(lum.begin(), high, lum.end());
            inf.highLuminance = *high;
        }
    }
    float exposure = 1.0f;
    if (img.hdr) {
        // Auto exposure: the bright end of the sky (99.5th percentile, past the sun's few texels) at 0.9, so the 8-bit
        // cube keeps the sky's range; only the sun disc clamps. An APPROXIMATION of Replay's tone mapping (doc 33).
        exposure = opt.exposure > 0.0f ? opt.exposure : (inf.highLuminance > 1e-6f ? 0.9f / inf.highLuminance : 1.0f);
        exposure *= std::clamp(opt.brightness, 0.1f, 16.0f);
    }
    inf.exposure = exposure;

    const int size = std::clamp(opt.faceSize, 4, 2048);
    inf.faceSize = size;
    // Below the horizon the source has nothing: the horizon texel fades to the mean colour of the border ring over the
    // first 0.2 of -z (about 11 degrees), instead of stretching the horizon into streaks down to the nadir.
    float ring[3] = {0, 0, 0};
    {
        double acc[3] = {0, 0, 0};
        size_t n = 0;
        const auto add = [&](int x, int y) {
            const float* p = &img.rgb[(size_t(y) * img.w + x) * 3];
            for (int c = 0; c < 3; ++c) acc[c] += p[c];
            ++n;
        };
        for (int x = 0; x < img.w; ++x) add(x, 0), add(x, img.h - 1);
        for (int y = 1; y + 1 < img.h; ++y) add(0, y), add(img.w - 1, y);
        for (int c = 0; c < 3; ++c) ring[c] = n ? float(acc[c] / double(n)) : 0.0f;
    }
    // The sky's frame (skyrotation): rows = the rotated axes (forward, left, up) of the angles, so the direction in
    // the sky's frame is their dot products with the world direction (the inverse rotation).
    float rot[3][3];
    {
        const double p = opt.rotation[0] * 3.14159265358979 / 180.0, y = opt.rotation[1] * 3.14159265358979 / 180.0,
                     r = opt.rotation[2] * 3.14159265358979 / 180.0;
        const double sp = std::sin(p), cp = std::cos(p), sy = std::sin(y), cy = std::cos(y), sr = std::sin(r), cr = std::cos(r);
        const double fwd[3] = {cp * cy, cp * sy, -sp};
        const double left[3] = {sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, sr * cp};  // -right; (0, 1, 0) at zero angles
        const double upv[3] = {cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp};
        for (int k = 0; k < 3; ++k) rot[0][k] = float(fwd[k]), rot[1][k] = float(left[k]), rot[2][k] = float(upv[k]);
    }
    cube = cubeHeader(size);
    cube.reserve(cube.size() + size_t(size) * size * 4 * 6);
    for (const CubeFace& f : kCubeFaces)
        for (int ty = 0; ty < size; ++ty)
            for (int tx = 0; tx < size; ++tx) {
                const float a = (float(tx) + 0.5f) / float(size) * 2.0f - 1.0f;
                const float b = (float(ty) + 0.5f) / float(size) * 2.0f - 1.0f;
                const float d[3] = {f.w[0] + a * f.u[0] + b * f.v[0], f.w[1] + a * f.u[1] + b * f.v[1],
                                    f.w[2] + a * f.u[2] + b * f.v[2]};
                float px, py, rgb[3];
                const float s[3] = {d[0] * rot[0][0] + d[1] * rot[0][1] + d[2] * rot[0][2], d[0] * rot[1][0] + d[1] * rot[1][1] + d[2] * rot[1][2],
                                    d[0] * rot[2][0] + d[1] * rot[2][1] + d[2] * rot[2][2]};
                hemiOctTexel(s[0], s[1], s[2], img.w, img.h, px, py);
                sampleBilinear(img, px, py, rgb);
                if (d[2] < 0.0f) {
                    const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                    const float k = std::clamp(-d[2] / len / 0.2f, 0.0f, 1.0f);
                    for (int c = 0; c < 3; ++c) rgb[c] += (ring[c] - rgb[c]) * k;
                }
                uint8_t bytes[3];
                for (int c = 0; c < 3; ++c) {
                    const float s = img.hdr ? linearToSrgb(std::clamp(rgb[c] * exposure, 0.0f, 1.0f)) : std::clamp(rgb[c], 0.0f, 1.0f);
                    bytes[c] = uint8_t(std::lround(s * 255.0f));
                }
                cube.push_back(char(bytes[2]));  // B
                cube.push_back(char(bytes[1]));  // G
                cube.push_back(char(bytes[0]));  // R
                cube.push_back(char(0xFF));
            }
    return true;
}

std::string skyImageName(const std::string& skyname) {
    const size_t slash = skyname.find_last_of("/\\");
    const std::string base = slash == std::string::npos ? skyname : skyname.substr(slash + 1);
    std::string out;
    for (char c : base) {
        const unsigned char u = static_cast<unsigned char>(c);
        const char l = char(std::tolower(u));
        out += (std::isalnum(u) || l == '_' || l == '-') ? l : '_';
        if (out.size() >= 64) break;
    }
    return out;
}

}  // namespace iw8r
