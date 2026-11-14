import io

# Plan 3.3, form 7: Preferences in the browser. The editing, build and F5 settings of the desktop dialog, through
# currentPreferences / applyPreferences, the build settings and DevmapRunner's play options. Left to the desktop:
# the asset folders and the F5 host command (paths and a command line on the editor's machine are not for a browser).

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


W = 'src/qtapp/main_window_web.cpp'
patch(W, '''    if (command == "CheckMap") return "checkMap";
''', '''    if (command == "CheckMap") return "checkMap";
    if (command == "Preferences") return "preferences";
''')
patch(W, '''                      QJsonObject{{"id", "checkMap"}, {"title", "Check Map"}, {"command", "CheckMap"}, {"menu", "Tools"}},''',
      '''                      QJsonObject{{"id", "checkMap"}, {"title", "Check Map"}, {"command", "CheckMap"}, {"menu", "Tools"}},
                      QJsonObject{{"id", "preferences"}, {"title", "Preferences"}, {"command", "Preferences"}, {"menu", "Edit"}},''')
patch(W, '''    if (id == "gameModes") return webGameModesForm();
''', '''    if (id == "gameModes") return webGameModesForm();
    if (id == "preferences") {
        const Preferences p = currentPreferences();
        const DevmapRunner::PlayOptions play = DevmapRunner::playOptions();
        const QString layout = Settings::instance().value("build/collisionLayout", "auto").toString();
        QJsonArray f{number("cameraSpeed", "Camera speed (units/s)", p.cameraSpeed, 50, 5000, 50),
                     number("cameraFov", "Camera field of view", p.cameraFov, 30, 150, 1),
                     number("autosaveMinutes", "Autosave every (minutes; 0 = off)", p.autosaveMinutes, 0, 60, 1),
                     boolean("toasts", "Toast notifications", p.toasts),
                     boolean("textureLock", "Texture lock", p.textureLock),
                     boolean("fogPreview", "Fog preview", p.fogPreview),
                     number("modelScale", "Custom model scale (OBJ units to map units)", p.modelScale, 0.001, 1000, 0.1),
                     boolean("realScriptables", "Build: carry real stock scripted objects (doors, cones, cars) into the map",
                             Settings::instance().value("build/realScriptables", true).toBool(),
                             "If a map with such an object does not load in the game, turn this off and build again"),
                     QJsonObject{{"key", "collisionLayout"}, {"label", "Build: collision"}, {"type", "choice"},
                                 {"value", layout == "one" || layout == "tiles" ? layout : QString("auto")},
                                 {"choices", QJsonArray{"auto", "one", "tiles"}},
                                 {"choiceLabels", QJsonArray{"Automatic: streamed tiles for a map wider than 20000 units", "Always one piece",
                                                             "Always streamed tiles"}},
                                 {"help", ""}},
                     text("playGametype", "F5: game mode", play.gametype, "A game type name: war, dm, dom, sd, ctf, koth, conf, br"),
                     number("playBots", "F5: bots", play.bots, 0, 17, 1),
                     boolean("playCover", "F5: cover the game window until the map loads", play.cover),
                     boolean("playStartRequest", "F5: let a client that can start the map by itself", play.startRequest)};
        return QJsonObject{{"id", id}, {"title", "Preferences"}, {"fields", f}};
    }
''')
patch(W, '''    if (id == "gameModes") return applyWebGameModes(values, why);
''', '''    if (id == "gameModes") return applyWebGameModes(values, why);
    if (id == "preferences") {
        Preferences p = currentPreferences();
        double autosave = p.autosaveMinutes;
        if (!numberIn(values, "cameraSpeed", 50, 5000, p.cameraSpeed, why) || !numberIn(values, "cameraFov", 30, 150, p.cameraFov, why) ||
            !numberIn(values, "autosaveMinutes", 0, 60, autosave, why) || !numberIn(values, "modelScale", 0.001, 1000, p.modelScale, why))
            return false;
        p.autosaveMinutes = int(std::lround(autosave));
        if (values.contains("toasts")) p.toasts = values.value("toasts").toBool();
        if (values.contains("textureLock")) p.textureLock = values.value("textureLock").toBool();
        if (values.contains("fogPreview")) p.fogPreview = values.value("fogPreview").toBool();
        DevmapRunner::PlayOptions play = DevmapRunner::playOptions();
        double bots = play.bots;
        if (!numberIn(values, "playBots", 0, 17, bots, why)) return false;
        if (values.contains("playGametype")) {
            const QString g = values.value("playGametype").toString().trimmed().toLower();
            bool ok = !g.isEmpty() && g.size() <= 11;
            for (const QChar c : g) ok = ok && ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_');
            if (!ok) return no("a game type name is 1 to 11 of a-z, 0-9 and _");
            play.gametype = g;
        }
        QString layout;
        if (values.contains("collisionLayout")) {
            layout = values.value("collisionLayout").toString();
            if (layout != "auto" && layout != "one" && layout != "tiles") return no("the collision layout is auto, one or tiles");
        }
        // Everything was accepted: now write.
        static const char* const editing[] = {"cameraSpeed", "cameraFov", "autosaveMinutes", "toasts", "textureLock", "fogPreview", "modelScale"};
        bool editingChanged = false;
        for (const char* k : editing) editingChanged = editingChanged || values.contains(k);
        if (editingChanged) applyPreferences(p);
        if (values.contains("realScriptables")) Settings::instance().setValue("build/realScriptables", values.value("realScriptables").toBool());
        if (!layout.isEmpty()) Settings::instance().setValue("build/collisionLayout", layout);
        play.bots = int(std::lround(bots));
        if (values.contains("playCover")) play.cover = values.value("playCover").toBool();
        if (values.contains("playStartRequest")) play.startRequest = values.value("playStartRequest").toBool();
        if (values.contains("playGametype") || values.contains("playBots") || values.contains("playCover") || values.contains("playStartRequest"))
            DevmapRunner::setPlayOptions(play);
        ctx_.setStatus("Preferences changed from the browser");
        return true;
    }
''')
s = read(W)
for inc in ('#include "devmap_launch.h"', '#include "settings.h"'):
    if inc not in s:
        s = s.replace('#include "compile_panel.h"', '#include "compile_panel.h"\n' + inc, 1)
