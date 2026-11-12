// On-demand texture cache for stock MW2019 Replay 1.20 materials (colour + normal DDS).
//
// A material's body and its slot-0 / slot-9 images are exported with ACTS (fastfile -a material|image -n <names>)
// from the Replay zones, one ACTS request per zone per round, all zones of a round at once (acts_runner.h: the
// requests go to ACTS servers that keep zones loaded). Results live under AppLocalDataLocation/material_cache:
//   index.json   material -> entry (paths relative to the cache root)
//   images/      colour and normal DDS files, named after the image
//   w/           scratch ACTS output of the batch in progress (removed when the batch finishes)
// Materials already in the index (or seeded from data/stock_assets/<zone>/index.json) never run ACTS again.
#pragma once

#include <QFileInfo>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace iw8r::qt {

struct ActsResult;

class MaterialCache : public QObject {
    Q_OBJECT
public:
    using ToolPathFn = std::function<QString(const QString&)>;  // keys "acts", "replay", "oodle" (MainWindow::toolPath)
    explicit MaterialCache(ToolPathFn tools, QObject* parent = nullptr);
    ~MaterialCache() override;

    struct Entry {
        QString material, zone, colorDds, normalDds, techset;
        QString maskDds;  // opacity / cut-out mask (texture slot 27, research 66); empty when the material has none
        QString emissiveDds;  // emissive map (texture slot 18); empty when none
        // Material body fields (research 66); -1 / empty for entries cached before they were kept.
        qint64 surfaceFlags = -1;  // bit 0x80 = nodraw (shadow-only, e.g. foliage *_shadowcaster meshes)
        QString materialType;      // e.g. MTL_TYPE_STATIC_MODEL_SELFVIS
        int sortKey = -1;
        // Texture table of the material body, "<slot>=<image name>" per entry (4.16); empty for entries cached before
        // the table was kept and for seeds.
        QStringList textures;
        bool failed = false;
        QString error;
    };

    std::optional<Entry> lookup(const QString& material) const;  // on-disk index and seeds only (instant)
    void request(const QStringList& materials);                  // async; de-duplicated; batched per zone
    bool busy() const;
    QString cacheRoot() const;
    // "<path> (WxH)" for a cached DDS, "none" for an empty path (Material Info).
    static QString describeImage(const QString& ddsPath);

signals:
    void materialReady(const QString& material);  // lookup() now returns an entry (possibly failed)
    void progress(const QString& message);

private:
    struct Run {
        bool images = false;  // false: material bodies, true: image payloads
        QString zone;
        QStringList names;
    };
    struct Work {  // one requested material in the current batch
        QString material;
        QStringList bodyZones, imageZones;  // candidate zones, tried in order
        int attempt = 0;                    // next index into bodyZones
        bool found = false;
        QString zone, techset;
        QString color, normal;  // image names without the leading ','; empty when absent or a '$' placeholder
        QString mask;           // opacity / cut-out image name (slot 27 of alpha-tested and blended sets), if any
        QString emissive;       // slot 18 image name, if any
        qint64 surfaceFlags = -1;
        QString materialType;
        int sortKey = -1;
        QStringList textures;   // "<slot>=<image>" for every non-placeholder entry
        QString error;
    };
    struct Image {  // one colour or normal image in the current batch
        QStringList zones;
        int attempt = 0;
        bool done = false;
        QString file, error;  // file: absolute path under images/ once done
    };

    void loadIndex();
    void loadSeeds();
    void loadCatalog();
    void writeIndex() const;
    QString replayZoneDir() const;
    QStringList candidates(const QStringList& zones, bool bodies) const;
    QList<QFileInfo> xpaks() const;
    QString imagePath(const QString& imageName) const;

    void startBatch();
    void buildBodyRound();
    void buildImageRound();
    void pump();
    void startRun(const Run& run);
    void finishRun(const Run& run, const QString& dir, const ActsResult& result);
    bool loadBody(Work& work, const QString& zone, const QString& jsonPath) const;
    void finishBatch();

    ToolPathFn tools_;
    QString root_;
    std::map<QString, Entry> index_;   // from root_/index.json
    std::map<QString, Entry> seeds_;   // from data/stock_assets (never written back)
    std::map<QString, QStringList> catalog_;  // material -> zones, from data/asset_catalog/iw8_materials.tsv
    // Colour images by name stem ("<stem>_c" before the '&'), from iw8_images.tsv: the fallback for a material whose
    // body is in no candidate zone (model materials referenced from another zone's xmodel).
    std::map<QString, std::pair<QString, QStringList>> colorByStem_;
    std::map<QString, QStringList> imageZonesByName_;  // lower-case image name -> zones (iw8_images.tsv)
    bool imageCatalogLoaded_ = false;
    void loadImageCatalog();
    bool catalogLoaded_ = false;

    QStringList pending_;
    std::set<QString> queued_;
    bool running_ = false;
    std::vector<Work> works_;
    std::map<QString, Image> images_;
    bool imagesListed_ = false;
    bool imagePhase_ = false;
    std::deque<Run> queue_;
    int outstanding_ = 0;  // runs of the current round that have not answered yet
    int runSeq_ = 0;
};

// Headless --material-cache-test: requests the materials, waits, writes <outDir>/material-cache-test.txt.
// Needs a QCoreApplication. Returns 0 when every material resolved with a usable entry.
int runMaterialCacheTest(const QStringList& materials, const QString& outDir);

}  // namespace iw8r::qt
