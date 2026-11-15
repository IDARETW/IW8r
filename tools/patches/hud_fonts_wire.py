import io

# 4.27 HUD fonts: the compile panel draws "<map>.hud/fonts.txt" (hud_font_render) into the build options; the build
# leaves out a font picture whose name a picture of the folder already has.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if s.count(a) == 0 and b in s:
        return
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


CP = 'src/qtapp/compile_panel.cpp'
patch(CP, '''#include "hud_images.h"
#include "map_strings.h"''', '''#include "hud_images.h"
#include "hud_font_render.h"
#include "map_strings.h"''')
patch(CP, '''    opt.hudImageDir = hudImageFolderFor(ctx_->doc.path());''', '''    opt.hudImageDir = hudImageFolderFor(ctx_->doc.path());
    // 4.27: "<map name>.hud/fonts.txt": text drawn in the map's own fonts into its HUD pictures, and the script helpers.
    {
        HudFontRender fonts = renderHudFonts(opt.hudImageDir);
        for (const std::string& w : fonts.warnings) append("warning: " + qs(w));
        if (!fonts.images.empty())
            append(QString("HUD fonts: %1 picture(s) from fonts.txt (%2 label(s), %3 glyph set(s))")
                       .arg(fonts.images.size()).arg(fonts.labels.size()).arg(fonts.sets.size()));
        opt.hudFontImages = std::move(fonts.images);
        opt.hudFontScript = std::move(fonts.script);
    }''')

CC = 'src/core/compile_iw8.cpp'
patch(CC, '''        for (const auto& fi : opt.hudFontImages) {
            std::vector<uint8_t> dds;''', '''        for (const auto& fi : opt.hudFontImages) {
            if (std::find(rep.hudImages.begin(), rep.hudImages.end(), fi.name) != rep.hudImages.end()) {
                rep.warnings.push_back("HUD font picture " + fi.name + " left out: a picture in the .hud folder has that name");
                continue;
            }
            std::vector<uint8_t> dds;''')
print('ok')
