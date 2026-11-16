import io

# Stock map objects select cleanly and act like models (owner re-check 2026-11-15, RD6 / RD7: "objects dont seem to
# cleanly select in the viewport on stock maps, it only selects parts of walls, selects random large areas in line
# with where i clicked"; "objects should simply be selectable and managable like a normal model in the viewport, i
# shouldnt have to create a brush to remove things"):
#  - props are picked by their model's triangles (the model box only narrows the search), so the prop under the cursor
#    is the one picked, not a big model whose box happens to enclose the ray; its outline is the model's own edges;
#  - a world surface selects the connected surface around the click (triangles sharing edges and facing within 35
#    degrees: the wall, the floor), outlined along its border;
#  - Delete with a stock object selected (and nothing of the document): a prop is hidden (iw8r_hide_smodels), a world
#    surface gets an iw8r_remove_world box around it (one undo step each); a stock light is read-only;
#  - a double-click on a stock prop makes it a model entity of the document (the placement hidden, a misc_model in its
#    place, selected: the gizmo moves, turns and scales it like any model; one undo step).

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
('''        int light = -1;                // a stock light (its place in stockLightList_), -1 otherwise''',
 '''        int light = -1;                // a stock light (its place in stockLightList_), -1 otherwise
        int prop = -1;                 // a stock prop (its place in refProps_), -1 otherwise
        int tri = -1;                  // the world triangle hit (index of its first vertex in the mesh's idx)'''),
('''    void updateStockLightShapes();
private:''', '''    void updateStockLightShapes();
    // The stock object last clicked in the camera (nothing of the document under the cursor) and its bounds.
    std::optional<StockSurfacePick> stockSel_;
    AABB stockSelBounds_;
    // Delete on it: a prop is hidden, a world surface gets a removal box around it. False when nothing is selected.
    bool deleteStockSelection();
    // A double-click on a stock prop: it becomes a model entity of the document (selected). False when no prop is there.
    bool editStockPropAsModel(const Ray& ray);
    // A stock prop's model for exact picking (read once, kept for the next clicks).
    std::shared_ptr<const ImportedModel> stockPickModel(const std::string& name) const;
    mutable std::map<std::string, std::shared_ptr<const ImportedModel>> stockPickModels_;
private:'''),
])

rw('src/qtapp/camera_view.h', [
('''    std::function<bool(const Ray&)> stockSurfacePicker_;''', '''    std::function<bool(const Ray&)> stockSurfacePicker_;
public:
    // A double-click on nothing of the document: a stock prop becomes a model entity (MainWindow::editStockPropAsModel).
    void setStockSurfaceEditor(std::function<bool(const Ray&)> fn) { stockSurfaceEditor_ = std::move(fn); }
protected:
    void mouseDoubleClickEvent(QMouseEvent* e) override;
private:
    std::function<bool(const Ray&)> stockSurfaceEditor_;'''),
])

S = 'src/qtapp/camera_view.cpp'
s = io.open(ROOT + S, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if 'CameraView::mouseDoubleClickEvent' not in s:
    s = s.replace('\r\n', '\n')
    k = s.index('void CameraView::wheelEvent(QWheelEvent* e) {')
    s = s[:k] + '''void CameraView::mouseDoubleClickEvent(QMouseEvent* e) {
    // A double-click on a stock map's prop (nothing of the document under the cursor) makes it a model of the document.
    if (e->button() == Qt::LeftButton && !orbit_ && !ctx_->playing && stockSurfaceEditor_ && !(e->modifiers() & ~Qt::KeypadModifier)) {
        const QPoint p = e->position().toPoint();
        const double nx = 2.0 * p.x() / std::max(1, width()) - 1.0, ny = 1.0 - 2.0 * p.y() / std::max(1, height());
        const Ray ray = app::cameraRay(ctx_->cam, nx, ny, double(std::max(1, width())) / std::max(1, height()));
        ctx_->scene.sync(ctx_->doc);
        if (ctx_->scene.pickAll(ray).empty() && stockSurfaceEditor_(ray)) {
            setStockPickOutline({});
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(e);
}

''' + s[k:]
    io.open(ROOT + S, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))

