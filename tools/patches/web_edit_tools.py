import io

# Plan 3.4: editing tools in the browser.
#  - POST /api/brush {mins, maxs}: a box brush with the current material (the page's "Draw brush": drag a rectangle
#    in a 2D view).
#  - POST /api/resize {axis, factor, pivot}: scales the selection along one axis about a point (the page's handles
#    on the sides of the selection in a 2D view).
#  - GET /api/entityclasses, GET /api/entitydef?class=&e=: the entity definitions (description, keys with help,
#    type, default, required) and the problems of one entity; the inspector shows them.
#  - POST /api/entity {classname, at | view}: a point entity of any class.

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


def append(p, text):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    write(p, s.rstrip() + nl + text.replace('\n', nl))


# --- entity definitions reachable from the server -------------------------------------------------------------------
patch('src/qtapp/find_dialog.h', '''void setCheckMapEntityDefs(const EntityDefs* defs);
''', '''void setCheckMapEntityDefs(const EntityDefs* defs);
const EntityDefs* checkMapEntityDefs();  // null until the main window has loaded them
''')
patch('src/qtapp/find_dialog.cpp', '''void setCheckMapEntityDefs(const EntityDefs* defs) { g_defs = defs; }
''', '''void setCheckMapEntityDefs(const EntityDefs* defs) { g_defs = defs; }
const EntityDefs* checkMapEntityDefs() { return g_defs; }
''')

H = 'src/qtapp/web_server.h'
patch(H, '''//   GET  /api/commands               every editor command in menu order''', '''//   POST /api/brush    {mins, maxs}  a box brush with the current material (one undo step)
//   POST /api/resize   {axis, factor, pivot:[x,y,z]}   scales the selection along axis 0..2 about the pivot
//   GET  /api/entityclasses          the entity classes: {name, kind, description, built}
//   GET  /api/entitydef?class=C&e=ID one class: description, kind, built, keys [{name, type, default, description,
//                                    required, examples}]; with e, "problems": what Check Map says about that entity
//   POST /api/entity   {classname, at:[x,y,z] | view:{...}}   a point entity of that class
//   GET  /api/commands               every editor command in menu order''')
S = 'src/qtapp/web_server.cpp'
patch(S, '''#include "brush_tools.h"
''', '''#include "brush_tools.h"
#include "entity_defs.h"
#include "find_dialog.h"
''')
patch(S, '''        if (path == "/api/forms") {''', '''        if (path == "/api/entityclasses") {
            QJsonArray classes;
            if (const EntityDefs* defs = checkMapEntityDefs())
                for (const std::string& n : defs->classNames())
                    if (const ClassDef* c = defs->find(n))
                        classes.append(QJsonObject{{"name", qs(c->name)}, {"kind", qs(c->kind)}, {"description", qs(c->description).section('.', 0, 0)},
                                                   {"built", c->buildAccepted}});
            Response r;
            r.body = json({{"classes", classes}});
            return r;
        }
        if (path == "/api/entitydef") {
            const EntityDefs* defs = checkMapEntityDefs();
            const ClassDef* c = defs ? defs->find(q.queryItemValue("class").toStdString()) : nullptr;
            QJsonObject o{{"name", q.queryItemValue("class")}, {"known", c != nullptr}};
            if (c) {
                QJsonArray keys;
                for (const KeyDef& k : c->keys) {
                    QJsonArray examples;
                    for (const std::string& e : k.examples) examples.append(qs(e));
                    keys.append(QJsonObject{{"name", qs(k.name)}, {"type", qs(k.type)}, {"default", qs(k.defaultValue)},
                                            {"description", qs(k.description)}, {"required", k.required}, {"examples", examples}});
                }
                o.insert("description", qs(c->description));
                o.insert("kind", qs(c->kind));
                o.insert("built", c->buildAccepted);
                o.insert("keys", keys);
            }
            if (q.hasQueryItem("e") && defs)
                if (const Entity* e = ctx_->doc.findEntity(Id(q.queryItemValue("e").toUInt()))) {
                    QJsonArray problems;
                    for (const std::string& m : defs->validate(*e)) problems.append(qs(m));
                    o.insert("problems", problems);
                }
            Response r;
            r.body = json(o);
            return r;
        }
        if (path == "/api/forms") {''')
