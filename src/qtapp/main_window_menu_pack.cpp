// `IW8Radiant.exe --menu-pack <kinds|all> [--fill] [--limit N] [--out <folder>]`: the side panels' menu packs
// (menu_pack.h; owner 2026-11-16: "build a one-time full metadata and thumbnails for everything we can in the menus,
// compress it as best as we can ... and include it as part of the project"; "compress previews too").
//
// Every entry of a panel's catalogue gets what the editor's caches know of it, gathered once:
//   materials  64 px thumbnail, 160 px preview (the colour image), facts: techset, material type, textures
//   models     160 px thumbnail, 256 px preview (the textured picture), facts: size, materials, zone
//   images     64 px thumbnail, 160 px preview, facts: size, DDS format, mip levels
//   effects    96 px picture of the preview at its busiest moment, the effect's definition (its preview plays without
//              an export; child effects of runners too), pill: loop / one-shot length, facts: emitters and elements
//   sounds     pill: length (range over variants) and variant count, facts: loop, variants, length
//   prefabs    112 px thumbnail, 256 px preview (the textured picture), facts: entities, brushes, models
// --fill exports what the caches lack first (effects and images here; materials, models and prefabs with
// `--thumb-pack materials|models|prefabs`, whose packs this reads). Without it, only what is cached is packed.
#include "main_window.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>

#include <cstdio>
#include <functional>
#include <set>

#include "asset_audit.h"
#include "map_text.h"
#include "material_cache.h"
#include "menu_pack.h"
#include "model_cache.h"
#include "panel_chrome.h"
#include "prefab_browser.h"
#include "sound_preview.h"
#include "stock_sky.h"
#include "textured_thumbs.h"
#include "texture_decode.h"
#include "thumb_store.h"
#include "vfx_preview.h"
#include "vfx_sim.h"

namespace iw8r::qt {

QStringList ddsFactsOf(const std::string& headerBytes);  // main_window.cpp

namespace {

void pump(int ms) {
    QElapsedTimer t;
    t.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    } while (t.elapsed() < ms);
}

bool waitFor(const std::function<bool()>& done, int ms) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) pump(30);
    return done();
}

QDir dataDir() {
    QDir d(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 4; ++up, d.cdUp())
        if (QFileInfo::exists(d.filePath("data/asset_catalog"))) return QDir(d.filePath("data"));
    return QDir(QCoreApplication::applicationDirPath());
}

// A catalogue's first column (and its zones), as the browsers list it.
QList<QPair<QString, QString>> catalogRows(const QString& file, bool skipStreamOnly) {
    QList<QPair<QString, QString>> out;
    QFile f(dataDir().filePath("asset_catalog/" + file));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream in(&f);
    in.readLine();
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.startsWith('#')) continue;
        const int tab = line.indexOf('\t');
        if (tab <= 0) continue;
        const QString zones = line.mid(tab + 1);
        if (skipStreamOnly) {
            bool streamOnly = true;
            for (const QString& z : zones.split(',', Qt::SkipEmptyParts))
                streamOnly &= z.trimmed() == "global_stream_mp" || z.trimmed() == "techsets_global_stream_mp";
            if (streamOnly) continue;
        }
        out << qMakePair(line.left(tab), zones);
    }
    return out;
}

QImage fitted(const QImage& img, int side) {
    if (img.isNull()) return {};
    if (img.width() <= side && img.height() <= side) return img;
    return img.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QImage ddsPicture(const QString& file, int side) {
    app::Rgba8Image small;
    if (file.isEmpty() || !app::loadDdsPreview(file.toStdString(), side, small)) return {};
    return QImage(small.pixels.data(), small.width, small.height, small.width * 4, QImage::Format_RGBA8888).convertToFormat(QImage::Format_RGB32);
}

QString secs(double s) { return QString::number(s, 'f', s < 10 ? 1 : 0); }

void report(const QString& kind, const MenuPackWriter::Stats& s, qint64 ms) {
    std::printf("menu pack %s: %d entries, %d thumbnails (%d stored, %d whole JPEGs), %d previews, %d data blocks (%.1f -> %.1f MB), "
                "%.1f MB in all (%.1f MB of pictures, %.2f MB of texts; %.1f MB of JPEG headers not stored), %.1f s\n",
                qPrintable(kind), s.entries, s.thumbs, s.uniqueThumbs, s.wholeJpegs, s.previews, s.datas, s.dataPlainBytes / 1048576.0,
                s.dataBytes / 1048576.0, s.bytes / 1048576.0, s.thumbBytes / 1048576.0, s.textBytes / 1048576.0, s.savedHeaderBytes / 1048576.0,
                ms / 1000.0);
    std::fflush(stdout);
}

}  // namespace

