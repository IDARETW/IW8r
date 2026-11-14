import io

# Plan 3.4: vertex and edge tools in the web page's 2D views. POST /api/vertices moves corner points of brushes
# (one undo step, refused as a whole when a brush would stop being a valid convex brush); the page shows a handle
# on every corner (Vertices) or edge (Edges) of the selected brushes and sends the corners under the dragged handle.

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
patch(S, '''    } else if (path == "/api/resize") {''', '''    } else if (path == "/api/vertices") {
        // {moves: [{e, i, from: [[x, y, z], ...]}], d: [x, y, z]}: the named corners of those brushes move by d.
        const QJsonArray d = in.value("d").toArray(), moves = in.value("moves").toArray();
        if (d.size() != 3 || moves.isEmpty() || moves.size() > 256) return error(400, "need moves:[{e,i,from:[[x,y,z],...]}] and d:[x,y,z]");
        const Vec3 delta{d[0].toDouble(), d[1].toDouble(), d[2].toDouble()};
        if (!(length(delta) > 1e-6) || !(length(delta) < 262144)) return error(400, "d is the distance to move: not 0, and inside the map");
        std::vector<VertexHandle> handles;
        for (const QJsonValue& mv : moves) {
            const QJsonObject m = mv.toObject();
            const Id e = Id(m.value("e").toDouble(-1)), i = Id(m.value("i").toDouble(-1));
            const EntityItem* it = ctx_->doc.findItem(e, i);
            const Primitive* prim = it ? it->prim() : nullptr;
            if (!prim || prim->kind != PrimKind::Brush) return error(400, "corners are moved on brushes only");
            const std::vector<Vec3> corners = brushVertices(*prim);
            std::vector<Vec3> from;
            for (const QJsonValue& pv : m.value("from").toArray()) {
                const QJsonArray p = pv.toArray();
                if (p.size() != 3) return error(400, "a corner is [x,y,z]");
                const Vec3 want{p[0].toDouble(), p[1].toDouble(), p[2].toDouble()};
                const Vec3* best = nullptr;
                for (const Vec3& c : corners)
                    if (!best || length(c - want) < length(*best - want)) best = &c;
                if (!best || length(*best - want) > 0.5) return error(400, "a point is not a corner of that brush");
                bool seen = false;
                for (const Vec3& f : from) seen = seen || length(f - *best) < 1e-9;
                if (!seen) from.push_back(*best);
            }
            if (from.empty()) return error(400, "a move names at least one corner");
            // All or nothing: one brush that cannot take the move stops the whole request.
            if (!moveBrushVerts(*prim, from, delta, ctx_->doc.eol()))
                return error(400, "this move would leave a brush that is not convex, or a face that is not flat");
            for (const Vec3& f : from) {
                VertexHandle h;
                h.pos = f, h.entity = e, h.item = i;
                handles.push_back(h);
            }
        }
        if (!moveVertexHandles(*ctx_, handles, delta)) return error(400, "nothing was moved");
        ctx_->documentEdited("Corners moved from the browser");
    } else if (path == "/api/resize") {''')
s = read(S)
for inc in ('#include "patch_tools.h"', '#include "patch_ops.h"'):
    if inc not in s:
        s = s.replace('#include "brush_tools.h"', '#include "brush_tools.h"\n' + inc, 1)
write(S, s)

# ---- the page
patch('data/web/index.html', '''  <button id="undo" title="Undo (shared with the editor)">Undo</button>''',
      '''  <label title="What a drag does to the selected brushes in a Top, Front or Side view. Vertices: a handle on every corner. Edges: a handle on every edge. A handle moves every corner under it, so in a Top view a corner handle moves the whole upright edge">Tool <select id="editMode"><option value="">Move / resize</option><option value="vertex">Vertices</option><option value="edge">Edges</option></select></label>
  <button id="undo" title="Undo (shared with the editor)">Undo</button>''')
J = 'data/web/app.js'
patch(J, '''    for (const p of it.polys) {
      p.forEach((q, k) => { const [x, y] = toScreen(q[a] + dragOffset(it, a), q[b] + dragOffset(it, b)); k ? g.lineTo(x, y) : g.moveTo(x, y); });
      g.closePath();
    }
    g.stroke();
  };''', '''    for (const p of it.polys) {
      p.forEach((q, k) => {
        const so = selected ? shapeOffset(q, a, b) : null;
        const [x, y] = toScreen(q[a] + dragOffset(it, a) + (so ? so[0] : 0), q[b] + dragOffset(it, b) + (so ? so[1] : 0));
        k ? g.lineTo(x, y) : g.moveTo(x, y);
      });
      g.closePath();
    }
    g.stroke();
  };''')
