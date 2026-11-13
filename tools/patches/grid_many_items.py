import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


G = 'src/qtapp/grid_view.cpp'
patch(G, '''    p.setRenderHint(QPainter::Antialiasing, true);
    for (int pass = 0; pass < 2; ++pass) {
        for (const app::PrimGeom* g : ctx_->scene.items()) {''', '''    // Warzone maps hold tens of thousands of entities (Verdansk: 47,545 plus 23,446 loot points): no antialiasing
    // then, and an entity a few pixels across is a dot (a filled, outlined, antialiased rectangle each made a zoomed-out
    // paint take seconds).
    const bool manyItems = ctx_->scene.items().size() > 20000;
    p.setRenderHint(QPainter::Antialiasing, !manyItems);
    for (int pass = 0; pass < 2; ++pass) {
        for (const app::PrimGeom* g : ctx_->scene.items()) {''')
patch(G, '''            } else if (g->kind == app::PrimGeom::PointEntity) {
                const QRectF r(worldToWidget(g->bounds.mins[H], g->bounds.maxs[V]), worldToWidget(g->bounds.maxs[H], g->bounds.mins[V]));
                p.fillRect(r, QColor(col.red(), col.green(), col.blue(), 96));''', '''            } else if (g->kind == app::PrimGeom::PointEntity) {
                const QRectF r(worldToWidget(g->bounds.mins[H], g->bounds.maxs[V]), worldToWidget(g->bounds.maxs[H], g->bounds.mins[V]));
                if (!sel && std::fabs(r.width()) < 3.0 && std::fabs(r.height()) < 3.0) {
                    p.drawPoint(r.center());
                    continue;
                }
                p.fillRect(r, QColor(col.red(), col.green(), col.blue(), 96));''')
print('ok')
