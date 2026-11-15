#include "model_cache.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
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
#include <cmath>
#include <cstring>
#include <map>
#include <tuple>
#include <utility>

#include "acts_runner.h"
#include "glb_mesh.h"
#include "stock_prop_fetch.h"

namespace iw8r::qt {
namespace {

constexpr const char* kIndexFormat = "iw8r-model-cache-1";
// Zone XPaks with a payload are far larger than the 32 KiB headers that most zones carry (see material_cache.cpp).
constexpr qint64 kXpakMinBytes = 64 * 1024;
constexpr double kMetresPerInch = 0.0254;  // ACTS geometry sidecars are inches under a 0.0254 node scale

QString safeFileName(const QString& name) {
    QString out;
    out.reserve(name.size());
    for (const QChar c : name) {
        const bool keep = (c.unicode() < 128 && c.isLetterOrNumber()) || c == '_' || c == '-' || c == '.';
        out += keep ? c : QChar('_');
    }
    return out;
}

// The repo's data folder: the first "data/stock_assets" found walking up from the exe.
QString repoDataDir() {
    QDir d(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 4; ++up, d.cdUp())
        if (QFileInfo::exists(d.filePath("data/stock_assets"))) return d.filePath("data");
    return {};
}

QString reasonOf(const QJsonObject& rec) {
    if (rec.isEmpty()) return "not in zone";
    return QString("%1 %2 %3")
        .arg(rec.value("status").toString(), rec.value("reason").toString(), rec.value("error").toString())
        .trimmed();
}

// Reads the LOD0 surfaces asset name (lodInfo[0]) and the bounds (midPoint +- halfSize, Z-up inches) from an
// exported xmodel JSON (mw19-asset-json-v1). Returns false with err set when the model has no LOD0 surfaces.
bool readXmodel(const QString& path, QString& lod0, std::array<double, 6>& bounds, QStringList& materials, QString& err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        err = "cannot read " + path;
        return false;
    }
    const QJsonObject fields = QJsonDocument::fromJson(f.readAll()).object().value("asset").toObject().value("fields").toObject();
    const QJsonArray lods = fields.value("lodInfo").toArray();
    if (lods.isEmpty()) {
        err = "xmodel has no LOD0 surfaces";
        return false;
    }
    lod0 = lods.at(0).toObject().value("modelSurfsStaging").toObject().value("name").toString();
    {
        // LOD0 surface i uses materialHandles[surfIndex + i]; ACTS prints asset names with a leading comma.
        const QJsonObject l0 = lods.at(0).toObject();
        const QJsonArray handles = fields.value("materialHandles").toObject().value("values").toArray();
        const int first = l0.value("surfIndex").toInt(), count = l0.value("numsurfs").toInt();
        materials.clear();
        for (int i = 0; i < count && first + i < handles.size(); ++i) {
            QString m = handles.at(first + i).toObject().value("name").toString();
            if (m.startsWith(',')) m.remove(0, 1);
            materials << m;
        }
    }
    const QJsonObject b = fields.value("bounds").toObject();
    const QJsonArray mid = b.value("midPoint").toObject().value("v").toArray();
    const QJsonArray half = b.value("halfSize").toObject().value("v").toArray();
    if (lod0.isEmpty() || mid.size() != 3 || half.size() != 3) {
        err = "xmodel has no LOD0 surfaces name or bounds";
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        bounds[i] = mid[i].toDouble() - half[i].toDouble();
        bounds[i + 3] = mid[i].toDouble() + half[i].toDouble();
    }
    return true;
}

}  // namespace

