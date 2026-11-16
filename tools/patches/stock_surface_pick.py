import io

# Picking a stock map's own surfaces in the camera (owner checklist 2026-11-15, D5 Retexture: the material of a wall
# had to be known first). A click that hits nothing of the document names the stock surface under the cursor: a world
# triangle's material (removed triangles left out), or a stock prop (its model box), in the status bar; a world
# material also goes to the Stock Map panel's Retexture (MainWindow::stockSurfacePicked).

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


rw('src/qtapp/main_window.h', [
('''    size_t addStockWorldCollision(CollisionWorld& w) const;''', '''    size_t addStockWorldCollision(CollisionWorld& w) const;
    // A stock map's own surface under a camera ray (owner checklist 2026-11-15, D5): the nearest world triangle
    // (triangles of iw8r_remove_world boxes left out) or stock prop (its model box; hidden props left out).
    struct StockSurfacePick {
        QString material;  // a world surface's material ("" for a prop)
        QString model;     // a prop's model ("" for the world)
        uint32_t propIndex = 0;
        Vec3 at;
        double t = 0;
    };
    std::optional<StockSurfacePick> pickStockSurface(const Ray& ray) const;
    // The camera's click on nothing of the document: the stock surface in the status bar (a world material also to
    // stockSurfacePicked). False when there is no stock surface there.
    bool showStockSurfaceAt(const Ray& ray);
    mutable const void* stockPickBoundsFor_ = nullptr;  // the stock world stockPickBounds_ was made for
    mutable std::vector<AABB> stockPickBounds_;          // per world mesh
    std::map<std::string, std::array<float, 6>> refPropBoxes_;  // stock prop model boxes (model space), for picking
signals:
    void stockSurfacePicked(const QString& material);  // StockMapPanel::setPickedMaterial
public:'''),
])

rw('src/qtapp/main_window.cpp', [
('''            if (built && built->tris > 0 && meshTris + built->tris <= kPropTriangleBudget) {''',
 '''            if (built && built->hasBox) {  // its box for picking (pickStockSurface), kept after refPropBuilt_ is dropped
                std::array<float, 6> pb;
                std::copy(built->box, built->box + 6, pb.begin());
                refPropBoxes_[in.model] = pb;
            }
            if (built && built->tris > 0 && meshTris + built->tris <= kPropTriangleBudget) {'''),
('''    camera_->setPlayWorldExtras([this](CollisionWorld& w) {''', '''    camera_->setStockSurfacePicker([this](const Ray& r) { return showStockSurfaceAt(r); });
    camera_->setPlayWorldExtras([this](CollisionWorld& w) {'''),
])

rw('src/qtapp/camera_view.h', [
('''    std::function<void(CollisionWorld&)> playWorldExtras_;''', '''    std::function<void(CollisionWorld&)> playWorldExtras_;
public:
    // A plain click on nothing of the document: a stock map's own surface under the cursor (MainWindow::showStockSurfaceAt).
    void setStockSurfacePicker(std::function<bool(const Ray&)> fn) { stockSurfacePicker_ = std::move(fn); }
private:
    std::function<bool(const Ray&)> stockSurfacePicker_;'''),
])

rw('src/qtapp/camera_view.cpp', [
('''        if (hits.empty()) {
            cycleHits_.clear();
            if (mods == Qt::NoModifier) ctx_->doc.deselectAll();
            ctx_->documentEdited();
            return;
        }''', '''        if (hits.empty()) {
            cycleHits_.clear();
            if (mods == Qt::NoModifier) ctx_->doc.deselectAll();
            ctx_->documentEdited();
            // A stock map: its own surface under the cursor (the material in the status bar and the Stock Map panel).
            if (mods == Qt::NoModifier && stockSurfacePicker_) stockSurfacePicker_(ray);
            return;
        }'''),
])

