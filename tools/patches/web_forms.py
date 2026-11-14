import io

# Plan 3.3, part 2: web forms. The dialogs Sun & Sky, Atmosphere, Lightmap Bake Settings and Map Info as forms the
# browser fills in (src/qtapp/main_window_web.cpp). GET /api/forms, GET /api/form?id=, POST /api/form {id, values}.
# In the browser's menus a command with a form is no longer greyed: it opens the form.

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


H = 'src/qtapp/web_server.h'
patch(H, '''//   POST /api/command  {id}          runs a command as the menu would (400 with the reason when it cannot)
''', '''//   POST /api/command  {id}          runs a command as the menu would (400 with the reason when it cannot)
//   GET  /api/forms                  the dialogs that have a web form: {id, title, command, menu}
//   GET  /api/form?id=ID             one form: {id, title, fields: [{key, label, type, value, min, max, step,
//                                    choices, help}]}; type number | text | choice | color (#rrggbb) | bool
//   POST /api/form     {id, values}  applies the changed fields (one undo step; 400 with the reason when a value
//                                    is not accepted)
''')
patch(H, '''        // Every command of the editor's menus, in menu order (MainWindow::webCommandList).
''', '''        // The web forms (MainWindow::webFormList / webForm / applyWebForm).
        std::function<QJsonArray()> forms;
        std::function<QJsonObject(const QString& id)> form;
        std::function<bool(const QString& id, const QJsonObject& values, QString* why)> applyForm;
        // Every command of the editor's menus, in menu order (MainWindow::webCommandList).
''')
S = 'src/qtapp/web_server.cpp'
patch(S, '''        if (path == "/api/commands") {''', '''        if (path == "/api/forms") {
            Response r;
            r.body = json({{"forms", hooks_.forms ? hooks_.forms() : QJsonArray()}});
            return r;
        }
        if (path == "/api/form") {
            const QJsonObject f = hooks_.form ? hooks_.form(q.queryItemValue("id")) : QJsonObject();
            if (f.isEmpty()) return error(404, "there is no form " + q.queryItemValue("id"));
            Response r;
            r.body = json(f);
            return r;
        }
        if (path == "/api/commands") {''')
patch(S, '''    } else if (path == "/api/command") {''', '''    } else if (path == "/api/form") {
        QString why;
        if (!hooks_.applyForm || !hooks_.applyForm(in.value("id").toString(), in.value("values").toObject(), &why))
            return error(400, why.isEmpty() ? QString("the form cannot be applied") : why);
    } else if (path == "/api/command") {''')

MH = 'src/qtapp/main_window.h'
patch(MH, '''    QJsonArray webCommandList() const;
''', '''    QJsonArray webCommandList() const;
    // Web forms (plan 3.3, main_window_web.cpp): the dialogs a browser can fill in. webForm gives one form's fields
    // with the map's current values ({} for an unknown id); applyWebForm takes the changed fields only.
    QJsonArray webFormList() const;
    QJsonObject webForm(const QString& id) const;
    bool applyWebForm(const QString& id, const QJsonObject& values, QString* why);
    static QString webFormForCommand(const QString& command);  // the form that stands for a dialog command, or ""
''')
M = 'src/qtapp/main_window.cpp'
patch(M, '''        hooks.commands = [this] { return webCommandList(); };
''', '''        hooks.forms = [this] { return webFormList(); };
        hooks.form = [this](const QString& id) { return webForm(id); };
        hooks.applyForm = [this](const QString& id, const QJsonObject& values, QString* why) { return applyWebForm(id, values, why); };
        hooks.commands = [this] { return webCommandList(); };
''')
# A command with a form is offered to the browser with that form; forms without a menu command get an entry of
# their own at the end of their menu.
patch(M, '''            out.append(QJsonObject{{"id", id}, {"label", label}, {"menu", path}, {"shortcut", a->shortcut().toString(QKeySequence::NativeText)},
                                   {"checkable", a->isCheckable()}, {"checked", a->isChecked()}, {"enabled", a->isEnabled()},
                                   {"blocked", webCommandBlocked(id, label)}});
        }
    };''', '''            const QString form = webFormForCommand(id);
            QJsonObject c{{"id", id}, {"label", label}, {"menu", path}, {"shortcut", a->shortcut().toString(QKeySequence::NativeText)},
                          {"checkable", a->isCheckable()}, {"checked", a->isChecked()}, {"enabled", a->isEnabled()},
                          {"blocked", form.isEmpty() ? webCommandBlocked(id, label) : QString()}};
            if (!form.isEmpty()) c.insert("form", form);
            out.append(c);
        }
    };''')