patch(S, '''    } else if (path == "/api/form") {''', '''    } else if (path == "/api/brush") {
        const QJsonArray lo = in.value("mins").toArray(), hi = in.value("maxs").toArray();
        if (lo.size() != 3 || hi.size() != 3) return error(400, "need mins:[x,y,z] and maxs:[x,y,z]");
        AABB box;
        box.add(Vec3{lo[0].toDouble(), lo[1].toDouble(), lo[2].toDouble()});
        box.add(Vec3{hi[0].toDouble(), hi[1].toDouble(), hi[2].toDouble()});
        for (int a = 0; a < 3; ++a)
            if (!(box.maxs[a] - box.mins[a] >= 1.0) || std::fabs(box.mins[a]) > 131072 || std::fabs(box.maxs[a]) > 131072)
                return error(400, "a brush is at least 1 unit on every side and inside the map's limits");
        if (!createBoxBrush(*ctx_, box)) return error(400, "the brush could not be made (no worldspawn?)");
        ctx_->documentEdited("Brush created from the browser");
    } else if (path == "/api/resize") {
        const int axis = in.value("axis").toInt(-1);
        const double factor = in.value("factor").toDouble(0);
        const QJsonArray pv = in.value("pivot").toArray();
        if (axis < 0 || axis > 2 || pv.size() != 3 || !(factor >= 0.01 && factor <= 100.0) || ctx_->doc.selection().empty())
            return error(400, "need axis 0..2, factor 0.01..100, pivot:[x,y,z] and a selection");
        if (scaleSelectionAbout(*ctx_, axis, factor, Vec3{pv[0].toDouble(), pv[1].toDouble(), pv[2].toDouble()}))
            ctx_->documentEdited("Resized from the browser");
    } else if (path == "/api/entity") {
        const std::string cls = in.value("classname").toString().trimmed().toStdString();
        const EntityDefs* defs = checkMapEntityDefs();
        const ClassDef* c = defs ? defs->find(cls) : nullptr;
        bool nameOk = !cls.empty() && cls.size() <= 64;
        for (const char ch : cls) nameOk = nameOk && ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_');
        if (!nameOk) return error(400, "a class name is a-z, 0-9 and _");
        if (c && c->kind == "brush") return error(400, qs(cls) + " is a brush class: select brushes and give them this class in the editor");
        QJsonArray at = in.value("at").toArray();
        if (in.value("view").isObject()) {
            const QJsonObject v = in.value("view").toObject();
            const QJsonArray pos = v.value("pos").toArray();
            WebCamera cam;
            for (int k = 0; k < 3 && pos.size() == 3; ++k) cam.pos[k] = pos[k].toDouble();
            cam.yaw = v.value("yaw").toDouble();
            cam.pitch = std::clamp(v.value("pitch").toDouble(), -89.0, 89.0);
            cam.fov = std::clamp(v.value("fov").toDouble(90), 10.0, 150.0);
            double point[3] = {0, 0, 0};
            if (pos.size() != 3 || !hooks_.surfacePoint ||
                !hooks_.surfacePoint(cam, std::clamp(v.value("x").toDouble(0.5), 0.0, 1.0), std::clamp(v.value("y").toDouble(0.5), 0.0, 1.0),
                                     std::clamp(v.value("aspect").toDouble(16.0 / 9.0), 0.2, 5.0), point))
                return error(400, "there is nothing under the centre of the view to place it on");
            at = QJsonArray{std::round(point[0]), std::round(point[1]), std::round(point[2])};
        }
        if (at.size() != 3) return error(400, "need at:[x,y,z] or view");
        const std::string origin = formatNumber(at[0].toDouble()) + " " + formatNumber(at[1].toDouble()) + " " + formatNumber(at[2].toDouble());
        const Id id = ctx_->doc.addEntity({{"classname", cls}, {"origin", origin}}, {}, "Place " + cls + " from the browser");
        ctx_->doc.select({id, kNoId}, false);
        ctx_->documentEdited(QString("Placed %1 from the browser").arg(qs(cls)));
    } else if (path == "/api/form") {''')

