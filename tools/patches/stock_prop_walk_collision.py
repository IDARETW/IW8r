import io

# The stock map's props in the play preview (owner 2026-11-15: bushes walked through): the collision agent's
# MainWindow::addStockPropCollision (the game's rule: a prop's stock physics asset shape; none without one) added to
# the stock world collision when the walk starts.

P = 'E:/IW8-Radiant/src/qtapp/main_window.cpp'
s = io.open(P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('\r\n', '\n')
a = '''        const size_t n = addStockWorldCollision(w);
        if (n) EditorLog::instance().add(LogLevel::Info, "play", QString("Stock world collision: %1 triangles (%2 ms)").arg(n).arg(t.elapsed()));
    });'''
b = '''        const size_t n = addStockWorldCollision(w);
        if (n) EditorLog::instance().add(LogLevel::Info, "play", QString("Stock world collision: %1 triangles (%2 ms)").arg(n).arg(t.elapsed()));
        if (!refProps_ || refProps_->empty()) return;
        t.restart();
        const StockPropCollisionStats ps = addStockPropCollision(w, ctx_.cam.pos, 0.0);  // every prop
        EditorLog::instance().add(LogLevel::Info, "play",
                                  QString("Stock prop collision: %1 prop(s) by their stock physics, %2 by their mesh, %3 walk-through "
                                          "(no physics asset, as in game), %4 not ready yet (next walk); %5 solids (%6 ms)")
                                      .arg(ps.physics)
                                      .arg(ps.renderMesh)
                                      .arg(ps.walkThrough)
                                      .arg(ps.pending)
                                      .arg(ps.solids)
                                      .arg(t.elapsed()));
    });'''
if b not in s:
    assert s.count(a) == 1
    s = s.replace(a, b)
io.open(P, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
print('ok')
