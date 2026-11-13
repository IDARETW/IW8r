#include "stock_sky.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <algorithm>

#include "acts_runner.h"
#include "sky_image.h"

namespace iw8r::qt {
namespace {

QString catalogPath() {
    QDir d(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 4; ++up, d.cdUp())
        if (QFileInfo::exists(d.filePath("data/asset_catalog/iw8_images.tsv"))) return d.filePath("data/asset_catalog/iw8_images.tsv");
    return {};
}

// Catalog images whose name starts with "sky_" (the stock sky images; material tw/<name> draws each), with their zones.
std::map<QString, QStringList> skyCatalog(bool all = false) {
    std::map<QString, QStringList> out;
    QFile f(catalogPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const int tab = line.indexOf('\t');
        if (tab <= 0 || line.startsWith('#')) continue;
        const QString name = line.left(tab);
        if (!all && (!name.startsWith("sky_") || name.contains('&') || name.contains('~'))) continue;
        out[name] = line.mid(tab + 1).trimmed().split(',', Qt::SkipEmptyParts);
    }
    return out;
}

QString imageNameOf(const QString& sky) { return QString::fromStdString(skyImageName(sky.trimmed().toStdString())); }

}  // namespace

QStringList stockSkyNames() {
    QStringList maps, other;
    for (const auto& [name, zones] : skyCatalog()) (name.startsWith("sky_mp_") ? maps : other) << name;
    return maps + other;  // std::map keeps each group sorted
}

StockSkyCache::StockSkyCache(ToolPathFn tools, const QString& imagesDir, QObject* parent, bool anyImage)
    : QObject(parent), tools_(std::move(tools)), imagesDir_(imagesDir), anyImage_(anyImage) {
    QDir().mkpath(imagesDir_);
    zones_ = skyCatalog(anyImage_);
}

StockSkyCache::~StockSkyCache() {
    // Shutting down: a run that ends from now on must not reach finishRun (it touches widgets that are already
    // gone). The runner does not call back for an owner that is gone.
    blockSignals(true);
    if (QCoreApplication::instance()) ActsRunner::shared()->cancel(this);
}

QString StockSkyCache::cachedDds(const QString& sky) const {
    const QString name = keyOf(sky);
    if (name.isEmpty()) return {};
    const QString file = fileOf(name);
    return QFileInfo::exists(file) ? file : QString();
}

QString StockSkyCache::lastError(const QString& sky) const {
    const auto it = errors_.find(keyOf(sky));
    return it == errors_.end() ? QString() : it->second;
}

void StockSkyCache::request(const QString& sky) {
    const QString name = keyOf(sky);
    if (name.isEmpty() || !cachedDds(name).isEmpty() || queued_.count(name)) return;
    queued_.insert(name);
    pending_ << name;
    if (!running_) pump();
}

void StockSkyCache::pump() {
    while (!running_ && !pending_.isEmpty()) {
        current_ = pending_.takeFirst();
        errors_.erase(current_);
        const QString zoneDir = QFileInfo(tools_("replay")).absolutePath() + "/zone";
        currentZones_.clear();
        // Catalog zones, then the map zone of a techsets_ zone and of the name (sky_mp_donetsk is catalogued only in
        // techsets_mp_donetsk, which holds no image payload).
        QStringList candidates;
        if (const auto it = zones_.find(current_); it != zones_.end()) {
            candidates = it->second;
            for (const QString& z : it->second)
                if (z.startsWith("techsets_")) candidates << z.mid(9);
            if (current_.startsWith("sky_mp_")) candidates << current_.mid(4);
        }
        // The stock map shown (addMapZone): its own sky (mp_m_speed's sky_mp_speed_midday) is in no traced zone.
        for (const QString& z : mapZones_) candidates << z << "techsets_" + z;
        for (const QString& z : candidates)
            if (QFileInfo::exists(zoneDir + "/" + z + ".ff") && !currentZones_.contains(z)) currentZones_ << z;
        QString missingTool;
        for (const QString& key : {"acts", "replay", "oodle"})
            if (!QFileInfo::exists(tools_(key))) missingTool = tools_(key);
        if (!missingTool.isEmpty() || currentZones_.isEmpty()) {
            const QString why = !missingTool.isEmpty() ? "missing tool " + missingTool
                                : zones_.count(current_) || !mapZones_.isEmpty() ? "no catalog zone of it in the Replay zone folder"
                                                         : "not a stock sky image (not in iw8_images.tsv)";
            errors_[current_] = why;
            queued_.erase(current_);
            const QString done = current_;
            current_.clear();
            emit progress("sky " + done + ": " + why);
            emit skyReady(done, QString());
            continue;
        }
        startRun();
    }
}

void StockSkyCache::startRun() {
    const QString zone = currentZones_.takeFirst();
    // Not the material cache's w/ folder: MaterialCache removes it when its batch ends.
    runDir_ = QDir::toNativeSeparators(QDir(imagesDir_).absoluteFilePath("../sky_w/" + QString::number(qHash(current_), 16)));
    // A short run folder: ACTS writes <run>/mw19replay/<zone>/assets/image/<name>.0.dds, and packed image names are
    // long enough to cross Windows' 260-character path limit (the export then reports "failed").
    QDir(runDir_).removeRecursively();
    QDir().mkpath(runDir_);
    ActsRequest request;
    request.zone = zone;
    request.types = "image";
    request.names = QStringList{current_};
    request.outDir = runDir_;
    lastArgs_ = "-a image -n " + current_ + " " + zone + ".ff";
    running_ = true;
    emit progress("ACTS sky " + current_ + " from " + zone);
    ActsRunner::shared()->submit(tools_, request, this, [this, zone](const ActsResult& result) { finishRun(zone, result); });
}

void StockSkyCache::finishRun(const QString& zone, const ActsResult& result) {
    running_ = false;
    const int exitCode = result.started ? result.exitCode : -1;

    const QString zoneOut = runDir_ + "/mw19replay/" + zone;
    QString file, why;
    QFile journal(zoneOut + "/assets.jsonl");
    if (journal.open(QIODevice::ReadOnly)) {
        while (!journal.atEnd()) {
            const QJsonObject o = QJsonDocument::fromJson(journal.readLine()).object();
            if (o.value("type").toString() != "image" || o.value("name").toString() != current_) continue;
            if (o.value("status").toString() == "ok") file = o.value("file").toString();
            else
                why = QString("%1 %2 (ACTS: %3)")
                          .arg(o.value("status").toString(), o.value("reason").toString(),
                               QString::fromLocal8Bit(result.log.right(300)).simplified() + " | args: " + lastArgs_);
        }
        journal.close();
        if (file.isEmpty() && why.isEmpty()) why = "not in zone";
    } else {
        why = QString("ACTS wrote no journal (exit %1): %2").arg(exitCode).arg(QString::fromLocal8Bit(result.log.right(400)).trimmed());
    }
    QString dest;
    if (!file.isEmpty()) {
        dest = fileOf(current_);
        QFile::remove(dest);
        if (!QFile::rename(zoneOut + "/" + file, dest)) {
            why = "cannot move the exported DDS to " + dest;
            dest.clear();
        }
    }
    QDir(runDir_).removeRecursively();
    if (dest.isEmpty() && !currentZones_.isEmpty()) {  // try the next catalog zone
        emit progress("sky " + current_ + ": " + zone + ": " + why);
        startRun();
        return;
    }
    const QString done = current_;
    current_.clear();
    queued_.erase(done);
    if (dest.isEmpty()) errors_[done] = zone + ": " + why;
    emit progress(dest.isEmpty() ? "sky " + done + " not exported: " + errors_[done] : "sky " + done + " exported");
    emit skyReady(done, dest);
    pump();
}

int runStockSkyTest(const QString& sky, const QString& outDir) {
    QDir().mkpath(outDir);
    QSettings ini(QDir(QCoreApplication::applicationDirPath()).filePath("IW8Radiant.ini"), QSettings::IniFormat);
    const auto tools = [&ini](const QString& key) -> QString {
        static const QMap<QString, QString> defaults = {
            {"acts", "E:/mw124/atian-cod-tools/build/bin/Release/acts.exe"},
            {"replay", "E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe"},
            {"oodle", "E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll"},
        };
        return ini.value("tools/" + key, defaults.value(key)).toString();
    };
    const QString images = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/material_cache/images";
    StockSkyCache cache(tools, images);
    QStringList lines{"# stock-sky-test", "# names=" + stockSkyNames().join(',')};
    QObject::connect(&cache, &StockSkyCache::progress, &cache, [&](const QString& m) { lines << "# progress: " + m; });
    QElapsedTimer timer;
    timer.start();
    QString file = cache.cachedDds(sky);
    const bool wasCached = !file.isEmpty();
    if (!wasCached) {
        QEventLoop loop;
        QObject::connect(&cache, &StockSkyCache::skyReady, &loop, [&](const QString&, const QString& f) {
            file = f;
            loop.quit();
        });
        cache.request(sky);
        if (cache.busy()) loop.exec();
    }
    int rc = 1;
    if (file.isEmpty()) {
        lines << sky + "\tFAILED\t" + cache.lastError(sky);
    } else {
        QFile f(file);
        std::string bytes;
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray b = f.readAll();
            bytes.assign(b.constData(), size_t(b.size()));
        }
        std::string cube, err;
        SkyConvertInfo info;
        SkyConvertOptions so;
        so.faceSize = 64;
        const bool ok = skyCubemapFromDds(bytes, cube, so, &info, &err);
        lines << QString("%1\t%2\t%3\t%4x%5 dxgi %6\tcube %7\t%8")
                     .arg(sky, wasCached ? "cached" : "exported", file)
                     .arg(info.sourceWidth)
                     .arg(info.sourceHeight)
                     .arg(info.sourceDxgiFormat)
                     .arg(ok && isConverterSkyCubemap(cube) ? "ok" : "FAILED")
                     .arg(QString::fromStdString(err));
        rc = ok ? 0 : 1;
    }
    lines << QString("# wall_ms=%1").arg(timer.elapsed());
    QFile out(outDir + "/stock-sky-test.txt");
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) out.write((lines.join('\n') + '\n').toUtf8());
    return rc;
}

QString StockSkyCache::keyOf(const QString& name) const { return anyImage_ ? name.trimmed() : imageNameOf(name); }

QString StockSkyCache::fileOf(const QString& key) const {
    QString stem = key;
    for (QChar& c : stem)
        if (!(c.isLetterOrNumber() || c == '_' || c == '-')) c = '_';
    return imagesDir_ + "/" + stem + ".dds";
}

}  // namespace iw8r::qt
