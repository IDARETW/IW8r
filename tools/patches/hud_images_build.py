import io

# ROADMAP 4.27: a map's own HUD images. Pictures in "<map name>.hud" beside the map are written into the extraction
# as images/hud_<name>.dds (core hud_images); the converter carries every hud_* image with a 2D material of its name
# (tools/patches/zt_hud_images.py), so the map script can draw it with setshader.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, n=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/core/compile_iw8.h'
patch(H, '''    std::string loadscreenImagePath;''', '''    std::string loadscreenImagePath;
    // The map's own HUD images (4.27, core hud_images.h): the folder "<map name>.hud". Each picture becomes
    // extracted/images/hud_<name>.dds, which the converter carries with a 2D material of that name. Empty or not a
    // folder: none.
    std::string hudImageDir;''')
patch(H, '''    bool loadscreen = false;      // extracted/images/loadscreen_<id>.dds written (1.17)''',
      '''    bool loadscreen = false;      // extracted/images/loadscreen_<id>.dds written (1.17)
    std::vector<std::string> hudImages;  // the hud_* images written (4.27), sorted''')
C = 'src/core/compile_iw8.cpp'
patch(C, '''    for (const auto& [name, text] : physicPresets) customFiles.push_back({root / "extracted" / "physic" / name, text});''',
      '''    // 4.27: the map's own HUD images (alpha kept: icons are cut out).
    {
        std::vector<std::string> hudWarnings;
        for (const HudImage& h : listHudImages(opt.hudImageDir, &hudWarnings)) {
            std::vector<uint8_t> dds;
            std::string imageErr;
            if (encodeDdsFromImage(h.path, false, true, dds, &imageErr)) {
                customFiles.push_back({root / "extracted" / "images" / (h.name + ".dds"), std::string(dds.begin(), dds.end())});
                rep.hudImages.push_back(h.name);
            } else {
                rep.warnings.push_back("HUD image " + h.path + " not used: " + imageErr);
            }
        }
        for (const std::string& w : hudWarnings) rep.warnings.push_back(w);
        if (!rep.hudImages.empty()) {
            std::string names;
            for (const std::string& n : rep.hudImages) names += (names.empty() ? "" : ", ") + n;
            rep.warnings.push_back(std::to_string(rep.hudImages.size()) + " HUD image(s) for the map script (setshader(\\"<name>\\", width, height)): " + names);
        }
    }
    for (const auto& [name, text] : physicPresets) customFiles.push_back({root / "extracted" / "physic" / name, text});''')
patch(C, '''                if (!extras.userScript.empty()) warn.push_back("map script includes " + p.filename().string() + " (user_main runs after the load script)");''',
      '''                if (!extras.userScript.empty()) warn.push_back("map script includes " + p.filename().string() + " (user_main runs after the load script)");
                // The script's hud_* names against the map's own HUD images (only when the map has any): a name that
                // is neither is drawn as the game's missing material, unless it is a stock one.
                if (!rep.hudImages.empty())
                    for (const std::string& n : hudNamesInScript(extras.userScript))
                        if (!std::binary_search(rep.hudImages.begin(), rep.hudImages.end(), n))
                            warn.push_back("the map script draws " + n + ", which is not one of this map's HUD images (fine if it is a stock material)");''')
s = io.open(ROOT + C, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if '#include "hud_images.h"' not in s:
    a = '#include "compile_iw8.h"' + nl
    assert s.count(a) == 1
    s = s.replace(a, a + '#include "hud_images.h"' + nl, 1)
    io.open(ROOT + C, 'w', encoding='utf-8', newline='').write(s)
P = 'src/qtapp/compile_panel.cpp'
patch(P, '''    if (const QString ls = loadscreenImageFile(); !ls.isEmpty() && QFileInfo::exists(ls)) opt.loadscreenImagePath = ls.toStdString();''',
      '''    if (const QString ls = loadscreenImageFile(); !ls.isEmpty() && QFileInfo::exists(ls)) opt.loadscreenImagePath = ls.toStdString();
    // 4.27: the pictures in "<map name>.hud" beside the map become the map's own HUD images.
    opt.hudImageDir = hudImageFolderFor(ctx_->doc.path());''')
s = io.open(ROOT + P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if '#include "hud_images.h"' not in s:
    i = s.index('#include "editor_log.h"')
    s = s[:i] + '#include "hud_images.h"' + nl + s[i:]
    io.open(ROOT + P, 'w', encoding='utf-8', newline='').write(s)
print('ok')
