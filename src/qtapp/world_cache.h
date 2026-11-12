// On-demand cache of stock Replay world geometry (gfx_map + gfx_map_trzone) for one zone, like ModelCache.
//   request(zone)   queues one ACTS export through the shared runner (acts_runner.h), the equivalent of:
//                   acts --noUpdater fastfile -r mw19replay -g <replay> --oodle <oodle> [--xpak <pak>]...
//                        -a gfx_map,gfx_map_trzone -l -o <scratch> <zone>.ff
//   ready(zone)     the export is in the cache (or failed; errorOf(zone) says why)
//   load(zone)      parses the cached export with iw8r::readStockWorld
// Layout under AppLocalDataLocation/world_cache:
//   index.json      zone -> entry (folder relative to the cache root, bytes, ACTS ms, failure)
//   <zone>/         the ACTS export folder (mw19replay/<zone>/assets/gfx_map*, manifest, journal)
//   w/<zone>/       scratch of the run in progress (removed when it ends)
// Refuses an export over 2 GB (C: has little free space): the size is estimated from the zone .ff before the run and
// measured after it. Cached zones never run ACTS again. docs/research/30_stock_world_geometry.md.
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>

#include <deque>
#include <functional>
#include <map>
#include <optional>

#include "stock_world.h"


namespace iw8r::qt {

struct ActsResult;

class WorldCache : public QObject {
    Q_OBJECT
public:
    using ToolPathFn = std::function<QString(const QString&)>;  // keys "acts", "replay", "oodle" (MainWindow::toolPath)
    explicit WorldCache(ToolPathFn tools, QObject* parent = nullptr);
    ~WorldCache() override;

    // The cached ACTS export folder of a zone (a zone name such as "mp_shipment"), or nullopt when not cached or failed.
    std::optional<QString> lookup(const QString& zone) const;
    QString errorOf(const QString& zone) const;  // why the last run failed ("" when cached or never run)
    void request(const QString& zone);            // async, one zone at a time; cached and queued zones are skipped
    bool busy() const;
    QString cacheRoot() const;

    // Parses the cached export. nullopt with err set when the zone is not cached.
    std::optional<StockWorld> load(const QString& zone, QString* err = nullptr) const;

    // Big maps (research 90 / 87 section 1.2): exports the streamed world tiles `tiles` (zone names such as
    // mp_donetsk_00587_tr) of a cached zone into <cache>/<zone>/streamed/<tile>, one ACTS run each, skipping those
    // already there; load() then includes them. streamedReady(zone) when the list is done.
    void requestStreamed(const QString& zone, const QStringList& tiles);
    int streamedPending() const { return int(streamQueue_.size()) + (streamRunning_ ? 1 : 0); }

signals:
    void ready(const QString& zone);  // lookup() or errorOf() now answers for this zone
    void streamedReady(const QString& zone, int loaded, int failed);
    void progress(const QString& message);

private:
    struct Entry {
        QString dir;  // absolute export folder (empty when failed)
        qint64 bytes = 0;
        qint64 actsMs = 0;
        bool failed = false;
        QString error;
    };

    void loadIndex();
    void writeIndex() const;
    QString replayZoneDir() const;
    QStringList xpaks() const;
    void startNext();
    void finishRun(const ActsResult& result);
    void fail(QString zone, const QString& why);

    ToolPathFn tools_;
    QString root_;
    std::map<QString, Entry> index_;
    QStringList queue_;
    QString zone_;             // zone of the run in progress ("" when idle)
    QString scratch_;          // root_/w/<zone> while running
    bool running_ = false;
    QElapsedTimer runTimer_;
    // Streamed tiles: (zone, tile) still to export.
    void startNextStreamed();
    std::deque<std::pair<QString, QString>> streamQueue_;
    bool streamRunning_ = false;
    int streamLoaded_ = 0, streamFailed_ = 0;
};

// Headless --world-cache-test <zone>: requests the zone, waits, loads the export and writes
// <outDir>/world-cache-test.txt (triangles, bounds, timings, cache size). Needs a QCoreApplication.
// Returns 0 when the zone resolved and loaded.
int runWorldCacheTest(const QString& zone, const QString& outDir);

}  // namespace iw8r::qt
