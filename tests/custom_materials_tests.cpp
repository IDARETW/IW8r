// Custom material library (src/core/custom_materials.cpp, doc 21): PNG decode through stb_image, DDS encode
// (header, mip chain, round trip), library load/find/create, and a compile that uses one custom material.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "compile_iw8.h"
#include "custom_materials.h"
#include "iw8_contents.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

std::string freshDir(const std::string& name) {
    const fs::path p = fs::path(test::tmpDir()) / "custom_materials" / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p.string();
}

std::vector<uint8_t> readBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

void writeBytes(const std::string& path, const std::vector<uint8_t>& data) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

void writeText(const std::string& path, const std::string& text) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
}

std::string readFileOrEmpty(const std::string& path) {
    std::string text;
    readFile(path, text);
    return text;
}

uint32_t crc32Of(const std::vector<uint8_t>& d) {
    uint32_t crc = 0xFFFFFFFFu;
    for (const uint8_t byte : d) {
        crc ^= byte;
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void be32(std::vector<uint8_t>& v, uint32_t x) {
    for (int s = 24; s >= 0; s -= 8) v.push_back(uint8_t(x >> s));
}

void pngChunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    be32(out, uint32_t(data.size()));
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    be32(out, crc32Of(td));
}

// Minimal PNG writer for tests: 8-bit RGBA, stored (uncompressed) deflate blocks.
std::vector<uint8_t> pngBytes(int w, int h, const std::vector<uint8_t>& rgba) {
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);  // filter: none
        raw.insert(raw.end(), rgba.begin() + size_t(y) * w * 4, rgba.begin() + size_t(y + 1) * w * 4);
    }
    std::vector<uint8_t> z{0x78, 0x01};
    size_t pos = 0;
    do {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + n == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(uint8_t(n));
        z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n));
        z.push_back(uint8_t((~n) >> 8));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    } while (pos < raw.size());
    uint32_t a = 1, b = 0;
    for (const uint8_t byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    be32(z, (b << 16) | a);

    std::vector<uint8_t> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    be32(ihdr, uint32_t(w));
    be32(ihdr, uint32_t(h));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    pngChunk(out, "IHDR", ihdr);
    pngChunk(out, "IDAT", z);
    pngChunk(out, "IEND", {});
    return out;
}

std::vector<uint8_t> flatRgba(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    std::vector<uint8_t> px(size_t(w) * h * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = a;
    }
    return px;
}

uint32_t le32(const std::vector<uint8_t>& b, size_t at) {
    return uint32_t(b[at]) | (uint32_t(b[at + 1]) << 8) | (uint32_t(b[at + 2]) << 16) | (uint32_t(b[at + 3]) << 24);
}

// Size of a full mip chain of w x h RGBA8 (levels down to 1x1).
size_t chainBytes(uint32_t w, uint32_t h, uint32_t& levels) {
    size_t total = 0;
    levels = 0;
    for (;;) {
        total += size_t(w) * h * 4;
        ++levels;
        if (w == 1 && h == 1) break;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
    return total;
}

bool contains(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }

std::string samplePathOf() {
    fs::path p = fs::current_path();
    for (int up = 0; up < 6; ++up, p = p.parent_path()) {
        const fs::path cand = p / "tests" / "data" / "iw8_sample_room.map";
        if (fs::exists(cand)) return cand.string();
    }
    return {};
}

}  // namespace

TEST(custom_png_decodes_through_stb) {
    const std::string dir = freshDir("png");
    std::vector<uint8_t> px;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            px.push_back(uint8_t(x * 60));
            px.push_back(uint8_t(y * 60));
            px.push_back(128);
            px.push_back(255);
        }
    const std::string path = dir + "/grad.png";
    writeBytes(path, pngBytes(4, 4, px));
    int w = 0, h = 0;
    std::vector<uint8_t> out;
    std::string err;
    CHECK(decodeImageFile(path, w, h, out, &err));
    CHECK(w == 4 && h == 4);
    CHECK(out.size() == 64);
    if (out.size() == 64) {
        const size_t at = (2 * 4 + 3) * 4;  // x=3, y=2
        CHECK(out[at] == 180 && out[at + 1] == 120 && out[at + 2] == 128 && out[at + 3] == 255);
    }
    CHECK(!decodeImageFile(dir + "/missing.png", w, h, out, &err));
    CHECK(!err.empty());
}

