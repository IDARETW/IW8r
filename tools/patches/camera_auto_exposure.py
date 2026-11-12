import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/qtapp/camera_view.h', [
 ('''    void rebakeGameLighting() { gameBakeRev_ = ~0ull; gameSeenAt_.invalidate(); update(); }''',
  '''    void rebakeGameLighting() { gameBakeRev_ = ~0ull; gameSeenAt_.invalidate(); update(); }
    // The stock map shown as the reference ("" = none): Game Lighting then uses that map's sun and auto exposure
    // (core game_look stockMapLook).
    void setGameStockMap(const std::string& zone) { gameStockMap_ = zone; update(); }
    double gameExposure() const { return gameExposure_; }'''),
 ('''    bool gameBaking_ = false, gameLutReady_ = false, gameWasOn_ = false;''',
  '''    bool gameBaking_ = false, gameLutReady_ = false, gameWasOn_ = false, gameMetering_ = false;
    std::string gameStockMap_;
    double gameExposure_ = 0, gameExposureTarget_ = 0;
    QElapsedTimer gameExposureClock_;'''),
])

patch('src/qtapp/camera_view.cpp', [
 ('''        const Vec3& t = ctx_->scene.sunToward;
        const Vec3& c = ctx_->scene.sunColor;
        g.sunToward[0] = float(t.x), g.sunToward[1] = float(t.y), g.sunToward[2] = float(t.z);
        g.sunColor[0] = float(c.x), g.sunColor[1] = float(c.y), g.sunColor[2] = float(c.z);
        const Vec3 sky = gameSkyOf(ctx_->doc);
        g.sky[0] = float(sky.x), g.sky[1] = float(sky.y), g.sky[2] = float(sky.z);
        g.lutScale = float(gameLutInputScale(10.0));''', '''        const Vec3& t = ctx_->scene.sunToward;
        // A stock map: its own sun (colour x intensity) and its vision's auto exposure; an own map: suncolor and the
        // converter's fixed exposure (iw3 style, EV 10).
        const StockMapLook* stock = gameStockMap_.empty() ? nullptr : stockMapLook(gameStockMap_);
        Vec3 c = ctx_->scene.sunColor;
        GameExposure exposure;
        if (stock) {
            c = Vec3{stock->sunColor[0], stock->sunColor[1], stock->sunColor[2]} * stock->sunIntensity;
            exposure = stock->exposure;
        }
        g.sunToward[0] = float(t.x), g.sunToward[1] = float(t.y), g.sunToward[2] = float(t.z);
        g.sunColor[0] = float(c.x), g.sunColor[1] = float(c.y), g.sunColor[2] = float(c.z);
        const Vec3 sky = gameSkyOf(ctx_->doc);
        g.sky[0] = float(sky.x), g.sky[1] = float(sky.y), g.sky[2] = float(sky.z);
        if (exposure.autoExposure != gameMetering_) {
            gameMetering_ = exposure.autoExposure;
            if (gameMetering_) {
                const std::vector<float> inv = gameTonemapInverseGrey(256);
                renderer_.setGameMetering(inv.data());
            } else {
                renderer_.setGameMetering(nullptr);
            }
            gameExposure_ = 0;
        }
        // Auto exposure: the metered log-average of the frames before (one or two frames late, as the game's own),
        // adapted over time at the vision's speed; until a measurement arrives, the exposure of a mid-grey scene.
        double avg = 0;
        if (renderer_.takeMeteredLuminance(avg)) gameExposureTarget_ = gameExposureFor(exposure, avg);
        if (!exposure.autoExposure || gameExposureTarget_ <= 0)
            gameExposureTarget_ = gameExposureFor(exposure, 0.001 * std::exp2(exposure.midEv - 2.84));
        const double dt = gameExposureClock_.isValid() ? gameExposureClock_.restart() / 1000.0 : 0.0;
        if (!gameExposureClock_.isValid()) gameExposureClock_.start();
        gameExposure_ = gameExposure_ > 0 ? gameAdaptExposure(gameExposure_, gameExposureTarget_, std::min(dt, 0.25), exposure.adaptSpeed)
                                          : gameExposureTarget_;
        if (exposure.autoExposure && std::fabs(std::log(gameExposure_ / gameExposureTarget_)) > 0.01)
            QTimer::singleShot(16, this, [this] { update(); });  // still adapting
        else if (exposure.autoExposure)
            QTimer::singleShot(250, this, [this] { update(); });  // keep metering slowly while the view is still
        g.lutScale = float(gameExposure_ * std::exp2(1.5));'''),
])

patch('src/qtapp/main_window.cpp', [
 ('''            self->stockWorld_ = world;
            self->camera_->setReferenceTextured(std::move(*texBatches));''', '''            self->stockWorld_ = world;
            self->camera_->setReferenceTextured(std::move(*texBatches));
            self->camera_->setGameStockMap(zone.toStdString());'''),
 ('''        camera_->setReferenceTextured({});
        ctx_.referencePlans.clear();
        stockWorld_.reset();''', '''        camera_->setReferenceTextured({});
        camera_->setGameStockMap("");
        ctx_.referencePlans.clear();
        stockWorld_.reset();'''),
])
print('ok')
