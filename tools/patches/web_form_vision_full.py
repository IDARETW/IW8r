import io

# The Vision Editor as a web form: every settable vision field (scalar and vector) by name, grouped as in the editor's
# dialog. A field holds the map's override ("" = the stock value); the form writes worldspawn iw8r_vision in one undo
# step, each value checked by the field's own rule.

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
patch(W, '''                      QJsonObject{{"id", "compile"}, {"title", "Compile Options and Build Budget"}, {"command", ""}, {"menu", "Compile"}},''',
      '''                      QJsonObject{{"id", "vision"}, {"title", "Vision Editor (every field)"}, {"command", ""}, {"menu", "Compile"}},
                      QJsonObject{{"id", "compile"}, {"title", "Compile Options and Build Budget"}, {"command", ""}, {"menu", "Compile"}},''')
patch(W, '''    if (id == "gameModes") return webGameModesForm();
''', '''    if (id == "gameModes") return webGameModesForm();
    if (id == "vision") {
        // Every vision field a number can set, group by group; the value is the map's override, empty = stock.
        std::string cur;
        for (const Entity& e : ctx_.doc.map().entities)
            if (e.classname() == "worldspawn") {
                if (const std::string* v = e.value(kVisionKey)) cur = *v;
                break;
            }
        std::vector<std::pair<std::string, std::string>> pairs;
        std::string perr;
        parseVisionOverrides(cur, pairs, &perr);
        const auto overrideOf = [&](const char* name) {
            for (const auto& [n, v] : pairs)
                if (QString::fromStdString(n).compare(name, Qt::CaseInsensitive) == 0) return qs(v);
            return QString();
        };
        static const char* const groups[] = {"tonemap", "exposure", "bloom", "grade", "film", "fog", "sky", "sun", "dof", "misc"};
        QJsonArray f;
        for (const char* group : groups)
            for (const VisionField& vf : visionFields()) {
                const QString type = vf.type;
                if (QString(vf.group) != group || (type != "scalar" && type != "vector")) continue;
                const QString def = vf.defaultText;
                f.append(QJsonObject{{"key", QString("v:") + vf.name},
                                     {"label", QString("%1: %2%3").arg(group, vf.name, type == "vector" ? " (x y z)" : "")},
                                     {"type", "text"},
                                     {"value", overrideOf(vf.name)},
                                     {"help", def.isEmpty() ? QString() : "stock: " + def}});
            }
        return QJsonObject{{"id", id}, {"title", "Vision Editor"}, {"fields", f}, {"reload", true}};
    }
''')
patch(W, '''    if (id == "gameModes") return applyWebGameModes(values, why);
''', '''    if (id == "gameModes") return applyWebGameModes(values, why);
    if (id == "vision") {
        Entity* ws = ctx_.doc.worldspawn();
        if (!ws) return no("the map has no worldspawn");
        const Id wid = ws->id;
        const std::string* curText = ws->value(kVisionKey);
        std::vector<std::pair<std::string, std::string>> pairs;
        std::string perr;
        if (curText && !parseVisionOverrides(*curText, pairs, &perr)) return no("the map's vision overrides cannot be read: " + qs(perr));
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (!it.key().startsWith("v:")) continue;
            const std::string name = it.key().mid(2).toStdString();
            const VisionField* vf = findVisionField(name);
            if (!vf) return no("there is no vision field " + it.key().mid(2));
            const std::string value = it.value().toString().trimmed().toStdString();
            if (value.find_first_of("\\";=") != std::string::npos) return no(qs(name) + ": a value is a number, or three numbers");
            if (!value.empty())
                if (const std::string bad = checkVisionValue(*vf, value); !bad.empty()) return no(qs(name) + ": " + qs(bad));
            // Replace, add or (empty) take out this field's override; the others keep their order.
            bool found = false;
            for (auto p = pairs.begin(); p != pairs.end();) {
                if (QString::fromStdString(p->first).compare(vf->name, Qt::CaseInsensitive) != 0) {
                    ++p;
                    continue;
                }
                if (value.empty() || found) {
                    p = pairs.erase(p);
                } else {
                    p->second = value;
                    ++p;
                }
                found = true;
            }
            if (!found && !value.empty()) pairs.push_back({vf->name, value});
        }
        const std::string out = formatVisionOverrides(pairs);
        std::string verr;
        if (!out.empty() && visionOverridesJson(out, &verr).empty()) return no("vision overrides: " + qs(verr));
        bool changed = false;
        if (out.empty()) changed = curText && ctx_.doc.removeKey(wid, kVisionKey);
        else if (!curText || *curText != out) changed = ctx_.doc.setKeyValue(wid, kVisionKey, out);
        if (changed) ctx_.documentEdited(QString("Vision: %1 field(s) overridden").arg(pairs.size()));
        return true;
    }
''')
s = read(W)
if '#include "vision_fields.h"' not in s:
    s = s.replace('#include "sun_sky_dialog.h"', '#include "sun_sky_dialog.h"\n#include "vision_fields.h"', 1)
    assert '#include "vision_fields.h"' in s
