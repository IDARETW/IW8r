// MainWindow: rebuilding the open stock map through the converter's editzone command (roadmap 11.2 / 11.4 / 11.5,
// docs/research/88_stock_map_editing.md). Kept out of main_window.cpp, which is at the compiler's section limit.
#include "main_window.h"
#include "compile_panel.h"
#include "camera_view.h"
#include "render.h"
#include "custom_shader.h"
#include <map>
#include <QRegularExpression>
#include <set>
#include <QFileDialog>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <thread>

#include <QCoreApplication>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QStandardPaths>

#include "editor_log.h"
#include "worker_guard.h"
#include <QPointer>
#include "compile_iw8.h"
#include "spawnlist.h"
#include "scriptable_catalog.h"
#include "stock_loot.h"
#include "stock_placed.h"
#include "stock_world.h"
#include "world_cache.h"
#include "settings.h"
#include "stock_install.h"
#include "stock_spawns.h"
#include "stock_zone_edit.h"
#include "brush_ops.h"
#include "model_cache.h"
#include "material_blend.h"
#include "material_cache.h"
#include "collision_world.h"

namespace iw8r::qt {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
// The stock zone the material catalogue lists for a material ("" unknown): the retexture's carry check.
std::string catalogueZoneOf(const std::string& material) {
    static const std::map<std::string, std::string> catalogue = [] {
        std::map<std::string, std::string> c;
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 4 && !QFileInfo::exists(d.filePath("data/asset_catalog/iw8_materials.tsv")); ++up) d.cdUp();
        QFile f(d.filePath("data/asset_catalog/iw8_materials.tsv"));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            while (!in.atEnd()) {
                const QString line = in.readLine();
                const int tab = line.indexOf('\t');
                if (tab <= 0 || line.startsWith('#')) continue;
                c.emplace(line.left(tab).toStdString(), line.mid(tab + 1).section(',', 0, 0).trimmed().toStdString());
            }
        }
        return c;
    }();
    const auto it = catalogue.find(material);
    return it == catalogue.end() ? std::string() : it->second;
}
// A question whose button says what it does ("Install", "Restore") next to Cancel, the default: these write to the
// game folder, so Enter alone must not start them.
bool confirmGameFolderAction(QWidget* parent, const QString& title, const QString& text, const QString& action) {
    QMessageBox box(QMessageBox::Question, title, text, QMessageBox::Cancel, parent);
    box.setObjectName("stockEditConfirm");
    QPushButton* go = box.addButton(action, QMessageBox::AcceptRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.setEscapeButton(QMessageBox::Cancel);
    box.exec();
    return box.clickedButton() == go;
}
}  // namespace

// Writes <work>/edit.ents (the entity text, with the spawn entities when the compiled spawns were made editable) and
// <work>/edits.json, and gives the converter call. The Replay install is only read by that call.
namespace {

// The placeholder for the helper map's package folder in edits.json: known only after its build.
const char* const kOverlayFolder = "@IW8R_OVERLAY_PACKAGE@";
const char* const kOverlayMapId = "mp_iw8rovl";

// A closed room with a floor, two spawns and one script_model per added model: the smallest map the build accepts,
// built only for the models it places.
std::string overlayMapText(const std::vector<std::string>& models) {
    std::string t = "iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n\"iw8r_title\" \"stock map brushes\"\n";
    const auto box = [&](int x0, int y0, int z0, int x1, int y1, int z1) {
        const auto v = [](int a, int b, int c) { return "( " + std::to_string(a) + " " + std::to_string(b) + " " + std::to_string(c) + " )"; };
        const std::string m = " iw8r_overlay_room 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n";
        t += "{\n";
        t += " " + v(x1, y1, z0) + " " + v(x0, y1, z0) + " " + v(x0, y0, z0) + m;
        t += " " + v(x0, y0, z1) + " " + v(x0, y1, z1) + " " + v(x1, y1, z1) + m;
        t += " " + v(x0, y0, z1) + " " + v(x1, y0, z1) + " " + v(x1, y0, z0) + m;
        t += " " + v(x1, y1, z1) + " " + v(x0, y1, z1) + " " + v(x0, y1, z0) + m;
        t += " " + v(x1, y0, z1) + " " + v(x1, y1, z1) + " " + v(x1, y1, z0) + m;
        t += " " + v(x0, y1, z1) + " " + v(x0, y0, z1) + " " + v(x0, y0, z0) + m;
        t += "}\n";
    };
    box(-256, -256, -16, 256, 256, 0);
    box(-256, -256, 256, 256, 256, 272);
    box(-272, -256, 0, -256, 256, 256);
    box(256, -256, 0, 272, 256, 256);
    box(-256, -272, 0, 256, -256, 256);
    box(-256, 256, 0, 256, 272, 256);
    t += "}\n";
    // The spawn set every build requires (team starts, a free-for-all start, the intermission point).
    for (const auto& [cls, x] : {std::pair<const char*, int>{"mp_tdm_spawn", -128}, {"mp_tdm_spawn_allies_start", -160},
                                 {"mp_tdm_spawn_axis_start", 160}, {"mp_dm_spawn_start", 128}, {"mp_global_intermission", 0}})
        t += std::string("{\n\"classname\" \"") + cls + "\"\n\"origin\" \"" + std::to_string(x) + " 0 0\"\n}\n";
    // A script_model that another entity targets stays a live model of its own name with its physics (so it
    // collides where it is placed); an untargeted one would be built into the helper's world as a static prop.
    for (const std::string& m : models) {
        t += "{\n\"classname\" \"script_model\"\n\"model\" \"" + m + "\"\n\"targetname\" \"" + m + "\"\n\"origin\" \"0 0 64\"\n}\n";
        t += "{\n\"classname\" \"script_origin\"\n\"target\" \"" + m + "\"\n\"origin\" \"0 0 32\"\n}\n";
    }
    return t;
}

}  // namespace

