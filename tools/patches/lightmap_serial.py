import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/qtapp/main_window.h'
patch(H, '''    bool refProbeReloadQueued_ = false;''', '''    bool refProbeReloadQueued_ = false;
    // One lightmap atlas build at a time (each needs a few hundred MB on Verdansk; streamed tiles finish in bursts):
    // a request while one runs is remembered and run once when it ends.
    bool refLmLoading_ = false, refLmAgain_ = false;''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''    if (paths.empty()) return;
    QPointer<MainWindow> self(this);
    std::thread([self, zone, paths] {
        std::vector<StockLightmap> lms;''', '''    if (paths.empty()) return;
    if (refLmLoading_) {
        refLmAgain_ = true;
        return;
    }
    refLmLoading_ = true;
    QPointer<MainWindow> self(this);
    std::thread([self, zone, paths] {
        std::vector<StockLightmap> lms;''')
patch(M, '''        std::shared_ptr<const app::StockLightmapLayers> layers;
        if (!lms.empty()) layers = app::stockLightmapAtlas(lms);
        QMetaObject::invokeMethod(qApp, [self, zone, layers, err] {
            if (!self || self->referenceZone_ != zone) return;
            if (layers && layers->width > 0) {''', '''        std::shared_ptr<const app::StockLightmapLayers> layers;
        try {
            if (!lms.empty()) layers = app::stockLightmapAtlas(lms);
        } catch (const std::bad_alloc&) {
            err = "out of memory while packing " + std::to_string(lms.size()) + " lightmaps";
            layers.reset();
        }
        lms.clear();
        lms.shrink_to_fit();
        QMetaObject::invokeMethod(qApp, [self, zone, layers, err] {
            if (!self) return;
            self->refLmLoading_ = false;
            if (self->refLmAgain_) {
                self->refLmAgain_ = false;
                if (!self->referenceZone_.isEmpty()) QTimer::singleShot(0, self, [self] {
                    if (self) self->loadStockLightmap(self->referenceZone_);
                });
            }
            if (self->referenceZone_ != zone) return;
            if (layers && layers->width > 0) {''')
print('ok')