patch(M, '''            walk(m, title);
        }
    return out;
}''', '''            walk(m, title);
            for (const QJsonValue& f : webFormList()) {
                const QJsonObject fo = f.toObject();
                if (!fo.value("command").toString().isEmpty() || fo.value("menu").toString() != title) continue;
                out.append(QJsonObject{{"id", "form:" + fo.value("id").toString()}, {"label", fo.value("title").toString() + "..."}, {"menu", title},
                                       {"shortcut", ""}, {"checkable", false}, {"checked", false}, {"enabled", true}, {"blocked", ""},
                                       {"form", fo.value("id").toString()}});
            }
        }
    return out;
}''')
patch(M, '''    if (!a) return no("there is no command " + id);
    QString label = a->text();''', '''    if (!a) return no("there is no command " + id);
    if (!webFormForCommand(id).isEmpty()) return no("this command has a web form: " + webFormForCommand(id));
    QString label = a->text();''')

# --- the page -------------------------------------------------------------------------------------------------------
patch('data/web/index.html', '''<div id="menuPopup" hidden></div>
''', '''<div id="menuPopup" hidden></div>
<div id="formShade" hidden><form id="formDialog"><h2 id="formTitle"></h2><div id="formFields"></div>
  <div id="formError"></div><div class="buttons"><button type="submit">OK</button><button type="button" id="formApply">Apply</button><button type="button" id="formCancel">Cancel</button></div></form></div>
''')
append('data/web/style.css', '''
/* Web forms: the editor's dialogs in the page (plan 3.3). */
#formShade { position: fixed; inset: 0; z-index: 30; background: rgba(0, 0, 0, .55); display: flex; align-items: center; justify-content: center; }
#formDialog { background: var(--panel); border: 1px solid var(--line); border-radius: 6px; padding: 14px 16px; width: min(520px, 94vw);
  max-height: 90vh; overflow: auto; box-shadow: 0 10px 30px rgba(0, 0, 0, .6); }
#formDialog h2 { margin: 0 0 10px; font-size: 15px; color: var(--accent); }
#formFields label { display: grid; grid-template-columns: 1fr 190px; gap: 10px; align-items: center; padding: 3px 0; }
#formFields label small { grid-column: 1 / -1; color: var(--muted); font-size: 11px; margin-top: -2px; }
#formFields input[type=color] { padding: 0; height: 24px; }
#formFields input[type=checkbox] { justify-self: start; }
#formError { color: #ff8080; min-height: 18px; margin-top: 6px; }
#formDialog .buttons { display: flex; gap: 8px; justify-content: flex-end; margin-top: 8px; }
''')
patch('data/web/app.js', '''  if (!off) row.onclick = () => runCommand(c);
  return row;''', '''  if (!off) row.onclick = () => (c.form ? (closeMenu(), openForm(c.form)) : runCommand(c));
  return row;''')
patch('data/web/app.js', '''  const first = searchCommands().find((c) => c.enabled && !c.blocked);
  if (first) { $("cmdSearch").value = ""; runCommand(first); }''', '''  const first = searchCommands().find((c) => c.enabled && !c.blocked);
  if (first) { $("cmdSearch").value = ""; closeMenu(); if (first.form) openForm(first.form); else runCommand(first); }''')