bool MainWindow::prepareStockEdit(const QString& work, QString* program, QStringList* args, QString* err, QStringList* notes,
                                  StockOverlayPlan* overlay) {
    if (stockZone_.isEmpty()) {
        if (err) *err = "Open a stock map first (File > Open IW8 Stock Map).";
        return false;
    }
    const QString zonetool = toolPath("zonetool"), replay = toolPath("replay");
    if (zonetool.isEmpty() || !QFileInfo::exists(zonetool) || replay.isEmpty() || !QFileInfo::exists(replay)) {
        if (err) *err = "The converter (zonetool) and the game exe must be set in Tools > Tool Paths.";
        return false;
    }
    std::string ents, serr;
    std::vector<SpawnRecord> spawns;
    splitStockDocument(ctx_.doc.map(), ents, spawns, &serr);
    if (!serr.empty() && notes) *notes << qs(serr).trimmed();
    // The stock text ends with its terminator; the converter adds it again.
    while (!ents.empty() && ents.back() == char(0)) ents.pop_back();
    if (stockSpawnsEditable_) {
        if (spawns.empty()) {
            if (err) *err = "Every spawn was deleted: the spawn list cannot be written empty (docs/research/26).";
            return false;
        }
        // The spawn entities ride in the entity text; the converter compiles them into the spawn list and takes
        // them out of the text, as a stock map has them.
        if (!ents.empty() && ents.back() != '\n') ents += "\n";
        ents += spawnsToEntityText(spawns);
    }
    // 11.4: brushes added to the stock map become models of a helper map (built first), carried into the zone and
    // placed by script_model entities at their place.
    std::vector<StockEditCarry> overlayCarry;
    {
        std::vector<std::string> overlayNotes;
        const std::vector<StockOverlayModel> models = stockOverlayModels(ctx_.doc.map(), 4, &overlayNotes);
        if (notes)
            for (const std::string& n : overlayNotes) *notes << qs(n);
        if (!models.empty() && !overlay) {
            if (notes) *notes << QString("%1 group(s) of added brushes are left out: only File > Stock Map > Rebuild Stock Zone builds them").arg(models.size());
        } else if (!models.empty()) {
            const QString dir = work + "/overlay";
            QDir().mkpath(dir + "/models");
            std::vector<std::string> names;
            std::string placed;
            for (const StockOverlayModel& m : models) {
                const QByteArray hash = QCryptographicHash::hash(QByteArray::fromStdString(stockZone_.toStdString() + "|" + m.obj),
                                                                 QCryptographicHash::Sha1).toHex().left(12);
                const std::string name = "iw8r_ovl_" + hash.toStdString();
                QFile of(dir + "/models/" + qs(name) + ".obj");
                if (!of.open(QIODevice::WriteOnly) || of.write(m.obj.data(), qint64(m.obj.size())) != qint64(m.obj.size())) {
                    if (err) *err = "Cannot write " + of.fileName();
                    return false;
                }
                names.push_back(name);
                overlayCarry.push_back({kOverlayMapId, "xmodel", name, kOverlayFolder});
                // The game's own form of the entity text (numeric keys: classname 212, model 652, origin 709).
                placed += "{\n212 \"script_model\"\n652 \"" + name + "\"\n709 \"" + formatNumber(m.centre.x) + " " + formatNumber(m.centre.y) + " " +
                          formatNumber(m.centre.z) + "\"\n}\n";
                if (notes)
                    *notes << QString("added brushes (entity %1): %2 triangles, %3 material(s), built as model %4")
                                  .arg(m.entity).arg(m.triangles).arg(m.materials.size()).arg(qs(name));
            }
            const std::string mapText = overlayMapText(names);
            QFile mf(dir + "/" + kOverlayMapId + ".map");
            if (!mf.open(QIODevice::WriteOnly) || mf.write(mapText.data(), qint64(mapText.size())) != qint64(mapText.size())) {
                if (err) *err = "Cannot write " + mf.fileName();
                return false;
            }
            mf.close();
            if (!ents.empty() && ents.back() != '\n') ents += "\n";
            ents += placed;
            overlay->map = mf.fileName();
            overlay->resultDir = dir + "/result";
            for (const std::string& n : names) overlay->models << qs(n);
        }
    }
    QDir().mkpath(work);
    const QString entsPath = work + "/edit.ents";
    QFile ef(entsPath);
    if (!ef.open(QIODevice::WriteOnly) || ef.write(ents.data(), qint64(ents.size())) != qint64(ents.size())) {
        if (err) *err = "Cannot write " + entsPath;
        return false;
    }
    ef.close();

    // Models the document places that the zone does not hold are carried from another stock zone.
    StockModelZones catalogue;
    {
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 4; ++up, d.cdUp())
            if (QFileInfo::exists(d.filePath("data/asset_catalog/iw8_models.tsv"))) {
                catalogue.load(d.filePath("data/asset_catalog/iw8_models.tsv").toStdString());
                break;
            }
    }
    std::vector<std::string> carryNotes;
    std::vector<StockEditCarry> carry =
        catalogue.size() ? stockEditCarryList(ctx_.doc.map(), stockZone_.toStdString(), catalogue, &carryNotes, &stockModelNames_)
                         : std::vector<StockEditCarry>{};
    // The added brushes' models are not in the catalogue: they come from the helper map.
    carry.insert(carry.end(), overlayCarry.begin(), overlayCarry.end());
    if (notes) {
        for (const std::string& n : carryNotes) *notes << qs(n);
        for (const StockEditCarry& c : carry) *notes << QString("model %1 is carried from %2").arg(qs(c.name), qs(c.zone));
        if (!catalogue.size()) *notes << "data/asset_catalog/iw8_models.tsv was not found: models added from other maps are not carried";
    }
    const std::vector<uint32_t> hidden = stockEditHiddenProps(ctx_.doc.map());
    // 11.5: their collision goes too, found by model and place in the collision map (the props of the stock
    // reference say which model stands where).
    std::vector<StockPropCollision> removeCollision;
    if (!hidden.empty() && refProps_ && refPropZone_ == stockZone_) {
        std::map<uint32_t, const StockModelInstance*> byIndex;
        for (const StockModelInstance& in : *refProps_) byIndex[in.index] = &in;
        for (const uint32_t i : hidden)
            if (const auto it = byIndex.find(i); it != byIndex.end())
                removeCollision.push_back({it->second->model, {it->second->origin.x, it->second->origin.y, it->second->origin.z}});
    }
    if (notes && !hidden.empty())
        *notes << QString("%1 stock prop(s) are hidden (scale 0)%2").arg(hidden.size()).arg(
            removeCollision.size() == hidden.size()
                ? QString(" and their collision is taken out (a big map keeps it in its collision tiles)")
                : QString("; their collision stays (load the map's props in the reference view first to take it out)"));

    // 13.5: the loot points, when the document holds some (File > Stock Map > Edit Loot Points): the whole list replaces the map's.
    // No loot point entity (never made, or taken out again by Undo) leaves the map's loot alone; only the worldspawn
    // key iw8r_no_loot 1 writes an empty list.
    std::string loot;
    bool noLoot = false;
    for (const Entity& e : ctx_.doc.map().entities)
        if (e.isWorldspawn()) {
            const std::string* v = e.value("iw8r_no_loot");
            noLoot = v && *v == "1";
            break;
        }
    const bool writeLoot = hasLootPoints(ctx_.doc.map()) || noLoot;
    if (!writeLoot && stockLootEditable_ && !stockLoot_.points.empty() && notes)
        *notes << "the document holds no loot point: the map keeps its own loot points (worldspawn iw8r_no_loot 1 removes them all)";
    if (writeLoot) {
        std::vector<std::string> lootNotes;
        const std::vector<LootPointEdit> points = documentLootPoints(ctx_.doc.map(), stockLoot_, &lootNotes);
        if (points.size() > kLootPointLimit) {
            if (err) *err = QString("%1 loot points: a map takes at most %2.").arg(points.size()).arg(kLootPointLimit);
            return false;
        }
        loot = lootPointsJson(points);
        if (notes) {
            *notes << QString("%1 loot point(s) are written (the map had %2)").arg(points.size()).arg(stockLoot_.points.size());
            for (const std::string& n : lootNotes) *notes << qs(n);
        }
    }

    // New loot clusters and zones (iw8r_loot_cluster / iw8r_loot_zone brush entities, research 89 section 9).
    std::string lootBoxes;
    {
        std::vector<std::string> boxNotes;
        const LootBoxEdits boxes = documentLootBoxes(ctx_.doc.map(), stockLoot_.zoneNames.empty() || stockLoot_.zoneNames[0].empty()
                                                                         ? std::string("map_quarry") : stockLoot_.zoneNames[0], &boxNotes);
        if (!boxes.empty()) {
            lootBoxes = lootBoxesJson(boxes);
            if (notes) *notes << QString("%1 new loot cluster(s) and %2 new loot zone(s) are added").arg(boxes.clusters.size()).arg(boxes.zones.size());
        }
        if (notes)
            for (const std::string& n : boxNotes) *notes << qs(n);
    }

    // 11.6: another stock map's sky (worldspawn iw8r_sky_from).
    std::string replaceMaterials;
    {
        std::vector<std::string> skyNotes;
        StockEditCarry skyCarry;
        if (stockSkyEdit(ctx_.doc.map(), stockZone_.toStdString(), skyCarry, replaceMaterials, &skyNotes)) carry.push_back(skyCarry);
        // Retexturing (worldspawn iw8r_stock_retexture): the catalogue says which stock zone holds each new material.
        std::vector<std::string> entries;
        stockRetextureEdits(ctx_.doc.map(), catalogueZoneOf, carry, entries, &skyNotes);
        if (!entries.empty()) {
            std::string list = replaceMaterials.empty() ? std::string("[") : replaceMaterials.substr(0, replaceMaterials.size() - 1) + ",";
            for (size_t i = 0; i < entries.size(); ++i) list += (i ? "," : "") + entries[i];
            replaceMaterials = list + "]";
        }
        if (notes)
            for (const std::string& n : skyNotes) *notes << qs(n);
    }

    // 11.6: the stock sun's colour, intensity and direction (worldspawn iw8r_sun_color / iw8r_sun_intensity /
    // iw8r_sun_direction). A new direction is baked into the zone's sun shadow again; the converter runs ACTS for the
    // static models' streamed geometry.
    std::vector<std::string> sunNotes;
    const QString sunActs = toolPath("acts");
    const std::string sun = stockSunJson(ctx_.doc.map(), &sunNotes, QDir::toNativeSeparators(sunActs).toStdString());
    if (notes) {
        for (const std::string& n : sunNotes) *notes << qs(n);
        if (sun.find("\"color\"") != std::string::npos || sun.find("\"intensity\"") != std::string::npos)
            *notes << "the stock sun gets a new colour or intensity";
        if (sun.find("\"direction\"") != std::string::npos) {
            *notes << "the stock sun gets a new direction: the rebuild bakes the map's sun shadow again (the static models "
                      "through ACTS) and turns the vision's sun fog with it; the baked lightmaps and light grid keep the old "
                      "sun's bounce light";
            if (sunActs.isEmpty() || !QFileInfo::exists(sunActs))
                *notes << "ACTS is not set, so the converter looks for IW8_ACTS; without it the rebuild stops at the sun shadow";
        }
    }

    // 11.5: world geometry removed inside iw8r_remove_world boxes (drawing and collision; the sun shadow re-baked
    // unless worldspawn iw8r_remove_world_sun is keep). Research 96.
    std::vector<std::string> rmNotes;
    const auto rmBoxes = documentWorldRemovals(ctx_.doc.map(), &rmNotes);
    const bool rmRebake = stockWorldRemovalRebakesSun(ctx_.doc.map());
    const std::string removeWorld = stockWorldRemovalJson(rmBoxes, rmRebake, QDir::toNativeSeparators(sunActs).toStdString());
    if (notes) {
        for (const std::string& n : rmNotes) *notes << qs(n);
        if (!rmBoxes.empty()) {
            *notes << QString("%1 world removal box(es): the stock world inside them stops drawing and colliding%2; the baked "
                              "lighting and the occlusion data keep it (things behind it may still be culled from some views)")
                          .arg(rmBoxes.size())
                          .arg(rmRebake ? ", and the sun shadow is baked again without it" : "");
            if (rmRebake && (sunActs.isEmpty() || !QFileInfo::exists(sunActs)))
                *notes << "ACTS is not set, so the converter looks for IW8_ACTS; without it the rebuild stops at the sun shadow";
        }
    }

    // 13.5: placed objects (buy stations) that were moved or added as `scriptable` entities.
    const PlacedObjectEdits placed =
        placedObjectEdits(ctx_.doc.map(), stockPlaced_, ScriptableCatalog::shared(QCoreApplication::applicationDirPath().toStdString()));
    if (notes)
        for (const std::string& n : placed.notes) *notes << qs(n);

    // Edits that are installed in the game replaced the game's own patch files, which carry real map data: the
    // converter reads those zones with the stock patch saved at install, so this rebuild starts from the stock map.
    const std::string backupRoot = (QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/stock_backup").toStdString();
    const std::vector<StockPatchOverride> stockPatches = installedStockPatchOverrides(backupRoot);
    // An edit installed as a whole zone took the place of the stock zone file, which waits in stockKeepDir().
    const std::vector<StockPatchOverride> stockZones = installedStockZoneOverrides(backupRoot, stockKeepDir().toStdString());
    if (notes && (!stockPatches.empty() || !stockZones.empty())) {
        QStringList names;
        for (const StockPatchOverride& o : stockZones) names << qs(o.name);
        for (const StockPatchOverride& o : stockPatches) names << qs(o.name);
        *notes << QString("installed edit(s) in the game: %1. This rebuild starts from the stock map (the stock files kept at install)")
                      .arg(names.join(", "));
    }

    // The map shader's filter (worldspawn iw8r_surface_shader, a file with iw8rFilter): compiled here and spliced by the
    // converter into every lit pixel shader of the map's techset zone (editzone "lit_filter"), so the world and the
    // static props keep the game's lighting and get the filter's look.
    std::string litFilter;
    if (const auto shader = mapSurfaceShader(); shader && customShaderHasFilter(shader->code)) {
        std::vector<uint8_t> program;
        std::string cerr;
        const QString programPath = work + "/lit_filter.cso";
        QFile pf(programPath);
        if (!iw8r::app::SceneRenderer::compileProgram(customShaderFilterProgramSource(*shader), kIw8rFilterEntry, program, &cerr)) {
            if (err) *err = "The shader's filter does not compile: " + qs(cerr);
            return false;
        }
        if (!pf.open(QIODevice::WriteOnly) || pf.write(reinterpret_cast<const char*>(program.data()), qint64(program.size())) != qint64(program.size())) {
            if (err) *err = "Cannot write " + programPath;
            return false;
        }
        pf.close();
        // The filter with the screen (iw8rFilterScreen: the toon shader's ink lines from the scene depth), when the
        // shader has one.
        QString screenPath;
        if (customShaderHasScreenFilter(shader->code)) {
            std::vector<uint8_t> screen;
            screenPath = work + "/lit_filter_screen.cso";
            QFile sf(screenPath);
            if (!iw8r::app::SceneRenderer::compileProgram(customShaderScreenFilterProgramSource(*shader), kIw8rFilterEntry, screen, &cerr)) {
                if (err) *err = "The shader's screen filter (iw8rFilterScreen) does not compile: " + qs(cerr);
                return false;
            }
            if (!sf.open(QIODevice::WriteOnly) || sf.write(reinterpret_cast<const char*>(screen.data()), qint64(screen.size())) != qint64(screen.size())) {
                if (err) *err = "Cannot write " + screenPath;
                return false;
            }
        }
        litFilter = stockLitFilterJson(QDir::toNativeSeparators(programPath).toStdString(), QDir::toNativeSeparators(screenPath).toStdString());
        if (notes)
            *notes << "the map shader " + qs(shader->name) + " filters the map's lit surfaces (world and static props; characters, weapons and "
                      "effects use shared shaders and keep their look)" +
                          (screenPath.isEmpty() ? QString() : QString(", with the screen where a shader can take it (lines from the scene depth)")) +
                          "; needs a converter with editzone \"lit_filter\"";
    } else if (const auto any = mapSurfaceShader(); any && notes) {
        *notes << "the map shader " + qs(any->name) + " has no iw8rFilter function: a stock map's surfaces keep the game's lighting, "
                  "so only a filter can change their look";
    }

    const QString editsPath = work + "/edits.json";
    QFile jf(editsPath);
    const std::string json = stockEditJson(QDir::toNativeSeparators(entsPath).toStdString(), hidden, carry, writeLoot ? &loot : nullptr,
                                           &placed.json, &stockPatches, &removeCollision, &lootBoxes, &sun, &replaceMaterials,
                                           &removeWorld, &litFilter, &stockZones, false);
    if (!jf.open(QIODevice::WriteOnly) || jf.write(json.data(), qint64(json.size())) != qint64(json.size())) {
        if (err) *err = "Cannot write " + editsPath;
        return false;
    }
    jf.close();
    if (program) *program = zonetool;
    if (args)
        *args = {"-game", "iw8", "-path", QDir::toNativeSeparators(QFileInfo(replay).absolutePath()), "editzone", stockZone_,
                 QDir::toNativeSeparators(editsPath), QDir::toNativeSeparators(work + "/out")};
    return true;
}