S = 'src/qtapp/main_window_stock_edit.cpp'
s = io.open(ROOT + S, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if 'MainWindow::pickStockSurface' not in s:
    s = s.replace('\r\n', '\n')
    k = s.rindex('}  // namespace iw8r::qt')
    s = s[:k] + '''// ------------------------------------------------------------------ picking the stock map's own surfaces

std::optional<MainWindow::StockSurfacePick> MainWindow::pickStockSurface(const Ray& ray) const {
    std::optional<StockSurfacePick> best;
    double bestT = 1e300;
    if (stockWorld_) {
        if (stockPickBoundsFor_ != stockWorld_.get()) {
            stockPickBounds_.assign(stockWorld_->meshes.size(), AABB{});
            for (size_t i = 0; i < stockWorld_->meshes.size(); ++i)
                for (const Vec3& p : stockWorld_->meshes[i].pos) stockPickBounds_[i].add(p);
            stockPickBoundsFor_ = stockWorld_.get();
        }
        const std::vector<WorldRemovalBox> boxes = documentWorldRemovals(ctx_.doc.map());
        const auto inBox = [](const Vec3& p, const WorldRemovalBox& b) {
            return p.x >= b.mins.x - 1e-3 && p.y >= b.mins.y - 1e-3 && p.z >= b.mins.z - 1e-3 && p.x <= b.maxs.x + 1e-3 &&
                   p.y <= b.maxs.y + 1e-3 && p.z <= b.maxs.z + 1e-3;
        };
        for (size_t mi = 0; mi < stockWorld_->meshes.size(); ++mi) {
            const ImportedMesh& m = stockWorld_->meshes[mi];
            const auto enter = rayAABB(ray, stockPickBounds_[mi]);
            if (!enter || *enter >= bestT) continue;
            const QString mat = qs(m.material).remove(QChar(',')).trimmed();
            if (mat.section('/', -1).toLower().startsWith("sky_")) continue;  // the sky box is not a surface to pick
            for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
                const uint32_t i0 = m.idx[t], i1 = m.idx[t + 1], i2 = m.idx[t + 2];
                if (i0 >= m.pos.size() || i1 >= m.pos.size() || i2 >= m.pos.size()) continue;
                const Vec3 &a = m.pos[i0], &b = m.pos[i1], &c = m.pos[i2];
                // Moller-Trumbore, both sides.
                const Vec3 e1 = b - a, e2 = c - a;
                const Vec3 pv = cross(ray.dir, e2);
                const double det = dot(e1, pv);
                if (std::fabs(det) < 1e-12) continue;
                const double inv = 1.0 / det;
                const Vec3 tv = ray.origin - a;
                const double u = dot(tv, pv) * inv;
                if (u < 0 || u > 1) continue;
                const Vec3 qv = cross(tv, e1);
                const double v = dot(ray.dir, qv) * inv;
                if (v < 0 || u + v > 1) continue;
                const double d = dot(e2, qv) * inv;
                if (d <= 0.01 || d >= bestT) continue;
                bool removed = false;
                for (const WorldRemovalBox& box : boxes)
                    if (inBox(a, box) && inBox(b, box) && inBox(c, box)) {
                        removed = true;
                        break;
                    }
                if (removed) continue;
                bestT = d;
                best = StockSurfacePick{mat, QString(), 0, ray.origin + ray.dir * d, d};
            }
        }
    }
    if (refProps_ && !refPropBoxes_.empty()) {
        std::set<uint32_t> hidden;
        for (const Entity& ws : ctx_.doc.map().entities)
            if (const std::string* v = ws.isWorldspawn() ? ws.value("iw8r_hide_smodels") : nullptr)
                for (const QString& tok : qs(*v).split(QRegularExpression("[\\\\s,]+"), Qt::SkipEmptyParts)) hidden.insert(tok.toUInt());
        for (const StockModelInstance& in : *refProps_) {
            const auto it = refPropBoxes_.find(in.model);
            if (it == refPropBoxes_.end() || hidden.count(in.index)) continue;
            const double sc = in.scale > 1e-6 ? in.scale : 1.0;
            const Vec3 d = ray.origin - in.origin;
            const Ray local{{dot(d, in.axis[0]) / sc, dot(d, in.axis[1]) / sc, dot(d, in.axis[2]) / sc},
                            {dot(ray.dir, in.axis[0]), dot(ray.dir, in.axis[1]), dot(ray.dir, in.axis[2])}};
            const auto& b = it->second;
            AABB box;
            box.mins = {b[0], b[1], b[2]};
            box.maxs = {b[3], b[4], b[5]};
            if (box.contains(local.origin)) continue;  // the camera inside a big model's box: what is beyond it
            if (const auto tl = rayAABB(local, box); tl && *tl * sc < bestT) {
                bestT = *tl * sc;
                best = StockSurfacePick{QString(), qs(in.model), in.index, ray.origin + ray.dir * bestT, bestT};
            }
        }
    }
    return best;
}

bool MainWindow::showStockSurfaceAt(const Ray& ray) {
    if (!stockWorld_ && !refProps_) return false;
    const auto hit = pickStockSurface(ray);
    if (!hit) return false;
    if (!hit->material.isEmpty()) {
        ctx_.setStatus(QString("Stock surface: %1 (the map's world, %2 units away). The Stock Map panel's Retexture takes it "
                               "(or worldspawn iw8r_stock_retexture %1=<material>)")
                           .arg(hit->material)
                           .arg(qRound(hit->t)));
        emit stockSurfacePicked(hit->material);
    } else {
        ctx_.setStatus(QString("Stock prop: %1 (row %2, %3 units away). To hide it: a brush around its origin, then "
                               "File > Stock Map > Hide Stock Props In Selection")
                           .arg(hit->model)
                           .arg(hit->propIndex)
                           .arg(qRound(hit->t)));
    }
    return true;
}

''' + s[k:]
    for inc in ('#include <set>', '#include <QRegularExpression>'):
        if inc not in s:
            s = s.replace('#include <map>\n', '#include <map>\n' + inc + '\n', 1)
    io.open(ROOT + S, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
print('ok')
