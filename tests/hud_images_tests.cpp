// A map's own HUD images (core hud_images, ROADMAP 4.27).
#include "hud_images.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "overview_image.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

void writePicture(const fs::path& p) {
    std::vector<uint8_t> rgba(8 * 8 * 4, 200);
    std::string err;
    CHECK(writePng(p.string(), 8, 8, rgba, &err));
}

}  // namespace

TEST(hud_images_names) {
    CHECK(hudImageName("flag.png") == "hud_flag");
    CHECK(hudImageName("Flag Icon.PNG") == "hud_flag_icon");
    CHECK(hudImageName("C:/maps/my map.hud/Zone-A (2).tga") == "hud_zone_a_2");
    CHECK(hudImageName("hud_flag.png") == "hud_flag");    // the prefix is not doubled
    CHECK(hudImageName("HUD_Flag.png") == "hud_flag");
    CHECK(hudImageName("__a__b__.png") == "hud_a_b");
    CHECK(hudImageName("...png").empty() && hudImageName("---.png").empty() && hudImageName("hud_.png").empty());
    CHECK(hudImageName(std::string(200, 'x') + ".png").size() == 4 + 48);
    CHECK(hudImageName("noextension") == "hud_noextension");
}

TEST(hud_images_folder_listing) {
    const fs::path dir = fs::path(iw8r::test::tmpDir()) / "hud_images_list" / "arena.hud";
    fs::remove_all(dir.parent_path());
    fs::create_directories(dir / "sub");
    writePicture(dir / "b_flag.png");
    writePicture(dir / "A Zone.png");
    writePicture(dir / "a_zone.png");   // the same name as "A Zone.png"
    writePicture(dir / "---.png");      // no usable name
    writePicture(dir / "sub" / "deep.png");  // sub-folders are not read
    std::ofstream(dir / "notes.txt") << "not a picture";
    std::vector<std::string> warnings;
    const auto list = listHudImages(dir.string(), &warnings);
    CHECK(list.size() == 2);
    if (list.size() == 2) {
        CHECK(list[0].name == "hud_a_zone" && list[1].name == "hud_b_flag");  // sorted by file name
        CHECK(fs::path(list[1].path).filename() == "b_flag.png");
    }
    CHECK(warnings.size() == 2);  // the duplicate and the nameless one
    // No folder: nothing, and nothing to say.
    warnings.clear();
    CHECK(listHudImages((dir.parent_path() / "missing.hud").string(), &warnings).empty() && warnings.empty());
    CHECK(listHudImages("", &warnings).empty());
    // The folder of a map file.
    CHECK(fs::path(hudImageFolderFor((dir.parent_path() / "arena.map").string())) == dir);
    CHECK(hudImageFolderFor("").empty());
}

TEST(hud_images_limit) {
    const fs::path dir = fs::path(iw8r::test::tmpDir()) / "hud_images_limit.hud";
    fs::remove_all(dir);
    fs::create_directories(dir);
    for (size_t i = 0; i < kMaxHudImages + 3; ++i) writePicture(dir / ("icon" + std::to_string(1000 + i) + ".png"));
    std::vector<std::string> warnings;
    CHECK(listHudImages(dir.string(), &warnings).size() == kMaxHudImages);
    CHECK(warnings.size() == 3);
}

TEST(hud_images_names_in_script) {
    const std::string script =
        "user_main() {\n"
        "  icon = newhudelem();\n"
        "  icon setshader( \"hud_flag\", 64, 64 );\n"
        "  icon SetShader(\"HUD_Zone_A\",32,32);\n"
        "  precacheshader(\"hud_flag\");\n"
        "  icon setshader(\"white\", 8, 8);\n"        // not a hud_ name
        "  icon setshader(name, 8, 8);\n"             // not a literal
        "  text = \"setshader hud_fake\";\n"          // not a call
        "}\n";
    const auto names = hudNamesInScript(script);
    CHECK(names.size() == 2);
    if (names.size() == 2) CHECK(names[0] == "hud_flag" && names[1] == "hud_zone_a");
    CHECK(hudNamesInScript("").empty());
}

TEST(hud_images_dds_is_square_dxt5) {
    // A 40 x 20 picture becomes a 64 x 64 DXT5 with 7 mip levels; a missing or broken file fails with a reason.
    const fs::path dir = fs::path(iw8r::test::tmpDir()) / "hud_images_dds";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::vector<uint8_t> rgba(40 * 20 * 4, 0);
    for (size_t i = 0; i < rgba.size(); i += 4) rgba[i] = 255, rgba[i + 3] = uint8_t(i / 4 % 40 < 20 ? 255 : 0);
    std::string err;
    CHECK(writePng((dir / "wide.png").string(), 40, 20, rgba, &err));
    std::vector<uint8_t> dds;
    CHECK(encodeHudImageDds((dir / "wide.png").string(), dds, &err));
    CHECK(dds.size() > 128 && std::string(dds.begin(), dds.begin() + 4) == "DDS ");
    if (dds.size() > 128) {
        CHECK(dds[12] == 64 && dds[16] == 64 && dds[28] == 7);  // height, width, mip levels
        CHECK(std::string(dds.begin() + 84, dds.begin() + 88) == "DXT5");
        size_t body = 0;
        for (int s = 64; s >= 1; s /= 2) body += size_t(std::max(1, s / 4)) * size_t(std::max(1, s / 4)) * 16;
        CHECK(dds.size() == 128 + body);
    }
    std::ofstream(dir / "broken.png") << "not a png";
    CHECK(!encodeHudImageDds((dir / "broken.png").string(), dds, &err) && !err.empty());
    CHECK(!encodeHudImageDds((dir / "missing.png").string(), dds, &err));
}
