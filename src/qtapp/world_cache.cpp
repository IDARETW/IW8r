#include "world_cache.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QRegularExpression>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <utility>

#include "acts_runner.h"

namespace iw8r::qt {
namespace {

constexpr const char* kIndexFormat = "iw8r-world-cache-1";
// Zone XPaks with a payload are far larger than the 32 KiB headers (see model_cache.cpp).
constexpr qint64 kXpakMinBytes = 64 * 1024;
// mp_shipment: the two JSON exports are 387 MB for a 188 MB zone (about 2.1x). The estimate uses 2.5x.
constexpr double kExportPerZoneByte = 2.5;
constexpr qint64 kMaxExportBytes = 2LL * 1024 * 1024 * 1024;

qint64 folderBytes(const QString& dir) {
    qint64 bytes = 0;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        bytes += it.fileInfo().size();
    }
    return bytes;
}

QString tailOf(const QByteArray& log) { return QString::fromLocal8Bit(log.right(600)).trimmed(); }

}  // namespace

WorldCache::WorldCache(ToolPathFn tools, QObject* parent) : QObject(parent), tools_(std::move(tools)) {
    root_ = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/world_cache";
    QDir().mkpath(root_);
    QDir(root_ + "/w").removeRecursively();  // scratch left behind by a crash
    loadIndex();
}

WorldCache::~WorldCache() {
    // Shutting down: a run that ends from now on must not reach finishRun (it touches widgets that are already
    // gone). The runner does not call back for an owner that is gone.
    blockSignals(true);
    if (QCoreApplication::instance()) ActsRunner::shared()->cancel(this);
}

std::optional<QString> WorldCache::lookup(const QString& zone) const {
    const auto it = index_.find(zone);
    if (it == index_.end() || it->second.failed) return std::nullopt;
    return it->second.dir;
}

QString WorldCache::errorOf(const QString& zone) const {
    const auto it = index_.find(zone);
    return it == index_.end() ? QString() : it->second.error;
}

void WorldCache::request(const QString& zone) {
    const QString z = zone.trimmed();
    if (z.isEmpty() || lookup(z) || queue_.contains(z) || z == zone_) return;
    queue_.append(z);
    if (!running_) startNext();
}

bool WorldCache::busy() const { return running_ || !queue_.isEmpty(); }

QString WorldCache::cacheRoot() const { return root_; }

std::optional<StockWorld> WorldCache::load(const QString& zone, QString* err) const {
    const auto dir = lookup(zone);
    if (!dir) {
        if (err) *err = "zone not in the world cache: " + zone;
        return std::nullopt;
    }
    StockWorld w;
    std::string serr;
    if (!readStockWorld(dir->toStdString(), w, &serr)) {
        if (err) *err = QString::fromStdString(serr);
        return std::nullopt;
    }
    return w;
}

// ---------------------------------------------------------------------------------------------------------------
// Index

void WorldCache::loadIndex() {
    QFile f(root_ + "/index.json");
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject zones = QJsonDocument::fromJson(f.readAll()).object().value("zones").toObject();
    for (auto it = zones.begin(); it != zones.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.failed = o.value("failed").toBool();
        e.error = o.value("error").toString();
        e.bytes = qint64(o.value("bytes").toDouble());
        e.actsMs = qint64(o.value("acts_ms").toDouble());
        if (!e.failed) {
            e.dir = QDir(root_).filePath(o.value("dir").toString());
            if (!QFileInfo::exists(e.dir + "/assets")) continue;  // removed by hand: resolve again
        }
        index_[it.key()] = e;
    }
}

