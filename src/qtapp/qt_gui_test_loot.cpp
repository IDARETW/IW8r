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
          QString("File > Edit Loot Points adds %1 loot point entities").arg(stockPoints));
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
    check(made == 4 && hasPlacedObjects(ctx.doc.map()), QString("File > Edit Placed Objects adds the %1 objects without physics entries").arg(made));
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
        check(hidden == 3 && list == "3 7 40", QString("File > Hide Stock Props In Selection hides the 3 props inside the brush (rows %1)").arg(QString::fromStdString(list)));
        check(w.hideStockPropsInSelection() == 0, "a second call finds nothing new");
        QString program, err;
        QStringList args, notes;
        const bool prepared = w.prepareStockEdit(work + "/hidden", &program, &args, &err, &notes);
        QFile jf(work + "/hidden/edits.json"), ef(work + "/hidden/edit.ents");
        const QByteArray json = jf.open(QIODevice::ReadOnly) ? jf.readAll() : QByteArray();
        const QByteArray ents = ef.open(QIODevice::ReadOnly) ? ef.readAll() : QByteArray();
        check(prepared && json.contains("\"hide_static_models\":[3,7,40]"), "the edits file lists the hidden rows: " + err);
        check(!ents.isEmpty() && !ents.contains("caulk") && !ents.contains("func_group"), "the helper brush left in the document is not written into the entity text");
        check(notes.join("; ").contains("1 brush(es)"), "the rebuild notes say the brush is left out");
        check(w.showAllStockProps() == 3, "File > Show All Stock Props brings them back");
        bool gone = true;
        for (const Entity& e : ctx.doc.map().entities)
            if (e.isWorldspawn() && e.value("iw8r_hide_smodels")) gone = false;
        check(gone, "the hidden list is cleared");
        w.setReferencePropsForTest({});
        for (int i = 0; i < 12 && ctx.doc.modified(); ++i) ctx.doc.undo();
        check(ctx.doc.text() == stockText, "Undo returns the document to the stock text");
    }
}

}  // namespace iw8r::qt
