// On-demand texture cache for stock MW2019 Replay 1.20 materials (colour + normal DDS).
//
// A material's body and its slot-0 / slot-9 images are exported with ACTS (fastfile -a material|image -n <names>)
// from the Replay zones, one ACTS request per zone per round, all zones of a round at once (acts_runner.h: the
// requests go to ACTS servers that keep zones loaded). Results live under AppLocalDataLocation/material_cache:
//   index.json   material -> entry (paths relative to the cache root)
//   images/      colour and normal DDS files, named after the image
//   w/           scratch ACTS output of the batch in progress (removed when the batch finishes)
// Materials already in the index (or seeded from data/stock_assets/<zone>/index.json) never run ACTS again.
// Layered world materials (tw4 / twc4) also get every layer's colour, normal and alpha + reveal image and their
// pixel constant buffer (layerImages / layerConstants; docs/research/97_stock_layered_materials.md).
#pragma once

#include <QByteArray>
#include <QFileInfo>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <array>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include "stock_layered_material.h"

namespace iw8r::qt {

struct ActsResult;

class MaterialCache : public QObject {
    Q_OBJECT
public:
    using ToolPathFn = std::function<QString(const QString&)>;  // keys "acts", "replay", "oodle" (MainWindow::toolPath)
    explicit MaterialCache(ToolPathFn tools, QObject* parent = nullptr);
    ~MaterialCache() override;

    // The images of one layer of a stock layered (tw4 / twc4) material, research 97: colour (slot L), normal + gloss
    // (slot 9 + L) and alpha + reveal (slot 27 + L, red alpha, green reveal; none for layer 0). Cached DDS paths, empty
    // when the material has none in that slot (or an engine image such as $black) or its export failed.
    struct LayerImages {
        QString colorDds, normalDds, revealDds;
    };

    struct Entry {
        QString material, zone, colorDds, normalDds, techset;
        QString maskDds;  // opacity / cut-out mask (texture slot 27, research 66); empty when the material has none
        QString emissiveDds;  // emissive map (texture slot 18); empty when none
        // Research 107: the transmission map (texture slot 68, sRGB; foliage, tarps, cloth, flags), and the lit
        // technique's material constants by index: 18 transmission tint, 36 emissive scale, 103 wrap diffuse, 104 the
        // wrap normal blend (rgba floats). Missing = not in the material.
        QString transmissionDds;
        std::map<int, std::array<float, 4>> consts;
        // Material body fields (research 66); -1 / empty for entries cached before they were kept.
        qint64 surfaceFlags = -1;  // bit 0x80 = nodraw (shadow-only, e.g. foliage *_shadowcaster meshes)
        QString materialType;      // e.g. MTL_TYPE_STATIC_MODEL_SELFVIS
        int sortKey = -1;
        // Texture table of the material body, "<slot>=<image name>" per entry (4.16); empty for entries cached before
        // the table was kept and for seeds.
        QStringList textures;
        // Layered materials (body field layerCount >= 2, research 97): layers[L] for L = 0 .. layerCount - 1 (layers[0]
        // repeats colorDds / normalDds), and the body's pixel constant buffer (constantBufferTable[0].psData) that holds
        // the per-layer blend constants. Empty for single-layer materials, seeds and entries cached before 2026-11-12.
        int layerCount = 0;
        std::vector<LayerImages> layers;
        // The lit technique's pixel constant buffer (constantBufferIndex[34], research 107 section 6; before constRules
        // 1 the first buffer with pixel data). Single-layer materials keep it in the canonical row order the decoders
        // read: row 0 constant 72, row 1 constant 0, row 2 constant 9, row 3 constant 54 (the lit technique puts 18 / 36
        // before 54); shorter than 64 bytes when the material has no constant 54.
        QByteArray psData;
        // The colour tint (material constant 9, RRGGBB; -1 = neutral or unknown) and whether a tint mask (texture slot
        // 59) limits it (research 95: albedo x lerp(1, tint, mask)).
        int tint = -1;
        bool tintMasked = false;
        QString tintMaskDds;  // slot 59 (red = where the tint applies); empty when none or not exported
        bool failed = false;
        QString error;
    };

    std::optional<Entry> lookup(const QString& material) const;  // on-disk index and seeds only (instant)
    // A stock layered material's colour image per layer, layer 0 first (layer 0 = lookup()->colorDds): cached DDS paths
    // as colorDds is, an empty string for a layer without one. Empty when the material is not layered or not resolved.
    std::vector<QString> layerColorImages(const QString& material) const;
    // The same with each layer's normal + gloss and alpha + reveal images.
    std::vector<LayerImages> layerImages(const QString& material) const;
    // The per-layer blend constants (flags, UV transform, tint, reveal sharpness, specular / gloss remaps), decoded from
    // the cached psData; nullopt when the material is not layered, not resolved, or its psData is missing or short.
    std::optional<StockLayeredMaterial> layerConstants(const QString& material) const;
    // The same for any stock material with a pixel constant buffer (single-layer world materials: layer 0 only), e.g.
    // a decal's flags and reveal sharpness (research 98).
    std::optional<StockLayeredMaterial> materialConstants(const QString& material) const;
    void request(const QStringList& materials);                  // async; de-duplicated; batched per zone
    // A stock map shown as the reference: materials the catalog does not list are looked up in this zone (and its
    // techsets zone). The per-map terrain blends (tw4/..., twc4/...) are named per map, and the catalog lists only
    // those of the traced zones. Materials that failed as "not listed" are tried again.
    void addMapZone(const QString& zone);
    // Layered world materials are numbered per map (tw4/29n_4n_4n, twc4/12n_13n_35n): the same name is a different
    // material on every map, so they are resolved in the stock map's own zones only and cached per map
    // ("<map zone>|<name>"). Owner report 2026-11-12: mp_m_speedball's ground took mp_bm_tut's and backlot2's
    // same-named blends (a metal base showing through, grass missing).
    static bool mapLocalName(const QString& material);
    QString indexKey(const QString& material) const;
    bool busy() const;
    QString cacheRoot() const;
    QString runRoot() const;  // ACTS run folders (short: long packed image names)
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
        QString key;  // index key (indexKey: per map for map-local names)
        QStringList bodyZones, imageZones;  // candidate zones, tried in order
        int attempt = 0;                    // next index into bodyZones
        bool found = false;
        QString zone, techset;
        QString color, normal;  // image names without the leading ','; empty when absent or a '$' placeholder
        QString mask;           // opacity / cut-out image name (slot 27 of alpha-tested and blended sets), if any
        QString emissive;       // slot 18 image name, if any
        QString transmission;   // slot 68 image name, if any (research 107)
        std::map<int, std::array<float, 4>> consts;  // Entry::consts
        qint64 surfaceFlags = -1;
        QString materialType;
        int sortKey = -1;
        QStringList textures;   // "<slot>=<image>" for every non-placeholder entry
        int layerCount = 0;     // body layerCount (0 when absent)
        // Per layer L (index L; layer 0 = color / normal): image names, empty when absent or a placeholder.
        QStringList layerColor, layerNormal, layerReveal;
        QByteArray psData;      // the body's first pixel constant buffer
        int tint = -1;          // psData row 2 bytes 0-2 (else constant 9), RRGGBB; -1 = neutral / none
        bool tintMasked = false;
        QString tintMask;       // slot 59 image name, if any
        QString error;
    };
    struct Image {  // one colour or normal image in the current batch
        QStringList zones;
        int attempt = 0;
        bool done = false;
        QString file, error;  // file: absolute path under images/ once done
    };

    QStringList mapZones_;  // addMapZone, newest first
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
