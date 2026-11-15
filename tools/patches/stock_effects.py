import io

# Research 132 G3: a stock map's own placed effects (fire, smoke, mist, glows: MapEnts clientSideEffects) play in the
# camera with View > Effect Preview, like the mapper's fx_origin entities. When a stock map opens, its effects are
# exported once with ACTS from the map's own zone (in the background); the camera picks them up as they arrive.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/qtapp/camera_view.h'
patch(H, '''    void setEffectSource(EffectSource fn) { effectSource_ = std::move(fn); }''', '''    void setEffectSource(EffectSource fn) { effectSource_ = std::move(fn); }
    // A stock map's own placed effects (MapEnts clientSideEffects, research 132 G3): played with View > Effect Preview
    // like fx_origin entities. Empty for an own map.
    struct StockEffect {
        Vec3 origin;
        std::string name;
    };
    void setStockEffects(std::vector<StockEffect> fx) {
        stockEffects_ = std::move(fx);
        update();
    }
    size_t stockEffectCount() const { return stockEffects_.size(); }
    size_t effectParticles() const { return fxParticles_; }''')
patch(H, '''    EffectSource effectSource_;''', '''    EffectSource effectSource_;
    std::vector<StockEffect> stockEffects_;''')

C = 'src/qtapp/camera_view.cpp'
patch(C, '''    for (const Entity& e : ctx_->doc.map().entities) {
        if (e.classname() != "fx_origin") continue;
        const std::string* fx = e.value("fx");
        const auto o = e.origin();
        if (!fx || !o) continue;
        const auto def = effectSource_(*fx);
        ++seed;
        if (!def || def->emitters.empty()) continue;''', '''    // The mapper's fx_origin entities, then the stock map's own placed effects.
    std::vector<std::pair<Vec3, const std::string*>> placed;
    for (const Entity& e : ctx_->doc.map().entities) {
        if (e.classname() != "fx_origin") continue;
        const std::string* fx = e.value("fx");
        const auto o = e.origin();
        if (fx && o) placed.push_back({*o, fx});
    }
    for (const StockEffect& s : stockEffects_) placed.push_back({s.origin, &s.name});
    const Vec3 eye = ctx_->cam.pos;
    for (const auto& [origin, fx] : placed) {
        const auto o = std::optional<Vec3>(origin);
        ++seed;
        // Far effects are left out (the game culls them by distance too); the 20,000 particle budget goes to near ones.
        if (length(origin - eye) > 12000) continue;
        const auto def = effectSource_(*fx);
        if (!def || def->emitters.empty()) continue;''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''        ctx_.scene.spawns = std::move(dump.spawns);
        stockModelNames_ = entityModelNames(ctx_.doc.map());''', '''        ctx_.scene.spawns = std::move(dump.spawns);
        stockModelNames_ = entityModelNames(ctx_.doc.map());
        {
            // The map's own placed effects (research 132 G3): played by View > Effect Preview. Their definitions are
            // exported once from the map's zone with ACTS (into vfx_cache/<zone>); the camera picks them up after.
            std::vector<CameraView::StockEffect> fx;
            QSet<QString> names;
            for (const auto& e : dump.effects) {
                fx.push_back({e.origin, e.effect});
                names.insert(qs(e.effect));
            }
            camera_->setStockEffects(std::move(fx));
            const QString zone = qs(dump.assetName).section('/', -1).section('.', 0, 0);
            QStringList missing;
            for (const QString& n : names)
                if (findVfxExport(n).isEmpty() && !vfxExporting_.contains(n)) missing << n;
            if (!missing.isEmpty() && !zone.isEmpty()) {
                const QString ownCache = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/vfx_cache";
                for (const QString& n : missing) vfxExporting_.insert(n);
                ActsRequest req;
                req.zone = zone;
                req.types = "vfx";
                req.names = missing;
                req.outDir = ownCache + "/" + zone;
                QDir().mkpath(req.outDir);
                ActsRunner::shared()->submit([this](const QString& k) { return toolPath(k); }, req, this, [this, missing](const ActsResult&) {
                    for (const QString& n : missing) {
                        vfxExporting_.remove(n);
                        fxSims_.erase(n.toStdString());
                    }
                    camera_->update();
                });
            }
        }''')
print('ok')
