#include "qt_gui_test_gizmo.h"

#include <QApplication>
#include <QComboBox>
#include <QMouseEvent>
#include <QTest>

#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "brush_tools.h"
#include "camera_view.h"
#include "main_window.h"
#include "settings.h"
#include "stock_smodels.h"

namespace iw8r::qt {

namespace {

void mouse(QWidget* w, QEvent::Type t, QPointF pos, Qt::MouseButton b, Qt::MouseButtons held) {
    QMouseEvent ev(t, pos, w->mapToGlobal(pos), b, held, Qt::NoModifier);
    QApplication::sendEvent(w, &ev);
}

// Press at a, move halfway and on to b, release there.
void drag(QWidget* w, QPointF a, QPointF b) {
    mouse(w, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
    mouse(w, QEvent::MouseMove, (a + b) / 2, Qt::NoButton, Qt::LeftButton);
    mouse(w, QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton);
    mouse(w, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
}

void lookAt(EditorContext& ctx, const Vec3& from, const Vec3& at) {
    const Vec3 d = at - from;
    ctx.cam.pos = from;
    ctx.cam.yaw = std::atan2(d.y, d.x) * 180.0 / 3.14159265358979;
    ctx.cam.pitch = std::atan2(d.z, std::hypot(d.x, d.y)) * 180.0 / 3.14159265358979;
    ctx.cameraMoved();
}

Vec3 vec3Key(const Entity* e, const char* key) {
    Vec3 v;
    if (const std::string* s = e ? e->value(key) : nullptr) std::sscanf(s->c_str(), "%lf %lf %lf", &v.x, &v.y, &v.z);
    return v;
}

QString str(const Vec3& v) { return QString("%1 %2 %3").arg(v.x, 0, 'f', 3).arg(v.y, 0, 'f', 3).arg(v.z, 0, 'f', 3); }

double width(const AABB& b, int a) { return b.maxs[a] - b.mins[a]; }

// The line vertices within `within` of c: how far they reach (in all, and across, from c), and their heights.
struct Spread {
    size_t n = 0;
    double maxD = 0, maxH = 0, minZ = 1e18, maxZ = -1e18;
};
Spread spreadAround(const std::vector<app::GpuVertex>& v, const Vec3& c, double within) {
    Spread s;
    for (const app::GpuVertex& g : v) {
        const Vec3 p{g.x, g.y, g.z};
        const double d = length(p - c);
        if (d > within) continue;
        ++s.n;
        s.maxD = std::max(s.maxD, d);
        s.maxH = std::max(s.maxH, std::hypot(p.x - c.x, p.y - c.y));
        s.minZ = std::min(s.minZ, p.z);
        s.maxZ = std::max(s.maxZ, p.z);
    }
    return s;
}

}  // namespace

void runGizmoChecks(std::function<void(const QString&)> section, std::function<void(bool, const QString&)> check, MainWindow& w) {
    EditorContext& ctx = w.context();
    CameraView* cv = w.cameraView();
    section("gizmo mode dropdown (owner 2026-11-15: Move / Rotate / Scale / All)");
    if (!cv) {
        check(false, "the camera view exists");
        return;
    }
    const auto savedCam = ctx.cam;
    const bool savedLocal = ctx.gizmoLocal, savedSnap = ctx.snap, savedGizmo = ctx.gizmo;
    const bool savedLight = ctx.scene.showLightRanges, savedSound = ctx.scene.showSoundRanges;
    const double savedGrid = ctx.doc.grid();
    const CameraView::GizmoMode savedMode = cv->gizmoMode();
    const QVariant savedSetting = Settings::instance().value("view/gizmoMode");
    const size_t up = ctx.doc.undoPosition();
    const std::string before = ctx.doc.text();
    ctx.gizmo = true;
    ctx.snap = true;
    ctx.gizmoLocal = false;
    ctx.doc.setGrid(16);
    ctx.doc.deselectAll();
    ctx.documentEdited();

    auto* box = w.findChild<QComboBox*>("tb_GizmoModeCombo");
    check(box && box->count() == 4 && box->itemText(0) == "Move" && box->itemText(1) == "Rotate" && box->itemText(2) == "Scale" &&
              box->itemText(3) == "All",
          "the main toolbar has the gizmo dropdown: Move, Rotate, Scale, All");
    cv->setGizmoMode(CameraView::GizmoMode::Move);
    if (box) {
        box->setCurrentIndex(3);
        emit box->activated(3);
    }
    QAction* allAct = w.action("GizmoModeAll");
    check(cv->gizmoMode() == CameraView::GizmoMode::All && Settings::instance().value("view/gizmoMode").toInt() == 3 && allAct && allAct->isChecked(),
          "choosing All in the dropdown sets the gizmo, checks View > Gizmo Mode > All and saves the choice");
    QTest::keyClick(&w, Qt::Key_R);
    check(cv->gizmoMode() == CameraView::GizmoMode::Rotate && box && box->currentIndex() == 1, "R switches to Rotate and the dropdown shows it");
    QTest::keyClick(&w, Qt::Key_T);
    check(cv->gizmoMode() == CameraView::GizmoMode::Scale && box && box->currentIndex() == 2, "T switches to Scale and the dropdown shows it");
    if (QAction* mv = w.action("GizmoModeMove")) mv->trigger();
    check(cv->gizmoMode() == CameraView::GizmoMode::Move && box && box->currentIndex() == 0 && Settings::instance().value("view/gizmoMode").toInt() == 0,
          "View > Gizmo Mode > Move goes back (the dropdown and the saved setting follow)");

    section("All gizmo: arrows move, rings rotate, boxes scale (each part on its own)");
    {
        // A 64 x 32 x 32 box far from everything, seen from above.
        AABB a;
        a.add(Vec3{26000, -16, 0});
        a.add(Vec3{26064, 16, 32});
        createBoxBrush(ctx, a);  // selects it
        ctx.documentEdited();
        cv->setGizmoMode(CameraView::GizmoMode::All);
        const Vec3 c0 = ctx.doc.selectionBounds().center();
        lookAt(ctx, c0 + Vec3{-150, -120, 380}, c0);
        cv->renderNow();
        const Vec3 c = cv->gizmoCentre();
        const double L = std::max(8.0, length(c - ctx.cam.pos) * 0.18);
        int part = -1, axis = -1;
        const auto hoverAt = [&](std::optional<QPointF> p) {
            part = axis = -1;
            if (!p) return;
            mouse(cv, QEvent::MouseMove, *p, Qt::NoButton, Qt::NoButton);
            part = cv->gizmoHoverPart(), axis = cv->gizmoHoverAxis();
        };
        hoverAt(cv->gizmoScaleBoxPoint(1));
        const int boxPart = part, boxAxis = axis;
        hoverAt(cv->widgetPointOf(c + Vec3{L * 0.85, 0, 0}));
        const int arrowPart = part, arrowAxis = axis;
        hoverAt(cv->gizmoRingPoint(2, 135));
        const int ringPart = part, ringAxis = axis;
        hoverAt(cv->widgetPointOf(c + Vec3{0, 0, -L * 3}));
        check(boxPart == 2 && boxAxis == 1 && arrowPart == 0 && arrowAxis == 0 && ringPart == 1 && ringAxis == 2,
              QString("the All gizmo tells its parts apart under the mouse (part/axis): Y box %1/%2, X arrow %3/%4, Z ring %5/%6")
                  .arg(boxPart).arg(boxAxis).arg(arrowPart).arg(arrowAxis).arg(ringPart).arg(ringAxis));
        // Scale: the X box dragged to twice its distance from the centre doubles the width.
        const AABB b0 = ctx.doc.selectionBounds();
        size_t u0 = ctx.doc.undoPosition();
        if (const auto p = cv->gizmoScaleBoxPoint(0), q = cv->widgetPointOf(c + Vec3{L * 0.9, 0, 0}); p && q) drag(cv, *p, *q);
        const AABB b1 = ctx.doc.selectionBounds();
        check(std::fabs(width(b1, 0) - 2 * width(b0, 0)) < 0.01 && std::fabs(width(b1, 1) - width(b0, 1)) < 0.01 && ctx.doc.undoPosition() == u0 + 1,
              QString("dragging the X scale box to twice its distance doubles the width (%1 -> %2; Y %3 -> %4), one undo step")
                  .arg(width(b0, 0)).arg(width(b1, 0)).arg(width(b0, 1)).arg(width(b1, 1)));
        while (ctx.doc.undoPosition() > u0 && ctx.doc.undo()) {}
        ctx.documentEdited();
        cv->renderNow();
        // Move: the Y arrow dragged 44 units lands the box on the grid along Y only.
        u0 = ctx.doc.undoPosition();
        const Vec3 cm = cv->gizmoCentre();
        if (const auto p = cv->widgetPointOf(cm + Vec3{0, L * 0.85, 0}), q = cv->widgetPointOf(cm + Vec3{0, L * 0.85 + 44, 0}); p && q) drag(cv, *p, *q);
        const AABB m1 = ctx.doc.selectionBounds();
        check(m1.mins.y > b0.mins.y && std::fabs(std::remainder(m1.mins.y, 16.0)) < 1e-9 && m1.mins.x == b0.mins.x && m1.mins.z == b0.mins.z &&
                  ctx.doc.undoPosition() == u0 + 1,
              QString("dragging the Y arrow moves the box along Y onto the grid (min y %1 -> %2, x and z kept)").arg(b0.mins.y).arg(m1.mins.y));
        while (ctx.doc.undoPosition() > u0 && ctx.doc.undo()) {}
        ctx.documentEdited();
        cv->renderNow();
        // Rotate: the Z ring dragged a quarter turn swaps the box's width and depth.
        u0 = ctx.doc.undoPosition();
        if (const auto p = cv->gizmoRingPoint(2, 45), q = cv->gizmoRingPoint(2, 135); p && q) drag(cv, *p, *q);
        const AABB r1 = ctx.doc.selectionBounds();
        check(std::fabs(width(r1, 0) - 32) < 0.01 && std::fabs(width(r1, 1) - 64) < 0.01 && ctx.doc.undoPosition() == u0 + 1,
              QString("dragging the Z ring a quarter turn turns the box (%1 x %2 -> %3 x %4)").arg(width(b0, 0)).arg(width(b0, 1)).arg(width(r1, 0)).arg(width(r1, 1)));
        while (ctx.doc.undoPosition() > u0 && ctx.doc.undo()) {}
        ctx.documentEdited();
    }

    section("local space: the gizmo's drags follow the entity's own axes");
    {
        ctx.doc.deselectAll();
        const Vec3 o{26400, 0, 16};
        const Id e = ctx.doc.addEntity({{"classname", "script_origin"}, {"origin", "26400 0 16"}, {"angles", "0 90 0"}}, {}, "t");
        ctx.doc.select({e, kNoId}, false);
        ctx.documentEdited();
        ctx.gizmoLocal = true;
        cv->setGizmoMode(CameraView::GizmoMode::Move);
        lookAt(ctx, o + Vec3{-220, -160, 200}, o);
        cv->renderNow();
        const Vec3 lx = cv->gizmoAxisDir(0);
        double L = std::max(8.0, length(cv->gizmoCentre() - ctx.cam.pos) * 0.18);
        size_t u0 = ctx.doc.undoPosition();
        if (const auto p = cv->widgetPointOf(o + lx * (L * 0.85)), q = cv->widgetPointOf(o + lx * (L * 0.85 + 44)); p && q) drag(cv, *p, *q);
        const Vec3 o1 = vec3Key(ctx.doc.findEntity(e), "origin");
        check(length(lx - Vec3{0, 1, 0}) < 1e-9 && o1.x == o.x && o1.z == o.z && o1.y > 0 && std::fabs(std::remainder(o1.y, 16.0)) < 1e-9,
              "local space: the X arrow of an entity turned 90 degrees moves it along world +Y onto the grid (origin " + str(o1) + ")");
        while (ctx.doc.undoPosition() > u0 && ctx.doc.undo()) {}
        ctx.documentEdited();
        // Rotate about the local X ring (world +Y): the forward axis stays, the up axis turns to +X.
        cv->setGizmoMode(CameraView::GizmoMode::Rotate);
        lookAt(ctx, o + Vec3{0, -320, 0}, o);
        cv->renderNow();
        u0 = ctx.doc.undoPosition();
        if (const auto p = cv->gizmoRingPoint(0, 45), q = cv->gizmoRingPoint(0, 135); p && q) drag(cv, *p, *q);
        {
            const Vec3 ang = vec3Key(ctx.doc.findEntity(e), "angles");
            const auto ax = anglesToAxis(ang);
            const Vec3 o2 = vec3Key(ctx.doc.findEntity(e), "origin");
            check(length(ax[0] - Vec3{0, 1, 0}) < 1e-6 && length(ax[2] - Vec3{1, 0, 0}) < 1e-6 && length(o2 - o) < 1e-9 && ctx.doc.undoPosition() == u0 + 1,
                  "local space: a quarter turn on the entity's own X ring keeps its forward (+Y) and turns its up to +X, in place (angles " +
                      str(ang) + ")");
        }
        while (ctx.doc.undoPosition() > u0 && ctx.doc.undo()) {}
        ctx.documentEdited();
        // World space: the world X ring turns a yawed entity about world X (its forward +Y turns up to +Z).
        ctx.gizmoLocal = false;
        lookAt(ctx, o + Vec3{-320, 0, 0}, o);
        cv->renderNow();
        u0 = ctx.doc.undoPosition();
        if (const auto p = cv->gizmoRingPoint(0, 45), q = cv->gizmoRingPoint(0, 135); p && q) drag(cv, *p, *q);
        {
            const Vec3 ang = vec3Key(ctx.doc.findEntity(e), "angles");
            const auto ax = anglesToAxis(ang);
            check(length(ax[0] - Vec3{0, 0, 1}) < 1e-6 && ctx.doc.undoPosition() == u0 + 1,
                  "world space: a quarter turn on the world X ring turns a yawed entity about world X (forward " + str(ax[0]) + ", angles " + str(ang) + ")");
        }
        while (ctx.doc.undoPosition() > u0 && ctx.doc.undo()) {}
        ctx.documentEdited();
        cv->setGizmoMode(CameraView::GizmoMode::Move);
    }

    section("entity ranges: light radius and spot cone, sound distance (editor lines only)");
    {
        ctx.doc.deselectAll();
        const Vec3 omniAt{27000, 0, 64}, spotAt{27600, 0, 200}, soundAt{29000, 0, 32};
        const Id omni = ctx.doc.addEntity({{"classname", "light"}, {"origin", "27000 0 64"}, {"radius", "200"}, {"_color", "1 0.5 0.25"}}, {}, "t");
        const Id spot = ctx.doc.addEntity(
            {{"classname", "light"}, {"origin", "27600 0 200"}, {"radius", "150"}, {"fov_outer", "60"}, {"fov_inner", "30"}, {"angles", "90 0 0"}}, {}, "t");
        const Id sound = ctx.doc.addEntity({{"classname", "sound_emitter"}, {"origin", "29000 0 32"}, {"soundalias", "emt_smokestack_lp"}, {"loop", "1"}}, {}, "t");
        ctx.documentEdited();
        ctx.scene.sync(ctx.doc);
        std::vector<app::GpuVertex> lines, over;
        const auto visuals = [&] {
            lines.clear(), over.clear();
            ctx.scene.sync(ctx.doc);
            app::appendEntityVisuals(ctx.scene, ctx.doc, lines, over);
        };
        ctx.scene.showLightRanges = true;
        visuals();
        const Spread s1 = spreadAround(lines, omniAt, 260);
        check(s1.n > 0 && std::fabs(s1.maxD - 200) < 0.5,
              QString("Light Ranges on: an unselected light shows its radius (depth-tested lines reach %1 from it; radius 200)").arg(s1.maxD));
        ctx.scene.showLightRanges = false;
        visuals();
        const Spread s2 = spreadAround(lines, omniAt, 260);
        check(s2.maxD < 150, QString("Light Ranges off: no radius on an unselected light (lines reach %1)").arg(s2.maxD));
        ctx.doc.select({omni, kNoId}, false);
        visuals();
        const Spread s3 = spreadAround(over, omniAt, 260);
        check(s3.n > 0 && std::fabs(s3.maxD - 200) < 0.5, QString("the selected light shows its radius on top either way (reach %1)").arg(s3.maxD));
        ctx.doc.select({spot, kNoId}, false);
        visuals();
        const Spread s4 = spreadAround(over, spotAt, 200);
        check(s4.n > 0 && std::fabs(s4.minZ - 50) < 0.5 && s4.maxZ < 200.5 && std::fabs(s4.maxD - 150) < 0.5 && std::fabs(s4.maxH - 75) < 0.5,
              QString("a selected spot (angles 90 0 0, fov_outer 60, radius 150) shows its cone pointing down: z %1..%2, reach %3, across %4")
                  .arg(s4.minZ).arg(s4.maxZ).arg(s4.maxD).arg(s4.maxH));
        ctx.doc.select({sound, kNoId}, false);
        visuals();
        const double dist = ctx.scene.soundRangeFor ? ctx.scene.soundRangeFor("emt_smokestack_lp") : 0.0;
        const Spread s5 = spreadAround(over, soundAt, 1400);
        check(dist == 1300 && std::fabs(s5.maxD - 1300) < 0.5,
              QString("a selected sound_emitter shows its audible distance (catalogue distMax %1, reach %2)").arg(dist).arg(s5.maxD));
        ctx.scene.captureMode = true;
        visuals();
        ctx.scene.captureMode = false;
        check(lines.empty() && over.empty(), "nothing of it in a probe capture");
        ctx.scene.showLightRanges = true;
        ctx.doc.select({spot, kNoId}, false);
        ctx.documentEdited();
        lookAt(ctx, spotAt + Vec3{-300, -300, 100}, spotAt);
        cv->renderNow();
        const QString self = cv->geometrySelfCheck();
        check(self.isEmpty(), "the camera's kept geometry with the range shapes matches a fresh build" + (self.isEmpty() ? QString() : ": " + self));
    }

    ctx.doc.deselectAll();
    while (ctx.doc.undoPosition() > up && ctx.doc.undo()) {}
    ctx.documentEdited();
    check(ctx.doc.text() == before, "undo leaves the map as it was");
    ctx.gizmoLocal = savedLocal, ctx.snap = savedSnap, ctx.gizmo = savedGizmo;
    ctx.scene.showLightRanges = savedLight, ctx.scene.showSoundRanges = savedSound;
    ctx.doc.setGrid(savedGrid);
    cv->setGizmoMode(savedMode);
    if (savedSetting.isValid()) Settings::instance().setValue("view/gizmoMode", savedSetting);
    ctx.cam = savedCam;
    ctx.cameraMoved();
}

}  // namespace iw8r::qt
