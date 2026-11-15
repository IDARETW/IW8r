import io

# Plan 1.10 / Milestone 1 "every feature has a GUI toggle, a saved setting and a test": the stock-map rendering work of
# 2026-11-14/15 had environment switches only. View > Game Lighting Options > Stock Map Detail, each saved (game/...):
#   GTAO (else the stand-in SSAO), props lit from the light grid, prop lightmaps, self-visibility, detail layers,
#   clear coats, model displacement, the thin-wall light push.
# Shader-side parts share an off mask in gameTime.y (cbuffer Game, bound to the props' VS and every PS):
#   1 self-visibility, 2 detail layers (fold; the layer passes are skipped on the CPU), 4 clear coats,
#   8 model displacement, 16 thin push. The environment switches stay as bench overrides.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if s.count(a) == 0 and b in s:
        return  # already applied
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


R = 'src/viewport/render.cpp'
patch(R, '''                              float4 fogK[8]; float4 gameTime; };  // gameTime.x: seconds (fmod 43200, research 133)''',
      '''                              float4 fogK[8]; float4 gameTime; };  // gameTime.x: seconds (fmod 43200, research 133); .y: stock detail off bits''')
patch(R, '''bool lgvLight(uint box, float3 w, float3 n, out float3 e, out float3 back) {''',
      '''// View > Game Lighting Options > Stock Map Detail, the parts turned off: 1 self-visibility, 2 detail layers, 4 clear
// coats, 8 model displacement, 16 the thin-wall light push.
uint stockDetailOff() { return (uint)(gameTime.y + 0.5); }
bool lgvLight(uint box, float3 w, float3 n, out float3 e, out float3 back) {''')
patch(R, '''    if ((flags & 4u) != 0u && axis < 3u) {''', '''    if ((flags & 4u) != 0u && axis < 3u && (stockDetailOff() & 16u) == 0u) {''')
patch(R, '''    if (dispC.x > 0.5 && dot(i.n, i.n) > 1e-12)''', '''    if (dispC.x > 0.5 && dot(i.n, i.n) > 1e-12 && (stockDetailOff() & 8u) == 0u)''')
patch(R, '''    if (i.sv.w > 0.004) {''', '''    if (i.sv.w > 0.004 && (stockDetailOff() & 1u) == 0u) {''')
patch(R, '''        if (stockRef && layerP.x < 0.5 && i.vc.w > 0.5 && foldL[0].x > 0.5) albedo = foldPropLayers(albedo, i);''',
      '''        if (stockRef && layerP.x < 0.5 && i.vc.w > 0.5 && foldL[0].x > 0.5 && (stockDetailOff() & 2u) == 0u) albedo = foldPropLayers(albedo, i);''')
patch(R, '''            if (stockRef && baseMat && i.vc.w > 0.5 && (foldL[0].x > 0.5 || foldL[4].x > 0.5)) {''',
      '''            if (stockRef && baseMat && i.vc.w > 0.5 && (foldL[0].x > 0.5 || foldL[4].x > 0.5) && (stockDetailOff() & 2u) == 0u) {''')
patch(R, '''            if (i.vc.w > 0.5 && coatL[0].x > 0.5 && layerP.x < 0.5) {''',
      '''            if (i.vc.w > 0.5 && coatL[0].x > 0.5 && layerP.x < 0.5 && (stockDetailOff() & 4u) == 0u) {''')
patch(R, '''    g[84] = float(std::fmod(gameTime_, 43200.0));  // the game's clock wraps (research 133 section 0)''',
      '''    g[84] = float(std::fmod(gameTime_, 43200.0));  // the game's clock wraps (research 133 section 0)
    g[85] = float(stockDetailOff_);                  // Stock Map Detail parts turned off (stockDetailOff() in the shaders)''')
patch(R, '''    if (decalPass == 1) drawPropLayers();''', '''    if (decalPass == 1 && (stockDetailOff_ & 2u) == 0u) drawPropLayers();''')
patch(R, '''    const bool gtao = !oldSsao && aoZTex_ && aoZPs_ && aoZDownPs_ && aoPointSampler_ && aoZRtv_[kAoZMips - 1];''',
      '''    const bool gtao = gtaoOn_ && !oldSsao && aoZTex_ && aoZPs_ && aoZDownPs_ && aoPointSampler_ && aoZRtv_[kAoZMips - 1];''')

H = 'src/viewport/render.h'
patch(H, '''    void setGameAo(bool on) { aoOn_ = on; }''', '''    void setGameAo(bool on) { aoOn_ = on; }
    // View > Game Lighting Options > Stock Map Detail: the game's GTAO (else the stand-in SSAO) and the shader parts
    // turned off (1 self-visibility, 2 detail layers, 4 clear coats, 8 model displacement, 16 thin push).
    void setGameAoGtao(bool on) { gtaoOn_ = on; }
    void setStockDetailOff(uint32_t bits) { stockDetailOff_ = bits; }
    uint32_t stockDetailOff() const { return stockDetailOff_; }''')
patch(H, '''    bool aoGtaoDrawn_ = false;''', '''    bool aoGtaoDrawn_ = false;
    bool gtaoOn_ = true;
    uint32_t stockDetailOff_ = 0;''')

