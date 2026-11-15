import io

# 4.27 HUD fonts (owner 2026-11-15): the build takes pictures drawn by the editor from the .hud folder's fonts.txt
# (CompileIw8Options::hudFontImages, RGBA) like the folder's own pictures, and the map script gets the helper
# functions (hudFontScript) with iw8r_font_init() called at the start of main().

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if s.count(a) == 0 and b in s:
        return
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


HC = 'src/core/hud_fonts.cpp'
patch(HC, '''#include <algorithm>
#include <cmath>
#include <cstdio>''', '''#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>''')

HH = 'src/core/hud_images.h'
patch(HH, '''bool encodeHudImageDds(const std::string& picturePath, std::vector<uint8_t>& dds, std::string* err);''',
      '''bool encodeHudImageDds(const std::string& picturePath, std::vector<uint8_t>& dds, std::string* err);
// The same for a picture in memory (RGBA, w x h): the map's font pictures (hud_fonts.h).
bool encodeHudRgbaDds(int w, int h, const std::vector<uint8_t>& rgba, std::vector<uint8_t>& dds, std::string* err);''')
HI = 'src/core/hud_images.cpp'
patch(HI, '''bool encodeHudImageDds(const std::string& picturePath, std::vector<uint8_t>& dds, std::string* err) {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    if (!decodeImageFile(picturePath, w, h, rgba, err)) return false;
    if (w <= 0''', '''bool encodeHudImageDds(const std::string& picturePath, std::vector<uint8_t>& dds, std::string* err) {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    if (!decodeImageFile(picturePath, w, h, rgba, err)) return false;
    return encodeHudRgbaDds(w, h, rgba, dds, err);
}

bool encodeHudRgbaDds(int w, int h, const std::vector<uint8_t>& rgba, std::vector<uint8_t>& dds, std::string* err) {
    if (w <= 0''')

CH = 'src/core/compile_iw8.h'
patch(CH, '''    std::string hudImageDir;''', '''    std::string hudImageDir;
    // 4.27 HUD fonts (hud_fonts.h): the pictures the editor drew from "<map>.hud/fonts.txt" (labels and glyphs, RGBA),
    // built like the folder's pictures, and the map script's helpers (iw8r_font_init / _text / _clear / _label).
    struct HudRgbaImage {
        std::string name;  // hud_...
        int width = 0, height = 0;
        std::vector<uint8_t> rgba;
    };
    std::vector<HudRgbaImage> hudFontImages;
    std::string hudFontScript;''')

CC = 'src/core/compile_iw8.cpp'
patch(CC, '''        for (const std::string& w : hudWarnings) rep.warnings.push_back(w);
        std::sort(rep.hudImages.begin(), rep.hudImages.end());  // searched below (the script check)''',
      '''        for (const std::string& w : hudWarnings) rep.warnings.push_back(w);
        // The map's fonts as pictures (labels and glyphs drawn by the editor).
        for (const auto& fi : opt.hudFontImages) {
            std::vector<uint8_t> dds;
            std::string imageErr;
            if (encodeHudRgbaDds(fi.width, fi.height, fi.rgba, dds, &imageErr)) {
                customFiles.push_back({root / "extracted" / "images" / (fi.name + ".dds"), std::string(dds.begin(), dds.end())});
                rep.hudImages.push_back(fi.name);
            } else {
                rep.warnings.push_back("HUD font picture " + fi.name + " not used: " + imageErr);
            }
        }
        if (!opt.hudFontImages.empty())
            rep.warnings.push_back(std::to_string(opt.hudFontImages.size()) +
                                   " HUD font picture(s) from fonts.txt; the map script can call iw8r_font_text / iw8r_font_label");
        std::sort(rep.hudImages.begin(), rep.hudImages.end());  // searched below (the script check)''')
patch(CC, '''        MapScriptExtras extras;
        extras.stockDoors = stockDoorPlaced;''', '''        MapScriptExtras extras;
        extras.stockDoors = stockDoorPlaced;
        extras.fontScript = opt.hudFontScript;''')

MH = 'src/core/map_script.h'
patch(MH, '''    std::string userScript;
    // Hot update (roadmap 7.4)''', '''    std::string userScript;
    // The map's HUD fonts (hud_fonts.h hudFontScriptGsc): appended, with iw8r_font_init() at the start of main().
    std::string fontScript;
    // Hot update (roadmap 7.4)''')
MC = 'src/core/map_script.cpp'
patch(MC, '''    if (!extras.userScript.empty()) s += "    level thread user_main();\\n";
    if (extras.hotUpdate)''', '''    if (!extras.fontScript.empty()) s += "    iw8r_font_init();\\n";  // before the map's own script uses the fonts
    if (!extras.userScript.empty()) s += "    level thread user_main();\\n";
    if (extras.hotUpdate)''')
patch(MC, '''    if (!extras.userScript.empty()) s += "\\n// ---- map script (worldspawn iw8r_script) ----\\n" + extras.userScript + "\\n";''',
      '''    if (!extras.userScript.empty()) s += "\\n// ---- map script (worldspawn iw8r_script) ----\\n" + extras.userScript + "\\n";
    if (!extras.fontScript.empty()) s += extras.fontScript;''')
print('ok')
