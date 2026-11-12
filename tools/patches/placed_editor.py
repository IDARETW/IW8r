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
 ('#include "stock_loot.h"\n', '#include "stock_loot.h"\n#include "stock_placed.h"\n'),
 ('''        if (isLootPointClass(e.classname())) continue;  // loot points go to the edits file, never into the text (stock_loot.h)
''', '''        if (isLootPointClass(e.classname())) continue;  // loot points go to the edits file, never into the text (stock_loot.h)
        if (lowerStr(e.classname()) == kPlacedObjectClass) continue;  // placed objects too (stock_placed.h)
'''),
])
patch('src/core/stock_spawns.h', [
 ('''// left out of `spawns`. Loot point entities (stock_loot.h) are in neither: they are written through the edits file.
''', '''// left out of `spawns`. Loot point entities (stock_loot.h) and `scriptable` entities (stock_placed.h) are in neither:
// they are written through the edits file.
'''),
])
patch('src/core/stock_zone_edit.h', [
 ('''// JSON list of lootPointsJson (stock_loot.h); null = the loot points of the map stay as they are.
std::string stockEditJson(const std::string& entitiesPath, const std::vector<uint32_t>& hiddenProps,
                          const std::vector<StockEditCarry>& carry, const std::string* lootPoints = nullptr);
''', '''// JSON list of lootPointsJson (stock_loot.h); null = the loot points of the map stay as they are. `placedObjects` is
// PlacedObjectEdits::json (stock_placed.h); null or empty = the placed objects stay as they are.
std::string stockEditJson(const std::string& entitiesPath, const std::vector<uint32_t>& hiddenProps,
                          const std::vector<StockEditCarry>& carry, const std::string* lootPoints = nullptr,
                          const std::string* placedObjects = nullptr);
'''),
])
patch('src/core/stock_zone_edit.cpp', [
 ('''                          const std::vector<StockEditCarry>& carry, const std::string* lootPoints) {
''', '''                          const std::vector<StockEditCarry>& carry, const std::string* lootPoints, const std::string* placedObjects) {
'''),
 ('''        json += "\\"loot_points\\":" + *lootPoints;
    }
''', '''        json += "\\"loot_points\\":" + *lootPoints;
    }
    if (placedObjects && !placedObjects->empty()) {
        sep();
        json += "\\"scriptable_instances\\":" + *placedObjects;
    }
'''),
])
patch('src/qtapp/main_window.h', [
 ('''    size_t stockLootPointCount() const { return stockLoot_.points.size(); }
''', '''    size_t stockLootPointCount() const { return stockLoot_.points.size(); }
    // 13.5: the placed objects of the open stock map that can be edited (the Warzone buy stations) become
    // `scriptable` entities; Rebuild Stock Zone writes moves and new ones. Returns how many were added; 0 when they
    // already are entities; -1 without a stock map.
    int editPlacedObjects();
    size_t stockPlacedObjectCount() const { return stockPlaced_.size(); }
'''),
 ('''    std::set<std::string> stockModelNames_;  // models the opened stock map's own entities name (never carried)
''', '''    std::set<std::string> stockModelNames_;  // models the opened stock map's own entities name (never carried)
    std::vector<StockPlacedObject> stockPlaced_;  // every scriptable instance of the opened stock map
'''),
 ('#include "stock_loot.h"\n', '#include "stock_loot.h"\n#include "stock_placed.h"\n'),
])
patch('src/qtapp/main_window.cpp', [
 ('''    stockModelNames_.clear();
''', '''    stockModelNames_.clear();
    stockPlaced_.clear();
'''),
 ('''        stockModelNames_ = entityModelNames(ctx_.doc.map());
''', '''        stockModelNames_ = entityModelNames(ctx_.doc.map());
        if (std::string perr; !readStockPlacedObjects(p, stockPlaced_, &perr)) {
            stockPlaced_.clear();
            EditorLog::instance().add(LogLevel::Warning, "stock", "the placed objects could not be read: " + qs(perr));
        }
'''),
 ('''    addCommand(file, "EditLootPoints", "Edit Loot Points (Warzone stock map)", QKeySequence(), [this] { editLootPoints(); });
''', '''    addCommand(file, "EditLootPoints", "Edit Loot Points (Warzone stock map)", QKeySequence(), [this] { editLootPoints(); });
    addCommand(file, "EditPlacedObjects", "Edit Placed Objects (Warzone buy stations)", QKeySequence(), [this] { editPlacedObjects(); });
'''),
])
patch('src/qtapp/main_window_stock_edit.cpp', [
 ('''    const QString editsPath = work + "/edits.json";
    QFile jf(editsPath);
    const std::string json = stockEditJson(QDir::toNativeSeparators(entsPath).toStdString(), hidden, carry, writeLoot ? &loot : nullptr);
''', '''    // 13.5: placed objects (buy stations) that were moved or added as `scriptable` entities.
    const PlacedObjectEdits placed =
        placedObjectEdits(ctx_.doc.map(), stockPlaced_, ScriptableCatalog::shared(QCoreApplication::applicationDirPath().toStdString()));
    if (notes)
        for (const std::string& n : placed.notes) *notes << qs(n);

    const QString editsPath = work + "/edits.json";
    QFile jf(editsPath);
    const std::string json =
        stockEditJson(QDir::toNativeSeparators(entsPath).toStdString(), hidden, carry, writeLoot ? &loot : nullptr, &placed.json);
'''),
 ('''QString MainWindow::stockBackupDir(const QString& zone) const {
''', '''int MainWindow::editPlacedObjects() {
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

QString MainWindow::stockBackupDir(const QString& zone) const {
'''),
 ('#include "stock_loot.h"\n', '#include "scriptable_catalog.h"\n#include "stock_loot.h"\n#include "stock_placed.h"\n'),
])
print('ok')