rw('src/qtapp/main_window.cpp', [
('''    camera_->setStockSurfacePicker([this](const Ray& r) { return showStockSurfaceAt(r); });''',
 '''    camera_->setStockSurfacePicker([this](const Ray& r) { return showStockSurfaceAt(r); });
    camera_->setStockSurfaceEditor([this](const Ray& r) { return editStockPropAsModel(r); });'''),
('''    addCommand(edit, "DeleteSelection", "&Delete", QKeySequence(Qt::Key_Backspace),
               [this] { ctx_.documentEdited(ctx_.doc.deleteSelection() ? "Deleted" : "Nothing selected"); });''',
 '''    addCommand(edit, "DeleteSelection", "&Delete", QKeySequence(Qt::Key_Backspace), [this] {
        // Nothing of the document selected, a stock object clicked in the camera: it is hidden or removed.
        if (ctx_.doc.selection().empty() && deleteStockSelection()) return;
        ctx_.documentEdited(ctx_.doc.deleteSelection() ? "Deleted" : "Nothing selected");
    });'''),
('''    stockLightList_.reset();
    if (camera_) camera_->setStockLightShapes({});''', '''    stockLightList_.reset();
    stockSel_.reset();
    stockPickModels_.clear();
    if (camera_) camera_->setStockLightShapes({});'''),
])

S = 'src/qtapp/main_window_stock_edit.cpp'
rw(S, [
# exact prop picking: candidates by box, then the model's triangles
('''        for (const StockModelInstance& in : *refProps_) {
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
                for (int k = 0; k < 8; ++k) {
                    const double x = (k & 1) ? b[3] : b[0], y = (k & 2) ? b[4] : b[1], z = (k & 4) ? b[5] : b[2];
                    best->corners[size_t(k)] = in.origin + (in.axis[0] * x + in.axis[1] * y + in.axis[2] * z) * sc;
                }
            }
        }''', '''        // Candidates by their model box (in model space), nearest first; then the model's own triangles decide (a box
        // only narrows the search: a big model's box encloses much that is not the model).
        struct Cand {
            double t;
            size_t i;
            Ray local;
            double sc;
        };
        std::vector<Cand> cands;
        for (size_t pi = 0; pi < refProps_->size(); ++pi) {
            const StockModelInstance& in = (*refProps_)[pi];
            const auto it = refPropBoxes_.find(in.model);
            if (it == refPropBoxes_.end() || hidden.count(in.index)) continue;
            const double sc = in.scale > 1e-6 ? in.scale : 1.0;
            const Vec3 d = ray.origin - in.origin;
            const Ray local{{dot(d, in.axis[0]) / sc, dot(d, in.axis[1]) / sc, dot(d, in.axis[2]) / sc},
                            {dot(ray.dir, in.axis[0]), dot(ray.dir, in.axis[1]), dot(ray.dir, in.axis[2])}};
            const auto& b = it->second;
            AABB box;
            box.mins = {b[0] - 1, b[1] - 1, b[2] - 1};
            box.maxs = {b[3] + 1, b[4] + 1, b[5] + 1};
            if (const auto tl = rayAABB(local, box); tl && *tl * sc < bestT) cands.push_back({*tl * sc, pi, local, sc});
        }
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.t < b.t; });
        size_t tested = 0;
        for (const Cand& c : cands) {
            if (c.t >= bestT || tested >= 64) break;
            ++tested;
            const StockModelInstance& in = (*refProps_)[c.i];
            const auto model = stockPickModel(in.model);
            double hitT = -1;
            if (model) {
                const auto zup = [](const Vec3& p) { return Vec3{p.x, -p.z, p.y}; };  // glTF Y-up -> Z-up (as the props' meshes)
                for (const ImportedMesh& m : model->meshes)
                    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
                        const uint32_t i0 = m.idx[t], i1 = m.idx[t + 1], i2 = m.idx[t + 2];
                        if (i0 >= m.pos.size() || i1 >= m.pos.size() || i2 >= m.pos.size()) continue;
                        const Vec3 a = zup(m.pos[i0]), e1 = zup(m.pos[i1]) - a, e2 = zup(m.pos[i2]) - a;
                        const Vec3 pv = cross(c.local.dir, e2);
                        const double det = dot(e1, pv);
                        if (std::fabs(det) < 1e-12) continue;
                        const double inv = 1.0 / det;
                        const Vec3 tv = c.local.origin - a;
                        const double u = dot(tv, pv) * inv;
                        if (u < 0 || u > 1) continue;
                        const Vec3 qv = cross(tv, e1);
                        const double v = dot(c.local.dir, qv) * inv;
                        if (v < 0 || u + v > 1) continue;
                        const double d = dot(e2, qv) * inv;
                        if (d > 0.01 && (hitT < 0 || d < hitT)) hitT = d;
                    }
            } else {
                hitT = c.t / c.sc;  // the model is not readable: its box stands in
            }
            if (hitT < 0 || hitT * c.sc >= bestT) continue;
            bestT = hitT * c.sc;
            best = StockSurfacePick{QString(), qs(in.model), in.index, ray.origin + ray.dir * bestT, bestT};
            best->prop = int(c.i);
            const auto& b = refPropBoxes_.at(in.model);
            for (int k = 0; k < 8; ++k) {
                const double x = (k & 1) ? b[3] : b[0], y = (k & 2) ? b[4] : b[1], z = (k & 4) ? b[5] : b[2];
                best->corners[size_t(k)] = in.origin + (in.axis[0] * x + in.axis[1] * y + in.axis[2] * z) * c.sc;
            }
        }'''),
# the world triangle hit is kept
('''                bestT = d;
                best = StockSurfacePick{mat, QString(), 0, ray.origin + ray.dir * d, d, int(mi)};''', '''                bestT = d;
                best = StockSurfacePick{mat, QString(), 0, ray.origin + ray.dir * d, d, int(mi)};
                best->tri = int(t);'''),
])