// File > Stock Map > Rebuild Stock Zone: runs the converter in the background, then the ACTS loader test on what it wrote. The
// result stays in the work folder; installing it over the stock file is the owner's step.
void MainWindow::rebuildStockZoneFast() {
    const QString work = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/stock_rebuild/" + stockZone_ + "-" +
                         QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    QString program, err;
    QStringList args, notes;
    StockOverlayPlan overlay;
    if (!prepareStockEdit(work, &program, &args, &err, &notes, &overlay)) {
        QMessageBox::warning(this, "Rebuild Stock Zone", err);
        return;
    }
    for (const QString& n : notes) EditorLog::instance().add(LogLevel::Info, "stock", n);
    if (!overlay.map.isEmpty()) {
        // 11.4: the helper map with the added brushes is built first with the editor's own headless build (stock
        // textures, models, collision), then the stock rebuild carries its models from that package.
        auto* build = new QProcess(this);
        build->setProgram(QCoreApplication::applicationFilePath());
        build->setArguments({"--compile", overlay.map, "--out", overlay.resultDir});
        build->setProcessChannelMode(QProcess::MergedChannels);
        connect(build, &QProcess::finished, this, [this, build, overlay, work, program, args](int code) {
            build->deleteLater();
            QFile rf(overlay.resultDir + "/compile-result.txt");
            const QString result = rf.open(QIODevice::ReadOnly) ? QString::fromUtf8(rf.readAll()).trimmed() : QString();
            const int at = result.indexOf("package=");
            if (code != 0 || !result.startsWith("ok") || at < 0) {
                ctx_.setStatus("Rebuild Stock Zone: the build of the added brushes failed; see " + overlay.resultDir + "/compile-log.txt");
                return;
            }
            const QString package = QDir::fromNativeSeparators(result.mid(at + 8).trimmed());
            QFile jf(work + "/edits.json");
            QByteArray json = jf.open(QIODevice::ReadOnly) ? jf.readAll() : QByteArray();
            jf.close();
            json.replace(kOverlayFolder, package.toUtf8());
            if (!jf.open(QIODevice::WriteOnly | QIODevice::Truncate) || jf.write(json) != json.size()) {
                ctx_.setStatus("Rebuild Stock Zone: cannot write " + jf.fileName());
                return;
            }
            jf.close();
            EditorLog::instance().add(LogLevel::Info, "stock", QString("the added brushes are built (%1 model(s)): %2").arg(overlay.models.size()).arg(package));
            runStockEdit(work, program, args);
        });
        build->start();
        ctx_.setStatus(QString("Rebuilding %1: building the added brushes first (%2 model(s), a minute or two)").arg(stockZone_).arg(overlay.models.size()));
        return;
    }
    runStockEdit(work, program, args);
}

void MainWindow::runStockEdit(const QString& work, const QString& program, const QStringList& args) {
    const QString zone = stockZone_;
    auto* proc = new QProcess(this);
    proc->setProgram(program);
    proc->setArguments(args);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, &QProcess::readyRead, this, [proc] {
        for (const QString& line : QString::fromLocal8Bit(proc->readAll()).split('\n', Qt::SkipEmptyParts))
            EditorLog::instance().add(LogLevel::Info, "stock", line.trimmed());
    });
    connect(proc, &QProcess::finished, this, [this, proc, work, zone](int code) {
        proc->deleteLater();
        const QString out = work + "/out/" + zone + ".ff";
        if (code != 0 || !QFileInfo::exists(out)) {
            ctx_.setStatus(QString("Stock zone rebuild failed (exit %1); see the Console. An old converter does not know editzone: "
                                   "update it in Tools > Tool Paths.").arg(code));
            return;
        }
        // The game's own loaders, through ACTS, on every zone written.
        const QString acts = toolPath("acts"), replay = toolPath("replay"), oodle = toolPath("oodle");
        if (acts.isEmpty() || !QFileInfo::exists(acts)) {
            ctx_.setStatus("Stock zone rebuilt: " + work + "/out (not checked: ACTS is not set; not installed)");
            return;
        }
        QStringList zones{out};
        if (QFileInfo::exists(work + "/out/techsets_" + zone + ".ff")) zones << work + "/out/techsets_" + zone + ".ff";
        auto remaining = std::make_shared<int>(int(zones.size()));
        auto failed = std::make_shared<int>(0);
        for (const QString& z : zones) {
            auto* check = new QProcess(this);
            check->setProgram(acts);
            QStringList a{"--noUpdater", "fastfile", "-r", "mw19replay", "-g", replay};
            if (!oodle.isEmpty()) a << "--oodle" << oodle;
            a << "--test" << "-o" << work + "/validate/" + QFileInfo(z).completeBaseName() << z;
            check->setArguments(a);
            check->setProcessChannelMode(QProcess::MergedChannels);
            connect(check, &QProcess::finished, this, [this, check, work, zone, remaining, failed, z](int) {
                const QString text = QString::fromLocal8Bit(check->readAll());
                check->deleteLater();
                bool ok = false;
                for (const QString& line : text.split('\n', Qt::SkipEmptyParts))
                    if (line.startsWith("Replay:")) {
                        EditorLog::instance().add(LogLevel::Info, "stock", QFileInfo(z).fileName() + ": " + line.trimmed());
                        ok = line.contains(" 0 failed");
                    }
                if (!ok) ++*failed;
                if (--*remaining > 0) return;
                if (*failed == 0) {
                    lastStockRebuild_ = work;
                    lastStockRebuildZone_ = zone;
                }
                ctx_.setStatus(*failed == 0 ? "Stock zone rebuilt and checked (ACTS, 0 failed): " + work +
                                                  "/out (not installed; File > Stock Map > Install Edited Stock Map Into Game)"
                                            : "Stock zone rebuilt, but the ACTS loader test reports failures; see the Console (" + work + "/out)");
            });
            check->start();
        }
    });
    proc->start();
    ctx_.setStatus("Rebuilding " + zone + " in " + work + " (a few seconds; the Console shows progress)");
}

int MainWindow::editLootPoints() {
    if (stockZone_.isEmpty()) {
        ctx_.setStatus("Edit Loot Points: open a stock map first (File > Open IW8 Stock Map)");
        return -1;
    }
    if (stockLootEditable_ || hasLootPoints(ctx_.doc.map())) {
        stockLootEditable_ = true;
        ctx_.setStatus("The loot points are already entities of the document (class iw8r_loot_point)");
        return 0;
    }
    ctx_.doc.beginGroup("Edit Loot Points");
    for (const LootPoint& p : stockLoot_.points) ctx_.doc.addEntity(lootPointEntityKeys(p, &stockLoot_), {}, "Loot point");
    ctx_.doc.endGroup();
    stockLootEditable_ = true;
    const size_t n = stockLoot_.points.size();
    ctx_.documentEdited(n ? QString("%1 loot points are now entities (class iw8r_loot_point): move, add or delete them; Rebuild Stock Zone "
                                    "writes them back").arg(n)
                          : QString("This map has no loot points. Add entities of class iw8r_loot_point; Rebuild Stock Zone writes them"));
    return int(n);
}

int MainWindow::editPlacedObjects() {
    if (stockZone_.isEmpty()) {
        ctx_.setStatus("Edit Placed Objects: open a stock map first (File > Open IW8 Stock Map)");
        return -1;
    }
    if (hasPlacedObjects(ctx_.doc.map())) {
        ctx_.setStatus("The placed objects are already entities of the document (class scriptable)");
        return 0;
    }
    const ScriptableCatalog& catalog = ScriptableCatalog::shared(QCoreApplication::applicationDirPath().toStdString());
    size_t n = 0;
    ctx_.doc.beginGroup("Edit Placed Objects");
    for (const StockPlacedObject& o : stockPlaced_) {
        const ScriptableDefinition* def = catalog.find(o.definition);
        if (!def || !(editablePlacedDefinition(*def) || movablePlacedDefinition(*def))) continue;
        ctx_.doc.addEntity(placedObjectEntityKeys(o), {}, "Placed object " + o.definition);
        ++n;
    }
    ctx_.doc.endGroup();
    if (n == 0) {
        ctx_.setStatus(QString("Edit Placed Objects: none of the %1 placed objects of this map can be edited (their definitions are not verified)").arg(stockPlaced_.size()));
        return 0;
    }
    ctx_.documentEdited(QString("%1 of %2 placed objects are now entities (class scriptable): move, copy or delete them (doors, signs, "
                                "props and buy stations; a door moves only a little, inside its area). Rebuild Stock Zone writes it")
                            .arg(n).arg(stockPlaced_.size()));
    return int(n);
}

