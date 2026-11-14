import io
import json
import subprocess
import sys

# Sky brightness for an own map (worldspawn iw8r_sky_brightness, default 1). A built map's sky is an 8-bit cube made
# from the stock HDR sky with an exposure that keeps its brightest clouds below white: most of a bright sky then sits
# dark next to sunlit walls. The key multiplies that exposure (the brightest parts go to white). Build, viewport
# preview, the Sun & Sky dialog and the entity definitions.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/core/sky_image.h', '''    float exposure = 0.0f;
''', '''    float exposure = 0.0f;
    // HDR sources: times that exposure (worldspawn iw8r_sky_brightness, kSkyBrightnessKey). Above 1 the sky is
    // brighter and its brightest parts (sun, cloud edges) go to white.
    float brightness = 1.0f;
''')
patch('src/core/sky_image.h', '''struct SkyConvertInfo {''', '''// An own map's sky brightness: a number from 0.1 to 16, 1 when absent. `bad` is set when the text is not one.
inline constexpr const char* kSkyBrightnessKey = "iw8r_sky_brightness";
float parseSkyBrightness(const std::string* text, bool* bad = nullptr);

struct SkyConvertInfo {''')
patch('src/core/sky_image.cpp', '''bool skyCubemapFromDds(const std::string& dds, std::string& cube, const SkyConvertOptions& opt, SkyConvertInfo* info,''',
      '''float parseSkyBrightness(const std::string* text, bool* bad) {
    if (bad) *bad = false;
    if (!text || text->empty()) return 1.0f;
    char* end = nullptr;
    const double v = std::strtod(text->c_str(), &end);
    while (end && (*end == ' ' || *end == '\\t')) ++end;
    if (end == text->c_str() || (end && *end) || !(v >= 0.1 && v <= 16.0)) {
        if (bad) *bad = true;
        return 1.0f;
    }
    return float(v);
}

bool skyCubemapFromDds(const std::string& dds, std::string& cube, const SkyConvertOptions& opt, SkyConvertInfo* info,''')
patch('src/core/sky_image.cpp', '''        exposure = opt.exposure > 0.0f ? opt.exposure : (inf.highLuminance > 1e-6f ? 0.9f / inf.highLuminance : 1.0f);''',
      '''        exposure = opt.exposure > 0.0f ? opt.exposure : (inf.highLuminance > 1e-6f ? 0.9f / inf.highLuminance : 1.0f);
        exposure *= std::clamp(opt.brightness, 0.1f, 16.0f);''')
patch('src/core/compile_iw8.cpp', '''            SkyConvertOptions so;
            so.faceSize = opt.skyFaceSize;''', '''            SkyConvertOptions so;
            so.faceSize = opt.skyFaceSize;
            {
                bool badBrightness = false;
                const std::string* sb = worldspawn->value(kSkyBrightnessKey);
                so.brightness = parseSkyBrightness(sb, &badBrightness);
                if (badBrightness)
                    warn.push_back("worldspawn " + std::string(kSkyBrightnessKey) + " '" + *sb + "' is not a number from 0.1 to 16: using 1");
            }''')

C = 'src/qtapp/camera_view.cpp'
patch(C, '''        const QString skyKey = sky.isEmpty() ? QString() : sky + QString("|%1 %2 %3").arg(skyRot[0]).arg(skyRot[1]).arg(skyRot[2]);''',
      '''        // An own map's sky brightness (worldspawn iw8r_sky_brightness), as the build applies it. A stock map's sky keeps
        // its own radiance (the preview's exposure is undone for it below).
        float skyBrightness = 1.0f;
        if (!gameStockLook())
            if (const Entity* ws = ctx_->doc.worldspawn()) skyBrightness = parseSkyBrightness(ws->value(kSkyBrightnessKey));
        const QString skyKey = sky.isEmpty() ? QString() : sky + QString("|%1 %2 %3|%4").arg(skyRot[0]).arg(skyRot[1]).arg(skyRot[2]).arg(skyBrightness);''')
patch(C, '''            for (int k = 0; k < 3; ++k) so.rotation[k] = skyRot[k];
            SkyConvertInfo skyInfo;''', '''            for (int k = 0; k < 3; ++k) so.rotation[k] = skyRot[k];
            so.brightness = skyBrightness;
            SkyConvertInfo skyInfo;''')

D = 'src/qtapp/sun_sky_dialog.'
patch(D + 'h', '''    QString sky;                   // "" = no sky key
};''', '''    QString sky;                   // "" = no sky key
    double skyBrightness = 1.0;    // iw8r_sky_brightness (1 = the key is absent)
};''')
patch(D + 'cpp', '''        for (const char* key : {"skyname", "sky"})
            if (const std::string* k = e.value(key); k && !k->empty()) {
                s.sky = QString::fromStdString(*k);
                break;
            }
        break;''', '''        for (const char* key : {"skyname", "sky"})
            if (const std::string* k = e.value(key); k && !k->empty()) {
                s.sky = QString::fromStdString(*k);
                break;
            }
        s.skyBrightness = double(parseSkyBrightness(e.value(kSkyBrightnessKey)));
        break;''')
patch(D + 'cpp', '''        {"skyname", s.sky.trimmed().toStdString()}};''', '''        {"skyname", s.sky.trimmed().toStdString()},
        {kSkyBrightnessKey, std::fabs(s.skyBrightness - 1.0) < 1e-6 ? std::string() : num(s.skyBrightness)}};''')
patch(D + 'cpp', '''    form->addRow("Sky", sky_);
''', '''    form->addRow("Sky", sky_);
    skyBrightness_ = new QDoubleSpinBox(this);
    skyBrightness_->setObjectName("skyBrightness");
    skyBrightness_->setRange(0.1, 16);
    skyBrightness_->setDecimals(2);
    skyBrightness_->setSingleStep(0.25);
    skyBrightness_->setValue(s.skyBrightness);
    skyBrightness_->setToolTip("The built sky is an 8-bit picture exposed so that its brightest clouds stay below white. Above 1 the "
                               "whole sky is brighter and its brightest parts go to white; below 1 it is darker.");
    form->addRow("Sky brightness", skyBrightness_);
