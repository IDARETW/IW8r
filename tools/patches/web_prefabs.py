import io

# Plan 3.3, item 8 (Prefabs): the prefab library in the browser's Assets panel. The list, pictures and placing all go
# through the editor's own Prefabs dock (PrefabBrowser), so bundles keep their badges and a prefab that cannot be
# placed says why.

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
patch(H, '''//   GET  /api/assets?kind=materials|models|effects|sounds&q=text&limit=N   catalog names (unbuildable ones left out)''',
      '''//   GET  /api/assets?kind=materials|models|effects|sounds|prefabs&q=text&limit=N   catalog names (unbuildable ones
//                                    left out); prefabs: the editor's prefab library, each {name, id, note}, placed
//                                    with POST /api/place {kind: "prefabs", name: id} in front of the view''')
patch(H, '''        // The web forms (MainWindow::webFormList / webForm / applyWebForm).''', '''        // The prefab library (the Prefabs dock): [{name, id, note}] (note: why it cannot be placed, "" = it can), and
        // placing one by id in front of the editor's camera or, with a camera, in front of that view.
        std::function<QJsonArray()> prefabs;
        std::function<bool(const QString& id, const WebCamera* cam, QString* why)> placePrefab;
        // The web forms (MainWindow::webFormList / webForm / applyWebForm).''')
S = 'src/qtapp/web_server.cpp'
patch(S, '''            const QString kind = q.queryItemValue("kind"), needle = q.queryItemValue("q").trimmed().toLower();
            const int limit = std::clamp(q.queryItemValue("limit").toInt() > 0 ? q.queryItemValue("limit").toInt() : 200, 1, 2000);
            QJsonArray names;
            int total = 0;''', '''            const QString kind = q.queryItemValue("kind"), needle = q.queryItemValue("q").trimmed().toLower();
            const int limit = std::clamp(q.queryItemValue("limit").toInt() > 0 ? q.queryItemValue("limit").toInt() : 200, 1, 2000);
            QJsonArray names;
            int total = 0;
            if (kind == "prefabs") {
                for (const QJsonValue& v : hooks_.prefabs ? hooks_.prefabs() : QJsonArray()) {
                    if (!needle.isEmpty() && !v.toObject().value("name").toString().toLower().contains(needle)) continue;
                    ++total;
                    if (names.size() < limit) names.append(v);
                }
                Response r;
                r.body = json({{"kind", kind}, {"total", total}, {"names", names}});
                return r;
            }''')
patch(S, '''        const QString kind = in.value("kind").toString();
        const std::string name = in.value("name").toString().trimmed().toStdString();
        QJsonArray at = in.value("at").toArray();''', '''        const QString kind = in.value("kind").toString();
        const std::string name = in.value("name").toString().trimmed().toStdString();
        if (kind == "prefabs") {
            // A prefab of the library, in front of the browser's live view when one is given, else of the editor's camera.
            WebCamera cam;
            bool haveCam = false;
            if (in.value("view").isObject()) {
                const QJsonObject v = in.value("view").toObject();
                const QJsonArray pos = v.value("pos").toArray();
                if (pos.size() == 3) {
                    for (int k = 0; k < 3; ++k) cam.pos[k] = pos[k].toDouble();
                    cam.yaw = v.value("yaw").toDouble();
                    cam.pitch = std::clamp(v.value("pitch").toDouble(), -89.0, 89.0);
                    cam.fov = std::clamp(v.value("fov").toDouble(90), 10.0, 150.0);
                    haveCam = true;
                }
            }
            QString why;
            if (!hooks_.placePrefab || !hooks_.placePrefab(in.value("name").toString(), haveCam ? &cam : nullptr, &why))
                return error(400, why.isEmpty() ? QString("the prefab cannot be placed") : why);
            ok.body = stateJson();
            return ok;
        }
        QJsonArray at = in.value("at").toArray();''')

