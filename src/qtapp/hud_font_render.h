// Draws a map's HUD fonts (core hud_fonts.h: "<map>.hud/fonts.txt") into pictures with Qt's font engine: one picture
// per label and per glyph, in the entry's colour on transparent, plus the map script's helper functions.
#pragma once

#include "compile_iw8.h"
#include "hud_fonts.h"

#include <string>
#include <vector>

namespace iw8r::qt {

struct HudFontRender {
    std::vector<CompileOptions::HudRgbaImage> images;  // hud_<label>, hud_<set>_<key>
    std::vector<HudGlyphSet> sets;
    std::vector<HudLabelSize> labels;
    std::string script;  // hudFontScriptGsc of the above ("" when there is nothing)
    std::vector<std::string> warnings;
};

// Reads <hudFolder>/fonts.txt and draws it. Must run on the GUI thread (it registers the font files with Qt).
HudFontRender renderHudFonts(const std::string& hudFolder);

}  // namespace iw8r::qt
