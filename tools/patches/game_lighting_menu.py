import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


RH = 'src/viewport/render.h'
R = 'src/viewport/render.cpp'
CH = 'src/qtapp/camera_view.h'
CC = 'src/qtapp/camera_view.cpp'
MH = 'src/qtapp/main_window.h'
MC = 'src/qtapp/main_window.cpp'

# ---------------- renderer: runtime switches (the environment variables stay as bench overrides)
patch(RH, '''    size_t stockLightShadowsDrawn() const { return lightShadowsDrawn_; }''', '''    size_t stockLightShadowsDrawn() const { return lightShadowsDrawn_; }
    // View > Game Lighting switches (saved settings; the IW8R_NO_* environment variables still turn them off for benches).
    void setStockLightsEnabled(bool on) { stockLightsEnabled_ = on; }
    void setLightShadowsEnabled(bool on) { lightShadowsEnabled_ = on; }
    // Debug view of Game Lighting: 0 the lit scene, 1 the AO buffer, 2 the albedo, 3 the reflections mask.
    void setGameDebugView(int v) { gameDebugView_ = v; }
    // The AO strength (research 107 section 4.3: the vision's r_ssaoStrength, 1 on MP maps): ao = lerp(1, ao, s).
    void setGameAoStrength(float s) { gameAoStrength_ = s; }''')
patch(RH, '''    std::vector<iw8r::StockLightShadow> stockLightShadows_;''', '''    std::vector<iw8r::StockLightShadow> stockLightShadows_;
    bool stockLightsEnabled_ = true, lightShadowsEnabled_ = true;
    int gameDebugView_ = 0;
    float gameAoStrength_ = 1.0f;''')
patch(R, '''    if (off || !game_.on || stockLights_.empty() || !M || w_ <= 0 || h_ <= 0) return done();''',
      '''    if (off || !stockLightsEnabled_ || !game_.on || stockLights_.empty() || !M || w_ <= 0 || h_ <= 0) return done();''')
patch(R, '''    if (!noLightShadows && stockLightShadows_.size() == stockLights_.size() && createLightShadowResources()) {''',
      '''    if (!noLightShadows && lightShadowsEnabled_ && stockLightShadows_.size() == stockLights_.size() && createLightShadowResources()) {''')
patch(R, '''            dst[k].packed = (l.packed & ~0xFFu) | (s >= 0 && lightSlots_[size_t(s)].drawn ? uint32_t(s) : 0xFFu);''',
      '''            dst[k].packed = (l.packed & ~0xFFu) | (s >= 0 && lightSlots_[size_t(s)].drawn && lightShadowsEnabled_ ? uint32_t(s) : 0xFFu);''')
patch(R, '''    g[42] = ssrAll ? 2.0f : ssrShow ? 1.0f : 0.0f;''', '''    g[42] = ssrAll ? 2.0f : ssrShow || gameDebugView_ == 3 ? 1.0f : 0.0f;''')
patch(R, '''    g[49] = aoShow ? 1.0f : 0.0f;''', '''    g[49] = aoShow || gameDebugView_ == 1 ? 1.0f : 0.0f;''')
patch(R, '''    g[50] = albedoShow ? 1.0f : 0.0f;''', '''    g[50] = albedoShow || gameDebugView_ == 2 ? 1.0f : 0.0f;
    g[11] = std::clamp(gameAoStrength_, 0.0f, 2.0f);  // gameSunCol.w: the AO strength''')
# the shader: AO strength (gameSunCol.w)
patch(R, '''        const float aoV = gameAo.x > 0.5 ? aoMap.Load(int3(i.pos.xy, 0)) : 1.0;''',
      '''        const float aoV = gameAo.x > 0.5 ? lerp(1.0, aoMap.Load(int3(i.pos.xy, 0)), gameSunCol.w) : 1.0;  // x the AO strength''')

