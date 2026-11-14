import io

# Plan 3.3, form 2: Game Modes in the browser. Pick a preset (the built-in ones and your own) and what to do with it:
# carry it in the map, use it for F5, or insert the Warzone start entities. A form may ask the page to send every
# field ("sendAll"), which an action form needs.

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


MH = 'src/qtapp/main_window.h'
patch(MH, '''    static QString webFormForCommand(const QString& command);  // the form that stands for a dialog command, or ""
''', '''    static QString webFormForCommand(const QString& command);  // the form that stands for a dialog command, or ""
    // The Game Modes form (main_window_game_modes.cpp): the presets and what to do with the chosen one.
    QJsonObject webGameModesForm() const;
    bool applyWebGameModes(const QJsonObject& values, QString* why);
''')
G = 'src/qtapp/main_window_game_modes.cpp'
patch(G, '''void MainWindow::showGameModes() {''', '''// The Game Modes dialog as a web form (plan 3.3): a preset and an action.
QJsonObject MainWindow::webGameModesForm() const {
    const std::vector<GameModePreset> modes = loadGameModes(userModesDir().toStdString(), nullptr);
    const QString f5 = DevmapRunner::modePresetSetting();
    QJsonArray ids, labels;
    QString current;
    for (const GameModePreset& m : modes) {
        ids.append(qs(m.id));
        labels.append(qs(m.title) + (m.builtin ? "" : "  (mine)") + (qs(m.id) == f5 ? "  [F5]" : "") + "  -  " + qs(m.gametype) +
                      (m.dvars.empty() ? QString() : QString(", %1 dvar(s)").arg(m.dvars.size())));
        if (qs(m.id) == f5) current = f5;
    }
    if (current.isEmpty() && !modes.empty()) current = qs(modes.front().id);
    const QJsonArray actions{"carry", "f5", "warzoneStart"};
    const QJsonArray actionLabels{"Carry in this map (the map's script sets the mode at every start)", "Use for F5 (sent when the editor starts the match)",
                                  "Insert the Warzone start entities into the map"};
    return QJsonObject{{"id", "gameModes"}, {"title", "Game Modes"}, {"sendAll", true},
                       {"fields", QJsonArray{QJsonObject{{"key", "preset"}, {"label", "Mode"}, {"type", "choice"}, {"value", current}, {"choices", ids},
                                                         {"choiceLabels", labels}, {"help", "A mode is a game type script plus dvars. Your own presets are made in the editor's Game Modes dialog"}},
                                             QJsonObject{{"key", "action"}, {"label", "Do"}, {"type", "choice"}, {"value", "carry"}, {"choices", actions},
                                                         {"choiceLabels", actionLabels}, {"help", "Not seen in game"}}}}};
}

bool MainWindow::applyWebGameModes(const QJsonObject& values, QString* why) {
    const auto no = [&](const QString& text) {
        if (why) *why = text;
        return false;
    };
    const QString action = values.value("action").toString();
    if (action == "warzoneStart") {
        const int n = insertWarzoneStart();
        return n >= 0 ? true : no("the Warzone start entities could not be inserted");
    }
    const QString id = values.value("preset").toString();
    for (const GameModePreset& m : loadGameModes(userModesDir().toStdString(), nullptr)) {
        if (qs(m.id) != id) continue;
        if (action == "f5") {
            DevmapRunner::setModePreset(qs(m.id), qs(m.gametype), qs(modeDvarsText(m.dvars)));
            ctx_.setStatus(QString("F5 starts '%1' (game type %2, %3 dvar(s) sent before the start)").arg(qs(m.title), qs(m.gametype)).arg(m.dvars.size()));
            return true;
        }
        if (action == "carry") return bakeGameMode(m) ? true : no("the map has no worldspawn");
        return no("the action is carry, f5 or warzoneStart");
    }
    return no("there is no game mode " + id);
}

void MainWindow::showGameModes() {''')
s = read(G)
for inc in ('#include <QJsonArray>', '#include <QJsonObject>'):
    if inc not in s:
        i = s.index('#include <Q')
        s = s[:i] + inc + '\n' + s[i:]
write(G, s)

