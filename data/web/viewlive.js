// IW8 Radiant Web: the editor's own render as the 3D view (plan 3.1). The page keeps a camera of its own and asks the
// editor for a picture of it (GET /api/frame) whenever the camera or the map changes; one request at a time. A click
// is picked in the editor (POST /api/pick). Nothing is asked while the tab is hidden or another view is shown.
"use strict";
(() => {
  const img = document.getElementById("viewLive");
  const R = () => window.radiant;
  let cam = null, on = false, busy = false, again = false, moving = false, settle = 0;
  const keys = new Set();
  let lastTick = 0, ticking = false;

  const rad = (d) => d * Math.PI / 180;
  const forward = () => [Math.cos(rad(cam.pitch)) * Math.cos(rad(cam.yaw)), Math.cos(rad(cam.pitch)) * Math.sin(rad(cam.yaw)), Math.sin(rad(cam.pitch))];
  const right = () => [Math.sin(rad(cam.yaw)), -Math.cos(rad(cam.yaw)), 0];
  const move = (v, d) => { for (let k = 0; k < 3; ++k) cam.pos[k] += v[k] * d; };

  async function frame() {
    if (!on || !cam || document.hidden) return;
    if (busy) { again = true; return; }
    busy = true; again = false;
    const r = img.parentElement.getBoundingClientRect();
    const scale = (moving ? 0.6 : 1) * Math.min(window.devicePixelRatio || 1, 1.5);   // lighter pictures while moving
    const w = Math.max(64, Math.min(2560, Math.round(r.width * scale))), h = Math.max(64, Math.min(1440, Math.round(r.height * scale)));
    const url = `/api/frame?pos=${cam.pos.map((v) => v.toFixed(2)).join(",")}&yaw=${cam.yaw.toFixed(3)}&pitch=${cam.pitch.toFixed(3)}` +
                `&fov=${cam.fov}&w=${w}&h=${h}&q=${moving ? 55 : 82}`;
    try {
      const resp = await fetch(R().api(url), { credentials: "same-origin", cache: "no-store" });
      if (resp.ok) {
        const old = img.src;
        img.src = URL.createObjectURL(await resp.blob());
        if (old.startsWith("blob:")) setTimeout(() => URL.revokeObjectURL(old), 1000);
      } else {
        R().setStatus("The editor cannot draw the view now");
      }
    } catch (e) { R().setStatus("Lost the editor's view: " + e.message); }
    busy = false;
    if (again) frame();
  }
  // A moving camera asks for light pictures; a moment after it stops, one full picture.
  function moved() {
    moving = true;
    clearTimeout(settle);
    settle = setTimeout(() => { moving = false; frame(); }, 220);
    frame();
  }

  function tick(now) {
    if (!on || !keys.size) { ticking = false; return; }
    const dt = Math.min(0.1, (now - lastTick) / 1000);
    lastTick = now;
    const speed = (keys.has("shift") ? 1800 : 600) * dt;
    const f = forward(), r = right();
    if (keys.has("w")) move(f, speed);
    if (keys.has("s")) move(f, -speed);
    if (keys.has("d")) move(r, speed);
    if (keys.has("a")) move(r, -speed);
    if (keys.has("e")) cam.pos[2] += speed;
    if (keys.has("q")) cam.pos[2] -= speed;
    moved();
    requestAnimationFrame(tick);
  }
  img.addEventListener("keydown", (ev) => {
    const k = ev.key.toLowerCase();
    if (!"wasdqe".includes(k) && k !== "shift") return;
    ev.preventDefault();
    keys.add(k);
    if (!ticking && cam) { ticking = true; lastTick = performance.now(); requestAnimationFrame(tick); }
  });
  img.addEventListener("keyup", (ev) => keys.delete(ev.key.toLowerCase()));
  img.addEventListener("blur", () => keys.clear());

  let drag = null;
  img.addEventListener("pointerdown", (ev) => {
    img.focus();
    img.setPointerCapture(ev.pointerId);
    drag = { x: ev.clientX, y: ev.clientY, button: ev.button, travelled: 0 };
  });
  img.addEventListener("pointermove", (ev) => {
    if (!drag || !cam) return;
    const dx = ev.clientX - drag.x, dy = ev.clientY - drag.y;
    drag.x = ev.clientX; drag.y = ev.clientY;
    drag.travelled += Math.abs(dx) + Math.abs(dy);
    if (drag.button === 2 || ev.shiftKey) {                 // slide sideways and up / down
      move(right(), -dx * 2); cam.pos[2] += dy * 2;
    } else {                                                // look around
      cam.yaw -= dx * 0.25;
      cam.pitch = Math.max(-89, Math.min(89, cam.pitch - dy * 0.25));
    }
    if (dx || dy) moved();
  });
  img.addEventListener("pointerup", async (ev) => {
    const d = drag;
    drag = null;
    if (!d || !cam || d.travelled > 4 || d.button !== 0) return;
    const r = img.getBoundingClientRect();
    try {
      await R().post("/api/pick", { pos: cam.pos, yaw: cam.yaw, pitch: cam.pitch, fov: cam.fov, x: (ev.clientX - r.left) / r.width,
                                    y: (ev.clientY - r.top) / r.height, aspect: r.width / r.height, add: ev.shiftKey });
    } catch (e) { /* post() showed the reason */ }
    frame();
  });
  img.addEventListener("contextmenu", (ev) => ev.preventDefault());
  img.addEventListener("wheel", (ev) => {
    if (!cam) return;
    ev.preventDefault();
    move(forward(), (ev.deltaY < 0 ? 1 : -1) * (ev.shiftKey ? 512 : 128));
    moved();
  }, { passive: false });
  document.addEventListener("visibilitychange", () => { if (!document.hidden) frame(); });
  window.addEventListener("resize", () => frame());

  window.viewLive = {
    // Shown or hidden by the View box; the first time, the camera starts where the editor's camera is.
    shown(yes, state) {
      on = yes;
      if (!yes) return;
      if (!cam) {
        const c = state && state.camera;
        cam = c ? { pos: c.pos.slice(), yaw: c.yaw, pitch: c.pitch, fov: c.fov } : { pos: [0, 0, 256], yaw: 45, pitch: -20, fov: 90 };
      }
      img.focus();
      frame();
    },
    refresh() { frame(); },
    camera() { return cam; },
  };
})();