# ---------------- camera: pass-through and the exposure override
patch(CH, '''    size_t stockLightShadowsDrawn() const { return renderer_.stockLightShadowsDrawn(); }''', '''    size_t stockLightShadowsDrawn() const { return renderer_.stockLightShadowsDrawn(); }
    // View > Game Lighting options (MainWindow keeps them in the settings).
    void setGameAoEnabled(bool on) { renderer_.setGameAo(on); update(); }
    void setGameSsrEnabled(bool on) { renderer_.setGameSsr(on); update(); }
    void setStockLightsEnabled(bool on) { renderer_.setStockLightsEnabled(on); update(); }
    void setLightShadowsEnabled(bool on) { renderer_.setLightShadowsEnabled(on); update(); }
    void setGameDebugView(int v) { renderer_.setGameDebugView(v); update(); }
    void setGameAoStrength(double s) { renderer_.setGameAoStrength(float(s)); update(); }
    // A fixed exposure (EV, the game's units) instead of the map's own (auto or its vision's); NaN = the map's.
    void setGameExposureOverride(double ev) { gameExposureOverride_ = ev; gameExposureSnap_ = true; update(); }
    double gameExposureOverride() const { return gameExposureOverride_; }''')
patch(CH, '''    std::shared_ptr<const std::vector<StockLightShadow>> pendingShadows_;''', '''    std::shared_ptr<const std::vector<StockLightShadow>> pendingShadows_;
    double gameExposureOverride_ = std::numeric_limits<double>::quiet_NaN();''')
patch(CC, '''        if (stock && !meterStock) exposure.ev = stock->exposure.midEv, exposure.exposureAdjust = stock->exposure.midAdjust + stock->exposure.exposureAdjust;''',
      '''        if (stock && !meterStock) exposure.ev = stock->exposure.midEv, exposure.exposureAdjust = stock->exposure.midAdjust + stock->exposure.exposureAdjust;
        if (std::isfinite(gameExposureOverride_)) {  // View > Game Lighting > Exposure Override
            exposure.autoExposure = false;
            exposure.ev = gameExposureOverride_, exposure.exposureAdjust = 0;
        }''')

