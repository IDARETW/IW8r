// IW8 Radiant Suite - Qt 6 + Qt Advanced Docking System shell.
//   IW8Radiant.exe [file.map | file.ents | <ACTS map_ents>.asset.json] [--keys iw8_canonical_keys.tsv]
//   IW8Radiant.exe --gui-test <iw8_sample_room.map> [--out <dir>]   scripted end-to-end UI test on Qt's offscreen
//                                                                    platform; exit code 0 = pass
//   IW8Radiant.exe --material-cache-test <material>[,<material>...] --out <dir>   headless stock texture cache run
//                                                                    (writes <dir>/material-cache-test.txt)
//   IW8Radiant.exe --thumb-pack materials|models [--limit N] [--batch N]   full browser thumbnail pack (thumb_pack.h)
//   IW8Radiant.exe --model-cache-test <model>[,<model>...] --out <dir>   headless stock LOD0 model cache run
//                                                                    (writes <dir>/model-cache-test.txt)
//   IW8Radiant.exe --world-cache-test <zone> --out <dir>             headless stock world geometry (gfx_map + trzone)
//                                                                    (writes <dir>/world-cache-test.txt)
//   IW8Radiant.exe --stock-sky-test <sky> --out <dir>               headless stock sky export (doc 33)
//                                                                    (writes <dir>/stock-sky-test.txt)
//   IW8Radiant.exe --smoke-test <file> [--out <dir>]                  opens the file, renders the D3D11 camera view,
//                                                                    counts drawn pixels; exit code 0 = geometry drawn
//   IW8Radiant.exe --stock-props-test <zone> --out <dir>             opens a stock map as File > Open IW8 Stock Map
//                                                                    does and waits until every prop model is fetched
//                                                                    (writes <dir>/stock-props-test.txt, screenshots)
//   IW8Radiant.exe --model-chain-test --out <dir>                    model fetch chain against a failing cache, no
//                                                                    game needed (writes <dir>/model-chain-test.txt)
//   IW8Radiant.exe --viewport-bench <map file> --out <dir> [--reference <stock world export folder>]
//                                                                    camera renderer timings and screenshots at
//                                                                    1920 x 1080 on Qt's offscreen platform (writes
//                                                                    <dir>/viewport-bench.txt; docs/research/85)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <chrono>
#include <cstdio>
#include <set>
#include <thread>
#include <windows.h>
#include <d3d11.h>
#include <d3d11_4.h>

#include <QApplication>
#include <QThread>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QScreen>
#include <QStyleFactory>
#include <QTimer>

#include "brush_tools.h"
#include "brush_ops.h"
#include "custom_shader.h"
#include "shader_cache.h"
#include "devmap_cover.h"
#include "devmap_launch.h"
#include "cache_manager.h"
#include "thumb_pack.h"
#include "camera_view.h"
#include "editor_log.h"
#include "main_window.h"
#include "map_text.h"
#include "material_cache.h"
#include "asset_audit.h"
#include "web_server.h"
#include "theme.h"
#include <QTextStream>
#include "model_cache.h"
#include "feature_shots.h"
#include "qt_gui_test.h"
#include "play_input_test.h"
#include "settings.h"
#include "world_cache.h"
#include "crash_handler.h"
#include "stall_watch.h"
#include "compile_panel.h"
#include "grid_view.h"
#include <QStandardPaths>
#include <QElapsedTimer>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif
#include <QDateTime>
#include "stock_sky.h"
#include "scene.h"
#include "stock_world.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace {

void applyDarkTheme(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    const QColor base(37, 39, 43), window(45, 47, 52), text(220, 222, 226), disabled(120, 120, 128), accent(64, 128, 220);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, QColor(42, 44, 49));
    p.setColor(QPalette::ToolTipBase, window);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, window);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    app.setPalette(p);
}

// ---------------------------------------------------------------------------------------------------------------
// --viewport-bench (docs/research/85_viewport_performance.md): times the camera renderer on one map, off-screen.
// Each scenario runs IW8R_BENCH_FRAMES frames (default 120). Per frame: `sync` is SceneCache::sync after a document
// change, `other` the rest of the change (the document command and the other panels' refresh), `cpu` the time inside
// CameraView::render(), `total` render() plus the wait until the GPU has finished the frame.
struct BenchSeries {
    std::vector<double> sync, other, cpu, total;
    size_t builds = 0;  // camera geometry rebuilds during the scenario
    size_t relayouts = 0, inPlace = 0, shadowPasses = 0, uploadBytes = 0;  // sums over the scenario's frames
    size_t nearShadowPasses = 0, lampShadowMaps = 0;
};

double benchMedian(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

double benchMax(const std::vector<double>& v) { return v.empty() ? 0 : *std::max_element(v.begin(), v.end()); }

// The process's memory now (commit = private bytes, the part that counts against the machine's commit limit), for the
// bench report's memory_at lines.
QString benchMemoryNow(const char* stage) {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return QString("memory_at %1 commit_mb=%2 working_set_mb=%3").arg(stage).arg(pmc.PagefileUsage / 1048576.0, 0, 'f', 0)
            .arg(pmc.WorkingSetSize / 1048576.0, 0, 'f', 0);
#endif
    return QString("memory_at %1 unknown").arg(stage);
}

// An allocation that fails in an event handler (a paint, a timer, a queued job's answer) is logged and the event
// dropped instead of ending the editor: Windows had about 14 GB committed by other programs while Verdansk loaded, and
// a texture decode on the UI thread ended the session (2026-11-13). The work is tried again on the next event.
class GuardedApplication : public QApplication {
public:
    using QApplication::QApplication;
    bool notify(QObject* receiver, QEvent* event) override {
        try {
            return QApplication::notify(receiver, event);
        } catch (const std::bad_alloc&) {
            static qint64 lastLog = 0;
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - lastLog > 10000) {
                lastLog = now;
                iw8r::qt::EditorLog::instance().add(iw8r::qt::LogLevel::Error, "memory",
                                                    "Out of memory: a step was skipped and will be tried again. Close other programs, or lower "
                                                    "the streamed world radius (stock/streamRadius) or the texture budget.");
            }
            return false;
        }
    }
};

