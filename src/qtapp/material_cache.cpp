#include "material_cache.h"
#include "compile_iw8.h"

#include <future>
#include <chrono>
#include <QMetaObject>
#include <QPointer>

#include "material_blend.h"
#include "stock_decal.h"

#include <cstring>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QSet>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <utility>

#include "acts_runner.h"
#include "texture_decode.h"

namespace iw8r::qt {
namespace {

// Research 133 section 1: a technique set whose hex field carries bit 0x800000000 binds constant 63 (UV animation).
// Read from any long hex token, so the unlit effect sets (unlit_6_effect_...) count too.
bool techsetAnimatesUv(const QString& techset) {
    const QStringList tok = techset.section('/', -1).split('_');
    for (const QString& t : tok) {
        if (t.size() < 9 || t.size() > 12) continue;
        bool ok = false;
        const qulonglong v = t.toULongLong(&ok, 16);
        if (ok && (v & 0x800000000ull)) return true;
    }
    return false;
}

constexpr const char* kIndexFormat = "iw8r-material-cache-1";
// Zone XPaks with a payload are far larger than the 32 KiB headers that most zones carry.
constexpr qint64 kXpakMinBytes = 64 * 1024;

QString safeFileName(const QString& name) {
    QString out;
    out.reserve(name.size());
    for (const QChar c : name) {
        const bool keep = (c.unicode() < 128 && c.isLetterOrNumber()) || c == '_' || c == '-' || c == '.';
        out += keep ? c : QChar('_');
    }
    return out;
}

// Material JSON writes name-only references with a leading ','.
QString bareName(QString name) {
    if (name.startsWith(',')) name.remove(0, 1);
    return name;
}

bool isPlaceholder(const QString& name) { return name.isEmpty() || name.startsWith('$'); }

// The repo's data folder: the first "data/stock_assets" found walking up from the exe.
QString repoDataDir() {
    QDir d(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 4; ++up, d.cdUp())
        if (QFileInfo::exists(d.filePath("data/stock_assets"))) return d.filePath("data");
    return {};
}

QString readAll(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QString reasonOf(const QJsonObject& rec) {
    if (rec.isEmpty()) return "not in zone";
    return QString("%1 %2 %3")
        .arg(rec.value("status").toString(), rec.value("reason").toString(), rec.value("error").toString())
        .trimmed();
}

}  // namespace

MaterialCache::MaterialCache(ToolPathFn tools, QObject* parent) : QObject(parent), tools_(std::move(tools)) {
    root_ = qEnvironmentVariable("IW8R_MATERIAL_CACHE_ROOT",  // the asset audit uses a scratch root
                                 QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/material_cache");
    QDir().mkpath(root_ + "/images");
    QDir(runRoot()).removeRecursively();  // scratch left behind by a crash
    // The index and the seeds are read on a worker thread; index() / seeds() wait for them on first use.
    indexLoad_ = std::async(std::launch::async, [root = root_, data = repoDataDir(), self = QPointer<MaterialCache>(this)] {
        IndexFiles f;
        f.index = readIndex(root);
        f.seeds = readSeeds(data, f.index);
        QMetaObject::invokeMethod(qApp, [self] { if (self) emit self->indexLoaded(); }, Qt::QueuedConnection);
        return f;
    });
}

bool MaterialCache::indexReady() const {
    return !indexLoad_.valid() || indexLoad_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

std::map<QString, MaterialCache::Entry>& MaterialCache::index() const {
    std::call_once(indexOnce_, [this] {  // once, whichever thread asks first
        IndexFiles f = indexLoad_.get();  // waits when the worker is not done yet
        indexData_ = std::move(f.index);
        seedsData_ = std::move(f.seeds);
        if (purgeNotListed_)  // addMapZone before the index was there
            for (auto it = indexData_.begin(); it != indexData_.end();)
                it = it->second.failed && it->second.error.contains("not listed") ? indexData_.erase(it) : std::next(it);
    });
    return indexData_;
}

std::map<QString, MaterialCache::Entry>& MaterialCache::seeds() const {
    index();
    return seedsData_;
}

MaterialCache::~MaterialCache() {
    // Shutting down: runs that end from now on must not reach finishRun (it touches widgets that are already
    // gone). Queued requests are dropped; the runner does not call back for an owner that is gone.
    blockSignals(true);
    if (QCoreApplication::instance()) ActsRunner::shared()->cancel(this);
}

bool MaterialCache::mapLocalName(const QString& material) {
    static const QRegularExpression re(QStringLiteral("^twc?4/\\d+d?n(_\\d+d?n)*$"), QRegularExpression::CaseInsensitiveOption);
    return re.match(material.trimmed()).hasMatch();
}

QString MaterialCache::indexKey(const QString& material) const {
    return mapLocalName(material) && !mapZones_.isEmpty() ? mapZones_.first() + "|" + material.trimmed() : material.trimmed();
}

QString MaterialCache::runRoot() const {
    // ACTS writes <run>/mw19replay/<zone>/assets/image/<packed image name>.<n>.dds: packed names reach 130 characters,
    // so a run folder under a deep cache root went past Windows' 260 (ACTS: "Cannot write Replay asset"; mp_rust's
    // corrugated metal stayed the missing-material checker). Runs go to a short folder of the system temp instead.
    return QDir::tempPath() + "/iw8m" + QString::number(quintptr(this) & 0xFFFFF, 16);
}

std::optional<MaterialCache::Entry> MaterialCache::lookup(const QString& material) const {
    const bool local = mapLocalName(material);
    if (local && mapZones_.isEmpty()) return std::nullopt;  // which map's blend: unknown without a stock map
    if (auto it = index().find(indexKey(material)); it != index().end()) return it->second;
    if (auto it = local ? seeds().end() : seeds().find(material); it != seeds().end()) return it->second;
    return std::nullopt;
}

std::vector<MaterialCache::LayerImages> MaterialCache::layerImages(const QString& material) const {
    const auto e = lookup(material);
    if (!e || e->failed || std::max(e->layerCount, e->modelLayers) < 2) return {};
    return e->layers;
}

std::optional<StockLayeredMaterial> MaterialCache::materialConstants(const QString& material) const {
    const auto e = lookup(material);
    if (!e || e->failed || e->psData.isEmpty()) return std::nullopt;
    const std::vector<uint8_t> bytes(e->psData.begin(), e->psData.end());
    StockLayeredMaterial m;
    if (!decodeStockLayeredMaterial(bytes, std::clamp(e->layerCount, 1, 8), m)) return std::nullopt;
    return m;
}

std::vector<QString> MaterialCache::layerColorImages(const QString& material) const {
    std::vector<QString> out;
    for (const LayerImages& l : layerImages(material)) out.push_back(l.colorDds);
    return out;
}

std::optional<StockLayeredMaterial> MaterialCache::layerConstants(const QString& material) const {
    const auto e = lookup(material);
    if (e && !e->failed && e->layerCount < 2 && e->modelLayers >= 2 && !e->layerPsData.isEmpty()) {
        // A model-layered material: its raw lit buffer, N layers on one UV set (research 107 section 3.3).
        const std::vector<uint8_t> raw(e->layerPsData.begin(), e->layerPsData.end());
        StockLayeredMaterial m;
        if (!decodeStockLayeredMaterial(raw, e->modelLayers, m)) return std::nullopt;
        return m;
    }
    if (!e || e->failed || e->layerCount < 2 || e->psData.isEmpty()) return std::nullopt;
    const std::vector<uint8_t> bytes(e->psData.begin(), e->psData.end());
    StockLayeredMaterial m;
    if (!decodeStockLayeredMaterial(bytes, e->layerCount, m)) return std::nullopt;
    return m;
}

void MaterialCache::request(const QStringList& materials) {
    bool added = false;
    for (QString m : materials) {
        m = m.trimmed();
        if (m.isEmpty() || lookup(m) || queued_.count(indexKey(m))) continue;
        if (mapLocalName(m) && mapZones_.isEmpty()) continue;  // no stock map: nothing to resolve it in
        queued_.insert(indexKey(m));
        pending_.append(m);
        added = true;
    }
    if (added && !running_) startBatch();
}

void MaterialCache::addMapZone(const QString& zone) {
    if (zone.isEmpty() || mapZones_.contains(zone)) return;
    mapZones_.prepend(zone);
    // Materials that failed as "not listed" are tried again (the map's zone may hold them). While the index is still
    // read on its worker thread this is done when it is taken (index()), not waited for here.
    if (!indexReady()) {
        purgeNotListed_ = true;
        return;
    }
    for (auto it = index().begin(); it != index().end();)
        it = it->second.failed && it->second.error.contains("not listed") ? index().erase(it) : std::next(it);
}

bool MaterialCache::busy() const { return running_ || !pending_.isEmpty(); }

QString MaterialCache::cacheRoot() const { return root_; }

// ---------------------------------------------------------------------------------------------------------------
// Index, seeds and catalog

std::map<QString, MaterialCache::Entry> MaterialCache::readIndex(const QString& root) {
    std::map<QString, Entry> out;
    QFile f(root + "/index.json");
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonObject mats = QJsonDocument::fromJson(f.readAll()).object().value("materials").toObject();
    // The image folder is listed once: a file check per entry (thousands of them) was most of this read.
    QSet<QString> images;
    const QString imagesDir = QDir::cleanPath(root + "/images").toLower() + "/";
    for (QDirIterator it(root + "/images", QDir::Files); it.hasNext();) images.insert(QDir::cleanPath(it.next()).toLower());
    const auto exists = [&](const QString& file) {
        const QString c = QDir::cleanPath(file).toLower();
        return c.startsWith(imagesDir) ? images.contains(c) : QFileInfo::exists(file);
    };
    for (auto it = mats.begin(); it != mats.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.material = it.key().section('|', -1);  // "<map zone>|<name>" for map-local names
        e.zone = o.value("zone").toString();
        e.techset = o.value("techset").toString();
        const QString color = o.value("color").toString();
        const QString normal = o.value("normal").toString();
        e.colorDds = color.isEmpty() ? QString() : QDir(root).filePath(color);
        e.normalDds = normal.isEmpty() ? QString() : QDir(root).filePath(normal);
        const QString mask = o.value("mask").toString();
        e.maskDds = mask.isEmpty() ? QString() : QDir(root).filePath(mask);
        const QString emissive = o.value("emissive").toString();
        e.emissiveDds = emissive.isEmpty() ? QString() : QDir(root).filePath(emissive);
        const QString transmission = o.value("transmission").toString();
        e.transmissionDds = transmission.isEmpty() ? QString() : QDir(root).filePath(transmission);
        const QString distortion = o.value("distortion").toString();
        e.distortionDds = distortion.isEmpty() ? QString() : QDir(root).filePath(distortion);
        const QJsonObject cs = o.value("consts").toObject();
        for (auto it = cs.begin(); it != cs.end(); ++it) {
            const QJsonArray a = it.value().toArray();
            if (a.size() == 4) e.consts[it.key().toInt()] = {float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()), float(a[3].toDouble())};
        }
        for (const QJsonValue& t : o.value("textures").toArray()) e.textures << t.toString();
        e.layerCount = o.value("layerCount").toInt(0);
        const auto cached = [&](const QString& rel) { return rel.isEmpty() ? QString() : QDir(root).filePath(rel); };
        bool layerImageGone = false;
        for (const QJsonValue& v : o.value("layers").toArray()) {
            const QJsonObject l = v.toObject();
            LayerImages li{cached(l.value("color").toString()), cached(l.value("normal").toString()),
                           cached(l.value("reveal").toString())};
            for (const QString& f : {li.colorDds, li.normalDds, li.revealDds})
                if (!f.isEmpty() && !exists(f)) layerImageGone = true;
            e.layers.push_back(li);
        }
        e.psData = QByteArray::fromHex(o.value("psData").toString().toLatin1());
        e.modelLayers = o.value("modelLayers").toInt(0);
        e.layerPsData = QByteArray::fromHex(o.value("layerPsData").toString().toLatin1());
        e.tint = o.contains("tint") ? o.value("tint").toString().toInt(nullptr, 16) : -1;
        e.tintMasked = o.value("tintMasked").toBool();
        if (const QString tm = o.value("tintMask").toString(); !tm.isEmpty()) e.tintMaskDds = QDir(root).filePath(tm);
        e.surfaceFlags = o.contains("surfaceFlags") ? qint64(o.value("surfaceFlags").toDouble()) : -1;
        e.materialType = o.value("materialType").toString();
        e.sortKey = o.value("sortKey").toInt(-1);
        e.failed = o.value("failed").toBool();
        // Resolved before every body's constants and tint were kept (decals, painted materials): resolve again once.
        if (!e.failed && o.value("tintRules").toInt() < 2) continue;  // 2: tint from psData row 2, the slot-59 mask
        // Images removed by the Cache Manager's size cap (4.13) or by hand: resolve again.
        if (!e.failed && !e.colorDds.isEmpty() && !exists(e.colorDds)) continue;
        // Resolved before cut-out masks were recorded: resolve again once (images already cached are reused).
        if (!e.failed && !o.contains("mask")) continue;
        // Resolved before slot-27 opacity masks and the body flags were kept (research 66): resolve again once.
        if (!e.failed && o.value("maskRules").toInt() < 4) continue;  // 4: + emissive (slot 18)
        // Resolved before the lit buffer and the constants by index were kept (research 107): resolve again once.
        if (!e.failed && o.value("constRules").toInt() < 2) continue;  // 2: non-finite words in memory byte order
        // Resolved before constant 63 was kept (research 133, UV animation): those technique sets resolve again once.
        if (!e.failed && o.value("constRules").toInt() < 3 && techsetAnimatesUv(e.techset)) continue;
        // Read from a zone that an installed edit replaced (its technique sets renamed iw8rf_<hash>; the cut-out masks
        // were lost): resolve again from the stock zone (stockZoneFile).
        if (!e.failed && e.techset.section('/', -1).startsWith("iw8rf_")) continue;
        // Resolved before model-layered materials kept their layers (research 132 G9): those resolve again once.
        if (!e.failed && !o.contains("modelLayers") && e.layerCount < 2) {
            const StockTechsetTraits tr = parseStockTechsetName(e.techset.toStdString());
            if (tr.valid && tr.layers >= 2 && (tr.family.rfind("m", 0) == 0 || tr.family.rfind("tm", 0) == 0)) continue;
        }
        // Resolved while a family with a variant mark ("~tm2o": spline models, ",mo", "#tmo") was not read as a model
        // family: its extra layers were dropped. Those resolve again once.
        if (!e.failed && o.value("modelLayers").toInt() < 2 && e.layerCount < 2 && !e.techset.isEmpty() &&
            (e.techset[0] == '~' || e.techset[0] == ',' || e.techset[0] == '#')) {
            const StockTechsetTraits tr = parseStockTechsetName(e.techset.toStdString());
            if (tr.valid && tr.layers >= 2 && (tr.family.rfind("m", 0) == 0 || tr.family.rfind("tm", 0) == 0)) continue;
        }
        // Resolved before the wave constants 79-82 were kept (research 133 section 6): the vertex-colour model families
        // (tmco, tm2co, mco, ...: the vertex alpha is the wave weight) resolve again once.
        if (!e.failed && o.value("constRules").toInt() < 4) {
            const QString prefix = QString::fromStdString(stockFamilyOf(e.material.toStdString())), leaf = e.material.section('/', 1).toLower();
            static const QStringList kWaveWords = {"flag", "tarp", "foliage", "canopy", "curtain", "fabric", "cloth", "grass", "vine", "leaves", "tree"};
            bool waveName = false;
            for (const QString& w : kWaveWords) waveName = waveName || leaf.contains(w);
            if ((prefix.contains('c') || waveName) && (prefix.startsWith('m') || prefix.startsWith("tm"))) continue;
        }
        // Resolved before the water's dual normal constants 110 / 111 were kept (research 133 section 5.3): world water
        // materials resolve again once.
        if (!e.failed && o.value("constRules").toInt() < 5 && e.material.startsWith("tw") && e.material.toLower().contains("water")) continue;
        // Resolved before code layers (clear coats, research 137) got canonical layer rows: those resolve again once.
        if (!e.failed && o.value("constRules").toInt() < 6 && o.value("modelLayers").toInt() >= 2) {
            const ModelLayerTokens tk = parseModelLayerTokens(e.techset.toStdString());
            bool code = false;
            for (const ModelLayerKind k : tk.kind) code = code || k == ModelLayerKind::Code;
            if (code) continue;
        }
        // Resolved before a distortion set's slot 157 image and constant 127 were kept (research 139): resolve again once.
        if (!e.failed && o.value("constRules").toInt() < 9 && e.techset.contains("dist_10")) continue;
        // Resolved before constant 94 (the atlas of effect materials, research 138) was kept: effect sets resolve again once.
        if (!e.failed && o.value("constRules").toInt() < 8 && e.techset.contains("_effect_")) continue;
        // Resolved before constant 45 was kept (static-model displacement, research 133 section 3): model materials with
        // a slot-95 height image resolve again once.
        if (!e.failed && o.value("constRules").toInt() < 7 && !e.techset.startsWith("tw")) {
            bool height = false;
            for (const QString& t : e.textures) height = height || t.startsWith("95=");
            if (height) continue;
        }
        // Layered materials (tw4 / twc4) resolved before their layer images and constants were kept (research 97), or
        // whose layer images were removed: resolve again once (images already cached are reused).
        if (!e.failed && (o.value("layerRules").toInt() < 1 || layerImageGone) &&
            (e.techset.startsWith("tw4/") || e.techset.startsWith("twc4/") || e.layerCount >= 2))
            continue;
        e.error = o.value("error").toString();
        // A body that was in no zone is retried once per session: the colour-image-by-name fallback may resolve it.
        if (e.failed && (e.error.contains("not in zone") || e.error.contains("body not found"))) continue;
        // Not listed in the catalog: a stock map reference may name its zone (addMapZone), so try again.
        if (e.failed && e.error.contains("not listed")) continue;
        // Images looked up only in the material's zone (streamed images live in global_stream_mp, shared ones in
        // common zones): resolve again, the image catalog now adds the zones that hold the image data.
        if (e.colorDds.isEmpty() && e.error.contains("not exported") && o.value("maskRules").toInt() < 2) continue;
        // A map-local name cached before it was keyed per map: whichever map the catalog named first. Dropped.
        if (mapLocalName(e.material) && !it.key().contains('|')) continue;
        out[it.key()] = e;
    }
    return out;
}

// data/stock_assets/<zone>/index.json (tools/acts_asset_index.py) already holds the colour of 371 materials.
std::map<QString, MaterialCache::Entry> MaterialCache::readSeeds(const QString& data, const std::map<QString, Entry>& index) {
    std::map<QString, Entry> out;
    if (data.isEmpty()) return out;
    const QDir stock(QDir(data).filePath("stock_assets"));
    for (const QString& zone : stock.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QJsonObject root = QJsonDocument::fromJson(readAll(stock.filePath(zone + "/index.json")).toUtf8()).object();
        if (root.isEmpty()) continue;
        QMap<QString, QString> imageFile;  // image name -> path relative to the zone folder
        for (const QJsonValue& v : root.value("images").toArray())
            imageFile.insert(v.toObject().value("name").toString(), v.toObject().value("file").toString());
        for (const QJsonValue& v : root.value("materials").toArray()) {
            const QJsonObject m = v.toObject();
            const QString name = m.value("name").toString();
            if (name.isEmpty() || index.count(name)) continue;
            Entry e;
            e.material = name;
            e.zone = zone;
            e.techset = m.value("techset").toString();
            const QString color = m.value("color_image_file").toString();
            if (!color.isEmpty() && QFileInfo::exists(stock.filePath(zone + "/" + color)))
                e.colorDds = stock.filePath(zone + "/" + color);
            const QString slot9 = m.value("images").toObject().value("slot_9").toString();
            if (!isPlaceholder(slot9) && imageFile.contains(slot9)) {
                const QString path = stock.filePath(zone + "/" + imageFile.value(slot9));
                if (QFileInfo::exists(path)) e.normalDds = path;
            }
            out[name] = e;
        }
    }
    return out;
}

void MaterialCache::loadCatalog() {
    catalogLoaded_ = true;
    const QString data = repoDataDir();
    if (data.isEmpty()) return;
    QFile f(QDir(data).filePath("asset_catalog/iw8_materials.tsv"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&f);
    in.readLine();  // header; names themselves may start with '#'
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const int tab = line.indexOf('\t');
        if (tab <= 0) continue;
        catalog_[line.left(tab)] = line.mid(tab + 1).trimmed().split(',', Qt::SkipEmptyParts);
    }
}

// A copy of the index is taken here; it is turned into JSON and written on a worker thread (13 MB: about 1.2 s on the
// GUI thread after every export batch of a stock map). One writer at a time; a newer copy replaces one not written yet.
// The destructor waits for the writer (writeJob_), so the last batch is on disk when the editor closes.
void MaterialCache::writeIndex() const {
    auto copy = std::make_shared<const std::map<QString, Entry>>(index());
    std::lock_guard lock(writeMutex_);
    writeNext_ = std::move(copy);
    if (writing_) return;  // the running writer takes it next
    writing_ = true;
    writeJob_ = std::async(std::launch::async, [this, root = root_] {
        for (;;) {
            std::shared_ptr<const std::map<QString, Entry>> next;
            {
                std::lock_guard l(writeMutex_);
                next = std::move(writeNext_);
                if (!next) {
                    writing_ = false;
                    return;
                }
            }
            writeIndexFile(root, *next);
        }
    });
}

void MaterialCache::writeIndexFile(const QString& rootPath, const std::map<QString, Entry>& index) {
    QJsonObject mats;
    const QDir root(rootPath);
    for (const auto& [name, e] : index) {
        QJsonObject o;
        o.insert("zone", e.zone);
        o.insert("techset", e.techset);
        o.insert("color", e.colorDds.isEmpty() ? QString() : root.relativeFilePath(e.colorDds));
        o.insert("normal", e.normalDds.isEmpty() ? QString() : root.relativeFilePath(e.normalDds));
        if (!e.failed) o.insert("mask", e.maskDds.isEmpty() ? QString() : root.relativeFilePath(e.maskDds));
        if (!e.failed) o.insert("maskRules", 4);  // 3: slot-27 opacity masks + body flags (research 66); 4: + emissive
        if (!e.failed && !e.emissiveDds.isEmpty()) o.insert("emissive", root.relativeFilePath(e.emissiveDds));
        if (!e.failed && !e.transmissionDds.isEmpty()) o.insert("transmission", root.relativeFilePath(e.transmissionDds));
        if (!e.failed && !e.distortionDds.isEmpty()) o.insert("distortion", root.relativeFilePath(e.distortionDds));
        if (!e.failed) {
            o.insert("constRules", 9);  // 1: the lit buffer, canonical rows, constants 18 / 36 / 103 / 104, slot 68; 2: bits order; 3: + 63; 4: + 79-82; 5: + 110 / 111; 6: code-layer rows; 7: + 45; 8: + 94; 9: + 127, slot 157
            QJsonObject cs;
            for (const auto& [k, f] : e.consts) cs.insert(QString::number(k), QJsonArray{f[0], f[1], f[2], f[3]});
            if (!cs.isEmpty()) o.insert("consts", cs);
        }
        if (e.surfaceFlags >= 0) o.insert("surfaceFlags", double(e.surfaceFlags));
        if (!e.materialType.isEmpty()) o.insert("materialType", e.materialType);
        if (e.sortKey >= 0) o.insert("sortKey", e.sortKey);
        if (!e.textures.isEmpty()) o.insert("textures", QJsonArray::fromStringList(e.textures));
        if (e.layerCount > 0) o.insert("layerCount", e.layerCount);
        if (!e.layers.empty()) {
            QJsonArray layers;
            const auto rel = [&](const QString& f) { return f.isEmpty() ? QString() : root.relativeFilePath(f); };
            for (const LayerImages& l : e.layers)
                layers.append(QJsonObject{{"color", rel(l.colorDds)}, {"normal", rel(l.normalDds)}, {"reveal", rel(l.revealDds)}});
            o.insert("layers", layers);
        }
        if (!e.psData.isEmpty()) o.insert("psData", QString::fromLatin1(e.psData.toHex()));
        if (!e.failed) o.insert("modelLayers", e.modelLayers);  // also 0: the name was read (research 107 section 3)
        if (!e.layerPsData.isEmpty()) o.insert("layerPsData", QString::fromLatin1(e.layerPsData.toHex()));
        if (!e.failed) o.insert("layerRules", 1);  // 1: layer images and psData of layered bodies (research 97)
        if (!e.failed) o.insert("tintRules", 2);   // 2: every body's psData, tint (psData row 2) and its mask image
        if (!e.tintMaskDds.isEmpty()) o.insert("tintMask", root.relativeFilePath(e.tintMaskDds));
        if (e.tint >= 0) o.insert("tint", QString("%1").arg(e.tint, 6, 16, QChar('0')));
        if (e.tintMasked) o.insert("tintMasked", true);
        o.insert("failed", e.failed);
        if (!e.error.isEmpty()) o.insert("error", e.error);
        mats.insert(name, o);
    }
    QJsonObject top;
    top.insert("format", kIndexFormat);
    top.insert("materials", mats);
    QSaveFile f(rootPath + "/index.json");
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(top).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

QString MaterialCache::replayZoneDir() const { return QFileInfo(tools_("replay")).absolutePath() + "/zone"; }

// Body and image zones for the catalog zones of one material. A material body is usually in techsets_<zone>.ff
// (the catalog names the map zone that references it); images are usually in the map zone itself.
QStringList MaterialCache::candidates(const QStringList& zones, bool bodies) const {
    const QString dir = replayZoneDir();
    QStringList out;
    const auto add = [&](const QString& z) {
        if (!out.contains(z) && QFileInfo::exists(dir + "/" + z + ".ff")) out << z;
    };
    for (const QString& z : zones) {
        const QString techZone = z.startsWith("techsets_") ? z : "techsets_" + z;
        // A techsets_ zone holds no image payload: its map zone does (Verdansk's grass image is catalogued only in
        // techsets_mp_donetsk and exports from mp_donetsk with the XPaks).
        const QString mapZone = z.startsWith("techsets_") ? z.mid(9) : z;
        if (bodies) {
            add(techZone);
            add(mapZone);
        } else {
            add(mapZone);
            add(techZone);
        }
    }
    return out;
}

// Zone XPaks that carry payloads. Stream keys are spread over several of them (see docs/research/20).
QList<QFileInfo> MaterialCache::xpaks() const {
    QList<QFileInfo> out;
    for (const QFileInfo& f : QDir(replayZoneDir()).entryInfoList({"*.xpak"}, QDir::Files, QDir::Name))
        if (f.size() >= kXpakMinBytes) out << f;
    return out;
}

QString MaterialCache::imagePath(const QString& imageName) const {
    return root_ + "/images/" + safeFileName(imageName) + ".dds";
}

// ---------------------------------------------------------------------------------------------------------------
// Batches

void MaterialCache::startBatch() {
    running_ = true;
    works_.clear();
    images_.clear();
    queue_.clear();
    imagesListed_ = false;
    imagePhase_ = false;
    if (!catalogLoaded_) loadCatalog();

    // At most kBatchMaterials per batch: a stock map reference asks for ~2,000 materials, and nothing is reported
    // ready until a batch ends, so small batches let textures appear progressively (the rest start right after).
    constexpr int kBatchMaterials = 64;
    const QStringList mats = pending_.mid(0, kBatchMaterials);
    pending_ = pending_.mid(kBatchMaterials);
    const QString acts = tools_("acts"), replay = tools_("replay"), oodle = tools_("oodle");
    for (const QString& f : {acts, replay, oodle})
        if (!QFileInfo::exists(f)) {
            // Environment problem, not a property of the material: nothing is cached.
            emit progress("Missing tool, material cache idle: " + f);
            for (const QString& m : mats) queued_.erase(indexKey(m));
            for (const QString& m : pending_) queued_.erase(indexKey(m));
            pending_.clear();
            running_ = false;
            return;
        }

    for (const QString& m : mats) {
        Work w;
        w.material = m;
        w.key = indexKey(m);
        // A map-local name only in the stock map's own zones; others from the catalog, else the map's zones.
        QStringList zones = mapLocalName(m) ? QStringList() : catalog_.count(m) ? catalog_.at(m) : QStringList();
        if (zones.isEmpty()) zones = mapLocalName(m) && !mapZones_.isEmpty() ? QStringList{mapZones_.first()} : mapZones_;
        if (zones.isEmpty()) w.error = "not listed in iw8_materials.tsv";
        w.bodyZones = candidates(zones, true);
        w.imageZones = candidates(zones, false);
        if (w.bodyZones.isEmpty() && w.error.isEmpty()) w.error = "no zone .ff in the replay zone folder for: " + zones.join(",");
        works_.push_back(w);
    }
    emit progress(QString("cache: %1 material(s) to resolve").arg(mats.size()));
    pump();
}

void MaterialCache::buildBodyRound() {
    std::map<QString, QStringList> groups;
    for (const Work& w : works_)
        if (!w.found && w.attempt < w.bodyZones.size()) groups[w.bodyZones[w.attempt]] << w.material;
    for (const auto& [zone, names] : groups) queue_.push_back(Run{false, zone, names});
}

void MaterialCache::loadImageCatalog() {
    imageCatalogLoaded_ = true;
    const QString data = repoDataDir();
    if (data.isEmpty()) return;
    QFile f(QDir(data).filePath("asset_catalog/iw8_images.tsv"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const int tab = line.indexOf('	');
        if (tab <= 0 || line.startsWith('#')) continue;
        const QString name = line.left(tab);
        imageZonesByName_[name.toLower()] = line.mid(tab + 1).trimmed().split(',', Qt::SkipEmptyParts);
        const int amp = name.indexOf('&');
        const QString stem = (amp > 0 ? name.left(amp) : name).toLower();
        if (!stem.endsWith("_c")) continue;
        colorByStem_.emplace(stem, std::make_pair(name, line.mid(tab + 1).trimmed().split(',', Qt::SkipEmptyParts)));
    }
}

void MaterialCache::buildImageRound() {
    if (!imagesListed_) {
        // Bodies exhausted without a match: use the colour image named after the material (mo/<x> -> <x>_c&...).
        for (Work& w : works_) {
            if (w.found || (w.attempt < w.bodyZones.size() && !w.bodyZones.isEmpty())) continue;
            if (!imageCatalogLoaded_) loadImageCatalog();
            const QString stem = w.material.section('/', -1).toLower() + "_c";
            const auto it = colorByStem_.find(stem);
            if (it == colorByStem_.end()) continue;
            w.found = true;
            w.color = it->second.first;
            w.normal.clear();
            w.techset = "(colour image by name; material body not found)";
            w.imageZones = candidates(it->second.second, false);
            w.error.clear();
        }
        imagesListed_ = true;
        for (const Work& w : works_) {
            if (!w.found) continue;
            QStringList names{w.color, w.normal, w.mask, w.emissive, w.transmission, w.distortion};
            if (w.tintMasked && w.tint >= 0) names << w.tintMask;  // only a tinted material needs its mask
            names << w.layerColor << w.layerNormal << w.layerReveal;  // layered materials (research 97)
            for (const QString& name : names) {
                if (name.isEmpty() || images_.count(name)) continue;
                Image im;
                im.zones = w.imageZones;
                if (!imageCatalogLoaded_) loadImageCatalog();
                if (const auto z = imageZonesByName_.find(name.toLower()); z != imageZonesByName_.end())
                    for (const QString& zone : candidates(z->second, false))
                        if (!im.zones.contains(zone)) im.zones << zone;
                const QString file = imagePath(name);
                if (QFileInfo::exists(file)) {  // shared with an earlier material
                    im.done = true;
                    im.file = file;
                }
                images_[name] = im;
            }
        }
    }
    std::map<QString, QStringList> groups;
    for (const auto& [name, im] : images_)
        if (!im.done && im.attempt < im.zones.size()) groups[im.zones[im.attempt]] << name;
    for (const auto& [zone, names] : groups) queue_.push_back(Run{true, zone, names});
}

void MaterialCache::pump() {
    for (;;) {
        if (!queue_.empty()) {
            // The whole round at once: the runner gives each zone to a server that holds it (or loads it), several
            // zones at a time. The next round is built when the last answer is in.
            const std::deque<Run> round = std::move(queue_);
            queue_.clear();
            outstanding_ = int(round.size());
            for (const Run& run : round) startRun(run);
            return;
        }
        if (!imagePhase_) {
            buildBodyRound();
            if (!queue_.empty()) continue;
            imagePhase_ = true;
        }
        buildImageRound();
        if (queue_.empty()) break;
    }
    finishBatch();
}

void MaterialCache::startRun(const Run& run) {
    const QString dir = QDir::toNativeSeparators(runRoot() + "/" + QString::number(++runSeq_));
    QDir().mkpath(dir);

    ActsRequest request;
    request.zone = run.zone;
    request.types = run.images ? "image" : "material";
    request.names = run.names;
    request.outDir = dir;
    request.xpaks = run.images;  // bodies are resident in the fastfile; image payloads are streamed from the XPaks
    emit progress(QString("ACTS %1 %2: %3 name(s)")
                      .arg(QString(run.images ? "images" : "materials"), run.zone)
                      .arg(run.names.size()));
    ActsRunner::shared()->submit(tools_, request, this, [this, run, dir](const ActsResult& result) { finishRun(run, dir, result); });
}

bool MaterialCache::loadBody(Work& work, const QString& zone, const QString& jsonPath) const {
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject fields = QJsonDocument::fromJson(f.readAll()).object().value("asset").toObject().value("fields").toObject();
    if (fields.isEmpty()) return false;
    work.zone = zone;
    work.techset = fields.value("techniqueSet").toObject().value("name").toString();
    work.surfaceFlags = fields.contains("surfaceFlags") ? qint64(fields.value("surfaceFlags").toDouble()) : -1;
    work.materialType = fields.value("materialType").toObject().value("name").toString();
    work.sortKey = fields.value("sortKey").toInt(-1);
    // Research 66: slot 27 is the opacity source of every alpha-tested (alt) set and of blended (bad / bar) sets: a
    // one-channel image (_sdf, _opacity, _a, _m, ...) or a colour image whose alpha is the opacity.
    const MaterialBlend blend = materialBlendFromTechset(work.techset.toStdString());
    // A name without a blend token (a renamed or hashed set) still has its opacity in slot 27: no stock opaque set
    // binds that slot.
    const bool opacitySlot = blend != MaterialBlend::Opaque || !materialBlendKnown(work.techset.toStdString());
    // Layered world materials (tw4 / twc4, research 97): layer L's colour is slot L, its normal + gloss 9 + L, its
    // alpha + reveal 27 + L; the per-layer constants are the body's pixel constant buffer.
    work.layerCount = fields.value("layerCount").toInt(0);
    // Model-layered materials (research 107 section 3.3): layerCount 0, the count in the technique set name.
    work.modelLayers = 0;
    work.layerPsData.clear();
    if (work.layerCount < 2) {
        const StockTechsetTraits tr = parseStockTechsetName(work.techset.toStdString());
        if (tr.valid && tr.layers >= 2 && (tr.family.rfind("m", 0) == 0 || tr.family.rfind("tm", 0) == 0))
            work.modelLayers = std::min(tr.layers, 8);
    }
    const int layers = std::clamp(std::max(work.layerCount, work.modelLayers), 0, 8);
    work.layerColor.clear();
    work.layerNormal.clear();
    work.layerReveal.clear();
    work.psData.clear();
    if (layers >= 2) {
        for (int l = 0; l < layers; ++l) {
            work.layerColor << QString();
            work.layerNormal << QString();
            work.layerReveal << QString();
        }
    }
    // The lit technique's buffer (research 107 section 6: constantBufferIndex[34], TECHNIQUE_LIT_FORWARDPLUS_BITMASK);
    // else the first buffer with pixel data (a decal's flags and ramp, research 98; displacement layers, `..._129dn_...`
    // slot 96, have a vertex-only buffer 0).
    const QJsonArray cbt = fields.value("constantBufferTable").toObject().value("values").toArray();
    {
        const QByteArray cbi = QByteArray::fromHex(fields.value("constantBufferIndex").toObject().value("bytes").toString().toLatin1());
        const int lit = cbi.size() > 34 ? int(uint8_t(cbi[34])) : 255;
        if (lit < cbt.size())
            work.psData = QByteArray::fromHex(cbt[lit].toObject().value("psData").toObject().value("bytes").toString().toLatin1());
    }
    if (work.psData.isEmpty())
        for (const QJsonValue& b : cbt) {
            work.psData = QByteArray::fromHex(b.toObject().value("psData").toObject().value("bytes").toString().toLatin1());
            if (!work.psData.isEmpty()) break;
        }
    // The constants by index (research 107): literal floats, a non-finite word as {"bits": "<hex>"}.
    work.consts.clear();
    std::map<int, QByteArray> constBytes;
    for (const QJsonValue& cv : fields.value("constantTable").toObject().value("values").toArray()) {
        const QJsonObject c = cv.toObject();
        const int index = c.value("index").toInt(-1);
        const QJsonArray v = c.value("literal").toObject().value("v").toArray();
        if (index < 0 || v.size() != 4) continue;
        QByteArray bytes(16, '\0');
        std::array<float, 4> f{0, 0, 0, 0};
        bool finite = true;
        for (int k = 0; k < 4; ++k) {
            uint32_t u = 0;
            if (v[k].isDouble()) {
                f[size_t(k)] = float(v[k].toDouble());
                std::memcpy(&u, &f[size_t(k)], 4);
            } else {
                // ACTS writes a non-finite word as its bytes in memory order ("c1ad96ff" = c1 ad 96 ff): read as bytes,
                // not as a number (which reversed them: Speedball's grey containers came out pink, the blue ones
                // orange; research 113).
                const QByteArray raw = QByteArray::fromHex(v[k].toObject().value("bits").toString().toLatin1());
                if (raw.size() == 4) std::memcpy(&u, raw.constData(), 4);
                finite = false;
            }
            std::memcpy(bytes.data() + 4 * k, &u, 4);
        }
        constBytes[index] = bytes;
        if (finite && (index == 18 || index == 36 || index == 45 || index == 63 || (index >= 79 && index <= 82) || index == 94 ||
                       index == 103 || index == 104 || index == 110 || index == 111 || index == 127))
            work.consts[index] = f;
    }
    if (work.modelLayers >= 2) work.layerPsData = work.psData;  // the raw lit buffer, before the single-layer rows below
    // Research 137: in technique sets with a code layer (a clear coat) the layer constants do not sit on rows 1 + 3 L
    // .. 3 + 3 L: rebuild them from the constant table (row 1 + 3 L <- L, 2 + 3 L <- 9 + L, 3 + 3 L <- 54 + L).
    if (work.modelLayers >= 2 && !constBytes.empty()) {
        const ModelLayerTokens tk = parseModelLayerTokens(work.techset.toStdString());
        bool code = false;
        for (const ModelLayerKind k : tk.kind) code = code || k == ModelLayerKind::Code;
        if (code) {
            QByteArray canon = work.layerPsData;
            while (canon.size() < 16 * (1 + 3 * work.modelLayers)) canon.append(char(0));
            for (int L = 0; L < work.modelLayers; ++L) {
                const int src[3] = {L, 9 + L, 54 + L};
                for (int r = 0; r < 3; ++r)
                    if (const auto it = constBytes.find(src[r]); it != constBytes.end()) canon.replace(16 * (1 + 3 * L + r), 16, it->second);
            }
            work.layerPsData = canon;
        }
    }
    // Single-layer materials: the rows in the order the decoders read (row 0 c72, 1 c0, 2 c9, 3 c54). The lit
    // technique puts transmission (18) and emissive (36) constants before 54, which then sits on row 4 to 6.
    if (std::clamp(work.layerCount, 0, 8) < 2 && !constBytes.empty()) {
        QByteArray canon = work.psData.left(64);
        while (canon.size() < 64) canon.append(char(0));
        const int order[4] = {72, 0, 9, 54};
        for (int r = 0; r < 4; ++r)
            if (const auto it = constBytes.find(order[r]); it != constBytes.end()) canon.replace(16 * r, 16, it->second);
        if (!constBytes.count(54)) canon.truncate(48);  // no spec / gloss remap: the decoder reports none
        work.psData = canon;
    }
    // The colour tint: psData row 2 .x bytes 0..2 (research 95; models use the same rows), the mask with row 1 .x bit 6;
    // else constant 9's literal (tools/build_material_tints.py). White = neutral.
    work.tint = -1;
    work.tintMasked = false;
    work.tintMask.clear();
    if (work.psData.size() >= 48) {
        const auto* p = reinterpret_cast<const uint8_t*>(work.psData.constData());
        if (!(p[32] == 255 && p[33] == 255 && p[34] == 255)) work.tint = (p[32] << 16) | (p[33] << 8) | p[34];
        work.tintMasked = (p[16] & 64) != 0;
    }
    for (const QJsonValue& cv : fields.value("constantTable").toObject().value("values").toArray()) {
        const QJsonObject c = cv.toObject();
        if (c.value("index").toInt(-1) != 9) continue;
        const QJsonArray v = c.value("literal").toObject().value("v").toArray();
        if (v.isEmpty() || !v[0].isDouble()) break;
        const float f = float(v[0].toDouble());
        uint8_t b[4];
        std::memcpy(b, &f, 4);
        if (work.tint < 0 && b[3] == 0 && !(b[0] == 255 && b[1] == 255 && b[2] == 255)) work.tint = (b[0] << 16) | (b[1] << 8) | b[2];
        break;
    }
    for (const QJsonValue& v : fields.value("textureTable").toObject().value("values").toArray()) {
        const QJsonObject t = v.toObject();
        const int slot = t.value("index").toInt(-1);
        const QString name = bareName(t.value("image").toObject().value("name").toString());
        if (isPlaceholder(name)) continue;
        work.textures << QString::number(slot) + "=" + name;
        if (layers >= 2) {
            if (slot >= 0 && slot < layers) work.layerColor[slot] = name;
            else if (slot >= 9 && slot < 9 + layers) work.layerNormal[slot - 9] = name;
            else if (slot >= 28 && slot < 27 + layers) work.layerReveal[slot - 27] = name;
        }
        if (slot == 0 && work.color.isEmpty()) work.color = name;  // colour (albedo) slot
        if (slot == 9 && work.normal.isEmpty()) work.normal = name;  // normal slot
        if (slot == 27 && opacitySlot) work.mask = name;              // opacity slot (wins over a name match)
        if (slot == 18 && work.emissive.isEmpty()) work.emissive = name;  // emissive slot
        if (slot == 68 && work.transmission.isEmpty()) work.transmission = name;  // transmission slot (research 107)
        if (slot == 157 && work.distortion.isEmpty()) work.distortion = name;  // distortion slot (research 139)
        if (slot == 59 && work.tintMask.isEmpty()) work.tintMask = name;  // tint mask (research 95)
        // Cut-out mask: Replay alpha-tested materials carry a signed-distance-field image (`<x>_sdf~hash`); the colour
        // image's alpha is specular (`_c&_s` packing), not opacity.
        // Foliage cards cut out with a plain opacity image instead (`<x>_alpha~hash`; their colour has no alpha).
        if (work.mask.isEmpty() && (name.section('~', 0, 0).endsWith("_sdf") || name.section('~', 0, 0).endsWith("_alpha")))
            work.mask = name;
    }
    work.found = true;
    return true;
}

void MaterialCache::finishRun(const Run& run, const QString& dir, const ActsResult& result) {
    const int exitCode = result.started ? result.exitCode : -1;
    const QString zoneDir = dir + "/mw19replay/" + run.zone;
    std::map<QString, QJsonObject> recs;
    QFile journal(zoneDir + "/assets.jsonl");
    const bool hasJournal = journal.open(QIODevice::ReadOnly);
    if (hasJournal) {
        const QString want = run.images ? "image" : "material";
        while (!journal.atEnd()) {
            const QJsonObject o = QJsonDocument::fromJson(journal.readLine()).object();
            if (o.value("type").toString() == want) recs[o.value("name").toString()] = o;
        }
        journal.close();  // open files block the scratch cleanup below on Windows
    }
    const QString logTail = QString::fromLocal8Bit(result.log.right(600)).trimmed();

    for (const QString& name : run.names) {
        const auto it = recs.find(name);
        const QJsonObject rec = it == recs.end() ? QJsonObject() : it->second;
        const bool ok = rec.value("status").toString() == "ok" && !rec.value("file").toString().isEmpty();
        QString reason = !hasJournal ? QString("ACTS wrote no journal (exit %1): %2").arg(exitCode).arg(logTail)
                                     : reasonOf(rec);

        if (!run.images) {
            Work* w = nullptr;
            for (Work& x : works_)
                if (x.material == name) w = &x;
            if (!w) continue;
            if (ok && loadBody(*w, run.zone, zoneDir + "/" + rec.value("file").toString())) continue;
            w->attempt++;
            w->error = run.zone + ": " + reason;
        } else {
            Image& im = images_[name];
            if (ok) {
                const QString dest = imagePath(name);
                QFile::remove(dest);
                if (QFile::rename(zoneDir + "/" + rec.value("file").toString(), dest)) {
                    im.done = true;
                    im.file = dest;
                    continue;
                }
                reason = "cannot move exported DDS to the cache";
            }
            im.attempt++;
            im.error = run.zone + ": " + reason;
        }
    }
    QDir(dir).removeRecursively();
    if (--outstanding_ <= 0) pump();
}

void MaterialCache::finishBatch() {
    QStringList done;
    for (const Work& w : works_) {
        Entry e;
        e.material = w.material;
        if (w.found) {
            e.zone = w.zone;
            e.techset = w.techset;
            e.textures = w.textures;
            e.surfaceFlags = w.surfaceFlags;
            e.materialType = w.materialType;
            e.sortKey = w.sortKey;
            if (!w.color.isEmpty()) {
                const Image& im = images_.at(w.color);
                if (im.done) e.colorDds = im.file;
                else e.error += "colour " + w.color + " not exported (" + im.error + "); ";
            }
            if (!w.normal.isEmpty()) {
                const Image& im = images_.at(w.normal);
                if (im.done) e.normalDds = im.file;
                else e.error += "normal " + w.normal + " not exported (" + im.error + "); ";
            }
            if (!w.mask.isEmpty()) {
                const Image& im = images_.at(w.mask);
                if (im.done) e.maskDds = im.file;
                else e.error += "mask " + w.mask + " not exported (" + im.error + "); ";
            }
            if (!w.emissive.isEmpty() && images_.count(w.emissive)) {
                const Image& im = images_.at(w.emissive);
                if (im.done) e.emissiveDds = im.file;  // optional: a failed emissive export is not an error
            }
            if (!w.transmission.isEmpty() && images_.count(w.transmission)) {
                const Image& im = images_.at(w.transmission);
                if (im.done) e.transmissionDds = im.file;  // optional, as the emissive map
            }
            if (!w.distortion.isEmpty() && images_.count(w.distortion)) {
                const Image& im = images_.at(w.distortion);
                if (im.done) e.distortionDds = im.file;  // optional, as the emissive map
            }
            e.consts = w.consts;
            e.layerCount = w.layerCount;
            e.modelLayers = w.modelLayers;
            e.layerPsData = w.layerPsData;
            e.psData = w.psData;
            e.tint = w.tint;
            e.tintMasked = w.tintMasked;
            if (!w.tintMask.isEmpty() && images_.count(w.tintMask)) {
                const Image& im = images_.at(w.tintMask);
                if (im.done) e.tintMaskDds = im.file;  // optional, as the emissive map
            }
            for (int l = 0; l < w.layerColor.size(); ++l) {
                const auto file = [&](const QString& name, const char* what) -> QString {
                    const auto it = name.isEmpty() ? images_.end() : images_.find(name);
                    if (it == images_.end()) return {};
                    if (it->second.done) return it->second.file;
                    e.error += QString("layer %1 %2 %3 not exported (%4); ").arg(l).arg(QString(what), name, it->second.error);
                    return {};
                };
                LayerImages li;
                li.colorDds = l == 0 ? e.colorDds : file(w.layerColor[l], "colour");
                li.normalDds = l == 0 ? e.normalDds : file(w.layerNormal[l], "normal");
                li.revealDds = file(w.layerReveal[l], "reveal");
                e.layers.push_back(li);
            }
        } else {
            e.failed = true;
            e.error = w.error.isEmpty() ? QStringLiteral("material body not found in any candidate zone") : w.error;
        }
        index()[w.key.isEmpty() ? w.material : w.key] = e;
        done << w.material;
        queued_.erase(w.key);
    }
    writeIndex();
    works_.clear();
    images_.clear();
    queue_.clear();
    running_ = false;
    for (const QString& m : done) queued_.erase(m);
    emit progress(QString("cache: %1 material(s) resolved").arg(done.size()));
    for (const QString& m : done) emit materialReady(m);
    if (!pending_.isEmpty()) startBatch();
    else if (!running_) QDir(runRoot()).removeRecursively();
}

// ---------------------------------------------------------------------------------------------------------------
// Headless test (main.cpp --material-cache-test)

namespace {

QString describeDds(const QString& path) {
    if (path.isEmpty()) return "none";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return path + " (missing)";
    const QByteArray bytes = f.readAll();
    app::Rgba8Image img;
    std::string err;
    if (!app::decodeDds(std::string(bytes.constData(), size_t(bytes.size())), img, &err))
        return QString("%1 (decode failed: %2)").arg(path, QString::fromStdString(err));
    return QString("%1 (%2x%3)").arg(path).arg(img.width).arg(img.height);
}

}  // namespace

QString MaterialCache::describeImage(const QString& ddsPath) { return describeDds(ddsPath); }

int runMaterialCacheTest(const QStringList& materials, const QString& outDir) {
    QDir().mkpath(outDir);
    QSettings ini(QDir(QCoreApplication::applicationDirPath()).filePath("IW8Radiant.ini"), QSettings::IniFormat);
    const auto tools = [&ini](const QString& key) -> QString {
        // The editor's Tool Paths (IW8Radiant.ini) with the MainWindow::toolPath defaults.
        static const QMap<QString, QString> defaults = {
            {"acts", QCoreApplication::applicationDirPath() + "/iw8r-acts.exe"},  // src/tools/acts
            {"replay", "E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe"},
            {"oodle", "E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll"},
        };
        return ini.value("tools/" + key, defaults.value(key)).toString();
    };

    MaterialCache cache(tools);
    QStringList log;
    int actsRuns = 0;
    QObject::connect(&cache, &MaterialCache::progress, &cache, [&](const QString& m) {
        log << m;
        if (m.startsWith("ACTS ")) ++actsRuns;
    });

    QEventLoop loop;
    QTimer poll;
    poll.setInterval(100);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (!cache.busy()) loop.quit();
    });
    QTimer::singleShot(30 * 60 * 1000, &loop, &QEventLoop::quit);

    QElapsedTimer timer;
    timer.start();
    poll.start();
    cache.request(materials);
    loop.exec();
    const qint64 wallMs = timer.elapsed();

    QStringList lines;
    int bad = 0;
    lines << "# material-cache-test";
    lines << "# cache_root=" + cache.cacheRoot();
    for (const QString& m : materials) {
        const auto e = cache.lookup(m.trimmed());
        if (!e) {
            lines << m + "\tERROR no entry";
            ++bad;
        } else if (e->failed) {
            lines << m + "\tFAILED\terror=" + e->error;
            ++bad;
        } else {
            QString line = m + "\tzone=" + e->zone + "\tcolour=" + describeDds(e->colorDds) + "\tnormal=" + describeDds(e->normalDds);
            if (e->layerCount >= 2) {
                QStringList layers;
                for (const MaterialCache::LayerImages& l : e->layers)
                    layers << QFileInfo(l.colorDds).fileName() + "+" + QFileInfo(l.revealDds).fileName();
                line += QString("\tlayers=%1 [%2] psData=%3B").arg(e->layerCount).arg(layers.join(" | ")).arg(e->psData.size());
            }
            if (!e->error.isEmpty()) line += "\terror=" + e->error;
            lines << line;
        }
    }
    qint64 bytes = 0;
    int files = 0;
    QDirIterator it(cache.cacheRoot(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        bytes += it.fileInfo().size();
        ++files;
    }
    lines << QString("# wall_ms=%1 acts_runs=%2 requested=%3 unresolved=%4").arg(wallMs).arg(actsRuns).arg(materials.size()).arg(bad);
    lines << QString("# cache_files=%1 cache_bytes=%2").arg(files).arg(bytes);
    for (const QString& l : log) lines << "# progress: " + l;

    QFile out(QDir(outDir).filePath("material-cache-test.txt"));
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) out.write((lines.join('\n') + '\n').toUtf8());
    return bad ? 1 : 0;
}

}  // namespace iw8r::qt
