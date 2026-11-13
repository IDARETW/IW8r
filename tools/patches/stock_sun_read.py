import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:90])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


# ---------------- core: nearest shipped look by sun intensity
patch('src/core/game_look.h', [
 ('''// The known shipped maps; null for others.
const StockMapLook* stockMapLook(const std::string& map);''', '''// The known shipped maps; null for others.
const StockMapLook* stockMapLook(const std::string& map);
// The shipped map whose sun intensity is nearest (in log terms) to `sunIntensity`: the converter's own way of giving a
// map an exposure (iw3_vision.cpp MatchIw8Lighting). mp_br_tut's sun equals mp_m_speedball's, for example.
const StockMapLook& stockMapLookNearest(double sunIntensity);'''),
])
patch('src/core/game_look.cpp', [
 ('''double srgbToLinear(double c) {''', '''const StockMapLook& stockMapLookNearest(double sunIntensity) {
    const auto& all = stockMapLooks();
    size_t best = 0;
    double bestD = 1e300;
    for (size_t k = 0; k < all.size(); ++k) {
        const double d = std::fabs(std::log(std::max(sunIntensity, 1e-3) / all[k].sunIntensity));
        if (d < bestD) bestD = d, best = k;
    }
    return all[best];
}

double srgbToLinear(double c) {'''),
])
patch('tests/game_look_tests.cpp', [
 ('''    if (!ship) return;
    // At the mid EV's luminance''', '''    if (!ship) return;
    CHECK(std::string(stockMapLookNearest(30.67234).map) == "mp_m_speedball" && std::string(stockMapLookNearest(8.5).map) == "mp_shipment");
    // At the mid EV's luminance'''),
])

# ---------------- camera: the stock sun read from the zone
patch('src/qtapp/camera_view.h', [
 ('''    void setGameStockMap(const std::string& zone) {
        if (zone != gameStockMap_) gameBakeRev_ = ~0ull;  // the preview bake follows that map's sun
        gameStockMap_ = zone;
        update();
    }''', '''    void setGameStockMap(const std::string& zone) {
        if (zone != gameStockMap_) gameBakeRev_ = ~0ull, stockSunKnown_ = false;  // the preview bake follows that map's sun
        gameStockMap_ = zone;
        update();
    }
    // The stock map's own sun (its ComWorld sun light: intensity, linear colour, direction toward the sun).
    void setGameStockSun(double intensity, const Vec3& color, const Vec3& toward) {
        stockSunKnown_ = intensity > 0;
        stockSunIntensity_ = intensity, stockSunColor_ = color, stockSunToward_ = toward;
        gameBakeRev_ = ~0ull;
        update();
    }'''),
 ('''    std::string gameStockMap_;''', '''    std::string gameStockMap_;
    bool stockSunKnown_ = false;
    double stockSunIntensity_ = 0;
    Vec3 stockSunColor_{1, 1, 1}, stockSunToward_{0, 0, 1};
    // The stock map's look for Game Lighting: its own entry, else the shipped map with the nearest sun.
    const StockMapLook* gameStockLook() const;
    Vec3 gameSunToward() const;'''),
])
patch('src/qtapp/camera_view.cpp', [
 ('''        const Vec3& t = ctx_->scene.sunToward;
        // A stock map: its own sun (colour x intensity) and its vision's auto exposure; an own map: suncolor and the
        // converter's fixed exposure (iw3 style, EV 10).
        const StockMapLook* stock = gameStockMap_.empty() ? nullptr : stockMapLook(gameStockMap_);
        Vec3 c = ctx_->scene.sunColor;
        GameExposure exposure;
        if (stock) {
            c = Vec3{stock->sunColor[0], stock->sunColor[1], stock->sunColor[2]} * stock->sunIntensity;
            exposure = stock->exposure;
        }''', '''        const Vec3 t = gameSunToward();
        // A stock map: its own sun (read from its ComWorld, else the shipped table) and the exposure of its vision (or
        // of the shipped map with the nearest sun); an own map: suncolor and the converter's fixed exposure (EV 10).
        const StockMapLook* stock = gameStockLook();
        Vec3 c = ctx_->scene.sunColor;
        GameExposure exposure;
        if (stock) {
            c = stockSunKnown_ ? stockSunColor_ * stockSunIntensity_
                               : Vec3{stock->sunColor[0], stock->sunColor[1], stock->sunColor[2]} * stock->sunIntensity;
            exposure = stock->exposure;
        }'''),
 ('''    if (const StockMapLook* stock = gameStockMap_.empty() ? nullptr : stockMapLook(gameStockMap_)) {
        in.scene.sunColor = Vec3{stock->sunColor[0], stock->sunColor[1], stock->sunColor[2]};
        in.scene.sunIntensity = stock->sunIntensity;
        in.scene.skyIntensity = stock->sunIntensity;''', '''    if (const StockMapLook* stock = gameStockLook()) {
        in.scene.sunColor = stockSunKnown_ ? stockSunColor_ : Vec3{stock->sunColor[0], stock->sunColor[1], stock->sunColor[2]};
        in.scene.sunIntensity = stockSunKnown_ ? stockSunIntensity_ : stock->sunIntensity;
        in.scene.skyIntensity = in.scene.sunIntensity;
        in.scene.sunDir = gameSunToward();'''),
 ('''            const double t[3] = {ctx_->scene.sunToward.x, ctx_->scene.sunToward.y, ctx_->scene.sunToward.z};''',
  '''            const Vec3 st = gameLookOn() ? gameSunToward() : ctx_->scene.sunToward;
            const double t[3] = {st.x, st.y, st.z};'''),
 ('''bool CameraView::gameLookOn() const { return ctx_->scene.gameLook && ctx_->texturedView && !ctx_->scene.captureMode; }''',
  '''bool CameraView::gameLookOn() const { return ctx_->scene.gameLook && ctx_->texturedView && !ctx_->scene.captureMode; }

const StockMapLook* CameraView::gameStockLook() const {
    if (gameStockMap_.empty()) return nullptr;
    if (const StockMapLook* l = stockMapLook(gameStockMap_)) return l;
    return stockSunKnown_ ? &stockMapLookNearest(stockSunIntensity_) : nullptr;
}

Vec3 CameraView::gameSunToward() const {
    return !gameStockMap_.empty() && stockSunKnown_ ? stockSunToward_ : ctx_->scene.sunToward;
}'''),
])