QString MainWindow::stockZoneForReading(const QString& zone, QString* tempDir, QString* err) {
    if (tempDir) tempDir->clear();
    const QString name = QFileInfo(zone).completeBaseName();
    const QString backup = stockBackupDir(name);
    // An edit installed as a whole zone: the stock zone (and its stock patch) waits in the keep folder.
    const std::vector<std::string> zones = installedStockZones(backup.toStdString());
    if (std::find(zones.begin(), zones.end(), (name + ".ff").toStdString()) != zones.end()) {
        const QString kept = stockKeepDir() + "/" + name + ".ff";
        if (!QFileInfo::exists(kept)) {
            if (err) *err = "An edit of " + name + " is installed in the game, but the stock zone is not in " + QDir::toNativeSeparators(stockKeepDir()) +
                            ". Put the stock " + name + ".ff back there (or into the game's zone folder), then open the map again.";
            return {};
        }
        EditorLog::instance().add(LogLevel::Info, "stock", "an edit of " + name + " is installed in the game: the stock map is read from " +
                                                               QDir::toNativeSeparators(kept));
        return kept;
    }
    // An edit installed by the old method (a patch file): the stock map is read from a copy with the stock patch.
    const std::vector<std::string> installed = installedStockPatches(backup.toStdString());
    if (std::find(installed.begin(), installed.end(), (name + ".fp").toStdString()) == installed.end()) return zone;
    const QString view = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/stock_view/" + name;
    QDir(view).removeRecursively();
    QDir().mkpath(view);
    const QString copy = view + "/" + name + ".ff";
    const QString saved = backup + "/" + name + ".fp.stock";
    if (!QFile::copy(zone, copy) || (QFileInfo::exists(saved) && !QFile::copy(saved, view + "/" + name + ".fp"))) {
        QDir(view).removeRecursively();
        if (err) *err = "An edit of " + name + " is installed in the game, and the stock map could not be copied to " + view +
                        " to read it (disk space?). File > Stock Map > Restore Stock Map In Game, then open the map again.";
        return {};
    }
    if (tempDir) *tempDir = view;
    EditorLog::instance().add(LogLevel::Info, "stock", "an edit of " + name + " is installed in the game: the stock map is read from a copy with "
                                                           "the stock patch saved at install");
    return copy;
}

int MainWindow::loadStreamedWorldAroundCamera() {
    if (!streamZones_.empty()) {  // the tiles follow the camera already: look now
        streamDirty_ = true;
        updateStreamedTiles();
        ctx_.setStatus(QString("Streamed world: %1 tile(s) drawn around the camera, %2 in the map").arg(streamTiles_.size()).arg(streamZones_.size()));
        return int(streamTiles_.size());
    }
    if (referenceZone_.isEmpty() || !worldCache_) {
        ctx_.setStatus("Load Streamed World: open a stock map with its reference first (File > Open IW8 Stock Map)");
        return -1;
    }
    const auto dir = worldCache_->lookup(referenceZone_);
    if (!dir) {
        ctx_.setStatus("Load Streamed World: the stock reference is still being read; try again when it is drawn");
        return -1;
    }
    std::vector<StreamedWorldZone> zones;
    std::string err;
    if (!readStreamedWorldZones(dir->toStdString(), zones, &err)) {
        ctx_.setStatus("Load Streamed World: " + qs(err));
        return -1;
    }
    if (zones.empty()) {
        ctx_.setStatus("Load Streamed World: " + referenceZone_ + " has no streamed world tiles (all of it is drawn already)");
        return 0;
    }
    const auto [radius, limit] = streamTileReach();
    const auto near = streamedZonesNear(zones, ctx_.cam.pos.x, ctx_.cam.pos.y, radius, limit);
    QStringList tiles;
    for (const StreamedWorldZone& z : near) tiles << qs(z.name);
    static bool connected = false;
    if (!connected) {
        connected = true;
        connect(worldCache_, &WorldCache::streamedReady, this, [this](const QString& zone, int loaded, int failed) {
            if (zone != referenceZone_) return;
            ctx_.setStatus(QString("Streamed world: %1 tile(s) added%2; redrawing the reference").arg(loaded).arg(
                failed ? QString(", %1 failed").arg(failed) : QString()));
            loadStockReference(zone);
        });
    }
    worldCache_->requestStreamed(referenceZone_, tiles);
    ctx_.setStatus(QString("Load Streamed World: %1 of %2 tiles within %3 units of the camera are being read (a few seconds each)")
                       .arg(tiles.size()).arg(zones.size()).arg(radius));
    return int(tiles.size());
}

QString MainWindow::stockBackupDir(const QString& zone) const {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/stock_backup/" + zone;
}

QString MainWindow::stockKeepDir() const {
    const QString replay = toolPath("replay");
    return replay.isEmpty() ? QString() : QFileInfo(replay).absolutePath() + "/iw8r_stock";
}

// The edited map goes into the game as whole unsigned zone files in place of the stock ones (research 116: the game
// reads a patch of a signed zone only with its own signature). The stock files are moved to stockKeepDir() and put
// back by Restore. Only these two actions write to the game folder, and only after the owner confirms the list.
void MainWindow::installEditedStockMap() {
    QString outDir = lastStockRebuild_.isEmpty() ? QString() : lastStockRebuild_ + "/out";
    QString zone = lastStockRebuildZone_;
    if (outDir.isEmpty()) {
        // No rebuild in this session: the out folder of an earlier rebuild can be installed.
        if (!confirmGameFolderAction(this, "Install Edited Stock Map",
                                     "No stock map was rebuilt in this session (File > Stock Map > Rebuild Stock Zone).\n\n"
                                     "Choose a folder with edited zone files (the out folder of an earlier rebuild) to install instead?",
                                     "Choose Folder..."))
            return;
        outDir = QFileDialog::getExistingDirectory(this, "Folder with the edited zone files");
        if (outDir.isEmpty()) return;
        const std::vector<std::string> files = stockZoneFiles(outDir.toStdString());
        if (!files.empty()) {
            zone = QFileInfo(qs(files.front())).completeBaseName();
            if (zone.startsWith("techsets_")) zone = zone.mid(9);
        }
    }
    installEditedStockZones(outDir, zone, true);
}

bool MainWindow::installEditedStockZones(const QString& outDir, const QString& zone, bool ask) {
    const QString replay = toolPath("replay");
    const QString zoneDir = QFileInfo(replay).absolutePath() + "/zone";
    const std::vector<std::string> files = stockZoneFiles(outDir.toStdString());
    if (replay.isEmpty() || !QFileInfo(zoneDir).isDir() || files.empty() || zone.isEmpty()) {
        QMessageBox::warning(this, "Install Edited Stock Map",
                             files.empty() ? "There is no edited zone file (.ff) in " + QDir::toNativeSeparators(outDir) + "."
                                           : "The game folder is not set (Tools > Tool Paths).");
        return false;
    }
    QStringList names;
    qint64 bytes = 0;
    for (const std::string& f : files) {
        names << qs(f);
        bytes += QFileInfo(outDir + "/" + qs(f)).size();
    }
    const QString backup = stockBackupDir(zone), keep = stockKeepDir();
    if (ask &&
        !confirmGameFolderAction(this, "Install Edited Stock Map",
                                 QString("Install %1 edited zone file(s) into the game?\n\n%2\n\nTo: %3\n\nThe stock files of the same "
                                         "name, and their stock patch files, are moved to\n%4\nFile > Stock Map > Restore Stock Map In Game moves them "
                                         "back. The copy needs about %5 MB free on the game's drive.\n\nClose the game before you continue.")
                                     .arg(names.size()).arg(names.join("\n"), QDir::toNativeSeparators(zoneDir), QDir::toNativeSeparators(keep))
                                     .arg(bytes / (1024 * 1024) + 1),
                                 "Install"))
        return false;
    StockInstallReport report;
    std::string err;
    const bool ok = installStockZones(outDir.toStdString(), zoneDir.toStdString(), keep.toStdString(), backup.toStdString(), report, &err);
    for (const std::string& f : report.removed) EditorLog::instance().add(LogLevel::Info, "stock", "removed " + qs(f) + " from " + zoneDir);
    for (const std::string& f : report.backedUp) EditorLog::instance().add(LogLevel::Info, "stock", "moved the stock " + qs(f) + " to " + keep);
    for (const std::string& f : report.installed) EditorLog::instance().add(LogLevel::Info, "stock", "installed " + qs(f) + " into " + zoneDir);
    if (!ok) {
        QMessageBox::warning(this, "Install Edited Stock Map", qs(err));
        ctx_.setStatus("Install Edited Stock Map failed: " + qs(err));
        return false;
    }
    lastStockRebuildZone_ = zone;
    ctx_.setStatus(QString("Installed the edited %1 (%2 zone file(s)); File > Stock Map > Restore Stock Map In Game takes it out")
                       .arg(zone).arg(report.installed.size()));
    return true;
}

bool MainWindow::installStockZonesQuiet(const QString& outDir, QStringList& out) {
    const QString replay = toolPath("replay");
    const QString zoneDir = QFileInfo(replay).absolutePath() + "/zone";
    const std::vector<std::string> files = stockZoneFiles(outDir.toStdString());
    if (replay.isEmpty() || !QFileInfo(zoneDir).isDir()) {
        out << "FAIL the game folder is not set (Tools > Tool Paths)";
        return false;
    }
    if (files.empty()) {
        out << "FAIL there is no edited zone file (.ff) in " + QDir::toNativeSeparators(outDir);
        return false;
    }
    QString zone = QFileInfo(qs(files.front())).completeBaseName();
    if (zone.startsWith("techsets_")) zone = zone.mid(9);
    const QString backup = stockBackupDir(zone), keep = stockKeepDir();
    out << "zone " + zone << "game zone folder " + QDir::toNativeSeparators(zoneDir) << "stock files kept in " + QDir::toNativeSeparators(keep)
        << "install markers in " + QDir::toNativeSeparators(backup);
    StockInstallReport report;
    std::string err;
    const bool ok = installStockZones(outDir.toStdString(), zoneDir.toStdString(), keep.toStdString(), backup.toStdString(), report, &err);
    for (const std::string& f : report.removed) out << "removed " + qs(f), EditorLog::instance().add(LogLevel::Info, "stock", "removed " + qs(f) + " from " + zoneDir);
    for (const std::string& f : report.backedUp) out << "moved the stock " + qs(f) + " to the keep folder", EditorLog::instance().add(LogLevel::Info, "stock", "moved the stock " + qs(f) + " to " + keep);
    for (const std::string& f : report.installed) out << "installed " + qs(f), EditorLog::instance().add(LogLevel::Info, "stock", "installed " + qs(f) + " into " + zoneDir);
    if (!ok) {
        out << "FAIL " + qs(err);
        return false;
    }
    lastStockRebuildZone_ = zone;
    out << QString("ok %1 zone file(s) installed").arg(report.installed.size());
    return true;
}

