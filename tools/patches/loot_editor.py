import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/core/stock_spawns.cpp', [
 ('#include "stock_spawns.h"\n', '#include "stock_spawns.h"\n\n#include "stock_loot.h"\n'),
 ('''    for (const Entity& e : map.entities) {
        if (!isSpawnListClass(e.classname())) {
''', '''    for (const Entity& e : map.entities) {
        if (isLootPointClass(e.classname())) continue;  // loot points go to the edits file, never into the text (stock_loot.h)
        if (!isSpawnListClass(e.classname())) {
'''),
])
patch('src/core/stock_spawns.h', [
 ('''// left out of `spawns`.
''', '''// left out of `spawns`. Loot point entities (stock_loot.h) are in neither: they are written through the edits file.
'''),
])
patch('src/core/stock_zone_edit.h', [
 ('''// The edits file. `entitiesPath` is the entity text file ("" = the entities are not replaced).
std::string stockEditJson(const std::string& entitiesPath, const std::vector<uint32_t>& hiddenProps,
                          const std::vector<StockEditCarry>& carry);
''', '''// The edits file. `entitiesPath` is the entity text file ("" = the entities are not replaced). `lootPoints` is the
// JSON list of lootPointsJson (stock_loot.h); null = the loot points of the map stay as they are.
std::string stockEditJson(const std::string& entitiesPath, const std::vector<uint32_t>& hiddenProps,
                          const std::vector<StockEditCarry>& carry, const std::string* lootPoints = nullptr);
'''),
])
patch('src/core/stock_zone_edit.cpp', [
 ('''std::string stockEditJson(const std::string& entitiesPath, const std::vector<uint32_t>& hiddenProps,
                          const std::vector<StockEditCarry>& carry) {
''', '''std::string stockEditJson(const std::string& entitiesPath, const std::vector<uint32_t>& hiddenProps,
                          const std::vector<StockEditCarry>& carry, const std::string* lootPoints) {
'''),
 ('''        json += "]";
    }
    return json + "}";
''', '''        json += "]";
    }
    if (lootPoints) {
        sep();
        json += "\\"loot_points\\":" + *lootPoints;
    }
    return json + "}";
'''),
])
patch('src/qtapp/main_window.h', [
 ('''    bool editStockSpawns();
''', '''    bool editStockSpawns();
    // 13.5: the loot points of the open Warzone stock map become entities (class iw8r_loot_point); Rebuild Stock Zone
    // writes them back. Returns how many were added; 0 when they already are entities; -1 without a stock map.
    int editLootPoints();
    size_t stockLootPointCount() const { return stockLoot_.points.size(); }
'''),
 ('''    bool stockSpawnsEditable_ = false;  // spawns were turned into document entities (Edit Compiled Spawns)
''', '''    bool stockSpawnsEditable_ = false;  // spawns were turned into document entities (Edit Compiled Spawns)
    StockLoot stockLoot_;               // the loot block of the opened stock map (empty: none)
    bool stockLootEditable_ = false;    // loot points were turned into document entities (Edit Loot Points)
'''),
])
patch('src/qtapp/main_window.cpp', [
 ('''    stockZone_.clear();  // openStockZone sets it again after loading its dump
    stockSpawnsEditable_ = false;
''', '''    stockZone_.clear();  // openStockZone sets it again after loading its dump
    stockSpawnsEditable_ = false;
    stockLoot_ = StockLoot{};
    stockLootEditable_ = false;
'''),
 ('''        ctx_.scene.spawns = std::move(dump.spawns);
        displayName_ = qs(dump.assetName) + " (IW8 entity string - Save As to write)";
        ctx_.setStatus(QString("Opened IW8 MapEnts: %1 entities, %2 compiled spawns (read-only overlay)")
                           .arg(ctx_.doc.map().entities.size())
                           .arg(ctx_.scene.spawns.size()));
''', '''        ctx_.scene.spawns = std::move(dump.spawns);
        // 13.5: the loot points of a Warzone map (File > Edit Loot Points makes them entities).
        if (std::string lerr; !readStockLoot(p, strings, stockLoot_, &lerr)) {
            stockLoot_ = StockLoot{};
            EditorLog::instance().add(LogLevel::Warning, "stock", "the loot points could not be read: " + qs(lerr));
        }
        displayName_ = qs(dump.assetName) + " (IW8 entity string - Save As to write)";
        ctx_.setStatus(QString("Opened IW8 MapEnts: %1 entities, %2 compiled spawns (read-only overlay)%3")
                           .arg(ctx_.doc.map().entities.size())
                           .arg(ctx_.scene.spawns.size())
                           .arg(stockLoot_.points.empty() ? QString() : QString(", %1 loot points (File > Edit Loot Points)").arg(stockLoot_.points.size())));
'''),
 ('''    addCommand(file, "EditStockSpawns", "Edit Compiled Spawns (stock map)", QKeySequence(), [this] { editStockSpawns(); });
''', '''    addCommand(file, "EditStockSpawns", "Edit Compiled Spawns (stock map)", QKeySequence(), [this] { editStockSpawns(); });
    addCommand(file, "EditLootPoints", "Edit Loot Points (Warzone stock map)", QKeySequence(), [this] { editLootPoints(); });
'''),
])
patch('src/qtapp/main_window_stock_edit.cpp', [
 ('''    const QString editsPath = work + "/edits.json";
    QFile jf(editsPath);
    const std::string json = stockEditJson(QDir::toNativeSeparators(entsPath).toStdString(), hidden, carry);
''', '''    // 13.5: the loot points, when they were made entities (or the document holds some): the whole list replaces the
    // map's. Without that the map keeps its loot points.
    std::string loot;
    const bool writeLoot = stockLootEditable_ || hasLootPoints(ctx_.doc.map());
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

    const QString editsPath = work + "/edits.json";
    QFile jf(editsPath);
    const std::string json = stockEditJson(QDir::toNativeSeparators(entsPath).toStdString(), hidden, carry, writeLoot ? &loot : nullptr);
'''),
 ('''QString MainWindow::stockBackupDir(const QString& zone) const {
''', '''int MainWindow::editLootPoints() {
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

QString MainWindow::stockBackupDir(const QString& zone) const {
'''),
 ('#include "spawnlist.h"\n', '#include "spawnlist.h"\n#include "stock_loot.h"\n'),
])
print('ok')
