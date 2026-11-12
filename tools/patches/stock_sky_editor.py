import io, json


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/core/stock_zone_edit.h', [
 ('''                          const std::string* sun = nullptr);''', '''                          const std::string* sun = nullptr, const std::string* replaceMaterials = nullptr);

// 11.6: another stock map's sky for this stock map (worldspawn iw8r_sky_from "<map id>", e.g. mp_backlot2): the carry
// entry for its sky material (tw/sky_<map id> from techsets_<map id>) and the edits file's "replace_materials" list
// ([{"from":"@sky","to":"tw/sky_<map id>"}]). False (nothing) when the key is unset, not a map id, or this map's own.
bool stockSkyEdit(const MapFile& map, const std::string& thisZone, StockEditCarry& carry, std::string& replaceMaterials,
                  std::vector<std::string>* notes = nullptr);'''),
])
patch('src/core/stock_zone_edit.cpp', [
 ('''                          const std::string* lootBoxes, const std::string* sun) {''', '''                          const std::string* lootBoxes, const std::string* sun, const std::string* replaceMaterials) {'''),
 ('''    if (sun && !sun->empty()) {''', '''    if (replaceMaterials && !replaceMaterials->empty()) {
        sep();
        json += "\\"replace_materials\\":" + *replaceMaterials;
    }
    if (sun && !sun->empty()) {'''),
])
s = io.open('src/core/stock_zone_edit.cpp', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
end = s.rstrip().rfind('}')
add = r'''
bool stockSkyEdit(const MapFile& map, const std::string& thisZone, StockEditCarry& carry, std::string& replaceMaterials,
                  std::vector<std::string>* notes) {
    const Entity* world = nullptr;
    for (const Entity& e : map.entities)
        if (e.isWorldspawn()) {
            world = &e;
            break;
        }
    const std::string* v = world ? world->value("iw8r_sky_from") : nullptr;
    if (!v || v->empty()) return false;
    std::string id;
    for (const char c : *v) id += char(std::tolower(static_cast<unsigned char>(c)));
    const bool valid = id.size() > 3 && id.size() <= 40 && id.rfind("mp_", 0) == 0 &&
                       std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::islower(c) || std::isdigit(c) || c == '_'; });
    if (!valid) {
        if (notes) notes->push_back("iw8r_sky_from '" + *v + "' is not a stock map id (mp_...); the sky stays");
        return false;
    }
    if (id == thisZone) return false;
    carry = {"techsets_" + id, "material", "tw/sky_" + id, ""};
    replaceMaterials = "[{\"from\":\"@sky\",\"to\":\"tw/sky_" + id + "\"}]";
    if (notes) notes->push_back("the sky of " + id + " replaces this map's sky (tw/sky_" + id + " carried from techsets_" + id + ")");
    return true;
}

'''.replace('\n', nl)
s = s[:end] + add.lstrip(nl) + s[end:]
for inc in ('#include <algorithm>', '#include <cctype>'):
    if inc not in s:
        s = s.replace('#include "stock_zone_edit.h"' + nl, '#include "stock_zone_edit.h"' + nl + inc + nl, 1)
io.open('src/core/stock_zone_edit.cpp', 'w', encoding='utf-8', newline='').write(s)

patch('src/qtapp/main_window_stock_edit.cpp', [
 ('''    // 11.6: the stock sun's colour and intensity (worldspawn iw8r_sun_color / iw8r_sun_intensity).''',
  '''    // 11.6: another stock map's sky (worldspawn iw8r_sky_from).
    std::string replaceMaterials;
    {
        std::vector<std::string> skyNotes;
        StockEditCarry skyCarry;
        if (stockSkyEdit(ctx_.doc.map(), stockZone_.toStdString(), skyCarry, replaceMaterials, &skyNotes)) carry.push_back(skyCarry);
        if (notes)
            for (const std::string& n : skyNotes) *notes << qs(n);
    }

    // 11.6: the stock sun's colour and intensity (worldspawn iw8r_sun_color / iw8r_sun_intensity).'''),
 ('''                                           &placed.json, &stockPatches, &removeCollision, &lootBoxes, &sun);''',
  '''                                           &placed.json, &stockPatches, &removeCollision, &lootBoxes, &sun, &replaceMaterials);'''),
])

patch('src/qtapp/main_window.cpp', [
 ('''    addCommand(file, "RelightStockSun", "Relight Stock Map Sun (Colour / Intensity)...", QKeySequence(), [this] {''',
  '''    addCommand(file, "RelightStockSky", "Relight Stock Map Sky (another map's sky)...", QKeySequence(), [this] {
        // 11.6: worldspawn iw8r_sky_from; File > Rebuild Stock Zone carries that map's sky material and puts it on this
        // map's sky surfaces (converter replace_materials "@sky").
        const Entity* world = nullptr;
        for (const Entity& e : ctx_.doc.map().entities)
            if (e.isWorldspawn()) {
                world = &e;
                break;
            }
        if (!world || stockZone_.isEmpty()) return ctx_.setStatus("Relight Sky: open a stock map first (File > Open IW8 Stock Map)");
        QStringList ids;
        {
            QDir d(QCoreApplication::applicationDirPath());
            for (int up = 0; up < 4 && !QFileInfo::exists(d.filePath("data/asset_catalog/iw8_stock_maps.tsv")); ++up) d.cdUp();
            QFile f(d.filePath("data/asset_catalog/iw8_stock_maps.tsv"));
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&f);
                while (!in.atEnd()) {
                    const QStringList c = in.readLine().split('\\t');
                    if (c.size() < 7 || c[0].startsWith('#') || c[0] == "map_id" || c[6] != "yes" || c[0] == stockZone_) continue;
                    ids << c[0];
                }
            }
        }
        ids.prepend("(this map's own sky)");
        bool ok = false;
        const QString pick = QInputDialog::getItem(this, "Relight Stock Map Sky", "Use the sky of:", ids, 0, false, &ok);
        if (!ok) return;
        if (pick.startsWith("(")) ctx_.doc.removeKey(world->id, "iw8r_sky_from");
        else ctx_.doc.setKeyValue(world->id, "iw8r_sky_from", pick.toStdString());
        ctx_.documentEdited(pick.startsWith("(") ? QString("The map keeps its own sky")
                                                 : "The sky of " + pick + " is used; File > Rebuild Stock Zone writes it (the sky's lighting stays)");
    });
    addCommand(file, "RelightStockSun", "Relight Stock Map Sun (Colour / Intensity)...", QKeySequence(), [this] {'''),
])

p = 'data/iw8_entity_defs_overrides.json'
d = json.loads(io.open(p, encoding='utf-8').read())
d['classes']['worldspawn']['keys']['iw8r_sky_from'] = {
    'type': 'string', 'default': '', 'required': False, 'examples': ['mp_backlot2'],
    'description': "Stock maps (11.6): use another stock map's sky (File > Relight Stock Map Sky). Rebuild Stock Zone carries "
                   "its sky material (tw/sky_<map>) and puts it on this map's sky surfaces. The baked lighting stays. Not seen in game."}
io.open(p, 'w', encoding='utf-8', newline='\n').write(json.dumps(d, indent=2, ensure_ascii=False) + '\n')
print('ok')
