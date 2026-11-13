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
//
// Stock lightmap (docs/research/94_stock_lightmaps.md, core stock_lightmap.h): after a zone's geometry export, and
// lazily for a zone cached before (request() / requestLightmap() on a cached zone without one), a separate job makes
//   <zone>/lightmap.iw8rlm   the map's baked lightmap (C, f, direction), lightmapFile(zone)
// It runs its own acts.exe (the shared runner has no decompress mode):
//   acts --noUpdater fastfile -r mw19replay -g <replay> --oodle <oodle> -d --noAssetDump -l -o w/lightmap_<zone> <zone>.ff
// which writes <zone>.ff.dec (300-400 MB; with a .fp patch also .fp.dec and .fp.patched.dec), then scans the .dec off
// the UI thread and deletes the whole scratch folder at once, before the cache file is written. A zone the scan
// rejects (Warzone: "258 lightmaps: not supported yet") keeps its error in index.json ("lightmap_error") and is never
// tried again; an ACTS or disk-space failure is not retried in this session. Runs only while no geometry export runs.
//
// Stock reflection probes (core stock_probes.h): the same job, from the same decompression, also makes
//   <zone>/probes.iw8rprb    the probe records (from the cached gfx_map export) and their BC6H octahedral images,
//                            probesFile(zone); read it with iw8r::readStockProbeCache
// The job runs when either file is missing and has no recorded error, and makes only the missing one(s): a zone
// cached with a lightmap but no probes gets its probes on the next requestLightmap (one decompression). A probe
// verdict on the data is kept as "probes_error". Both lightmapReady and probesReady are emitted when the job ends.
//
// Many lightmaps (Warzone, research 94 section "Warzone (type 0)"): lightmap i of a map is in transient zone
// lightmapTransientIndex[i]: zone 0 is the main .ff (handled by the job above), zone k the streamed tile
// <map>_<k, 5 digits>_tr.ff. Cache files:
//   <zone>/lightmap.iw8rlm          the map's only lightmap (index 0) when it has one of type 1 or 2 (every MP map)
//   <zone>/lightmaps/<index>.iw8rlm every other lightmap (type 0 / Warzone, index = GfxSurface.lightmapIndex): the
//                                   main zone's from the job above, a tile's from the tile lightmap job
//   <zone>/streamed/<tile>/lightmap.done   the tile's lightmaps were extracted ("ok <indices>"), or the scan's
//                                   verdict on the tile ("error <why>"); not tried again either way
// lightmapFile(zone, index) finds either form. A tile lightmap job runs for every tile requestStreamed exports
// (and for tiles exported before without lightmap.done): acts -d on <tile>.ff into w/lightmap_<tile>, the scan
// off the UI thread, the scratch (every .dec) deleted before the cache files are written. One decompression at a
// time (main and tile jobs share one slot), none while a geometry export runs or waits; each needs the estimated
// .dec size free and never less than 3 GB. lightmapReady(zone) when a zone's queued tile jobs are all done.
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QSet>
#include <QString>
#include <QList>
#include <QStringList>

#include <deque>
#include <functional>
#include <map>
#include <optional>

#include "stock_world.h"

