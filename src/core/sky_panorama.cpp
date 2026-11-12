// Equirectangular / HDR panoramas as custom sky images (roadmap 5.17). See sky_panorama.h for the conventions.
#include "sky_panorama.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "custom_materials.h"  // decodeImageFile (PNG/TGA/JPG/BMP through stb_image)
#include "map_text.h"          // formatNumber, as the sun dialog writes "sundirection"

// stb_image is compiled once in stb_image_impl.cpp; this include only declares stbi_loadf (HDR), which that file already
// builds in (STBI_NO_HDR is not defined).
#pragma warning(push, 0)
#include "stb_image.h"
#pragma warning(pop)

namespace iw8r {
namespace {

constexpr double kPi = 3.14159265358979323846;

bool setErr(std::string* err, const std::string& m) {
    if (err) *err = m;
    return false;
}

void put32(std::string& s, uint32_t v) {
    for (int i = 0; i < 4; ++i) s.push_back(char((v >> (8 * i)) & 0xFF));
}

// sRGB transfer functions, the same as sky_image.cpp.
float srgbToLinear(float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); }
float linearToSrgb(float v) { return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f; }

// Copy of kCubeFaces in sky_image.cpp (that table is in an anonymous namespace, so it is repeated here; the
// sky_panorama tests check that the two agree). Texel (a, b) in [-1, 1] of face f looks along w + a * u + b * v, D3D order
// +X -X +Y -Y +Z -Z, in IW world axes.
struct CubeFace {
    double w[3], u[3], v[3];
};
constexpr CubeFace kCubeFaces[6] = {
    {{1, 0, 0}, {0, 0, -1}, {0, -1, 0}},
    {{-1, 0, 0}, {0, 0, 1}, {0, -1, 0}},
    {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},
    {{0, -1, 0}, {1, 0, 0}, {0, 0, -1}},
    {{0, 0, 1}, {1, 0, 0}, {0, -1, 0}},
    {{0, 0, -1}, {-1, 0, 0}, {0, -1, 0}},
};

// Rotation about +Z by yawDeg (from +X towards +Y).
Vec3 rotateZ(const Vec3& d, double yawDeg) {
    const double a = yawDeg * kPi / 180.0, c = std::cos(a), s = std::sin(a);
    return {c * d.x - s * d.y, s * d.x + c * d.y, d.z};
}

// World direction -> panorama texel (continuous, texel i spans [i, i + 1), origin top left).
void dirToTexel(const Vec3& d, int w, int h, double& px, double& py) {
    const double az = std::atan2(d.y, d.x);
    const double el = std::asin(std::clamp(d.z, -1.0, 1.0));
    // Viewed from inside, like Blender and HDRI sites: centre column = +X, +Y (azimuth 90) a quarter to the left.
    px = (0.5 - az / (2.0 * kPi)) * w;
    py = (0.5 - el / kPi) * h;          // row 0 = zenith
}

// Inverse of dirToTexel.
Vec3 texelToDir(double px, double py, int w, int h) {
    const double az = (0.5 - px / w) * 2.0 * kPi;
    const double el = (0.5 - py / h) * kPi;
    return {std::cos(el) * std::cos(az), std::cos(el) * std::sin(az), std::sin(el)};
}

int wrapX(int x, int w) { return ((x % w) + w) % w; }
int clampY(int y, int h) { return std::clamp(y, 0, h - 1); }

// Bilinear sample at texel coordinates (texel centres at +0.5). Wraps across the azimuth seam, clamps at the poles.
void sampleLinear(const FloatImage& img, double px, double py, float out[3]) {
    const double x = px - 0.5, y = py - 0.5;
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float fx = float(x - x0), fy = float(y - y0);
    out[0] = out[1] = out[2] = 0.0f;
    for (int k = 0; k < 4; ++k) {
        const int tx = wrapX(x0 + (k & 1), img.w), ty = clampY(y0 + (k >> 1), img.h);
        const float wgt = ((k & 1) ? fx : 1.0f - fx) * ((k >> 1) ? fy : 1.0f - fy);
        const float* p = &img.rgb[(size_t(ty) * img.w + tx) * 3];
        for (int c = 0; c < 3; ++c) out[c] += p[c] * wgt;
    }
}

// Same header as sky_image.cpp's cubeHeader: legacy DDS, B8G8R8A8 masks, CUBEMAP with all six faces, one mip.
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

bool validPanorama(const FloatImage& p) {
    return p.w >= 2 && p.h >= 2 && p.w <= 16384 && p.h <= 16384 && p.rgb.size() == size_t(p.w) * size_t(p.h) * 3;
}

}  // namespace

bool readPanorama(const std::string& path, FloatImage& out, std::string* err) {
    out = FloatImage{};
    std::string ext = std::filesystem::path(path).extension().string();
    for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".hdr") {
        int w = 0, h = 0, comp = 0;
        float* px = stbi_loadf(path.c_str(), &w, &h, &comp, 3);  // Radiance RGBE, already linear
        if (!px)
            return setErr(err, "cannot decode HDR '" + path + "': " + (stbi_failure_reason() ? stbi_failure_reason() : "unknown"));
        out.w = w;
        out.h = h;
        out.rgb.assign(px, px + size_t(w) * size_t(h) * 3);
        stbi_image_free(px);
    } else {
        int w = 0, h = 0;
        std::vector<uint8_t> rgba;
        if (!decodeImageFile(path, w, h, rgba, err)) return false;
        out.w = w;
        out.h = h;
        out.rgb.resize(size_t(w) * size_t(h) * 3);
        // 8-bit sRGB -> linear through a 256 entry table; alpha is ignored.
        float lut[256];
        for (int i = 0; i < 256; ++i) lut[i] = srgbToLinear(float(i) / 255.0f);
        for (size_t i = 0; i < size_t(w) * size_t(h); ++i)
            for (int c = 0; c < 3; ++c) out.rgb[i * 3 + c] = lut[rgba[i * 4 + c]];
    }
    if (!validPanorama(out)) {
        out = FloatImage{};
        return setErr(err, "panorama '" + path + "' must be at least 2x2 and at most 16384 pixels a side");
    }
    return true;
}

