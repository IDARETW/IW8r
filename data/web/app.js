// IW8 Radiant Web: the browser client of the editor's web server (src/qtapp/web_server.h).
"use strict";

const token = new URLSearchParams(location.search).get("token");
const api = (path) => path + (token ? (path.includes("?") ? "&" : "?") + "token=" + encodeURIComponent(token) : "");
async function get(path) {
  const r = await fetch(api(path), { credentials: "same-origin" });
  if (!r.ok) throw new Error((await r.json().catch(() => ({}))).error || r.statusText);
  return r.json();
}
async function post(path, body) {
  const r = await fetch(api(path), { method: "POST", credentials: "same-origin", headers: { "Content-Type": "application/json" },
                                     body: JSON.stringify(body || {}) });
  const j = await r.json().catch(() => ({}));
  if (!r.ok) { setStatus(j.error || r.statusText); throw new Error(j.error); }
  return j;
}

const $ = (id) => document.getElementById(id);
const canvas = $("view"), g = canvas.getContext("2d");
let map = { items: [], entities: [] }, state = { selection: [], grid: 8 };
let axis = 0;                      // 0 top XY, 1 front XZ, 2 side YZ
let view = { cx: 0, cy: 0, scale: 0.2 };
let fitted = false;
const axes = () => [[0, 1], [0, 2], [1, 2]][axis];

function setStatus(t) { $("status").textContent = t; }
function hex(c) { const r = c & 255, gg = (c >> 8) & 255, b = (c >> 16) & 255; return `rgb(${r},${gg},${b})`; }
const selKey = (e, i) => e + ":" + i;
function selectedSet() { return new Set(state.selection.map(([e, i]) => selKey(e, i))); }

function resize() {
  const r = canvas.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, r.width * dpr); canvas.height = Math.max(1, r.height * dpr);
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  draw();
}
function toScreen(h, v) {
  const r = canvas.getBoundingClientRect();
  return [r.width / 2 + (h - view.cx) * view.scale, r.height / 2 - (v - view.cy) * view.scale];
}
function toWorld(x, y) {
  const r = canvas.getBoundingClientRect();
  return [view.cx + (x - r.width / 2) / view.scale, view.cy - (y - r.height / 2) / view.scale];
}

function fit() {
  const [a, b] = axes();
  let lo = [Infinity, Infinity], hi = [-Infinity, -Infinity];
  for (const it of map.items) for (const p of it.polys) for (const q of p) {
    lo = [Math.min(lo[0], q[a]), Math.min(lo[1], q[b])]; hi = [Math.max(hi[0], q[a]), Math.max(hi[1], q[b])];
  }
  if (!isFinite(lo[0])) return;
  const r = canvas.getBoundingClientRect();
  view.cx = (lo[0] + hi[0]) / 2; view.cy = (lo[1] + hi[1]) / 2;
  view.scale = 0.9 * Math.min(r.width / Math.max(64, hi[0] - lo[0]), r.height / Math.max(64, hi[1] - lo[1]));
  draw();
}

