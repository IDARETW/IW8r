// MainWindow: rebuilding the open stock map through the converter's editzone command (roadmap 11.2 / 11.4 / 11.5,
// docs/research/88_stock_map_editing.md). Kept out of main_window.cpp, which is at the compiler's section limit.
#include "main_window.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>

#include "editor_log.h"
#include "spawnlist.h"
#include "scriptable_catalog.h"
#include "stock_loot.h"
#include "stock_placed.h"
#include "stock_install.h"
#include "stock_spawns.h"
#include "stock_zone_edit.h"

namespace iw8r::qt {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

// Writes <work>/edit.ents (the entity text, with the spawn entities when the compiled spawns were made editable) and
// <work>/edits.json, and gives the converter call. The Replay install is only read by that call.
bool MainWindow::prepareStockEdit(const QString& work, QString* program, QStringList* args, QString* err, QStringList* notes) {
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
    const std::vector<StockEditCarry> carry =
        catalogue.size() ? stockEditCarryList(ctx_.doc.map(), stockZone_.toStdString(), catalogue, &carryNotes, &stockModelNames_)
                         : std::vector<StockEditCarry>{};
    if (notes) {
        for (const std::string& n : carryNotes) *notes << qs(n);
        for (const StockEditCarry& c : carry) *notes << QString("model %1 is carried from %2").arg(qs(c.name), qs(c.zone));
        if (!catalogue.size()) *notes << "data/asset_catalog/iw8_models.tsv was not found: models added from other maps are not carried";
    }
    const std::vector<uint32_t> hidden = stockEditHiddenProps(ctx_.doc.map());
    if (notes && !hidden.empty()) *notes << QString("%1 stock prop(s) are hidden (scale 0; their collision stays)").arg(hidden.size());

    // 13.5: the loot points, when the document holds some (File > Edit Loot Points): the whole list replaces the map's.
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

    // 13.5: placed objects (buy stations) that were moved or added as `scriptable` entities.
    const PlacedObjectEdits placed =
        placedObjectEdits(ctx_.doc.map(), stockPlaced_, ScriptableCatalog::shared(QCoreApplication::applicationDirPath().toStdString()));
    if (notes)
        for (const std::string& n : placed.notes) *notes << qs(n);

    // Edits that are installed in the game replaced the game's own patch files, which carry real map data: the
    // converter reads those zones with the stock patch saved at install, so this rebuild starts from the stock map.
    const std::vector<StockPatchOverride> stockPatches = installedStockPatchOverrides(
        (QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/stock_backup").toStdString());
    if (notes && !stockPatches.empty()) {
        QStringList names;
        for (const StockPatchOverride& o : stockPatches) names << qs(o.name);
        *notes << QString("installed edit(s) in the game: %1. This rebuild starts from the stock map (the stock patch files saved at install)")
                      .arg(names.join(", "));
    }

    const QString editsPath = work + "/edits.json";
    QFile jf(editsPath);
    const std::string json = stockEditJson(QDir::toNativeSeparators(entsPath).toStdString(), hidden, carry, writeLoot ? &loot : nullptr,
                                           &placed.json, &stockPatches);
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

// File > Rebuild Stock Zone: runs the converter in the background, then the ACTS loader test on what it wrote. The
// result stays in the work folder; installing it over the stock file is the owner's step.
void MainWindow::rebuildStockZoneFast() {
    const QString work = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/stock_rebuild/" + stockZone_ + "-" +
                         QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    QString program, err;
    QStringList args, notes;
    if (!prepareStockEdit(work, &program, &args, &err, &notes)) {
        QMessageBox::warning(this, "Rebuild Stock Zone", err);
        return;
    }
    for (const QString& n : notes) EditorLog::instance().add(LogLevel::Info, "stock", n);
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
                                                  "/out (not installed; File > Install Edited Stock Map Into Game)"
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
        if (!def || !editablePlacedDefinition(*def)) continue;
        ctx_.doc.addEntity(placedObjectEntityKeys(o), {}, "Placed object " + o.definition);
        ++n;
    }
    ctx_.doc.endGroup();
    if (n == 0) {
        ctx_.setStatus(QString("Edit Placed Objects: none of the %1 placed objects of this map can be edited (only objects without physics "
                               "entries of their own, such as the Warzone buy station)").arg(stockPlaced_.size()));
        return 0;
    }
    ctx_.documentEdited(QString("%1 of %2 placed objects are now entities (class scriptable): move them, or copy one for a new object; "
                                "Rebuild Stock Zone writes it. They cannot be removed").arg(n).arg(stockPlaced_.size()));
    return int(n);
}

QString MainWindow::stockZoneForReading(const QString& zone, QString* tempDir, QString* err) {
    if (tempDir) tempDir->clear();
    const QString name = QFileInfo(zone).completeBaseName();
    const QString backup = stockBackupDir(name);
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
                        " to read it (disk space?). File > Restore Stock Map In Game, then open the map again.";
        return {};
    }
    if (tempDir) *tempDir = view;
    EditorLog::instance().add(LogLevel::Info, "stock", "an edit of " + name + " is installed in the game: the stock map is read from a copy with "
                                                           "the stock patch saved at install");
    return copy;
}

QString MainWindow::stockBackupDir(const QString& zone) const {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/stock_backup/" + zone;
}

// The edited map goes into the game as patch files (<zone>.fp beside the stock <zone>.ff, which the game applies when
// it loads the zone): the stock zones are not changed. Only this action and Restore write to the game folder, and
// only after the owner confirms the list.
void MainWindow::installEditedStockMap() {
    if (lastStockRebuild_.isEmpty()) {
        QMessageBox::information(this, "Install Edited Stock Map",
                                 "Rebuild the stock map first (File > Rebuild Stock Zone). The install uses the last rebuild that passed its check.");
        return;
    }
    const QString replay = toolPath("replay");
    const QString zoneDir = QFileInfo(replay).absolutePath() + "/zone";
    const QString patchDir = lastStockRebuild_ + "/out/patch";
    const std::vector<std::string> files = stockPatchFiles(patchDir.toStdString());
    if (replay.isEmpty() || !QFileInfo(zoneDir).isDir() || files.empty()) {
        QMessageBox::warning(this, "Install Edited Stock Map",
                             files.empty() ? "The last rebuild wrote no patch files (" + patchDir + "). An old converter writes none: update it."
                                           : "The game folder is not set (Tools > Tool Paths).");
        return;
    }
    QStringList names;
    for (const std::string& f : files) names << qs(f);
    const QString backup = stockBackupDir(lastStockRebuildZone_);
    if (QMessageBox::question(this, "Install Edited Stock Map",
                              QString("Copy %1 patch file(s) into the game?\n\n%2\n\nTo: %3\n\nThe stock zone files are not changed: the game "
                                      "applies a patch beside its zone. A stock patch of the same name is saved first in\n%4\n\n"
                                      "Close the game before you continue. File > Restore Stock Map In Game undoes this.")
                                  .arg(names.size()).arg(names.join("\n"), QDir::toNativeSeparators(zoneDir), QDir::toNativeSeparators(backup)),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    StockInstallReport report;
    std::string err;
    const bool ok = installStockPatches(patchDir.toStdString(), zoneDir.toStdString(), backup.toStdString(), report, &err);
    for (const std::string& f : report.backedUp) EditorLog::instance().add(LogLevel::Info, "stock", "saved the stock " + qs(f) + " in " + backup);
    for (const std::string& f : report.installed) EditorLog::instance().add(LogLevel::Info, "stock", "installed " + qs(f) + " into " + zoneDir);
    if (!ok) {
        QMessageBox::warning(this, "Install Edited Stock Map", qs(err));
        ctx_.setStatus("Install Edited Stock Map failed: " + qs(err));
        return;
    }
    ctx_.setStatus(QString("Installed the edited %1 (%2 patch file(s)); File > Restore Stock Map In Game takes it out")
                       .arg(lastStockRebuildZone_).arg(report.installed.size()));
}

void MainWindow::restoreStockMap() {
    const QString zone = !stockZone_.isEmpty() ? stockZone_ : lastStockRebuildZone_;
    if (zone.isEmpty()) {
        QMessageBox::information(this, "Restore Stock Map", "Open the stock map whose edit you want to take out (File > Open IW8 Stock Map).");
        return;
    }
    const QString backup = stockBackupDir(zone);
    const std::vector<std::string> installed = installedStockPatches(backup.toStdString());
    if (installed.empty()) {
        QMessageBox::information(this, "Restore Stock Map", "No edit of " + zone + " is installed by this editor.");
        return;
    }
    const QString zoneDir = QFileInfo(toolPath("replay")).absolutePath() + "/zone";
    QStringList names;
    for (const std::string& f : installed) names << qs(f);
    if (QMessageBox::question(this, "Restore Stock Map",
                              QString("Take the edit of %1 out of the game?\n\n%2\n\nFrom: %3\n\nA stock patch that was saved at install "
                                      "is put back. Close the game before you continue.")
                                  .arg(zone, names.join("\n"), QDir::toNativeSeparators(zoneDir)),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    StockInstallReport report;
    std::string err;
    const bool ok = restoreStockPatches(backup.toStdString(), zoneDir.toStdString(), report, &err);
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
    ctx_.documentEdited(QString("%1 stock prop(s) hidden (%2 in all): File > Rebuild Stock Zone leaves them out; their collision stays")
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

}  // namespace iw8r::qt
