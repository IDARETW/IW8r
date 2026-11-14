import io

# Plan 1.9, part 1: Game Lighting shows an own map without "Bake lightmaps" as the game draws it: the converter's
# constant sky fill and the live sun, no preview bake (research 117). The ambient light of own brushes takes the
# converter's multi-bounce occlusion.

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


# --- renderer -------------------------------------------------------------------------------------------------------
patch('src/viewport/render.h', '''        bool skyIsStock = false;  // sky = the stock map's mean baked light: unlit surfaces take the probes at that level
''', '''        bool skyIsStock = false;  // sky = the stock map's mean baked light: unlit surfaces take the probes at that level
        // An own map that the build gives no lightmap: surfaces take the converter's constant sky fill (research 117).
        bool skyIsUnbaked = false;
''')
R = 'src/viewport/render.cpp'
patch(R, '''                   game_.sky[0], game_.sky[1], game_.sky[2], game_.skyIsStock ? 1.0f : 0.0f};
    for (int r = 0; r < 6; ++r)''', '''                   game_.sky[0], game_.sky[1], game_.sky[2], game_.skyIsStock ? 1.0f : (game_.skyIsUnbaked ? 2.0f : 0.0f)};
    for (int r = 0; r < 6; ++r)''')
patch(R, '''        float3 indirect = gameSky.rgb * (0.6 + 0.4 * saturate(N.z));  // the probes' light, below, where no lightmap
''', '''        float3 indirect = gameSky.rgb * (0.6 + 0.4 * saturate(N.z));  // the probes' light, below, where no lightmap
        // An own map built without lightmaps (gameSky.w = 2): the converter's world shader gives every surface this
        // constant sky fill by the vertex normal (map_surface_realtime.hlsl, "Soft hemispherical sky fill"; research 117).
        if (gameSky.w > 1.5) indirect = lerp(float3(0.22, 0.23, 0.24), float3(0.38, 0.41, 0.44), saturate(Nf.z * 0.5 + 0.5));
''')
patch(R, '''        } else if (!lmOk && gameSky.w > 0.5 && probeCount.x > 0.5)''', '''        } else if (!lmOk && gameSky.w > 0.5 && gameSky.w < 1.5 && probeCount.x > 0.5)''')
patch(R, '''            s.ambient = indirect * aoV, s.gloss = 0.3;''', '''            s.ambient = indirect * aoOwn, s.gloss = 0.3;''')
patch(R, '''            float3 light = indirect * aoV + visibility * saturate(dot(N, gameSun.xyz)) * gameSunCol.rgb;
            c = albedo * light;''', '''            float3 light = indirect * aoOwn + visibility * saturate(dot(N, gameSun.xyz)) * gameSunCol.rgb;
            c = albedo * light;''')
patch(R, '''#ifdef IW8R_CUSTOM
            // A custom surface shader (core custom_shader): the map's own lighting function.
            IW8RSurface s;''', '''            // The converter's world shader occludes only the indirect light, in the multi-bounce form
            // ao / (1 - min(albedo, .85) (1 - ao)) (map_surface_realtime.hlsl; research 117).
            const float3 aoOwn = aoV / max(1.0 - min(albedo, 0.85) * (1.0 - aoV), 1e-3);
#ifdef IW8R_CUSTOM
            // A custom surface shader (core custom_shader): the map's own lighting function.
            IW8RSurface s;''')

# --- camera ---------------------------------------------------------------------------------------------------------
H = 'src/qtapp/camera_view.h'
patch(H, '''    bool gameBaking() const { return gameBaking_; }
''', '''    bool gameBaking() const { return gameBaking_; }
    // Game Lighting on an own map that the build gives no lightmap (worldspawn iw8r_bake_lightmaps is not 1): there
    // is no preview bake; the surfaces take the converter's constant sky fill and the live sun, as in the game.
    bool gameLookUnbaked() const;
''')
patch(H, '''    bool gameBaking_ = false, gameLutReady_ = false, gameWasOn_ = false, gameMetering_ = false;
''', '''    bool gameBaking_ = false, gameLutReady_ = false, gameWasOn_ = false, gameMetering_ = false;
    bool gameUnbakedTold_ = false;  // the status line said once that the map has no lightmap
''')
V = 'src/qtapp/camera_view.cpp'
patch(V, '''void CameraView::prepareGameLook() {''', '''bool CameraView::gameLookUnbaked() const { return !gameStockLook() && !gameBakeOptionsOf(ctx_->doc).bake; }

void CameraView::prepareGameLook() {''')
patch(V, '''        const Vec3 sky = gameSkyOf(ctx_->doc);
        g.sky[0] = float(sky.x), g.sky[1] = float(sky.y), g.sky[2] = float(sky.z);
        if (stock) {  // the stock map's bloom veil and vignette (research 109)''', '''        const Vec3 sky = gameSkyOf(ctx_->doc);
        g.sky[0] = float(sky.x), g.sky[1] = float(sky.y), g.sky[2] = float(sky.z);
        g.skyIsUnbaked = gameLookUnbaked();
        if (stock) {  // the stock map's bloom veil and vignette (research 109)''')