CH = 'src/qtapp/camera_view.h'
patch(CH, '''    void setGameAoEnabled(bool on) { renderer_.setGameAo(on); update(); }''',
      '''    void setGameAoEnabled(bool on) { renderer_.setGameAo(on); update(); }
    void setGameAoGtao(bool on) { renderer_.setGameAoGtao(on); update(); }
    void setStockDetailOff(uint32_t bits) { renderer_.setStockDetailOff(bits); update(); }
    uint32_t stockDetailOff() const { return renderer_.stockDetailOff(); }''')

MH = 'src/qtapp/main_window.h'
patch(MH, '''    bool propLodsEnabled_ = true;''', '''    // View > Game Lighting Options > Stock Map Detail: props lit from the light grid / their lightmap (game/...).
    bool propLightGridOn_ = true, propLightmapsOn_ = true;
    uint32_t stockDetailOff_ = 0;  // the shader parts turned off (CameraView::setStockDetailOff)
    bool propLodsEnabled_ = true;''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''    if (!noPropLm && k.lmapPage >= 0 && camera_ && camera_->hasStockLightmap()) {''',
      '''    if (!noPropLm && propLightmapsOn_ && k.lmapPage >= 0 && camera_ && camera_->hasStockLightmap()) {''')
patch(M, '''    if (noPropLgv || !k.probe) return false;''', '''    if (noPropLgv || !propLightGridOn_ || !k.probe) return false;''')
patch(M, '''        addCommand(gm, "GameAoStrength", "Ambient Occlusion Strength...", QKeySequence(), [this] {''',
      '''        {
            // The stock maps' rendering parts decoded from the game (research 105, 133, 135, 136, 137), each saved and
            // on by default; off shows the viewport as it was before that part, for a comparison.
            QMenu* sd = gm->addMenu("Stock Map Detail");
            sd->setObjectName("stockDetailMenu");
            const auto detail = [&](const char* id, const QString& text, const QString& key, std::function<void(bool)> apply) {
                QAction* a = addCommand(sd, id, text, QKeySequence(), [this, id, key, apply] {
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
            const auto bit = [this](uint32_t b) {
                return [this, b](bool on) {
                    stockDetailOff_ = on ? (stockDetailOff_ & ~b) : (stockDetailOff_ | b);
                    camera_->setStockDetailOff(stockDetailOff_);
                };
            };
            const auto relight = [this] {
                if (refProps_) QTimer::singleShot(0, this, [this] { relightReferenceProps(); });
            };
            detail("GameGtao", "Game's Ambient Occlusion (GTAO; off: simple SSAO)", "game/gtao", [this](bool on) { camera_->setGameAoGtao(on); });
            detail("GamePropLightGrid", "Props Lit from the Light Grid", "game/propLightGrid", [this, relight](bool on) {
                if (propLightGridOn_ == on) return;
                propLightGridOn_ = on;
                relight();
            });
            detail("GamePropLightmaps", "Lightmapped Props", "game/propLightmaps", [this, relight](bool on) {
                if (propLightmapsOn_ == on) return;
                propLightmapsOn_ = on;
                relight();
            });
            detail("GameSelfVisibility", "Prop Self-Visibility", "game/selfVisibility", bit(1u));
            detail("GamePropLayers", "Prop Detail Layers (dirt, paint, wear)", "game/propLayers", bit(2u));
            detail("GameClearCoats", "Clear Coats", "game/clearCoats", bit(4u));
            detail("GameModelDisplacement", "Model Displacement", "game/modelDisplacement", bit(8u));
            detail("GameThinPush", "Thin-Wall Light Push", "game/thinPush", bit(16u));
        }
        addCommand(gm, "GameAoStrength", "Ambient Occlusion Strength...", QKeySequence(), [this] {''')

T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''                                                               {"GamePropLods", "game/propLods"}};''',
      '''                                                               {"GamePropLods", "game/propLods"},
                                                               // Stock Map Detail (2026-11-15)
                                                               {"GameGtao", "game/gtao"},
                                                               {"GamePropLightGrid", "game/propLightGrid"},
                                                               {"GamePropLightmaps", "game/propLightmaps"},
                                                               {"GameSelfVisibility", "game/selfVisibility"},
                                                               {"GamePropLayers", "game/propLayers"},
                                                               {"GameClearCoats", "game/clearCoats"},
                                                               {"GameModelDisplacement", "game/modelDisplacement"},
                                                               {"GameThinPush", "game/thinPush"}};''')
patch(T, '''        for (const char* id : {"GameAoStrength", "GameExposureOverride", "GameRebake", "GameSunShadowDetail"})''',
      '''        if (CameraView* cv = w.cameraView()) {
            // Stock Map Detail: a shader part off sets its bit for the shaders, on clears it again.
            const std::pair<const char*, uint32_t> bits[] = {{"GameSelfVisibility", 1u}, {"GamePropLayers", 2u}, {"GameClearCoats", 4u},
                                                             {"GameModelDisplacement", 8u}, {"GameThinPush", 16u}};
            for (const auto& [id, b] : bits) {
                QAction* a = w.action(id);
                if (!a || !a->isChecked()) continue;
                a->trigger();
                const bool off = (cv->stockDetailOff() & b) != 0;
                a->trigger();
                r.check(off && (cv->stockDetailOff() & b) == 0, QString("%1 off sets shader bit %2, on clears it").arg(id).arg(b));
            }
        }
        for (const char* id : {"GameAoStrength", "GameExposureOverride", "GameRebake", "GameSunShadowDetail"})''')
print('ok')
