import io

# GUI test: the haze controls of the Atmosphere dialog, the sky brightness of the Sun & Sky dialog, and both in the
# web forms.

ROOT = 'E:/IW8-Radiant/'
P = 'src/qtapp/qt_gui_test.cpp'
s = io.open(ROOT + P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (s.count(a), a[:70])
    s = s.replace(a, b)


rep('''        r.check(applyAtmosphere(ctx, Atmosphere{}) && !currentAtmosphere(ctx).fog && !currentAtmosphere(ctx).film,
                "an empty atmosphere removes both keys");''', '''        {
            // The haze (research 126): the dialog's preset list and amount write worldspawn iw8r_haze; a preset turns the
            // baked fog's group off; "None" removes the key.
            AtmosphereDialog dlg(currentAtmosphere(ctx), &w);
            auto* preset = dlg.findChild<QComboBox*>("atmoHazePreset");
            auto* amount = dlg.findChild<QDoubleSpinBox*>("atmoHazeAmount");
            auto* fogBox = dlg.findChild<QGroupBox*>("atmoFog");
            const bool have = preset && amount && fogBox && preset->count() == int(hazePresets().size()) + 1;
            r.check(have && !amount->isEnabled() && fogBox->isEnabled(), "the Atmosphere dialog lists the haze presets, none chosen at first");
            if (have) {
                preset->setCurrentIndex(preset->findData("desert"));
                amount->setValue(0.7);
                r.check(amount->isEnabled() && !fogBox->isEnabled() && applyAtmosphere(ctx, dlg.value()) && ctx.doc.worldspawn()->value(kHazeKey) &&
                            *ctx.doc.worldspawn()->value(kHazeKey) == "desert 0.7",
                        "a haze preset and amount write worldspawn iw8r_haze and turn the baked fog's group off");
                AtmosphereDialog again(currentAtmosphere(ctx), &w);
                auto* p2 = again.findChild<QComboBox*>("atmoHazePreset");
                r.check(p2 && p2->currentData().toString() == "desert" && again.value().haze && std::fabs(again.value().haze->amount - 0.7) < 1e-9,
                        "the dialog opens with the map's haze");
                if (p2) p2->setCurrentIndex(0);
                r.check(applyAtmosphere(ctx, again.value()) && !ctx.doc.worldspawn()->value(kHazeKey), "the preset None removes the haze key");
            }
        }
        {
            // Sun & Sky: the sky brightness writes worldspawn iw8r_sky_brightness, and 1 removes the key.
            SunSky sun = currentSunSky(ctx);
            const double was = sun.skyBrightness;
            sun.skyBrightness = 2.5;
            const bool set = applySunSky(ctx, sun) && ctx.doc.worldspawn()->value(kSkyBrightnessKey) &&
                             *ctx.doc.worldspawn()->value(kSkyBrightnessKey) == "2.5" && std::fabs(currentSunSky(ctx).skyBrightness - 2.5) < 1e-9;
            SunSkyDialog dlg(currentSunSky(ctx), {}, &w);
            auto* spin = dlg.findChild<QDoubleSpinBox*>("skyBrightness");
            const bool shown = spin && std::fabs(spin->value() - 2.5) < 1e-9;
            sun.skyBrightness = 1.0;
            const bool cleared = applySunSky(ctx, sun) && !ctx.doc.worldspawn()->value(kSkyBrightnessKey);
            r.check(was == 1.0 && set && shown && cleared, "Sun & Sky's sky brightness writes iw8r_sky_brightness, shows it, and 1 removes it");
        }
        r.check(applyAtmosphere(ctx, Atmosphere{}) && !currentAtmosphere(ctx).fog && !currentAtmosphere(ctx).film,
                "an empty atmosphere removes both keys");''')
rep('''                fweb.handle("POST", "/api/form", "{\\"id\\":\\"mapInfo\\",\\"values\\":{\\"iw8r_title\\":\\"Web Title\\",\\"iw8r_minimap_scale\\":\\"1.2\\"}}", true, {});''',
    '''                {
                    // The haze and the sky brightness from the browser, as the desktop dialogs write them.
                    const QJsonObject af = QJsonDocument::fromJson(fweb.handle("GET", "/api/form?id=atmosphere", {}, true, {}).body).object();
                    bool hazeField = false;
                    for (const auto f : af.value("fields").toArray())
                        hazeField |= f.toObject().value("key").toString() == "haze" && f.toObject().value("choices").toArray().size() == int(hazePresets().size()) + 1;
                    const auto okH = fweb.handle("POST", "/api/form", "{\\"id\\":\\"atmosphere\\",\\"values\\":{\\"haze\\":\\"dusk\\",\\"hazeAmount\\":1.5}}", true, {});
                    const QString hazeNow = key("iw8r_haze");
                    const auto badH = fweb.handle("POST", "/api/form", "{\\"id\\":\\"atmosphere\\",\\"values\\":{\\"haze\\":\\"soup\\"}}", true, {});
                    const bool hazeKept = key("iw8r_haze") == hazeNow;
                    const auto offH = fweb.handle("POST", "/api/form", "{\\"id\\":\\"atmosphere\\",\\"values\\":{\\"haze\\":\\"none\\"}}", true, {});
                    const bool hazeGone = key("iw8r_haze").isEmpty();
                    const auto okS = fweb.handle("POST", "/api/form", "{\\"id\\":\\"sunSky\\",\\"values\\":{\\"skyBrightness\\":3}}", true, {});
                    const QString skyNow = key("iw8r_sky_brightness");
                    const auto badS = fweb.handle("POST", "/api/form", "{\\"id\\":\\"sunSky\\",\\"values\\":{\\"skyBrightness\\":99}}", true, {});
                    fweb.handle("POST", "/api/form", "{\\"id\\":\\"sunSky\\",\\"values\\":{\\"skyBrightness\\":1}}", true, {});
                    r.check(hazeField && okH.status == 200 && hazeNow == "dusk 1.5" && badH.status == 400 && hazeKept && offH.status == 200 && hazeGone,
                            QString("the Atmosphere form sets a haze preset and amount, refuses an unknown one and removes it (%1)").arg(hazeNow));
                    r.check(okS.status == 200 && skyNow == "3" && badS.status == 400 && key("iw8r_sky_brightness").isEmpty(),
                            QString("the Sun & Sky form sets the sky brightness within its range (%1)").arg(skyNow));
                }
                fweb.handle("POST", "/api/form", "{\\"id\\":\\"mapInfo\\",\\"values\\":{\\"iw8r_title\\":\\"Web Title\\",\\"iw8r_minimap_scale\\":\\"1.2\\"}}", true, {});''')
io.open(ROOT + P, 'w', encoding='utf-8', newline='').write(s)
print('ok')