bool MainWindow::restoreStockZonesQuiet(const QString& zone, QStringList& out) {
    const QString replay = toolPath("replay");
    const QString zoneDir = QFileInfo(replay).absolutePath() + "/zone";
    if (replay.isEmpty() || !QFileInfo(zoneDir).isDir() || zone.isEmpty()) {
        out << "FAIL the game folder is not set (Tools > Tool Paths), or no map was named";
        return false;
    }
    const QString backup = stockBackupDir(zone);
    std::vector<std::string> installed = installedStockZones(backup.toStdString());
    for (const std::string& f : installedStockPatches(backup.toStdString())) installed.push_back(f);
    if (installed.empty()) {
        out << "FAIL no edit of " + zone + " is installed by this editor";
        return false;
    }
    StockInstallReport report;
    std::string err;
    const bool ok = restoreStockZones(backup.toStdString(), zoneDir.toStdString(), stockKeepDir().toStdString(), report, &err);
    for (const std::string& f : report.removed) out << "removed " + qs(f), EditorLog::instance().add(LogLevel::Info, "stock", "removed " + qs(f) + " from " + zoneDir);
    for (const std::string& f : report.restored) out << "put back the stock " + qs(f), EditorLog::instance().add(LogLevel::Info, "stock", "put back the stock " + qs(f));
    if (!ok) {
        out << "FAIL " + qs(err);
        return false;
    }
    out << QString("ok %1 file(s) removed, %2 stock file(s) put back").arg(report.removed.size()).arg(report.restored.size());
    return true;
}

bool MainWindow::deployBuiltMapQuiet(QString* err) {
    const QString replay = toolPath("replay");
    if (replay.isEmpty() || !QFileInfo(replay).isFile()) {
        if (err) *err = "the game is not set (Tools > Tool Paths)";
        return false;
    }
    return compile_->deploy(QFileInfo(replay).absolutePath(), err);
}

void MainWindow::restoreStockMap() {
    const QString zone = !stockZone_.isEmpty() ? stockZone_ : lastStockRebuildZone_;
    if (zone.isEmpty()) {
        QMessageBox::information(this, "Restore Stock Map", "Open the stock map whose edit you want to take out (File > Open IW8 Stock Map).");
        return;
    }
    const QString backup = stockBackupDir(zone);
    std::vector<std::string> installed = installedStockZones(backup.toStdString());
    for (const std::string& f : installedStockPatches(backup.toStdString())) installed.push_back(f);
    if (installed.empty()) {
        QMessageBox::information(this, "Restore Stock Map", "No edit of " + zone + " is installed by this editor.");
        return;
    }
    const QString zoneDir = QFileInfo(toolPath("replay")).absolutePath() + "/zone";
    QStringList names;
    for (const std::string& f : installed) names << qs(f);
    if (!confirmGameFolderAction(this, "Restore Stock Map",
                                 QString("Take the edit of %1 out of the game?\n\n%2\n\nFrom: %3\n\nThe stock files that were moved "
                                         "away at install are put back. Close the game before you continue.")
                                     .arg(zone, names.join("\n"), QDir::toNativeSeparators(zoneDir)),
                                 "Restore"))
        return;
    StockInstallReport report;
    std::string err;
    const bool ok = restoreStockZones(backup.toStdString(), zoneDir.toStdString(), stockKeepDir().toStdString(), report, &err);
    for (const std::string& f : report.removed) EditorLog::instance().add(LogLevel::Info, "stock", "removed " + qs(f) + " from " + zoneDir);
    for (const std::string& f : report.restored) EditorLog::instance().add(LogLevel::Info, "stock", "put back the stock " + qs(f));
    if (!ok) {
        QMessageBox::warning(this, "Restore Stock Map", qs(err));
        return;
    }
    ctx_.setStatus(QString("Restored the stock %1 (%2 file(s) removed, %3 stock file(s) put back)")
                       .arg(zone).arg(report.removed.size()).arg(report.restored.size()));
}

int MainWindow::hideStockPropsInSelection() {
    if (stockZone_.isEmpty() || !refProps_ || refPropZone_ != referenceZone_) {
        ctx_.setStatus("Hide Stock Props: open a stock map and wait for its props first (File > Open IW8 Stock Map)");
        return -1;
    }
    const AABB box = ctx_.doc.selectionBounds();
    if (!box.valid()) {
        ctx_.setStatus("Hide Stock Props: select a brush that encloses the props to hide (the brush itself is not built into a stock map)");
        return -1;
    }
    std::vector<uint32_t> hidden = stockEditHiddenProps(ctx_.doc.map());
    const size_t before = hidden.size();
    for (const StockModelInstance& in : *refProps_) {
        const Vec3& o = in.origin;
        if (o.x < box.mins.x || o.y < box.mins.y || o.z < box.mins.z || o.x > box.maxs.x || o.y > box.maxs.y || o.z > box.maxs.z) continue;
        hidden.push_back(in.index);
    }
    std::sort(hidden.begin(), hidden.end());
    hidden.erase(std::unique(hidden.begin(), hidden.end()), hidden.end());
    const int added = int(hidden.size() - before);
    if (added == 0) {
        ctx_.setStatus("Hide Stock Props: no stock prop has its origin inside the selection");
        return 0;
    }
    std::string list;
    for (const uint32_t i : hidden) list += (list.empty() ? "" : " ") + std::to_string(i);
    for (const Entity& e : ctx_.doc.map().entities)
        if (e.isWorldspawn()) {
            ctx_.doc.setKeyValue(e.id, "iw8r_hide_smodels", list);
            break;
        }
    rebuildReferenceProps();
    ctx_.documentEdited(QString("%1 stock prop(s) hidden (%2 in all): File > Stock Map > Rebuild Stock Zone leaves them out; their collision stays")
                            .arg(added).arg(hidden.size()));
    return added;
}

void MainWindow::setReferencePropsForTest(std::vector<StockModelInstance> props) {
    referenceZone_ = stockZone_;
    refPropZone_ = stockZone_;
    refPropUse_ = stockModelUsage(props);
    refProps_ = std::make_shared<std::vector<StockModelInstance>>(std::move(props));
}

int MainWindow::showAllStockProps() {
    const std::vector<uint32_t> hidden = stockEditHiddenProps(ctx_.doc.map());
    if (hidden.empty()) {
        ctx_.setStatus("Show All Stock Props: no stock prop is hidden");
        return 0;
    }
    for (const Entity& e : ctx_.doc.map().entities)
        if (e.isWorldspawn()) {
            ctx_.doc.removeKey(e.id, "iw8r_hide_smodels");
            break;
        }
    rebuildReferenceProps();
    ctx_.documentEdited(QString("%1 hidden stock prop(s) are shown again").arg(hidden.size()));
    return int(hidden.size());
}

// ------------------------------------------------------------------ stock edits in the viewport

MainWindow::StockWorldEdits MainWindow::stockWorldEdits() const {
    StockWorldEdits e;
    // The retexture pairs the rebuild takes (same parse and catalogue check: a pair it leaves out is not shown).
    std::vector<StockEditCarry> carry;
    std::vector<std::string> entries;
    stockRetextureEdits(ctx_.doc.map(), catalogueZoneOf, carry, entries, nullptr);
    for (const std::string& j : entries) {
        const size_t f = j.find("\"from\":\""), t = j.find("\",\"to\":\"");
        if (f == std::string::npos || t == std::string::npos) continue;
        const std::string from = j.substr(f + 8, t - (f + 8));
        const size_t te = j.find('"', t + 8);
        if (te == std::string::npos) continue;
        e.remap[from] = j.substr(t + 8, te - (t + 8));
    }
    for (const WorldRemovalBox& b : documentWorldRemovals(ctx_.doc.map()))
        if (b.render) e.boxes.push_back({b.mins.x, b.mins.y, b.mins.z, b.maxs.x, b.maxs.y, b.maxs.z});
    for (const auto& [from, to] : e.remap) e.signature += qs(from) + "=" + qs(to) + ";";
    for (const auto& b : e.boxes)
        for (const double v : b) e.signature += QString::number(v, 'f', 2) + " ";
    return e;
}

void MainWindow::applyStockWorldEdits(std::vector<app::ReferenceTexBatch>& batches, const StockWorldEdits& e, size_t* removed,
                                      size_t* swapped) {
    size_t gone = 0, renamed = 0;
    const auto inside = [&](const app::GpuTexVertex& v, const std::array<double, 6>& b) {
        constexpr double eps = 1e-3;
        return v.x >= b[0] - eps && v.y >= b[1] - eps && v.z >= b[2] - eps && v.x <= b[3] + eps && v.y <= b[4] + eps && v.z <= b[5] + eps;
    };
    for (app::ReferenceTexBatch& b : batches) {
        // A world surface's material (its layers carry the same name); decal volumes and terrain layers are not
        // world surfaces.
        if (!b.decalVolume && !b.terrainLayer)
            if (const auto it = e.remap.find(b.material); it != e.remap.end()) b.material = it->second, ++renamed;
        if (e.boxes.empty() || b.tris.size() < 3) continue;
        // World triangles with all three vertices inside a box (as the rebuild's remove_world_surfaces), and the decals
        // projected on them.
        size_t w = 0;
        for (size_t t = 0; t + 2 < b.tris.size(); t += 3) {
            bool out = false;
            for (const auto& box : e.boxes)
                if (inside(b.tris[t], box) && inside(b.tris[t + 1], box) && inside(b.tris[t + 2], box)) {
                    out = true;
                    break;
                }
            if (out) {
                ++gone;
                continue;
            }
            if (w != t) b.tris[w] = b.tris[t], b.tris[w + 1] = b.tris[t + 1], b.tris[w + 2] = b.tris[t + 2];
            w += 3;
        }
        b.tris.resize(w);
    }
    if (removed) *removed = gone;
    if (swapped) *swapped = renamed;
}

void MainWindow::refreshStockWorldPreview() {
    stockPreviewQueued_ = false;
    applyStockSunPreview();
    if (camera_) camera_->update();  // the sky source (iw8r_sky_from) is asked every frame
    if (referenceZone_.isEmpty() || !stockWorld_) return;
    StockWorldEdits edits = stockWorldEdits();
    if (edits.signature == stockWorldEditSig_) return;
    stockWorldEditSig_ = edits.signature;
    if (!stockRefBatches_) {
        // The world was too big to keep a copy of: read again, with the edits.
        ctx_.setStatus("Stock map edits: reading " + referenceZone_ + " again to show them...");
        loadStockReference(referenceZone_);
        return;
    }
    const int gen = ++stockPreviewGen_;
    QPointer<MainWindow> self(this);
    std::thread(guardedWorker(__FILE__, __LINE__, [self, src = stockRefBatches_, edits = std::move(edits), gen] {
        auto out = std::make_shared<std::vector<app::ReferenceTexBatch>>(*src);
        size_t removed = 0, swapped = 0;
        applyStockWorldEdits(*out, edits, &removed, &swapped);
        QMetaObject::invokeMethod(qApp, [self, out, gen, removed, swapped] {
            if (!self || gen != self->stockPreviewGen_) return;  // a newer edit replaces this one
            self->camera_->setReferenceTextured(std::move(*out));
            QStringList parts;
            if (swapped) parts << QString("%1 surface batches retextured").arg(swapped);
            if (removed) parts << QString("%1 world triangles removed").arg(removed);
            self->ctx_.setTransientStatus(parts.isEmpty() ? QString("Stock map edits: the world is shown as stock")
                                                          : "Stock map edits shown in the viewport (" + parts.join(", ") + "; the rebuild writes them)");
        }, Qt::QueuedConnection);
    })).detach();
}

