import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


M = 'src/qtapp/main_window.cpp'
patch(M, '''        auto batches = std::make_shared<std::vector<app::ReferenceTexBatch>>(buildTerrainLayerBatches(*st, c.x, c.y, terrainLayerRadius()));
        QMetaObject::invokeMethod(qApp, [self, batches, zone, c] {
            if (!self) return;
            self->terrainLayerBusy_ = false;
            if (self->referenceZone_ != zone) return;''', '''        // Out of memory: the layers drawn now stay (an empty result would take the ground away); the next camera move
        // tries again.
        std::shared_ptr<std::vector<app::ReferenceTexBatch>> batches;
        try {
            batches = std::make_shared<std::vector<app::ReferenceTexBatch>>(buildTerrainLayerBatches(*st, c.x, c.y, terrainLayerRadius()));
        } catch (const std::bad_alloc&) {
            batches.reset();
        }
        QMetaObject::invokeMethod(qApp, [self, batches, zone, c] {
            if (!self) return;
            self->terrainLayerBusy_ = false;
            if (!batches) {
                self->ctx_.setStatus("Not enough memory for the terrain's layers around the camera: the ones drawn stay");
                return;
            }
            if (self->referenceZone_ != zone) return;''')
print('ok')
