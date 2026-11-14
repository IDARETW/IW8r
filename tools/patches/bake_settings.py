import io

# Plan 1.10: the lightmap bake settings in the Compile panel (Bake Settings...), and one reader (core bake_options) for
# the build's bake and the viewport's preview bake.

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


def include(p, inc, after):
    s = read(p)
    if inc in s:
        return
    nl = '\r\n' if '\r\n' in s else '\n'
    i = s.index(after)
    j = s.index(nl, i) + len(nl)
    write(p, s[:j] + inc + nl + s[j:])


# --- the build ------------------------------------------------------------------------------------------------------
C = 'src/core/compile_iw8.cpp'
patch(C, '''        // The sky: worldspawn iw8r_bake_sky "r g b intensity" (default a pale blue at 0.35 of the sun).
        scene.skyIntensity = 0.35;
        if (const std::string* sk = worldspawn->value("iw8r_bake_sky")) {
            double v[4] = {0, 0, 0, 0};
            if (std::sscanf(sk->c_str(), "%lf %lf %lf %lf", &v[0], &v[1], &v[2], &v[3]) == 4) {
                scene.skyColor = Vec3{v[0], v[1], v[2]};
                scene.skyIntensity = v[3];
            } else {
                warn.push_back("worldspawn iw8r_bake_sky '" + *sk + "' is not 'r g b intensity': the default sky is used");
            }
        }
''', '''        // The sky (iw8r_bake_sky "r g b intensity", default a pale blue at 0.35 of the sun), the texel size and the ray
        // count: core bake_options, the reader the viewport's preview bake uses too.
        const MapBakeOptions bakeOpt = mapBakeOptions(worldspawn);
        for (const std::string& n : bakeOpt.notes) warn.push_back(n);
        scene.skyColor = bakeOpt.skyColor;
        scene.skyIntensity = bakeOpt.skyIntensity;
''')
patch(C, '''        BakeSettings settings;
        if (const std::string* ts = worldspawn->value("iw8r_lightmap_texel")) {
            double v = 0;
            if (parseNumber(*ts, v) && v >= 1) settings.texelSize = v;
        }
        if (const std::string* rs = worldspawn->value("iw8r_bake_rays")) {
            double v = 0;
            if (parseNumber(*rs, v) && v >= 1) settings.skyRays = int(std::min(v, 1024.0));
        }
''', '''        BakeSettings settings;
        settings.texelSize = bakeOpt.texelSize;
        settings.skyRays = bakeOpt.skyRays;
''')
include(C, '#include "bake_options.h"', '#include "compile_iw8.h"')

# --- the viewport's preview bake ------------------------------------------------------------------------------------
V = 'src/qtapp/camera_view.cpp'
patch(V, '''iw8r::Vec3 gameSkyOf(const iw8r::Document& doc) {
    iw8r::Vec3 sky{0.55 * 0.35, 0.65 * 0.35, 0.8 * 0.35};
    for (const auto& e : doc.map().entities)
        if (e.isWorldspawn()) {
            if (const std::string* s = e.value("iw8r_bake_sky")) {
                double v[4];
                if (std::sscanf(s->c_str(), "%lf %lf %lf %lf", &v[0], &v[1], &v[2], &v[3]) == 4) sky = iw8r::Vec3{v[0] * v[3], v[1] * v[3], v[2] * v[3]};
            }
            break;
        }
    return sky;
}
''', '''// The map's bake settings (worldspawn keys, core bake_options: the reader the build uses).
iw8r::MapBakeOptions gameBakeOptionsOf(const iw8r::Document& doc) {
    for (const auto& e : doc.map().entities)
        if (e.isWorldspawn()) return iw8r::mapBakeOptions(&e);
    return iw8r::mapBakeOptions(nullptr);
}
iw8r::Vec3 gameSkyOf(const iw8r::Document& doc) { return gameBakeOptionsOf(doc).skyLight(); }
''')
patch(V, '''    for (const auto& e : ctx_->doc.map().entities)
        if (e.isWorldspawn()) {
            if (const std::string* ts = e.value("iw8r_lightmap_texel")) in.settings.texelSize = std::max(1.0, std::atof(ts->c_str()));
            if (const std::string* rs = e.value("iw8r_bake_rays")) in.settings.skyRays = std::clamp(std::atoi(rs->c_str()), 1, 1024);
            break;
        }
''', '''    {
        const MapBakeOptions opt = gameBakeOptionsOf(ctx_->doc);
        in.settings.texelSize = opt.texelSize;
        in.settings.skyRays = opt.skyRays;
    }
''')
include(V, '#include "bake_options.h"', '#include "camera_view.h"')