# The outline and the selection: the connected surface for the world, the model's edges for a prop.
rw(S, [
('''    } else if (hit->mesh >= 0 && stockWorld_ && size_t(hit->mesh) < stockWorld_->meshes.size()) {
        const ImportedMesh& m = stockWorld_->meshes[size_t(hit->mesh)];
        const double r2 = 160.0 * 160.0;
        for (size_t t = 0; t + 2 < m.idx.size() && lines.size() < 12000; t += 3) {
            const uint32_t i0 = m.idx[t], i1 = m.idx[t + 1], i2 = m.idx[t + 2];
            if (i0 >= m.pos.size() || i1 >= m.pos.size() || i2 >= m.pos.size()) continue;
            const Vec3 &a = m.pos[i0], &b = m.pos[i1], &c = m.pos[i2];
            const Vec3 mid = (a + b + c) * (1.0 / 3.0) - hit->at;
            if (dot(mid, mid) > r2) continue;
            line(a, b), line(b, c), line(c, a);
        }
    } else if (hit->mesh < 0 && hit->light < 0) {
        const auto& k = hit->corners;
        for (int i = 0; i < 8; ++i)
            for (int bit : {1, 2, 4})
                if (!(i & bit)) line(k[size_t(i)], k[size_t(i | bit)]);
    }''', '''    } else if (hit->mesh >= 0 && hit->tri >= 0 && stockWorld_ && size_t(hit->mesh) < stockWorld_->meshes.size()) {
        // The connected surface: triangles of the same mesh sharing an edge (by position) with one already in, facing
        // within 35 degrees of the clicked one (a wall, a floor); outlined along its border.
        const ImportedMesh& m = stockWorld_->meshes[size_t(hit->mesh)];
        const size_t nt = m.idx.size() / 3;
        const auto key = [&](uint32_t vi) {
            const Vec3& p = m.pos[vi];
            return std::array<long long, 3>{std::llround(p.x * 8), std::llround(p.y * 8), std::llround(p.z * 8)};
        };
        const auto triN = [&](size_t t) {
            const Vec3 &a = m.pos[m.idx[t * 3]], &b = m.pos[m.idx[t * 3 + 1]], &c = m.pos[m.idx[t * 3 + 2]];
            const Vec3 n = cross(b - a, c - a);
            const double l = length(n);
            return l > 1e-12 ? n * (1.0 / l) : Vec3{0, 0, 0};
        };
        bool ok = true;
        for (size_t i = 0; i < m.idx.size() && ok; ++i) ok = m.idx[i] < m.pos.size();
        std::map<std::pair<std::array<long long, 3>, std::array<long long, 3>>, std::vector<uint32_t>> edges;
        const auto ekey = [&](uint32_t a, uint32_t b) {
            auto ka = key(a), kb = key(b);
            if (kb < ka) std::swap(ka, kb);
            return std::make_pair(ka, kb);
        };
        if (ok && nt < 2000000)
            for (size_t t = 0; t < nt; ++t)
                for (int e = 0; e < 3; ++e) edges[ekey(m.idx[t * 3 + e], m.idx[t * 3 + (e + 1) % 3])].push_back(uint32_t(t));
        const size_t t0 = size_t(hit->tri) / 3;
        const Vec3 n0 = triN(t0);
        std::vector<char> in(nt, 0);
        std::vector<size_t> stack{t0}, picked;
        in[t0] = 1;
        while (!stack.empty() && picked.size() < 40000) {
            const size_t t = stack.back();
            stack.pop_back();
            picked.push_back(t);
            for (int e = 0; e < 3 && ok; ++e) {
                const auto it = edges.find(ekey(m.idx[t * 3 + e], m.idx[t * 3 + (e + 1) % 3]));
                if (it == edges.end()) continue;
                for (const uint32_t o : it->second)
                    if (!in[o] && dot(triN(o), n0) > 0.82) in[o] = 1, stack.push_back(o);
            }
        }
        AABB sb;
        for (const size_t t : picked)
            for (int e = 0; e < 3; ++e) {
                const uint32_t a = m.idx[t * 3 + e], b = m.idx[t * 3 + (e + 1) % 3];
                sb.add(m.pos[a]);
                // A border edge: no other picked triangle shares it.
                int shared = 0;
                if (const auto it = edges.find(ekey(a, b)); it != edges.end())
                    for (const uint32_t o : it->second) shared += in[o] ? 1 : 0;
                if (shared <= 1 && lines.size() < 60000) line(m.pos[a], m.pos[b]);
            }
        stockSelBounds_ = sb;
        stockSel_ = *hit;
        if (!picked.empty()) stockSel_->tri = int(picked.size());  // (the count, for the status below)
    } else if (hit->prop >= 0 && refProps_ && size_t(hit->prop) < refProps_->size()) {
        // The model's own edges: the border and the creases (faces meeting at more than 40 degrees), in place.
        const StockModelInstance& in = (*refProps_)[size_t(hit->prop)];
        const double sc = in.scale > 1e-6 ? in.scale : 1.0;
        const auto place = [&](const Vec3& p) {
            const Vec3 z{p.x, -p.z, p.y};
            return in.origin + (in.axis[0] * z.x + in.axis[1] * z.y + in.axis[2] * z.z) * sc;
        };
        AABB sb;
        if (const auto model = stockPickModel(in.model)) {
            for (const ImportedMesh& m : model->meshes) {
                std::map<std::pair<std::array<long long, 3>, std::array<long long, 3>>, std::vector<Vec3>> edges;
                const auto key = [&](const Vec3& p) {
                    return std::array<long long, 3>{std::llround(p.x * 16), std::llround(p.y * 16), std::llround(p.z * 16)};
                };
                for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
                    if (m.idx[t] >= m.pos.size() || m.idx[t + 1] >= m.pos.size() || m.idx[t + 2] >= m.pos.size()) continue;
                    const Vec3 &a = m.pos[m.idx[t]], &b = m.pos[m.idx[t + 1]], &c = m.pos[m.idx[t + 2]];
                    Vec3 n = cross(b - a, c - a);
                    const double l = length(n);
                    n = l > 1e-12 ? n * (1.0 / l) : Vec3{0, 0, 0};
                    for (const auto& [p, q] : {std::make_pair(a, b), std::make_pair(b, c), std::make_pair(c, a)}) {
                        auto kp = key(p), kq = key(q);
                        if (kq < kp) std::swap(kp, kq);
                        edges[{kp, kq}].push_back(n);
                    }
                }
                for (const auto& [k, ns] : edges) {
                    const bool border = ns.size() == 1, crease = ns.size() >= 2 && dot(ns[0], ns[1]) < 0.766;
                    if (!border && !crease) continue;
                    const Vec3 p{k.first[0] / 16.0, k.first[1] / 16.0, k.first[2] / 16.0}, q{k.second[0] / 16.0, k.second[1] / 16.0, k.second[2] / 16.0};
                    if (lines.size() < 80000) line(place(p), place(q));
                    sb.add(place(p)), sb.add(place(q));
                }
            }
        }
        if (lines.empty()) {
            const auto& k = hit->corners;
            for (int i = 0; i < 8; ++i)
                for (int bit : {1, 2, 4})
                    if (!(i & bit)) line(k[size_t(i)], k[size_t(i | bit)]);
        }
        if (!sb.valid())
            for (const Vec3& c : hit->corners) sb.add(c);
        stockSelBounds_ = sb;
        stockSel_ = *hit;
    } else if (hit->mesh < 0 && hit->light < 0) {
        const auto& k = hit->corners;
        for (int i = 0; i < 8; ++i)
            for (int bit : {1, 2, 4})
                if (!(i & bit)) line(k[size_t(i)], k[size_t(i | bit)]);
    }'''),
('''bool MainWindow::showStockSurfaceAt(const Ray& ray) {
    if (!stockWorld_ && !refProps_) return false;
    const auto hit = pickStockSurface(ray);
    if (!hit) return false;''', '''bool MainWindow::showStockSurfaceAt(const Ray& ray) {
    stockSel_.reset();
    if (!stockWorld_ && !refProps_ && !stockLightList_) return false;
    const auto hit = pickStockSurface(ray);
    if (!hit) return false;'''),
# status texts name the new actions
('''        ctx_.setStatus(QString("Stock surface: %1 (the map's world, %2 units away). The Stock Map panel's Retexture takes it "
                               "(or worldspawn iw8r_stock_retexture %1=<material>)")
                           .arg(hit->material)
                           .arg(qRound(hit->t)));''', '''        ctx_.setStatus(QString("Stock surface: %1 (the map's world, %2 triangles connected, %3 units away). Delete removes it (a "
                               "world removal box around it); the Stock Map panel's Retexture takes the material")
                           .arg(hit->material)
                           .arg(stockSel_ ? stockSel_->tri : 0)
                           .arg(qRound(hit->t)));'''),
('''        ctx_.setStatus(QString("Stock prop: %1 (row %2, %3 units away). To hide it: a brush around its origin, then "
                               "File > Stock Map > Hide Stock Props In Selection")
                           .arg(hit->model)
                           .arg(hit->propIndex)
                           .arg(qRound(hit->t)));''', '''        ctx_.setStatus(QString("Stock prop: %1 (row %2, %3 units away). Delete hides it; a double-click makes it a model you can move, "
                               "turn and scale")
                           .arg(hit->model)
                           .arg(hit->propIndex)
                           .arg(qRound(hit->t)));'''),
])

