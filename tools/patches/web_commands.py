import io

# Plan 3.3, part 1: every editor command is listed (GET /api/commands) and runnable (POST /api/command) from the
# browser, and the web page gets a menu bar built from that list, with a command search.

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
patch(H, '''//   POST /api/material {name}        apply to the selected brushes / faces
''', '''//   POST /api/material {name}        apply to the selected brushes / faces
//   GET  /api/commands               every editor command in menu order: {id, label, menu, shortcut, checkable,
//                                    checked, enabled, blocked} ("blocked": why the browser cannot run it, "" = it
//                                    can) and {sep: true, menu} between groups
//   POST /api/command  {id}          runs a command as the menu would (400 with the reason when it cannot)
''')
patch(H, '''        std::function<QByteArray(const QString& kind, const QString& name)> thumbnail;  // JPEG bytes, empty = none yet
''', '''        std::function<QByteArray(const QString& kind, const QString& name)> thumbnail;  // JPEG bytes, empty = none yet
        // Every command of the editor's menus, in menu order (MainWindow::webCommandList).
        std::function<QJsonArray()> commands;
        // Runs one by id; false with *why when it is unknown, not available now, or not for the browser.
        std::function<bool(const QString& id, QString* why)> runCommand;
''')
patch(H, '''#include <QHash>
''', '''#include <QHash>
#include <QJsonArray>
''')
S = 'src/qtapp/web_server.cpp'
patch(S, '''        if (path == "/api/log") {''', '''        if (path == "/api/commands") {
            Response r;
            r.body = json({{"commands", hooks_.commands ? hooks_.commands() : QJsonArray()}});
            return r;
        }
        if (path == "/api/log") {''')
patch(S, '''    } else if (path == "/api/material") {''', '''    } else if (path == "/api/command") {
        const QString id = in.value("id").toString();
        QString why;
        if (!hooks_.runCommand || !hooks_.runCommand(id, &why)) return error(400, why.isEmpty() ? QString("the command cannot be run") : why);
    } else if (path == "/api/material") {''')

# --- main window ----------------------------------------------------------------------------------------------------
MH = 'src/qtapp/main_window.h'
patch(MH, '''    WebServer* webServer() const { return web_; }
''', '''    WebServer* webServer() const { return web_; }
    // The browser's menu bar (plan 3.3): every command of the menus in menu order, as the web API lists them, and
    // one run by id. A command is not run from the browser when it opens a dialog on the editor's screen (its label
    // ends in "...") or belongs to the person at the editor (exit, the web server itself, installs into the game).
    QJsonArray webCommandList() const;
    bool runWebCommand(const QString& id, QString* why);
    static QString webCommandBlocked(const QString& id, const QString& label);
''')
s = read(MH)
if '#include <QJsonArray>' not in s:
    i = s.index('#include <Q')
    s = s[:i] + '#include <QJsonArray>\n' + s[i:]
    write(MH, s)
