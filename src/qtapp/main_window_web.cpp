// MainWindow: web forms (plan 3.3). The dialogs of the desktop window, as forms a browser can fill in.
//
// A form is a list of fields {key, label, type, value, ...}; type is "number" (min, max, step), "text" (choices: a
// list of suggestions), "choice" (choices), "color" ("#rrggbb") or "bool". The browser sends back only the fields the
// user changed; every other value stays exactly as the map has it. A form reads and writes through the same functions
// as its dialog (currentSunSky / applySunSky and so on), so both give the same map and the same single undo step.
#include "main_window.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

#include "atmosphere.h"
#include "atmosphere_dialog.h"
#include "bake_options.h"
#include "build_report.h"
#include "compile_panel.h"
#include "settings.h"
#include "devmap_launch.h"
#include "find_dialog.h"
#include "map_gametypes.h"
#include "layers.h"
#include "layers_panel.h"
#include "panels.h"
#include "sun_sky_dialog.h"

namespace iw8r::qt {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

QString hexOf(const Vec3& c) {
    const auto b = [](double v) { return int(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
    return QString("#%1%2%3").arg(b(c.x), 2, 16, QChar('0')).arg(b(c.y), 2, 16, QChar('0')).arg(b(c.z), 2, 16, QChar('0'));
}
bool colorOf(const QJsonValue& v, Vec3& out) {
    const QString s = v.toString().trimmed();
    if (s.size() != 7 || s[0] != '#') return false;
    bool ok = false;
    const uint rgb = s.mid(1).toUInt(&ok, 16);
    if (!ok) return false;
    out = Vec3{((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0};
    return true;
}

QJsonObject number(const char* key, const QString& label, double value, double lo, double hi, double step, const QString& help = {}) {
    return {{"key", key}, {"label", label}, {"type", "number"}, {"value", value}, {"min", lo}, {"max", hi}, {"step", step}, {"help", help}};
}
QJsonObject text(const char* key, const QString& label, const QString& value, const QString& help = {}, const QStringList& choices = {}) {
    QJsonObject o{{"key", key}, {"label", label}, {"type", "text"}, {"value", value}, {"help", help}};
    if (!choices.isEmpty()) o.insert("choices", QJsonArray::fromStringList(choices));
    return o;
}
QJsonObject choice(const char* key, const QString& label, const QString& value, const QStringList& choices, const QString& help = {}) {
    return {{"key", key}, {"label", label}, {"type", "choice"}, {"value", value}, {"choices", QJsonArray::fromStringList(choices)}, {"help", help}};
}
QJsonObject color(const char* key, const QString& label, const Vec3& value, const QString& help = {}) {
    return {{"key", key}, {"label", label}, {"type", "color"}, {"value", hexOf(value)}, {"help", help}};
}
QJsonObject boolean(const char* key, const QString& label, bool value, const QString& help = {}) {
    return {{"key", key}, {"label", label}, {"type", "bool"}, {"value", value}, {"help", help}};
}

// A number of the reply, kept inside the field's range; false when the value is not a number.
bool numberIn(const QJsonObject& values, const char* key, double lo, double hi, double& out, QString* why) {
    if (!values.contains(key)) return true;
    const QJsonValue v = values.value(key);
    bool ok = v.isDouble();
    double d = v.toDouble();
    if (v.isString()) d = v.toString().toDouble(&ok);
    if (!ok || !std::isfinite(d)) {
        if (why) *why = QString("%1 is not a number").arg(key);
        return false;
    }
    out = std::clamp(d, lo, hi);
    return true;
}

const char* const kMapInfoKeys[] = {"iw8r_title", "iw8r_description", "iw8r_compass_rotation", "iw8r_minimap_scale",
                                    "iw8r_ladder_rung_distance", "iw8r_exposure_adjust", "iw8r_gametypes"};

}  // namespace

QString MainWindow::webFormForCommand(const QString& command) {
    if (command == "MapSunSky") return "sunSky";
    if (command == "MapAtmosphere") return "atmosphere";
    if (command == "GameModes") return "gameModes";
    if (command == "CheckMap") return "checkMap";
    if (command == "Preferences") return "preferences";
    return {};
}

QJsonArray MainWindow::webFormList() const {
    return QJsonArray{QJsonObject{{"id", "sunSky"}, {"title", "Sun & Sky"}, {"command", "MapSunSky"}, {"menu", "Compile"}},
                      QJsonObject{{"id", "atmosphere"}, {"title", "Atmosphere (Fog / Film Grade)"}, {"command", "MapAtmosphere"}, {"menu", "Compile"}},
                      QJsonObject{{"id", "gameModes"}, {"title", "Game Modes"}, {"command", "GameModes"}, {"menu", "Tools"}},
                      QJsonObject{{"id", "checkMap"}, {"title", "Check Map"}, {"command", "CheckMap"}, {"menu", "Tools"}},
                      QJsonObject{{"id", "preferences"}, {"title", "Preferences"}, {"command", "Preferences"}, {"menu", "Edit"}},
                      QJsonObject{{"id", "history"}, {"title", "Undo History"}, {"command", ""}, {"menu", "Edit"}},
                      QJsonObject{{"id", "surface"}, {"title", "Surface (texture of the selection)"}, {"command", ""}, {"menu", "Edit"}},
                      QJsonObject{{"id", "layers"}, {"title", "Layers"}, {"command", ""}, {"menu", "View"}},
                      QJsonObject{{"id", "compile"}, {"title", "Compile Options and Build Budget"}, {"command", ""}, {"menu", "Compile"}},
                      QJsonObject{{"id", "bakeSettings"}, {"title", "Lightmap Bake Settings"}, {"command", ""}, {"menu", "Compile"}},
                      QJsonObject{{"id", "mapInfo"}, {"title", "Map Info"}, {"command", ""}, {"menu", "Compile"}}};
}

QJsonObject MainWindow::webForm(const QString& id) const {
    if (id == "gameModes") return webGameModesForm();
    if (id == "history") {
        // The document's steps, the newest 200 at most: choosing one undoes or redoes to it.
        const auto& h = ctx_.doc.history();
        const size_t pos = ctx_.doc.undoPosition(), first = h.size() > 200 ? h.size() - 200 : 0;
        QJsonArray steps, labels;
        for (size_t i = first; i <= h.size(); ++i) {
            steps.append(QString::number(i));
            labels.append(i == 0 ? QString("0: (opened document)") : QString("%1: %2%3").arg(i).arg(qs(h[i - 1].name), i > pos ? "  (undone)" : ""));
        }
        QJsonArray f{QJsonObject{{"key", "step"}, {"label", QString("Go to step (now at %1 of %2)").arg(pos).arg(h.size())}, {"type", "choice"},
                                 {"value", QString::number(std::max(pos, first))}, {"choices", steps}, {"choiceLabels", labels},
                                 {"help", "Steps after the chosen one are undone; they stay in the list until a new edit replaces them"}}};
        return QJsonObject{{"id", id}, {"title", "Undo History"}, {"fields", f}, {"reload", true}};
    }
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
            budget = QString("%1, built %2\n").arg(qs(rep.mapId), QFileInfo(file).lastModified().toString("yyyy-MM-dd HH:mm"));
            for (const BudgetGauge& g : budgetGauges(rep)) {
                const QString unit = g.unit.empty() ? QString() : " " + qs(g.unit);
                budget += QString("%1 %2%: %3%5 of %4%5%6\n")
                              .arg(g.fraction() >= 1.0 ? "OVER" : g.fraction() >= 0.75 ? "near" : "ok  ")
                              .arg(int(g.fraction() * 100), 3)
                              .arg(fmt(g.value), fmt(g.limit), unit, QString("  ") + qs(g.label) + (g.native ? " (native limit)" : " (guideline)"));
            }
            for (const std::string& warn : budgetWarnings(rep)) budget += "! " + qs(warn) + "\n";
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
    if (id == "layers") {
        QJsonArray f;
        QJsonArray names{""}, labels{"(nothing)"};
        QStringList plain;
        for (const LayersPanel::Row& row : layers_ ? layers_->rows() : std::vector<LayersPanel::Row>{}) {
            f.append(QJsonObject{{"key", "show:" + row.name}, {"label", QString("Show %1 (%2)").arg(row.name).arg(row.count)}, {"type", "bool"},
                                 {"value", row.visible}, {"help", ""}});
            names.append(row.name);
            labels.append(QString("%1 (%2)").arg(row.name).arg(row.count));
            plain << row.name;
        }
        f.append(QJsonObject{{"key", "select"}, {"label", "Select everything in"}, {"type", "choice"}, {"value", ""}, {"choices", names},
                             {"choiceLabels", labels}, {"help", ""}});
        f.append(text("moveTo", "Move the selection to layer", "", "The selected brushes and patches; a new name declares the layer. One undo step", plain));
        f.append(text("newLayer", "Declare a new layer", "", "A name without quotes"));
        return QJsonObject{{"id", id}, {"title", "Layers"}, {"fields", f}, {"reload", true}};
    }
    if (id == "surface") {
        const SurfacePanel* sp = surface_;
        const SurfacePanel::Current c = sp ? sp->current() : SurfacePanel::Current{};
        const auto report = [](const QString& t) { return QJsonObject{{"key", "selection"}, {"label", "Selection"}, {"type", "report"}, {"value", t}}; };
        QJsonArray f;
        if (!c.any) {
            f.append(report("Nothing to edit: select one or more brushes first (click them in a 2D view or in the live view)."));
        } else {
            f.append(report(QString("%1 %2; material %3.\nThe fields show the first face. A field you change goes to %4.")
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
    if (id == "checkMap") {
        // The dialog's own list, from a dialog that is never shown: the same checks in the same order.
        CheckMapDialog check(const_cast<EditorContext*>(&ctx_), nullptr);
        const QStringList lines = check.itemTexts();
        const QString head = lines.isEmpty() ? QString("No problems found")
                                             : QString("%1 finding(s), %2 with an automatic fix").arg(lines.size()).arg(check.fixableCount());
        QJsonArray f{QJsonObject{{"key", "findings"}, {"label", head}, {"type", "report"}, {"value", lines.join('\n')}}};
        if (check.fixableCount() > 0)
            f.append(boolean("fixAll", "Fix all findings marked [fix]", false, "One undo step: spawn angles, spawns onto the floor, dangling targets, broken primitives"));
        return QJsonObject{{"id", id}, {"title", "Check Map"}, {"fields", f}};
    }
    QJsonArray fields;
    QString title;
    const Entity* w = nullptr;
    for (const Entity& e : ctx_.doc.map().entities)
        if (e.isWorldspawn()) {
            w = &e;
            break;
        }
    if (id == "sunSky") {
        title = "Sun & Sky";
        const SunSky s = currentSunSky(ctx_);
        fields = {number("pitch", "Sun pitch (degrees; negative = above the horizon)", s.pitch, -90, 90, 1),
                  number("yaw", "Sun yaw (degrees)", s.yaw, -360, 360, 1), color("color", "Sun colour", s.color),
                  text("sky", "Sky (a stock sky name; empty = none)", s.sky, "The build converts the sky's image when the game files have it",
                       stockSkyList())};
    } else if (id == "atmosphere") {
        title = "Atmosphere (Fog / Film Grade)";
        const Atmosphere a = currentAtmosphere(ctx_);
        const ExpFog fog = a.fog.value_or(ExpFog{});
        const FilmGrade film = a.film.value_or(FilmGrade{});
        fields = {boolean("fog", "Fog", a.fog.has_value()),
                  number("fogStart", "Fog start (units)", fog.start, 0, 20000, 16),
                  number("fogHalfway", "Fog halfway distance (units)", fog.halfway, 1, 70000, 50, "Where the fog hides half of what is behind it"),
                  color("fogColor", "Fog colour", fog.color),
                  boolean("film", "Film grade", a.film.has_value()),
                  number("contrast", "Contrast", film.contrast, 0, 4, 0.05),
                  number("brightness", "Brightness", film.brightness, -1, 1, 0.02),
                  number("desaturation", "Desaturation", film.desaturation, 0, 1, 0.05),
                  boolean("invert", "Invert", film.invert),
                  color("lightTint", "Light tint", film.lightTint),
                  color("darkTint", "Dark tint", film.darkTint),
                  text("lut", "Colour grading LUT (.cube file; empty = none)", qs(a.lut)),
                  text("vision", "Vision overrides (empty = none)", qs(a.vision),
                       "name=value; name=x y z, with the game's vision field names (tonemapExposure, ...). The Vision Editor of the "
                       "editor lists all 218 fields")};
    } else if (id == "bakeSettings") {
        title = "Lightmap Bake Settings";
        const MapBakeOptions o = mapBakeOptions(w);
        fields = {boolean("bake", "Bake lightmaps", o.bake, "Off: the build has no lightmap and the game lights the map with a flat sky light"),
                  color("skyColor", "Sky colour", o.skyColor),
                  number("skyIntensity", "Sky strength", o.skyIntensity, 0, 16, 0.05, "The open sky's light as a part of the sun's (0.35 = a clear day)"),
                  number("texelSize", "Texel size (units)", o.texelSize, 1, 256, 1, "Smaller is sharper and slower"),
                  number("skyRays", "Sky rays per texel", o.skyRays, 1, 1024, 8, "More is smoother and slower")};
    } else if (id == "mapInfo") {
        title = "Map Info";
        const auto value = [&](const char* key) {
            const std::string* v = w ? w->value(key) : nullptr;
            return v ? qs(*v) : QString();
        };
        const QString compass = value("iw8r_compass_rotation");
        fields = {text("iw8r_title", "Title", value("iw8r_title")),
                  text("iw8r_description", "Description", value("iw8r_description")),
                  choice("iw8r_compass_rotation", "Compass rotation", compass.isEmpty() ? QString("auto") : compass, {"auto", "0", "90", "180", "270"}),
                  text("iw8r_minimap_scale", "Minimap scale (0.5 to 1.5; empty = default)", value("iw8r_minimap_scale")),
                  text("iw8r_ladder_rung_distance", "Ladder rung distance (8 to 32; empty = default)", value("iw8r_ladder_rung_distance")),
                  text("iw8r_exposure_adjust", "Exposure adjust (EV, -8 to 8; empty = default)", value("iw8r_exposure_adjust")),
                  text("iw8r_gametypes", "Game modes (empty = detected: " + qs(detectedGameTypes(ctx_.doc.map())) + ")", value("iw8r_gametypes"))};
    } else {
        return {};
    }
    return QJsonObject{{"id", id}, {"title", title}, {"fields", fields}};
}

bool MainWindow::applyWebForm(const QString& id, const QJsonObject& values, QString* why) {
    const auto no = [&](const QString& text) {
        if (why) *why = text;
        return false;
    };
    if (id == "gameModes") return applyWebGameModes(values, why);
    if (id == "history") {
        if (!values.contains("step")) return true;
        bool ok = false;
        const qlonglong want = values.value("step").toString().toLongLong(&ok);
        if (!ok || want < 0 || size_t(want) > ctx_.doc.history().size()) return no("there is no such step");
        size_t guard = ctx_.doc.history().size() + 1;
        while (ctx_.doc.undoPosition() > size_t(want) && guard-- > 0 && ctx_.doc.undo()) {}
        while (ctx_.doc.undoPosition() < size_t(want) && guard-- > 0 && ctx_.doc.redo()) {}
        ctx_.documentEdited(QString("History: step %1").arg(ctx_.doc.undoPosition()));
        return ctx_.doc.undoPosition() == size_t(want) ? true : no("the document could not go to that step");
    }
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
    if (id == "layers") {
        if (!layers_) return no("the layers are not available");
        const std::vector<LayersPanel::Row> rows = layers_->rows();
        const auto known = [&](const QString& n) {
            for (const LayersPanel::Row& r : rows)
                if (r.name == n) return true;
            return false;
        };
        std::vector<std::pair<QString, bool>> shows;
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (!it.key().startsWith("show:")) continue;
            const QString n = it.key().mid(5);
            if (!known(n)) return no("there is no layer " + n);
            shows.push_back({n, it.value().toBool()});
        }
        const QString select = values.value("select").toString();
        if (!select.isEmpty() && !known(select)) return no("there is no layer " + select);
        const QString moveTo = values.value("moveTo").toString().trimmed();
        const QString fresh = values.value("newLayer").toString().trimmed();
        const bool noLayerRow = !rows.empty() && moveTo == rows.front().name;
        if (!moveTo.isEmpty() && !noLayerRow && !isValidLayerName(moveTo.toStdString())) return no("a layer name has no quote and no line break");
        if (!moveTo.isEmpty() && ctx_.doc.selection().empty()) return no("select the brushes or patches to move first");
        if (!fresh.isEmpty() && !isValidLayerName(fresh.toStdString())) return no("a layer name has no quote and no line break");
        // Everything was accepted: now do it. The selection is moved before another layer is selected.
        for (const auto& [n, on] : shows) layers_->setLayerVisible(n, on);
        if (!fresh.isEmpty() && !known(fresh)) layers_->newLayer(fresh);
        if (!moveTo.isEmpty() && !layers_->moveSelectionToLayer(moveTo)) return no("nothing in the selection can be moved to a layer");
        if (!select.isEmpty()) {
            const size_t n = layers_->selectLayer(select);
            ctx_.documentEdited(QString("%1 primitive(s) of layer %2 selected").arg(n).arg(select));
        }
        return true;
    }
    if (id == "surface") {
        SurfacePanel* sp = surface_;
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
    if (id == "checkMap") {
        if (!values.value("fixAll").toBool()) return true;  // a report: nothing to write
        CheckMapDialog check(&ctx_, nullptr);
        const size_t fixed = check.fixAll();
        ctx_.setStatus(QString("Check Map: %1 finding(s) fixed from the browser").arg(fixed));
        return true;
    }
    Entity* w = ctx_.doc.worldspawn();
    if (!w) return no("the map has no worldspawn");
    if (id == "sunSky") {
        SunSky s = currentSunSky(ctx_);
        if (!numberIn(values, "pitch", -90, 90, s.pitch, why) || !numberIn(values, "yaw", -360, 360, s.yaw, why)) return false;
        if (values.contains("color") && !colorOf(values.value("color"), s.color)) return no("the sun colour is not #rrggbb");
        if (values.contains("sky")) {
            s.sky = values.value("sky").toString().trimmed();
            if (s.sky.contains(' ') || s.sky.contains('"')) return no("a sky name has no spaces or quotes");
        }
        applySunSky(ctx_, s);
        return true;
    }
    if (id == "atmosphere") {
        Atmosphere a = currentAtmosphere(ctx_);
        ExpFog fog = a.fog.value_or(ExpFog{});
        FilmGrade film = a.film.value_or(FilmGrade{});
        bool fogOn = a.fog.has_value(), filmOn = a.film.has_value();
        if (values.contains("fog")) fogOn = values.value("fog").toBool();
        if (values.contains("film")) filmOn = values.value("film").toBool();
        // A changed fog or film value turns its part on, as typing into the dialog's fields does.
        for (const char* k : {"fogStart", "fogHalfway", "fogColor"})
            if (values.contains(k) && !values.contains("fog")) fogOn = true;
        for (const char* k : {"contrast", "brightness", "desaturation", "invert", "lightTint", "darkTint"})
            if (values.contains(k) && !values.contains("film")) filmOn = true;
        if (!numberIn(values, "fogStart", 0, 20000, fog.start, why) || !numberIn(values, "fogHalfway", 1, 70000, fog.halfway, why) ||
            !numberIn(values, "contrast", 0, 4, film.contrast, why) || !numberIn(values, "brightness", -1, 1, film.brightness, why) ||
            !numberIn(values, "desaturation", 0, 1, film.desaturation, why))
            return false;
        if (values.contains("fogColor") && !colorOf(values.value("fogColor"), fog.color)) return no("the fog colour is not #rrggbb");
        if (values.contains("lightTint") && !colorOf(values.value("lightTint"), film.lightTint)) return no("the light tint is not #rrggbb");
        if (values.contains("darkTint") && !colorOf(values.value("darkTint"), film.darkTint)) return no("the dark tint is not #rrggbb");
        if (values.contains("invert")) film.invert = values.value("invert").toBool();
        if (values.contains("lut")) {
            const QString lut = values.value("lut").toString().trimmed();
            if (lut.contains('"')) return no("a file name has no quotes");
            a.lut = lut.toStdString();
        }
        if (values.contains("vision")) {
            const std::string vision = values.value("vision").toString().trimmed().toStdString();
            std::string verr;
            if (vision.find('"') != std::string::npos) return no("the vision overrides have no quotes");
            if (!vision.empty() && visionOverridesJson(vision, &verr).empty()) return no("vision overrides: " + qs(verr.empty() ? std::string("not name=value; name=value") : verr));
            a.vision = vision;
        }
        a.fog = fogOn ? std::optional<ExpFog>(fog) : std::nullopt;
        a.film = filmOn ? std::optional<FilmGrade>(film) : std::nullopt;
        applyAtmosphere(ctx_, a);
        return true;
    }
    if (id == "bakeSettings") {
        MapBakeOptions o = mapBakeOptions(w);
        double rays = o.skyRays;
        if (!numberIn(values, "skyIntensity", 0, 16, o.skyIntensity, why) || !numberIn(values, "texelSize", 1, 256, o.texelSize, why) ||
            !numberIn(values, "skyRays", 1, 1024, rays, why))
            return false;
        o.skyRays = int(std::lround(rays));
        if (values.contains("skyColor") && !colorOf(values.value("skyColor"), o.skyColor)) return no("the sky colour is not #rrggbb");
        const bool bakeWanted = values.contains("bake") ? values.value("bake").toBool() : o.bake;
        ctx_.doc.beginGroup("Bake settings");
        if (compile_) compile_->applyBakeSettings(o);
        if (bakeWanted != o.bake) {
            if (bakeWanted) ctx_.doc.setKeyValue(w->id, "iw8r_bake_lightmaps", "1");
            else ctx_.doc.removeKey(w->id, "iw8r_bake_lightmaps");
        }
        ctx_.doc.endGroup();
        ctx_.documentEdited(bakeWanted != o.bake ? (bakeWanted ? "The build bakes lightmaps" : "The build makes no lightmap") : QString());
        return true;
    }
    if (id == "mapInfo") {
        std::vector<std::pair<const char*, QString>> changes;
        for (const char* key : kMapInfoKeys) {
            if (!values.contains(key)) continue;
            QString v = values.value(key).isDouble() ? QString::number(values.value(key).toDouble(), 'g', 6) : values.value(key).toString().trimmed();
            if (v.contains('"') || v.contains('\n')) return no(QString("%1 has a quote or a line break").arg(key));
            const QString k = key;
            if (k == "iw8r_compass_rotation" && v == "auto") v.clear();
            const auto ranged = [&](double lo, double hi) {
                if (v.isEmpty()) return true;
                bool ok = false;
                const double d = v.toDouble(&ok);
                return ok && d >= lo && d <= hi;
            };
            if (k == "iw8r_compass_rotation" && !v.isEmpty() && v != "0" && v != "90" && v != "180" && v != "270") return no("the compass rotation is auto, 0, 90, 180 or 270");
            if (k == "iw8r_minimap_scale" && !ranged(0.5, 1.5)) return no("the minimap scale is 0.5 to 1.5, or empty");
            if (k == "iw8r_ladder_rung_distance" && !ranged(8, 32)) return no("the ladder rung distance is 8 to 32, or empty");
            if (k == "iw8r_exposure_adjust" && !ranged(-8, 8)) return no("the exposure adjust is -8 to 8, or empty");
            changes.push_back({key, v});
        }
        const Id wid = w->id;
        bool any = false;
        ctx_.doc.beginGroup("Map Info");
        for (const auto& [key, v] : changes) {
            const Entity* now = ctx_.doc.findEntity(wid);
            const std::string* cur = now ? now->value(key) : nullptr;
            if (v.isEmpty()) {
                if (cur) any = ctx_.doc.removeKey(wid, key) || any;
            } else if (!cur || qs(*cur) != v) {
                any = ctx_.doc.setKeyValue(wid, key, v.toStdString()) || any;
            }
        }
        ctx_.doc.endGroup();
        if (any) ctx_.documentEdited("Map info updated (written to map.json on the next build)");
        return true;
    }
    return no("there is no form " + id);
}

}  // namespace iw8r::qt