void MainWindow::applyStockSunPreview() {
    if (!stockSunBaseKnown_ || !camera_) return;
    double intensity = stockSunBaseIntensity_;
    Vec3 color = stockSunBaseColor_, toward = stockSunBaseToward_;
    QString sig;
    for (const Entity& e : ctx_.doc.map().entities) {
        if (!e.isWorldspawn()) continue;
        // The values the rebuild writes (core stockSunJson: the same ranges; a bad value is left out there too).
        if (const std::string* c = e.value("iw8r_sun_color"); c && !c->empty()) {
            double v[3] = {0, 0, 0};
            char tail = 0;
            if (std::sscanf(c->c_str(), "%lf %lf %lf %c", &v[0], &v[1], &v[2], &tail) == 3 && v[0] >= 0 && v[1] >= 0 && v[2] >= 0 &&
                v[0] <= 16 && v[1] <= 16 && v[2] <= 16)
                color = Vec3{v[0], v[1], v[2]}, sig += "c" + qs(*c);
        }
        if (const std::string* i = e.value("iw8r_sun_intensity"); i && !i->empty()) {
            char* endp = nullptr;
            const double v = std::strtod(i->c_str(), &endp);
            if (endp && *endp == 0 && v >= 0 && v <= 1000000) intensity = v, sig += "i" + qs(*i);
        }
        if (const std::string* d = e.value("iw8r_sun_direction"); d && !d->empty()) {
            double pitch = 0, yaw = 0;
            if (parseStockSunDirection(*d, pitch, yaw)) {
                constexpr double kDeg = 3.14159265358979323846 / 180.0;
                const double p = pitch * kDeg, y = yaw * kDeg;
                toward = normalize(Vec3{std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), -std::sin(p)});
                sig += "d" + qs(*d);
            }
        }
        break;
    }
    if (sig == stockSunSig_ && stockSunApplied_) return;
    stockSunSig_ = sig;
    stockSunApplied_ = true;
    if (intensity > 0) camera_->setGameStockSun(intensity, color, toward);
}

// ------------------------------------------------------------------ the stock world in the play preview

size_t MainWindow::addStockWorldCollision(CollisionWorld& w) const {
    // The script brush models ("*N": doors, bomb sites, barriers): solid in game through their compiled submodel; a
    // box of the submodel's bounds, turned by the entity's angles.
    size_t added = 0;
    for (const Entity& e : ctx_.doc.map().entities) {
        if (e.classname() != "script_brushmodel") continue;
        const std::string* model = e.value("model");
        const auto o = e.origin();
        if (!model || model->size() < 2 || (*model)[0] != '*' || !o) continue;
        const int n = std::atoi(model->c_str() + 1);
        if (n <= 0 || size_t(n) >= stockSubModels_.size()) continue;
        const Iw8SubModel& sm = stockSubModels_[size_t(n)];
        if (sm.half.x <= 0 || sm.half.y <= 0 || sm.half.z <= 0 || sm.half.x > 4096 || sm.half.y > 4096 || sm.half.z > 4096) continue;
        Vec3 ang;
        if (const std::string* a = e.value("angles")) std::sscanf(a->c_str(), "%lf %lf %lf", &ang.x, &ang.y, &ang.z);
        const auto ax = anglesToAxis(ang);
        const Vec3 c = *o + ax[0] * sm.mid.x + ax[1] * sm.mid.y + ax[2] * sm.mid.z;
        std::vector<Plane> planes;
        const double h[3] = {sm.half.x, sm.half.y, sm.half.z};
        for (int k = 0; k < 3; ++k)
            for (const double s : {1.0, -1.0}) {
                Plane p;
                p.n = ax[size_t(k)] * s;
                p.d = dot(p.n, c) + h[k];
                planes.push_back(p);
            }
        if (w.addConvex(std::move(planes))) ++added;
    }
    if (!stockWorld_) return added;
    // Removal boxes that take the collision out (iw8r_remove_world, "collision" not 0).
    std::vector<WorldRemovalBox> boxes;
    for (const WorldRemovalBox& b : documentWorldRemovals(ctx_.doc.map()))
        if (b.collision) boxes.push_back(b);
    const auto inBox = [&](const Vec3& p, const WorldRemovalBox& b) {
        return p.x >= b.mins.x - 1e-3 && p.y >= b.mins.y - 1e-3 && p.z >= b.mins.z - 1e-3 && p.x <= b.maxs.x + 1e-3 &&
               p.y <= b.maxs.y + 1e-3 && p.z <= b.maxs.z + 1e-3;
    };
    for (const ImportedMesh& m : stockWorld_->meshes) {
        const QString mat = qs(m.material).remove(QChar(',')).trimmed().toLower();
        const QString leaf = mat.section('/', -1);
        // Not walked on or against in game: the sky, decals, foliage and grass cards, translucent surfaces.
        if (leaf.startsWith("sky_") || mat.contains("decal")) continue;
        const bool plantWord = mat.contains("foliage") || mat.contains("grass") || mat.contains("leaves") || mat.contains("ivy") || mat.contains("vine");
        if (const auto e = matCache_->lookup(qs(m.material).remove(QChar(',')).trimmed()); e && !e->failed) {
            // The material is known: blended surfaces and cut-out plant cards are not solid; opaque ground named after
            // grass or leaves ("tw/grass_lawn_cut_01" on Shipment) is.
            const MaterialBlend blend = materialBlendFromTechset(e->techset.toStdString());
            if (blend == MaterialBlend::Blend || blend == MaterialBlend::Additive) continue;
            if (plantWord && blend == MaterialBlend::AlphaTest) continue;
        } else if (mat.contains("foliage") || mat.contains("ivy") || mat.contains("vine")) {
            continue;  // not read yet: plant cards by name; grass and leaf ground stays solid
        }
        for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
            const uint32_t i0 = m.idx[t], i1 = m.idx[t + 1], i2 = m.idx[t + 2];
            if (i0 >= m.pos.size() || i1 >= m.pos.size() || i2 >= m.pos.size()) continue;
            const Vec3 &a = m.pos[i0], &b = m.pos[i1], &c = m.pos[i2];
            bool removed = false;
            for (const WorldRemovalBox& box : boxes)
                if (inBox(a, box) && inBox(b, box) && inBox(c, box)) {
                    removed = true;
                    break;
                }
            if (!removed && w.addTriangle(a, b, c, 2.0)) ++added;
        }
    }
    return added;
}

// ------------------------------------------------------------------ picking the stock map's own surfaces

