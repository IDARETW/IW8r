import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b and s.count(b) >= 1:
        return
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/qtapp/main_window.h'
C = 'src/qtapp/main_window.cpp'

patch(H, '''    void loadStockLightGrid(const QString& zone);''', '''    void loadStockLightGrid(const QString& zone);
    // Warzone: the streamed tiles' light grids (WorldCache::tileLightGrids) not read yet, into refPropTileLight_; the
    // props are rebuilt once they arrive.
    void loadStockTileLightGrids(const QString& zone);''')
patch(H, '''    std::shared_ptr<const std::vector<float>> refPropVolLight_;
    QString refPropVolLightZone_;''', '''    std::shared_ptr<const std::vector<float>> refPropVolLight_;
    QString refPropVolLightZone_;
    // The same per streamed tile (Warzone), by transientZoneIndex (StockModelInstance::worldTransient).
    std::map<int, std::shared_ptr<const std::vector<float>>> refPropTileLight_;
    std::set<int> refPropTileLightAsked_;
    bool refPropRebuildQueued_ = false;''')

# one sampler for both
patch(C, '''void MainWindow::loadStockLightGrid(const QString& zone) {
    const std::optional<QString> file = worldCache_ ? worldCache_->lightGridFile(zone) : std::nullopt;
    if (!file) return;
    QPointer<MainWindow> self(this);
    const QString path = *file;
    std::thread([self, zone, path] {
        std::string err;
        auto vol = std::make_shared<std::vector<float>>();
        bool ok = false;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            err = "cannot read " + path.toStdString();
        } else {
            const QByteArray bytes = f.readAll();
            StockLgvAtlas atlas;
            ok = parseStockLgvBlock(std::string(bytes.constData(), size_t(bytes.size())), atlas, &err);
            if (ok) {
                // Each box at its centre (research 105 section 5.3): exact for 1x1x1 boxes, the mean for big ones.
                const double centre[3] = {0.5, 0.5, 0.5};
                vol->resize(atlas.pack.size() * 12);
                for (size_t v = 0; v < atlas.pack.size(); ++v) {
                    const StockLgvSh sh = sampleStockLgv(atlas, v, centre);
                    float* o = &(*vol)[v * 12];
                    for (int c = 0; c < 3; ++c) {
                        o[c] = sh.dc[c];
                        o[3 + c] = sh.sh[0][c];
                        o[6 + c] = sh.sh[2][c];
                        o[9 + c] = sh.sh[3][c];
                    }
                }
            }
        }
        QMetaObject::invokeMethod(qApp, [self, zone, vol, ok, err] {''', '''namespace {
// A light grid cache file (lightgrid.iw8rlg) to 12 floats per volume: each box at its centre (research 105 section
// 5.3: exact for 1x1x1 boxes, the mean for big ones), DC rgb then the first-order slots 0, 2 and 3.
std::shared_ptr<std::vector<float>> sampleLightGridFile(const QString& path, std::string* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *err = "cannot read " + path.toStdString();
        return nullptr;
    }
    const QByteArray bytes = f.readAll();
    StockLgvAtlas atlas;
    if (!parseStockLgvBlock(std::string(bytes.constData(), size_t(bytes.size())), atlas, err)) return nullptr;
    auto vol = std::make_shared<std::vector<float>>(atlas.pack.size() * 12);
    const double centre[3] = {0.5, 0.5, 0.5};
    for (size_t v = 0; v < atlas.pack.size(); ++v) {
        const StockLgvSh sh = sampleStockLgv(atlas, v, centre);
        float* o = &(*vol)[v * 12];
        for (int c = 0; c < 3; ++c) {
            o[c] = sh.dc[c];
            o[3 + c] = sh.sh[0][c];
            o[6 + c] = sh.sh[2][c];
            o[9 + c] = sh.sh[3][c];
        }
    }
    return vol;
}
}  // namespace

void MainWindow::loadStockTileLightGrids(const QString& zone) {
    if (!worldCache_) return;
    std::vector<std::pair<int, QString>> todo;
    for (const auto& [k, path] : worldCache_->tileLightGrids(zone))
        if (refPropTileLightAsked_.insert(k).second) todo.push_back({k, path});
    if (todo.empty()) return;
    QPointer<MainWindow> self(this);
    std::thread([self, zone, todo] {
        std::vector<std::pair<int, std::shared_ptr<std::vector<float>>>> got;
        for (const auto& [k, path] : todo) {
            std::string err;
            if (auto vol = sampleLightGridFile(path, &err)) got.push_back({k, vol});
        }
        QMetaObject::invokeMethod(qApp, [self, zone, got] {
            if (!self || self->referenceZone_ != zone || got.empty()) return;
            for (const auto& [k, vol] : got) self->refPropTileLight_[k] = vol;
            // Tiles finish one after another: one prop rebuild for the ones that arrive close together.
            if (!self->refPropRebuildQueued_) {
                self->refPropRebuildQueued_ = true;
                QTimer::singleShot(1500, self, [self] {
                    if (!self) return;
                    self->refPropRebuildQueued_ = false;
                    self->rebuildReferenceProps();
                });
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::loadStockLightGrid(const QString& zone) {
    const std::optional<QString> file = worldCache_ ? worldCache_->lightGridFile(zone) : std::nullopt;
    if (!file) return;
    QPointer<MainWindow> self(this);
    const QString path = *file;
    std::thread([self, zone, path] {
        std::string err;
        const std::shared_ptr<std::vector<float>> vol = sampleLightGridFile(path, &err);
        const bool ok = vol != nullptr;
        QMetaObject::invokeMethod(qApp, [self, zone, vol, ok, err] {''')