function draw() {
  const r = canvas.getBoundingClientRect();
  g.clearRect(0, 0, r.width, r.height);
  // Grid.
  let step = state.grid || 8;
  while (step * view.scale < 8) step *= 2;
  const [h0, v1] = toWorld(0, 0), [h1, v0] = toWorld(r.width, r.height);
  g.lineWidth = 1;
  for (const [s, col] of [[step, "#26292f"], [Math.max(64, step * 8), "#353943"]]) {
    g.strokeStyle = col; g.beginPath();
    for (let h = Math.floor(h0 / s) * s; h <= h1; h += s) { const [x] = toScreen(h, 0); g.moveTo(x, 0); g.lineTo(x, r.height); }
    for (let v = Math.floor(v0 / s) * s; v <= v1; v += s) { const [, y] = toScreen(0, v); g.moveTo(0, y); g.lineTo(r.width, y); }
    g.stroke();
  }
  const [a, b] = axes();
  const sel = selectedSet();
  const drawItem = (it, selected) => {
    g.strokeStyle = selected ? "#ff5050" : hex(it.color);
    g.lineWidth = selected ? 1.6 : 1;
    g.beginPath();
    for (const p of it.polys) {
      p.forEach((q, k) => {
        const so = selected ? shapeOffset(q, a, b) : null;
        const [x, y] = toScreen(q[a] + dragOffset(it, a) + (so ? so[0] : 0), q[b] + dragOffset(it, b) + (so ? so[1] : 0));
        k ? g.lineTo(x, y) : g.moveTo(x, y);
      });
      g.closePath();
    }
    g.stroke();
  };
  for (const it of map.items) if (!sel.has(selKey(it.e, it.i)) && !sel.has(selKey(it.e, -1))) drawItem(it, false);
  for (const it of map.items) if (sel.has(selKey(it.e, it.i)) || sel.has(selKey(it.e, -1))) drawItem(it, true);
  // Vertices / Edges: a handle on every corner or edge of the selected brushes.
  const shaping = !!$("editMode").value;
  for (const hd of shapeHandles()) {
    const moving = drag && drag.mode === "shape" && drag.handle.key === hd.key;
    const [x, y] = toScreen(hd.h + (moving ? drag.delta[0] : 0), hd.v + (moving ? drag.delta[1] : 0));
    g.fillStyle = moving ? "#ffffff" : $("editMode").value === "vertex" ? "#7fd0ff" : "#8fe08a";
    g.fillRect(x - 4, y - 4, 8, 8);
  }
  // The selection's box with a handle on each side (drag one to resize along that axis).
  const box = shaping ? null : selectionBox();
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
// Vertices / Edges (plan 3.4). A handle stands for every corner of the selected brushes under it in this view (a
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
// The handle of the selection's box under a canvas point: {side: 0 low | 1 high, dim: 0 horizontal | 1 vertical}.
function handleAt(x, y) {
  const box = selectionBox();
  if (!box) return null;
  const [a, b] = axes();
  const [x0, y1] = toScreen(box.lo[a], box.lo[b]), [x1, y0] = toScreen(box.hi[a], box.hi[b]);
  const spots = [[x0, (y0 + y1) / 2, 0, 0], [x1, (y0 + y1) / 2, 0, 1], [(x0 + x1) / 2, y1, 1, 0], [(x0 + x1) / 2, y0, 1, 1]];
  for (const [hx, hy, dim, side] of spots) if (Math.abs(x - hx) <= 7 && Math.abs(y - hy) <= 7) return { dim, side, box };
  return null;
}

// Dragging the selection previews the move locally; the server moves it on release (one undo step).
let drag = null;
function dragOffset(it, ax) {
  if (!drag || drag.mode !== "move") return 0;
  const sel = selectedSet();
  if (!sel.has(selKey(it.e, it.i)) && !sel.has(selKey(it.e, -1))) return 0;
  return drag.delta[ax] || 0;
}
const snap = (x) => Math.round(x / (state.grid || 8)) * (state.grid || 8);

function pick(x, y) {
  // Smallest item whose 2D bounds hold the point (good enough for a top view).
  const [h, v] = toWorld(x, y), [a, b] = axes();
  let best = null, bestArea = Infinity;
  for (const it of map.items) {
    let lo = [Infinity, Infinity], hi = [-Infinity, -Infinity];
    for (const p of it.polys) for (const q of p) { lo = [Math.min(lo[0], q[a]), Math.min(lo[1], q[b])]; hi = [Math.max(hi[0], q[a]), Math.max(hi[1], q[b])]; }
    const pad = 4 / view.scale;
    if (h < lo[0] - pad || h > hi[0] + pad || v < lo[1] - pad || v > hi[1] + pad) continue;
    const area = (hi[0] - lo[0] + pad) * (hi[1] - lo[1] + pad);
    if (area < bestArea) { bestArea = area; best = it; }
  }
  return best;
}

let drawing = false;   // "Draw brush" is on: the next drag in a 2D view makes a brush
$("drawBrush").onclick = () => { drawing = !drawing; $("drawBrush").classList.toggle("on", drawing); setStatus(drawing ? "Drag a rectangle in the view" : ""); };
canvas.addEventListener("pointerdown", (ev) => {
  canvas.setPointerCapture(ev.pointerId);
  if (drawing) {
    const w = toWorld(ev.offsetX, ev.offsetY).map(snap);
    drag = { mode: "draw", x: ev.offsetX, y: ev.offsetY, from: w, to: w, moved: false };
    return;
  }
  const shape = shapeHandleAt(ev.offsetX, ev.offsetY);
  if (shape) {
    drag = { mode: "shape", x: ev.offsetX, y: ev.offsetY, handle: shape, start: toWorld(ev.offsetX, ev.offsetY), delta: [0, 0], moved: false };
    return;
  }
  const handle = $("editMode").value ? null : handleAt(ev.offsetX, ev.offsetY);
  if (handle) {
    const [a, b] = axes();
    drag = { mode: "resize", x: ev.offsetX, y: ev.offsetY, handle, lo: [handle.box.lo[a], handle.box.lo[b]], hi: [handle.box.hi[a], handle.box.hi[b]], moved: false };
    return;
  }
  const hit = pick(ev.offsetX, ev.offsetY);
  const sel = selectedSet();
  if (hit && (sel.has(selKey(hit.e, hit.i)) || sel.has(selKey(hit.e, -1))) && !ev.shiftKey) {
    drag = { mode: "move", x: ev.offsetX, y: ev.offsetY, start: toWorld(ev.offsetX, ev.offsetY), delta: [0, 0, 0], moved: false };
  } else {
    drag = { mode: "pan", x: ev.offsetX, y: ev.offsetY, hit, shift: ev.shiftKey, moved: false };
  }
});
canvas.addEventListener("pointermove", (ev) => {
  if (!drag) return;
  if (Math.abs(ev.offsetX - drag.x) + Math.abs(ev.offsetY - drag.y) > 3) drag.moved = true;
  if (drag.mode === "draw") { drag.to = toWorld(ev.offsetX, ev.offsetY).map(snap); draw(); return; }
  if (drag.mode === "shape") {
    const [h, v] = toWorld(ev.offsetX, ev.offsetY);
    drag.delta = [snap(h - drag.start[0]), snap(v - drag.start[1])];
    setStatus(`Move ${drag.delta[0]} ${drag.delta[1]}`);
    draw();
    return;
  }
  if (drag.mode === "resize") {
    const w = toWorld(ev.offsetX, ev.offsetY).map(snap), h = drag.handle, step = state.grid || 8;
    if (h.side) drag.hi[h.dim] = Math.max(w[h.dim], drag.lo[h.dim] + step); else drag.lo[h.dim] = Math.min(w[h.dim], drag.hi[h.dim] - step);
    draw();
    return;
  }
  if (drag.mode === "pan" && drag.moved) {
    view.cx -= (ev.offsetX - drag.x) / view.scale; view.cy += (ev.offsetY - drag.y) / view.scale;
    drag.x = ev.offsetX; drag.y = ev.offsetY; draw();
  } else if (drag.mode === "move") {
    const [h, v] = toWorld(ev.offsetX, ev.offsetY), [a, b] = axes();
    drag.delta = [0, 0, 0];
    drag.delta[a] = snap(h - drag.start[0]); drag.delta[b] = snap(v - drag.start[1]);
    setStatus(`Move ${drag.delta.join(" ")}`); draw();
  }
});
canvas.addEventListener("pointerup", async (ev) => {
  const d = drag; drag = null;
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
  if (d.mode === "shape") {
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
  if (d.mode === "move" && d.moved && d.delta.some((x) => x)) { await post("/api/move", { d: d.delta }); return; }
  if (!d.moved) {
    if (d.hit) await post("/api/select", { e: d.hit.e, i: d.hit.i, add: d.shift });
    else await post("/api/deselect");
  }
  draw();
});
canvas.addEventListener("wheel", (ev) => {
  ev.preventDefault();
  const before = toWorld(ev.offsetX, ev.offsetY);
  view.scale *= ev.deltaY < 0 ? 1.2 : 1 / 1.2;
  const after = toWorld(ev.offsetX, ev.offsetY);
  view.cx += before[0] - after[0]; view.cy += before[1] - after[1];
  draw();
}, { passive: false });

// Entity list and inspector.
function renderEntities() {
  const f = $("filter").value.trim().toLowerCase();
  const sel = new Set(state.selection.map(([e]) => e));
  const ul = $("entities"); ul.textContent = "";
  for (const e of map.entities) {
    const text = e.classname + " " + Object.entries(e.keys).map(([k, v]) => k + " " + v).join(" ");
    if (f && !text.toLowerCase().includes(f)) continue;
    const li = document.createElement("li");
    li.className = sel.has(e.id) ? "sel" : "";
    const name = e.keys.targetname || e.keys.model || "";
    li.innerHTML = "";
    li.append(e.classname + " ");
    const sm = document.createElement("small"); sm.textContent = name; li.append(sm);
    li.onclick = (ev) => post("/api/select", { e: e.id, i: -1, add: ev.shiftKey });
    ul.append(li);
  }
  renderInspector();
}
function inspected() {
  const ids = [...new Set(state.selection.map(([e]) => e))];
  return ids.length === 1 ? map.entities.find((e) => e.id === ids[0]) : null;
}
// Entity definitions (GET /api/entitydef): what a class is, its keys with help, and what Check Map says about the
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
  if (def && def.problems && def.problems.length && defAsked === e.id + ":" + JSON.stringify(e.keys)) $("entityProblems").textContent = def.problems.join("\n");
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
    if (kd && kd.examples && kd.examples.length) inp.placeholder = kd.examples[0];
    inp.onchange = () => post("/api/key", { e: e.id, key: k, value: inp.value });
    inp.onkeydown = (ev) => { if (ev.key === "Delete" && ev.ctrlKey) post("/api/key", { e: e.id, key: k, value: null }); };
    inp.title = "Enter to set; Ctrl+Delete removes the key";
    tv.append(inp); tr.append(tk, tv); t.append(tr);
  }
}
$("addKey").onsubmit = (ev) => {
  ev.preventDefault();
  const e = inspected(), k = $("newKey").value.trim();
  if (e && k) post("/api/key", { e: e.id, key: k, value: $("newValue").value }).then(() => { $("newKey").value = $("newValue").value = ""; });
};
$("filter").oninput = renderEntities;
$("axis").onchange = () => {
  const v = +$("axis").value;
  const is3d = v === 3, live = v === 4;
  $("view").hidden = is3d || live; $("view3d").hidden = !is3d; $("viewLive").hidden = !live;
  if (window.viewLive) window.viewLive.shown(live, state);
  if (live) {
    $("hint").textContent = "Drag: look · W A S D, Q E: fly (Shift faster) · Wheel: forward / back · Right-drag: slide · Click: select (Shift adds)";
    return;
  }
  $("hint").textContent = is3d ? "Drag: orbit · Right-drag: pan · Wheel: zoom · Click: select (Shift adds)"
                               : "Drag: pan (empty space) or move the selection · Wheel: zoom · Click: select (Shift adds)";
  if (is3d) { window.view3d && window.view3d.show(map, state); return; }
  axis = v; fit();
};
$("fit").onclick = fit;
$("undo").onclick = () => post("/api/undo");
$("redo").onclick = () => post("/api/redo");
$("save").onclick = () => post("/api/save").then(() => setStatus("Saved"));
$("compile").onclick = () => post("/api/compile").then(() => setStatus("Compile started (see the log)"));
// Roadmap 10.6: deploy / build and play / load in game, and the asset browsers (thumbnails, apply, place).
$("deploy").onclick = () => post("/api/deploy").then(() => setStatus("Deploy started (see the log)"));
$("build").onclick = () => post("/api/build").then(() => setStatus("Build, deploy and load started (see the log)"));
$("loadGame").onclick = () => post("/api/load").then(() => setStatus("Sent the load to the game"));
let assetTimer = 0;
async function searchAssets() {
  const kind = $("assetKind").value, q = $("assetQuery").value.trim();
  const list = $("assetList");
  list.textContent = "";
  let res;
  try { res = await get(`/api/assets?kind=${encodeURIComponent(kind)}&q=${encodeURIComponent(q)}&limit=80`); }
  catch (e) { $("assetMore").textContent = String(e.message || e); return; }
  for (const it of res.names) {
    const li = document.createElement("li");
    if (kind === "materials" || kind === "models" || kind === "prefabs") {
      const img = document.createElement("img");
      img.loading = "lazy";
      img.alt = "";
      img.src = api(`/api/thumb?kind=${encodeURIComponent(kind)}&name=${encodeURIComponent(kind === "prefabs" ? it.id : it.name)}`);
      img.onerror = () => { img.style.visibility = "hidden"; };
      li.appendChild(img);
    }
    const span = document.createElement("span");
    span.textContent = it.name + (it.loop ? "  (loop)" : "") + (it.note ? "  (cannot be placed)" : "");
    li.appendChild(span);
    li.title = it.note || it.name;
    if (kind === "materials" || kind === "models" || kind === "effects") {
      const b = document.createElement("button");
      b.type = "button"; b.className = "info"; b.textContent = "i"; b.title = "Info about " + it.name;
      b.onclick = (ev) => { ev.stopPropagation(); window.radiant.openInfo(kind, it.name); };
      li.appendChild(b);
    }
    li.onclick = async () => {
      if (kind === "materials") { await post("/api/material", { name: it.name }); setStatus("Material " + it.name); return; }
      if (kind === "prefabs") {
        // In front of the live view's camera when it is shown, else of the editor's own camera.
        const body = { kind, name: it.id };
        const pc = !$("viewLive").hidden && window.viewLive && window.viewLive.camera();
        if (pc) body.view = { pos: pc.pos, yaw: pc.yaw, pitch: pc.pitch, fov: pc.fov };
        try { await post("/api/place", body); setStatus("Placed prefab " + it.name); } catch (e) { /* shown */ }
        return;
      }
      // In the live editor view: on the surface under the view's centre. Else at the 2D view centre (third axis 0).
      const liveCam = !$("viewLive").hidden && window.viewLive && window.viewLive.camera();
      if (liveCam) {
        const r = $("viewLive").getBoundingClientRect();
        await post("/api/place", { kind, name: it.name, loop: !!it.loop,
                                   view: { pos: liveCam.pos, yaw: liveCam.yaw, pitch: liveCam.pitch, fov: liveCam.fov, x: 0.5, y: 0.5, aspect: r.width / r.height } });
        setStatus("Placed " + it.name + " on the surface at the view's centre");
        return;
      }
      const [h, v] = axes(), at = [0, 0, 0];
      at[h] = view.cx; at[v] = view.cy;
      await post("/api/place", { kind, name: it.name, at, loop: !!it.loop });
      setStatus("Placed " + it.name);
    };
    list.appendChild(li);
  }
  $("assetMore").textContent = res.total > res.names.length ? `${res.names.length} of ${res.total}; refine the search` : `${res.total} found`;
}
$("assetKind").onchange = searchAssets;
$("assetQuery").oninput = () => { clearTimeout(assetTimer); assetTimer = setTimeout(searchAssets, 250); };
searchAssets();

