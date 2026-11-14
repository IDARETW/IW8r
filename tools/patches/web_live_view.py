import io

# Plan 3.1: the browser's 3D view is the editor's own render. GET /api/frame draws the scene from a camera the
# browser sends (each browser has its own) with the editor's renderer and returns a JPEG; POST /api/pick selects what
# lies under a point of that view. The page gets a "Editor view (live)" option with fly controls.

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


# --- server ---------------------------------------------------------------------------------------------------------
H = 'src/qtapp/web_server.h'
patch(H, '''//   GET  /api/commands               every editor command in menu order''', '''//   GET  /api/frame?pos=x,y,z&yaw=&pitch=&fov=&w=&h=&q=   the editor's own 3D render from that camera, as a JPEG
//                                    (w 64..2560, h 64..1440, q 30..95); the editor's modes apply (textured, lit,
//                                    Game Lighting, the stock reference)
//   POST /api/pick     {pos, yaw, pitch, fov, x, y, aspect, add}   select what lies under the point (x, y in 0..1
//                                    from the top left) of that view; nothing there deselects
//   GET  /api/commands               every editor command in menu order''')
patch(H, '''class WebServer : public QObject {
    Q_OBJECT
public:
    struct Hooks {''', '''// A camera a browser sends with its view requests (each browser has its own).
struct WebCamera {
    double pos[3] = {0, 0, 0};
    double yaw = 0, pitch = 0;  // degrees: yaw around +Z from +X, pitch up
    double fov = 90;            // horizontal degrees
};

class WebServer : public QObject {
    Q_OBJECT
public:
    struct Hooks {''')
patch(H, '''        // Every command of the editor's menus, in menu order (MainWindow::webCommandList).
''', '''        // The editor's own 3D render from a browser's camera (plan 3.1): JPEG bytes, empty when it cannot draw.
        std::function<QByteArray(const WebCamera& cam, int w, int h, int quality)> renderFrame;
        // Selects what lies under a point of that view (x, y in 0..1 from the top left); false when nothing is there.
        std::function<bool(const WebCamera& cam, double x, double y, double aspect, bool add)> pickFrame;
        // The editor's own camera, as {pos: [x, y, z], yaw, pitch, fov}: where a browser's view starts.
        std::function<QJsonObject()> camera;
        // Every command of the editor's menus, in menu order (MainWindow::webCommandList).
''')
S = 'src/qtapp/web_server.cpp'
patch(S, '''                  {"logNext", double(logTail().next)}};
    return json(o);''', '''                  {"logNext", double(logTail().next)}};
    if (hooks_.camera) o.insert("camera", hooks_.camera());
    return json(o);''')
patch(S, '''        if (path == "/api/commands") {''', '''        if (path == "/api/frame") {
            WebCamera cam;
            const QStringList pos = q.queryItemValue("pos").split(',');
            bool ok = pos.size() == 3;
            for (int k = 0; k < 3 && ok; ++k) cam.pos[k] = pos[k].toDouble(&ok);
            cam.yaw = q.queryItemValue("yaw").toDouble();
            cam.pitch = std::clamp(q.queryItemValue("pitch").toDouble(), -89.0, 89.0);
            if (q.hasQueryItem("fov")) cam.fov = std::clamp(q.queryItemValue("fov").toDouble(), 10.0, 150.0);
            const int w = q.queryItemValue("w").toInt(), h = q.queryItemValue("h").toInt();
            if (!ok || w < 64 || h < 64 || w > 2560 || h > 1440 || !std::isfinite(cam.pos[0] + cam.pos[1] + cam.pos[2] + cam.yaw))
                return error(400, "need pos=x,y,z, yaw, pitch, w (64..2560) and h (64..1440)");
            const int quality = std::clamp(q.hasQueryItem("q") ? q.queryItemValue("q").toInt() : 75, 30, 95);
            const QByteArray jpeg = hooks_.renderFrame ? hooks_.renderFrame(cam, w, h, quality) : QByteArray();
            if (jpeg.isEmpty()) return error(503, "the editor cannot draw the view now");
            Response r;
            r.type = "image/jpeg";
            r.body = jpeg;
            r.extraHeaders = "Cache-Control: no-store\\r\\n";
            return r;
        }
        if (path == "/api/commands") {''')