append('data/web/app.js', '''
// --- Web forms (plan 3.3): a dialog of the editor as a form here. Only the fields that were changed are sent. ---
let openFormId = "";
function fieldInput(f) {
  let el;
  if (f.type === "choice") {
    el = document.createElement("select");
    for (const c of f.choices || []) { const o = document.createElement("option"); o.value = o.textContent = c; el.append(o); }
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
  $("formTitle").textContent = form.title;
  $("formError").textContent = "";
  const box = $("formFields");
  box.textContent = "";
  for (const f of form.fields) {
    const row = document.createElement("label");
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
    if (!el.dataset.changed) continue;
    values[el.dataset.key] = el.dataset.type === "bool" ? el.checked : el.dataset.type === "number" ? Number(el.value) : el.value;
  }
  if (Object.keys(values).length) {
    try { await post("/api/form", { id: openFormId, values }); }
    catch (e) { $("formError").textContent = e.message || "The editor did not accept the values"; return; }
    for (const el of $("formFields").querySelectorAll("[data-changed]")) delete el.dataset.changed;
    setStatus($("formTitle").textContent + " applied");
    queueRefresh();
  }
  if (thenClose) closeForm();
}
$("formDialog").addEventListener("submit", (ev) => { ev.preventDefault(); sendForm(true); });
$("formApply").onclick = () => sendForm(false);
$("formCancel").onclick = closeForm;
$("formShade").addEventListener("mousedown", (ev) => { if (ev.target === $("formShade")) closeForm(); });
document.addEventListener("keydown", (ev) => { if (ev.key === "Escape" && openFormId) closeForm(); });
window.radiant.openForm = openForm;
''')

