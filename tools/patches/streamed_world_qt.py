import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/qtapp/world_cache.h', [
 ('''    // Parses the cached export. nullopt with err set when the zone is not cached.
    std::optional<StockWorld> load(const QString& zone, QString* err = nullptr) const;
''', '''    // Parses the cached export. nullopt with err set when the zone is not cached.
    std::optional<StockWorld> load(const QString& zone, QString* err = nullptr) const;

    // Big maps (research 90 / 87 section 1.2): exports the streamed world tiles `tiles` (zone names such as
    // mp_donetsk_00587_tr) of a cached zone into <cache>/<zone>/streamed/<tile>, one ACTS run each, skipping those
    // already there; load() then includes them. streamedReady(zone) when the list is done.
    void requestStreamed(const QString& zone, const QStringList& tiles);
    int streamedPending() const { return int(streamQueue_.size()) + (streamRunning_ ? 1 : 0); }
'''),
 ('''    void ready(const QString& zone);  // lookup() or errorOf() now answers for this zone
''', '''    void ready(const QString& zone);  // lookup() or errorOf() now answers for this zone
    void streamedReady(const QString& zone, int loaded, int failed);
'''),
 ('''    bool running_ = false;
    QElapsedTimer runTimer_;
''', '''    bool running_ = false;
    QElapsedTimer runTimer_;
    // Streamed tiles: (zone, tile) still to export.
    void startNextStreamed();
    std::deque<std::pair<QString, QString>> streamQueue_;
    bool streamRunning_ = false;
    int streamLoaded_ = 0, streamFailed_ = 0;
'''),
])
patch('src/qtapp/world_cache.cpp', [
 ('''// ---------------------------------------------------------------------------------------------------------------
// Headless test (main.cpp --world-cache-test)
''', '''void WorldCache::requestStreamed(const QString& zone, const QStringList& tiles) {
    const auto dir = lookup(zone);
    if (!dir) return;
    for (const QString& t : tiles) {
        static const QRegularExpression safe("^[a-z0-9_]+_tr$");
        if (!safe.match(t).hasMatch()) continue;
        if (QFileInfo(*dir + "/streamed/" + t).isDir()) continue;
        bool queued = false;
        for (const auto& q : streamQueue_) queued |= q.second == t;
        if (!queued) streamQueue_.push_back({zone, t});
    }
    if (streamQueue_.empty() && !streamRunning_) {
        emit streamedReady(zone, 0, 0);
        return;
    }
    startNextStreamed();
}

void WorldCache::startNextStreamed() {
    if (streamRunning_) return;
    if (streamQueue_.empty()) return;
    const auto [zone, tile] = streamQueue_.front();
    streamQueue_.pop_front();
    const auto dir = lookup(zone);
    const QString ff = replayZoneDir() + "/" + tile + ".ff";
    if (!dir || !QFileInfo::exists(ff)) {
        ++streamFailed_;
        if (streamQueue_.empty()) {
            emit streamedReady(zone, streamLoaded_, streamFailed_);
            streamLoaded_ = streamFailed_ = 0;
        } else {
            startNextStreamed();
        }
        return;
    }
    streamRunning_ = true;
    const QString scratch = *dir + "/streamed/w";
    QDir(scratch).removeRecursively();
    QDir().mkpath(scratch);
    ActsRequest request;
    request.zone = tile;
    request.types = "gfx_map_trzone";
    request.outDir = QDir::toNativeSeparators(scratch);
    emit progress(QString("ACTS streamed world tile %1 (%2 more)").arg(tile).arg(streamQueue_.size()));
    ActsRunner::shared()->submit(tools_, request, this, [this, zone, tile, scratch, dir = *dir](const ActsResult&) {
        const QString exported = scratch + "/mw19replay/" + tile;
        const QString dest = dir + "/streamed/" + tile;
        bool ok = false;
        QDirIterator it(exported, {"*.json"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            if (it.next().contains("gfx_map_trzone")) ok = true;
        if (ok) {
            QDir(dest).removeRecursively();
            ok = QDir().rename(exported, dest);
        }
        QDir(scratch).removeRecursively();
        ok ? ++streamLoaded_ : ++streamFailed_;
        streamRunning_ = false;
        if (streamQueue_.empty()) {
            emit progress(QString("world cache: %1 streamed tile(s) of %2 exported%3").arg(streamLoaded_).arg(zone).arg(
                streamFailed_ ? QString(", %1 failed").arg(streamFailed_) : QString()));
            emit streamedReady(zone, streamLoaded_, streamFailed_);
            streamLoaded_ = streamFailed_ = 0;
        } else {
            startNextStreamed();
        }
    });
}

// ---------------------------------------------------------------------------------------------------------------
// Headless test (main.cpp --world-cache-test)
'''),
 ('''    WorldCache cache(tools);
    QStringList log;
    QObject::connect(&cache, &WorldCache::progress, &cache, [&](const QString& m) { log << m; });
''', '''    WorldCache cache(tools);
    QStringList log;
    QObject::connect(&cache, &WorldCache::progress, &cache, [&](const QString& m) { log << m; });
    // IW8R_STREAM_AT="x y radius [limit]": after the zone, the streamed world tiles near that point are exported too
    // (big maps), and the load below includes them.
    bool streamDone = true;
    QObject::connect(&cache, &WorldCache::streamedReady, &cache, [&](const QString&, int loaded, int failed) {
        log << QString("streamed tiles: %1 exported, %2 failed").arg(loaded).arg(failed);
        streamDone = true;
    });
'''),
 ('''    cache.request(zone);
    loop.exec();
    const qint64 wallMs = wall.elapsed();
''', '''    cache.request(zone);
    loop.exec();
    if (const QStringList at = qEnvironmentVariable("IW8R_STREAM_AT").split(' ', Qt::SkipEmptyParts); at.size() >= 3)
        if (const auto dir = cache.lookup(zone)) {
            std::vector<StreamedWorldZone> zones;
            std::string serr;
            if (readStreamedWorldZones(dir->toStdString(), zones, &serr)) {
                const auto near = streamedZonesNear(zones, at[0].toDouble(), at[1].toDouble(), at[2].toDouble(),
                                                    at.size() > 3 ? size_t(at[3].toInt()) : 40);
                log << QString("streamed zones: %1 in the map, %2 near the point").arg(zones.size()).arg(near.size());
                QStringList tiles;
                for (const StreamedWorldZone& z : near) tiles << QString::fromStdString(z.name);
                streamDone = false;
                cache.requestStreamed(zone, tiles);
                QEventLoop wait;
                QTimer tick;
                tick.setInterval(200);
                QObject::connect(&tick, &QTimer::timeout, &wait, [&] {
                    if (streamDone) wait.quit();
                });
                QTimer::singleShot(60 * 60 * 1000, &wait, &QEventLoop::quit);
                tick.start();
                wait.exec();
            } else {
                log << "streamed zones: " + QString::fromStdString(serr);
            }
        }
    const qint64 wallMs = wall.elapsed();
'''),
])
patch('src/qtapp/main_window.h', [
 ('''    void installEditedStockMap();
''', '''    void installEditedStockMap();
    // Big maps (Quarry, Verdansk): the streamed world tiles around the camera are exported and drawn in the stock
    // reference (View > Load Streamed World Around Camera). Returns how many tiles were asked for; -1 without a big map.
    int loadStreamedWorldAroundCamera();
'''),
])
patch('src/qtapp/main_window_stock_edit.cpp', [
 ('''QString MainWindow::stockBackupDir(const QString& zone) const {
''', '''int MainWindow::loadStreamedWorldAroundCamera() {
    if (referenceZone_.isEmpty() || !worldCache_) {
        ctx_.setStatus("Load Streamed World: open a stock map with its reference first (File > Open IW8 Stock Map)");
        return -1;
    }
    const auto dir = worldCache_->lookup(referenceZone_);
    if (!dir) {
        ctx_.setStatus("Load Streamed World: the stock reference is still being read; try again when it is drawn");
        return -1;
    }
    std::vector<StreamedWorldZone> zones;
    std::string err;
    if (!readStreamedWorldZones(dir->toStdString(), zones, &err)) {
        ctx_.setStatus("Load Streamed World: " + qs(err));
        return -1;
    }
    if (zones.empty()) {
        ctx_.setStatus("Load Streamed World: " + referenceZone_ + " has no streamed world tiles (all of it is drawn already)");
        return 0;
    }
    const double radius = Settings::instance().value("stock/streamRadius", 12000.0).toDouble();
    const int limit = Settings::instance().value("stock/streamLimit", 48).toInt();
    const auto near = streamedZonesNear(zones, ctx_.cam.pos.x, ctx_.cam.pos.y, radius, size_t(std::max(1, limit)));
    QStringList tiles;
    for (const StreamedWorldZone& z : near) tiles << qs(z.name);
    static bool connected = false;
    if (!connected) {
        connected = true;
        connect(worldCache_, &WorldCache::streamedReady, this, [this](const QString& zone, int loaded, int failed) {
            if (zone != referenceZone_) return;
            ctx_.setStatus(QString("Streamed world: %1 tile(s) added%2; redrawing the reference").arg(loaded).arg(
                failed ? QString(", %1 failed").arg(failed) : QString()));
            loadStockReference(zone);
        });
    }
    worldCache_->requestStreamed(referenceZone_, tiles);
    ctx_.setStatus(QString("Load Streamed World: %1 of %2 tiles within %3 units of the camera are being read (a few seconds each)")
                       .arg(tiles.size()).arg(zones.size()).arg(radius));
    return int(tiles.size());
}

QString MainWindow::stockBackupDir(const QString& zone) const {
'''),
 ('#include "stock_placed.h"\n', '#include "stock_placed.h"\n#include "stock_world.h"\n#include "world_cache.h"\n#include "settings.h"\n'),
])
patch('src/qtapp/main_window.cpp', [
 ('''    addCommand(file, "EditPlacedObjects", "Edit Placed Objects (Warzone buy stations)", QKeySequence(), [this] { editPlacedObjects(); });
''', '''    addCommand(file, "EditPlacedObjects", "Edit Placed Objects (Warzone buy stations)", QKeySequence(), [this] { editPlacedObjects(); });
    addCommand(file, "LoadStreamedWorld", "Load Streamed World Around Camera (Warzone maps)", QKeySequence(), [this] { loadStreamedWorldAroundCamera(); });
'''),
])
print('ok')