patch(J, '''  // The selection's box with a handle on each side (drag one to resize along that axis).
  const box = selectionBox();
  if (box) {''', '''  // Vertices / Edges: a handle on every corner or edge of the selected brushes.
  const shaping = !!$("editMode").value;
  for (const hd of shapeHandles()) {
    const moving = drag && drag.mode === "shape" && drag.handle.key === hd.key;
    const [x, y] = toScreen(hd.h + (moving ? drag.delta[0] : 0), hd.v + (moving ? drag.delta[1] : 0));
    g.fillStyle = moving ? "#ffffff" : $("editMode").value === "vertex" ? "#7fd0ff" : "#8fe08a";
    g.fillRect(x - 4, y - 4, 8, 8);
  }
  // The selection's box with a handle on each side (drag one to resize along that axis).
  const box = shaping ? null : selectionBox();
  if (box) {''')
patch(J, '''// The handle of the selection's box under a canvas point: {side: 0 low | 1 high, dim: 0 horizontal | 1 vertical}.''',
      '''// Vertices / Edges (plan 3.4). A handle stands for every corner of the selected brushes under it in this view (a
// corner handle in a Top view is the whole upright edge), so a drag keeps faces flat where a single corner would not.
const key2 = (h, v) => Math.round(h * 8) + "," + Math.round(v * 8);
function selectedBrushes() {
  const sel = selectedSet();
  return map.items.filter((it) => it.kind === "brush" && (sel.has(selKey(it.e, it.i)) || sel.has(selKey(it.e, -1))));
}
function shapeHandles() {
  const mode = $("editMode").value;
  if (!mode || Number($("axis").value) > 2) return [];
  const [a, b] = axes(), out = new Map();
  for (const it of selectedBrushes())
    for (const p of it.polys)
      p.forEach((q, n) => {
        if (mode === "vertex") {
          const k = key2(q[a], q[b]);
          if (!out.has(k)) out.set(k, { key: k, h: q[a], v: q[b], ends: [k] });
          return;
        }
        const r = p[(n + 1) % p.length], kq = key2(q[a], q[b]), kr = key2(r[a], r[b]);
        if (kq === kr) return;                     // an edge seen end-on is a corner in this view
        const k = [kq, kr].sort().join("|");
        if (!out.has(k)) out.set(k, { key: k, h: (q[a] + r[a]) / 2, v: (q[b] + r[b]) / 2, ends: [kq, kr] });
      });
  return [...out.values()];
}
function shapeHandleAt(x, y) {
  let best = null, bestD = 8;
  for (const hd of shapeHandles()) {
    const [hx, hy] = toScreen(hd.h, hd.v), dd = Math.max(Math.abs(x - hx), Math.abs(y - hy));
    if (dd < bestD) { bestD = dd; best = hd; }
  }
  return best;
}
// The preview of a handle drag: the offset of a corner of a selected brush, or null.
function shapeOffset(q, a, b) {
  if (!drag || drag.mode !== "shape" || !drag.handle.ends.includes(key2(q[a], q[b]))) return null;
  return drag.delta;
}
$("editMode").onchange = () => {
  const m = $("editMode").value;
  setStatus(m === "vertex" ? "Drag a corner of a selected brush" : m === "edge" ? "Drag an edge of a selected brush" : "");
  draw();
};
// The handle of the selection's box under a canvas point: {side: 0 low | 1 high, dim: 0 horizontal | 1 vertical}.''')
patch(J, '''  const handle = handleAt(ev.offsetX, ev.offsetY);
  if (handle) {''', '''  const shape = shapeHandleAt(ev.offsetX, ev.offsetY);
  if (shape) {
    drag = { mode: "shape", x: ev.offsetX, y: ev.offsetY, handle: shape, start: toWorld(ev.offsetX, ev.offsetY), delta: [0, 0], moved: false };
    return;
  }
  const handle = $("editMode").value ? null : handleAt(ev.offsetX, ev.offsetY);
  if (handle) {''')