# --- the Compile panel ----------------------------------------------------------------------------------------------
H = 'src/qtapp/compile_panel.h'
patch(H, '''    void editMapInfo();
''', '''    void editMapInfo();
    // The lightmap bake settings (core bake_options: sky colour and strength, texel size, sky rays), stored as
    // worldspawn keys so they save with the map and are undoable. The dialog calls applyBakeSettings.
    void editBakeSettings();
    // Writes the settings as worldspawn keys in one undo step (a value at its default removes its key). False when
    // nothing changed or there is no worldspawn.
    bool applyBakeSettings(const struct MapBakeOptions& options);
''')
P = 'src/qtapp/compile_panel.cpp'
patch(P, '''    auto* info = new QPushButton("Map Info...", this);
    info->setObjectName("compileMapInfo");''', '''    auto* bakeSettings = new QPushButton("Bake Settings...", this);
    bakeSettings->setObjectName("compileBakeSettings");
    bakeSettings->setToolTip("The lightmap bake's sky colour and strength, texel size and sky rays (worldspawn iw8r_bake_sky, "
                             "iw8r_lightmap_texel, iw8r_bake_rays). View > Game Lighting bakes its preview with the same values");
    connect(bakeSettings, &QPushButton::clicked, this, &CompilePanel::editBakeSettings);
    top->addWidget(bakeSettings);
    auto* info = new QPushButton("Map Info...", this);
    info->setObjectName("compileMapInfo");''')
patch(P, '''void CompilePanel::editMapInfo() {''', '''bool CompilePanel::applyBakeSettings(const MapBakeOptions& options) {
    Entity* w = ctx_->doc.worldspawn();
    if (!w) return false;
    const BakeOptionKeys keys = bakeOptionKeys(options);
    const std::pair<const char*, const std::string*> wanted[] = {
        {"iw8r_bake_sky", &keys.sky}, {"iw8r_lightmap_texel", &keys.texel}, {"iw8r_bake_rays", &keys.rays}};
    bool differs = false;
    for (const auto& [key, value] : wanted) {
        const std::string* cur = w->value(key);
        differs = differs || (cur ? *cur : std::string()) != *value;
    }
    if (!differs) return false;
    const Id wid = w->id;
    ctx_->doc.beginGroup("Bake settings");
    for (const auto& [key, value] : wanted) {
        const Entity* now = ctx_->doc.worldspawn();
        const std::string* cur = now ? now->value(key) : nullptr;
        if (value->empty()) {
            if (cur) ctx_->doc.removeKey(wid, key);
        } else if (!cur || *cur != *value) {
            ctx_->doc.setKeyValue(wid, key, *value);
        }
    }
    ctx_->doc.endGroup();
    ctx_->documentEdited("Lightmap bake settings changed (the build and View > Game Lighting bake with them)");
    return true;
}

void CompilePanel::editBakeSettings() {
    const Entity* w = ctx_->doc.worldspawn();
    if (!w) return;
    const MapBakeOptions cur = mapBakeOptions(w), def;
    QDialog dlg(this);
    dlg.setWindowTitle("Lightmap Bake Settings");
    auto* form = new QFormLayout(&dlg);
    auto spin = [&](double lo, double hi, double step, int decimals, double v) {
        auto* s = new QDoubleSpinBox(&dlg);
        s->setDecimals(decimals);
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setValue(v);
        return s;
    };
    auto* red = spin(0, 1, 0.05, 3, cur.skyColor.x);
    auto* green = spin(0, 1, 0.05, 3, cur.skyColor.y);
    auto* blue = spin(0, 1, 0.05, 3, cur.skyColor.z);
    auto* strength = spin(0, 16, 0.05, 3, cur.skyIntensity);
    auto* texel = spin(1, 256, 1, 1, cur.texelSize);
    auto* rays = spin(1, 1024, 8, 0, cur.skyRays);
    red->setObjectName("bakeSkyRed"), green->setObjectName("bakeSkyGreen"), blue->setObjectName("bakeSkyBlue");
    strength->setObjectName("bakeSkyStrength"), texel->setObjectName("bakeTexel"), rays->setObjectName("bakeRays");
    strength->setToolTip("The light of the open sky on a surface facing up, as a part of the sun's light (0.35 = a clear day)");
    texel->setToolTip("World units per lightmap texel. Smaller is sharper and slower; the bake raises it when the map does not fit");
    rays->setToolTip("Sky rays per texel. More is smoother and slower");
    auto* row = new QHBoxLayout;
    row->addWidget(red), row->addWidget(green), row->addWidget(blue);
    form->addRow("Sky colour (red, green, blue)", row);
    form->addRow("Sky strength", strength);
    form->addRow("Texel size (units)", texel);
    form->addRow("Sky rays per texel", rays);
    auto* note = new QLabel("The build bakes with these when \\"Bake lightmaps\\" is on. View > Game Lighting bakes its preview with "
                            "the same values. The sun and the lights are not baked: the game draws them live.", &dlg);
    note->setWordWrap(true);
    form->addRow(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dlg, [&] {
        red->setValue(def.skyColor.x), green->setValue(def.skyColor.y), blue->setValue(def.skyColor.z);
        strength->setValue(def.skyIntensity), texel->setValue(def.texelSize), rays->setValue(def.skyRays);
    });
    if (dlg.exec() != QDialog::Accepted) return;
    MapBakeOptions out = cur;
    out.skyColor = Vec3{red->value(), green->value(), blue->value()};
    out.skyIntensity = strength->value();
    out.texelSize = texel->value();
    out.skyRays = int(std::lround(rays->value()));
    applyBakeSettings(out);
}

void CompilePanel::editMapInfo() {''')
include(P, '#include "bake_options.h"', '#include "compile_panel.h"')