s = io.open(ROOT + S, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if 'MainWindow::deleteStockSelection' not in s:
    s = s.replace('\r\n', '\n')
    k = s.rindex('}  // namespace iw8r::qt')
    s = s[:k] + '''std::shared_ptr<const ImportedModel> MainWindow::stockPickModel(const std::string& name) const {
    if (const auto it = stockPickModels_.find(name); it != stockPickModels_.end()) return it->second;
    std::shared_ptr<const ImportedModel> m;
    if (modelCacheStock_)
        if (auto im = modelCacheStock_->loadModel(qs(name))) m = std::make_shared<const ImportedModel>(std::move(*im));
    if (stockPickModels_.size() > 256) stockPickModels_.clear();
    stockPickModels_[name] = m;
    return m;
}

bool MainWindow::deleteStockSelection() {
    if (!stockSel_) return false;
    const StockSurfacePick sel = *stockSel_;
    if (sel.light >= 0) {
        ctx_.setStatus("A stock map's own light is read-only here (its edits need the converter)");
        return true;
    }
    if (sel.prop >= 0 && refProps_ && size_t(sel.prop) < refProps_->size()) {
        std::vector<uint32_t> hidden = stockEditHiddenProps(ctx_.doc.map());
        const uint32_t row = (*refProps_)[size_t(sel.prop)].index;
        if (std::find(hidden.begin(), hidden.end(), row) == hidden.end()) hidden.push_back(row);
        std::sort(hidden.begin(), hidden.end());
        std::string list;
        for (const uint32_t i : hidden) list += (list.empty() ? "" : " ") + std::to_string(i);
        for (const Entity& e : ctx_.doc.map().entities)
            if (e.isWorldspawn()) {
                ctx_.doc.setKeyValue(e.id, "iw8r_hide_smodels", list);
                break;
            }
        stockSel_.reset();
        if (camera_) camera_->setStockPickOutline({});
        rebuildReferenceProps();
        ctx_.documentEdited(QString("Stock prop %1 (row %2) hidden (Ctrl+Z brings it back; File > Stock Map > Rebuild Stock Zone leaves it out)")
                                .arg(sel.model)
                                .arg(row));
        return true;
    }
    if (sel.mesh >= 0 && stockSelBounds_.valid()) {
        AABB b = stockSelBounds_;
        for (int a = 0; a < 3; ++a) {  // a flat surface still gives a box; a little past its faces
            b.mins[a] -= 1.0, b.maxs[a] += 1.0;
        }
        const auto prim = makeBrushPrimitive(makeBox(b, " caulk 128 128 0 0 0 0"), ctx_.doc.eol());
        if (!prim) return false;
        ctx_.doc.addEntity({{"classname", kRemoveWorldClass}}, {*prim}, "Remove Stock Surface");
        stockSel_.reset();
        if (camera_) camera_->setStockPickOutline({});
        ctx_.documentEdited(QString("Stock surface %1 removed: a world removal box %2 x %3 x %4 around it (Ctrl+Z brings it back; the "
                                    "rebuild takes it out of the drawing and the collision)")
                                .arg(sel.material)
                                .arg(qRound(b.maxs.x - b.mins.x)).arg(qRound(b.maxs.y - b.mins.y)).arg(qRound(b.maxs.z - b.mins.z)));
        return true;
    }
    return false;
}

bool MainWindow::editStockPropAsModel(const Ray& ray) {
    const auto hit = pickStockSurface(ray);
    if (!hit || hit->prop < 0 || !refProps_ || size_t(hit->prop) >= refProps_->size()) return false;
    const StockModelInstance in = (*refProps_)[size_t(hit->prop)];
    std::vector<uint32_t> hidden = stockEditHiddenProps(ctx_.doc.map());
    if (std::find(hidden.begin(), hidden.end(), in.index) == hidden.end()) hidden.push_back(in.index);
    std::sort(hidden.begin(), hidden.end());
    std::string list;
    for (const uint32_t i : hidden) list += (list.empty() ? "" : " ") + std::to_string(i);
    auto keys = stockModelInstancesAsEntities(in);
    keys.push_back({"iw8r_stock_prop", std::to_string(in.index)});
    ctx_.doc.beginGroup("Edit Stock Prop As Model");
    for (const Entity& e : ctx_.doc.map().entities)
        if (e.isWorldspawn()) {
            ctx_.doc.setKeyValue(e.id, "iw8r_hide_smodels", list);
            break;
        }
    const Id id = ctx_.doc.addEntity(keys, {}, "Edit Stock Prop As Model");
    ctx_.doc.endGroup();
    ctx_.doc.deselectAll();
    if (id != kNoId) ctx_.doc.select(SelRef{id, kNoId}, false);
    stockSel_.reset();
    rebuildReferenceProps();
    ctx_.documentEdited(QString("Stock prop %1 (row %2) is now a model of the map: move, turn and scale it with the gizmo (Ctrl+Z puts the "
                                "stock prop back). The rebuild hides the placement and adds the model")
                            .arg(qs(in.model))
                            .arg(in.index));
    return true;
}

''' + s[k:]
    io.open(ROOT + S, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
print('ok')
