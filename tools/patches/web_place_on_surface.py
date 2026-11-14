import io

# Plan 3.4 (placement): an asset placed from the browser while the live editor view is shown lands on the surface
# under the centre of that view (a pick through the browser's camera), not at height 0 of a 2D view.

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


H = 'src/qtapp/web_server.h'
patch(H, '''//   POST /api/place    {kind, name, at:[x,y,z]}   models -> misc_model, effects -> fx_origin, sounds -> sound_emitter
''', '''//   POST /api/place    {kind, name, at:[x,y,z]}   models -> misc_model, effects -> fx_origin, sounds -> sound_emitter;
//                                    with view: {pos, yaw, pitch, fov, x, y, aspect} instead of at, on the surface
//                                    under that point of the browser's live view (400 when nothing is there)
''')
patch(H, '''        // The editor's own camera, as {pos: [x, y, z], yaw, pitch, fov}: where a browser's view starts.
''', '''        // The point of the scene under a point of that view; false when the ray hits nothing.
        std::function<bool(const WebCamera& cam, double x, double y, double aspect, double point[3])> surfacePoint;
        // The editor's own camera, as {pos: [x, y, z], yaw, pitch, fov}: where a browser's view starts.
''')
S = 'src/qtapp/web_server.cpp'
patch(S, '''        const QJsonArray at = in.value("at").toArray();
        if (name.empty() || name.find_first_of(" \\t\\"") != std::string::npos || at.size() != 3) return error(400, "need kind, name and at");
        const std::string origin = formatNumber(at[0].toDouble()) + " " + formatNumber(at[1].toDouble()) + " " + formatNumber(at[2].toDouble());
''', '''        QJsonArray at = in.value("at").toArray();
        if (in.value("view").isObject()) {
            // On the surface the browser's live view shows under the given point.
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
        if (name.empty() || name.find_first_of(" \\t\\"") != std::string::npos || at.size() != 3) return error(400, "need kind, name and at");
        const std::string origin = formatNumber(at[0].toDouble()) + " " + formatNumber(at[1].toDouble()) + " " + formatNumber(at[2].toDouble());
''')
MH = 'src/qtapp/main_window.h'
patch(MH, '''    QJsonObject webCamera() const;
''', '''    QJsonObject webCamera() const;
    bool webSurfacePoint(const struct WebCamera& cam, double x, double y, double aspect, double point[3]);
''')
M = 'src/qtapp/main_window.cpp'
patch(M, '''        hooks.camera = [this] { return webCamera(); };
''', '''        hooks.camera = [this] { return webCamera(); };
        hooks.surfacePoint = [this](const WebCamera& cam, double x, double y, double aspect, double point[3]) {
            return webSurfacePoint(cam, x, y, aspect, point);
        };
''')
patch(M, '''QString MainWindow::webCommandBlocked(const QString& id, const QString& label) {''', '''bool MainWindow::webSurfacePoint(const WebCamera& cam, double x, double y, double aspect, double point[3]) {
    app::Camera c = ctx_.cam;
    c.pos = Vec3{cam.pos[0], cam.pos[1], cam.pos[2]};
    c.yaw = cam.yaw, c.pitch = cam.pitch, c.fov = cam.fov;
    ctx_.scene.sync(ctx_.doc);
    const Ray ray = app::cameraRay(c, 2.0 * x - 1.0, 1.0 - 2.0 * y, aspect);
    double t = 0;
    if (!ctx_.scene.pick(ray, &t) || t < 1.0) return false;  // nothing there, or the camera is inside an object
    const Vec3 p = ray.origin + ray.dir * t;
    point[0] = p.x, point[1] = p.y, point[2] = p.z;
    return true;
}

QString MainWindow::webCommandBlocked(const QString& id, const QString& label) {''')

