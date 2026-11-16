import io

# The stock world as Play Preview collision (MainWindow::addStockWorldCollision, set as the camera's play world
# extras): every opaque world triangle as a facet, leaving out the sky, decals, foliage cards, translucent surfaces and
# triangles inside iw8r_remove_world boxes that take the collision out. Props are not added here (the collision agent's
# MainWindow::addStockPropCollision follows the game's model collision rule).

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


S = 'src/qtapp/main_window_stock_edit.cpp'
s = io.open(ROOT + S, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if 'MainWindow::addStockWorldCollision' not in s:
    s = s.replace('\r\n', '\n')
    k = s.rindex('}  // namespace iw8r::qt')
    s = s[:k] + '''// ------------------------------------------------------------------ the stock world in the play preview

size_t MainWindow::addStockWorldCollision(CollisionWorld& w) const {
    if (!stockWorld_) return 0;
    // Removal boxes that take the collision out (iw8r_remove_world, "collision" not 0).
    std::vector<WorldRemovalBox> boxes;
    for (const WorldRemovalBox& b : documentWorldRemovals(ctx_.doc.map()))
        if (b.collision) boxes.push_back(b);
    const auto inBox = [&](const Vec3& p, const WorldRemovalBox& b) {
        return p.x >= b.mins.x - 1e-3 && p.y >= b.mins.y - 1e-3 && p.z >= b.mins.z - 1e-3 && p.x <= b.maxs.x + 1e-3 &&
               p.y <= b.maxs.y + 1e-3 && p.z <= b.maxs.z + 1e-3;
    };
    size_t added = 0;
    for (const ImportedMesh& m : stockWorld_->meshes) {
        const QString mat = qs(m.material).remove(QChar(',')).trimmed().toLower();
        const QString leaf = mat.section('/', -1);
        // Not walked on or against in game: the sky, decals, foliage and grass cards, translucent surfaces.
        if (leaf.startsWith("sky_") || mat.contains("decal") || mat.contains("foliage") || mat.contains("grass") || mat.contains("leaves") ||
            mat.contains("ivy") || mat.contains("vine"))
            continue;
        if (const auto e = matCache_->lookup(qs(m.material).remove(QChar(',')).trimmed()); e && !e->failed &&
            materialBlendFromTechset(e->techset.toStdString()) == MaterialBlend::Blend)
            continue;
        for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
            const uint32_t i0 = m.idx[t], i1 = m.idx[t + 1], i2 = m.idx[t + 2];
            if (i0 >= m.pos.size() || i1 >= m.pos.size() || i2 >= m.pos.size()) continue;
            const Vec3 &a = m.pos[i0], &b = m.pos[i1], &c = m.pos[i2];
            bool removed = false;
            for (const WorldRemovalBox& box : boxes)
                if (inBox(a, box) && inBox(b, box) && inBox(c, box)) {
                    removed = true;
                    break;
                }
            if (!removed && w.addTriangle(a, b, c, 2.0)) ++added;
        }
    }
    return added;
}

''' + s[k:]
    io.open(ROOT + S, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))

rw('src/qtapp/main_window.h', [
('''    void refreshStockWorldPreview();''', '''    void refreshStockWorldPreview();
    // The stock world's surfaces as play preview collision (opaque surfaces as facets; removal boxes respected).
    size_t addStockWorldCollision(CollisionWorld& w) const;'''),
])

rw('src/qtapp/main_window.cpp', [
('''    camera_ = new CameraView(&ctx_, dev_, dc_);
''', '''    camera_ = new CameraView(&ctx_, dev_, dc_);
    // A stock map's world is walked on in the play preview (owner checklist 2026-11-15: it was noclip there).
    camera_->setPlayWorldExtras([this](CollisionWorld& w) {
        QElapsedTimer t;
        t.start();
        const size_t n = addStockWorldCollision(w);
        if (n) EditorLog::instance().add(LogLevel::Info, "play", QString("Stock world collision: %1 triangles (%2 ms)").arg(n).arg(t.elapsed()));
    });
'''),
])
print('ok')