# --- the page -------------------------------------------------------------------------------------------------------
patch('data/web/index.html', '''  <button id="fit" title="Frame the whole map">Fit</button>
''', '''  <button id="fit" title="Frame the whole map">Fit</button>
  <button id="drawBrush" title="Then drag a rectangle in a Top, Front or Side view: a brush with the current material. Its third size is 64 units from 0; resize it in another view">Draw brush</button>
''')
patch('data/web/index.html', '''      <ul id="entities"></ul>
''', '''      <ul id="entities"></ul>
      <form id="addEntity"><input id="newClass" placeholder="entity class to place" list="classList" autocomplete="off"><datalist id="classList"></datalist><button>Place</button></form>
''')
patch('data/web/index.html', '''      <table id="keys"></table>
''', '''      <div id="classHelp"></div>
      <div id="entityProblems"></div>
      <table id="keys"></table>
''')
patch('data/web/index.html', '''<form id="addKey"><input id="newKey" placeholder="key">''', '''<form id="addKey"><input id="newKey" placeholder="key" list="keyList" autocomplete="off"><datalist id="keyList"></datalist>''')
append('data/web/style.css', '''
/* Editing tools (plan 3.4). */
#drawBrush.on { border-color: var(--accent); color: var(--accent); }
#addEntity { display: flex; gap: 4px; margin-top: 6px; }
#addEntity input { flex: 1; min-width: 0; }
#classHelp { color: var(--muted); font-size: 11px; margin: 0 0 4px; }
#entityProblems { color: #ff8080; font-size: 11px; margin: 0 0 4px; white-space: pre-wrap; }
td.required { color: var(--accent); }
''')
J = 'data/web/app.js'
# Draw: the selection's resize handles and the rectangle being drawn.
patch(J, '''  for (const it of map.items) if (sel.has(selKey(it.e, it.i)) || sel.has(selKey(it.e, -1))) drawItem(it, true);
}''', '''  for (const it of map.items) if (sel.has(selKey(it.e, it.i)) || sel.has(selKey(it.e, -1))) drawItem(it, true);
  // The selection's box with a handle on each side (drag one to resize along that axis).
  const box = selectionBox();
  if (box) {
    let lo = [box.lo[a], box.lo[b]], hi = [box.hi[a], box.hi[b]];
    if (drag && drag.mode === "resize") { lo = drag.lo.slice(); hi = drag.hi.slice(); }
    const [x0, y1] = toScreen(lo[0], lo[1]), [x1, y0] = toScreen(hi[0], hi[1]);
    g.strokeStyle = "#e8b84a"; g.lineWidth = 1; g.setLineDash([4, 3]);
    g.strokeRect(x0, y0, x1 - x0, y1 - y0);
    g.setLineDash([]);
    g.fillStyle = "#e8b84a";
    for (const [hx, hy] of [[x0, (y0 + y1) / 2], [x1, (y0 + y1) / 2], [(x0 + x1) / 2, y0], [(x0 + x1) / 2, y1]]) g.fillRect(hx - 4, hy - 4, 8, 8);
    if (drag && drag.mode === "resize") setStatus(`Size ${(hi[0] - lo[0]).toFixed(0)} x ${(hi[1] - lo[1]).toFixed(0)}`);
  }
  if (drag && drag.mode === "draw" && drag.moved) {
    const [x0, y0] = toScreen(drag.from[0], drag.from[1]), [x1, y1] = toScreen(drag.to[0], drag.to[1]);
    g.strokeStyle = "#7fd0ff"; g.lineWidth = 1.5;
    g.strokeRect(Math.min(x0, x1), Math.min(y0, y1), Math.abs(x1 - x0), Math.abs(y1 - y0));
  }
}
// The box around the selected brushes and patches (world space), or null.
function selectionBox() {
  const sel = selectedSet();
  let lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity], any = false;
  for (const it of map.items) {
    if (it.kind === "entity" || (!sel.has(selKey(it.e, it.i)) && !sel.has(selKey(it.e, -1)))) continue;
    for (const p of it.polys) for (const q of p) for (let k = 0; k < 3; ++k) { lo[k] = Math.min(lo[k], q[k]); hi[k] = Math.max(hi[k], q[k]); }
    any = true;
  }
  return any ? { lo, hi } : null;
}
// The handle of the selection's box under a canvas point: {side: 0 low | 1 high, dim: 0 horizontal | 1 vertical}.
function handleAt(x, y) {
  const box = selectionBox();
  if (!box) return null;
  const [a, b] = axes();
  const [x0, y1] = toScreen(box.lo[a], box.lo[b]), [x1, y0] = toScreen(box.hi[a], box.hi[b]);
  const spots = [[x0, (y0 + y1) / 2, 0, 0], [x1, (y0 + y1) / 2, 0, 1], [(x0 + x1) / 2, y1, 1, 0], [(x0 + x1) / 2, y0, 1, 1]];
  for (const [hx, hy, dim, side] of spots) if (Math.abs(x - hx) <= 7 && Math.abs(y - hy) <= 7) return { dim, side, box };
  return null;
}''')
patch(J, '''canvas.addEventListener("pointerdown", (ev) => {
  canvas.setPointerCapture(ev.pointerId);
  const hit = pick(ev.offsetX, ev.offsetY);''', '''let drawing = false;   // "Draw brush" is on: the next drag in a 2D view makes a brush
$("drawBrush").onclick = () => { drawing = !drawing; $("drawBrush").classList.toggle("on", drawing); setStatus(drawing ? "Drag a rectangle in the view" : ""); };
canvas.addEventListener("pointerdown", (ev) => {
  canvas.setPointerCapture(ev.pointerId);
  if (drawing) {
    const w = toWorld(ev.offsetX, ev.offsetY).map(snap);
    drag = { mode: "draw", x: ev.offsetX, y: ev.offsetY, from: w, to: w, moved: false };
    return;
  }
  const handle = handleAt(ev.offsetX, ev.offsetY);
  if (handle) {
    const [a, b] = axes();
    drag = { mode: "resize", x: ev.offsetX, y: ev.offsetY, handle, lo: [handle.box.lo[a], handle.box.lo[b]], hi: [handle.box.hi[a], handle.box.hi[b]], moved: false };
    return;
  }
  const hit = pick(ev.offsetX, ev.offsetY);''')
