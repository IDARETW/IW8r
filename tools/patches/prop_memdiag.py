import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


# crash_handler.h/.cpp: processCommitMb() (added by hand)
M = 'src/qtapp/main_window.cpp'
patch(M, '''void MainWindow::rebuildReferencePropsUnguarded() {
    if (!refProps_ || !refBaseTris_ || referenceZone_.isEmpty()) return;''', '''void MainWindow::rebuildReferencePropsUnguarded() {
    if (!refProps_ || !refBaseTris_ || referenceZone_.isEmpty()) return;
    // Bench diagnosis (IW8R_BENCH_PROFILE): the memory a rebuild holds, logged at its end.
    static const bool memDiag = std::getenv("IW8R_BENCH_PROFILE") != nullptr;
    const size_t commitAtStart = memDiag ? processCommitMb() : 0;
    size_t loadedModels = 0;''')
patch(M, '''            } else if (auto im = modelCacheStock_->loadModel(qs(in.model))) {
                auto fresh = std::make_shared<RefPropMesh>();''', '''            } else if (auto im = modelCacheStock_->loadModel(qs(in.model))) {
                ++loadedModels;
                auto fresh = std::make_shared<RefPropMesh>();''')
patch(M, '''    lightKeys.resize(texInstanced.size());
    refPropLightKeys_ = std::move(lightKeys);''', '''    lightKeys.resize(texInstanced.size());
    refPropLightKeys_ = std::move(lightKeys);
    if (memDiag) {
        size_t keptMb = 0, keptLodMb = 0, drawMb = 0, rowsMb = 0;
        for (const auto& [name, b] : refPropBuilt_) {
            for (const auto& t : b->tex) keptMb += t.second->size() * sizeof(app::GpuTexVertex);
            for (const auto& l : b->lods)
                for (const auto& t : l.tex) keptLodMb += t.second->size() * sizeof(app::GpuTexVertex);
            keptMb += b->plain.size() * sizeof(app::GpuVertex);
        }
        for (const auto& m : texInstanced) drawMb += m.verts().size() * sizeof(app::GpuTexVertex), rowsMb += (m.rows.size() + m.light.size()) * 4;
        EditorLog::instance().add(LogLevel::Info, "bench",
                                  QString("prop rebuild memory: commit %1 -> %2 MB; kept LOD0 %3 MB, kept LODs %4 MB, drawn meshes "
                                          "%5 MB in %6 meshes, rows+light %7 MB, %8 models read from disk")
                                      .arg(commitAtStart).arg(processCommitMb()).arg(keptMb >> 20).arg(keptLodMb >> 20)
                                      .arg(drawMb >> 20).arg(texInstanced.size()).arg(rowsMb >> 20).arg(loadedModels));
    }''')
print('ok')