ModelCache::ModelCache(ToolPathFn tools, QObject* parent) : QObject(parent), tools_(std::move(tools)) {
    root_ = qEnvironmentVariable("IW8R_MODEL_CACHE_ROOT",  // the asset audit uses a scratch root
                                 QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/model_cache");
    QDir().mkpath(root_ + "/glb");
    QDir(root_ + "/w").removeRecursively();  // scratch left behind by a crash
    loadIndex();
}

ModelCache::~ModelCache() {
    // Shutting down: runs that end from now on must not reach finishRun (it touches widgets that are already
    // gone). Queued requests are dropped; the runner does not call back for an owner that is gone.
    blockSignals(true);
    if (QCoreApplication::instance()) ActsRunner::shared()->cancel(this);
}

std::optional<ModelCache::Entry> ModelCache::lookup(const QString& name) const {
    if (auto it = index_.find(name); it != index_.end()) return it->second;
    return std::nullopt;
}

QString ModelCache::detailPath(const QString& name) const { return root_ + "/detail/" + safeFileName(name) + ".json"; }

void ModelCache::saveDetail(const QString& name, const QString& exportPath) const {
    XModelInfo m;
    if (!readXModelExport(exportPath.toStdString(), m)) return;
    QJsonObject o;
    o.insert("name", QString::fromStdString(m.name));
    o.insert("collLod", m.collLod);
    o.insert("radius", m.radius);
    QJsonArray b, lods, bones;
    for (double v : m.bounds) b.append(v);
    for (const XModelLod& l : m.lods)
    {
        QJsonArray lm;
        for (const std::string& s : l.materials) lm.append(QString::fromStdString(s));
        lods.append(QJsonObject{{"surfs", QString::fromStdString(l.surfs)}, {"distance", l.distance}, {"surfCount", l.surfCount},
                                {"surfIndex", l.surfIndex}, {"materials", lm}});
    }
    for (const XModelBone& bn : m.bones)
        bones.append(QJsonObject{{"name", QString::fromStdString(bn.name)}, {"parent", bn.parent},
                                 {"pos", QJsonArray{bn.pos[0], bn.pos[1], bn.pos[2]}}});
    o.insert("bounds", b);
    o.insert("lods", lods);
    o.insert("bones", bones);
    QDir().mkpath(root_ + "/detail");
    QFile f(detailPath(name));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

std::optional<XModelInfo> ModelCache::loadDetail(const QString& name) const {
    QFile f(detailPath(name));
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.isEmpty()) return std::nullopt;
    XModelInfo m;
    m.name = o.value("name").toString().toStdString();
    m.collLod = o.value("collLod").toInt(-1);
    m.radius = o.value("radius").toDouble();
    const QJsonArray b = o.value("bounds").toArray();
    for (int i = 0; i < 6 && i < b.size(); ++i) m.bounds[i] = b[i].toDouble();
    for (const auto& v : o.value("lods").toArray()) {
        const QJsonObject l = v.toObject();
        XModelLod lod;
        lod.surfs = l.value("surfs").toString().toStdString();
        lod.distance = l.value("distance").toDouble();
        lod.surfCount = l.value("surfCount").toInt();
        lod.surfIndex = l.value("surfIndex").toInt();
        for (const auto& s : l.value("materials").toArray()) lod.materials.push_back(s.toString().toStdString());
        m.lods.push_back(lod);
    }
    for (const auto& v : o.value("bones").toArray()) {
        const QJsonObject bo = v.toObject();
        XModelBone bn;
        bn.name = bo.value("name").toString().toStdString();
        bn.parent = bo.value("parent").toInt(-1);
        const QJsonArray p = bo.value("pos").toArray();
        for (int i = 0; i < 3 && i < p.size(); ++i) bn.pos[i] = p[i].toDouble();
        m.bones.push_back(bn);
    }
    return m;
}

void ModelCache::refetch(const QString& name) {
    if (running_ || queued_.count(name) || !refetched_.insert(name).second) return;  // once per model and session
    index_.erase(name);
    writeIndex();
    request({name});
}

void ModelCache::setFallbackZones(const QStringList& zones) {
    fallbackZones_ = zones;
    if (zones.isEmpty()) return;
    if (running_) {
        forgetNotListed_ = true;  // the index is in use by the batch: done when it ends (finishBatch)
        return;
    }
    bool dropped = false;
    for (auto it = index_.begin(); it != index_.end();) {
        if (it->second.failed && it->second.error == "not listed in iw8_models.tsv") {
            it = index_.erase(it);
            dropped = true;
        } else {
            ++it;
        }
    }
    if (dropped) writeIndex();
}

namespace {
// Whether a cached GLB's JSON chunk names an attribute (the ACTS fork's "_SELFVIS", "_LMAPUV").
int glbHas(const QString& path, const char* attribute) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return 0;
    const QByteArray head = f.read(20);
    if (head.size() < 20 || !head.startsWith("glTF")) return 0;
    uint32_t len = 0;
    std::memcpy(&len, head.constData() + 12, 4);
    if (len == 0 || len > (64u << 20)) return 0;
    return f.read(qint64(len)).contains(QByteArray("\"") + attribute + "\"") ? 1 : 0;
}
int glbSelfVis(const QString& path) { return glbHas(path, "_SELFVIS"); }
int glbLmapUv(const QString& path) { return glbHas(path, "_LMAPUV"); }
// A lightmapped static model (research 105 section 3.1): one of its surfaces has a tml / tmlc material.
bool lightmappedModel(const QStringList& materials) {
    for (const QString& m : materials)
        if (m.section('/', 0, 0).startsWith("tml")) return true;
    return false;
}
}  // namespace

void ModelCache::refreshGeometry(const QStringList& names) {
    bool added = false;
    for (const QString& n : names) {
        const auto it = index_.find(n);
        if (it == index_.end() || it->second.failed || queued_.count(n)) continue;
        const bool lmapUnknown = it->second.lmapUv < 0 && lightmappedModel(it->second.materials);
        if ((it->second.selfVis >= 0 && !lmapUnknown) || !refreshed_.insert(n).second) continue;
        refreshPending_ << n;
        added = true;
    }
    if (added && !running_) startRefreshBatch();
}

void ModelCache::startRefreshBatch() {
    if (refreshPending_.isEmpty()) return;
    running_ = true;
    refreshBatch_ = true;
    works_.clear();
    queue_.clear();
    phase_ = Phase::Geometry;  // the tables are known: only the surface buffers again
    for (const QString& f : {tools_("acts"), tools_("replay"), tools_("oodle")})
        if (!QFileInfo::exists(f)) {
            problem_ = "Missing tool, model cache idle: " + f;
            emit progress(problem_);
            refreshPending_.clear();
            running_ = refreshBatch_ = false;
            emit batchEnded(0);
            return;
        }
    QStringList names;
    while (!refreshPending_.isEmpty() && names.size() < kBatchModels) names << refreshPending_.takeFirst();
    for (const QString& n : names) {
        const auto it = index_.find(n);
        if (it == index_.end() || it->second.failed) continue;
        Work w;
        w.name = n;
        w.found = true;
        w.zone = it->second.zone;
        w.lod0Surfs = it->second.lod0Surfs;
        w.bounds = it->second.bounds;
        w.materials = it->second.materials;
        queued_.insert(n);
        works_.push_back(w);
    }
    emit progress(QString("model cache: %1 model(s) fetched again for their self visibility").arg(works_.size()));
    pump();
}

void ModelCache::request(const QStringList& names) {
    bool added = false;
    for (QString n : names) {
        n = n.trimmed();
        if (n.isEmpty() || lookup(n) || queued_.count(n)) continue;
        queued_.insert(n);
        pending_.append(n);
        added = true;
    }
    if (added && !running_) startBatch();  // (a refresh batch running: finishBatch starts these first)
}

bool ModelCache::busy() const { return running_ || !pending_.isEmpty(); }

QString ModelCache::cacheRoot() const { return root_; }

std::optional<ImportedModel> ModelCache::loadModel(const QString& name, QString* err) const {
    const auto e = lookup(name);
    if (!e || e->failed) {
        if (err) *err = e ? e->error : QStringLiteral("not in the model cache");
        return std::nullopt;
    }
    return loadGlb(e->glbPath, e->materials, name, err);
}

std::optional<ImportedModel> ModelCache::loadGlb(const QString& path, const QStringList& materials, const QString& name,
                                                 QString* err) const {
    GlbReadOptions opt;
    opt.scale = 1.0 / kMetresPerInch;  // glTF metres (after the node scale) to inches, Y-up
    ImportedModel m;
    std::string serr;
    if (!readGlb(path.toUtf8().toStdString(), m, &serr, opt)) {
        if (err) *err = QString::fromStdString(serr);
        return std::nullopt;
    }
    m.name = name.toUtf8().toStdString();
    // One GLB primitive per surface, in surface order: name each mesh after its xmodel material.
    if (materials.size() == qsizetype(m.meshes.size()))
        for (size_t i = 0; i < m.meshes.size(); ++i) m.meshes[i].material = materials[qsizetype(i)].toStdString();
    // Shadow-only surfaces (materials named *shadowcaster*: foliage cards, proxies) never draw in game; as ordinary
    // surfaces they show as solid untextured cards, in the camera and in a build.
    std::erase_if(m.meshes, [](const ImportedMesh& mesh) {
        return QString::fromStdString(mesh.material).contains("shadowcaster", Qt::CaseInsensitive);
    });
    // ACTS sidecars wind clockwise seen from outside (the IW3/IW7 model convention, docs/research/22). ImportedModel
    // and the editor's face normals use counter-clockwise, so swap two indices of every triangle.
    for (ImportedMesh& mesh : m.meshes)
        for (size_t i = 0; i + 2 < mesh.idx.size(); i += 3) std::swap(mesh.idx[i + 1], mesh.idx[i + 2]);
    return m;
}

