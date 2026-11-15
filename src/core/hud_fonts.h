// A map's own fonts for its HUD (ROADMAP 4.27; owner 2026-11-15: "custom fonts, within the scope of the map data").
// The game picks HUD fonts from a fixed list (bigfixed, smallfixed, objective, big, hudbig, hudsmall: the exe's table;
// the TTF assets fonts/main_regular.ttf, fonts/main_bold.ttf, ... live in code_post_gfx), so a map cannot add a font
// name, and a font carried under a stock name would change every menu while the map is loaded. A map's own font
// therefore travels as pictures: the build draws text in the font into the map's own HUD images (hud_images.h), which
// go into the map zone with a 2D material each, and the map script shows them.
//
// "<map name>.hud/fonts.txt", one entry per line (fields separated by '|', '#' starts a comment):
//     label  | <name> | <font file> | <pixel height> | <#rrggbb> | <text>
//     glyphs | <name> | <font file> | <pixel height> | <#rrggbb> | <characters>
// A label is one picture of its text, hud_<name>: setshader("hud_<name>", w, h), or iw8r_font_label("<name>", x, y,
// height). A glyph set is one picture per character, hud_<name>_<key>; the generated script helper
// iw8r_font_text("<name>", text, x, y, height) writes any text made of those characters (spaces advance), and
// iw8r_font_clear(elems) removes it. Font files are TTF / OTF in the .hud folder (or a path from it).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace iw8r {

struct HudFontEntry {
    bool glyphs = false;      // a glyph set (else a label)
    std::string name;         // [a-z0-9_], 1..24 characters
    std::string fontFile;     // absolute path
    int pixelHeight = 48;     // 8..256
    uint8_t rgb[3] = {255, 255, 255};
    std::string text;         // the label's text, or the set's characters (each once, no space)
    int line = 0;             // in fonts.txt
};

inline constexpr size_t kMaxHudFontImages = 192;  // labels + glyphs of one map

// The fonts.txt path of a .hud folder ("" for an empty folder).
std::string hudFontsFileFor(const std::string& hudFolder);

// Parses fonts.txt text. Font paths are taken relative to `folder`. Bad lines are left out with a warning each
// (unknown kind, bad name, a name used twice, size or colour out of range, a font file that does not exist, empty
// text); a glyph set drops repeated characters and spaces.
std::vector<HudFontEntry> parseHudFonts(const std::string& text, const std::string& folder, std::vector<std::string>* warnings = nullptr);
// readHudFonts: parseHudFonts of <folder>/fonts.txt (none and no warning when the file does not exist).
std::vector<HudFontEntry> readHudFonts(const std::string& hudFolder, std::vector<std::string>* warnings = nullptr);

// The image key of a glyph: a-z and 0-9 as themselves, A-Z as "u" + the lower letter, anything else "c" + its byte
// value ("!" -> "c33"); "" for a space.
std::string hudGlyphKey(unsigned char c);
// "hud_<set>_<key>".
std::string hudGlyphImageName(const std::string& set, unsigned char c);

// A drawn glyph set (sizes in pixels at the set's pixel height) and a drawn label, for the script helper.
struct HudGlyphSet {
    std::string name;
    int pixelHeight = 48;
    double spaceAdvance = 12;
    struct Glyph {
        unsigned char c = 0;
        int width = 0, height = 0;  // the picture
        double advance = 0;         // to the next glyph's left edge
    };
    std::vector<Glyph> glyphs;
};
struct HudLabelSize {
    std::string name;
    int width = 0, height = 0;
};

// The GSC of the helpers for these sets and labels: iw8r_font_init() (tables), iw8r_font_text(set, text, x, y,
// height) -> array of HUD elements, iw8r_font_clear(elems), iw8r_font_label(name, x, y, height) -> one element.
// Empty when there is nothing.
std::string hudFontScriptGsc(const std::vector<HudGlyphSet>& sets, const std::vector<HudLabelSize>& labels);

}  // namespace iw8r