MH = 'src/qtapp/main_window.h'
patch(MH, '''    QJsonObject webCamera() const;
''', '''    QJsonObject webCamera() const;
    // The prefab library for the browser (the Prefabs dock's rows) and one placed by its row id.
    QJsonArray webPrefabList() const;
    bool webPlacePrefab(const QString& id, const struct WebCamera* cam, QString* why);
''')
M = 'src/qtapp/main_window.cpp'
patch(M, '''        hooks.forms = [this] { return webFormList(); };
''', '''        // The Prefabs dock fills its list when it is first shown: a browser may ask before that.
        hooks.prefabs = [this] {
            if (prefabs_ && prefabs_->count() == 0) prefabs_->refresh();
            return webPrefabList();
        };
        hooks.placePrefab = [this](const QString& id, const WebCamera* cam, QString* why) { return webPlacePrefab(id, cam, why); };
        hooks.forms = [this] { return webFormList(); };
''')
patch(M, '''            if (kind == "materials") {
                if (auto e = matCache_->lookup(name); e && !e->failed && !e->colorDds.isEmpty()) {''', '''            if (kind == "prefabs" && prefabs_) {
                bool isRow = false;
                const int row = name.toInt(&isRow);
                if (isRow && row >= 0 && row < prefabs_->count()) img = prefabs_->pictureAt(row);
            }
            if (kind == "materials") {
                if (auto e = matCache_->lookup(name); e && !e->failed && !e->colorDds.isEmpty()) {''')
patch(M, '''QJsonObject MainWindow::webCamera() const {''', '''QJsonArray MainWindow::webPrefabList() const {
    QJsonArray out;
    if (!prefabs_) return out;
    for (int row = 0; row < prefabs_->count(); ++row) {
        QString name = prefabs_->titleAt(row);
        name.replace('\\n', ' ');
        out.append(QJsonObject{{"name", name.trimmed()}, {"id", QString::number(row)},
                               {"note", prefabs_->placeableAt(row) ? QString() : prefabs_->badgeReasonAt(row)}});
    }
    return out;
}

bool MainWindow::webPlacePrefab(const QString& id, const WebCamera* cam, QString* why) {
    bool isRow = false;
    const int row = id.toInt(&isRow);
    if (!prefabs_ || !isRow || row < 0 || row >= prefabs_->count()) {
        if (why) *why = "there is no prefab " + id;
        return false;
    }
    // The dock places a prefab in front of the editor's camera: for a browser's view, in front of that camera.
    const app::Camera saved = ctx_.cam;
    if (cam) {
        ctx_.cam.pos = Vec3{cam->pos[0], cam->pos[1], cam->pos[2]};
        ctx_.cam.yaw = cam->yaw, ctx_.cam.pitch = cam->pitch, ctx_.cam.fov = cam->fov;
    }
    QString err;
    const bool ok = prefabs_->stamp(row, &err);
    ctx_.cam = saved;
    if (!ok && why) *why = err.isEmpty() ? prefabs_->badgeReasonAt(row) : err;
    return ok;
}

QJsonObject MainWindow::webCamera() const {''')
s = read(M)
if '#include "prefab_browser.h"' not in s:
    i = s.index('#include "main_window.h"')
    j = s.index('\n', i) + 1
    s = s[:j] + '#include "prefab_browser.h"\n' + s[j:]
    write(M, s)

patch('data/web/index.html', '''<option value="sounds">Sounds</option></select>''', '''<option value="sounds">Sounds</option><option value="prefabs">Prefabs</option></select>''')
J = 'data/web/app.js'
patch(J, '''    if (kind === "materials" || kind === "models") {
      const img = document.createElement("img");
      img.loading = "lazy";
      img.alt = "";
      img.src = api(`/api/thumb?kind=${encodeURIComponent(kind)}&name=${encodeURIComponent(it.name)}`);''', '''    if (kind === "materials" || kind === "models" || kind === "prefabs") {
      const img = document.createElement("img");
      img.loading = "lazy";
      img.alt = "";
      img.src = api(`/api/thumb?kind=${encodeURIComponent(kind)}&name=${encodeURIComponent(kind === "prefabs" ? it.id : it.name)}`);''')
