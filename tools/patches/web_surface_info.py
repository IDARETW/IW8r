import io

# Plan 3.3, forms 9 and 10: Surface / UV as a web form (the Surface panel's fields and buttons on the selection), and
# Material / Model / Effect Info for the rows of the web Assets panel (the text of the editor's info windows).

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


# ---- the Surface panel: what the web form reads and writes
patch('src/qtapp/panels.h', '''private:
    void apply(const Primitive::TexFields& f, const QString& what);
    void nudge(int field, double delta, bool multiply);
public:
''', '''public:
    // The given texture fields on the selected faces, or on every face of the selected brushes (one undo step).
    void apply(const Primitive::TexFields& f, const QString& what);
    // What the panel shows: the first selected face (or the first face of the first selected brush).
    struct Current {
        bool any = false;    // there is a brush or a face to edit
        bool faces = false;  // faces are selected (else whole brushes)
        size_t count = 0;    // selected faces, or selected brushes
        QString material;
        bool hasTex = false;
        std::array<double, 5> tex{};  // width, height, offset x, offset y, rotation
        double lightmapX = 16384, lightmapY = 16384;
    };
    Current current() const;
private:
    void nudge(int field, double delta, bool multiply);
public:
''')
s = read('src/qtapp/panels.h')
if '#include <array>' not in s:
    s = s.replace('#include <', '#include <array>\n#include <', 1)
    write('src/qtapp/panels.h', s)
patch('src/qtapp/panels.cpp', '''void SurfacePanel::nudge(int field, double delta, bool multiply) {''', '''SurfacePanel::Current SurfacePanel::current() const {
    Current c;
    const Primitive* p = nullptr;
    size_t face = 0;
    c.faces = !ctx_->doc.faceSelection().empty();
    if (c.faces) {
        const FaceRef& fr = *ctx_->doc.faceSelection().begin();
        const EntityItem* it = ctx_->doc.findItem(fr.entity, fr.item);
        p = it ? it->prim() : nullptr;
        face = fr.face;
        c.count = ctx_->doc.faceSelection().size();
    } else {
        for (const auto& r : ctx_->doc.selection()) {
            const EntityItem* it = r.item ? ctx_->doc.findItem(r.entity, r.item) : nullptr;
            if (!it || !it->prim() || it->prim()->kind != PrimKind::Brush) continue;
            if (!p) p = it->prim();
            ++c.count;
        }
    }
    if (!p || face >= p->faces.size()) return c;
    c.any = true;
    c.material = qs(p->faces[face].material());
    if (const auto v = p->textureFields(face)) c.tex = *v, c.hasTex = true;
    if (const auto lp = lightmapParamsOfFace(*p, face)) c.lightmapX = lp->sx, c.lightmapY = lp->sy;
    return c;
}

void SurfacePanel::nudge(int field, double delta, bool multiply) {''')

# ---- the info windows: the text without the window
H = 'src/qtapp/main_window.h'
patch(H, '''    QString showMaterialInfo(const QString& name);
    QString showModelInfo(const QString& name);
    QString showEffectInfo(const QString& name);''', '''    // The info text of an asset; `show` false returns the text and opens no window (the web page shows it).
    QString showMaterialInfo(const QString& name, bool show = true);
    QString showModelInfo(const QString& name, bool show = true);
    QString showEffectInfo(const QString& name, bool show = true);
    // {title, html} for the web Assets panel; empty when the kind has no info or the name is not an asset name.
    QJsonObject webAssetInfo(const QString& kind, const QString& name);''')
M = 'src/qtapp/main_window.cpp'
patch(M, '''QString MainWindow::showMaterialInfo(const QString& name) {''', '''QString MainWindow::showMaterialInfo(const QString& name, bool show) {''')
patch(M, '''    const QString html = materialInfoHtml(in);
    showAssetInfo(this, "materialInfoDialog", "Material Info: " + name, html);''', '''    const QString html = materialInfoHtml(in);
    if (show) showAssetInfo(this, "materialInfoDialog", "Material Info: " + name, html);''')
