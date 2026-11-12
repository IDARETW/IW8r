import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/core/stock_install.h', [
 ('''// Names that an install has put into the zone folder and that are still marked (empty: nothing is installed).
std::vector<std::string> installedStockPatches(const std::string& backupDir);
''', '''// Names that an install has put into the zone folder and that are still marked (empty: nothing is installed).
std::vector<std::string> installedStockPatches(const std::string& backupDir);

// The game ships patch files with real map data (mp_br_quarry.fp, mp_donetsk.fp, ...; a map zone without its patch is
// not even readable as the map). An installed edit replaces the patch of its zone, so whoever reads the "stock" map
// from the game folder afterwards would read the edit. This lists, for every edit installed by this editor, the patch
// file name and the stock patch saved at install ("" when the stock game has no patch of that name): the converter
// reads the zone with that file (editzone "stock_patches").
struct StockPatchOverride {
    std::string name;       // <zone>.fp, as in the game's zone folder
    std::string stockFile;  // the saved stock patch, or ""
    bool operator==(const StockPatchOverride& o) const { return name == o.name && stockFile == o.stockFile; }
};
// `backupRoot` holds one backup folder per zone (<backupRoot>/<zone>/). Sorted by name.
std::vector<StockPatchOverride> installedStockPatchOverrides(const std::string& backupRoot);
'''),
])
patch('src/core/stock_install.cpp', [
 ('''std::vector<std::string> installedStockPatches(const std::string& backupDir) {
''', '''std::vector<StockPatchOverride> installedStockPatchOverrides(const std::string& backupRoot) {
    std::vector<StockPatchOverride> out;
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(backupRoot), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        for (const std::string& name : installedStockPatches(it->path().string())) {
            const fs::path saved = it->path() / (name + kStock);
            std::error_code sec;
            out.push_back({name, fs::is_regular_file(saved, sec) ? saved.string() : std::string()});
        }
    }
    std::sort(out.begin(), out.end(), [](const StockPatchOverride& a, const StockPatchOverride& b) { return a.name < b.name; });
    return out;
}

std::vector<std::string> installedStockPatches(const std::string& backupDir) {
'''),
])
patch('src/core/stock_zone_edit.h', [
 ('''std::string stockEditJson(const std::string& entitiesPath, const std::vector<uint32_t>& hiddenProps,
                          const std::vector<StockEditCarry>& carry, const std::string* lootPoints = nullptr,
                          const std::string* placedObjects = nullptr);
''', '''// `stockPatches` (stock_install.h): the saved stock patches of the edits that are installed in the game; the
// converter then reads those zones as the stock game has them, not as the installed edit left them.
std::string stockEditJson(const std::string& entitiesPath, const std::vector<uint32_t>& hiddenProps,
                          const std::vector<StockEditCarry>& carry, const std::string* lootPoints = nullptr,
                          const std::string* placedObjects = nullptr, const std::vector<StockPatchOverride>* stockPatches = nullptr);
'''),
 ('#include "map_text.h"\n', '#include "map_text.h"\n#include "stock_install.h"\n'),
])
patch('src/core/stock_zone_edit.cpp', [
 ('''                          const std::vector<StockEditCarry>& carry, const std::string* lootPoints, const std::string* placedObjects) {
''', '''                          const std::vector<StockEditCarry>& carry, const std::string* lootPoints, const std::string* placedObjects,
                          const std::vector<StockPatchOverride>* stockPatches) {
'''),
 ('''        json += "\\"scriptable_instances\\":" + *placedObjects;
    }
''', '''        json += "\\"scriptable_instances\\":" + *placedObjects;
    }
    if (stockPatches && !stockPatches->empty()) {
        sep();
        json += "\\"stock_patches\\":{";
        for (size_t i = 0; i < stockPatches->size(); ++i)
            json += std::string(i ? "," : "") + jsonText((*stockPatches)[i].name) + ":" + jsonText((*stockPatches)[i].stockFile);
        json += "}";
    }
'''),
])
patch('src/qtapp/main_window_stock_edit.cpp', [
 ('''    const QString editsPath = work + "/edits.json";
    QFile jf(editsPath);
    const std::string json =
        stockEditJson(QDir::toNativeSeparators(entsPath).toStdString(), hidden, carry, writeLoot ? &loot : nullptr, &placed.json);
''', '''    // Edits that are installed in the game replaced the game's own patch files, which carry real map data: the
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
'''),
])
print('ok')