J = 'data/web/app.js'
patch(J, '''      // Place at the 2D view centre (the third axis at 0).
      const [h, v] = axes(), at = [0, 0, 0];
      at[h] = view.cx; at[v] = view.cy;
      await post("/api/place", { kind, name: it.name, at, loop: !!it.loop });
      setStatus("Placed " + it.name);''', '''      // In the live editor view: on the surface under the view's centre. Else at the 2D view centre (third axis 0).
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
      setStatus("Placed " + it.name);''')

T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''                auto livePage = web.handle("GET", "/", {}, true, {});
                auto liveJs = web.handle("GET", "/viewlive.js", {}, true, {});''', '''                {
                    // An asset placed through the live view lands on the surface under the view's centre.
                    ch.surfacePoint = [&w](const WebCamera& cam, double x, double y, double aspect, double point[3]) {
                        return w.webSurfacePoint(cam, x, y, aspect, point);
                    };
                    WebServer pweb(&ctx, ch);
                    // A point of the brush's top with nothing standing on it (the room's centre has a spawn, whose box
                    // would hold the camera): the first of a few where a ray straight down meets the top itself.
                    ctx.scene.sync(ctx.doc);
                    for (const auto* g : ctx.scene.items()) {
                        if (g->kind != iw8r::app::PrimGeom::Brush || !g->bounds.valid()) continue;
                        bool found = false;
                        for (const double fu : {0.12, 0.31, 0.69, 0.88})
                            for (const double fv : {0.12, 0.31, 0.69, 0.88}) {
                                if (found) break;
                                const Vec3 top{g->bounds.mins.x + (g->bounds.maxs.x - g->bounds.mins.x) * fu,
                                               g->bounds.mins.y + (g->bounds.maxs.y - g->bounds.mins.y) * fv, g->bounds.maxs.z};
                                double tt = 0;
                                if (ctx.scene.pick(Ray{Vec3{top.x, top.y, top.z + 40}, Vec3{0, 0, -1}}, &tt) && std::fabs(tt - 40.0) < 0.5) centre = top, found = true;
                            }
                        break;
                    }
                    const size_t entsBefore = ctx.doc.map().entities.size();
                    const QByteArray placeDown = QString("{\\"kind\\":\\"effects\\",\\"name\\":\\"test_fx\\",\\"view\\":{\\"pos\\":[%1,%2,%3],\\"yaw\\":0,\\"pitch\\":-89,"
                                                         "\\"fov\\":90,\\"x\\":0.5,\\"y\\":0.5,\\"aspect\\":1.7778}}")
                                                     .arg(centre.x).arg(centre.y).arg(centre.z + 40).toUtf8();
                    const auto placed = pweb.handle("POST", "/api/place", placeDown, true, {});
                    double placedZ = 1e9;
                    if (ctx.doc.map().entities.size() == entsBefore + 1)
                        if (const auto o = ctx.doc.map().entities.back().origin()) placedZ = o->z;
                    if (ctx.doc.map().entities.size() == entsBefore + 1) ctx.doc.undo();
                    const QByteArray placeUp = QString("{\\"kind\\":\\"effects\\",\\"name\\":\\"test_fx\\",\\"view\\":{\\"pos\\":[%1,%2,%3],\\"yaw\\":0,\\"pitch\\":89,"
                                                       "\\"fov\\":90,\\"x\\":0.5,\\"y\\":0.5,\\"aspect\\":1.7778}}")
                                                   .arg(centre.x).arg(centre.y).arg(centre.z + 90000).toUtf8();
                    const auto nowhere = pweb.handle("POST", "/api/place", placeUp, true, {});
                    ctx.doc.deselectAll();
                    ctx.documentEdited();
                    r.check(haveBrush && placed.status == 200 && std::fabs(placedZ - centre.z) <= 1.0 && nowhere.status == 400 &&
                                ctx.doc.map().entities.size() == entsBefore,
                            QString("POST /api/place with a view puts the asset on the surface under the view's centre (z %1, the brush top %2)")
                                .arg(placedZ).arg(centre.z));
                }
                auto livePage = web.handle("GET", "/", {}, true, {});
                auto liveJs = web.handle("GET", "/viewlive.js", {}, true, {});''')
print('ok')