TEST(custom_dds_header_mips_and_round_trip) {
    const std::string dir = freshDir("dds");
    // 64x16, flat (10,20,30,40): 7 mip levels (64x16 down to 1x1), top-left pixel survives the round trip.
    writeBytes(dir + "/flat.png", pngBytes(64, 16, flatRgba(64, 16, 10, 20, 30, 40)));
    const std::string dds = dir + "/out/flat_c.dds";
    std::string err;
    CHECK(writeDdsFromImage(dir + "/flat.png", dds, false, &err, true));
    const auto bytes = readBytes(dds);
    uint32_t levels = 0;
    const size_t expected = 128 + chainBytes(64, 16, levels);
    CHECK(bytes.size() == expected);
    CHECK(bytes.size() >= 128 && std::string(bytes.begin(), bytes.begin() + 4) == "DDS ");
    if (bytes.size() >= 128) {
        CHECK(le32(bytes, 4) == 124);
        CHECK(le32(bytes, 12) == 16);                // height
        CHECK(le32(bytes, 16) == 64);                // width
        CHECK(le32(bytes, 28) == levels);            // mip count
        CHECK(le32(bytes, 76) == 32);                // pixel format size
        CHECK(le32(bytes, 84) == 0);                 // uncompressed
        CHECK(le32(bytes, 88) == 32);                // bits per pixel
        CHECK(le32(bytes, 92) == 0x00FF0000u && le32(bytes, 104) == 0xFF000000u);
    }
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    CHECK(decodeImageFile(dds, w, h, rgba, &err));
    CHECK(w == 64 && h == 16);
    if (rgba.size() == size_t(64) * 16 * 4)
        CHECK(rgba[0] == 10 && rgba[1] == 20 && rgba[2] == 30 && rgba[3] == 40);

    // 100x40 is not a power of two: it becomes 128x32 (nearest, ties up) with 8 levels.
    writeBytes(dir + "/odd.png", pngBytes(100, 40, flatRgba(100, 40, 200, 100, 50, 255)));
    const std::string oddDds = dir + "/out/odd_c.dds";
    CHECK(writeDdsFromImage(dir + "/odd.png", oddDds, false, &err));
    const auto odd = readBytes(oddDds);
    const size_t oddExpected = 128 + chainBytes(128, 32, levels);
    CHECK(odd.size() == oddExpected);
    if (odd.size() >= 128) {
        CHECK(le32(odd, 12) == 32 && le32(odd, 16) == 128 && le32(odd, 28) == levels);
    }

    // Normal maps are opaque even when the source has no alpha.
    writeBytes(dir + "/normal.png", pngBytes(4, 4, flatRgba(4, 4, 128, 128, 255, 0)));
    std::vector<uint8_t> dds2;
    CHECK(encodeDdsFromImage(dir + "/normal.png", true, false, dds2, &err));
    CHECK(dds2.size() >= 128 + 4 && dds2[128 + 3] == 255);
}

TEST(custom_library_load_find_and_errors) {
    const std::string lib = freshDir("library");
    writeBytes(lib + "/brick/brick_red.png", pngBytes(8, 8, flatRgba(8, 8, 150, 50, 40, 255)));
    writeText(lib + "/brick/brick_red.iw8mat.json",
              "{\"name\":\"custom/brick_red\",\"color\":\"brick_red.png\",\"surface\":\"brick\",\"alphaTest\":true,"
              "\"scale\":[256,256]}");
    // Default name comes from the path; a missing image is an error, but the rest still loads.
    writeText(lib + "/plain.iw8mat.json", "{\"color\":\"nowhere.png\"}");
    writeText(lib + "/bad_surface.iw8mat.json", "{\"color\":\"brick/brick_red.png\",\"surface\":\"cheese\"}");

    MaterialLibrary library;
    std::string err;
    CHECK(!library.load({lib}, &err));
    CHECK(contains(err, "not found"));
    CHECK(contains(err, "unknown surface type 'cheese'"));
    const CustomMaterial* m = library.find("CUSTOM/Brick_Red");  // case-insensitive
    CHECK(m != nullptr);
    if (m) {
        CHECK(m->name == "custom/brick_red");
        CHECK(m->surfaceType == iw3SurfaceTypeForMaterial("brick"));
        CHECK(m->surfaceType == 2);
        CHECK(m->alphaTest && !m->twoSided);
        CHECK(m->width == 256 && m->height == 256);
        CHECK(fs::path(m->colorImage).filename() == "brick_red.png");
        CHECK(m->normalImage.empty() && m->specImage.empty());
    }
    CHECK(library.find("plain") == nullptr);
    CHECK(library.find("custom/none") == nullptr);
    const auto names = library.names();
    CHECK(names.size() == 1 && names[0] == "custom/brick_red");

    // Reserved and malformed names are rejected.
    CHECK(!isValidCustomMaterialName("mc/lambert1"));
    CHECK(!isValidCustomMaterialName("a/../b"));
    CHECK(!isValidCustomMaterialName("a b"));
    CHECK(isValidCustomMaterialName("custom/brick_red"));
}

