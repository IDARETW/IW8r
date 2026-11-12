import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/core/stock_zone_edit.h', [
 ('''                          const std::string* placedObjects = nullptr, const std::vector<StockPatchOverride>* stockPatches = nullptr);
''', '''                          const std::string* placedObjects = nullptr, const std::vector<StockPatchOverride>* stockPatches = nullptr,
                          const std::vector<StockPropCollision>* removeCollision = nullptr);
'''),
 ('''// `stockPatches` (stock_install.h): the saved stock patches''', '''// A hidden stock prop whose collision is taken out too (editzone "remove_static_model_collision"): its model and
// place, as the world's static model table has them.
struct StockPropCollision {
    std::string model;
    double origin[3] = {0, 0, 0};
};
// `removeCollision`: the hidden props whose collision goes too (a small map keeps it in its collision map; a big map in
// its collision tiles, which this does not change).
// `stockPatches` (stock_install.h): the saved stock patches'''),
])
patch('src/core/stock_zone_edit.cpp', [
 ('''                          const std::vector<StockPatchOverride>* stockPatches) {
''', '''                          const std::vector<StockPatchOverride>* stockPatches, const std::vector<StockPropCollision>* removeCollision) {
'''),
 (r'''    if (stockPatches && !stockPatches->empty()) {
''', r'''    if (removeCollision && !removeCollision->empty()) {
        sep();
        json += "\"remove_static_model_collision\":[";
        for (size_t i = 0; i < removeCollision->size(); ++i) {
            const StockPropCollision& c = (*removeCollision)[i];
            json += std::string(i ? "," : "") + "{\"model\":" + jsonText(c.model) + ",\"origin\":[" + formatNumber(c.origin[0]) + "," +
                    formatNumber(c.origin[1]) + "," + formatNumber(c.origin[2]) + "]}";
        }
        json += "]";
    }
    if (stockPatches && !stockPatches->empty()) {
'''),
])
patch('src/qtapp/main_window_stock_edit.cpp', [
 ('''    const std::vector<uint32_t> hidden = stockEditHiddenProps(ctx_.doc.map());
    if (notes && !hidden.empty()) *notes << QString("%1 stock prop(s) are hidden (scale 0; their collision stays)").arg(hidden.size());
''', '''    const std::vector<uint32_t> hidden = stockEditHiddenProps(ctx_.doc.map());
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
'''),
 ('''    const std::string json = stockEditJson(QDir::toNativeSeparators(entsPath).toStdString(), hidden, carry, writeLoot ? &loot : nullptr,
                                           &placed.json, &stockPatches);
''', '''    const std::string json = stockEditJson(QDir::toNativeSeparators(entsPath).toStdString(), hidden, carry, writeLoot ? &loot : nullptr,
                                           &placed.json, &stockPatches, &removeCollision);
'''),
])
patch('src/qtapp/qt_gui_test_loot.cpp', [
 ('''        check(prepared && json.contains("\\"hide_static_models\\":[3,7,40]"), "the edits file lists the hidden rows: " + err);
''', '''        check(prepared && json.contains("\\"hide_static_models\\":[3,7,40]"), "the edits file lists the hidden rows: " + err);
        check(json.contains("\\"remove_static_model_collision\\":[{\\"model\\":\\"test_prop\\",\\"origin\\":[0,0,0]},{\\"model\\":\\"test_prop\\",\\"origin\\":[10,10,10]},"
                            "{\\"model\\":\\"test_prop\\",\\"origin\\":[20,-20,5]}]"),
              "and takes their collision out by model and place");
'''),
])
print('ok')
