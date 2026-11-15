// A map's own HUD fonts as pictures (hud_fonts.h).
#include "hud_fonts.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace iw8r {
namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

std::vector<std::string> splitBars(const std::string& line, size_t maxFields) {
    // The last field takes the rest of the line (a label's text may hold '|').
    std::vector<std::string> out;
    size_t at = 0;
    while (out.size() + 1 < maxFields) {
        const size_t bar = line.find('|', at);
        if (bar == std::string::npos) break;
        out.push_back(trim(line.substr(at, bar - at)));
        at = bar + 1;
    }
    out.push_back(trim(line.substr(at)));
    return out;
}

bool validName(const std::string& n) {
    if (n.empty() || n.size() > 24) return false;
    return std::all_of(n.begin(), n.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

bool parseColour(const std::string& s, uint8_t rgb[3]) {
    std::string h = s;
    if (!h.empty() && h[0] == '#') h = h.substr(1);
    if (h.size() != 6) return false;
    for (char c : h)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    const unsigned long v = std::stoul(h, nullptr, 16);
    rgb[0] = uint8_t(v >> 16), rgb[1] = uint8_t(v >> 8), rgb[2] = uint8_t(v);
    return true;
}

// A GSC string literal.
std::string gscString(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string num(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.1f", v);
    std::string s = b;
    if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.resize(s.size() - 2);
    return s;
}

}  // namespace

std::string hudFontsFileFor(const std::string& hudFolder) {
    if (hudFolder.empty()) return {};
    return (std::filesystem::path(hudFolder) / "fonts.txt").string();
}

std::vector<HudFontEntry> parseHudFonts(const std::string& text, const std::string& folder, std::vector<std::string>* warnings) {
    std::vector<HudFontEntry> out;
    std::set<std::string> names;
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    const auto warn = [&](const std::string& w) {
        if (warnings) warnings->push_back("fonts.txt line " + std::to_string(lineNo) + ": " + w);
    };
    while (std::getline(in, line)) {
        ++lineNo;
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        const std::vector<std::string> f = splitBars(t, 6);
        if (f.size() < 6) {
            warn("needs six fields: kind | name | font | pixel height | #rrggbb | text");
            continue;
        }
        HudFontEntry e;
        e.line = lineNo;
        if (f[0] == "label") e.glyphs = false;
        else if (f[0] == "glyphs") e.glyphs = true;
        else {
            warn("unknown kind \"" + f[0] + "\" (label or glyphs)");
            continue;
        }
        e.name = f[1];
        if (!validName(e.name)) {
            warn("the name \"" + e.name + "\" must be 1 to 24 of a-z, 0-9 and _");
            continue;
        }
        if (!names.insert(e.name).second) {
            warn("the name \"" + e.name + "\" is used twice");
            continue;
        }
        std::filesystem::path font(f[2]);
        if (font.is_relative() && !folder.empty()) font = std::filesystem::path(folder) / font;
        std::error_code ec;
        if (f[2].empty() || !std::filesystem::is_regular_file(font, ec)) {
            warn("font file \"" + f[2] + "\" not found");
            continue;
        }
        e.fontFile = font.string();
        char* end = nullptr;
        const long px = std::strtol(f[3].c_str(), &end, 10);
        if (!end || *end != '\0' || px < 8 || px > 256) {
            warn("pixel height \"" + f[3] + "\" must be 8 to 256");
            continue;
        }
        e.pixelHeight = int(px);
        if (!parseColour(f[4], e.rgb)) {
            warn("colour \"" + f[4] + "\" must be #rrggbb");
            continue;
        }
        if (e.glyphs) {
            std::string chars;
            for (const char c : f[5])
                if (c != ' ' && static_cast<unsigned char>(c) >= 32 && static_cast<unsigned char>(c) < 127 && chars.find(c) == std::string::npos)
                    chars += c;
            e.text = chars;
        } else {
            e.text = f[5];
        }
        if (e.text.empty()) {
            warn(std::string(e.glyphs ? "no characters" : "no text"));
            continue;
        }
        out.push_back(e);
    }
    return out;
}

std::vector<HudFontEntry> readHudFonts(const std::string& hudFolder, std::vector<std::string>* warnings) {
    const std::string path = hudFontsFileFor(hudFolder);
    if (path.empty()) return {};
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in) return {};
    std::ostringstream s;
    s << in.rdbuf();
    return parseHudFonts(s.str(), hudFolder, warnings);
}

std::string hudGlyphKey(unsigned char c) {
    if (c == ' ') return {};
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) return std::string(1, char(c));
    if (c >= 'A' && c <= 'Z') return std::string("u") + char(c - 'A' + 'a');
    return "c" + std::to_string(int(c));
}

std::string hudGlyphImageName(const std::string& set, unsigned char c) { return "hud_" + set + "_" + hudGlyphKey(c); }

