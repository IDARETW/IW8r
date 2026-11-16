#include "thumb_bench.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QListView>
#include <QScrollBar>
#include <QStringList>
#include <QThread>

#include <algorithm>
#include <cstdio>
#include <functional>

#include "asset_browser.h"
#include "main_window.h"
#include "prefab_browser.h"
#include "textured_thumbs.h"

namespace iw8r::qt {

namespace {

struct Panel {
    QString name;
    std::function<VisibleThumbs()> state;
    std::function<QScrollBar*()> bar;
};

void pump(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
}

// Waits until no visible tile waits for its picture (or `timeoutMs`): the placeholders on the first frame, the time
// until the last one was filled and how many still wait at the end.
QString settle(const Panel& p, int timeoutMs) {
    QElapsedTimer t;
    t.start();
    QApplication::processEvents();  // the first frame after the change
    const VisibleThumbs first = p.state();
    VisibleThumbs s = first;
    qint64 filledAt = s.waiting == 0 ? 0 : -1;
    while (t.elapsed() < timeoutMs) {
        QApplication::processEvents(QEventLoop::AllEvents, 5);
        s = p.state();
        if (s.waiting == 0 && s.visible > 0) {
            filledAt = t.elapsed();
            break;
        }
        QThread::msleep(2);
    }
    return QString("%1 visible, %2 waiting on the first frame; %3; %4 real, %5 waiting at the end")
        .arg(first.visible)
        .arg(first.waiting)
        .arg(filledAt >= 0 ? QString("all filled after %1 ms").arg(filledAt) : QString("NOT filled after %1 ms").arg(timeoutMs))
        .arg(s.real)
        .arg(s.waiting);
}

}  // namespace

int runThumbBench(MainWindow& w, const QString& outDir) {
    QStringList out;
    QElapsedTimer total;
    total.start();
    w.waitForCatalogs();
    out << QString("catalogues read after %1 ms").arg(total.elapsed());
    pump(1500);  // the start-up's own work settles (pack indexes, cache indexes)
    std::vector<Panel> panels;
    for (AssetBrowser* b : {w.materialBrowser(), w.modelsBrowser(), w.imagesBrowser()}) {
        if (!b) continue;
        const QString name = b == w.materialBrowser() ? "Materials" : b == w.modelsBrowser() ? "Models" : "Images";
        panels.push_back({name, [b] { return b->visibleThumbState(); }, [b] { return b->scrollBar(); }});
    }
    if (PrefabBrowser* pb = w.prefabBrowser())
        panels.push_back({"Prefabs", [pb] { return pb->visibleThumbState(); }, [pb] { return pb->scrollBar(); }});
    // IW8R_THUMB_BENCH_PANELS=Models,Prefabs: only those panels.
    const QStringList only = qEnvironmentVariable("IW8R_THUMB_BENCH_PANELS").split(',', Qt::SkipEmptyParts);
    for (const Panel& p : panels) {
        if (!only.isEmpty() && !only.contains(p.name)) continue;
        QElapsedTimer t;
        t.start();
        w.showDock(p.name);
        out << p.name + " first open: " + settle(p, 15000);
        if (p.name == "Models") {
            TexturedThumbs* tt = w.thumbs3dForTests();
            const QString first = w.modelsBrowser()->visibleName(0);
            out << QString("  textured pack: %1 pictures; first row %2 has one: %3")
                       .arg(tt ? tt->store(TexturedThumbs::Model).count() : -1)
                       .arg(first)
                       .arg(tt && !tt->picture(TexturedThumbs::Model, first).isNull() ? "yes" : "no");
            if (auto* lv = w.modelsBrowser()->findChild<QListView*>()) {
                const QRect r0 = lv->visualRect(lv->model()->index(0, 0)), r1 = lv->visualRect(lv->model()->index(1, 0));
                const QModelIndex hit = lv->indexAt(QPoint(4, 4));
                out << QString("  layout: viewport %1x%2, grid %3x%4, row 0 at %5,%6 %7x%8, row 1 at %9,%10, hit at 4,4: %11")
                           .arg(lv->viewport()->width()).arg(lv->viewport()->height())
                           .arg(lv->gridSize().width()).arg(lv->gridSize().height())
                           .arg(r0.x()).arg(r0.y()).arg(r0.width()).arg(r0.height()).arg(r1.x()).arg(r1.y())
                           .arg(hit.isValid() ? hit.row() : -1);
            }
        }
        QScrollBar* bar = p.bar();
        if (!bar || bar->maximum() <= 0) {
            out << "  (no scroll range)";
            continue;
        }
        for (const double f : {0.1, 0.3, 0.5, 0.7, 0.9}) {
            bar->setValue(int(bar->maximum() * f));
            out << QString("  jump to %1 %: ").arg(int(f * 100)) + settle(p, 15000);
        }
        // A page per frame for two seconds (a fast flick through the list).
        bar->setValue(int(bar->maximum() * 0.2));
        pump(300);
        int frames = 0, framesWaiting = 0;
        long tiles = 0, waitingTiles = 0;
        QElapsedTimer flick;
        flick.start();
        while (flick.elapsed() < 2000 && bar->value() < bar->maximum()) {
            bar->setValue(std::min(bar->maximum(), bar->value() + bar->pageStep()));
            QApplication::processEvents(QEventLoop::AllEvents, 16);
            QThread::msleep(16);
            QApplication::processEvents();
            const VisibleThumbs s = p.state();
            ++frames;
            tiles += s.visible;
            waitingTiles += s.waiting;
            framesWaiting += s.waiting > 0;
        }
        out << QString("  flick (a page per frame, %1 frames): %2 frames showed a waiting tile; %3 of %4 tiles waiting (%5 %)")
                   .arg(frames)
                   .arg(framesWaiting)
                   .arg(waitingTiles)
                   .arg(tiles)
                   .arg(tiles ? 100.0 * waitingTiles / tiles : 0.0, 0, 'f', 1);
        out << "  after the flick: " + settle(p, 15000);
        bar->setValue(0);
        out << QString("  panel total %1 ms").arg(t.elapsed());
    }
    const QString text = out.join('\n') + '\n';
    std::printf("%s", qPrintable(text));
    std::fflush(stdout);
    QDir().mkpath(outDir);
    QFile f(QDir(outDir).filePath("thumb-bench.txt"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) f.write(text.toUtf8());
    return 0;
}

}  // namespace iw8r::qt
