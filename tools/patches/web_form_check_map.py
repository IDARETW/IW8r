import io

# Plan 3.3, form 6: Check Map in the browser. The findings of the editor's Check Map dialog as a report, and "Fix
# all" for the ones with an automatic fix (one undo step). A form field of type "report" is read-only text.

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


W = 'src/qtapp/main_window_web.cpp'
patch(W, '''    if (command == "GameModes") return "gameModes";
''', '''    if (command == "GameModes") return "gameModes";
    if (command == "CheckMap") return "checkMap";
''')
patch(W, '''                      QJsonObject{{"id", "gameModes"}, {"title", "Game Modes"}, {"command", "GameModes"}, {"menu", "Tools"}},''',
      '''                      QJsonObject{{"id", "gameModes"}, {"title", "Game Modes"}, {"command", "GameModes"}, {"menu", "Tools"}},
                      QJsonObject{{"id", "checkMap"}, {"title", "Check Map"}, {"command", "CheckMap"}, {"menu", "Tools"}},''')
patch(W, '''    if (id == "gameModes") return webGameModesForm();
''', '''    if (id == "gameModes") return webGameModesForm();
    if (id == "checkMap") {
        // The dialog's own list, from a dialog that is never shown: the same checks in the same order.
        CheckMapDialog check(const_cast<EditorContext*>(&ctx_), nullptr);
        const QStringList lines = check.itemTexts();
        const QString head = lines.isEmpty() ? QString("No problems found")
                                             : QString("%1 finding(s), %2 with an automatic fix").arg(lines.size()).arg(check.fixableCount());
        QJsonArray f{QJsonObject{{"key", "findings"}, {"label", head}, {"type", "report"}, {"value", lines.join('\\n')}}};
        if (check.fixableCount() > 0)
            f.append(boolean("fixAll", "Fix all findings marked [fix]", false, "One undo step: spawn angles, spawns onto the floor, dangling targets, broken primitives"));
        return QJsonObject{{"id", id}, {"title", "Check Map"}, {"fields", f}};
    }
''')
patch(W, '''    if (id == "gameModes") return applyWebGameModes(values, why);
''', '''    if (id == "gameModes") return applyWebGameModes(values, why);
    if (id == "checkMap") {
        if (!values.value("fixAll").toBool()) return true;  // a report: nothing to write
        CheckMapDialog check(&ctx_, nullptr);
        const size_t fixed = check.fixAll();
        ctx_.setStatus(QString("Check Map: %1 finding(s) fixed from the browser").arg(fixed));
        return true;
    }
''')
patch(W, '''#include "compile_panel.h"
''', '''#include "compile_panel.h"
#include "find_dialog.h"
''')

J = 'data/web/app.js'
patch(J, '''function fieldInput(f) {
  let el;
  if (f.type === "choice") {''', '''function fieldInput(f) {
  let el;
  if (f.type === "report") {            // read-only text: a list of findings
    el = document.createElement("pre");
    el.className = "report";
    el.textContent = f.value || "";
    return el;
  }
  if (f.type === "choice") {''')
patch(J, '''    const row = document.createElement("label");
    const name = document.createElement("span"); name.textContent = f.label;''', '''    const row = document.createElement("label");
    if (f.type === "report") row.className = "wide";
    const name = document.createElement("span"); name.textContent = f.label;''')
append('data/web/style.css', '''
#formFields label.wide { grid-template-columns: 1fr; }
#formFields pre.report { margin: 0; max-height: 46vh; overflow: auto; white-space: pre-wrap; font: 11px/1.4 ui-monospace, monospace;
  background: #1a1c20; border: 1px solid var(--line); border-radius: 4px; padding: 6px 8px; }
''')

T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''                r.check(forms.size() == 5 && sun.value("title").toString() == "Sun & Sky" &&''',
      '''                r.check(forms.size() == 6 && sun.value("title").toString() == "Sun & Sky" &&''')
patch(T, '''                {
                    // Game Modes: a preset carried in the map through the form, as the dialog's "Carry in this map".''', '''                {
                    // Check Map: the dialog's findings as a report; "fix all" applies the automatic fixes in one step.
                    const Id bad = ctx.doc.addEntity({{"classname", "mp_dm_spawn"}, {"origin", "0 0 4000"}, {"angles", "12 400 3"}}, {}, "web check");
                    ctx.documentEdited();
                    const QJsonObject cm = QJsonDocument::fromJson(fweb.handle("GET", "/api/form?id=checkMap", {}, true, {}).body).object();
                    const QJsonArray cf = cm.value("fields").toArray();
                    const QJsonObject report = cf.isEmpty() ? QJsonObject() : cf.at(0).toObject();
                    CheckMapDialog same(&ctx);
                    const bool matches = report.value("value").toString() == same.itemTexts().join('\\n') && report.value("type").toString() == "report";
                    const size_t fixable = same.fixableCount();
                    const size_t undoC = ctx.doc.undoPosition();
                    const auto fix = fweb.handle("POST", "/api/form", "{\\"id\\":\\"checkMap\\",\\"values\\":{\\"fixAll\\":true}}", true, {});
                    CheckMapDialog after(&ctx);
                    const bool fixedNow = fixable > 0 ? (after.fixableCount() < fixable && ctx.doc.undoPosition() == undoC + 1) : ctx.doc.undoPosition() == undoC;
                    while (ctx.doc.undoPosition() > undoC) ctx.doc.undo();
                    ctx.doc.undo();  // the test spawn
                    ctx.documentEdited();
                    r.check(matches && cf.size() == (fixable > 0 ? 2 : 1) && fix.status == 200 && fixedNow && !ctx.doc.findEntity(bad),
                            QString("the Check Map form reports the dialog's findings (%1, %2 fixable) and fixes them in one undo step")
                                .arg(report.value("label").toString()).arg(fixable));
                }
                {
                    // Game Modes: a preset carried in the map through the form, as the dialog's "Carry in this map".''')
print('ok')
