// On-demand LOD0 geometry cache for stock MW2019 Replay 1.20 models (script_model "model" names, e.g.
// automotive_truck_loading_ramp_short, catalog data/asset_catalog/iw8_models.tsv).
//
// Two ACTS runs per zone and round, like MaterialCache:
//   1. -a xmodel -n <models>                 the xmodel table: LOD0 surfs name (lodInfo[0]) and bounds
//   2. --geometry -a xmodelsurfs -n <lod0>   the LOD0 surface buffer as <name>.geometry.glb, with every populated
//                                            zone XPak (the surface payloads are streamed; see docs/research/23)
// Results live under AppLocalDataLocation/model_cache:
//   index.json   model -> entry (paths relative to the cache root)
//   glb/         one LOD0 .glb per model, the ACTS sidecar unmodified
//   w/           scratch ACTS output of the batch in progress (removed when the batch finishes)
// Models already in the index never run ACTS again.
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>

#include <array>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include "model_import.h"
#include "xmodel_info.h"

namespace iw8r::qt {

struct ActsResult;

class ModelCache : public QObject {
    Q_OBJECT
public:
    using ToolPathFn = std::function<QString(const QString&)>;  // keys "acts", "replay", "oodle" (MainWindow::toolPath)
    explicit ModelCache(ToolPathFn tools, QObject* parent = nullptr);
    ~ModelCache() override;

    struct Entry {
        QString name, zone;
        QString glbPath;                 // absolute path of the cached LOD0 .glb (empty when failed)
        QString lod0Surfs;               // xmodelsurfs asset name of LOD0
        std::array<double, 6> bounds{};  // xmodel bounds in Radiant units (Z-up inches): mins xyz, maxs xyz
        QStringList materials;           // LOD0 surface materials in surface order (xmodel materialHandles)
        bool failed = false;
        QString error;
        // The GLB's per-vertex self visibility (_SELFVIS, ACTS fork 3667413c; research 105 section 4): 1 present,
        // 0 checked and absent, -1 not known (cached before it was recorded: refreshGeometry fetches it again).
        int selfVis = -1;
        // The GLB's lightmap coordinates (_LMAPUV, ACTS fork 834568b9) of a lightmapped model: 1 present, 0 checked and
        // absent, -1 not known.
        int lmapUv = -1;
    };

    std::optional<Entry> lookup(const QString& name) const;  // on-disk index only (instant)
    // Model detail (3.18): LODs with switch distances, bones and tags, read from the xmodel export when the model was
    // cached (core xmodel_info) and kept as <root>/detail/<name>.json. Empty when the model was cached before detail
    // was kept; refetch() drops the entry and caches the model again.
    QString detailPath(const QString& name) const;
    std::optional<XModelInfo> loadDetail(const QString& name) const;
    void refetch(const QString& name);
    void request(const QStringList& names);                  // async; de-duplicated; batched per zone
    // Runs the geometry export again for cached models whose GLB predates the self visibility (Entry::selfVis -1) or,
    // for a lightmapped model (a tml / tmlc material), its lightmap coordinates (Entry::lmapUv -1),
    // once per model and session, after any requested models. The old GLB stays in use until the new one is in, and
    // stays when the export fails. modelReady / batchEnded follow as for request().
    void refreshGeometry(const QStringList& names);
    int refreshPending() const { return int(refreshPending_.size()); }
    // Zones tried for a model the catalogue does not list (owner playtest 2026-11-11: a stock map's props that are
    // missing from iw8_models.tsv were never drawn; they are in that map's own zone). Setting it also forgets earlier
    // "not listed" failures so those models are tried again.
    void setFallbackZones(const QStringList& zones);
    bool busy() const;
    // The index file has been read (on the worker thread): lookup() answers at once. Before, lookup() waits for it.
    bool indexReady() const;
    QString cacheRoot() const;
    // Models of one batch. A longer request is worked off in batches of this size, one after the other, by the cache
    // itself: every batch writes the index and reports its models, so a long fetch shows progress and survives a quit.
    static constexpr int kBatchModels = 150;
    // Why the last request could not even start (a missing tool); empty when the cache could work.
    QString problem() const { return problem_; }

    // The cached LOD0 as an ImportedModel in the editor's file convention: glTF Y-up, inches. The editor's
    // buildModelGeom maps it to Radiant's Z-up with (x, -z, y), which gives back the raw ACTS Z-up inches.
    std::optional<ImportedModel> loadModel(const QString& name, QString* err = nullptr) const;

