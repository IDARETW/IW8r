// IW8 Radiant Web: 3D view (WebGL 1). Flat-shaded faces in each item's colour, selection in red, orbit camera.
// Drag: orbit · Right-drag or Shift+drag: pan · Wheel: zoom · Click: select (Shift adds).
"use strict";
(() => {
  const canvas = document.getElementById("view3d");
  const gl = canvas.getContext("webgl", { antialias: true });
  if (!gl) { window.view3d = { show() { window.radiant.setStatus("WebGL is not available in this browser"); }, update() {} }; return; }

  const vs = `attribute vec3 p; attribute vec3 c; uniform mat4 m; varying vec3 vc;
    void main() { vc = c; gl_Position = m * vec4(p, 1.0); }`;
  const fs = `precision mediump float; varying vec3 vc; void main() { gl_FragColor = vec4(vc, 1.0); }`;
  const sh = (type, src) => { const s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s); return s; };
  const prog = gl.createProgram();
  gl.attachShader(prog, sh(gl.VERTEX_SHADER, vs)); gl.attachShader(prog, sh(gl.FRAGMENT_SHADER, fs));
  gl.linkProgram(prog);
  const aP = gl.getAttribLocation(prog, "p"), aC = gl.getAttribLocation(prog, "c"), uM = gl.getUniformLocation(prog, "m");
  const buf = gl.createBuffer(), lineBuf = gl.createBuffer();
  // 10.5: textured faces. Each material's thumbnail (/api/thumb) becomes a 128 x 128 repeating texture; faces of a
  // material without one yet stay in the flat buffer above.
  const tvs = `attribute vec3 p; attribute vec2 uv; attribute float k; uniform mat4 m; varying vec2 vuv; varying float vk;
    void main() { vuv = uv; vk = k; gl_Position = m * vec4(p, 1.0); }`;
  const tfs = `precision mediump float; uniform sampler2D tex; varying vec2 vuv; varying float vk;
    void main() { gl_FragColor = vec4(texture2D(tex, vuv).rgb * vk, 1.0); }`;
  const tprog = gl.createProgram();
  gl.attachShader(tprog, sh(gl.VERTEX_SHADER, tvs)); gl.attachShader(tprog, sh(gl.FRAGMENT_SHADER, tfs));
  gl.linkProgram(tprog);
  const tP = gl.getAttribLocation(tprog, "p"), tUV = gl.getAttribLocation(tprog, "uv"), tK = gl.getAttribLocation(tprog, "k");
  const tM = gl.getUniformLocation(tprog, "m");
  const textures = new Map();   // material -> { tex, ready, failed }
  let texBatches = [];          // { mat, buf, count }
  let textured = true;
  function textureFor(mat) {
    let t = textures.get(mat);
    if (t) return t;
    t = { tex: null, ready: false, failed: false };
    textures.set(mat, t);
    const img = new Image();
    img.onload = () => {
      const c = document.createElement("canvas");
      c.width = c.height = 128;
      c.getContext("2d").drawImage(img, 0, 0, 128, 128);
      t.tex = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, t.tex);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, c);
      gl.generateMipmap(gl.TEXTURE_2D);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
      t.ready = true;
      if (haveMap) { build(haveMap, haveState); draw(); }
    };
    img.onerror = () => { t.failed = true; };
    img.src = window.radiant.api(`/api/thumb?kind=materials&name=${encodeURIComponent(mat)}`);
    return t;
  }
  let triCount = 0, lineCount = 0, tris = [];   // tris: {a, b, c, e, i} for picking
  let cam = { target: [0, 0, 0], dist: 2000, yaw: -0.8, pitch: 0.5 };
  let haveMap = null, haveState = null, framed = false;

  const light = [0.35, 0.55, 0.75];
  const norm = (v) => { const l = Math.hypot(v[0], v[1], v[2]) || 1; return [v[0] / l, v[1] / l, v[2] / l]; };
  const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
  const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];

  function build(map, state) {
    const sel = window.radiant.selectedSet(), key = window.radiant.selKey;
    const v = [], l = [];
    const perMat = new Map();  // material -> [x y z u v k ...]
    tris = [];
    let lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
    for (const it of map.items) {
      const selected = sel.has(key(it.e, it.i)) || sel.has(key(it.e, -1));
      const base = selected ? [1, 0.32, 0.32] : [(it.color & 255) / 255, ((it.color >> 8) & 255) / 255, ((it.color >> 16) & 255) / 255];
      for (const poly of it.polys) {
        if (poly.length < 3) continue;
        const n = norm(cross(sub(poly[1], poly[0]), sub(poly[2], poly[0])));
        const k = 0.55 + 0.45 * Math.abs(dot(n, light));
        const col = [base[0] * k, base[1] * k, base[2] * k];
        const pi = it.polys.indexOf(poly);
        const mat = textured && !selected && it.mats && it.mats.length === it.polys.length ? it.mats[pi] : null;
        const uv = mat ? it.uvs[pi] : null;
        const tex = mat && uv && uv.length === poly.length ? textureFor(mat) : null;
        for (let j = 1; j + 1 < poly.length; ++j) {
          if (tex && tex.ready) {
            if (!perMat.has(mat)) perMat.set(mat, []);
            const arr = perMat.get(mat);
            for (const idx of [0, j, j + 1]) arr.push(poly[idx][0], poly[idx][1], poly[idx][2], uv[idx][0], uv[idx][1], k);
          } else {
            for (const q of [poly[0], poly[j], poly[j + 1]]) v.push(q[0], q[1], q[2], col[0], col[1], col[2]);
          }
          tris.push({ a: poly[0], b: poly[j], c: poly[j + 1], e: it.e, i: it.i });
        }
        const lc = selected ? [1, 0.8, 0.8] : [0.08, 0.08, 0.1];
        for (let j = 0; j < poly.length; ++j) {
          const a = poly[j], b = poly[(j + 1) % poly.length];
          l.push(a[0], a[1], a[2], lc[0], lc[1], lc[2], b[0], b[1], b[2], lc[0], lc[1], lc[2]);
        }
        for (const q of poly) for (let x = 0; x < 3; ++x) { lo[x] = Math.min(lo[x], q[x]); hi[x] = Math.max(hi[x], q[x]); }
      }
    }
    gl.bindBuffer(gl.ARRAY_BUFFER, buf); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(v), gl.STATIC_DRAW);
    gl.bindBuffer(gl.ARRAY_BUFFER, lineBuf); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(l), gl.STATIC_DRAW);
    triCount = v.length / 6; lineCount = l.length / 6;
    for (const b of texBatches) gl.deleteBuffer(b.buf);
    texBatches = [];
    for (const [mat, arr] of perMat) {
      const b = gl.createBuffer();
      gl.bindBuffer(gl.ARRAY_BUFFER, b); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(arr), gl.STATIC_DRAW);
      texBatches.push({ mat, buf: b, count: arr.length / 6 });
    }
    if (!framed && isFinite(lo[0])) {
      framed = true;
      cam.target = [(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2];
      cam.dist = Math.max(256, Math.hypot(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]) * 0.9);
    }
  }

  function eye() {
    const cp = Math.cos(cam.pitch);
    return [cam.target[0] - Math.cos(cam.yaw) * cp * cam.dist, cam.target[1] - Math.sin(cam.yaw) * cp * cam.dist,
            cam.target[2] + Math.sin(cam.pitch) * cam.dist];
  }
  function matrix() {
    const w = canvas.width, h = canvas.height, e = eye();
    const f = norm(sub(cam.target, e)), r = norm(cross(f, [0, 0, 1])), u = cross(r, f);
    const fov = 70 * Math.PI / 180, near = 4, far = 200000, t = 1 / Math.tan(fov / 2), asp = w / Math.max(1, h);
    // view (column-major for WebGL)
    const view = [r[0], u[0], -f[0], 0, r[1], u[1], -f[1], 0, r[2], u[2], -f[2], 0, -dot(r, e), -dot(u, e), dot(f, e), 1];
    const proj = [t / asp, 0, 0, 0, 0, t, 0, 0, 0, 0, (far + near) / (near - far), -1, 0, 0, 2 * far * near / (near - far), 0];
    const m = new Array(16).fill(0);
    for (let c = 0; c < 4; ++c) for (let rr = 0; rr < 4; ++rr) for (let k = 0; k < 4; ++k) m[c * 4 + rr] += proj[k * 4 + rr] * view[c * 4 + k];
    return { m, e, f, r, u, t, asp };
  }

  function draw() {
    const dpr = window.devicePixelRatio || 1, rect = canvas.getBoundingClientRect();
    canvas.width = Math.max(1, rect.width * dpr); canvas.height = Math.max(1, rect.height * dpr);
    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.clearColor(0.1, 0.11, 0.13, 1); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.enable(gl.DEPTH_TEST); gl.enable(gl.POLYGON_OFFSET_FILL); gl.polygonOffset(1, 1);
    gl.useProgram(prog);
    gl.uniformMatrix4fv(uM, false, new Float32Array(matrix().m));
    const M = matrix().m;
    for (const [b, mode, count] of [[buf, gl.TRIANGLES, triCount], [lineBuf, gl.LINES, lineCount]]) {
      if (!count) continue;
      gl.bindBuffer(gl.ARRAY_BUFFER, b);
      gl.enableVertexAttribArray(aP); gl.vertexAttribPointer(aP, 3, gl.FLOAT, false, 24, 0);
      gl.enableVertexAttribArray(aC); gl.vertexAttribPointer(aC, 3, gl.FLOAT, false, 24, 12);
      gl.drawArrays(mode, 0, count);
    }
    if (texBatches.length) {
      gl.disableVertexAttribArray(aC);
      gl.useProgram(tprog);
      gl.uniformMatrix4fv(tM, false, new Float32Array(M));
      for (const b of texBatches) {
        const t = textures.get(b.mat);
        if (!t || !t.ready) continue;
        gl.bindTexture(gl.TEXTURE_2D, t.tex);
        gl.bindBuffer(gl.ARRAY_BUFFER, b.buf);
        gl.enableVertexAttribArray(tP); gl.vertexAttribPointer(tP, 3, gl.FLOAT, false, 24, 0);
        gl.enableVertexAttribArray(tUV); gl.vertexAttribPointer(tUV, 2, gl.FLOAT, false, 24, 12);
        gl.enableVertexAttribArray(tK); gl.vertexAttribPointer(tK, 1, gl.FLOAT, false, 24, 20);
        gl.drawArrays(gl.TRIANGLES, 0, b.count);
      }
      gl.disableVertexAttribArray(tUV); gl.disableVertexAttribArray(tK);
      gl.useProgram(prog);
    }
  }

  // Ray through a pixel; nearest triangle (Moller-Trumbore).
  function pick(x, y) {
    const rect = canvas.getBoundingClientRect(), M = matrix();
    const nx = 2 * x / rect.width - 1, ny = 1 - 2 * y / rect.height;
    const d = norm([M.f[0] + (M.r[0] * nx * M.asp + M.u[0] * ny) / M.t, M.f[1] + (M.r[1] * nx * M.asp + M.u[1] * ny) / M.t,
                    M.f[2] + (M.r[2] * nx * M.asp + M.u[2] * ny) / M.t]);
    let best = null, bestT = Infinity;
    for (const tr of tris) {
      const e1 = sub(tr.b, tr.a), e2 = sub(tr.c, tr.a), p = cross(d, e2), det = dot(e1, p);
      if (Math.abs(det) < 1e-9) continue;
      const s = sub(M.e, tr.a), u = dot(s, p) / det; if (u < 0 || u > 1) continue;
      const q = cross(s, e1), v = dot(d, q) / det; if (v < 0 || u + v > 1) continue;
      const t = dot(e2, q) / det;
      if (t > 1 && t < bestT) { bestT = t; best = tr; }
    }
    return best;
  }

  let drag = null;
  canvas.addEventListener("contextmenu", (e) => e.preventDefault());
  canvas.addEventListener("pointerdown", (e) => {
    canvas.setPointerCapture(e.pointerId);
    drag = { x: e.offsetX, y: e.offsetY, pan: e.button === 2 || e.shiftKey && e.button !== 0 || e.button === 1, moved: false, shift: e.shiftKey };
  });
  canvas.addEventListener("pointermove", (e) => {
    if (!drag) return;
    const dx = e.offsetX - drag.x, dy = e.offsetY - drag.y;
    if (Math.abs(dx) + Math.abs(dy) > 3) drag.moved = true;
    if (!drag.moved) return;
    if (drag.pan) {
      const M = matrix(), k = cam.dist / 800;
      for (let i = 0; i < 3; ++i) cam.target[i] += (-M.r[i] * dx + M.u[i] * dy) * k;
    } else {
      cam.yaw -= dx * 0.008; cam.pitch = Math.max(-1.5, Math.min(1.5, cam.pitch + dy * 0.008));
    }
    drag.x = e.offsetX; drag.y = e.offsetY; draw();
  });
  canvas.addEventListener("pointerup", async (e) => {
    const d = drag; drag = null;
    if (!d || d.moved || e.button !== 0) return;
    const hit = pick(e.offsetX, e.offsetY);
    if (hit) await window.radiant.post("/api/select", { e: hit.e, i: hit.i, add: d.shift });
    else await window.radiant.post("/api/deselect");
  });
  canvas.addEventListener("wheel", (e) => { e.preventDefault(); cam.dist *= e.deltaY < 0 ? 1 / 1.15 : 1.15; draw(); }, { passive: false });
  window.addEventListener("resize", () => { if (!canvas.hidden) draw(); });

  window.view3d = {
    show(map, state) { haveMap = map; haveState = state; build(map, state); draw(); },
    update(map, state) { haveMap = map; haveState = state; build(map, state); draw(); },
  };
})();
