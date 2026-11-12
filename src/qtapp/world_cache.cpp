#include "world_cache.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
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