// ---------------------------------------------------------------------------------------------------------------
// Index and catalog

void ModelCache::loadIndex() {
    QFile f(root_ + "/index.json");
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject models = QJsonDocument::fromJson(f.readAll()).object().value("models").toObject();
    for (auto it = models.begin(); it != models.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.name = it.key();
        e.zone = o.value("zone").toString();
        e.lod0Surfs = o.value("lod0").toString();
        const QJsonArray b = o.value("bounds").toArray();
        for (int i = 0; i < 6 && i < b.size(); ++i) e.bounds[i] = b[i].toDouble();
        e.failed = o.value("failed").toBool();
        e.error = o.value("error").toString();
        e.selfVis = o.contains("selfvis") ? o.value("selfvis").toInt() : -1;
        e.lmapUv = o.contains("lmapuv") ? o.value("lmapuv").toInt() : -1;
        if (!e.failed) {
            const QString glb = o.value("glb").toString();
            e.glbPath = glb.isEmpty() ? QString() : QDir(root_).filePath(glb);
            if (e.glbPath.isEmpty() || !QFileInfo::exists(e.glbPath)) continue;  // removed by hand: resolve again
            if (!o.contains("materials")) continue;  // cached before material names were recorded: resolve again
            for (const QJsonValue v : o.value("materials").toArray()) e.materials << v.toString();
        }
        // Failures of a whole batch that one bad name caused are forgotten, so those models are fetched again.
        if (e.failed && e.error.contains("Invalid asset name in request")) continue;
        // So are failures that were not about the model: the old GLB could not be replaced at that moment (its
        // file is still there), and LOD0 buffers named as a reference, which are now looked for in other zones.
        if (e.failed && (e.error == "cannot copy the GLB into the cache" || e.error.contains("contains a comma, which ACTS cannot request")))
            continue;
        index_[e.name] = e;
    }
}

void ModelCache::loadCatalog() {
    catalogLoaded_ = true;
    const QString data = repoDataDir();
    if (data.isEmpty()) return;
    QFile f(QDir(data).filePath("asset_catalog/iw8_models.tsv"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.startsWith('#') || line.trimmed().isEmpty()) continue;  // the leading comment line is the header
        const int tab = line.indexOf('\t');
        if (tab <= 0) continue;
        catalog_[line.left(tab).trimmed()] = line.mid(tab + 1).trimmed().split(',', Qt::SkipEmptyParts);
    }
}