std::string hudFontScriptGsc(const std::vector<HudGlyphSet>& sets, const std::vector<HudLabelSize>& labels) {
    if (sets.empty() && labels.empty()) return {};
    std::string s;
    s += "\n// ---- HUD fonts (the map's .hud/fonts.txt): text drawn in the map's own fonts as its own HUD images ----\n";
    s += "iw8r_font_init()\n{\n";
    for (const char* t : {"h", "space", "chars", "keys", "w", "gh", "adv"}) s += std::string("    level.iw8r_font_") + t + " = [];\n";
    s += "    level.iw8r_label_w = [];\n    level.iw8r_label_h = [];\n";
    // Every picture by its literal name, before any wait: the converter lists the names it reads in setshader /
    // precacheshader in the level's material table (the helpers below build the glyph names at run time).
    for (const HudLabelSize& l : labels) s += "    precacheshader(" + gscString("hud_" + l.name) + ");\n";
    for (const HudGlyphSet& g : sets)
        for (const auto& gl : g.glyphs) s += "    precacheshader(" + gscString(hudGlyphImageName(g.name, gl.c)) + ");\n";
    for (const HudGlyphSet& g : sets) {
        const std::string n = gscString(g.name);
        std::string chars;
        for (const auto& gl : g.glyphs) chars += char(gl.c);
        s += "    level.iw8r_font_h[" + n + "] = " + std::to_string(g.pixelHeight) + ";\n";
        s += "    level.iw8r_font_space[" + n + "] = " + num(g.spaceAdvance) + ";\n";
        s += "    level.iw8r_font_chars[" + n + "] = " + gscString(chars) + ";\n";
        s += "    k = [];\n    w = [];\n    gh = [];\n    adv = [];\n";
        for (size_t i = 0; i < g.glyphs.size(); ++i) {
            const auto& gl = g.glyphs[i];
            const std::string ix = std::to_string(i);
            s += "    k[" + ix + "] = " + gscString(hudGlyphKey(gl.c)) + ";\n";
            s += "    w[" + ix + "] = " + std::to_string(gl.width) + ";\n";
            s += "    gh[" + ix + "] = " + std::to_string(gl.height) + ";\n";
            s += "    adv[" + ix + "] = " + num(gl.advance) + ";\n";
        }
        s += "    level.iw8r_font_keys[" + n + "] = k;\n    level.iw8r_font_w[" + n + "] = w;\n";
        s += "    level.iw8r_font_gh[" + n + "] = gh;\n    level.iw8r_font_adv[" + n + "] = adv;\n";
    }
    for (const HudLabelSize& l : labels) {
        const std::string n = gscString(l.name);
        s += "    level.iw8r_label_w[" + n + "] = " + std::to_string(l.width) + ";\n";
        s += "    level.iw8r_label_h[" + n + "] = " + std::to_string(l.height) + ";\n";
    }
    s += "}\n\n";
    // Text from a glyph set: one HUD element per character, left to right from (x, y) (top left), `height` pixels tall.
    s += "iw8r_font_text(set, text, x, y, height)\n{\n"
         "    elems = [];\n"
         "    if (!isdefined(level.iw8r_font_h) || !isdefined(level.iw8r_font_h[set]))\n"
         "        return elems;\n"
         "    scale = float(height) / level.iw8r_font_h[set];\n"
         "    chars = level.iw8r_font_chars[set];\n"
         "    cx = x;\n"
         "    for (i = 0; i < text.size; i++)\n"
         "    {\n"
         "        c = getsubstr(text, i, i + 1);\n"
         "        if (c == \" \")\n"
         "        {\n"
         "            cx = cx + level.iw8r_font_space[set] * scale;\n"
         "            continue;\n"
         "        }\n"
         "        for (j = 0; j < chars.size; j++)\n"
         "        {\n"
         "            if (c != getsubstr(chars, j, j + 1))\n"
         "                continue;\n"
         "            e = newhudelem();\n"
         "            e.alignx = \"left\";\n"
         "            e.aligny = \"top\";\n"
         "            e.x = cx;\n"
         "            e.y = y;\n"
         "            e setshader(\"hud_\" + set + \"_\" + level.iw8r_font_keys[set][j], int(level.iw8r_font_w[set][j] * scale + 0.5), "
         "int(level.iw8r_font_gh[set][j] * scale + 0.5));\n"
         "            elems[elems.size] = e;\n"
         "            cx = cx + level.iw8r_font_adv[set][j] * scale;\n"
         "            break;\n"
         "        }\n"
         "    }\n"
         "    return elems;\n"
         "}\n\n";
    s += "iw8r_font_clear(elems)\n{\n"
         "    if (!isdefined(elems))\n"
         "        return;\n"
         "    for (i = 0; i < elems.size; i++)\n"
         "    {\n"
         "        e = elems[i];\n"
         "        if (isdefined(e))\n"
         "            e destroy();\n"
         "    }\n"
         "}\n\n";
    // A label: its picture at (x, y) (top left), `height` pixels tall, its width in proportion.
    s += "iw8r_font_label(name, x, y, height)\n{\n"
         "    if (!isdefined(level.iw8r_label_w) || !isdefined(level.iw8r_label_w[name]))\n"
         "        return undefined;\n"
         "    e = newhudelem();\n"
         "    e.alignx = \"left\";\n"
         "    e.aligny = \"top\";\n"
         "    e.x = x;\n"
         "    e.y = y;\n"
         "    e setshader(\"hud_\" + name, int(level.iw8r_label_w[name] * float(height) / level.iw8r_label_h[name] + 0.5), height);\n"
         "    return e;\n"
         "}\n";
    return s;
}

}  // namespace iw8r
