import io

# A stock map's own lights in the viewport (owner 2026-11-15: "map lights not showing as lights with gizmos in the
# viewport ... make sure the stock map editing is actually proper"): the map's spot and omni primary lights (ComWorld,
# parsed already for the lighting) are drawn as light markers with their radius and spot cone (depth-tested and
# dimmed, as the own map's light ranges; View > Entity Display > Light Ranges), and a click on one names it (type,
# radius, intensity, colour, cone) and draws its shape bright on top.

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


# ---- the frame: a third depth-tested line stream
rw('src/viewport/render.h', [
('''    GeoStream<GpuVertex>* lines = nullptr;
    GeoStream<GpuVertex>* linesMisc = nullptr;''', '''    GeoStream<GpuVertex>* lines = nullptr;
    GeoStream<GpuVertex>* linesMisc = nullptr;
    GeoStream<GpuVertex>* linesExtra = nullptr;  // the camera's own depth-tested lines (a stock map's light shapes)'''),
])
rw('src/viewport/render.cpp', [
('''    Flat fShadowOnly, fCasters, fFaceSel, fNoCast, fLines, fLinesMisc, fOverlay[4];''',
 '''    Flat fShadowOnly, fCasters, fFaceSel, fNoCast, fLines, fLinesMisc, fLinesExtra, fOverlay[4];'''),
('''            fLinesMisc = syncFlat(in.linesMisc);
''', '''            fLinesMisc = syncFlat(in.linesMisc);
            fLinesExtra = syncFlat(in.linesExtra);
'''),
('''    if (fLines.ready() || fLinesMisc.ready()) {''', '''    if (fLines.ready() || fLinesMisc.ready() || fLinesExtra.ready()) {'''),
('''        drawFlat(fLines, true);
        drawFlat(fLinesMisc, true);
    }''', '''        drawFlat(fLines, true);
        drawFlat(fLinesMisc, true);
        drawFlat(fLinesExtra, true);
    }'''),
])

# ---- the camera: the stream and its setter
rw('src/qtapp/camera_view.h', [
('''    std::vector<app::GpuVertex> stockPickLines_;''', '''    std::vector<app::GpuVertex> stockPickLines_;
public:
    // A stock map's light shapes (line pairs: markers, radii, spot cones), depth-tested; empty = none.
    void setStockLightShapes(std::vector<app::GpuVertex> lines) {
        stockLightPending_ = std::move(lines);
        stockLightDirty_ = true;
        update();
    }
    size_t stockLightShapeSize() const { return stockLightLines_.v.size(); }  // tests
private:
    std::vector<app::GpuVertex> stockLightPending_;
    bool stockLightDirty_ = false;
    app::GeoStream<app::GpuVertex> stockLightLines_;'''),
])
rw('src/qtapp/camera_view.cpp', [
('''    if (!cleanCapture_) gizmoLines_.scratch.insert(gizmoLines_.scratch.end(), stockPickLines_.begin(), stockPickLines_.end());
''', '''    if (!cleanCapture_) gizmoLines_.scratch.insert(gizmoLines_.scratch.end(), stockPickLines_.begin(), stockPickLines_.end());
    if (stockLightDirty_) {  // a stock map's light shapes (MainWindow::updateStockLightShapes)
        stockLightLines_.v.swap(stockLightPending_);
        stockLightPending_.clear();
        stockLightLines_.markAll();
        stockLightDirty_ = false;
    }
'''),
('''        in.linesMisc = &geo_.linesMisc;''', '''        in.linesMisc = &geo_.linesMisc;
        in.linesExtra = cleanCapture_ ? nullptr : &stockLightLines_;'''),
])

