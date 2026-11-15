#include "qt_gui_test_loot.h"

#include <string>
#include <vector>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "main_window.h"
#include "map_text.h"
#include "settings.h"
#include "stock_loot.h"
#include "brush_ops.h"
#include "stock_placed.h"
#include "stock_smodels.h"

namespace iw8r::qt {

void runLootPointChecks(std::function<void(const QString&)> section, std::function<void(bool, const QString&)> check,
                        std::function<void(const QString&)> skip, MainWindow& w, const QString& outDir) {
    section("loot points of a Warzone stock map (13.5: mp_br_tut through ACTS)");
    const QString zone = "E:/IW8/Builds/1.20-replay/zone/mp_br_tut.ff";
    if (!QFile::exists(zone)) {
        skip("no " + zone);
        return;
    }
    EditorContext& ctx = w.context();
    // Only the entities and the loot block are needed here: the world reference of the map stays unloaded.
    const QVariant referenceBefore = Settings::instance().value("stock/referenceOnOpen", true);
    Settings::instance().setValue("stock/referenceOnOpen", false);
    QString zerr;
    const bool ok = w.openStockZone(zone, &zerr);
    Settings::instance().setValue("stock/referenceOnOpen", referenceBefore);
    check(ok, "ACTS read mp_br_tut and the editor opened it" + (zerr.isEmpty() ? QString() : " (" + zerr.left(200) + ")"));
    if (!ok) return;
    const size_t stockPoints = w.stockLootPointCount();
    check(stockPoints == 5, QString("the map's loot block is read: %1 points").arg(stockPoints));
    const std::string stockText = ctx.doc.text();
    const size_t entCount = ctx.doc.map().entities.size();

    // Before Edit Loot Points the rebuild leaves the map's loot alone.
    const QString work = QDir(outDir).filePath("loot_points");
    QDir(work).removeRecursively();
    {
        QString program, err;
        QStringList args, notes;
        const bool prepared = w.prepareStockEdit(work + "/untouched", &program, &args, &err, &notes);
        QFile jf(work + "/untouched/edits.json");
        const QByteArray json = jf.open(QIODevice::ReadOnly) ? jf.readAll() : QByteArray();
        check(prepared && !json.isEmpty() && !json.contains("\"loot_points\":"), "without Edit Loot Points the edits file does not touch the loot: " + err);
        check(!json.contains("\"carry\":"), "an unedited stock map carries nothing: the models its own entities name stay as the stock map has them");
    }

    check(w.editLootPoints() == int(stockPoints) && ctx.doc.map().entities.size() == entCount + stockPoints && hasLootPoints(ctx.doc.map()),
          QString("File > Stock Map > Edit Loot Points adds %1 loot point entities").arg(stockPoints));
    check(w.editLootPoints() == 0 && ctx.doc.map().entities.size() == entCount + stockPoints, "a second call adds nothing");
    std::vector<Id> loot;
    size_t cache = 0, zoned = 0;
    for (const Entity& e : ctx.doc.map().entities) {
        if (!isLootPointClass(e.classname())) continue;
        loot.push_back(e.id);
        if (const std::string* c = e.value("loot_cluster"); c && *c == "cache") ++cache;
        if (const std::string* z = e.value("loot_zone_name"); z && *z == "tutorial_scavenger") ++zoned;
    }
    check(loot.size() == stockPoints && cache == 1 && zoned == stockPoints,
          QString("the entities carry the cluster kind and the zone name (%1 cache point, %2 in tutorial_scavenger)").arg(cache).arg(zoned));
    if (loot.size() != stockPoints) return;

    // Unchanged points go back with their stock numbers.
    {
        QString program, err;
        QStringList args, notes;
        const bool prepared = w.prepareStockEdit(work + "/same", &program, &args, &err, &notes);
        QFile jf(work + "/same/edits.json"), ef(work + "/same/edit.ents");
        const QByteArray json = jf.open(QIODevice::ReadOnly) ? jf.readAll() : QByteArray();
        const QByteArray ents = ef.open(QIODevice::ReadOnly) ? ef.readAll() : QByteArray();
        check(prepared && json.contains("\"loot_points\":[{\"origin\":[") && json.count("\"cluster\":") == int(stockPoints) &&
                  !json.contains("\"auto\"") && json.contains("\"cluster\":\"cache\",\"zone\":1}"),
              QString("the edits file lists the %1 points with their stock cluster and zone: %2").arg(stockPoints).arg(err));
        check(!ents.isEmpty() && !ents.contains("iw8r_loot_point") && !ents.contains("loot_cluster"), "the entity text holds no loot point");
        check(notes.join("\n").contains(QString("%1 loot point(s) are written").arg(stockPoints)), "the rebuild notes say how many points are written");
    }

    // One point moved far out of its zone, one deleted, one new point without keys.
    ctx.doc.setKeyValue(loot[0], "origin", "90000 90000 0");
    ctx.doc.deselectAll();
    ctx.doc.select(SelRef{loot[2], kNoId}, false);
    ctx.doc.deleteSelection();
    ctx.doc.addEntity({{"classname", "iw8r_loot_point"}, {"origin", "200 100 36"}}, {}, "Loot point");
    {
        QString program, err;
        QStringList args, notes;
        const bool prepared = w.prepareStockEdit(work + "/edited", &program, &args, &err, &notes);
        QFile jf(work + "/edited/edits.json");
        const QByteArray json = jf.open(QIODevice::ReadOnly) ? jf.readAll() : QByteArray();
        check(prepared && json.count("\"cluster\":") == int(stockPoints) &&
                  json.contains("{\"origin\":[90000,90000,0],") && json.contains("{\"origin\":[200,100,36],\"angles\":[0,0,0],\"cluster\":\"auto\",\"zone\":\"auto\"}"),
              QString("moved, deleted and added points reach the edits file (%1 points): %2").arg(json.count("\"cluster\":")).arg(err));
        const int moved = json.indexOf("{\"origin\":[90000,90000,0],");
        check(moved >= 0 && json.mid(moved, 120).contains("\"zone\":\"auto\""), "the point moved out of its zone's box is picked by position again");
        check(notes.join("\n").contains("moved out of their zone's box"), "the rebuild notes name the moved point");
        check(args.contains("editzone") && args.contains("mp_br_tut"), "the converter call edits mp_br_tut");
    }
    for (int i = 0; i < 12 && ctx.doc.modified(); ++i) ctx.doc.undo();
    check(ctx.doc.text() == stockText, "Undo takes the loot point entities out again");

    // 13.5: the placed objects that can be edited (the buy station and the ammo boxes of the tutorial map).
    section("placed objects of a Warzone stock map (13.5: buy stations)");
    check(w.stockPlacedObjectCount() == 90, QString("the map's scriptable instances are read: %1").arg(w.stockPlacedObjectCount()));
    const int made = w.editPlacedObjects();
    check(made == 90 && hasPlacedObjects(ctx.doc.map()), QString("File > Stock Map > Edit Placed Objects adds the %1 movable objects (none of this map is a door)").arg(made));
    check(w.editPlacedObjects() == 0, "a second call adds nothing");
    Id kiosk = kNoId;
    std::string kioskIndex;
    for (const Entity& e : ctx.doc.map().entities)
        if (const std::string* d = e.value("scriptable"); d && *d == "br_plunder_box") {
            kiosk = e.id;
            if (const std::string* i = e.value(kPlacedObjectIndexKey)) kioskIndex = *i;
        }
    check(kiosk != kNoId && !kioskIndex.empty(), "the buy station is one of them, with its row number " + QString::fromStdString(kioskIndex));
    if (kiosk == kNoId) return;
    {
        QString program, err;
        QStringList args, notes;
        const bool prepared = w.prepareStockEdit(work + "/placed_same", &program, &args, &err, &notes);
        QFile jf(work + "/placed_same/edits.json"), ef(work + "/placed_same/edit.ents");
        const QByteArray json = jf.open(QIODevice::ReadOnly) ? jf.readAll() : QByteArray();
        const QByteArray ents = ef.open(QIODevice::ReadOnly) ? ef.readAll() : QByteArray();
        check(prepared && !json.contains("scriptable_instances"), "unchanged placed objects leave the edits file alone: " + err);
        check(!ents.isEmpty() && !ents.contains("\"scriptable\"") && !ents.contains("iw8r_stock_index"), "the entity text holds no placed object");
    }
    ctx.doc.setKeyValue(kiosk, "origin", "320 -64 36");
    ctx.doc.addEntity({{"classname", "scriptable"}, {"scriptable", "br_plunder_box"}, {"origin", "-320 64 36"}, {"angles", "0 180 0"}}, {}, "Placed object");
    {
        QString program, err;
        QStringList args, notes;
        const bool prepared = w.prepareStockEdit(work + "/placed_edited", &program, &args, &err, &notes);
        QFile jf(work + "/placed_edited/edits.json");
        const QByteArray json = jf.open(QIODevice::ReadOnly) ? jf.readAll() : QByteArray();
        const QByteArray index = QByteArray::fromStdString(kioskIndex);
        check(prepared && json.contains("\"scriptable_instances\":{\"move\":[{\"index\":" + index + ",\"origin\":[320,-64,36],") &&
                  json.contains("\"add\":[{\"like\":" + index + ",\"origin\":[-320,64,36],\"angles\":[0,180,0],\"server_instanced_parts\":1,"
                                "\"usable_parts\":1,\"physics_limit\":true,\"physics_limit_detail\":true}]}"),
              "a moved buy station and a new one reach the edits file: " + err);
        check(notes.join("\n").contains("1 placed object(s) moved, 1 added"), "the rebuild notes count them");
    }
    // Deleting the buy station's entity removes it (the converter renumbers the dynamic entities after it).
    ctx.doc.deselectAll();
    ctx.doc.select(SelRef{kiosk, kNoId}, false);
    ctx.doc.deleteSelection();
    {
        QString program, err;
        QStringList args, notes;
        const bool prepared = w.prepareStockEdit(work + "/placed_removed", &program, &args, &err, &notes);
        QFile jf(work + "/placed_removed/edits.json");
        const QByteArray json = jf.open(QIODevice::ReadOnly) ? jf.readAll() : QByteArray();
        const QByteArray index = QByteArray::fromStdString(kioskIndex);
        check(prepared && json.contains("\"remove\":[{\"index\":" + index + ",\"server_instanced_parts\":1,\"usable_parts\":1,"),
              "a deleted buy station reaches the edits file as a removal: " + err);
        check(notes.join("\n").contains("1 removed"), "the rebuild notes count the removal");
    }
    for (int i = 0; i < 12 && ctx.doc.modified(); ++i) ctx.doc.undo();
    check(ctx.doc.text() == stockText, "Undo takes the placed object entities out again");

    // 11.5: stock props inside the selected helper brush are hidden; the brush itself never reaches the entity text.
    section("hide stock props in the selection (11.5)");
    {
        std::vector<StockModelInstance> props(4);
        const uint32_t rows[4] = {40, 3, 12, 7};
        const Vec3 at[4] = {Vec3{20, -20, 5}, Vec3{0, 0, 0}, Vec3{500, 0, 0}, Vec3{10, 10, 10}};
        for (int i = 0; i < 4; ++i) {
            props[size_t(i)].model = "test_prop";
            props[size_t(i)].index = rows[i];
            props[size_t(i)].origin = at[i];
        }
        w.setReferencePropsForTest(props);
        AABB box;
        box.add(Vec3{-32, -32, -32});
        box.add(Vec3{32, 32, 32});
        const auto prim = makeBrushPrimitive(makeBox(box, " caulk 128 128 0 0 0 0"), "\n");
        check(prim.has_value(), "a helper brush is made");
        if (!prim) return;
        ctx.doc.addEntity({{"classname", "func_group"}}, {*prim}, "Helper brush");
        const int hidden = w.hideStockPropsInSelection();
        std::string list;
        for (const Entity& e : ctx.doc.map().entities)
            if (e.isWorldspawn())
                if (const std::string* v = e.value("iw8r_hide_smodels")) list = *v;
        check(hidden == 3 && list == "3 7 40", QString("File > Stock Map > Hide Stock Props In Selection hides the 3 props inside the brush (rows %1)").arg(QString::fromStdString(list)));
        check(w.hideStockPropsInSelection() == 0, "a second call finds nothing new");
        QString program, err;
        QStringList args, notes;
        const bool prepared = w.prepareStockEdit(work + "/hidden", &program, &args, &err, &notes);
        QFile jf(work + "/hidden/edits.json"), ef(work + "/hidden/edit.ents");
        const QByteArray json = jf.open(QIODevice::ReadOnly) ? jf.readAll() : QByteArray();
        const QByteArray ents = ef.open(QIODevice::ReadOnly) ? ef.readAll() : QByteArray();
        check(prepared && json.contains("\"hide_static_models\":[3,7,40]"), "the edits file lists the hidden rows: " + err);
        check(json.contains("\"remove_static_model_collision\":[{\"model\":\"test_prop\",\"origin\":[0,0,0]},{\"model\":\"test_prop\",\"origin\":[10,10,10]},"
                            "{\"model\":\"test_prop\",\"origin\":[20,-20,5]}]"),
              "and takes their collision out by model and place");
        check(!ents.isEmpty() && !ents.contains("caulk") && !ents.contains("func_group"), "the helper brush left in the document is not written into the entity text");
        check(notes.join("; ").contains("1 brush(es)"), "the rebuild notes say the brush is left out");
        check(w.showAllStockProps() == 3, "File > Stock Map > Show All Stock Props brings them back");
        bool gone = true;
        for (const Entity& e : ctx.doc.map().entities)
            if (e.isWorldspawn() && e.value("iw8r_hide_smodels")) gone = false;
        check(gone, "the hidden list is cleared");
        w.setReferencePropsForTest({});
        for (int i = 0; i < 12 && ctx.doc.modified(); ++i) ctx.doc.undo();
        check(ctx.doc.text() == stockText, "Undo returns the document to the stock text");
    }

    // 11.4: a visible brush added to the stock map is built as a model of a helper map and placed by a script_model.
    section("brushes added to a stock map (11.4)");
    {
        AABB box;
        box.add(Vec3{96, 0, 0});
        box.add(Vec3{160, 64, 48});
        const auto prim = makeBrushPrimitive(makeBox(box, " tm2co/me_curb_stone_01 128 128 0 0 0 0"), "\n");
        check(prim.has_value(), "a brush is made");
        if (!prim) return;
        ctx.doc.addEntity({{"classname", "func_group"}}, {*prim}, "Added brush");
        QString program, err;
        QStringList args, notes;
        const bool withoutPlan = w.prepareStockEdit(work + "/brush_noplan", &program, &args, &err, &notes);
        check(withoutPlan && notes.join("; ").contains("left out: only File > Stock Map > Rebuild Stock Zone builds them"),
              "without a build plan the brushes are left out with a note");
        MainWindow::StockOverlayPlan plan;
        notes.clear();
        const bool prepared = w.prepareStockEdit(work + "/brush", &program, &args, &err, &notes, &plan);
        check(prepared && QFileInfo(plan.map).isFile() && plan.models.size() == 1 && !plan.resultDir.isEmpty(),
              "Rebuild Stock Zone plans a helper map with one model: " + plan.models.join(", ") + err);
        if (plan.models.size() == 1) {
            const QString model = plan.models.front();
            check(QFileInfo(QFileInfo(plan.map).absolutePath() + "/models/" + model + ".obj").isFile(), "the model's OBJ file is written beside the helper map");
            QFile mf(plan.map), jf(work + "/brush/edits.json"), ef(work + "/brush/edit.ents");
            const QByteArray mapText = mf.open(QIODevice::ReadOnly) ? mf.readAll() : QByteArray();
            const QByteArray json = jf.open(QIODevice::ReadOnly) ? jf.readAll() : QByteArray();
            const QByteArray ents = ef.open(QIODevice::ReadOnly) ? ef.readAll() : QByteArray();
            check(mapText.contains("\"model\" \"" + model.toUtf8() + "\"") && mapText.contains("\"target\" \"" + model.toUtf8() + "\"") &&
                      mapText.contains("mp_tdm_spawn_allies_start"),
                  "the helper map places the model on a targeted script_model, with the spawns a build needs");
            check(json.contains("{\"zone\":\"mp_iw8rovl\",\"type\":\"xmodel\",\"name\":\"" + model.toUtf8() + "\",\"folder\":\"@IW8R_OVERLAY_PACKAGE@\"}"),
                  "the edits file carries the model from the helper map's package (filled in after its build)");
            check(ents.contains("212 \"script_model\"\n652 \"" + model.toUtf8() + "\"\n709 \"128 32 24\""),
                  "the entity text places it where the brush was (its centre)");
            check(notes.join("; ").contains("12 triangles"), "the notes say what was built");
        }
        for (int i = 0; i < 12 && ctx.doc.modified(); ++i) ctx.doc.undo();
        check(ctx.doc.text() == stockText, "Undo takes the added brush out again");
    }

    // 11.6: worldspawn iw8r_sun_color / iw8r_sun_intensity reach the edits file as "sun".
    section("stock sun colour and intensity (11.6)");
    {
        Id world = kNoId;
        for (const Entity& e : ctx.doc.map().entities)
            if (e.isWorldspawn()) world = e.id;
        ctx.doc.beginGroup("Relight Sun");
        ctx.doc.setKeyValue(world, "iw8r_sun_color", "1 0.55 0.35");
        ctx.doc.setKeyValue(world, "iw8r_sun_intensity", "7.5");
        ctx.doc.setKeyValue(world, "iw8r_sky_from", "mp_backlot2");
        ctx.doc.endGroup();
        QString program, err;
        QStringList args, notes;
        const bool prepared = w.prepareStockEdit(work + "/sun", &program, &args, &err, &notes);
        QFile jf(work + "/sun/edits.json"), ef(work + "/sun/edit.ents");
        const QByteArray json = jf.open(QIODevice::ReadOnly) ? jf.readAll() : QByteArray();
        const QByteArray ents = ef.open(QIODevice::ReadOnly) ? ef.readAll() : QByteArray();
        check(prepared && json.contains("\"sun\":{\"color\":[1,0.55,0.35],\"intensity\":7.5}"), "the sun colour and intensity reach the edits file: " + err);
        check(notes.join("; ").contains("the stock sun gets a new colour or intensity") && w.action("RelightStockSun"), "the rebuild notes say so, and the command exists");
        check(json.contains("{\"zone\":\"techsets_mp_backlot2\",\"type\":\"material\",\"name\":\"tw/sky_mp_backlot2\"}") &&
                  json.contains("\"replace_materials\":[{\"from\":\"@sky\",\"to\":\"tw/sky_mp_backlot2\"}]") && w.action("RelightStockSky"),
              "another map's sky reaches the edits file (carry and replace_materials)");
        for (int i = 0; i < 4 && ctx.doc.modified(); ++i) ctx.doc.undo();
        check(ctx.doc.text() == stockText, "Undo takes the sun keys out again");
    }

    // New loot clusters and zones (research 89 section 9): box brush entities that go to the edits file as loot_boxes,
    // never into the entity text and never built as models.
    section("new loot clusters and zones on a stock map (13.5)");
    {
        AABB cbox, zbox;
        cbox.add(Vec3{-64, -64, 0});
        cbox.add(Vec3{64, 64, 64});
        zbox.add(Vec3{-512, -512, -64});
        zbox.add(Vec3{512, 512, 256});
        const auto cprim = makeBrushPrimitive(makeBox(cbox, " caulk 128 128 0 0 0 0"), "\n");
        const auto zprim = makeBrushPrimitive(makeBox(zbox, " caulk 128 128 0 0 0 0"), "\n");
        check(cprim.has_value() && zprim.has_value(), "two box brushes are made");
        if (!cprim || !zprim) return;
        ctx.doc.addEntity({{"classname", "iw8r_loot_zone"}, {"name", "tutorial_scavenger"}}, {*zprim}, "Loot zone");
        ctx.doc.addEntity({{"classname", "iw8r_loot_cluster"}}, {*cprim}, "Loot cluster");
        QString program, err;
        QStringList args, notes;
        MainWindow::StockOverlayPlan plan;
        const bool prepared = w.prepareStockEdit(work + "/boxes", &program, &args, &err, &notes, &plan);
        QFile jf(work + "/boxes/edits.json"), ef(work + "/boxes/edit.ents");
        const QByteArray json = jf.open(QIODevice::ReadOnly) ? jf.readAll() : QByteArray();
        const QByteArray ents = ef.open(QIODevice::ReadOnly) ? ef.readAll() : QByteArray();
        check(prepared && json.contains("\"loot_boxes\":{\"clusters\":[{\"mid\":[0,0,32],\"half\":[64,64,32]}],"
                                        "\"zones\":[{\"name\":\"tutorial_scavenger\",\"mid\":[0,0,96],\"half\":[512,512,160]}]}"),
              "the new cluster and zone reach the edits file as loot_boxes: " + err);
        check(!ents.contains("iw8r_loot") && plan.models.isEmpty(), "they are not in the entity text and not built as models");
        check(notes.join("; ").contains("1 new loot cluster(s) and 1 new loot zone(s) are added"), "the rebuild notes count them");
        for (int i = 0; i < 12 && ctx.doc.modified(); ++i) ctx.doc.undo();
        check(ctx.doc.text() == stockText, "Undo takes the loot boxes out again");
    }

    // Stock world removal (11.5, research 96): an iw8r_remove_world box goes to the edits file as
    // remove_world_surfaces, never into the entity text and never built as a model.
    section("stock world removal boxes (11.5)");
    {
        AABB rbox;
        rbox.add(Vec3{-32, -32, 0});
        rbox.add(Vec3{32, 32, 64});
        const auto rprim = makeBrushPrimitive(makeBox(rbox, " caulk 128 128 0 0 0 0"), "\n");
        check(rprim.has_value(), "a box brush is made");
        if (!rprim) return;
        ctx.doc.addEntity({{"classname", "iw8r_remove_world"}}, {*rprim}, "Remove world");
        QString program, err;
        QStringList args, notes;
        MainWindow::StockOverlayPlan plan;
        const bool prepared = w.prepareStockEdit(work + "/remove_world", &program, &args, &err, &notes, &plan);
        QFile jf(work + "/remove_world/edits.json"), ef(work + "/remove_world/edit.ents");
        const QByteArray json = jf.open(QIODevice::ReadOnly) ? jf.readAll() : QByteArray();
        const QByteArray ents = ef.open(QIODevice::ReadOnly) ? ef.readAll() : QByteArray();
        check(prepared && json.contains("\"remove_world_surfaces\"") && json.contains("[-32,-32,0]") && json.contains("[32,32,64]"),
              "the box reaches the edits file as remove_world_surfaces: " + err + " " + QString::fromUtf8(json.mid(json.indexOf("remove_world"), 200)));
        check(!ents.contains("iw8r_remove_world") && plan.models.isEmpty(), "it is not in the entity text and not built as a model");
        check(notes.join("; ").contains("1 world removal box(es)"), "the rebuild notes count it");
        for (int i = 0; i < 12 && ctx.doc.modified(); ++i) ctx.doc.undo();
        check(ctx.doc.text() == stockText, "Undo takes the removal box out again");
    }
}

}  // namespace iw8r::qt