    // LOD preview (3.18): LOD `lod` (>= 1) of a cached model, fetched on demand with one ACTS xmodelsurfs run in the
    // model's zone and kept as glb/<name>.lod<N>.glb. lodReady fires when the file is there (or the fetch failed:
    // lodFailed() is then true and the preview keeps LOD0). LOD 0 is the ordinary cached model.
    QString lodGlbPath(const QString& name, int lod) const;
    bool hasLod(const QString& name, int lod) const;
    bool lodFailed(const QString& name, int lod) const;
    void requestLod(const QString& name, int lod);
    // Many LODs at once (stock prop LODs, plan 1.13): grouped by zone, up to 48 surfaces per ACTS run. Each answered
    // pair gets lodReady, as requestLod. Pairs already cached, pending or failed are skipped.
    void requestLods(const QList<QPair<QString, int>>& want);
    // Fetches still running or queued by requestLod / requestLods.
    int lodsPending() const { return int(lodPending_.size()); }
    std::optional<ImportedModel> loadModelLod(const QString& name, int lod, QString* err = nullptr) const;

signals:
    void modelReady(const QString& name);  // lookup() now returns an entry (possibly failed)
    void indexLoaded();  // the index read on the worker thread is ready (indexReady())
    // A batch has ended, with `resolved` models answered (0: nothing could be done, see problem()). Sent for every
    // batch, also one that delivered nothing, so a caller that waits for models is never left waiting.
    void batchEnded(int resolved);
    void lodReady(const QString& name, int lod);
    void progress(const QString& message);

private:
    void saveDetail(const QString& name, const QString& exportPath) const;
    std::optional<ImportedModel> loadGlb(const QString& path, const QStringList& materials, const QString& name,
                                         QString* err) const;
    std::set<QString> lodPending_, lodFailed_;  // "<name>#<lod>"
    struct Run {
        bool geometry = false;  // false: xmodel tables, true: xmodelsurfs buffers
        QString zone;
        QStringList names;
        bool reference = false;  // geometry run for buffers the models' own zones only name as a reference
    };
    struct Work {
        QString name;
        QStringList zones;   // candidate zones from the catalog that have a .ff, tried in order
        int attempt = 0;     // next index into zones
        bool found = false;  // xmodel table loaded
        QString zone, lod0Surfs, glb, error;
        std::array<double, 6> bounds{};
        QStringList materials;
        // LOD0 named as a reference (",name"): the buffer is in another zone. Zones of models with a like name,
        // tried in order for the plain name.
        QStringList refZones;
        int refAttempt = 0;
    };

    void loadCatalog();
    void writeIndex() const;
    QString replayZoneDir() const;
    QStringList xpaks() const;
    QString glbPath(const QString& name) const;

    void startBatch();
    void startRefreshBatch();
    void buildTableRound();
    void buildGeometryRound();
    void buildReferenceRound();
    QStringList referenceZones(const QString& surfs, const QString& except) const;
    bool storeGlb(Work& w, const QString& file);
    void pump();
    void startRun(const Run& run);
    void finishRun(const Run& run, const QString& dir, const ActsResult& result);
    void finishBatch();

    ToolPathFn tools_;
    QString root_;
    // From root_/index.json, read on a worker thread from the constructor (4 MB of JSON and a file check per GLB held
    // the start-up for about 1 s): index() waits for it on first use.
    static std::map<QString, Entry> readIndex(const QString& root);
    std::map<QString, Entry>& index() const;
    mutable std::future<std::map<QString, Entry>> indexLoad_;
    mutable std::once_flag indexOnce_;
    mutable std::map<QString, Entry> indexData_;
    std::map<QString, QStringList> catalog_;  // model -> zones, from data/asset_catalog/iw8_models.tsv
    bool catalogLoaded_ = false;

    QStringList pending_;
    QStringList fallbackZones_;
    std::set<QString> queued_;  // requested and not yet resolved
    std::set<QString> refetched_;  // refetch() ran for these (never twice: a model whose detail cannot be read)
    QStringList refreshPending_;       // refreshGeometry, not yet in a batch
    std::set<QString> refreshed_;      // refreshGeometry took these (once per session)
    bool refreshBatch_ = false;        // the running batch refreshes geometry (a failure keeps the old entry)
    bool running_ = false;
    std::vector<Work> works_;
    enum class Phase { Tables, Geometry, References, Done };
    Phase phase_ = Phase::Tables;
    QString problem_;
    bool forgetNotListed_ = false;  // setFallbackZones came during a batch: drop "not listed" failures when it ends
    std::deque<Run> queue_;
    int outstanding_ = 0;  // runs of the current round that have not answered yet (acts_runner.h)
    int runSeq_ = 0;
};

// Headless --model-cache-test: requests the models, waits, loads each cached LOD0 and writes
// <outDir>/model-cache-test.txt. Needs a QCoreApplication. Returns 0 when every model resolved and loaded.
int runModelCacheTest(const QStringList& names, const QString& outDir);

// Headless --model-chain-test: the fetch chain against a cache that cannot deliver, without the game. Two cases in
// scratch folders under <outDir>: a missing ACTS (nothing can start: the chain must stop and say why) and an ACTS
// that fails every run (whole batches fail: the chain must go on to the last batch and end). Writes
// <outDir>/model-chain-test.txt. Needs a QCoreApplication. Returns 0 when both cases ended as they should.
int runModelCacheChainTest(const QString& outDir);

}  // namespace iw8r::qt