void WorldCache::writeIndex() const {
    QJsonObject zones;
    const QDir root(root_);
    for (const auto& [zone, e] : index_) {
        QJsonObject o;
        o.insert("dir", e.dir.isEmpty() ? QString() : root.relativeFilePath(e.dir));
        o.insert("bytes", double(e.bytes));
        o.insert("acts_ms", double(e.actsMs));
        o.insert("failed", e.failed);
        if (!e.error.isEmpty()) o.insert("error", e.error);
        zones.insert(zone, o);
    }
    QJsonObject top;
    top.insert("format", kIndexFormat);
    top.insert("zones", zones);
    QSaveFile f(root_ + "/index.json");
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(top).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

QString WorldCache::replayZoneDir() const { return QFileInfo(tools_("replay")).absolutePath() + "/zone"; }

QStringList WorldCache::xpaks() const {
    QStringList out;
    for (const QFileInfo& f : QDir(replayZoneDir()).entryInfoList({"*.xpak"}, QDir::Files, QDir::Name))
        if (f.size() >= kXpakMinBytes) out << f.absoluteFilePath();
    return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Runs

void WorldCache::fail(QString zone, const QString& why) {
    Entry e;
    e.failed = true;
    e.error = why;
    index_[zone] = e;
    writeIndex();
    emit progress("world cache: " + zone + " failed: " + why);
    emit ready(zone);
    running_ = false;
    zone_.clear();
    startNext();
}

void WorldCache::startNext() {
    if (running_ || queue_.isEmpty()) return;
    if (!QDir(root_).exists()) QDir().mkpath(root_);
    running_ = true;
    zone_ = queue_.takeFirst();

    const QString acts = tools_("acts"), replay = tools_("replay"), oodle = tools_("oodle");
    for (const QString& f : {acts, replay, oodle})
        if (!QFileInfo::exists(f)) {
            // Environment problem, not a property of the zone: nothing is recorded as failed.
            emit progress("Missing tool, world cache idle: " + f);
            running_ = false;
            zone_.clear();
            queue_.clear();
            return;
        }

    const QString ff = replayZoneDir() + "/" + zone_ + ".ff";
    if (!QFileInfo::exists(ff)) return fail(zone_, "no zone .ff at " + ff);
    const double estimate = double(QFileInfo(ff).size()) * kExportPerZoneByte;
    if (estimate > double(kMaxExportBytes))
        return fail(zone_, QString("estimated export %1 MB is over the 2 GB limit, not run").arg(qint64(estimate / 1048576)));

    scratch_ = root_ + "/w/" + zone_;
    QDir(scratch_).removeRecursively();
    QDir().mkpath(scratch_);

    ActsRequest request;
    request.zone = zone_;
    request.types = "gfx_map,gfx_map_trzone";
    request.outDir = QDir::toNativeSeparators(scratch_);
    emit progress("ACTS gfx_map + gfx_map_trzone " + zone_);
    runTimer_.start();
    ActsRunner::shared()->submit(tools_, request, this, [this](const ActsResult& result) { finishRun(result); });
}

void WorldCache::finishRun(const ActsResult& result) {
    const int exitCode = result.started ? result.exitCode : -1;
    const qint64 ms = runTimer_.elapsed();
    const QString zone = zone_;
    const QString zoneDir = scratch_ + "/mw19replay/" + zone;

    // The journal names the two files; both types need an ok row.
    bool gfxOk = false, trzOk = false;
    QFile journal(zoneDir + "/assets.jsonl");
    if (journal.open(QIODevice::ReadOnly)) {
        while (!journal.atEnd()) {
            const QJsonObject o = QJsonDocument::fromJson(journal.readLine()).object();
            if (o.value("status").toString() != "ok") continue;
            if (o.value("type").toString() == "gfx_map") gfxOk = true;
            if (o.value("type").toString() == "gfx_map_trzone") trzOk = true;
        }
        journal.close();
    }
    if (!gfxOk || !trzOk) {
        QDir(scratch_).removeRecursively();
        return fail(zone, QString("ACTS exit %1, no gfx_map / gfx_map_trzone export: %2").arg(exitCode).arg(tailOf(result.log)));
    }

    const qint64 bytes = folderBytes(zoneDir + "/assets");
    if (bytes > kMaxExportBytes) {
        QDir(scratch_).removeRecursively();
        return fail(zone, QString("export is %1 MB, over the 2 GB limit, not kept").arg(bytes / 1048576));
    }

    const QString dest = root_ + "/" + zone;
    QDir(dest).removeRecursively();
    if (!QDir().rename(zoneDir, dest)) {
        QDir(scratch_).removeRecursively();
        return fail(zone, "cannot move the export into the cache");
    }
    QDir(scratch_).removeRecursively();

    Entry e;
    e.dir = dest;
    e.bytes = bytes;
    e.actsMs = ms;
    index_[zone] = e;
    writeIndex();
    emit progress(QString("world cache: %1 cached, %2 MB, ACTS %3 ms").arg(zone).arg(bytes / 1048576).arg(ms));
    running_ = false;
    zone_.clear();
    emit ready(zone);
    startNext();
}

void WorldCache::requestStreamed(const QString& zone, const QStringList& tiles) {
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

int runWorldCacheTest(const QString& zone, const QString& outDir) {
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

    WorldCache cache(tools);
    QStringList log;
    QObject::connect(&cache, &WorldCache::progress, &cache, [&](const QString& m) { log << m; });
    // IW8R_STREAM_AT="x y radius [limit]": after the zone, the streamed world tiles near that point are exported too
    // (big maps), and the load below includes them.
    bool streamDone = true;
    QObject::connect(&cache, &WorldCache::streamedReady, &cache, [&](const QString&, int loaded, int failed) {
        log << QString("streamed tiles: %1 exported, %2 failed").arg(loaded).arg(failed);
        streamDone = true;
    });

    QEventLoop loop;
    QTimer poll;
    poll.setInterval(100);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (!cache.busy()) loop.quit();
    });
    QTimer::singleShot(30 * 60 * 1000, &loop, &QEventLoop::quit);

    QElapsedTimer wall;
    wall.start();
    poll.start();
    cache.request(zone);
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

    QStringList lines;
    lines << "# world-cache-test";
    lines << "# cache_root=" + cache.cacheRoot();
    lines << "# zone=" + zone;
    int bad = 0;
    const auto dir = cache.lookup(zone);
    if (!dir) {
        lines << "FAILED " + cache.errorOf(zone);
        bad = 1;
    } else {
        QElapsedTimer parse;
        parse.start();
        QString err;
        const auto world = cache.load(zone, &err);
        const qint64 parseMs = parse.elapsed();
        if (!world) {
            lines << "ERROR load: " + err;
            bad = 1;
        } else {
            size_t verts = 0, tris = 0;
            for (const ImportedMesh& m : world->meshes) {
                verts += m.pos.size();
                tris += m.idx.size() / 3;
            }
            const AABB& b = world->bounds;
            const auto vec = [](const Vec3& v) {
                return QString("%1 %2 %3").arg(v.x, 0, 'f', 1).arg(v.y, 0, 'f', 1).arg(v.z, 0, 'f', 1);
            };
            lines << QString("cache_dir=%1").arg(*dir);
            lines << QString("meshes=%1 vertices=%2 triangles=%3").arg(world->meshes.size()).arg(verts).arg(tris);
            lines << QString("bounds_zup_inches min=[%1] max=[%2]").arg(vec(b.mins), vec(b.maxs));
            lines << QString("cache_bytes=%1").arg(folderBytes(*dir));
            lines << QString("request_ms=%1 (ACTS run, 0 or near 0 when cached) parse_ms=%2").arg(wallMs).arg(parseMs);
            for (const std::string& w : world->warnings) lines << "warning: " + QString::fromStdString(w);
            for (const ImportedMesh& m : world->meshes)
                lines << QString("  material %1 vertices=%2 triangles=%3").arg(QString::fromStdString(m.material)).arg(m.pos.size()).arg(m.idx.size() / 3);
        }
    }
    for (const QString& l : log) lines << "# progress: " + l;

    QFile out(QDir(outDir).filePath("world-cache-test.txt"));
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) out.write((lines.join('\n') + '\n').toUtf8());
    return bad;
}

}  // namespace iw8r::qt
