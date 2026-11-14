import io

# Plan 2.4, editor side: a map asks for the occlusion bake with worldspawn iw8r_umbra_bake 1 (Compile panel:
# "Occlusion bake"). The build then writes the map's opaque, axis-aligned box brushes as "umbra_occluders" into the
# extraction's world, and the converter bakes the Umbra tome from them (x64-zt umbra_bake.hpp). Off by default: the
# one-cell tome that hides nothing is the proven one.

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


C = 'src/core/compile_iw8.cpp'
patch(C, '''    // The map's surface shader (worldspawn iw8r_surface_shader, core custom_shader): the converter compiles it into the
    // map's own surface shaders (needs the x64-zt converter with "iw8r_surface_shader"; plan M6).
    std::string surfaceShaderKey;''', '''    // The occlusion bake (worldspawn iw8r_umbra_bake 1, plan 2.4): the opaque box brushes of the world as occluder
    // boxes; the converter cuts the map's free space into cells along them and joins the cells by portals, so the
    // game's Umbra hides what lies behind them. Only axis-aligned boxes whose faces are all opaque (or caulk) count;
    // anything else hides nothing. Without the key the converter writes its one-cell tome (nothing hidden by Umbra).
    std::string occludersKey;
    if (worldspawn && worldspawn->value("iw8r_umbra_bake") && *worldspawn->value("iw8r_umbra_bake") == "1") {
        std::string boxes;
        size_t count = 0, skewed = 0, open = 0;
        for (const EdgeBrush& b : edgeData.brushes) {
            if (!b.opaque) {
                ++open;
                continue;
            }
            bool isBox = b.planes.size() == 6 && b.bounds.valid();
            for (const Plane& p : b.planes) {
                const double m = std::max({std::fabs(p.n.x), std::fabs(p.n.y), std::fabs(p.n.z)});
                if (m < 0.99999) isBox = false;
            }
            if (!isBox) {
                ++skewed;
                continue;
            }
            if (count) boxes += ",";
            boxes += "[" + num(b.bounds.mins.x) + "," + num(b.bounds.mins.y) + "," + num(b.bounds.mins.z) + "," + num(b.bounds.maxs.x) + "," +
                     num(b.bounds.maxs.y) + "," + num(b.bounds.maxs.z) + "]";
            ++count;
        }
        if (count) occludersKey = ",\\"umbra_occluders\\":[" + boxes + "]";
        warn.push_back("occlusion bake: " + std::to_string(count) + " occluder box(es) from the opaque box brushes (" + std::to_string(skewed) +
                       " opaque brush(es) are not axis-aligned boxes and " + std::to_string(open) +
                       " are not opaque: they hide nothing)" +
                       (count ? "; needs a converter with umbra_occluders; not seen in game" : ": nothing to bake, the one-cell tome is written"));
    }
    // The map's surface shader (worldspawn iw8r_surface_shader, core custom_shader): the converter compiles it into the
    // map's own surface shaders (needs the x64-zt converter with "iw8r_surface_shader"; plan M6).
    std::string surfaceShaderKey;''')
patch(C, '''modelsKey + gradingKey + volumesKey + decalsKey + surfaceShaderKey + lightmapKeys +''',
      '''modelsKey + gradingKey + volumesKey + decalsKey + surfaceShaderKey + occludersKey + lightmapKeys +''')

# Compile panel: a checkbox beside Bake lightmaps.
P = 'src/qtapp/compile_panel.cpp'
patch(P, '''    auto* bakeSettings = new QPushButton("Bake Settings...", this);''', '''    occlusionBake_ = new QCheckBox("Occlusion bake", this);
    occlusionBake_->setObjectName("compileOcclusionBake");
    occlusionBake_->setToolTip("Worldspawn iw8r_umbra_bake: the build makes occlusion data from the map's opaque box brushes, so the game "
                               "stops drawing what lies behind walls (faster on maps with rooms and streets). Off: the game hides nothing "
                               "by occlusion, which always works. Checked offline only; not seen in game");
    top->addWidget(occlusionBake_);
    connect(occlusionBake_, &QCheckBox::toggled, this, [this](bool on) {
        Entity* w = ctx_->doc.worldspawn();
        if (!w) return;
        const std::string* cur = w->value("iw8r_umbra_bake");
        if ((cur && *cur == "1") == on) return;
        const Id wid = w->id;
        ctx_->doc.beginGroup(on ? "Occlusion bake on" : "Occlusion bake off");
        if (on) ctx_->doc.setKeyValue(wid, "iw8r_umbra_bake", "1");
        else ctx_->doc.removeKey(wid, "iw8r_umbra_bake");
        ctx_->doc.endGroup();
        ctx_->documentEdited(on ? "The build bakes occlusion from the opaque box brushes (worldspawn iw8r_umbra_bake 1)"
                                : "The build makes no occlusion bake (the game hides nothing by occlusion)");
    });
    auto* bakeSettings = new QPushButton("Bake Settings...", this);''')
patch(P, '''void CompilePanel::syncBakeLightmaps() {
    if (!bakeLightmaps_) return;
    const Entity* w = ctx_->doc.worldspawn();
    const std::string* v = w ? w->value("iw8r_bake_lightmaps") : nullptr;
    const QSignalBlocker block(bakeLightmaps_);
    bakeLightmaps_->setChecked(v && *v == "1");
}''', '''void CompilePanel::syncBakeLightmaps() {
    if (!bakeLightmaps_) return;
    const Entity* w = ctx_->doc.worldspawn();
    const std::string* v = w ? w->value("iw8r_bake_lightmaps") : nullptr;
    const QSignalBlocker block(bakeLightmaps_);
    bakeLightmaps_->setChecked(v && *v == "1");
    if (occlusionBake_) {
        const std::string* o = w ? w->value("iw8r_umbra_bake") : nullptr;
        const QSignalBlocker blockO(occlusionBake_);
        occlusionBake_->setChecked(o && *o == "1");
    }
}''')
patch('src/qtapp/compile_panel.h', '''    class QCheckBox* bakeLightmaps_ = nullptr;
''', '''    class QCheckBox* bakeLightmaps_ = nullptr;
    class QCheckBox* occlusionBake_ = nullptr;  // worldspawn iw8r_umbra_bake (plan 2.4)
''')

# GUI test: the checkbox sets and clears the key and follows undo.
T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''        } else {
            r.check(false, "the Compile panel has a Bake lightmaps checkbox");
        }
''', '''        } else {
            r.check(false, "the Compile panel has a Bake lightmaps checkbox");
        }
        if (auto* occ = w.compilePanel() ? w.compilePanel()->findChild<QCheckBox*>("compileOcclusionBake") : nullptr) {
            const auto key = [&] {
                const Entity* ws = ctx.doc.worldspawn();
                const std::string* v = ws ? ws->value("iw8r_umbra_bake") : nullptr;
                return v ? *v : std::string();
            };
            const bool was = occ->isChecked();
            occ->setChecked(true);
            const std::string on = key();
            occ->setChecked(false);
            const std::string off = key();
            ctx.doc.undo();
            ctx.documentEdited();
            const bool followsUndo = occ->isChecked();
            occ->setChecked(was);
            r.check(on == "1" && off.empty() && followsUndo, "Compile > Occlusion bake sets and clears worldspawn iw8r_umbra_bake and follows undo");
        } else {
            r.check(false, "the Compile panel has an Occlusion bake checkbox");
        }
''')
print('ok')