patch(S, '''    } else if (path == "/api/command") {''', '''    } else if (path == "/api/pick") {
        WebCamera cam;
        const QJsonArray pos = in.value("pos").toArray();
        if (pos.size() != 3 || !hooks_.pickFrame) return error(400, "need pos:[x,y,z], yaw, pitch, x and y");
        for (int k = 0; k < 3; ++k) cam.pos[k] = pos[k].toDouble();
        cam.yaw = in.value("yaw").toDouble();
        cam.pitch = std::clamp(in.value("pitch").toDouble(), -89.0, 89.0);
        cam.fov = std::clamp(in.value("fov").toDouble(90), 10.0, 150.0);
        const double x = in.value("x").toDouble(-1), y = in.value("y").toDouble(-1);
        if (x < 0 || x > 1 || y < 0 || y > 1) return error(400, "x and y are 0..1 from the top left of the view");
        hooks_.pickFrame(cam, x, y, std::clamp(in.value("aspect").toDouble(16.0 / 9.0), 0.2, 5.0), in.value("add").toBool());
    } else if (path == "/api/command") {''')
s = read(S)
if '#include <cmath>' not in s:
    i = s.index('#include <Q')
    s = s[:i] + '#include <cmath>\n' + s[i:]
    write(S, s)

# --- main window ----------------------------------------------------------------------------------------------------
MH = 'src/qtapp/main_window.h'
patch(MH, '''    QJsonArray webCommandList() const;
''', '''    QJsonArray webCommandList() const;
    // The browser's live view (plan 3.1): the camera view's own render from a camera of the browser's, as a JPEG; a
    // pick through that camera; the editor's camera as JSON. The editor's own camera is put back after each.
    QByteArray webRenderFrame(const struct WebCamera& cam, int w, int h, int quality);
    bool webPick(const struct WebCamera& cam, double x, double y, double aspect, bool add);
    QJsonObject webCamera() const;
''')
s = read(MH)
if '#include <QJsonObject>' not in s:
    i = s.index('#include <Q')
    s = s[:i] + '#include <QJsonObject>\n' + s[i:]
    write(MH, s)
M = 'src/qtapp/main_window.cpp'
patch(M, '''        hooks.commands = [this] { return webCommandList(); };
''', '''        hooks.renderFrame = [this](const WebCamera& cam, int w, int h, int quality) { return webRenderFrame(cam, w, h, quality); };
        hooks.pickFrame = [this](const WebCamera& cam, double x, double y, double aspect, bool add) { return webPick(cam, x, y, aspect, add); };
        hooks.camera = [this] { return webCamera(); };
        hooks.commands = [this] { return webCommandList(); };
''')
patch(M, '''QString MainWindow::webCommandBlocked(const QString& id, const QString& label) {''', '''QJsonObject MainWindow::webCamera() const {
    const app::Camera& c = ctx_.cam;
    return QJsonObject{{"pos", QJsonArray{c.pos.x, c.pos.y, c.pos.z}}, {"yaw", c.yaw}, {"pitch", c.pitch}, {"fov", c.fov}};
}

QByteArray MainWindow::webRenderFrame(const WebCamera& cam, int w, int h, int quality) {
    if (!camera_) return {};
    // The editor's renderer draws the browser's camera into an off-screen target (no second renderer, no second set
    // of textures); the editor's own camera comes back before anything else runs.
    const app::Camera saved = ctx_.cam;
    ctx_.cam.pos = Vec3{cam.pos[0], cam.pos[1], cam.pos[2]};
    ctx_.cam.yaw = cam.yaw, ctx_.cam.pitch = cam.pitch, ctx_.cam.fov = cam.fov;
    const QImage img = camera_->renderImage(w, h, false);
    ctx_.cam = saved;
    if (img.isNull()) return {};
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "JPG", quality);
    return out;
}

bool MainWindow::webPick(const WebCamera& cam, double x, double y, double aspect, bool add) {
    app::Camera c = ctx_.cam;
    c.pos = Vec3{cam.pos[0], cam.pos[1], cam.pos[2]};
    c.yaw = cam.yaw, c.pitch = cam.pitch, c.fov = cam.fov;
    ctx_.scene.sync(ctx_.doc);
    double t = 0;
    const auto hit = ctx_.scene.pick(app::cameraRay(c, 2.0 * x - 1.0, 1.0 - 2.0 * y, aspect), &t);
    if (hit) ctx_.doc.select(*hit, add);
    else if (!add) ctx_.doc.deselectAll();
    ctx_.documentEdited(hit ? "Selected from the browser's view" : QString());
    return hit.has_value();
}

QString MainWindow::webCommandBlocked(const QString& id, const QString& label) {''')
s = read(M)
if '#include <QBuffer>' not in s:
    i = s.index('#include <Q')
    s = s[:i] + '#include <QBuffer>\n' + s[i:]
    write(M, s)