patch(V, '''void CameraView::maybeBakeGameLook() {
    if (!gameLookOn() || gameBaking_) return;
''', '''void CameraView::maybeBakeGameLook() {
    if (!gameLookOn() || gameBaking_) return;
    // An own map without "Bake lightmaps": the build makes no lightmap, and the game lights the map with the
    // converter's constant sky fill and the live sun. Nothing to bake; a lightmap of an earlier bake goes.
    if (gameLookUnbaked()) {
        if (gameLm_) {
            gameLm_.reset();
            renderer_.setGameLightmap(0, 0, nullptr, nullptr, nullptr);
            geomDirty_ = true;
            update();
        }
        if (!gameUnbakedTold_) {
            gameUnbakedTold_ = true;
            emit ctx_->statusMessage("Game lighting: this map is built without lightmaps (Compile > Bake lightmaps is off), so the "
                                     "game's flat sky light and the live sun are shown");
        }
        return;
    }
    gameUnbakedTold_ = false;
''')

# --- bench: nothing to wait for on an unbaked map ---------------------------------------------------------------------
M = 'src/qtapp/main.cpp'
patch(M, '''            if (cam->gameLightmap() && !cam->gameBaking()) break;
        }
        const auto lm = cam->gameLightmap();
        report << QString("game_bake ms=%1 lightmap=%2x%3 faces=%4 bake_s=%5 %6").arg(bt.elapsed()).arg(lm ? lm->width : 0)''', '''            if ((cam->gameLightmap() || cam->gameLookUnbaked()) && !cam->gameBaking()) break;
        }
        const auto lm = cam->gameLightmap();
        if (cam->gameLookUnbaked()) report << "game_unbaked 1 (no lightmap in the build: the converter's sky fill and the live sun)";
        report << QString("game_bake ms=%1 lightmap=%2x%3 faces=%4 bake_s=%5 %6").arg(bt.elapsed()).arg(lm ? lm->width : 0)''')

# --- GUI test -------------------------------------------------------------------------------------------------------
T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''        const QImage before = cv->renderImage(320, 180, true, 0, 300);
        if (game) game->trigger();
        r.check(ctx.scene.gameLook && game && game->isChecked(), "Ctrl+Alt+L turns game lighting on (checked)");''', '''        const QImage before = cv->renderImage(320, 180, true, 0, 300);
        // The preview bake belongs to a map with "Bake lightmaps" on (the build then makes the same lightmap).
        const bool bakeKeyWas = iw8r::mapBakeOptions(ctx.doc.worldspawn()).bake;
        if (const Entity* ws = ctx.doc.worldspawn(); ws && !bakeKeyWas) {
            ctx.doc.setKeyValue(ws->id, "iw8r_bake_lightmaps", "1");
            ctx.documentEdited();
        }
        if (game) game->trigger();
        r.check(ctx.scene.gameLook && game && game->isChecked(), "Ctrl+Alt+L turns game lighting on (checked)");''')
patch(T, '''        // The stock world shader's environment BRDF table comes from the install once (ACTS, game_look_assets.h).''', '''        // Without "Bake lightmaps" the build makes no lightmap and the game lights the map with the converter's constant
        // sky fill and the live sun: the view shows that, with no preview bake (research 117).
        {
            const bool bakedHad = cv->gameLightmap() != nullptr && !cv->gameLookUnbaked();
            if (const Entity* ws = ctx.doc.worldspawn()) ctx.doc.removeKey(ws->id, "iw8r_bake_lightmaps");
            ctx.documentEdited();
            QElapsedTimer ut;
            ut.start();
            QImage unbaked;
            while (ut.elapsed() < 10000) {
                unbaked = cv->renderImage(320, 180, true, 0, 0);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                if (!cv->gameLightmap() && cv->gameLookUnbaked()) break;
                QThread::msleep(20);
            }
            unbaked = cv->renderImage(320, 180, true, 0, 300);
            double change = 0, lum = 0;
            size_t px = 0;
            for (int y = 0; y < unbaked.height() && unbaked.size() == after.size(); ++y)
                for (int x = 0; x < unbaked.width(); ++x) {
                    const QRgb a = unbaked.pixel(x, y), b = after.pixel(x, y);
                    change += std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) + std::abs(qBlue(a) - qBlue(b));
                    lum += (qRed(a) + qGreen(a) + qBlue(a)) / 765.0;
                    ++px;
                }
            const double perPixel = px ? change / double(px) : 0, meanU = px ? lum / double(px) : 0;
            r.check(bakedHad && !cv->gameLightmap() && cv->gameLookUnbaked() && perPixel > 0.3 && meanU > 0.02 && meanU < 0.98,
                    QString("a map without Bake lightmaps is shown unbaked: no preview lightmap, the constant sky fill (mean %1, change %2 per pixel)")
                        .arg(meanU, 0, 'f', 3).arg(perPixel, 0, 'f', 1));
            if (!shot.isEmpty()) unbaked.save(QString::fromLocal8Bit(shot).replace(".png", "_unbaked.png"));
            if (bakeKeyWas)
                if (const Entity* ws = ctx.doc.worldspawn()) {
                    ctx.doc.setKeyValue(ws->id, "iw8r_bake_lightmaps", "1");
                    ctx.documentEdited();
                }
        }
        // The stock world shader's environment BRDF table comes from the install once (ACTS, game_look_assets.h).''')
print('ok')