int runViewportBench(iw8r::qt::MainWindow& w, const QString& mapFile, const QString& outDir, const QString& referenceDir) {
    // A memory sample every 2 s from its own thread (the UI thread can be busy for long), appended as it is taken so a
    // crash leaves the trace: seconds, commit MB, working set MB.
    std::thread([outDir] {
        QElapsedTimer clock;
        clock.start();
        const std::string path = QDir(outDir).filePath("memory.log").toStdString();
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
#ifdef _WIN32
            PROCESS_MEMORY_COUNTERS pmc{};
            if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) continue;
            if (FILE* f = std::fopen(path.c_str(), "a")) {
                std::fprintf(f, "%lld %llu %llu\n", (long long)(clock.elapsed() / 1000), (unsigned long long)(pmc.PagefileUsage >> 20),
                             (unsigned long long)(pmc.WorkingSetSize >> 20));
                std::fclose(f);
            }
#endif
        }
    }).detach();
    // Every log line (status messages say which load step runs) goes into memory.log too, stamped with the time it was
    // logged, so a memory spike can be matched to the step that caused it: "<seconds> S <text>".
    {
        const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
        const std::string path = QDir(outDir).filePath("memory.log").toStdString();
        auto* gpuTick = new QTimer(&w);
        gpuTick->setInterval(10000);
        QObject::connect(gpuTick, &QTimer::timeout, &w, [&w] {
            if (auto* cam = w.findChild<iw8r::qt::CameraView*>())
                iw8r::qt::EditorLog::instance().add(iw8r::qt::LogLevel::Info, "bench", "gpu memory MB: " + QString::fromStdString(cam->gpuMemoryReport()) +
                                                                                     "; scene vertices: " + QString::fromStdString(cam->sceneStreamReport()));
            // IW8R_BENCH_ITEMS: the scene items' triangles by label (classname or first material), the largest first.
            if (qEnvironmentVariableIsSet("IW8R_BENCH_ITEMS")) {
                std::map<std::string, std::pair<size_t, size_t>> byLabel;  // items, triangles
                for (const auto* g : w.context().scene.items()) {
                    size_t tris = 0;
                    for (const auto& pw : g->polys) tris += pw.size() >= 3 ? pw.size() - 2 : 0;
                    auto& e = byLabel[g->label + (g->mesh ? " (mesh)" : "")];
                    ++e.first, e.second += tris;
                }
                std::vector<std::pair<size_t, std::string>> top;
                for (const auto& [l, e] : byLabel) top.push_back({e.second, l + " items " + std::to_string(e.first)});
                std::sort(top.rbegin(), top.rend());
                QString line = "scene items:";
                for (size_t i = 0; i < top.size() && i < 12; ++i) line += QString(" | %1 tris %2").arg(QString::fromStdString(top[i].second)).arg(top[i].first);
                iw8r::qt::EditorLog::instance().add(iw8r::qt::LogLevel::Info, "bench", line);
            }
        });
        gpuTick->start();
        QObject::connect(&iw8r::qt::EditorLog::instance(), &iw8r::qt::EditorLog::added, &w, [t0, path](const iw8r::qt::LogEntry& e) {
            if (FILE* f = std::fopen(path.c_str(), "a")) {
                std::fprintf(f, "%.1f S %s\n", (e.time.toMSecsSinceEpoch() - t0) / 1000.0, e.text.left(200).toUtf8().constData());
                std::fclose(f);
            }
        });
    }
    using iw8r::Vec3;
    QDir().mkpath(outDir);
    QStringList report;
    QElapsedTimer t;
    t.start();
    // "stock:<zone .ff path>" opens a stock zone as the editor's File > Open Stock Zone does (with its reference world,
    // props, textures, lightmap and probes, all from the real caches), then waits up to two minutes for them.
    const bool stockZone = mapFile.startsWith("stock:");
    if (stockZone) {
        QString err;
        if (!w.openStockZone(mapFile.mid(6), &err)) {
            QFile f(QDir(outDir).filePath("viewport-bench.txt"));
            if (f.open(QIODevice::WriteOnly)) f.write(("stock zone not opened: " + err).toUtf8());
            return 3;
        }
    } else {
        w.openPath(mapFile);
    }
    QApplication::processEvents();
    auto& ctx = w.context();
    iw8r::qt::CameraView* cam = w.cameraView();
    if (!cam || (!stockZone && ctx.doc.path().empty())) {  // a stock zone's document has no file path
        QFile f(QDir(outDir).filePath("viewport-bench.txt"));
        if (f.open(QIODevice::WriteOnly)) f.write("no document opened");
        return 3;
    }
    if (stockZone) {
        QElapsedTimer wt;
        wt.start();
        while (wt.elapsed() < 120000 && cam->referenceTriangles() == 0) {
            cam->renderNow();
            QApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        report << benchMemoryNow("reference_drawn");
        while (wt.elapsed() < 120000) {  // props, textures, lightmap and probes keep arriving: give them time
            cam->renderNow();
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            if (wt.elapsed() > 30000 && !cam->texturesPending()) break;
        }
        // The world cache's lightmap / probes / light grid job (a decompression; minutes on a zone cached before the
        // light grid existed), then the prop rebuild it starts.
        while (wt.elapsed() < 600000 && !w.stockCacheIdle()) {
            cam->renderNow();
            QApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        // Then what that work set going: textures, the reference's unresolved images and layers, the terrain patch.
        {
            QElapsedTimer settle;
            settle.start();
            while (settle.elapsed() < 3000 || cam->texturesPending() || cam->referencePending() || w.terrainLayersBusy() || !w.stockCacheIdle()) {
                cam->renderNow();
                QApplication::processEvents(QEventLoop::AllEvents, 50);
                if (wt.elapsed() > 660000 || settle.elapsed() > 180000) break;
            }
        }
        report << benchMemoryNow("caches_idle");
        report << "scene_streams " + QString::fromStdString(cam->sceneStreamReport());
        report << QString("reference_pending %1 %2 terrain_busy %3 caches_idle %4: %5")
                      .arg(cam->referencePending() ? 1 : 0).arg(cam->texturesPending() ? 1 : 0).arg(w.terrainLayersBusy() ? 1 : 0)
                      .arg(w.stockCacheIdle() ? 1 : 0).arg(QString::fromStdString(cam->referencePendingReport()));
        const auto pl = w.propLightStats();
        report << QString("prop_light %1 of %2 textured placements lit from their light grid box (per vertex from the atlas: %3, zones %4)")
                      .arg(pl[0]).arg(pl[1]).arg(cam->hasStockLgvAtlas() ? "yes" : "no").arg(w.propLgvZones());
        report << QString("stock zone %1 opened, reference %2 triangles after %3 ms, stock lightmap %4, probes %5")
                      .arg(mapFile.mid(6)).arg(cam->referenceTriangles()).arg(wt.elapsed()).arg(cam->hasStockLightmap() ? "yes" : "no")
                      .arg(cam->stockProbeCount());
    }
    // IW8R_BENCH_STOCK_EDIT="key=value|...|remove=x0 y0 z0 x1 y1 z1": stock map edits (worldspawn keys, a world
    // removal box) put in the document before the frames; the viewport shows them without a rebuild.
    if (const QString se = qEnvironmentVariable("IW8R_BENCH_STOCK_EDIT"); stockZone && !se.isEmpty()) {
        iw8r::Id worldId = iw8r::kNoId;
        for (const auto& e : ctx.doc.map().entities)
            if (e.isWorldspawn()) worldId = e.id;
        bool sky = false;
        for (const QString& part : se.split('|', Qt::SkipEmptyParts)) {
            const QString key = part.section('=', 0, 0).trimmed(), value = part.section('=', 1).trimmed();
            if (key == "remove") {
                const QStringList n = value.split(' ', Qt::SkipEmptyParts);
                if (n.size() != 6) continue;
                iw8r::AABB b;
                b.add(iw8r::Vec3{n[0].toDouble(), n[1].toDouble(), n[2].toDouble()});
                b.add(iw8r::Vec3{n[3].toDouble(), n[4].toDouble(), n[5].toDouble()});
                if (const auto prim = iw8r::makeBrushPrimitive(iw8r::makeBox(b, " caulk 128 128 0 0 0 0"), "\n"))
                    ctx.doc.addEntity({{"classname", "iw8r_remove_world"}}, {*prim}, "Remove world");
            } else if (worldId != iw8r::kNoId) {
                ctx.doc.setKeyValue(worldId, key.toStdString(), value.toStdString());
                sky = sky || key == "iw8r_sky_from";
            }
        }
        ctx.documentEdited();
        QElapsedTimer et;
        et.start();
        while (et.elapsed() < 90000) {
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            cam->renderNow();
            if (et.elapsed() > (sky ? 30000 : 3000) && !cam->texturesPending()) break;
        }
        report << "stock_edit " + se + QString(" (shown after %1 ms)").arg(et.elapsed());
    }
    const double openMs = t.nsecsElapsed() / 1e6;
    cam->setFixedRenderSize(1920, 1080);
    cam->setWindTime(0);
    cam->setEffectTime(0);
    // IW8R_BENCH_FX=<seconds>: View > Effect Preview on, the placed effects shown at that time of their run.
    if (const QString fxT = qEnvironmentVariable("IW8R_BENCH_FX"); !fxT.isEmpty()) {
        ctx.effectPreview = true;
        cam->setEffectTime(fxT.toDouble());
    }
    const int frames = std::max(8, qEnvironmentVariableIsSet("IW8R_BENCH_FRAMES") ? qEnvironmentVariableIntValue("IW8R_BENCH_FRAMES") : 120);
    iw8r::app::Prof::on = qEnvironmentVariableIntValue("IW8R_BENCH_PROFILE") > 0;  // section timers in the report
    const QString only = qEnvironmentVariable("IW8R_BENCH_ONLY");  // "" = every scenario, else a comma list of names

    ctx.scene.sync(ctx.doc);
    iw8r::AABB all;
    for (const auto* g : ctx.scene.items())
        if (g->bounds.valid()) all.add(g->bounds);
    if (!all.valid()) all.add(Vec3{-256, -256, 0}), all.add(Vec3{256, 256, 256});
    const Vec3 centre = all.center();
    const double R = std::max(256.0, iw8r::length(all.maxs - all.mins) * 0.5);

    if (!referenceDir.isEmpty()) {
        // Stock reference path: the cached world export is only read (core readStockWorld), never the cache object.
        iw8r::StockWorld world;
        std::string err;
        if (iw8r::readStockWorld(referenceDir.toStdString(), world, &err)) {
            std::vector<iw8r::app::GpuVertex> tris;
            const size_t n = iw8r::app::buildReferenceTris(world, tris);
            std::vector<iw8r::app::ReferenceTexBatch> tex;
            const size_t nt = iw8r::app::buildReferenceTexBatches(world, tex);
            report << QString("reference %1 flat_triangles=%2 textured_triangles=%3 textured_batches=%4").arg(QFileInfo(referenceDir).fileName()).arg(n).arg(nt).arg(tex.size());
            cam->setReference(std::move(tris));
            if (qEnvironmentVariableIntValue("IW8R_BENCH_REFERENCE_FLAT") <= 0) cam->setReferenceTextured(std::move(tex));
            // Game Lighting: the zone's own lightmap when the world cache has made it (lightmap.iw8rlm), and the map's
            // sun and exposure (core game_look).
            const QString zone = QFileInfo(referenceDir).fileName();
            cam->setGameStockMap(zone.toStdString());
            // MP maps: lightmap.iw8rlm; Warzone maps: lightmaps/<index>.iw8rlm (the streamed tiles' lightmaps too).
            std::vector<iw8r::StockLightmap> lms;
            std::string lerr;
            QStringList lmFiles;
            if (QFileInfo::exists(QDir(referenceDir).filePath("lightmap.iw8rlm"))) lmFiles << QDir(referenceDir).filePath("lightmap.iw8rlm");
            for (const QString& f : QDir(QDir(referenceDir).filePath("lightmaps")).entryList({"*.iw8rlm"}, QDir::Files))
                lmFiles << QDir(QDir(referenceDir).filePath("lightmaps")).filePath(f);
            for (const QString& f : lmFiles) {
                iw8r::StockLightmap slm;
                if (iw8r::readStockLightmapCache(f.toStdString(), slm, &lerr)) lms.push_back(std::move(slm));
            }
            if (!lms.empty()) {
                const auto layers = iw8r::app::stockLightmapAtlas(lms);
                cam->setStockLightmap(layers);
                size_t withUv = 0, all = 0;
                for (const auto& v : world.lightmapUv)
                    for (const auto& uv : v) withUv += uv[0] >= 0 ? 1 : 0, ++all;
                report << QString("stock_lightmap %1 lightmap(s), atlas %2x%3, vertices_with_uv=%4/%5").arg(lms.size()).arg(layers->width)
                              .arg(layers->height).arg(withUv).arg(all);
            } else {
                report << QString("stock_lightmap none (%1)").arg(lmFiles.isEmpty() ? QString("not cached") : QString::fromStdString(lerr));
            }
            const QString prFile = QDir(referenceDir).filePath("probes.iw8rprb");
            auto probes = std::make_shared<iw8r::StockProbeSet>();
            std::string perr;
            if (QFileInfo::exists(prFile) && iw8r::readStockProbeCache(prFile.toStdString(), *probes, &perr, false)) {
                report << QString("stock_probes %1 probes, %2 boxes").arg(probes->probes.size()).arg(probes->boxes.size());
                cam->setStockProbes(probes);
            } else {
                report << QString("stock_probes none (%1)").arg(QFileInfo::exists(prFile) ? QString::fromStdString(perr) : "not cached");
            }
        } else {
            report << QString("reference FAILED: %1").arg(QString::fromStdString(err));
        }
    }

    const auto setCam = [&](const Vec3& pos, double yaw, double pitch) {
        ctx.cam.pos = pos;
        ctx.cam.yaw = yaw;
        ctx.cam.pitch = pitch;
        ctx.cam.fov = 90;
    };
    // IW8R_BENCH_VOLUMETRIC: the volumetric fog on for this bench (View > Game Lighting Options has it off).
    if (qEnvironmentVariableIsSet("IW8R_BENCH_VOLUMETRIC") && cam) cam->setVolumetricFogEnabled(true);
    // Fixed cameras: `a` sees the whole map from a corner, `b` stands inside it near the middle.
    // IW8R_BENCH_CAM="x y z yaw pitch [fov]" puts camera a where a comparison needs it (an in-game screenshot's spot).
    double benchCam[6] = {0, 0, 0, 0, 0, 90};
    QStringList camParts = qEnvironmentVariable("IW8R_BENCH_CAM").split(' ', Qt::SkipEmptyParts);
    bool fixedCam = camParts.size() >= 5;
    for (int k = 0; k < camParts.size() && k < 6; ++k) benchCam[k] = camParts[k].toDouble();
    if (qEnvironmentVariableIsSet("IW8R_BENCH_CLASSES")) {  // diagnosis: the most common entity classnames
        std::map<std::string, int> cls;
        for (const auto& e : ctx.doc.map().entities) ++cls[e.classname()];
        std::vector<std::pair<int, std::string>> v;
        for (const auto& [c, n] : cls) v.push_back({n, c});
        std::sort(v.rbegin(), v.rend());
        QStringList l;
        for (size_t i = 0; i < v.size() && i < 15; ++i) l << QString("%1=%2").arg(QString::fromStdString(v[i].second)).arg(v[i].first);
        report << "classes " + l.join(' ');
    }
    // IW8R_BENCH_CAM="spawn N [fov]" / "ent <classname part> N [yaw] [fov]": stand at the Nth entity whose classname
    // contains "spawn" (or the given part; stock maps without spawns in their entity string have node_pathnode on
    // their floors), eye height, facing its yaw (or the given one).
    if ((camParts.size() >= 2 && camParts[0] == "spawn") || (camParts.size() >= 3 && camParts[0] == "ent")) {
        const bool byClass = camParts[0] == "ent";
        const std::string part = byClass ? camParts[1].toStdString() : std::string("spawn");
        if (byClass) camParts.removeAt(1);
        int n = camParts[1].toInt(), seen = 0;
        for (const auto& e : ctx.doc.map().entities) {
            if (e.classname().find(part) == std::string::npos || !e.origin()) continue;
            if (seen++ < n) continue;
            const Vec3 o = *e.origin();
            double yaw = 0;
            if (const std::string* an = e.value("angles")) {
                const QStringList a = QString::fromStdString(*an).split(' ', Qt::SkipEmptyParts);
                if (a.size() >= 2) yaw = a[1].toDouble();
            }
            if (byClass && camParts.size() >= 3) yaw = camParts[2].toDouble();
            benchCam[0] = o.x, benchCam[1] = o.y, benchCam[2] = o.z + 60, benchCam[3] = yaw, benchCam[4] = -5;
            benchCam[5] = camParts.size() >= (byClass ? 4 : 3) ? camParts[byClass ? 3 : 2].toDouble() : 100;
            fixedCam = true;
            report << QString("bench_cam spawn %1 (%2) at %3 %4 %5 yaw %6").arg(n).arg(QString::fromStdString(e.classname()))
                          .arg(o.x).arg(o.y).arg(o.z).arg(yaw);
            break;
        }
    }
    // IW8R_BENCH_CAM="prop <model name part> N [distance] [yaw] [fov]": look at the Nth stock prop placement whose model
    // name holds the part, from `distance` (default 160) units away along `yaw` (default 0), 50 units above its origin.
    if (camParts.size() >= 3 && camParts[0] == "prop") {
        const std::string part = camParts[1].toStdString();
        const int n = camParts[2].toInt();
        const double dist = camParts.size() >= 4 ? camParts[3].toDouble() : 160.0, yaw = camParts.size() >= 5 ? camParts[4].toDouble() : 0.0;
        int seen = 0;
        if (const auto props = w.referencePropPlacements())
            for (const auto& in : *props) {
                if (in.model.find(part) == std::string::npos || seen++ < n) continue;
                const double r = yaw * 3.14159265358979 / 180.0;
                benchCam[0] = in.origin.x - std::cos(r) * dist, benchCam[1] = in.origin.y - std::sin(r) * dist;
                benchCam[2] = in.origin.z + 50, benchCam[3] = yaw, benchCam[4] = -8;
                benchCam[5] = camParts.size() >= 6 ? camParts[5].toDouble() : 70;
                fixedCam = true;
                report << QString("bench_cam prop %1 %2 at %3 %4 %5").arg(QString::fromStdString(in.model)).arg(n).arg(in.origin.x)
                              .arg(in.origin.y).arg(in.origin.z);
                break;
            }
    }
    // IW8R_BENCH_CAM="fx <effect name part> N [distance] [yaw] [height] [fov]": look at the Nth placed stock effect whose
    // name holds the part, from `distance` (default 400) units back along `yaw` (default 0), `height` (default 100) up.
    if (camParts.size() >= 3 && camParts[0] == "fx") {
        const std::string part = camParts[1].toStdString();
        const int n = camParts[2].toInt();
        const double dist = camParts.size() >= 4 ? camParts[3].toDouble() : 400.0, yaw = camParts.size() >= 5 ? camParts[4].toDouble() : 0.0;
        const double up = camParts.size() >= 6 ? camParts[5].toDouble() : 100.0;
        int seen = 0;
        if (iw8r::qt::CameraView* cv = w.cameraView())
            for (const auto& fx : cv->stockEffects()) {
                if (fx.name.find(part) == std::string::npos || seen++ < n) continue;
                const double r = yaw * 3.14159265358979 / 180.0;
                benchCam[0] = fx.origin.x - std::cos(r) * dist, benchCam[1] = fx.origin.y - std::sin(r) * dist;
                benchCam[2] = fx.origin.z + up, benchCam[3] = yaw, benchCam[4] = -std::atan2(up, dist) * 180.0 / 3.14159265358979;
                benchCam[5] = camParts.size() >= 7 ? camParts[6].toDouble() : 70;
                fixedCam = true;
                report << QString("bench_cam fx %1 %2 at %3 %4 %5").arg(QString::fromStdString(fx.name)).arg(n).arg(fx.origin.x)
                              .arg(fx.origin.y).arg(fx.origin.z);
                break;
            }
    }
    // IW8R_BENCH_CAM="sun x y z [yaw offset] [pitch offset] [fov]": from (x, y, z) toward the sun, turned by the offsets
    // (degrees) so the sun sits off the centre and a flare's pieces spread along the line through it (research 139).
    bool sunCam = false;
    if (camParts.size() >= 4 && camParts[0] == "sun") {
        sunCam = true;
        if (iw8r::qt::CameraView* cv = w.cameraView()) {
            const iw8r::Vec3 t = cv->gameSunTowardNow();
            benchCam[0] = camParts[1].toDouble(), benchCam[1] = camParts[2].toDouble(), benchCam[2] = camParts[3].toDouble();
            benchCam[3] = std::atan2(t.y, t.x) * 180.0 / 3.14159265358979 + (camParts.size() >= 5 ? camParts[4].toDouble() : 0.0);
            benchCam[4] = std::asin(std::clamp(t.z, -1.0, 1.0)) * 180.0 / 3.14159265358979 + (camParts.size() >= 6 ? camParts[5].toDouble() : 0.0);
            benchCam[5] = camParts.size() >= 7 ? camParts[6].toDouble() : 70;
            fixedCam = true;
            report << QString("bench_cam sun toward %1 %2 %3 yaw %4 pitch %5").arg(t.x).arg(t.y).arg(t.z).arg(benchCam[3]).arg(benchCam[4]);
        }
    }
    // IW8R_BENCH_CAM="mat <material part> N [distance] [yaw] [fov]": look down at the Nth stock world batch whose material
    // holds the part, from `distance` (default 400) units back along `yaw` (default 0) and half that above its centre.
    if (camParts.size() >= 3 && camParts[0] == "mat") {
        std::string material;
        const iw8r::AABB bb = cam->referenceMaterialBounds(camParts[1].toStdString(), camParts[2].toInt(), &material);
        if (bb.valid()) {
            const double dist = camParts.size() >= 4 ? camParts[3].toDouble() : 400.0, yaw = camParts.size() >= 5 ? camParts[4].toDouble() : 0.0;
            const iw8r::Vec3 c = bb.center();
            const double r = yaw * 3.14159265358979 / 180.0;
            benchCam[0] = c.x - std::cos(r) * dist, benchCam[1] = c.y - std::sin(r) * dist, benchCam[2] = c.z + dist * 0.5;
            benchCam[3] = yaw, benchCam[4] = -26.5;  // looking at the centre
            benchCam[5] = camParts.size() >= 6 ? camParts[5].toDouble() : 70;
            fixedCam = true;
            report << QString("bench_cam mat %1 at %2 %3 %4 size %5 %6 %7").arg(QString::fromStdString(material)).arg(c.x).arg(c.y).arg(c.z)
                          .arg(bb.maxs.x - bb.mins.x).arg(bb.maxs.y - bb.mins.y).arg(bb.maxs.z - bb.mins.z);
        } else {
            report << "bench_cam mat: no batch with " + camParts[1];
        }
    }
    // IW8R_BENCH_NEAR=<radius>: the scene items near the bench camera (kind, label, bounds), to name what a shot shows.
    if (fixedCam && qEnvironmentVariableIsSet("IW8R_BENCH_NEAR")) {
        const double r = qEnvironmentVariable("IW8R_BENCH_NEAR").toDouble();
        const Vec3 c{benchCam[0], benchCam[1], benchCam[2]};
        int listed = 0;
        for (const auto* g : ctx.scene.items()) {
            if (!g->bounds.valid() || listed >= 60) continue;
            const Vec3 m = g->bounds.center();
            if (std::hypot(m.x - c.x, m.y - c.y) > r) continue;
            ++listed;
            report << QString("near kind=%1 label=%2 volume=%3 brushEntity=%4 mesh=%5 color=%6 min=%7,%8,%9 max=%10,%11,%12")
                          .arg(int(g->kind)).arg(QString::fromStdString(g->label)).arg(g->volume).arg(g->brushEntity).arg(g->mesh)
                          .arg(g->color, 8, 16, QChar('0')).arg(g->bounds.mins.x, 0, 'f', 0).arg(g->bounds.mins.y, 0, 'f', 0)
                          .arg(g->bounds.mins.z, 0, 'f', 0).arg(g->bounds.maxs.x, 0, 'f', 0).arg(g->bounds.maxs.y, 0, 'f', 0)
                          .arg(g->bounds.maxs.z, 0, 'f', 0);
        }
    }
    {
        // Where the point entities cluster (spawns, markers): the play area of a stock map.
        std::vector<double> xs, ys, zs;
        for (const auto* g : ctx.scene.items())
            if (g->kind == iw8r::app::PrimGeom::PointEntity && g->bounds.valid()) {
                const Vec3 c = g->bounds.center();
                xs.push_back(c.x), ys.push_back(c.y), zs.push_back(c.z);
            }
        if (!xs.empty()) {
            const auto med = [](std::vector<double>& v) { std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end()); return v[v.size() / 2]; };
            report << QString("entity_median %1 %2 %3 (%4 point entities)").arg(med(xs), 0, 'f', 0).arg(med(ys), 0, 'f', 0).arg(med(zs), 0, 'f', 0).arg(xs.size());
        }
    }
    const auto camA = [&] {
        if (sunCam) {
            // Aimed again each time: the stock map's own sun is known only once its lighting has loaded.
            if (iw8r::qt::CameraView* cv = w.cameraView()) {
                const iw8r::Vec3 t = cv->gameSunTowardNow();
                benchCam[3] = std::atan2(t.y, t.x) * 180.0 / 3.14159265358979 + (camParts.size() >= 5 ? camParts[4].toDouble() : 0.0);
                benchCam[4] = std::asin(std::clamp(t.z, -1.0, 1.0)) * 180.0 / 3.14159265358979 + (camParts.size() >= 6 ? camParts[5].toDouble() : 0.0);
            }
        }
        if (fixedCam) {
            setCam(Vec3{benchCam[0], benchCam[1], benchCam[2]}, benchCam[3], benchCam[4]);
            ctx.cam.fov = benchCam[5];
            return;
        }
        setCam(centre + Vec3{-0.45 * R, -0.45 * R, 0.35 * R}, 45, -28.8);
    };
    const auto camB = [&] { setCam(Vec3{centre.x + 40, centre.y - 150, all.mins.z + 88}, 30, -5); };
    // Streamed world tiles (Warzone) follow the camera: stand at the bench camera and let them come in (at most ten
    // minutes; settled for three seconds) before anything is measured.
    if (fixedCam && w.streamedTilesKnown() > 0) {
        camA();
        QElapsedTimer st, calm;
        st.start();
        calm.start();
        while (st.elapsed() < 600000) {
            cam->renderNow();
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            // The tiles, then what they set going: their light grids and lightmaps, the terrain patch at the bench
            // camera, the images and layers of what they added.
            if (!w.streamedTilesSettled() || cam->texturesPending() || cam->referencePending() || w.terrainLayersBusy() || !w.stockCacheIdle())
                calm.restart();
            else if (calm.elapsed() > 3000) break;
        }
        report << QString("streamed_tiles drawn %1 of %2 after %3 ms").arg(w.streamedTilesDrawn()).arg(w.streamedTilesKnown()).arg(st.elapsed());
        report << benchMemoryNow("tiles_settled");
        report << QString("tiles_pending %1 %2 terrain_busy %3 caches_idle %4 tiles_settled %5: %6")
                      .arg(cam->referencePending() ? 1 : 0).arg(cam->texturesPending() ? 1 : 0).arg(w.terrainLayersBusy() ? 1 : 0)
                      .arg(w.stockCacheIdle() ? 1 : 0).arg(w.streamedTilesSettled() ? 1 : 0).arg(QString::fromStdString(cam->referencePendingReport()));
        report << "streamed_tiles " + w.streamedTilesReport();
    }
    const auto trigger = [&](const char* name) {
        if (QAction* a = w.action(name)) a->trigger();
    };
    const auto setLit = [&](bool on) {
        if (ctx.scene.litPreview != on) trigger("ToggleLitPreview");
    };
    const auto frame = [&](BenchSeries* s) {
        QElapsedTimer ft;
        ft.start();
        cam->renderNow();
        const double cpu = ft.nsecsElapsed() / 1e6;
        cam->finishGpu();
        const double total = ft.nsecsElapsed() / 1e6;
        if (s) {
            s->cpu.push_back(cpu), s->total.push_back(total);
            s->shadowPasses += cam->frameStats().shadowPasses;
            s->nearShadowPasses += cam->frameStats().nearShadowPasses;
            s->lampShadowMaps += cam->frameStats().lampShadowMaps;
            s->uploadBytes += cam->frameStats().uploadBytes;
        }
    };
    // After a mode change: frames until the streamed textures are in (a few loads per frame), at most 30 s.
    const auto settle = [&] {
        QElapsedTimer st;
        st.start();
        int calm = 0;
        // Textures, the reference's unresolved images and layers, and a Warzone map's terrain patch for this camera
        // (rebuilt on its timer after a camera move).
        const auto busy = [&] { return cam->texturesPending() || cam->referencePending() || !w.terrainLayersCurrent(); };
        while (calm < 4 && st.elapsed() < (stockZone ? 120000 : 30000)) {
            frame(nullptr);
            QApplication::processEvents(QEventLoop::AllEvents, 5);
            calm = busy() ? 0 : calm + 1;
        }
    };
    const auto edited = [&](BenchSeries& s, QElapsedTimer& op) {
        // The viewport's share (SceneCache::sync) apart from the document command and the other panels.
        const double before = op.nsecsElapsed() / 1e6;
        QElapsedTimer st;
        st.start();
        ctx.scene.sync(ctx.doc);
        const double sync = st.nsecsElapsed() / 1e6;
        st.restart();
        ctx.documentEdited();
        s.sync.push_back(sync);
        s.other.push_back(before + st.nsecsElapsed() / 1e6);
    };
    const auto line = [&](const QString& name, const BenchSeries& s) {
        report << QString("%1 frames=%2 cpu_med=%3 cpu_max=%4 total_med=%5 total_max=%6 sync_med=%7 sync_max=%8 other_med=%9 other_max=%10 geometry_builds=%11 "
                          "relayouts=%12 items_in_place=%13 shadow_passes=%14 upload_kb_per_frame=%15")
                      .arg(name, -18).arg(s.cpu.size())
                      .arg(benchMedian(s.cpu), 0, 'f', 3).arg(benchMax(s.cpu), 0, 'f', 3)
                      .arg(benchMedian(s.total), 0, 'f', 3).arg(benchMax(s.total), 0, 'f', 3)
                      .arg(benchMedian(s.sync), 0, 'f', 3).arg(benchMax(s.sync), 0, 'f', 3)
                      .arg(benchMedian(s.other), 0, 'f', 3).arg(benchMax(s.other), 0, 'f', 3).arg(s.builds)
                      .arg(s.relayouts).arg(s.inPlace).arg(s.shadowPasses).arg(double(s.uploadBytes) / 1024.0 / double(std::max<size_t>(1, s.cpu.size())), 0, 'f', 1);
        if (s.nearShadowPasses || s.lampShadowMaps)
            report << QString("  shadows_%1 near_cascade=%2 lamp_maps=%3 (over %4 frames)").arg(name.trimmed()).arg(s.nearShadowPasses)
                          .arg(s.lampShadowMaps).arg(s.cpu.size());
    };
    const auto wanted = [&](const char* name) { return only.isEmpty() || only.split(',').contains(name); };
    const auto counts = [&](const QString& name) {
        const auto& st = cam->frameStats();
        report << QString("%1 triangles=%2 lines=%3 batches=%4 draw_calls=%5 shadow_draw_calls=%6 upload_bytes=%7 textures=%8")
                      .arg(name, -18).arg(st.triangles).arg(st.lines).arg(st.batches).arg(st.drawCalls).arg(st.shadowDrawCalls).arg(st.uploadBytes)
                      .arg(cam->loadedTextures());
    };
    const auto shot = [&](const QString& name) {
        frame(nullptr);
        cam->grabFrame().save(QDir(outDir).filePath(name + ".png"));
    };
    const auto run = [&](const char* name, const std::function<void(int, BenchSeries&)>& before) {
        if (!wanted(name)) return;
        BenchSeries s;
        frame(nullptr);  // warm-up: whatever the scenario's first state needs is built here, not in the numbers
        iw8r::app::Prof::dump();
        const size_t b0 = cam->geometryBuilds(), r0 = cam->geometryRelayouts(), p0 = cam->itemsUpdatedInPlace();
        for (int i = 0; i < frames; ++i) {
            if (before) before(i, s);
            frame(&s);
        }
        s.builds = cam->geometryBuilds() - b0;
        s.relayouts = cam->geometryRelayouts() - r0;
        s.inPlace = cam->itemsUpdatedInPlace() - p0;
        line(name, s);
        // The kept geometry must equal a fresh build, and a frame from it must equal a frame after a forced rebuild.
        {
            const QString diff = cam->geometrySelfCheck();
            const QImage kept = cam->grabFrame();
            cam->invalidateGeometry();
            frame(nullptr);
            const QImage fresh = cam->grabFrame();
            // How far apart the two frames are: Game Lighting's auto exposure keeps adapting between frames, so a
            // small drift (max channel step, share of pixels off by more than 8) is expected there.
            QString pic = "same";
            if (kept != fresh) {
                int maxD = 0;
                qint64 off = 0, total = 0;
                if (kept.size() == fresh.size()) {
                    const QImage ka = kept.convertToFormat(QImage::Format_RGB32), fa = fresh.convertToFormat(QImage::Format_RGB32);
                    for (int y = 0; y < ka.height(); ++y) {
                        const QRgb* kr = reinterpret_cast<const QRgb*>(ka.constScanLine(y));
                        const QRgb* fr = reinterpret_cast<const QRgb*>(fa.constScanLine(y));
                        for (int x = 0; x < ka.width(); ++x) {
                            const int d = std::max({std::abs(qRed(kr[x]) - qRed(fr[x])), std::abs(qGreen(kr[x]) - qGreen(fr[x])),
                                                    std::abs(qBlue(kr[x]) - qBlue(fr[x]))});
                            maxD = std::max(maxD, d);
                            off += d > 8;
                            ++total;
                        }
                    }
                }
                pic = QString("DIFFERENT max=%1 over8=%2%").arg(maxD).arg(total ? 100.0 * double(off) / double(total) : 100.0, 0, 'f', 3);
            }
            report << QString("  check_%1 geometry=%2 picture=%3").arg(name, diff.isEmpty() ? QString("same") : "DIFFERENT: " + diff, pic);
        }
        if (iw8r::app::Prof::on) report << QString("  profile_%1 (sum over %2 frames):%3").arg(name).arg(frames).arg(QString::fromStdString(iw8r::app::Prof::dump()));
    };

    // First frame, then the textured view with everything loaded.
    ctx.doc.deselectAll();
    trigger("CameraModeTextured");
    setLit(false);
    camA();
    t.restart();
    frame(nullptr);
    const double firstMs = t.nsecsElapsed() / 1e6;
    settle();
    report << QString("map %1 items=%2 entities=%3 open_ms=%4 first_frame_ms=%5 radius=%6")
                  .arg(QFileInfo(mapFile).fileName()).arg(ctx.scene.items().size()).arg(ctx.doc.map().entities.size())
                  .arg(openMs, 0, 'f', 0).arg(firstMs, 0, 'f', 0).arg(R, 0, 'f', 0);

    // Screenshots: one per mode from both cameras, then a selection and a face selection.
    struct Mode {
        const char* name;
        const char* action;
        bool lit;
    };
    const Mode modes[] = {{"textured", "CameraModeTextured", false}, {"lit", "CameraModeTextured", true},
                          {"wireframe", "CameraModeWireframe", false}, {"flat", "CameraModeFlat", false}};
    for (const Mode& m : modes) {
        trigger(m.action);
        setLit(m.lit);
        camA();
        settle();
        shot(QString("%1_a").arg(m.name));
        counts(QString("counts_%1_a").arg(m.name));
        camB();
        settle();
        shot(QString("%1_b").arg(m.name));
        counts(QString("counts_%1_b").arg(m.name));
    }
    trigger("CameraModeTextured");
    setLit(false);
    // The items the scenarios select and move: brushes spread over the document, and a few models.
    std::vector<iw8r::SelRef> brushes, others;
    for (const auto* g : ctx.scene.items()) (g->kind == iw8r::app::PrimGeom::Brush && !g->brushEntity && !g->volume ? brushes : others).push_back(g->ref);
    const auto pick = [&](int i) -> iw8r::SelRef {
        if (brushes.empty()) return others.empty() ? iw8r::SelRef{} : others[size_t(i) % others.size()];
        if (i % 4 == 3 && !others.empty()) return others[(size_t(i) * 7919) % others.size()];
        return brushes[(size_t(i) * 7919) % brushes.size()];
    };
    if (!brushes.empty() || !others.empty()) {
        camB();
        ctx.doc.select(pick(0), false);
        ctx.doc.select(pick(1), true);
        ctx.doc.select(pick(3), true);
        ctx.documentEdited();
        settle();
        shot("selected_b");
        camA();
        shot("selected_a");
        if (!brushes.empty()) {
            ctx.doc.selectFace(iw8r::FaceRef{brushes[0].entity, brushes[0].item, 1}, false);
            ctx.documentEdited();
            shot("face_a");
        }
        ctx.doc.deselectAll();
        ctx.documentEdited();
    }

    // Scenarios, in the textured view unless named otherwise.
    camA();
    settle();
    run("still", nullptr);
    counts("counts_still");
    camB();
    run("still_inside", nullptr);
    run("moving", [&](int i, BenchSeries&) {
        // First half: a circle around the map looking at its middle. Second half: a flight through it at head height.
        const int half = frames / 2;
        if (i < half) {
            const double a = 2 * 3.14159265358979 * i / half;
            const Vec3 pos = centre + Vec3{std::cos(a) * 0.4 * R, std::sin(a) * 0.4 * R, 0.2 * R};
            setCam(pos, a * 180 / 3.14159265358979 + 180, -26.6);
        } else {
            const double f = double(i - half) / std::max(1, frames - half - 1);
            setCam(Vec3{centre.x + (f - 0.5) * 0.8 * R, centre.y + 24, all.mins.z + 88}, 0, 0);
        }
    });
    // The editor's own camera flight: every step notifies the window (grid views, docks) and the event loop paints them,
    // as when flying with W A S D. Measured as wall time per step (frame() also renders the camera).
    if (wanted("moving_window")) {
        BenchSeries s;
        const int half = frames / 2;
        for (int i = 0; i < frames; ++i) {
            QElapsedTimer st;
            st.start();
            if (i < half) {
                const double a = 2 * 3.14159265358979 * i / half;
                setCam(centre + Vec3{std::cos(a) * 0.4 * R, std::sin(a) * 0.4 * R, 0.2 * R}, a * 180 / 3.14159265358979 + 180, -26.6);
            } else {
                const double f = double(i - half) / std::max(1, frames - half - 1);
                setCam(Vec3{centre.x + (f - 0.5) * 0.8 * R, centre.y + 24, all.mins.z + 88}, 0, 0);
            }
            ctx.cameraMoved();
            QApplication::processEvents(QEventLoop::AllEvents, 1);
            frame(nullptr);
            const double ms = st.nsecsElapsed() / 1e6;
            s.cpu.push_back(ms), s.total.push_back(ms), s.sync.push_back(0), s.other.push_back(0);
        }
        line("moving_window", s);
    }
    camA();
    run("select", [&](int i, BenchSeries& s) {
        QElapsedTimer op;
        op.start();
        if (i % 2 == 0) ctx.doc.select(pick(i / 2), false);
        else ctx.doc.deselectAll();
        edited(s, op);
    });
    ctx.doc.deselectAll();
    ctx.documentEdited();
    if (!brushes.empty() && wanted("edit")) {
        ctx.doc.select(brushes[brushes.size() / 2], false);
        ctx.documentEdited();
        run("edit", [&](int i, BenchSeries& s) {
            QElapsedTimer op;
            op.start();
            ctx.doc.translateSelection(Vec3{i % 2 == 0 ? 16.0 : -16.0, 0, 0});
            edited(s, op);
        });
        ctx.doc.deselectAll();
        ctx.documentEdited();
    }
    run("toggle_modes", [&](int i, BenchSeries& s) {
        QElapsedTimer op;
        op.start();
        const Mode& m = modes[i % 3];
        trigger(m.action);
        setLit(m.lit);
        s.sync.push_back(0);
        s.other.push_back(op.nsecsElapsed() / 1e6);
    });
    // View > Game Lighting: the preview bake first (background), then the same still and moving scenarios.
    if (wanted("game_still") || wanted("game_moving")) {
        trigger("CameraModeTextured");
        // The owner works with the lit preview and Game Lighting both on: the benchmark does too (IW8R_BENCH_GAME_LIT=0
        // turns the lit preview off for Game Lighting alone).
        setLit(qEnvironmentVariable("IW8R_BENCH_GAME_LIT") != "0");
        if (qEnvironmentVariable("IW8R_BENCH_NOSHADOW") == "1") ctx.scene.litShadows = false;  // diagnosis: no sun shadow map
        bool benchBox = false;
        // IW8R_BENCH_BOX="x0 y0 z0 x1 y1 z1": a box brush added before the shots (a brush on a stock map);
        // IW8R_BENCH_BOX_MATERIAL names its material.
        if (const QStringList v = qEnvironmentVariable("IW8R_BENCH_BOX").split(' ', Qt::SkipEmptyParts); v.size() == 6) {
            iw8r::AABB box;
            box.add(iw8r::Vec3{v[0].toDouble(), v[1].toDouble(), v[2].toDouble()});
            box.add(iw8r::Vec3{v[3].toDouble(), v[4].toDouble(), v[5].toDouble()});
            if (const QString m = qEnvironmentVariable("IW8R_BENCH_BOX_MATERIAL"); !m.isEmpty()) ctx.currentMaterial = m.toStdString();
            const bool made = iw8r::qt::createBoxBrush(ctx, box);
            ctx.documentEdited();
            report << QString("bench_box %1 material %2").arg(made ? "made" : "FAILED").arg(QString::fromStdString(ctx.currentMaterial));
            benchBox = made;
        }
        if (!ctx.scene.gameLook) trigger("ToggleGameLighting");
        camA();
        QElapsedTimer bt;
        bt.start();
        while (bt.elapsed() < 600000) {
            frame(nullptr);
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            // With a bench box the bake must have seen it (the first bake may run before the scene has the brush).
            const auto lmNow = cam->gameLightmap();
            if (benchBox && !cam->gameLookUnbaked() && !(lmNow && lmNow->polys > 0)) continue;
            if ((cam->gameLightmap() || cam->gameLookUnbaked()) && !cam->gameBaking() && !cam->stockGpuGridLoading()) break;
        }
        if (sunCam) {
            camA();  // the stock sun is known now
            const iw8r::Vec3 t = cam->gameSunTowardNow();
            report << QString("bench_cam sun (stock) toward %1 %2 %3 yaw %4 pitch %5").arg(t.x).arg(t.y).arg(t.z).arg(benchCam[3]).arg(benchCam[4]);
        }
        if (const auto grid = cam->stockGpuGrid())
            report << QString("game_grid probes=%1 tetrahedra=%2 (brushes on the stock map take the grid's light)").arg(grid->pos.size()).arg(grid->tets.size());
        const auto lm = cam->gameLightmap();
        if (cam->gameLookUnbaked()) report << "game_unbaked 1 (no lightmap in the build: the converter's sky fill and the live sun)";
        report << QString("game_bake ms=%1 lightmap=%2x%3 faces=%4 bake_s=%5 %6").arg(bt.elapsed()).arg(lm ? lm->width : 0)
                      .arg(lm ? lm->height : 0).arg(lm ? lm->polys : 0).arg(lm ? lm->seconds : 0, 0, 'f', 2)
                      .arg(lm && !lm->error.empty() ? QString::fromStdString(lm->error) : QString());
        settle();
        {
            // IW8R_UMBRA=cull | cells | both: View > Umbra Visibility for the shots (always set, so a bench without it
            // is not left with the last run's choice).
            // IW8R_BENCH_SHADER=<name>: the map's surface shader for the shots (a stock map: its filter on the reference).
            if (const QString sh = qEnvironmentVariable("IW8R_BENCH_SHADER"); !sh.isEmpty()) {
                if (const iw8r::Entity* ws = ctx.doc.worldspawn()) {
                    ctx.doc.setKeyValue(ws->id, "iw8r_surface_shader", sh.toStdString());
                    ctx.documentEdited();
                }
                QElapsedTimer st;
                st.start();
                while (st.elapsed() < 4000) {
                    frame(nullptr);
                    QApplication::processEvents(QEventLoop::AllEvents, 20);
                }
                settle();
                report << QString("shader %1 reference_filter %2").arg(sh).arg(cam->referenceFilterOn() ? 1 : 0);
            }
            const QString um = qEnvironmentVariable("IW8R_UMBRA");
            w.setUmbraView(um == "cull" || um == "both", um == "cells" || um == "both");
            if (!um.isEmpty()) {
                QElapsedTimer ut;
                ut.start();
                while (ut.elapsed() < 120000) {
                    frame(nullptr);
                    QApplication::processEvents(QEventLoop::AllEvents, 20);
                    if (!w.umbraBusy() && ut.elapsed() > 1500) break;
                }
                settle();
                const iw8r::UmbraViewResult& ur = w.umbraLastResult();
                report << QString("umbra %1 status '%2' culls %3 surfaces %4 models %5 lights %6 probes %7 cells %8 query_ms %9 props_hidden %10 lines %11 lamps_hidden %12 probes_hidden %13 decals_hidden %14 decal_groups %15")
                              .arg(um).arg(iw8r::umbraStatusText(ur.status)).arg(ur.culls ? 1 : 0).arg(ur.visibleByClass[iw8r::kUmbraSurface])
                              .arg(ur.visibleByClass[iw8r::kUmbraStaticModel]).arg(ur.visibleByClass[iw8r::kUmbraPrimaryLight])
                              .arg(ur.visibleByClass[iw8r::kUmbraReflectionProbe]).arg(ur.cells).arg(ur.seconds * 1000, 0, 'f', 1)
                              .arg(cam->tomeCulledProps()).arg(ctx.scene.tomeLines.size())
                              .arg(cam->tomeCulledLights()).arg(cam->tomeCulledProbes()).arg(cam->tomeCulledDecals())
                              .arg(ur.visibleByClass[iw8r::kUmbraDecalCollection]);
            }
        }
        {
            const auto sl = cam->skyLuminance();
            report << QString("game_sky exposure=%1 mean_lum=%2 high_lum=%3").arg(sl[0]).arg(sl[1]).arg(sl[2]);
            report << QString("instances_culled %1").arg(cam->instancesCulled());
            report << QString("background_decodes %1 (%2)").arg(cam->backgroundDecodes()).arg(iw8r::qt::CameraView::backgroundTextureDecode() ? "on" : "off: decoded in place");
            report << QString("effect_particles %1 (preview %2) grid_lit %3 distortion %4 drawn %5 sun_shadow %6")
                          .arg(cam->effectParticles()).arg(ctx.effectPreview ? "on" : "off").arg(cam->effectParticlesGridLit())
                          .arg(cam->effectDistortionQuads()).arg(cam->effectDistortionDrawn()).arg(cam->effectSunShadowUsed() ? "yes" : "no");
            report << QString("effect_flares %1 drawn %2").arg(cam->effectFlareQuads()).arg(cam->effectFlaresDrawn());
            report << QString("effect_distortion_alpha max %1 mean %2").arg(cam->effectDistortionAlphaMax()).arg(cam->effectDistortionAlphaMean());
            report << QString("terrain_patch centre %1 %2 batches %3 camera %4 %5").arg(w.terrainLayerCentre().x, 0, 'f', 0)
                          .arg(w.terrainLayerCentre().y, 0, 'f', 0).arg(w.terrainLayerBatchCount()).arg(ctx.cam.pos.x, 0, 'f', 0).arg(ctx.cam.pos.y, 0, 'f', 0);
            if (const auto* cl = w.stockClutter(); cl && !cl->empty()) {
                std::set<std::string> models;
                for (const auto& c : *cl) models.insert(c.model);
                const auto& c0 = (*cl)[cl->size() / 2];
                report << QString("stock_clutter %1 placements, %2 models, e.g. %3 at %4 %5 %6").arg(cl->size()).arg(models.size())
                              .arg(QString::fromStdString(c0.model)).arg(c0.origin.x).arg(c0.origin.y).arg(c0.origin.z);
            }
            {
                const auto ft = cam->fileTextureStats();
                report << QString("file_textures asked %1 loaded %2 failed %3").arg(ft[0]).arg(ft[1]).arg(ft[2]);
            }
            {
                const auto ts = cam->referenceTriangleSplit();
                report << QString("reference_triangles base %1 layers %2 decals %3").arg(ts[0]).arg(ts[1]).arg(ts[2]);
                const auto lp = cam->layerPassStats();
                report << QString("layer_pass batches %1 draws %2 triangles %3").arg(lp[0]).arg(lp[1]).arg(lp[2]);
                const auto bm = cam->baseMaterialStats();
                report << QString("base_materials batches %1 constants %2 spec_alpha %3").arg(bm[0]).arg(bm[1]).arg(bm[2]);
                report << QString("game_ao drawn %1 hdr %2 gtao %3").arg(cam->gameAoDrawn() ? 1 : 0).arg(cam->gameHdrDrawn() ? 1 : 0).arg(cam->gameAoGtao() ? 1 : 0);
                report << QString("stock_lights %1 in the map, %2 drawn, %3 shadow maps").arg(cam->stockLocalLightCount()).arg(cam->stockLocalLightsDrawn())
                              .arg(cam->stockLightShadowsDrawn());
                report << "post_fx " + cam->postFxReport();
                const auto sb = cam->ssrBatchStats();
                report << QString("ssr_batches base %1 layers %2").arg(sb[0]).arg(sb[1]);
                report << QString("texture_memory mb %1 budget_mb %2 reduced_to_128px %3").arg(double(cam->textureBytes()) / 1048576.0, 0, 'f', 0)
                              .arg(double(cam->textureBudgetBytes()) / 1048576.0, 0, 'f', 0).arg(cam->texturesReducedByBudget());
            }
            const auto ex = cam->gameExposureState();
            report << QString("game_exposure %1 target %2 metered %3 (EV %4)").arg(ex[0]).arg(ex[1]).arg(ex[2])
                          .arg(ex[0] > 0 ? 2.84 - std::log2(ex[0] / 180.0) : 0.0, 0, 'f', 2);
            {
                const auto ds = cam->referenceDecalStats();
                report << QString("stock_decals world %1 (ready %2) props %3 (ready %4, %5 triangles)").arg(ds[0]).arg(ds[1]).arg(ds[2]).arg(ds[3]).arg(ds[4]);
            }
            const auto ls = cam->referenceLayerStats();
            report << QString("stock_layers batches=%1 ready=%2 multiply=%3 with_normal=%4").arg(ls[0]).arg(ls[1]).arg(ls[2]).arg(ls[3]);
        }
        shot("game_a");
        report << "instance_cull_a " + QString::fromStdString(cam->instanceCullReport());
        // IW8R_BENCH_PICK="x y [x y ...]": the stock object at those pixels of the 1920 x 1080 shot (model, material).
        if (const QStringList pk = qEnvironmentVariable("IW8R_BENCH_PICK").split(' ', Qt::SkipEmptyParts); pk.size() >= 2)
            for (int k = 0; k + 1 < pk.size(); k += 2) {
                const double nx = 2.0 * pk[k].toDouble() / 1920.0 - 1.0, ny = 1.0 - 2.0 * pk[k + 1].toDouble() / 1080.0;
                const iw8r::Ray ray = iw8r::app::cameraRay(ctx.cam, nx, ny, 1920.0 / 1080.0);
                const auto h = w.pickStockSurface(ray);
                report << QString("pick %1 %2: %3").arg(pk[k], pk[k + 1],
                                                          !h ? QString("nothing")
                                                             : !h->material.isEmpty() ? "world " + h->material
                                                             : h->light >= 0          ? QString("light %1").arg(h->light)
                                                                                      : QString("prop %1 row %2").arg(h->model).arg(h->propIndex));
            }
        // IW8R_BENCH_PATH="dx dy dz dyaw dpitch N [settle]": a camera path from camera a, N steps to a + the offsets
        // (units, degrees), one shot per step (path_00 .. path_N) with its props by cull reason and the pops (props drawn
        // at the step before and left out while in view: not a LOD swap). settle 0 = one frame per step (as flying),
        // else the step's textures settle first. IW8R_POP_LOG=<file> names each pop.
        if (const QStringList pv = qEnvironmentVariable("IW8R_BENCH_PATH").split(' ', Qt::SkipEmptyParts); pv.size() >= 6) {
            const Vec3 p0 = ctx.cam.pos;
            const double yaw0 = ctx.cam.yaw, pitch0 = ctx.cam.pitch, fov0 = ctx.cam.fov;
            const int steps = std::max(1, pv[5].toInt());
            const bool settleEach = pv.size() < 7 || pv[6] != "0";
            for (int i = 0; i <= steps; ++i) {
                const double f = double(i) / steps;
                setCam(p0 + Vec3{pv[0].toDouble() * f, pv[1].toDouble() * f, pv[2].toDouble() * f}, yaw0 + pv[3].toDouble() * f,
                       pitch0 + pv[4].toDouble() * f);
                ctx.cam.fov = fov0;
                if (settleEach) settle();
                QElapsedTimer pt;
                pt.start();
                frame(nullptr);
                const double ms = pt.nsecsElapsed() / 1e6;
                const QString name = QString("path_%1").arg(i, 2, 10, QChar('0'));
                cam->grabFrame().save(QDir(outDir).filePath(name + ".png"));
                report << QString("%1 cam %2 %3 %4 yaw %5 pitch %6 frame_ms %7 props %8 flares %9")
                              .arg(name).arg(ctx.cam.pos.x, 0, 'f', 1).arg(ctx.cam.pos.y, 0, 'f', 1).arg(ctx.cam.pos.z, 0, 'f', 1)
                              .arg(ctx.cam.yaw, 0, 'f', 2).arg(ctx.cam.pitch, 0, 'f', 2).arg(ms, 0, 'f', 2)
                              .arg(QString::fromStdString(cam->instanceCullReport())).arg(cam->effectFlareQuads());
            }
            camA();
            settle();
        }
        run("game_still", nullptr);
        counts("counts_game");
        run("game_moving", [&](int i, BenchSeries&) {
            const int half = frames / 2;
            if (i < half) {
                const double a = 2 * 3.14159265358979 * i / half;
                const Vec3 pos = centre + Vec3{std::cos(a) * 0.4 * R, std::sin(a) * 0.4 * R, 0.2 * R};
                setCam(pos, a * 180 / 3.14159265358979 + 180, -26.6);
            } else {
                const double f = double(i - half) / std::max(1, frames - half - 1);
                setCam(Vec3{centre.x + (f - 0.5) * 0.8 * R, centre.y + 24, all.mins.z + 88}, 0, 0);
            }
        });
        camB();
        settle();
        shot("game_b");
        trigger("ToggleGameLighting");
    }
    trigger("CameraModeTextured");
    setLit(true);
    camA();
    settle();
    run("still_lit", nullptr);
    run("select_lit", [&](int i, BenchSeries& s) {
        QElapsedTimer op;
        op.start();
        if (i % 2 == 0) ctx.doc.select(pick(i / 2), false);
        else ctx.doc.deselectAll();
        edited(s, op);
    });
    ctx.doc.deselectAll();
    ctx.documentEdited();
    if (!brushes.empty() && wanted("edit_lit")) {
        ctx.doc.select(brushes[brushes.size() / 2], false);
        ctx.documentEdited();
        run("edit_lit", [&](int i, BenchSeries& s) {
            QElapsedTimer op;
            op.start();
            ctx.doc.translateSelection(Vec3{i % 2 == 0 ? 16.0 : -16.0, 0, 0});
            edited(s, op);
        });
        ctx.doc.deselectAll();
        ctx.documentEdited();
    }
    setLit(false);
    trigger("CameraModeWireframe");
    settle();
    run("still_wireframe", nullptr);
    camB();
    run("wireframe_inside", nullptr);
    trigger("CameraModeTextured");
#ifdef _WIN32
    {
        PROCESS_MEMORY_COUNTERS pmc{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
            report << QString("memory working_set_mb=%1 peak_working_set_mb=%2 peak_pagefile_mb=%3")
                          .arg(pmc.WorkingSetSize / 1048576.0, 0, 'f', 0).arg(pmc.PeakWorkingSetSize / 1048576.0, 0, 'f', 0)
                          .arg(pmc.PeakPagefileUsage / 1048576.0, 0, 'f', 0);
    }
#endif
    QFile f(QDir(outDir).filePath("viewport-bench.txt"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 4;
    f.write(report.join(QChar(10)).toUtf8() + QByteArray(1, char(10)));
    return 0;
}

}  // namespace

namespace {

// --stock-props-test <zone>: File > Open IW8 Stock Map as the owner does it, then waits until every prop model of
// the map is in the model cache (or failed, or the time limit IW8R_PROPS_TIMEOUT_MS passes, default 20 minutes) and
// writes <out>/stock-props-test.txt (models wanted / ready / failed with their errors / missing, seconds) plus
// screenshot-fetching.png (taken while models are still being fetched: the markers) and screenshot.png (the end).
// Honours IW8R_CAMERA, IW8R_SMOKE_TEXTURED and IW8R_SMOKE_SETTLE_MS like --smoke-test. Use IW8R_MODEL_CACHE_ROOT for
// a scratch model cache. Exit code 0: the reference is shown and no model is left missing. docs/research/84.
int runStockPropsTest(iw8r::qt::MainWindow& w, const QString& zoneArg, const QString& outDir) {
    using iw8r::qt::MainWindow;
    QDir().mkpath(outDir);
    QStringList report;
    report << "# stock-props-test";
    const auto write = [&] {
        QFile f(QDir(outDir).filePath("stock-props-test.txt"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(report.join(QChar(10)).toUtf8() + QByteArray(1, char(10)));
    };
    const QString replay = iw8r::qt::Settings::instance().value("tools/replay", "E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe").toString();
    const QString zonePath = zoneArg.endsWith(".ff", Qt::CaseInsensitive) ? zoneArg : QFileInfo(replay).absolutePath() + "/zone/" + zoneArg + ".ff";
    const QString zone = QFileInfo(zonePath).completeBaseName();
    QElapsedTimer total;
    total.start();
    QString err;
    const bool opened = w.openStockZone(zonePath, &err);
    QApplication::processEvents();
    report << QString("zone=%1 opened=%2 entities=%3").arg(zone, opened ? "yes" : "NO").arg(w.context().doc.map().entities.size());
    if (!opened) {
        report << "error=" + err.simplified();
        write();
        return 3;
    }
    // Opening a stock map is expected to show its world and props without a second command.
    const bool byOpen = w.stockPropStatus().zone == zone;
    if (!byOpen) w.loadStockReference(zone);
    report << QString("reference_loaded_by_open=%1").arg(byOpen ? "yes" : "NO (the test asked for it, as View > Stock Map Reference does)");

    if (const QStringList p = qEnvironmentVariable("IW8R_CAMERA").split(' ', Qt::SkipEmptyParts); p.size() == 5) {
        w.context().cam.pos = iw8r::Vec3{p[0].toDouble(), p[1].toDouble(), p[2].toDouble()};
        w.context().cam.pitch = p[3].toDouble();
        w.context().cam.yaw = p[4].toDouble();
    }
    if (qEnvironmentVariableIntValue("IW8R_SMOKE_TEXTURED") > 0)
        if (QAction* m = w.action("CameraModeTextured")) m->trigger();
    const auto frame = [&] {
        w.cameraView()->renderAndCount();  // a real frame: update() alone does not paint here
        QApplication::processEvents(QEventLoop::AllEvents, 30);
        QThread::msleep(15);
    };
    const auto shot = [&](const QString& name) {
        // Composite: Qt widgets plus the D3D camera frame (which grab() cannot see).
        QPixmap pix = w.grab();
        QPainter painter(&pix);
        const QImage img = w.cameraView()->grabFrame();
        const QPoint at = w.cameraView()->mapTo(&w, QPoint(0, 0));
        if (!img.isNull()) painter.drawImage(QRect(at, w.cameraView()->size()), img);
        painter.end();
        pix.save(QDir(outDir).filePath(name));
    };

    const qint64 limitMs = qEnvironmentVariableIsSet("IW8R_PROPS_TIMEOUT_MS") ? qEnvironmentVariableIntValue("IW8R_PROPS_TIMEOUT_MS") : 20 * 60 * 1000;
    qint64 loadedMs = -1, doneMs = -1;
    bool midShot = false;
    MainWindow::StockPropStatus s, first;
    while (total.elapsed() < limitMs) {
        frame();
        s = w.stockPropStatus();
        if (!s.loaded) {
            if (s.zone.isEmpty()) break;  // the world export or its load failed
            continue;
        }
        if (loadedMs < 0) {
            loadedMs = total.elapsed();
            first = s;
        }
        if (!midShot && s.fetching && s.placeholders > 0 && total.elapsed() - loadedMs > 1200) {
            midShot = true;
            shot("screenshot-fetching.png");
            report << "label_while_fetching=" + s.line;
        }
        if (!s.fetching) {
            doneMs = total.elapsed();
            break;
        }
    }
    if (loadedMs >= 0 && doneMs >= 0) {
        // The props are redrawn a moment after the last batch: wait for that pass (markers left = failed placements).
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 15000 && !s.stalled && w.stockPropStatus().placeholders != s.counts.placementsFailed) frame();
        const int settle = qEnvironmentVariableIntValue("IW8R_SMOKE_SETTLE_MS");
        t.restart();
        while (t.elapsed() < settle) frame();
        s = w.stockPropStatus();
    }
    frame();
    shot("screenshot.png");
    // IW8R_SMOKE_LIT=1: then the lit preview is switched on. Markers are editor-only and the lit preview draws sun
    // shadows, so the markers must be gone after the toggle and back after the second one.
    if (qEnvironmentVariableIntValue("IW8R_SMOKE_LIT") > 0 && s.loaded && !w.context().scene.litPreview) {
        if (QAction* a = w.action("ToggleLitPreview")) {
            const size_t before = w.stockPropStatus().placeholders;
            QElapsedTimer t;
            a->trigger();
            for (t.start(); t.elapsed() < 1500;) frame();
            const size_t lit = w.stockPropStatus().placeholders;
            shot("screenshot-lit.png");
            a->trigger();
            for (t.restart(); t.elapsed() < 1500;) frame();
            report << QString("lit_preview: markers before=%1 in the lit preview=%2 after=%3").arg(before).arg(lit).arg(w.stockPropStatus().placeholders);
        }
    }

    const auto& c = s.counts;
    report << QString("world_loaded=%1 world_triangles=%2 seconds_to_world=%3").arg(s.loaded ? "yes" : "NO").arg(s.worldTriangles)
                  .arg(loadedMs < 0 ? -1.0 : loadedMs / 1000.0, 0, 'f', 1);
    report << QString("first_pass: models=%1 ready=%2 failed=%3 missing=%4 markers=%5").arg(first.counts.models).arg(first.counts.ready)
                  .arg(first.counts.failed).arg(first.counts.missing).arg(first.placeholders);
    report << QString("end: models=%1 ready=%2 failed=%3 missing=%4").arg(c.models).arg(c.ready).arg(c.failed).arg(c.missing);
    report << QString("end: placements=%1 ready=%2 failed=%3 missing=%4 markers=%5 drawn_as_meshes=%6 gpu_instances=%7")
                  .arg(c.placements).arg(c.placementsReady).arg(c.placementsFailed).arg(c.placementsMissing).arg(s.placeholders)
                  .arg(w.referencePropMeshes()).arg(w.cameraView()->referenceInstances());
    report << QString("fetch_finished=%1 stalled=%2 seconds_fetching=%3 seconds_total=%4")
                  .arg(doneMs >= 0 && !s.stalled ? "yes" : "NO", s.stalled ? "YES: " + s.stallReason : QString("no"))
                  .arg(s.fetchMs < 0 ? 0.0 : s.fetchMs / 1000.0, 0, 'f', 1).arg(total.elapsed() / 1000.0, 0, 'f', 1);
    report << "label=" + s.line;
    for (const auto& [model, error] : s.failures) report << "failed\t" + model + "\t" + error.simplified();
    const bool ok = s.loaded && doneMs >= 0 && !s.stalled && c.missing == 0;
    report << QString("# %1").arg(ok ? "ok" : "FAIL");
    write();
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    QString auditKind, webUser;
    QString thumbPackKind;
    QString thumbTest;  // --thumb-test <model or prefab .map>: one textured picture (textured_thumbs.h), with diagnostics
    int auditLimit = 0, auditBatch = 100;
    bool auditReverse = false;
    bool coverCheck = false;
    bool f5Check = false;
    QString filterShaderFile;  // --shader-filter-program <file.iw8shader.hlsl> --out <file.cso>
    // --install-stock <folder with edited zone files> / --restore-stock <map>: File > Stock Map > Install Edited Stock Map Into
    // Game and Restore Stock Map In Game without a dialog; the report goes to <--out folder>/stock-install.txt.
    QString installStockDir, restoreStockZone;
    // --viewmodel-look <shader file> --out <folder>: File > Stock Map > First-Person Look > Build From Shader without a dialog; the
    // zone goes to <folder>/out, the report to <folder>/viewmodel-look.txt. Nothing is installed.
    QString viewmodelLookShader;
    bool compileDeploy = false;  // --deploy with --compile: install the built package into the game's maps folder
    QString compileFile, openFile, keysPath, guiTestSample, smokeFile, outDir, materialTest, modelTest, worldTest, skyTest;
    QString benchMap, benchReference;
    QString propsZone;       // --stock-props-test
    bool chainTest = false;  // --model-chain-test
    bool featureShots = false;  // --feature-shots (feature_shots.h)
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == "--keys" && i + 1 < argc) keysPath = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--gui-test" && i + 1 < argc) guiTestSample = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--smoke-test" && i + 1 < argc) smokeFile = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--viewport-bench" && i + 1 < argc) benchMap = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--reference" && i + 1 < argc) benchReference = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--cover-check") coverCheck = true;
        else if (a == "--f5-check") f5Check = true;
        else if (a == "--compile" && i + 1 < argc) compileFile = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--out" && i + 1 < argc) outDir = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--material-cache-test" && i + 1 < argc) materialTest = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--model-cache-test" && i + 1 < argc) modelTest = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--world-cache-test" && i + 1 < argc) worldTest = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--stock-sky-test" && i + 1 < argc) skyTest = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--stock-props-test" && i + 1 < argc) propsZone = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--model-chain-test") chainTest = true;
        else if (a == "--feature-shots") featureShots = true;
        else if (a == "--asset-audit" && i + 1 < argc) auditKind = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--thumb-pack" && i + 1 < argc) thumbPackKind = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--thumb-test" && i + 1 < argc) thumbTest = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--limit" && i + 1 < argc) auditLimit = std::atoi(argv[++i]);
        else if (a == "--batch" && i + 1 < argc) auditBatch = std::atoi(argv[++i]);
        else if (a == "--reverse") auditReverse = true;
        else if (a == "--set-web-credentials" && i + 1 < argc) webUser = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--shader-filter-program" && i + 1 < argc) filterShaderFile = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--install-stock" && i + 1 < argc) installStockDir = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--viewmodel-look" && i + 1 < argc) viewmodelLookShader = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--deploy") compileDeploy = true;
        else if (a == "--restore-stock" && i + 1 < argc) restoreStockZone = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--safe-mode") continue;
        else if (!a.startsWith("-")) openFile = a;
    }
    if (!filterShaderFile.isEmpty()) {
        // A shader file's iw8rFilter as the compiled program File > Stock Map > Rebuild Stock Zone hands the converter (editzone
        // "lit_filter"). No window, no device: the compiler only.
        iw8r::CustomShader shader;
        std::string err;
        std::vector<uint8_t> program;
        bool ok = iw8r::readCustomShader(filterShaderFile.toStdString(), shader, &err);
        if (ok && !iw8r::customShaderHasFilter(shader.code)) ok = false, err = "the shader has no float3 iw8rFilter(float3 colour)";
        if (ok) ok = iw8r::app::SceneRenderer::compileProgram(iw8r::customShaderFilterProgramSource(shader), iw8r::kIw8rFilterEntry, program, &err);
        QFile out(outDir);
        if (ok && (outDir.isEmpty() || !out.open(QIODevice::WriteOnly) || out.write(reinterpret_cast<const char*>(program.data()), qint64(program.size())) != qint64(program.size())))
            ok = false, err = "cannot write --out " + outDir.toStdString();
        // The filter with the screen (iw8rFilterScreen), when the shader has one: <out>.screen.cso (editzone
        // lit_filter "screen_program").
        if (ok && iw8r::customShaderHasScreenFilter(shader.code)) {
            std::vector<uint8_t> screen;
            QFile sout(outDir + ".screen.cso");
            ok = iw8r::app::SceneRenderer::compileProgram(iw8r::customShaderScreenFilterProgramSource(shader), iw8r::kIw8rFilterEntry, screen, &err);
            if (ok && (!sout.open(QIODevice::WriteOnly) || sout.write(reinterpret_cast<const char*>(screen.data()), qint64(screen.size())) != qint64(screen.size())))
                ok = false, err = "cannot write " + outDir.toStdString() + ".screen.cso";
            if (ok) std::printf("shader screen filter program written: %s.screen.cso\n", outDir.toStdString().c_str());
        }
        std::printf("%s%s\n", ok ? "shader filter program written: " : "shader filter program FAILED: ", ok ? outDir.toStdString().c_str() : err.c_str());
        std::fflush(stdout);
        return ok ? 0 : 1;
    }
    if (!materialTest.isEmpty()) {
        // Headless: no window, no D3D device. Same app names as the editor so the cache sits in the same folder.
        QCoreApplication core(argc, argv);
        QCoreApplication::setApplicationName("IW8 Radiant Suite");
        QCoreApplication::setOrganizationName("IW8 Radiant Suite");
        if (outDir.isEmpty()) outDir = QCoreApplication::applicationDirPath() + "/material-cache-test";
        return iw8r::qt::runMaterialCacheTest(materialTest.split(',', Qt::SkipEmptyParts), outDir);
    }
    if (!modelTest.isEmpty()) {
        // Headless like --material-cache-test: the model cache under the same AppLocalData folder as the editor.
        QCoreApplication core(argc, argv);
        QCoreApplication::setApplicationName("IW8 Radiant Suite");
        QCoreApplication::setOrganizationName("IW8 Radiant Suite");
        if (outDir.isEmpty()) outDir = QCoreApplication::applicationDirPath() + "/model-cache-test";
        return iw8r::qt::runModelCacheTest(modelTest.split(',', Qt::SkipEmptyParts), outDir);
    }
    if (!webUser.isEmpty()) {
        // --set-web-credentials <user>: the password is read from stdin (never from the command line) and stored as a
        // salted hash in the editor settings (web_server.h).
        QCoreApplication core(argc, argv);
        QCoreApplication::setApplicationName("IW8 Radiant Suite");
        QCoreApplication::setOrganizationName("IW8 Radiant Suite");
        QTextStream in(stdin);
        const QString pass = in.readLine();
        if (pass.size() < 8) {
            std::fprintf(stderr, "password must be at least 8 characters\n");
            return 2;
        }
        iw8r::qt::WebServer::setCredentials(webUser, pass);
        std::printf("web credentials set for %s\n", qPrintable(webUser));
        // --web-enable: also turn on the server with LAN and UPnP (remote access) on its port (default 8780).
        for (int i = 1; i < argc; ++i)
            if (QString::fromLocal8Bit(argv[i]) == "--web-enable") {
                auto& s = iw8r::qt::Settings::instance();
                s.setValue("web/enabled", true);
                s.setValue("web/lan", true);
                s.setValue("web/upnp", true);
                if (s.value("web/port").toInt() <= 0) s.setValue("web/port", 8780);
                std::printf("web server enabled: LAN + UPnP, port %d\n", s.value("web/port").toInt());
            }
        return 0;
    }
    // Models and prefabs: textured pictures made by the editor's own 3D view (textured_thumbs.h), after the window
    // exists (below). Materials: their colour images (thumb_pack.h).
    const bool texturedPack = thumbPackKind == "models" || thumbPackKind == "prefabs";
    if (!thumbPackKind.isEmpty() && !texturedPack) {
        // 4.13: full thumbnail packs (thumb_pack.h).
        QGuiApplication gui(argc, argv);
        QCoreApplication::setApplicationName("IW8 Radiant Suite");
        QCoreApplication::setOrganizationName("IW8 Radiant Suite");
        return iw8r::qt::runThumbPack(thumbPackKind, auditLimit, auditBatch);
    }
    if (!auditKind.isEmpty()) {
        // Headless stock asset availability audit (asset_audit.h); --out is the status TSV.
        QCoreApplication core(argc, argv);
        QCoreApplication::setApplicationName("IW8 Radiant Suite");
        QCoreApplication::setOrganizationName("IW8 Radiant Suite");
        if (outDir.isEmpty()) outDir = QCoreApplication::applicationDirPath() + "/iw8_" + auditKind + "_status.tsv";
        return iw8r::qt::runAssetAudit(auditKind, outDir, auditLimit, auditBatch, auditReverse);
    }
    if (!skyTest.isEmpty()) {
        // Headless: exports one stock sky into the material cache images/ folder (doc 33).
        QCoreApplication core(argc, argv);
        QCoreApplication::setApplicationName("IW8 Radiant Suite");
        QCoreApplication::setOrganizationName("IW8 Radiant Suite");
        if (outDir.isEmpty()) outDir = QCoreApplication::applicationDirPath() + "/stock-sky-test";
        return iw8r::qt::runStockSkyTest(skyTest, outDir);
    }
    if (chainTest) {
        // Headless, no game needed: the model fetch chain against a cache that cannot deliver (model_cache.h).
        QCoreApplication core(argc, argv);
        QCoreApplication::setApplicationName("IW8 Radiant Suite");
        QCoreApplication::setOrganizationName("IW8 Radiant Suite");
        if (outDir.isEmpty()) outDir = QCoreApplication::applicationDirPath() + "/model-chain-test";
        return iw8r::qt::runModelCacheChainTest(outDir);
    }
    if (!worldTest.isEmpty()) {
        // Headless like --model-cache-test: the world cache under the editor's AppLocalData folder.
        QCoreApplication core(argc, argv);
        QCoreApplication::setApplicationName("IW8 Radiant Suite");
        QCoreApplication::setOrganizationName("IW8 Radiant Suite");
        if (outDir.isEmpty()) outDir = QCoreApplication::applicationDirPath() + "/world-cache-test";
        return iw8r::qt::runWorldCacheTest(worldTest, outDir);
    }
    if (f5Check) {
        // What F5 would do before it starts the game, against the real install: which exe it picks, whether that exe
        // takes the start request, and whether the local host answers the health probe. Starts nothing, writes only
        // the report (--out <file>, default f5-check.txt beside the exe).
        QCoreApplication core(argc, argv);
        QCoreApplication::setApplicationName("IW8 Radiant Suite");
        QCoreApplication::setOrganizationName("IW8 Radiant Suite");
        using iw8r::qt::DevmapRunner;
        const QString replay = iw8r::qt::Settings::instance().value("tools/replay", "E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe").toString();
        QString note;
        const QString exe = DevmapRunner::chooseLaunchExe(iw8r::qt::Settings::instance().value("tools/launch").toString(), replay, &note);
        QStringList report;
        report << "exe: " + exe << "note: " + note;
        report << QString("exe exists: %1").arg(QFileInfo::exists(exe) ? "yes" : "NO");
        report << QString("client start request (DW_DEVMAP marker): %1").arg(DevmapRunner::exeHasClientRequest(exe) ? "yes" : "no");
        const QString host = DevmapRunner::localHostOf(exe);
        report << "local host named by the exe's ini: " + (host.isEmpty() ? QString("(none)") : host);
        report << "health address: " + DevmapRunner::hostHealthSetting();
        const bool up = DevmapRunner::probeHost(DevmapRunner::hostHealthSetting());
        report << QString("host answers the probe: %1").arg(up ? "yes" : "NO");
        report << "host command: " + (DevmapRunner::hostCommandSetting().isEmpty() ? QString("(none)") : DevmapRunner::hostCommandSetting());
        const QString out = outDir.isEmpty() ? QCoreApplication::applicationDirPath() + "/f5-check.txt" : outDir;
        QFile f(out);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(report.join(QChar(10)).toUtf8() + QByteArray(1, char(10)));
        return QFileInfo::exists(exe) && (host.isEmpty() || up) ? 0 : 1;
    }
    if (coverCheck) {
        // The F5 cover card on the real desktop (devmap_cover.h); --out is the report file.
        QApplication app(argc, argv);
        QCoreApplication::setApplicationName("IW8 Radiant Suite");
        QCoreApplication::setOrganizationName("IW8 Radiant Suite");
        return iw8r::qt::DevmapCover::runRealCheck(outDir.isEmpty() ? QCoreApplication::applicationDirPath() + "/cover-check.txt" : outDir);
    }
    const bool bench = !benchMap.isEmpty();
    const bool guiTest = !guiTestSample.isEmpty() || bench;  // the benchmark runs like the GUI test: off-screen, volatile settings
    const bool stockInstall = !installStockDir.isEmpty() || !restoreStockZone.isEmpty();
    const bool lookBuild = !viewmodelLookShader.isEmpty();
    const bool smoke = !smokeFile.isEmpty() || !compileFile.isEmpty() || !propsZone.isEmpty() || featureShots || stockInstall || lookBuild;
    if ((stockInstall || lookBuild) && !outDir.isEmpty()) QDir().mkpath(outDir);
    // The start-up check writes its report into --out: a folder that is not there yet is made (it ended with exit
    // code 0 and no report before).
    if (!smokeFile.isEmpty() && !outDir.isEmpty()) QDir().mkpath(outDir);
    if (guiTest) qputenv("QT_QPA_PLATFORM", "offscreen");
    if (bench) {
        // The benchmark never writes the mapper's caches: without the overrides it uses (empty) scratch caches under
        // --out. tools/make_perf_maps.py makes copies of the entries the benchmark maps need.
        if (outDir.isEmpty()) outDir = "viewport-bench";
        outDir = QDir(outDir).absolutePath();
        if (!qEnvironmentVariableIsSet("IW8R_MODEL_CACHE_ROOT")) qputenv("IW8R_MODEL_CACHE_ROOT", (outDir + "/cache/model_cache").toLocal8Bit());
        if (!qEnvironmentVariableIsSet("IW8R_MATERIAL_CACHE_ROOT")) qputenv("IW8R_MATERIAL_CACHE_ROOT", (outDir + "/cache/material_cache").toLocal8Bit());
    }

    GuardedApplication app(argc, argv);
    iw8r::qt::EditorLog::installQtMessageHandler();  // Qt warnings also go to the Console dock
    QApplication::setApplicationName("IW8 Radiant Suite");
    QApplication::setOrganizationName("IW8 Radiant Suite");
    // Crash reports (stack with file:line + minidump) under the app data folder; see crash_handler.h.
    // (The benchmark keeps its crash reports with its other output, not in the mapper's folder.)
    iw8r::qt::installCrashHandler(bench ? outDir + "/crashes" : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/crashes");
    applyDarkTheme(app);  // replaced by the saved theme once settings are known (below)
    if (outDir.isEmpty()) outDir = QCoreApplication::applicationDirPath();
    if (guiTest || smoke) iw8r::qt::Settings::instance().setVolatile(true);
    if (bench) {
        // No tool runs and no autosave during the benchmark (settings are in memory only here).
        // (A stock zone bench, stock:<zone>, needs ACTS to open the zone, as the editor does.)
        if (!benchMap.startsWith("stock:")) iw8r::qt::Settings::instance().setValue("tools/acts", outDir + "/no-acts.exe");
        iw8r::qt::Settings::instance().setValue("autosave/minutes", 0);
    }
    // Safe mode (9.5): --safe-mode, or Shift held at start: default settings in memory only (no saved layout, window
    // position, web server or other preferences), nothing written back.
    bool safeMode = false;
    for (int i = 1; i < argc; ++i) safeMode |= QString::fromLocal8Bit(argv[i]) == "--safe-mode";
    if (!guiTest && !smoke && (QGuiApplication::queryKeyboardModifiers() & Qt::ShiftModifier)) safeMode = true;
    // Runs that draw a fixed number of frames decode every texture where it is asked for (camera_view.h).
    // IW8R_BACKGROUND_DECODE: keeps the workers on in such a run (a bench of the loading itself).
    if ((guiTest || smoke) && !qEnvironmentVariableIsSet("IW8R_BACKGROUND_DECODE")) iw8r::qt::CameraView::setBackgroundTextureDecode(false);
    if (safeMode) {
        iw8r::qt::Settings::instance().setVolatile(true);
        qputenv("IW8R_SAFE_MODE", "1");
    }
    // UI theme (9.2): View > Theme; tests keep the dark default.
    if (!guiTest && !smoke)
        iw8r::qt::applyTheme(iw8r::qt::Settings::instance().value("ui/theme", "Dark").toString(),
                             QColor(iw8r::qt::Settings::instance().value("ui/accent").toString()));

    // The renderer's shaders: compiled on worker threads from here (shader_cache.h), read back from the disk cache when
    // their source is unchanged, so the camera view does not wait on D3DCompile (it took about 10 s on the GUI thread).
    iw8r::app::setShaderCacheDir((QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/shader_cache").toStdWString());
    iw8r::app::SceneRenderer::prewarmShaders();

    // One D3D11 device for every viewport.
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1,
                                   D3D11_SDK_VERSION, &device, nullptr, &context);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1, D3D11_SDK_VERSION,
                               &device, nullptr, &context);
    // The camera views draw from their render threads too (camera_view_render_thread.cpp): the immediate context is
    // made thread-safe call by call (Present runs outside the editor's GPU lock).
    if (context) {
        ID3D11Multithread* mt = nullptr;
        if (SUCCEEDED(context->QueryInterface(__uuidof(ID3D11Multithread), reinterpret_cast<void**>(&mt))) && mt) {
            mt->SetMultithreadProtected(TRUE);
            mt->Release();
        }
    }

    // IW8 canonical key table: --keys, else <repo>/data/generated beside the exe tree.
    if (keysPath.isEmpty()) {
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 3 && keysPath.isEmpty(); ++up) {
            if (QFileInfo::exists(d.filePath("data/generated/iw8_canonical_keys.tsv"))) keysPath = d.filePath("data/generated/iw8_canonical_keys.tsv");
            d.cdUp();
        }
    }
    if (!keysPath.isEmpty()) iw8r::KeyTable::global().loadTsv(keysPath.toStdString());

    // 4.13: cache purges, a move to another drive and the size cap, before any cache index is read.
    if (!guiTest && !smoke)
        if (const QString note = iw8r::qt::runCacheStartupTasks(); !note.isEmpty()) std::fprintf(stderr, "caches: %s\n", qPrintable(note));

    int rc = 0;
    // GUI thread stall watch (IW8R_STALL_WATCH=<ms> or 1; or Tools > Report Freezes): from here, so a slow start counts.
    if (const QString sw = qEnvironmentVariable("IW8R_STALL_WATCH");
        !sw.isEmpty() || (!guiTest && !smoke && iw8r::qt::Settings::instance().value("debug/stallWatch", false).toBool()))
        iw8r::qt::StallWatch::instance().start(sw.toInt() > 1 ? sw.toInt() : 250);
    {
        iw8r::qt::MainWindow w(device, context);
        // The asset browsers read their catalogues on worker threads: automated runs start from full lists.
        if (guiTest || smoke || texturedPack || !thumbTest.isEmpty()) w.waitForCatalogs();
        if (texturedPack) return w.runTexturedThumbPack(thumbPackKind, auditLimit);  // the window is never shown
        if (!thumbTest.isEmpty()) return w.runThumbTest(thumbTest, outDir.isEmpty() ? QDir::tempPath() : outDir);
        if (smoke) {
            // Keep the smoke-test window off every monitor and never activate it.
            w.setAttribute(Qt::WA_ShowWithoutActivating);
            QRect virt;
            for (QScreen* s : QGuiApplication::screens()) virt = virt.united(s->geometry());
            w.move(virt.left() - 1800, virt.top() + 40);
        }
        w.show();
        if (!guiTest && !smoke) {
            // A crash since the last start: say where its report is (crash_handler.h).
            const QString crashes = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/crashes";
            const qint64 seen = iw8r::qt::Settings::instance().value("crash/lastSeen", 0).toLongLong();
            if (const QString r = iw8r::qt::newestCrashReport(crashes, seen + 1); !r.isEmpty()) {
                w.context().setStatus("The editor crashed last time; report: " + r);
                iw8r::qt::Settings::instance().setValue("crash/lastSeen", QDateTime::currentMSecsSinceEpoch());
            }
        }
        // First-run setup at startup when the Replay exe is missing; never in the scripted GUI and smoke runs.
        if (!guiTest && !smoke) QTimer::singleShot(0, &w, [&w] { w.showFirstRunSetup(true); });
        // Browser access (L10): the web server starts with the editor when Settings web/enabled is on.
        if (!guiTest && !smoke) QTimer::singleShot(500, &w, [&w] { w.applyWebServerSettings(); });
        // The textured picture packs of the Models and Prefabs tabs stay complete by themselves (idle top-up).
        if (!guiTest && !smoke) w.startThumbnailTopUp();
        // A map to open, or stock:<zone .ff> for a stock map (as File > Open IW8 Stock Map).
        if (openFile.startsWith("stock:")) {
            QString err;
            if (!w.openStockZone(openFile.mid(6), &err)) w.context().setStatus("Stock map not opened: " + err);
        } else if (!openFile.isEmpty()) {
            w.openPath(openFile);
        }
        if (lookBuild) {
            QTimer::singleShot(300, &w, [&] {
                QStringList report;
                const QString dir = outDir.isEmpty() ? QDir::tempPath() + "/iw8r_look" : outDir;
                QDir().mkpath(dir);
                const bool ok = w.buildViewmodelLookQuiet(QDir::fromNativeSeparators(viewmodelLookShader), QDir::fromNativeSeparators(dir), report);
                QFile rf(QDir(dir).filePath("viewmodel-look.txt"));
                if (rf.open(QIODevice::WriteOnly | QIODevice::Text)) rf.write((report.join("\n") + "\n").toUtf8());
                app.exit(ok ? 0 : 5);
            });
        }
        if (stockInstall) {
            QTimer::singleShot(300, &w, [&] {
                QStringList report;
                const bool ok = !installStockDir.isEmpty() ? w.installStockZonesQuiet(QDir::fromNativeSeparators(installStockDir), report)
                                                           : w.restoreStockZonesQuiet(restoreStockZone, report);
                QFile rf(QDir(outDir.isEmpty() ? QDir::tempPath() : outDir).filePath("stock-install.txt"));
                if (rf.open(QIODevice::WriteOnly | QIODevice::Text)) rf.write((report.join("\n") + "\n").toUtf8());
                app.exit(ok ? 0 : 5);
            });
        }
        if (!compileFile.isEmpty()) {
            // Headless F7 of a map through the editor's own path (stock textures/models, sky, gsc-tool, ACTS
            // validation): writes <out>/compile-log.txt and <out>/compile-result.txt (ok/FAIL, map id, package folder).
            QTimer::singleShot(300, &w, [&] {
                w.openPath(compileFile);
                if (w.context().doc.path().empty()) {
                    app.exit(3);
                    return;
                }
                // IW8R_CAPTURE_PROBES=1: capture the reflection probes with the camera renderer first (5.5).
                if (qEnvironmentVariableIntValue("IW8R_CAPTURE_PROBES") > 0) {
                    if (QAction* m = w.action("CameraModeTextured")) m->trigger();
                    QElapsedTimer settle;
                    settle.start();
                    while (settle.elapsed() < qEnvironmentVariableIntValue("IW8R_CAPTURE_SETTLE_MS")) {
                        w.cameraView()->renderAndCount();  // a real frame: update() alone does not paint here
                        QApplication::processEvents(QEventLoop::AllEvents, 30);
                        QThread::msleep(15);
                    }
                    w.captureReflectionProbes();
                }
                // Stock models / materials the map uses are fetched first, so one headless build is complete
                // (IW8R_PREFETCH_MS caps the wait, default 10 minutes; 0 skips it).
                QString prefetch;
                {
                    const int ms = qEnvironmentVariableIsSet("IW8R_PREFETCH_MS") ? qEnvironmentVariableIntValue("IW8R_PREFETCH_MS") : 600000;
                    if (ms > 0) prefetch = w.prefetchStockAssets(ms);
                }
                QDir().mkpath(outDir);
                iw8r::qt::CompilePanel* cp = w.compilePanel();
                QObject::connect(cp, &iw8r::qt::CompilePanel::finished, &w, [&, cp, prefetch](bool ok) {
                    QFile lf(QDir(outDir).filePath("compile-log.txt"));
                    if (lf.open(QIODevice::WriteOnly | QIODevice::Truncate)) lf.write((prefetch + "\n" + cp->logText()).toUtf8());
                    QFile rf(QDir(outDir).filePath("compile-result.txt"));
                    if (rf.open(QIODevice::WriteOnly | QIODevice::Truncate))
                        rf.write(QString("%1 map=%2 package=%3\n").arg(ok ? "ok" : "FAIL", cp->mapId(), cp->zonesDir()).toUtf8());
                    // --deploy: the built package into <game>/mods/mw120r/maps (F6 without the dialog).
                    if (ok && compileDeploy) {
                        QString derr;
                        const bool deployed = w.deployBuiltMapQuiet(&derr);
                        if (rf.isOpen()) rf.write(deployed ? QByteArray("deploy ok\n") : ("deploy FAIL " + derr + "\n").toUtf8());
                        if (!deployed) ok = false;
                    }
                    app.exit(ok ? 0 : 1);
                });
                cp->run({iw8r::qt::CompilePanel::Stage::Extraction, iw8r::qt::CompilePanel::Stage::Zones,
                         iw8r::qt::CompilePanel::Stage::Validate});
            });
        } else if (bench) {
            QTimer::singleShot(300, &w, [&] { app.exit(runViewportBench(w, benchMap, outDir, benchReference)); });
        } else if (guiTest) {
            QTimer::singleShot(300, &w, [&] { app.exit(iw8r::qt::runQtGuiTest(w, guiTestSample, outDir)); });
        } else if (featureShots) {
            QTimer::singleShot(300, &w, [&] {
                app.exit(iw8r::qt::runFeatureShots(w, outDir.isEmpty() ? QCoreApplication::applicationDirPath() + "/feature_shots" : outDir));
            });
        } else if (!propsZone.isEmpty()) {
            QTimer::singleShot(300, &w, [&] { app.exit(runStockPropsTest(w, propsZone, outDir)); });
        } else if (smoke && !stockInstall && !lookBuild) {
            QTimer::singleShot(300, &w, [&] {
                QElapsedTimer openTimer;
                openTimer.start();
                w.openPath(smokeFile);
                QApplication::processEvents();
                const qint64 openMs = openTimer.elapsed();
                // IW8R_SMOKE_REFERENCE=<zone>: also draw a cached stock world (View > Stock Map Reference) from above.
                if (const QString zone = qEnvironmentVariable("IW8R_SMOKE_REFERENCE"); !zone.isEmpty()) {
                    iw8r::qt::WorldCache wc([](const QString&) { return QString(); });
                    QString err;
                    if (wc.lookup(zone)) {
                        w.loadStockReference(zone);  // the editor's own path: world triangles plus prop boxes (async)
                        const QStringList p = qEnvironmentVariable("IW8R_SMOKE_CAMERA").split(' ', Qt::SkipEmptyParts);
                        if (p.size() == 5) {
                            w.context().cam.pos = iw8r::Vec3{p[0].toDouble(), p[1].toDouble(), p[2].toDouble()};
                            w.context().cam.pitch = p[3].toDouble();
                            w.context().cam.yaw = p[4].toDouble();
                        }
                    } else {
                        std::fprintf(stderr, "reference %s: %s\n", qPrintable(zone), qPrintable(err));
                    }
                }
                // Performance (9.4): first frame (scene build), then steady frames with and without an edit in between.
                // IW8R_CAMERA="x y z pitch yaw": camera for the screenshot (any map).
                if (const QStringList p = qEnvironmentVariable("IW8R_CAMERA").split(' ', Qt::SkipEmptyParts); p.size() == 5) {
                    w.context().cam.pos = iw8r::Vec3{p[0].toDouble(), p[1].toDouble(), p[2].toDouble()};
                    w.context().cam.pitch = p[3].toDouble();
                    w.context().cam.yaw = p[4].toDouble();
                }
                QElapsedTimer frameTimer;
                frameTimer.start();
                const size_t pixels = w.cameraView()->renderAndCount();
                const qint64 firstMs = frameTimer.elapsed();
                frameTimer.restart();
                for (int i = 0; i < 10; ++i) w.cameraView()->renderAndCount();
                const double steadyMs = frameTimer.elapsed() / 10.0;
                frameTimer.restart();
                for (int i = 0; i < 5; ++i) {
                    w.context().doc.deselectAll();
                    w.context().documentEdited();
                    w.cameraView()->renderAndCount();
                }
                const double editMs = frameTimer.elapsed() / 5.0;
                double workingMb = -1, peakMb = -1;
#ifdef _WIN32
                {
                    PROCESS_MEMORY_COUNTERS pmc{};
                    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
                        workingMb = pmc.WorkingSetSize / 1048576.0;
                        peakMb = pmc.PeakWorkingSetSize / 1048576.0;
                    }
                }
#endif
                // 2D grid view: zoomed out to see the whole map, repaint timed (QPainter, offscreen grab).
                double gridMs = -1;
                if (iw8r::qt::GridView* g = w.gridView()) {
                    // Zoom out to the whole map (all items' bounds).
                    iw8r::AABB all;
                    for (const auto* it : w.context().scene.items()) all.add(it->bounds);
                    if (all.valid()) {
                        auto& c = w.context();
                        c.gridCenterH = (all.mins[c.axisH()] + all.maxs[c.axisH()]) / 2;
                        c.gridCenterV = (all.mins[c.axisV()] + all.maxs[c.axisV()]) / 2;
                        c.gridScale = std::max(0.01, 0.9 * g->width() / std::max(1.0, all.maxs[c.axisH()] - all.mins[c.axisH()]));
                    }
                    g->grab();
                    frameTimer.restart();
                    for (int i = 0; i < 5; ++i) g->grab();
                    gridMs = frameTimer.elapsed() / 5.0;
                }
                {
                    QFile pf(QDir(outDir).filePath("perf.txt"));
                    if (pf.open(QIODevice::WriteOnly | QIODevice::Truncate))
                        pf.write(QString("open_ms=%1 first_frame_ms=%2 steady_frame_ms=%3 frame_after_edit_ms=%4 items=%5 grid_frame_ms=%6 working_set_mb=%7 peak_working_set_mb=%8\n")
                                     .arg(openMs).arg(firstMs).arg(steadyMs, 0, 'f', 1).arg(editMs, 0, 'f', 1)
                                     .arg(w.context().scene.items().size()).arg(gridMs, 0, 'f', 1).arg(workingMb, 0, 'f', 1).arg(peakMb, 0, 'f', 1).toUtf8());
                }
                // The viewport render thread (camera_view_render_thread.cpp).
                QString renderThreadLine;
                bool renderThreadOk = true;
                if (iw8r::qt::CameraView* cam = w.cameraView(); cam->hasSwapChain() && iw8r::qt::CameraView::renderThreadOn()) {
                    const int f0 = cam->renderThreadFrames();
                    cam->prepareFrameForTest();  // prepared here, drawn by the render thread while this thread sleeps
                    Sleep(400);
                    const int f1 = cam->renderThreadFrames();
                    const iw8r::Vec3 before = w.context().cam.pos;
                    w.context().looking = true;
                    cam->setFakeFlyInputForTest(true);
                    cam->prepareFrameForTest();
                    Sleep(1500);  // this thread does not answer: the render thread flies the camera (W held)
                    const int f2 = cam->renderThreadFrames();
                    cam->setFakeFlyInputForTest(false);
                    w.context().looking = false;
                    cam->prepareFrameForTest();  // takes the render thread's camera over
                    QApplication::processEvents();
                    const double moved = iw8r::length(w.context().cam.pos - before);
                    w.context().cam.pos = before;
                    renderThreadLine = QString(" render_thread_frames=%1 takeover_frames=%2 takeover_moved=%3").arg(f1 - f0).arg(f2 - f1).arg(moved, 0, 'f', 0);
                    renderThreadOk = f1 > f0 && f2 - f1 >= 10 && moved > 50;
                }
                const bool ok = w.cameraView()->hasSwapChain() && pixels > 1000 && renderThreadOk;
                // IW8R_SMOKE_SETTLE_MS=<ms>: keep the event loop and the camera running so streamed textures (a few
                // decodes per frame, ACTS exports in the background) appear in the screenshot.
                // IW8R_SMOKE_NORMALMAPS=0: the textured shot without normal maps (A/B for 3.2); not saved to settings.
                if (qEnvironmentVariable("IW8R_SMOKE_NORMALMAPS") == "0") w.context().normalMaps = false;
                // IW8R_SMOKE_WIND=1: the wind preview on (foliage sway shader).
                if (qEnvironmentVariableIntValue("IW8R_SMOKE_WIND") > 0 && !w.context().windPreview)
                    if (QAction* a = w.action("WindPreview")) a->trigger();
                if (const QString t = qEnvironmentVariable("IW8R_SMOKE_WIND_TIME"); !t.isEmpty() && w.cameraView())
                    w.cameraView()->setWindTime(t.toDouble());
                if (qEnvironmentVariableIntValue("IW8R_SMOKE_LIT") > 0 && !w.context().scene.litPreview)
                    if (QAction* a = w.action("ToggleLitPreview")) a->trigger();
                if (qEnvironmentVariableIntValue("IW8R_SMOKE_TEXTURED") > 0)
                    if (QAction* m = w.action("CameraModeTextured")) m->trigger();
                if (const int settle = qEnvironmentVariableIntValue("IW8R_SMOKE_SETTLE_MS"); settle > 0) {
                    QElapsedTimer t;
                    t.start();
                    while (t.elapsed() < settle) {
                        w.cameraView()->renderAndCount();  // a real frame: update() alone does not paint here
                        QApplication::processEvents(QEventLoop::AllEvents, 30);
                        QThread::msleep(15);
                    }
                }
                // Composite screenshot: Qt widgets plus the D3D camera frame (which grab() cannot see).
                QPixmap shot = w.grab();
                {
                    QPainter painter(&shot);
                    const QImage frame = w.cameraView()->grabFrame();
                    const QPoint at = w.cameraView()->mapTo(&w, QPoint(0, 0));
                    if (!frame.isNull()) painter.drawImage(QRect(at, w.cameraView()->size()), frame);
                }
                shot.save(QDir(outDir).filePath("screenshot.png"));
                QFile f(QDir(outDir).filePath("smoke-test.txt"));
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    f.write(QString("%1 swapchain=%2 drawn_pixels=%3 entities=%4 textures=%6 normal_maps=%7%8 title=%5\n")
                                .arg(ok ? "ok" : "FAIL")
                                .arg(w.cameraView()->hasSwapChain() ? "yes" : "no")
                                .arg(pixels)
                                .arg(w.context().doc.map().entities.size())
                                .arg(w.windowTitle())
                                .arg(w.cameraView()->loadedTextures())
                                .arg(w.cameraView()->loadedNormalMaps())
                                .arg(renderThreadLine)
                                .toUtf8());
                app.exit(ok ? 0 : 2);
            });
        }
        // IW8R_TEST_WALK_AFTER=<seconds>: View > Play Preview (Walk) then, for six seconds (render thread checks).
        if (const int walkAfter = qEnvironmentVariableIntValue("IW8R_TEST_WALK_AFTER"); walkAfter > 0)
            QTimer::singleShot(walkAfter * 1000, &w, [&w] {
                if (QAction* a = w.action("PlayPreview")) a->trigger();
                QTimer::singleShot(2500, &w, [&w] {
                    const auto& c = w.context().cam.pos;
                    std::fprintf(stderr, "walk test: noclip=%d at %.0f %.0f %.0f, %zu collision solids\n", w.cameraView()->playNoclip() ? 1 : 0,
                                 c.x, c.y, c.z, w.cameraView()->playSolids());
                });
                QTimer::singleShot(6000, &w, [&w] {
                    if (QAction* a = w.action("PlayPreview")) a->trigger();
                    std::fprintf(stderr, "walk test: done\n");
                });
            });
        // IW8R_TEST_PLAY_INPUT=<script>: the play preview driven by real key events to the camera (play_input_test.h).
        if (const QString script = qEnvironmentVariable("IW8R_TEST_PLAY_INPUT"); !script.isEmpty()) iw8r::qt::startPlayInputTest(w, script);
        // IW8R_QUIT_AFTER=<seconds>: a normal session that ends by itself (stall measurements with IW8R_STALL_WATCH).
        if (const int quitAfter = qEnvironmentVariableIntValue("IW8R_QUIT_AFTER"); quitAfter > 0)
            QTimer::singleShot(quitAfter * 1000, &app, &QCoreApplication::quit);
        rc = app.exec();
    }
    iw8r::qt::StallWatch::instance().stop();
    if (context) context->Release();
    if (device) device->Release();
    return rc;
}