# --- tests ----------------------------------------------------------------------------------------------------------
write('tests/bake_options_tests.cpp', '''#include "bake_options.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;

namespace {
Entity worldspawnWith(std::initializer_list<std::pair<const char*, const char*>> keys) {
    std::string text = "{\\n\\"classname\\" \\"worldspawn\\"\\n";
    for (const auto& [k, v] : keys) text += std::string("\\"") + k + "\\" \\"" + v + "\\"\\n";
    text += "}\\n";
    Id next = 1;
    const ParseResult pr = parseMap("iwmap 4\n" + text, next);
    CHECK(pr.map && pr.map->entities.size() == 1);
    return pr.map && !pr.map->entities.empty() ? pr.map->entities[0] : Entity{};
}
}  // namespace

// Plan 1.10: the lightmap bake settings are worldspawn keys with one reader for the build, the viewport and the
// Compile panel's dialog.
TEST(bake_options_defaults_and_keys) {
    const MapBakeOptions none = mapBakeOptions(nullptr);
    CHECK(!none.bake && none.skyIsDefault() && none.texelIsDefault() && none.raysAreDefault() && none.notes.empty());
    CHECK(none.skyIntensity == 0.35 && none.texelSize == 16 && none.skyRays == 32);
    const BakeOptionKeys nk = bakeOptionKeys(none);
    CHECK(nk.sky.empty() && nk.texel.empty() && nk.rays.empty());  // defaults write no key

    const Entity w = worldspawnWith({{"iw8r_bake_lightmaps", "1"}, {"iw8r_bake_sky", "0.9 0.6 0.4 0.5"}, {"iw8r_lightmap_texel", "8"}, {"iw8r_bake_rays", "64"}});
    const MapBakeOptions o = mapBakeOptions(&w);
    CHECK(o.bake && o.notes.empty());
    CHECK(o.skyColor.x == 0.9 && o.skyColor.y == 0.6 && o.skyColor.z == 0.4 && o.skyIntensity == 0.5);
    CHECK(o.texelSize == 8 && o.skyRays == 64 && !o.skyIsDefault() && !o.texelIsDefault() && !o.raysAreDefault());
    const Vec3 light = o.skyLight();
    CHECK(light.x == 0.45 && light.y == 0.3 && light.z == 0.2);
    // What the dialog writes reads back as the same settings.
    const BakeOptionKeys k = bakeOptionKeys(o);
    CHECK(k.sky == "0.9 0.6 0.4 0.5" && k.texel == "8" && k.rays == "64");
    const Entity back = worldspawnWith({{"iw8r_bake_sky", k.sky.c_str()}, {"iw8r_lightmap_texel", k.texel.c_str()}, {"iw8r_bake_rays", k.rays.c_str()}});
    const MapBakeOptions again = mapBakeOptions(&back);
    CHECK(again.skyColor.x == o.skyColor.x && again.skyIntensity == o.skyIntensity && again.texelSize == o.texelSize && again.skyRays == o.skyRays);
    CHECK(!again.bake);
}

TEST(bake_options_bad_values_and_limits) {
    const Entity w = worldspawnWith({{"iw8r_bake_sky", "blue"}, {"iw8r_lightmap_texel", "0.5"}, {"iw8r_bake_rays", "many"}});
    const MapBakeOptions o = mapBakeOptions(&w);
    CHECK(o.notes.size() == 3 && o.skyIsDefault() && o.texelIsDefault() && o.raysAreDefault());
    CHECK(o.notes[0].find("iw8r_bake_sky 'blue' is not 'r g b intensity'") != std::string::npos);
    const Entity neg = worldspawnWith({{"iw8r_bake_sky", "1 1 1 -2"}, {"iw8r_bake_rays", "5000"}});
    const MapBakeOptions n = mapBakeOptions(&neg);
    CHECK(n.notes.size() == 1 && n.skyIsDefault() && n.skyRays == 1024);  // a negative sky is refused, rays are capped
    const Entity five = worldspawnWith({{"iw8r_bake_sky", "1 1 1 1 1"}});
    CHECK(mapBakeOptions(&five).notes.size() == 1);
    // Out-of-range dialog values are clamped to what the bake accepts.
    MapBakeOptions wild;
    wild.skyColor = Vec3{2, -1, 0.5};
    wild.skyIntensity = 99;
    wild.texelSize = 0;
    wild.skyRays = 100000;
    const BakeOptionKeys k = bakeOptionKeys(wild);
    CHECK(k.sky == "1 0 0.5 16" && k.texel == "1" && k.rays == "1024");
}
''')

