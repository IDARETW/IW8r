import io

# Web forms: the haze (preset and amount) in the Atmosphere form and the sky brightness in the Sun & Sky form, as in
# the desktop dialogs.

ROOT = 'E:/IW8-Radiant/'
P = 'src/qtapp/main_window_web.cpp'
s = io.open(ROOT + P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (s.count(a), a[:70])
    s = s.replace(a, b)


rep('''                  text("sky", "Sky (a stock sky name; empty = none)", s.sky, "The build converts the sky's image when the game files have it",
                       stockSkyList())};''', '''                  text("sky", "Sky (a stock sky name; empty = none)", s.sky, "The build converts the sky's image when the game files have it",
                       stockSkyList()),
                  number("skyBrightness", "Sky brightness", s.skyBrightness, 0.1, 16, 0.25,
                         "1 keeps the sky's brightest clouds just below white; 2 or 3 is a bright day sky")};''')
rep('''        if (values.contains("color") && !colorOf(values.value("color"), s.color)) return no("the sun colour is not #rrggbb");
        if (values.contains("sky")) {''', '''        if (values.contains("color") && !colorOf(values.value("color"), s.color)) return no("the sun colour is not #rrggbb");
        if (!numberIn(values, "skyBrightness", 0.1, 16, s.skyBrightness, why)) return false;
        if (values.contains("sky")) {''')
rep('''        fields = {boolean("fog", "Fog", a.fog.has_value()),
                  number("fogStart", "Fog start (units)", fog.start, 0, 20000, 16),''', '''        QStringList hazeIds{"none"};
        QString hazeHelp = "The game's own fog, its blend into the sky and volumetric air (it replaces the fog below).";
        for (const HazePreset& p : hazePresets()) {
            hazeIds << QString::fromLatin1(p.id);
            hazeHelp += QString(" %1: %2.").arg(QString::fromLatin1(p.id), QString::fromLatin1(p.about));
        }
        fields = {choice("haze", "Haze", a.haze ? qs(a.haze->preset) : QString("none"), hazeIds, hazeHelp),
                  number("hazeAmount", "Haze amount", a.haze ? a.haze->amount : 1.0, 0.1, 4, 0.1, "1 = the preset as designed, 0.5 half as thick, 2 twice"),
                  boolean("fog", "Fog", a.fog.has_value()),
                  number("fogStart", "Fog start (units)", fog.start, 0, 20000, 16),''')
rep('''        a.fog = fogOn ? std::optional<ExpFog>(fog) : std::nullopt;
        a.film = filmOn ? std::optional<FilmGrade>(film) : std::nullopt;
        applyAtmosphere(ctx_, a);''', '''        // The haze: a preset name (or none) and its amount; a changed amount keeps the preset.
        {
            QString preset = a.haze ? qs(a.haze->preset) : QString("none");
            double amount = a.haze ? a.haze->amount : 1.0;
            if (values.contains("haze")) preset = values.value("haze").toString().trimmed();
            if (!numberIn(values, "hazeAmount", 0.1, 4, amount, why)) return false;
            if (preset.isEmpty() || preset == "none") {
                a.haze.reset();
            } else {
                const auto parsed = parseHaze(preset.toStdString());
                if (!parsed) return no("the haze is not one of the presets");
                a.haze = Haze{parsed->preset, amount};
            }
        }
        a.fog = fogOn ? std::optional<ExpFog>(fog) : std::nullopt;
        a.film = filmOn ? std::optional<FilmGrade>(film) : std::nullopt;
        applyAtmosphere(ctx_, a);''')
io.open(ROOT + P, 'w', encoding='utf-8', newline='').write(s)
print('ok')