# --- the page -------------------------------------------------------------------------------------------------------
patch('data/web/index.html', '''<option value="3">3D</option></select></label>''',
      '''<option value="3">3D</option><option value="4">Editor view (live)</option></select></label>''')
patch('data/web/index.html', '''    <canvas id="view3d" hidden></canvas>
''', '''    <canvas id="view3d" hidden></canvas>
    <img id="viewLive" hidden alt="" tabindex="0" draggable="false">
''')
patch('data/web/index.html', '''<script src="view3d.js"></script>
''', '''<script src="view3d.js"></script>
<script src="viewlive.js"></script>
''')
append('data/web/style.css', '''
/* The editor's own render as the 3D view (plan 3.1). */
#viewLive { position: absolute; inset: 0; width: 100%; height: 100%; object-fit: fill; background: #1a1c20; outline: none;
  cursor: crosshair; user-select: none; -webkit-user-drag: none; touch-action: none; }
''')
patch('data/web/app.js', '''  const is3d = v === 3;
  $("view").hidden = is3d; $("view3d").hidden = !is3d;
''', '''  const is3d = v === 3, live = v === 4;
  $("view").hidden = is3d || live; $("view3d").hidden = !is3d; $("viewLive").hidden = !live;
  if (window.viewLive) window.viewLive.shown(live, state);
  if (live) {
    $("hint").textContent = "Drag: look · W A S D, Q E: fly (Shift faster) · Wheel: forward / back · Right-drag: slide · Click: select (Shift adds)";
    return;
  }
''')
patch('data/web/app.js', '''  if (window.view3d && !$("view3d").hidden) window.view3d.update(map, state);
}''', '''  if (window.view3d && !$("view3d").hidden) window.view3d.update(map, state);
  if (window.viewLive) window.viewLive.refresh();
}''')
write('data/web/viewlive.js', '''// IW8 Radiant Web: the editor's own render as the 3D view (plan 3.1). The page keeps a camera of its own and asks the
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
''')