patch(M, '''QString MainWindow::showEffectInfo(const QString& name) {''', '''QString MainWindow::showEffectInfo(const QString& name, bool show) {''')
patch(M, '''                    .arg(list(v.materials), list(v.models), list(v.sounds), list(v.childEffects), found.toHtmlEscaped());
    }
    showAssetInfo(this, "effectInfoDialog", "Effect Info: " + name, html);''', '''                    .arg(list(v.materials), list(v.models), list(v.sounds), list(v.childEffects), show ? found.toHtmlEscaped() : QString());
    }
    if (!show) return html;
    showAssetInfo(this, "effectInfoDialog", "Effect Info: " + name, html);''')
patch(M, '''QString MainWindow::showModelInfo(const QString& name) {''', '''QString MainWindow::showModelInfo(const QString& name, bool show) {''')
patch(M, '''    const QString html = modelInfoHtml(in);
    showAssetInfo(this, "modelInfoDialog", "Model Info: " + name, html);''', '''    const QString html = modelInfoHtml(in);
    if (show) showAssetInfo(this, "modelInfoDialog", "Model Info: " + name, html);''')
patch(M, '''QJsonArray MainWindow::webPrefabList() const {''', '''QJsonObject MainWindow::webAssetInfo(const QString& kind, const QString& name) {
    // An asset name only: the effect info also reads an export file named by its path, which is not for a browser.
    if (name.isEmpty() || name.size() > 200 || name.contains(':') || name.contains("..") || name.startsWith('/') || name.startsWith('\\\\') ||
        name.endsWith(".json", Qt::CaseInsensitive) || name.contains('<') || name.contains('"'))
        return {};
    if (kind == "materials") return {{"title", "Material Info: " + name}, {"html", showMaterialInfo(name, false)}};
    if (kind == "models") return {{"title", "Model Info: " + name}, {"html", showModelInfo(name, false)}};
    if (kind == "effects") return {{"title", "Effect Info: " + name}, {"html", showEffectInfo(name, false)}};
    return {};
}

QJsonArray MainWindow::webPrefabList() const {''')
patch(M, '''        hooks.placePrefab = [this](const QString& id, const WebCamera* cam, QString* why) { return webPlacePrefab(id, cam, why); };''',
      '''        hooks.placePrefab = [this](const QString& id, const WebCamera* cam, QString* why) { return webPlacePrefab(id, cam, why); };
        hooks.assetInfo = [this](const QString& kind, const QString& name) { return webAssetInfo(kind, name); };''')

patch('src/qtapp/web_server.h', '''        // The web forms (MainWindow::webFormList / webForm / applyWebForm).''', '''        // The info text of a material, model or effect: {title, html}; empty = none.
        std::function<QJsonObject(const QString& kind, const QString& name)> assetInfo;
        // The web forms (MainWindow::webFormList / webForm / applyWebForm).''')
patch('src/qtapp/web_server.cpp', '''        if (path == "/api/forms") {''', '''        if (path == "/api/assetinfo") {
            const QJsonObject info = hooks_.assetInfo ? hooks_.assetInfo(q.queryItemValue("kind"), q.queryItemValue("name", QUrl::FullyDecoded)) : QJsonObject();
            if (info.isEmpty()) return error(404, "there is no info for this");
            Response r;
            r.body = json(info);
            return r;
        }
        if (path == "/api/forms") {''')

# ---- the Surface form
W = 'src/qtapp/main_window_web.cpp'
patch(W, '''                      QJsonObject{{"id", "bakeSettings"}, {"title", "Lightmap Bake Settings"}, {"command", ""}, {"menu", "Compile"}},''',
      '''                      QJsonObject{{"id", "surface"}, {"title", "Surface (texture of the selection)"}, {"command", ""}, {"menu", "Edit"}},
                      QJsonObject{{"id", "bakeSettings"}, {"title", "Lightmap Bake Settings"}, {"command", ""}, {"menu", "Compile"}},''')