patch(C, '''            if (worldCache_->lightGridFile(z)) loadStockLightGrid(z);
            else if (!worldCache_->lightGridErrorOf(z).isEmpty()) ctx_.setStatus("Stock prop light grid of " + z + ": " + worldCache_->lightGridErrorOf(z));''',
      '''            if (worldCache_->lightGridFile(z) && !refPropVolLight_) loadStockLightGrid(z);
            else if (!worldCache_->lightGridFile(z) && !worldCache_->lightGridErrorOf(z).isEmpty())
                ctx_.setStatus("Stock prop light grid of " + z + ": " + worldCache_->lightGridErrorOf(z));
            loadStockTileLightGrids(z);''')
patch(C, '''            if (self->refPropVolLightZone_ != zone) self->refPropVolLight_.reset();
            if (self->worldCache_->lightGridFile(zone) && !self->refPropVolLight_) self->loadStockLightGrid(zone);''',
      '''            if (self->refPropVolLightZone_ != zone) {
                self->refPropVolLight_.reset();
                self->refPropTileLight_.clear();
                self->refPropTileLightAsked_.clear();
            }
            if (self->worldCache_->lightGridFile(zone) && !self->refPropVolLight_) self->loadStockLightGrid(zone);
            self->loadStockTileLightGrids(zone);''')
patch(C, '''        refPropVolLight_.reset();
        refPropVolLightZone_.clear();''', '''        refPropVolLight_.reset();
        refPropVolLightZone_.clear();
        refPropTileLight_.clear();
        refPropTileLightAsked_.clear();''')
patch(C, '''        if (!noPropLgv && refPropVolLight_ && (in.modelFlags & 1u) && in.worldTransient == 0 &&
            size_t(in.lightingIndex) < refPropVolLight_->size() / 12) {
            std::memcpy(lit, &(*refPropVolLight_)[size_t(in.lightingIndex) * 12], sizeof(lit));''', '''        const std::vector<float>* grid = nullptr;  // the prop's transient zone's grid: the main zone's, or its tile's
        if (in.worldTransient == 0) {
            grid = refPropVolLight_.get();
        } else if (const auto t = refPropTileLight_.find(int(in.worldTransient)); t != refPropTileLight_.end()) {
            grid = t->second.get();
        }
        if (!noPropLgv && grid && (in.modelFlags & 1u) && size_t(in.lightingIndex) < grid->size() / 12) {
            std::memcpy(lit, &(*grid)[size_t(in.lightingIndex) * 12], sizeof(lit));''')
print('ok')