write(W, s)

T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''                r.check(forms.size() == 6 && sun.value("title").toString() == "Sun & Sky" &&''',
      '''                r.check(forms.size() == 7 && sun.value("title").toString() == "Sun & Sky" &&''')
patch(T, '''                {
                    // Check Map: the dialog's findings as a report; "fix all" applies the automatic fixes in one step.''', '''                {
                    // Preferences: the editing, build and F5 settings; the host command and the folders stay at the editor.
                    const MainWindow::Preferences before = w.currentPreferences();
                    const QVariant layoutWas = Settings::instance().value("build/collisionLayout", "auto");
                    const QJsonObject pf = QJsonDocument::fromJson(fweb.handle("GET", "/api/form?id=preferences", {}, true, {}).body).object();
                    QStringList keys;
                    for (const QJsonValue& f : pf.value("fields").toArray()) keys << f.toObject().value("key").toString();
                    const auto setP = fweb.handle("POST", "/api/form", "{\\"id\\":\\"preferences\\",\\"values\\":{\\"cameraSpeed\\":900,\\"textureLock\\":true,\\"collisionLayout\\":\\"one\\"}}", true, {});
                    const MainWindow::Preferences now = w.currentPreferences();
                    const bool applied = setP.status == 200 && now.cameraSpeed == 900 && now.textureLock && now.cameraFov == before.cameraFov &&
                                         Settings::instance().value("build/collisionLayout").toString() == "one";
                    const auto badP = fweb.handle("POST", "/api/form", "{\\"id\\":\\"preferences\\",\\"values\\":{\\"playGametype\\":\\"war; quit\\"}}", true, {});
                    const auto badL = fweb.handle("POST", "/api/form", "{\\"id\\":\\"preferences\\",\\"values\\":{\\"cameraSpeed\\":1200,\\"collisionLayout\\":\\"sideways\\"}}", true, {});
                    const bool untouched = w.currentPreferences().cameraSpeed == 900;  // a refused form writes nothing
                    w.applyPreferences(before);
                    Settings::instance().setValue("build/collisionLayout", layoutWas);
                    r.check(keys.contains("cameraSpeed") && keys.contains("collisionLayout") && keys.contains("playBots") && !keys.contains("hostCommand") &&
                                !keys.join(' ').contains("Root") && applied && badP.status == 400 && badL.status == 400 && untouched,
                            "the Preferences form changes the editing, build and F5 settings; folders and the host command are not in it");
                }
                {
                    // Check Map: the dialog's findings as a report; "fix all" applies the automatic fixes in one step.''')
print('ok')
