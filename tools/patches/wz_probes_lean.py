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

patch(H, '''    std::shared_ptr<const StockProbeSet> refProbesFull_;''', '''    std::shared_ptr<const StockProbeSet> refProbesFull_;      // records only (no images)
    std::shared_ptr<const StockProbeImageIndex> refProbesIndex_;  // where each probe's image is on disk''')
patch(C, '''    std::thread([self, zone, path, tiles] {
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
            } else {''', '''    std::thread([self, zone, path, tiles] {
        auto set = std::make_shared<StockProbeSet>();
        auto index = std::make_shared<StockProbeImageIndex>();
        std::string err;
        // Warzone (v2): the records and where each image lies (main file, then each read tile's file); the images stay
        // on disk (160+ MB on Verdansk) and only the subset's are read. MP (v1): everything, as the GPU takes it.
        bool ok = readStockProbeCacheRecords(path, *set, &err);
        if (ok && set->streamed) {
            ok = indexStockProbeCacheImages(path, set->probes.size(), *index, &err);
            for (const QString& t : tiles) indexStockProbeCacheImages(t.toStdString(), set->probes.size(), *index);
        } else if (ok) {
            ok = readStockProbeCache(path, *set, &err, false);
        }
        QMetaObject::invokeMethod(qApp, [self, zone, set, index, ok, err] {
            if (!self || self->referenceZone_ != zone) return;
            if (!ok) {
                self->ctx_.setStatus("Stock reflection probes of " + zone + " not read: " + QString::fromStdString(err));
            } else if (set->streamed) {
                self->refProbesFull_ = set;
                self->refProbesIndex_ = index;
                self->updateProbeSubset(true);
            } else {''')
patch(C, '''    const std::shared_ptr<const StockProbeSet> full = refProbesFull_;
    const QString zone = referenceZone_;
    std::thread([self, full, c, zone] {
        auto subset = std::make_shared<StockProbeSet>(buildStockProbeSubset(*full, ProbeVec3{float(c.x), float(c.y), float(c.z)}));''',
      '''    const std::shared_ptr<const StockProbeSet> full = refProbesFull_;
    const std::shared_ptr<const StockProbeImageIndex> index = refProbesIndex_;
    const QString zone = referenceZone_;
    std::thread([self, full, index, c, zone] {
        auto subset = std::make_shared<StockProbeSet>(index ? buildStockProbeSubset(*full, *index, ProbeVec3{float(c.x), float(c.y), float(c.z)})
                                                            : buildStockProbeSubset(*full, ProbeVec3{float(c.x), float(c.y), float(c.z)}));''')
patch(C, '''        refProbesFull_.reset();
        refPropVolLight_.reset();''', '''        refProbesFull_.reset();
        refProbesIndex_.reset();
        refPropVolLight_.reset();''')
patch(C, '''            self->refProbesFull_.reset();
            self->refProbeSubsetAt_ = Vec3{1e30, 1e30, 1e30};''', '''            self->refProbesFull_.reset();
            self->refProbesIndex_.reset();
            self->refProbeSubsetAt_ = Vec3{1e30, 1e30, 1e30};''')
patch(C, '''            } else {
                self->refProbesFull_.reset();
                self->camera_->setStockProbes(set);''', '''            } else {
                self->refProbesFull_.reset();
                self->refProbesIndex_.reset();
                self->camera_->setStockProbes(set);''')
print('ok')
