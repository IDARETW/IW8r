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
 ('''bool stockSkyEdit(const MapFile& map, const std::string& thisZone, StockEditCarry& carry, std::string& replaceMaterials,
                  std::vector<std::string>* notes = nullptr);''', '''bool stockSkyEdit(const MapFile& map, const std::string& thisZone, StockEditCarry& carry, std::string& replaceMaterials,
                  std::vector<std::string>* notes = nullptr);

// Retexturing a stock map (worldspawn iw8r_stock_retexture "from=to; from=to"): every world surface of material `from`
// gets material `to`. `zoneOf` names the stock zone the catalogue lists for a material ("" when unknown); a material
// of a map zone is carried from its techsets companion (techsets_<zone>), one listed in a techsets zone from that zone.
// Appends the carry entries and the replace_materials entries (JSON objects, no brackets) and returns how many pairs
// were taken; a pair whose material is unknown or held only by an always-loaded zone is left out with a note.
size_t stockRetextureEdits(const MapFile& map, const std::function<std::string(const std::string&)>& zoneOf,
                           std::vector<StockEditCarry>& carry, std::vector<std::string>& replaceEntries,
                           std::vector<std::string>* notes = nullptr);'''),
])
s = io.open('src/core/stock_zone_edit.h', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if '#include <functional>' not in s:
    i = s.index('#include <')
    s = s[:i] + '#include <functional>' + nl + s[i:]
io.open('src/core/stock_zone_edit.h', 'w', encoding='utf-8', newline='').write(s)

s = io.open('src/core/stock_zone_edit.cpp', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
end = s.rstrip().rfind('}')
add = r'''
size_t stockRetextureEdits(const MapFile& map, const std::function<std::string(const std::string&)>& zoneOf,
                           std::vector<StockEditCarry>& carry, std::vector<std::string>& replaceEntries, std::vector<std::string>* notes) {
    const Entity* world = nullptr;
    for (const Entity& e : map.entities)
        if (e.isWorldspawn()) {
            world = &e;
            break;
        }
    const std::string* v = world ? world->value("iw8r_stock_retexture") : nullptr;
    if (!v || v->empty()) return 0;
    const auto trim = [](std::string t) {
        const auto a = t.find_first_not_of(" \t"), b = t.find_last_not_of(" \t");
        return a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
    };
    const auto validName = [](const std::string& n) {
        return !n.empty() && n.size() < 128 && std::all_of(n.begin(), n.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '_' || c == '/' || c == '-' || c == '.' || c == '~' || c == '&' || c == '#';
        });
    };
    size_t taken = 0;
    size_t at = 0;
    while (at <= v->size()) {
        const size_t semi = v->find(';', at);
        const std::string part = trim(v->substr(at, semi == std::string::npos ? std::string::npos : semi - at));
        at = semi == std::string::npos ? v->size() + 1 : semi + 1;
        if (part.empty()) continue;
        const size_t eq = part.find('=');
        const std::string from = eq == std::string::npos ? std::string() : trim(part.substr(0, eq));
        const std::string to = eq == std::string::npos ? std::string() : trim(part.substr(eq + 1));
        if (!validName(from) || !validName(to)) {
            if (notes) notes->push_back("iw8r_stock_retexture '" + part + "' is not 'from=to' (material names); left out");
            continue;
        }
        const std::string zone = zoneOf ? zoneOf(to) : std::string();
        std::string source;
        if (zone.rfind("techsets_", 0) == 0) source = zone;
        else if (zone.rfind("mp_", 0) == 0) source = "techsets_" + zone;
        if (source.empty()) {
            if (notes)
                notes->push_back("material '" + to + "' is " + (zone.empty() ? std::string("not in the material catalogue") : "held by " + zone + " only") +
                                 ": it cannot be carried into a stock map yet; '" + from + "' stays");
            continue;
        }
        carry.push_back({source, "material", to, ""});
        replaceEntries.push_back("{\"from\":\"" + from + "\",\"to\":\"" + to + "\"}");
        ++taken;
    }
    if (notes && taken) notes->push_back(std::to_string(taken) + " stock material(s) replaced on the world surfaces (iw8r_stock_retexture)");
    return taken;
}

'''.replace('\n', nl)
s = s[:end] + add.lstrip(nl) + s[end:]
io.open('src/core/stock_zone_edit.cpp', 'w', encoding='utf-8', newline='').write(s)

patch('src/qtapp/main_window_stock_edit.cpp', [
 ('''        if (stockSkyEdit(ctx_.doc.map(), stockZone_.toStdString(), skyCarry, replaceMaterials, &skyNotes)) carry.push_back(skyCarry);''',
  '''        if (stockSkyEdit(ctx_.doc.map(), stockZone_.toStdString(), skyCarry, replaceMaterials, &skyNotes)) carry.push_back(skyCarry);
        // Retexturing (worldspawn iw8r_stock_retexture): the catalogue says which stock zone holds each new material.
        static std::map<std::string, std::string> catalogue;
        if (catalogue.empty()) {
            QDir d(QCoreApplication::applicationDirPath());
            for (int up = 0; up < 4 && !QFileInfo::exists(d.filePath("data/asset_catalog/iw8_materials.tsv")); ++up) d.cdUp();
            QFile f(d.filePath("data/asset_catalog/iw8_materials.tsv"));
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&f);
                while (!in.atEnd()) {
                    const QString line = in.readLine();
                    const int tab = line.indexOf('\\t');
                    if (tab <= 0 || line.startsWith('#')) continue;
                    catalogue.emplace(line.left(tab).toStdString(), line.mid(tab + 1).section(',', 0, 0).trimmed().toStdString());
                }
            }
        }
        std::vector<std::string> entries;
        stockRetextureEdits(
            ctx_.doc.map(),
            [](const std::string& m) {
                const auto it = catalogue.find(m);
                return it == catalogue.end() ? std::string() : it->second;
            },
            carry, entries, &skyNotes);
        if (!entries.empty()) {
            std::string list = replaceMaterials.empty() ? std::string("[") : replaceMaterials.substr(0, replaceMaterials.size() - 1) + ",";
            for (size_t i = 0; i < entries.size(); ++i) list += (i ? "," : "") + entries[i];
            replaceMaterials = list + "]";
        }'''),
])
s = io.open('src/qtapp/main_window_stock_edit.cpp', encoding='utf-8', newline='').read()
for inc in ('#include <QTextStream>', '#include <map>'):
    if inc not in s:
        nl = '\r\n' if '\r\n' in s else '\n'
        s = s.replace('#include "main_window.h"' + nl, '#include "main_window.h"' + nl + inc + nl, 1)
io.open('src/qtapp/main_window_stock_edit.cpp', 'w', encoding='utf-8', newline='').write(s)

p = 'data/iw8_entity_defs_overrides.json'
d = json.loads(io.open(p, encoding='utf-8').read())
d['classes']['worldspawn']['keys']['iw8r_stock_retexture'] = {
    'type': 'string', 'default': '', 'required': False, 'examples': ['tm2co/me_curb_stone_01=tm2co/me_plaster_barrier_fence_wall'],
    'description': "Stock maps: 'from=to; from=to' material swaps on the world surfaces. Rebuild Stock Zone carries each new "
                   "material from the zone the material catalogue names (map materials only). Not seen in game."}
io.open(p, 'w', encoding='utf-8', newline='\n').write(json.dumps(d, indent=2, ensure_ascii=False) + '\n')
print('ok')