# ---------------- main window: read the stock sun when the reference loads
patch('src/qtapp/main_window.h', [
 ('''    void loadStockProbes(const QString& zone);''', '''    void loadStockProbes(const QString& zone);
    // The stock map's sun (its ComWorld sun light, through ACTS once; cached beside the world export) into the camera.
    void loadStockSun(const QString& zone);'''),
])
patch('src/qtapp/main_window.cpp', [
 ('''            self->camera_->setStockLightmap(nullptr);
            self->camera_->setStockProbes(nullptr);''', '''            self->camera_->setStockLightmap(nullptr);
            self->camera_->setStockProbes(nullptr);
            self->loadStockSun(zone);'''),
 ('''void MainWindow::loadStockProbes(const QString& zone) {''', '''void MainWindow::loadStockSun(const QString& zone) {
    const QString acts = toolPath("acts"), replay = toolPath("replay"), oodle = toolPath("oodle");
    const QString ff = QFileInfo(replay).absolutePath() + "/zone/" + zone + ".ff";
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/world_cache/" + zone + "/com_map";
    QPointer<MainWindow> self(this);
    std::thread([self, zone, acts, replay, oodle, ff, dir] {
        const auto findJson = [&] {
            for (QDirIterator it(dir, {"*.json"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
                const QString f = it.next();
                if (f.contains("/assets/com_map/")) return f;
            }
            return QString();
        };
        QString json = findJson();
        if (json.isEmpty() && QFileInfo::exists(acts) && QFileInfo::exists(ff)) {
            QProcess p;
            p.start(acts, {"--noUpdater", "fastfile", "-r", "mw19replay", "-g", replay, "--oodle", oodle, "-a", "com_map", "-l", "-o", dir, ff});
            p.waitForFinished(120000);
            json = findJson();
        }
        double intensity = 0;
        Vec3 color{1, 1, 1}, toward{0, 0, 1};
        QFile f(json);
        if (!json.isEmpty() && f.open(QIODevice::ReadOnly)) {
            const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
            const QJsonArray lights = root["asset"].toObject()["fields"].toObject()["primaryLights"].toObject()["values"].toArray();
            for (const QJsonValue& lv : lights) {
                const QJsonObject l = lv.toObject();
                if (l["type"].toInt() != 1) continue;  // the sun
                intensity = l["intensity"].toDouble();
                const QJsonArray c = l["colorLinearSrgb"].toObject()["v"].toArray(), d = l["dir"].toObject()["v"].toArray();
                if (c.size() == 3) color = Vec3{c[0].toDouble(), c[1].toDouble(), c[2].toDouble()};
                if (d.size() == 3) toward = normalize(Vec3{d[0].toDouble(), d[1].toDouble(), d[2].toDouble()});
                break;
            }
        }
        QMetaObject::invokeMethod(qApp, [self, zone, intensity, color, toward] {
            if (!self || self->referenceZone_ != zone || intensity <= 0) return;
            self->camera_->setGameStockSun(intensity, color, toward);
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::loadStockProbes(const QString& zone) {'''),
])
for inc in ('#include <QJsonArray>', '#include <QJsonDocument>', '#include <QJsonObject>', '#include <QDirIterator>', '#include <QProcess>'):
    s = io.open('src/qtapp/main_window.cpp', encoding='utf-8', newline='').read()
    if inc not in s:
        i = s.index('#include <')
        s = s[:i] + inc + '\n' + s[i:]
        io.open('src/qtapp/main_window.cpp', 'w', encoding='utf-8', newline='').write(s)
print('ok')
