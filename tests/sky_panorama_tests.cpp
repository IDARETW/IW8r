// Sky panoramas (src/core/sky_panorama.*, roadmap 5.17): equirectangular / HDR to the cube DDS, brightest direction,
// sun key. The face table here is a copy of sky_image.cpp's kCubeFaces; the hemi-octahedral cross-check below ties the
// two together.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "geometry.h"
#include "sky_image.h"
#include "sky_panorama.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

constexpr double kPiT = 3.14159265358979323846;

uint32_t rd32(const std::string& s, size_t off) {
    uint32_t v = 0;
    std::memcpy(&v, s.data() + off, 4);
    return v;
}

void put32(std::string& s, uint32_t v) {
    for (int i = 0; i < 4; ++i) s.push_back(char((v >> (8 * i)) & 0xFF));
}

// Direction for an azimuth (from +X towards +Y) and elevation, degrees.
Vec3 dirAzEl(double azDeg, double elDeg) {
    const double a = azDeg * kPiT / 180.0, e = elDeg * kPiT / 180.0;
    return {std::cos(e) * std::cos(a), std::cos(e) * std::sin(a), std::sin(e)};
}

double angleDeg(const Vec3& a, const Vec3& b) {
    return std::acos(std::clamp(dot(normalize(a), normalize(b)), -1.0, 1.0)) * 180.0 / kPiT;
}

// The sun vector of a worldspawn "sundirection" (compile_iw8.cpp sunDirection, scene.cpp sunToward).
Vec3 sunToward(double pitchDeg, double yawDeg) {
    const double p = pitchDeg * kPiT / 180.0, y = yawDeg * kPiT / 180.0;
    return normalize({std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), -std::sin(p)});
}

// Equirectangular image of a uniform colour with one disc at (azDeg, elDeg), radius in texels. The pixel index of a
// direction is the inverse of the azimuth/elevation layout documented in sky_panorama.h.
FloatImage spotPano(int w, int h, double azDeg, double elDeg, double radiusPx, float value, float background) {
    FloatImage p;
    p.w = w;
    p.h = h;
    p.rgb.assign(size_t(w) * h * 3, background);
    const double cx = (0.5 - azDeg / 360.0) * w - 0.5;  // azimuth grows to the left
    const double cy = (0.5 - elDeg / 180.0) * h - 0.5;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double dx = x - cx;
            dx -= w * std::round(dx / w);  // the seam is at the centre of the wrap
            const double dy = y - cy;
            if (dx * dx + dy * dy <= radiusPx * radiusPx)
                for (int c = 0; c < 3; ++c) p.rgb[(size_t(y) * w + x) * 3 + c] = value;
        }
    return p;
}

// Copy of kCubeFaces (sky_image.cpp) as {w, u, v} rows.
constexpr double kFace[6][3][3] = {
    {{1, 0, 0}, {0, 0, -1}, {0, -1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, -1, 0}}, {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},
    {{0, -1, 0}, {1, 0, 0}, {0, 0, -1}}, {{0, 0, 1}, {1, 0, 0}, {0, -1, 0}}, {{0, 0, -1}, {-1, 0, 0}, {0, -1, 0}},
};

Vec3 cubeTexelDir(int face, int tx, int ty, int size) {
    const double a = (tx + 0.5) / size * 2.0 - 1.0, b = (ty + 0.5) / size * 2.0 - 1.0;
    const auto comp = [&](int k) { return kFace[face][0][k] + a * kFace[face][1][k] + b * kFace[face][2][k]; };
    return normalize(Vec3{comp(0), comp(1), comp(2)});
}

// Texels of a legacy BGRA cube with the maximum red value: their faces, and the mean direction of them.
struct MaxTexels {
    int count = 0;
    int faceMask = 0;  // bit f set when face f holds a maximal texel
    Vec3 centroid;     // normalised mean of the maximal texels' directions
    int maxRed = -1;
};

MaxTexels maxTexels(const std::string& dds, int size) {
    MaxTexels r;
    if (dds.size() != 128 + size_t(6) * size * size * 4) return r;
    for (int f = 0; f < 6; ++f)
        for (int ty = 0; ty < size; ++ty)
            for (int tx = 0; tx < size; ++tx) {
                const size_t off = 128 + ((size_t(f) * size + ty) * size + tx) * 4;
                const int red = uint8_t(dds[off + 2]);
                if (red > r.maxRed) {
                    r = MaxTexels{};
                    r.maxRed = red;
                }
                if (red == r.maxRed) {
                    ++r.count;
                    r.faceMask |= 1 << f;
                    const Vec3 d = cubeTexelDir(f, tx, ty, size);
                    r.centroid = r.centroid + d;
                }
            }
    r.centroid = normalize(r.centroid);
    return r;
}