// A stock light's shape as line pairs: a marker (three axes), and with `ranges` its radius (omni: three circles) or its
// cone (spot: the outer cone to the radius, its end circle and the aim line). Light shines along -dir (dir points back
// toward the light, research 106 section 3.1).
static void appendStockLightShape(std::vector<app::GpuVertex>& out, const StockPrimaryLight& l, uint32_t col, bool ranges) {
    const Vec3 o{l.origin[0], l.origin[1], l.origin[2]};
    const auto line = [&](const Vec3& a, const Vec3& b) {
        out.push_back(app::GpuVertex{float(a.x), float(a.y), float(a.z), col});
        out.push_back(app::GpuVertex{float(b.x), float(b.y), float(b.z), col});
    };
    const double m = 10.0;
    line(o - Vec3{m, 0, 0}, o + Vec3{m, 0, 0}), line(o - Vec3{0, m, 0}, o + Vec3{0, m, 0}), line(o - Vec3{0, 0, m}, o + Vec3{0, 0, m});
    if (!ranges || l.radius <= 0) return;
    const double r = std::min<double>(l.radius, 16384.0);
    const auto circle = [&](const Vec3& c, const Vec3& u, const Vec3& v, double rad, int seg) {
        for (int i = 0; i < seg; ++i) {
            const double a0 = 6.283185307 * i / seg, a1 = 6.283185307 * (i + 1) / seg;
            line(c + u * (std::cos(a0) * rad) + v * (std::sin(a0) * rad), c + u * (std::cos(a1) * rad) + v * (std::sin(a1) * rad));
        }
    };
    if (l.type == 3) {  // omni
        circle(o, Vec3{1, 0, 0}, Vec3{0, 1, 0}, r, 48);
        circle(o, Vec3{1, 0, 0}, Vec3{0, 0, 1}, r, 48);
        circle(o, Vec3{0, 1, 0}, Vec3{0, 0, 1}, r, 48);
        return;
    }
    Vec3 dir = Vec3{-l.dir[0], -l.dir[1], -l.dir[2]};
    if (length(dir) < 1e-6) dir = Vec3{0, 0, -1};
    dir = normalize(dir);
    const Vec3 ref = std::fabs(dir.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    const Vec3 u = normalize(cross(dir, ref)), v = cross(dir, u);
    const double c = std::clamp<double>(l.cosHalfFovOuter, 0.0, 0.9999);
    const double half = std::acos(c);
    const double len = r * std::cos(half), rad = r * std::sin(half);  // the cone's cap on the radius sphere
    const Vec3 end = o + dir * len;
    for (int i = 0; i < 8; ++i) {
        const double a = 6.283185307 * i / 8;
        line(o, end + u * (std::cos(a) * rad) + v * (std::sin(a) * rad));
    }
    circle(end, u, v, rad, 32);
    line(o, o + dir * r);
}

void MainWindow::updateStockLightShapes() {
    if (!camera_) return;
    std::vector<app::GpuVertex> lines;
    if (stockLightList_) {
        const bool ranges = ctx_.scene.showLightRanges;
        for (const StockPrimaryLight& l : *stockLightList_) {
            // The light's colour, dimmed (the own map's unselected light ranges are dimmed too).
            const double mx = std::max({double(l.color[0]), double(l.color[1]), double(l.color[2]), 1e-6});
            const auto b = [&](float c) { return uint32_t(std::clamp(c / mx * 0.55 + 0.15, 0.0, 1.0) * 255.0 + 0.5); };
            appendStockLightShape(lines, l, b(l.color[0]) | (b(l.color[1]) << 8) | (b(l.color[2]) << 16) | 0xFF000000u, ranges);
        }
    }
    camera_->setStockLightShapes(std::move(lines));
}

std::optional<MainWindow::StockSurfacePick> MainWindow::pickStockSurface(const Ray& ray) const {
    std::optional<StockSurfacePick> best;
    double bestT = 1e300;
    if (stockWorld_) {
        if (stockPickBoundsFor_ != stockWorld_.get()) {
            stockPickBounds_.assign(stockWorld_->meshes.size(), AABB{});
            for (size_t i = 0; i < stockWorld_->meshes.size(); ++i)
                for (const Vec3& p : stockWorld_->meshes[i].pos) stockPickBounds_[i].add(p);
            stockPickBoundsFor_ = stockWorld_.get();
        }
        const std::vector<WorldRemovalBox> boxes = documentWorldRemovals(ctx_.doc.map());
        const auto inBox = [](const Vec3& p, const WorldRemovalBox& b) {
            return p.x >= b.mins.x - 1e-3 && p.y >= b.mins.y - 1e-3 && p.z >= b.mins.z - 1e-3 && p.x <= b.maxs.x + 1e-3 &&
                   p.y <= b.maxs.y + 1e-3 && p.z <= b.maxs.z + 1e-3;
        };
        for (size_t mi = 0; mi < stockWorld_->meshes.size(); ++mi) {
            const ImportedMesh& m = stockWorld_->meshes[mi];
            const auto enter = rayAABB(ray, stockPickBounds_[mi]);
            if (!enter || *enter >= bestT) continue;
            const QString mat = qs(m.material).remove(QChar(',')).trimmed();
            if (mat.section('/', -1).toLower().startsWith("sky_")) continue;  // the sky box is not a surface to pick
            for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
                const uint32_t i0 = m.idx[t], i1 = m.idx[t + 1], i2 = m.idx[t + 2];
                if (i0 >= m.pos.size() || i1 >= m.pos.size() || i2 >= m.pos.size()) continue;
                const Vec3 &a = m.pos[i0], &b = m.pos[i1], &c = m.pos[i2];
                // Moller-Trumbore, both sides.
                const Vec3 e1 = b - a, e2 = c - a;
                const Vec3 pv = cross(ray.dir, e2);
                const double det = dot(e1, pv);
                if (std::fabs(det) < 1e-12) continue;
                const double inv = 1.0 / det;
                const Vec3 tv = ray.origin - a;
                const double u = dot(tv, pv) * inv;
                if (u < 0 || u > 1) continue;
                const Vec3 qv = cross(tv, e1);
                const double v = dot(ray.dir, qv) * inv;
                if (v < 0 || u + v > 1) continue;
                const double d = dot(e2, qv) * inv;
                if (d <= 0.01 || d >= bestT) continue;
                bool removed = false;
                for (const WorldRemovalBox& box : boxes)
                    if (inBox(a, box) && inBox(b, box) && inBox(c, box)) {
                        removed = true;
                        break;
                    }
                if (removed) continue;
                bestT = d;
                best = StockSurfacePick{mat, QString(), 0, ray.origin + ray.dir * d, d, int(mi)};
                best->tri = int(t);
            }
        }
    }
    if (stockLightList_)
        for (size_t li = 0; li < stockLightList_->size(); ++li) {
            const StockPrimaryLight& l = (*stockLightList_)[li];
            const Vec3 c{l.origin[0], l.origin[1], l.origin[2]};
            const Vec3 oc = ray.origin - c;
            const double bq = dot(oc, ray.dir), cq = dot(oc, oc) - 12.0 * 12.0, disc = bq * bq - cq;
            if (disc < 0) continue;
            const double t = -bq - std::sqrt(disc);
            if (t > 0.01 && t < bestT) {
                bestT = t;
                best = StockSurfacePick{QString(), QString(), 0, ray.origin + ray.dir * t, t, -1};
                best->light = int(li);
            }
        }
    if (refProps_ && !refPropBoxes_.empty()) {
        std::set<uint32_t> hidden;
        for (const Entity& ws : ctx_.doc.map().entities)
            if (const std::string* v = ws.isWorldspawn() ? ws.value("iw8r_hide_smodels") : nullptr)
                for (const QString& tok : qs(*v).split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts)) hidden.insert(tok.toUInt());
        // Candidates by their model box (in model space), nearest first; then the model's own triangles decide (a box
        // only narrows the search: a big model's box encloses much that is not the model).
        struct Cand {
            double t;
            size_t i;
            Ray local;
            double sc;
        };
        std::vector<Cand> cands;
        for (size_t pi = 0; pi < refProps_->size(); ++pi) {
            const StockModelInstance& in = (*refProps_)[pi];
            const auto it = refPropBoxes_.find(in.model);
            if (it == refPropBoxes_.end() || hidden.count(in.index)) continue;
            const double sc = in.scale > 1e-6 ? in.scale : 1.0;
            const Vec3 d = ray.origin - in.origin;
            const Ray local{{dot(d, in.axis[0]) / sc, dot(d, in.axis[1]) / sc, dot(d, in.axis[2]) / sc},
                            {dot(ray.dir, in.axis[0]), dot(ray.dir, in.axis[1]), dot(ray.dir, in.axis[2])}};
            const auto& b = it->second;
            AABB box;
            box.mins = {b[0] - 1, b[1] - 1, b[2] - 1};
            box.maxs = {b[3] + 1, b[4] + 1, b[5] + 1};
            if (const auto tl = rayAABB(local, box); tl && *tl * sc < bestT) cands.push_back({*tl * sc, pi, local, sc});
        }
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.t < b.t; });
        size_t tested = 0;
        for (const Cand& c : cands) {
            if (c.t >= bestT || tested >= 64) break;
            ++tested;
            const StockModelInstance& in = (*refProps_)[c.i];
            const auto model = stockPickModel(in.model);
            double hitT = -1;
            if (model) {
                const auto zup = [](const Vec3& p) { return Vec3{p.x, -p.z, p.y}; };  // glTF Y-up -> Z-up (as the props' meshes)
                for (const ImportedMesh& m : model->meshes)
                    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
                        const uint32_t i0 = m.idx[t], i1 = m.idx[t + 1], i2 = m.idx[t + 2];
                        if (i0 >= m.pos.size() || i1 >= m.pos.size() || i2 >= m.pos.size()) continue;
                        const Vec3 a = zup(m.pos[i0]), e1 = zup(m.pos[i1]) - a, e2 = zup(m.pos[i2]) - a;
                        const Vec3 pv = cross(c.local.dir, e2);
                        const double det = dot(e1, pv);
                        if (std::fabs(det) < 1e-12) continue;
                        const double inv = 1.0 / det;
                        const Vec3 tv = c.local.origin - a;
                        const double u = dot(tv, pv) * inv;
                        if (u < 0 || u > 1) continue;
                        const Vec3 qv = cross(tv, e1);
                        const double v = dot(c.local.dir, qv) * inv;
                        if (v < 0 || u + v > 1) continue;
                        const double d = dot(e2, qv) * inv;
                        if (d > 0.01 && (hitT < 0 || d < hitT)) hitT = d;
                    }
            } else {
                hitT = c.t / c.sc;  // the model is not readable: its box stands in
            }
            if (hitT < 0 || hitT * c.sc >= bestT) continue;
            bestT = hitT * c.sc;
            best = StockSurfacePick{QString(), qs(in.model), in.index, ray.origin + ray.dir * bestT, bestT};
            best->prop = int(c.i);
            const auto& b = refPropBoxes_.at(in.model);
            for (int k = 0; k < 8; ++k) {
                const double x = (k & 1) ? b[3] : b[0], y = (k & 2) ? b[4] : b[1], z = (k & 4) ? b[5] : b[2];
                best->corners[size_t(k)] = in.origin + (in.axis[0] * x + in.axis[1] * y + in.axis[2] * z) * c.sc;
            }
        }
    }
    return best;
}