void ModelCache::writeIndex() const {
    QJsonObject models;
    const QDir root(root_);
    for (const auto& [name, e] : index_) {
        QJsonObject o;
        o.insert("zone", e.zone);
        o.insert("lod0", e.lod0Surfs);
        QJsonArray b;
        for (double v : e.bounds) b.append(v);
        o.insert("bounds", b);
        o.insert("glb", e.glbPath.isEmpty() ? QString() : root.relativeFilePath(e.glbPath));
        if (!e.failed) o.insert("materials", QJsonArray::fromStringList(e.materials));
        o.insert("failed", e.failed);
        if (!e.error.isEmpty()) o.insert("error", e.error);
        if (e.selfVis >= 0) o.insert("selfvis", e.selfVis);
        if (e.lmapUv >= 0) o.insert("lmapuv", e.lmapUv);
        models.insert(name, o);
    }
    QJsonObject top;
    top.insert("format", kIndexFormat);
    top.insert("models", models);
    QSaveFile f(root_ + "/index.json");
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(top).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

QString ModelCache::replayZoneDir() const { return QFileInfo(tools_("replay")).absolutePath() + "/zone"; }

QStringList ModelCache::xpaks() const {
    QStringList out;
    for (const QFileInfo& f : QDir(replayZoneDir()).entryInfoList({"*.xpak"}, QDir::Files, QDir::Name))
        if (f.size() >= kXpakMinBytes) out << f.absoluteFilePath();
    return out;
}

QString ModelCache::glbPath(const QString& name) const { return root_ + "/glb/" + safeFileName(name) + ".glb"; }

// ---------------------------------------------------------------------------------------------------------------
// LOD preview (3.18)

QString ModelCache::lodGlbPath(const QString& name, int lod) const {
    return lod <= 0 ? glbPath(name) : root_ + "/glb/" + safeFileName(name) + ".lod" + QString::number(lod) + ".glb";
}

bool ModelCache::hasLod(const QString& name, int lod) const { return QFileInfo::exists(lodGlbPath(name, lod)); }

bool ModelCache::lodFailed(const QString& name, int lod) const {
    return lodFailed_.count(name + "#" + QString::number(lod)) != 0;
}

void ModelCache::requestLod(const QString& name, int lod) {
    const QString key = name + "#" + QString::number(lod);
    if (lod <= 0 || hasLod(name, lod) || lodPending_.count(key) || lodFailed_.count(key)) return;
    const auto e = lookup(name);
    const auto d = loadDetail(name);
    if (!e || e->failed || e->zone.isEmpty() || !d || lod >= int(d->lods.size()) || d->lods[size_t(lod)].surfs.empty()) {
        lodFailed_.insert(key);
        return;
    }
    lodPending_.insert(key);
    const QString surfs = QString::fromStdString(d->lods[size_t(lod)].surfs);
    const QString dir = QDir::toNativeSeparators(root_ + "/w/lod" + QString::number(++runSeq_));
    QDir().mkpath(dir);
    ActsRequest request;
    request.zone = e->zone;
    request.types = "xmodelsurfs";
    request.names = {surfs};
    request.outDir = dir;
    request.geometry = true;
    request.xpaks = true;
    emit progress(QString("ACTS xmodelsurfs %1: LOD %2 of %3").arg(e->zone).arg(lod).arg(name));
    ActsRunner::shared()->submit(tools_, request, this, [this, name, lod, key, surfs, dir, zone = e->zone](const ActsResult& result) {
        lodPending_.erase(key);
        bool ok = false;
        QFile journal(dir + "/mw19replay/" + zone + "/assets.jsonl");
        if (journal.open(QIODevice::ReadOnly)) {
            while (!journal.atEnd()) {
                const QJsonObject o = QJsonDocument::fromJson(journal.readLine()).object();
                if (o.value("type").toString() != "xmodelsurfs" || o.value("name").toString() != surfs) continue;
                const QString geo = o.value("geometry_file").toString();
                const QString file = dir + "/mw19replay/" + zone + "/" + geo;
                if (o.value("status").toString() == "ok" && !geo.isEmpty() && QFileInfo::exists(file)) {
                    QFile::remove(lodGlbPath(name, lod));
                    ok = QFile::copy(file, lodGlbPath(name, lod));
                }
            }
            journal.close();
        }
        QDir(dir).removeRecursively();
        if (!ok) lodFailed_.insert(key);
        emit progress(QString("LOD %1 of %2: %3").arg(lod).arg(name, ok ? "ready" : "not available (exit " + QString::number(result.exitCode) + ")"));
        emit lodReady(name, lod);
    });
}

void ModelCache::requestLods(const QList<QPair<QString, int>>& want) {
    // zone -> (surfs name -> (model, lod)), only what can be fetched
    std::map<QString, std::vector<std::tuple<QString, QString, int>>> byZone;
    for (const auto& [name, lod] : want) {
        const QString key = name + "#" + QString::number(lod);
        if (lod <= 0 || hasLod(name, lod) || lodPending_.count(key) || lodFailed_.count(key)) continue;
        const auto e = lookup(name);
        const auto d = loadDetail(name);
        // A surfs name with a comma is a reference to another zone's asset (",name", as the LOD0 failures say); the
        // ACTS server refuses such a name and with it the whole batch ("Invalid asset name in request": on Verdansk
        // every LOD batch failed, 2026-11-13). Those LODs stay with the level before them.
        if (!e || e->failed || e->zone.isEmpty() || !d || lod >= int(d->lods.size()) || d->lods[size_t(lod)].surfs.empty() ||
            d->lods[size_t(lod)].surfs.find(',') != std::string::npos) {
            lodFailed_.insert(key);
            continue;
        }
        lodPending_.insert(key);
        byZone[e->zone].push_back({QString::fromStdString(d->lods[size_t(lod)].surfs), name, lod});
    }
    constexpr size_t kPerRun = 48;
    for (auto& [zone, items] : byZone)
        for (size_t at = 0; at < items.size(); at += kPerRun) {
            const std::vector<std::tuple<QString, QString, int>> part(items.begin() + ptrdiff_t(at),
                                                                      items.begin() + ptrdiff_t(std::min(items.size(), at + kPerRun)));
            const QString dir = QDir::toNativeSeparators(root_ + "/w/lods" + QString::number(++runSeq_));
            QDir().mkpath(dir);
            ActsRequest request;
            request.zone = zone;
            request.types = "xmodelsurfs";
            for (const auto& [surfs, name, lod] : part) request.names << surfs;
            request.outDir = dir;
            request.geometry = true;
            request.xpaks = true;
            emit progress(QString("ACTS xmodelsurfs %1: %2 prop LODs").arg(zone).arg(part.size()));
            ActsRunner::shared()->submit(tools_, request, this, [this, part, dir, zone = zone](const ActsResult& result) {
                std::map<QString, QString> files;  // surfs -> exported GLB
                QFile journal(dir + "/mw19replay/" + zone + "/assets.jsonl");
                if (journal.open(QIODevice::ReadOnly)) {
                    while (!journal.atEnd()) {
                        const QJsonObject o = QJsonDocument::fromJson(journal.readLine()).object();
                        if (o.value("type").toString() != "xmodelsurfs" || o.value("status").toString() != "ok") continue;
                        const QString geo = o.value("geometry_file").toString();
                        const QString file = dir + "/mw19replay/" + zone + "/" + geo;
                        if (!geo.isEmpty() && QFileInfo::exists(file)) files[o.value("name").toString()] = file;
                    }
                    journal.close();
                }
                int ok = 0;
                for (const auto& [surfs, name, lod] : part) {
                    const QString key = name + "#" + QString::number(lod);
                    lodPending_.erase(key);
                    bool got = false;
                    if (const auto f = files.find(surfs); f != files.end()) {
                        QFile::remove(lodGlbPath(name, lod));
                        got = QFile::copy(f->second, lodGlbPath(name, lod));
                    }
                    if (!got) lodFailed_.insert(key);
                    ok += got ? 1 : 0;
                }
                QDir(dir).removeRecursively();
                // A run that failed as a whole says why (the server's error, else the last lines of the tool's output).
                QString why;
                if (ok == 0 && result.exitCode != 0 && result.exitCode != 127) {
                    why = result.error.trimmed();
                    if (why.isEmpty()) {
                        const QList<QByteArray> lines = result.log.trimmed().split('\n');
                        for (qsizetype k = std::max<qsizetype>(0, lines.size() - 2); k < lines.size(); ++k)
                            why += (why.isEmpty() ? "" : " | ") + QString::fromUtf8(lines[k]).trimmed();
                    }
                    if (!result.started) why = "acts.exe could not be started";
                    if (!why.isEmpty()) why = ": " + why.left(300);
                }
                emit progress(QString("Prop LODs from %1: %2 of %3 (exit %4)%5").arg(zone).arg(ok).arg(part.size()).arg(result.exitCode).arg(why));
                for (const auto& [surfs, name, lod] : part) emit lodReady(name, lod);
            });
        }
}

std::optional<ImportedModel> ModelCache::loadModelLod(const QString& name, int lod, QString* err) const {
    if (lod <= 0) return loadModel(name, err);
    if (!hasLod(name, lod)) {
        if (err) *err = "LOD not cached";
        return std::nullopt;
    }
    QStringList mats;
    if (const auto d = loadDetail(name); d && lod < int(d->lods.size()))
        for (const std::string& s : d->lods[size_t(lod)].materials) mats << QString::fromStdString(s);
    auto m = loadGlb(lodGlbPath(name, lod), mats, name, err);
    if (m && mats.isEmpty()) {
        // Detail saved before per-LOD materials were kept: the LOD0 list when the surface count matches.
        if (const auto e = lookup(name); e && e->materials.size() == qsizetype(m->meshes.size()))
            for (size_t i = 0; i < m->meshes.size(); ++i) m->meshes[i].material = e->materials[qsizetype(i)].toStdString();
    }
    return m;
}

// ---------------------------------------------------------------------------------------------------------------
// Batches

void ModelCache::startBatch() {
    running_ = true;
    works_.clear();
    queue_.clear();
    phase_ = Phase::Tables;
    if (!catalogLoaded_) loadCatalog();

    // One batch at a time; the rest stays in pending_ and finishBatch starts the next one at once. The chain is
    // here, in the cache, so it cannot stop because a caller did not ask again.
    QStringList names;
    while (!pending_.isEmpty() && names.size() < kBatchModels) names << pending_.takeFirst();
    const QString acts = tools_("acts"), replay = tools_("replay"), oodle = tools_("oodle");
    for (const QString& f : {acts, replay, oodle})
        if (!QFileInfo::exists(f)) {
            // Environment problem, not a property of the model: nothing is cached, everything asked for is dropped,
            // and whoever waits is told (batchEnded) and can read why (problem()).
            problem_ = "Missing tool, model cache idle: " + f;
            emit progress(problem_);
            for (const QString& n : names) queued_.erase(n);
            for (const QString& n : pending_) queued_.erase(n);
            pending_.clear();
            running_ = false;
            emit batchEnded(0);
            return;
        }
    problem_.clear();

    for (const QString& n : names) {
        Work w;
        w.name = n;
        const QStringList zones = catalog_.count(n) ? catalog_.at(n) : fallbackZones_;
        for (const QString& z : zones)
            if (!w.zones.contains(z) && QFileInfo::exists(replayZoneDir() + "/" + z + ".ff")) w.zones << z;
        if (zones.isEmpty()) w.error = "not listed in iw8_models.tsv";
        else if (w.zones.isEmpty()) w.error = "no zone .ff in the replay zone folder for: " + zones.join(",");
        works_.push_back(w);
    }
    emit progress(QString("model cache: %1 model(s) to resolve").arg(names.size()));
    pump();
}

// One xmodel run per zone for the models not yet found, trying each model's next candidate zone.
void ModelCache::buildTableRound() {
    std::map<QString, QStringList> groups;
    for (const Work& w : works_)
        if (!w.found && w.attempt < int(w.zones.size())) groups[w.zones[w.attempt]] << w.name;
    for (const auto& [zone, names] : groups) queue_.push_back(Run{false, zone, names});
}

// One xmodelsurfs run per zone for the LOD0 surface buffers of the models found.
void ModelCache::buildGeometryRound() {
    std::map<QString, QStringList> groups;
    // A few stock surface names begin with a comma (",military_carepackage_01_lod010-4-v00c11"). ACTS separates the
    // names of a request by commas, so such a name cannot be asked for, and in a batch it failed the whole request
    // (owner playtest 2026-11-11: 183 mp_backlot2 props lost to one name). The comma marks a reference: the buffer
    // is not in the model's zone but in another one, under the name without the comma. Those models are left out
    // here and looked for in the zones of models with a like name (buildReferenceRound).
    for (Work& w : works_) {
        if (!w.found || !w.glb.isEmpty() || !w.error.isEmpty() || !w.lod0Surfs.contains(',')) continue;
        const QString plain = w.lod0Surfs.mid(1);
        if (!w.lod0Surfs.startsWith(',') || plain.isEmpty() || plain.contains(','))
            w.error = "its LOD0 surface name contains a comma, which ACTS cannot request: " + w.lod0Surfs;
        else
            w.refZones = referenceZones(plain, w.zone);
    }
    for (const Work& w : works_)
        if (w.found && w.glb.isEmpty() && w.error.isEmpty() && !w.lod0Surfs.isEmpty() && !w.lod0Surfs.contains(',') &&
            !groups[w.zone].contains(w.lod0Surfs))
            groups[w.zone] << w.lod0Surfs;
    for (const auto& [zone, names] : groups)
        if (!names.isEmpty()) queue_.push_back(Run{true, zone, names});
}

// Zones that may hold the surface buffer `surfs` (a LOD0 that the model's own zone only names as a reference): the
// zones of catalogue models whose name starts like the buffer's. "uk_industrial_light_01_lod000v011" is cut back
// word by word until models match ("uk_industrial_light_01", "tm/uk_industrial_light_01_on", ...); checked on
// mp_rust, where that buffer is in common_base_mp. Zones with a .ff only, `except` left out, eight at most.
QStringList ModelCache::referenceZones(const QString& surfs, const QString& except) const {
    QStringList zones;
    const auto addZonesOf = [&](const QString& prefix) {
        for (auto it = catalog_.lower_bound(prefix); it != catalog_.end() && it->first.startsWith(prefix); ++it)
            for (const QString& z : it->second)
                if (z != except && !zones.contains(z) && zones.size() < 8 && QFileInfo::exists(replayZoneDir() + "/" + z + ".ff"))
                    zones << z;
    };
    for (QString stem = surfs; zones.isEmpty();) {
        const qsizetype cut = stem.lastIndexOf('_');
        if (cut <= 0) break;
        stem.truncate(cut);
        if (stem.count('_') < 1) break;  // one word matches half the catalogue
        addZonesOf(stem);
        addZonesOf("tm/" + stem);
    }
    return zones;
}

// One xmodelsurfs run per zone for the LOD0 buffers named as a reference, each model's next candidate zone.
void ModelCache::buildReferenceRound() {
    std::map<QString, QStringList> groups;
    for (Work& w : works_) {
        if (!w.found || !w.glb.isEmpty() || !w.error.isEmpty() || !w.lod0Surfs.startsWith(',')) continue;
        if (w.refAttempt >= int(w.refZones.size())) {
            w.error = "its LOD0 surface buffer is in another zone, which was not found: " + w.lod0Surfs;
            continue;
        }
        QStringList& g = groups[w.refZones[w.refAttempt]];
        if (const QString plain = w.lod0Surfs.mid(1); !g.contains(plain)) g << plain;
    }
    for (const auto& [zone, names] : groups) queue_.push_back(Run{true, zone, names, true});
}

// Puts an exported GLB into the cache as the model's LOD0. Copy, not move: two models may share one LOD0 buffer.
bool ModelCache::storeGlb(Work& w, const QString& file) {
    const QString dest = glbPath(w.name);
    QFile::remove(dest);
    if (QFile::copy(file, dest)) {
        w.glb = dest;
        return true;
    }
    // An older file that cannot be replaced right now (something has it open) is an earlier export of the same
    // LOD0: it is kept. Before, the model was marked failed and stayed failed in every later session although its
    // GLB was in the cache (the owner's cache on 2026-11-11: four models, used by ten of the eleven cached maps).
    if (QFileInfo(dest).size() > 0) {
        w.glb = dest;
        return true;
    }
    w.error = "cannot copy the GLB into the cache";
    return false;
}

void ModelCache::pump() {
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
        if (phase_ == Phase::Tables) {
            buildTableRound();
            if (queue_.empty()) phase_ = Phase::Geometry;
            continue;
        }
        if (phase_ == Phase::Geometry) {
            buildGeometryRound();
            phase_ = Phase::References;
            continue;
        }
        if (phase_ == Phase::References) {
            buildReferenceRound();  // a round per candidate zone, until every reference is found or out of zones
            if (queue_.empty()) phase_ = Phase::Done;
            continue;
        }
        break;
    }
    finishBatch();
}