T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''        } else {
            r.check(false, "the Compile panel has a Bake lightmaps checkbox");
        }
''', '''        } else {
            r.check(false, "the Compile panel has a Bake lightmaps checkbox");
        }
        if (CompilePanel* cp = w.compilePanel()) {
            // Compile > Bake Settings: the values go to worldspawn keys in one undo step; defaults remove the keys.
            const auto key = [&](const char* k) {
                const Entity* ws = ctx.doc.worldspawn();
                const std::string* v = ws ? ws->value(k) : nullptr;
                return v ? *v : std::string("(none)");
            };
            const iw8r::MapBakeOptions before = iw8r::mapBakeOptions(ctx.doc.worldspawn());
            iw8r::MapBakeOptions o = before;
            o.skyColor = Vec3{0.8, 0.7, 0.6};
            o.skyIntensity = 0.5;
            o.texelSize = 8;
            o.skyRays = 48;
            const bool changed = cp->applyBakeSettings(o);
            const bool written = key("iw8r_bake_sky") == "0.8 0.7 0.6 0.5" && key("iw8r_lightmap_texel") == "8" && key("iw8r_bake_rays") == "48";
            const bool same = !cp->applyBakeSettings(o);  // nothing to change the second time
            ctx.doc.undo();
            ctx.documentEdited();
            const iw8r::MapBakeOptions undone = iw8r::mapBakeOptions(ctx.doc.worldspawn());
            const bool oneStep = undone.skyIntensity == before.skyIntensity && undone.texelSize == before.texelSize && undone.skyRays == before.skyRays;
            cp->applyBakeSettings(o);
            const bool cleared = cp->applyBakeSettings(iw8r::MapBakeOptions()) && key("iw8r_bake_sky") == "(none)" &&
                                 key("iw8r_lightmap_texel") == "(none)" && key("iw8r_bake_rays") == "(none)";
            cp->applyBakeSettings(before);
            r.check(changed && written && same && oneStep && cleared && cp->findChild<QPushButton*>("compileBakeSettings"),
                    "Compile > Bake Settings writes the sky, texel size and rays as worldspawn keys, in one undo step, and defaults remove them");
        }
''')
include(T, '#include "bake_options.h"', '#include "')
print('ok')