# ---- the main window: the light list, its shapes, picking
rw('src/qtapp/main_window.h', [
('''    std::vector<Iw8SubModel> stockSubModels_;''', '''    std::vector<Iw8SubModel> stockSubModels_;
public:
    // The opened stock map's spot and omni lights (ComWorld primary lights), for their shapes and picking.
    std::shared_ptr<const std::vector<StockPrimaryLight>> stockLightList_;
    // Their shapes in the camera (markers; radius and cone with View > Entity Display > Light Ranges).
    void updateStockLightShapes();
private:'''),
('''        int mesh = -1;                 // the world mesh (-1 for a prop)''', '''        int mesh = -1;                 // the world mesh (-1 for a prop)
        int light = -1;                // a stock light (its place in stockLightList_), -1 otherwise'''),
])
rw('src/qtapp/main_window.cpp', [
('''    stockSubModels_.clear();
    selectionSets_.clear();''', '''    stockSubModels_.clear();
    stockLightList_.reset();
    if (camera_) camera_->setStockLightShapes({});
    selectionSets_.clear();'''),
('''        auto localLights = std::make_shared<std::vector<StockGameLight>>();''', '''        auto localLights = std::make_shared<std::vector<StockGameLight>>();
        auto lightList = std::make_shared<std::vector<StockPrimaryLight>>();  // the spots and omnis, for their shapes'''),
('''                    if (packStockLight(l, d != defs.end() && !iesStrip->empty() ? &d->second : nullptr, g)) {''',
 '''                    if (l.type == 2 || l.type == 3) lightList->push_back(l);
                    if (packStockLight(l, d != defs.end() && !iesStrip->empty() ? &d->second : nullptr, g)) {'''),
('''        QMetaObject::invokeMethod(qApp, [self, zone, intensity, color, toward, visionText, skyIntensity, grade, localLights, iesStrip,
                                         lightShadows] {
            if (!self || self->referenceZone_ != zone) return;
            self->camera_->setStockLocalLights(localLights, iesStrip, lightShadows);''', '''        QMetaObject::invokeMethod(qApp, [self, zone, intensity, color, toward, visionText, skyIntensity, grade, localLights, iesStrip,
                                         lightShadows, lightList] {
            if (!self || self->referenceZone_ != zone) return;
            self->camera_->setStockLocalLights(localLights, iesStrip, lightShadows);
            self->stockLightList_ = lightList;
            self->updateStockLightShapes();'''),
('''                ctx_.scene.*flag = !(ctx_.scene.*flag);
                Settings::instance().setValue(key, ctx_.scene.*flag);
                if (QAction* act = action(id)) act->setChecked(ctx_.scene.*flag);''', '''                ctx_.scene.*flag = !(ctx_.scene.*flag);
                Settings::instance().setValue(key, ctx_.scene.*flag);
                if (QAction* act = action(id)) act->setChecked(ctx_.scene.*flag);
                updateStockLightShapes();  // a stock map's own lights follow Light Ranges too'''),
])

S = 'src/qtapp/main_window_stock_edit.cpp'
s = io.open(ROOT + S, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('\r\n', '\n')
if 'appendStockLightShape' not in s:
    k = s.index('std::optional<MainWindow::StockSurfacePick> MainWindow::pickStockSurface')
    s = s[:k] + '''// A stock light's shape as line pairs: a marker (three axes), and with `ranges` its radius (omni: three circles) or its
// cone (spot: the outer cone to the radius, its end circle and the aim line). Light shines along -dir (dir points back
// toward the light, research 106 section 3.1).
static void appendStockLightShape(std::vector<app::GpuVertex>& out, const StockPrimaryLight& l, uint32_t col, bool ranges) {
    const Vec3 o{l.origin[0], l.origin[1], l.origin[2]};
    const auto line = [&](const Vec3& a, const Vec3& b) {
        out.push_back(app::GpuVertex{float(a.x), float(a.y), float(a.z), col});
        out.push_back(app::GpuVertex{float(b.x), float(b.y), float(b.z), col});
    };
    const double m = 10.0;
    line(o - Vec3{m, 0, 0}, o + Vec3{m, 0, 0}), line(o - Vec3{0, m, 0}, o + Vec3{0, m, 0}), line(o - Vec3{0, 0, m}, o + Vec3{0, 0, m});
    if (!ranges || l.radius <= 0) return;
    const double r = std::min<double>(l.radius, 16384.0);
    const auto circle = [&](const Vec3& c, const Vec3& u, const Vec3& v, double rad, int seg) {
        for (int i = 0; i < seg; ++i) {
            const double a0 = 6.283185307 * i / seg, a1 = 6.283185307 * (i + 1) / seg;
            line(c + u * (std::cos(a0) * rad) + v * (std::sin(a0) * rad), c + u * (std::cos(a1) * rad) + v * (std::sin(a1) * rad));
        }
    };
    if (l.type == 3) {  // omni
        circle(o, Vec3{1, 0, 0}, Vec3{0, 1, 0}, r, 48);
        circle(o, Vec3{1, 0, 0}, Vec3{0, 0, 1}, r, 48);
        circle(o, Vec3{0, 1, 0}, Vec3{0, 0, 1}, r, 48);
        return;
    }
    Vec3 dir = Vec3{-l.dir[0], -l.dir[1], -l.dir[2]};
    if (length(dir) < 1e-6) dir = Vec3{0, 0, -1};
    dir = normalize(dir);
    const Vec3 ref = std::fabs(dir.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    const Vec3 u = normalize(cross(dir, ref)), v = cross(dir, u);
    const double c = std::clamp<double>(l.cosHalfFovOuter, 0.0, 0.9999);
    const double half = std::acos(c);
    const double len = r * std::cos(half), rad = r * std::sin(half);  // the cone's cap on the radius sphere
    const Vec3 end = o + dir * len;
    for (int i = 0; i < 8; ++i) {
        const double a = 6.283185307 * i / 8;
        line(o, end + u * (std::cos(a) * rad) + v * (std::sin(a) * rad));
    }
    circle(end, u, v, rad, 32);
    line(o, o + dir * r);
}

void MainWindow::updateStockLightShapes() {
    if (!camera_) return;
    std::vector<app::GpuVertex> lines;
    if (stockLightList_) {
        const bool ranges = ctx_.scene.showLightRanges;
        for (const StockPrimaryLight& l : *stockLightList_) {
            // The light's colour, dimmed (the own map's unselected light ranges are dimmed too).
            const double mx = std::max({double(l.color[0]), double(l.color[1]), double(l.color[2]), 1e-6});
            const auto b = [&](float c) { return uint32_t(std::clamp(c / mx * 0.55 + 0.15, 0.0, 1.0) * 255.0 + 0.5); };
            appendStockLightShape(lines, l, b(l.color[0]) | (b(l.color[1]) << 8) | (b(l.color[2]) << 16) | 0xFF000000u, ranges);
        }
    }
    camera_->setStockLightShapes(std::move(lines));
}

''' + s[k:]
    io.open(ROOT + S, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))

