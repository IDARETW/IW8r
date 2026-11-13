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

patch(H, '''    // The reference zone's reflection probes (world cache probes.iw8rprb) into the camera, read off the UI thread.
    void loadStockProbes(const QString& zone);''', '''    // The reference zone's reflection probes (world cache probes.iw8rprb) into the camera, read off the UI thread.
    void loadStockProbes(const QString& zone);
    // The reference zone's static-model light grid (world cache lightgrid.iw8rlg, research 105): each box's light at
    // its centre into refPropVolLight_, read off the UI thread; the props are rebuilt with it.
    void loadStockLightGrid(const QString& zone);''')
patch(H, '''    std::map<std::string, std::shared_ptr<const RefPropMesh>> refPropBuilt_;
''', '''    std::map<std::string, std::shared_ptr<const RefPropMesh>> refPropBuilt_;
    // 12 floats per light grid volume of the reference zone's main transient zone (app::InstancedTexMesh::light: DC rgb,
    // then the first-order slots 0, 2 and 3), null when not read; refPropVolLightZone_ is its zone.
    std::shared_ptr<const std::vector<float>> refPropVolLight_;
    QString refPropVolLightZone_;
''')

patch(C, '''#include "stock_smodels.h"''', '''#include "stock_smodels.h"
#include "stock_lightgrid.h"''')

# the per-volume light, read off the UI thread
patch(C, '''void MainWindow::loadStockReference(const QString& zone) {
    if (!worldCache_) {''', '''void MainWindow::loadStockLightGrid(const QString& zone) {
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
        QMetaObject::invokeMethod(qApp, [self, zone, vol, ok, err] {
            if (!self || self->referenceZone_ != zone) return;
            if (!ok) {
                self->ctx_.setStatus("Stock prop light grid of " + zone + " not read: " + QString::fromStdString(err));
                return;
            }
            self->refPropVolLight_ = vol;
            self->refPropVolLightZone_ = zone;
            self->rebuildReferenceProps();
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::loadStockReference(const QString& zone) {
    if (!worldCache_) {''')
patch(C, '''        connect(worldCache_, &WorldCache::lightmapReady, this, [this](const QString& z) {
            if (z != referenceZone_) return;''', '''        connect(worldCache_, &WorldCache::lightGridReady, this, [this](const QString& z) {
            if (z != referenceZone_) return;
            if (worldCache_->lightGridFile(z)) loadStockLightGrid(z);
            else if (!worldCache_->lightGridErrorOf(z).isEmpty()) ctx_.setStatus("Stock prop light grid of " + z + ": " + worldCache_->lightGridErrorOf(z));
        });
        connect(worldCache_, &WorldCache::lightmapReady, this, [this](const QString& z) {
            if (z != referenceZone_) return;''')
patch(C, '''            if (self->worldCache_->probesFile(zone)) self->loadStockProbes(zone);
            const bool needLm = !haveLm && self->worldCache_->lightmapErrorOf(zone).isEmpty();
            const bool needProbes = !self->worldCache_->probesFile(zone) && self->worldCache_->probesErrorOf(zone).isEmpty();
            if (needLm || needProbes) self->worldCache_->requestLightmap(zone);''', '''            if (self->worldCache_->probesFile(zone)) self->loadStockProbes(zone);
            if (self->refPropVolLightZone_ != zone) self->refPropVolLight_.reset();
            if (self->worldCache_->lightGridFile(zone) && !self->refPropVolLight_) self->loadStockLightGrid(zone);
            if (self->worldCache_->needsLightmapJob(zone)) self->worldCache_->requestLightmap(zone);''')
patch(C, '''        camera_->setStockProbes(nullptr);
        ctx_.referencePlans.clear();''', '''        camera_->setStockProbes(nullptr);
        refPropVolLight_.reset();
        refPropVolLightZone_.clear();
        ctx_.referencePlans.clear();''')

# each textured prop instance carries its box's light
patch(C, '''        for (const int ti : it->second.tex) {
            auto& dst = texInstanced[size_t(ti)].rows;
            dst.insert(dst.end(), rows, rows + 12);
        }
        ++meshed;''', '''        // Research 105: a LIGHTPROBE prop of the main transient zone takes its light grid box (12 floats, DC first);
        // every other placement gets DC r = -1 (the probes' fallback in the shader).
        float lit[12] = {-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        if (refPropVolLight_ && (in.modelFlags & 1u) && in.worldTransient == 0 &&
            size_t(in.lightingIndex) < refPropVolLight_->size() / 12)
            std::memcpy(lit, &(*refPropVolLight_)[size_t(in.lightingIndex) * 12], sizeof(lit));
        for (const int ti : it->second.tex) {
            auto& dst = texInstanced[size_t(ti)].rows;
            dst.insert(dst.end(), rows, rows + 12);
            auto& ld = texInstanced[size_t(ti)].light;
            ld.insert(ld.end(), lit, lit + 12);
        }
        ++meshed;''')
print('ok')