write(W, s)

T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''                r.check(forms.size() == 11 && sun.value("title").toString() == "Sun & Sky" &&''',
      '''                r.check(forms.size() == 12 && sun.value("title").toString() == "Sun & Sky" &&''')
patch(T, '''                {
                    // Compile options: the dock's map id and switches, and the Build Budget as a report.''', '''                {
                    // The Vision Editor as a form: every settable field by name; a value is checked by the field's rule.
                    const QString visWas = key("iw8r_vision");
                    const size_t undoV = ctx.doc.undoPosition();
                    const QJsonObject vf = QJsonDocument::fromJson(fweb.handle("GET", "/api/form?id=vision", {}, true, {}).body).object();
                    const QJsonArray vfields = vf.value("fields").toArray();
                    bool hasExposure = false;
                    for (const QJsonValue& f : vfields) hasExposure = hasExposure || f.toObject().value("key").toString() == "v:tonemapExposure";
                    const auto setV = fweb.handle("POST", "/api/form", "{\\"id\\":\\"vision\\",\\"values\\":{\\"v:tonemapExposure\\":\\"0.75\\"}}", true, {});
                    const QString one = key("iw8r_vision");
                    const QJsonObject vf2 = QJsonDocument::fromJson(fweb.handle("GET", "/api/form?id=vision", {}, true, {}).body).object();
                    QString shown;
                    for (const QJsonValue& f : vf2.value("fields").toArray())
                        if (f.toObject().value("key").toString() == "v:tonemapExposure") shown = f.toObject().value("value").toString();
                    const auto badV = fweb.handle("POST", "/api/form", "{\\"id\\":\\"vision\\",\\"values\\":{\\"v:tonemapExposure\\":\\"bright\\"}}", true, {});
                    const auto noField = fweb.handle("POST", "/api/form", "{\\"id\\":\\"vision\\",\\"values\\":{\\"v:noSuchVisionField\\":\\"1\\"}}", true, {});
                    const bool kept = key("iw8r_vision") == one;
                    const auto clearV = fweb.handle("POST", "/api/form", "{\\"id\\":\\"vision\\",\\"values\\":{\\"v:tonemapExposure\\":\\"\\"}}", true, {});
                    const QString cleared = key("iw8r_vision");
                    while (ctx.doc.undoPosition() > undoV) ctx.doc.undo();
                    ctx.documentEdited();
                    r.check(vfields.size() > 200 && hasExposure && setV.status == 200 && one.contains("tonemapExposure=0.75") && shown == "0.75" &&
                                badV.status == 400 && noField.status == 400 && kept && clearV.status == 200 && !cleared.contains("tonemapExposure") &&
                                key("iw8r_vision") == visWas,
                            QString("the Vision form lists every settable vision field (%1) and sets, refuses and clears an override").arg(vfields.size()));
                }
                {
                    // Compile options: the dock's map id and switches, and the Build Budget as a report.''')
print('ok')