// The map comes whole once, then as differences (GET /api/map?since=): changed items and entities replace their
// old ones, the ones in `removed` go.
let mapRev = -1, mapEpoch = "";
function applyMap(m) {
  if (m.full || mapRev < 0) {
    map = { items: m.items || [], entities: m.entities || [] };
  } else {
    const gone = new Set((m.removed || []).map(([e, i]) => selKey(e, i)));
    const fresh = new Map((m.items || []).map((it) => [selKey(it.e, it.i), it]));
    const items = [];
    for (const it of map.items) {
      const k = selKey(it.e, it.i);
      if (gone.has(k)) continue;
      if (fresh.has(k)) { items.push(fresh.get(k)); fresh.delete(k); } else items.push(it);
    }
    for (const it of fresh.values()) items.push(it);
    const goneEnts = new Set(m.removedEntities || []);
    const freshEnts = new Map((m.entities || []).map((e) => [e.id, e]));
    const ents = [];
    for (const e of map.entities) {
      if (goneEnts.has(e.id)) continue;
      if (freshEnts.has(e.id)) { ents.push(freshEnts.get(e.id)); freshEnts.delete(e.id); } else ents.push(e);
    }
    for (const e of freshEnts.values()) ents.push(e);
    map = { items, entities: ents };
  }
  mapRev = m.mapRev; mapEpoch = m.epoch || "";
}
async function refresh() {
  const [m, s] = await Promise.all([get(mapRev < 0 ? "/api/map" : `/api/map?since=${mapRev}&epoch=${encodeURIComponent(mapEpoch)}`), get("/api/state")]);
  applyMap(m); state = s;
  $("map").textContent = s.map + (s.modified ? " *" : "");
  $("state").textContent = s.compiling ? "compiling..." : "";
  if (!fitted) { fitted = true; fit(); }
  renderEntities(); draw();
  if (window.view3d && !$("view3d").hidden) window.view3d.update(map, state);
  if (window.viewLive) window.viewLive.refresh();
}
let refreshQueued = false;
function queueRefresh() {
  if (refreshQueued) return;
  refreshQueued = true;
  setTimeout(() => { refreshQueued = false; refresh().catch((e) => setStatus(e.message)); }, 60);
}

