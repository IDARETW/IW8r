// Sky cube maps for the x64-zt extraction (src/core/sky_image.*, docs/research/33_sky_route.md): the hemi-octahedral 2D
// source (the layout of Replay's stock sky_mp_* images) to the legacy B8G8R8A8 cube that x64-zt's DecodeDds reads, and the
// compiler writing images/<name>.dds and world.json "sky".
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "brush_ops.h"
#include "compile_iw8.h"
#include "json_lite.h"
#include "map_text.h"
#include "sky_image.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

void put32(std::string& s, uint32_t v) {
    for (int i = 0; i < 4; ++i) s.push_back(char((v >> (8 * i)) & 0xFF));
}
uint32_t get32(const std::string& s, size_t off) {
    uint32_t v = 0;
    std::memcpy(&v, s.data() + off, 4);
    return v;
}

// DX10 header for a 2D texture of dxgi format `fmt`.
std::string dx10Header(int w, int h, uint32_t fmt, uint32_t miscFlag = 0) {
    std::string out = "DDS ";
    put32(out, 124);
    put32(out, 0x1 | 0x2 | 0x4 | 0x1000);
    put32(out, uint32_t(h));
    put32(out, uint32_t(w));
    put32(out, 0);
    put32(out, 0);
    put32(out, 1);
    for (int i = 0; i < 11; ++i) put32(out, 0);
    put32(out, 32);
    put32(out, 0x4);  // DDPF_FOURCC
    put32(out, 0x30315844u);  // "DX10"
    for (int i = 0; i < 5; ++i) put32(out, 0);
    put32(out, 0x1000);
    for (int i = 0; i < 4; ++i) put32(out, 0);
    put32(out, fmt);
    put32(out, 3);  // TEXTURE2D
    put32(out, miscFlag);
    put32(out, 1);
    put32(out, 0);
    return out;
}

uint16_t toHalf(float f) {  // positive normal values only (test data)
    if (f <= 0.0f) return 0;
    int e;
    const float m = std::frexp(f, &e);  // f = m * 2^e, m in [0.5, 1)
    const int he = e - 1 + 15;
    const uint32_t mant = uint32_t(std::lround((m * 2.0f - 1.0f) * 1024.0f)) & 0x3FF;
    return uint16_t((he << 10) | mant);
}

// 16x16 RGBA16F hemi-octahedral sky: the zenith (centre, max(|u|,|v|) < 0.5) blue, the rest red. Linear values 0.4.
std::string syntheticHdrSky() {
    const int n = 16;
    std::string dds = dx10Header(n, n, 10);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float u = (x + 0.5f) / n * 2 - 1, v = (y + 0.5f) / n * 2 - 1;
            const bool zenith = std::max(std::fabs(u), std::fabs(v)) < 0.5f;
            const float rgba[4] = {zenith ? 0.0f : 0.4f, 0.0f, zenith ? 0.4f : 0.0f, 1.0f};
            for (float c : rgba) {
                const uint16_t h = toHalf(c);
                dds.push_back(char(h & 0xFF));
                dds.push_back(char(h >> 8));
            }
        }
    return dds;
}

// Texel (x, y) of face f of a B8G8R8A8 cube made by skyCubemapFromDds (128-byte header, one mip): {r, g, b}.
void cubeTexel(const std::string& cube, int f, int x, int y, int rgb[3]) {
    const int size = int(get32(cube, 16));
    const size_t off = 128 + (size_t(f) * size * size + size_t(y) * size + x) * 4;
    rgb[0] = uint8_t(cube[off + 2]);
    rgb[1] = uint8_t(cube[off + 1]);
    rgb[2] = uint8_t(cube[off + 0]);
}

void writeBytes(const fs::path& p, const std::string& bytes) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << bytes;
}