M = 'src/qtapp/main_window.cpp'
patch(M, '''        web_ = new WebServer(&ctx_, hooks, this);
''', '''        hooks.commands = [this] { return webCommandList(); };
        hooks.runCommand = [this](const QString& id, QString* why) { return runWebCommand(id, why); };
        web_ = new WebServer(&ctx_, hooks, this);
''')
patch(M, '''bool MainWindow::applyWebServerSettings(QString* message) {''', '''QString MainWindow::webCommandBlocked(const QString& id, const QString& label) {
    static const QSet<QString> editorOnly{"Exit", "WebServer", "WebServerSettings", "InstallStockMap", "RestoreStockMap", "OpenInNewWindow"};
    if (editorOnly.contains(id)) return "only at the editor itself";
    if (label.endsWith("...")) return "opens a dialog on the editor's screen";
    return {};
}

QJsonArray MainWindow::webCommandList() const {
    QJsonArray out;
    const std::function<void(const QMenu*, const QString&)> walk = [&](const QMenu* menu, const QString& path) {
        bool separatorDue = false, any = false;
        for (QAction* a : menu->actions()) {
            if (a->isSeparator()) {
                separatorDue = any;
                continue;
            }
            if (const QMenu* sub = a->menu()) {
                QString title = sub->title();
                title.remove('&');
                walk(sub, path + "/" + title);
                continue;
            }
            const QString id = a->objectName();
            if (id.isEmpty() || actions_.value(id) != a || !a->isVisible()) continue;  // only addCommand's actions
            if (separatorDue) out.append(QJsonObject{{"sep", true}, {"menu", path}});
            separatorDue = false, any = true;
            QString label = a->text();
            label.remove('&');
            out.append(QJsonObject{{"id", id}, {"label", label}, {"menu", path}, {"shortcut", a->shortcut().toString(QKeySequence::NativeText)},
                                   {"checkable", a->isCheckable()}, {"checked", a->isChecked()}, {"enabled", a->isEnabled()},
                                   {"blocked", webCommandBlocked(id, label)}});
        }
    };
    for (QAction* top : menuBar()->actions())
        if (const QMenu* m = top->menu()) {
            QString title = m->title();
            title.remove('&');
            walk(m, title);
        }
    return out;
}

bool MainWindow::runWebCommand(const QString& id, QString* why) {
    QAction* a = actions_.value(id);
    const auto no = [&](const QString& text) {
        if (why) *why = text;
        return false;
    };
    if (!a) return no("there is no command " + id);
    QString label = a->text();
    label.remove('&');
    if (const QString blocked = webCommandBlocked(id, label); !blocked.isEmpty()) return no(label + ": " + blocked);
    if (!a->isEnabled()) return no(label + " is not available now");
    // After the reply has gone out: a command may take long, or show a message at the editor.
    QPointer<QAction> ptr(a);
    QTimer::singleShot(0, this, [ptr] {
        if (ptr && ptr->isEnabled()) ptr->trigger();
    });
    ctx_.setStatus(label + " (from the browser)");
    return true;
}

bool MainWindow::applyWebServerSettings(QString* message) {''')
s = read(M)
for inc in ('#include <QPointer>', '#include <QSet>', '#include <QJsonObject>'):
    if inc not in s:
        i = s.index('#include <Q')
        s = s[:i] + inc + '\n' + s[i:]
write(M, s)

# --- the page -------------------------------------------------------------------------------------------------------
patch('data/web/index.html', '''</header>
<main>''', '''</header>
<nav id="menubar"><input id="cmdSearch" placeholder="Find a command..." autocomplete="off"></nav>
<div id="menuPopup" hidden></div>
<main>''')
append('data/web/style.css', '''
/* The menu bar: every editor command (GET /api/commands). */
body { grid-template-rows: auto auto 1fr auto; }
#menubar { display: flex; align-items: center; gap: 2px; padding: 2px 8px; border-bottom: 1px solid var(--line); flex-wrap: wrap; }
#menubar button { border-color: transparent; background: none; padding: 2px 8px; }
#menubar button:hover, #menubar button.open { background: #30343c; border-color: var(--line); }
#cmdSearch { margin-left: auto; width: 200px; }
#menuPopup { position: fixed; z-index: 20; min-width: 260px; max-width: 460px; max-height: 70vh; overflow: auto; background: var(--panel);
  border: 1px solid var(--line); border-radius: 4px; box-shadow: 0 6px 18px rgba(0, 0, 0, .5); padding: 4px 0; }
#menuPopup .item { display: flex; gap: 12px; padding: 3px 12px 3px 8px; cursor: pointer; white-space: nowrap; }
#menuPopup .item:hover, #menuPopup .item.first { background: #30343c; }
#menuPopup .item.off { color: var(--muted); cursor: default; }
#menuPopup .item.off:hover { background: none; }
#menuPopup .tick { width: 14px; flex: none; color: var(--accent); }
#menuPopup .label { flex: 1; overflow: hidden; text-overflow: ellipsis; }
#menuPopup .key, #menuPopup .where { color: var(--muted); font-size: 11px; }
#menuPopup .caption { padding: 4px 8px 2px; color: var(--accent); font-size: 11px; text-transform: uppercase; letter-spacing: .04em; }
#menuPopup hr { border: 0; border-top: 1px solid var(--line); margin: 3px 0; }
''')
append('data/web/app.js', '''
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
  const tick = document.createElement("span"); tick.className = "tick"; tick.textContent = c.checked ? "\\u2713" : "";
  const label = document.createElement("span"); label.className = "label"; label.textContent = c.label;
  row.append(tick, label);
  if (withPath) { const w = document.createElement("span"); w.className = "where"; w.textContent = c.menu.replace(/\\//g, " > "); row.append(w); }
  if (c.shortcut) { const k = document.createElement("span"); k.className = "key"; k.textContent = c.shortcut; row.append(k); }
  if (!off) row.onclick = () => runCommand(c);
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
      if (sub !== top) { const cap = document.createElement("div"); cap.className = "caption"; cap.textContent = sub.slice(top.length + 1).replace(/\\//g, " > "); rows.push(cap); }
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
  if (first) { $("cmdSearch").value = ""; runCommand(first); }
});
document.addEventListener("click", (ev) => { if (!$("menuPopup").contains(ev.target) && ev.target !== $("cmdSearch")) closeMenu(); });
get("/api/commands").then((j) => { commands = j.commands || []; buildMenuBar(); }).catch(() => {});
''')

