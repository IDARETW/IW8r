import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/qtapp/compile_panel.h', [
 ('''    class QCheckBox* stockTextures_ = nullptr;''', '''    class QCheckBox* stockTextures_ = nullptr;
    // Worldspawn iw8r_bake_lightmaps (5.11): checked = the build bakes the world's lightmap.
    class QCheckBox* bakeLightmaps_ = nullptr;
    void syncBakeLightmaps();'''),
])
patch('src/qtapp/compile_panel.cpp', [
 ('''    top->addWidget(stockTextures_);
    auto* info = new QPushButton("Map Info...", this);''', '''    top->addWidget(stockTextures_);
    bakeLightmaps_ = new QCheckBox("Bake lightmaps", this);
    bakeLightmaps_->setObjectName("compileBakeLightmaps");
    bakeLightmaps_->setToolTip("Worldspawn iw8r_bake_lightmaps: the build bakes the world's lightmap (sky light, bounce, sun "
                               "visibility; the game adds the sun and the lights live). View > Game Lighting shows the same bake");
    top->addWidget(bakeLightmaps_);
    connect(bakeLightmaps_, &QCheckBox::toggled, this, [this](bool on) {
        Entity* w = ctx_->doc.worldspawn();
        if (!w) return;
        const std::string* cur = w->value("iw8r_bake_lightmaps");
        const bool was = cur && *cur == "1";
        if (was == on) return;
        const Id wid = w->id;
        ctx_->doc.beginGroup(on ? "Bake lightmaps on" : "Bake lightmaps off");
        if (on) ctx_->doc.setKeyValue(wid, "iw8r_bake_lightmaps", "1");
        else ctx_->doc.removeKey(wid, "iw8r_bake_lightmaps");
        ctx_->doc.endGroup();
        ctx_->documentEdited(on ? "The build bakes lightmaps (worldspawn iw8r_bake_lightmaps 1)" : "The build makes no lightmap");
    });
    auto* info = new QPushButton("Map Info...", this);'''),
 ('''    connect(ctx_, &EditorContext::documentChanged, this, &CompilePanel::refreshMapId);''',
  '''    connect(ctx_, &EditorContext::documentChanged, this, &CompilePanel::refreshMapId);
    connect(ctx_, &EditorContext::documentChanged, this, &CompilePanel::syncBakeLightmaps);'''),
 ('''QString CompilePanel::mapId() const { return sanitizeMapId(mapId_->text()); }''', '''QString CompilePanel::mapId() const { return sanitizeMapId(mapId_->text()); }

void CompilePanel::syncBakeLightmaps() {
    if (!bakeLightmaps_) return;
    const Entity* w = ctx_->doc.worldspawn();
    const std::string* v = w ? w->value("iw8r_bake_lightmaps") : nullptr;
    const QSignalBlocker block(bakeLightmaps_);
    bakeLightmaps_->setChecked(v && *v == "1");
}'''),
])
s = io.open('src/qtapp/compile_panel.cpp', encoding='utf-8', newline='').read()
if '#include <QSignalBlocker>' not in s:
    s = s.replace('#include <QCheckBox>', '#include <QCheckBox>\n#include <QSignalBlocker>', 1)
    io.open('src/qtapp/compile_panel.cpp', 'w', encoding='utf-8', newline='').write(s)
print('ok')
