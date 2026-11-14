import io

# Plan 3.3, form 5: the compile options and the Build Budget in the browser. The Compile dock's map id and its three
# switches (stock textures, bake lightmaps, occlusion bake) through the dock's own widgets, and the last build's
# figures against their limits as a report.

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


patch('src/qtapp/compile_panel.h', '''    QString mapId() const;      // validated IW8 map id (mp_<name>, lower case)''',
      '''    QString mapId() const;      // validated IW8 map id (mp_<name>, lower case)
    // Sets the map id as if it was typed. False (and nothing changes) when the text is not a map id as it stands.
    // `typed` false leaves the id following the map's file name again (as before anybody typed one).
    bool setMapId(const QString& id, bool typed = true);
    bool mapIdTyped() const { return userEditedId_; }''')
patch('src/qtapp/compile_panel.cpp', '''QString CompilePanel::mapId() const { return sanitizeMapId(mapId_->text()); }''',
      '''QString CompilePanel::mapId() const { return sanitizeMapId(mapId_->text()); }

bool CompilePanel::setMapId(const QString& id, bool typed) {
    const QString want = id.trimmed();
    if (want.isEmpty() || sanitizeMapId(want) != want) return false;
    mapId_->setText(want);
    userEditedId_ = typed;
    return true;
}''')

W = 'src/qtapp/main_window_web.cpp'
patch(W, '''                      QJsonObject{{"id", "bakeSettings"}, {"title", "Lightmap Bake Settings"}, {"command", ""}, {"menu", "Compile"}},''',
      '''                      QJsonObject{{"id", "compile"}, {"title", "Compile Options and Build Budget"}, {"command", ""}, {"menu", "Compile"}},
                      QJsonObject{{"id", "bakeSettings"}, {"title", "Lightmap Bake Settings"}, {"command", ""}, {"menu", "Compile"}},''')
patch(W, '''    if (id == "gameModes") return webGameModesForm();
''', '''    if (id == "gameModes") return webGameModesForm();
    if (id == "compile") {
        if (!compile_) return {};
        const auto checked = [this](const char* name) {
            const QCheckBox* box = compile_->findChild<QCheckBox*>(name);
            return box && box->isChecked();
        };
        QString budget;
        const QString file = compile_->buildRoot() + "/last_build_report.json";
        BuildReport rep;
        std::string err;
        if (!QFileInfo::exists(file) || !loadBuildReport(file.toStdString(), rep, &err)) {
            budget = "No build of this map yet: compile it and the figures appear here.";
        } else {
            const auto fmt = [](double v) { return v >= 100 || v == double(int64_t(v)) ? QString::number(v, 'f', 0) : QString::number(v, 'f', 1); };
            budget = QString("%1, built %2\\n").arg(qs(rep.mapId), QFileInfo(file).lastModified().toString("yyyy-MM-dd HH:mm"));
            for (const BudgetGauge& g : budgetGauges(rep)) {
                const QString unit = g.unit.empty() ? QString() : " " + qs(g.unit);
                budget += QString("%1 %2%: %3%5 of %4%5%6\\n")
                              .arg(g.fraction() >= 1.0 ? "OVER" : g.fraction() >= 0.75 ? "near" : "ok  ")
                              .arg(int(g.fraction() * 100), 3)
                              .arg(fmt(g.value), fmt(g.limit), unit, QString("  ") + qs(g.label) + (g.native ? " (native limit)" : " (guideline)"));
            }
            for (const std::string& warn : budgetWarnings(rep)) budget += "! " + qs(warn) + "\\n";
        }
        QJsonArray f{text("mapId", "Map id (mp_<name>, lower case, at most 15 characters)", compile_->mapId(),
                          "The name of the built zones and of the map in the game"),
                     boolean("stockTextures", "Stock textures", checked("compileStockTextures"),
                             "Stock IW8 materials are built with their colour textures instead of grey"),
                     boolean("bakeLightmaps", "Bake lightmaps", checked("compileBakeLightmaps"),
                             "The build bakes the world's lightmap (sky light, bounce, where the sun reaches). The detail is in Lightmap Bake Settings"),
                     boolean("occlusionBake", "Occlusion bake", checked("compileOcclusionBake"),
                             "The build makes occlusion data from the map's opaque box brushes, so the game skips what is behind walls"),
                     QJsonObject{{"key", "budget"}, {"label", "Build Budget (the last build against its limits)"}, {"type", "report"}, {"value", budget}}};
        return QJsonObject{{"id", id}, {"title", "Compile Options and Build Budget"}, {"fields", f}, {"reload", true}};
    }
''')
patch(W, '''    if (id == "gameModes") return applyWebGameModes(values, why);
''', '''    if (id == "gameModes") return applyWebGameModes(values, why);
    if (id == "compile") {
        if (!compile_) return no("the compile panel is not available");
        if (compile_->busy()) return no("a build is running: change the options when it is done");
        if (values.contains("mapId")) {
            const QString want = values.value("mapId").toString().trimmed();
            if (CompilePanel::sanitizeMapId(want) != want || want.isEmpty()) return no("a map id is mp_<name>: lower case a-z, 0-9 and _, at most 15 characters");
        }
        // Everything was accepted: now write. The switches go through the dock's own boxes (the two bakes are worldspawn
        // keys, each an undo step of its own).
        if (values.contains("mapId")) compile_->setMapId(values.value("mapId").toString());
        static const std::pair<const char*, const char*> boxes[] = {{"stockTextures", "compileStockTextures"},
                                                                    {"bakeLightmaps", "compileBakeLightmaps"},
                                                                    {"occlusionBake", "compileOcclusionBake"}};
        for (const auto& [key, name] : boxes)
            if (values.contains(key))
                if (QCheckBox* box = compile_->findChild<QCheckBox*>(name)) box->setChecked(values.value(key).toBool());
        return true;
    }
''')
s = read(W)
for inc in ('#include <QCheckBox>', '#include <QFileInfo>'):
    if inc not in s:
        s = s.replace('#include <QJsonArray>', inc + '\n#include <QJsonArray>', 1)
