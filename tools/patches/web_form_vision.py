import io

# Plan 3.3, item 1 (Vision): the Atmosphere web form also holds the map's vision overrides (worldspawn iw8r_vision,
# "name=value; name=x y z"), checked by the parser the build uses before anything is written.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, n=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    write(p, s.replace(a, b))


W = 'src/qtapp/main_window_web.cpp'
patch(W, '''                  text("lut", "Colour grading LUT (.cube file; empty = none)", qs(a.lut))};''',
      '''                  text("lut", "Colour grading LUT (.cube file; empty = none)", qs(a.lut)),
                  text("vision", "Vision overrides (empty = none)", qs(a.vision),
                       "name=value; name=x y z, with the game's vision field names (tonemapExposure, ...). The Vision Editor of the "
                       "editor lists all 218 fields")};''')
patch(W, '''        a.fog = fogOn ? std::optional<ExpFog>(fog) : std::nullopt;''', '''        if (values.contains("vision")) {
            const std::string vision = values.value("vision").toString().trimmed().toStdString();
            std::string verr;
            if (vision.find('"') != std::string::npos) return no("the vision overrides have no quotes");
            if (!vision.empty() && visionOverridesJson(vision, &verr).empty()) return no("vision overrides: " + qs(verr.empty() ? std::string("not name=value; name=value") : verr));
            a.vision = vision;
        }
        a.fog = fogOn ? std::optional<ExpFog>(fog) : std::nullopt;''')

T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''                fweb.handle("POST", "/api/form", "{\\"id\\":\\"mapInfo\\",\\"values\\":{\\"iw8r_title\\":\\"Web Title\\",\\"iw8r_minimap_scale\\":\\"1.2\\"}}", true, {});''',
      '''                {
                    // The vision overrides of the Atmosphere form: the build's own parser decides what is accepted.
                    const QString visionWas = key("iw8r_vision");
                    const auto okV = fweb.handle("POST", "/api/form", "{\\"id\\":\\"atmosphere\\",\\"values\\":{\\"vision\\":\\"tonemapExposure=0.5\\"}}", true, {});
                    const QString visionNow = key("iw8r_vision");
                    const auto badV = fweb.handle("POST", "/api/form", "{\\"id\\":\\"atmosphere\\",\\"values\\":{\\"vision\\":\\"no equals sign here\\"}}", true, {});
                    const bool kept = key("iw8r_vision") == visionNow;
                    ctx.doc.undo();
                    ctx.documentEdited();
                    r.check(okV.status == 200 && visionNow == "tonemapExposure=0.5" && badV.status == 400 && kept && key("iw8r_vision") == visionWas,
                            "the Atmosphere form sets the vision overrides and refuses text the build would not read");
                }
                fweb.handle("POST", "/api/form", "{\\"id\\":\\"mapInfo\\",\\"values\\":{\\"iw8r_title\\":\\"Web Title\\",\\"iw8r_minimap_scale\\":\\"1.2\\"}}", true, {});''')
print('ok')