void ModelCache::startRun(const Run& run) {
    const QString dir = QDir::toNativeSeparators(root_ + "/w/" + QString::number(++runSeq_));
    QDir().mkpath(dir);

    ActsRequest request;
    request.zone = run.zone;
    request.types = run.geometry ? "xmodelsurfs" : "xmodel";
    request.names = run.names;
    request.outDir = dir;
    // Surface payloads are streamed from the XPaks; pak_tu20 served the sample (docs/research/23).
    request.geometry = run.geometry;
    request.xpaks = run.geometry;
    emit progress(QString("ACTS %1 %2: %3 name(s)")
                      .arg(run.geometry ? "xmodelsurfs" : "xmodel", run.zone)
                      .arg(run.names.size()));
    ActsRunner::shared()->submit(tools_, request, this, [this, run, dir](const ActsResult& result) { finishRun(run, dir, result); });
}

void ModelCache::finishRun(const Run& run, const QString& dir, const ActsResult& result) {
    const int exitCode = result.started ? result.exitCode : -1;
    const qint64 ms = result.ms;

    const QString zoneDir = dir + "/mw19replay/" + run.zone;
    std::map<QString, QJsonObject> recs;
    QFile journal(zoneDir + "/assets.jsonl");
    const bool hasJournal = journal.open(QIODevice::ReadOnly);
    const QString want = run.geometry ? "xmodelsurfs" : "xmodel";
    if (hasJournal) {
        while (!journal.atEnd()) {
            const QJsonObject o = QJsonDocument::fromJson(journal.readLine()).object();
            if (o.value("type").toString() == want) recs[o.value("name").toString()] = o;
        }
        journal.close();  // open files block the scratch cleanup below on Windows
    }
    const QString logTail = QString::fromLocal8Bit(result.log.right(600)).trimmed();
    emit progress(QString("ACTS %1 %2 done: exit %3, %4 ms%5")
                      .arg(want, run.zone)
                      .arg(exitCode)
                      .arg(ms)
                      .arg(result.cached ? ", zone already loaded" : ""));

    for (const QString& name : run.names) {
        const auto it = recs.find(name);
        const QJsonObject rec = it == recs.end() ? QJsonObject() : it->second;
        const bool ok = rec.value("status").toString() == "ok" && !rec.value("file").toString().isEmpty();
        const QString reason = !hasJournal ? QString("ACTS wrote no journal (exit %1): %2").arg(exitCode).arg(logTail)
                                           : reasonOf(rec);
        if (!run.geometry) {
            for (Work& w : works_) {
                if (w.name != name) continue;
                QString lod0, err;
                if (ok && readXmodel(zoneDir + "/" + rec.value("file").toString(), lod0, w.bounds, w.materials, err)) {
                    w.found = true;
                    saveDetail(name, zoneDir + "/" + rec.value("file").toString());
                    w.zone = run.zone;
                    w.lod0Surfs = lod0;
                    w.error.clear();
                } else {
                    w.attempt++;
                    w.error = run.zone + ": " + (ok ? err : reason);
                }
            }
        } else {
            const QString geo = rec.value("geometry_file").toString();
            const QString file = geo.isEmpty() ? QString() : zoneDir + "/" + geo;
            const bool have = ok && !file.isEmpty() && QFileInfo::exists(file);
            for (Work& w : works_) {
                if (!w.found || !w.glb.isEmpty()) continue;
                if (run.reference) {
                    // A buffer the model's zone names as a reference, asked for by its plain name in this zone.
                    if (w.lod0Surfs != "," + name || w.refAttempt >= int(w.refZones.size()) || w.refZones[w.refAttempt] != run.zone)
                        continue;
                    if (!have || !storeGlb(w, file)) w.refAttempt++;  // the next candidate zone, in the next round
                    continue;
                }
                if (w.lod0Surfs != name) continue;
                if (have) storeGlb(w, file);
                else w.error = run.zone + ": " + reason;
            }
        }
    }
    QDir(dir).removeRecursively();
    if (--outstanding_ <= 0) pump();
}