patch(J, '''  if (Math.abs(ev.offsetX - drag.x) + Math.abs(ev.offsetY - drag.y) > 3) drag.moved = true;
  if (drag.mode === "pan" && drag.moved) {''', '''  if (Math.abs(ev.offsetX - drag.x) + Math.abs(ev.offsetY - drag.y) > 3) drag.moved = true;
  if (drag.mode === "draw") { drag.to = toWorld(ev.offsetX, ev.offsetY).map(snap); draw(); return; }
  if (drag.mode === "resize") {
    const w = toWorld(ev.offsetX, ev.offsetY).map(snap), h = drag.handle, step = state.grid || 8;
    if (h.side) drag.hi[h.dim] = Math.max(w[h.dim], drag.lo[h.dim] + step); else drag.lo[h.dim] = Math.min(w[h.dim], drag.hi[h.dim] - step);
    draw();
    return;
  }
  if (drag.mode === "pan" && drag.moved) {''')
patch(J, '''  const d = drag; drag = null;
  if (!d) return;
  if (d.mode === "move" && d.moved && d.delta.some((x) => x)) { await post("/api/move", { d: d.delta }); return; }''', '''  const d = drag; drag = null;
  if (!d) return;
  if (d.mode === "draw") {
    drawing = false; $("drawBrush").classList.remove("on");
    const [a, b] = axes(), third = 3 - a - b;
    const mins = [0, 0, 0], maxs = [0, 0, 0];
    mins[a] = Math.min(d.from[0], d.to[0]); maxs[a] = Math.max(d.from[0], d.to[0]);
    mins[b] = Math.min(d.from[1], d.to[1]); maxs[b] = Math.max(d.from[1], d.to[1]);
    mins[third] = 0; maxs[third] = 64;
    if (d.moved && maxs[a] > mins[a] && maxs[b] > mins[b]) { try { await post("/api/brush", { mins, maxs }); setStatus("Brush made; resize it in another view"); } catch (e) { /* shown */ } }
    draw();
    return;
  }
  if (d.mode === "resize") {
    const [a, b] = axes(), h = d.handle, ax = h.dim ? b : a;
    const was = h.box.hi[ax] - h.box.lo[ax], now = d.hi[h.dim] - d.lo[h.dim];
    if (d.moved && was > 0 && now > 0 && Math.abs(now - was) > 1e-6) {
      const pivot = [0, 1, 2].map((k) => (h.box.lo[k] + h.box.hi[k]) / 2);
      pivot[ax] = h.side ? h.box.lo[ax] : h.box.hi[ax];   // the opposite side stays where it is
      try { await post("/api/resize", { axis: ax, factor: now / was, pivot }); } catch (e) { /* shown */ }
    }
    draw();
    return;
  }
  if (d.mode === "move" && d.moved && d.delta.some((x) => x)) { await post("/api/move", { d: d.delta }); return; }''')
