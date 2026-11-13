import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


M = 'src/qtapp/main.cpp'
patch(M, '''int runViewportBench(iw8r::qt::MainWindow& w, const QString& mapFile, const QString& outDir, const QString& referenceDir) {''',
      '''// The process's memory now (commit = private bytes, the part that counts against the machine's commit limit), for the
// bench report's memory_at lines.
QString benchMemoryNow(const char* stage) {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return QString("memory_at %1 commit_mb=%2 working_set_mb=%3").arg(stage).arg(pmc.PagefileUsage / 1048576.0, 0, 'f', 0)
            .arg(pmc.WorkingSetSize / 1048576.0, 0, 'f', 0);
#endif
    return QString("memory_at %1 unknown").arg(stage);
}

int runViewportBench(iw8r::qt::MainWindow& w, const QString& mapFile, const QString& outDir, const QString& referenceDir) {''')
patch(M, '''        while (wt.elapsed() < 120000 && cam->referenceTriangles() == 0) {
            cam->renderNow();
            QApplication::processEvents(QEventLoop::AllEvents, 50);
        }''', '''        while (wt.elapsed() < 120000 && cam->referenceTriangles() == 0) {
            cam->renderNow();
            QApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        report << benchMemoryNow("reference_drawn");''')
patch(M, '''        const auto pl = w.propLightStats();''', '''        report << benchMemoryNow("caches_idle");
        const auto pl = w.propLightStats();''')
patch(M, '''        report << QString("streamed_tiles drawn %1 of %2 after %3 ms").arg(w.streamedTilesDrawn()).arg(w.streamedTilesKnown()).arg(st.elapsed());''',
      '''        report << QString("streamed_tiles drawn %1 of %2 after %3 ms").arg(w.streamedTilesDrawn()).arg(w.streamedTilesKnown()).arg(st.elapsed());
        report << benchMemoryNow("tiles_settled");''')
print('ok')
