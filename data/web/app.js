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
      p.forEach((q, k) => { const [x, y] = toScreen(q[a] + dragOffset(it, a), q[b] + dragOffset(it, b)); k ? g.lineTo(x, y) : g.moveTo(x, y); });
      g.closePath();
    }
    g.stroke();
  };
  for (const it of map.items) if (!sel.has(selKey(it.e, it.i)) && !sel.has(selKey(it.e, -1))) drawItem(it, false);
  for (const it of map.items) if (sel.has(selKey(it.e, it.i)) || sel.has(selKey(it.e, -1))) drawItem(it, true);
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

canvas.addEventListener("pointerdown", (ev) => {
  canvas.setPointerCapture(ev.pointerId);
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
function renderInspector() {
  const e = inspected(), t = $("keys"); t.textContent = "";
  $("inspTitle").textContent = e ? e.classname : state.selection.length ? state.selection.length + " selected" : "nothing selected";
  if (!e) return;
  for (const [k, v] of Object.entries(e.keys)) {
    const tr = document.createElement("tr"), tk = document.createElement("td"), tv = document.createElement("td");
    tk.textContent = k;
    const inp = document.createElement("input"); inp.value = v;
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
  const is3d = v === 3;
  $("view").hidden = is3d; $("view3d").hidden = !is3d;
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
    if (kind === "materials" || kind === "models") {
      const img = document.createElement("img");
      img.loading = "lazy";
      img.alt = "";
      img.src = api(`/api/thumb?kind=${encodeURIComponent(kind)}&name=${encodeURIComponent(it.name)}`);
      img.onerror = () => { img.style.visibility = "hidden"; };
      li.appendChild(img);
    }
    const span = document.createElement("span");
    span.textContent = it.name + (it.loop ? "  (loop)" : "");
    li.appendChild(span);
    li.title = it.name;
    li.onclick = async () => {
      if (kind === "materials") { await post("/api/material", { name: it.name }); setStatus("Material " + it.name); return; }
      // Place at the 2D view centre (the third axis at 0).
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

async function refresh() {
  const [m, s] = await Promise.all([get("/api/map"), get("/api/state")]);
  map = m; state = s;
  $("map").textContent = s.map + (s.modified ? " *" : "");
  $("state").textContent = s.compiling ? "compiling..." : "";
  if (!fitted) { fitted = true; fit(); }
  renderEntities(); draw();
  if (window.view3d && !$("view3d").hidden) window.view3d.update(map, state);
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