patch(W, '''    if (id == "gameModes") return webGameModesForm();
''', '''    if (id == "gameModes") return webGameModesForm();
    if (id == "surface") {
        const SurfacePanel* sp = findChild<SurfacePanel*>();
        const SurfacePanel::Current c = sp ? sp->current() : SurfacePanel::Current{};
        const auto report = [](const QString& t) { return QJsonObject{{"key", "selection"}, {"label", "Selection"}, {"type", "report"}, {"value", t}}; };
        QJsonArray f;
        if (!c.any) {
            f.append(report("Nothing to edit: select one or more brushes first (click them in a 2D view or in the live view)."));
        } else {
            f.append(report(QString("%1 %2; material %3.\\nThe fields show the first face. A field you change goes to %4.")
                                .arg(c.count)
                                .arg(c.faces ? "face(s)" : "brush(es)", c.material, c.faces ? "the selected faces" : "every face of the selected brushes")));
            if (c.hasTex) {
                f.append(number("width", "Texture width (units one repeat covers)", c.tex[0], -65536, 65536, 8));
                f.append(number("height", "Texture height", c.tex[1], -65536, 65536, 8));
                f.append(number("xoff", "Offset X", c.tex[2], -65536, 65536, 1));
                f.append(number("yoff", "Offset Y", c.tex[3], -65536, 65536, 1));
                f.append(number("rot", "Rotation (degrees)", c.tex[4], -360, 360, 15));
            }
            f.append(number("lightmapX", "Lightmap scale X (16384 = default; smaller = finer)", c.lightmapX, 1, 1048576, 1024));
            f.append(number("lightmapY", "Lightmap scale Y", c.lightmapY, 1, 1048576, 1024));
            f.append(QJsonObject{{"key", "op"}, {"label", "Then"}, {"type", "choice"}, {"value", "none"},
                                 {"choices", QJsonArray{"none", "fit", "natural", "left", "right", "top", "bottom", "center"}},
                                 {"choiceLabels", QJsonArray{"(nothing more)", "Fit the texture to each face", "Natural size", "Justify left", "Justify right",
                                                             "Justify top", "Justify bottom", "Justify centre"}},
                                 {"help", "One undo step for the whole form"}});
        }
        return QJsonObject{{"id", id}, {"title", "Surface"}, {"fields", f}, {"reload", true}};
    }
''')
patch(W, '''    if (id == "gameModes") return applyWebGameModes(values, why);
''', '''    if (id == "gameModes") return applyWebGameModes(values, why);
    if (id == "surface") {
        SurfacePanel* sp = findChild<SurfacePanel*>();
        const SurfacePanel::Current c = sp ? sp->current() : SurfacePanel::Current{};
        if (!c.any) return no("select one or more brushes first");
        Primitive::TexFields tf;
        std::optional<double>* slot[5] = {&tf.width, &tf.height, &tf.xoff, &tf.yoff, &tf.rot};
        static const char* const keys[5] = {"width", "height", "xoff", "yoff", "rot"};
        bool texChanged = false;
        for (int i = 0; i < 5; ++i) {
            if (!values.contains(keys[i])) continue;
            double v = 0;
            if (!numberIn(values, keys[i], i == 4 ? -360 : -65536, i == 4 ? 360 : 65536, v, why)) return false;
            if (i < 2 && std::abs(v) < 0.001) return no("the texture width and height are not 0");
            *slot[i] = v;
            texChanged = true;
        }
        double lx = c.lightmapX, ly = c.lightmapY;
        if (!numberIn(values, "lightmapX", 1, 1048576, lx, why) || !numberIn(values, "lightmapY", 1, 1048576, ly, why)) return false;
        const QString op = values.value("op").toString("none");
        static const char* const ops[] = {"fit", "natural", "left", "right", "top", "bottom", "center"};
        int opIndex = -1;
        for (int i = 0; i < 7; ++i)
            if (op == ops[i]) opIndex = i;
        if (opIndex < 0 && op != "none") return no("there is no surface action " + op);
        // Everything was accepted: now write, as one undo step.
        ctx_.doc.beginGroup("Surface");
        if (texChanged) sp->apply(tf, "Surface");
        if ((values.contains("lightmapX") || values.contains("lightmapY")) && sp->applyLightmapScale(lx, ly)) ctx_.documentEdited("Lightmap scale");
        if (opIndex >= 0) sp->applyOp(SurfacePanel::Op(opIndex));
        ctx_.doc.endGroup();
        return true;
    }
''')
s = read(W)
if '#include "panels.h"' not in s:
    s = s.replace('#include "map_gametypes.h"', '#include "map_gametypes.h"\n#include "panels.h"', 1)
    write(W, s)