void ModelCache::finishBatch() {
    QStringList done;
    const bool refresh = refreshBatch_;
    refreshBatch_ = false;
    for (const Work& w : works_) {
        Entry e;
        e.name = w.name;
        e.zone = w.zone;
        e.lod0Surfs = w.lod0Surfs;
        const bool ok = w.found && !w.glb.isEmpty() && w.error.isEmpty();
        if (refresh && !ok) {
            // A refresh that could not be done: the model keeps the GLB it had, now checked (not asked again).
            if (const auto it = index_.find(w.name); it != index_.end())
                it->second.selfVis = glbSelfVis(it->second.glbPath), it->second.lmapUv = glbLmapUv(it->second.glbPath);
            done << w.name;
            continue;
        }
        if (ok) {
            e.glbPath = w.glb;
            e.bounds = w.bounds;
            e.materials = w.materials;
            e.selfVis = glbSelfVis(w.glb);
            e.lmapUv = glbLmapUv(w.glb);
            if (refresh)  // its LODs were exported before too: fetched again when next wanted
                for (int lod = 1; lod <= 8; ++lod) {
                    QFile::remove(lodGlbPath(w.name, lod));
                    lodFailed_.erase(w.name + "#" + QString::number(lod));
                }
        } else {
            e.failed = true;
            e.error = !w.error.isEmpty() ? w.error : w.found ? QStringLiteral("LOD0 GLB not exported")
                                                             : QStringLiteral("model table not found in any candidate zone");
        }
        index_[w.name] = e;
        done << w.name;
    }
    if (forgetNotListed_) {
        // Fallback zones were set while this batch ran: models that failed only because no catalogue lists them
        // are asked again, now with those zones.
        forgetNotListed_ = false;
        std::erase_if(index_, [](const auto& kv) { return kv.second.failed && kv.second.error == "not listed in iw8_models.tsv"; });
    }
    writeIndex();
    works_.clear();
    queue_.clear();
    running_ = false;
    for (const QString& n : done) queued_.erase(n);
    emit progress(QString("model cache: %1 model(s) resolved").arg(done.size()));
    for (const QString& n : done) emit modelReady(n);
    emit batchEnded(int(done.size()));
    if (running_) return;  // a handler asked for more and that batch has started
    if (!pending_.isEmpty()) startBatch();
    else if (!refreshPending_.isEmpty()) startRefreshBatch();
    else if (!running_) QDir(root_ + "/w").removeRecursively();
}