TEST(custom_library_create_from_image) {
    const std::string dir = freshDir("create");
    writeBytes(dir + "/src/wood_grain.png", pngBytes(16, 16, flatRgba(16, 16, 120, 80, 40, 255)));
    std::string err;
    CHECK(MaterialLibrary::createFromImage(dir + "/src/wood_grain.png", dir + "/lib", "custom/wood_grain", &err));
    CHECK(fs::exists(dir + "/lib/custom/wood_grain.iw8mat.json"));
    // Creating it again must not overwrite.
    CHECK(!MaterialLibrary::createFromImage(dir + "/src/wood_grain.png", dir + "/lib", "custom/wood_grain", &err));
    MaterialLibrary library;
    CHECK(library.load({dir + "/lib"}, &err));
    const CustomMaterial* m = library.find("custom/wood_grain");
    CHECK(m != nullptr);
    if (m) {
        CHECK(m->surfaceType == iw3SurfaceTypeForMaterial("custom/wood_grain"));
        CHECK(fs::exists(m->colorImage));
    }
    // A file that is not an image is refused.
    writeText(dir + "/src/notes.txt", "not an image");
    CHECK(!MaterialLibrary::createFromImage(dir + "/src/notes.txt", dir + "/lib", "custom/notes", &err));
}

TEST(compile_uses_custom_material_and_keeps_others_grey) {
    const std::string sample = samplePathOf();
    CHECK(!sample.empty());
    if (sample.empty()) return;
    const std::string dir = freshDir("compile");
    writeBytes(dir + "/library/custom/brick_red.png", pngBytes(8, 8, flatRgba(8, 8, 150, 50, 40, 255)));
    writeBytes(dir + "/library/custom/brick_red_n.png", pngBytes(8, 8, flatRgba(8, 8, 128, 128, 255, 255)));
    writeText(dir + "/library/custom/brick_red.iw8mat.json",
              "{\"name\":\"custom/brick_red\",\"color\":\"brick_red.png\",\"normal\":\"brick_red_n.png\","
              "\"surface\":\"brick\"}");
    MaterialLibrary library;
    std::string err;
    CHECK(library.load({dir + "/library"}, &err));

    // The sample with one of its materials replaced by the custom name.
    std::string text;
    CHECK(readFile(sample, text));
    const std::string from = "tm2co/me_curb_stone_01";
    size_t replaced = 0;
    for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + 1)) ++replaced;
    CHECK(replaced > 0);
    for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at)) text.replace(at, from.size(), "custom/brick_red");
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;

    CompileOptions opt;
    opt.mapId = "mp_iw8_sample";
    opt.library = &library;
    CompileReport rep;
    const std::string out = dir + "/out";
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    if (!fs::exists(out + "/complete")) return;

    const std::string world = readFileOrEmpty(out + "/extracted/maps/mp/mp_iw8_sample.d3dbsp.replay-world.json");
    CHECK(contains(world, "\"material\":\"custom/brick_red\""));
    CHECK(contains(world, "\"material\":\"iw8r/grey\""));

    const std::string matPath = out + "/extracted/materials/custom/brick_red.json";
    CHECK(fs::exists(matPath));
    const std::string mat = readFileOrEmpty(matPath);
    const std::string stem = "iw8rm_custom_brick_red_" + customNameHash("custom/brick_red");
    CHECK(contains(mat, "\"techniqueSet\":\"iw8rcm" + customNameHash("custom/brick_red") + "\""));
    CHECK(contains(mat, "\"surfaceTypeBits\":" + std::to_string(iw3SurfaceTypeBits(2))));
    CHECK(contains(mat, "\"name\":\"colorMap\",\"semantic\":\"colorMap\",\"image\":\"" + stem + "_c\""));
    CHECK(contains(mat, "\"name\":\"normalMap\",\"semantic\":\"normalMap\",\"image\":\"" + stem + "_n\""));
    CHECK(!contains(mat, "specularMap"));
    CHECK(fs::exists(out + "/extracted/images/" + stem + "_c.dds"));
    CHECK(fs::exists(out + "/extracted/images/" + stem + "_n.dds"));
    CHECK(fs::exists(out + "/extracted/materials/iw8r/grey.json"));

    // Collision brushes whose most-used face is the custom material take its surface type (brick = 2).
    const std::string col = readFileOrEmpty(out + "/extracted/maps/mp/mp_iw8_sample.d3dbsp.replay-collision.json");
    CHECK(contains(col, "\"surface_type\":" + std::to_string(replaySurfaceTypeForIw3(2))));

    bool sawCustomWarning = false;
    for (const auto& w : rep.warnings)
        if (contains(w, "custom material")) sawCustomWarning = true;
    CHECK(sawCustomWarning);
}