# ---- the page
J = 'data/web/app.js'
patch(J, '''    li.title = it.note || it.name;
''', '''    li.title = it.note || it.name;
    if (kind === "materials" || kind === "models" || kind === "effects") {
      const b = document.createElement("button");
      b.type = "button"; b.className = "info"; b.textContent = "i"; b.title = "Info about " + it.name;
      b.onclick = (ev) => { ev.stopPropagation(); window.radiant.openInfo(kind, it.name); };
      li.appendChild(b);
    }
''')
patch(J, '''  openFormId = id;
  $("formDialog").dataset.sendAll = form.sendAll ? "1" : "";
''', '''  openFormId = id;
  $("formApply").hidden = false;
  $("formDialog").dataset.sendAll = form.sendAll ? "1" : "";
  $("formDialog").dataset.reload = form.reload ? "1" : "";
''')
patch(J, '''    setStatus($("formTitle").textContent + " applied");
    queueRefresh();
  }
  if (thenClose) closeForm();
}''', '''    setStatus($("formTitle").textContent + " applied");
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
window.radiant.openInfo = openInfo;''')
patch('data/web/style.css', '''#assetList span { overflow: hidden; text-overflow: ellipsis; }''', '''#assetList span { overflow: hidden; text-overflow: ellipsis; }
#assetList button.info { margin-left: auto; flex: none; width: 20px; height: 20px; padding: 0; border-radius: 10px; font: italic 12px Georgia, serif; }
#formFields div.info { max-height: 60vh; overflow: auto; font-size: 12px; line-height: 1.45; }
#formFields div.info table { border-collapse: collapse; }
#formFields div.info td, #formFields div.info th { padding: 2px 10px 2px 0; vertical-align: top; text-align: left; }
#formFields div.info h3, #formFields div.info h4 { margin: 8px 0 4px; }''')