bool MainWindow::showStockSurfaceAt(const Ray& ray) {
    stockSel_.reset();
    if (!stockWorld_ && !refProps_ && !stockLightList_) return false;
    const auto hit = pickStockSurface(ray);
    if (!hit) return false;
    // Its outline in the camera: the world surface's triangles around the click, or the prop's model box.
    std::vector<app::GpuVertex> lines;
    const uint32_t col = 0xFF20D8FFu;  // amber (R in the low byte)
    const auto line = [&](const Vec3& p, const Vec3& q) {
        lines.push_back(app::GpuVertex{float(p.x), float(p.y), float(p.z), col});
        lines.push_back(app::GpuVertex{float(q.x), float(q.y), float(q.z), col});
    };
    if (hit->light >= 0 && stockLightList_ && size_t(hit->light) < stockLightList_->size()) {
        appendStockLightShape(lines, (*stockLightList_)[size_t(hit->light)], col, true);
    } else if (hit->mesh >= 0 && hit->tri >= 0 && stockWorld_ && size_t(hit->mesh) < stockWorld_->meshes.size()) {
        // The connected surface: triangles of the same mesh sharing an edge (by position) with one already in, facing
        // within 35 degrees of the clicked one (a wall, a floor); outlined along its border.
        const ImportedMesh& m = stockWorld_->meshes[size_t(hit->mesh)];
        const size_t nt = m.idx.size() / 3;
        const auto key = [&](uint32_t vi) {
            const Vec3& p = m.pos[vi];
            return std::array<long long, 3>{std::llround(p.x * 8), std::llround(p.y * 8), std::llround(p.z * 8)};
        };
        const auto triN = [&](size_t t) {
            const Vec3 &a = m.pos[m.idx[t * 3]], &b = m.pos[m.idx[t * 3 + 1]], &c = m.pos[m.idx[t * 3 + 2]];
            const Vec3 n = cross(b - a, c - a);
            const double l = length(n);
            return l > 1e-12 ? n * (1.0 / l) : Vec3{0, 0, 0};
        };
        bool ok = true;
        for (size_t i = 0; i < m.idx.size() && ok; ++i) ok = m.idx[i] < m.pos.size();
        std::map<std::pair<std::array<long long, 3>, std::array<long long, 3>>, std::vector<uint32_t>> edges;
        const auto ekey = [&](uint32_t a, uint32_t b) {
            auto ka = key(a), kb = key(b);
            if (kb < ka) std::swap(ka, kb);
            return std::make_pair(ka, kb);
        };
        if (ok && nt < 2000000)
            for (size_t t = 0; t < nt; ++t)
                for (int e = 0; e < 3; ++e) edges[ekey(m.idx[t * 3 + e], m.idx[t * 3 + (e + 1) % 3])].push_back(uint32_t(t));
        const size_t t0 = size_t(hit->tri) / 3;
        const Vec3 n0 = triN(t0);
        std::vector<char> in(nt, 0);
        std::vector<size_t> stack{t0}, picked;
        in[t0] = 1;
        while (!stack.empty() && picked.size() < 40000) {
            const size_t t = stack.back();
            stack.pop_back();
            picked.push_back(t);
            for (int e = 0; e < 3 && ok; ++e) {
                const auto it = edges.find(ekey(m.idx[t * 3 + e], m.idx[t * 3 + (e + 1) % 3]));
                if (it == edges.end()) continue;
                for (const uint32_t o : it->second)
                    if (!in[o] && dot(triN(o), n0) > 0.82) in[o] = 1, stack.push_back(o);
            }
        }
        AABB sb;
        for (const size_t t : picked)
            for (int e = 0; e < 3; ++e) {
                const uint32_t a = m.idx[t * 3 + e], b = m.idx[t * 3 + (e + 1) % 3];
                sb.add(m.pos[a]);
                // A border edge: no other picked triangle shares it.
                int shared = 0;
                if (const auto it = edges.find(ekey(a, b)); it != edges.end())
                    for (const uint32_t o : it->second) shared += in[o] ? 1 : 0;
                if (shared <= 1 && lines.size() < 60000) line(m.pos[a], m.pos[b]);
            }
        stockSelBounds_ = sb;
        stockSel_ = *hit;
        if (!picked.empty()) stockSel_->tri = int(picked.size());  // (the count, for the status below)
    } else if (hit->prop >= 0 && refProps_ && size_t(hit->prop) < refProps_->size()) {
        // The model's own edges: the border and the creases (faces meeting at more than 40 degrees), in place.
        const StockModelInstance& in = (*refProps_)[size_t(hit->prop)];
        const double sc = in.scale > 1e-6 ? in.scale : 1.0;
        const auto place = [&](const Vec3& p) {
            const Vec3 z{p.x, -p.z, p.y};
            return in.origin + (in.axis[0] * z.x + in.axis[1] * z.y + in.axis[2] * z.z) * sc;
        };
        AABB sb;
        if (const auto model = stockPickModel(in.model)) {
            for (const ImportedMesh& m : model->meshes) {
                std::map<std::pair<std::array<long long, 3>, std::array<long long, 3>>, std::vector<Vec3>> edges;
                const auto key = [&](const Vec3& p) {
                    return std::array<long long, 3>{std::llround(p.x * 16), std::llround(p.y * 16), std::llround(p.z * 16)};
                };
                for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
                    if (m.idx[t] >= m.pos.size() || m.idx[t + 1] >= m.pos.size() || m.idx[t + 2] >= m.pos.size()) continue;
                    const Vec3 &a = m.pos[m.idx[t]], &b = m.pos[m.idx[t + 1]], &c = m.pos[m.idx[t + 2]];
                    Vec3 n = cross(b - a, c - a);
                    const double l = length(n);
                    n = l > 1e-12 ? n * (1.0 / l) : Vec3{0, 0, 0};
                    for (const auto& [p, q] : {std::make_pair(a, b), std::make_pair(b, c), std::make_pair(c, a)}) {
                        auto kp = key(p), kq = key(q);
                        if (kq < kp) std::swap(kp, kq);
                        edges[{kp, kq}].push_back(n);
                    }
                }
                for (const auto& [k, ns] : edges) {
                    const bool border = ns.size() == 1, crease = ns.size() >= 2 && dot(ns[0], ns[1]) < 0.766;
                    if (!border && !crease) continue;
                    const Vec3 p{k.first[0] / 16.0, k.first[1] / 16.0, k.first[2] / 16.0}, q{k.second[0] / 16.0, k.second[1] / 16.0, k.second[2] / 16.0};
                    if (lines.size() < 80000) line(place(p), place(q));
                    sb.add(place(p)), sb.add(place(q));
                }
            }
        }
        if (lines.empty()) {
            const auto& k = hit->corners;
            for (int i = 0; i < 8; ++i)
                for (int bit : {1, 2, 4})
                    if (!(i & bit)) line(k[size_t(i)], k[size_t(i | bit)]);
        }
        if (!sb.valid())
            for (const Vec3& c : hit->corners) sb.add(c);
        stockSelBounds_ = sb;
        stockSel_ = *hit;
    } else if (hit->mesh < 0 && hit->light < 0) {
        const auto& k = hit->corners;
        for (int i = 0; i < 8; ++i)
            for (int bit : {1, 2, 4})
                if (!(i & bit)) line(k[size_t(i)], k[size_t(i | bit)]);
    }
    camera_->setStockPickOutline(std::move(lines));
    if (hit->light >= 0 && stockLightList_ && size_t(hit->light) < stockLightList_->size()) {
        const StockPrimaryLight& l = (*stockLightList_)[size_t(hit->light)];
        const double outer = 2.0 * std::acos(std::clamp<double>(l.cosHalfFovOuter, -1.0, 1.0)) * 57.29578;
        ctx_.setStatus(QString("Stock light (%1): row %2 at %3 %4 %5, radius %6, intensity %7, colour %8 %9 %10%11%12. The map's own "
                               "light (read-only here: its edits need the converter)")
                           .arg(l.type == 2 ? "spot" : "omni")
                           .arg(l.index)
                           .arg(qRound(l.origin[0])).arg(qRound(l.origin[1])).arg(qRound(l.origin[2]))
                           .arg(qRound(l.radius))
                           .arg(l.intensity, 0, 'g', 4)
                           .arg(l.color[0], 0, 'f', 2).arg(l.color[1], 0, 'f', 2).arg(l.color[2], 0, 'f', 2)
                           .arg(l.type == 2 ? QString(", cone %1 degrees").arg(qRound(outer)) : QString())
                           .arg(l.defName.empty() ? QString() : ", profile " + qs(l.defName)));
        return true;
    }
    if (!hit->material.isEmpty()) {
        ctx_.setStatus(QString("Stock surface: %1 (the map's world, %2 triangles connected, %3 units away). Delete removes it (a "
                               "world removal box around it); the Stock Map panel's Retexture takes the material")
                           .arg(hit->material)
                           .arg(stockSel_ ? stockSel_->tri : 0)
                           .arg(qRound(hit->t)));
        emit stockSurfacePicked(hit->material);
    } else {
        ctx_.setStatus(QString("Stock prop: %1 (row %2, %3 units away). Delete hides it; a double-click makes it a model you can move, "
                               "turn and scale")
                           .arg(hit->model)
                           .arg(hit->propIndex)
                           .arg(qRound(hit->t)));
        emit stockPropPicked(hit->model, hit->propIndex);
    }
    return true;
}

std::shared_ptr<const ImportedModel> MainWindow::stockPickModel(const std::string& name) const {
    if (const auto it = stockPickModels_.find(name); it != stockPickModels_.end()) return it->second;
    std::shared_ptr<const ImportedModel> m;
    if (modelCacheStock_)
        if (auto im = modelCacheStock_->loadModel(qs(name))) m = std::make_shared<const ImportedModel>(std::move(*im));
    if (stockPickModels_.size() > 256) stockPickModels_.clear();
    stockPickModels_[name] = m;
    return m;
}

bool MainWindow::deleteStockSelection() {
    if (!stockSel_) return false;
    const StockSurfacePick sel = *stockSel_;
    if (sel.light >= 0) {
        ctx_.setStatus("A stock map's own light is read-only here (its edits need the converter)");
        return true;
    }
    if (sel.prop >= 0 && refProps_ && size_t(sel.prop) < refProps_->size()) {
        std::vector<uint32_t> hidden = stockEditHiddenProps(ctx_.doc.map());
        const uint32_t row = (*refProps_)[size_t(sel.prop)].index;
        if (std::find(hidden.begin(), hidden.end(), row) == hidden.end()) hidden.push_back(row);
        std::sort(hidden.begin(), hidden.end());
        std::string list;
        for (const uint32_t i : hidden) list += (list.empty() ? "" : " ") + std::to_string(i);
        for (const Entity& e : ctx_.doc.map().entities)
            if (e.isWorldspawn()) {
                ctx_.doc.setKeyValue(e.id, "iw8r_hide_smodels", list);
                break;
            }
        stockSel_.reset();
        if (camera_) camera_->setStockPickOutline({});
        rebuildReferenceProps();
        ctx_.documentEdited(QString("Stock prop %1 (row %2) hidden (Ctrl+Z brings it back; File > Stock Map > Rebuild Stock Zone leaves it out)")
                                .arg(sel.model)
                                .arg(row));
        return true;
    }
    if (sel.mesh >= 0 && stockSelBounds_.valid()) {
        AABB b = stockSelBounds_;
        for (int a = 0; a < 3; ++a) {  // a flat surface still gives a box; a little past its faces
            b.mins[a] -= 1.0, b.maxs[a] += 1.0;
        }
        const auto prim = makeBrushPrimitive(makeBox(b, " caulk 128 128 0 0 0 0"), ctx_.doc.eol());
        if (!prim) return false;
        ctx_.doc.addEntity({{"classname", kRemoveWorldClass}}, {*prim}, "Remove Stock Surface");
        stockSel_.reset();
        if (camera_) camera_->setStockPickOutline({});
        ctx_.documentEdited(QString("Stock surface %1 removed: a world removal box %2 x %3 x %4 around it (Ctrl+Z brings it back; the "
                                    "rebuild takes it out of the drawing and the collision)")
                                .arg(sel.material)
                                .arg(qRound(b.maxs.x - b.mins.x)).arg(qRound(b.maxs.y - b.mins.y)).arg(qRound(b.maxs.z - b.mins.z)));
        return true;
    }
    return false;
}

bool MainWindow::editStockPropAsModel(const Ray& ray) {
    const auto hit = pickStockSurface(ray);
    if (!hit || hit->prop < 0 || !refProps_ || size_t(hit->prop) >= refProps_->size()) return false;
    const StockModelInstance in = (*refProps_)[size_t(hit->prop)];
    std::vector<uint32_t> hidden = stockEditHiddenProps(ctx_.doc.map());
    if (std::find(hidden.begin(), hidden.end(), in.index) == hidden.end()) hidden.push_back(in.index);
    std::sort(hidden.begin(), hidden.end());
    std::string list;
    for (const uint32_t i : hidden) list += (list.empty() ? "" : " ") + std::to_string(i);
    auto keys = stockModelInstancesAsEntities(in);
    keys.push_back({"iw8r_stock_prop", std::to_string(in.index)});
    ctx_.doc.beginGroup("Edit Stock Prop As Model");
    for (const Entity& e : ctx_.doc.map().entities)
        if (e.isWorldspawn()) {
            ctx_.doc.setKeyValue(e.id, "iw8r_hide_smodels", list);
            break;
        }
    const Id id = ctx_.doc.addEntity(keys, {}, "Edit Stock Prop As Model");
    ctx_.doc.endGroup();
    ctx_.doc.deselectAll();
    if (id != kNoId) ctx_.doc.select(SelRef{id, kNoId}, false);
    stockSel_.reset();
    rebuildReferenceProps();
    ctx_.documentEdited(QString("Stock prop %1 (row %2) is now a model of the map: move, turn and scale it with the gizmo (Ctrl+Z puts the "
                                "stock prop back). The rebuild hides the placement and adds the model")
                            .arg(qs(in.model))
                            .arg(in.index));
    return true;
}

}  // namespace iw8r::qt
