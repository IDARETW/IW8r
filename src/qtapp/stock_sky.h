// Stock Replay 1.20 skies for builds (docs/research/33_sky_route.md).
//
// stockSkyNames() lists the sky images of data/asset_catalog/iw8_images.tsv (sky_mp_*), the values a worldspawn
// `skyname` can take. StockSkyCache exports one sky image with ACTS (fastfile -a image -n <name>, every populated pak
// XPak, the same command as MaterialCache::startRun) into the material cache's images/ folder, where the compile panel
// picks it up as CompileOptions::skyDdsPath; the compiler turns the 2D hemi-octahedral BC6H image into the cube map
// x64-zt reads (src/core/sky_image.h).
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <map>
#include <set>


namespace iw8r::qt {

struct ActsResult;

// Stock sky image names from the catalog, sky_mp_* first then the rest (sky_dev_thermal), sorted. Empty when the
// catalog is not found. Each name is a valid worldspawn `skyname`.
QStringList stockSkyNames();

class StockSkyCache : public QObject {
    Q_OBJECT
public:
    using ToolPathFn = std::function<QString(const QString&)>;  // keys "acts", "replay", "oodle" (MainWindow::toolPath)
    // imagesDir: where exported DDS files live (the material cache's images/ folder).
    // anyImage: every image of the catalog (the Images browser), names kept as written (packed "a_c&b~hash" too) and
    // stored under a file-safe name; otherwise only sky_* images, named as a worldspawn skyname.
    StockSkyCache(ToolPathFn tools, const QString& imagesDir, QObject* parent = nullptr, bool anyImage = false);
    ~StockSkyCache() override;

    // The cached DDS of a sky ("tw/sky_mp_shipment" and "sky_mp_shipment" both work), "" when not exported yet.
    QString cachedDds(const QString& sky) const;
    // Starts an ACTS export when the sky is not cached (async, one run at a time, de-duplicated). Emits skyReady.
    void request(const QString& sky);
    QString lastError(const QString& sky) const;  // why the last export of this sky failed ("" if none)
    bool busy() const { return running_ || !pending_.empty(); }
    // A stock map shown as the reference: images the catalog does not list are also looked for in its zones.
    void addMapZone(const QString& zone) {
        if (!zone.isEmpty() && !mapZones_.contains(zone)) mapZones_.prepend(zone);
    }

signals:
    void skyReady(const QString& image, const QString& file);  // file "" when the export failed (see lastError)
    void progress(const QString& message);

private:
    void pump();
    void startRun();
    void finishRun(const QString& zone, const ActsResult& result);

    ToolPathFn tools_;
    QString imagesDir_;
    std::map<QString, QStringList> zones_;  // image -> catalog zones
    QStringList mapZones_;                  // addMapZone, newest first
    bool anyImage_ = false;
    QString lastArgs_;
    QString keyOf(const QString& name) const;   // catalog key of a requested name
    QString fileOf(const QString& key) const;   // cached DDS path of a key
    std::set<QString> queued_;
    QStringList pending_;
    std::map<QString, QString> errors_;
    QString current_;
    QStringList currentZones_;  // candidate zones left for current_
    QString runDir_;
    bool running_ = false;  // an export of current_ is with the runner (acts_runner.h)
};

// Headless --stock-sky-test <sky> --out <dir>: exports the sky if needed, writes <dir>/stock-sky-test.txt.
int runStockSkyTest(const QString& sky, const QString& outDir);

}  // namespace iw8r::qt