# --- GUI test -------------------------------------------------------------------------------------------------------
T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''            auto pageNow = web.handle("GET", "/", {}, true, {});
            auto jsNow = web.handle("GET", "/app.js", {}, true, {});
            r.check(pageNow.body.contains("id=\\"menubar\\"")''', '''            {
                // Plan 3.3: web forms. A dialog's values come as fields; only the changed ones go back, through the
                // dialog's own apply function, as one undo step.
                ch.forms = [&w] { return w.webFormList(); };
                ch.form = [&w](const QString& id) { return w.webForm(id); };
                ch.applyForm = [&w](const QString& id, const QJsonObject& values, QString* why) { return w.applyWebForm(id, values, why); };
                WebServer fweb(&ctx, ch);
                const auto key = [&](const char* k) {
                    const Entity* ws = ctx.doc.worldspawn();
                    const std::string* v = ws ? ws->value(k) : nullptr;
                    return v ? QString::fromStdString(*v) : QString("(none)");
                };
                const QJsonArray forms = QJsonDocument::fromJson(fweb.handle("GET", "/api/forms", {}, true, {}).body).object().value("forms").toArray();
                const QJsonObject sun = QJsonDocument::fromJson(fweb.handle("GET", "/api/form?id=sunSky", {}, true, {}).body).object();
                QStringList sunKeys;
                for (const QJsonValue& f : sun.value("fields").toArray()) sunKeys << f.toObject().value("key").toString() + ":" + f.toObject().value("type").toString();
                r.check(forms.size() == 4 && sun.value("title").toString() == "Sun & Sky" &&
                            sunKeys == QStringList({"pitch:number", "yaw:number", "color:color", "sky:text"}) &&
                            fweb.handle("GET", "/api/form?id=nothing", {}, true, {}).status == 404,
                        "GET /api/forms lists four forms and GET /api/form gives a form's fields with the map's values");
                const size_t undoAt = ctx.doc.undoPosition();
                const QString dirWas = key("sundirection"), colWas = key("suncolor");
                const auto ap = fweb.handle("POST", "/api/form", "{\\"id\\":\\"sunSky\\",\\"values\\":{\\"pitch\\":-33,\\"yaw\\":120}}", true, {});
                const bool sunSet = key("sundirection") == "-33 120 0" && ctx.doc.undoPosition() == undoAt + 1;
                ctx.doc.undo();
                ctx.documentEdited();
                r.check(ap.status == 200 && sunSet && key("sundirection") == dirWas && key("suncolor") == colWas,
                        "POST /api/form applies the changed fields as one undo step (Sun & Sky: pitch and yaw; the colour is not touched)");
                const auto badNum = fweb.handle("POST", "/api/form", "{\\"id\\":\\"sunSky\\",\\"values\\":{\\"pitch\\":\\"steep\\"}}", true, {});
                const auto badCol = fweb.handle("POST", "/api/form", "{\\"id\\":\\"sunSky\\",\\"values\\":{\\"color\\":\\"red\\"}}", true, {});
                const auto badForm = fweb.handle("POST", "/api/form", "{\\"id\\":\\"nothing\\",\\"values\\":{}}", true, {});
                r.check(badNum.status == 400 && badCol.status == 400 && badForm.status == 400 && key("sundirection") == dirWas,
                        "a value the form cannot take is refused with the reason and changes nothing");
                // Bake settings and the fog, through their forms.
                const size_t undoB = ctx.doc.undoPosition();
                fweb.handle("POST", "/api/form", "{\\"id\\":\\"bakeSettings\\",\\"values\\":{\\"bake\\":true,\\"texelSize\\":8}}", true, {});
                const bool bakeSet = key("iw8r_bake_lightmaps") == "1" && key("iw8r_lightmap_texel") == "8" && ctx.doc.undoPosition() == undoB + 1;
                ctx.doc.undo();
                ctx.documentEdited();
                const QString fogWas = key("iw8r_fog");
                fweb.handle("POST", "/api/form", "{\\"id\\":\\"atmosphere\\",\\"values\\":{\\"fogHalfway\\":2400,\\"fogColor\\":\\"#804020\\"}}", true, {});
                const QString fogNow = key("iw8r_fog");
                ctx.doc.undo();
                ctx.documentEdited();
                fweb.handle("POST", "/api/form", "{\\"id\\":\\"mapInfo\\",\\"values\\":{\\"iw8r_title\\":\\"Web Title\\",\\"iw8r_minimap_scale\\":\\"1.2\\"}}", true, {});
                const bool infoSet = key("iw8r_title") == "Web Title" && key("iw8r_minimap_scale") == "1.2";
                const auto infoBad = fweb.handle("POST", "/api/form", "{\\"id\\":\\"mapInfo\\",\\"values\\":{\\"iw8r_minimap_scale\\":\\"9\\"}}", true, {});
                ctx.doc.undo();
                ctx.documentEdited();
                r.check(bakeSet && fogNow.contains("2400") && fogNow != fogWas && key("iw8r_fog") == fogWas && infoSet && infoBad.status == 400 &&
                            key("iw8r_title") != "Web Title",
                        QString("the bake, atmosphere and map info forms write their keys and undo in one step (fog %1)").arg(fogNow));
                // In the browser's menus a dialog command with a form is offered, with the form's id.
                QJsonObject sunCmd, bakeCmd;
                for (const QJsonValue& v : w.webCommandList()) {
                    if (v.toObject().value("id").toString() == "MapSunSky") sunCmd = v.toObject();
                    if (v.toObject().value("id").toString() == "form:bakeSettings") bakeCmd = v.toObject();
                }
                QString whyNot;
                r.check(sunCmd.value("form").toString() == "sunSky" && sunCmd.value("blocked").toString().isEmpty() &&
                            bakeCmd.value("menu").toString() == "Compile" && bakeCmd.value("form").toString() == "bakeSettings" &&
                            !w.runWebCommand("MapSunSky", &whyNot) && whyNot.contains("web form"),
                        "a dialog command with a web form is offered to the browser as that form, never as the desktop dialog");
                auto formJs = web.handle("GET", "/app.js", {}, true, {});
                auto formPage = web.handle("GET", "/", {}, true, {});
                r.check(formJs.body.contains("/api/form") && formPage.body.contains("formDialog"), "the browser page has the form dialog");
            }
            auto pageNow = web.handle("GET", "/", {}, true, {});
            auto jsNow = web.handle("GET", "/app.js", {}, true, {});
            r.check(pageNow.body.contains("id=\\"menubar\\"")''')
print('ok')