# ---- GUI test
T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''                r.check(forms.size() == 7 && sun.value("title").toString() == "Sun & Sky" &&''',
      '''                r.check(forms.size() == 8 && sun.value("title").toString() == "Sun & Sky" &&''')
patch(T, '''                {
                    // Preferences: the editing, build and F5 settings; the host command and the folders stay at the editor.''', '''                {
                    // Surface: the Surface panel's fields on the selection. Nothing selected = nothing to edit.
                    ctx.doc.deselectAll();
                    const QJsonObject none = QJsonDocument::fromJson(fweb.handle("GET", "/api/form?id=surface", {}, true, {}).body).object();
                    const auto refused = fweb.handle("POST", "/api/form", "{\\"id\\":\\"surface\\",\\"values\\":{\\"xoff\\":32}}", true, {});
                    const Entity* wsS = ctx.doc.worldspawn();
                    SelRef brush{};
                    for (size_t n = 0; wsS && n < 64 && !brush.item; ++n)
                        if (const EntityItem* it = nthPrim(*wsS, n); it && it->prim() && it->prim()->kind == PrimKind::Brush && it->prim()->textureFields(0))
                            brush = SelRef{wsS->id, it->id};
                    ctx.doc.select(brush, false);
                    const size_t undoS = ctx.doc.undoPosition();
                    const auto fieldsOf = [&](size_t face) {
                        const EntityItem* it = ctx.doc.findItem(brush.entity, brush.item);
                        return it && it->prim() ? it->prim()->textureFields(face) : std::nullopt;
                    };
                    const auto before = fieldsOf(0);
                    const QJsonObject sf = QJsonDocument::fromJson(fweb.handle("GET", "/api/form?id=surface", {}, true, {}).body).object();
                    QStringList skeys;
                    for (const QJsonValue& f : sf.value("fields").toArray()) skeys << f.toObject().value("key").toString();
                    const auto setS = fweb.handle("POST", "/api/form", "{\\"id\\":\\"surface\\",\\"values\\":{\\"xoff\\":32,\\"rot\\":45,\\"op\\":\\"none\\"}}", true, {});
                    const auto after0 = fieldsOf(0), after1 = fieldsOf(1);
                    const bool set = setS.status == 200 && before && after0 && after1 && (*after0)[2] == 32 && (*after0)[4] == 45 && (*after1)[2] == 32 &&
                                     (*after0)[0] == (*before)[0] && ctx.doc.undoPosition() == undoS + 1;
                    const auto fit = fweb.handle("POST", "/api/form", "{\\"id\\":\\"surface\\",\\"values\\":{\\"op\\":\\"fit\\",\\"lightmapX\\":8192}}", true, {});
                    const bool oneStep = ctx.doc.undoPosition() == undoS + 2;
                    const QJsonObject sf2 = QJsonDocument::fromJson(fweb.handle("GET", "/api/form?id=surface", {}, true, {}).body).object();
                    double lmShown = 0;
                    for (const QJsonValue& f : sf2.value("fields").toArray())
                        if (f.toObject().value("key").toString() == "lightmapX") lmShown = f.toObject().value("value").toDouble();
                    const auto zero = fweb.handle("POST", "/api/form", "{\\"id\\":\\"surface\\",\\"values\\":{\\"width\\":0}}", true, {});
                    const auto badOp = fweb.handle("POST", "/api/form", "{\\"id\\":\\"surface\\",\\"values\\":{\\"xoff\\":1,\\"op\\":\\"melt\\"}}", true, {});
                    const bool kept = ctx.doc.undoPosition() == undoS + 2;
                    while (ctx.doc.undoPosition() > undoS) ctx.doc.undo();
                    const auto back = fieldsOf(0);
                    ctx.doc.deselectAll();
                    ctx.documentEdited();
                    r.check(none.value("fields").toArray().size() == 1 && refused.status == 400 && brush.item && skeys.contains("width") && skeys.contains("op") &&
                                set && fit.status == 200 && oneStep && lmShown == 8192 && zero.status == 400 && badOp.status == 400 && kept && back && before &&
                                *back == *before,
                            QString("the Surface form edits the selected brush's texture fields, one undo step per form (set %1, fit %2, lightmap %3)")
                                .arg(set).arg(fit.status).arg(lmShown));
                }
                {
                    // Asset info for the web Assets panel: the info window's text, and no window opens.
                    ch.assetInfo = [&w](const QString& kind, const QString& name) { return w.webAssetInfo(kind, name); };
                    WebServer iweb(&ctx, ch);
                    const bool dialogBefore = w.findChild<QDialog*>("materialInfoDialog") != nullptr;
                    const auto mi = iweb.handle("GET", "/api/assetinfo?kind=materials&name=caulk", {}, true, {});
                    const QJsonObject mo = QJsonDocument::fromJson(mi.body).object();
                    const auto fi = iweb.handle("GET", "/api/assetinfo?kind=effects&name=vfx/iw8r/no_such_effect_here", {}, true, {});
                    const auto path = iweb.handle("GET", "/api/assetinfo?kind=effects&name=C:/secret/x.asset.json", {}, true, {});
                    const auto up = iweb.handle("GET", "/api/assetinfo?kind=effects&name=../../x", {}, true, {});
                    const auto sound = iweb.handle("GET", "/api/assetinfo?kind=sounds&name=amb_wind", {}, true, {});
                    const auto page = iweb.handle("GET", "/app.js", {}, true, {});
                    r.check(mi.status == 200 && mo.value("title").toString() == "Material Info: caulk" && mo.value("html").toString().contains("caulk") &&
                                fi.status == 200 && path.status == 404 && up.status == 404 && sound.status == 404 &&
                                (w.findChild<QDialog*>("materialInfoDialog") != nullptr) == dialogBefore && !w.findChild<QDialog*>("effectInfoDialog") &&
                                page.body.contains("openInfo") && page.body.contains("safeHtml"),
                            "material and effect info reach the browser as text; a file path is refused and no window opens at the editor");
                }
                {
                    // Preferences: the editing, build and F5 settings; the host command and the folders stay at the editor.''')
print('ok')
