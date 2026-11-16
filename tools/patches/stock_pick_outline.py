import io

# The picked stock surface drawn (owner checklist 2026-11-15, D5 "cant seem to select any objects in the viewport of
# stock maps"): a world surface's triangles around the click outlined, a prop's model box outlined, on top in the
# camera until the next click. A picked prop also goes to the Stock Map panel (stockPropPicked: its Hide button).

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


rw('src/qtapp/camera_view.h', [
('''    std::function<bool(const Ray&)> stockSurfacePicker_;''', '''    std::function<bool(const Ray&)> stockSurfacePicker_;
public:
    // The picked stock surface's outline (line pairs), drawn on top until the next click; empty = none.
    void setStockPickOutline(std::vector<app::GpuVertex> lines) {
        stockPickLines_ = std::move(lines);
        update();
    }
    size_t stockPickOutlineSize() const { return stockPickLines_.size(); }  // tests
private:
    std::vector<app::GpuVertex> stockPickLines_;'''),
])

rw('src/qtapp/camera_view.cpp', [
('''    if (!cleanCapture_) addGizmoLines();
''', '''    if (!cleanCapture_) addGizmoLines();
    if (!cleanCapture_) gizmoLines_.scratch.insert(gizmoLines_.scratch.end(), stockPickLines_.begin(), stockPickLines_.end());
'''),
('''            // A stock map: its own surface under the cursor (the material in the status bar and the Stock Map panel).
            if (mods == Qt::NoModifier && stockSurfacePicker_) stockSurfacePicker_(ray);
            return;''', '''            // A stock map: its own surface under the cursor (the material in the status bar and the Stock Map panel).
            if (!(mods == Qt::NoModifier && stockSurfacePicker_ && stockSurfacePicker_(ray)) && !stockPickLines_.empty())
                setStockPickOutline({});
            return;'''),
('''        cyclePos_ = p, cycleHits_ = hits, cycleIndex_ = index;''', '''        cyclePos_ = p, cycleHits_ = hits, cycleIndex_ = index;
        if (!stockPickLines_.empty()) setStockPickOutline({});'''),
])

rw('src/qtapp/main_window.h', [
('''        uint32_t propIndex = 0;
        Vec3 at;
        double t = 0;
    };''', '''        uint32_t propIndex = 0;
        Vec3 at;
        double t = 0;
        int mesh = -1;                 // the world mesh (-1 for a prop)
        std::array<Vec3, 8> corners{};  // a prop's model box corners in world space (bit 0 x, 1 y, 2 z: max)
    };'''),
('''    void stockSurfacePicked(const QString& material);  // StockMapPanel::setPickedMaterial''',
 '''    void stockSurfacePicked(const QString& material);  // StockMapPanel::setPickedMaterial
    void stockPropPicked(const QString& model, uint32_t row);  // a stock prop clicked in the camera (its smodel row)'''),
])

S = 'src/qtapp/main_window_stock_edit.cpp'
rw(S, [
('''                bestT = d;
                best = StockSurfacePick{mat, QString(), 0, ray.origin + ray.dir * d, d};''', '''                bestT = d;
                best = StockSurfacePick{mat, QString(), 0, ray.origin + ray.dir * d, d, int(mi)};'''),
('''            if (const auto tl = rayAABB(local, box); tl && *tl * sc < bestT) {
                bestT = *tl * sc;
                best = StockSurfacePick{QString(), qs(in.model), in.index, ray.origin + ray.dir * bestT, bestT};
            }''', '''            if (const auto tl = rayAABB(local, box); tl && *tl * sc < bestT) {
                bestT = *tl * sc;
                best = StockSurfacePick{QString(), qs(in.model), in.index, ray.origin + ray.dir * bestT, bestT};
                for (int k = 0; k < 8; ++k) {
                    const double x = (k & 1) ? b[3] : b[0], y = (k & 2) ? b[4] : b[1], z = (k & 4) ? b[5] : b[2];
                    best->corners[size_t(k)] = in.origin + (in.axis[0] * x + in.axis[1] * y + in.axis[2] * z) * sc;
                }
            }'''),
('''    const auto hit = pickStockSurface(ray);
    if (!hit) return false;
    if (!hit->material.isEmpty()) {''', '''    const auto hit = pickStockSurface(ray);
    if (!hit) return false;
    // Its outline in the camera: the world surface's triangles around the click, or the prop's model box.
    std::vector<app::GpuVertex> lines;
    const uint32_t col = 0xFF20D8FFu;  // amber (R in the low byte)
    const auto line = [&](const Vec3& p, const Vec3& q) {
        lines.push_back(app::GpuVertex{float(p.x), float(p.y), float(p.z), col});
        lines.push_back(app::GpuVertex{float(q.x), float(q.y), float(q.z), col});
    };
    if (hit->mesh >= 0 && stockWorld_ && size_t(hit->mesh) < stockWorld_->meshes.size()) {
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
    } else if (hit->mesh < 0) {
        const auto& k = hit->corners;
        for (int i = 0; i < 8; ++i)
            for (int bit : {1, 2, 4})
                if (!(i & bit)) line(k[size_t(i)], k[size_t(i | bit)]);
    }
    camera_->setStockPickOutline(std::move(lines));
    if (!hit->material.isEmpty()) {'''),
('''                           .arg(hit->propIndex)
                           .arg(qRound(hit->t)));
    }''', '''                           .arg(hit->propIndex)
                           .arg(qRound(hit->t)));
        emit stockPropPicked(hit->model, hit->propIndex);
    }'''),
])
print('ok')
