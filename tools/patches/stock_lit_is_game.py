import io
def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b, *c in pairs:
        n = c[0] if c else 1
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == n, (p, s.count(a), a[:100])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)
patch('src/viewport/scene.h', [
 ('''    bool gameLook = false;''', '''    bool gameLook = false;
    // A stock map reference is shown (set by the camera): its lit preview is the game's lighting, as the editor's own
    // light bake has nothing to light there (owner report 2026-11-12: Lit Preview alone was blown out on Speedball).
    bool gameLookAuto = false;
    bool gameLookActive() const { return gameLook || (gameLookAuto && litPreview); }'''),
])
patch('src/viewport/camera_geometry.cpp', [
 ('''    const bool lit = sc.litPreview && !sc.gameLook;
    const bool encoded = sc.litEncoded() && !sc.gameLook;
    const GameLightmap* gameLm = sc.gameLook ? sc.gameLightmap.get() : nullptr;''',
  '''    const bool lit = sc.litPreview && !sc.gameLookActive();
    const bool encoded = sc.litEncoded() && !sc.gameLookActive();
    const GameLightmap* gameLm = sc.gameLookActive() ? sc.gameLightmap.get() : nullptr;'''),
 ('''            if (sc.gameLook) shade = 1.0;  // the renderer lights textured faces''', '''            if (sc.gameLookActive()) shade = 1.0;  // the renderer lights textured faces'''),
])
patch('src/viewport/scene.cpp', [
 ('''            const bool litMode = scene.litPreview && !scene.gameLook;
            const bool encodedMode = scene.litEncoded() && !scene.gameLook;
            const GameLightmap* gameLm = scene.gameLook ? scene.gameLightmap.get() : nullptr;''',
  '''            const bool litMode = scene.litPreview && !scene.gameLookActive();
            const bool encodedMode = scene.litEncoded() && !scene.gameLookActive();
            const GameLightmap* gameLm = scene.gameLookActive() ? scene.gameLightmap.get() : nullptr;'''),
 ('''                if (scene.gameLook) shade = 1.0;''', '''                if (scene.gameLookActive()) shade = 1.0;'''),
])
patch('src/qtapp/camera_view.cpp', [
 ('''bool CameraView::gameLookOn() const { return ctx_->scene.gameLook && ctx_->texturedView && !ctx_->scene.captureMode; }''',
  '''bool CameraView::gameLookOn() const {
    ctx_->scene.gameLookAuto = !gameStockMap_.empty();
    return ctx_->scene.gameLookActive() && ctx_->texturedView && !ctx_->scene.captureMode;
}'''),
 ('''        (uint64_t(ctx_->scene.captureMode) << 13));''', '''        (uint64_t(ctx_->scene.captureMode) << 13) | (uint64_t(gameLookOn()) << 14));'''),
])
print('ok')
