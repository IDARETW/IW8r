// IW8 Radiant - View > Umbra Visibility (plan M2.2): what the stock map's occlusion tome lets the game draw from
// the camera. The tome (core umbra_tome, research 110) is cut out of the map's cached world export in a worker; a
// view query (core umbra_view) follows the camera in a worker too, one at a time. "Cull by Tome" hands its result to
// the renderer (surfaces and props the game would not draw from here are hidden); "Show Cells and Portals" draws the
// camera's cell, its portals and the visible cells around it. The status bar shows the counts.
#include "main_window.h"

#include <cmath>
#include <thread>

#include <QDirIterator>
#include <QLabel>
#include <QStatusBar>
#include <QTimer>

#include "camera_view.h"
#include "settings.h"
#include "umbra_view.h"
#include "worker_guard.h"
#include "world_cache.h"

namespace iw8r::qt {

void MainWindow::setUmbraView(bool cull, bool overlay) {
    umbraCull_ = cull;
    umbraOverlay_ = overlay;
    umbraPose_.clear();
    if (QAction* a = action("UmbraCullByTome")) a->setChecked(cull);
    if (QAction* a = action("UmbraShowCells")) a->setChecked(overlay);
    Settings::instance().setValue("view/umbraCull", cull);
    Settings::instance().setValue("view/umbraOverlay", overlay);
    updateUmbraView();
}

void MainWindow::clearUmbraView(const QString& text) {
    camera_->setTomeVisibility({}, {});
    if (!ctx_.scene.tomeLines.empty()) {
        ctx_.scene.tomeLines.clear();
        ++ctx_.scene.tomeLinesVersion;
        ctx_.cameraMoved();
    }
    if (umbraLabel_) {
        umbraLabel_->setText(text);
        umbraLabel_->setVisible(!text.isEmpty());
    }
}

void MainWindow::updateUmbraView() {
    if (!umbraLabel_) {
        umbraLabel_ = new QLabel(this);
        umbraLabel_->setToolTip("View > Umbra Visibility: what the map's occlusion data lets the game draw from the camera");
        statusBar()->addPermanentWidget(umbraLabel_);
        umbraLabel_->hide();
    }
    if (!umbraCull_ && !umbraOverlay_) return clearUmbraView({});
    if (referenceZone_.isEmpty()) return clearUmbraView("Umbra: no stock map reference loaded");
    // The tome of the reference zone.
    if (umbraZone_ != referenceZone_ || !umbraTome_) {
        if (umbraLoading_) return;
        const std::optional<QString> dir = worldCache_ ? worldCache_->lookup(referenceZone_) : std::nullopt;
        QString json;
        if (dir) {
            QDirIterator it(*dir + "/assets/gfx_map", {"*.json"}, QDir::Files, QDirIterator::Subdirectories);
            if (it.hasNext()) json = it.next();
        }
        if (json.isEmpty()) return clearUmbraView("Umbra: the map's world export is not cached yet");
        umbraTome_.reset();
        umbraIndex_.reset();
        umbraLoading_ = true;
        clearUmbraView("Umbra: reading the tome of " + referenceZone_ + "...");
        const QString zone = referenceZone_;
        std::thread(guardedWorker(__FILE__, __LINE__, [this, zone, json] {
            auto tome = std::make_shared<UmbraTome>();
            std::vector<uint8_t> bytes;
            std::string err;
            const bool ok = extractUmbraTomeFromGfxMapJson(json.toStdString(), bytes, &err) && tome->parse(std::move(bytes), &err);
            auto index = ok ? std::make_shared<UmbraTomeIndex>(umbraTomeIndex(*tome)) : nullptr;
            QMetaObject::invokeMethod(this, [this, zone, ok, tome, index, err] {
                umbraLoading_ = false;
                if (!ok) {
                    umbraZone_ = zone;  // not asked again until the reference changes
                    umbraFailed_ = true;
                    clearUmbraView("Umbra: no tome in " + zone + (err.empty() ? QString() : " (" + QString::fromStdString(err) + ")"));
                    return;
                }
                umbraFailed_ = false;
                umbraZone_ = zone;
                umbraTome_ = tome;
                umbraIndex_ = index;
                umbraPose_.clear();
                updateUmbraView();
            }, Qt::QueuedConnection);
        })).detach();
        return;
    }
    if (umbraFailed_) return;
    // One view query for the camera as it is now; none when nothing changed since the last one (showing a result
    // repaints the camera, which reports a camera change).
    const iw8r::app::Camera cam = ctx_.cam;
    const QString pose = QString("%1 %2 %3 %4 %5 %6 %7 %8 %9").arg(cam.pos.x).arg(cam.pos.y).arg(cam.pos.z).arg(cam.yaw).arg(cam.pitch)
                             .arg(cam.fov).arg(camera_->width() * 10000 + camera_->height()).arg(umbraCull_).arg(umbraOverlay_);
    if (pose == umbraPose_ && !umbraQueryPending_) return;
    if (umbraQueryRunning_) {
        umbraQueryPending_ = true;
        return;
    }
    umbraPose_ = pose;
    const double aspect = double(std::max(1, camera_->width())) / double(std::max(1, camera_->height()));
    const double tanX = std::tan(cam.fov * 3.14159265358979 / 360.0);
    const double tanY = tanX / aspect;
    umbraQueryRunning_ = true;
    umbraQueryPending_ = false;
    const auto tome = umbraTome_;
    const auto index = umbraIndex_;
    const bool overlay = umbraOverlay_;
    std::thread(guardedWorker(__FILE__, __LINE__, [this, tome, index, cam, tanX, tanY, overlay] {
        // Occlusion only. The objects' distance windows (m_objDistances; their use is INFERRED in research 110) are not
        // applied: with them Shipment's far container stacks vanished from a spot where the uncut view, and the game,
        // show them. Without them the view can only show too much, never hide what the game draws.
        // IW8R_UMBRA_DIST=1 turns them on for a comparison.
        UmbraQueryOptions qo;
        qo.distanceCull = qEnvironmentVariableIntValue("IW8R_UMBRA_DIST") > 0;
        auto res = std::make_shared<UmbraViewResult>(umbraViewVisibility(*tome, *index, cam.pos, cam.forward(), cam.up(), tanX, tanY, qo));
        auto lines = std::make_shared<std::vector<UmbraLine>>();
        if (overlay) *lines = umbraOverlayLines(*tome, *res, cam.pos, 3000.0);
        QMetaObject::invokeMethod(this, [this, tome, index, res, lines] {
            umbraQueryRunning_ = false;
            if (tome == umbraTome_ && (umbraCull_ || umbraOverlay_)) {
                if (umbraCull_ && res->culls)
                    camera_->setTomeVisibility(res->surfaceBits, res->modelBits, res->lightBits, res->probeBits, res->decalBits);
                else camera_->setTomeVisibility({}, {});
                ctx_.scene.tomeLines = umbraOverlay_ ? std::move(*lines) : std::vector<UmbraLine>();
                ++ctx_.scene.tomeLinesVersion;
                const auto& v = res->visibleByClass;
                const auto& t = index->idsByClass;
                umbraLast_ = *res;
                umbraLast_.surfaceBits.clear(), umbraLast_.modelBits.clear(), umbraLast_.visibleCells.clear();
                umbraLast_.lightBits.clear(), umbraLast_.probeBits.clear(), umbraLast_.decalBits.clear();
                QString text;
                if (res->culls)
                    text = QString("Umbra: %1 / %2 surfaces, %3 / %4 models, %5 / %6 lights, %7 / %8 probes, %9 / %10 decal groups, %11 cells (%12 ms)%13")
                               .arg(v[kUmbraSurface]).arg(t[kUmbraSurface]).arg(v[kUmbraStaticModel]).arg(t[kUmbraStaticModel])
                               .arg(v[kUmbraPrimaryLight]).arg(t[kUmbraPrimaryLight]).arg(v[kUmbraReflectionProbe]).arg(t[kUmbraReflectionProbe])
                               .arg(v[kUmbraDecalCollection]).arg(t[kUmbraDecalCollection])
                               .arg(res->cells).arg(int(std::lround(res->seconds * 1000)))
                               .arg(res->truncated ? " (query cut short)" : "");
                else if (res->status == UmbraQueryStatus::OutsideTome || res->status == UmbraQueryStatus::NoTile)
                    // Research 121: the game's query fails here; it draws the whole static map and no moving object.
                    text = QString("Umbra: camera %1: the game draws the whole map from here, and no players or vehicles")
                               .arg(umbraStatusText(res->status));
                else
                    text = QString("Umbra: camera %1: nothing is culled here").arg(umbraStatusText(res->status));
                umbraLabel_->setText(text);
                umbraLabel_->show();
                ctx_.cameraMoved();
            }
            if (umbraQueryPending_) updateUmbraView();
        }, Qt::QueuedConnection);
    })).detach();
}

}  // namespace iw8r::qt