function connect() {
  const es = new EventSource(api("/api/events"));
  es.addEventListener("revision", queueRefresh);
  es.addEventListener("status", (ev) => { try { setStatus(JSON.parse(ev.data).text); } catch (_) {} });
  es.addEventListener("log", (ev) => {
    try {
      const l = JSON.parse(ev.data), pre = $("log");
      pre.textContent += `[${l.source}] ${l.text}\n`;
      if (pre.textContent.length > 20000) pre.textContent = pre.textContent.slice(-15000);
      pre.scrollTop = pre.scrollHeight;
    } catch (_) {}
  });
  es.onopen = () => setStatus("Connected to the editor");
  es.onerror = () => setStatus("Connection lost; retrying...");
}

window.radiant = { post, setStatus, selectedSet, selKey, api };
window.addEventListener("resize", resize);
resize();
refresh().then(connect).catch((e) => setStatus("Cannot reach the editor: " + e.message));

// --- Menu bar (plan 3.3): the editor's own commands, listed by GET /api/commands and run by POST /api/command. ---
let commands = [];
function closeMenu() {
  $("menuPopup").hidden = true;
  for (const b of document.querySelectorAll("#menubar button.open")) b.classList.remove("open");
}
function commandRow(c, withPath) {
  const row = document.createElement("div");
  const off = !c.enabled || c.blocked;
  row.className = "item" + (off ? " off" : "");
  row.title = c.blocked ? c.label + ": " + c.blocked : (c.enabled ? "" : "Not available now");
  const tick = document.createElement("span"); tick.className = "tick"; tick.textContent = c.checked ? "\u2713" : "";
  const label = document.createElement("span"); label.className = "label"; label.textContent = c.label;
  row.append(tick, label);
  if (withPath) { const w = document.createElement("span"); w.className = "where"; w.textContent = c.menu.replace(/\//g, " > "); row.append(w); }
  if (c.shortcut) { const k = document.createElement("span"); k.className = "key"; k.textContent = c.shortcut; row.append(k); }
  if (!off) row.onclick = () => (c.form ? (closeMenu(), openForm(c.form)) : runCommand(c));
  return row;
}
async function runCommand(c) {
  closeMenu();
  try { await post("/api/command", { id: c.id }); setStatus(c.label); queueRefresh(); } catch (e) { /* post() showed the reason */ }
}
function showPopup(rows, x, y) {
  const pop = $("menuPopup");
  pop.textContent = "";
  pop.append(...rows);
  pop.hidden = false;
  pop.style.left = Math.max(4, Math.min(x, window.innerWidth - pop.offsetWidth - 4)) + "px";
  pop.style.top = y + "px";
}
async function openMenu(top, anchor) {
  const wasOpen = anchor.classList.contains("open");
  closeMenu();
  if (wasOpen) return;
  try { commands = (await get("/api/commands")).commands || []; } catch (e) { return; }  // fresh ticks and states
  const rows = [];
  let sub = top;
  for (const c of commands) {
    if (c.menu !== top && !c.menu.startsWith(top + "/")) continue;
    if (c.menu !== sub) {
      sub = c.menu;
      if (sub !== top) { const cap = document.createElement("div"); cap.className = "caption"; cap.textContent = sub.slice(top.length + 1).replace(/\//g, " > "); rows.push(cap); }
      else rows.push(document.createElement("hr"));
    }
    if (c.sep) { rows.push(document.createElement("hr")); continue; }
    rows.push(commandRow(c, false));
  }
  anchor.classList.add("open");
  const r = anchor.getBoundingClientRect();
  showPopup(rows, r.left, r.bottom + 2);
}
function buildMenuBar() {
  const bar = $("menubar"), search = $("cmdSearch");
  for (const b of bar.querySelectorAll("button")) b.remove();
  const tops = [];
  for (const c of commands) { const t = c.menu.split("/")[0]; if (!tops.includes(t)) tops.push(t); }
  for (const t of tops) {
    const b = document.createElement("button");
    b.textContent = t;
    b.onclick = (ev) => { ev.stopPropagation(); openMenu(t, b); };
    bar.insertBefore(b, search);
  }
}
function searchCommands() {
  const text = $("cmdSearch").value.trim().toLowerCase();
  closeMenu();
  if (!text) return [];
  const found = commands.filter((c) => !c.sep && (c.label.toLowerCase().includes(text) || c.menu.toLowerCase().includes(text))).slice(0, 40);
  const rows = found.map((c) => commandRow(c, true));
  if (!rows.length) { const none = document.createElement("div"); none.className = "caption"; none.textContent = "No command matches"; rows.push(none); }
  else rows[0].classList.add("first");
  const r = $("cmdSearch").getBoundingClientRect();
  showPopup(rows, r.left, r.bottom + 2);
  return found;
}
$("cmdSearch").addEventListener("input", searchCommands);
$("cmdSearch").addEventListener("keydown", (ev) => {
  if (ev.key === "Escape") { $("cmdSearch").value = ""; closeMenu(); }
  if (ev.key !== "Enter") return;
  const first = searchCommands().find((c) => c.enabled && !c.blocked);
  if (first) { $("cmdSearch").value = ""; closeMenu(); if (first.form) openForm(first.form); else runCommand(first); }
});
document.addEventListener("click", (ev) => { if (!$("menuPopup").contains(ev.target) && ev.target !== $("cmdSearch")) closeMenu(); });
get("/api/commands").then((j) => { commands = j.commands || []; buildMenuBar(); }).catch(() => {});

// --- Web forms (plan 3.3): a dialog of the editor as a form here. Only the fields that were changed are sent. ---
let openFormId = "";
function fieldInput(f) {
  let el;
  if (f.type === "report") {            // read-only text: a list of findings
    el = document.createElement("pre");
    el.className = "report";
    el.textContent = f.value || "";
    return el;
  }
  if (f.type === "choice") {
    el = document.createElement("select");
    (f.choices || []).forEach((c, k) => {
      const o = document.createElement("option");
      o.value = c; o.textContent = (f.choiceLabels && f.choiceLabels[k]) || c;
      el.append(o);
    });
    el.value = f.value;
  } else {
    el = document.createElement("input");
    el.type = f.type === "number" ? "number" : f.type === "color" ? "color" : f.type === "bool" ? "checkbox" : "text";
    if (f.type === "number") { el.min = f.min; el.max = f.max; el.step = "any"; }
    if (f.type === "bool") el.checked = !!f.value; else el.value = f.value;
    if (f.type === "text" && f.choices) {
      const list = document.createElement("datalist");
      list.id = "choices_" + f.key;
      for (const c of f.choices) { const o = document.createElement("option"); o.value = c; list.append(o); }
      el.setAttribute("list", list.id);
      el.after(list);
      el.dataset.list = "1";
      el._list = list;
    }
  }
  el.dataset.key = f.key; el.dataset.type = f.type;
  el.addEventListener("input", () => { el.dataset.changed = "1"; });
  el.addEventListener("change", () => { el.dataset.changed = "1"; });
  return el;
}
async function openForm(id) {
  let form;
  try { form = await get("/api/form?id=" + encodeURIComponent(id)); } catch (e) { setStatus(e.message); return; }
  openFormId = id;
  $("formApply").hidden = false;
  $("formDialog").dataset.sendAll = form.sendAll ? "1" : "";
  $("formDialog").dataset.reload = form.reload ? "1" : "";
  $("formTitle").textContent = form.title;
  $("formError").textContent = "";
  const box = $("formFields");
  box.textContent = "";
  for (const f of form.fields) {
    const row = document.createElement("label");
    if (f.type === "report") row.className = "wide";
    const name = document.createElement("span"); name.textContent = f.label;
    const input = fieldInput(f);
    row.append(name, input);
    if (input._list) row.append(input._list);
    if (f.help) { const h = document.createElement("small"); h.textContent = f.help; row.append(h); }
    box.append(row);
  }
  $("formShade").hidden = false;
  const first = box.querySelector("input, select");
  if (first) first.focus();
}
function closeForm() { $("formShade").hidden = true; openFormId = ""; }
async function sendForm(thenClose) {
  const values = {};
  for (const el of $("formFields").querySelectorAll("[data-key]")) {
    if (!el.dataset.changed && !$("formDialog").dataset.sendAll) continue;
    values[el.dataset.key] = el.dataset.type === "bool" ? el.checked : el.dataset.type === "number" ? Number(el.value) : el.value;
  }
  if (Object.keys(values).length) {
    try { await post("/api/form", { id: openFormId, values }); }
    catch (e) { $("formError").textContent = e.message || "The editor did not accept the values"; return; }
    for (const el of $("formFields").querySelectorAll("[data-changed]")) delete el.dataset.changed;
    setStatus($("formTitle").textContent + " applied");
    queueRefresh();
    if (!thenClose && $("formDialog").dataset.reload) { await openForm(openFormId); return; }  // the fields follow the edit
  }
  if (thenClose) closeForm();
}
// The editor's info text of an asset (Material / Model / Effect Info), shown in the form window. The text is the
// editor's own HTML; anything that could run or load is taken out before it is shown.
function safeHtml(html) {
  const doc = new DOMParser().parseFromString(html, "text/html");
  for (const el of doc.body.querySelectorAll("script, style, iframe, object, embed, link, img, form, input, button, meta, base")) el.remove();
  for (const el of doc.body.querySelectorAll("*"))
    for (const a of [...el.attributes])
      if (a.name.startsWith("on") || a.name === "href" || a.name === "src" || (a.name === "style" && /url|expression/i.test(a.value))) el.removeAttribute(a.name);
  const box = document.createElement("div");
  box.className = "info";
  box.append(...doc.body.childNodes);
  return box;
}
async function openInfo(kind, name) {
  let info;
  try { info = await get(`/api/assetinfo?kind=${encodeURIComponent(kind)}&name=${encodeURIComponent(name)}`); }
  catch (e) { setStatus("No info: " + (e.message || e)); return; }
  openFormId = "info";
  $("formApply").hidden = true;
  $("formDialog").dataset.sendAll = "";
  $("formDialog").dataset.reload = "";
  $("formTitle").textContent = info.title;
  $("formError").textContent = "";
  const box = $("formFields");
  box.textContent = "";
  box.append(safeHtml(info.html));
  $("formShade").hidden = false;
}
window.radiant.openInfo = openInfo;
$("formDialog").addEventListener("submit", (ev) => { ev.preventDefault(); sendForm(true); });
$("formApply").onclick = () => sendForm(false);
$("formCancel").onclick = closeForm;
$("formShade").addEventListener("mousedown", (ev) => { if (ev.target === $("formShade")) closeForm(); });
document.addEventListener("keydown", (ev) => { if (ev.key === "Escape" && openFormId) closeForm(); });
window.radiant.openForm = openForm;

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