// ---------------------------------------------------------------------------------------------------------------
// Headless test (main.cpp --model-cache-test)

int runModelCacheTest(const QStringList& names, const QString& outDir) {
    QDir().mkpath(outDir);
    QSettings ini(QDir(QCoreApplication::applicationDirPath()).filePath("IW8Radiant.ini"), QSettings::IniFormat);
    const auto tools = [&ini](const QString& key) -> QString {
        // The editor's Tool Paths (IW8Radiant.ini) with the MainWindow::toolPath defaults.
        static const QMap<QString, QString> defaults = {
            {"acts", "E:/mw124/atian-cod-tools/build/bin/Release/acts.exe"},
            {"replay", "E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe"},
            {"oodle", "E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll"},
        };
        return ini.value("tools/" + key, defaults.value(key)).toString();
    };

    ModelCache cache(tools);
    QStringList log;
    int actsRuns = 0;
    QObject::connect(&cache, &ModelCache::progress, &cache, [&](const QString& m) {
        log << m;
        if (m.startsWith("ACTS ") && m.contains("name(s)")) ++actsRuns;
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
    cache.request(names);
    loop.exec();
    const qint64 wallMs = timer.elapsed();

    QStringList lines;
    int bad = 0;
    lines << "# model-cache-test";
    lines << "# cache_root=" + cache.cacheRoot();
    for (const QString& n0 : names) {
        const QString n = n0.trimmed();
        const auto e = cache.lookup(n);
        if (!e) {
            lines << n + "\tERROR no entry";
            ++bad;
            continue;
        }
        if (e->failed) {
            lines << n + "\tFAILED\terror=" + e->error;
            ++bad;
            continue;
        }
        QString err;
        const auto model = cache.loadModel(n, &err);
        if (!model) {
            lines << n + "\tERROR load: " + err;
            ++bad;
            continue;
        }
        // Winding check: a triangle agrees when its geometric normal points along its vertex normals.
        const auto windingAgree = [](const ImportedModel& mm, size_t& total) {
            size_t agree = 0;
            total = 0;
            for (const ImportedMesh& m : mm.meshes)
                for (size_t i = 0; i + 2 < m.idx.size(); i += 3) {
                    ++total;
                    const Vec3 a = m.pos[m.idx[i]], b = m.pos[m.idx[i + 1]], c = m.pos[m.idx[i + 2]];
                    const Vec3 vn = m.normal[m.idx[i]] + m.normal[m.idx[i + 1]] + m.normal[m.idx[i + 2]];
                    if (dot(cross(b - a, c - a), vn) > 0) ++agree;
                }
            return agree;
        };
        size_t rawTris = 0;
        ImportedModel raw;
        GlbReadOptions rawOpt;
        rawOpt.scale = 1.0 / kMetresPerInch;
        const size_t rawAgree = readGlb(e->glbPath.toUtf8().toStdString(), raw, nullptr, rawOpt) ? windingAgree(raw, rawTris) : 0;

        size_t verts = 0, tris = 0, agree = 0;
        AABB fileBounds, radiant;  // fileBounds: glTF Y-up inches; radiant: Radiant Z-up (editor mapping)
        for (const ImportedMesh& m : model->meshes) {
            verts += m.pos.size();
            tris += m.idx.size() / 3;
            for (const Vec3& p : m.pos) {
                fileBounds.add(p);
                radiant.add(Vec3{p.x, -p.z, p.y});
            }
        }
        agree = windingAgree(*model, tris);
        const auto& xb = e->bounds;
        double maxDiff = 0;
        for (int i = 0; i < 3; ++i) {
            maxDiff = std::max(maxDiff, std::fabs(radiant.mins[i] - xb[i]));
            maxDiff = std::max(maxDiff, std::fabs(radiant.maxs[i] - xb[i + 3]));
        }
        const auto vec = [](const Vec3& v) {
            return QString("%1 %2 %3").arg(v.x, 0, 'f', 3).arg(v.y, 0, 'f', 3).arg(v.z, 0, 'f', 3);
        };
        lines << QString("%1\tzone=%2\tlod0=%3\tglb=%4\tmeshes=%5\tvertices=%6\ttriangles=%7")
                     .arg(n, e->zone, e->lod0Surfs, e->glbPath)
                     .arg(qulonglong(model->meshes.size()))
                     .arg(qulonglong(verts))
                     .arg(qulonglong(tris));
        lines << QString("  bounds_radiant_inches min=[%1] max=[%2]").arg(vec(radiant.mins), vec(radiant.maxs));
        lines << QString("  bounds_gltf_yup_inches min=[%1] max=[%2]").arg(vec(fileBounds.mins), vec(fileBounds.maxs));
        lines << QString("  xmodel_bounds_inches min=[%1 %2 %3] max=[%4 %5 %6] max_abs_diff=%7")
                     .arg(xb[0], 0, 'f', 3).arg(xb[1], 0, 'f', 3).arg(xb[2], 0, 'f', 3)
                     .arg(xb[3], 0, 'f', 3).arg(xb[4], 0, 'f', 3).arg(xb[5], 0, 'f', 3)
                     .arg(maxDiff, 0, 'g', 3);
        lines << QString("  winding_agree_raw=%1/%2 winding_agree_loaded=%3/%4")
                     .arg(qulonglong(rawAgree)).arg(qulonglong(rawTris)).arg(qulonglong(agree)).arg(qulonglong(tris));
    }
    qint64 bytes = 0;
    int files = 0;
    QDirIterator it(cache.cacheRoot(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        bytes += it.fileInfo().size();
        ++files;
    }
    lines << QString("# wall_ms=%1 acts_runs=%2 requested=%3 unresolved=%4").arg(wallMs).arg(actsRuns).arg(names.size()).arg(bad);
    lines << QString("# cache_files=%1 cache_bytes=%2").arg(files).arg(bytes);
    for (const QString& l : log) lines << "# progress: " + l;

    QFile out(QDir(outDir).filePath("model-cache-test.txt"));
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) out.write((lines.join('\n') + '\n').toUtf8());
    return bad ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Headless chain test (main.cpp --model-chain-test)

int runModelCacheChainTest(const QString& outDir) {
    QDir().mkpath(outDir);
    QStringList lines;
    lines << "# model-chain-test";
    int bad = 0;

    // A stand-in game folder: the cache only checks that these files exist.
    const QString fake = QDir(outDir).absoluteFilePath("fake_game");
    QDir().mkpath(fake + "/zone");
    for (const QString& f : {fake + "/game.exe", fake + "/oodle.dll", fake + "/zone/iw8r_fake_zone.ff"}) {
        QFile file(f);
        if (file.open(QIODevice::WriteOnly)) file.write("x");
    }
    // An "ACTS" that fails every run: a system program that knows none of the arguments, prints an error and ends.
    const QString failing = QDir::fromNativeSeparators(qEnvironmentVariable("SystemRoot", "C:/Windows")) + "/System32/whoami.exe";
    constexpr int kModels = 400;  // three batches: 150, 150, 100
    QStringList names;
    for (int i = 0; i < kModels; ++i) names << QString("iw8r_chain_test_model_%1").arg(i, 3, 10, QChar('0'));

    const auto runCase = [&](const QString& label, const QString& acts, bool expectStall) {
        const QString root = QDir(outDir).absoluteFilePath("cache_" + label);
        QDir(root).removeRecursively();
        qputenv("IW8R_MODEL_CACHE_ROOT", root.toUtf8());
        const auto tools = [&](const QString& key) -> QString {
            return key == "acts" ? acts : key == "replay" ? fake + "/game.exe" : fake + "/oodle.dll";
        };
        ModelCache cache(tools);
        cache.setFallbackZones({"iw8r_fake_zone"});  // the names are in no catalogue
        int batches = 0, emptyBatches = 0, ready = 0;
        QObject::connect(&cache, &ModelCache::batchEnded, &cache, [&](int resolved) {
            ++batches;
            if (resolved == 0) ++emptyBatches;
        });
        QObject::connect(&cache, &ModelCache::modelReady, &cache, [&](const QString&) { ++ready; });

        // The editor's loop (MainWindow::rebuildReferenceProps): a pass over the models after every batch, and the
        // chain decides whether to ask, wait, or stop.
        PropFetchChain chain;
        PropFetchStep last = PropFetchStep::Wait;
        QElapsedTimer timer;
        timer.start();
        int passes = 0;
        while (timer.elapsed() < 120000) {
            QStringList want;
            for (const QString& n : names)
                if (!cache.lookup(n)) want << n;
            last = chain.step(size_t(want.size()), cache.busy());
            ++passes;
            if (last == PropFetchStep::Done || last == PropFetchStep::Stalled) break;
            const int seen = batches;
            if (last == PropFetchStep::Request) cache.request(want);
            QElapsedTimer wait;
            wait.start();
            while (batches == seen && cache.busy() && wait.elapsed() < 60000) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        int entries = 0, failed = 0;
        for (const QString& n : names)
            if (const auto e = cache.lookup(n)) {
                ++entries;
                if (e->failed) ++failed;
            }
        const bool ok = expectStall ? last == PropFetchStep::Stalled && entries == 0 && !cache.problem().isEmpty() && !cache.busy() &&
                                          chain.requests() == 2 && emptyBatches == 2
                                    : last == PropFetchStep::Done && entries == kModels && failed == kModels && batches == 3 &&
                                          ready == kModels && !cache.busy();
        if (!ok) ++bad;
        lines << QString("case=%1 %2 end=%3 passes=%4 requests=%5 batches=%6 empty_batches=%7 entries=%8 failed=%9 model_ready=%10 ms=%11")
                     .arg(label, ok ? "ok" : "FAIL",
                          last == PropFetchStep::Done ? "done" : last == PropFetchStep::Stalled ? "stalled" : "TIME LIMIT")
                     .arg(passes).arg(chain.requests()).arg(batches).arg(emptyBatches).arg(entries).arg(failed).arg(ready)
                     .arg(timer.elapsed());
        if (!cache.problem().isEmpty()) lines << "  problem: " + cache.problem();
        if (failed)
            if (const auto e = cache.lookup(names.front())) lines << "  first error: " + e->error.left(160).simplified();
    };

    // 1. No ACTS at all: nothing can start. The chain asks twice and stops with the reason; it never waits for ever.
    runCase("missing_tool", fake + "/no_such_acts.exe", true);
    // 2. An ACTS that fails every run: every batch fails as a whole. The cache goes on to the last batch on its own
    //    and every model ends with an answer (failed), so the chain ends.
    if (QFileInfo::exists(failing)) {
        runCase("failing_acts", failing, false);
    } else {
        ++bad;
        lines << "case=failing_acts FAIL: no stand-in program at " + failing;
    }
    lines << QString("# %1").arg(bad ? "FAIL" : "ok");

    QFile out(QDir(outDir).filePath("model-chain-test.txt"));
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) out.write((lines.join('\n') + '\n').toUtf8());
    return bad ? 1 : 0;
}

}  // namespace iw8r::qt