''')
patch(D + 'cpp', '''    s.sky = sky_->currentText().trimmed();
    return s;''', '''    s.sky = sky_->currentText().trimmed();
    s.skyBrightness = skyBrightness_->value();
    return s;''')
print('source ok')

p = ROOT + 'data/iw8_entity_defs_overrides.json'
text = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in text else '\n'
j = json.loads(text)


def find_named(node, name):
    if isinstance(node, dict):
        if name in node and isinstance(node[name], dict):
            return node[name]
        for v in node.values():
            r = find_named(v, name)
            if r is not None:
                return r
    elif isinstance(node, list):
        for v in node:
            r = find_named(v, name)
            if r is not None:
                return r
    return None


ws = find_named(j, 'worldspawn')
keymap = None
for k, v in ws.items():
    if isinstance(v, dict) and 'iw8r_haze' in v:
        keymap = v
assert keymap is not None
keymap['iw8r_sky_brightness'] = {
    'type': 'float',
    'default': '1',
    'description': 'Brightness of the built sky, 0.1 to 16. A built map\'s sky is an 8-bit picture made from the stock sky, '
                   'exposed so that its brightest clouds stay below white; most of a bright sky then looks dark beside sunlit '
                   'walls. 2 doubles the sky (its brightest parts go to white), 0.5 halves it. Compile > Sun & Sky edits it; '
                   'the viewport shows it. Not seen in game.',
    'required': False,
    'examples': ['2', '3.5'],
}
io.open(p, 'w', encoding='utf-8', newline='').write(json.dumps(j, indent=2, ensure_ascii=False).replace('\n', nl) + nl)
r = subprocess.run([sys.executable, '-I', ROOT + 'tools/gen_iw8_entity_defs.py'], capture_output=True, text=True, cwd=ROOT)
print(r.stdout[-200:], r.stderr[-300:])
