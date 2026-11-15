import io

# 4.27 HUD fonts: a GUI test section draws a fonts.txt (a label and a glyph set in a Windows TrueType font) with the
# editor's font engine and checks the pictures, names, colour, alpha and the script helpers.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if s.count(a) == 0 and b in s:
        return
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''#include "stock_lightgrid.h"
''', '''#include "stock_lightgrid.h"
#include "hud_font_render.h"
''')
patch(T, '''    r.section("game lighting options (View > Game Lighting Options, plan 1.10)");''',
      '''    r.section("map HUD fonts (4.27: <map>.hud/fonts.txt drawn into the map's own HUD pictures)");
    {
        QString font;
        for (const char* f : {"C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/tahoma.ttf"})
            if (QFile::exists(f)) {
                font = f;
                break;
            }
        QTemporaryDir dir;
        if (font.isEmpty() || !dir.isValid()) {
            r.check(true, "map HUD fonts: no Windows TrueType font here, skipped");
        } else {
            QFile::copy(font, dir.filePath("Test Font.ttf"));
            QFile txt(dir.filePath("fonts.txt"));
            if (txt.open(QIODevice::WriteOnly)) txt.write("label | title | Test Font.ttf | 40 | #ffcc00 | Cinder Rail\\nglyphs | set | Test Font.ttf | 32 | #ffffff | AB1:\\n");
            txt.close();
            const HudFontRender fr = renderHudFonts(dir.path().toStdString());
            r.check(fr.warnings.empty(), QString("fonts.txt draws without warnings (%1)").arg(fr.warnings.empty() ? "" : QString::fromStdString(fr.warnings[0])));
            r.check(fr.images.size() == 5 && fr.labels.size() == 1 && fr.sets.size() == 1, QString("a label and 4 glyphs: %1 pictures").arg(fr.images.size()));
            if (fr.images.size() == 5) {
                const auto& label = fr.images[0];
                r.check(label.name == "hud_title" && label.width > 3 * label.height && label.height >= 40,
                        QString("the label picture hud_title is a line of text (%1 x %2)").arg(label.width).arg(label.height));
                size_t clear = 0, solid = 0, gold = 0;
                for (size_t k = 0; k + 3 < label.rgba.size(); k += 4) {
                    const uint8_t a = label.rgba[k + 3];
                    clear += a == 0, solid += a > 200;
                    gold += a > 200 && label.rgba[k] > 230 && label.rgba[k + 1] > 180 && label.rgba[k + 1] < 230 && label.rgba[k + 2] < 40;
                }
                r.check(clear > 0 && solid > 0 && gold * 10 >= solid * 9,
                        QString("the label's text is opaque #ffcc00 on transparent (%1 solid, %2 of them gold)").arg(solid).arg(gold));
                r.check(fr.images[1].name == "hud_set_ua" && fr.images[2].name == "hud_set_ub" && fr.images[3].name == "hud_set_1" &&
                            fr.images[4].name == "hud_set_c58",
                        "glyph pictures are named hud_<set>_<key> (A -> ua, : -> c58)");
                bool advances = true;
                for (const auto& g : fr.sets[0].glyphs) advances = advances && g.advance > 0 && g.width > 0 && g.height == fr.sets[0].pixelHeight;
                r.check(advances, "every glyph has an advance and the set's height");
            }
            r.check(fr.script.find("iw8r_font_init()") != std::string::npos && fr.script.find("precacheshader(\\"hud_set_c58\\");") != std::string::npos &&
                        fr.script.find("iw8r_font_label(name, x, y, height)") != std::string::npos,
                    "the map script gets the helpers, with every picture precached");
        }
    }

    r.section("game lighting options (View > Game Lighting Options, plan 1.10)");''')
print('ok')
