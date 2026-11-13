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
      '''// An allocation that fails in an event handler (a paint, a timer, a queued job's answer) is logged and the event
// dropped instead of ending the editor: Windows had about 14 GB committed by other programs while Verdansk loaded, and
// a texture decode on the UI thread ended the session (2026-11-13). The work is tried again on the next event.
class GuardedApplication : public QApplication {
public:
    using QApplication::QApplication;
    bool notify(QObject* receiver, QEvent* event) override {
        try {
            return QApplication::notify(receiver, event);
        } catch (const std::bad_alloc&) {
            static qint64 lastLog = 0;
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - lastLog > 10000) {
                lastLog = now;
                iw8r::qt::EditorLog::instance().add(iw8r::qt::LogLevel::Error, "memory",
                                                    "Out of memory: a step was skipped and will be tried again. Close other programs, or lower "
                                                    "the streamed world radius (stock/streamRadius) or the texture budget.");
            }
            return false;
        }
    }
};

int runViewportBench(iw8r::qt::MainWindow& w, const QString& mapFile, const QString& outDir, const QString& referenceDir) {''')
patch(M, '''    QApplication app(argc, argv);
    iw8r::qt::EditorLog::installQtMessageHandler();  // Qt warnings also go to the Console dock''', '''    GuardedApplication app(argc, argv);
    iw8r::qt::EditorLog::installQtMessageHandler();  // Qt warnings also go to the Console dock''')

MW = 'src/qtapp/main_window.cpp'
patch(MW, '''    const double radius = Settings::instance().value("stock/streamRadius", 20000.0).toDouble();
    const size_t limit = size_t(std::max(1, Settings::instance().value("stock/streamLimit", 96).toInt()));
    const auto wanted = streamedZonesNear(streamZones_, c.x, c.y, radius, limit);
    const auto keepZones = streamedZonesNear(streamZones_, c.x, c.y, radius * 1.3, limit * 2);''', '''    const auto [radius, limit] = streamTileReach();
    const auto wanted = streamedZonesNear(streamZones_, c.x, c.y, radius, limit);
    const auto keepZones = streamedZonesNear(streamZones_, c.x, c.y, radius * 1.15, limit + limit / 4);''')
patch(MW, '''    const double radius = Settings::instance().value("stock/streamRadius", 20000.0).toDouble();
    const size_t limit = size_t(std::max(1, Settings::instance().value("stock/streamLimit", 96).toInt()));
    const auto wanted = streamedZonesNear(streamZones_, ctx_.cam.pos.x, ctx_.cam.pos.y, radius, limit);''', '''    const auto [radius, limit] = streamTileReach();
    const auto wanted = streamedZonesNear(streamZones_, ctx_.cam.pos.x, ctx_.cam.pos.y, radius, limit);''')
patch(MW, '''void MainWindow::updateStreamedTiles() {''', '''// The streamed tiles' reach: the settings, else by the machine's memory (each Verdansk tile is about 20 MB of video
// memory and as much again in the editor; 96 tiles within 20,000 units ran a 16 GB machine out of memory).
std::pair<double, size_t> MainWindow::streamTileReach() const {
    const bool big = CameraView::systemMemoryMB() >= 24 * 1024;
    const double radius = Settings::instance().value("stock/streamRadius", big ? 20000.0 : 14000.0).toDouble();
    const size_t limit = size_t(std::max(1, Settings::instance().value("stock/streamLimit", big ? 96 : 48).toInt()));
    return {radius, limit};
}

void MainWindow::updateStreamedTiles() {''')
patch(MW, '''// Streamed world tiles follow the camera (docs/PLAN_REMAINING.md 1.1): the tiles within stock/streamRadius (20,000)''',
      '''// Streamed world tiles follow the camera (docs/PLAN_REMAINING.md 1.1): the tiles within stock/streamRadius (20,000;
// 14,000 on a machine under 24 GB)''')
patch(MW, '''// the main zone's surface table (buildStockWorldTile) and drawn as world groups of the camera's extras; tiles past 1.3
// times the radius are dropped.''', '''// the main zone's surface table (buildStockWorldTile) and drawn as world groups of the camera's extras; tiles past 1.15
// times the radius are dropped.''')
H = 'src/qtapp/main_window.h'
patch(H, '''    void relightReferenceProps();  // new light grids: only the props' light (falls back to a rebuild)''',
      '''    void relightReferenceProps();  // new light grids: only the props' light (falls back to a rebuild)
    std::pair<double, size_t> streamTileReach() const;  // streamed tiles: radius and count''')
S = 'src/qtapp/main_window_stock_edit.cpp'
patch(S, '''    const double radius = Settings::instance().value("stock/streamRadius", 12000.0).toDouble();
    const int limit = Settings::instance().value("stock/streamLimit", 48).toInt();
    const auto near = streamedZonesNear(zones, ctx_.cam.pos.x, ctx_.cam.pos.y, radius, size_t(std::max(1, limit)));''',
      '''    const auto [radius, limit] = streamTileReach();
    const auto near = streamedZonesNear(zones, ctx_.cam.pos.x, ctx_.cam.pos.y, radius, limit);''')
print('ok')