patch(J, '''  if (drag.mode === "draw") { drag.to = toWorld(ev.offsetX, ev.offsetY).map(snap); draw(); return; }''',
      '''  if (drag.mode === "draw") { drag.to = toWorld(ev.offsetX, ev.offsetY).map(snap); draw(); return; }
  if (drag.mode === "shape") {
    const [h, v] = toWorld(ev.offsetX, ev.offsetY);
    drag.delta = [snap(h - drag.start[0]), snap(v - drag.start[1])];
    setStatus(`Move ${drag.delta[0]} ${drag.delta[1]}`);
    draw();
    return;
  }''')
patch(J, '''  if (d.mode === "resize") {
    const [a, b] = axes(), h = d.handle, ax = h.dim ? b : a;''', '''  if (d.mode === "shape") {
    if (d.moved && (d.delta[0] || d.delta[1])) {
      const [a, b] = axes(), dd = [0, 0, 0], moves = [];
      dd[a] = d.delta[0]; dd[b] = d.delta[1];
      for (const it of selectedBrushes()) {
        const from = new Map();
        for (const p of it.polys) for (const q of p) if (d.handle.ends.includes(key2(q[a], q[b]))) from.set(q.join(","), q);
        if (from.size) moves.push({ e: it.e, i: it.i, from: [...from.values()] });
      }
      if (moves.length) { try { await post("/api/vertices", { moves, d: dd }); setStatus("Moved"); } catch (e) { /* the reason is shown */ } }
    }
    draw();
    return;
  }
  if (d.mode === "resize") {
    const [a, b] = axes(), h = d.handle, ax = h.dim ? b : a;''')

# ---- GUI test
T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''            const auto badBrush = web.handle("POST", "/api/brush", "{\\"mins\\":[0,0,0],\\"maxs\\":[0,64,64]}", true, {});''', '''            {
                // Corners: the upright edge at (4224, 4224) moves 64 units along x in one undo step; lifting one
                // corner of the flat top is refused and changes nothing.
                const size_t undoV = ctx.doc.undoPosition();
                const QString body = QString("{\\"moves\\":[{\\"e\\":%1,\\"i\\":%2,\\"from\\":[[4224,4224,0],[4224,4224,64]]}],\\"d\\":[64,0,0]}").arg(be).arg(bi);
                const auto mvv = web.handle("POST", "/api/vertices", body.toUtf8(), true, {});
                ctx.scene.sync(ctx.doc);
                AABB sheared;
                for (const auto* g : ctx.scene.items())
                    if (g->ref.entity == be && g->ref.item == bi) sheared = g->bounds;
                const bool one = ctx.doc.undoPosition() == undoV + 1;
                const QString lift = QString("{\\"moves\\":[{\\"e\\":%1,\\"i\\":%2,\\"from\\":[[4096,4096,64]]}],\\"d\\":[0,0,32]}").arg(be).arg(bi);
                const auto bad = web.handle("POST", "/api/vertices", lift.toUtf8(), true, {});
                const QString off = QString("{\\"moves\\":[{\\"e\\":%1,\\"i\\":%2,\\"from\\":[[1,2,3]]}],\\"d\\":[8,0,0]}").arg(be).arg(bi);
                const auto notCorner = web.handle("POST", "/api/vertices", off.toUtf8(), true, {});
                const bool kept = ctx.doc.undoPosition() == undoV + 1;
                ctx.doc.undo();
                ctx.documentEdited();
                ctx.scene.sync(ctx.doc);
                AABB restored;
                for (const auto* g : ctx.scene.items())
                    if (g->ref.entity == be && g->ref.item == bi) restored = g->bounds;
                const auto pageV = web.handle("GET", "/app.js", {}, true, {});
                r.check(mvv.status == 200 && one && sheared.valid() && std::fabs(sheared.maxs.x - 4288) < 0.01 && std::fabs(sheared.mins.x - 4096) < 0.01 &&
                            bad.status == 400 && notCorner.status == 400 && kept && std::fabs(restored.maxs.x - 4224) < 0.01 &&
                            pageV.body.contains("/api/vertices") && pageV.body.contains("shapeHandles"),
                        QString("POST /api/vertices moves brush corners as one undo step and refuses a move that breaks the brush (x max %1)").arg(sheared.maxs.x));
            }
            const auto badBrush = web.handle("POST", "/api/brush", "{\\"mins\\":[0,0,0],\\"maxs\\":[0,64,64]}", true, {});''')
print('ok')
