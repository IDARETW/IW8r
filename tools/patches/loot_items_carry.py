import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/core/stock_loot.h', [
 ('''#include <cstdint>
''', '''#include <cstdint>
#include <map>
'''),
 ('''// The JSON list for the edits file: [{"origin":[x,y,z],"angles":[p,y,r],"cluster":...,"zone":...}, ...].
std::string lootPointsJson(const std::vector<LootPointEdit>& points);''', '''// The JSON list for the edits file: [{"origin":[x,y,z],"angles":[p,y,r],"cluster":...,"zone":...}, ...].
std::string lootPointsJson(const std::vector<LootPointEdit>& points);

// The loot item tables (data/asset_catalog/iw8_br_loot_items.tsv, tools/build_br_loot_items.py): the brloot_*
// scriptables each stock loot item table (code_post_gfx mp/loot_item_defs*.csv) spawns on loot points. A custom map
// never loads common_br_mp, which holds them (research 93), so a build with loot points carries the items of the
// map's table (dvar loot_table_name; default kDefaultLootItemTable).
inline constexpr const char* kDefaultLootItemTable = "mp/loot_item_defs.csv";
class BrLootItemTables {
public:
    bool parse(const std::string& text);  // "table<TAB>scriptable" lines; '#' lines are comments
    bool load(const std::string& path, std::string* err = nullptr);
    // The items of a table (name as the dvar gives it, case-insensitive, "mp/" optional); nullptr when unknown.
    const std::vector<std::string>* items(const std::string& table) const;
    size_t size() const { return tables_.size(); }

private:
    std::map<std::string, std::vector<std::string>> tables_;
};'''),
])

patch('src/core/stock_loot.cpp', [
 ('''bool isLootBoxClass(const std::string& classname) {''', '''bool BrLootItemTables::parse(const std::string& text) {
    tables_.clear();
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t tab = line.find('\\t');
        if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size()) continue;
        std::string table = line.substr(0, tab);
        for (char& c : table) c = char(std::tolower(static_cast<unsigned char>(c)));
        auto& list = tables_[table];
        const std::string item = line.substr(tab + 1);
        if (std::find(list.begin(), list.end(), item) == list.end()) list.push_back(item);
    }
    return !tables_.empty();
}

bool BrLootItemTables::load(const std::string& path, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    if (parse(ss.str())) return true;
    if (err) *err = "no loot item table in " + path;
    return false;
}

const std::vector<std::string>* BrLootItemTables::items(const std::string& table) const {
    std::string key;
    for (const char c : table) key += char(std::tolower(static_cast<unsigned char>(c)));
    if (key.rfind("mp/", 0) != 0) key = "mp/" + key;
    const auto it = tables_.find(key);
    return it == tables_.end() ? nullptr : &it->second;
}

bool isLootBoxClass(const std::string& classname) {'''),
 ('''#include <cctype>
''', '''#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
'''),
])

patch('src/core/compile_iw8.h', [
 ('''    const VehicleCatalog* vehicles = nullptr;''', '''    const VehicleCatalog* vehicles = nullptr;
    // The loot item tables (stock_loot.h BrLootItemTables): a map with loot points carries the brloot_* definitions
    // of its loot table from common_br_mp (a custom map never loads that zone, research 93). Needs carryScriptables.
    const BrLootItemTables* lootItems = nullptr;'''),
])

patch('src/core/compile_iw8.cpp', [
 ('''    std::string lootJson, lootZone, lootBoxes;
''', '''    std::string lootJson, lootZone, lootBoxes;
    std::vector<StockEditCarry> lootCarry;  // the loot item definitions of the map's loot table (research 93)
'''),
 ('''        // Authored loot clusters and zones (iw8r_loot_cluster / iw8r_loot_zone brush entities, research 89 section
        // 6d): with zones, each point takes the first box that holds it and iw8r_loot_zone only names unnamed zones.
        if (lootCount) {''', '''        // The loot item definitions (research 93): the loot system spawns brloot_* scriptables of common_br_mp on the
        // points, and a custom map never loads that zone, so the items of the map's loot table are carried from it.
        if (lootCount) {
            std::string table = kDefaultLootItemTable;
            if (worldspawn)
                if (const std::string* dv = worldspawn->value("iw8r_mode_dvars"))
                    for (const auto& [name, value] : parseModeDvars(*dv))
                        if (lower(name) == "loot_table_name") table = lower(value);
            if (!opt.lootItems) {
                warn.push_back("loot points: the loot item table catalogue (data/asset_catalog/iw8_br_loot_items.tsv) was not found, so no "
                               "loot item is carried; a custom map does not load the Warzone zone that holds them (research 93)");
            } else if (!opt.carryScriptables) {
                warn.push_back("loot points: the loot items live in the Warzone zone (common_br_mp), which a custom map does not load "
                               "(research 93); turn on Edit > Preferences > Real stock scripted objects to carry them, or the points stay empty");
            } else if (const std::vector<std::string>* items = opt.lootItems->items(table)) {
                size_t skipped = 0;
                for (const std::string& item : *items) {
                    const ScriptableDefinition* def = opt.scriptables ? opt.scriptables->find(item) : nullptr;
                    if (def && def->brZone()) lootCarry.push_back({"common_br_mp", "scriptable", item, ""});
                    else ++skipped;
                }
                warn.push_back(std::to_string(lootCarry.size()) + " loot item definition(s) of " + table +
                               " carried from common_br_mp (a custom map does not load it, research 93)" +
                               (skipped ? "; " + std::to_string(skipped) + " item(s) of the table are not in that zone and are left out" : std::string()) +
                               "; not seen in game");
            } else {
                warn.push_back("loot points: loot_table_name '" + table + "' is not one of the stock loot item tables; no loot item is carried");
            }
        }
        // Authored loot clusters and zones (iw8r_loot_cluster / iw8r_loot_zone brush entities, research 89 section
        // 6d): with zones, each point takes the first box that holds it and iw8r_loot_zone only names unnamed zones.
        if (lootCount) {'''),
 ('''        const std::vector<StockEditCarry> carry =
            opt.vehicles ? vehicleCarryList(map, *opt.vehicles, opt.stockModelZones, &notes, &placed) : std::vector<StockEditCarry>{};''',
  '''        std::vector<StockEditCarry> carry =
            opt.vehicles ? vehicleCarryList(map, *opt.vehicles, opt.stockModelZones, &notes, &placed) : std::vector<StockEditCarry>{};'''),
 ('''        if (!carry.empty()) carryAssets = carryAssetsJson(carry);''', '''        carry.insert(carry.end(), lootCarry.begin(), lootCarry.end());
        if (!carry.empty()) carryAssets = carryAssetsJson(carry);'''),
])

patch('src/qtapp/compile_panel.cpp', [
 ('''        static VehicleCatalog vehicleCatalog;
        static StockModelZones modelZones;''', '''        static VehicleCatalog vehicleCatalog;
        static StockModelZones modelZones;
        static BrLootItemTables lootItemTables;'''),
 ('''                    modelZones.load(d.filePath("data/asset_catalog/iw8_models.tsv").toStdString());
                    break;''', '''                    modelZones.load(d.filePath("data/asset_catalog/iw8_models.tsv").toStdString());
                    lootItemTables.load(d.filePath("data/asset_catalog/iw8_br_loot_items.tsv").toStdString());
                    break;'''),
 ('''        if (modelZones.size()) opt.stockModelZones = &modelZones;''', '''        if (modelZones.size()) opt.stockModelZones = &modelZones;
        if (lootItemTables.size()) opt.lootItems = &lootItemTables;'''),
])
print('ok')
