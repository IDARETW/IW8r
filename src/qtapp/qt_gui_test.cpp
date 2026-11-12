#include <DockWidget.h>
#include "qt_gui_test_assets_b.h"
#include "qt_gui_test_assets_a.h"
#include "material_cache.h"
#include "material_clone.h"
#include "cache_manager.h"
#include "fx_editor.h"
#include "cache_trim.h"
#include "uv_editor.h"
#include <QElapsedTimer>
#include <QScrollBar>
#include <QListView>
#include "vfx_preview.h"
#include "port_wizard_dialog.h"
#include "trigger_templates.h"
#include "camera_path.h"
#include "outliner_panel.h"
#include <QMenuBar>
#include "command_palette.h"
#include "script_check.h"
#include "script_panel.h"
#include "asset_info.h"
#include "qt_gui_test.h"

#include <QAbstractItemView>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QPushButton>
#include <QToolButton>
#include <QApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QRegularExpression>
#include <atomic>
#include <thread>
#include <QDialog>
#include <QLabel>
#include <QTimer>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTableWidget>
#include <QTest>
#include <cmath>

#include "asset_browser.h"
#include "atmosphere_dialog.h"
#include "thumb_store.h"
#include "game_modes.h"
#include "map_gametypes.h"
#include "prefab_browser.h"
#include "qt_gui_test_bundles.h"
#include "qt_gui_test_loot.h"
#include "qt_gui_test_prefab_models.h"
#include "model_preview.h"
#include "sun_sky_dialog.h"
#include "brush_flags.h"
#include "brush_ops.h"
#include "brush_tools.h"
#include "iw8_contents.h"
#include "tex_projection.h"
#include "camera_view.h"
#include "compile_panel.h"
#include "find_dialog.h"
#include "live_link_panel.h"
#include "devmap_cover.h"
#include "devmap_launch.h"
#include "tool_commands.h"
#include "web_server.h"
#include <QMenu>
#include <QToolBar>
#include <QSslSocket>
#include "settings.h"
#include <QComboBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "shape_models.h"
#include "patch_tools.h"
#include "material_editor.h"
#include "ies_browser.h"
#include "model_import.h"
#include "budget_panel.h"
#include "help_panel.h"
#include "compile_iw8.h"
#include "lightmap_fields.h"
#include "link_graph_view.h"
#include "autosave_history.h"
#include "layers.h"
#include "layers_panel.h"
#include "replay_log_panel.h"
#include "setup_wizard.h"
#include "editor_log.h"
#include "log_panel.h"
#include "toast_host.h"
#include "grid_view.h"
#include "region.h"
#include "main_window.h"
#include <QStandardPaths>
#include <QTextBrowser>
#include <QClipboard>
#include "panels.h"

#ifdef _MSC_VER
// The scripted GUI test is one long function; optimizing it exhausts the compiler heap (C1060) and buys nothing.
#pragma optimize("", off)
#endif