// 5.8 (research 82): a decal volume that names a library material gets a twin in the converter's effect-decal form
// (<name>_dcl); the same material painted on a face stays a normal custom material.
TEST(compile_decal_volume_custom_material_is_decal_form) {
    const std::string sample = samplePathOf();
    CHECK(!sample.empty());
    if (sample.empty()) return;
    const std::string dir = freshDir("decalform");
    writeBytes(dir + "/library/custom/stain.png", pngBytes(8, 8, flatRgba(8, 8, 90, 60, 40, 128)));
    writeBytes(dir + "/library/custom/stain_n.png", pngBytes(8, 8, flatRgba(8, 8, 128, 128, 255, 255)));
    writeText(dir + "/library/custom/stain.iw8mat.json",
              "{\"name\":\"custom/stain\",\"color\":\"stain.png\",\"normal\":\"stain_n.png\",\"blend\":true}");
    MaterialLibrary library;
    std::string err;
    CHECK(library.load({dir + "/library"}, &err));
    std::string text;
    CHECK(readFile(sample, text));
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    text += "{" + eol + "\"classname\" \"iw8r_decal\"" + eol + "\"origin\" \"0 0 4\"" + eol + "\"angles\" \"90 0 0\"" + eol +
            "\"size\" \"8 64 64\"" + eol + "\"material\" \"custom/stain\"" + eol + "}" + eol;
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    CompileOptions opt;
    opt.mapId = "mp_iw8_sample";
    opt.library = &library;
    CompileReport rep;
    const std::string out = dir + "/out";
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    if (!fs::exists(out + "/complete")) return;
    const std::string world = readFileOrEmpty(out + "/extracted/maps/mp/mp_iw8_sample.d3dbsp.replay-world.json");
    CHECK(contains(world, "\"decal_volumes\":[") && contains(world, "\"material\":\"custom/stain_dcl\""));
    const std::string mat = readFileOrEmpty(out + "/extracted/materials/custom/stain_dcl.json");
    const std::string stem = "iw8rm_custom_stain_dcl_" + customNameHash("custom/stain_dcl");
    CHECK(contains(mat, "\"_game\":\"iw3\",\"_type\":\"material\",\"_version\":1"));
    CHECK(contains(mat, "\"techniqueSet\":\"wc_l_sm_b0c0n0\""));
    CHECK(contains(mat, "\"textureAtlas\":{\"rows\":1,\"columns\":1},\"stateFlags\":52,\"cameraRegion\":\"decal\""));
    CHECK(contains(mat, "\"name\":\"colorTint\",\"literal\":[1,1,1,1]"));
    CHECK(contains(mat, "\"semantic\":\"colorMap\",\"image\":\"" + stem + "_c\""));
    CHECK(contains(mat, "\"semantic\":\"normalMap\",\"image\":\"" + stem + "_n\""));
    CHECK(!contains(mat, "specularMap") && !contains(mat, "surfaceTypeBits"));
    CHECK(fs::exists(out + "/extracted/images/" + stem + "_c.dds"));
    CHECK(fs::exists(out + "/extracted/images/" + stem + "_n.dds"));
    // Not painted on any face, so the normal form of the material is not written.
    CHECK(!fs::exists(out + "/extracted/materials/custom/stain.json"));
}

TEST(compile_blended_custom_material_gets_b0_techset) {
    // 8.10 / research 66: a blended material (decal, glass) is written with the converter's "_b<n>" techset tag.
    const std::string sample = samplePathOf();
    if (sample.empty()) return;
    const std::string dir = freshDir("compile_blend");
    writeBytes(dir + "/library/custom/decal_dirt.png", pngBytes(8, 8, flatRgba(8, 8, 90, 70, 50, 128)));
    writeText(dir + "/library/custom/decal_dirt.iw8mat.json", "{\"name\":\"custom/decal_dirt\",\"color\":\"decal_dirt.png\",\"blend\":true}");
    MaterialLibrary library;
    std::string err;
    CHECK(library.load({dir + "/library"}, &err));
    const CustomMaterial* cm = library.find("custom/decal_dirt");
    CHECK(cm && cm->blend && !cm->alphaTest);
    std::string text;
    CHECK(readFile(sample, text));
    const std::string from = "tm2co/me_curb_stone_01";
    for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at)) text.replace(at, from.size(), "custom/decal_dirt");
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    CompileOptions opt;
    opt.mapId = "mp_iw8_blend";
    opt.library = &library;
    CompileReport rep;
    const std::string out = dir + "/out";
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const std::string mat = readFileOrEmpty(out + "/extracted/materials/custom/decal_dirt.json");
    CHECK(contains(mat, "\"techniqueSet\":\"iw8rcm" + customNameHash("custom/decal_dirt") + "_b0\""));
}

