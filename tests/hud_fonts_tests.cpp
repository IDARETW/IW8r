// A map's own HUD fonts (src/core/hud_fonts.*): fonts.txt, glyph names, the script helpers, the map script hook.
#include "hud_fonts.h"
#include "map_script.h"
#include "test_harness.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace iw8r;

namespace {

// A folder with a (fake) font file: parseHudFonts only checks that the file exists.
std::string fontFolder() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "iw8r_hud_fonts_test";
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "Display Font.ttf") << "x";
    return dir.string();
}

}  // namespace

TEST(hud_fonts_parse_labels_and_glyph_sets) {
    const std::string dir = fontFolder();
    std::vector<std::string> warn;
    const std::string text =
        "# the map's fonts\n"
        "label  | title  | Display Font.ttf | 64 | #ffcc00 | Welcome | to Cinder Rail\n"
        "glyphs | digits | Display Font.ttf | 48 | #FFFFFF | 0123456789 0:\n"
        "\n"
        "banner | x | Display Font.ttf | 48 | #ffffff | x\n"         // unknown kind
        "label | Bad Name | Display Font.ttf | 48 | #ffffff | x\n"    // name
        "label | title | Display Font.ttf | 48 | #ffffff | again\n"   // twice
        "label | nofont | Missing.ttf | 48 | #ffffff | x\n"           // font file
        "label | small | Display Font.ttf | 4 | #ffffff | x\n"        // size
        "label | tint | Display Font.ttf | 48 | red | x\n"            // colour
        "glyphs | blank | Display Font.ttf | 48 | #ffffff |    \n"    // nothing
        "label | short | Display Font.ttf | 48\n";                     // fields
    const auto e = parseHudFonts(text, dir, &warn);
    CHECK(e.size() == 2);
    CHECK(warn.size() == 8);
    if (e.size() == 2) {
        CHECK(!e[0].glyphs && e[0].name == "title" && e[0].pixelHeight == 64 && e[0].text == "Welcome | to Cinder Rail");
        CHECK(e[0].rgb[0] == 0xff && e[0].rgb[1] == 0xcc && e[0].rgb[2] == 0x00 && e[0].line == 2);
        CHECK(std::filesystem::path(e[0].fontFile).filename() == "Display Font.ttf");
        CHECK(e[1].glyphs && e[1].name == "digits" && e[1].text == "0123456789:");  // repeats and spaces dropped
    }
    for (const auto& w : warn) CHECK(w.rfind("fonts.txt line ", 0) == 0);
    CHECK(readHudFonts(dir + "/no_such_folder").empty());
    CHECK(hudFontsFileFor("").empty());
    CHECK(std::filesystem::path(hudFontsFileFor(dir)).filename() == "fonts.txt");
}

TEST(hud_fonts_glyph_keys) {
    CHECK(hudGlyphKey('a') == "a" && hudGlyphKey('z') == "z" && hudGlyphKey('7') == "7");
    CHECK(hudGlyphKey('A') == "ua" && hudGlyphKey('Q') == "uq");
    CHECK(hudGlyphKey('!') == "c33" && hudGlyphKey(':') == "c58" && hudGlyphKey('"') == "c34");
    CHECK(hudGlyphKey(' ').empty());
    CHECK(hudGlyphImageName("digits", '3') == "hud_digits_3" && hudGlyphImageName("t", 'B') == "hud_t_ub");
}

TEST(hud_fonts_script_helpers) {
    CHECK(hudFontScriptGsc({}, {}).empty());
    HudGlyphSet set;
    set.name = "digits";
    set.pixelHeight = 58;
    set.spaceAdvance = 12.5;
    set.glyphs.push_back({'1', 30, 58, 27.0});
    set.glyphs.push_back({'"', 18, 58, 15.5});
    const std::string s = hudFontScriptGsc({set}, {{"title", 512, 74}});
    CHECK(s.find("iw8r_font_init()") != std::string::npos);
    CHECK(s.find("iw8r_font_text(set, text, x, y, height)") != std::string::npos);
    CHECK(s.find("iw8r_font_clear(elems)") != std::string::npos);
    CHECK(s.find("iw8r_font_label(name, x, y, height)") != std::string::npos);
    CHECK(s.find("level.iw8r_font_h[\"digits\"] = 58;") != std::string::npos);
    CHECK(s.find("level.iw8r_font_space[\"digits\"] = 12.5;") != std::string::npos);
    CHECK(s.find("level.iw8r_font_chars[\"digits\"] = \"1\\\"\";") != std::string::npos);  // the quote escaped
    CHECK(s.find("k[1] = \"c34\";") != std::string::npos && s.find("adv[0] = 27;") != std::string::npos);
    CHECK(s.find("level.iw8r_label_w[\"title\"] = 512;") != std::string::npos);
    CHECK(s.find("e setshader(\"hud_\" + set + \"_\"") != std::string::npos);
    // Every picture precached by its literal name (the converter's level material table).
    CHECK(s.find("precacheshader(\"hud_digits_1\");") != std::string::npos && s.find("precacheshader(\"hud_digits_c34\");") != std::string::npos);
    CHECK(s.find("precacheshader(\"hud_title\");") != std::string::npos);
    CHECK(s.find("precacheshader(") < s.find("iw8r_font_text("));  // in iw8r_font_init, before anything waits
    // Balanced braces and parentheses (the converter compiles it with gsc-tool; the build proof runs that).
    int braces = 0, parens = 0;
    for (char c : s) braces += c == '{' ? 1 : c == '}' ? -1 : 0, parens += c == '(' ? 1 : c == ')' ? -1 : 0;
    CHECK(braces == 0 && parens == 0);
}

TEST(hud_fonts_map_script_calls_init) {
    MapScriptExtras extras;
    extras.fontScript = hudFontScriptGsc({}, {{"title", 100, 20}});
    extras.userScript = "user_main()\n{\n    iw8r_font_label(\"title\", 10, 10, 20);\n}\n";
    const std::string s = mapScriptGsc(std::nullopt, {}, {}, extras);
    const size_t init = s.find("    iw8r_font_init();"), user = s.find("level thread user_main();");
    CHECK(init != std::string::npos && user != std::string::npos && init < user);
    CHECK(s.find("iw8r_font_label(name, x, y, height)") != std::string::npos);
    MapScriptExtras none;
    CHECK(mapScriptGsc(std::nullopt, {}, {}, none).find("iw8r_font_init") == std::string::npos);
}