# ---------------- main window: the submenu, saved settings
patch(MC, '''    gameToggle->setCheckable(true);''', '''    gameToggle->setCheckable(true);
    {
        // Plan 1.10: Game Lighting's parts, each saved (game/...). Defaults: everything on, the map's own exposure.
        QMenu* gm = view->addMenu("Game Lighting Options");
        gm->setObjectName("gameLightingMenu");
        auto& st = Settings::instance();
        const auto toggle = [&](const char* id, const QString& text, const QString& key, std::function<void(bool)> apply) {
            QAction* a = addCommand(gm, id, text, QKeySequence(), [this, id, key, apply] {
                QAction* self = action(id);
                const bool on = self && self->isChecked();
                Settings::instance().setValue(key, on);
                apply(on);
                ctx_.cameraMoved();
            });
            a->setCheckable(true);
            const bool on = st.value(key, true).toBool();
            a->setChecked(on);
            apply(on);
        };
        toggle("GameAo", "Ambient Occlusion", "game/ao", [this](bool on) { camera_->setGameAoEnabled(on); });
        toggle("GameSsr", "Screen-Space Reflections", "game/ssr", [this](bool on) { camera_->setGameSsrEnabled(on); });
        toggle("GameStockLights", "Stock Map Lamps", "game/stockLights", [this](bool on) { camera_->setStockLightsEnabled(on); });
        toggle("GameLightShadows", "Lamp Shadows", "game/lightShadows", [this](bool on) { camera_->setLightShadowsEnabled(on); });
        toggle("GamePropLods", "Prop Levels of Detail (as in game)", "game/propLods", [this](bool on) {
            if (propLodsEnabled_ == on) return;
            propLodsEnabled_ = on;
            refPropBuilt_.clear();
            refPropBuiltTris_ = 0;
            if (refProps_) QTimer::singleShot(0, this, [this] { rebuildReferenceProps(); });
        });
        addCommand(gm, "GameAoStrength", "Ambient Occlusion Strength...", QKeySequence(), [this] {
            bool ok = false;
            const double v = QInputDialog::getDouble(this, "Ambient Occlusion Strength",
                                                     "Strength (the game's r_ssaoStrength, 1 on every MP map; 0 = none):",
                                                     Settings::instance().value("game/aoStrength", 1.0).toDouble(), 0.0, 2.0, 2, &ok);
            if (!ok) return;
            Settings::instance().setValue("game/aoStrength", v);
            camera_->setGameAoStrength(v);
        });
        camera_->setGameAoStrength(st.value("game/aoStrength", 1.0).toDouble());
        addCommand(gm, "GameExposureOverride", "Exposure Override...", QKeySequence(), [this] {
            QStringList items{"The map's own (auto exposure / its vision)"};
            for (int ev = 6; ev <= 16; ++ev) items << QString("EV %1").arg(ev);
            const double cur = camera_->gameExposureOverride();
            const int at = std::isfinite(cur) ? std::clamp(int(std::lround(cur)) - 6 + 1, 1, int(items.size()) - 1) : 0;
            bool ok = false;
            const QString pick = QInputDialog::getItem(this, "Exposure Override", "Game Lighting exposure:", items, at, false, &ok);
            if (!ok) return;
            const double ev = pick.startsWith("EV ") ? pick.mid(3).toDouble() : std::numeric_limits<double>::quiet_NaN();
            Settings::instance().setValue("game/exposureEv", std::isfinite(ev) ? QVariant(ev) : QVariant());
            camera_->setGameExposureOverride(ev);
        });
        if (const QVariant ev = st.value("game/exposureEv"); ev.isValid() && !ev.isNull())
            camera_->setGameExposureOverride(ev.toDouble());
        addCommand(gm, "GameRebake", "Re-bake Lighting Now", QKeySequence(), [this] {
            camera_->rebakeGameLighting();
            ctx_.setStatus("Game Lighting: the preview bake runs again");
        });
        QMenu* dbg = gm->addMenu("Debug View");
        const QStringList views{"Lit Scene", "Ambient Occlusion Buffer", "Albedo", "Reflections Mask"};
        auto* group = new QActionGroup(dbg);
        const int savedView = st.value("game/debugView", 0).toInt();
        for (int v = 0; v < views.size(); ++v) {
            QAction* a = dbg->addAction(views[v], this, [this, v] {
                Settings::instance().setValue("game/debugView", v);
                camera_->setGameDebugView(v);
            });
            a->setCheckable(true);
            a->setChecked(v == savedView);
            group->addAction(a);
        }
        camera_->setGameDebugView(savedView);
    }''')
# Game Lighting itself is remembered
patch(MC, '''        ctx_.scene.gameLook = !ctx_.scene.gameLook;
        if (QAction* a = action("ToggleGameLighting")) a->setChecked(ctx_.scene.gameLook);''', '''        ctx_.scene.gameLook = !ctx_.scene.gameLook;
        if (QAction* a = action("ToggleGameLighting")) a->setChecked(ctx_.scene.gameLook);
        Settings::instance().setValue("view/gameLighting", ctx_.scene.gameLook);''')
patch(MH, '''    QList<QPair<QString, int>> wantLods_;''', '''    QList<QPair<QString, int>> wantLods_;
    bool propLodsEnabled_ = true;  // View > Game Lighting Options > Prop Levels of Detail (game/propLods)''')
patch(MC, '''                    static const bool noLods = std::getenv("IW8R_NO_PROP_LODS") != nullptr;''',
      '''                    const bool noLods = std::getenv("IW8R_NO_PROP_LODS") != nullptr || !propLodsEnabled_;''')
print('ok')