# The inspector with the entity definitions.
patch(J, '''function renderInspector() {
  const e = inspected(), t = $("keys"); t.textContent = "";
  $("inspTitle").textContent = e ? e.classname : state.selection.length ? state.selection.length + " selected" : "nothing selected";
  if (!e) return;
  for (const [k, v] of Object.entries(e.keys)) {
    const tr = document.createElement("tr"), tk = document.createElement("td"), tv = document.createElement("td");
    tk.textContent = k;
    const inp = document.createElement("input"); inp.value = v;''', '''// Entity definitions (GET /api/entitydef): what a class is, its keys with help, and what Check Map says about the
// inspected entity. Asked once per class and again for the entity whenever it changes.
const classDefs = new Map();
let defAsked = "";
async function loadDef(e) {
  const stamp = e.id + ":" + JSON.stringify(e.keys);
  if (defAsked === stamp) return;
  defAsked = stamp;
  try {
    const d = await get(`/api/entitydef?class=${encodeURIComponent(e.classname)}&e=${e.id}`);
    classDefs.set(e.classname, d);
    const cur = inspected();
    if (cur && cur.id === e.id) renderInspector();
  } catch (_) { /* the inspector works without definitions */ }
}
function renderInspector() {
  const e = inspected(), t = $("keys"); t.textContent = "";
  $("inspTitle").textContent = e ? e.classname : state.selection.length ? state.selection.length + " selected" : "nothing selected";
  $("classHelp").textContent = ""; $("entityProblems").textContent = ""; $("keyList").textContent = "";
  if (!e) return;
  const def = classDefs.get(e.classname);
  loadDef(e);
  const keyDefs = new Map(((def && def.keys) || []).map((k) => [k.name, k]));
  if (def && def.known) $("classHelp").textContent = def.description + (def.built ? "" : "  (The build leaves this class out.)");
  if (def && def.problems && def.problems.length && defAsked === e.id + ":" + JSON.stringify(e.keys)) $("entityProblems").textContent = def.problems.join("\\n");
  for (const k of keyDefs.values()) {
    if (k.name in e.keys) continue;
    const o = document.createElement("option");
    o.value = k.name; o.label = (k.required ? "required: " : "") + k.description;
    $("keyList").append(o);
  }
  for (const [k, v] of Object.entries(e.keys)) {
    const tr = document.createElement("tr"), tk = document.createElement("td"), tv = document.createElement("td");
    tk.textContent = k;
    const kd = keyDefs.get(k);
    if (kd) { tk.title = kd.description + (kd.type ? "  (" + kd.type + (kd.default ? ", default " + kd.default : "") + ")" : ""); if (kd.required) tk.className = "required"; }
    const inp = document.createElement("input"); inp.value = v;
    if (kd && kd.examples && kd.examples.length) inp.placeholder = kd.examples[0];''')
append(J, '''
// --- Placing an entity of any class (plan 3.4): on the surface at the live view's centre, else at the 2D view centre. ---
get("/api/entityclasses").then((j) => {
  for (const c of j.classes || []) {
    if (c.kind !== "point") continue;
    const o = document.createElement("option");
    o.value = c.name; o.label = c.description + (c.built ? "" : " (not built)");
    $("classList").append(o);
  }
}).catch(() => {});
$("addEntity").onsubmit = async (ev) => {
  ev.preventDefault();
  const classname = $("newClass").value.trim();
  if (!classname) return;
  const body = { classname };
  const liveCam = !$("viewLive").hidden && window.viewLive && window.viewLive.camera();
  if (liveCam) {
    const r = $("viewLive").getBoundingClientRect();
    body.view = { pos: liveCam.pos, yaw: liveCam.yaw, pitch: liveCam.pitch, fov: liveCam.fov, x: 0.5, y: 0.5, aspect: r.width / r.height };
  } else {
    const [h, v] = axes();
    body.at = [0, 0, 0]; body.at[h] = snap(view.cx); body.at[v] = snap(view.cy);
  }
  try { await post("/api/entity", body); setStatus("Placed " + classname); $("newClass").value = ""; } catch (e) { /* shown */ }
};
''')

