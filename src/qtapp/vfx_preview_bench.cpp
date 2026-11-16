#include "vfx_preview_bench.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QThread>

#include <functional>
#include <memory>
#include <vector>

#include "asset_browser.h"
#include "main_window.h"
#include "map_text.h"
#include "vfx_preview.h"
#include "vfx_sim.h"

namespace iw8r::qt {

namespace {

void pump(int ms) {
    QElapsedTimer t;
    t.start();
    do {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    } while (t.elapsed() < ms);
}

bool waitUntil(const std::function<bool()>& done, int ms) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) pump(50);
    return done();
}

}  // namespace

int runVfxPanelShots(MainWindow& w, const QString& outDir) {
    QDir().mkpath(outDir);
    QStringList report;
    const auto flush = [&] {
        QFile f(QDir(outDir).filePath("vfx-panel.txt"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(report.join("\n").toUtf8() + "\n");
    };
    QStringList names = qEnvironmentVariable("IW8R_VFX_PANEL_EFFECTS").split(',', Qt::SkipEmptyParts);
    if (names.isEmpty())
        names = {"vfx/iw8/veh/scriptables/vfx_smoke_light_ch",
                 "vfx/iw8/veh/scriptables/shared/vfx_veh_fire_linger_med",
                 "vfx/core/expl/spark_death",
                 "vfx/iw8/level/safehouse/vfx_safehouse_finale_drone_heat_dist",
                 "vfx/core/expl/grenade_flash_aerial",
                 "vfx/_requests/mp_gameplay/vfx_smk_signal",
                 "vfx/core/impacts/small_glass",
                 "vfx/iw8_mp/killstreak/vfx_ac130_flares"};
    w.waitForCatalogs();
    int failed = 0;
    for (const QString& name : names) {
        const QString leaf = name.section('/', -1);
        report << "== " + name;
        flush();
        QString file = w.findVfxExport(name);
        if (file.isEmpty()) {
            bool done = false;
            const auto c = QObject::connect(&w, &MainWindow::vfxExportDone, &w, [&](const QString& n, bool) {
                if (n == name) done = true;
            });
            if (w.requestVfxExport(name)) waitUntil([&] { return done; }, 240000);
            QObject::disconnect(c);
            file = w.findVfxExport(name);
        }
        std::string text;
        VfxSimDef def;
        if (file.isEmpty() || !readFile(file.toStdString(), text) || !buildVfxSim(text, def) || def.emitters.empty()) {
            report << "  skipped: no export with particles (" + (file.isEmpty() ? QString("not exported") : file) + ")";
            continue;
        }
        QStringList elements;
        for (const VfxEmitterSim& e : def.emitters) elements << QString("%1:%2").arg(e.element).arg(QString::fromStdString(e.materials.empty() ? e.material : e.materials.front()));
        report << QString("  %1 emitter(s), %2; elements:materials %3")
                      .arg(def.emitters.size())
                      .arg(def.duration < 0 ? QString("looping") : QString("one-shot %1 s").arg(def.duration, 0, 'f', 2))
                      .arg(elements.join(" "));
        // The previews: before (soft discs, the panel's old 240 px high box) and after (square), at 300 and 420 px.
        std::vector<std::unique_ptr<VfxPreview>> shots;
        for (const int width : {300, 420})
            for (const bool discs : {true, false}) {
                auto pv = std::make_unique<VfxPreview>();
                pv->setAttribute(Qt::WA_DontShowOnScreen);
                pv->setDiscStyle(discs);
                pv->resize(width, discs ? 240 : 600);
                pv->show();
                pv->setEffect(def);
                shots.push_back(std::move(pv));
            }
        pump(200);
        // The sprites: exported and read (or known missing).
        QElapsedTimer wait;
        wait.start();
        const bool loaded = waitUntil([&] {
            for (const auto& pv : shots)
                if (!pv->discStyle()) {
                    pv->frame();  // looks the materials up
                    if (pv->materialsPending() > 0) return false;
                }
            return true;
        }, 180000);
        report << QString("  materials %1 after %2 s").arg(loaded ? "ready" : "STILL LOADING").arg(wait.elapsed() / 1000.0, 0, 'f', 1);
        const double at = shots.front()->busiestTime();
        for (const auto& pv : shots) {
            pv->setTime(at);
            pump(30);
            const QImage f = pv->frame();
            const QString out = QString("%1_%2_%3.png").arg(leaf, pv->discStyle() ? "old" : "new").arg(pv->width());
            pv->grab().save(QDir(outDir).filePath(out));
            if (!pv->discStyle()) {
                const QRect vr = pv->viewRect();
                report << QString("  %1: widget %2 x %3, square %4 x %5, t %6 s, %7 particles, %8 drawn with images, %9 ms; missing: %10")
                              .arg(out).arg(pv->width()).arg(pv->height()).arg(vr.width()).arg(vr.height()).arg(at, 0, 'f', 2)
                              .arg(pv->particlesNow()).arg(pv->texturedParticles()).arg(pv->lastRenderMs(), 0, 'f', 1)
                              .arg(pv->missingMaterials().isEmpty() ? QString("none") : pv->missingMaterials().join("; "));
                if (vr.width() != vr.height()) ++failed;
                if (pv->width() == 300)
                    for (const QString& l : pv->debugLines()) report << "    " + l;
            }
        }
        // Four moments of the run at 300 px.
        {
            VfxPreview* pv = shots[1].get();
            const double len = def.duration >= 0 ? def.duration : 4.0;
            const double base = def.duration >= 0 ? 0.0 : at - 2.0;
            QImage strip(4 * pv->width() + 3 * 4, pv->height(), QImage::Format_RGB32);
            strip.fill(QColor(10, 10, 12));
            QPainter g(&strip);
            const double fr[4] = {0.1, 0.3, 0.55, 0.8};
            for (int i = 0; i < 4; ++i) {
                pv->setTime(base + len * fr[i]);
                pump(20);
                g.drawImage(i * (pv->width() + 4), 0, pv->grab().toImage());
            }
            g.end();
            strip.save(QDir(outDir).filePath(leaf + "_strip.png"));
        }
        flush();
    }
    // The Effects panel itself with an effect selected (the side bar's column as it is laid out).
    if (AssetBrowser* eb = w.effectsBrowser()) {
        const QString name = "vfx/core/expl/spark_death";
        w.showDock("Effects");
        eb->setFilter(name);
        pump(300);
        for (int r = 0; r < eb->visibleCount(); ++r)
            if (eb->visibleName(r) == name) {
                eb->selectRow(r);
                break;
            }
        VfxPreview* pv = eb->effectPreview();
        waitUntil([&] { return pv && pv->isVisible() && pv->materialsPending() == 0; }, 120000);
        if (pv && pv->isVisible()) {
            pv->setTime(pv->busiestTime());
            pump(300);
            eb->grab().save(QDir(outDir).filePath("effects_panel.png"));
            const QRect vr = pv->viewRect();
            report << QString("== Effects panel: preview %1 x %2, square %3 x %4, %5 drawn with images")
                          .arg(pv->width()).arg(pv->height()).arg(vr.width()).arg(vr.height()).arg(pv->texturedParticles());
        } else {
            report << "== Effects panel: the preview did not show";
            ++failed;
        }
        eb->setFilter("");
    }
    report << QString("%1 failure(s)").arg(failed);
    flush();
    return failed ? 2 : 0;
}

}  // namespace iw8r::qt
