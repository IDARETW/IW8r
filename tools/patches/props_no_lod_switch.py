import io

# Every prop at full detail, nothing switching (owner 2026-11-15: "objects still seem to pop in and out of visibility,
# lets just make all of the objects render and not change LoD/shift at all so it stays consistent"): View > Game
# Lighting Options > Prop Levels of Detail is off by default now (a new setting key, so an earlier "on" does not carry
# over), and with it off the props keep LOD0 at every distance and the pixel-size cut is off too: a placement is left
# out only when it is outside the view.

ROOT = 'E:/IW8-Radiant/src/'


def rw(p, pairs):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        if b in s and a not in s:
            continue
        assert s.count(a) == 1, (p, s.count(a), a[:90])
        s = s.replace(a, b)
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


rw('viewport/render.h', [
('''    static bool gameLodCutoff();''', '''    static bool gameLodCutoff();
    // The cut of placements under a pixel (on with prop levels of detail; off: only the view's edges leave props out).
    static void setPixelCull(bool on);
    static bool pixelCull();'''),
])
rw('viewport/render.cpp', [
('''void SceneRenderer::setGameLodCutoff(bool on) { gameLodCutoffFlag() = on; }''', '''void SceneRenderer::setGameLodCutoff(bool on) { gameLodCutoffFlag() = on; }
static std::atomic<bool>& pixelCullFlag() {
    static std::atomic<bool> on{false};
    return on;
}
void SceneRenderer::setPixelCull(bool on) { pixelCullFlag() = on; }
bool SceneRenderer::pixelCull() { return pixelCullFlag(); }'''),
('''            if (e.why == 1 && e.dist > e.rad && e.rad < e.dist * pixelAngle * kPx) e.why = 3;''',
 '''            if (e.why == 1 && pixelCull() && e.dist > e.rad && e.rad < e.dist * pixelAngle * kPx) e.why = 3;'''),
('''                if (dist > mr && mr < dist * pixelAngle * kPx) why = 3;  // the model under kPx pixels in radius''',
 '''                if (pixelCull() && dist > mr && mr < dist * pixelAngle * kPx) why = 3;  // the model under kPx pixels in radius'''),
])
rw('qtapp/main_window.h', [
('''    bool propLodsEnabled_ = true;''', '''    bool propLodsEnabled_ = false;'''),
])
rw('qtapp/main_window.cpp', [
('''        toggle("GamePropLods", "Prop Levels of Detail (as in game)", "game/propLods", [this](bool on) {
            if (propLodsEnabled_ == on) return;
            propLodsEnabled_ = on;
            refPropBuilt_.clear();
            refPropBuiltTris_ = 0;
            if (refProps_) QTimer::singleShot(0, this, [this] { rebuildReferenceProps(); });
        });''', '''        // Off by default (owner 2026-11-15: no switching at all, so the view stays consistent): every prop at LOD0 and
        // no pixel-size cut; on: the game's LOD distances and props under a pixel left out.
        toggle("GamePropLods", "Prop Levels of Detail (as in game)", "game/propLodSwitching", [this](bool on) {
            app::SceneRenderer::setPixelCull(on);
            if (propLodsEnabled_ == on) return;
            propLodsEnabled_ = on;
            refPropBuilt_.clear();
            refPropBuiltTris_ = 0;
            if (refProps_) QTimer::singleShot(0, this, [this] { rebuildReferenceProps(); });
        }, false);'''),
])
print('ok')