std::string readBytes(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

TEST(sky_image_name) {
    CHECK(skyImageName("sky_mp_shipment") == "sky_mp_shipment");
    CHECK(skyImageName("tw/sky_mp_shipment") == "sky_mp_shipment");
    CHECK(skyImageName("Sky MP Rust") == "sky_mp_rust");
    CHECK(skyImageName("").empty());
}

// The hemi-octahedral source becomes a converter cube: legacy header, caps2 0xFE00, 32-bit BGRA masks, six faces and
// one mip; the zenith face samples the centre of the source, the horizon the border, below the horizon the border mean.
TEST(sky_cube_from_hemi_octahedral_hdr) {
    std::string cube, err;
    SkyConvertOptions so;
    so.faceSize = 8;
    so.exposure = 1.0f;
    SkyConvertInfo info;
    CHECK(skyCubemapFromDds(syntheticHdrSky(), cube, so, &info, &err));
    CHECK(err.empty());
    CHECK(info.hdr && !info.passedThrough && info.sourceWidth == 16 && info.sourceDxgiFormat == 10);
    CHECK(cube.size() == 128 + 6 * 8 * 8 * 4);
    CHECK(cube.compare(0, 4, "DDS ") == 0);
    CHECK(get32(cube, 12) == 8 && get32(cube, 16) == 8);
    CHECK(get32(cube, 84) == 0 && get32(cube, 88) == 32);                                 // no fourCC, 32 bits
    CHECK(get32(cube, 92) == 0x00FF0000u && get32(cube, 104) == 0xFF000000u);             // R and A masks
    CHECK((get32(cube, 112) & 0xFE00u) == 0xFE00u);                                       // complete cube
    CHECK(isConverterSkyCubemap(cube));
    int c[3];
    // +Z (face 4) centre: the zenith, blue. sRGB of linear 0.4 is 170.
    cubeTexel(cube, 4, 4, 4, c);
    CHECK(c[0] == 0 && c[2] >= 165 && c[2] <= 175);
    // -Y (face 3), the row just above the horizon (b < 0 is up on this face): the border, red.
    cubeTexel(cube, 3, 4, 3, c);
    CHECK(c[0] >= 165 && c[2] == 0);
    // -Z (face 5) centre: below the horizon, the border ring mean (red).
    cubeTexel(cube, 5, 4, 4, c);
    CHECK(c[0] >= 150 && c[2] <= 20);
    // Every alpha is opaque.
    bool opaque = true;
    for (size_t i = 128 + 3; i < cube.size(); i += 4) opaque = opaque && uint8_t(cube[i]) == 0xFF;
    CHECK(opaque);
}

// Auto exposure maps the bright end (99.5th percentile luminance) to 0.9.
TEST(sky_cube_auto_exposure) {
    std::string cube, err;
    SkyConvertOptions so;
    so.faceSize = 4;
    SkyConvertInfo info;
    CHECK(skyCubemapFromDds(syntheticHdrSky(), cube, so, &info, &err));
    CHECK(info.highLuminance > 0.0f);
    CHECK(std::fabs(info.exposure * info.highLuminance - 0.9f) < 1e-3f);
}

// An LDR RGBA8 source keeps its bytes (no exposure); a ready legacy cube passes through; a DX10 cube is refused.
TEST(sky_cube_ldr_passthrough_and_rejects) {
    std::string ldr = dx10Header(8, 8, 28);
    for (int i = 0; i < 64; ++i) ldr += std::string("\x80\x40\x20\xFF", 4);
    std::string cube, err;
    SkyConvertOptions so;
    so.faceSize = 4;
    SkyConvertInfo info;
    CHECK(skyCubemapFromDds(ldr, cube, so, &info, &err));
    CHECK(!info.hdr);
    int c[3];
    cubeTexel(cube, 2, 1, 1, c);
    CHECK(c[0] == 0x80 && c[1] == 0x40 && c[2] == 0x20);

    std::string again;
    CHECK(skyCubemapFromDds(cube, again, so, &info, &err));
    CHECK(info.passedThrough && again == cube);

    std::string dxCube = dx10Header(8, 8, 28, 0x4);
    dxCube.append(size_t(8) * 8 * 4 * 6, '\0');
    CHECK(!skyCubemapFromDds(dxCube, cube, so, &info, &err));
    CHECK(err.find("cube") != std::string::npos);
    CHECK(!skyCubemapFromDds("not a dds", cube, so, &info, &err));
    CHECK(!isConverterSkyCubemap(ldr));
}

// The compiler converts opt.skyDdsPath into extracted/images/<name>.dds and names it in world.json "sky".
TEST(sky_compile_writes_cube_and_world_sky) {
    const fs::path dir = fs::path(test::tmpDir()) / "sky_tests";
    std::error_code ec;
    fs::remove_all(dir, ec);
    const fs::path src = dir / "src" / "sky_mp_test.dds";
    writeBytes(src, syntheticHdrSky());
    const auto prim = makeBrushPrimitive(makeBox({{0, 0, -16}, {256, 256, 0}}, " world/a 128 128 0 0 0 0"), "\n");
    CHECK(bool(prim));
    if (!prim) return;
    const std::string text = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n\"skyname\" \"tw/Sky_MP_Test\"\n" + prim->text() +
                             "}\n{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"64 128 16\"\n}\n"
                             "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"128 128 16\"\n}\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(bool(pr.map));
    if (!pr.map) return;
    const auto compile = [&](const std::string& out, const std::string& sky, CompileReport& rep) {
        CompileOptions opt;
        opt.mapId = "mp_sky_t";
        opt.skyDdsPath = sky;
        opt.skyFaceSize = 16;
        std::string err;
        const bool ok = compileIw8Extraction(*pr.map, out, opt, rep, &err);
        if (!ok) std::printf("  compile error: %s\n", err.c_str());
        return ok;
    };
    const auto worldSky = [](const fs::path& out) {
        std::string err;
        const auto v = json::parse(readBytes(out / "extracted" / "maps" / "mp" / "mp_sky_t.d3dbsp.replay-world.json"), &err);
        const json::Value* s = v ? v->get("sky") : nullptr;
        return s && s->string() ? *s->string() : std::string("<missing>");
    };
    {
        const fs::path out = dir / "with";
        CompileReport rep;
        CHECK(compile(out.string(), src.string(), rep));
        CHECK(rep.sky == "sky_mp_test");
        CHECK(worldSky(out) == "sky_mp_test");
        const std::string cube = readBytes(out / "extracted" / "images" / "sky_mp_test.dds");
        CHECK(isConverterSkyCubemap(cube));
        CHECK(cube.size() == 128 + 6 * 16 * 16 * 4);
        bool warned = false;
        for (const auto& w : rep.warnings) warned = warned || w.find("Sky_MP_Test") != std::string::npos;
        CHECK(!warned);
    }
    {
        const fs::path out = dir / "missing";
        CompileReport rep;
        CHECK(compile(out.string(), (dir / "nope.dds").string(), rep));
        CHECK(rep.sky.empty() && worldSky(out).empty());
        bool warned = false;
        for (const auto& w : rep.warnings) warned = warned || w.find("cannot read") != std::string::npos;
        CHECK(warned);
        CHECK(!fs::exists(out / "extracted" / "images" / "sky_mp_test.dds"));
    }
    {
        const fs::path out = dir / "none";
        CompileReport rep;
        CHECK(compile(out.string(), "", rep));
        CHECK(worldSky(out).empty());
        bool warned = false;
        for (const auto& w : rep.warnings) warned = warned || w.find("no sky image was provided") != std::string::npos;
        CHECK(warned);
    }
}

// Research 132 G10: RGBM faces keep an HDR sky's range: srgbToLinear(rgb) * alpha * range gives back the linear value,
// above 1 too (exposure 8: the sky's 0.4 becomes 3.2, which the 8-bit faces clip to 1).
TEST(sky_cube_rgbm_keeps_hdr) {
    std::string cube, err;
    SkyConvertOptions so;
    so.faceSize = 8;
    so.exposure = 8.0f;
    so.rgbm = true;
    SkyConvertInfo info;
    CHECK(skyCubemapFromDds(syntheticHdrSky(), cube, so, &info, &err));
    CHECK(info.hdr && info.rgbm);
    const auto toLin = [](int b) {
        const float s = float(b) / 255.0f;
        return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
    };
    int c[3];
    cubeTexel(cube, 4, 4, 4, c);  // the zenith, blue
    const size_t off = 128 + (size_t(4) * 8 * 8 + 4 * 8 + 4) * 4;
    const float m = float(uint8_t(cube[off + 3])) / 255.0f * so.rgbmRange;
    const float blue = toLin(c[2]) * m;
    CHECK(std::fabs(blue - 3.2f) < 0.1f && c[0] == 0);
    // Without RGBM the same texel clips at 1 (sRGB 255).
    std::string clipped;
    so.rgbm = false;
    CHECK(skyCubemapFromDds(syntheticHdrSky(), clipped, so, &info, &err) && !info.rgbm);
    cubeTexel(clipped, 4, 4, 4, c);
    CHECK(c[2] == 255);
}
