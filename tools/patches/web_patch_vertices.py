import io

# Plan 3.4, the last open point: patch vertex editing in the web page. GET /api/handles gives the control points of
# the selected patches; POST /api/vertices takes {e, i, points: [index, ...]} for a patch beside the brush form. With
# Tool: Vertices the page shows a handle on every control point of the selected patches.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, n=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    write(p, s.replace(a, b))


S = 'src/qtapp/web_server.cpp'
patch(S, '''            const Primitive* prim = it ? it->prim() : nullptr;
            if (!prim || prim->kind != PrimKind::Brush) return error(400, "corners are moved on brushes only");''',
      '''            const Primitive* prim = it ? it->prim() : nullptr;
            if (prim && prim->kind == PrimKind::Patch && m.value("points").isArray()) {
                // A patch: its control points by index (row-major, as GET /api/handles lists them).
                const auto grid = patchGrid(*prim);
                const QJsonArray points = m.value("points").toArray();
                if (!grid || points.isEmpty()) return error(400, "a patch move names control points by index");
                std::vector<bool> taken(grid->pos.size(), false);
                for (const QJsonValue& pv : points) {
                    const double k = pv.toDouble(-1);
                    if (!(k >= 0) || k >= double(grid->pos.size()) || k != std::floor(k)) return error(400, "there is no such control point");
                    if (taken[size_t(k)]) continue;
                    taken[size_t(k)] = true;
                    VertexHandle h;
                    h.pos = grid->pos[size_t(k)], h.entity = e, h.item = i, h.patch = true, h.index = size_t(k);
                    handles.push_back(h);
                }
                continue;
            }
            if (!prim || prim->kind != PrimKind::Brush) return error(400, "corners are moved on brushes, control points on patches");''')
patch(S, '''        if (!moveVertexHandles(*ctx_, handles, delta)) return error(400, "nothing was moved");
        ctx_->documentEdited("Corners moved from the browser");''', '''        size_t rejected = 0, locked = 0;
        if (!moveVertexHandles(*ctx_, handles, delta, &rejected, &locked))
            return error(400, locked ? QString("nothing was moved: the control points are locked") : QString("nothing was moved"));
        ctx_->documentEdited("Points moved from the browser");''')
patch(S, '''        if (path == "/api/forms") {''', '''        if (path == "/api/handles") {
            // The control points of the selected patches (the page's Vertices tool): [[x, y, z, locked], ...] in the
            // patch's own order. A selection with more than 20,000 points gets none (truncated: true).
            QJsonArray patches;
            size_t total = 0;
            bool truncated = false;
            for (const auto& sel : ctx_->doc.selection()) {
                const EntityItem* it = sel.item ? ctx_->doc.findItem(sel.entity, sel.item) : nullptr;
                const Primitive* prim = it ? it->prim() : nullptr;
                if (!prim || prim->kind != PrimKind::Patch) continue;
                const auto grid = patchGrid(*prim);
                if (!grid) continue;
                if (total + grid->pos.size() > 20000) {
                    truncated = true;
                    continue;
                }
                total += grid->pos.size();
                const auto lk = ctx_->patchLocks.find({sel.entity, sel.item});
                QJsonArray points;
                for (size_t k = 0; k < grid->pos.size(); ++k) {
                    const bool locked = lk != ctx_->patchLocks.end() && k < lk->second.size() && lk->second[k];
                    points.append(QJsonArray{grid->pos[k].x, grid->pos[k].y, grid->pos[k].z, locked ? 1 : 0});
                }
                patches.append(QJsonObject{{"e", double(sel.entity)}, {"i", double(sel.item)}, {"rows", grid->rows}, {"cols", grid->cols}, {"points", points}});
            }
            Response r;
            r.body = json({{"patches", patches}, {"total", double(total)}, {"truncated", truncated}});
            return r;
        }
        if (path == "/api/forms") {''')

J = 'data/web/app.js'
patch(J, '''function shapeHandles() {
  const mode = $("editMode").value;
  if (!mode || Number($("axis").value) > 2) return [];
  const [a, b] = axes(), out = new Map();''', '''// The control points of the selected patches (Vertices tool), from the editor: [{e, i, k, p: [x, y, z], locked}].
let patchPoints = [], patchPointsFor = "";
async function loadPatchPoints() {
  const want = $("editMode").value === "vertex" ? state.revision + "|" + JSON.stringify(state.selection) : "";
  if (want === patchPointsFor) return;
  patchPointsFor = want;
  patchPoints = [];
  if (want) {
    try {
      const j = await get("/api/handles");
      for (const pt of j.patches) pt.points.forEach((p, k) => patchPoints.push({ e: pt.e, i: pt.i, k, p, locked: !!p[3] }));
      if (j.truncated) setStatus("Too many control points selected to show them all");
    } catch (e) { /* no handles then */ }
  }
  draw();
}
function shapeHandles() {
  const mode = $("editMode").value;
  if (!mode || Number($("axis").value) > 2) return [];
  const [a, b] = axes(), out = new Map();
  if (mode === "vertex")
    for (const pp of patchPoints) {
      if (pp.locked) continue;
      const k = key2(pp.p[a], pp.p[b]);
      if (!out.has(k)) out.set(k, { key: k, h: pp.p[a], v: pp.p[b], ends: [k] });
    }''')