int MainWindow::runMenuPack(const QString& kindsArg, const QString& outDirArg, bool fill, int limit) {
    const QStringList all{"sounds", "effects", "images", "materials", "models", "prefabs"};
    const QStringList kinds = kindsArg == "all" ? all : kindsArg.split(',', Qt::SkipEmptyParts);
    const QString outDir = outDirArg.isEmpty() ? MenuPack::folder() : outDirArg;
    const QString thumbDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/thumbs/";
    int failures = 0;
    for (const QString& kind : kinds) {
        QElapsedTimer clock;
        clock.start();
        std::printf("menu pack %s: gathering%s\n", qPrintable(kind), fill ? " (exporting what is missing)" : "");
        std::fflush(stdout);
        if (kind == "sounds") {
            MenuPackWriter w(kind, 80);
            auto* sp = findChild<SoundPreview*>("soundPreview");
            bool ready = false;
            if (sp) {
                const auto c = connect(sp, &SoundPreview::indexReady, this, [&ready] { ready = true; });
                sp->loadIndex();
                waitFor([&] { return ready; }, 180000);
                disconnect(c);
            }
            const QHash<QString, SoundPreview::AliasFacts> facts = sp ? sp->aliasFacts() : QHash<QString, SoundPreview::AliasFacts>();
            int n = 0;
            for (const auto& [alias, zones] : catalogRows("iw8_common_sounds.tsv", false)) {
                if (limit > 0 && n >= limit) break;
                ++n;
                const auto it = facts.constFind(alias);
                QString pill, more;
                if (it != facts.constEnd()) {
                    const SoundPreview::AliasFacts& f = *it;
                    QStringList parts;
                    if (f.longest >= 0.05)
                        parts << (f.longest - f.shortest < 0.05 ? secs(f.longest) : secs(f.shortest) + QChar(0x2013) + secs(f.longest)) + " s";
                    if (f.variants > 1) parts << QString(QChar(0x00D7)) + QString::number(f.variants);
                    pill = parts.join(factSep());
                    more = QString("%1 variant%2%3").arg(f.variants).arg(f.variants == 1 ? "" : "s").arg(f.loop ? factSep() + "loops" : QString());
                }
                w.add(alias, {}, pill, more);
            }
            MenuPackWriter::Stats st;
            QString err;
            if (!w.save(QDir(outDir).filePath(kind + ".iw8mp"), &err, &st)) ++failures, std::printf("  %s\n", qPrintable(err));
            report(kind, st, clock.elapsed());
            if (facts.isEmpty()) std::printf("  note: the sound banks' index was not read (prepare them once with Play in the Sounds panel)\n");
            continue;
        }
        if (kind == "effects") {
            MenuPackWriter w(kind, 85);
            QStringList names;
            for (const auto& [n, z] : catalogRows("iw8_common_vfx.tsv", false)) names << n;
            if (limit > 0 && names.size() > limit) names = names.mid(0, limit);
            if (fill) {
                // Exports first (a few at a time through the ACTS runner), then their children.
                int asked = 0;
                for (const QString& n : names)
                    if (findVfxExport(n).isEmpty() && requestVfxExport(n)) ++asked;
                std::printf("  exporting %d effect(s)...\n", asked);
                std::fflush(stdout);
                waitFor([this] { return vfxExporting_.isEmpty(); }, 6 * 60 * 60 * 1000);
            }
            std::set<QString> packed;
            int pictured = 0;
            // The effect's definition (and its runners' children, again and again), so the preview plays offline.
            const std::function<void(const QString&, const QString&, int, bool)> addEffect = [&](const QString& name, const QString& parent, int depth,
                                                                                                bool picture) {
                if (packed.count(name) || depth > 3) return;
                QString file = findVfxExport(name);
                if (file.isEmpty() && fill && !parent.isEmpty()) {
                    bool done = false;
                    const auto c = connect(this, &MainWindow::vfxExportDone, this, [&](const QString& n, bool) {
                        if (n == name) done = true;
                    });
                    if (requestVfxExport(name, vfxZoneOf(parent))) waitFor([&] { return done; }, 240000);
                    disconnect(c);
                    file = findVfxExport(name);
                }
                QFile f(file);
                if (file.isEmpty() || !f.open(QIODevice::ReadOnly)) {
                    if (parent.isEmpty()) w.add(name, {}, {}, "not exported yet");
                    packed.insert(name);
                    return;
                }
                const QByteArray json = f.readAll();
                VfxSimDef def;
                if (!buildVfxSim(json.toStdString(), def)) {
                    w.add(name, {}, {}, "no particles the preview can show");
                    packed.insert(name);
                    return;
                }
                packed.insert(name);
                QMap<QString, int> kinds;
                for (const VfxEmitterSim& e : def.emitters) {
                    const int el = e.element;
                    kinds[el == 7 ? "models" : el == 9 ? "child effects" : el == 5 || el == 6 ? "lights" : el == 2 ? "decals" : el == 3 ? "flares"
                          : el == 11 ? "vector fields" : "sprites"]++;
                }
                QStringList k;
                for (auto it = kinds.begin(); it != kinds.end(); ++it) k << QString("%1 %2").arg(it.value()).arg(it.key());
                const QString pill = def.duration < 0 ? QString("loop") : QString("%1 s").arg(def.duration, 0, 'f', 1);
                const QString facts = QString("%1 emitter%2: %3").arg(def.emitters.size()).arg(def.emitters.size() == 1 ? "" : "s").arg(k.join(", "));
                QImage thumb;
                if (picture) {
                    // The preview at its busiest moment, once its sprites are read (a few seconds at most).
                    VfxPreview pv;
                    pv.setAttribute(Qt::WA_DontShowOnScreen);
                    pv.resize(192, 240);
                    pv.show();
                    pv.setEffect(def);
                    waitFor([&] {
                        pv.frame();
                        return pv.materialsPending() == 0 && !pv.exportsPending();
                    }, fill ? 60000 : 4000);
                    pv.setTime(pv.busiestTime());
                    const QImage fr = pv.frame();
                    if (!fr.isNull() && pv.texturedParticles() + pv.modelsDrawn() > 0) thumb = fitted(fr, 96), ++pictured;
                }
                w.add(name, thumb, parent.isEmpty() ? pill : QString(), facts, {}, json);
                for (const VfxEmitterSim& e : def.emitters)
                    for (const std::string& child : e.runners) addEffect(QString::fromStdString(child), name, depth + 1, false);
            };
            int done = 0;
            for (const QString& n : names) {
                addEffect(n, {}, 0, true);
                if (++done % 50 == 0) {
                    std::printf("  %d of %lld effects (%d pictured), %.0f s\n", done, qlonglong(names.size()), pictured, clock.elapsed() / 1000.0);
                    std::fflush(stdout);
                }
            }
            MenuPackWriter::Stats st;
            QString err;
            if (!w.save(QDir(outDir).filePath(kind + ".iw8mp"), &err, &st)) ++failures, std::printf("  %s\n", qPrintable(err));
            report(kind, st, clock.elapsed());
            continue;
        }
        if (kind == "images") {
            MenuPackWriter w(kind, 75);  // 50k images: 64 px thumbnails, 160 px previews at 75 keep the pack small
            auto store = std::make_shared<ThumbStore>(thumbDir + "images.pack");
            QStringList names;
            for (const auto& [n, z] : catalogRows("iw8_images.tsv", false)) names << n;
            if (limit > 0 && names.size() > limit) names = names.mid(0, limit);
            // One image from its DDS: the thumbnail, the preview and the facts.
            const auto fromDds = [](const QString& dds, QImage* thumb, QImage* preview, QString* facts) {
                if (thumb->isNull()) *thumb = ddsPicture(dds, 64);
                *preview = ddsPicture(dds, 160);
                QFile f(dds);
                if (!f.open(QIODevice::ReadOnly)) return;
                const QByteArray head = f.read(160);
                int fw = 0, fh = 0;
                app::Rgba8Image tiny;
                app::loadDdsPreview(dds.toStdString(), 1, tiny, &fw, &fh);
                QStringList parts{QString("%1 x %2").arg(fw).arg(fh)};
                parts += ddsFactsOf(head.toStdString());
                *facts = parts.join(factSep());
            };
            QStringList missing;
            int done = 0;
            for (const QString& n : names) {
                const QString dds = imageCache_ ? imageCache_->cachedDds(n) : QString();
                QImage thumb = store->getAny(n), preview;
                QString facts;
                if (!dds.isEmpty()) fromDds(dds, &thumb, &preview, &facts);
                else if (thumb.isNull()) missing << n;
                w.add(n, fitted(thumb, 64), {}, facts, fitted(preview, 160));
                if (++done % 10000 == 0) std::printf("  %d of %lld images, %.0f s\n", done, qlonglong(names.size()), clock.elapsed() / 1000.0), std::fflush(stdout);
            }
            if (fill && !missing.isEmpty()) {
                // Batches exported into a scratch folder, packed and deleted: the image cache does not grow.
                const QString scratch = QDir::tempPath() + "/iw8r_menu_images";
                constexpr int kBatch = 300;
                int got = 0;
                for (int at = 0; at < missing.size(); at += kBatch) {
                    QDir(scratch).removeRecursively();
                    QDir().mkpath(scratch);
                    const QStringList batch = missing.mid(at, kBatch);
                    {
                        StockSkyCache cache([this](const QString& k) { return toolPath(k); }, scratch, nullptr, true);
                        for (const QString& n : batch) cache.request(n);
                        waitFor([&cache] { return !cache.busy(); }, 2 * 60 * 60 * 1000);
                        for (const QString& n : batch) {
                            const QString dds = cache.cachedDds(n);
                            if (dds.isEmpty()) continue;
                            QImage thumb, preview;
                            QString facts;
                            fromDds(dds, &thumb, &preview, &facts);
                            w.add(n, fitted(thumb, 64), {}, facts, fitted(preview, 160));
                            ++got;
                        }
                    }
                    QDir(scratch).removeRecursively();
                    const double s = clock.elapsed() / 1000.0;
                    const int doneN = std::min<int>(at + kBatch, int(missing.size()));
                    std::printf("  exported %d of %lld missing images (%d with a picture), %.0f s, about %.0f min left\n", doneN,
                                qlonglong(missing.size()), got, s, s / std::max(1, doneN) * (missing.size() - doneN) / 60.0);
                    std::fflush(stdout);
                    // Saved every few batches: a stopped run keeps what it made (the next run skips nothing it has, but
                    // the pictures are in the pack).
                    if ((at / kBatch) % 10 == 9) w.save(QDir(outDir).filePath(kind + ".iw8mp"));
                }
            }
            MenuPackWriter::Stats st;
            QString err;
            if (!w.save(QDir(outDir).filePath(kind + ".iw8mp"), &err, &st)) ++failures, std::printf("  %s\n", qPrintable(err));
            report(kind, st, clock.elapsed());
            continue;
        }
        if (kind == "materials") {
            MenuPackWriter w(kind, 80);
            auto full = std::make_shared<ThumbStore>(thumbDir + "materials.full.pack");
            auto own = std::make_shared<ThumbStore>(thumbDir + "materials.pack");
            waitFor([this] { return matCache_->indexReady(); }, 120000);
            int done = 0;
            for (const auto& [n, z] : catalogRows("iw8_materials.tsv", false)) {
                if (limit > 0 && done >= limit) break;
                const auto e = matCache_->lookup(n);
                const QString dds = e && !e->failed && QFileInfo::exists(e->colorDds) ? e->colorDds : QString();
                QImage thumb = full->getAny(n);
                if (thumb.isNull()) thumb = own->getAny(n);
                if (thumb.isNull() && !dds.isEmpty()) thumb = ddsPicture(dds, 64);
                const QImage preview = dds.isEmpty() ? QImage() : ddsPicture(dds, 160);
                QStringList facts;
                if (e && !e->failed) {
                    if (!e->techset.isEmpty()) facts << "techset " + e->techset;
                    if (!e->materialType.isEmpty()) facts << QString(e->materialType).remove("MTL_TYPE_").toLower();
                    if (!e->textures.isEmpty()) facts << QString("%1 images").arg(e->textures.size());
                }
                w.add(n, fitted(thumb, 64), {}, facts.join(factSep()), fitted(preview, 160));
                if (++done % 10000 == 0) std::printf("  %d materials, %.0f s\n", done, clock.elapsed() / 1000.0), std::fflush(stdout);
            }
            MenuPackWriter::Stats st;
            QString err;
            if (!w.save(QDir(outDir).filePath(kind + ".iw8mp"), &err, &st)) ++failures, std::printf("  %s\n", qPrintable(err));
            report(kind, st, clock.elapsed());
            continue;
        }
        if (kind == "models") {
            MenuPackWriter w(kind, 82);
            TexturedThumbs* tt = texturedThumbs();
            auto grey = std::make_shared<ThumbStore>(thumbDir + "models.full.pack");
            int done = 0;
            for (const auto& [n, z] : catalogRows("iw8_models.tsv", true)) {
                if (limit > 0 && done >= limit) break;
                ++done;
                QImage pic = tt ? tt->storePtr(TexturedThumbs::Model)->getAny(n) : QImage();
                if (pic.isNull()) pic = grey->getAny(n);
                QStringList facts;
                if (const auto e = modelCacheStock_ ? modelCacheStock_->lookup(n) : std::nullopt; e && !e->failed) {
                    const auto& b = e->bounds;
                    facts << QString("%1 x %2 x %3 in").arg(b[3] - b[0], 0, 'f', 0).arg(b[4] - b[1], 0, 'f', 0).arg(b[5] - b[2], 0, 'f', 0);
                    if (!e->materials.isEmpty()) facts << QString("%1 material%2").arg(e->materials.size()).arg(e->materials.size() == 1 ? "" : "s");
                    if (!e->zone.isEmpty()) facts << e->zone;
                }
                w.add(n, fitted(pic, 160), {}, facts.join(factSep()), fitted(pic, 256));
            }
            MenuPackWriter::Stats st;
            QString err;
            if (!w.save(QDir(outDir).filePath(kind + ".iw8mp"), &err, &st)) ++failures, std::printf("  %s\n", qPrintable(err));
            report(kind, st, clock.elapsed());
            continue;
        }
        if (kind == "prefabs") {
            // The bundled library (data/prefabs), keyed by the path under it (the same on every machine).
            MenuPackWriter w(kind, 82);
            TexturedThumbs* tt = texturedThumbs();
            const QDir root(dataDir().filePath("prefabs"));
            int done = 0;
            for (QDirIterator it(root.path(), {"*.map"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
                const QString f = QFileInfo(it.next()).absoluteFilePath();
                if (f.contains(".iw8prefab/", Qt::CaseInsensitive)) continue;
                if (limit > 0 && done >= limit) break;
                ++done;
                QImage pic = tt ? tt->storePtr(TexturedThumbs::Prefab)->getAny(f) : QImage();
                if (pic.isNull()) pic = PrefabBrowser::thumbnail(f, 112);
                int ents = 0, brushes = 0;
                std::set<std::string> models;
                std::string text;
                if (readFile(f.toStdString(), text)) {
                    Id next = 1;
                    ParseResult pr = parseMap(text, next);
                    if (pr.map)
                        for (const auto& e : pr.map->entities) {
                            if (!e.isWorldspawn()) ++ents;
                            if (const std::string* m = e.value("model")) models.insert(*m);
                            for (const auto& item : e.items) brushes += item.prim() != nullptr;
                        }
                }
                QStringList facts;
                if (ents) facts << QString("%1 entit%2").arg(ents).arg(ents == 1 ? "y" : "ies");
                if (brushes) facts << QString("%1 brush%2").arg(brushes).arg(brushes == 1 ? "" : "es");
                if (!models.empty()) facts << QString("%1 model%2").arg(models.size()).arg(models.size() == 1 ? "" : "s");
                w.add(root.relativeFilePath(f), fitted(pic, 112), {}, facts.join(factSep()), fitted(pic, 256));
            }
            MenuPackWriter::Stats st;
            QString err;
            if (!w.save(QDir(outDir).filePath(kind + ".iw8mp"), &err, &st)) ++failures, std::printf("  %s\n", qPrintable(err));
            report(kind, st, clock.elapsed());
            continue;
        }
        std::printf("menu pack: unknown kind %s (sounds, effects, images, materials, models, prefabs or all)\n", qPrintable(kind));
        ++failures;
    }
    // The full build is done: the start-up tip stops asking.
    if (!failures && fill && kindsArg == "all")
        if (QFile done(QDir(outDir).filePath("complete.txt")); done.open(QIODevice::WriteOnly | QIODevice::Truncate))
            done.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8() + "\n");
    return failures ? 1 : 0;
}

}  // namespace iw8r::qt
