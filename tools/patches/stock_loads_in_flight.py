import io

# The bench's "caches idle" wait (MainWindow::stockCacheIdle) did not wait for the stock lighting reads on worker threads
# (lightmap, probes, probe subset, light grid, tile light grids, the atlas restack). On Verdansk with a warm model cache
# the shots were taken before them: no lightmap, no probes, no light grid atlas (2026-11-15). A counter of reads in
# flight now holds the wait.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if s.count(a) == 0 and b in s:
        return  # already applied
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/qtapp/main_window.h'
patch(H, '''    bool refLmLoading_ = false, refLmAgain_ = false;''',
      '''    bool refLmLoading_ = false, refLmAgain_ = false;
    // Stock lighting reads on worker threads (lightmap, probes, light grids, the atlas restack); stockCacheIdle waits.
    int stockLoadsInFlight_ = 0;''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''    return (!worldCache_ || !worldCache_->busy()) && (!modelCacheStock_ || (modelCacheStock_->lodsPending() == 0 && modelCacheStock_->refreshPending() == 0 && !modelCacheStock_->busy())) && !refPropRebuildQueued_ && !refPropRelightQueued_;''',
      '''    return (!worldCache_ || !worldCache_->busy()) && (!modelCacheStock_ || (modelCacheStock_->lodsPending() == 0 && modelCacheStock_->refreshPending() == 0 && !modelCacheStock_->busy())) && !refPropRebuildQueued_ && !refPropRelightQueued_ &&
           stockLoadsInFlight_ == 0;''')
# Lightmap.
patch(M, '''    refLmLoading_ = true;
    QPointer<MainWindow> self(this);''', '''    refLmLoading_ = true;
    ++stockLoadsInFlight_;
    QPointer<MainWindow> self(this);''')
patch(M, '''            if (!self) return;
            self->refLmLoading_ = false;''', '''            if (!self) return;
            --self->stockLoadsInFlight_;
            self->refLmLoading_ = false;''')
# Probes.
patch(M, '''    const QStringList tiles = worldCache_->tileProbeFiles(zone);
    std::thread(''', '''    const QStringList tiles = worldCache_->tileProbeFiles(zone);
    ++stockLoadsInFlight_;
    std::thread(''')
patch(M, '''        QMetaObject::invokeMethod(qApp, [self, zone, set, index, ok, err] {
            if (!self || self->referenceZone_ != zone) return;''', '''        QMetaObject::invokeMethod(qApp, [self, zone, set, index, ok, err] {
            if (!self) return;
            --self->stockLoadsInFlight_;
            if (self->referenceZone_ != zone) return;''')
# Probe subset.
patch(M, '''    const QString zone = referenceZone_;
    std::thread(guardedWorker(__FILE__, __LINE__, [self, full, index, c, zone] {''', '''    const QString zone = referenceZone_;
    ++stockLoadsInFlight_;
    std::thread(guardedWorker(__FILE__, __LINE__, [self, full, index, c, zone] {''')
patch(M, '''            if (!self || self->referenceZone_ != zone || self->refProbesFull_ != full) return;''',
      '''            if (!self) return;
            --self->stockLoadsInFlight_;
            if (self->referenceZone_ != zone || self->refProbesFull_ != full) return;''')
# Light grid.
patch(M, '''    std::thread(guardedWorker(__FILE__, __LINE__, [self, zone, path] {
        std::string err;
        std::shared_ptr<const StockLgvAtlas> atlas;''', '''    ++stockLoadsInFlight_;
    std::thread(guardedWorker(__FILE__, __LINE__, [self, zone, path] {
        std::string err;
        std::shared_ptr<const StockLgvAtlas> atlas;''')
patch(M, '''        QMetaObject::invokeMethod(qApp, [self, zone, vol, atlas, ok, err] {
            if (!self || self->referenceZone_ != zone) return;''', '''        QMetaObject::invokeMethod(qApp, [self, zone, vol, atlas, ok, err] {
            if (!self) return;
            --self->stockLoadsInFlight_;
            if (self->referenceZone_ != zone) return;''')
# Atlas restack.
patch(M, '''    QPointer<MainWindow> self(this);
    std::thread(guardedWorker(__FILE__, __LINE__, [self, parts, gen, zone] {''', '''    QPointer<MainWindow> self(this);
    ++stockLoadsInFlight_;
    std::thread(guardedWorker(__FILE__, __LINE__, [self, parts, gen, zone] {''')
patch(M, '''            if (!self || self->refPropLgvGen_ != gen || self->referenceZone_ != zone) return;''',
      '''            if (!self) return;
            --self->stockLoadsInFlight_;
            if (self->refPropLgvGen_ != gen || self->referenceZone_ != zone) return;''')
# Tile light grids.
patch(M, '''    std::thread(guardedWorker(__FILE__, __LINE__, [self, zone, todo] {''', '''    ++stockLoadsInFlight_;
    std::thread(guardedWorker(__FILE__, __LINE__, [self, zone, todo] {''')
patch(M, '''            if (!self || self->referenceZone_ != zone || got.empty()) return;''',
      '''            if (!self) return;
            --self->stockLoadsInFlight_;
            if (self->referenceZone_ != zone || got.empty()) return;''')
print('ok')
