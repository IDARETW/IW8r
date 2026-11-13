import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/qtapp/main_window.h'
C = 'src/qtapp/main_window.cpp'

patch(H, '''    void loadStockProbes(const QString& zone);''', '''    void loadStockProbes(const QString& zone);
    // Warzone probes (research 108): the full set (records, shared, zone-0 and tile images) stays here; the renderer
    // gets the boxes nearest the camera (buildStockProbeSubset, 128 at most), again after 1,500 units of movement.
    void updateProbeSubset(bool force = false);
    std::shared_ptr<const StockProbeSet> refProbesFull_;
    Vec3 refProbeSubsetAt_{1e30, 1e30, 1e30};
    bool refProbeReloadQueued_ = false;''')
patch(C, '''void MainWindow::loadStockProbes(const QString& zone) {
    const std::optional<QString> file = worldCache_ ? worldCache_->probesFile(zone) : std::nullopt;
    if (!file) return;
    QPointer<MainWindow> self(this);
    const std::string path = file->toStdString();
    std::thread([self, zone, path] {
        auto set = std::make_shared<StockProbeSet>();
        std::string err;
        const bool ok = readStockProbeCache(path, *set, &err, false);  // the GPU takes the BC6H blocks as they are
        QMetaObject::invokeMethod(qApp, [self, zone, set, ok, err] {
            if (!self || self->referenceZone_ != zone) return;
            if (ok) self->camera_->setStockProbes(set);
            else self->ctx_.setStatus("Stock reflection probes of " + zone + " not read: " + QString::fromStdString(err));
        }, Qt::QueuedConnection);
    }).detach();
}''', '''void MainWindow::loadStockProbes(const QString& zone) {
    const std::optional<QString> file = worldCache_ ? worldCache_->probesFile(zone) : std::nullopt;
    if (!file) return;
    QPointer<MainWindow> self(this);
    const std::string path = file->toStdString();
    const QStringList tiles = worldCache_->tileProbeFiles(zone);
    std::thread([self, zone, path, tiles] {
        auto set = std::make_shared<StockProbeSet>();
        std::string err;
        const bool ok = readStockProbeCache(path, *set, &err, false);  // the GPU takes the BC6H blocks as they are
        if (ok && set->streamed)
            for (const QString& t : tiles) {  // Warzone: each read tile's own images
                StockProbeImages imgs;
                if (readStockProbeTileCache(t.toStdString(), imgs)) mergeStockProbeImages(*set, imgs);
            }
        QMetaObject::invokeMethod(qApp, [self, zone, set, ok, err] {
            if (!self || self->referenceZone_ != zone) return;
            if (!ok) {
                self->ctx_.setStatus("Stock reflection probes of " + zone + " not read: " + QString::fromStdString(err));
            } else if (set->streamed) {
                self->refProbesFull_ = set;
                self->updateProbeSubset(true);
            } else {
                self->refProbesFull_.reset();
                self->camera_->setStockProbes(set);
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::updateProbeSubset(bool force) {
    if (!refProbesFull_) return;
    const Vec3 c = ctx_.cam.pos;
    if (!force && length(c - refProbeSubsetAt_) < 1500.0) return;
    refProbeSubsetAt_ = c;
    QPointer<MainWindow> self(this);
    const std::shared_ptr<const StockProbeSet> full = refProbesFull_;
    const QString zone = referenceZone_;
    std::thread([self, full, c, zone] {
        auto subset = std::make_shared<StockProbeSet>(buildStockProbeSubset(*full, ProbeVec3{float(c.x), float(c.y), float(c.z)}));
        QMetaObject::invokeMethod(qApp, [self, subset, full, zone] {
            if (!self || self->referenceZone_ != zone || self->refProbesFull_ != full) return;
            self->camera_->setStockProbes(subset);
        }, Qt::QueuedConnection);
    }).detach();
}''')
patch(C, '''            if (worldCache_->probesFile(z)) loadStockProbes(z);''', '''            // Warzone tiles add their images one after another: one reload for a burst.
            if (worldCache_->probesFile(z) && !refProbeReloadQueued_) {
                refProbeReloadQueued_ = true;
                QTimer::singleShot(refProbesFull_ ? 3000 : 0, this, [this, z] {
                    refProbeReloadQueued_ = false;
                    if (z == referenceZone_) loadStockProbes(z);
                });
            }''')
patch(C, '''void MainWindow::updateStreamedTiles() {
    if (streamZones_.empty() || !worldCache_ || !stockWorld_ || !stockWorld_->surfaceTable || referenceZone_.isEmpty()) return;''',
      '''void MainWindow::updateStreamedTiles() {
    updateProbeSubset();  // Warzone probes follow the camera too
    if (streamZones_.empty() || !worldCache_ || !stockWorld_ || !stockWorld_->surfaceTable || referenceZone_.isEmpty()) return;''')
patch(C, '''        camera_->setStockLocalLights(nullptr, nullptr);
        refPropVolLight_.reset();''', '''        camera_->setStockLocalLights(nullptr, nullptr);
        refProbesFull_.reset();
        refPropVolLight_.reset();''')
patch(C, '''            self->camera_->setStockProbes(nullptr);
            self->camera_->setStockLocalLights(nullptr, nullptr);''', '''            self->camera_->setStockProbes(nullptr);
            self->camera_->setStockLocalLights(nullptr, nullptr);
            self->refProbesFull_.reset();
            self->refProbeSubsetAt_ = Vec3{1e30, 1e30, 1e30};''')
print('ok')