# --- GUI test -------------------------------------------------------------------------------------------------------
T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''        // Edits go through the shared document (one undo history).
        const std::string before = ctx.doc.text();''', '''        {
            // Plan 3.4: a brush drawn in the browser, a resize by a handle, the entity definitions and any class placed.
            const size_t undoE = ctx.doc.undoPosition();
            const auto brushCount = [&] {
                ctx.scene.sync(ctx.doc);
                int n = 0;
                for (const auto* g : ctx.scene.items()) n += g->kind == iw8r::app::PrimGeom::Brush ? 1 : 0;
                return n;
            };
            const int brushesBefore = brushCount();
            ctx.doc.deselectAll();
            const auto mk = web.handle("POST", "/api/brush", "{\\"mins\\":[4096,4096,0],\\"maxs\\":[4160,4224,64]}", true, {});
            const int brushesAfter = brushCount();
            Id be = 0, bi = 0;
            AABB made;
            for (const auto* g : ctx.scene.items())
                if (g->kind == iw8r::app::PrimGeom::Brush && g->bounds.valid() && g->bounds.mins.x > 4000) be = g->ref.entity, bi = g->ref.item, made = g->bounds;
            r.check(mk.status == 200 && brushesAfter == brushesBefore + 1 && std::fabs(made.mins.x - 4096) < 0.01 && std::fabs(made.maxs.y - 4224) < 0.01 &&
                        std::fabs(made.maxs.z - 64) < 0.01,
                    "POST /api/brush makes a box brush of the given size");
            ctx.doc.select({be, bi}, false);
            const auto rs = web.handle("POST", "/api/resize", "{\\"axis\\":0,\\"factor\\":2,\\"pivot\\":[4096,4160,32]}", true, {});
            ctx.scene.sync(ctx.doc);
            AABB grown;
            for (const auto* g : ctx.scene.items())
                if (g->ref.entity == be && g->ref.item == bi) grown = g->bounds;
            r.check(rs.status == 200 && grown.valid() && std::fabs(grown.mins.x - 4096) < 0.01 && std::fabs(grown.maxs.x - 4224) < 0.01 &&
                        std::fabs(grown.maxs.y - 4224) < 0.01,
                    QString("POST /api/resize scales the selection along one axis about the pivot (x %1 .. %2)").arg(grown.mins.x).arg(grown.maxs.x));
            const auto badBrush = web.handle("POST", "/api/brush", "{\\"mins\\":[0,0,0],\\"maxs\\":[0,64,64]}", true, {});
            const auto badResize = web.handle("POST", "/api/resize", "{\\"axis\\":5,\\"factor\\":2,\\"pivot\\":[0,0,0]}", true, {});
            r.check(badBrush.status == 400 && badResize.status == 400, "a flat brush and a bad resize are refused");
            const QJsonArray classes = QJsonDocument::fromJson(web.handle("GET", "/api/entityclasses", {}, true, {}).body).object().value("classes").toArray();
            const QJsonObject def = QJsonDocument::fromJson(web.handle("GET", "/api/entitydef?class=mp_tdm_spawn", {}, true, {}).body).object();
            bool hasOrigin = false;
            for (const QJsonValue& k : def.value("keys").toArray()) hasOrigin = hasOrigin || k.toObject().value("name").toString() == "origin";
            const auto pl = web.handle("POST", "/api/entity", "{\\"classname\\":\\"mp_tdm_spawn\\",\\"at\\":[4096,4096,64]}", true, {});
            const Entity* placedEnt = ctx.doc.map().entities.empty() ? nullptr : &ctx.doc.map().entities.back();
            const bool placedOk = pl.status == 200 && placedEnt && placedEnt->classname() == "mp_tdm_spawn" && placedEnt->value("origin") &&
                                  *placedEnt->value("origin") == "4096 4096 64";
            const QJsonObject withProblems = placedEnt ? QJsonDocument::fromJson(web.handle("GET", QString("/api/entitydef?class=mp_tdm_spawn&e=%1").arg(placedEnt->id).toUtf8(), {}, true, {}).body).object()
                                                       : QJsonObject();
            const auto badClass = web.handle("POST", "/api/entity", "{\\"classname\\":\\"Bad Class\\",\\"at\\":[0,0,0]}", true, {});
            r.check(classes.size() > 20 && def.value("known").toBool() && !def.value("description").toString().isEmpty() && hasOrigin && placedOk &&
                        withProblems.contains("problems") && badClass.status == 400,
                    QString("the entity definitions reach the browser (%1 classes; mp_tdm_spawn with its keys) and any point class can be placed").arg(classes.size()));
            while (ctx.doc.undoPosition() > undoE) ctx.doc.undo();
            ctx.doc.deselectAll();
            ctx.documentEdited();
            auto editJs = web.handle("GET", "/app.js", {}, true, {});
            auto editPage = web.handle("GET", "/", {}, true, {});
            r.check(brushCount() == brushesBefore && editJs.body.contains("/api/brush") && editJs.body.contains("/api/resize") &&
                        editJs.body.contains("/api/entitydef") && editPage.body.contains("drawBrush") && editPage.body.contains("classHelp"),
                    "the browser page has Draw brush, the resize handles and the inspector's key help");
        }
        // Edits go through the shared document (one undo history).
        const std::string before = ctx.doc.text();''')
print('ok')