# --- GUI test -------------------------------------------------------------------------------------------------------
T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''            auto pageNow = web.handle("GET", "/", {}, true, {});
            auto jsNow = web.handle("GET", "/app.js", {}, true, {});
            r.check(pageNow.body.contains("id=\\"menubar\\"")''', '''            {
                // Plan 3.1: the browser's 3D view is the editor's own render from the browser's camera.
                ch.renderFrame = [&w](const WebCamera& cam, int fw, int fh, int quality) { return w.webRenderFrame(cam, fw, fh, quality); };
                ch.pickFrame = [&w](const WebCamera& cam, double x, double y, double aspect, bool add) { return w.webPick(cam, x, y, aspect, add); };
                ch.camera = [&w] { return w.webCamera(); };
                WebServer vweb(&ctx, ch);
                const app::Camera camWas = ctx.cam;
                const QJsonObject sc = QJsonDocument::fromJson(vweb.handle("GET", "/api/state", {}, true, {}).body).object().value("camera").toObject();
                r.check(sc.value("pos").toArray().size() == 3 && std::fabs(sc.value("yaw").toDouble() - camWas.yaw) < 1e-9 && sc.value("fov").toDouble() > 0,
                        "GET /api/state carries the editor's camera for the browser's view to start from");
                const auto fr = vweb.handle("GET", "/api/frame?pos=0,-200,120&yaw=90&pitch=-25&fov=90&w=320&h=180&q=80", {}, true, {});
                const QImage shot = QImage::fromData(fr.body, "JPG");
                double sum = 0, sum2 = 0;
                for (int y = 0; y < shot.height(); ++y)
                    for (int x = 0; x < shot.width(); ++x) {
                        const double l = qGray(shot.pixel(x, y)) / 255.0;
                        sum += l, sum2 += l * l;
                    }
                const double n = std::max(1, shot.width() * shot.height()), mean = sum / n, var = sum2 / n - mean * mean;
                r.check(fr.status == 200 && fr.type == "image/jpeg" && fr.body.startsWith("\\xFF\\xD8") && shot.width() == 320 && shot.height() == 180 && var > 1e-4,
                        QString("GET /api/frame returns the editor's render of the browser's camera as a JPEG (%1 bytes, variance %2)")
                            .arg(fr.body.size()).arg(var, 0, 'f', 4));
                r.check(ctx.cam.pos.x == camWas.pos.x && ctx.cam.pos.y == camWas.pos.y && ctx.cam.yaw == camWas.yaw && ctx.cam.pitch == camWas.pitch,
                        "the editor's own camera is where it was after a browser frame");
                const auto bad1 = vweb.handle("GET", "/api/frame?pos=0,0&yaw=0&pitch=0&w=320&h=180", {}, true, {});
                const auto bad2 = vweb.handle("GET", "/api/frame?pos=0,0,0&yaw=0&pitch=0&w=9000&h=180", {}, true, {});
                r.check(bad1.status == 400 && bad2.status == 400, "a frame request without a full camera or with a size out of range is refused");
                // A pick through the browser's camera: down at the first brush selects it; up at the sky deselects.
                ctx.doc.deselectAll();
                Vec3 centre{};
                bool haveBrush = false;
                ctx.scene.sync(ctx.doc);
                for (const auto* g : ctx.scene.items())
                    if (g->kind == iw8r::app::PrimGeom::Brush && g->bounds.valid()) {
                        centre = (g->bounds.mins + g->bounds.maxs) * 0.5;
                        centre.z = g->bounds.maxs.z;
                        haveBrush = true;
                        break;
                    }
                const QByteArray down = QString("{\\"pos\\":[%1,%2,%3],\\"yaw\\":0,\\"pitch\\":-89,\\"fov\\":90,\\"x\\":0.5,\\"y\\":0.5,\\"aspect\\":1.7778}")
                                            .arg(centre.x).arg(centre.y).arg(centre.z + 300).toUtf8();
                const auto pk = vweb.handle("POST", "/api/pick", down, true, {});
                const bool picked = !ctx.doc.selection().empty();
                const QByteArray up = QString("{\\"pos\\":[%1,%2,%3],\\"yaw\\":0,\\"pitch\\":89,\\"fov\\":90,\\"x\\":0.5,\\"y\\":0.5,\\"aspect\\":1.7778}")
                                          .arg(centre.x).arg(centre.y).arg(centre.z + 90000).toUtf8();
                vweb.handle("POST", "/api/pick", up, true, {});
                r.check(haveBrush && pk.status == 200 && picked && ctx.doc.selection().empty(),
                        "POST /api/pick selects what the browser's view shows under the pointer, and nothing there deselects");
                auto livePage = web.handle("GET", "/", {}, true, {});
                auto liveJs = web.handle("GET", "/viewlive.js", {}, true, {});
                r.check(livePage.body.contains("viewLive") && livePage.body.contains("Editor view (live)") && liveJs.status == 200 &&
                            liveJs.body.contains("/api/frame") && liveJs.body.contains("/api/pick"),
                        "the browser page has the live editor view");
            }
            auto pageNow = web.handle("GET", "/", {}, true, {});
            auto jsNow = web.handle("GET", "/app.js", {}, true, {});
            r.check(pageNow.body.contains("id=\\"menubar\\"")''')
print('ok')
