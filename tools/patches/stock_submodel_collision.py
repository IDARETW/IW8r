import io

# Stock maps' script brush models are solid in the walk preview (owner re-check 2026-11-15: "still pass thru things
# like objective models (bombsite for example)"). A bomb site is a script_model (the crate, no physics asset) plus a
# script_brushmodel naming compiled submodel "*N", whose collision is what blocks the player in game. The map's
# MapEnts lists every submodel's bounds (cmodels[N].bounds: midPoint relative to the entity origin, halfSize); each
# script_brushmodel of the stock document becomes a box of those bounds, turned by the entity's angles.

ROOT = 'E:/IW8-Radiant/'


def rw(p, pairs):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        if b in s and a not in s:
            continue
        assert s.count(a) == 1, (p, s.count(a), a[:90])
        s = s.replace(a, b)
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


rw('src/core/iw8_mapents.h', [
('''struct Iw8MapEntsDump {''', '''// A compiled submodel (MapEnts cmodels[N]; entities name it "*N"): its bounds relative to the entity's origin.
struct Iw8SubModel {
    Vec3 mid, half;
};

struct Iw8MapEntsDump {'''),
('''    std::vector<Iw8PlacedEffect> effects;  // clientSideEffects.oneshotEffects, in record order''',
 '''    std::vector<Iw8PlacedEffect> effects;  // clientSideEffects.oneshotEffects, in record order
    std::vector<Iw8SubModel> subModels;    // cmodels, by index (0 = the world)'''),
])

rw('src/core/iw8_mapents.cpp', [
('''    auto count = [&](const char* section, const json::Value* v) {''', '''    // The submodels' bounds (cmodel_t.bounds: midPoint, halfSize), by index.
    if (const json::Value* cm = fields->get("cmodels"))
        if (const json::Value* values = cm->get("values"); values && values->type == json::Value::Type::Array)
            for (const auto& r : values->arr) {
                Iw8SubModel m;
                if (const json::Value* b = r.get("bounds")) {
                    vec3(b->get("midPoint"), m.mid);
                    vec3(b->get("halfSize"), m.half);
                }
                out.subModels.push_back(m);
            }
    auto count = [&](const char* section, const json::Value* v) {'''),
])

rw('src/qtapp/main_window.h', [
('''    std::vector<StockPlacedObject> stockPlaced_;''', '''    std::vector<StockPlacedObject> stockPlaced_;
    std::vector<Iw8SubModel> stockSubModels_;  // the opened stock map's compiled submodels (script brush models' bounds)'''),
])

rw('src/qtapp/main_window.cpp', [
('''    stockPlaced_.clear();
    selectionSets_.clear();''', '''    stockPlaced_.clear();
    stockSubModels_.clear();
    selectionSets_.clear();'''),
('''        ctx_.scene.spawns = std::move(dump.spawns);
        stockModelNames_''', '''        ctx_.scene.spawns = std::move(dump.spawns);
        stockSubModels_ = std::move(dump.subModels);
        stockModelNames_'''),
])

rw('src/qtapp/main_window_stock_edit.cpp', [
('''size_t MainWindow::addStockWorldCollision(CollisionWorld& w) const {
    if (!stockWorld_) return 0;''', '''size_t MainWindow::addStockWorldCollision(CollisionWorld& w) const {
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
    if (!stockWorld_) return added;'''),
('''    size_t added = 0;
    for (const ImportedMesh& m : stockWorld_->meshes) {''', '''    for (const ImportedMesh& m : stockWorld_->meshes) {'''),
])
print('ok')