namespace iw8r::qt {

namespace {

struct Report {
    QString text;
    int checks = 0, failures = 0;
    QString path;  // rewritten after every check so a crash still leaves the progress on disk
    void section(const QString& s) {
        text += "[" + s + "]\n";
        flush();
    }
    void check(bool ok, const QString& what) {
        ++checks;
        if (!ok) ++failures;
        text += QString(ok ? "  ok   " : "  FAIL ") + what + "\n";
        flush();
    }
    void flush() const {
        QFile f(path);
        if (!path.isEmpty() && f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(text.toUtf8());
    }
};

const EntityItem* nthPrim(const Entity& e, size_t n) {
    size_t k = 0;
    for (const auto& it : e.items)
        if (it.isPrimitive() && k++ == n) return &it;
    return nullptr;
}

void sendMouse(QWidget* w, QEvent::Type t, QPointF pos, Qt::MouseButton b, Qt::MouseButtons held, Qt::KeyboardModifiers m = {}) {
    QMouseEvent ev(t, pos, w->mapToGlobal(pos), b, held, m);
    QApplication::sendEvent(w, &ev);
}

}  // namespace

int runQtGuiTest(MainWindow& w, const QString& samplePath, const QString& outDir) {
    // Real stock scripted objects are on by default for mappers. The checks below cover the off path (left-out
    // objects, the door prefab's fallback contents) and switch the on path in where they test it, so the run does
    // not depend on the saved preference.
    if (qEnvironmentVariableIsEmpty("IW8R_REAL_SCRIPTABLES")) qputenv("IW8R_REAL_SCRIPTABLES", "0");
    QDir().mkpath(outDir);  // progress is flushed after every check; some steps write files here and wait on them
    Report r;
    r.path = QDir(outDir).filePath("gui-test-report.txt");
    EditorContext& ctx = w.context();
    GridView* grid = w.gridView();
    std::string original, edited;

    ctx.doc.textureLock = false;  // a mapper's saved preference must not change the exact-text checks
    r.section("open IW8 sample room");
    w.openPath(samplePath);
    QTest::qWait(50);
    readFile(samplePath.toStdString(), original);
    r.check(!original.empty() && ctx.doc.text() == original, "document opened byte-identical: " + samplePath);
    w.activateWindow();
    r.check(QTest::qWaitForWindowActive(&w, 3000), "main window active (offscreen platform)");
    r.check(ctx.gridAxis == 0, "grid view is XY top");

    r.section("hotkey 5 -> grid 16");
    QTest::keyClick(&w, Qt::Key_5);
    r.check(ctx.doc.grid() == 16, QString("grid size is 16 (got %1)").arg(ctx.doc.grid()));

    r.section("shift+click pillar in grid view");
    const QPointF pillarPt = grid->worldToWidget(24, 24);
    sendMouse(grid, QEvent::MouseButtonPress, pillarPt, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    sendMouse(grid, QEvent::MouseButtonRelease, pillarPt, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    const Entity* ws = ctx.doc.worldspawn();
    const EntityItem* pillar = ws ? nthPrim(*ws, 5) : nullptr;
    const SelRef pillarRef = (ws && pillar) ? SelRef{ws->id, pillar->id} : SelRef{};
    r.check(pillar && ctx.doc.selection().size() == 1 && ctx.doc.isSelected(pillarRef), "pillar brush selected");

    r.section("drag pillar on the grid (+30, -9 -> snapped 32, -16)");
    sendMouse(grid, QEvent::MouseButtonPress, pillarPt, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 6; ++i) sendMouse(grid, QEvent::MouseMove, grid->worldToWidget(24 + 30.0 * i / 6, 24 - 9.0 * i / 6), Qt::NoButton, Qt::LeftButton);
    sendMouse(grid, QEvent::MouseButtonRelease, grid->worldToWidget(54, 15), Qt::LeftButton, Qt::NoButton);
    {
        Document o;
        o.loadFromText(original, "", nullptr);
        const MapDiff d = diffMaps(o.map(), ctx.doc.map());
        r.check(ctx.doc.history().size() == 1 && ctx.doc.history().back().name == "Move", "one undoable Move transaction");
        r.check(d.changes.size() == 1 && d.changes[0].kind == "brush-moved" && d.changes[0].detail == "32 -16 0",
                "pillar moved exactly 32 -16 0 (got " + (d.changes.empty() ? QString("nothing") : QString::fromStdString(d.changes[0].kind + " " + d.changes[0].detail)) + ")");
    }

    r.section("camera view pick (look straight down at the pillar)");
    ctx.doc.deselectAll();
    ctx.cam.pos = Vec3{32, -16, 600};
    ctx.cam.yaw = 0;
    ctx.cam.pitch = -89.9;
    ctx.cameraMoved();
    CameraView* cam = w.cameraView();
    QTest::mouseClick(cam, Qt::LeftButton, Qt::NoModifier, cam->rect().center());
    r.check(ctx.doc.selection().size() == 1 && ctx.doc.isSelected(pillarRef), "LMB in the 3D view picks the pillar");

    r.section("hide selected (H) and show all (Shift+H)");
    {
        const size_t visibleBefore = ctx.scene.items().size();
        QTest::keyClick(&w, Qt::Key_H);
        ctx.scene.sync(ctx.doc);
        r.check(ctx.scene.items().size() == visibleBefore - 1 && ctx.doc.selection().empty(), "H hides the pillar and deselects it");
        QTest::mouseClick(cam, Qt::LeftButton, Qt::NoModifier, cam->rect().center());
        r.check(!ctx.doc.isSelected(pillarRef), "a hidden brush cannot be picked");
        QTest::keyClick(&w, Qt::Key_H, Qt::ShiftModifier);
        ctx.scene.sync(ctx.doc);
        r.check(ctx.scene.items().size() == visibleBefore, "Shift+H shows everything again");
        ctx.doc.select(pillarRef, false);
    }

    r.section("Escape, shift+click allies start spawn");
    QTest::keyClick(&w, Qt::Key_Escape);
    r.check(ctx.doc.selection().empty(), "Escape deselects");
    const QPointF spawnPt = grid->worldToWidget(-192, -192);
    sendMouse(grid, QEvent::MouseButtonPress, spawnPt, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    sendMouse(grid, QEvent::MouseButtonRelease, spawnPt, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    const Entity* insp = ctx.doc.findEntity(ctx.doc.inspectedEntity());
    r.check(insp && insp->classname() == "mp_tdm_spawn_allies_start", "inspector shows mp_tdm_spawn_allies_start");

    r.section("IW8 class picker and stock key suggestions (script_model)");
    {
        QTest::keyClick(&w, Qt::Key_Escape);
        const QPointF rampPt = grid->worldToWidget(160, -96);
        sendMouse(grid, QEvent::MouseButtonPress, rampPt, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
        sendMouse(grid, QEvent::MouseButtonRelease, rampPt, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
        EntityInspector* ins = w.inspector();
        r.check(ins->classBoxText() == "script_model", "class box shows script_model (got " + ins->classBoxText() + ")");
        r.check(ins->classChoices() >= 30, QString("class box offers %1 stock IW8 classes").arg(ins->classChoices()));
        const QStringList keys = ins->suggestedKeys();
        r.check(!keys.isEmpty(), "stock keys suggested for script_model: " + keys.mid(0, 6).join(", "));
        QTest::keyClick(&w, Qt::Key_Escape);
        sendMouse(grid, QEvent::MouseButtonPress, spawnPt, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
        sendMouse(grid, QEvent::MouseButtonRelease, spawnPt, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    }

    r.section("edit angles in the entity inspector");
    QTableWidget* table = w.inspector()->table();
    const int row = w.inspector()->rowForKey("angles");
    r.check(row >= 0, "angles row present");
    if (row >= 0) {
        QTableWidgetItem* item = table->item(row, 1);
        table->scrollToItem(item);
        QTest::mouseDClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, table->visualItemRect(item).center());
        QTest::qWait(20);
        QLineEdit* editor = table->viewport()->findChild<QLineEdit*>();
        if (!editor) {
            table->editItem(item);
            editor = table->viewport()->findChild<QLineEdit*>();
        }
        r.check(editor != nullptr, "cell editor opened by double-click");
        if (editor) {
            editor->selectAll();
            QTest::keyClicks(editor, "0 90 0");
            QTest::keyClick(editor, Qt::Key_Return);
            QTest::qWait(20);
        }
    }
    const Entity* spawn = ctx.doc.findEntity(ctx.doc.inspectedEntity());
    const std::string* angles = spawn ? spawn->value("angles") : nullptr;
    r.check(angles && *angles == "0 90 0", "angles is \"0 90 0\" (got \"" + (angles ? QString::fromStdString(*angles) : QString("?")) + "\")");
    r.check(ctx.doc.history().size() == 2, QString("two transactions in history (got %1)").arg(ctx.doc.history().size()));
    edited = ctx.doc.text();

    r.section("Ctrl+Z x2, Ctrl+Y x2");
    grid->setFocus();
    QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
    QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
    r.check(ctx.doc.text() == original, "after Ctrl+Z x2 the text is byte-identical to the file on disk");
    QTest::keyClick(&w, Qt::Key_Y, Qt::ControlModifier);
    QTest::keyClick(&w, Qt::Key_Y, Qt::ControlModifier);
    r.check(ctx.doc.text() == edited, "after Ctrl+Y x2 the edits are back");

    r.section("save a copy and reopen it");
    const QString out = QDir(outDir).filePath("iw8_sample_room_gui_edit.map");
    QString err;
    r.check(w.saveTo(out, &err), "saved " + out + (err.isEmpty() ? "" : " (" + err + ")"));
    r.check(!ctx.doc.modified(), "document is clean after save");
    w.openPath(out);
    r.check(ctx.doc.text() == edited, "reopened copy matches the edited document byte for byte");
    {
        Document o;
        o.loadFromText(original, "", nullptr);
        const MapDiff d = diffMaps(o.map(), ctx.doc.map());
        bool moved = false, rotated = false;
        for (const auto& c : d.changes) {
            moved |= c.kind == "brush-moved" && c.detail == "32 -16 0";
            rotated |= c.kind == "kv-changed" && c.detail == "\"angles\": \"0 45 0\" -> \"0 90 0\"";
        }
        size_t items = 0;
        for (const auto& e : o.map().entities) items += e.items.size();
        r.check(d.changes.size() == 2 && moved && rotated, "exactly two changes: brush moved 32 -16 0 and angles 0 45 0 -> 0 90 0");
        r.check(d.identicalItems + 2 == items, QString("every other item is byte-identical (%1 of %2)").arg(d.identicalItems).arg(items));
        const std::string& t = ctx.doc.text();
        r.check(t.find("layer \"000_Global/Floors\"") != std::string::npos && t.find(" contents detail;") != std::string::npos &&
                    t.find("toolFlags splitGeo;") != std::string::npos && t.find("\"editor_note\" \"unknown keys must survive\"") != std::string::npos,
                "unknown fields (layer, contents, toolFlags, editor_note) survive");
    }

    r.section("brush tools: create, resize, clip, hollow, material, undo");
    {
        const std::string before = ctx.doc.text();
        const size_t history = ctx.doc.history().size();
        auto worldPrims = [&] {
            size_t n = 0;
            for (const auto& it : ctx.doc.worldspawn()->items) n += it.isPrimitive();
            return n;
        };
        const size_t prims0 = worldPrims();
        QTest::keyClick(&w, Qt::Key_Escape);
        QTest::keyClick(&w, Qt::Key_6);  // grid 32
        // Create: LMB drag on empty space (outside the room) with nothing selected.
        sendMouse(grid, QEvent::MouseButtonPress, grid->worldToWidget(320, 320), Qt::LeftButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseMove, grid->worldToWidget(408, 376), Qt::NoButton, Qt::LeftButton);  // not on a half-grid line: pixel rounding must not decide the snap
        sendMouse(grid, QEvent::MouseButtonRelease, grid->worldToWidget(416, 384), Qt::LeftButton, Qt::NoButton);
        r.check(worldPrims() == prims0 + 1 && ctx.doc.selection().size() == 1, "LMB drag on empty grid creates one brush and selects it");
        const AABB created = ctx.doc.selectionBounds();
        r.text += QString("  info created %1..%2 x %3..%4, grid %5, grid view %6x%7, scale %8\n")
                      .arg(created.mins.x).arg(created.maxs.x).arg(created.mins.y).arg(created.maxs.y)
                      .arg(ctx.doc.grid()).arg(grid->width()).arg(grid->height()).arg(ctx.gridScale);
        r.check(created.mins.x == 320 && created.maxs.x == 416 && created.mins.y == 320 && created.maxs.y == 384,
                "new brush snapped to grid 32: 320..416 x 320..384");
        // Resize: drag the +X side outward by 64.
        sendMouse(grid, QEvent::MouseButtonPress, grid->worldToWidget(440, 352), Qt::LeftButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseMove, grid->worldToWidget(504, 352), Qt::NoButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseButtonRelease, grid->worldToWidget(504, 352), Qt::LeftButton, Qt::NoButton);
        const AABB resized = ctx.doc.selectionBounds();
        r.check(resized.maxs.x == 480 && resized.mins.x == 320 && resized.maxs.y == 384, "dragging outside the brush moved its +X face to 480");
        // Clip: X, two points along x = 384, Enter.
        QTest::keyClick(&w, Qt::Key_X);
        r.check(ctx.clipMode, "X turns the clipper on");
        sendMouse(grid, QEvent::MouseButtonPress, grid->worldToWidget(384, 300), Qt::LeftButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseButtonRelease, grid->worldToWidget(384, 300), Qt::LeftButton, Qt::NoButton);
        sendMouse(grid, QEvent::MouseButtonPress, grid->worldToWidget(384, 400), Qt::LeftButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseButtonRelease, grid->worldToWidget(384, 400), Qt::LeftButton, Qt::NoButton);
        const size_t clipPointsPlaced = ctx.clipPoints.size();
        const bool clipEnabled = w.action("ClipSelected")->isEnabled();
        QTest::keyClick(&w, Qt::Key_Return);
        const AABB clipped = ctx.doc.selectionBounds();
        r.text += QString("  info clip points %1, ClipSelected enabled %2, bounds x %3..%4\n")
                      .arg(clipPointsPlaced).arg(clipEnabled).arg(clipped.mins.x).arg(clipped.maxs.x);
        r.check(!ctx.clipMode && worldPrims() == prims0 + 1 &&
                    ((clipped.mins.x == 320 && clipped.maxs.x == 384) || (clipped.mins.x == 384 && clipped.maxs.x == 480)),
                "Enter clips the brush at x = 384 (grid 32) and leaves the clipper");
        // Hollow (wall = grid 16 so a 64-deep brush keeps an interior) and material.
        QTest::keyClick(&w, Qt::Key_5);
        w.action("CSGHollow")->trigger();
        r.check(worldPrims() == prims0 + 6 || worldPrims() == prims0 + 5 || worldPrims() == prims0 + 4,
                QString("CSG Hollow replaced the brush with walls (%1 primitives added)").arg(worldPrims() - prims0));
        ctx.currentMaterial = "tm2co/me_curb_stone_01";
        QTest::keyClick(&w, Qt::Key_M, Qt::ShiftModifier);
        bool allStone = !ctx.doc.selection().empty();
        for (const auto& sel : ctx.doc.selection())
            if (const EntityItem* it = ctx.doc.findItem(sel.entity, sel.item))
                for (const auto& f : it->prim()->faces) allStone &= f.material() == "tm2co/me_curb_stone_01";
        r.check(allStone, "Shift+M applies the current material to every face of the selection");
        // Every tool step is undoable back to the exact text.
        while (ctx.doc.history().size() > history && ctx.doc.undoPosition() > history) QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        r.check(ctx.doc.text() == before, "Ctrl+Z through create/resize/clip/hollow/material restores the exact text");
        r.check(ctx.doc.history().size() - history == 5, QString("five undo steps recorded (%1)").arg(ctx.doc.history().size() - history));
    }

    r.section("surface panel: texture fields");
    {
        const std::string before = ctx.doc.text();
        const size_t history = ctx.doc.undoPosition();  // earlier sections leave redo entries
        QTest::keyClick(&w, Qt::Key_Escape);
        const EntityItem* pil = nthPrim(*ctx.doc.worldspawn(), 5);  // ids changed when the copy was reopened
        const SelRef pref{ctx.doc.worldspawn()->id, pil ? pil->id : kNoId};
        ctx.doc.select(pref, false);
        emit ctx.documentChanged();
        w.showDock("Surface");  // ADS keeps only the current tab of an area parented
        QTest::qWait(20);
        auto* width = w.findChild<QDoubleSpinBox*>("surface0");
        auto* rot = w.findChild<QDoubleSpinBox*>("surface4");
        auto* xPlus = w.findChild<QToolButton*>("surfacePlus2");
        auto* wHalf = w.findChild<QToolButton*>("surfaceMinus0");
        const EntityItem* p = ctx.doc.findItem(pref.entity, pref.item);
        const auto f0 = p ? p->prim()->textureFields(0) : std::nullopt;
        r.check(width && rot && xPlus && wHalf && width->isEnabled() && f0 && width->value() == (*f0)[0],
                QString("panel shows the pillar's first face (width %1)").arg(width ? width->value() : -1));
        if (!width || !rot || !xPlus || !wHalf) {
            r.check(false, "surface panel widgets not found");
        } else {
        rot->setValue(45);
        p = ctx.doc.findItem(pref.entity, pref.item);
        bool allRot = p != nullptr;
        if (p)
            for (size_t i = 0; i < p->prim()->faces.size(); ++i) allRot &= p->prim()->textureFields(i) && (*p->prim()->textureFields(i))[4] == 45;
        r.check(allRot, "rotation 45 applied to every face of the selection");
        xPlus->click();  // + grid (16)
        wHalf->click();  // width / 2
        p = ctx.doc.findItem(pref.entity, pref.item);
        const auto f1 = p ? p->prim()->textureFields(0) : std::nullopt;
        r.check(f0 && f1 && (*f1)[2] == (*f0)[2] + 16 && (*f1)[0] == (*f0)[0] / 2,
                QString("offset X +16 and width /2 (offset %1, width %2)").arg(f1 ? (*f1)[2] : -1).arg(f1 ? (*f1)[0] : -1));
        r.check(ctx.doc.undoPosition() - history == 3, QString("three Surface undo steps (%1)").arg(ctx.doc.undoPosition() - history));
        for (int undoGuard = 0; undoGuard < 64 && ctx.doc.undoPosition() > history; ++undoGuard) QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        r.check(ctx.doc.text() == before, "Ctrl+Z restores the exact face text");
    }
    }

    r.section("asset browsers: stock IW8 materials and models");
    {
        AssetBrowser* mats = w.materialBrowser();
        r.check(mats->assetCount() > 40000, QString("material catalog loaded: %1 stock materials").arg(mats->assetCount()));
        mats->setFilter("curb_stone");
        r.check(mats->visibleCount() > 0, QString("search 'curb_stone' finds %1").arg(mats->visibleCount()));
        r.check(w.modelBrowser()->assetCount() > 6000 && w.modelBrowser()->assetCount() < 8000, QString("model catalog loaded: %1 placeable stock models (streaming-zone weapons left out)").arg(w.modelBrowser()->assetCount()));
        const std::string before = ctx.doc.text();
        QTest::keyClick(&w, Qt::Key_Escape);
        emit w.modelBrowser()->modelChosen("automotive_truck_loading_ramp_short");
        const Entity* placed = ctx.doc.findEntity(ctx.doc.inspectedEntity());
        r.check(placed && placed->classname() == "misc_model" && placed->value("model") &&
                    *placed->value("model") == "automotive_truck_loading_ramp_short",
                "choosing a model places a static misc_model with that model key");
        QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        r.check(ctx.doc.text() == before, "Ctrl+Z removes the placed model");
    }

    auto selectPillar = [&]() -> SelRef {
        QTest::keyClick(&w, Qt::Key_Escape);
        const EntityItem* pil = nthPrim(*ctx.doc.worldspawn(), 5);
        const SelRef ref{ctx.doc.worldspawn()->id, pil ? pil->id : kNoId};
        ctx.doc.select(ref, false);
        ctx.documentEdited();
        return ref;
    };

    r.section("face selection: Ctrl+Shift+click in 3D, per-face material and texture fields");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        const SelRef pil = selectPillar();
        const AABB pb = ctx.doc.selectionBounds();
        QTest::keyClick(&w, Qt::Key_Escape);
        ctx.cam.pos = Vec3{pb.center().x, pb.center().y, pb.maxs.z + 400};
        ctx.cam.yaw = 0;
        ctx.cam.pitch = -89.9;
        ctx.cameraMoved();
        CameraView* cv = w.cameraView();
        QTest::mouseClick(cv, Qt::LeftButton, Qt::ControlModifier | Qt::ShiftModifier, cv->rect().center());
        const EntityItem* it = ctx.doc.findItem(pil.entity, pil.item);
        const FaceRef* face = ctx.doc.faceSelection().size() == 1 ? &*ctx.doc.faceSelection().begin() : nullptr;
        bool topFace = false;
        if (face && it && face->item == pil.item) {
            const auto pl = facePlane(*it->prim(), face->face);
            topFace = pl && pl->n.z > 0.99;
        }
        r.check(topFace && ctx.doc.selection().empty(), "Ctrl+Shift+click selected the pillar's top face only");
        const std::string otherMat = it ? it->prim()->faces[face ? (face->face + 1) % it->prim()->faces.size() : 0].material() : std::string();
        ctx.currentMaterial = "tm2co/iw8r_face_test";
        QTest::keyClick(&w, Qt::Key_M, Qt::ShiftModifier);
        it = ctx.doc.findItem(pil.entity, pil.item);
        size_t changed = 0;
        if (it)
            for (const auto& f : it->prim()->faces) changed += f.material() == "tm2co/iw8r_face_test";
        r.check(changed == 1 && ctx.doc.faceSelection().size() == 1 && ctx.doc.undoPosition() == pos + 1,
                QString("Shift+M changed exactly one face (%1) and kept the face selection").arg(changed));
        r.check(it && face && it->prim()->faces[(face->face + 1) % it->prim()->faces.size()].material() == otherMat, "the other faces kept their material");
        w.showDock("Surface");
        QTest::qWait(20);
        auto* rot = w.findChild<QDoubleSpinBox*>("surface4");
        if (rot) rot->setValue(30);
        it = ctx.doc.findItem(pil.entity, pil.item);
        size_t rotated = 0;
        if (it)
            for (size_t i = 0; i < it->prim()->faces.size(); ++i) rotated += it->prim()->textureFields(i) && (*it->prim()->textureFields(i))[4] == 30;
        r.check(rot && rotated == 1, QString("the Surface panel edits only the selected face (%1 rotated)").arg(rotated));
        QTest::keyClick(&w, Qt::Key_Escape);
        r.check(ctx.doc.faceSelection().empty(), "Escape clears the face selection");
        for (int undoGuard = 0; undoGuard < 64 && ctx.doc.undoPosition() > pos; ++undoGuard) QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        r.check(ctx.doc.text() == before, "Ctrl+Z restores the faces byte for byte");
    }

    r.section("texture lock (Shift+T) and Surface Fit");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        const SelRef pil = selectPillar();
        QTest::keyClick(&w, Qt::Key_5);  // grid 16
        const Primitive orig = *ctx.doc.findItem(pil.entity, pil.item)->prim();
        QTest::keyClick(&w, Qt::Key_T, Qt::ShiftModifier);
        r.check(ctx.doc.textureLock, "Shift+T turns texture lock on");
        QTest::keyClick(&w, Qt::Key_Right, Qt::AltModifier);
        const Primitive moved = *ctx.doc.findItem(pil.entity, pil.item)->prim();
        // Every face keeps the UV of its material points: uv(old params, p) == uv(new params, p + delta).
        double worst = 0;
        size_t lockedFaces = 0;
        for (size_t f = 0; f < orig.faces.size() && f < moved.faces.size(); ++f) {
            const auto a = texParamsOfFace(orig, f), b = texParamsOfFace(moved, f);
            const auto pl = facePlane(orig, f);
            if (!a || !b || !pl) continue;
            const Vec3 delta = moved.faces[f].p[0] - orig.faces[f].p[0];
            const auto u0 = faceUV(pl->n, *a, orig.faces[f].p[1]);
            const auto u1 = faceUV(pl->n, *b, orig.faces[f].p[1] + delta);
            worst = std::max(worst, std::max(std::fabs(u0[0] - u1[0]), std::fabs(u0[1] - u1[1])));
            ++lockedFaces;
        }
        r.check(lockedFaces == orig.faces.size() && worst < 1e-4 && moved.text() != orig.text(),
                QString("Alt+Right with texture lock keeps every face's UVs (max error %1)").arg(worst));
        QTest::keyClick(&w, Qt::Key_T, Qt::ShiftModifier);
        r.check(!ctx.doc.textureLock, "Shift+T turns texture lock off again");
        auto* fit = w.findChild<QToolButton*>("surfaceOpFit");
        w.showDock("Surface");
        QTest::qWait(20);
        fit = w.findChild<QToolButton*>("surfaceOpFit");
        if (fit) fit->click();
        const Primitive fitted = *ctx.doc.findItem(pil.entity, pil.item)->prim();
        bool spansOnce = fit != nullptr;
        const auto wins = faceWindingsOf(fitted);
        for (size_t f = 0; f < fitted.faces.size(); ++f) {
            const auto tp = texParamsOfFace(fitted, f);
            const auto pl = facePlane(fitted, f);
            if (!tp || !pl || wins[f].size() < 3) continue;
            double lo = 1e300, hi = -1e300;
            for (const Vec3& v : wins[f]) {
                const double u = faceUV(pl->n, *tp, v)[0];
                lo = std::min(lo, u);
                hi = std::max(hi, u);
            }
            spansOnce &= std::fabs((hi - lo) - 1.0) < 1e-3;
        }
        r.check(spansOnce && ctx.doc.undoPosition() == pos + 2, "Surface > Fit makes every face's texture span it once (one undo step)");
        for (int undoGuard = 0; undoGuard < 64 && ctx.doc.undoPosition() > pos; ++undoGuard) QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        r.check(ctx.doc.text() == before, "Ctrl+Z restores the text");
    }

    r.section("textured camera view");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        const QString mat = w.materialBrowser()->anyMaterialWithImage();
        r.check(!mat.isEmpty(), "a stock material with an exported colour image exists: " + mat);
        if (!mat.isEmpty()) {
            selectPillar();
            ctx.currentMaterial = mat.toStdString();
            QTest::keyClick(&w, Qt::Key_M, Qt::ShiftModifier);
            ctx.texturedView = true;
            const AABB pb = ctx.doc.selectionBounds();
            QTest::keyClick(&w, Qt::Key_Escape);
            ctx.cam.pos = Vec3{pb.center().x - 200, pb.center().y - 200, pb.maxs.z + 150};
            ctx.cam.yaw = 45;
            ctx.cam.pitch = -25;
            ctx.cameraMoved();
            CameraView* cv = w.cameraView();
            for (int i = 0; i < 4; ++i) cv->renderAndCount();  // textures load a few per frame
            r.check(cv->loadedTextures() >= 1 && cv->texturedTriangles() >= 12,
                    QString("the pillar draws textured (%1 textures, %2 textured triangles)").arg(cv->loadedTextures()).arg(cv->texturedTriangles()));
            ctx.texturedView = false;
            cv->renderAndCount();
            r.check(cv->texturedTriangles() == 0, "View > Textured off draws flat colours");
            ctx.texturedView = true;
            for (int undoGuard = 0; undoGuard < 64 && ctx.doc.undoPosition() > pos; ++undoGuard) QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
            r.check(ctx.doc.text() == before, "Ctrl+Z restores the material");
        }
    }

    r.section("select by volume (complete tall) and scale");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        const SelRef pil = selectPillar();
        const AABB pb = ctx.doc.selectionBounds();
        QTest::keyClick(&w, Qt::Key_Escape);
        // A thin template brush around the pillar in the XY view (any height: "tall" ignores depth).
        AABB templ;
        templ.add(Vec3{pb.mins.x - 8, pb.mins.y - 8, 400});
        templ.add(Vec3{pb.maxs.x + 8, pb.maxs.y + 8, 416});
        auto primCount = [&] {
            size_t c = 0;
            for (const auto& it : ctx.doc.worldspawn()->items) c += it.isPrimitive();
            return c;
        };
        const size_t prims0 = primCount();
        createBoxBrush(ctx, templ);
        const size_t withTemplate = primCount();
        const size_t n = selectByTemplate(ctx, SelectMode::CompleteTall);
        r.check(n >= 1 && ctx.doc.isSelected(pil), QString("Select Complete Tall picked the pillar (%1 selected)").arg(n));
        r.check(withTemplate == prims0 + 1 && primCount() == prims0, "the template brush was removed");
        ctx.doc.deselectAll();
        ctx.doc.select(pil, false);
        r.check(scaleSelection(ctx, Vec3{2, 1, 1}), "Scale x2 along X");
        const AABB sb = ctx.doc.selectionBounds();
        r.check(std::fabs((sb.maxs.x - sb.mins.x) - 2 * (pb.maxs.x - pb.mins.x)) < 1e-6 && std::fabs((sb.maxs.y - sb.mins.y) - (pb.maxs.y - pb.mins.y)) < 1e-6,
                QString("the pillar is twice as wide in X (%1 -> %2)").arg(pb.maxs.x - pb.mins.x).arg(sb.maxs.x - sb.mins.x));
        for (int undoGuard = 0; undoGuard < 64 && ctx.doc.undoPosition() > pos; ++undoGuard) QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        r.check(ctx.doc.text() == before, "Ctrl+Z restores the template brush and the pillar");
    }

    r.section("layers: list, hide, move selection, new layer");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        LayersPanel* lp = w.layersPanel();
        r.check(lp && lp->layerNames().contains("000_Global/Floors") && lp->layerNames().contains("000_Global"),
                "the Layers dock lists declared and used layers: " + (lp ? lp->layerNames().join(", ") : QString()));
        if (lp) {
            ctx.scene.sync(ctx.doc);
            const size_t visible = ctx.scene.items().size();
            lp->setLayerVisible("000_Global/Floors", false);
            ctx.scene.sync(ctx.doc);
            r.check(ctx.scene.items().size() + 1 == visible, "hiding 000_Global/Floors hides its brush (the floor)");
            lp->setLayerVisible("000_Global/Floors", true);
            const SelRef pil = selectPillar();
            r.check(lp->moveSelectionToLayer("iw8r test layer") && ctx.doc.undoPosition() == pos + 1, "Move Selection Here declares the layer, one undo step");
            const EntityItem* it = ctx.doc.findItem(pil.entity, pil.item);
            r.check(it && primitiveLayer(*it->prim()) == "iw8r test layer" && ctx.doc.map().header.find("\"iw8r test layer\" flags ") != std::string::npos,
                    "the pillar has the layer line and the header declares it");
            r.check(lp->selectLayer("iw8r test layer") == 1, "Select picks the layer's one primitive");
            for (int undoGuard = 0; undoGuard < 64 && ctx.doc.undoPosition() > pos; ++undoGuard) QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
            r.check(ctx.doc.text() == before, "Ctrl+Z restores the header and the pillar");
        }
    }

    r.section("translate gizmo in the camera view");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        ctx.gizmo = true;
        selectPillar();
        QTest::keyClick(&w, Qt::Key_5);  // grid 16
        const AABB pb = ctx.doc.selectionBounds();
        ctx.cam.pos = Vec3{pb.center().x - 300, pb.center().y - 300, pb.center().z + 200};
        ctx.cam.yaw = 45;
        ctx.cam.pitch = -20;
        ctx.cameraMoved();
        CameraView* cv = w.cameraView();
        cv->renderAndCount();
        const auto gz = cv->gizmoAxisOnScreen(2);  // Z handle
        r.check(cv->gizmoActive() && gz.visible, "the gizmo shows on the selection");
        if (gz.visible) {
            const QPointF grab = gz.from + (gz.to - gz.from) * 0.8;
            const QPointF drop = grab + QPointF(0, -(gz.to - gz.from).manhattanLength() * 0.6 - 30);
            sendMouse(cv, QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton);
            sendMouse(cv, QEvent::MouseMove, drop, Qt::NoButton, Qt::LeftButton);
            sendMouse(cv, QEvent::MouseButtonRelease, drop, Qt::LeftButton, Qt::NoButton);
            const AABB mb = ctx.doc.selectionBounds();
            const double dz = mb.mins.z - pb.mins.z;
            r.check(dz > 0 && std::fmod(dz, 16.0) == 0 && mb.mins.x == pb.mins.x && mb.mins.y == pb.mins.y && ctx.doc.undoPosition() == pos + 1,
                    QString("dragging the Z handle raised the pillar by %1 (grid steps, X/Y unchanged, one undo step)").arg(dz));
        }
        for (int undoGuard = 0; undoGuard < 64 && ctx.doc.undoPosition() > pos; ++undoGuard) QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        r.check(ctx.doc.text() == before, "Ctrl+Z restores the pillar");
    }

    r.section("custom texture and custom model import");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        // Inputs: a 4x4 PNG written here (Qt) and a tiny OBJ cube.
        const QString png = QDir(outDir).filePath("import_src/iw8r_gui_brick.png");
        QDir().mkpath(QFileInfo(png).absolutePath());
        {
            QImage img(4, 4, QImage::Format_RGBA8888);
            img.fill(QColor(180, 60, 40));
            img.save(png);
        }
        const QString obj = QDir(outDir).filePath("import_src/IW8R Gui Cube.obj");
        {
            QFile f(obj);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.write("v -8 -8 -8\nv 8 -8 -8\nv 8 8 -8\nv -8 8 -8\nv -8 -8 8\nv 8 -8 8\nv 8 8 8\nv -8 8 8\n"
                        "f 1 2 3 4\nf 5 8 7 6\nf 1 5 6 2\nf 2 6 7 3\nf 3 7 8 4\nf 4 8 5 1\n");
        }
        QString err;
        r.check(w.importTexture(png, "custom/iw8r_gui_brick", &err) && w.customMaterials() && w.customMaterials()->find("custom/iw8r_gui_brick"),
                "Import Texture creates custom/iw8r_gui_brick in the map's materials folder" + (err.isEmpty() ? QString() : " (" + err + ")"));
        w.materialBrowser()->setFilter("iw8r_gui_brick");
        r.check(w.materialBrowser()->visibleCount() >= 1, "the material browser lists the custom material");
        w.materialBrowser()->setFilter("");
        selectPillar();
        QTest::keyClick(&w, Qt::Key_M, Qt::ShiftModifier);  // current material = the imported one
        const AABB pb = ctx.doc.selectionBounds();
        QTest::keyClick(&w, Qt::Key_Escape);
        ctx.texturedView = true;
        ctx.cam.pos = Vec3{pb.center().x - 200, pb.center().y - 200, pb.maxs.z + 150};
        ctx.cam.yaw = 45;
        ctx.cam.pitch = -25;
        ctx.cameraMoved();
        CameraView* cv = w.cameraView();
        for (int i = 0; i < 4; ++i) cv->renderAndCount();
        r.check(cv->texturedTriangles() >= 12, QString("the pillar draws with the imported PNG (%1 textured triangles)").arg(cv->texturedTriangles()));
        QString placed;
        r.check(w.importModel(obj, &placed, &err) && placed == "iw8r_gui_cube", "Import Model copies the OBJ as iw8r_gui_cube and places a misc_model" +
                                                                                   (err.isEmpty() ? QString() : " (" + err + ")"));
        ctx.scene.sync(ctx.doc);
        const app::PrimGeom* mg = nullptr;
        for (const app::PrimGeom* g : ctx.scene.items())
            if (g->label == "model:iw8r_gui_cube") mg = g;
        r.check(mg && mg->polys.size() == 12, QString("the placed model draws as its mesh (%1 triangles)").arg(mg ? mg->polys.size() : 0));
        {
            // glTF import (4.19): one triangle in a .gltf with an embedded buffer.
            const QString gltf = QDir(outDir).filePath("import_src/iw8r_gui_tri.gltf");
            QFile f(gltf);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.write("{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
                        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
                        "\"buffers\":[{\"byteLength\":44,\"uri\":\"data:application/octet-stream;base64,"
                        "AAAAAAAAAAAAAAAAAACAQQAAAAAAAAAAAAAAAAAAgEEAAAAAAAABAAIAAAA=\"}],"
                        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],"
                        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[16,16,0]},"
                        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}]}");
            f.close();
            QString gplaced, gerr;
            r.check(w.importModel(gltf, &gplaced, &gerr) && gplaced == "iw8r_gui_tri" &&
                        QFileInfo::exists(w.customModelRoots().front() + "/iw8r_gui_tri.gltf"),
                    "Import Model takes a .gltf and places it as a misc_model" + (gerr.isEmpty() ? QString() : " (" + gerr + ")"));
            ctx.scene.sync(ctx.doc);
            const app::PrimGeom* tg = nullptr;
            for (const app::PrimGeom* g : ctx.scene.items())
                if (g->label == "model:iw8r_gui_tri") tg = g;
            r.check(tg && tg->polys.size() == 1, QString("the glTF model draws as its mesh (%1 triangles)").arg(tg ? tg->polys.size() : 0));
            QFile::remove(w.customModelRoots().front() + "/iw8r_gui_tri.gltf");
        }
        for (int undoGuard = 0; undoGuard < 64 && ctx.doc.undoPosition() > pos; ++undoGuard) QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        r.check(ctx.doc.text() == before, "Ctrl+Z removes the model and restores the material");
    }

    r.section("rotate gizmo (R) in the camera view");
    {

        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        ctx.gizmo = true;
        selectPillar();
        const AABB pb = ctx.doc.selectionBounds();
        ctx.cam.pos = Vec3{pb.center().x, pb.center().y, pb.center().z + 500};
        ctx.cam.yaw = 0;
        ctx.cam.pitch = -89.9;  // looking down: the Z ring is a circle on screen
        ctx.cameraMoved();
        CameraView* cv = w.cameraView();
        cv->renderAndCount();
        if (!cv->gizmoRotateMode()) QTest::keyClick(&w, Qt::Key_R);
        const auto a0 = cv->gizmoRingPoint(2, 0), a45 = cv->gizmoRingPoint(2, 45);
        r.check(cv->gizmoRotateMode() && a0 && a45, "R switches the gizmo to rotate rings");
        if (a0 && a45) {
            sendMouse(cv, QEvent::MouseButtonPress, *a0, Qt::LeftButton, Qt::LeftButton);
            sendMouse(cv, QEvent::MouseMove, *a45, Qt::NoButton, Qt::LeftButton);
            const QString ringStatus = EditorLog::instance().entries().isEmpty() ? QString() : EditorLog::instance().entries().last().text;
            sendMouse(cv, QEvent::MouseButtonRelease, *a45, Qt::LeftButton, Qt::NoButton);
            const AABB rb = ctx.doc.selectionBounds();
            const double w0 = pb.maxs.x - pb.mins.x, w1 = rb.maxs.x - rb.mins.x;
            r.check(w1 > w0 * 1.35 && w1 < w0 * 1.45 && ctx.doc.undoPosition() == pos + 1,
                    QString("dragging the Z ring 45 degrees turned the pillar (width %1 -> %2, one undo step) [a0 %3,%4 a45 %5,%6 cam %7x%8 undo %9 status %10]").arg(w0).arg(w1).arg(a0->x()).arg(a0->y()).arg(a45->x()).arg(a45->y()).arg(cv->width()).arg(cv->height()).arg(ctx.doc.undoPosition() - pos).arg(ringStatus));
        }
        QTest::keyClick(&w, Qt::Key_R);  // back to move
        for (int undoGuard = 0; undoGuard < 64 && ctx.doc.undoPosition() > pos; ++undoGuard) QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        r.check(ctx.doc.text() == before && !cv->gizmoRotateMode(), "Ctrl+Z restores the pillar; R returns to move");
    }

    r.section("play preview (P): walk, collide, jump, exit");
    {
        const std::string before = ctx.doc.text();
        CameraView* cv = w.cameraView();
        // Start above the middle of the room floor, facing +X.
        ctx.doc.deselectAll();
        ctx.cam.pos = Vec3{-150, -150, 120};
        ctx.cam.yaw = 0;
        ctx.cam.pitch = 0;
        ctx.cameraMoved();
        w.action("PlayPreview")->trigger();
        const double z0 = cv->player().origin.z;
        r.check(cv->playing() && cv->player().onGround && std::fabs(z0) < 0.5 && !w.action("Undo")->isEnabled(),
                QString("P starts play on the floor (feet z %1); editor shortcuts are paused").arg(z0));
        ctx.heldKeys.insert(Qt::Key_W);
        for (int i = 0; i < 300; ++i) cv->tickPlay(1.0 / 60.0);  // 5 s forward: into the room's +X wall
        ctx.heldKeys.clear();
        const double x = cv->player().origin.x;
        r.check(x > 0 && x < 256 && std::fabs(cv->player().origin.z) < 0.5,
                QString("walking forward stops at the wall (x %1) and stays on the floor").arg(x));
        r.check(std::fabs(ctx.cam.pos.z - (cv->player().origin.z + cv->moveParams().standEye)) < 0.01, "the camera sits at eye height");
        ctx.heldKeys.insert(Qt::Key_Space);
        double apex = 0;
        for (int i = 0; i < 90; ++i) {
            cv->tickPlay(1.0 / 60.0);
            apex = std::max(apex, cv->player().origin.z);
        }
        ctx.heldKeys.clear();
        for (int i = 0; i < 30; ++i) cv->tickPlay(1.0 / 60.0);
        r.check(apex > 30 && cv->player().onGround, QString("Space jumps (apex %1) and lands").arg(apex));
        QTest::keyClick(cv, Qt::Key_Escape);
        r.check(!cv->playing() && w.action("Undo")->isEnabled() && ctx.doc.text() == before, "Esc leaves play; shortcuts return; the map is unchanged");
    }

    r.section("play preview: mantle onto a 40-unit ledge");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        CameraView* cv = w.cameraView();
        AABB box;  // taller than the 39-unit jump apex: only a mantle gets on top
        box.add(Vec3{-60, -220, 0});
        box.add(Vec3{40, -80, 40});
        const std::string mat0 = ctx.currentMaterial;
        ctx.currentMaterial = "tm2co/me_curb_stone_01";  // solid (an earlier section may leave a tool material current)
        const bool made = createBoxBrush(ctx, box);
        ctx.currentMaterial = mat0;
        ctx.doc.deselectAll();
        ctx.cam.pos = Vec3{-150, -150, 120};
        ctx.cam.yaw = 0;
        ctx.cam.pitch = 0;
        ctx.cameraMoved();
        w.action("PlayPreview")->trigger();
        const auto probe = cv->collisionWorld().traceRay(Vec3{-150, -150, 20}, Vec3{100, -150, 20});
        r.check(made && probe.fraction < 1, QString("the ledge box is in the play collision world (made %1, ray fraction %2, solids %3, start %4 %5 %6)")
                                                .arg(made).arg(probe.fraction).arg(cv->collisionWorld().solids().size())
                                                .arg(cv->player().origin.x).arg(cv->player().origin.y).arg(cv->player().origin.z));
        ctx.heldKeys.insert(Qt::Key_W);
        for (int i = 0; i < 40; ++i) cv->tickPlay(1.0 / 60.0);  // walk up to the box face
        ctx.heldKeys.insert(Qt::Key_Space);
        bool sawMantle = false;
        for (int i = 0; i < 120; ++i) {
            cv->tickPlay(1.0 / 60.0);
            sawMantle |= cv->player().mantling;
            if (sawMantle && !cv->player().mantling) break;  // stop before walking off the far side
        }
        ctx.heldKeys.clear();
        for (int i = 0; i < 30; ++i) cv->tickPlay(1.0 / 60.0);
        const Vec3 o = cv->player().origin;
        r.check(sawMantle && std::fabs(o.z - 40) < 1 && o.x > -60 && cv->player().onGround,
                QString("W + Space at the face mantles onto the top (feet %1 %2 %3, material was %4)").arg(o.x).arg(o.y).arg(o.z).arg(QString::fromStdString(mat0)));
        QTest::keyClick(cv, Qt::Key_Escape);
        for (int undoGuard = 0; undoGuard < 64 && ctx.doc.undoPosition() > pos; ++undoGuard) QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        r.check(!cv->playing() && ctx.doc.text() == before, "Esc and Ctrl+Z leave the map as it was");
    }

    r.section("primitives, CSG merge, terrain, rope, drop to floor, stock model mesh");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        auto worldPrimCount = [&] {
            size_t c = 0;
            for (const auto& it : ctx.doc.worldspawn()->items) c += it.isPrimitive();
            return c;
        };
        QTest::keyClick(&w, Qt::Key_Escape);
        const size_t p0 = worldPrimCount();
        w.action("CreateStairs")->trigger();
        r.check(worldPrimCount() == p0 + 8, QString("Primitives > Stairs adds 8 step brushes (%1)").arg(worldPrimCount() - p0));
        QTest::keyClick(&w, Qt::Key_Escape);
        AABB a, b;
        a.add(Vec3{600, 600, 0});
        a.add(Vec3{664, 664, 64});
        b.add(Vec3{664, 600, 0});
        b.add(Vec3{728, 664, 64});
        createBoxBrush(ctx, a);
        const SelRef ra = *ctx.doc.selection().begin();
        createBoxBrush(ctx, b);
        ctx.doc.select(ra, true);
        const size_t beforeMerge = worldPrimCount();
        w.action("CSGMerge")->trigger();
        const AABB mb = ctx.doc.selectionBounds();
        r.check(worldPrimCount() == beforeMerge - 1 && mb.mins.x == 600 && mb.maxs.x == 728, "CSG Merge joins two touching boxes into one");
        QTest::keyClick(&w, Qt::Key_Escape);
        w.action("TerrainCreate")->trigger();
        const std::string flat = ctx.doc.text();
        w.action("TerrainNoise")->trigger();
        r.check(ctx.doc.selection().size() == 1 && ctx.doc.text() != flat, "Create Terrain then Terrain Noise changes the new patch");
        // Rope between the two TDM start spawns; drop a raised spawn back to the floor.
        QTest::keyClick(&w, Qt::Key_Escape);
        std::vector<Id> spawns;
        for (const auto& e : ctx.doc.map().entities)
            if (e.classname().rfind("mp_tdm_spawn", 0) == 0) spawns.push_back(e.id);
        if (spawns.size() >= 2) {
            ctx.doc.select({spawns[0], kNoId}, false);
            ctx.doc.select({spawns[1], kNoId}, true);
            const size_t pr = worldPrimCount();
            w.action("CreateRope")->trigger();
            r.check(worldPrimCount() == pr + 1, "Create Rope adds a rope patch between two spawns");
            const Vec3 o = *ctx.doc.findEntity(spawns[0])->origin();
            ctx.doc.setKeyValue(spawns[0], "origin", formatNumber(o.x) + " " + formatNumber(o.y) + " " + formatNumber(o.z + 40));
            ctx.doc.select({spawns[0], kNoId}, false);
            w.action("DropToFloor")->trigger();
            const double dz = ctx.doc.findEntity(spawns[0])->origin()->z;
            r.check(std::fabs(dz) < 0.01, QString("Drop To Floor puts the raised spawn on the floor top (z %1 from %2)").arg(dz).arg(o.z + 40));
        }
        for (int undoGuard = 0; undoGuard < 64 && ctx.doc.undoPosition() > pos; ++undoGuard) QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        r.check(ctx.doc.text() == before, "Ctrl+Z restores everything");
        // The fixture's stock script_model draws as its real mesh once the model cache has it (cached on this machine).
        ctx.scene.invalidate();
        ctx.scene.sync(ctx.doc);
        const app::PrimGeom* truck = nullptr;
        for (const app::PrimGeom* g : ctx.scene.items())
            if (g->label == "model:automotive_truck_loading_ramp_short") truck = g;
        if (truck) r.check(truck->polys.size() > 1000, QString("the stock truck ramp draws as its mesh (%1 triangles)").arg(truck->polys.size()));
        else r.text += "  info stock model not cached yet (requested from ACTS)\n";
        // 3.18 LOD preview: a placed stock model with 2+ LODs (any one the model cache already holds) switches to the
        // LOD its camera distance selects (fetched once), and past the last LOD it shows as its entity box.
        QString lodModel;
        std::optional<iw8r::XModelInfo> lodDetail;
        if (auto* mc = w.stockModelCache()) {
            QFile idx(mc->cacheRoot() + "/index.json");
            if (idx.open(QIODevice::ReadOnly)) {
                const QJsonObject models = QJsonDocument::fromJson(idx.readAll()).object().value("models").toObject();
                for (auto it = models.begin(); it != models.end() && lodModel.isEmpty(); ++it) {
                    if (it.value().toObject().value("failed").toBool()) continue;
                    if (auto d = mc->loadDetail(it.key()); d && d->lods.size() >= 2 && d->lods[0].distance > 0 && !d->lods[1].surfs.empty()) {
                        lodModel = it.key();
                        lodDetail = d;
                    }
                }
            }
        }
        if (!lodModel.isEmpty()) {
            const std::string lm = lodModel.toStdString();
            const size_t lodUndo = ctx.doc.undoPosition();
            const Id lodId = ctx.doc.addEntity({{"classname", "script_model"}, {"model", lm}, {"origin", "0 0 0"}, {"targetname", "lodtest"}}, {}, "t");
            const iw8r::Entity* le = ctx.doc.findEntity(lodId);
            const iw8r::Vec3 camBefore = ctx.cam.pos;
            const double d1 = (lodDetail->lods[0].distance + lodDetail->lods[1].distance) * 0.5;
            const auto labelOf = [&] {
                ctx.scene.sync(ctx.doc);
                for (const app::PrimGeom* g : ctx.scene.items())
                    if (g->ref.entity == lodId) return QString::fromStdString(g->label);
                return QString();
            };
            w.setLodPreview(true);
            ctx.cam.pos = iw8r::Vec3{d1, 0, 0};
            r.check(le && w.stockLodForTest(*le, lm) == 1,
                    QString("LOD Preview picks LOD 1 of %1 at %2 units (LOD0 to %3)").arg(lodModel).arg(d1).arg(lodDetail->lods[0].distance));
            QString label;
            for (int t = 0; t < 1200; ++t) {  // up to 120 s: one ACTS xmodelsurfs run the first time
                ctx.cameraMoved();
                QTest::qWait(100);
                label = labelOf();
                if (label.endsWith("#lod1") || w.stockModelCache()->lodFailed(lodModel, 1)) break;
            }
            r.check(label.endsWith("#lod1"), "the placement draws its LOD 1 mesh in LOD Preview (" + label + ")");
            const double last = lodDetail->lods.back().distance;
            if (last > 0) {
                ctx.cam.pos = iw8r::Vec3{last * 2 + 100, 0, 0};
                ctx.cameraMoved();
                QTest::qWait(400);
                label = labelOf();
                r.check(le && w.stockLodForTest(*le, lm) == -1 && label == "script_model",
                        "past the last LOD the placement shows as its entity box (" + label + ")");
            }
            // lod_scale 4 pushes the switch out: back to LOD0 at the LOD 1 distance.
            ctx.cam.pos = iw8r::Vec3{d1, 0, 0};
            ctx.doc.setKeyValue(lodId, "lod_scale", "4");
            le = ctx.doc.findEntity(lodId);
            r.check(le && w.stockLodForTest(*le, lm) == 0, "lod_scale 4 keeps LOD0 at that distance");
            if (lodDetail->collLod > 0) {
                // Collision Only camera mode draws the model's collision LOD.
                const auto modeBefore = ctx.scene.cameraMode;
                ctx.scene.cameraMode = app::CameraMode::Collision;
                r.check(le && w.stockLodForTest(*le, lm) == lodDetail->collLod,
                        QString("Collision Only mode picks the collision LOD (%1)").arg(lodDetail->collLod));
                ctx.scene.cameraMode = modeBefore;
            }
            w.setLodPreview(false);
            ctx.cam.pos = camBefore;
            ctx.cameraMoved();
            QTest::qWait(400);
            label = labelOf();
            r.check(label == "model:" + lodModel && le && w.stockLodForTest(*le, lm) == 0, "LOD Preview off: LOD0 again (" + label + ")");
            for (int undoGuard = 0; undoGuard < 8 && ctx.doc.undoPosition() > lodUndo; ++undoGuard) ctx.doc.undo();
        } else {
            r.text += "  info LOD preview not checked (no cached stock model with 2+ LODs and detail)\n";
        }
    }

    r.section("select cycle (Alt+Shift click) in the camera view");
    {
        const SelRef pil = selectPillar();
        const AABB pb = ctx.doc.selectionBounds();
        QTest::keyClick(&w, Qt::Key_Escape);
        ctx.cam.pos = Vec3{pb.center().x, pb.center().y, pb.maxs.z + 400};
        ctx.cam.yaw = 0;
        ctx.cam.pitch = -89.9;
        ctx.cameraMoved();
        CameraView* cv = w.cameraView();
        QTest::mouseClick(cv, Qt::LeftButton, Qt::AltModifier | Qt::ShiftModifier, cv->rect().center());
        const bool first = ctx.doc.isSelected(pil);
        QTest::mouseClick(cv, Qt::LeftButton, Qt::AltModifier | Qt::ShiftModifier, cv->rect().center());
        const bool second = ctx.doc.selection().size() == 1 && !ctx.doc.isSelected(pil);
        r.check(first && second, "Alt+Shift click selects the pillar, then the floor below it");
        QTest::keyClick(&w, Qt::Key_Escape);
    }

    r.section("entity inspector: IW8 definitions, missing keys, light group");
    {
        const size_t undo0 = ctx.doc.undoPosition();
        const std::string text0 = ctx.doc.text();
        EntityInspector* ins = w.inspector();
        // The fixture's mp_tdm_spawn: documented, built into Replay, origin required.
        Id spawnId = kNoId;
        for (const auto& e : ctx.doc.map().entities)
            if (e.classname() == "mp_tdm_spawn") spawnId = e.id;
        ctx.doc.select({spawnId, kNoId}, false);
        ctx.documentEdited();
        r.check(spawnId != kNoId && ctx.doc.inspectedEntity() == spawnId, "the fixture's mp_tdm_spawn is inspected");
        r.check(ins->classInfoText().contains("Team deathmatch spawn point") && ins->classInfoText().contains("built into Replay: yes"),
                "inspector shows the mp_tdm_spawn description and built into Replay: yes");
        r.check(ins->missingKeys().isEmpty() && !ins->findChild<QPushButton*>("addMissing_origin"), "the fixture spawn has no missing required key");
        const int originRow = ins->rowForKey("origin");
        r.check(originRow >= 0 && ins->table()->item(originRow, 0)->toolTip().contains("required") &&
                    ins->table()->item(originRow, 0)->toolTip().contains("default: 0 0 0"),
                "the origin key tooltip gives type, default and required");
        r.check(originRow >= 0 && ins->table()->item(originRow, 1)->background().style() == Qt::NoBrush, "a valid origin value is not tinted");

        // Missing required key: a new spawn without origin, fixed by one Add (one undo step).
        const Id fresh = ctx.doc.addEntity({{"classname", "mp_tdm_spawn"}}, {}, "Create Entity");
        ctx.doc.select({fresh, kNoId}, false);
        ctx.documentEdited();
        r.check(ctx.doc.inspectedEntity() == fresh, "a new mp_tdm_spawn without origin is inspected");
        r.check(ins->missingKeys() == QStringList{"origin"}, "the warning lists origin (got " + ins->missingKeys().join(", ") + ")");
        QPushButton* addOrigin = ins->findChild<QPushButton*>("addMissing_origin");
        r.check(addOrigin != nullptr, "an Add button for origin is shown");
        const size_t historyBefore = ctx.doc.history().size();
        if (addOrigin) addOrigin->click();
        const Entity* fe = ctx.doc.findEntity(fresh);
        const std::string* origin = fe ? fe->value("origin") : nullptr;
        r.check(origin && *origin == "0 0 0" && ctx.doc.history().size() == historyBefore + 1,
                "Add inserts origin \"0 0 0\" in one undo step");
        r.check(ins->missingKeys().isEmpty() && !ins->findChild<QPushButton*>("addMissing_origin"), "the warning clears after Add");

        // Value that does not fit its key type: red tint and a tooltip.
        ctx.doc.select({spawnId, kNoId}, false);
        ctx.doc.setKeyValue(spawnId, "angles", "north east");
        ctx.documentEdited();
        const int anglesRow = ins->rowForKey("angles");
        QTableWidgetItem* bad = anglesRow >= 0 ? ins->table()->item(anglesRow, 1) : nullptr;
        r.check(bad && bad->background().style() != Qt::NoBrush && bad->background().color().red() > bad->background().color().green() &&
                    bad->toolTip().contains("three numbers"),
                "angles \"north east\" is tinted red with a tooltip");
        ctx.doc.setKeyValue(spawnId, "angles", "0 90 0");
        ctx.documentEdited();

        // Light group: colour writes _color (0..1 floats), radius and fov keys write their keys.
        const Id lightId = ctx.doc.addEntity({{"classname", "light"}, {"origin", "0 0 128"}}, {}, "Create Entity");
        ctx.doc.select({lightId, kNoId}, false);
        ctx.documentEdited();
        QGroupBox* group = ins->findChild<QGroupBox*>("lightGroup");
        r.check(group && !group->isHidden(), "the Light group shows for a light");
        r.check(ins->classInfoText().contains("built into Replay: yes"), "a light says built into Replay: yes (primary lights, 5.1)");
        ins->setLightColor(1.0, 0.5, 0.25);
        const std::string* color = ctx.doc.findEntity(lightId)->value("_color");
        r.check(color && *color == "1 0.5 0.25", "the colour writes _color as 0..1 floats (got " + QString::fromStdString(color ? *color : "?") + ")");
        if (QDoubleSpinBox* radius = ins->findChild<QDoubleSpinBox*>("lightRadius")) radius->setValue(600);
        if (QDoubleSpinBox* outer = ins->findChild<QDoubleSpinBox*>("lightFovOuter")) outer->setValue(70);
        if (QDoubleSpinBox* inner = ins->findChild<QDoubleSpinBox*>("lightFovInner")) inner->setValue(35);
        const Entity* le = ctx.doc.findEntity(lightId);
        const std::string* radiusV = le ? le->value("radius") : nullptr;
        const std::string* outerV = le ? le->value("fov_outer") : nullptr;
        const std::string* innerV = le ? le->value("fov_inner") : nullptr;
        r.check(radiusV && *radiusV == "600" && outerV && *outerV == "70" && innerV && *innerV == "35",
                "the light group writes radius 600, fov_outer 70 and fov_inner 35");

        // Undo back to the fixture text.
        while (ctx.doc.undoPosition() > undo0 && ctx.doc.undo()) {}
        ctx.documentEdited();
        r.check(ctx.doc.text() == text0, "Ctrl+Z through the inspector edits restores the fixture text");
    }

    r.section("3-point clipper: a third point tilts the plane, Enter clips along it");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        selectPillar();
        const AABB pb = ctx.doc.selectionBounds();
        const double cx = pb.center().x, cy = pb.center().y;
        QTest::keyClick(&w, Qt::Key_X);
        auto click = [&](double h, double v) {
            const QPointF at = grid->worldToWidget(h, v);
            sendMouse(grid, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
            sendMouse(grid, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
        };
        click(cx, pb.mins.y - 48);
        click(cx, pb.maxs.y + 48);
        const size_t twoPoints = ctx.clipPoints.size();
        click(pb.maxs.x + 48, cy);
        const size_t threePoints = ctx.clipPoints.size();
        const auto tilted = ctx.clipPlane();
        r.check(twoPoints == 2 && threePoints == 3, QString("two clicks, then a third click adds a point (%1, %2)").arg(twoPoints).arg(threePoints));
        r.check(tilted && std::fabs(tilted->n[0]) > 1e-3 && std::fabs(tilted->n[2]) > 1e-3,
                "the third point tilts the clip plane (normal has x and z parts)");
        QTest::keyClick(&w, Qt::Key_Return);
        bool slanted = false;
        size_t brushes = 0;
        for (const auto& it : ctx.doc.worldspawn()->items) {
            const Primitive* prim = it.isPrimitive() ? it.prim() : nullptr;
            if (!prim || prim->kind != PrimKind::Brush) continue;
            ++brushes;
            for (const auto& face : brushFromPrimitive(*prim)) {
                const double ln = length(face.plane.n);
                if (ln > 0 && std::fabs(face.plane.n[0]) / ln > 1e-3 && std::fabs(face.plane.n[2]) / ln > 1e-3) slanted = true;
            }
        }
        r.check(!ctx.clipMode && slanted && brushes > 0, "Enter clips with the tilted plane: the result has a sloped face");
        for (int undoGuard = 0; undoGuard < 64 && ctx.doc.undoPosition() > pos; ++undoGuard) QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        r.check(ctx.doc.text() == before, "Ctrl+Z restores the pillar after the 3-point clip");
    }

    r.section("edge mode: drag a top edge into a wedge, undo, rejected move");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        selectPillar();
        QTest::keyClick(&w, Qt::Key_5);  // grid 16
        grid->setFocus();
        QTest::keyClick(grid, Qt::Key_E);
        r.check(ctx.edgeMode && w.action("ToggleEdgeMode")->isChecked(), "E turns edge mode on");
        r.check(!ctx.vertexMode, "edge mode leaves vertex mode off");
        const AABB pb = ctx.doc.selectionBounds();
        const double W = pb.maxs.x - pb.mins.x, H = pb.maxs.z - pb.mins.z, L = pb.maxs.y - pb.mins.y;
        const double vol0 = selectedBrushes(ctx).empty() ? 0 : brushVolume(selectedBrushes(ctx)[0].brush);
        // The top +X edge: its midpoint is at (maxs.x, centre y), and the top handle is the one nearest the viewer.
        const QPointF mid = grid->worldToWidget(pb.maxs.x, pb.center().y);
        sendMouse(grid, QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseMove, grid->worldToWidget(pb.maxs.x + 20, pb.center().y), Qt::NoButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseMove, grid->worldToWidget(pb.maxs.x + 32, pb.center().y), Qt::NoButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseButtonRelease, grid->worldToWidget(pb.maxs.x + 32, pb.center().y), Qt::LeftButton, Qt::NoButton);
        const double vol1 = selectedBrushes(ctx).empty() ? 0 : brushVolume(selectedBrushes(ctx)[0].brush);
        r.check(ctx.doc.undoPosition() == pos + 1, "edge drag is one undo step");
        r.check(std::fabs((vol1 - vol0) - 0.5 * 32.0 * H * L) < 0.5,
                QString("top edge moved +32 makes a wedge: volume +%1 (expected %2)").arg(vol1 - vol0).arg(0.5 * 32.0 * H * L));
        // A move that drives the edge through the opposite face is rejected: nothing changes, no undo step.
        const std::string wedge = ctx.doc.text();
        const AABB wb = ctx.doc.selectionBounds();
        const QPointF mid2 = grid->worldToWidget(wb.maxs.x, wb.center().y);
        sendMouse(grid, QEvent::MouseButtonPress, mid2, Qt::LeftButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseMove, grid->worldToWidget(wb.mins.x - 200, wb.center().y), Qt::NoButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseButtonRelease, grid->worldToWidget(wb.mins.x - 200, wb.center().y), Qt::LeftButton, Qt::NoButton);
        r.check(ctx.doc.text() == wedge && ctx.doc.undoPosition() == pos + 1, "an edge dragged through the opposite face is rejected unchanged");
        for (int undoGuard = 0; undoGuard < 64 && ctx.doc.undoPosition() > pos; ++undoGuard) QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        r.check(ctx.doc.text() == before, "Ctrl+Z restores the brush text after the edge drag");
        QTest::keyClick(grid, Qt::Key_E);
        r.check(!ctx.edgeMode && !w.action("ToggleEdgeMode")->isChecked(), "E again leaves edge mode");
    }

    r.section("terrain brush: raise and lower a patch around the cursor, one undo step per stroke");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        selectPillar();
        w.action("TerrainCreate")->trigger();
        r.check(selectedPatchCount(ctx) == 1, "Create Terrain selects one 9x9 patch");
        w.action("ToggleTerrainBrush")->trigger();
        r.check(ctx.terrainBrush && w.action("ToggleTerrainBrush")->isChecked(), "the Terrain Brush command turns the mode on");
        Vec3 centre;
        bool found = false;
        for (const VertexHandle& h : selectionVertexHandles(ctx))
            if (h.patch && h.index == 40) {
                centre = h.pos;
                found = true;
            }
        r.check(found, "the 9x9 patch has a centre control point (index 40)");
        auto zOfCentre = [&]() {
            for (const VertexHandle& h : selectionVertexHandles(ctx))
                if (h.patch && h.index == 40) return h.pos.z;
            return 0.0;
        };
        const QPointF at = grid->worldToWidget(centre.x, centre.y);
        // One click: a single step of one grid unit, full strength at the cursor.
        const double z0 = zOfCentre();
        const size_t u0 = ctx.doc.undoPosition();
        sendMouse(grid, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
        r.check(std::fabs(zOfCentre() - (z0 + 16)) < 0.01,
                QString("LMB raises the point under the cursor by one grid unit (%1 -> %2)").arg(z0).arg(zOfCentre()));
        r.check(ctx.doc.undoPosition() == u0 + 1, "the click is one undo step");
        // A drag: steps every grid unit of travel, one undo step for the whole stroke.
        const double z1 = zOfCentre();
        sendMouse(grid, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseMove, grid->worldToWidget(centre.x + 8, centre.y), Qt::NoButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseMove, grid->worldToWidget(centre.x + 20, centre.y), Qt::NoButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseMove, grid->worldToWidget(centre.x + 40, centre.y), Qt::NoButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseButtonRelease, grid->worldToWidget(centre.x + 40, centre.y), Qt::LeftButton, Qt::NoButton);
        r.check(zOfCentre() - z1 > 10 && ctx.doc.undoPosition() == u0 + 2,
                QString("a drag raises the patch around the cursor in one undo step (+%1)").arg(zOfCentre() - z1));
        // Shift lowers.
        const double z2 = zOfCentre();
        sendMouse(grid, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
        sendMouse(grid, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
        r.check(std::fabs(zOfCentre() - (z2 - 16)) < 0.01 && ctx.doc.undoPosition() == u0 + 3, "Shift+LMB lowers the point under the cursor");
        for (int undoGuard = 0; undoGuard < 64 && ctx.doc.undoPosition() > pos; ++undoGuard) QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        r.check(ctx.doc.text() == before, "Ctrl+Z x4 restores the text before Create Terrain");
        w.action("ToggleTerrainBrush")->trigger();
        r.check(!ctx.terrainBrush, "the Terrain Brush command turns the mode off");
    }

    r.section("terrain brush modes: smooth, flatten, noise; radius; one undo step per stroke");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        selectPillar();
        w.action("TerrainCreate")->trigger();
        r.check(selectedPatchCount(ctx) == 1, "Create Terrain selects one 9x9 patch for the modes");
        w.action("ToggleTerrainBrush")->trigger();
        auto zAt = [&](size_t index) {
            for (const VertexHandle& h : selectionVertexHandles(ctx))
                if (h.patch && h.index == index) return h.pos.z;
            return 0.0;
        };
        auto posAt = [&](size_t index) {
            for (const VertexHandle& h : selectionVertexHandles(ctx))
                if (h.patch && h.index == index) return h.pos;
            return Vec3{};
        };
        const Vec3 centre = posAt(40);
        const Vec3 flatNeighbour = posAt(41);  // flat control point one grid cell east of the centre
        const QPointF at = grid->worldToWidget(centre.x, centre.y);
        const double z0 = zAt(40);

        // Raise a spike at the centre (one click, one grid unit).
        sendMouse(grid, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
        const double spike = zAt(40);
        r.check(std::fabs(spike - (z0 + 16)) < 0.01, QString("Raise: the centre rises one grid unit (%1 -> %2)").arg(z0).arg(spike));

        // Smooth mode from the menu (exclusive with the other modes).
        w.action("TerrainModeSmooth")->trigger();
        r.check(ctx.terrainBrushMode == EditorContext::TerrainBrushMode::Smooth && w.action("TerrainModeSmooth")->isChecked() &&
                    !w.action("TerrainModeRaise")->isChecked(),
                "Terrain Mode: Smooth is checked and Raise is unchecked");
        // A smooth click pulls the centre toward the average of its grid neighbours, which sit lower than the spike.
        const size_t u1 = ctx.doc.undoPosition();
        sendMouse(grid, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
        const double smoothed = zAt(40);
        r.check(smoothed < spike - 0.01 && smoothed > z0, QString("Smooth lowers the spike (%1 -> %2)").arg(spike).arg(smoothed));
        r.check(ctx.doc.undoPosition() == u1 + 1, "a smooth click is one undo step");
        // A smooth drag over the spike: several steps, still one undo step.
        const double s0 = zAt(40);
        sendMouse(grid, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseMove, grid->worldToWidget(centre.x + 20, centre.y), Qt::NoButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseMove, grid->worldToWidget(centre.x + 4, centre.y), Qt::NoButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseButtonRelease, grid->worldToWidget(centre.x + 4, centre.y), Qt::LeftButton, Qt::NoButton);
        r.check(zAt(40) < s0 && ctx.doc.undoPosition() == u1 + 2, QString("a smooth drag lowers it again in one undo step (%1 -> %2)").arg(s0).arg(zAt(40)));

        // Flatten mode: the target is the control point nearest the stroke start. Start on the flat neighbour
        // (its height is the target) and drag onto the centre, which is pulled toward that height.
        w.action("TerrainModeFlatten")->trigger();
        r.check(ctx.terrainBrushMode == EditorContext::TerrainBrushMode::Flatten, "Terrain Mode: Flatten is selected");
        const double target = zAt(41);
        const double f0 = zAt(40);
        const size_t u2 = ctx.doc.undoPosition();
        const QPointF start = grid->worldToWidget(flatNeighbour.x, flatNeighbour.y);
        sendMouse(grid, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseMove, grid->worldToWidget(centre.x + 32, centre.y), Qt::NoButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseMove, at, Qt::NoButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
        r.check(std::fabs(zAt(40) - target) < std::fabs(f0 - target) && std::fabs(zAt(40) - target) > 0.0001,
                QString("Flatten pulls the centre toward the start point's height %1 (%2 -> %3)").arg(target).arg(f0).arg(zAt(40)));
        r.check(std::fabs(zAt(41) - target) < 0.01, "Flatten leaves the start point at its own (target) height");
        r.check(ctx.doc.undoPosition() == u2 + 1, "a flatten stroke is one undo step");

        // Noise mode: a click adds a bounded, seeded bump at the cursor (the exact value depends on the seed).
        w.action("TerrainModeNoise")->trigger();
        const double n0 = zAt(40);
        const size_t u3 = ctx.doc.undoPosition();
        sendMouse(grid, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
        r.check(std::fabs(zAt(40) - n0) <= 8.0 + 0.01 && zAt(40) != n0, "Noise: a click moves the centre by at most half a grid unit");
        r.check(ctx.doc.undoPosition() == u3 + 1, "a noise click is one undo step");

        // Radius: Larger / Smaller step the brush radius in grid units (1..32).
        w.action("TerrainRadiusUp")->trigger();
        r.check(ctx.terrainRadius == 5, "Terrain Brush Radius Larger goes from 4 to 5 grid units");
        w.action("TerrainRadiusDown")->trigger();
        w.action("TerrainRadiusDown")->trigger();
        r.check(ctx.terrainRadius == 3, "Terrain Brush Radius Smaller goes down by one each time");
        w.action("TerrainRadiusUp")->trigger();
        r.check(ctx.terrainRadius == 4, "the radius is back at 4 grid units");

        // Ctrl+Z steps back to the text before Create Terrain.
        for (int undoGuard = 0; undoGuard < 64 && ctx.doc.undoPosition() > pos; ++undoGuard) QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        r.check(ctx.doc.text() == before, "Ctrl+Z restores the text before Create Terrain (all brush modes)");

        w.action("TerrainModeRaise")->trigger();
        w.action("ToggleTerrainBrush")->trigger();
        r.check(!ctx.terrainBrush && ctx.terrainBrushMode == EditorContext::TerrainBrushMode::Raise, "brush off and back to Raise");
    }

    r.section("atmosphere (fog / film keys)");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        r.check(w.action("MapAtmosphere") != nullptr, "Compile > Atmosphere command exists");
        Atmosphere a;
        a.fog = ExpFog{0, 1800, Vec3{0.55, 0.6, 0.7}};
        FilmGrade film;
        film.desaturation = 0.5;
        a.film = film;
        r.check(applyAtmosphere(ctx, a) && ctx.doc.undoPosition() == pos + 1, "applying fog + film is one undo step");
        const Atmosphere back = currentAtmosphere(ctx);
        r.check(back.fog && back.fog->halfway == 1800 && back.film && back.film->desaturation == 0.5,
                "the worldspawn keys read back as the same fog and film");
        r.check(!applyAtmosphere(ctx, a), "applying the same values again changes nothing");
        {
            AtmosphereDialog dlg(currentAtmosphere(ctx), &w);
            auto* vis = dlg.findChild<QLineEdit*>("atmoVision");
            if (vis) vis->setText("tonemapExposureAdjust=0.25; sunColor=1 0.9 0.8");
            r.check(vis && applyAtmosphere(ctx, dlg.value()) && ctx.doc.worldspawn()->value(kVisionKey) &&
                        *ctx.doc.worldspawn()->value(kVisionKey) == "tonemapExposureAdjust=0.25; sunColor=1 0.9 0.8",
                    "the dialog's Vision overrides field writes worldspawn iw8r_vision");
        }
        r.check(applyAtmosphere(ctx, Atmosphere{}) && !currentAtmosphere(ctx).fog && !currentAtmosphere(ctx).film,
                "an empty atmosphere removes both keys");
        for (int undoGuard = 0; undoGuard < 64 && ctx.doc.undoPosition() > pos; ++undoGuard) QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        r.check(ctx.doc.text() == before, "Ctrl+Z restores the map text");
    }

    r.section("first-run setup: Detect on a fake game folder, fields, nothing saved");
    {
        // A fake IW8\Builds\<name> folder under the test out dir: a recompiled Replay exe and the Oodle DLL.
        const QString root = QDir(outDir).filePath("setup_root");
        const QString game = root + "/IW8/Builds/fake-1.20";
        QDir().mkpath(game);
        QByteArray fakeExe(0x100, '\0');
        fakeExe[0] = 'M';
        fakeExe[1] = 'Z';
        fakeExe.replace(0x40, 8, "RPLYCMP2");
        QFile exeFile(game + "/game_dx12_ship_replay_local_dw.exe");
        if (exeFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) exeFile.write(fakeExe);
        exeFile.close();
        QFile dllFile(game + "/oo2core_7_win64.dll");
        if (dllFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) dllFile.write("fake oodle");
        dllFile.close();
        // Empty current paths and a fake drive root. The real Settings are not read or written here (accept is never called).
        SetupWizard wiz([](const QString&) { return QString(); }, {root.toStdString()});  // not shown, never exec'd
        wiz.detect();
        r.check(QFileInfo(wiz.replayFolder()).absoluteFilePath() == QFileInfo(game).absoluteFilePath(),
                "Detect fills the game folder from the fake drive root");
        r.check(wiz.replayExe().endsWith("/game_dx12_ship_replay_local_dw.exe"), "the renamed recompiled exe is the Replay exe");
        r.check(wiz.replayStatus().contains("recompiled"), QString("the Replay exe is reported recompiled (%1)").arg(wiz.replayStatus()));
        r.check(wiz.oodlePath().endsWith("/oo2core_7_win64.dll"), "Detect fills the Oodle DLL from the game folder");
        r.check(wiz.launchExe().isEmpty(), "the launch exe defaults to the Replay exe");
        r.check(wiz.actsPath().isEmpty() && wiz.zonetoolPath().isEmpty(), "ACTS and x64-zt stay as they were (not on the fake folder)");
    }

    r.section("help: keyboard shortcut sheet");
    {
        QTimer::singleShot(200, [] {
            if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) d->reject();
        });
        w.action("ShortcutSheet")->trigger();
        // The modal dialog took activation: give it back, or later keyboard shortcuts never reach the window.
        QApplication::setActiveWindow(&w);
        w.activateWindow();
        QApplication::processEvents();
        r.check(w.lastShortcutRows() > 100, QString("F1 lists every command plus the mouse/play keys (%1 rows)").arg(w.lastShortcutRows()));
    }

    r.section("console log: status entries, level and text filters, toasts");
    {
        // Queued connections deliver log entries to the panel and the toast host: spin the event loop (no exec()).
        LogPanel* lp = w.logPanel();
        ToastHost* toasts = w.toasts();
        const int before = EditorLog::instance().count();
        ctx.setStatus("iw8r log test: status line");
        QApplication::processEvents();
        const QList<LogEntry> all = EditorLog::instance().entries();
        const LogEntry last = all.isEmpty() ? LogEntry{} : all.back();
        r.check(EditorLog::instance().count() == before + 1 && last.level == LogLevel::Info && last.source == "status" &&
                    last.text == "iw8r log test: status line",
                "setStatus adds one Info entry with source status");
        r.check(lp && lp->shownText().contains("iw8r log test: status line"), "the Console dock lists the status entry");
        EditorLog::instance().add(LogLevel::Warning, "compile", "warning: iw8r log test warning");
        QApplication::processEvents();
        r.check(lp && lp->shownText().contains("warning: iw8r log test warning"), "the Console dock lists a Warning entry");
        r.check(toasts && toasts->isVisible() && toasts->visibleCount() >= 1,
                QString("a Warning shows a toast (%1 on screen)").arg(toasts ? toasts->visibleCount() : -1));
        lp->levelBox(LogLevel::Info)->setChecked(false);
        QApplication::processEvents();
        r.check(!lp->shownText().contains("iw8r log test: status line") && lp->shownText().contains("warning: iw8r log test warning"),
                "unchecking Info hides the Info rows and keeps the Warning");
        lp->levelBox(LogLevel::Info)->setChecked(true);
        lp->filterEdit()->setText("iw8r log test");
        QApplication::processEvents();
        r.check(lp->shownRows() == 2, QString("text filter 'iw8r log test' shows 2 rows (got %1)").arg(lp->shownRows()));
        lp->filterEdit()->setText("no such text zz");
        QApplication::processEvents();
        r.check(lp->shownRows() == 0, "text filter with no match shows no rows");
        lp->filterEdit()->clear();
        QApplication::processEvents();
        r.check(lp->shownRows() >= 2, "clearing the text filter shows the rows again");
        w.action("ToggleToasts")->trigger();
        r.check(toasts && !toasts->toastsEnabled() && toasts->visibleCount() == 0 && !w.action("ToggleToasts")->isChecked(),
                "View > Toast Notifications off clears the toasts");
        EditorLog::instance().add(LogLevel::Error, "compile", "error: iw8r log test error");
        QApplication::processEvents();
        r.check(toasts && toasts->visibleCount() == 0, "no toast appears while toasts are off (the entry still reaches the Console)");
        r.check(lp && lp->shownText().contains("error: iw8r log test error"), "the Error entry is listed in the Console with toasts off");
        w.action("ToggleToasts")->trigger();
        r.check(toasts && toasts->toastsEnabled() && w.action("ToggleToasts")->isChecked(), "View > Toast Notifications turns back on");
    }

    r.section("tool materials: ladder");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        const std::string mat0 = ctx.currentMaterial;
        const SelRef ref = selectPillar();
        w.action("ToolMaterial_ladder")->trigger();
        const EntityItem* it = ctx.doc.findItem(ref.entity, ref.item);
        r.check(it && it->prim() && brushContents(*it->prim()).ladder && ctx.currentMaterial == "ladder" && ctx.doc.undoPosition() == pos + 1,
                "Brush > Tool Material > Ladder makes the selected brush a ladder (one undo step)");
        for (int i = 0; i < 8 && ctx.doc.undoPosition() > pos; ++i) w.action("Undo")->trigger();
        ctx.currentMaterial = mat0;
        r.check(ctx.doc.text() == before, "Ctrl+Z restores the brush");
        QTest::keyClick(&w, Qt::Key_Escape);
    }

    r.section("game mode templates: Primitives > Game Modes inserts one undo step and Undo restores");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        const size_t ents0 = ctx.doc.map().entities.size();
        auto countEnt = [&](const char* cls, const char* key, const char* value) {
            size_t n = 0;
            for (const Entity& e : ctx.doc.map().entities) {
                if (e.classname() != cls) continue;
                const std::string* v = e.value(key);
                if (v && *v == value) ++n;
            }
            return n;
        };
        w.action("GameModeTemplate_ctf_flags")->trigger();
        r.check(ctx.doc.map().entities.size() == ents0 + 6, QString("Game Modes > CTF adds 6 entities (%1)").arg(int(ctx.doc.map().entities.size() - ents0)));
        r.check(countEnt("script_model", "targetname", "ctf_flag_allies") == 1 && countEnt("script_model", "targetname", "ctf_flag_axis") == 1,
                "the CTF template has an allied and an axis flag (script_model)");
        r.check(countEnt("trigger_radius", "script_gameobjectname", "ctf") == 4, "the CTF template has four ctf radius triggers");
        r.check(ctx.doc.undoPosition() == pos + 1, "the template is one undo step");
        r.check(ctx.doc.selection().size() >= 1, "the inserted entities are selected");
        w.action("GameModeTemplate_hardpoint_zones")->trigger();
        r.check(countEnt("trigger_multiple", "script_gameobjectname", "hardpoint") == 5, "Game Modes > Hardpoint adds five hardpoint zone brushes");
        r.check(ctx.doc.undoPosition() == pos + 2, "the second template is a second undo step");
        for (int i = 0; i < 16 && ctx.doc.undoPosition() > pos; ++i) w.action("Undo")->trigger();
        r.check(ctx.doc.text() == before, "Undo x2 restores the original map text");
    }

    r.section("brush flags: Ctrl+M make detail, non-colliding, structural, undo");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        const SelRef ref = selectPillar();
        const EntityItem* plain = ctx.doc.findItem(ref.entity, ref.item);
        r.check(plain && plain->prim() && plain->prim()->kind == PrimKind::Brush && !readBrushFlags(*plain->prim()).detail,
                "the pillar is a plain brush without flags");
        QTest::keyClick(&w, Qt::Key_M, Qt::ControlModifier);
        const EntityItem* det = ctx.doc.findItem(ref.entity, ref.item);
        r.check(det && det->prim() && readBrushFlags(*det->prim()).detail && brushIsDetail(*det->prim()),
                "Ctrl+M (Make Detail) writes contents detail; into the brush");
        r.check(ctx.doc.undoPosition() == pos + 1 && ctx.doc.selection().size() == 1, "Make Detail is one undo step and keeps the selection");
        w.action("BrushToggleNonColliding")->trigger();
        const EntityItem* nc = ctx.doc.findItem(ref.entity, ref.item);
        r.check(nc && nc->prim() && readBrushFlags(*nc->prim()).nonColliding && readBrushFlags(*nc->prim()).detail
                    && !brushContents(*nc->prim()).collides,
                "Toggle Non-Colliding: brushContents().collides is false, detail stays set");
        r.check(ctx.doc.undoPosition() == pos + 2, "Toggle Non-Colliding is one undo step");
        w.action("BrushMakeStructural")->trigger();
        const EntityItem* st = ctx.doc.findItem(ref.entity, ref.item);
        r.check(st && st->prim() && !readBrushFlags(*st->prim()).detail && !readBrushFlags(*st->prim()).nonColliding
                    && brushContents(*st->prim()).collides,
                "Make Structural clears detail and nonColliding");
        for (int undoGuard = 0; undoGuard < 64 && ctx.doc.undoPosition() > pos; ++undoGuard) QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        r.check(ctx.doc.text() == before, "Ctrl+Z x3 restores the original map text");
    }

    r.section("stock map reference layer");
    {
        StockWorld sw;
        ImportedMesh floor;
        floor.material = "tw/concrete_floor";
        floor.pos = {Vec3{0, 0, 0}, Vec3{512, 0, 0}, Vec3{512, 512, 0}, Vec3{0, 512, 0}};
        floor.idx = {0, 1, 2, 0, 2, 3};
        ImportedMesh sky = floor;
        sky.material = "tw/sky_dome";
        sw.meshes = {floor, sky};
        std::vector<iw8r::app::GpuVertex> tris;
        r.check(iw8r::app::buildReferenceTris(sw, tris) == 2 && tris.size() == 6, "sky meshes are left out of the reference layer");
        r.check(w.action("StockReference") && w.action("ClearStockReference"), "View > Stock Map Reference commands exist");
        w.cameraView()->setReference(tris);
        r.check(w.cameraView()->referenceTriangles() == 2, "the camera holds the reference triangles");
        w.action("ClearStockReference")->trigger();
        r.check(w.cameraView()->referenceTriangles() == 0, "Clear Stock Map Reference empties it");
    }

    r.section("region: set from selection hides the rest, Save Selected / Save Region, Region Off");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        const SelRef pil = selectPillar();
        const size_t drawnBefore = ctx.scene.items().size();
        auto docPrims = [&]() {
            std::vector<iw8r::Id> ids;
            for (const auto& e : ctx.doc.map().entities)
                for (const auto& it : e.items)
                    if (it.isPrimitive()) ids.push_back(it.id);
            return ids;
        };
        auto parsePrims = [](const QString& file, size_t& prims, size_t& entities, bool& worldspawnOnly) {
            std::string text;
            iw8r::Id next = 1;
            prims = 0;
            entities = 0;
            worldspawnOnly = false;
            if (!iw8r::readFile(file.toStdString(), text)) return false;
            iw8r::ParseResult pr = iw8r::parseMap(text, next);
            if (!pr.map) return false;
            entities = pr.map->entities.size();
            worldspawnOnly = entities == 1 && pr.map->entities[0].isWorldspawn();
            for (const auto& e : pr.map->entities)
                for (const auto& it : e.items)
                    if (it.isPrimitive()) ++prims;
            return true;
        };
        const QString dir = QDir(outDir).filePath("region");
        QDir().mkpath(dir);
        const QString selFile = QDir(dir).filePath("iw8r_gui_selected.map");
        const QString regFile = QDir(dir).filePath("iw8r_gui_region.map");
        QFile::remove(selFile);
        QFile::remove(regFile);

        QString serr;
        r.check(w.saveSelectedAs(selFile, &serr) && QFile::exists(selFile), "Save Selected writes the selected pillar" + (serr.isEmpty() ? QString() : " (" + serr + ")"));
        size_t selPrims = 0, selEnts = 0;
        bool selWorldOnly = false;
        r.check(parsePrims(selFile, selPrims, selEnts, selWorldOnly) && selPrims == 1 && selEnts == 1,
                "the saved selection parses and holds worldspawn with exactly 1 brush");

        w.action("RegionFromSelection")->trigger();
        const AABB pb = ctx.doc.selectionBounds();
        r.check(ctx.region && ctx.region->mins == pb.mins && ctx.region->maxs == pb.maxs, "Region > Set From Selection uses the pillar bounds");
        ctx.scene.sync(ctx.doc);
        size_t totalPrims = 0, hiddenPrims = 0;
        for (iw8r::Id id : docPrims()) {
            ++totalPrims;
            if (ctx.scene.hiddenByRegion.count(id)) ++hiddenPrims;
        }
        r.check(hiddenPrims > 0 && hiddenPrims < totalPrims,
                QString("the region hides %1 of %2 brushes outside the pillar").arg(hiddenPrims).arg(totalPrims));
        r.check(!ctx.scene.hiddenByRegion.count(pil.item), "the pillar itself stays visible in the region");
        bool hiddenDrawn = false;
        for (const app::PrimGeom* g : ctx.scene.items())
            if (ctx.scene.hiddenByRegion.count(g->ref.item) || ctx.scene.hiddenByRegion.count(g->ref.entity)) hiddenDrawn = true;
        r.check(!hiddenDrawn && ctx.scene.items().size() < drawnBefore, "region-hidden brushes are neither drawn nor pickable");

        QString rerr;
        r.check(w.saveRegionAs(regFile, &rerr) && QFile::exists(regFile), "Save Region writes the region" + (rerr.isEmpty() ? QString() : " (" + rerr + ")"));
        size_t regPrims = 0, regEnts = 0;
        bool regWorldOnly = false;
        r.check(parsePrims(regFile, regPrims, regEnts, regWorldOnly) && regPrims == totalPrims - hiddenPrims,
                QString("the saved region parses and holds the %1 brushes inside it").arg(totalPrims - hiddenPrims));

        w.action("RegionFromXY")->trigger();
        r.check(ctx.region && ctx.region->valid() && ctx.region->mins.z < -1e5 && ctx.region->maxs.z > 1e5 && ctx.region->mins.x < ctx.region->maxs.x,
                "Region > Set From XY View gives a box with the grid's visible X/Y and full Z");
        iw8r::Vec3 viewCentre;
        viewCentre[ctx.axisH()] = ctx.gridCenterH;
        viewCentre[ctx.axisV()] = ctx.gridCenterV;
        r.check(ctx.region->contains(viewCentre), "the XY region contains the centre of the grid view");
        w.action("RegionOff")->trigger();
        ctx.scene.sync(ctx.doc);
        r.check(!ctx.region && ctx.scene.hiddenByRegion.empty() && ctx.scene.items().size() == drawnBefore, "Region > Off shows everything again");
        r.check(ctx.doc.text() == before && ctx.doc.undoPosition() == pos, "region and save commands leave the document unchanged");
    }

    r.section("sun & sky keys");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        r.check(w.action("MapSunSky") != nullptr, "Compile > Sun & Sky command exists");
        SunSky sky = currentSunSky(ctx);
        sky.pitch = -30;
        sky.yaw = 120;
        sky.color = Vec3{1, 0.9, 0.8};
        sky.sky = "sky_test";
        r.check(applySunSky(ctx, sky) && ctx.doc.undoPosition() == pos + 1, "applying sun and sky is one undo step");
        const SunSky back = currentSunSky(ctx);
        r.check(back.pitch == -30 && back.yaw == 120 && std::fabs(back.color.y - 0.9) < 1e-9 && back.sky == "sky_test",
                "sundirection / suncolor / skyname read back");
        r.check(!applySunSky(ctx, sky), "applying the same values again changes nothing");
        ctx.scene.sync(ctx.doc);
        r.check(ctx.scene.sunToward.z > 0.45 && ctx.scene.sunToward.z < 0.55, "the lit preview sun follows the new pitch (sin 30)");
        for (int i = 0; i < 8 && ctx.doc.undoPosition() > pos; ++i) w.action("Undo")->trigger();
        r.check(ctx.doc.text() == before, "Undo restores the map text");
    }

    r.section("lit preview: sun shadows");
    {
        auto sumRgb = [&] {
            std::vector<iw8r::app::GpuVertex> tris, lines, overlay;
            iw8r::app::buildCameraGeometry(ctx.scene, ctx.doc, false, tris, lines, overlay);
            double s = 0;
            for (const auto& v : tris) s += (v.rgba & 0xff) + ((v.rgba >> 8) & 0xff) + ((v.rgba >> 16) & 0xff);
            return s;
        };
        const bool lit0 = ctx.scene.litPreview, sh0 = ctx.scene.litShadows;
        ctx.scene.litPreview = true;
        ctx.scene.litShadows = false;
        const double open = sumRgb();
        ctx.scene.litShadows = true;
        const double shadowed = sumRgb();
        r.check(shadowed < open, QString("walls and props shadow part of the room from the sun (%1 < %2)").arg(shadowed).arg(open));
        r.check(ctx.scene.shadowWorld(ctx.doc) && !ctx.scene.shadowWorld(ctx.doc)->solids().empty(), "the shadow world holds the map's solids");
        ctx.scene.litPreview = lit0;
        ctx.scene.litShadows = sh0;
    }

    r.section("model browser preview");
    {
        w.previewModel("accessory_money_pile_large");
        AssetBrowser* models = w.modelsBrowser();
        const QString cap = models ? models->previewCaption() : QString();
        r.check(models && (models->previewHasImage() || cap.contains("exporting")),
                "previewing a stock model shows its render (or that it is exporting): " + cap.section('\n', 1, 1));
        const QImage img = renderModelPreview([] {
            ImportedModel m;
            ImportedMesh mesh;
            mesh.pos = {Vec3{0, 0, 0}, Vec3{32, 0, 0}, Vec3{0, 32, 0}, Vec3{0, 0, -32}};
            mesh.idx = {0, 1, 2, 0, 2, 3, 0, 3, 1};
            m.meshes.push_back(mesh);
            return m;
        }(), 128);
        int lit = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) lit += qGray(img.pixel(x, y)) > 60;
        r.check(!img.isNull() && lit > 500, QString("the preview renderer fills a small tetrahedron (%1 lit pixels)").arg(lit));
    }

    r.section("effects browser");
    {
        AssetBrowser* eb = w.effectsBrowser();
        r.check(eb && eb->assetCount() > 1000, QString("the Effects dock lists the stock effects of the common zones (%1)").arg(eb ? eb->assetCount() : 0));
        const size_t pos = ctx.doc.undoPosition();
        const std::string before = ctx.doc.text();
        emit eb->effectChosen("vfx/core/expl/spark_death");
        const Entity* placed = ctx.doc.findEntity(ctx.doc.inspectedEntity());
        r.check(placed && placed->classname() == "fx_origin" && placed->value("fx") && *placed->value("fx") == "vfx/core/expl/spark_death" &&
                    ctx.doc.undoPosition() == pos + 1,
                "choosing an effect places an fx_origin with that fx key (one undo step)");
        for (int i = 0; i < 4 && ctx.doc.undoPosition() > pos; ++i) w.action("Undo")->trigger();
        r.check(ctx.doc.text() == before, "Undo removes it");
    }

    r.section("batch tools: key replace and selection sets (8.7)");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        const Id a = ctx.doc.addEntity({{"classname", "misc_model"}, {"model", "tm/com_pallet_2"}, {"origin", "0 0 0"}}, {}, "t");
        const Id b = ctx.doc.addEntity({{"classname", "misc_model"}, {"model", "tm/com_pallet_2"}, {"origin", "64 0 0"}}, {}, "t");
        KeyReplaceDialog kd(&ctx, &w);
        kd.set("model", "tm/com_pallet_2", "tm/cinderblock_pallet_01", false, false);
        r.check(kd.selectMatches() == 2 && ctx.doc.selection().size() == 2, "Select Matches selects both pallets");
        w.saveSelectionSet("pallets");
        ctx.doc.deselectAll();
        r.check(w.restoreSelectionSet("pallets") && ctx.doc.selection().size() == 2, "a saved selection set restores the selection");
        const size_t u = ctx.doc.undoPosition();
        r.check(kd.replaceAll() == 2 && ctx.doc.undoPosition() == u + 1 && ctx.doc.findEntity(a)->value("model") &&
                    *ctx.doc.findEntity(a)->value("model") == "tm/cinderblock_pallet_01" &&
                    *ctx.doc.findEntity(b)->value("model") == "tm/cinderblock_pallet_01",
                "Replace All swaps the model on both in one undo step");
        for (int i = 0; i < 8 && ctx.doc.undoPosition() > pos; ++i) w.action("Undo")->trigger();
        r.check(ctx.doc.text() == before, "Undo restores the map");
    }

    r.section("model paint (8.3)");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        // The sample room's floor: paint around a point on it.
        AABB all;
        ctx.scene.sync(ctx.doc);
        for (const auto* g : ctx.scene.items())
            if (g->bounds.valid()) all.add(g->bounds);
        const Vec3 floorPt{(all.mins.x + all.maxs.x) / 2, (all.mins.y + all.maxs.y) / 2, all.mins.z + 16};
        const size_t n = paintModelsAt(ctx, "tm/com_pallet_2", floorPt, 4, 256, 7);
        size_t statics = 0;
        for (const auto& e : ctx.doc.map().entities)
            if (e.classname() == "misc_model" && e.value("model") && *e.value("model") == "tm/com_pallet_2") ++statics;
        r.check(n > 0 && statics == n && ctx.doc.undoPosition() == pos + 1,
                QString("a paint stroke places %1 static models in one undo step").arg(n));
        r.check(w.action("ModelPaintMode") && w.action("ModelPaintMode")->isCheckable(), "Tools > Model Paint Mode is a toggle");
        for (int i = 0; i < 4 && ctx.doc.undoPosition() > pos; ++i) w.action("Undo")->trigger();
        r.check(ctx.doc.text() == before, "Undo removes them");
    }

    r.section("prefab enter / leave (8.5)");
    {
        const std::string keepText = ctx.doc.text(), keepPath = ctx.doc.path();  // restored at the end (later sections use them)
        const QString dir = QDir(outDir).filePath("prefab_edit");
        QDir(dir).removeRecursively();
        QDir().mkpath(dir + "/prefabs");
        // A box brush of `size` units in Radiant plane form.
        auto boxText = [](int size) {
            const QString n = QString::number(size);
            const QString face = " common/caulk 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n";
            QString t = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n{\n";
            t += QString(" ( %1 %1 0 ) ( 0 %1 0 ) ( 0 0 0 )").arg(n) + face;
            t += QString(" ( 0 0 %1 ) ( 0 %1 %1 ) ( %1 %1 %1 )").arg(n) + face;
            t += QString(" ( 0 0 0 ) ( 0 0 %1 ) ( %1 0 %1 )").arg(n) + face;
            t += QString(" ( %1 0 0 ) ( %1 0 %1 ) ( %1 %1 %1 )").arg(n) + face;
            t += QString(" ( %1 %1 0 ) ( %1 %1 %1 ) ( 0 %1 %1 )").arg(n) + face;
            t += QString(" ( 0 %1 0 ) ( 0 %1 %1 ) ( 0 0 %1 )").arg(n) + face;
            t += "}\n}\n";
            return t.toStdString();
        };
        {
            QFile pf(dir + "/prefabs/box.map");
            pf.open(QIODevice::WriteOnly);
            pf.write(QByteArray::fromStdString(boxText(32)));
        }
        {
            QFile mf(dir + "/parent.map");
            mf.open(QIODevice::WriteOnly);
            mf.write("iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"misc_prefab\"\n\"model\" \"prefabs/box.map\"\n\"origin\" \"0 0 0\"\n}\n");
        }
        w.openPath(dir + "/parent.map");
        Id prefabId = kNoId;
        for (const auto& e : ctx.doc.map().entities)
            if (e.classname() == "misc_prefab") prefabId = e.id;
        ctx.doc.select({prefabId, kNoId}, false);
        r.check(w.enterPrefab() && w.prefabDepth() == 1 && QString::fromStdString(ctx.doc.path()).endsWith("box.map"),
                "Enter Prefab opens the prefab's map");
        std::string err;
        r.check(ctx.doc.loadFromText(boxText(64), (dir + "/prefabs/box.map").toStdString(), &err) && ctx.doc.save(&err),
                "the prefab is edited and saved: " + QString::fromStdString(err));
        r.check(w.leavePrefab() && w.prefabDepth() == 0 && QString::fromStdString(ctx.doc.path()).endsWith("parent.map"),
                "Leave Prefab returns to the parent map");
        ctx.scene.sync(ctx.doc);
        double maxX = 0;
        for (const auto* g : ctx.scene.items())
            if (g->prefab && g->bounds.valid()) maxX = std::max(maxX, g->bounds.maxs.x);
        r.check(maxX > 60, QString("the instance shows the edited prefab (extent %1)").arg(maxX));
        ctx.doc.loadFromText(keepText, keepPath, &err);
        ctx.documentEdited("restored");
    }

    r.section("shape models and scale gizmo");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        QString name;
        const bool added = !ctx.doc.path().empty() && w.addShapeModel(int(ShapeKind::Cube), &name);
        const QString obj = w.customModelRoots().front() + "/" + name + ".obj";
        r.check(added && QFile::exists(obj) && name.startsWith("iw8r_shape_cube_"), "Primitives > Shape Models > Cube writes " + obj);
        Id cube = kNoId;
        for (const auto& e : ctx.doc.map().entities)
            if (e.value("model") && QString::fromStdString(*e.value("model")) == name) cube = e.id;
        r.check(cube != kNoId && ctx.doc.findEntity(cube)->classname() == "misc_model", "the shape is placed as a static misc_model");
        if (cube != kNoId) {
            ctx.doc.select({cube, kNoId}, false);
            const Vec3 o = ctx.doc.findEntity(cube)->origin().value_or(Vec3{});
            r.check(scaleSelectionAbout(ctx, 2, 2.0, o) && ctx.doc.findEntity(cube)->value("modelscale") &&
                        *ctx.doc.findEntity(cube)->value("modelscale") == "2",
                    "the scale gizmo sets modelscale 2 on the model");
            ctx.scene.sync(ctx.doc);
            double height = 0;
            for (const auto* g : ctx.scene.items())
                if (g->ref.entity == cube && g->bounds.valid()) height = g->bounds.maxs.z - g->bounds.mins.z;
            r.check(height > 120 && height < 136, QString("the camera draws the cube at the new size (%1 tall)").arg(height));
        }
        r.check(w.action("GizmoScale") != nullptr, "View > Gizmo: Scale (T)");
        for (int i = 0; i < 8 && ctx.doc.undoPosition() > pos; ++i) w.action("Undo")->trigger();
        r.check(ctx.doc.text() == before, "Undo removes the shape");
        // Brushes: the pillar doubles in height along Z about its base.
        selectPillar();
        const AABB b0 = ctx.doc.selectionBounds();
        r.check(scaleSelectionAbout(ctx, 2, 2.0, b0.mins) && std::fabs((ctx.doc.selectionBounds().maxs.z - b0.mins.z) - 2 * (b0.maxs.z - b0.mins.z)) < 0.01,
                "a brush scales along one axis about the pivot");
        for (int i = 0; i < 8 && ctx.doc.undoPosition() > pos; ++i) w.action("Undo")->trigger();
        r.check(ctx.doc.text() == before, "Undo restores the pillar");
    }

    r.section("reflection probe capture (5.5)");
    {
        ctx.scene.sync(ctx.doc);
        AABB all;
        for (const auto* g : ctx.scene.items())
            if (g->bounds.valid()) all.add(g->bounds);
        const std::vector<uint8_t> faces = w.cameraView()->captureCube(all.center(), 64);
        std::set<uint32_t> colours;
        for (size_t i = 0; i + 3 < faces.size(); i += 4 * 37) colours.insert(uint32_t(faces[i]) | uint32_t(faces[i + 1]) << 8 | uint32_t(faces[i + 2]) << 16);
        r.check(faces.size() == size_t(6 * 64 * 64 * 4) && colours.size() > 4,
                QString("a probe capture renders six 64x64 faces with the room in them (%1 colours sampled)").arg(colours.size()));
        QString dir;
        const int n = w.captureReflectionProbes(&dir);
        r.check(n >= 1 && QFileInfo(dir + "/probe0.rgba").size() == 6 * 64 * 64 * 4 && QFileInfo::exists(dir + "/probe0.pos"),
                QString("Compile > Capture Reflection Probes writes %1 capture(s) to %2").arg(n).arg(dir));
        // 3.16: the textured camera then reflects the nearest captured probe on glossy surfaces.
        const bool wasTextured = ctx.texturedView;
        if (QAction* m = w.action("CameraModeTextured")) m->trigger();
        w.cameraView()->renderAndCount();
        r.check(w.cameraView()->activeProbe().contains("probe"), "the textured camera uses the nearest captured probe for reflections: " +
                                                                     QFileInfo(w.cameraView()->activeProbe().section('|', 0, 0)).fileName());
        if (!wasTextured)
            if (QAction* m = w.action("CameraModeFlat")) m->trigger();
    }

    r.section("measure tool and snap to grid (2.19)");
    {
        w.action("MeasureTool")->trigger();
        const int H = ctx.axisH(), V = ctx.axisV();
        const double g = ctx.doc.grid();
        const double h0 = std::round(ctx.gridCenterH / g) * g, v0 = std::round(ctx.gridCenterV / g) * g;
        const QPoint a = grid->worldToWidget(h0, v0).toPoint();
        const QPoint b = grid->worldToWidget(h0 + 3 * g * 4, v0 + 4 * g * 4).toPoint();
        QTest::mousePress(grid, Qt::LeftButton, Qt::NoModifier, a);
        QMouseEvent move(QEvent::MouseMove, QPointF(b), grid->mapToGlobal(QPointF(b)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(grid, &move);
        QTest::mouseRelease(grid, Qt::LeftButton, Qt::NoModifier, b);
        const Vec3 d = ctx.measureB - ctx.measureA;
        r.check(ctx.measureValid && std::fabs(length(d) - 5 * g * 4) < g, QString("a 3-4-5 drag measures %1 (expected %2)")
                                                                              .arg(length(d)).arg(5 * g * 4));
        (void)H;
        (void)V;
        w.action("MeasureTool")->trigger();
        r.check(!ctx.measureMode, "Alt+M toggles the measure tool off");

        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        const Id e = ctx.doc.addEntity({{"classname", "script_origin"}, {"origin", "3 5 7"}}, {}, "t");
        ctx.doc.select({e, kNoId}, false);
        w.action("SnapSelectionToGrid")->trigger();
        const Vec3 o = ctx.doc.findEntity(e)->origin().value_or(Vec3{1, 1, 1});
        const auto onGrid = [&](double x) { return std::fabs(x / g - std::round(x / g)) < 1e-9; };
        r.check(onGrid(o.x) && onGrid(o.y) && onGrid(o.z), QString("Snap Selection to Grid puts the origin on the grid (%1 %2 %3)").arg(o.x).arg(o.y).arg(o.z));
        for (int i = 0; i < 6 && ctx.doc.undoPosition() > pos; ++i) w.action("Undo")->trigger();
        r.check(ctx.doc.text() == before, "Undo restores the map");
    }

    r.section("web server API and auth (L10)");
    {
        WebServer::Hooks hooks;
        hooks.mapName = [] { return QString("test.map"); };
        WebServer web(&ctx, hooks);
        // Local: no login. The page and its API.
        auto page = web.handle("GET", "/", {}, true, {});
        r.check(page.status == 200 && page.body.contains("IW8 Radiant"), "GET / serves the browser client");
        {
            // 10.6: the browser client has the deploy / play buttons and the asset browser, and the asset API answers.
            auto js = web.handle("GET", "/app.js", {}, true, {});
            r.check(page.body.contains("assetKind") && page.body.contains("id=\"build\"") && js.status == 200 && js.body.contains("/api/place"),
                    "the browser page has Deploy / Build & Play / Load in Game and an Assets panel");
            auto mj = web.handle("GET", "/api/map", {}, true, {});
            QFile mf(QDir(outDir).filePath("web_map.json"));
            if (mf.open(QIODevice::WriteOnly)) mf.write(mj.body);
            r.check(mj.body.contains("\"mats\"") && mj.body.contains("\"uvs\"") && mj.body.contains("tm2co/me_curb_stone_01"),
                    "GET /api/map carries face materials and texture coordinates for the textured 3D view (10.5)");
            auto as = web.handle("GET", "/api/assets?kind=models&q=barrel&limit=5", {}, true, {});
            r.check(as.status == 200 && QJsonDocument::fromJson(as.body).object().value("total").toInt() > 0, "GET /api/assets finds models");
        }
        auto st = web.handle("GET", "/api/state", {}, true, {});
        r.check(st.status == 200 && QJsonDocument::fromJson(st.body).object().value("map").toString() == "test.map", "GET /api/state");
        auto mp = web.handle("GET", "/api/map", {}, true, {});
        const QJsonObject mo = QJsonDocument::fromJson(mp.body).object();
        r.check(mp.status == 200 && mo.value("items").toArray().size() > 0 && mo.value("entities").toArray().size() > 0,
                QString("GET /api/map lists %1 items").arg(mo.value("items").toArray().size()));
        // Edits go through the shared document (one undo history).
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        const Id e = ctx.doc.addEntity({{"classname", "script_origin"}, {"origin", "0 0 0"}}, {}, "t");
        auto sel = web.handle("POST", "/api/select", QJsonDocument(QJsonObject{{"e", double(e)}, {"i", -1}}).toJson(), true, {});
        auto key = web.handle("POST", "/api/key", QJsonDocument(QJsonObject{{"e", double(e)}, {"key", "targetname"}, {"value", "from_web"}}).toJson(), true, {});
        auto mv = web.handle("POST", "/api/move", QJsonDocument(QJsonObject{{"d", QJsonArray{16, 0, 0}}}).toJson(), true, {});
        r.check(sel.status == 200 && key.status == 200 && mv.status == 200 && ctx.doc.findEntity(e)->value("targetname") &&
                    *ctx.doc.findEntity(e)->value("targetname") == "from_web" && ctx.doc.findEntity(e)->origin()->x == 16,
                "select, key and move from the browser edit the document");
        auto un = web.handle("POST", "/api/undo", {}, true, {});
        r.check(un.status == 200 && ctx.doc.findEntity(e)->origin()->x == 0, "browser Undo shares the editor's undo history");
        for (int i = 0; i < 8 && ctx.doc.undoPosition() > pos; ++i) w.action("Undo")->trigger();
        r.check(ctx.doc.text() == before, "Undo restores the map");
        // Remote: token without credentials; Basic auth with them; wrong logins lock out.
        r.check(web.handle("GET", "/api/state", {}, false, {}, {}, "10.0.0.5").status == 403, "a remote request without the token is refused");
        r.check(web.handle("GET", "/api/state?token=" + web.token().toUtf8(), {}, false, {}, {}, "10.0.0.5").status == 200,
                "the session token admits a remote client (no credentials set)");
        WebServer::setCredentials("tester", "correct-horse-1");
        auto noAuth = web.handle("GET", "/api/state", {}, false, {}, {}, "10.0.0.6");
        r.check(noAuth.status == 401 && noAuth.extraHeaders.contains("WWW-Authenticate: Basic"), "with credentials: 401 asks for a login");
        const QByteArray good = "Basic " + QByteArray("tester:correct-horse-1").toBase64();
        const QByteArray bad = "Basic " + QByteArray("tester:wrong").toBase64();
        r.check(web.handle("GET", "/api/state", {}, false, {}, good, "10.0.0.6").status == 200, "the right login is accepted");
        r.check(web.handle("GET", "/api/state", {}, false, {}, bad, "10.0.0.7").status == 401, "a wrong password is refused");
        for (int i = 0; i < 8; ++i) web.handle("GET", "/api/state", {}, false, {}, bad, "10.0.0.8");
        r.check(web.handle("GET", "/api/state", {}, false, {}, good, "10.0.0.8").status == 403, "eight failures lock that address out");
        r.check(Settings::instance().value("web/hash").toString().size() == 64 &&
                    !Settings::instance().value("web/hash").toString().contains("correct-horse"),
                "only a salted hash of the password is stored");
        WebServer::setCredentials({}, {});
        QString err;
        r.check(web.start(0, false, &err) && web.running() && web.url().startsWith("http://127.0.0.1:"), "the server listens on localhost: " + web.url());
        web.stop();
        // HTTPS (web/tls): a real TLS handshake and request with the test certificate.
        const QString tlsDir = QFileInfo(samplePath).absolutePath() + "/web_tls";
        if (QFileInfo::exists(tlsDir + "/cert.pem")) {
            QString terr;
            const bool started = web.start(0, false, &terr, tlsDir + "/cert.pem", tlsDir + "/key.pem");
            r.check(started && web.tls() && web.url().startsWith("https://"), "the server starts with HTTPS: " + terr);
            QSslSocket sock;
            QObject::connect(&sock, &QSslSocket::sslErrors, &sock, [&sock](const QList<QSslError>&) { sock.ignoreSslErrors(); });
            sock.connectToHostEncrypted("127.0.0.1", web.port());
            QByteArray reply;
            // The server runs on this thread: wait with the event loop, not waitForEncrypted.
            for (int i = 0; i < 300 && !sock.isEncrypted() && sock.state() != QAbstractSocket::UnconnectedState; ++i) QTest::qWait(20);
            if (sock.isEncrypted()) {
                sock.write("GET /api/state HTTP/1.1\r\nHost: localhost\r\n\r\n");
                for (int i = 0; i < 100 && !reply.contains("\r\n\r\n{"); ++i) {
                    QTest::qWait(30);
                    reply += sock.readAll();
                }
            }
            r.check(reply.startsWith("HTTP/1.1 200") && reply.contains("\"revision\""),
                    "an HTTPS client gets /api/state over TLS (" + QString::fromLatin1(reply.left(15)) + " " + sock.errorString() + " / " + web.lastTlsError() + ")");
            web.stop();
        } else {
            r.text += "  skip HTTPS (no tests/data/web_tls)\n";
        }
    }

    r.section("entity display modes (2.17)");
    {
        // Zoom the grid onto the sample's entities so labels are drawn.
        const bool names0 = ctx.showEntityNames, origins0 = ctx.showModelOrigins;
        ctx.showEntityNames = false;
        const QImage off = grid->grab().toImage();
        w.action("ShowEntityNames")->trigger();
        const QImage on = grid->grab().toImage();
        int diff = 0;
        for (int y = 0; y < std::min(off.height(), on.height()); y += 2)
            for (int x = 0; x < std::min(off.width(), on.width()); x += 2) diff += off.pixel(x, y) != on.pixel(x, y);
        r.check(ctx.showEntityNames && diff > 20, QString("View > Entity Display > Names draws labels (%1 pixels changed)").arg(diff));
        w.action("ShowEntityNames")->trigger();
        r.check(!ctx.showEntityNames && w.action("ShowEntityAngles")->isChecked() == ctx.showAngles && w.action("ShowModelOrigins"),
                "the three display modes toggle and remember their state");
        ctx.showEntityNames = names0;
        ctx.showModelOrigins = origins0;
    }

    r.section("camera context menu (3.7)");
    {
        // The centre of the camera looks at something in the sample room.
        QMenu* m = w.buildCameraContextMenu(QPointF(w.cameraView()->width() / 2.0, w.cameraView()->height() / 2.0));
        QStringList texts;
        for (QAction* a : m->actions()) texts << a->text();
        r.check(texts.join("|").contains("Camera Speed") && texts.join("|").contains("Field of View"),
                "the camera context menu has speed and FOV: " + texts.join(", ").left(160));
        delete m;
    }

    r.section("themes, toolbar, safe mode (9.2 / 9.5)");
    {
        QToolBar* tb = w.findChild<QToolBar*>("mainToolbar");
        r.check(tb && tb->actions().size() >= 8, QString("the main toolbar shows %1 commands").arg(tb ? tb->actions().size() : 0));
        if (tb) {
            int longest = 0;
            for (QAction* a2 : tb->actions()) longest = std::max(longest, int(a2->text().size()));
            QAction* lit = w.findChild<QAction*>("tb_ToggleLitPreview");
            const bool before2 = ctx.scene.litPreview;
            if (lit) lit->trigger();
            const bool toggled = ctx.scene.litPreview != before2;
            if (lit) lit->trigger();
            tb->grab().save(QDir(outDir).filePath("toolbar.png"));
            r.check(longest <= 9 && lit && toggled && ctx.scene.litPreview == before2,
                    QString("toolbar labels are short (longest %1) and the twins run their commands").arg(longest));
        }
        const QColor before = qApp->palette().color(QPalette::Window);
        QAction* light = w.findChild<QAction*>("ThemeLight");
        if (light) light->trigger();
        const QColor lightWin = qApp->palette().color(QPalette::Window);
        if (QAction* dark = w.findChild<QAction*>("ThemeDark")) dark->trigger();
        r.check(light && lightWin.lightness() > 200 && qApp->palette().color(QPalette::Window) == before,
                "View > Theme switches to Light and back to Dark");
        r.check(w.action("RestartSafeMode") && w.action("ResetSettings") && w.action("CustomizeToolbar"),
                "Help > Restart in Safe Mode, Reset All Settings and View > Customize Toolbar exist");
    }

    r.section("local-space gizmo (3.6)");
    {
        const size_t pos = ctx.doc.undoPosition();
        const std::string before = ctx.doc.text();
        const Id e = ctx.doc.addEntity({{"classname", "script_origin"}, {"origin", "0 0 0"}, {"angles", "0 90 0"}}, {}, "t");
        ctx.doc.select({e, kNoId}, false);
        const bool wasLocal = ctx.gizmoLocal;
        ctx.gizmoLocal = false;
        const Vec3 world = w.cameraView()->gizmoAxisDir(0);
        w.action("GizmoLocal")->trigger();
        const Vec3 local = w.cameraView()->gizmoAxisDir(0);
        r.check(world.x > 0.99 && ctx.gizmoLocal && local.y > 0.99 && std::fabs(local.x) < 1e-6,
                QString("local space turns the X handle with the entity's yaw (%1 %2 %3)").arg(local.x).arg(local.y).arg(local.z));
        ctx.gizmoLocal = wasLocal;
        for (int i = 0; i < 6 && ctx.doc.undoPosition() > pos; ++i) w.action("Undo")->trigger();
        r.check(ctx.doc.text() == before, "Undo restores the map");
    }

    r.section("one-key load map (7.2)");
    {
        r.check(w.action("LoadMapInGame") != nullptr, "Compile > Load Map in Game exists (Ctrl+F8)");
        // Replay MP has no map/devmap command (docs/research/65): the default is the lobby start.
        r.check(LiveLinkPanel::loadMapCommand("mp_iw8r_test") == "set NSQLTTMRMP mp_iw8r_test; set MOLPOSLOMO war; xpartygo 1",
                "the default load command is the lobby start with no countdown (ui_mapname, ui_gametype, xpartygo 1)");
        r.check(w.action("BuildDeployLoad") && w.action("BuildDeployLoad")->shortcut() == QKeySequence("F5"),
                "F5 is Build, Deploy and Play in Game (devmap)");
        {
            const QStringList q = LiveLinkPanel::quickMatchCommands("mp_x", "war", 4, 1);
            r.check(q.size() == 6 && q[0] == "set NSQLTTMRMP mp_x" && q[1] == "set MOLPOSLOMO war" && q[2] == "set RNRLKSNNR 4" &&
                        q[5] == "set replay_dw_botCount 5",
                    "quick match selects the map, the mode and the bot counts (token dvars, and the client's own bot count)");
            std::string joined;
            std::vector<std::string> all;
            for (const QString& c : q) all.push_back(c.toStdString());
            for (const std::string& c : devmap::hostLobbyCommands()) all.push_back(c);
            r.check(devmap::joinCommands(all, joined) && joined.find("xstartprivatematch") != std::string::npos,
                    "quick match from the menu is one command text that also hosts the private match");
        }
        // 7.4 hot update: moving a live script entity builds the dvar command for the running game.
        {
            // The proxy sections of the Live Link dock exist only for a game folder that has the proxy DLL.
            const QString noProxy = outDir + "/live_no_proxy", withProxy = outDir + "/live_with_proxy";
            QDir().mkpath(noProxy);
            QDir().mkpath(withProxy);
            QFile dll(withProxy + "/XInput9_1_0.dll");
            if (dll.open(QIODevice::WriteOnly | QIODevice::Truncate)) dll.write("MZ");
            dll.close();
            r.check(!LiveLinkPanel::proxyInstalledIn(noProxy) && LiveLinkPanel::proxyInstalledIn(withProxy) && !LiveLinkPanel::proxyInstalledIn(QString()),
                    "the proxy is taken as installed only when its DLL is in the game folder");
            QDir().mkpath(noProxy + "/.proxy");
            LiveLinkPanel plain([&] { return noProxy; }, [] { return QString("mp_iw8r_test"); });
            plain.show();
            QApplication::processEvents();
            r.check(!plain.proxySectionsVisible() && plain.findChild<QGroupBox*>() != nullptr, "without the proxy DLL the Live Link dock shows only the game link section");
            QTest::qWait(150);
            r.check(!QFileInfo::exists(noProxy + "/.proxy/control.ini") && !QFileInfo::exists(noProxy + "/.proxy/control.ini.tmp"),
                    "without the proxy DLL no control request is written into the game folder");
            plain.hide();
            LiveLinkPanel old([&] { return withProxy; }, [] { return QString("mp_iw8r_test"); });
            old.show();
            QApplication::processEvents();
            r.check(old.proxySectionsVisible(), "with the proxy DLL the status, select, console and camera sections are shown");
            old.hide();
            w.activateWindow();
            QApplication::setActiveWindow(&w);
            QApplication::processEvents();
        }
        if (LiveLinkPanel* ll = w.liveLinkPanel()) {
            // Map debug overlay controls (research 78): presets, filter, and the report made from the client's dump.
            auto* preset = ll->findChild<QComboBox*>("liveLinkDebugPreset");
            auto* filter = ll->findChild<QLineEdit*>("liveLinkDebugFilter");
            r.check(preset && filter && preset->count() == 5 && preset->itemData(2).toInt() == 7 && preset->itemData(4).toInt() == 63 &&
                        ll->findChild<QPushButton*>("liveLinkDebugSelected") && ll->findChild<QPushButton*>("liveLinkDebugDump"),
                    "the Live Link dock has the debug overlay presets, the filter and the Compare with game button");
            r.check(!ll->setDebugOverlay(7, "door1; quit") && ll->messageText().contains("filter"),
                    "a debug overlay filter that could carry a second command is refused before anything is sent");
            const QString log = "t IW8R_MAPDEBUG begin map=mp_x\nt IW8R_VIEW 1.00 2.00 3.00 4.00 5.00\nt IW8R_FEET 1.00 2.00 -57.00 0\n"
                                "t IW8R_ENT 40|script_model|script_model||gui_dump_a||||||10.00 20.00 30.00|0.00 0.00 0.00|0.00 0.00 0.00|8.00 8.00 8.00|1|0|-1\n"
                                "t IW8R_MAPDEBUG end entities=1\n";
            Id next = 900000;
            const ParseResult pm = parseMap("iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"script_model\"\n\"targetname\" \"gui_dump_a\"\n\"origin\" \"10 20 30\"\n}\n"
                                            "{\n\"classname\" \"script_origin\"\n\"targetname\" \"gui_dump_b\"\n\"origin\" \"0 0 0\"\n}\n", next);
            MapDebugDump parsed;
            const QString report = pm.map ? LiveLinkPanel::dumpReport(log, &*pm.map, &parsed) : QString();
            r.check(parsed.complete && parsed.hasView && report.contains("2 named map entities looked for: 1 found in place, 0 at another place, 1 missing") &&
                        report.contains("MISSING  gui_dump_b (script_origin)"),
                    "the game's entity dump is compared with the map: found, moved and missing entities are listed");
            r.check(LiveLinkPanel::dumpReport("no dump here", nullptr).isEmpty(), "a log without a dump gives no report");
        }
        if (LiveLinkPanel* ll = w.liveLinkPanel()) {
            const size_t pos = ctx.doc.undoPosition();
            const std::string before = ctx.doc.text();
            const Id m = ctx.doc.addEntity({{"classname", "script_model"}, {"model", "com_junktire"}, {"targetname", "hot_box"}, {"origin", "0 0 0"}}, {}, "t");
            ctx.doc.select({m, kNoId}, false);
            ll->setHotUpdateEnabled(true);
            ctx.doc.setKeyValue(m, "origin", "16 32 48");
            ctx.documentEdited("moved");
            r.check(ll->lastHotCommand().startsWith("set iw8r_hot \"") && ll->lastHotCommand().contains("hot_box 16 32 48 0 0 0"),
                    "hot update builds: " + ll->lastHotCommand());
            // 7.7: moving an effect replays it at the new spot (dvar iw8r_fx).
            const Id f = ctx.doc.addEntity({{"classname", "fx_origin"}, {"fx", "vfx/iw8/core/vfx_test"}, {"origin", "0 0 0"}}, {}, "t");
            ctx.doc.select({f, kNoId}, false);
            ctx.doc.setKeyValue(f, "origin", "8 0 0");
            ctx.documentEdited("moved fx");
            r.check(ll->lastHotCommand().startsWith("set iw8r_fx \"") && ll->lastHotCommand().contains("vfx/iw8/core/vfx_test 8 0 0"),
                    "hot effect channel builds: " + ll->lastHotCommand());
            // 7.9: a worldspawn sun edit is NOT pushed (the sun builtins crashed Replay MP).
            if (const iw8r::Entity* ws = ctx.doc.worldspawn()) {
                const Id wsId = ws->id;
                ctx.doc.setKeyValue(wsId, "sundirection", "-45 30 0");
                ctx.documentEdited("sun");
                ctx.doc.setKeyValue(wsId, "sundirection", "-50 30 0");
                ctx.documentEdited("sun 2");
                r.check(!ll->lastHotCommand().startsWith("set iw8r_sun"), "sun edits are not sent live (rebuild instead): " + ll->lastHotCommand());
            }
            ll->setHotUpdateEnabled(false);
            for (int i = 0; i < 12 && ctx.doc.undoPosition() > pos; ++i) w.action("Undo")->trigger();
            r.check(ctx.doc.text() == before, "Undo restores the map");
        }
    }

    r.section("browser availability marks");
    {
        // tmo/uk_portable_radio_01: its images are only referenced from techsets_mp_donetsk (no data in Replay).
        AssetBrowser* mb = w.materialBrowser();
        mb->setFilter("tmo/uk_portable_radio_01");
        for (int i = 0; i < 1800 && mb->unavailableMarked() == 0; ++i) {
            if (i % 20 == 0) mb->refreshThumbnails();
            QTest::qWait(50);
        }
        r.check(mb->unavailableMarked() > 0, "a material Replay has no images for is greyed as not available");
        mb->setFilter("");
    }

    r.section("thumbnail store");
    {
        const QString pack = QDir(outDir).filePath("thumbs_test/test.pack");
        QFile::remove(pack);
        QImage a(96, 96, QImage::Format_RGB32), b(96, 96, QImage::Format_RGB32);
        a.fill(QColor(200, 40, 40));
        b.fill(QColor(40, 200, 40));
        {
            ThumbStore s(pack);
            s.put("tmo/a", 11, a);
            s.put("tmo/b", 22, b);
            r.check(s.count() == 2 && !s.get("tmo/a", 11).isNull() && s.get("tmo/a", 12).isNull(),
                    "put/get by signature; a changed source (other signature) misses");
        }
        {
            ThumbStore s(pack);
            const QImage got = s.get("tmo/b", 22);
            r.check(s.count() == 2 && !got.isNull() && qGreen(got.pixel(48, 48)) > 150 && qRed(got.pixel(48, 48)) < 100,
                    "reopened: thumbnails persist (JPEG, colour kept)");
        }
        {
            QFile f(pack);
            f.open(QIODevice::Append);
            f.write("\x05\x00tmo/c", 7);  // a record cut off by a crash
        }
        {
            ThumbStore s(pack);
            r.check(s.count() == 2, "a torn last record is ignored");
            s.put("tmo/c", 33, a);
            ThumbStore again(pack);
            r.check(again.count() == 3 && !again.get("tmo/c", 33).isNull() && !again.get("tmo/a", 11).isNull(),
                    "the next write truncates the torn tail and appends cleanly");
        }
        {
            ThumbStore s(pack);
            for (int i = 0; i < 400; ++i) s.put("tmo/a", quint64(100 + i), i % 2 ? a : b);  // many replacements
            ThumbStore again(pack);
            r.check(again.count() == 3 && !again.get("tmo/a", 499).isNull() && again.fileBytes() < 40 * 1024 && again.garbageBytes() <= 32 * 1024,
                    QString("replaced records are compacted away (%1 bytes, %2 garbage)").arg(again.fileBytes()).arg(again.garbageBytes()));
        }
        {
            // 4.13 full packs: records without a source file (signature 1) are read with getAny.
            ThumbStore s(pack);
            s.put("tmo/full", 1, b);
            ThumbStore again(pack);
            r.check(again.contains("tmo/full") && !again.getAny("tmo/full").isNull() && again.get("tmo/full", 77).isNull() &&
                        again.getAny("tmo/none").isNull(),
                    "full-pack records read with getAny whatever the source signature");
        }
    }

    r.section("sounds browser");
    {
        AssetBrowser* sb = w.soundsBrowser();
        r.check(sb && sb->assetCount() > 10000, QString("the Sounds dock lists the stock sound aliases (%1)").arg(sb ? sb->assetCount() : 0));
        const size_t pos = ctx.doc.undoPosition();
        const std::string before = ctx.doc.text();
        emit sb->soundChosen("emt_smokestack_lp", true);
        const Entity* placed = ctx.doc.findEntity(ctx.doc.inspectedEntity());
        r.check(placed && placed->classname() == "sound_emitter" && placed->value("soundalias") &&
                    *placed->value("soundalias") == "emt_smokestack_lp" && placed->value("loop") && *placed->value("loop") == "1" &&
                    ctx.doc.undoPosition() == pos + 1,
                "choosing a looping alias places a sound_emitter (soundalias, loop 1) in one undo step");
        for (int i = 0; i < 4 && ctx.doc.undoPosition() > pos; ++i) w.action("Undo")->trigger();
        r.check(ctx.doc.text() == before, "Undo removes it");
    }

    r.section("images browser");
    {
        AssetBrowser* ib = w.imagesBrowser();
        r.check(ib && ib->assetCount() > 40000, QString("the Images dock lists the stock image catalog (%1)").arg(ib ? ib->assetCount() : 0));
        // A packed colour image (name with & and ~) of a material the editor uses: exported with ACTS, then previewed.
        const QString name = "lm_accessory_don_currency_01_a_c&defaultspec~16780375389846537521";
        w.previewImage(name);
        for (int i = 0; i < 1200 && !ib->previewHasImage() && !ib->previewCaption().contains("not") &&
                        !ib->previewCaption().contains("cannot");
             ++i)
            QTest::qWait(50);
        r.check(ib->previewHasImage(), "a packed stock image is exported and previewed: " + ib->previewCaption().section('\n', 1, 1));
    }

    r.section("browser thumbnails");
    {
        // Rows show real thumbnails once their asset is cached (stock textures/models come from ACTS in the background).
        struct Case {
            AssetBrowser* b;
            const char* filter;
            const char* what;
        };
        for (const Case& c : {Case{w.materialBrowser(), "me_curb_stone_01", "materials"},
                              Case{w.modelsBrowser(), "accessory_money_pile_large", "models"},
                              Case{w.imagesBrowser(), "lm_accessory_don_currency_01_a_c", "images"}}) {
            if (!c.b) {
                r.check(false, QString("%1 browser exists").arg(c.what));
                continue;
            }
            c.b->setFilter(c.filter);
            for (int i = 0; i < 1200 && c.b->thumbnailsLoaded() == 0; ++i) {
                if (i % 20 == 0) c.b->refreshThumbnails();
                QTest::qWait(50);
            }
            r.check(c.b->thumbnailsLoaded() > 0, QString("the %1 browser shows a thumbnail for '%2' (%3 loaded)")
                                                     .arg(c.what, c.filter)
                                                     .arg(c.b->thumbnailsLoaded()));
            c.b->setFilter("");
        }
    }

    r.section("prefab browser");
    {
        const QString dir = QDir(outDir).filePath("prefab_root");
        QDir(dir).removeRecursively();
        QDir().mkpath(dir + "/props");
        QFile pf(dir + "/props/crate_stack.map");
        if (pf.open(QIODevice::WriteOnly)) {
            const char* face = " common/caulk 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n";
            QByteArray t = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n{\n";
            t += QByteArray(" ( 32 32 0 ) ( 0 32 0 ) ( 0 0 0 )") + face + " ( 0 0 32 ) ( 0 32 32 ) ( 32 32 32 )" + face +
                 " ( 0 0 32 ) ( 32 0 32 ) ( 32 0 0 )" + face + " ( 32 32 32 ) ( 0 32 32 ) ( 0 32 0 )" + face +
                 " ( 32 0 32 ) ( 32 32 32 ) ( 32 32 0 )" + face + " ( 0 32 32 ) ( 0 0 32 ) ( 0 0 0 )" + face + "}\n}\n";
            pf.write(t);
            pf.close();
        }
        auto prefs = w.currentPreferences();
        const auto prefs0 = prefs;
        prefs.prefabRoots = QStringList{dir};
        w.applyPreferences(prefs);
        PrefabBrowser* pb = w.prefabBrowser();
        pb->refresh();
        int row = -1;
        for (int i = 0; i < pb->count(); ++i)
            if (pb->fileAt(i).endsWith("crate_stack.map")) row = i;
        r.check(row >= 0, QString("the Prefabs dock lists the prefab folder's maps (%1 listed)").arg(pb->count()));
        {
            QElapsedTimer t;
            t.start();
            pb->refresh();
            const qint64 ms = t.elapsed();
            r.check(pb->count() > 5000 && pb->thumbnailsLoaded() < 200 && ms < 8000,
                    QString("the bundled prefab library lists %1 prefabs in %2 ms with thumbnails drawn lazily (%3 so far)")
                        .arg(pb->count()).arg(ms).arg(pb->thumbnailsLoaded()));
        }
        r.check(!PrefabBrowser::thumbnail(dir + "/props/crate_stack.map").isNull(), "a top-down thumbnail is drawn from its brushes");
        {
            // Owner playtest 2026-11-11: pictures were cut off, names missing, prefabs could not be dragged.
            PrefabBrowser::thumbnail(dir + "/props/crate_stack.map", 256).save(QDir(outDir).filePath("prefab_thumb.png"));
            pb->setFilter("window");
            for (QWidget* pw = pb; pw; pw = pw->parentWidget())
                if (auto* dw = qobject_cast<ads::CDockWidget*>(pw)) {
                    dw->toggleView(true);
                    dw->setAsCurrentTab();
                }
            QTest::qWait(900);
            pb->grab().save(QDir(outDir).filePath("prefab_browser.png"));
            r.check(w.dropAsset("prefabs", pb->fileAt(row), false, Vec3{64, 64, 0}, {kNoId, kNoId}),
                    "a prefab dragged into a view is placed at the drop point");
            bool at = false;
            for (const auto& e : ctx.doc.map().entities)
                if (e.classname() == "misc_prefab")
                    if (const std::string* o = e.value("origin")) at |= *o == "64 64 0";
            r.check(at, "the dropped misc_prefab has the drop point as its origin");
            w.action("Undo")->trigger();
        }
        pb->setFilter("crate");
        r.check(pb->visibleCount() >= 1, "the filter keeps matching prefabs");
        pb->setFilter("zzz_no_match");
        r.check(pb->visibleCount() == 0, "the filter hides the rest");
        pb->setFilter("");
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        QString perr;
        const bool stamped = row >= 0 && pb->stamp(row, &perr);
        size_t prefabsInMap = 0;
        for (const auto& e : ctx.doc.map().entities) prefabsInMap += e.classname() == "misc_prefab";
        r.check(stamped && prefabsInMap >= 1 && ctx.doc.undoPosition() == pos + 1, "double-click stamps a misc_prefab (one undo step)" + (perr.isEmpty() ? QString() : " (" + perr + ")"));
        for (int i = 0; i < 8 && ctx.doc.undoPosition() > pos; ++i) w.action("Undo")->trigger();
        r.check(ctx.doc.text() == before, "Undo removes it");
        w.applyPreferences(prefs0);
    }

    r.section("preferences");
    {
        const auto before = w.currentPreferences();
        r.check(w.action("Preferences") && w.action("UserGuide"), "Edit > Preferences and Help > Mapper's Guide exist");
        auto p = before;
        p.cameraSpeed = 1234;
        p.cameraFov = 75;
        p.autosaveMinutes = 0;
        p.textureLock = !before.textureLock;
        p.prefabRoots = QStringList{"C:/nonexistent/prefabs"};
        w.applyPreferences(p);
        const auto now = w.currentPreferences();
        r.check(ctx.cam.speed == 1234 && ctx.cam.fov == 75 && now.autosaveMinutes == 0 && now.textureLock == p.textureLock &&
                    now.prefabRoots == p.prefabRoots && w.action("ToggleTextureLock")->isChecked() == p.textureLock,
                QString("applying preferences changes the camera, autosave, texture lock (menu check in step) and prefab folders (%1 %2 %3 %4/%5 %6 %7)").arg(ctx.cam.speed).arg(ctx.cam.fov).arg(now.autosaveMinutes).arg(now.textureLock).arg(p.textureLock).arg(now.prefabRoots.join(";")).arg(w.action("ToggleTextureLock")->isChecked()));
        w.applyPreferences(before);
        const auto back = w.currentPreferences();
        r.check(back.cameraSpeed == before.cameraSpeed && back.textureLock == before.textureLock && back.autosaveMinutes == before.autosaveMinutes,
                "applying the old preferences restores them");
    }

    r.section("lit preview");
    {
        auto sumRgb = [&] {
            std::vector<iw8r::app::GpuVertex> tris, lines, overlay;
            iw8r::app::buildCameraGeometry(ctx.scene, ctx.doc, false, tris, lines, overlay);
            double s = 0;
            for (const auto& v : tris) s += (v.rgba & 0xff) + ((v.rgba >> 8) & 0xff) + ((v.rgba >> 16) & 0xff);
            return std::make_pair(s, tris.size());
        };
        const bool was = ctx.scene.litPreview;
        if (was) w.action("ToggleLitPreview")->trigger();
        const auto unlit = sumRgb();
        w.action("ToggleLitPreview")->trigger();
        r.check(ctx.scene.litPreview && w.action("ToggleLitPreview")->isChecked(), "Ctrl+L turns the lit preview on (checked)");
        const auto lit = sumRgb();
        r.check(lit.second == unlit.second && lit.second > 0 && lit.first != unlit.first,
                QString("lit shading changes face colours (%1 tris)").arg(lit.second));
        const auto savedLights = ctx.scene.lights;
        iw8r::app::SceneCache::PreviewLight L;
        L.pos = ctx.doc.map().entities.empty() ? Vec3{} : Vec3{0, 0, 64};
        L.radius = 100000;
        L.color = Vec3{1, 1, 1};
        ctx.scene.lights.push_back(L);
        r.check(sumRgb().first > lit.first, "an extra omni light brightens the lit preview");
        ctx.scene.lights = savedLights;
        w.action("ToggleLitPreview")->trigger();
        r.check(!ctx.scene.litPreview, "Ctrl+L again turns it off");
        if (was) w.action("ToggleLitPreview")->trigger();
    }

    r.section("camera modes");
    {
        struct Geo {
            std::vector<iw8r::app::GpuVertex> tris, lines, overlay;
        };
        auto build = [&] {
            Geo g;
            iw8r::app::buildCameraGeometry(ctx.scene, ctx.doc, false, g.tris, g.lines, g.overlay);
            return g;
        };
        auto colourSum = [](const std::vector<iw8r::app::GpuVertex>& v) {
            double s = 0;
            for (const auto& t : v) s += (t.rgba & 0xff) + ((t.rgba >> 8) & 0xff) + ((t.rgba >> 16) & 0xff);
            return s;
        };
        const auto mode = [&](const char* cmd) {
            w.action(cmd)->trigger();
            return ctx.scene.cameraMode;
        };
        const bool litWas = ctx.scene.litPreview;
        if (litWas) w.action("ToggleLitPreview")->trigger();

        r.check(w.action("CameraModeTextured") && w.action("CameraModeFlat") && w.action("CameraModeWireframe") &&
                    w.action("CameraModeFullbright") && w.action("CameraModeNormals"),
                "View > Camera Mode has the five mode actions");
        r.check(w.action("CameraModeWireframe")->isCheckable() && w.action("CameraModeTextured")->isCheckable(),
                "camera mode actions are checkable");

        const Geo flat = (mode("CameraModeFlat"), build());
        r.check(ctx.scene.cameraMode == iw8r::app::CameraMode::Flat && !ctx.texturedView && w.action("CameraModeFlat")->isChecked(),
                "Flat turns textures off and is checked");
        r.check(!flat.tris.empty(), "Flat draws shaded faces");

        const Geo wire = (mode("CameraModeWireframe"), build());
        r.check(ctx.scene.cameraMode == iw8r::app::CameraMode::Wireframe && !ctx.texturedView && w.action("CameraModeWireframe")->isChecked(),
                "Wireframe is checked and turns textures off");
        r.check(wire.tris.empty(), "wireframe draws no filled triangles");
        r.check(wire.lines.size() > 6, QString("wireframe draws edge lines (%1)").arg(wire.lines.size()));

        const Geo full = (mode("CameraModeFullbright"), build());
        r.check(ctx.scene.cameraMode == iw8r::app::CameraMode::Fullbright && ctx.texturedView,
                "Fullbright keeps textures on (texturedView) and is checked");
        r.check(full.tris.size() == flat.tris.size() && colourSum(full.tris) > colourSum(flat.tris),
                "fullbright is unshaded, so it is brighter than flat");

        const Geo norm = (mode("CameraModeNormals"), build());
        r.check(ctx.scene.cameraMode == iw8r::app::CameraMode::Normals && !ctx.texturedView,
                "Normals is checked and turns textures off");
        int upFaces = 0;
        for (const auto& t : norm.tris) {
            const int R = t.rgba & 0xff, G = (t.rgba >> 8) & 0xff, B = (t.rgba >> 16) & 0xff;
            if (std::abs(R - 128) <= 2 && std::abs(G - 128) <= 2 && B >= 253) ++upFaces;
        }
        r.check(upFaces > 0, QString("normals colour upward faces near (128,128,255) (%1 tris)").arg(upFaces));
        r.check(norm.tris.size() == flat.tris.size() && colourSum(norm.tris) != colourSum(flat.tris),
                "normals recolours the same faces");

        // 3.13: Collision Only drops what the player passes through (markers, trigger volumes, non-colliding brushes).
        const Geo coll = (mode("CameraModeCollision"), build());
        r.check(ctx.scene.cameraMode == iw8r::app::CameraMode::Collision && !coll.tris.empty() && coll.tris.size() < flat.tris.size(),
                QString("Collision Only draws fewer triangles than Flat (%1 of %2)").arg(coll.tris.size()).arg(flat.tris.size()));
        // Selection highlight setting changes the tint of a selected face.
        {
            const uint32_t oldTint = ctx.scene.selectionTint;
            const double oldAmount = ctx.scene.selectionTintAmount;
            w.setSelectionHighlight(QColor(0, 255, 0), 0.8);
            r.check(ctx.scene.selectionTint == 0xff00ff00u && std::abs(ctx.scene.selectionTintAmount - 0.8) < 1e-9,
                    "View > Selection Highlight sets the tint colour and strength");
            w.setSelectionHighlight(QColor(int(oldTint & 0xff), int((oldTint >> 8) & 0xff), int((oldTint >> 16) & 0xff)), oldAmount);
        }
        mode("CameraModeFlat");
        // 3.22: four-view layout: two more 2D views with fixed axes, each with its own zoom.
        {
            const double scale0 = ctx.gridScale;
            w.fourViewLayout();
            QApplication::processEvents();
            QList<GridView*> views = w.findChildren<GridView*>();
            int front = 0, side = 0;
            for (GridView* g : views) front += g->fixedAxis() == 1, side += g->fixedAxis() == 2;
            r.check(front == 1 && side == 1 && ctx.gridAxis == 0, QString("View > Four-View Layout adds Front (XZ) and Side (YZ) views (%1 2D views)").arg(views.size()));
            for (GridView* g : views)
                if (g->fixedAxis() == 1) {
                    QWheelEvent we(QPointF(20, 20), g->mapToGlobal(QPointF(20, 20)), QPoint(), QPoint(0, 240), Qt::NoButton, Qt::NoModifier,
                                   Qt::NoScrollPhase, false);
                    QApplication::sendEvent(g, &we);
                }
            r.check(ctx.gridScale == scale0 && ctx.gridAxis == 0, "zooming the front view leaves the main view's zoom and axis alone");
            w.fourViewLayout();  // again: reuses the views
            r.check(w.findChildren<GridView*>().size() == views.size(), "running it again reuses the views");
        }

        {
            // Material Info / Model Info (4.16 / 4.18): built from the caches, no ACTS call.
            const QString mh = w.showMaterialInfo("caulk");
            r.check(mh.contains("tool material") && w.findChild<QDialog*>("materialInfoDialog") != nullptr,
                    "Material Info describes a tool material and opens its window");
            iw8r::qt::MaterialInfoInput mi;
            mi.name = "tmo/test_wall";
            mi.faces = 6, mi.primitives = 1;
            mi.buildsNormal = true;
            mi.entry = iw8r::qt::MaterialCache::Entry{};
            mi.entry->zone = "mp_test";
            mi.entry->techset = "tw_l1c0n0";
            mi.entry->textures = {"0=test_wall_c", "9=test_wall_n", "27=test_wall_a", "200=test_wall_x"};
            const QString html = iw8r::qt::materialInfoHtml(mi);
            r.check(html.contains("6 face(s) on 1 brush") && html.contains("tw_l1c0n0") && html.contains("0x09") &&
                        html.contains("normal + gloss") && html.contains("slot 200") && html.contains("not built") &&
                        html.contains("Draws as"),
                    "Material Info lists usage, technique set and the texture table by slot");
            iw8r::qt::ModelInfoInput mo;
            mo.name = "test_crate";
            mo.placed = 3;
            mo.entry = iw8r::qt::ModelCache::Entry{};
            mo.entry->zone = "mp_test";
            mo.entry->bounds = {-8, -8, 0, 8, 8, 32};
            mo.entry->materials = {"mo/test_crate"};
            const QString mhtml = iw8r::qt::modelInfoHtml(mo);
            r.check(mhtml.contains("3 time(s)") && mhtml.contains("16.0 x 16.0 x 32.0") && mhtml.contains("mo/test_crate"),
                    "Model Info lists placements, size and materials");
            if (auto* d = w.findChild<QDialog*>("materialInfoDialog")) d->close();
        }
        {
            // Drag and drop from the browsers (4.24): the drop handler places at the given point, one undo step each.
            const size_t before = ctx.doc.map().entities.size();
            const bool placed = w.dropAsset("models", "iw8r_drop_test_model", false, iw8r::Vec3{104, 203, 16}, {iw8r::kNoId, iw8r::kNoId});
            bool at = false;
            for (const auto& e : ctx.doc.map().entities)
                if (const std::string* m = e.value("model"); m && *m == "iw8r_drop_test_model")
                    if (const std::string* o = e.value("origin")) {
                        double x = 0, y = 0, z = 0;
                        const double g = ctx.snap ? ctx.doc.grid() : 0.01;
                        at = std::sscanf(o->c_str(), "%lf %lf %lf", &x, &y, &z) == 3 && std::fabs(x - 104) <= g &&
                             std::fabs(y - 203) <= g && std::fabs(z - 16) <= g;
                    }
            r.check(placed && at && ctx.doc.map().entities.size() == before + 1, "a dropped model is placed at the drop point");
            const bool fx = w.dropAsset("effects", "vfx/test/drop", false, iw8r::Vec3{64, 64, 0}, {iw8r::kNoId, iw8r::kNoId});
            r.check(fx && ctx.doc.map().entities.size() == before + 2, "a dropped effect places an fx_origin");
            r.check(!w.dropAsset("materials", "caulk", false, iw8r::Vec3{}, {iw8r::kNoId, iw8r::kNoId}),
                    "a material dropped on nothing does nothing");
            {
                // Owner request 2026-11-11: a material dragged over a model previews on it, leaving takes it back,
                // dropping keeps it (key iw8r_material); the same for a brush.
                iw8r::SelRef model{iw8r::kNoId, iw8r::kNoId}, brush{iw8r::kNoId, iw8r::kNoId};
                std::string brushMat;
                for (const auto& e : ctx.doc.map().entities) {
                    if (const std::string* m = e.value("model"); m && *m == "iw8r_drop_test_model") model.entity = e.id;
                    if (e.isWorldspawn())
                        for (const auto& it : e.items)
                            if (const iw8r::Primitive* pr = it.prim(); pr && pr->kind == iw8r::PrimKind::Brush && brush.entity == iw8r::kNoId) {
                                brush = {e.id, it.id};
                                brushMat = pr->faces.front().material();
                            }
                }
                const auto modelMat = [&]() -> std::string {
                    const iw8r::Entity* e = ctx.doc.findEntity(model.entity);
                    const std::string* v = e ? e->value("iw8r_material") : nullptr;
                    return v ? *v : std::string();
                };
                const auto brushNow = [&]() -> std::string {
                    const iw8r::EntityItem* it = ctx.doc.findItem(brush.entity, brush.item);
                    return it && it->prim() ? it->prim()->faces.front().material() : std::string();
                };
                const size_t pos = ctx.doc.undoPosition();
                w.previewMaterialAt("iw8r_hover_mat", model);
                r.check(w.materialPreviewActive() && modelMat() == "iw8r_hover_mat", "a material hovering over a model previews on it");
                w.previewMaterialAt("iw8r_hover_mat", brush);
                r.check(modelMat().empty() && brushNow() == "iw8r_hover_mat", "moving on to a brush takes the model preview back and previews on the brush");
                w.endMaterialPreview(false);
                r.check(!w.materialPreviewActive() && brushNow() == brushMat && ctx.doc.undoPosition() == pos,
                        "leaving the view restores the brush and leaves no undo step");
                w.previewMaterialAt("iw8r_hover_mat", model);
                r.check(w.dropAsset("materials", "iw8r_hover_mat", false, iw8r::Vec3{}, model) && modelMat() == "iw8r_hover_mat" &&
                            ctx.doc.undoPosition() == pos + 1,
                        "dropping keeps the previewed material on the model (one undo step)");
                ctx.doc.undo();
                r.check(modelMat().empty(), "undo removes the model's material");
                // Owner report 2026-11-11: picking a material with models selected did nothing (a staircase of
                // shape-model cubes kept its texture). A picked material now goes on selected models and brushes alike.
                const size_t pos2 = ctx.doc.undoPosition();
                ctx.doc.select(model, false);
                ctx.doc.select(brush, true);
                r.check(iw8r::qt::applyMaterialToSelection(ctx, "iw8r_pick_mat") && modelMat() == "iw8r_pick_mat" && brushNow() == "iw8r_pick_mat",
                        "a picked material is applied to the selected model (iw8r_material) and the selected brush");
                r.check(!iw8r::qt::applyMaterialToSelection(ctx, "iw8r_pick_mat"), "applying the same material again changes nothing");
                ctx.doc.undo();
                r.check(modelMat().empty() && brushNow() == brushMat && ctx.doc.undoPosition() == pos2,
                        "one undo takes the material off the model and the brush together");
                ctx.doc.select(model, false);
                r.check(iw8r::qt::applyMaterialToSelection(ctx, "iw8r_pick_mat") && modelMat() == "iw8r_pick_mat", "a model alone takes a picked material");
                ctx.doc.undo();
                ctx.doc.deselectAll();
            }
            ctx.doc.undo();
            ctx.doc.undo();
            ctx.documentEdited("undo drops");
            r.check(ctx.doc.map().entities.size() == before, "undo removes the dropped entities");
        }
        {
            // Script dock (6.11 / 6.12): builtin list, checks and behaviour snippets. No file is written here.
            iw8r::qt::ScriptPanel* sp = w.scriptPanel();
            r.check(sp && sp->builtinCount() > 2000, QString("the Script dock loads the IW8 builtin list (%1 names)").arg(sp ? sp->builtinCount() : 0));
            if (sp) {
                r.check(sp->scriptPath().isEmpty() && !sp->insertSnippet("door"), "without a map script nothing can be inserted");
                sp->setText(QString::fromStdString(iw8r::userScriptTemplate("mp_test")));
                r.check(sp->check() == 0 && sp->issueCount() == 0, "the script template passes the checks");
                sp->setText("user_main()\n{\n    x = (1;\n");
                r.check(sp->check() >= 2 && sp->issueCount() >= 2, "broken brackets are listed with their lines");
                sp->reload();
                r.check(!sp->modified() && sp->text().isEmpty(), "Reload drops the unsaved text");
            }
        }
        {
            // Time of Day (5.19): one undo step from the menu command.
            const size_t pos = ctx.doc.undoPosition();
            QAction* tod = w.action("TimeOfDay_dusk");
            if (tod) tod->trigger();
            const iw8r::Entity* ws = ctx.doc.worldspawn();
            const std::string* sc = ws ? ws->value("suncolor") : nullptr;
            r.check(tod && sc && *sc == "1 0.58 0.32" && ctx.doc.undoPosition() == pos + 1,
                    "Compile > Time of Day > Dusk sets the sun in one undo step");
            if (ctx.doc.undoPosition() > pos) {
                ctx.doc.undo();
                ctx.documentEdited("undo time of day");
            }
        }
        {
            // Command palette (9.13): every menu command, fuzzy search, runs the best match.
            const auto entries = iw8r::qt::paletteEntries(w.menuBar());
            r.check(entries.size() > 150, QString("the command palette lists the menu commands (%1)").arg(entries.size()));
            r.check(iw8r::qt::paletteScore("tod dusk", "Compile > Time of Day > Dusk (low warm sun)") >= 0 &&
                        iw8r::qt::paletteScore("xyzzy", "Compile > Time of Day > Dusk") < 0,
                    "palette search matches words and initials");
            iw8r::qt::CommandPalette pal(w.menuBar(), &w);
            pal.setQuery("tod dusk");
            r.check(pal.visibleCount() >= 1 && pal.firstPath().contains("Dusk"), "the palette finds Time of Day > Dusk: " + pal.firstPath());
            const size_t pos = ctx.doc.undoPosition();
            pal.runFirst();
            r.check(ctx.doc.undoPosition() == pos + 1, "Enter in the palette runs the command");
            if (ctx.doc.undoPosition() > pos) {
                ctx.doc.undo();
                ctx.documentEdited("undo palette");
            }
        }
        if (iw8r::qt::OutlinerPanel* ol = w.outliner()) {
            // Outliner (2.29): classes, filter, stats, selection both ways.
            ol->setFilter("");
            const int all = ol->entityCount();
            r.check(ol->classCount() >= 1 && all >= 1 && ol->statsText().contains("brushes"),
                    QString("the Outliner lists %1 entities in %2 classes: %3").arg(all).arg(ol->classCount()).arg(ol->statsText()));
            ol->setFilter("zzzz_no_such_entity");
            r.check(ol->entityCount() == 0, "the Outliner filter hides what does not match");
            ol->setFilter("");
            ctx.doc.deselectAll();
            const int picked = ol->selectMatching(" ") > 0 ? 1 : ol->selectMatching("");
            r.check(picked > 0 && !ctx.doc.selection().empty(), "selecting Outliner rows selects the entities");
            ctx.doc.deselectAll();
            ctx.documentEdited("deselect");
        }
        {
            // Camera bookmarks (3.22): save slot 9, move away, go back.
            const auto saved = ctx.cam;
            w.saveCameraBookmark(9);
            ctx.cam.pos = {9999, 9999, 9999};
            ctx.cam.yaw = 1;
            const bool back = w.goToCameraBookmark(9);
            r.check(back && std::fabs(ctx.cam.pos.x - saved.pos.x) < 1e-3 && std::fabs(ctx.cam.yaw - saved.yaw) < 1e-3,
                    "Ctrl+9 returns the camera to bookmark 9");
            ctx.cam = saved;
        }
        if (iw8r::qt::AssetBrowser* fx = w.effectsBrowser(); fx && fx->assetCount() > 2) {
            // Favourites and recents (4.22).
            const QString a = fx->visibleName(0), b = fx->visibleName(1);
            fx->setFavourite(b, true);
            fx->setListMode(iw8r::qt::AssetBrowser::ListMode::Favourites);
            r.check(fx->visibleCount() == 1 && fx->visibleName(0) == b && fx->isFavourite(b), "Favourites shows only the favourite");
            fx->noteRecent(a);
            fx->setListMode(iw8r::qt::AssetBrowser::ListMode::Recent);
            r.check(fx->visibleCount() >= 1 && fx->recent().value(0) == a, "Recent lists the last pick first");
            fx->setFavourite(b, false);
            fx->setListMode(iw8r::qt::AssetBrowser::ListMode::All);
            r.check(fx->visibleCount() == fx->assetCount() && !fx->isFavourite(b), "All shows everything again");
        }
        {
            // Arrange menu (2.25): mirror copy and distribute from the menu, one undo step each.
            const std::string before = ctx.doc.text();
            ctx.doc.deselectAll();
            for (const auto& e : ctx.doc.map().entities)
                if (e.isWorldspawn())
                    for (const auto& it : e.items)
                        if (const iw8r::Primitive* p = it.prim(); p && p->kind != iw8r::PrimKind::Patch) {
                            ctx.doc.select({e.id, it.id}, false);
                            break;
                        }
            const size_t pos = ctx.doc.undoPosition(), n0 = ctx.doc.map().entities.front().items.size();
            if (QAction* a = w.action("MirrorCloneX")) a->trigger();
            r.check(ctx.doc.undoPosition() == pos + 1 && ctx.doc.map().entities.front().items.size() == n0 + 1,
                    "Brush > Arrange > Mirror Copy adds one mirrored brush in one undo step");
            ctx.doc.undo();
            ctx.documentEdited("undo mirror");
            r.check(ctx.doc.text() == before, "undo removes the mirrored copy");
            ctx.doc.deselectAll();
        }
        {
            // Camera path (8.15): two keys, the middle of the path lies between them.
            const auto saved = ctx.cam;
            w.cameraPath() = iw8r::CameraPath{};
            ctx.cam.pos = {0, 0, 100};
            w.action("CameraPathAddKey")->trigger();
            ctx.cam.pos = {600, 0, 100};
            w.action("CameraPathAddKey")->trigger();
            const double d = iw8r::cameraPathDuration(w.cameraPath());
            w.stepCameraPath(d / 2);
            r.check(w.cameraPath().keys.size() == 2 && d > 0 && std::fabs(ctx.cam.pos.x - 300) < 30,
                    QString("the camera path plays between its keys (x %1 at the middle)").arg(ctx.cam.pos.x));
            w.action("CameraPathClear")->trigger();
            ctx.cam = saved;
        }
        {
            // Primitives > Triggers (6.17): a hurt trigger in one undo step, then undone.
            const std::string before = ctx.doc.text();
            const size_t pos = ctx.doc.undoPosition();
            std::string err;
            iw8r::AABB box;
            box.add({0, 0, 0});
            box.add({64, 64, 64});
            const iw8r::Id id = iw8r::insertTrigger(ctx.doc, "hurt", box, "t_hurt", &err);
            ctx.documentEdited("trigger");
            r.check(id != iw8r::kNoId && ctx.doc.undoPosition() == pos + 1 && w.action("Trigger_hurt") != nullptr,
                    "Primitives > Triggers inserts a trigger_hurt in one undo step");
            ctx.doc.undo();
            ctx.documentEdited("undo trigger");
            r.check(ctx.doc.text() == before, "undo removes the trigger");
        }
        {
            // Spline wall (2.26) through three placed script_origins, one undo step.
            const std::string before = ctx.doc.text();
            const iw8r::Id a = ctx.doc.addEntity({{"classname", "script_origin"}, {"origin", "0 0 0"}}, {}, "p");
            const iw8r::Id b = ctx.doc.addEntity({{"classname", "script_origin"}, {"origin", "256 0 0"}}, {}, "p");
            const iw8r::Id c = ctx.doc.addEntity({{"classname", "script_origin"}, {"origin", "256 256 0"}}, {}, "p");
            ctx.doc.deselectAll();
            for (iw8r::Id id : {a, b, c}) ctx.doc.select({id, iw8r::kNoId}, true);
            const size_t pos = ctx.doc.undoPosition();
            const size_t n = w.splineFromSelection("wall", 16, 96, false);
            r.check(n >= 2 && ctx.doc.undoPosition() == pos + 1, QString("Brush > Spline > Wall builds %1 brushes in one undo step").arg(n));
            while (ctx.doc.text() != before && ctx.doc.undo()) {}
            ctx.documentEdited("undo spline");
            r.check(ctx.doc.text() == before, "undo removes the spline wall and its points");
        }
        if (iw8r::qt::AssetBrowser* mb = w.modelBrowser(); mb && mb->kits().contains("containers")) {
            // Kits (4.23): the containers kit shows only container models.
            mb->setKit("containers");
            const int n = mb->visibleCount();
            r.check(n > 0 && n < mb->assetCount(), QString("the Models browser's containers kit shows %1 of %2 models").arg(n).arg(mb->assetCount()));
            mb->setKit("");
            r.check(mb->visibleCount() == mb->assetCount() || mb->visibleCount() > n, "All kits shows everything again");
        }
        if (w.compilePanel()) {
            // Error round trip (7.13): a log line naming "entity 1" selects map.entities[1].
            ctx.doc.deselectAll();
            const int n = w.compilePanel()->selectFromLogLine("warning: LEAK from script_origin (entity 1); Tools > Leak Check draws the path.");
            bool right = false;
            for (const auto& r2 : ctx.doc.selection()) right |= ctx.doc.map().entities.size() > 1 && r2.entity == ctx.doc.map().entities[1].id;
            r.check(n == 1 && right, "double-clicking a compile log line selects the entity it names");
            ctx.doc.deselectAll();
            ctx.documentEdited("deselect");
        }
        {
            // Command script (9.10): add, select and set in one undo step; a bad line rolls back.
            const std::string before = ctx.doc.text();
            const QString ok = w.runScriptText("add script_origin 0 0 64 targetname iw8r_cmd_test\nset script_noteworthy hello", true);
            bool found = false;
            for (const auto& e : ctx.doc.map().entities)
                if (const std::string* t = e.value("targetname"); t && *t == "iw8r_cmd_test")
                    found = e.value("script_noteworthy") && *e.value("script_noteworthy") == "hello";
            r.check(found, "Run Command Script adds and edits an entity: " + ok);
            ctx.doc.undo();
            ctx.documentEdited("undo script");
            r.check(ctx.doc.text() == before, "one undo removes the whole script");
            {
                // Macro recording (9.10): a live move is recorded; a script run while recording is not.
                ctx.doc.deselectAll();
                for (const auto& e : ctx.doc.map().entities)
                    if (!e.isWorldspawn()) {
                        ctx.doc.select({e.id, iw8r::kNoId}, false);
                        break;
                    }
                const std::string start = ctx.doc.text();
                w.toggleMacroRecording();
                ctx.doc.translateSelection(iw8r::Vec3{16, 0, 0});
                ctx.doc.translateSelection(iw8r::Vec3{16, 0, 0});
                w.runScriptText("move 0 0 8", false);
                const QString rec = w.toggleMacroRecording();
                r.check(!w.macroRecording() && rec == "move 32 0 0\n", "macro recording keeps the live moves (merged) and skips script runs: " + rec);
                ctx.doc.undo();
                ctx.doc.undo();
                ctx.doc.undo();
                ctx.documentEdited("undo macro");
                r.check(ctx.doc.text() == start, "macro test edits undo");
                ctx.doc.deselectAll();
            }
            w.runScriptText("add script_origin 0 0 0\nnosuchcommand", true);
            r.check(ctx.doc.text() == before, "a failing script leaves the map as it was");
        }
        {
            // Hotkey presets (2.30) and Effect Info (8.9).
            QApplication::processEvents();  // the presets menu is built after the menus
            r.check(w.findChild<QMenu*>("hotkeyPresetsMenu") && !w.findChild<QMenu*>("hotkeyPresetsMenu")->actions().isEmpty(),
                    "Edit > Hotkey Presets lists the presets");
            const QString fx = w.showEffectInfo("vfx/core/mp/core/vfx_hp_chev_grey");
            r.check(fx.contains("Emitters") || fx.contains("No export"), "Effect Info shows the effect's export (or says it has none yet)");
            {
                // Model detail (3.18): LODs with the camera's LOD, bones and tags from an xmodel export.
                iw8r::XModelInfo xm;
                const QString xf = QDir(QCoreApplication::applicationDirPath() + "/../../tests/data/xmodel_probe")
                                       .absoluteFilePath("body_pilot_helicopter_british.json");
                iw8r::qt::ModelInfoInput mi;
                mi.name = "body_pilot_helicopter_british";
                if (iw8r::readXModelExport(xf.toStdString(), xm)) mi.detail = xm;
                mi.cameraDistance = 100;
                const QString h = iw8r::qt::modelInfoHtml(mi);
                r.check(mi.detail && h.contains("LODs (") && h.contains("(camera now)") && h.contains("tags:"),
                        QString("Model Info lists LODs, the camera's LOD and tags (%1 bones)").arg(mi.detail ? mi.detail->bones.size() : 0));
            }
            // FX preview (8.1): the test export plays as particles.
            w.showEffectInfo(QDir(QCoreApplication::applicationDirPath() + "/../../tests/data/vfx_sim").absoluteFilePath("vfx_hp_chev_grey.vfx.33.asset.json"));
            if (auto* d = w.findChild<QDialog*>("effectInfoDialog")) {
                auto* pv = d->findChild<iw8r::qt::VfxPreview*>();
                if (pv) {
                    pv->setTime(0.5);
                    d->grab().save(QDir(outDir).filePath("fx_preview.png"));
                }
                r.check(pv && pv->isVisible() && pv->effect().emitters.size() == 4 && pv->particlesNow() > 0,
                        QString("the FX preview plays the effect (%1 particles at 0.5 s)").arg(pv ? pv->particlesNow() : -1));
                auto* tb = d->findChild<QTextBrowser*>();
                r.check(tb && tb->toPlainText().contains("Bounds (simulated)"), "Effect Info shows the simulated bounds (8.9)");
                d->close();
            }
            {
                // 8.9: an always-loaded effect with no export yet is exported with ACTS on demand.
                const QString name = "vfx/_requests/mp_gameplay/vfx_smk_signal";
                QString html = w.showEffectInfo(name);
                QElapsedTimer t;
                t.start();
                while (!html.contains("Emitters") && t.elapsed() < 120000) {
                    QTest::qWait(500);
                    if (!html.contains("Exporting")) break;
                    html = w.showEffectInfo(name);
                }
                r.check(html.contains("Emitters") || html.contains("not in an always-loaded zone"),
                        QString("Effect Info exports an always-loaded effect on demand (%1 s)").arg(t.elapsed() / 1000));
                if (auto* d = w.findChild<QDialog*>("effectInfoDialog")) d->close();
            }
        }
        {
            // UV editor (2.27): offset a selected brush's texture in one undo step.
            const std::string before = ctx.doc.text();
            ctx.doc.deselectAll();
            for (const auto& e : ctx.doc.map().entities)
                if (e.isWorldspawn())
                    for (const auto& it : e.items)
                        if (const iw8r::Primitive* p = it.prim(); p && p->kind == iw8r::PrimKind::Brush) {
                            ctx.doc.select({e.id, it.id}, false);
                            break;
                        }
            {
                // CoD4 port wizard (11.9): a .ff plans the iw3 -> iw8 dumpzone step; a bad name is an error.
                const QString ff = QDir(outDir).filePath("mp_crash.ff");
                QFile f(ff);
                if (f.open(QIODevice::WriteOnly)) f.write(QByteArray("IWffu100").append(4, char(1)));
                f.close();
                iw8r::qt::PortWizardDialog* pw = w.openPortWizard();
                pw->setInput(ff, "mp_crash", outDir, QDir(outDir).filePath("port"));
                const QString plan = pw->planText();
                const bool routeA = pw->plan().source == iw8r::PortSource::BuiltFastFile;
                pw->setInput(ff, "x", outDir, QDir(outDir).filePath("port"));
                r.check(routeA && plan.contains("dumpzone") && plan.contains("--target iw8") && !pw->plan().errors.empty(),
                        "the port wizard plans the CoD4 .ff conversion and rejects a bad map name: " + plan.left(200));
                // Owner 2026-11-11: CoD4 porting is out of the UI for now (the dialog code stays for later).
                r.check(w.action("PortCod4Map") == nullptr, "File > Port CoD4 Map is not in the menus");
                pw->close();
                QFile::remove(ff);
            }
            iw8r::qt::UvEditor* uv = w.openUvEditor();
            const size_t pos = ctx.doc.undoPosition();
            uv->setPending(0.25, 0, 1, 15);
            uv->grab().save(QDir(outDir).filePath("uv_editor.png"));
            r.check(uv->faceCount() >= 6 && uv->apply() && ctx.doc.undoPosition() == pos + 1 && ctx.doc.text() != before,
                    QString("the UV editor moves the texture of %1 faces in one undo step").arg(uv->faceCount()));
            ctx.doc.undo();
            ctx.documentEdited("undo uv");
            r.check(ctx.doc.text() == before, "undo restores the texture fields");
            {
                // Unwrap: a box brush lays out as a net (six faces, distinct places), then a vertex drag moves that corner.
                uv->loadSelection();
                {
                    // Mesh layout (default): the brush unfolded with its own textures; opening changes nothing; a vertex
                    // drag moves that face's UVs only.
                    uv->grab().save(QDir(outDir).filePath("uv_layout.png"));
                    const auto f0 = uv->faceUvs(0), f1 = uv->faceUvs(1);
                    const bool dragged = uv->dragLayoutVertex(0, 0, 8, 0);
                    r.check(uv->layoutView() && ctx.doc.text() == before && dragged && uv->faceUvs(0) != f0 && uv->faceUvs(1) == f1,
                            "the UV editor opens on the mesh layout without changing the map; a vertex drag edits that face");
                    uv->loadSelection();  // drop the pending drag
                }
                const int laid = uv->unwrap();
                uv->grab().save(QDir(outDir).filePath("uv_unwrap.png"));
                std::set<std::pair<long long, long long>> centres;
                for (int f = 0; f < uv->faceCount(); ++f) {
                    double cu = 0, cv = 0;
                    const auto uvs = uv->faceUvs(f);
                    for (const auto& q : uvs) cu += q[0] / double(uvs.size()), cv += q[1] / double(uvs.size());
                    centres.insert({std::llround(cu * 1000), std::llround(cv * 1000)});
                }
                r.check(laid == uv->faceCount() && int(centres.size()) == uv->faceCount(),
                        QString("Unwrap lays the %1 faces out as a net (%2 distinct places)").arg(laid).arg(centres.size()));
                const auto before0 = uv->faceUvs(0)[0];
                uv->moveVertex(0, 0, before0[0] + 0.3, before0[1]);
                const auto after0 = uv->faceUvs(0)[0];
                r.check(after0[0] > before0[0] + 0.1, QString("dragging a vertex handle moves that UV (%1 -> %2)").arg(before0[0]).arg(after0[0]));
                const size_t p2 = ctx.doc.undoPosition();
                r.check(uv->apply() && ctx.doc.undoPosition() == p2 + 1, "the unwrapped layout applies in one undo step");
                ctx.doc.undo();
                ctx.documentEdited("undo unwrap");
                r.check(ctx.doc.text() == before, "undo restores the faces");
            }
            {
                // Patches: every control point has its own UV (true per-vertex), written back in file units.
                ctx.doc.deselectAll();
                for (const auto& e : ctx.doc.map().entities)
                    for (const auto& it : e.items)
                        if (const iw8r::Primitive* p = it.prim(); p && p->kind == iw8r::PrimKind::Patch) ctx.doc.select({e.id, it.id}, true);
                uv->loadSelection();
                const size_t p0 = ctx.doc.undoPosition();
                bool moved = false;
                if (uv->patchCount() > 0) {
                    const auto c0 = uv->patchUvs(0);
                    moved = uv->movePatchVertex(0, int(c0.size()) - 1, c0.back()[0] + 0.5, c0.back()[1] - 0.25);
                    const auto c1 = uv->patchUvs(0);
                    moved = moved && std::fabs(c1.back()[0] - c0.back()[0] - 0.5) < 1e-9 && c1.front() == c0.front();
                }
                uv->grab().save(QDir(outDir).filePath("uv_patch.png"));
                r.check(uv->patchCount() > 0 && moved && uv->apply() && ctx.doc.undoPosition() == p0 + 1,
                        QString("the UV editor moves one patch control point's UV (%1 patch(es)) in one undo step").arg(uv->patchCount()));
                ctx.doc.undo();
                ctx.documentEdited("undo patch uv");
                r.check(ctx.doc.text() == before, "undo restores the patch");
                ctx.doc.deselectAll();
            }
            uv->close();
            ctx.doc.deselectAll();
        }
        const Geo cont = (mode("CameraModeContents"), build());
        int grey = 0;
        for (const auto& t : cont.tris) {
            const int R = t.rgba & 0xff, G = (t.rgba >> 8) & 0xff, B = (t.rgba >> 16) & 0xff;
            if (R == G && G == B) ++grey;
        }
        r.check(ctx.scene.cameraMode == iw8r::app::CameraMode::Contents && !ctx.texturedView && cont.tris.size() == flat.tris.size() && grey > 0,
                QString("Contents colours structural brushes grey (%1 grey tris)").arg(grey));
        const Geo dens = (mode("CameraModeDensity"), build());
        int coloured = 0;
        for (const auto& t : dens.tris) {
            const int R = t.rgba & 0xff, G = (t.rgba >> 8) & 0xff, B = (t.rgba >> 16) & 0xff;
            if (!(R == G && G == B)) ++coloured;
        }
        r.check(ctx.scene.cameraMode == iw8r::app::CameraMode::Density && dens.tris.size() == flat.tris.size() && coloured > 0,
                QString("Texel Density colours brush faces by pixels per unit (%1 coloured tris)").arg(coloured));
        r.check(ctx.scene.shadowCasterVertices > 0 && ctx.scene.shadowCasterVertices < dens.tris.size(),
                QString("entity markers and tool faces are kept out of the sun shadow casters (%1 of %2 vertices cast)")
                    .arg(ctx.scene.shadowCasterVertices).arg(dens.tris.size()));

        w.action("CameraModeTextured")->trigger();
        r.check(ctx.scene.cameraMode == iw8r::app::CameraMode::Textured && ctx.texturedView && w.action("CameraModeTextured")->isChecked(),
                "Textured mode is restored at the end");
        if (litWas) w.action("ToggleLitPreview")->trigger();
    }

    r.section("replay log panel (fake game folder)");
    {
        const QString fake = QDir(outDir).filePath("fake_game");
        QDir().mkpath(fake);
        QFile lf(fake + "/mw120rproxy.log");
        if (lf.open(QIODevice::WriteOnly | QIODevice::Truncate))
            lf.write("[12:00:00.000] [INFO][Maps] scanned 2 custom map package(s)\n"
                     "[12:00:01.000] [WARN][Maps] package unavailable id=mp_bad reason=missing srv_mp_bad.ff\n"
                     "[12:00:02.000] [INFO][Core] unrelated line\n");
        lf.close();
        ReplayLogPanel panel([fake] { return fake; }, [] { return QString("mp_bad"); });
        r.check(panel.logFiles().size() == 1, "the panel finds the proxy log in the game folder");
        panel.refresh();
        r.check(panel.shownText().count('\n') == 2, "all three lines are shown");
        auto* problems = panel.findChild<QCheckBox*>();
        if (problems) problems->setChecked(true);  // first checkbox: warnings / errors
        r.check(problems && panel.shownText().contains("package unavailable") && !panel.shownText().contains("unrelated"),
                "Warnings / errors shows only the refused package line");
    }

    r.section("F5 devmap runner (scratch game folder, no game)");
    {
        // Scratch folder only: the deploy step is a stub and the "game" is a script that writes a marker and exits.
        const QString fake = QDir(outDir).absoluteFilePath("fake_devmap_game");
        QDir(fake).removeRecursively();
        QDir().mkpath(fake);
        const QString marker = fake + "/launched.txt";
        QFile script(fake + "/launch_game.cmd");
        if (script.open(QIODevice::WriteOnly | QIODevice::Truncate))
            script.write(("@echo launched> \"" + QDir::toNativeSeparators(marker) + "\"\r\n").toLocal8Bit());
        script.close();
        DevmapRunner runner;
        int deploys = 0;
        QStringList statuses;
        bool done = false, doneOk = true;
        QObject::connect(&runner, &DevmapRunner::status, [&](const QString& t, bool) { statuses << t; });
        QObject::connect(&runner, &DevmapRunner::finished, [&](bool ok, const QString&) { done = true, doneOk = ok; });
        DevmapRunner::Config cfg;
        cfg.mapId = "mp_iw8r_test";
        cfg.gameRoot = fake;
        cfg.launchExe = script.fileName();
        cfg.launchArgs = DevmapRunner::launchArgsSetting();
        runner.start(cfg, [&](QString*) { return ++deploys, true; });
        QElapsedTimer t;
        t.start();
        while (!QFileInfo::exists(marker) && t.elapsed() < 10000) QApplication::processEvents(QEventLoop::AllEvents, 50);
        r.check(deploys == 1, "game not running: the runner deploys once before it launches");
        r.check(QFileInfo::exists(marker), "the configured launcher was started (a .cmd runs through cmd /c)");
        r.check(runner.busy() && !done, "the runner then waits for the Multiplayer menu");
        r.check(statuses.join("\n").contains("not running"), "the status line says the game was not running");
        r.check(cfg.launchArgs == "+mpMode 1", "the default launch arguments boot into multiplayer (+mpMode 1)");
        runner.cancel("test");
        r.check(!runner.busy() && done && !doneOk, "cancel stops the runner and reports it");
        r.check(!CompilePanel::gameRunningFrom(fake), "no game runs from the scratch folder");
        r.check(statuses.join("\n").contains(QRegularExpression("F5 \\+\\d+\\.\\d s: ")), "F5 status lines carry the time since F5");
        {
            // The F5 options of Edit > Preferences are the devmap/* settings; values are cleaned on the way in.
            const DevmapRunner::PlayOptions before = DevmapRunner::playOptions();
            DevmapRunner::PlayOptions o;
            o.gametype = " DOM ";
            o.bots = 500;
            o.cover = false;
            o.hostCheck = false;
            o.hostCommand = " - ";
            o.startRequest = false;
            DevmapRunner::setPlayOptions(o);
            const DevmapRunner::PlayOptions got = DevmapRunner::playOptions();
            DevmapRunner::Config c2;
            DevmapRunner::applySettings(c2);
            r.check(got.gametype == "dom" && got.bots == 199 && !got.cover && !got.hostCheck && got.hostCommand == "-" && !got.startRequest &&
                        c2.gametype == "dom" && c2.bots == 199 && !c2.cover && !c2.hostCheck && c2.hostCommand.isEmpty() && !c2.startRequest,
                    "the F5 options are stored cleaned and reach the next F5 run (- means never start a host)");
            o.gametype = "war; quit";
            DevmapRunner::setPlayOptions(o);
            r.check(DevmapRunner::playOptions().gametype == "war", "a game mode that is not a plain name falls back to war");
            DevmapRunner::setPlayOptions(before);
            r.check(DevmapRunner::playOptions().gametype == before.gametype && DevmapRunner::playOptions().cover == before.cover,
                    "the F5 options are put back");
        }

        // Which exe F5 starts when Tool Paths 'launch' is empty (research 76: the stock exe cannot load custom maps).
        const QString pick = fake + "/pick";
        QDir().mkpath(pick);
        auto writeExe = [&](const QString& name, bool recompiled, const QByteArray& extra = QByteArray()) {
            QByteArray b(0x200, '\0');
            b[0] = 'M', b[1] = 'Z';
            if (recompiled) b.replace(0x40, 8, "RPLYCMP2");
            b += extra;
            QFile f(pick + "/" + name);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(b);
            return pick + "/" + name;
        };
        const QString stockExe = writeExe("game_dx12_ship_replay.exe", false);
        QString note;
        QString picked = DevmapRunner::chooseLaunchExe(QString(), stockExe, &note);
        r.check(QFileInfo(picked).fileName() == "game_dx12_ship_replay.exe" && note.startsWith("warning"),
                "only a stock exe in the game folder: it is used, with a warning that it cannot load custom maps");
        const QString dwExe = writeExe("game_dx12_ship_replay_dw.exe", true);
        const QString localExe = writeExe("game_dx12_ship_replay_local_dw.exe", true, QByteArray(300, 'x') + "t DW_DEVMAP request %s");
        picked = DevmapRunner::chooseLaunchExe(QString(), stockExe, &note);
        r.check(QFileInfo(picked).fileName() == "game_dx12_ship_replay_local_dw.exe" && !note.startsWith("warning") && !note.isEmpty(),
                "the local DW client is preferred over the Tool Paths default (the stock exe)");
        r.check(DevmapRunner::chooseLaunchExe("C:/x/launch.cmd", stockExe, &note) == "C:/x/launch.cmd" && note.isEmpty(),
                "a Tool Paths 'launch' entry is always used as it is");
        r.check(DevmapRunner::exeHasClientRequest(localExe) && !DevmapRunner::exeHasClientRequest(dwExe) &&
                    !DevmapRunner::exeHasClientRequest(script.fileName()),
                "an exe gets the start request only when it contains the client's DW_DEVMAP marker");
        {
            QFile ini(pick + "/game_dx12_ship_replay_local_dw.ini");
            if (ini.open(QIODevice::WriteOnly | QIODevice::Truncate)) ini.write("host=127.0.0.1\r\nbypass_update_restart=1\r\n");
        }
        r.check(DevmapRunner::localHostOf(localExe) == "127.0.0.1" && DevmapRunner::localHostOf(dwExe).isEmpty(),
                "the ini beside the exe tells a local-host client from the others");

        // The host probe, against a one-shot fake host on a free loopback port.
        std::atomic<int> port{0};
        std::thread fakeHost([&port] {
            QTcpServer srv;
            if (!srv.listen(QHostAddress::LocalHost, 0)) return port.store(-1);
            port.store(srv.serverPort());
            if (!srv.waitForNewConnection(8000)) return;
            QTcpSocket* c = srv.nextPendingConnection();
            c->waitForReadyRead(2000);
            // Like the real host: only an HTTP/1.1 request with a Host header gets the health answer.
            const QByteArray request = c->readAll();
            if (request.startsWith("GET /health HTTP/1.1\r\n") && request.contains("\r\nHost: 127.0.0.1:"))
                c->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"server\":\"iw8-dw\",\"local_qos_enabled\":true}");
            else
                c->write("HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"error\":\"invalid_request\"}");
            c->waitForBytesWritten(2000);
            c->disconnectFromHost();
            if (c->state() != QAbstractSocket::UnconnectedState) c->waitForDisconnected(1000);
            delete c;
        });
        t.restart();
        while (port.load() == 0 && t.elapsed() < 5000) QThread::msleep(5);
        const bool probed = port.load() > 0 && DevmapRunner::probeHost(QString("http://127.0.0.1:%1/health").arg(port.load()));
        if (!probed && port.load() > 0) {  // let the fake host's accept return
            QTcpSocket poke;
            poke.connectToHost(QHostAddress::LocalHost, quint16(port.load()));
            poke.waitForConnected(500);
        }
        fakeHost.join();
        r.check(probed, "the host probe sends a request the real host accepts (HTTP/1.1 with Host) and reads its health answer");
        const QString deadUrl = QString("http://127.0.0.1:%1/health").arg(port.load());  // the fake host is gone
        r.check(!DevmapRunner::probeHost(deadUrl), "the host probe fails when nothing listens");
        r.check(!DevmapRunner::probeHost("http://example.com/health"), "the host probe only ever talks to this machine");

        // A local-host client with no host and no command to start one: F5 stops before it starts the game.
        DevmapRunner hostRunner;
        QStringList hostStatuses;
        bool hostDone = false, hostOk = true;
        QString hostEnd;
        QObject::connect(&hostRunner, &DevmapRunner::status, [&](const QString& s, bool) { hostStatuses << s; });
        QObject::connect(&hostRunner, &DevmapRunner::finished, [&](bool ok, const QString& s) { hostDone = true, hostOk = ok, hostEnd = s; });
        DevmapRunner::Config hc;
        hc.mapId = "mp_iw8r_test";
        hc.gameRoot = pick;
        hc.launchExe = localExe;
        hc.hostHealthUrl = deadUrl;
        hc.hostCommand.clear();
        int hostDeploys = 0;
        hostRunner.start(hc, [&](QString*) { return ++hostDeploys, true; });
        t.restart();
        while (!hostDone && t.elapsed() < 10000) QApplication::processEvents(QEventLoop::AllEvents, 50);
        r.check(hostDone && !hostOk && hostEnd.contains("online-services host") && hostEnd.contains("host=127.0.0.1"),
                "a local-host client whose host does not answer: F5 stops with the reason instead of starting the game");
        r.check(!CompilePanel::gameRunningFrom(pick), "the fake exe was not started");

        // The cover card of a cold start: over a given rectangle, topmost, never focused, clicks pass through.
        {
            DevmapCover cover;
            cover.setMap("mp_iw8r_test", "war");
            for (int i = 0; i < DevmapCover::kMaxLines + 3; ++i) cover.addLine(QString("+%1.0 s: line %1").arg(i));
            cover.setElapsed(12.3);
            const QRect want(120, 90, 800, 450);
            cover.coverRect(want);
            QApplication::processEvents();
            r.check(cover.isVisible() && cover.coveredRect() == want, "the cover card shows over the rectangle it is given");
            r.check(cover.lines().size() == DevmapCover::kMaxLines && cover.lines().last().contains("line 16"),
                    "the cover card keeps the latest timeline lines");
            const Qt::WindowFlags f = cover.windowFlags();
            r.check((f & Qt::WindowStaysOnTopHint) && (f & Qt::WindowDoesNotAcceptFocus) && (f & Qt::WindowTransparentForInput) &&
                        cover.testAttribute(Qt::WA_ShowWithoutActivating),
                    "the cover card is topmost, never takes focus and passes clicks through");
            const QImage shot = cover.grab().toImage();
            int bright = 0;
            for (int y = 0; y < shot.height(); y += 3)
                for (int x = 0; x < shot.width(); x += 3) bright += qGray(shot.pixel(x, y)) > 120;
            r.check(shot.width() > 100 && bright > 50 && qGray(shot.pixel(2, 2)) < 40, "the cover card is a dark card with the map name and the timeline");
            shot.save(outDir + "/devmap_cover.png");
            cover.hide();
        }
        // (The offscreen test platform activates every window it shows; the real focus rule is checked by
        // IW8Radiant.exe --cover-check on the real platform.)
        w.activateWindow();
        QApplication::setActiveWindow(&w);
        QApplication::processEvents();
        r.check(DevmapCover::gameWindowRect(0xFFFFFFF0u).isNull(), "a process without a window has no game window rectangle");
    }

    r.section("patches: create from brush, invert, undo");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        selectPillar();
        const AABB pb = ctx.doc.selectionBounds();
        QTest::keyClick(&w, Qt::Key_P, Qt::ShiftModifier);
        const EntityItem* made = ctx.doc.selection().size() == 1 ? ctx.doc.findItem(ctx.doc.selection().begin()->entity, ctx.doc.selection().begin()->item) : nullptr;
        r.check(made && made->prim() && made->prim()->kind == PrimKind::Patch && made->prim()->verts.size() == 9,
                "Shift+P replaced the pillar with a 3x3 patch mesh");
        const AABB patchBox = ctx.doc.selectionBounds();
        r.check(patchBox.mins.x >= pb.mins.x - 0.01 && patchBox.maxs.x <= pb.maxs.x + 0.01, "the patch spans the pillar's bounds");
        const std::string flat = ctx.doc.text();
        QTest::keyClick(&w, Qt::Key_I, Qt::ControlModifier);
        r.check(ctx.doc.text() != flat && ctx.doc.undoPosition() == pos + 2, "Ctrl+I inverted the patch (second undo step)");
        for (int undoGuard = 0; undoGuard < 64 && ctx.doc.undoPosition() > pos; ++undoGuard) QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        r.check(ctx.doc.text() == before, "Ctrl+Z x2 restores the pillar byte for byte");
    }

    r.section("vertex mode: drag a brush corner in the 2D view");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        selectPillar();
        const AABB pb = ctx.doc.selectionBounds();
        QTest::keyClick(&w, Qt::Key_5);  // grid 16
        QTest::keyClick(&w, Qt::Key_V);
        r.check(ctx.vertexMode && selectionVertexHandles(ctx).size() == 8, QString("V shows %1 corner handles").arg(selectionVertexHandles(ctx).size()));
        const QPointF from = grid->worldToWidget(pb.maxs.x, pb.maxs.y), to = grid->worldToWidget(pb.maxs.x + 16, pb.maxs.y);
        sendMouse(grid, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
        sendMouse(grid, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
        const AABB moved = ctx.doc.selectionBounds();
        r.check(ctx.doc.undoPosition() == pos + 1 && moved.maxs.x == pb.maxs.x + 16 && moved.maxs.y == pb.maxs.y && moved.mins.x == pb.mins.x,
                QString("the corner edge moved +16 in x (bounds x %1..%2)").arg(moved.mins.x).arg(moved.maxs.x));
        QTest::keyClick(&w, Qt::Key_V);
        r.check(!ctx.vertexMode, "V again leaves vertex mode");
        for (int undoGuard = 0; undoGuard < 64 && ctx.doc.undoPosition() > pos; ++undoGuard) QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        r.check(ctx.doc.text() == before, "Ctrl+Z restores the brush text");
    }

    r.section("find / replace materials, select all of type");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        QTest::keyClick(&w, Qt::Key_Escape);
        w.action("FindReplace")->trigger();
        FindReplaceDialog* fd = w.findDialog();
        r.check(fd && fd->isVisible(), "Ctrl+F opens Find / Replace");
        if (fd) {
            fd->setFind("tm2co/me_curb_stone_01");
            fd->setSubstring(false);
            fd->setSelectionOnly(false);
            const size_t found = fd->selectMatches();
            r.check(found > 0 && ctx.doc.selection().size() == found, QString("Select Matches selected %1 primitive(s)").arg(found));
            fd->setReplace("tm2co/iw8r_replaced_test");
            const size_t replaced = fd->replaceAll();
            r.check(replaced > 0 && ctx.doc.text().find("tm2co/me_curb_stone_01") == std::string::npos &&
                        ctx.doc.text().find("tm2co/iw8r_replaced_test") != std::string::npos && ctx.doc.undoPosition() == pos + 1,
                    QString("Replace All changed %1 face(s) in one undo step").arg(replaced));
            fd->close();
            QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
            r.check(ctx.doc.text() == before, "Ctrl+Z undoes the whole replace");
        }
        // Shift+A on a spawn selects every entity of that class.
        const Entity* spawn = nullptr;
        size_t spawns = 0;
        for (const auto& e : ctx.doc.map().entities)
            if (e.classname() == "mp_tdm_spawn_allies_start") spawn = spawn ? spawn : &e, ++spawns;
        if (spawn) {
            ctx.doc.select({spawn->id, kNoId}, false);
            QTest::keyClick(&w, Qt::Key_A, Qt::ShiftModifier);
            r.check(ctx.doc.selection().size() == spawns, QString("Shift+A selected all %1 mp_tdm_spawn_allies_start").arg(spawns));
        }
    }

    r.section("prefabs: save selection as prefab, render, explode");
    {
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        selectPillar();
        const AABB pb = ctx.doc.selectionBounds();
        const QString file = QDir(outDir).filePath("prefabs/iw8r_gui_pillar.map");
        QDir().mkpath(QFileInfo(file).absolutePath());
        QFile::remove(file);
        QString perr;
        r.check(w.saveSelectionAsPrefab(file, &perr) && QFile::exists(file), "saved the pillar as " + file + (perr.isEmpty() ? QString() : " (" + perr + ")"));
        const Entity* pf = nullptr;
        for (const auto& e : ctx.doc.map().entities)
            if (e.classname() == "misc_prefab") pf = &e;
        r.check(pf && pf->value("model") && *pf->value("model") == "prefabs/iw8r_gui_pillar.map" && ctx.doc.undoPosition() == pos + 1,
                "the selection became one misc_prefab with model prefabs/iw8r_gui_pillar.map (one undo step)");
        ctx.scene.sync(ctx.doc);
        const app::PrimGeom* drawn = nullptr;
        for (const app::PrimGeom* g : ctx.scene.items())
            if (g->prefab) drawn = g;
        r.check(drawn && drawn->polys.size() >= 6 && std::fabs(drawn->bounds.maxs.x - pb.maxs.x) < 0.01 && std::fabs(drawn->bounds.mins.y - pb.mins.y) < 0.01,
                "the prefab is drawn with its contents at the pillar's position");
        QString xerr;
        r.check(w.explodeSelectedPrefab(&xerr), "Explode Prefab" + (xerr.isEmpty() ? QString() : " (" + xerr + ")"));
        const AABB eb = ctx.doc.selectionBounds();
        r.check(ctx.doc.undoPosition() == pos + 2 && std::fabs(eb.maxs.x - pb.maxs.x) < 0.01 && std::fabs(eb.mins.z - pb.mins.z) < 0.01,
                "explode put the brush back at the same bounds, selected, in one undo step");
        for (int undoGuard = 0; undoGuard < 64 && ctx.doc.undoPosition() > pos; ++undoGuard) QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        r.check(ctx.doc.text() == before, "Ctrl+Z x2 restores the original text");
    }

    r.section("asset browser tiles");
    if (AssetBrowser* mb = w.materialBrowser()) {
        w.showDock("Materials");
        mb->setFilter("tm2co/");
        for (int i = 0; i < 20; ++i) QTest::qWait(50);
        mb->grab().save(QDir(outDir).filePath("material_browser.png"));
        r.check(mb->visibleCount() > 0, QString("material tiles render (%1 shown)").arg(mb->visibleCount()));
        if (auto* lv = mb->findChild<QListView*>()) {
            auto rowShape = [&]() {
                std::set<std::pair<int, int>> sizes;
                int perRow = 0;
                const int firstY = lv->visualRect(lv->model()->index(0, 0)).y();
                for (int i = 0; i < std::min(60, lv->model()->rowCount()); ++i) {
                    const QRect rc = lv->visualRect(lv->model()->index(i, 0));
                    sizes.insert({rc.width(), rc.height()});
                    if (rc.y() == firstY) ++perRow;
                }
                return std::make_pair(sizes.size(), perRow);
            };
            const auto before2 = rowShape();
            lv->verticalScrollBar()->setValue(lv->verticalScrollBar()->maximum() / 2);
            for (int i = 0; i < 10; ++i) QTest::qWait(30);
            const auto after = rowShape();
            r.check(before2.first == 1 && after.first == 1 && before2.second == after.second && before2.second > 0,
                    QString("material tiles keep one size and %1 per row while scrolling").arg(before2.second));
        }
        mb->setFilter("");
    }

    r.section("compile log levels");
    {
        using LL = iw8r::qt::LogLevel;
        r.check(CompilePanel::lineLevel("Replay: 1108 assets loaded, 24 tested, 0 failed, 0 unavailable") == LL::Info &&
                    CompilePanel::lineLevel("preflight: 0 error(s), 0 warning(s)") == LL::Info &&
                    CompilePanel::lineLevel("[info] assets: 0 materials, 1 images, 0 models, 0 model surfaces, 0 failures") == LL::Info &&
                    CompilePanel::lineLevel("Replay: 10 assets loaded, 4 tested, 2 failed, 0 unavailable") == LL::Error &&
                    CompilePanel::lineLevel("FAILED: zonetool exited with 1") == LL::Error &&
                    CompilePanel::lineLevel("preflight error script: missing user_main") == LL::Error,
                "compile summaries with zero failures are not errors; real failures are");
    }

    r.section("sun from sky");
    if (const iw8r::Entity* ws = ctx.doc.worldspawn()) {
        // Setting a (cached) stock sky aims the sun at it; undoing the sky change adds no edit.
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        const iw8r::Id wid = ws->id;
        ctx.doc.setKeyValue(wid, "skyname", "sky_mp_shipment");
        ctx.documentEdited("sky");
        const std::string* sd = ctx.doc.worldspawn()->value("sundirection");
        const QString got = sd ? QString::fromStdString(*sd) : QString();
        const bool cached = !w.currentSkyFile().isEmpty();
        r.check(!cached || got.startsWith("-18.5 268.9"), "a new sky aims the sun at it: sundirection " + got +
                                                            (cached ? QString() : QString(" (sky not cached here: skipped)")));
        while (ctx.doc.undoPosition() > pos) {
            ctx.doc.undo();
            ctx.documentEdited("undo sky");
        }
        r.check(ctx.doc.text() == before && !ctx.doc.canRedo() == false, "undo restores the sky and the sun without new edits");
    }

    r.section("sun from sky");
    if (const iw8r::Entity* ws = ctx.doc.worldspawn()) {
        // Setting a (cached) stock sky aims the sun at it; undoing the sky change adds no edit.
        const std::string before = ctx.doc.text();
        const size_t pos = ctx.doc.undoPosition();
        const iw8r::Id wid = ws->id;
        ctx.doc.setKeyValue(wid, "skyname", "sky_mp_shipment");
        ctx.documentEdited("sky");
        const std::string* sd = ctx.doc.worldspawn()->value("sundirection");
        const QString got = sd ? QString::fromStdString(*sd) : QString();
        const bool cached = !w.currentSkyFile().isEmpty();
        r.check(!cached || got.startsWith("-18.5 268.9"), "a new sky aims the sun at it: sundirection " + got +
                                                            (cached ? QString() : QString(" (sky not cached here: skipped)")));
        while (ctx.doc.undoPosition() > pos) {
            ctx.doc.undo();
            ctx.documentEdited("undo sky");
        }
        r.check(ctx.doc.text() == before && !ctx.doc.canRedo() == false, "undo restores the sky and the sun without new edits");
    }

    r.section("stock reference in the 2D views (3.19)");
    if (GridView* gv = w.gridView()) {
        // A 2048-unit floor square at z 0 and a raised block: three plans; the top view draws them as a backdrop.
        std::vector<iw8r::app::GpuVertex> tris;
        auto quad = [&](float x0, float y0, float x1, float y1, float z) {
            const float q[6][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y0}, {x1, y1}, {x0, y1}};
            for (const auto& c : q) tris.push_back({c[0], c[1], z, 0xffffffffu});
        };
        quad(-1024, -1024, 1024, 1024, 0);
        quad(-256, -256, 256, 256, 300);
        ctx.referencePlans = iw8r::qt::buildReferencePlans(tris, 256);
        const double oldScale = ctx.gridScale;
        const int oldAxis = ctx.gridAxis;
        ctx.gridAxis = 0;
        ctx.gridScale = 0.1;
        gv->update();
        const QImage with = gv->grab().toImage();
        ctx.referencePlans.clear();
        const QImage without = gv->grab().toImage();
        const QPoint c(gv->width() / 2 + 60, gv->height() / 2 + 60);  // on the floor square, off the axis lines
        r.check(ctx.referencePlans.empty() && with.pixel(c) != without.pixel(c),
                QString("the reference backdrop shows in the top view (%1 vs %2)").arg(with.pixel(c), 0, 16).arg(without.pixel(c), 0, 16));
        ctx.gridScale = oldScale;
        ctx.gridAxis = oldAxis;
        r.check(iw8r::qt::buildReferencePlans(tris, 256).size() == 3, "three reference plans (top, front, side)");
    }

    r.section("check map");
    {
        CheckMapDialog d(&ctx);
        r.check(d.problemCount() >= 1, QString("Check Map lists %1 item(s) (the sample has one mp_tdm_spawn)").arg(d.problemCount()));
        // 6.13: an S&D bomb site trigger is recognised as a stock scripted object.
        const size_t pos = ctx.doc.undoPosition();
        ctx.doc.addEntity({{"classname", "trigger_use_touch"}, {"targetname", "bombzone"}, {"origin", "0 0 0"}}, {}, "t");
        CheckMapDialog d2(&ctx);
        const QString hit = d2.itemTexts().filter("Stock scripted objects").join(" | ");
        r.check(hit.contains("sd_site"), "Check Map names stock scripted objects: " + hit.left(160));
        while (ctx.doc.undoPosition() > pos) ctx.doc.undo();
        ctx.documentEdited("undo check map entity");
        {
            // 1.8: a sun below the horizon is a lighting error in Check Map.
            const iw8r::Entity* ws = ctx.doc.worldspawn();
            if (ws) {
                const size_t p2 = ctx.doc.undoPosition();
                ctx.doc.setKeyValue(ws->id, "sundirection", "30 45 0");
                CheckMapDialog d3(&ctx);
                const QString l = d3.itemTexts().filter("Lighting").join(" | ");
                r.check(l.contains("Lighting:"), "Check Map flags a sun below the horizon: " + l.left(160));
                while (ctx.doc.undoPosition() > p2) ctx.doc.undo();
                ctx.documentEdited("undo sun");
            }
        }
        {
            // 8.11: vertex paint changes the sample patch's control colours in one undo step and tints it in the camera.
            const std::string before = ctx.doc.text();
            const size_t p3 = ctx.doc.undoPosition();
            const int n = ctx.vertexPaintAt(iw8r::Vec3{64, 128, 0});
            ctx.scene.sync(ctx.doc);
            size_t tinted = 0;
            for (const iw8r::app::PrimGeom* g : ctx.scene.items()) tinted += g->polyTint.size();
            r.check(n > 0 && ctx.doc.undoPosition() == p3 + 1 && ctx.doc.text() != before && tinted > 0,
                    QString("vertex paint: %1 control point(s), %2 tinted triangles").arg(n).arg(tinted));
            ctx.doc.undo();
            ctx.documentEdited("undo vertex paint");
            r.check(ctx.doc.text() == before, "undo restores the patch colours");
        }
        // 4.25: the streamed-image catalogue loads (sample of one zone).
        r.check(iw8r::qt::streamedImages().size() > 1000, QString("streamed image catalogue: %1 images").arg(iw8r::qt::streamedImages().size()));
    }

    r.section("copy / cut / paste through the clipboard (2.28)");
    {
        const std::string before = ctx.doc.text();
        ctx.doc.deselectAll();
        size_t picked = 0;
        for (const auto* g : ctx.scene.items())
            if (g->kind == iw8r::app::PrimGeom::Brush && !g->brushEntity && picked < 2) {
                ctx.doc.select(g->ref, true);
                ++picked;
            }
        ctx.documentEdited();
        const size_t prims = [&] {
            size_t n = 0;
            for (const auto& e : ctx.doc.map().entities)
                for (const auto& it : e.items) n += it.isPrimitive();
            return n;
        }();
        r.check(w.copySelection(false) == 2 && QGuiApplication::clipboard()->text().startsWith("iwmap 4"), "Edit > Copy puts the two brushes on the clipboard as map text");
        QString err;
        r.check(w.pasteFromClipboard(&err) == 2, "Edit > Paste adds them back in place " + err);
        size_t after = 0;
        for (const auto& e : ctx.doc.map().entities)
            for (const auto& it : e.items) after += it.isPrimitive();
        r.check(after == prims + 2 && ctx.doc.selection().size() == 2, "two more brushes, and they are the selection");
        r.check(ctx.doc.undo() && ctx.doc.text() == before, "one undo removes the paste");
        ctx.doc.deselectAll();
        ctx.documentEdited();
    }

    r.section("game to editor: select what the player looks at, move to the player (7.8)");
    {
        const std::string before = ctx.doc.text();
        // A player standing at -150 -150 0 looking straight down (game pitch +90) sees the floor.
        r.check(w.selectAlongView(iw8r::Vec3{-150, -150, 0}, iw8r::Vec3{89, 0, 0}) && !ctx.doc.selection().empty(),
                "the view ray selects the floor under a player looking down");
        const iw8r::AABB sb = ctx.doc.selectionBounds();
        r.check(w.moveSelectionTo(iw8r::Vec3{1000, 2000, 64}), "the selection moves to the player's feet");
        const iw8r::AABB nb = ctx.doc.selectionBounds();
        r.check(std::abs((nb.mins.x + nb.maxs.x) / 2 - 1000) < 0.01 && std::abs(nb.mins.z - 64) < 0.01 && sb.valid(),
                "its bottom centre is on the player's feet");
        QString err;
        // No game is running in the test: the commands explain why.
        r.check(!w.playerView(&err).has_value() && !err.isEmpty(), "without a game the Live Link commands say why: " + err);
        while (ctx.doc.text() != before && ctx.doc.undo()) {}
        ctx.doc.deselectAll();
        ctx.documentEdited();
    }

    r.section("stock map entity rebuild from the menu (6.10)");
    {
        QString err;
        const QString work = w.rebuildStockEntities("mp_shipment", &err, false);
        QFile cmd(work + "/command.txt");
        const QString c = cmd.open(QIODevice::ReadOnly) ? QString::fromUtf8(cmd.readAll()) : QString();
        r.check(!work.isEmpty() && QFileInfo::exists(work + "/edited.ents") && c.contains("m2_rebuild_mapents.ps1") && c.contains("-Ents"),
                "File > Rebuild Stock Map prepares the entities and the pipeline command (not run in the test) " + err);
        QDir(work).removeRecursively();
    }

    r.section("log line round trip: script lines and the Replay Log (7.13)");
    {
        const QString dir = QDir(outDir).absoluteFilePath("script_jump");
        QDir(dir).removeRecursively();
        QDir().mkpath(dir);
        QString err;
        r.check(w.saveTo(dir + "/mp_jump.map", &err), "map saved for the script");
        iw8r::qt::ScriptPanel* sp = w.scriptPanel();
        const std::string before = ctx.doc.text();
        r.check(sp && sp->createScript(&err), "a map script is created " + err);
        if (sp) {
            sp->setText("user_main()\n{\n    wait 1;\n    iprintln(\"hi\");\n}\n");
            sp->save(&err);
            const QString own = QFileInfo(sp->scriptPath()).fileName();
            r.check(w.jumpToScriptLine("error: " + own + ":4: unknown function") && sp->currentLine() == 4,
                    QString("a log line naming %1:4 puts the Script dock on line 4 (%2)").arg(own).arg(sp->currentLine()));
            r.check(!w.jumpToScriptLine("warning: nothing about scripts here"), "other lines are left to entity selection");
        }
        while (ctx.doc.text() != before && ctx.doc.undo()) {}
        ctx.documentEdited();
    }

    r.section("prefab parameters: per-instance overrides (8.8)");
    {
        // A prefab with one light and a brush, placed once; override the light colour and swap the material.
        const QString dir = QDir(outDir).absoluteFilePath("prefab_params");
        QDir(dir).removeRecursively();
        QDir().mkpath(dir + "/prefabs");
        QFile pf(dir + "/prefabs/lamp.map");
        if (pf.open(QIODevice::WriteOnly)) {
            pf.write(QByteArray("iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n{\n") +
                     " ( 8 8 64 ) ( -8 8 64 ) ( -8 -8 64 ) tm2co/me_curb_stone_01 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
                     " ( -8 -8 0 ) ( -8 8 0 ) ( 8 8 0 ) tm2co/me_curb_stone_01 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
                     " ( -8 -8 0 ) ( 8 -8 0 ) ( 8 -8 64 ) tm2co/me_curb_stone_01 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
                     " ( 8 8 0 ) ( -8 8 0 ) ( -8 8 64 ) tm2co/me_curb_stone_01 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
                     " ( 8 -8 0 ) ( 8 8 0 ) ( 8 8 64 ) tm2co/me_curb_stone_01 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
                     " ( -8 8 0 ) ( -8 -8 0 ) ( -8 -8 64 ) tm2co/me_curb_stone_01 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
                     "}\n}\n// entity 1\n{\n\"classname\" \"light\"\n\"origin\" \"0 0 72\"\n\"targetname\" \"lamp_light\"\n\"_color\" \"1 1 1\"\n\"radius\" \"300\"\n}\n");
            pf.close();
        }
        QString err;
        r.check(w.saveTo(dir + "/mp_params.map", &err), "map saved beside the prefab");
        const std::string before = ctx.doc.text();
        const iw8r::Id pid = ctx.doc.addEntity({{"classname", "misc_prefab"}, {"model", "prefabs/lamp.map"}, {"origin", "0 0 0"}}, {}, "lamp");
        ctx.documentEdited();
        auto params = w.prefabParameters(pid, &err);
        const iw8r::qt::MainWindow::PrefabParam* colour = nullptr;
        const iw8r::qt::MainWindow::PrefabParam* mat = nullptr;
        for (const auto& p : params) {
            if (!p.material && p.key == "_color") colour = &p;
            if (p.material) mat = &p;
        }
        r.check(colour && mat && colour->selector == "lamp_light" && colour->prefabValue == "1 1 1",
                QString("Prefab Parameters lists the light's keys and the prefab's material (%1 row(s)) %2").arg(params.size()).arg(err));
        if (colour && mat) {
            w.setPrefabParameter(pid, *colour, "1 0.5 0.2", false);
            w.setPrefabParameter(pid, *mat, "tm2co/plaster_beige_01", false);
            const iw8r::Entity* e = ctx.doc.findEntity(pid);
            r.check(e && e->value("iw8r_ovr:lamp_light:_color") && *e->value("iw8r_ovr:lamp_light:_color") == "1 0.5 0.2" &&
                        e->value("iw8r_mat:tm2co/me_curb_stone_01"),
                    "the overrides are written as iw8r_ovr / iw8r_mat keys on the instance");
            params = w.prefabParameters(pid, &err);
            bool shown = false;
            for (const auto& p : params) shown |= !p.material && p.key == "_color" && p.overridden && p.override == "1 0.5 0.2";
            r.check(shown, "reading the parameters back shows the override");
        }
        while (ctx.doc.text() != before && ctx.doc.undo()) {}
        ctx.documentEdited();
    }

    r.section("convert brushes to a static model (8.8 / 2.24)");
    {
        // Works on a saved copy (the OBJ goes to <map folder>/models).
        const QString dir = QDir(outDir).absoluteFilePath("bake_map");
        QDir(dir).removeRecursively();
        QDir().mkpath(dir);
        QString err;
        r.check(w.saveTo(dir + "/mp_bake.map", &err), "map saved for the bake");
        const std::string before = ctx.doc.text();
        ctx.doc.deselectAll();
        size_t picked = 0;
        for (const auto* g : ctx.scene.items())
            if (g->kind == iw8r::app::PrimGeom::Brush && !g->brushEntity && picked < 2) {
                ctx.doc.select(g->ref, true);
                ++picked;
            }
        ctx.documentEdited();
        const size_t tris = w.bakeSelectionToModel(&err);
        int models = 0;
        for (const auto& e : ctx.doc.map().entities)
            if (e.classname() == "misc_model" && e.value("model") && e.value("model")->rfind("baked_part", 0) == 0) ++models;
        const QStringList objs = QDir(dir + "/models").entryList({"baked_part*.obj"});
        r.check(tris > 0 && models == 1 && objs.size() == 1,
                QString("Brush > Convert Selection to Static Model: %1 triangle(s) -> %2 placed as a misc_model %3").arg(tris).arg(objs.join(",")).arg(err));
        if (!objs.isEmpty()) {
            iw8r::ImportedModel im;
            std::string ierr;
            r.check(iw8r::importModelFile((dir + "/models/" + objs.front()).toStdString(), im, &ierr) && !im.meshes.empty() && !im.meshes.front().material.empty(),
                    "the OBJ imports back with its material groups " + QString::fromStdString(ierr));
        }
        r.check(ctx.doc.undo() && ctx.doc.text() == before, "one undo brings the brushes back");
        ctx.doc.deselectAll();
        ctx.documentEdited();
    }

    r.section("stair and building generators (8.12)");
    {
        const std::string before = ctx.doc.text();
        ctx.doc.deselectAll();
        iw8r::AABB sb;
        sb.add(iw8r::Vec3{1000, 0, 0});
        sb.add(iw8r::Vec3{1256, 64, 128});
        r.check(iw8r::qt::createBoxBrush(ctx, sb), "a 256 x 64 x 128 brush to fill");
        QString err;
        const int stairs = w.generateStructure(false, 0, 1, &err);
        r.check(stairs == 8, QString("Generate Stairs replaces it with 8 steps of 16 (%1) %2").arg(stairs).arg(err));
        while (ctx.doc.text() != before && ctx.doc.undo()) {}
        ctx.doc.deselectAll();
        iw8r::AABB bb;
        bb.add(iw8r::Vec3{1000, 0, 0});
        bb.add(iw8r::Vec3{1512, 384, 256});
        iw8r::qt::createBoxBrush(ctx, bb);
        const int parts = w.generateStructure(true, 0, 2, &err);
        r.check(parts > 10, QString("Generate Building makes a two-storey building (%1 brushes) %2").arg(parts).arg(err));
        while (ctx.doc.text() != before && ctx.doc.undo()) {}
        {
            {
                // Owner request 2026-11-11: Make Hinged Door, and the Behaviour Preview swings it about its hinge.
                AABB box;
                box.add(iw8r::Vec3{600, 396, 0});
                box.add(iw8r::Vec3{664, 404, 96});
                const size_t pos0 = ctx.doc.undoPosition();
                ctx.doc.deselectAll();
                bool made = false, swung = false;
                if (iw8r::qt::createBoxBrush(ctx, box)) {
                    iw8r::SelRef br{iw8r::kNoId, iw8r::kNoId};
                    if (const iw8r::Entity* ws = ctx.doc.worldspawn())
                        for (const auto& it : ws->items)
                            if (it.prim()) br = {ws->id, it.id};
                    ctx.doc.select(br, false);
                    if (QAction* a = w.action("MakeHingedDoor")) a->trigger();
                    iw8r::Id door = iw8r::kNoId;
                    for (const auto& e : ctx.doc.map().entities)
                        if (const std::string* d = e.value("iw8r_door"); d && d->rfind("hinge ", 0) == 0 && e.classname() == "script_brushmodel") door = e.id;
                    made = door != iw8r::kNoId;
                    if (made) {
                        const auto far = [&]() {
                            for (const auto* g : ctx.scene.items())
                                if (g->ref.entity == door && g->ref.item != iw8r::kNoId) return g->bounds.maxs.y;
                            return -1e30;
                        };
                        ctx.scene.behaviourTime = 2.0;  // 1 s closed, 0.6 s opening, then held open
                        ctx.scene.invalidate();
                        ctx.scene.sync(ctx.doc);
                        const double open = far();
                        ctx.scene.behaviourTime = -1;
                        ctx.scene.invalidate();
                        ctx.scene.sync(ctx.doc);
                        // Closed the door reaches y 404; swung 100 degrees about (600, 400) its far end is near y 463.
                        swung = open > 450 && open < 470 && std::fabs(far() - 404) < 0.5;
                    }
                }
                while (ctx.doc.undoPosition() > pos0 && ctx.doc.undo()) {}
                ctx.documentEdited("undo hinged door test");
                r.check(made, "Make Hinged Door turns the selected brush into a script_brushmodel with iw8r_door hinge");
                r.check(swung, "Behaviour Preview swings the hinged door about its hinge and restores it");
            }
            // The asset dialogs opened and driven (helper checks A and B).
            iw8r::qt::runAssetDialogChecksA([&r](const QString& sname) { r.section(sname); },
                                            [&r](bool ok, const QString& m) { r.check(ok, m); }, ctx.doc.map(), outDir);
            iw8r::qt::runAssetDialogChecksB([&r](const QString& sname) { r.section(sname); },
                                            [&r](bool ok, const QString& m) { r.check(ok, m); }, outDir);
            {
                // L12 asset editing: the commands exist, and the Material Editor writes the shader state fields.
                bool all = true;
                QString missing;
                for (const char* id : {"CloneStockMaterial", "AdjustImage", "TechsetInfo", "AssetUsage", "SoundSettings", "ReplaceMaterialInMap",
                                       "ReplaceModelMaterial", "VisionEditor", "MakeLadder", "BehaviourPreview", "CompareWithBackup"})
                    if (!w.action(id)) {
                        all = false;
                        missing += QString(" ") + id;
                    }
                r.check(all, "the asset editing commands are in the menus" + (missing.isEmpty() ? QString() : " (missing:" + missing + ")"));
                // 12.5 on real data: a stock material that is in this machine's texture cache becomes an editable copy
                // (skipped, and said so, when the cache has none of the candidates).
                {
                    QString stock;
                    std::optional<iw8r::qt::MaterialCache::Entry> entry;
                    for (const char* cand : {"tm2co/me_curb_stone_01", "tm2co/me_plaster_barrier_fence_wall", "tm2co/ee_trim_beam_airport_concrete"}) {
                        const auto e = w.materialCache() ? w.materialCache()->lookup(cand) : std::nullopt;
                        if (e && !e->failed && QFileInfo(e->colorDds).isFile()) {
                            stock = cand;
                            entry = e;
                            break;
                        }
                    }
                    if (!entry) {
                        r.check(true, "editable copy of a stock material: skipped (none of the test materials is in this machine's texture cache)");
                    } else {
                        const QString dest = QDir(outDir).filePath("l12_clone");
                        QDir(dest).removeRecursively();
                        QDir().mkpath(dest);
                        const QString name = iw8r::qt::suggestCloneName(stock, {});
                        iw8r::qt::MaterialCloneResult res;
                        QString cerr;
                        const bool ok = iw8r::qt::cloneStockMaterial(*entry, dest, name, &res, &cerr);
                        iw8r::MaterialLibrary lib;
                        std::string lerr;
                        lib.load({dest.toStdString()}, &lerr);
                        const iw8r::CustomMaterial* cm = lib.find(name.toStdString());
                        int cw = 0, ch = 0;
                        std::vector<uint8_t> px;
                        std::string derr;
                        const bool image = cm && iw8r::decodeImageFile(cm->colorImage, cw, ch, px, &derr);
                        r.check(ok && cm && image && cw >= 64 && ch >= 64,
                                QString("Make Editable Copy turns %1 into the custom material %2 with a %3 x %4 colour image%5")
                                    .arg(stock, name).arg(cw).arg(ch).arg(cerr.isEmpty() ? QString() : " (" + cerr + ")"));
                        QString again;
                        r.check(!iw8r::qt::cloneStockMaterial(*entry, dest, name, &res, &again) && !again.isEmpty(),
                                "a second copy under the same name is refused: " + again);
                    }
                }
                // Owner report 2026-11-11: the built-in glass showed the missing-material checker in the camera.
                r.check(ctx.scene.blendFor && ctx.scene.blendFor("iw8r_glass") == 1 && w.cameraView()->materialImagePath("iw8r_glass").endsWith("iw8r_builtin_glass_c.png") &&
                            QFileInfo(w.cameraView()->materialImagePath("iw8r_glass")).isFile(),
                        "the built-in glass material has its image in the camera and draws see-through");
                iw8r::qt::MaterialEditorDialog med(QDir(outDir).filePath("l12_materials"), nullptr, &w);
                med.setName("test/l12_glow");
                med.setImage(0, QDir(outDir).filePath("camera_shot.png"));
                med.setShaderState(1, true, 4, 1, 0.5, 0.25, 12, 1);
                // 12.12: the live shader preview draws the material and follows the shader state.
                QTest::qWait(150);
                const QImage liveAdd = med.livePreview();
                med.setShaderState(0, false, 0, 1, 1, 1, 0, 0);
                QTest::qWait(150);
                const QImage livePlain = med.livePreview();
                int liveDiff = 0;
                if (!liveAdd.isNull() && liveAdd.size() == livePlain.size())
                    for (int y = 0; y < liveAdd.height(); y += 4)
                        for (int x = 0; x < liveAdd.width(); x += 4) liveDiff += liveAdd.pixel(x, y) != livePlain.pixel(x, y);
                liveAdd.save(QDir(outDir).filePath("material_live_additive.png"));
                livePlain.save(QDir(outDir).filePath("material_live_plain.png"));
                r.check(!liveAdd.isNull() && liveAdd.width() >= 128 && liveDiff > 50,
                        QString("the Material Editor's live shader preview draws and changes with the shader state (%1 sampled pixels differ)").arg(liveDiff));
                med.setShaderState(1, true, 4, 1, 0.5, 0.25, 12, 1);
                QString jerr;
                const QString json = med.jsonText(&jerr);
                r.check(json.contains("\"blendMode\": \"add\"") && json.contains("\"unlit\": true") && json.contains("\"emission\": 4") &&
                            json.contains("\"tint\"") && json.contains("\"sortKey\": 12") && json.contains("\"polygonOffset\": 1"),
                        "the Material Editor writes blend mode, unlit, emission, tint, sort order and depth offset" +
                            (jerr.isEmpty() ? QString() : " (" + jerr + ")"));
            }
            {
                // View > Behaviour Preview: spinning / bobbing models move in the camera; off restores the resting pose.
                QAction* bp = w.action("BehaviourPreview");
                if (bp) bp->trigger();
                const bool on = bp && ctx.behaviourPreview && ctx.scene.behaviourTime >= 0;
                // A meshed model with iw8r_bob is drawn higher half-way through its cycle.
                iw8r::Id mid = iw8r::kNoId;
                for (const auto* g : ctx.scene.items())
                    if (g->mesh && g->ref.item == iw8r::kNoId) mid = g->ref.entity;
                bool moved = true;
                if (mid != iw8r::kNoId) {
                    const auto zOf = [&]() {
                        for (const auto* g : ctx.scene.items())
                            if (g->ref.entity == mid && g->ref.item == iw8r::kNoId) return g->bounds.maxs.z;
                        return -1e30;
                    };
                    ctx.scene.behaviourTime = -1;
                    ctx.scene.invalidate();
                    ctx.scene.sync(ctx.doc);
                    const double rest = zOf();
                    ctx.doc.setKeyValue(mid, "iw8r_bob", "40 2");
                    ctx.scene.behaviourTime = 1.0;
                    ctx.scene.invalidate();
                    ctx.scene.sync(ctx.doc);
                    moved = std::fabs(zOf() - (rest + 40)) < 0.5;
                    ctx.doc.undo();
                }
                // A brush model with iw8r_lift is drawn at the top of its travel half-way through its cycle.
                bool lifted = false;
                {
                    AABB box;
                    box.add(iw8r::Vec3{400, 400, 0});
                    box.add(iw8r::Vec3{432, 432, 16});
                    const size_t pos0 = ctx.doc.undoPosition();
                    ctx.doc.deselectAll();
                    if (iw8r::qt::createBoxBrush(ctx, box)) {
                        iw8r::SelRef br{iw8r::kNoId, iw8r::kNoId};
                        if (const iw8r::Entity* ws = ctx.doc.worldspawn())
                            for (const auto& it : ws->items)
                                if (it.prim()) br = {ws->id, it.id};  // the newest primitive
                        ctx.doc.select(br, false);
                        const iw8r::Id bm = ctx.doc.groupSelection("script_brushmodel");
                        if (bm != iw8r::kNoId) {
                            ctx.doc.setKeyValue(bm, "iw8r_lift", "96 2 2");
                            const auto top = [&]() {
                                for (const auto* g : ctx.scene.items())
                                    if (g->ref.entity == bm && g->ref.item != iw8r::kNoId) return g->bounds.maxs.z;
                                return -1e30;
                            };
                            ctx.scene.behaviourTime = 5.0;  // wait 2, rise 2, hold 2: at the top
                            ctx.scene.invalidate();
                            ctx.scene.sync(ctx.doc);
                            const double up = top();
                            ctx.scene.behaviourTime = -1;
                            ctx.scene.invalidate();
                            ctx.scene.sync(ctx.doc);
                            lifted = std::fabs(up - 112) < 0.5 && std::fabs(top() - 16) < 0.5;
                        }
                    }
                    while (ctx.doc.undoPosition() > pos0 && ctx.doc.undo()) {}
                    ctx.documentEdited("undo lift test");
                }
                r.check(lifted, "Behaviour Preview draws an iw8r_lift brush model at the top of its travel and restores it");
                if (bp) bp->trigger();
                ctx.scene.sync(ctx.doc);
                r.check(on && moved && !ctx.behaviourPreview && ctx.scene.behaviourTime < 0,
                        QString("Behaviour Preview moves an iw8r_bob model in the camera and switches off again%1")
                            .arg(mid == iw8r::kNoId ? " (no meshed model in the map: toggle only)" : ""));
            }
            {
                // Owner playtest 2026-11-11: Make Ladder places the invisible climbable brush and a ladder model together.
                ctx.doc.deselectAll();
                const size_t pos = ctx.doc.undoPosition();
                const iw8r::Id lid = w.makeLadder();
                const iw8r::Entity* le = ctx.doc.findEntity(lid);
                bool ladderBrush = false;
                if (const iw8r::Entity* ws = ctx.doc.worldspawn())
                    for (const auto& it : ws->items)
                        if (const iw8r::Primitive* pr = it.prim(); pr && pr->kind == iw8r::PrimKind::Brush && !pr->faces.empty())
                            ladderBrush |= pr->faces.front().material() == "ladder";
                r.check(le && le->classname() == "script_model" && le->value("model") && *le->value("model") == "tm/ladder_old_wood_01_160" &&
                            le->value("iw8r_no_collision") &&
                            ladderBrush && ctx.doc.undoPosition() == pos + 1,
                        "Make Ladder adds a ladder brush and a ladder model in one undo step");
                ctx.doc.undo();
                ctx.documentEdited("undo ladder");
            }
            // 5.12: Make Glass Pane: a thin brush becomes a script_brushmodel glass_N with the built-in iw8r_glass.
            ctx.doc.deselectAll();
            iw8r::AABB pane;
            pane.add(iw8r::Vec3{1000, 0, 0});
            pane.add(iw8r::Vec3{1128, 4, 96});
            iw8r::qt::createBoxBrush(ctx, pane);
            const iw8r::Id gid = w.makeGlassPane();
            const iw8r::Entity* g = gid != iw8r::kNoId ? ctx.doc.findEntity(gid) : nullptr;
            bool glassFaces = g && !g->items.empty();
            if (g)
                for (const auto& it : g->items)
                    if (const iw8r::Primitive* p = it.prim())
                        for (const auto& f : p->faces) glassFaces &= f.material() == "iw8r_glass";
            r.check(g && g->classname() == "script_brushmodel" && g->value("targetname") && g->value("targetname")->rfind("glass_", 0) == 0 &&
                        glassFaces,
                    "Make Glass Pane makes a script_brushmodel glass_N with iw8r_glass on every face");
            ctx.doc.undo();
            r.check(gid == iw8r::kNoId || !ctx.doc.findEntity(gid), "one undo removes the pane entity");
        }
        while (ctx.doc.text() != before && ctx.doc.undo()) {}
        ctx.doc.deselectAll();
        ctx.documentEdited();
        r.check(ctx.doc.text() == before, "undo restores the map");
    }

    r.section("decal mode: click to place (8.10)");
    {
        const std::string before = ctx.doc.text();
        const std::string oldMat = ctx.currentMaterial;
        ctx.currentMaterial = "tm2co/me_curb_stone_01";
        // Straight down from above the sample floor, away from the props.
        iw8r::Ray ray;
        ray.origin = iw8r::Vec3{-150, -150, 200};
        ray.dir = iw8r::Vec3{0, 0, -1};
        ctx.scene.sync(ctx.doc);
        double t = 0;
        const auto hit = ctx.scene.pick(ray, &t);
        const int n = hit ? ctx.decalAt(ray, t, *hit) : 0;
        size_t patches = 0;
        for (const auto& e : ctx.doc.map().entities)
            for (const auto& it : e.items)
                if (const iw8r::Primitive* p = it.prim(); p && p->kind == iw8r::PrimKind::Patch) ++patches;
        r.check(hit && n >= 1 && patches >= 1, QString("a click on the floor places a decal patch (%1 piece(s))").arg(n));
        r.check(ctx.doc.undo() && ctx.doc.text() == before, "one undo removes it");
        ctx.currentMaterial = oldMat;
        ctx.documentEdited();
    }

    r.section("in-app help: search and first map guide (9.11)");
    {
        iw8r::qt::HelpSearchDialog hd(&w);
        hd.search("spawn");
        r.check(hd.sectionCount() > 50 && hd.resultCount() > 0 && hd.shownText().toLower().contains("spawn"),
                QString("Help > Search Help indexes %1 sections; \"spawn\" finds %2").arg(hd.sectionCount()).arg(hd.resultCount()));
        hd.search("prefab misc_prefab");
        r.check(hd.resultCount() > 0, QString("two-term search (prefab misc_prefab): %1 hit(s)").arg(hd.resultCount()));
        if (QAction* a = w.action("FirstMapGuide")) a->trigger();
        auto* g = w.findChild<iw8r::qt::FirstMapGuide*>();
        r.check(g && g->stepCount() == 9 && g->doneCount() >= 1,
                QString("Help > First Map Guide shows 9 steps (%1 done on the sample)").arg(g ? g->doneCount() : -1));
    }

    r.section("browser tags, usage counts, replace everywhere (4.22)");
    {
        iw8r::qt::AssetBrowser* mb = w.materialsBrowser();
        const std::string before = ctx.doc.text();
        std::string used;
        for (const auto& e : ctx.doc.map().entities)
            for (const auto& it : e.items)
                if (const iw8r::Primitive* p = it.prim(); p && !p->faces.empty() && !iw8r::isIw8ToolMaterial(p->faces.front().material()))
                    if (used.empty()) used = p->faces.front().material();
        const QString um = QString::fromStdString(used);
        const QVariant oldTags = Settings::instance().value("assets/materials/tags");
        r.check(mb && mb->usageCount(um) > 0, QString("usage count of %1: %2 face(s)").arg(um).arg(mb ? mb->usageCount(um) : 0));
        if (mb) {
            mb->setTags(um, {"Walls", "test-tag"});
            r.check(mb->tagsOf(um) == QStringList{"walls", "test-tag"} && mb->allTags().contains("test-tag"), "tags are saved (lower case)");
            mb->showTag("test-tag");
            r.check(mb->visibleCount() == 1 && mb->visibleName(0) == um, QString("the #test-tag list shows only the tagged material (%1)").arg(mb->visibleCount()));
            mb->setTags(um, {});
            mb->setListMode(iw8r::qt::AssetBrowser::ListMode::All);
        }
        Settings::instance().setValue("assets/materials/tags", oldTags);
        const int faces = mb ? mb->usageCount(um) : 0;
        r.check(w.replaceEverywhere(true, um, "caulk") == size_t(faces) && mb && mb->usageCount(um) == 0,
                QString("Replace Everywhere swaps all %1 face(s) to caulk in one step").arg(faces));
        r.check(ctx.doc.undo() && ctx.doc.text() == before, "one undo restores them");
        ctx.documentEdited();
        r.check(true, QString("List Unused Custom Assets: %1 entry(ies)").arg(w.unusedCustomAssets().size()));
    }

    r.section("spawn set generator (6.18)");
    {
        const std::string before = ctx.doc.text();
        QString msg;
        const int n = w.generateSpawnSet(6, 2, true, &msg);
        int dm = 0, allies = 0, axis = 0;
        for (const auto& e : ctx.doc.map().entities) {
            const std::string c = e.classname();
            dm += c == "mp_dm_spawn";
            allies += c == "mp_tdm_spawn_allies_start";
            axis += c == "mp_tdm_spawn_axis_start";
        }
        r.check(n > 0 && dm >= 2 && allies >= 1 && axis >= 1,
                QString("Tools > Generate Spawns replaces the spawns: %1 entities (%2 dm, %3 allies / %4 axis starts) %5").arg(n).arg(dm).arg(allies).arg(axis).arg(msg));
        r.check(ctx.doc.undo() && ctx.doc.text() == before, "one undo step restores the original spawns");
        ctx.documentEdited();
    }

    r.section("Map Doctor fixes in Check Map (9.12)");
    {
        // A spawn floating 200 above the sample floor and a dangling target: two [fix] lines, Fix All clears them.
        const std::string before = ctx.doc.text();
        const iw8r::Id sp = ctx.doc.addEntity({{"classname", "mp_dm_spawn"}, {"origin", "-200 -150 300"}, {"angles", "5 90 0"}}, {}, "test spawn");
        const iw8r::Id tg = ctx.doc.addEntity({{"classname", "script_origin"}, {"origin", "0 0 32"}, {"target", "no_such_name"}}, {}, "test target");
        ctx.documentEdited();
        CheckMapDialog dlg(&ctx, &w);
        const size_t fixable = dlg.fixableCount();
        r.check(fixable >= 3 && dlg.itemTexts().front().startsWith("[fix] "),
                QString("Check Map lists %1 [fix] line(s) first (spawn angles, spawn floor, dangling target)").arg(fixable));
        const size_t applied = dlg.fixAll();
        const auto* e = ctx.doc.findEntity(sp);
        const auto o = e ? e->origin() : std::nullopt;
        r.check(applied == fixable && o && o->z < 300 && e->value("angles") && *e->value("angles") == "0 90 0" &&
                    ctx.doc.findEntity(tg) && !ctx.doc.findEntity(tg)->value("target"),
                QString("Fix All applies them (%1): the spawn is on the floor at z %2 with clean angles, the dangling target is gone")
                    .arg(applied).arg(o ? o->z : -1));
        r.check(dlg.fixableCount() == 0, "Check Again finds nothing left to fix");
        {
            // 6.15: a dynamic_prop with a model the audit lists as missing is reported.
            const iw8r::Id dp = ctx.doc.addEntity({{"classname", "dynamic_prop"}, {"origin", "0 0 0"},
                                                   {"model", "attachment_vm_lm_pkilo_receiver_v11"}}, {}, "test prop");
            ctx.documentEdited();
            dlg.recheck();
            bool listed = false;
            for (const QString& t : dlg.itemTexts()) listed |= t.contains("attachment_vm_lm_pkilo_receiver_v11") && t.contains("not available");
            r.check(listed, "Check Map reports a dynamic_prop whose model is missing in Replay");
            ctx.doc.undo();
            ctx.documentEdited();
            (void)dp;
        }
        {
            // 6.14: a scriptable whose definition lives in a stock map zone, and an unknown one, are reported; the
            // hardpoint marker (always loaded, fully known) is not.
            ctx.doc.addEntity({{"classname", "scriptable"}, {"origin", "0 0 0"}, {"scriptable", "com_trafficcone_01"}}, {}, "test scriptable");
            ctx.doc.addEntity({{"classname", "scriptable"}, {"origin", "16 0 0"}, {"scriptable", "no_such_definition_x"}}, {}, "test scriptable");
            ctx.doc.addEntity({{"classname", "scriptable"}, {"origin", "32 0 0"}, {"scriptable", "hardpoint_chevron"}}, {}, "test scriptable");
            ctx.documentEdited();
            dlg.recheck();
            int mapZone = 0, unknown = 0, chevron = 0;
            for (const QString& t : dlg.itemTexts()) {
                if (!t.startsWith("Scriptables:")) continue;
                mapZone += t.contains("com_trafficcone_01") && t.contains("stock map zones");
                unknown += t.contains("no_such_definition_x") && t.contains("not a stock scriptable definition");
                chevron += t.contains("hardpoint_chevron");
            }
            r.check(mapZone == 1 && unknown == 1 && chevron == 0,
                    QString("Check Map reports scriptables the build leaves out (map zone %1, unknown %2) and not the hardpoint marker (%3)")
                        .arg(mapZone).arg(unknown).arg(chevron));
            // With "Real stock scripted objects" on (here through its one-run override) the cone is carried, so it is
            // no longer reported, and prefab bundles get the three build features the stock door asks for.
            const QByteArray savedEnv = qgetenv("IW8R_REAL_SCRIPTABLES");
            qputenv("IW8R_REAL_SCRIPTABLES", "1");
            dlg.recheck();
            int coneOn = 0, unknownOn = 0;
            for (const QString& t : dlg.itemTexts()) {
                if (!t.startsWith("Scriptables:")) continue;
                coneOn += t.contains("com_trafficcone_01");
                unknownOn += t.contains("no_such_definition_x");
            }
            const PrefabBuildSupport on = PrefabBrowser::buildSupport();
            qputenv("IW8R_REAL_SCRIPTABLES", "0");
            const PrefabBuildSupport off = PrefabBrowser::buildSupport();
            if (savedEnv.isEmpty()) qunsetenv("IW8R_REAL_SCRIPTABLES");
            else qputenv("IW8R_REAL_SCRIPTABLES", savedEnv);
            r.check(coneOn == 0 && unknownOn == 1 && on.features.count("scriptable_def_carry") && on.features.count("scriptable_instances") &&
                        on.features.count("stock_script_calls") && !off.features.count("scriptable_def_carry"),
                    QString("with real stock scripted objects on, a stock-zone scriptable is built (reported %1) and bundles may use scriptables")
                        .arg(coneOn));
            for (int k = 0; k < 3; ++k) ctx.doc.undo();
            ctx.documentEdited();
        }
        {
            // Game modes (research 87): a preset carried by the map, the Warzone start entities, and the F5 preset.
            const std::vector<GameModePreset>& modes = builtinGameModes();
            const GameModePreset* small = findGameMode(modes, "br_small_map");
            const size_t pos = ctx.doc.undoPosition(), entsBefore = ctx.doc.map().entities.size();
            const bool baked = small && w.bakeGameMode(*small);
            std::string dvars, br;
            for (const Entity& e : ctx.doc.map().entities)
                if (e.isWorldspawn()) {
                    if (const std::string* v = e.value("iw8r_mode_dvars")) dvars = *v;
                    if (const std::string* v = e.value("iw8r_br")) br = *v;
                }
            r.check(baked && dvars == "scr_br_teamsize=1; scr_br_circle_disable=1; scr_br_gulag=0" && br == "nocircle nogulag skipdrop",
                    QString("a game mode preset is carried by the map (dvars '%1', switches '%2')").arg(QString::fromStdString(dvars), QString::fromStdString(br)));
            const int added = w.insertWarzoneStart();
            bool brListed = false;
            for (const GameTypeCheck& g : gameTypeChecks(ctx.doc.map())) brListed |= g.gametype == "br" && g.supported;
            r.check(added == 9 && ctx.doc.map().entities.size() == entsBefore + 9 && brListed,
                    QString("the Warzone start adds 8 spawns and a prematch area, and the map then counts as a br map (%1 added)").arg(added));
            while (ctx.doc.undoPosition() > pos && ctx.doc.undo()) {}
            ctx.documentEdited();
            // The F5 preset: stored, read back as dvars, and restored to what it was.
            Settings& st = Settings::instance();
            const QVariant oldPreset = st.value("devmap/modePreset"), oldDvars = st.value("devmap/modeDvars"), oldType = st.value("devmap/gametype");
            DevmapRunner::setModePreset("br_trios", "br", "loot_table_name=mp/loot_item_defs_s3.csv; scr_br_teamsize=3; bad name=1");
            const auto f5 = DevmapRunner::modeDvarsSetting();
            const bool stored = DevmapRunner::modePresetSetting() == "br_trios" && DevmapRunner::gametypeSetting() == "br" && f5.size() == 2 &&
                                f5[1].first == "scr_br_teamsize" && f5[1].second == "3";
            st.setValue("devmap/modePreset", oldPreset);
            st.setValue("devmap/modeDvars", oldDvars);
            st.setValue("devmap/gametype", oldType);
            r.check(stored && w.action("GameModes") != nullptr, "the F5 game mode preset stores its game type and dvars (the bad pair is dropped)");
        }
        while (ctx.doc.text() != before && ctx.doc.undo()) {}
        ctx.documentEdited();
        r.check(ctx.doc.text() == before, "undo removes the test entities");
    }

    r.section("heat maps in the top view (5.15 / 6.18)");
    {
        GridView* gv = nullptr;
        for (GridView* g : w.findChildren<GridView*>())
            if (g->fixedAxis() < 0) gv = g;
        if (QAction* a = w.action("HeatMapSpawns")) a->trigger();
        const auto& sp = gv ? gv->heatMap() : GridView::HeatMap{};
        r.check(gv && ctx.heatMap == 2 && sp.cellsX > 0 && sp.maxValue > 0,
                QString("View > Heat Map > Spawn Density fills %1 x %2 cells (max %3)").arg(sp.cellsX).arg(sp.cellsY).arg(sp.maxValue));
        if (QAction* a = w.action("HeatMapLights")) a->trigger();
        const auto& lh = gv ? gv->heatMap() : GridView::HeatMap{};
        r.check(ctx.heatMap == 1 && lh.mode == 1 && lh.cellsX > 0, QString("Light Overlap builds its grid (max %1 light(s) on a cell)").arg(lh.maxValue));
        if (gv) gv->grab().save(QDir(outDir).filePath("heatmap_lights.png"));
        if (QAction* a = w.action("HeatMapNone")) a->trigger();
        r.check(ctx.heatMap == 0, "Heat Map > None turns it off");
    }

    r.section("Surface panel lightmap scale (2.4 / 2.21)");
    {
        SurfacePanel* sp = w.findChild<SurfacePanel*>();
        const std::string before = ctx.doc.text();
        ctx.doc.deselectAll();
        for (const auto* g : ctx.scene.items())
            if (g->kind == iw8r::app::PrimGeom::Brush) {
                ctx.doc.select(g->ref, true);
                break;
            }
        ctx.documentEdited();
        r.check(sp && sp->applyLightmapScale(8192, 4096), "the Surface panel writes a lightmap scale to every face of the selected brush");
        const iw8r::EntityItem* it = nullptr;
        for (const auto& ref : ctx.doc.selection()) it = ctx.doc.findItem(ref.entity, ref.item);
        const auto lp = it && it->prim() ? iw8r::lightmapParamsOfFace(*it->prim(), 0) : std::nullopt;
        r.check(lp && lp->sx == 8192 && lp->sy == 4096, "the face reads back 8192 x 4096");
        r.check(sp && !sp->applyLightmapScale(8192, 4096), "the same values again change nothing");
        while (ctx.doc.text() != before && ctx.doc.undo()) {}
        ctx.doc.deselectAll();
        ctx.documentEdited();
        r.check(ctx.doc.text() == before, "undo restores the lightmap tokens");
    }

    r.section("autosave history and low-disk guard (9.14)");
    {
        const QString dir = w.autosaveDir();
        const size_t before = iw8r::listAutosaves(dir.toStdString()).size();
        QString msg;
        const QString a1 = w.autosaveNow(&msg);
        r.check(!a1.isEmpty() && QFileInfo::exists(a1) && a1.contains(".autosave."), "an autosave writes a timestamped copy: " + msg);
        iw8r::AutosaveEntry e;
        r.check(iw8r::parseAutosaveFileName(QFileInfo(a1).fileName().toStdString(), e) && e.stamp.size() == 15,
                "its name parses back with a yyyyMMdd-HHmmss stamp");
        r.check(iw8r::listAutosaves(dir.toStdString()).size() >= before, "the history lists it");
        QFile::remove(a1);  // the test leaves the owner's autosave folder as it was
        uintmax_t freeBytes = 0;
        r.check(iw8r::hasFreeSpace(dir.toStdString(), 1, &freeBytes) && freeBytes > 0, QString("free space is read (%1 MB)").arg(freeBytes >> 20));
    }

    r.section("patch vertex locks and Patch Inspector (2.21)");
    {
        const std::string before = ctx.doc.text();
        ctx.doc.deselectAll();
        r.check(iw8r::qt::createPatch(ctx, iw8r::qt::PatchShape::Flat3x3), "a 3x3 patch is created and selected");
        ctx.documentEdited("test patch");
        r.check(iw8r::qt::lockSelectedPatchVertices(ctx, iw8r::qt::PatchLockMode::Border) == 1, "Lock Border Vertices locks the selected patch");
        auto handles = iw8r::qt::selectionVertexHandles(ctx);
        size_t lockedCount = 0;
        for (const auto& h : handles) lockedCount += h.locked;
        r.check(handles.size() == 9 && lockedCount == 8, QString("8 of 9 control points are locked (%1 of %2)").arg(lockedCount).arg(handles.size()));
        size_t rejected = 0, skipped = 0;
        const auto g0 = iw8r::qt::firstSelectedPatchGrid(ctx);
        r.check(iw8r::qt::moveVertexHandles(ctx, handles, iw8r::Vec3{0, 0, 16}, &rejected, &skipped) && skipped == 8,
                QString("moving every handle moves only the free one (%1 skipped)").arg(skipped));
        const auto g1 = iw8r::qt::firstSelectedPatchGrid(ctx);
        r.check(g0 && g1 && std::abs(g1->pos[4].z - g0->pos[4].z - 16) < 1e-6 && std::abs(g1->pos[0].z - g0->pos[0].z) < 1e-6,
                "the centre point moved 16 up, the corner stayed");
        r.check(iw8r::qt::lockSelectedPatchVertices(ctx, iw8r::qt::PatchLockMode::Unlock) == 1 && ctx.patchLocks.empty(), "Unlock Vertices clears the locks");
        // Patch Inspector values: lift one corner by 8 and change its s.
        std::vector<std::array<double, 5>> pts;
        for (size_t i = 0; i < g1->pos.size(); ++i) pts.push_back({g1->pos[i].x, g1->pos[i].y, g1->pos[i].z, g1->st[i][0], g1->st[i][1]});
        pts[0][2] += 8;
        pts[0][3] += 0.5;
        r.check(iw8r::qt::applyPatchPoints(ctx, pts), "Patch Inspector applies edited control points");
        const auto g2 = iw8r::qt::firstSelectedPatchGrid(ctx);
        r.check(g2 && std::abs(g2->pos[0].z - g1->pos[0].z - 8) < 1e-6 && std::abs(g2->st[0][0] - g1->st[0][0] - 0.5) < 1e-6,
                "the corner moved 8 up and its texture coordinate changed");
        r.check(!iw8r::qt::applyPatchPoints(ctx, pts), "applying the same values again changes nothing");
        while (ctx.doc.text() != before && ctx.doc.undo()) {}
        ctx.doc.deselectAll();
        ctx.documentEdited("undo patch test");
        r.check(ctx.doc.text() == before, "undo removes the test patch");
    }

    r.section("custom material editor (4.17)");
    {
        // A map saved in its own folder; a generated colour + alpha image; a blended, two-sided, glass-type material.
        const QString dir = QDir(outDir).absoluteFilePath("material_editor");
        QDir(dir).removeRecursively();
        QDir().mkpath(dir);
        QString err;
        r.check(w.saveTo(dir + "/mp_mated.map", &err), "map saved for the material");
        QImage img(64, 64, QImage::Format_RGBA8888);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) img.setPixelColor(x, y, QColor(200, 60 + x * 2, 40, 64 + y * 2));
        const QString src = QDir(outDir).absoluteFilePath("glass_src.png");
        img.save(src);
        const QString path = w.editCustomMaterial(QString(), [&](iw8r::qt::MaterialEditorDialog& d) {
            d.setName("custom/test_glass");
            d.setImage(0, src);
            d.setFlags(false, true, true);
            d.setSurface("glass");
            d.setScale(64, 64);
            const QImage pv = d.preview();
            r.check(!pv.isNull() && pv.width() == 400, "the editor renders a sphere + wall preview");
        });
        r.check(!path.isEmpty() && QFileInfo::exists(dir + "/materials/custom__test_glass_c.png"),
                "Save writes the definition and copies the image beside it: " + QFileInfo(path).fileName());
        const iw8r::CustomMaterial* cm = w.customMaterials() ? w.customMaterials()->find("custom/test_glass") : nullptr;
        r.check(cm && cm->blend && cm->twoSided && cm->width == 64 && cm->surfaceType > 0,
                "the library reloads with the new material (blend, two-sided, 64 units, glass surface)");
    }

    r.section("asset availability audit (4.12)");
    {
        w.action("AssetAudit")->trigger();
        auto* d = w.findChild<QDialog*>("assetAuditDialog");
        auto* kind = d ? d->findChild<QComboBox*>("assetAuditKind") : nullptr;
        auto* summary = d ? d->findChild<QLabel*>("assetAuditSummary") : nullptr;
        r.check(d && kind && kind->count() == 4 && kind->findText("effects") >= 0 && kind->findText("images") >= 0,
                "Tools > Asset Availability Audit offers materials, models, effects and images");
        if (kind && summary) {
            kind->setCurrentText("effects");
            r.check(summary->text().startsWith("Recorded: ") && w.assetAuditStatusFile("effects").endsWith("iw8_effects_status.tsv"),
                    "the dialog shows the recorded effects audit: " + summary->text());
            kind->setCurrentText("materials");
            r.check(summary->text().contains(" ok,"), "and the materials audit: " + summary->text());
        }
        if (d) d->close();
        QApplication::processEvents();
    }

    r.section("effect editor (8.2)");
    {
        const QString folder = QDir(outDir).filePath("fx_editor_test");
        QDir(folder).removeRecursively();
        QString placed;
        iw8r::qt::FxEditorDialog dlg(folder, [&](const QString& n) { placed = n; }, &w);
        r.check(dlg.newEffect("iw8r/gui_smoke") && QFileInfo::exists(dlg.fileFor("iw8r/gui_smoke")) &&
                    dlg.effectNames().contains("iw8r/gui_smoke"),
                "New Effect writes <fx folder>/iw8r/gui_smoke.iw8fx.json and lists it");
        r.check(!dlg.newEffect("bad name"), "an invalid effect name is refused");
        dlg.effectForEdit().elements[0].count = 3;
        iw8r::AuthoredFxElement glow;
        glow.name = "glow";
        glow.additive = true;
        dlg.effectForEdit().elements.push_back(glow);
        r.check(dlg.save(), "Save writes the effect");
        iw8r::qt::FxEditorDialog again(folder, nullptr, &w);
        r.check(again.openEffect("iw8r/gui_smoke") && again.effect().elements.size() == 2 && again.effect().elements[0].count == 3 &&
                    again.effect().elements[1].additive,
                "reopened: both elements and their values are kept");
        r.check(again.previewParticles() > 0, QString("the preview simulates the effect (%1 particles at 1.5 s)").arg(again.previewParticles()));
        dlg.show();
        QApplication::processEvents();
        dlg.grab().save(QDir(outDir).filePath("fx_editor.png"));
        if (auto* placeB = dlg.findChild<QPushButton*>(QString(), Qt::FindChildrenRecursively)) (void)placeB;
        for (QPushButton* b : dlg.findChildren<QPushButton*>())
            if (b->text() == "Place in Map") b->click();
        r.check(placed == "iw8r/gui_smoke", "Place in Map hands the effect name to the map (an fx_origin)");
        // Curve keys: Add Key on an element without keys makes start, end and a middle key; saved and reopened.
        dlg.openEffect("iw8r/gui_smoke");
        for (QPushButton* b : dlg.findChildren<QPushButton*>())
            if (b->text() == "Add Key") b->click();
        r.check(dlg.keyRows() == 3 && dlg.effect().elements[0].keys.size() == 3,
                QString("Add Key makes a three-key curve (%1 rows)").arg(dlg.keyRows()));
        dlg.save();
        iw8r::qt::FxEditorDialog third(folder, nullptr, &w);
        r.check(third.openEffect("iw8r/gui_smoke") && third.effect().elements[0].keys.size() == 3 && third.keyRows() == 3,
                "the curve keys are saved and shown again");
        // An element switched to Light in the Kind box is saved as an omni light.
        if (auto* kind = third.findChild<QComboBox*>("fxElementKind")) kind->setCurrentIndex(1);
        third.save();
        iw8r::qt::FxEditorDialog fourth(folder, nullptr, &w);
        r.check(fourth.openEffect("iw8r/gui_smoke") && fourth.effect().elements[0].kind == "light",
                "Kind > Light saves the element as an omni light");
        if (auto* kind = fourth.findChild<QComboBox*>("fxElementKind")) kind->setCurrentIndex(2);
        if (auto* model = fourth.findChild<QLineEdit*>("fxElementModel")) {
            model->setText("my_rock");
            emit model->editingFinished();
        }
        fourth.save();
        iw8r::qt::FxEditorDialog fifth(folder, nullptr, &w);
        r.check(fifth.openEffect("iw8r/gui_smoke") && fifth.effect().elements[0].kind == "model" &&
                    fifth.effect().elements[0].model == "my_rock",
                "Kind > Model with a model name saves a model element");
        dlg.close();
        r.check(w.action("EffectEditor") != nullptr, "Tools > Effect Editor is in the menus");
    }

    r.section("cache manager (4.13)");
    {
        w.action("CacheManager")->trigger();
        auto* cm = w.findChild<iw8r::qt::CacheManagerDialog*>();
        r.check(cm && cm->rowCount() >= 8, QString("Tools > Cache Manager lists the caches (%1)").arg(cm ? cm->rowCount() : 0));
        if (cm) {
            qint64 total = 0;
            int matRow = -1;
            for (int i = 0; i < cm->rowCount(); ++i) {
                total += cm->rowBytes(i);
                if (cm->rowKey(i) == "material_cache") matRow = i;
            }
            r.check(matRow >= 0 && total > 0, "with their sizes (" + QString::fromStdString(iw8r::formatCacheBytes(uint64_t(total))) + ")");
            // A live cache is only scheduled (volatile test settings): nothing is deleted.
            const QString msg = cm->purge("thumbs", false);
            r.check(cm->pendingPurges().contains("thumbs") && msg.contains("next start"), "purging an open cache waits for the next start");
            iw8r::qt::Settings::instance().setValue("cache/purge", QStringList());
            cm->close();
        }
        QApplication::processEvents();
    }

    r.section("IES profile browser (5.13)");
    {
        const QVariant oldFolder = Settings::instance().value("ies/folder");
        iw8r::qt::IesBrowserDialog dlg(&ctx, &w);
        dlg.setFolder(iw8r::qt::IesBrowserDialog::defaultFolder());
        r.check(dlg.profileCount() >= 4 && dlg.selectProfile("iw8r_spot_narrow.ies"),
                QString("the browser lists the starter profiles (%1) and parses one").arg(dlg.profileCount()));
        const std::string before = ctx.doc.text();
        const iw8r::Id lid = ctx.doc.addEntity({{"classname", "light_spot"}, {"origin", "0 0 96"}}, {}, "test light");
        ctx.doc.deselectAll();
        ctx.doc.select(iw8r::SelRef{lid, iw8r::kNoId}, true);
        r.check(dlg.applyToSelection() == 1 && ctx.doc.findEntity(lid)->value("iw8r_ies") &&
                    ctx.doc.findEntity(lid)->value("iw8r_ies")->find("iw8r_spot_narrow.ies") != std::string::npos,
                "Use for Selected Lights writes iw8r_ies on the light");
        dlg.grab().save(QDir(outDir).filePath("ies_browser.png"));
        while (ctx.doc.text() != before && ctx.doc.undo()) {}
        ctx.documentEdited();
        Settings::instance().setValue("ies/folder", oldFolder);
    }

    r.section("stock reference props as meshes (3.19 / 11.3)");
    {
        // mp_m_king (smallest cached world): props whose models are cached draw their meshes.
        const QString wc = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/world_cache/mp_m_king";
        if (QFileInfo(wc).isDir()) {
            w.loadStockReference("mp_m_king");
            QElapsedTimer t;
            t.start();
            while (w.referencePropMeshes() == 0 && t.elapsed() < 90000) QTest::qWait(250);
            r.check(w.referencePropMeshes() > 0, QString("the reference draws %1 prop(s) as their meshes (%2 s)").arg(w.referencePropMeshes()).arg(t.elapsed() / 1000));
            // 3.20: frustum culling of the reference chunks leaves the picture unchanged and skips chunks.
            CameraView* cv = w.cameraView();
            const app::Camera camBefore = ctx.cam;
            ctx.cam.yaw = 45;
            ctx.cam.pitch = -20;
            ctx.cameraMoved();
            // Textures of the reference (and of its props) stream in over the first frames, which changed pixels between
            // the two renders now and then. The pair is rendered again until two rounds agree (at most five rounds).
            QImage culledImg, fullImg;
            size_t culled = 0;
            int differ = 0;
            for (int round = 0; round < 5; ++round) {
                cv->setReferenceCulling(true);
                culledImg = cv->renderImage(480, 270, true, 0, 300);
                culled = cv->lastCulledReferenceChunks();
                cv->setReferenceCulling(false);
                fullImg = cv->renderImage(480, 270, true, 0, 300);
                cv->setReferenceCulling(true);
                differ = 0;
                if (culledImg.size() == fullImg.size())
                    for (int y = 0; y < culledImg.height(); ++y)
                        for (int x = 0; x < culledImg.width(); ++x) differ += culledImg.pixel(x, y) != fullImg.pixel(x, y);
                if (differ == 0) break;
                QTest::qWait(400);
            }
            {
                // Texture memory budget: past it new textures load at 128 px.
                const size_t budgetBefore = cv->textureBudgetBytes();
                const bool used = cv->textureBytes() > 1024 * 1024;
                cv->setTextureBudgetMB(1);
                const int sideLow = cv->textureSideNow();
                cv->setTextureBudgetMB(4096);
                const int sideHigh = cv->textureSideNow();
                cv->setTextureBudgetMB(int(budgetBefore / (1024 * 1024)));
                r.check(!used || (sideLow == 128 && sideHigh == 512),
                        QString("texture memory budget: %1 MB in use; past a 1 MB budget new textures load at %2 px, under 4 GB at %3 px")
                            .arg(cv->textureBytes() / (1024 * 1024)).arg(sideLow).arg(sideHigh));
            }
            {
                // 11.8: Stock Reference to Brushes around a prop's spot: triangle prisms in a func_group, one undo step.
                const std::string before = ctx.doc.text();
                const iw8r::Vec3 at = w.referencePropSample();
                iw8r::AABB box;
                box.add(at - iw8r::Vec3{160, 160, 96});
                box.add(at + iw8r::Vec3{160, 160, 96});
                ctx.doc.deselectAll();
                iw8r::qt::createBoxBrush(ctx, box);
                const size_t made = w.exportReferenceBrushes();
                bool grouped = false;
                for (const auto& e : ctx.doc.map().entities) grouped |= e.classname() == "func_group" && e.items.size() >= made;
                r.check(made > 0 && grouped, QString("Stock Reference to Brushes makes %1 brush(es) in a func_group").arg(made));
                for (int g = 0; g < 4 && ctx.doc.text() != before; ++g) ctx.doc.undo();
                r.check(ctx.doc.text() == before, "undo removes them");
                ctx.doc.deselectAll();
            }
            r.check(cv->referenceInstances() >= w.referencePropMeshes() && w.referencePropMeshes() > 0,
                    QString("reference props draw as GPU instances (%1 instances)").arg(cv->referenceInstances()));
            {
                // Next to one prop, with and without the instances: the props change the picture.
                const iw8r::AABB wb = w.referenceWorldBounds();
                if (wb.valid()) {
                    const iw8r::Vec3 prop = w.referencePropSample();
                    ctx.cam.pos = prop + iw8r::Vec3{-160, 0, 90};
                    ctx.cam.yaw = 0;
                    ctx.cam.pitch = -25;
                    ctx.cameraMoved();
                    const QImage withProps = cv->renderImage(640, 360, true, 0, 600);
                    withProps.save(QDir(outDir).filePath("reference_instanced.png"));
                    cv->setReferenceInstances({});
                    cv->setReferenceTexInstances({}, {});
                    const QImage without = cv->renderImage(640, 360, true, 0, 300);
                    int changed = 0;
                    for (int y = 0; y < without.height() && withProps.size() == without.size(); ++y)
                        for (int x = 0; x < without.width(); ++x) changed += withProps.pixel(x, y) != without.pixel(x, y);
                    r.check(changed > 100, QString("the instanced props show next to a prop (%1 pixels change without them)").arg(changed));
                }
            }
            r.check(cv->referenceChunks() > 0 && culled > 0 && !culledImg.isNull() && differ == 0,
                    QString("reference culling: %1 of %2 chunks skipped, picture identical (%3 pixels differ)")
                        .arg(culled).arg(cv->referenceChunks()).arg(differ));
            ctx.cam = camBefore;
            ctx.cameraMoved();
            if (QAction* a = w.action("ClearStockReference")) a->trigger();
            // The editor's own big batches are chunked too: 600 boxes of one material in a row behind the camera.
            {
                const std::string before = ctx.doc.text();
                const bool wasTextured = ctx.texturedView;
                if (QAction* m = w.action("CameraModeTextured")) m->trigger();
                for (int i = 0; i < 600; ++i) {
                    ctx.doc.deselectAll();
                    iw8r::AABB bx;
                    bx.add(iw8r::Vec3{-200.0 - (i % 30) * 40.0, 3000.0 + (i / 30) * 400.0, 0});
                    bx.add(iw8r::Vec3{-170.0 - (i % 30) * 40.0, 3030.0 + (i / 30) * 400.0, 30});
                    iw8r::qt::createBoxBrush(ctx, bx);
                }
                ctx.doc.deselectAll();
                ctx.documentEdited();
                ctx.cam.pos = iw8r::Vec3{0, 0, 64};
                ctx.cam.yaw = 90;  // looking down +Y: the far rows are in view, the camera's own area is not
                ctx.cam.pitch = 0;
                ctx.cameraMoved();
                const QImage on = cv->renderImage(480, 270, true, 0, 1500);
                const size_t sceneCulled = cv->lastCulledSceneChunks();
                ctx.cam.yaw = 270;  // looking away: everything culled
                ctx.cameraMoved();
                cv->renderImage(480, 270, true, 0, 300);
                const size_t away = cv->lastCulledSceneChunks();
                if (away > 0)
                    r.check(away > sceneCulled && !on.isNull(),
                            QString("scene batches over 4096 triangles are culled by chunk (%1 looking at them, %2 looking away)").arg(sceneCulled).arg(away));
                else
                    r.text += "  info scene chunk culling not checked (the box material has no texture here)\n";
                while (ctx.doc.text() != before && ctx.doc.undo()) {}
                ctx.doc.deselectAll();
                ctx.documentEdited();
                if (!wasTextured)
                    if (QAction* m = w.action("CameraModeFlat")) m->trigger();
                ctx.cam = camBefore;
                ctx.cameraMoved();
            }
        } else {
            r.check(true, "reference props: mp_m_king is not in the world cache here (skipped)");
        }
    }

    r.section("camera geometry kept between frames (docs/research/85)");
    {
        // The camera keeps its geometry: a selection change or a moved brush writes that item's vertices in place, an
        // added brush puts only its streams together again, and a still frame uploads nothing. After every step the
        // kept geometry must equal a fresh buildCameraGeometry, and the frame must equal one after a forced rebuild.
        CameraView* cv = w.cameraView();
        const std::string before = ctx.doc.text();
        const iw8r::app::Camera camBefore = ctx.cam;
        const bool wasTextured = ctx.texturedView;
        const bool wasLit = ctx.scene.litPreview;
        ctx.doc.deselectAll();
        ctx.documentEdited();
        ctx.scene.sync(ctx.doc);
        SelRef brush{};
        iw8r::AABB all;
        for (const auto* g : ctx.scene.items()) {
            if (g->bounds.valid()) all.add(g->bounds);
            if (brush.item == kNoId && g->kind == iw8r::app::PrimGeom::Brush && !g->brushEntity && !g->volume) brush = g->ref;
        }
        ctx.cam.pos = all.center() + iw8r::Vec3{-300, -260, 180};
        ctx.cam.yaw = 40;
        ctx.cam.pitch = -25;
        ctx.cameraMoved();
        const auto settle = [&] {
            for (int i = 0; i < 400; ++i) {
                cv->renderNow();
                if (!cv->texturesPending()) break;
                QTest::qWait(5);
            }
            cv->renderNow();
        };
        // The frame from the kept geometry against the frame after everything was made again.
        const auto sameAsFresh = [&] {
            cv->renderNow();
            const QImage kept = cv->grabFrame();
            const QString diff = cv->geometrySelfCheck();
            cv->invalidateGeometry();
            cv->renderNow();
            const bool same = !kept.isNull() && kept == cv->grabFrame();
            if (!diff.isEmpty()) r.text += "  info kept geometry differs from a fresh build: " + diff + "\n";
            return same && diff.isEmpty();
        };
        struct Mode {
            const char* action;
            bool lit;
            const char* name;
        };
        const Mode modes[] = {{"CameraModeFlat", false, "flat"}, {"CameraModeTextured", false, "textured"}, {"CameraModeTextured", true, "lit"},
                              {"CameraModeWireframe", false, "wireframe"}, {"CameraModeContents", false, "contents"}};
        for (const Mode& m : modes) {
            if (QAction* a = w.action(m.action)) a->trigger();
            if (ctx.scene.litPreview != m.lit)
                if (QAction* a = w.action("ToggleLitPreview")) a->trigger();
            settle();
            const QString mode = m.name;
            r.check(sameAsFresh(), "kept camera geometry equals a fresh build (" + mode + " mode)");
            settle();
            const size_t builds = cv->geometryBuilds(), relayouts = cv->geometryRelayouts();
            size_t inPlace = cv->itemsUpdatedInPlace();
            const uint64_t generation = ctx.scene.generation();
            const uint64_t casters = cv->shadowCasterVersion();
            // A still frame: nothing is sent to the GPU.
            cv->renderNow();
            r.check(cv->frameStats().uploadBytes == 0 && cv->geometryBuilds() == builds,
                    QString("a still frame uploads nothing (%1 bytes, %2 mode)").arg(cv->frameStats().uploadBytes).arg(mode));
            // Selecting an item: its vertices are written in place, nothing is rebuilt, the scene cache is not synced again.
            ctx.doc.select(brush, false);
            ctx.documentEdited();
            cv->renderNow();
            r.check(cv->geometryBuilds() == builds && cv->geometryRelayouts() == relayouts && cv->itemsUpdatedInPlace() == inPlace + 1 &&
                        ctx.scene.generation() == generation,
                    QString("selecting an item does not rebuild the scene geometry (%1 mode: %2 item written in place, %3 rebuilds)")
                        .arg(mode).arg(cv->itemsUpdatedInPlace() - inPlace).arg(cv->geometryBuilds() - builds));
            // (The shadow map is drawn again when the casters' version changes; the stock reference has its own flag.)
            r.check(cv->shadowCasterVersion() == casters, "a selection change does not ask for the sun shadow map again (" + mode + " mode)");
            r.check(sameAsFresh(), "the frame with a selected item equals a fresh build (" + mode + " mode)");
            settle();
            // Moving it (a document edit): in place again.
            const size_t builds2 = cv->geometryBuilds(), relayouts2 = cv->geometryRelayouts();
            inPlace = cv->itemsUpdatedInPlace();
            ctx.doc.translateSelection(iw8r::Vec3{16, 0, 0});
            ctx.documentEdited();
            cv->renderNow();
            const size_t moveShadowPasses = cv->frameStats().shadowPasses;
            const bool castersMoved = cv->shadowCasterVersion() != casters;
            const size_t moveUpload = cv->frameStats().uploadBytes;
            r.check(cv->geometryBuilds() == builds2 && cv->geometryRelayouts() == relayouts2 && cv->itemsUpdatedInPlace() == inPlace + 1 && moveUpload > 0 &&
                        moveUpload < 64 * 1024 && ctx.scene.generation() != generation,
                    QString("moving a brush writes only that brush (%1 mode: %2 bytes uploaded, %3 rebuilds)").arg(mode).arg(moveUpload).arg(cv->geometryBuilds() - builds2));
            r.check(castersMoved && (!m.lit || moveShadowPasses >= 1), "a moved brush draws the sun shadow map again (" + mode + " mode)");
            r.check(sameAsFresh(), "the frame after a moved brush equals a fresh build (" + mode + " mode)");
            settle();
            // Deselecting: in place.
            const size_t builds3 = cv->geometryBuilds(), relayouts3 = cv->geometryRelayouts();
            ctx.doc.deselectAll();
            ctx.documentEdited();
            cv->renderNow();
            r.check(cv->geometryBuilds() == builds3 && cv->geometryRelayouts() == relayouts3, "deselecting does not rebuild the scene geometry (" + mode + " mode)");
            r.check(sameAsFresh(), "the frame after deselecting equals a fresh build (" + mode + " mode)");
            settle();
            // A new brush and its removal: streams are put together again, the other items are not made again.
            const size_t builds4 = cv->geometryBuilds(), relayouts4 = cv->geometryRelayouts();
            iw8r::AABB bx;
            bx.add(all.center() + iw8r::Vec3{-40, -40, 40});
            bx.add(all.center() + iw8r::Vec3{-8, -8, 72});
            iw8r::qt::createBoxBrush(ctx, bx);
            ctx.documentEdited();
            cv->renderNow();
            r.check(cv->geometryBuilds() == builds4 && cv->geometryRelayouts() == relayouts4 + 1,
                    QString("a new brush does not make the other items again (%1 mode: %2 rebuilds, %3 relayouts)")
                        .arg(mode).arg(cv->geometryBuilds() - builds4).arg(cv->geometryRelayouts() - relayouts4));
            r.check(sameAsFresh(), "the frame with a new brush equals a fresh build (" + mode + " mode)");
            settle();
            ctx.doc.deleteSelection();
            ctx.documentEdited();
            r.check(sameAsFresh(), "the frame after deleting it equals a fresh build (" + mode + " mode)");
            while (ctx.doc.text() != before && ctx.doc.undo()) {}
            ctx.doc.deselectAll();
            ctx.documentEdited();
            r.check(sameAsFresh(), "the frame after undoing everything equals a fresh build (" + mode + " mode)");
        }
        // Face selection: a small stream of its own, nothing rebuilt.
        if (QAction* a = w.action("CameraModeTextured")) a->trigger();
        if (ctx.scene.litPreview)
            if (QAction* a = w.action("ToggleLitPreview")) a->trigger();
        settle();
        {
            const size_t builds = cv->geometryBuilds();
            ctx.doc.selectFace(FaceRef{brush.entity, brush.item, 1}, false);
            ctx.documentEdited();
            cv->renderNow();
            r.check(cv->geometryBuilds() == builds, "selecting a face does not rebuild the scene geometry");
            r.check(sameAsFresh(), "the frame with a selected face equals a fresh build");
            ctx.doc.deselectAll();
            ctx.documentEdited();
        }
        if (QAction* m = w.action(wasTextured ? "CameraModeTextured" : "CameraModeFlat")) m->trigger();
        if (ctx.scene.litPreview != wasLit)
            if (QAction* a = w.action("ToggleLitPreview")) a->trigger();
        ctx.cam = camBefore;
        ctx.cameraMoved();
    }

    r.section("effect preview in the camera (8.1)");
    {
        // An fx_origin with the test effect (found through its export copied into the vfx cache).
        const QString cache = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/vfx_cache/gui_test/vfx/core/mp/core";
        QDir().mkpath(cache);
        QFile::remove(cache + "/vfx_gui_test_chev.0.asset.json");
        QFile::copy(QDir(QCoreApplication::applicationDirPath() + "/../../tests/data/vfx_sim").absoluteFilePath("vfx_hp_chev_grey.vfx.33.asset.json"),
                    cache + "/vfx_gui_test_chev.0.asset.json");
        const std::string before = ctx.doc.text();
        ctx.doc.addEntity({{"classname", "fx_origin"}, {"origin", "0 0 64"}, {"fx", "vfx/core/mp/core/vfx_gui_test_chev"}}, {}, "fx");
        ctx.documentEdited();
        const bool wasTextured = ctx.texturedView;
        if (QAction* m = w.action("CameraModeTextured")) m->trigger();
        if (QAction* a = w.action("ToggleEffectPreview")) a->trigger();
        w.cameraView()->setEffectTime(0.5);
        w.cameraView()->renderAndCount();
        r.check(ctx.effectPreview && w.cameraView()->effectParticlesDrawn() > 0,
                QString("View > Effect Preview in Camera draws the fx_origin's particles (%1)").arg(w.cameraView()->effectParticlesDrawn()));
        if (QAction* a = w.action("ToggleEffectPreview")) a->trigger();
        w.cameraView()->setEffectTime(-1);
        if (!wasTextured)
            if (QAction* m = w.action("CameraModeFlat")) m->trigger();
        while (ctx.doc.text() != before && ctx.doc.undo()) {}
        ctx.documentEdited();
        QFile::remove(cache + "/vfx_gui_test_chev.0.asset.json");
    }

    r.section("colour grade preview: film and LUT (3.15)");
    {
        // Mean saturation of a camera render, before and after a fully desaturating film grade; then a LUT that maps
        // everything to pure red.
        auto meanSat = [](const QImage& img) {
            double s = 0;
            int n = 0;
            for (int y = 0; y < img.height(); y += 7)
                for (int x = 0; x < img.width(); x += 7) {
                    const QColor c = img.pixelColor(x, y);
                    s += c.hsvSaturationF() * c.valueF();
                    ++n;
                }
            return n ? s / n : 0.0;
        };
        auto meanRed = [](const QImage& img) {
            double r = 0, g = 0;
            for (int y = 0; y < img.height(); y += 7)
                for (int x = 0; x < img.width(); x += 7) {
                    const QColor c = img.pixelColor(x, y);
                    r += c.redF(), g += c.greenF();
                }
            return r > 3 * g + 1;
        };
        const std::string before = ctx.doc.text();
        const iw8r::Id ws = ctx.doc.worldspawn()->id;
        const QString d0 = QDir(outDir).filePath("grade_off.png"), d1 = QDir(outDir).filePath("grade_film.png");
        w.exportCameraImage(d0, 320, 180, true);
        ctx.doc.setKeyValue(ws, "iw8r_film", "1 0 1 0 1 1 1 1 1 1");  // contrast 1, brightness 0, desaturation 1
        ctx.documentEdited();
        w.exportCameraImage(d1, 320, 180, true);
        const double s0 = meanSat(QImage(d0)), s1 = meanSat(QImage(d1));
        r.check(s0 > 0.02 && s1 < s0 * 0.25, QString("a desaturating iw8r_film greys the camera (saturation %1 -> %2)").arg(s0, 0, 'f', 3).arg(s1, 0, 'f', 3));
        ctx.doc.removeKey(ws, "iw8r_film");
        // A 2x2x2 .cube mapping every colour to red.
        const QString cube = QDir(outDir).absoluteFilePath("all_red.cube");
        QFile cf(cube);
        if (cf.open(QIODevice::WriteOnly)) cf.write("LUT_3D_SIZE 2\n1 0 0\n1 0 0\n1 0 0\n1 0 0\n1 0 0\n1 0 0\n1 0 0\n1 0 0\n");
        cf.close();
        ctx.doc.setKeyValue(ws, "iw8r_lut", cube.toStdString());
        ctx.documentEdited();
        const QString d2 = QDir(outDir).filePath("grade_lut.png");
        w.exportCameraImage(d2, 320, 180, true);
        r.check(meanRed(QImage(d2)), "an iw8r_lut .cube is applied (an all-red LUT turns the view red)");
        while (ctx.doc.text() != before && ctx.doc.undo()) {}
        ctx.documentEdited();
    }

    r.section("camera image export, loadscreen shot and path frames (3.21, 1.17, 8.15)");
    {
        const QString shot = QDir(outDir).filePath("camera_shot.png");
        QString err;
        r.check(w.exportCameraImage(shot, 1600, 900, true, &err) && QImage(shot).size() == QSize(1600, 900),
                "File > Export Camera Image renders 1600 x 900 off-screen" + (err.isEmpty() ? QString() : " (" + err + ")"));
        const QString big = QDir(outDir).filePath("camera_shot_4k.png");
        r.check(w.exportCameraImage(big, 3840, 2160, false, &err) && QImage(big).size() == QSize(3840, 2160),
                "a 3840 x 2160 image larger than the viewport");
        r.check(!w.exportCameraImage(QDir(outDir).filePath("too_big.png"), 20000, 100, true, &err), "an over-limit size is refused: " + err);
        // The shot is kept beside the map: work on a copy in the test folder (same file name, so the same map id).
        const QString lsDir = QDir(outDir).absoluteFilePath("loadscreen_map");
        QDir().mkpath(lsDir);
        err.clear();
        r.check(w.saveTo(lsDir + "/" + QFileInfo(QString::fromStdString(ctx.doc.path())).fileName(), &err), "map copy saved for the loadscreen shot");
        const QString ls = w.setLoadscreenFromCamera(&err);
        r.check(!ls.isEmpty() && ls.startsWith(lsDir) && ls.endsWith(".loadscreen.png") && QImage(ls).size() == QSize(2048, 1024),
                "Compile > Set Loadscreen From Camera writes a 2048 x 1024 shot beside the map: " + QFileInfo(ls).fileName());
        err.clear();
        // Two keys 600 units apart (timed by distance at the path speed, 300 units/s by default): 2 s, 9 frames at 4 fps.
        if (QAction* a = w.action("CameraPathClear")) a->trigger();
        const iw8r::Vec3 camStart = ctx.cam.pos;
        if (QAction* a = w.action("CameraPathAddKey")) a->trigger();
        ctx.cam.pos = ctx.cam.pos + iw8r::Vec3{600, 0, 0};
        if (QAction* a = w.action("CameraPathAddKey")) a->trigger();
        ctx.cam.pos = camStart;
        const QString frames = QDir(outDir).filePath("path_frames");
        QDir(frames).removeRecursively();
        const int n = w.exportCameraPathFrames(frames, 4, 320, 180, &err);
        r.check(n >= 2 && QFileInfo::exists(frames + "/frame_00000.png") && QImage(frames + "/frame_00001.png").size() == QSize(320, 180),
                QString("Camera Path > Export Frames writes a PNG sequence (%1 frame(s))").arg(n) + (err.isEmpty() ? QString() : " " + err));
        if (QAction* a = w.action("CameraPathClear")) a->trigger();
        // Turntable of a selected brush: 8 views around it, the selection restored afterwards.
        ctx.doc.deselectAll();
        for (const auto* g : ctx.scene.items())
            if (g->kind == iw8r::app::PrimGeom::Brush) {
                ctx.doc.select(g->ref, true);
                break;
            }
        const size_t selBefore = ctx.doc.selection().size();
        const QString tt = QDir(outDir).filePath("turntable");
        QDir(tt).removeRecursively();
        err.clear();
        const int tn = w.exportTurntable(tt, 8, 256, 256, &err);
        r.check(tn == 8 && QImage(tt + "/turntable_00007.png").size() == QSize(256, 256) && ctx.doc.selection().size() == selBefore,
                QString("File > Export Turntable writes 8 frames and keeps the selection (%1)").arg(tn) + (err.isEmpty() ? QString() : " " + err));
        ctx.doc.deselectAll();
        ctx.documentEdited("turntable test");
    }

    r.section("compile: native IW8 extraction of the open map");
    {
        CompilePanel* cp = w.compilePanel();
        r.check(cp && cp->mapId().startsWith("mp_"), "compile panel derives an IW8 map id: " + (cp ? cp->mapId() : QString("?")));
        if (cp) {
            bool done = false, ok = false;
            auto conn = QObject::connect(cp, &CompilePanel::finished, [&](bool k) { done = true; ok = k; });
            w.action("CompileExtraction")->trigger();
            for (int i = 0; i < 600 && !done; ++i) QTest::qWait(50);
            QObject::disconnect(conn);
            const QString ex = cp->extractionDir();
            const QString m = cp->mapId();
            r.check(done && ok, "Shift+F7 compiled the extraction" + (ok ? QString() : " (log: " + cp->logText().right(300) + ")"));
            r.check(QFile::exists(ex + "/complete") && QFile::exists(ex + "/skeleton/complete") &&
                        QFile::exists(ex + "/extracted/maps/mp/" + m + ".d3dbsp.replay-world.json") &&
                        QFile::exists(ex + "/extracted/maps/mp/" + m + ".d3dbsp.replay-collision.json") &&
                        QFile::exists(ex + "/extracted/maps/mp/" + m + ".d3dbsp.replay-comworld.json") &&
                        QFile::exists(ex + "/extracted/maps/mp/" + m + ".d3dbsp.ents"),
                    "extraction folder has the markers, world, collision, comworld and .ents: " + ex);
            {
                QFile ents(ex + "/extracted/maps/mp/" + m + ".d3dbsp.ents");
                const QByteArray et = ents.open(QIODevice::ReadOnly) ? ents.readAll() : QByteArray();
                r.check(QFile::exists(ex + "/extracted/images/compass_map_" + m + ".dds") && et.count("minimap_corner") == 2,
                        "the extraction has the DXT5 compass image and two minimap_corner script_origins");
                {
                    auto* bp = w.budgetPanel();
                    if (bp) bp->refresh();
                    r.check(bp && bp->gaugeCount() == 8, QString("the Build Budget dock shows the build's 8 gauges (%1, %2 over; report %3)")
                                                             .arg(bp ? bp->gaugeCount() : -1).arg(bp ? bp->overCount() : -1)
                                                             .arg(QFileInfo::exists(cp->buildRoot() + "/last_build_report.json") ? "present" : "missing"));
                }
                r.check(QFile::exists(ex + "/extracted/images/loadscreen_" + m + ".dds"),
                        "the loadscreen shot is built as images/loadscreen_" + m + ".dds (the converter's preview source)");
            }
            // Opt-in (about a minute, writes to the x64-zt output root): F7 = extraction + x64-zt zones + ACTS --test.
            if (qEnvironmentVariableIsSet("IW8R_FULL_COMPILE")) {
                const std::string beforeFull = ctx.doc.text();
                done = ok = false;
                auto c2 = QObject::connect(cp, &CompilePanel::finished, [&](bool k) { done = true; ok = k; });
                QTest::keyClick(&w, Qt::Key_F7);
                for (int i = 0; i < 12000 && !done; ++i) QTest::qWait(50);
                QObject::disconnect(c2);
                const QString z = cp->zonesDir();
                r.check(done && ok, "F7: extraction, x64-zt buildextraction and ACTS --test all passed" +
                                        (ok ? QString() : " (log: " + cp->logText().right(600) + ")"));
                r.check(QFile::exists(cp->extractionDir() + "/extracted/materials/iw8r_stock/tm2co/me_curb_stone_01.json"),
                        "stock textures: me_curb_stone_01 was built with its colour image (" +
                            cp->logText().section("stock textures:", 1, 1).section('\n', 0, 0).trimmed() + ")");
                r.check(QFile::exists(z + "/" + m + ".ff") && QFile::exists(z + "/eng_" + m + ".ff") && QFile::exists(z + "/ww_" + m + ".ff") &&
                            QFile::exists(z + "/techsets_" + m + ".ff") && QFile::exists(z + "/srv_" + m + ".ff"),
                        "five Replay 1.20 zones written to " + z);
                r.check(ctx.doc.text() == beforeFull, "the full compile leaves the document unchanged");
                // Deploy into a scratch game folder (never the real install): first install, then a replace with backup, then undo.
                const QString scratch = QDir(outDir).filePath("scratch_game");
                QDir(scratch).removeRecursively();
                QDir().mkpath(scratch);
                QString derr;
                const QString installed = scratch + "/mods/mw120r/maps/" + m;
                r.check(cp->deploy(scratch, &derr) && QFile::exists(installed + "/" + m + ".ff") && QFile::exists(installed + "/map.json") &&
                            QDir(installed).entryList(QDir::Files).size() == 6,
                        "deploy installed exactly the five zones + map.json into the scratch mods/mw120r/maps" + (derr.isEmpty() ? QString() : " (" + derr + ")"));
                QFile marker(installed + "/old_marker.txt");
                if (marker.open(QIODevice::WriteOnly)) marker.write("previous package");
                marker.close();
                QTest::qWait(1100);  // the backup folder name has a one-second stamp
                const bool again = cp->deploy(scratch, &derr);
                const QStringList backups = QDir(scratch + "/.proxy/backups").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                r.check(again && !QFile::exists(installed + "/old_marker.txt") && backups.size() == 1,
                        "a second deploy moved the previous package to .proxy/backups");
                r.check(cp->undoDeploy(&derr) && QFile::exists(installed + "/old_marker.txt"), "Undo Last Deploy restored the previous package");
                {
                    // A handle inside the installed folder (Explorer, antivirus, the proxy) blocks the folder rename:
                    // the deploy falls back to replacing the files one by one.
                    QFile holder(installed + "/old_marker.txt");
                    const bool held = holder.open(QIODevice::ReadOnly);
                    QTest::qWait(1100);
                    const QDateTime before = QFileInfo(installed + "/" + m + ".ff").lastModified();
                    const bool swapped = cp->deploy(scratch, &derr);
                    r.check(held && swapped && QFile::exists(installed + "/" + m + ".ff") && cp->logText().contains("replacing its"),
                            "with a file held open in the installed folder, deploy replaces the package files in place" +
                                (derr.isEmpty() ? QString() : " (" + derr + ")"));
                    Q_UNUSED(before);
                    holder.close();
                }
                QFile logFile(QDir(outDir).filePath("compile-log.txt"));
                if (logFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) logFile.write(cp->logText().toUtf8());
            } else {
                r.text += "  skip full compile (set IW8R_FULL_COMPILE=1)\n";
            }
            r.check(!CompilePanel::gameRunningFrom(QDir(outDir).filePath("scratch_game")), "no Replay process runs from the scratch game folder");
        }
    }

    r.section("open a stock IW8 zone through ACTS (mp_shipment, map_ents only)");
    const QString zone = "E:/IW8/Builds/1.20-replay/zone/mp_shipment.ff";
    if (QFile::exists(zone)) {
        QString zerr;
        const bool ok = w.openStockZone(zone, &zerr);
        r.check(ok, "ACTS read mp_shipment and the editor opened it" + (zerr.isEmpty() ? QString() : " (" + zerr.left(200) + ")"));
        if (ok) {
            size_t canonical = 0;
            for (const auto& e : ctx.doc.map().entities)
                for (const auto& it : e.items)
                    if (const KeyValueLine* kv = it.kv(); kv && kv->form == KeyForm::Canonical) ++canonical;
            r.check(ctx.doc.map().entities.size() > 100 && canonical > 0 && !ctx.scene.spawns.empty(),
                    QString("stock entities %1 (canonical keys %2), compiled spawns %3").arg(ctx.doc.map().entities.size()).arg(canonical).arg(ctx.scene.spawns.size()));
            r.check(ctx.doc.text().back() == '\0' && !ctx.doc.modified(), "entity string loaded unmodified (NUL terminator kept)");
            // 6.1: the compiled spawns become entities, and the export splits them back out unchanged.
            const std::string stockText = ctx.doc.text();
            const size_t spawnCount = ctx.scene.spawns.size(), entCount = ctx.doc.map().entities.size();
            r.check(w.stockZone() == "mp_shipment" && w.editStockSpawns() && ctx.scene.spawns.empty() &&
                        ctx.doc.map().entities.size() == entCount + spawnCount,
                    QString("Edit Compiled Spawns adds %1 spawn entities").arg(spawnCount));
            QStringList args;
            QString perr;
            const QString work = QDir(outDir).filePath("stock_rebuild");
            QDir(work).removeRecursively();
            r.check(w.prepareStockRebuild(work, &args, &perr) && args.contains("-Spawns"), "the stock rebuild gets an entity string and a spawnList: " + perr);
            {
                // The fast path writes one entity text (the spawn entities ride in it) and an edits file for the
                // converter's editzone command.
                QString program, eerr;
                QStringList eargs, enotes;
                const QString ework = work + "_edit";
                const bool prepared = w.prepareStockEdit(ework, &program, &eargs, &eerr, &enotes);
                QFile ef(ework + "/edit.ents"), jf(ework + "/edits.json");
                const QByteArray etext = ef.open(QIODevice::ReadOnly) ? ef.readAll() : QByteArray();
                const QByteArray ejson = jf.open(QIODevice::ReadOnly) ? jf.readAll() : QByteArray();
                r.check(prepared && eargs.contains("editzone") && eargs.contains(w.stockZone()) && etext.contains("mp_") &&
                            etext.contains("spawn") && ejson.startsWith("{\"entities\":") && ejson.contains("edit.ents"),
                        QString("the fast stock rebuild writes the entity text with the spawn entities and an edits file (%1; %2 bytes of text)")
                            .arg(prepared ? QString("ok") : eerr).arg(etext.size()));
            }
            QFile ef(work + "/edit.ents");
            r.check(ef.open(QIODevice::ReadOnly) && ef.readAll().toStdString() == stockText,
                    "unchanged spawns: the exported entity string is the stock one byte for byte");
            QFile sf(work + "/edit.spawns.ents");
            r.check(sf.open(QIODevice::ReadOnly) && QString::fromUtf8(sf.readAll()).count("{") == int(spawnCount),
                    "the exported spawnList text has one entity per spawn");
            for (int i = 0; i < 8 && ctx.doc.modified(); ++i) w.action("Undo")->trigger();
            r.check(ctx.doc.text() == stockText, "Undo removes the spawn entities again");
        }
    } else {
        r.text += "  skip (no " + zone + ")\n";
    }

    // 13.5: the loot points of a Warzone stock map as entities, and their way into the stock rebuild.
    iw8r::qt::runLootPointChecks([&r](const QString& sname) { r.section(sname); }, [&r](bool ok, const QString& m) { r.check(ok, m); },
                                 [&r](const QString& why) { r.text += "  skip (" + why + ")\n"; }, w, outDir);

    r.section("map source package export / import (4.26)");
    {
        // Saved first (the document here may have no file), into its own folder with a sidecar loadscreen shot.
        const QString src = QDir(outDir).absoluteFilePath("package_src");
        QDir(src).removeRecursively();
        QDir().mkpath(src);
        QString err;
        r.check(w.saveTo(src + "/mp_pkgtest.map", &err), "map saved for packaging");
        QImage(64, 32, QImage::Format_RGB888).save(src + "/mp_pkgtest.loadscreen.png");
        const QString zip = QDir(outDir).filePath("map_package.zip");
        QFile::remove(zip);
        const int n = w.exportMapPackage(zip, &err);
        r.check(n >= 1 && QFileInfo(zip).size() > 0, QString("File > Export Map Package wrote %1 file(s)").arg(n) + (err.isEmpty() ? QString() : " (" + err + ")"));
        const QString dest = QDir(outDir).filePath("package_import");
        QDir(dest).removeRecursively();
        QDir().mkpath(dest);
        const QStringList rootsBefore = Settings::instance().value("prefabRoots").toStringList();
        err.clear();
        const QString map = w.importMapPackage(zip, dest, &err);
        r.check(!map.isEmpty() && QFileInfo::exists(map), "File > Import Map Package extracts it and finds the map: " + QFileInfo(map).fileName() + err);
        r.check(QFileInfo::exists(dest + "/mp_pkgtest.loadscreen.png"), "the loadscreen sidecar travels with the map");
        QFile a(QString::fromStdString(ctx.doc.path()));
        QFile b(map);
        r.check(a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly) && a.readAll() == b.readAll(), "the imported map is byte for byte the saved one");
        Settings::instance().setValue("prefabRoots", rootsBefore);  // the test leaves the owner's settings as they were
    }

    r.section("Links dock: node view of target chains (6.25)");
    {
        const QString lm = QDir(outDir).filePath("links_test.map");
        QFile lf(lm);
        if (lf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            lf.write("iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n}\n"
                     "// entity 1\n{\n\"classname\" \"trigger_radius\"\n\"origin\" \"0 0 0\"\n\"radius\" \"64\"\n\"height\" \"64\"\n\"target\" \"door1\"\n}\n"
                     "// entity 2\n{\n\"classname\" \"script_model\"\n\"origin\" \"128 0 0\"\n\"targetname\" \"door1\"\n\"target\" \"fx1\"\n}\n"
                     "// entity 3\n{\n\"classname\" \"script_origin\"\n\"origin\" \"256 0 0\"\n\"targetname\" \"fx1\"\n}\n"
                     "// entity 4\n{\n\"classname\" \"script_origin\"\n\"origin\" \"0 128 0\"\n\"target\" \"nothing_here\"\n}\n");
            lf.close();
        }
        w.openPath(lm);
        iw8r::qt::LinkGraphView* lv = w.linkGraphView();
        r.check(lv != nullptr, "the Links dock exists");
        if (lv) {
            lv->resize(900, 400);
            lv->rebuild();
            r.check(lv->nodeCount() == 3 && lv->edgeCount() == 2, QString("3 linked entities and 2 links (%1, %2)").arg(lv->nodeCount()).arg(lv->edgeCount()));
            r.check(lv->issueCount() == 1, QString("the target naming nothing is flagged (%1 issue)").arg(lv->issueCount()));
            iw8r::Id mid = iw8r::kNoId;
            for (const auto& e : ctx.doc.map().entities)
                if (e.classname() == "script_model") mid = e.id;
            const QPointF c = lv->nodeCenter(mid);
            QTest::mouseClick(lv, Qt::LeftButton, Qt::NoModifier, c.toPoint());
            r.check(ctx.doc.isSelected(iw8r::SelRef{mid, iw8r::kNoId}), "clicking a node selects its entity");
            lv->grab().save(QDir(outDir).filePath("links_view.png"));
        }
    }

    // Prefab bundles (docs/PREFAB_BUNDLES.md): dock, placing, parameters, save, explode / edit, assets, build.
    iw8r::qt::runPrefabBundleChecks([&r](const QString& sname) { r.section(sname); }, [&r](bool ok, const QString& m) { r.check(ok, m); }, w,
                                    outDir);
    // Models inside placed prefabs, scriptables with their definition's model, reference markers out of captures.
    iw8r::qt::runPrefabModelChecks([&r](const QString& sname) { r.section(sname); }, [&r](bool ok, const QString& m) { r.check(ok, m); }, w,
                                   outDir);

    r.text += QString("\n%1 checks, %2 failures\n").arg(r.checks).arg(r.failures);
    QFile f(QDir(outDir).filePath("gui-test-report.txt"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(r.text.toUtf8());
    return r.failures ? 1 : 0;
}

}  // namespace iw8r::qt