patch(J, '''    span.textContent = it.name + (it.loop ? "  (loop)" : "");
    li.appendChild(span);
    li.title = it.name;
    li.onclick = async () => {
      if (kind === "materials") { await post("/api/material", { name: it.name }); setStatus("Material " + it.name); return; }''', '''    span.textContent = it.name + (it.loop ? "  (loop)" : "") + (it.note ? "  (cannot be placed)" : "");
    li.appendChild(span);
    li.title = it.note || it.name;
    li.onclick = async () => {
      if (kind === "materials") { await post("/api/material", { name: it.name }); setStatus("Material " + it.name); return; }
      if (kind === "prefabs") {
        // In front of the live view's camera when it is shown, else of the editor's own camera.
        const body = { kind, name: it.id };
        const pc = !$("viewLive").hidden && window.viewLive && window.viewLive.camera();
        if (pc) body.view = { pos: pc.pos, yaw: pc.yaw, pitch: pc.pitch, fov: pc.fov };
        try { await post("/api/place", body); setStatus("Placed prefab " + it.name); } catch (e) { /* shown */ }
        return;
      }''')
patch('data/web/index.html', '''Click a material to apply it to the selection; a model, effect or sound to place it at the view centre.''',
      '''Click a material to apply it to the selection; a model, effect, sound or prefab to place it.''')

T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''                auto livePage = web.handle("GET", "/", {}, true, {});
                auto liveJs = web.handle("GET", "/viewlive.js", {}, true, {});''', '''                {
                    // The prefab library in the browser: the Prefabs dock's rows, with a picture, placed like a double-click.
                    ch.prefabs = [&w] { return w.webPrefabList(); };
                    ch.placePrefab = [&w](const QString& id, const WebCamera* cam, QString* why) { return w.webPlacePrefab(id, cam, why); };
                    if (w.prefabBrowser() && w.prefabBrowser()->count() == 0) w.prefabBrowser()->refresh();  // the dock fills when first shown
                    WebServer bweb(&ctx, ch);
                    const QJsonObject list = QJsonDocument::fromJson(bweb.handle("GET", "/api/assets?kind=prefabs&limit=2000", {}, true, {}).body).object();
                    const QJsonArray rows = list.value("names").toArray();
                    QString pick;
                    for (const QJsonValue& v : rows)
                        if (pick.isEmpty() && v.toObject().value("note").toString().isEmpty()) pick = v.toObject().value("id").toString();
                    const size_t undoP = ctx.doc.undoPosition();
                    const size_t entsP = ctx.doc.map().entities.size();
                    const auto put = bweb.handle("POST", "/api/place", QString("{\\"kind\\":\\"prefabs\\",\\"name\\":\\"%1\\",\\"view\\":{\\"pos\\":[0,-300,120],\\"yaw\\":90,\\"pitch\\":-10,\\"fov\\":90}}").arg(pick).toUtf8(), true, {});
                    const bool placedP = ctx.doc.map().entities.size() > entsP && ctx.doc.undoPosition() == undoP + 1;
                    const bool camBack = ctx.cam.pos.x == camWas.pos.x && ctx.cam.yaw == camWas.yaw;
                    while (ctx.doc.undoPosition() > undoP) ctx.doc.undo();
                    ctx.doc.deselectAll();
                    ctx.documentEdited();
                    const auto none = bweb.handle("POST", "/api/place", "{\\"kind\\":\\"prefabs\\",\\"name\\":\\"99999\\"}", true, {});
                    const auto filtered = QJsonDocument::fromJson(bweb.handle("GET", "/api/assets?kind=prefabs&q=zzzznothing", {}, true, {}).body).object();
                    r.check(rows.size() > 3 && !pick.isEmpty() && put.status == 200 && placedP && camBack && none.status == 400 &&
                                filtered.value("total").toInt() == 0 && ctx.doc.map().entities.size() == entsP,
                            QString("the prefab library reaches the browser (%1 prefabs) and one is placed in one undo step").arg(rows.size()));
                }
                auto livePage = web.handle("GET", "/", {}, true, {});
                auto liveJs = web.handle("GET", "/viewlive.js", {}, true, {});''')
print('ok')
