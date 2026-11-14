import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


M = 'src/qtapp/main_window.cpp'
patch(M, '''        const auto toggle = [&](const char* id, const QString& text, const QString& key, std::function<void(bool)> apply) {''',
      '''        const auto toggle = [&](const char* id, const QString& text, const QString& key, std::function<void(bool)> apply, bool byDefault = true) {''')
patch(M, '''            a->setCheckable(true);
            const bool on = st.value(key, true).toBool();
            a->setChecked(on);
            apply(on);
        };''', '''            a->setCheckable(true);
            const bool on = st.value(key, byDefault).toBool();
            a->setChecked(on);
            apply(on);
        };''')
patch(M, '''        toggle("GamePostFx", "Bloom and Vignette", "game/postFx", [this](bool on) { camera_->setPostFxEnabled(on); });''',
      '''        toggle("GamePostFx", "Bloom and Vignette", "game/postFx", [this](bool on) { camera_->setPostFxEnabled(on); });
        // Research 109 section 2. Off by default: with the decoded constants it darkens Speedball's sky by a third, which
        // the owner's in-game shot does not show (a scale of its scattered light, cb7[42].y, is not decoded yet).
        toggle("GameVolumetricFog", "Volumetric Fog (uncalibrated)", "game/volumetricFog", [this](bool on) { camera_->setVolumetricFogEnabled(on); },
               false);''')
CH = 'src/qtapp/camera_view.h'
patch(CH, '''    void setPostFxEnabled(bool on) { renderer_.setPostFxEnabled(on); update(); }''', '''    void setPostFxEnabled(bool on) { renderer_.setPostFxEnabled(on); update(); }
    void setVolumetricFogEnabled(bool on) { renderer_.setVolumetricFogEnabled(on); update(); }''')
RH = 'src/viewport/render.h'
patch(RH, '''    void setPostFxEnabled(bool on) { postFxEnabled_ = on; }''', '''    void setPostFxEnabled(bool on) { postFxEnabled_ = on; }
    void setVolumetricFogEnabled(bool on) { volumetricOn_ = on; }''')
patch(RH, '''    ID3D11Buffer* cbVol_ = nullptr;  // b13: the volumetric fog (drawPost)''', '''    ID3D11Buffer* cbVol_ = nullptr;  // b13: the volumetric fog (drawPost)
    bool volumetricOn_ = false;      // View > Game Lighting Options > Volumetric Fog (game/volumetricFog, off by default)''')
R = 'src/viewport/render.cpp'
patch(R, '''        const bool volOn = game_.on && game_.vol[2][2] > 0.5f && depthSrv_ && postFxEnabled_ && invert4(lastViewProj_, cb);''',
      '''        const bool volOn = game_.on && volumetricOn_ && game_.vol[2][2] > 0.5f && depthSrv_ && invert4(lastViewProj_, cb);''')
T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''        for (const char* id : {"GameAoStrength", "GameExposureOverride", "GameRebake"})''', '''        if (QAction* v = w.action("GameVolumetricFog")) {
            r.check(v->isCheckable(), "Volumetric Fog is a Game Lighting option");
            const bool was = v->isChecked();
            v->trigger();
            r.check(Settings::instance().value("game/volumetricFog", false).toBool() == !was, "Volumetric Fog toggles and saves game/volumetricFog");
            v->trigger();
        } else {
            r.check(false, "Volumetric Fog is in View > Game Lighting Options");
        }
        for (const char* id : {"GameAoStrength", "GameExposureOverride", "GameRebake"})''')
print('ok')