W = 'src/qtapp/main_window_web.cpp'
patch(W, '''    if (command == "MapAtmosphere") return "atmosphere";
''', '''    if (command == "MapAtmosphere") return "atmosphere";
    if (command == "GameModes") return "gameModes";
''')
patch(W, '''                      QJsonObject{{"id", "bakeSettings"}, {"title", "Lightmap Bake Settings"}, {"command", ""}, {"menu", "Compile"}},''',
      '''                      QJsonObject{{"id", "gameModes"}, {"title", "Game Modes"}, {"command", "GameModes"}, {"menu", "Tools"}},
                      QJsonObject{{"id", "bakeSettings"}, {"title", "Lightmap Bake Settings"}, {"command", ""}, {"menu", "Compile"}},''')
patch(W, '''QJsonObject MainWindow::webForm(const QString& id) const {
''', '''QJsonObject MainWindow::webForm(const QString& id) const {
    if (id == "gameModes") return webGameModesForm();
''')
patch(W, '''    Entity* w = ctx_.doc.worldspawn();
    if (!w) return no("the map has no worldspawn");
    if (id == "sunSky") {''', '''    if (id == "gameModes") return applyWebGameModes(values, why);
    Entity* w = ctx_.doc.worldspawn();
    if (!w) return no("the map has no worldspawn");
    if (id == "sunSky") {''')

# The page: choice labels, and forms that send every field.
J = 'data/web/app.js'
patch(J, '''    for (const c of f.choices || []) { const o = document.createElement("option"); o.value = o.textContent = c; el.append(o); }
    el.value = f.value;''', '''    (f.choices || []).forEach((c, k) => {
      const o = document.createElement("option");
      o.value = c; o.textContent = (f.choiceLabels && f.choiceLabels[k]) || c;
      el.append(o);
    });
    el.value = f.value;''')
patch(J, '''  openFormId = id;
  $("formTitle").textContent = form.title;''', '''  openFormId = id;
  $("formDialog").dataset.sendAll = form.sendAll ? "1" : "";
  $("formTitle").textContent = form.title;''')
patch(J, '''    if (!el.dataset.changed) continue;
    values[el.dataset.key]''', '''    if (!el.dataset.changed && !$("formDialog").dataset.sendAll) continue;
    values[el.dataset.key]''')

T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''                r.check(forms.size() == 4 && sun.value("title").toString() == "Sun & Sky" &&''',
      '''                r.check(forms.size() == 5 && sun.value("title").toString() == "Sun & Sky" &&''')
patch(T, '''                        "GET /api/forms lists four forms and GET /api/form gives a form's fields with the map's values");''',
      '''                        "GET /api/forms lists the forms and GET /api/form gives a form's fields with the map's values");
                {
                    // Game Modes: a preset carried in the map through the form, as the dialog's "Carry in this map".
                    const QJsonObject gm = QJsonDocument::fromJson(fweb.handle("GET", "/api/form?id=gameModes", {}, true, {}).body).object();
                    const QJsonArray gf = gm.value("fields").toArray();
                    const QJsonArray presets = gf.isEmpty() ? QJsonArray() : gf.at(0).toObject().value("choices").toArray();
                    const std::string textBefore = ctx.doc.text();
                    const size_t undoG = ctx.doc.undoPosition();
                    // A preset with dvars (a Warzone one): carrying it writes them into the map.
                    const QJsonArray presetLabels = gf.isEmpty() ? QJsonArray() : gf.at(0).toObject().value("choiceLabels").toArray();
                    QString pick;
                    for (int k = 0; k < presets.size() && k < presetLabels.size() && pick.isEmpty(); ++k)
                        if (presetLabels.at(k).toString().contains("dvar(s)")) pick = presets.at(k).toString();
                    const auto carry = fweb.handle("POST", "/api/form", QString("{\\"id\\":\\"gameModes\\",\\"values\\":{\\"preset\\":\\"%1\\",\\"action\\":\\"carry\\"}}").arg(pick).toUtf8(), true, {});
                    const bool carried = ctx.doc.text() != textBefore && ctx.doc.undoPosition() > undoG;
                    while (ctx.doc.undoPosition() > undoG) ctx.doc.undo();
                    ctx.documentEdited();
                    const auto noMode = fweb.handle("POST", "/api/form", "{\\"id\\":\\"gameModes\\",\\"values\\":{\\"preset\\":\\"no_such_mode\\",\\"action\\":\\"carry\\"}}", true, {});
                    r.check(gm.value("sendAll").toBool() && presets.size() >= 3 && gf.size() == 2 && carry.status == 200 && carried &&
                                ctx.doc.text() == textBefore && noMode.status == 400,
                            QString("the Game Modes form lists %1 presets and carries the chosen one in the map").arg(presets.size()));
                }''')
print('ok')
