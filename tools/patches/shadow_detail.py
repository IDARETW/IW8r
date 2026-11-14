import io

# Plan 1.10 (the last open item): View > Game Lighting Options > Sun Shadow Detail. The side of the two sun shadow
# maps (the whole scene and the box around the camera) is a saved setting: 1024, 2048 (as before) or 4096 texels.

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


H = 'src/viewport/render.h'
patch(H, '''    static constexpr unsigned kShadowSize = 2048;
''', '''    unsigned shadowSize_ = 2048;  // the side of both sun shadow maps (setSunShadowMapSize)
''')
patch(H, '''    void setGameAo(bool on) { aoOn_ = on; }
''', '''    void setGameAo(bool on) { aoOn_ = on; }
    // The side of the sun shadow maps in texels (the lit preview and Game Lighting): 1024, 2048 (the default) or 4096.
    // Other values go to the nearest of the three. The maps are made again at the next frame that draws shadows.
    void setSunShadowMapSize(unsigned size);
    unsigned sunShadowMapSize() const { return shadowSize_; }
''')
R = 'src/viewport/render.cpp'
patch(R, '''float shadowPcf(float2 uv, float z, float slice) {
    float s = 0;
''', '''float shadowPcf(float2 uv, float z, float slice) {
    float sw, sh, sn;
    shadowMap.GetDimensions(sw, sh, sn);  // the map's side (View > Game Lighting Options > Sun Shadow Detail)
    float s = 0;
''', 2)
patch(R, '''float3(uv + float2(x, y) / 2048.0, slice), z);''', '''float3(uv + float2(x, y) / sw, slice), z);''', 2)
patch(R, '''        D3D11_VIEWPORT svp{0, 0, float(kShadowSize), float(kShadowSize), 0, 1};''',
      '''        D3D11_VIEWPORT svp{0, 0, float(shadowSize_), float(shadowSize_), 0, 1};''')
patch(R, '''    td.Width = td.Height = kShadowSize;''', '''    td.Width = td.Height = shadowSize_;''')
patch(R, '''        FAILED(dev_->CreateShaderResourceView(shadowTex_, &sd, &shadowSrv_)) || FAILED(dev_->CreateSamplerState(&cs, &cmpSampler_))) {
        shadowFailed_ = true;
        return false;
    }
    dev_->CreateRasterizerState(&rd, &rsShadow_);
''', '''        FAILED(dev_->CreateShaderResourceView(shadowTex_, &sd, &shadowSrv_)) ||
        (!cmpSampler_ && FAILED(dev_->CreateSamplerState(&cs, &cmpSampler_)))) {
        shadowFailed_ = true;
        return false;
    }
    if (!rsShadow_) dev_->CreateRasterizerState(&rd, &rsShadow_);
''')
patch(R, '''bool SceneRenderer::createShadowResources() {''', '''void SceneRenderer::setSunShadowMapSize(unsigned size) {
    size = size < 1536 ? 1024u : (size < 3072 ? 2048u : 4096u);
    if (size == shadowSize_) return;
    shadowSize_ = size;
    // The maps of the old size go; createShadowResources makes the new ones (the sampler and the raster state stay).
    safeRelease(shadowSrv_), safeRelease(shadowDsv_), safeRelease(shadowDsvNear_), safeRelease(shadowTex_);
    shadowFailed_ = false;
    shadowDirty_ = nearDirty_ = true;
}

bool SceneRenderer::createShadowResources() {''')

C = 'src/qtapp/camera_view.h'
patch(C, '''    // View > Game Lighting options (MainWindow keeps them in the settings).
''', '''    // View > Game Lighting options (MainWindow keeps them in the settings).
    void setSunShadowMapSize(unsigned size) { renderer_.setSunShadowMapSize(size); update(); }
    unsigned sunShadowMapSize() const { return renderer_.sunShadowMapSize(); }
''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''        camera_->setGameAoStrength(st.value("game/aoStrength", 1.0).toDouble());
''', '''        camera_->setGameAoStrength(st.value("game/aoStrength", 1.0).toDouble());
        // The side of the sun shadow maps (the whole map and the box around the camera): sharper edges for memory.
        addCommand(gm, "GameSunShadowDetail", "Sun Shadow Detail...", QKeySequence(), [this] {
            const QStringList items{"Low (1024 texels, 8 MB)", "Normal (2048 texels, 32 MB)", "High (4096 texels, 128 MB)"};
            const unsigned cur = camera_->sunShadowMapSize();
            bool ok = false;
            const QString pick = QInputDialog::getItem(this, "Sun Shadow Detail", "The sun's shadow maps (lit preview and Game Lighting):", items,
                                                       cur <= 1024 ? 0 : (cur >= 4096 ? 2 : 1), false, &ok);
            if (!ok) return;
            const int size = 1024 << std::max(0, int(items.indexOf(pick)));
            Settings::instance().setValue("game/sunShadowSize", size);
            camera_->setSunShadowMapSize(unsigned(size));
            ctx_.setStatus(QString("Sun shadow maps: %1 x %1 texels").arg(camera_->sunShadowMapSize()));
        });
        camera_->setSunShadowMapSize(unsigned(std::clamp(st.value("game/sunShadowSize", 2048).toInt(), 1024, 4096)));
''')

T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''        for (const char* id : {"GameAoStrength", "GameExposureOverride", "GameRebake"})
            r.check(w.action(id) != nullptr, QString("%1 is in View > Game Lighting Options").arg(id));
''', '''        for (const char* id : {"GameAoStrength", "GameExposureOverride", "GameRebake", "GameSunShadowDetail"})
            r.check(w.action(id) != nullptr, QString("%1 is in View > Game Lighting Options").arg(id));
        if (CameraView* cv = w.cameraView()) {
            // Sun Shadow Detail: the shadow maps are made again at the new size and the lit view still draws.
            const unsigned was = cv->sunShadowMapSize();
            const bool litWas = ctx.scene.litPreview;
            if (!litWas && w.action("ToggleLitPreview")) w.action("ToggleLitPreview")->trigger();
            cv->setSunShadowMapSize(4096);
            const QImage high = cv->renderImage(320, 180, true, 0, 200);
            const bool highOk = cv->sunShadowMapSize() == 4096 && !high.isNull();
            cv->setSunShadowMapSize(1000);  // to the nearest of the three sizes
            const QImage low = cv->renderImage(320, 180, true, 0, 200);
            const bool lowOk = cv->sunShadowMapSize() == 1024 && !low.isNull();
            cv->setSunShadowMapSize(was);
            cv->renderImage(320, 180, true, 0, 100);
            if (!litWas && w.action("ToggleLitPreview")) w.action("ToggleLitPreview")->trigger();
            r.check(highOk && lowOk && cv->sunShadowMapSize() == was && (was == 1024 || was == 2048 || was == 4096),
                    QString("Sun Shadow Detail changes the shadow maps' size (4096, 1024, back to %1) and the view keeps drawing").arg(was));
        }
''')
print('ok')