# --- GUI test -------------------------------------------------------------------------------------------------------
T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''        auto st = web.handle("GET", "/api/state", {}, true, {});
        r.check(st.status == 200 && QJsonDocument::fromJson(st.body).object().value("map").toString() == "test.map", "GET /api/state");
''', '''        auto st = web.handle("GET", "/api/state", {}, true, {});
        r.check(st.status == 200 && QJsonDocument::fromJson(st.body).object().value("map").toString() == "test.map", "GET /api/state");
        {
            // Plan 3.3: the browser's menu bar. Every command of the editor's menus is listed in menu order and can be
            // run by id; commands that open a dialog at the editor, and the editor's own (exit, installs), are refused.
            WebServer::Hooks ch;
            ch.mapName = [] { return QString("test.map"); };
            ch.commands = [&w] { return w.webCommandList(); };
            ch.runCommand = [&w](const QString& id, QString* why) { return w.runWebCommand(id, why); };
            WebServer cweb(&ctx, ch);
            const QJsonArray list = QJsonDocument::fromJson(cweb.handle("GET", "/api/commands", {}, true, {}).body).object().value("commands").toArray();
            int runnable = 0, blocked = 0, separators = 0;
            QSet<QString> tops, ids;
            QJsonObject gameLighting, exitCmd, shaderEditor;
            for (const QJsonValue& v : list) {
                const QJsonObject c = v.toObject();
                if (c.value("sep").toBool()) {
                    ++separators;
                    continue;
                }
                ids.insert(c.value("id").toString());
                tops.insert(c.value("menu").toString().section('/', 0, 0));
                (c.value("blocked").toString().isEmpty() ? runnable : blocked)++;
                if (c.value("id").toString() == "ToggleGameLighting") gameLighting = c;
                if (c.value("id").toString() == "Exit") exitCmd = c;
                if (c.value("id").toString() == "GameAoStrength") shaderEditor = c;
            }
            r.check(runnable > 150 && blocked > 20 && separators > 10 && tops.contains("File") && tops.contains("View") && tops.contains("Tools"),
                    QString("GET /api/commands lists the editor's menus: %1 runnable, %2 for the editor only, %3 menus")
                        .arg(runnable).arg(blocked).arg(tops.size()));
            r.check(gameLighting.value("menu").toString() == "View" && gameLighting.value("checkable").toBool() &&
                        gameLighting.value("blocked").toString().isEmpty() && !exitCmd.value("blocked").toString().isEmpty() &&
                        shaderEditor.value("menu").toString() == "View/Game Lighting Options" &&
                        shaderEditor.value("blocked").toString().contains("dialog"),
                    "a command carries its menu path, its tick and why the browser cannot run it");
            QAction* snap = w.action("ToggleGameLighting");
            const bool was = snap && snap->isChecked();
            const auto run = cweb.handle("POST", "/api/command", "{\\"id\\":\\"ToggleGameLighting\\"}", true, {});
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            const bool toggled = snap && snap->isChecked() != was;
            cweb.handle("POST", "/api/command", "{\\"id\\":\\"ToggleGameLighting\\"}", true, {});
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            r.check(run.status == 200 && toggled && snap && snap->isChecked() == was, "POST /api/command runs a command as its menu entry would");
            const auto refuse1 = cweb.handle("POST", "/api/command", "{\\"id\\":\\"Exit\\"}", true, {});
            const auto refuse2 = cweb.handle("POST", "/api/command", "{\\"id\\":\\"GameAoStrength\\"}", true, {});
            const auto refuse3 = cweb.handle("POST", "/api/command", "{\\"id\\":\\"NoSuchCommand\\"}", true, {});
            r.check(refuse1.status == 400 && refuse1.body.contains("only at the editor") && refuse2.status == 400 && refuse2.body.contains("dialog") &&
                        refuse3.status == 400 && w.isVisible(),
                    "the editor's own commands, dialogs and unknown ids are refused with the reason");
            auto pageNow = web.handle("GET", "/", {}, true, {});
            auto jsNow = web.handle("GET", "/app.js", {}, true, {});
            r.check(pageNow.body.contains("id=\\"menubar\\"") && pageNow.body.contains("cmdSearch") && jsNow.body.contains("/api/commands"),
                    "the browser page has the menu bar and the command search");
        }
''')
print('ok')
