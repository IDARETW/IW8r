#include "qt_gui_test_stock_panel.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTableWidget>

#include <DockManager.h>
#include <DockWidget.h>

#include "brush_ops.h"
#include "main_window.h"
#include "settings.h"
#include "stock_map_panel.h"
#include "stock_smodels.h"
#include "stock_zone_edit.h"

namespace iw8r::qt {

namespace {

QString worldKey(const Document& doc, const char* key) {
    for (const Entity& e : doc.map().entities)
        if (e.isWorldspawn()) {
            const std::string* v = e.value(key);
            return v ? QString::fromStdString(*v) : QString("(unset)");
        }
    return "(no worldspawn)";
}

void pump() {
    for (int i = 0; i < 3; ++i) QApplication::processEvents();
}

}  // namespace

void runStockPanelChecks(std::function<void(const QString&)> section, std::function<void(bool, const QString&)> check,
                         std::function<void(const QString&)> skip, MainWindow& w, const QString& zonePath, bool closeAndReopen) {
    section("Stock Map side panel (sun, sky, retexture, removal boxes, props)");
    EditorContext& ctx = w.context();
    Document& doc = ctx.doc;
    StockMapPanel* p = w.stockMapPanel();
    check(p != nullptr, "the main window has a Stock Map panel");
    if (!p) return;
    if (w.stockZone().isEmpty()) {
        skip("no stock map is open");
        return;
    }
    const std::string stockText = doc.text();
    const size_t pos0 = doc.undoPosition();
    pump();
    QAction* side = w.findChild<QAction*>("side_Stock_Map");
    check(side && side->isVisible(), "its side bar icon shows while a stock map is open (" + w.stockZone() + ")");
    w.showDock("Stock Map");
    pump();
    check(p->isVisible() && p->scrollArea() && p->scrollArea()->isVisible(), "the side bar opens it on the stock map's page");
    {
        const int need = p->scrollArea() && p->scrollArea()->widget() ? p->scrollArea()->widget()->minimumSizeHint().width() : 9999;
        // The widest child, named when the column is too wide.
        QString widest;
        int widestW = 0;
        if (p->scrollArea() && p->scrollArea()->widget())
            for (QWidget* c : p->scrollArea()->widget()->findChildren<QWidget*>())
                if (c->isVisibleTo(p) && c->findChildren<QWidget*>(Qt::FindDirectChildrenOnly).isEmpty() &&  // single controls
                    c->minimumSizeHint().width() > widestW) {
                    widestW = c->minimumSizeHint().width();
                    const auto* lab = qobject_cast<const QLabel*>(c);
                    widest = QString("%1 %2 %3").arg(c->metaObject()->className(), c->objectName(), lab ? lab->text().left(60) : QString());
                }
        // 284 px with the Windows UI font ("Intensity" 50 px wide, Segoe UI 9 pt); the suite runs on the offscreen
        // platform, whose fallback font is about twice as wide, so the limit follows the font in use.
        const double fontScale = std::max(1.0, p->fontMetrics().horizontalAdvance("Intensity") / 50.0);
        const int limit = int(std::lround(284 * fontScale));
        check(need <= limit, QString("it fits the narrow side section: its column needs %1 px (%2 px allowed: 300 px with the scroll bar, "
                                     "x%3 for this font)%4")
                                 .arg(need)
                                 .arg(limit)
                                 .arg(fontScale, 0, 'f', 2)
                                 .arg(need <= limit ? QString() : QString("; widest: %1 at %2 px").arg(widest).arg(widestW)));
        if (need > limit && p->scrollArea() && p->scrollArea()->widget()) {
            // Every visible widget and layout 240 px or wider, with its parents, into the report (diagnosis).
            QWidget* body = p->scrollArea()->widget();
            const auto pathOf = [body](const QObject* o) {
                QStringList parts;
                for (; o && o != body; o = o->parent())
                    parts.prepend(QString("%1(%2)").arg(o->metaObject()->className(), o->objectName()));
                return parts.join("/");
            };
            for (QWidget* c : body->findChildren<QWidget*>())
                if (c->isVisibleTo(p) && c->minimumSizeHint().width() >= 240) {
                    QString extra;
                    if (const auto* lab = qobject_cast<const QLabel*>(c)) extra = " text \"" + lab->text().left(80) + "\"";
                    if (const auto* ab = qobject_cast<const QAbstractButton*>(c)) extra = " text \"" + ab->text().left(80) + "\"";
                    std::fprintf(stderr, "stock panel width: %s\n", qPrintable(QString("    wide widget %1 px (min width %2, fixed %3): %4%5")
                               .arg(c->minimumSizeHint().width()).arg(c->minimumWidth()).arg(c->maximumWidth())
                               .arg(pathOf(c), extra)));
                }
            for (QLayout* lay : body->findChildren<QLayout*>())
                if (lay->minimumSize().width() >= 240) {
                    QStringList items;
                    for (int k = 0; k < lay->count(); ++k)
                        if (QLayoutItem* it = lay->itemAt(k))
                            items << QString("%1:%2").arg(it->widget() ? QString(it->widget()->metaObject()->className()) + "(" +
                                                                             it->widget()->objectName() + ")"
                                                                       : QString(it->layout() ? "layout" : "spacer"))
                                                     .arg(it->minimumSize().width());
                    std::fprintf(stderr, "stock panel width: %s\n", qPrintable(QString("    wide layout %1 px in %2: %3").arg(lay->minimumSize().width()).arg(pathOf(lay->parentWidget()), items.join(" "))));
                }
        }
    }

    // ---- Sun: one undo step per change, a slider drag one step, spin box steps right after each other one step.
    p->setSunIntensity(12.5);
    check(worldKey(doc, "iw8r_sun_intensity") == "12.5" && doc.undoPosition() == pos0 + 1 && doc.history()[pos0].name == "Stock Sun Intensity",
          "Intensity sets worldspawn iw8r_sun_intensity in one undo step: " + worldKey(doc, "iw8r_sun_intensity"));
    {
        QSlider* s = p->intensitySlider();
        bool live = true;
        s->setSliderDown(true);  // the mouse on the handle
        for (const int v : {500, 520, 540, 560}) {
            s->setValue(v);
            const double want = 0.1 * std::pow(10.0, 4.0 * v / 1000.0);
            live = live && std::fabs(worldKey(doc, "iw8r_sun_intensity").toDouble() - want) < 0.06;
        }
        s->setSliderDown(false);
        const double last = 0.1 * std::pow(10.0, 4.0 * 560 / 1000.0);
        check(live, "while the intensity slider is dragged, every position reaches the key (the viewport follows)");
        check(doc.undoPosition() == pos0 + 2 && std::fabs(worldKey(doc, "iw8r_sun_intensity").toDouble() - last) < 0.06,
              QString("the drag is one undo step (%1 steps, key %2)").arg(doc.undoPosition() - pos0).arg(worldKey(doc, "iw8r_sun_intensity")));
        pump();
        check(std::fabs(p->intensitySpin()->value() - last) < 0.06, "the spin box beside it shows the dragged value");
        doc.undo();
        ctx.documentEdited();
        check(worldKey(doc, "iw8r_sun_intensity") == "12.5", "Undo takes the whole drag back to the value before it");
        doc.redo();
        ctx.documentEdited();
    }
    const size_t posDir = doc.undoPosition();
    p->pitchSpin()->setValue(-30);
    p->pitchSpin()->setValue(-31);
    p->yawSpin()->setValue(45);
    check(worldKey(doc, "iw8r_sun_direction") == "-31 45" && doc.undoPosition() == posDir + 1,
          QString("pitch and yaw steps right after each other are one undo step (%1 steps): iw8r_sun_direction %2")
              .arg(doc.undoPosition() - posDir).arg(worldKey(doc, "iw8r_sun_direction")));
    p->setSunColor(1, 0.5, 0.25);
    check(worldKey(doc, "iw8r_sun_color") == "1 0.5 0.25" && doc.undoPosition() == posDir + 2, "Colour sets iw8r_sun_color (linear r g b)");
    {
        const StockMapPanel::Sun s = p->shownSun();
        check(s.color[1] == 0.5 && std::fabs(s.pitch + 31) < 1e-9 && std::fabs(s.yaw - 45) < 1e-9, "the panel's sun is the document's keys over the stock sun");
    }
    // The Sun's text says what changed; a direction change warns about the slower rebuild.
    pump();
    check(p->sunInfo()->text().contains("Changed: colour, intensity, direction") && p->sunInfo()->text().contains("bakes the sun shadow again"),
          "the Sun's note lists the changed values: " + p->sunInfo()->text());
    const size_t posReset = doc.undoPosition();
    p->resetSun();
    check(worldKey(doc, "iw8r_sun_color") == "(unset)" && worldKey(doc, "iw8r_sun_intensity") == "(unset)" &&
              worldKey(doc, "iw8r_sun_direction") == "(unset)" && doc.undoPosition() == posReset + 1,
          "Reset to Stock removes the three keys in one undo step");
    doc.undo();
    ctx.documentEdited();
    check(worldKey(doc, "iw8r_sun_intensity") != "(unset)", "Undo brings the edited sun back");

    // ---- Sky
    {
        const QStringList choices = p->skyChoices();
        check(choices.size() >= 5 && (choices.contains("mp_backlot2") || choices.contains("mp_shipment")) && !choices.contains(w.stockZone()),
              QString("the Sky list offers the stock maps (%1), not this map").arg(choices.size()));
        p->setSkyFrom("mp_backlot2");
        pump();
        check(worldKey(doc, "iw8r_sky_from") == "mp_backlot2" && p->skyCombo()->currentData().toString() == "mp_backlot2",
              "picking a map sets iw8r_sky_from and the list shows it");
        p->setSkyFrom("");
        pump();
        check(worldKey(doc, "iw8r_sky_from") == "(unset)" && p->skyCombo()->currentIndex() == 0, "\"This map's own sky\" removes the key");
    }

    // ---- Retexture
    {
        check(p->setRetexturePair("tw/old_floor", "tw/brick_stone_wall_rough_pale"), "a pair is set");
        check(worldKey(doc, "iw8r_stock_retexture") == "tw/old_floor=tw/brick_stone_wall_rough_pale", "it is the key iw8r_stock_retexture");
        p->setRetexturePair("tw/a", "tw/not_a_material_xyz");
        pump();
        QTableWidget* t = p->retextureTable();
        check(worldKey(doc, "iw8r_stock_retexture") == "tw/old_floor=tw/brick_stone_wall_rough_pale; tw/a=tw/not_a_material_xyz" &&
                  t->rowCount() == 2,
              "a second pair joins it; the table lists both: " + worldKey(doc, "iw8r_stock_retexture"));
        check(t->item(0, 1) && !t->item(0, 1)->toolTip().contains("left out") && t->item(1, 1) && t->item(1, 1)->toolTip().contains("left out"),
              "the pair the rebuild cannot carry is marked as left out");
        p->setRetexturePair("tw/old_floor", "tw/other_pale");
        const auto pairs = p->retexturePairs();
        check(pairs.size() == 2 && pairs[0].second == "tw/other_pale", "setting a From again replaces its pair");
        check(!p->setRetexturePair("tw/a b", "tw/c"), "a name with a space is refused");
        p->removeRetexturePair("tw/a");
        p->removeRetexturePair("tw/old_floor");
        check(worldKey(doc, "iw8r_stock_retexture") == "(unset)", "removing the last pair removes the key");
        p->setPickedMaterial("tw/picked_surface");
        check(p->fromEdit()->text() == "tw/picked_surface", "a picked surface's material fills From (setPickedMaterial)");
        const std::string currentBefore = ctx.currentMaterial;
        ctx.currentMaterial = "tw/current_pick";
        p->toEdit()->clear();
        if (auto* b = p->findChild<QAbstractButton*>("stockRetextureToBrowser")) b->click();
        check(!p->toEdit()->text().isEmpty() && p->toEdit()->text() == p->browserMaterial(),
              "the Browser button fills To from the Materials browser: " + p->toEdit()->text());
        ctx.currentMaterial = currentBefore;
    }

    // ---- World removal boxes
    {
        AABB box;
        box.add(Vec3{-32, -32, 0});
        box.add(Vec3{32, 32, 64});
        const auto prim = makeBrushPrimitive(makeBox(box, " caulk 128 128 0 0 0 0"), doc.eol());
        check(prim.has_value(), "a helper brush is made");
        if (!prim) return;
        doc.deselectAll();
        check(p->addRemovalBoxAroundSelection() == kNoId, "without a selection no box is added");
        doc.addEntity({{"classname", "func_group"}}, {*prim}, "Helper brush");  // selected
        ctx.documentEdited();
        const Id id = p->addRemovalBoxAroundSelection();
        const Entity* e = doc.findEntity(id);
        pump();
        check(e && isRemoveWorldClass(e->classname()) && p->removalBoxes().size() == 1 && p->removalList()->count() == 1,
              "Add Box Around Selection makes an iw8r_remove_world box, listed in the panel");
        check(w.stockWorldEdits().boxes.size() == 1 && w.stockWorldEdits().boxes[0][0] == -32 && w.stockWorldEdits().boxes[0][5] == 64,
              "the viewport's world edits take the box (around the selected brush)");
        check(p->removalList()->count() == 1 && p->removalList()->item(0)->checkState() == Qt::Checked, "its row is checked: it takes out the drawing");
        p->setRemovalBoxRender(id, false);
        check(doc.findEntity(id)->value("render") && *doc.findEntity(id)->value("render") == "0" && w.stockWorldEdits().boxes.empty(),
              "unchecked: render 0, the viewport keeps the triangles (only the collision goes)");
        pump();
        p->removalList()->item(0)->setCheckState(Qt::Checked);  // the row's check box
        check(!doc.findEntity(id)->value("render") && w.stockWorldEdits().boxes.size() == 1, "checking the row again removes the render key");
        p->setRemovalBoxCollision(id, false);
        check(doc.findEntity(id)->value("collision") && *doc.findEntity(id)->value("collision") == "0", "Keep Collision sets collision 0");
        // World brushes become boxes.
        Id worldId = kNoId;
        for (const Entity& we : doc.map().entities)
            if (we.isWorldspawn()) worldId = we.id;
        doc.editPrimitives({{worldId, kNoId, *prim}}, "World brush");
        ctx.documentEdited();
        const Id made = p->makeSelectedBrushesBoxes();
        pump();
        check(made != kNoId && p->removalBoxes().size() == 2 && p->removalList()->count() == 2, "Selected Brushes Become Boxes makes a second removal entity");
        p->removalList()->setCurrentRow(1);
        if (auto* del = p->findChild<QPushButton*>("stockRemovalDelete")) del->click();
        pump();
        check(p->removalBoxes().size() == 1 && p->removalList()->count() == 1, "Delete takes the picked box out");
    }

    // ---- Hidden props (the panel's buttons call File > Stock Map > Hide Stock Props In Selection / Show All).
    {
        std::vector<StockModelInstance> props(3);
        const Vec3 at[3] = {Vec3{0, 0, 10}, Vec3{10, 10, 20}, Vec3{900, 0, 0}};
        for (int i = 0; i < 3; ++i) props[size_t(i)].model = "test_prop", props[size_t(i)].index = uint32_t(20 + i), props[size_t(i)].origin = at[i];
        w.setReferencePropsForTest(props);
        doc.deselectAll();
        for (const Entity& e : doc.map().entities)
            if (isRemoveWorldClass(e.classname()))
                for (const auto& it : e.items)
                    if (it.isPrimitive()) doc.select({e.id, it.id}, true);
        ctx.documentEdited();
        pump();
        if (auto* hide = p->findChild<QPushButton*>("stockPropsHide")) hide->click();
        pump();
        check(p->hiddenPropCount() == 2 && p->propsInfo()->text().startsWith("2 stock prop(s) hidden"),
              "Hide In Selection hides the 2 props inside the selected box: " + p->propsInfo()->text());
        if (auto* show = p->findChild<QPushButton*>("stockPropsShow")) show->click();
        pump();
        check(p->hiddenPropCount() == 0, "Show All brings them back");
        // A prop clicked in the camera (MainWindow::stockPropPicked): hidden and shown on its own, one undo step each.
        emit w.stockPropPicked("test_prop", 22);
        pump();
        // (The model name carries zero-width break points so it wraps in the narrow column.)
        const QString pickedText = p->pickedPropInfo()->text().remove(QChar(0x200B));
        check(pickedText.startsWith("Picked prop: test_prop (row 22)"), "a prop picked in the camera is named: " + pickedText);
        const size_t posPick = doc.undoPosition();
        const bool hid = p->togglePickedPropHidden();
        const QString hiddenKey = worldKey(doc, "iw8r_hide_smodels");
        check(hid && hiddenKey == "22" && doc.undoPosition() == posPick + 1,
              "Hide Picked Prop adds its row to iw8r_hide_smodels in one undo step: " + hiddenKey);
        check(p->pickedPropInfo()->text().contains("hidden"), "the panel says it is hidden");
        const bool shown = p->togglePickedPropHidden();
        check(shown && worldKey(doc, "iw8r_hide_smodels") == "(unset)", "Show Picked Prop takes the row out again");
        emit w.stockSurfacePicked("tw/clicked_surface");
        check(p->fromEdit()->text() == "tw/clicked_surface", "a stock surface clicked in the camera fills the retexture's From");
        w.setReferencePropsForTest({});
    }
    for (int i = 0; i < 200 && doc.undoPosition() > pos0; ++i) doc.undo();
    ctx.documentEdited();
    check(doc.text() == stockText, "Undo takes every edit of the panel out again");

    // ---- Another map: the icon and the panel go; the stock map again: the icon is back.
    if (closeAndReopen) {
        w.resetForNewDocument();  // what New Map and opening another map do to the stock state
        ctx.documentEdited();
        pump();
        ads::CDockManager* dm = w.findChild<ads::CDockManager*>();
        ads::CDockWidget* d = dm ? dm->findDockWidget("Stock Map") : nullptr;
        check(side && !side->isVisible() && d && d->isClosed(), "without a stock map the icon is hidden and the panel closed");
        const QVariant referenceBefore = Settings::instance().value("stock/referenceOnOpen", true);
        Settings::instance().setValue("stock/referenceOnOpen", false);
        QString err;
        const bool ok = w.openStockZone(zonePath, &err);
        Settings::instance().setValue("stock/referenceOnOpen", referenceBefore);
        pump();
        check(ok && side && side->isVisible(), "the stock map opened again: the icon is back " + err.left(200));
    }
}

}  // namespace iw8r::qt