void writeBytes(const fs::path& p, const std::string& bytes) {
    std::ofstream f(p, std::ios::binary);
    f.write(bytes.data(), std::streamsize(bytes.size()));
}

// Radiance RGBE for one pixel (standard encoder: the largest channel keeps 8 mantissa bits).
void encodeRgbe(float r, float g, float b, uint8_t out[4]) {
    const float v = std::max({r, g, b});
    if (!(v > 1e-32f)) {
        out[0] = out[1] = out[2] = out[3] = 0;
        return;
    }
    int e = 0;
    const float m = std::frexp(v, &e);
    (void)m;
    const double scale = 256.0 / std::ldexp(1.0, e);
    const float in[3] = {r, g, b};
    for (int c = 0; c < 3; ++c) out[c] = uint8_t(std::clamp(std::floor(in[c] * scale), 0.0, 255.0));
    out[3] = uint8_t(e + 128);
}

// Expected 8-bit sRGB byte of a linear value.
int expectedByte(double lin) {
    const double s = lin <= 0.0031308 ? lin * 12.92 : 1.055 * std::pow(lin, 1.0 / 2.4) - 0.055;
    return int(std::lround(std::clamp(s, 0.0, 1.0) * 255.0));
}

}  // namespace

TEST(sky_pano_hdr_reader_hand_made_and_round_trip) {
    const fs::path dir = fs::temp_directory_path() / "iw8r_sky_panorama_tests";
    fs::create_directories(dir);

    // Hand-made 4x2 file, uncompressed scanlines (width < 8 is never RLE). Row 0 then row 1.
    const std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n";
    std::string bytes = header;
    const uint8_t px[8][4] = {
        {128, 64, 32, 129},  {0, 0, 0, 0},       {255, 255, 255, 137}, {128, 128, 128, 128},  // row 0
        {64, 128, 0, 128},   {1, 2, 3, 120},     {0, 0, 0, 0},         {200, 100, 50, 136},   // row 1
    };
    for (const auto& p : px) bytes.append(reinterpret_cast<const char*>(p), 4);
    const fs::path hand = dir / "hand_made.hdr";
    writeBytes(hand, bytes);
    FloatImage img;
    std::string err;
    CHECK(readPanorama(hand.string(), img, &err));
    CHECK(img.w == 4 && img.h == 2 && img.rgb.size() == 4 * 2 * 3);
    // value = mantissa * 2^(e - 136): (128, 64, 32, 129) is (1, 0.5, 0.25); (255,255,255,137) is 510.
    const float expect0[3] = {1.0f, 0.5f, 0.25f}, expect2[3] = {510.0f, 510.0f, 510.0f}, expect4[3] = {0.25f, 0.5f, 0.0f};
    for (int c = 0; c < 3; ++c) {
        CHECK(std::fabs(img.rgb[c] - expect0[c]) <= 0.01f * expect0[c]);
        CHECK(std::fabs(img.rgb[2 * 3 + c] - expect2[c]) <= 0.01f * expect2[c]);
        CHECK(std::fabs(img.rgb[4 * 3 + c] - expect4[c]) <= 0.01f * std::max(expect4[c], 0.01f));
    }
    CHECK(img.rgb[1 * 3] == 0.0f && img.rgb[1 * 3 + 2] == 0.0f);

    // Round trip: encode floats with the standard RGBE encoder, read them back.
    FloatImage src;
    src.w = 4;
    src.h = 2;
    src.rgb = {1.0f, 0.5f, 0.25f, 0.0f, 0.0f, 0.0f, 3.0f, 2.0f, 0.1f, 0.001f, 0.002f, 0.0005f,
               100.0f, 50.0f, 10.0f, 0.5f, 0.5f, 0.5f, 7.0f, 0.0f, 1.5f, 0.02f, 0.03f, 0.04f};
    std::string rt = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n";
    for (size_t i = 0; i < src.rgb.size(); i += 3) {
        uint8_t e4[4];
        encodeRgbe(src.rgb[i], src.rgb[i + 1], src.rgb[i + 2], e4);
        rt.append(reinterpret_cast<const char*>(e4), 4);
    }
    const fs::path trip = dir / "round_trip.hdr";
    writeBytes(trip, rt);
    FloatImage back;
    CHECK(readPanorama(trip.string(), back, &err));
    CHECK(back.w == 4 && back.h == 2);
    bool tripOk = back.rgb.size() == src.rgb.size();
    for (size_t i = 0; tripOk && i < src.rgb.size(); i += 3) {
        const float vmax = std::max({src.rgb[i], src.rgb[i + 1], src.rgb[i + 2]});
        for (int c = 0; c < 3; ++c) tripOk = tripOk && std::fabs(back.rgb[i + c] - src.rgb[i + c]) <= 0.01f * vmax + 1e-4f;
    }
    CHECK(tripOk);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(sky_pano_ldr_is_srgb_to_linear) {
    const fs::path dir = fs::temp_directory_path() / "iw8r_sky_panorama_tests";
    fs::create_directories(dir);
    // Uncompressed 24-bit TGA, 2x1, top-left origin: pixel (128,128,128) and pure red (255,0,0). TGA stores BGR.
    std::string tga;
    tga.push_back(0);
    tga.push_back(0);
    tga.push_back(2);
    for (int i = 0; i < 5; ++i) tga.push_back(0);
    for (int i = 0; i < 4; ++i) tga.push_back(0);  // x and y origin
    tga.push_back(2);
    tga.push_back(0);  // width 2
    tga.push_back(2);
    tga.push_back(0);  // height 2 (panoramas must be at least 2x2)
    tga.push_back(24);
    tga.push_back(0x20);
    const uint8_t pixels[12] = {128, 128, 128, 0, 0, 255, 128, 128, 128, 0, 0, 255};  // two rows of B G R, B G R
    tga.append(reinterpret_cast<const char*>(pixels), 12);
    const fs::path p = dir / "ldr.tga";
    writeBytes(p, tga);

    FloatImage img;
    std::string err;
    CHECK(readPanorama(p.string(), img, &err));
    CHECK(img.w == 2 && img.h == 2);
    if (img.rgb.size() < 6) return;  // the read failed: the checks below would index an empty image
    const double mid = std::pow((128.0 / 255.0 + 0.055) / 1.055, 2.4);
    CHECK(std::fabs(img.rgb[0] - mid) < 1e-4);
    CHECK(std::fabs(img.rgb[1] - mid) < 1e-4);
    CHECK(std::fabs(img.rgb[2] - mid) < 1e-4);
    CHECK(std::fabs(img.rgb[3] - 1.0f) < 1e-6 && img.rgb[4] == 0.0f && img.rgb[5] == 0.0f);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(sky_pano_brightest_finds_spot) {
    const FloatImage p = spotPano(512, 256, 30.0, 40.0, 3.0, 100.0f, 0.05f);
    const Vec3 got = brightestDirection(p, 0.0);
    CHECK(angleDeg(got, dirAzEl(30.0, 40.0)) < 3.0);
    // A spot with no sky around it, and a hot single pixel elsewhere that must not beat the disc after the blur.
    FloatImage q = spotPano(512, 256, -120.0, -20.0, 3.0, 10.0f, 0.0f);
    q.rgb[(size_t(10) * 512 + 300) * 3] = 1000.0f;  // one hot texel
    CHECK(angleDeg(brightestDirection(q, 0.0), dirAzEl(-120.0, -20.0)) < 3.0);
}

TEST(sky_pano_brightest_yaw_moves_spot) {
    const FloatImage p = spotPano(512, 256, 30.0, 40.0, 3.0, 100.0f, 0.05f);
    const Vec3 got = brightestDirection(p, 90.0);
    CHECK(angleDeg(got, dirAzEl(120.0, 40.0)) < 3.0);
    CHECK(angleDeg(got, dirAzEl(30.0, 40.0)) > 60.0);
}

TEST(sky_pano_sun_key_matches_sun_convention) {
    // Azimuth 30, elevation 40: sunDirection(pitch, yaw) gives it at pitch -40, yaw 30.
    const std::string key = sunDirectionKeyFor(dirAzEl(30.0, 40.0));
    double pitch = 0, yaw = 0, roll = 99;
    CHECK(std::sscanf(key.c_str(), "%lf %lf %lf", &pitch, &yaw, &roll) == 3);
    CHECK(std::fabs(pitch + 40.0) < 0.01 && std::fabs(yaw - 30.0) < 0.01 && roll == 0.0);
    CHECK(angleDeg(sunToward(pitch, yaw), dirAzEl(30.0, 40.0)) < 0.01);
    // Below the horizon gives a positive pitch; straight up gives -90; yaw is kept in [0, 360).
    CHECK(sunDirectionKeyFor(dirAzEl(200.0, -15.0)).rfind("15 ", 0) == 0);
    CHECK(sunDirectionKeyFor(Vec3{0, 0, 1}) == "-90 0 0");
    double y2 = 0;
    std::sscanf(sunDirectionKeyFor(dirAzEl(-90.0, 10.0)).c_str(), "%*f %lf", &y2);
    CHECK(std::fabs(y2 - 270.0) < 0.01);
}

TEST(sky_pano_cube_face_holds_spot) {
    const FloatImage p = spotPano(512, 256, 30.0, 20.0, 2.0, 1.0f, 0.0f);
    std::string err;
    const std::string cube = cubeDdsFromPanorama(p, 256, 0.0, 0.0, &err);
    CHECK(!cube.empty());
    const MaxTexels m = maxTexels(cube, 256);
    // Elevation 20 keeps the spot inside one face (at 40 it straddles the +X / +Z edge): (0.81, 0.47, 0.34), +X = face 0.
    CHECK(m.faceMask == 1 << 0);
    CHECK(m.maxRed == 255);
    CHECK(angleDeg(m.centroid, dirAzEl(30.0, 20.0)) < 2.0);
    // Far away from the spot the cube is dark.
    CHECK(angleDeg(cubeTexelDir(1, 128, 128, 256), dirAzEl(30.0, 20.0)) > 90.0);
}

TEST(sky_pano_cube_yaw_moves_spot_to_face) {
    const FloatImage p = spotPano(512, 256, 30.0, 20.0, 2.0, 1.0f, 0.0f);
    std::string err;
    // Yaw 90: the spot moves to azimuth 120, which lies on the +Y face (face 2).
    const std::string cube = cubeDdsFromPanorama(p, 256, 90.0, 0.0, &err);
    CHECK(!cube.empty());
    const MaxTexels m = maxTexels(cube, 256);
    CHECK(m.faceMask == 1 << 2);
    CHECK(angleDeg(m.centroid, dirAzEl(120.0, 20.0)) < 2.0);
}

TEST(sky_pano_solid_colour_gives_uniform_faces) {
    const FloatImage p = []() {
        FloatImage q;
        q.w = 8;
        q.h = 4;
        q.rgb.resize(8 * 4 * 3);
        for (size_t i = 0; i < q.rgb.size(); i += 3) q.rgb[i] = 0.5f, q.rgb[i + 1] = 0.25f, q.rgb[i + 2] = 0.1f;
        return q;
    }();
    std::string err;
    for (double ev : {0.0, -1.0}) {
        const std::string cube = cubeDdsFromPanorama(p, 16, 33.0, ev, &err);
        CHECK(cube.size() == 128 + size_t(6) * 16 * 16 * 4);
        const double gain = std::pow(2.0, ev);
        const int wantR = expectedByte(0.5 * gain), wantG = expectedByte(0.25 * gain), wantB = expectedByte(0.1 * gain);
        bool uniform = true;
        for (size_t i = 128; i + 3 < cube.size(); i += 4) {
            const int b = uint8_t(cube[i]), g = uint8_t(cube[i + 1]), r = uint8_t(cube[i + 2]);
            uniform = uniform && std::abs(b - wantB) <= 1 && std::abs(g - wantG) <= 1 && std::abs(r - wantR) <= 1 &&
                      uint8_t(cube[i + 3]) == 255;
        }
        CHECK(uniform);
    }
}

TEST(sky_pano_dds_header_and_converter_accepts) {
    const FloatImage p = spotPano(64, 32, 0.0, 0.0, 1.0, 1.0f, 0.0f);
    std::string err;
    const std::string cube = cubeDdsFromPanorama(p, 32, 0.0, 0.0, &err);
    CHECK(cube.size() == 128 + size_t(6) * 32 * 32 * 4);
    CHECK(cube.compare(0, 4, "DDS ") == 0);
    CHECK(rd32(cube, 4) == 124);
    CHECK(rd32(cube, 8) == (0x1 | 0x2 | 0x4 | 0x8 | 0x1000));
    CHECK(rd32(cube, 12) == 32 && rd32(cube, 16) == 32);  // height, width
    CHECK(rd32(cube, 20) == 32 * 4);                      // pitch
    CHECK(rd32(cube, 28) == 1);                           // one mip
    CHECK(rd32(cube, 76) == 32 && rd32(cube, 80) == 0x41 && rd32(cube, 84) == 0 && rd32(cube, 88) == 32);
    CHECK(rd32(cube, 92) == 0x00FF0000 && rd32(cube, 96) == 0x0000FF00 && rd32(cube, 100) == 0x000000FF &&
          rd32(cube, 104) == 0xFF000000);
    CHECK(rd32(cube, 112) == 0xFE00);
    CHECK(isConverterSkyCubemap(cube));
    SkyConvertInfo info;
    std::string out;
    CHECK(skyCubemapFromDds(cube, out, {}, &info, &err) && info.passedThrough && out == cube);

    // Bad face sizes are refused, not clamped.
    std::string bad = cubeDdsFromPanorama(p, 2, 0.0, 0.0, &err);
    CHECK(bad.empty() && !err.empty());
}

TEST(sky_pano_faces_match_sky_image_hemi_mapping) {
    // A 2D hemi-octahedral source (the stock sky layout sky_image.cpp converts) with one lit texel. Its direction is
    // the exact centre of that texel under sky_image.cpp's hemiOctTexel mapping, inverted here.
    const int hw = 256, hh = 256, ix = 179, iy = 102;
    std::string hemi = "DDS ";
    put32(hemi, 124);
    put32(hemi, 0x1 | 0x2 | 0x4 | 0x8 | 0x1000);
    put32(hemi, uint32_t(hh));
    put32(hemi, uint32_t(hw));
    put32(hemi, uint32_t(hw * 4));
    put32(hemi, 0);
    put32(hemi, 1);
    for (int i = 0; i < 11; ++i) put32(hemi, 0);
    put32(hemi, 32);
    put32(hemi, 0x41);
    put32(hemi, 0);
    put32(hemi, 32);
    put32(hemi, 0x00FF0000);
    put32(hemi, 0x0000FF00);
    put32(hemi, 0x000000FF);
    put32(hemi, 0xFF000000);
    put32(hemi, 0x1000);
    for (int i = 0; i < 4; ++i) put32(hemi, 0);
    std::string px(size_t(hw) * hh * 4, '\0');
    const size_t at = (size_t(iy) * hw + ix) * 4;
    for (int c = 0; c < 4; ++c) px[at + c] = char(0xFF);
    hemi += px;

    const double U = 2.0 * (ix + 0.5) / hw - 1.0, V = 2.0 * (iy + 0.5) / hh - 1.0;
    const double ox = (U + V) / 2.0, oy = (U - V) / 2.0;
    const Vec3 spot{ox, oy, 1.0 - std::fabs(ox) - std::fabs(oy)};
    CHECK(spot.z > 0.3);

    std::string skyCube;
    SkyConvertOptions opt;
    opt.faceSize = 256;
    std::string err;
    CHECK(skyCubemapFromDds(hemi, skyCube, opt, nullptr, &err));
    const MaxTexels fromSkyImage = maxTexels(skyCube, 256);

    // The same direction in an equirectangular panorama, through the new path.
    const double az = std::atan2(spot.y, spot.x) * 180.0 / kPiT;
    const double el = std::asin(spot.z / length(spot)) * 180.0 / kPiT;
    const FloatImage p = spotPano(512, 256, az, el, 1.0, 1.0f, 0.0f);
    const std::string mine = cubeDdsFromPanorama(p, 256, 0.0, 0.0, &err);
    CHECK(!mine.empty());
    const MaxTexels fromPanorama = maxTexels(mine, 256);

    // Both faces must agree on the face and the direction (within a few degrees: one hi-oct texel is about a degree).
    CHECK(fromSkyImage.faceMask == fromPanorama.faceMask);
    CHECK(angleDeg(fromSkyImage.centroid, spot) < 3.0);
    CHECK(angleDeg(fromPanorama.centroid, spot) < 3.0);
}
