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
M = 'src/qtapp/main.cpp'

patch(H, '''    QString streamedTilesReport() const;  // benchmark: wanted / built / empty / not exported around the camera
private:''', '''    QString streamedTilesReport() const;  // benchmark: wanted / built / empty / not exported around the camera
    // No world cache job (export, lightmap / probes / light grid, tiles) queued or running (benchmark).
    bool stockCacheIdle() const;
    // Textured stock prop placements lit from their light grid box, and all textured placements, in the last rebuild.
    std::array<size_t, 2> propLightStats() const { return {refPropLit_, refPropLitOf_}; }
private:''')
patch(H, '''    std::shared_ptr<const std::vector<float>> refPropVolLight_;
    QString refPropVolLightZone_;
''', '''    std::shared_ptr<const std::vector<float>> refPropVolLight_;
    QString refPropVolLightZone_;
    size_t refPropLit_ = 0, refPropLitOf_ = 0;  // propLightStats
''')
patch(C, '''bool MainWindow::streamedTilesSettled() const {''', '''bool MainWindow::stockCacheIdle() const { return !worldCache_ || !worldCache_->busy(); }

bool MainWindow::streamedTilesSettled() const {''')
patch(C, '''        float lit[12] = {-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        if (refPropVolLight_ && (in.modelFlags & 1u) && in.worldTransient == 0 &&
            size_t(in.lightingIndex) < refPropVolLight_->size() / 12)
            std::memcpy(lit, &(*refPropVolLight_)[size_t(in.lightingIndex) * 12], sizeof(lit));''', '''        float lit[12] = {-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        if (refPropVolLight_ && (in.modelFlags & 1u) && in.worldTransient == 0 &&
            size_t(in.lightingIndex) < refPropVolLight_->size() / 12) {
            std::memcpy(lit, &(*refPropVolLight_)[size_t(in.lightingIndex) * 12], sizeof(lit));
            if (!it->second.tex.empty()) ++refPropLit_;
        }
        if (!it->second.tex.empty()) ++refPropLitOf_;''')
patch(C, '''    size_t meshTris = 0, meshed = 0;
    constexpr size_t kPropTriangleBudget = 4'000'000;''', '''    size_t meshTris = 0, meshed = 0;
    refPropLit_ = refPropLitOf_ = 0;
    constexpr size_t kPropTriangleBudget = 4'000'000;''')
patch(M, '''        report << QString("stock zone %1 opened, reference %2 triangles after %3 ms, stock lightmap %4, probes %5")''', '''        // The world cache's lightmap / probes / light grid job (a decompression; minutes on a zone cached before the
        // light grid existed), then the prop rebuild it starts.
        if (!w.stockCacheIdle()) {
            while (wt.elapsed() < 600000 && !w.stockCacheIdle()) {
                cam->renderNow();
                QApplication::processEvents(QEventLoop::AllEvents, 50);
            }
            QElapsedTimer settle;
            settle.start();
            while (settle.elapsed() < 3000 || cam->texturesPending()) {
                cam->renderNow();
                QApplication::processEvents(QEventLoop::AllEvents, 50);
                if (wt.elapsed() > 660000) break;
            }
        }
        const auto pl = w.propLightStats();
        report << QString("prop_light %1 of %2 textured placements lit from their light grid box").arg(pl[0]).arg(pl[1]);
        report << QString("stock zone %1 opened, reference %2 triangles after %3 ms, stock lightmap %4, probes %5")''')
print('ok')
