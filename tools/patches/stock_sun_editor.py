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
 ('''                          const std::vector<StockPropCollision>* removeCollision = nullptr, const std::string* lootBoxes = nullptr);''',
  '''                          const std::vector<StockPropCollision>* removeCollision = nullptr, const std::string* lootBoxes = nullptr,
                          const std::string* sun = nullptr);

// 11.6: the stock sun the document asks for (worldspawn iw8r_sun_color "r g b", linear 0..16, and iw8r_sun_intensity
// 0..1000000) as the edits file's "sun" object; "" when neither key is set. A bad value is left out with a note. The
// direction is never changed (the stock zone's baked sun shadows hold it).
std::string stockSunJson(const MapFile& map, std::vector<std::string>* notes = nullptr);'''),
])
patch('src/core/stock_zone_edit.cpp', [
 ('''                          const std::vector<StockPatchOverride>* stockPatches, const std::vector<StockPropCollision>* removeCollision,
                          const std::string* lootBoxes) {''', '''                          const std::vector<StockPatchOverride>* stockPatches, const std::vector<StockPropCollision>* removeCollision,
                          const std::string* lootBoxes, const std::string* sun) {'''),
 ('''    if (lootBoxes && !lootBoxes->empty()) {''', '''    if (sun && !sun->empty()) {
        sep();
        json += "\\"sun\\":" + *sun;
    }
    if (lootBoxes && !lootBoxes->empty()) {'''),
])
s = io.open('src/core/stock_zone_edit.cpp', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
end = s.rstrip().rfind('}')
add = r'''
std::string stockSunJson(const MapFile& map, std::vector<std::string>* notes) {
    const Entity* world = nullptr;
    for (const Entity& e : map.entities)
        if (e.isWorldspawn()) {
            world = &e;
            break;
        }
    if (!world) return {};
    std::string json;
    const auto num = [](double v) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.6g", v);
        return std::string(buf);
    };
    if (const std::string* c = world->value("iw8r_sun_color"); c && !c->empty()) {
        double v[3] = {0, 0, 0};
        char tail = 0;
        if (std::sscanf(c->c_str(), "%lf %lf %lf %c", &v[0], &v[1], &v[2], &tail) == 3 && v[0] >= 0 && v[1] >= 0 && v[2] >= 0 &&
            v[0] <= 16 && v[1] <= 16 && v[2] <= 16)
            json += "\"color\":[" + num(v[0]) + "," + num(v[1]) + "," + num(v[2]) + "]";
        else if (notes)
            notes->push_back("iw8r_sun_color '" + *c + "' is not three numbers 0 to 16 (linear); the sun colour is not changed");
    }
    if (const std::string* i = world->value("iw8r_sun_intensity"); i && !i->empty()) {
        char* endp = nullptr;
        const double v = std::strtod(i->c_str(), &endp);
        if (endp && *endp == 0 && v >= 0 && v <= 1000000)
            json += std::string(json.empty() ? "" : ",") + "\"intensity\":" + num(v);
        else if (notes)
            notes->push_back("iw8r_sun_intensity '" + *i + "' is not a number 0 to 1000000; the sun intensity is not changed");
    }
    return json.empty() ? std::string() : "{" + json + "}";
}

'''.replace('\n', nl)
s = s[:end] + add.lstrip(nl) + s[end:]
if '#include <cstdio>' not in s:
    s = s.replace('#include "stock_zone_edit.h"' + nl, '#include "stock_zone_edit.h"' + nl + nl + '#include <cstdio>' + nl + '#include <cstdlib>' + nl, 1)
io.open('src/core/stock_zone_edit.cpp', 'w', encoding='utf-8', newline='').write(s)

patch('src/qtapp/main_window_stock_edit.cpp', [
 ('''    // 13.5: placed objects (buy stations) that were moved or added as `scriptable` entities.''', '''    // 11.6: the stock sun's colour and intensity (worldspawn iw8r_sun_color / iw8r_sun_intensity).
    std::vector<std::string> sunNotes;
    const std::string sun = stockSunJson(ctx_.doc.map(), &sunNotes);
    if (notes) {
        for (const std::string& n : sunNotes) *notes << qs(n);
        if (!sun.empty()) *notes << "the stock sun gets a new colour or intensity (its direction stays: the baked sun shadows hold it)";
    }

    // 13.5: placed objects (buy stations) that were moved or added as `scriptable` entities.'''),
 ('''                                           &placed.json, &stockPatches, &removeCollision, &lootBoxes);''',
  '''                                           &placed.json, &stockPatches, &removeCollision, &lootBoxes, &sun);'''),
])

patch('src/qtapp/main_window.cpp', [
 ('''    addCommand(file, "RelightStockMap", "Relight Stock Map (Fog / Vision)...", QKeySequence(), [this] { relightStockMapDialog(); });''',
  '''    addCommand(file, "RelightStockMap", "Relight Stock Map (Fog / Vision)...", QKeySequence(), [this] { relightStockMapDialog(); });
    addCommand(file, "RelightStockSun", "Relight Stock Map Sun (Colour / Intensity)...", QKeySequence(), [this] {
        // 11.6: worldspawn iw8r_sun_color (linear) and iw8r_sun_intensity; File > Rebuild Stock Zone writes them.
        const Entity* world = nullptr;
        for (const Entity& e : ctx_.doc.map().entities)
            if (e.isWorldspawn()) {
                world = &e;
                break;
            }
        if (!world || stockZone_.isEmpty()) return ctx_.setStatus("Relight Sun: open a stock map first (File > Open IW8 Stock Map)");
        double lin[3] = {1.0, 0.95, 0.9}, intensity = 8.6;  // about the stock suns (mp_shipment 1.05 0.99 0.92, 8.63)
        if (const std::string* c = world->value("iw8r_sun_color")) std::sscanf(c->c_str(), "%lf %lf %lf", &lin[0], &lin[1], &lin[2]);
        if (const std::string* i = world->value("iw8r_sun_intensity")) intensity = std::atof(i->c_str());
        const auto toS = [](double v) { return int(std::lround(std::pow(std::clamp(v, 0.0, 1.0), 1.0 / 2.2) * 255.0)); };
        const QColor picked = QColorDialog::getColor(QColor(toS(lin[0]), toS(lin[1]), toS(lin[2])), this, "Sun colour");
        if (!picked.isValid()) return;
        bool ok = false;
        const double in = QInputDialog::getDouble(this, "Relight Sun", "Sun intensity (stock suns are about 6 to 12):", intensity, 0, 1000, 2, &ok);
        if (!ok) return;
        const auto toL = [](int v) { return std::pow(v / 255.0, 2.2); };
        const std::string color = formatNumber(toL(picked.red())) + " " + formatNumber(toL(picked.green())) + " " + formatNumber(toL(picked.blue()));
        ctx_.doc.beginGroup("Relight Sun");
        ctx_.doc.setKeyValue(world->id, "iw8r_sun_color", color);
        ctx_.doc.setKeyValue(world->id, "iw8r_sun_intensity", formatNumber(in));
        ctx_.doc.endGroup();
        ctx_.documentEdited("Stock sun set (colour " + QString::fromStdString(color) + ", intensity " + QString::number(in) +
                            "); File > Rebuild Stock Zone writes it. The direction stays (baked sun shadows)");
    });'''),
])

# Entity definitions: the two worldspawn keys.
p = 'data/iw8_entity_defs_overrides.json'
d = json.loads(io.open(p, encoding='utf-8').read())
ws = d['classes']['worldspawn']['keys']
ws['iw8r_sun_color'] = {'type': 'string', 'default': '', 'required': False, 'examples': ['1 0.55 0.35'],
                        'description': "Stock maps (11.6): the sun's new colour, linear r g b 0 to 16 (File > Relight Stock Map Sun). "
                                       "Rebuild Stock Zone writes it; the direction stays (baked sun shadows). Not seen in game."}
ws['iw8r_sun_intensity'] = {'type': 'float', 'default': '', 'required': False, 'examples': ['7.5'],
                            'description': "Stock maps (11.6): the sun's new intensity (stock suns are about 6 to 12). Rebuild Stock Zone "
                                           "writes it. Not seen in game."}
io.open(p, 'w', encoding='utf-8', newline='\n').write(json.dumps(d, indent=2, ensure_ascii=False) + '\n')
print('ok')
