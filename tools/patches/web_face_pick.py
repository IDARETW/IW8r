import io

# Plan 3.4 (face picking): Ctrl + click in the web page's live view selects the brush face under the pointer
# (Ctrl + Shift adds or removes one), as Ctrl + Shift + click does in the editor. The Surface form then works on the
# selected faces (fit one face, justify, set its texture fields).

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


patch('src/qtapp/web_server.h', '''        std::function<bool(const WebCamera& cam, double x, double y, double aspect, bool add)> pickFrame;''',
      '''        std::function<bool(const WebCamera& cam, double x, double y, double aspect, bool add)> pickFrame;
        // The same for one brush face (the editor's Ctrl + Shift + click): add toggles it in the face selection.
        std::function<bool(const WebCamera& cam, double x, double y, double aspect, bool add)> pickFace;''')
patch('src/qtapp/web_server.cpp', '''        hooks_.pickFrame(cam, x, y, std::clamp(in.value("aspect").toDouble(16.0 / 9.0), 0.2, 5.0), in.value("add").toBool());''',
      '''        const double aspect = std::clamp(in.value("aspect").toDouble(16.0 / 9.0), 0.2, 5.0);
        if (in.value("face").toBool()) {
            if (!hooks_.pickFace) return error(400, "faces cannot be picked here");
            hooks_.pickFace(cam, x, y, aspect, in.value("add").toBool());
        } else {
            hooks_.pickFrame(cam, x, y, aspect, in.value("add").toBool());
        }''')
patch('src/qtapp/main_window.h', '''    bool webPick(const struct WebCamera& cam, double x, double y, double aspect, bool add);''',
      '''    bool webPick(const struct WebCamera& cam, double x, double y, double aspect, bool add);
    bool webPickFace(const struct WebCamera& cam, double x, double y, double aspect, bool add);''')
M = 'src/qtapp/main_window.cpp'
patch(M, '''// A menu text without its keyboard marker: "E&xit" is Exit, "Sun && Sky" is Sun & Sky.''', '''bool MainWindow::webPickFace(const WebCamera& cam, double x, double y, double aspect, bool add) {
    app::Camera c = ctx_.cam;
    c.pos = Vec3{cam.pos[0], cam.pos[1], cam.pos[2]};
    c.yaw = cam.yaw, c.pitch = cam.pitch, c.fov = cam.fov;
    ctx_.scene.sync(ctx_.doc);
    const auto face = ctx_.scene.pickFace(app::cameraRay(c, 2.0 * x - 1.0, 1.0 - 2.0 * y, aspect));
    if (face) {
        if (add) ctx_.doc.toggleFace(*face);
        else ctx_.doc.selectFace(*face, false);
    } else if (!add) {
        ctx_.doc.clearFaceSelection();
    }
    ctx_.documentEdited(face ? QString("%1 face(s) selected from the browser's view").arg(ctx_.doc.faceSelection().size()) : QString());
    return face.has_value();
}

// A menu text without its keyboard marker: "E&xit" is Exit, "Sun && Sky" is Sun & Sky.''')
patch(M, '''        hooks.pickFrame = [this](const WebCamera& cam, double x, double y, double aspect, bool add) { return webPick(cam, x, y, aspect, add); };''',
      '''        hooks.pickFrame = [this](const WebCamera& cam, double x, double y, double aspect, bool add) { return webPick(cam, x, y, aspect, add); };
        hooks.pickFace = [this](const WebCamera& cam, double x, double y, double aspect, bool add) { return webPickFace(cam, x, y, aspect, add); };''')

J = 'data/web/viewlive.js'
patch(J, '''      await R().post("/api/pick", { pos: cam.pos, yaw: cam.yaw, pitch: cam.pitch, fov: cam.fov, x: (ev.clientX - r.left) / r.width,
                                    y: (ev.clientY - r.top) / r.height, aspect: r.width / r.height, add: ev.shiftKey });''',
      '''      // Ctrl: the brush face under the pointer (for Edit > Surface) instead of the whole brush.
      await R().post("/api/pick", { pos: cam.pos, yaw: cam.yaw, pitch: cam.pitch, fov: cam.fov, x: (ev.clientX - r.left) / r.width,
                                    y: (ev.clientY - r.top) / r.height, aspect: r.width / r.height, add: ev.shiftKey,
                                    face: ev.ctrlKey || ev.metaKey });
      if (ev.ctrlKey || ev.metaKey) R().setStatus("Face selected: Edit > Surface works on it");''')

T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''                r.check(haveBrush && pk.status == 200 && picked && ctx.doc.selection().empty(),
                        "POST /api/pick selects what the browser's view shows under the pointer, and nothing there deselects");''',
      '''                r.check(haveBrush && pk.status == 200 && picked && ctx.doc.selection().empty(),
                        "POST /api/pick selects what the browser's view shows under the pointer, and nothing there deselects");
                {
                    // The same click with face: one brush face (the top, seen from above); the Surface form then names faces.
                    ch.pickFace = [&w](const WebCamera& cam, double x, double y, double aspect, bool add) { return w.webPickFace(cam, x, y, aspect, add); };
                    ch.forms = [&w] { return w.webFormList(); };
                    ch.form = [&w](const QString& id) { return w.webForm(id); };
                    WebServer faceWeb(&ctx, ch);
                    QByteArray faceDown = down;
                    faceDown.replace("\\"aspect\\":1.7778}", "\\"aspect\\":1.7778,\\"face\\":true}");
                    const auto pf = faceWeb.handle("POST", "/api/pick", faceDown, true, {});
                    const size_t faces = ctx.doc.faceSelection().size();
                    bool topFace = false;
                    if (faces == 1) {
                        const FaceRef fr = *ctx.doc.faceSelection().begin();
                        const EntityItem* it = ctx.doc.findItem(fr.entity, fr.item);
                        const auto planes = it && it->prim() ? it->prim()->planes() : std::vector<Plane>{};
                        topFace = fr.face < planes.size() && planes[fr.face].n.z > 0.9;
                    }
                    const QJsonObject sfF = QJsonDocument::fromJson(faceWeb.handle("GET", "/api/form?id=surface", {}, true, {}).body).object();
                    const QString said = sfF.value("fields").toArray().first().toObject().value("value").toString();
                    QByteArray faceUp = up;
                    faceUp.replace("\\"aspect\\":1.7778}", "\\"aspect\\":1.7778,\\"face\\":true}");
                    faceWeb.handle("POST", "/api/pick", faceUp, true, {});
                    const bool cleared = ctx.doc.faceSelection().empty();
                    r.check(pf.status == 200 && faces == 1 && topFace && said.startsWith("1 face(s)") && cleared,
                            "POST /api/pick with face selects one brush face for the Surface form: " + said.section('\\n', 0, 0));
                }''')
print('ok')