std::string cubeDdsFromPanorama(const FloatImage& pano, int faceSize, double yawDeg, double exposureEv, std::string* err) {
    if (!validPanorama(pano)) {
        setErr(err, "bad panorama image");
        return {};
    }
    if (faceSize < 4 || faceSize > 2048) {
        setErr(err, "cube face size must be 4..2048 (got " + std::to_string(faceSize) + ")");
        return {};
    }
    if (!std::isfinite(yawDeg) || !std::isfinite(exposureEv)) {
        setErr(err, "yaw and exposure must be finite");
        return {};
    }
    const float gain = float(std::pow(2.0, exposureEv));
    std::string out = cubeHeader(faceSize);
    out.reserve(out.size() + size_t(faceSize) * size_t(faceSize) * 4 * 6);
    for (const CubeFace& f : kCubeFaces)
        for (int ty = 0; ty < faceSize; ++ty)
            for (int tx = 0; tx < faceSize; ++tx) {
                // Same texel-centre mapping as sky_image.cpp.
                const double a = (tx + 0.5) / faceSize * 2.0 - 1.0, b = (ty + 0.5) / faceSize * 2.0 - 1.0;
                const Vec3 d = normalize(Vec3{f.w[0] + a * f.u[0] + b * f.v[0], f.w[1] + a * f.u[1] + b * f.v[1],
                                              f.w[2] + a * f.u[2] + b * f.v[2]});
                // The sky at world direction d shows the panorama at Rz(-yaw) d, so content at azimuth a moves to a + yaw.
                double px = 0, py = 0;
                dirToTexel(rotateZ(d, -yawDeg), pano.w, pano.h, px, py);
                float rgb[3];
                sampleLinear(pano, px, py, rgb);
                uint8_t bytes[3];
                for (int c = 0; c < 3; ++c) {
                    const float v = std::clamp(rgb[c] * gain, 0.0f, 1.0f);  // hard clamp after exposure
                    bytes[c] = uint8_t(std::lround(linearToSrgb(v) * 255.0f));
                }
                out.push_back(char(bytes[2]));  // B
                out.push_back(char(bytes[1]));  // G
                out.push_back(char(bytes[0]));  // R
                out.push_back(char(0xFF));
            }
    return out;
}

Vec3 brightestDirection(const FloatImage& pano, double yawDeg) {
    if (!validPanorama(pano)) return {0, 0, 1};
    const int w = pano.w, h = pano.h;
    // Cells of at most 256 columns (a sun disc of a few texels is kept, a single texel is diluted). Each cell is the mean
    // luminance weighted by cos(elevation), so the tiny rows at the poles do not win on pixel count.
    const int f = std::max(1, (w + 255) / 256);
    const int bw = (w + f - 1) / f, bh = (h + f - 1) / f;
    std::vector<double> sum(size_t(bw) * size_t(bh), 0.0), wsum(size_t(bw) * size_t(bh), 0.0);
    for (int y = 0; y < h; ++y) {
        const double wt = std::cos((0.5 - (y + 0.5) / h) * kPi);
        for (int x = 0; x < w; ++x) {
            const float* p = &pano.rgb[(size_t(y) * w + x) * 3];
            const double lum = std::max(0.0, 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]);
            const size_t cell = size_t(y / f) * bw + size_t(x / f);
            sum[cell] += lum * wt;
            wsum[cell] += wt;
        }
    }
    std::vector<double> mean(sum.size(), 0.0);
    for (size_t i = 0; i < mean.size(); ++i) mean[i] = wsum[i] > 0.0 ? sum[i] / wsum[i] : 0.0;

    // 3x3 box blur over the cells (wrapped across the seam, clamped at the poles), then the peak.
    int bestX = 0, bestY = 0;
    double best = -1.0;
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            double acc = 0.0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) acc += mean[size_t(clampY(by + dy, bh)) * bw + wrapX(bx + dx, bw)];
            if (acc > best) {
                best = acc;
                bestX = bx;
                bestY = by;
            }
        }
    // Cell centre in panorama texels.
    const double px = std::min((bestX + 0.5) * f, double(w)), py = std::min((bestY + 0.5) * f, double(h));
    return rotateZ(texelToDir(px, py, w, h), yawDeg);
}

std::string sunDirectionKeyFor(const Vec3& dir) {
    const double len = length(dir);
    if (!(len > 1e-12) || !std::isfinite(len)) return "-90 0 0";
    const double z = dir.z / len;
    // Inverse of d = (cos p cos y, cos p sin y, -sin p) with p the pitch and y the yaw, in degrees.
    double pitch = -std::asin(std::clamp(z, -1.0, 1.0)) * 180.0 / kPi;
    double yaw = std::atan2(dir.y, dir.x) * 180.0 / kPi;
    if (yaw < 0.0) yaw += 360.0;
    pitch = std::round(pitch * 100.0) / 100.0;
    yaw = std::round(yaw * 100.0) / 100.0;
    if (yaw >= 360.0) yaw -= 360.0;
    return formatNumber(pitch) + " " + formatNumber(yaw) + " 0";
}

}  // namespace iw8r