class QProcess;

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
    // async, one zone at a time; queued zones are skipped. A cached zone queues only its lightmap job when it has none.
    void request(const QString& zone);
    bool busy() const;  // a geometry export or a lightmap job is queued or running
    QString cacheRoot() const;

    // Parses the cached export. nullopt with err set when the zone is not cached.
    // includeStreamed false: the main zone only (the streamed tiles are added one by one by the camera-following
    // loader; buildStockWorldTile).
    std::optional<StockWorld> load(const QString& zone, QString* err = nullptr, bool includeStreamed = true) const;

    // The zone's lightmap cache file (<zone cache folder>/lightmap.iw8rlm, read it with iw8r::readStockLightmapCache),
    // or nullopt when the zone is not cached or has no lightmap (yet). Does not start a job. MP maps only: a Warzone
    // map's lightmaps are per index, see below.
    std::optional<QString> lightmapFile(const QString& zone) const;
    // The cache file of lightmap `index` (GfxSurface.lightmapIndex, StockWorld::lightmapIndex) of a zone:
    // <zone>/lightmaps/<index>.iw8rlm, or for index 0 the single-lightmap file lightmap.iw8rlm. nullopt when the zone
    // is not cached or that lightmap is not made (yet). Read it with iw8r::readStockLightmapCache. Starts no job.
    std::optional<QString> lightmapFile(const QString& zone, int index) const;
    // Every lightmap index of the zone that has a cache file, ascending. Starts no job.
    QList<int> lightmapIndices(const QString& zone) const;
    QString lightmapErrorOf(const QString& zone) const;  // why the lightmap job failed ("" when made, pending or never run)
    // Queues the lightmap + probes job of a cached zone (async; nothing when it has both files (or their recorded
    // errors) or is queued). lightmapReady(zone) and probesReady(zone) when it ends either way.
    void requestLightmap(const QString& zone);

    // The zone's reflection probe cache file (<zone cache folder>/probes.iw8rprb), or nullopt when the zone is not
    // cached or has no probes (yet). Does not start a job (requestLightmap does).
    std::optional<QString> probesFile(const QString& zone) const;
    QString probesErrorOf(const QString& zone) const;  // why the probe extraction failed ("" when made, pending or never run)

    // Big maps (research 90 / 87 section 1.2): exports the streamed world tiles `tiles` (zone names such as
    // mp_donetsk_00587_tr) of a cached zone into <cache>/<zone>/streamed/<tile>, one ACTS run each, skipping those
    // already there; load() then includes them. streamedReady(zone) when the list is done. Each tile (also one
    // exported before) then gets its tile lightmap job (see above) unless it has lightmap.done.
    void requestStreamed(const QString& zone, const QStringList& tiles);
    int streamedPending() const { return int(streamQueue_.size()) + (streamRunning_ ? 1 : 0); }
    // Tile lightmap jobs queued or running.
    int tileLightmapsPending() const { return int(tileLmQueue_.size()) + (lmTile_.isEmpty() ? 0 : 1); }

signals:
    void ready(const QString& zone);  // lookup() or errorOf() now answers for this zone
    void streamedReady(const QString& zone, int loaded, int failed);
    void lightmapReady(const QString& zone);  // lightmapFile() or lightmapErrorOf() now answers for this zone
    void probesReady(const QString& zone);    // probesFile() or probesErrorOf() now answers for this zone
    void progress(const QString& message);

private:
    struct Entry {
        QString dir;  // absolute export folder (empty when failed)
        qint64 bytes = 0;
        qint64 actsMs = 0;
        bool failed = false;
        QString error;
        QString lightmapError;  // the lightmap scan rejected this zone (not tried again)
        QString probesError;    // the probe extraction rejected this zone (not tried again)
    };
    // The lightmap / probes job still has something to make for this cached zone.
    bool needsLightmap(const QString& zone) const;
    bool needsProbes(const QString& zone) const;

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
    // Lightmap jobs: zones still to do, the one running (ACTS process, then the scan on a worker thread).
    void startNextLightmap();
    void lightmapActsEnded(int exitCode, bool startFailed);
    // Starts acts -d on `ffPath` into lmScratch_ (lmZone_ / lmTile_ set by the caller).
    void startDecompress(const QString& ffPath, const QString& what);
    // Tile lightmap jobs (lmTile_ set while one runs).
    void queueTileLightmap(const QString& zone, const QString& tile);
    void startTileLightmap(const QString& zone, const QString& tile);
    void endTileLightmap(const QString& zone, const QString& tile, bool ok, const QString& message, bool persist);
    std::deque<std::pair<QString, QString>> tileLmQueue_;  // (zone, tile)
    QString lmTile_;           // tile of the tile lightmap job in progress ("" for none or for a main-zone job)
    // The whole job failed before the scans (ACTS, disk space, no .ff): both outputs share the verdict.
    void endLightmap(const QString& zone, bool ok, const QString& error, bool persistError);
    // One output's scan result (the probes when `probes`, else the lightmap): progress line, and a failure is kept
    // in index.json (persistError) or in lmSkip_. Emits no ready signal.
    void noteExtraction(const QString& zone, bool probes, bool ok, const QString& message, bool persistError);
    QStringList lmQueue_;
    QString lmZone_;           // zone of the job in progress ("" when idle)
    QString lmScratch_;        // root_/w/lightmap_<zone> while it runs
    QProcess* lmProc_ = nullptr;
    QSet<QString> lmSkip_;     // ACTS or disk failures of this session (zone, or zone/tile): not tried again until the next start
    QElapsedTimer lmTimer_;
};

// Headless --world-cache-test <zone>: requests the zone, waits, loads the export and writes
// <outDir>/world-cache-test.txt (triangles, bounds, timings, cache size). Needs a QCoreApplication.
// Returns 0 when the zone resolved and loaded.
int runWorldCacheTest(const QString& zone, const QString& outDir);

}  // namespace iw8r::qt