patch(J, '''  setStatus(m === "vertex" ? "Drag a corner of a selected brush" : m === "edge" ? "Drag an edge of a selected brush" : "");
  draw();''', '''  setStatus(m === "vertex" ? "Drag a corner of a selected brush or a control point of a selected patch" : m === "edge" ? "Drag an edge of a selected brush" : "");
  loadPatchPoints();
  draw();''')
patch(J, '''        if (from.size) moves.push({ e: it.e, i: it.i, from: [...from.values()] });
      }''', '''        if (from.size) moves.push({ e: it.e, i: it.i, from: [...from.values()] });
      }
      const byPatch = new Map();
      for (const pp of patchPoints) {
        if (pp.locked || !d.handle.ends.includes(key2(pp.p[a], pp.p[b]))) continue;
        const pk = pp.e + ":" + pp.i;
        if (!byPatch.has(pk)) byPatch.set(pk, { e: pp.e, i: pp.i, points: [] });
        byPatch.get(pk).points.push(pp.k);
      }
      moves.push(...byPatch.values());''')
patch(J, '''  if (window.viewLive) window.viewLive.refresh();
}''', '''  if (window.viewLive) window.viewLive.refresh();
  loadPatchPoints();
}''')

T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''            const auto badBrush = web.handle("POST", "/api/brush", "{\\"mins\\":[0,0,0],\\"maxs\\":[0,64,64]}", true, {});''', '''            {
                // A patch: its control points are listed for the selection, and one is moved by its index.
                SelRef patchRef{};
                for (const auto& ent : ctx.doc.map().entities)
                    for (const auto& item : ent.items)
                        if (!patchRef.item && item.prim() && item.prim()->kind == PrimKind::Patch && patchGrid(*item.prim())) patchRef = SelRef{ent.id, item.id};
                ctx.doc.select(patchRef, false);
                const size_t undoP = ctx.doc.undoPosition();
                const QJsonObject hs = QJsonDocument::fromJson(web.handle("GET", "/api/handles", {}, true, {}).body).object();
                const QJsonObject first = hs.value("patches").toArray().first().toObject();
                const QJsonArray pts = first.value("points").toArray();
                const auto gridOf = [&] {
                    const EntityItem* it = ctx.doc.findItem(patchRef.entity, patchRef.item);
                    return it && it->prim() ? patchGrid(*it->prim()) : std::nullopt;
                };
                const auto g0 = gridOf();
                const QString mv = QString("{\\"moves\\":[{\\"e\\":%1,\\"i\\":%2,\\"points\\":[0,0]}],\\"d\\":[0,0,8]}").arg(patchRef.entity).arg(patchRef.item);
                const auto moved = web.handle("POST", "/api/vertices", mv.toUtf8(), true, {});
                const auto g1 = gridOf();
                const bool one = ctx.doc.undoPosition() == undoP + 1;
                const QString badIx = QString("{\\"moves\\":[{\\"e\\":%1,\\"i\\":%2,\\"points\\":[99999]}],\\"d\\":[0,0,8]}").arg(patchRef.entity).arg(patchRef.item);
                const auto bad = web.handle("POST", "/api/vertices", badIx.toUtf8(), true, {});
                while (ctx.doc.undoPosition() > undoP) ctx.doc.undo();
                const auto g2 = gridOf();
                ctx.doc.deselectAll();
                ctx.documentEdited();
                const auto pageP = web.handle("GET", "/app.js", {}, true, {});
                r.check(patchRef.item && g0 && g1 && g2 && pts.size() == int(g0->pos.size()) && first.value("rows").toInt() == g0->rows &&
                            std::fabs(pts.first().toArray().at(2).toDouble() - g0->pos[0].z) < 1e-9 && moved.status == 200 && one &&
                            std::fabs(g1->pos[0].z - g0->pos[0].z - 8) < 1e-6 && std::fabs(g1->pos[1].z - g0->pos[1].z) < 1e-9 && bad.status == 400 &&
                            std::fabs(g2->pos[0].z - g0->pos[0].z) < 1e-9 && pageP.body.contains("/api/handles"),
                        QString("GET /api/handles lists a selected patch's control points (%1) and POST /api/vertices moves one by index").arg(pts.size()));
            }
            const auto badBrush = web.handle("POST", "/api/brush", "{\\"mins\\":[0,0,0],\\"maxs\\":[0,64,64]}", true, {});''')
print('ok')