if '#include "build_report.h"' not in s:
    s = s.replace('#include "bake_options.h"', '#include "bake_options.h"\n#include "build_report.h"', 1)
write(W, s)

T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''                r.check(forms.size() == 9 && sun.value("title").toString() == "Sun & Sky" &&''',
      '''                r.check(forms.size() == 10 && sun.value("title").toString() == "Sun & Sky" &&''')
patch(T, '''                {
                    // Layers: show / hide, select, move the selection, declare. Hidden layers leave the scene (and the web map).''', '''                {
                    // Compile options: the dock's map id and switches, and the Build Budget as a report.
                    const QString idWas = w.compilePanel()->mapId();
                    const bool typedWas = w.compilePanel()->mapIdTyped();
                    const size_t undoC = ctx.doc.undoPosition();
                    const QJsonObject cf = QJsonDocument::fromJson(fweb.handle("GET", "/api/form?id=compile", {}, true, {}).body).object();
                    QStringList ckeys;
                    QString budgetText;
                    for (const QJsonValue& f : cf.value("fields").toArray()) {
                        ckeys << f.toObject().value("key").toString();
                        if (f.toObject().value("key").toString() == "budget") budgetText = f.toObject().value("value").toString();
                    }
                    const auto setC = fweb.handle("POST", "/api/form", "{\\"id\\":\\"compile\\",\\"values\\":{\\"mapId\\":\\"mp_webform\\",\\"occlusionBake\\":true}}", true, {});
                    const bool idSet = w.compilePanel()->mapId() == "mp_webform";
                    const bool occOn = key("iw8r_umbra_bake") == "1";
                    const auto* occBox = w.compilePanel()->findChild<QCheckBox*>("compileOcclusionBake");
                    const bool boxOn = occBox && occBox->isChecked();
                    const auto badId = fweb.handle("POST", "/api/form", "{\\"id\\":\\"compile\\",\\"values\\":{\\"mapId\\":\\"Bad Id!\\",\\"occlusionBake\\":false}}", true, {});
                    const bool kept = key("iw8r_umbra_bake") == "1" && w.compilePanel()->mapId() == "mp_webform";
                    while (ctx.doc.undoPosition() > undoC) ctx.doc.undo();
                    ctx.documentEdited();
                    w.compilePanel()->setMapId(idWas, typedWas);
                    r.check(ckeys.contains("mapId") && ckeys.contains("stockTextures") && ckeys.contains("bakeLightmaps") && ckeys.contains("occlusionBake") &&
                                !budgetText.isEmpty() && setC.status == 200 && idSet && occOn && boxOn && badId.status == 400 && kept &&
                                key("iw8r_umbra_bake") == "(none)" && w.compilePanel()->mapId() == idWas,
                            QString("the Compile form sets the map id and the build switches and shows the Build Budget (%1)").arg(budgetText.section('\\n', 0, 0)));
                }
                {
                    // Layers: show / hide, select, move the selection, declare. Hidden layers leave the scene (and the web map).''')
print('ok')