rw(S, [
# picking: the lights first (a small sphere at each origin)
('''    if (refProps_ && !refPropBoxes_.empty()) {
        std::set<uint32_t> hidden;''', '''    if (stockLightList_)
        for (size_t li = 0; li < stockLightList_->size(); ++li) {
            const StockPrimaryLight& l = (*stockLightList_)[li];
            const Vec3 c{l.origin[0], l.origin[1], l.origin[2]};
            const Vec3 oc = ray.origin - c;
            const double bq = dot(oc, ray.dir), cq = dot(oc, oc) - 12.0 * 12.0, disc = bq * bq - cq;
            if (disc < 0) continue;
            const double t = -bq - std::sqrt(disc);
            if (t > 0.01 && t < bestT) {
                bestT = t;
                best = StockSurfacePick{QString(), QString(), 0, ray.origin + ray.dir * t, t, -1};
                best->light = int(li);
            }
        }
    if (refProps_ && !refPropBoxes_.empty()) {
        std::set<uint32_t> hidden;'''),
('''    if (hit->mesh >= 0 && stockWorld_ && size_t(hit->mesh) < stockWorld_->meshes.size()) {''',
 '''    if (hit->light >= 0 && stockLightList_ && size_t(hit->light) < stockLightList_->size()) {
        appendStockLightShape(lines, (*stockLightList_)[size_t(hit->light)], col, true);
    } else if (hit->mesh >= 0 && stockWorld_ && size_t(hit->mesh) < stockWorld_->meshes.size()) {'''),
('''    } else if (hit->mesh < 0) {
        const auto& k = hit->corners;''', '''    } else if (hit->mesh < 0 && hit->light < 0) {
        const auto& k = hit->corners;'''),
('''    camera_->setStockPickOutline(std::move(lines));
    if (!hit->material.isEmpty()) {''', '''    camera_->setStockPickOutline(std::move(lines));
    if (hit->light >= 0 && stockLightList_ && size_t(hit->light) < stockLightList_->size()) {
        const StockPrimaryLight& l = (*stockLightList_)[size_t(hit->light)];
        const double outer = 2.0 * std::acos(std::clamp<double>(l.cosHalfFovOuter, -1.0, 1.0)) * 57.29578;
        ctx_.setStatus(QString("Stock light (%1): row %2 at %3 %4 %5, radius %6, intensity %7, colour %8 %9 %10%11%12. The map's own "
                               "light (read-only here: its edits need the converter)")
                           .arg(l.type == 2 ? "spot" : "omni")
                           .arg(l.index)
                           .arg(qRound(l.origin[0])).arg(qRound(l.origin[1])).arg(qRound(l.origin[2]))
                           .arg(qRound(l.radius))
                           .arg(l.intensity, 0, 'g', 4)
                           .arg(l.color[0], 0, 'f', 2).arg(l.color[1], 0, 'f', 2).arg(l.color[2], 0, 'f', 2)
                           .arg(l.type == 2 ? QString(", cone %1 degrees").arg(qRound(outer)) : QString())
                           .arg(l.defName.empty() ? QString() : ", profile " + qs(l.defName)));
        return true;
    }
    if (!hit->material.isEmpty()) {'''),
])
print('ok')
