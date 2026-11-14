#include "volume_mask.h"
#include "test_harness.h"

#include "compile_iw8.h"
#include "map_text.h"

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace iw8r;
namespace fs = std::filesystem;

// Research 127 part A: the masks of a volumetric box (keys mask .. mask4 of an iw8r_volume).
TEST(volume_mask_parse_and_format) {
    VolumeMask m;
    std::string err;
    // A picture and one size: both sizes, the z axis, no scroll.
    CHECK(parseVolumeMask("clouds_soft 800", m, &err));
    CHECK(m.image == "clouds_soft" && m.axis == 2 && m.size[0] == 800 && m.size[1] == 800 && m.scroll[0] == 0 && m.offset[1] == 0);
    CHECK(formatVolumeMask(m) == "clouds_soft 800");
    // The full form; a folder and an ending in the name are dropped, the case is lowered.
    CHECK(parseVolumeMask("masks/Clouds_Hard.png x 400 200 50 -75 16 32", m, &err));
    CHECK(m.image == "clouds_hard" && m.axis == 0 && m.size[0] == 400 && m.size[1] == 200 && m.scroll[0] == 50 && m.scroll[1] == -75 &&
          m.offset[0] == 16 && m.offset[1] == 32);
    CHECK(formatVolumeMask(m) == "clouds_hard x 400 200 50 -75 16 32");
    VolumeMask again;
    CHECK(parseVolumeMask(formatVolumeMask(m), again) && again.axis == 0 && again.scroll[1] == -75 && again.offset[1] == 32);
    CHECK(parseVolumeMask("mist y 300 300 20 0", m) && m.axis == 1 && formatVolumeMask(m) == "mist y 300 300 20 0");
    CHECK(parseVolumeMask("mist", m) && m.size[0] == 256 && formatVolumeMask(m) == "mist 256");
    // Refused: no picture, a size of 0, three numbers, a word where a number belongs.
    CHECK(!parseVolumeMask("", m, &err) && !err.empty());
    CHECK(!parseVolumeMask("mist 0", m, &err));
    CHECK(!parseVolumeMask("mist 100 100 5", m, &err));
    CHECK(!parseVolumeMask("mist wide", m, &err));
    CHECK(!parseVolumeMask("!!! 100", m, &err));
    CHECK(volumeMaskImageName("Clouds Soft.PNG") == "iw8r_vmask_cloudssoft");
}

TEST(volume_mask_where_it_reads_the_picture) {
    // The game's rule (research 127 A.2): along z the picture lies on the box's x and y, v turned over, half a repeat
    // added, and the scroll moves it with time.
    VolumeMask m;
    CHECK(parseVolumeMask("clouds z 400 200 50 0 100 0", m));
    float t[4];
    volumeMaskTerms(m, 0, t);
    CHECK(std::fabs(t[0] - 1.0f / 400) < 1e-9f && std::fabs(t[1] - 1.0f / 200) < 1e-9f);
    CHECK(std::fabs(t[2] - 0.75f) < 1e-6f && std::fabs(t[3] - 0.5f) < 1e-6f);  // 100 / 400 + 0.5
    const double p[3] = {200, 50, 999};
    double uv[2];
    volumeMaskUv(m, p, 0, uv);
    CHECK(std::fabs(uv[0] - (0.5 + 0.75)) < 1e-6 && std::fabs(uv[1] - (-0.25 + 0.5)) < 1e-6);
    // Eight seconds at 50 units a second: one whole repeat of 400, the same place again.
    volumeMaskTerms(m, 8, t);
    CHECK(std::fabs(t[2] - 0.75f) < 1e-5f);
    volumeMaskTerms(m, 2, t);
    CHECK(std::fabs(t[2] - 0.0f) < 1e-5f || std::fabs(t[2] - 1.0f) < 1e-5f);  // a quarter repeat further: 1.0 = 0.0
    // Along x the picture lies on y and z; along y on x and z.
    m.axis = 0;
    volumeMaskUv(m, p, 0, uv);
    CHECK(std::fabs(uv[0] - (50.0 / 400 + 0.75)) < 1e-6 && std::fabs(uv[1] - (-999.0 / 200 + 0.5)) < 1e-6);
    m.axis = 1;
    volumeMaskUv(m, p, 0, uv);
    CHECK(std::fabs(uv[0] - (200.0 / 400 + 0.75)) < 1e-6 && std::fabs(uv[1] - (-999.0 / 200 + 0.5)) < 1e-6);
}

TEST(volume_mask_build) {
    // A box with two masks: one picture from the map's own folder, one that does not exist. The build writes the
    // found mask into the box and its picture into the extraction, and says what it left out.
    const fs::path dir = fs::temp_directory_path() / "iw8r_volume_mask_build";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "map" / kVolumeMaskFolder);
    {
        // A 4 x 4 grey TGA (uncompressed, type 3).
        std::ofstream f(dir / "map" / kVolumeMaskFolder / "mist.tga", std::ios::binary);
        const unsigned char header[18] = {0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 4, 0, 8, 0};
        f.write(reinterpret_cast<const char*>(header), 18);
        for (int i = 0; i < 16; ++i) f.put(char(i * 16));
    }
    const std::string text =
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n{\n"
        " ( 256 256 -16 ) ( -256 256 -16 ) ( -256 -256 -16 ) custom/a 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        " ( -256 -256 0 ) ( -256 256 0 ) ( 256 256 0 ) custom/a 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        " ( -256 -256 0 ) ( 256 -256 0 ) ( 256 -256 -16 ) custom/a 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        " ( 256 256 0 ) ( -256 256 0 ) ( -256 256 -16 ) custom/a 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        " ( 256 -256 0 ) ( 256 256 0 ) ( 256 256 -16 ) custom/a 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        " ( -256 256 0 ) ( -256 -256 0 ) ( -256 -256 -16 ) custom/a 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        "}\n}\n{\n\"classname\" \"mp_dm_spawn\"\n\"origin\" \"0 0 16\"\n}\n"
        "{\n\"classname\" \"iw8r_volume\"\n\"origin\" \"0 0 64\"\n\"mask\" \"mist z 300 300 25 0\"\n\"mask2\" \"nowhere 100\"\n\"mask3\" \"mist 0\"\n}\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    CompileOptions opt;
    opt.mapId = "mp_vmask";
    opt.mapDir = (dir / "map").string();
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, (dir / "out").string(), opt, rep, &err));
    std::string world;
    {
        std::ifstream f(dir / "out" / "extracted" / "maps" / "mp" / "mp_vmask.d3dbsp.replay-world.json", std::ios::binary);
        world.assign(std::istreambuf_iterator<char>(f), {});
    }
    CHECK(world.find("\"masks\":[{\"type\":2,\"axis\":2,\"image\":\"iw8r_vmask_mist\",\"scale\":[300,300],\"offset\":[0,0],\"scroll\":[25,0]}]") != std::string::npos);
    CHECK(world.find("nowhere") == std::string::npos);
    CHECK(fs::is_regular_file(dir / "out" / "extracted" / "images" / "iw8r_vmask_mist.dds"));
    bool told = false, missing = false, bad = false;
    for (const std::string& w : rep.warnings) {
        told = told || w.find("with 1 mask(s) of 1 picture(s)") != std::string::npos;
        missing = missing || w.find("mask picture 'nowhere' not found") != std::string::npos;
        bad = bad || (w.find("mask3 'mist 0' not used") != std::string::npos);
    }
    CHECK(told && missing && bad);
    fs::remove_all(dir, ec);
}
