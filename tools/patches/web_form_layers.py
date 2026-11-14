import io

# Plan 3.4 (layers in the page): the Layers dock as a web form. Show or hide a layer, select a layer's primitives,
# move the selection to a layer, declare a layer. Hidden layers leave the page's views with the editor's (the web map
# is built from the scene's visible items), and View > Region runs from the web menu already.

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


patch('src/qtapp/layers_panel.h', '''    bool newLayer(const QString& name);
''', '''    bool newLayer(const QString& name);
    // The dock's rows: "(no layer)" first, then every declared or used layer.
    struct Row {
        QString name;
        size_t count = 0;  // primitives in the layer
        bool visible = true;
    };
    std::vector<Row> rows() const;
''')
s = read('src/qtapp/layers_panel.h')
if '#include <vector>' not in s:
    s = s.replace('#include <', '#include <vector>\n#include <', 1)
    write('src/qtapp/layers_panel.h', s)
patch('src/qtapp/layers_panel.cpp', '''void LayersPanel::refresh() {''', '''std::vector<LayersPanel::Row> LayersPanel::rows() const {
    std::vector<Row> out;
    for (int r = 0; r < table_->rowCount(); ++r) {
        const QTableWidgetItem* name = table_->item(r, 0);
        const QTableWidgetItem* count = table_->item(r, 1);
        if (!name) continue;
        out.push_back({name->text(), count ? size_t(count->data(Qt::DisplayRole).toULongLong()) : 0, name->checkState() == Qt::Checked});
    }
    return out;
}

void LayersPanel::refresh() {''')

W = 'src/qtapp/main_window_web.cpp'
patch(W, '''                      QJsonObject{{"id", "surface"}, {"title", "Surface (texture of the selection)"}, {"command", ""}, {"menu", "Edit"}},''',
      '''                      QJsonObject{{"id", "surface"}, {"title", "Surface (texture of the selection)"}, {"command", ""}, {"menu", "Edit"}},
                      QJsonObject{{"id", "layers"}, {"title", "Layers"}, {"command", ""}, {"menu", "View"}},''')
patch(W, '''    if (id == "gameModes") return webGameModesForm();
''', '''    if (id == "gameModes") return webGameModesForm();
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
''')
patch(W, '''    if (id == "gameModes") return applyWebGameModes(values, why);
''', '''    if (id == "gameModes") return applyWebGameModes(values, why);
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
''')
s = read(W)
for inc in ('#include "layers_panel.h"', '#include "layers.h"'):
    if inc not in s:
        s = s.replace('#include "map_gametypes.h"', '#include "map_gametypes.h"\n' + inc, 1)
write(W, s)

T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''                r.check(forms.size() == 8 && sun.value("title").toString() == "Sun & Sky" &&''',
      '''                r.check(forms.size() == 9 && sun.value("title").toString() == "Sun & Sky" &&''')
patch(T, '''                {
                    // Surface: the Surface panel's fields on the selection. Nothing selected = nothing to edit.''', '''                {
                    // Layers: show / hide, select, move the selection, declare. Hidden layers leave the scene (and the web map).
                    ctx.doc.deselectAll();
                    ctx.documentEdited();
                    const size_t undoL = ctx.doc.undoPosition();
                    const QJsonObject lf = QJsonDocument::fromJson(fweb.handle("GET", "/api/form?id=layers", {}, true, {}).body).object();
                    QStringList lkeys;
                    for (const QJsonValue& f : lf.value("fields").toArray()) lkeys << f.toObject().value("key").toString();
                    const std::vector<LayersPanel::Row> rows0 = w.layersPanel()->rows();
                    QString busy;  // a layer that holds primitives
                    for (const LayersPanel::Row& row : rows0)
                        if (busy.isEmpty() && row.count > 0) busy = row.name;
                    ctx.scene.sync(ctx.doc);
                    const size_t shown0 = ctx.scene.items().size();
                    const auto hide = fweb.handle("POST", "/api/form", QString("{\\"id\\":\\"layers\\",\\"values\\":{\\"show:%1\\":false}}").arg(busy).toUtf8(), true, {});
                    ctx.scene.sync(ctx.doc);
                    const size_t shown1 = ctx.scene.items().size();
                    bool listedHidden = false;
                    for (const LayersPanel::Row& row : w.layersPanel()->rows()) listedHidden = listedHidden || (row.name == busy && !row.visible);
                    fweb.handle("POST", "/api/form", QString("{\\"id\\":\\"layers\\",\\"values\\":{\\"show:%1\\":true}}").arg(busy).toUtf8(), true, {});
                    ctx.scene.sync(ctx.doc);
                    const size_t shown2 = ctx.scene.items().size();
                    const auto sel = fweb.handle("POST", "/api/form", QString("{\\"id\\":\\"layers\\",\\"values\\":{\\"select\\":\\"%1\\"}}").arg(busy).toUtf8(), true, {});
                    const size_t selected = ctx.doc.selection().size();
                    const auto mv = fweb.handle("POST", "/api/form", "{\\"id\\":\\"layers\\",\\"values\\":{\\"moveTo\\":\\"web_layer\\"}}", true, {});
                    bool moved = false;
                    for (const LayersPanel::Row& row : w.layersPanel()->rows()) moved = moved || (row.name == "web_layer" && row.count == selected);
                    const bool oneStep = ctx.doc.undoPosition() == undoL + 1;
                    const auto badShow = fweb.handle("POST", "/api/form", "{\\"id\\":\\"layers\\",\\"values\\":{\\"show:no such layer\\":false}}", true, {});
                    const auto badName = fweb.handle("POST", "/api/form", "{\\"id\\":\\"layers\\",\\"values\\":{\\"newLayer\\":\\"a\\\\\\"b\\"}}", true, {});
                    while (ctx.doc.undoPosition() > undoL) ctx.doc.undo();
                    ctx.doc.deselectAll();
                    ctx.documentEdited();
                    bool gone = true;
                    for (const LayersPanel::Row& row : w.layersPanel()->rows()) gone = gone && row.name != "web_layer";
                    r.check(lkeys.contains("show:" + busy) && lkeys.contains("select") && lkeys.contains("moveTo") && !busy.isEmpty() && hide.status == 200 &&
                                shown1 < shown0 && listedHidden && shown2 == shown0 && sel.status == 200 && selected > 0 && mv.status == 200 && moved && oneStep &&
                                badShow.status == 400 && badName.status == 400 && gone,
                            QString("the Layers form hides a layer (%1 of %2 items left), selects it (%3) and moves the selection to a new layer in one undo step [%4%5%6%7%8%9]")
                                .arg(shown1).arg(shown0).arg(selected).arg(listedHidden).arg(shown2 == shown0).arg(moved).arg(oneStep).arg(badShow.status == 400 && badName.status == 400).arg(gone));
                }
                {
                    // Surface: the Surface panel's fields on the selection. Nothing selected = nothing to edit.''')
print('ok')

# A layer hidden through setLayerVisible (the web form, the GUI test) also unticks its row in the dock, and rows()
# reports what is hidden, not what the row shows.
patch('src/qtapp/layers_panel.cpp', '''        out.push_back({name->text(), count ? size_t(count->data(Qt::DisplayRole).toULongLong()) : 0, name->checkState() == Qt::Checked});''',
      '''        out.push_back({name->text(), count ? size_t(count->data(Qt::DisplayRole).toULongLong()) : 0, hiddenLayers_.count(name->text().toStdString()) == 0});''')
patch('src/qtapp/layers_panel.cpp', '''    else hiddenLayers_.insert(layer.toStdString());
    applyVisibility();''', '''    else hiddenLayers_.insert(layer.toStdString());
    const bool was = filling_;
    filling_ = true;  // the row follows; its change signal must not come back here
    for (int r = 0; r < table_->rowCount(); ++r)
        if (QTableWidgetItem* it = table_->item(r, 0); it && it->text() == layer) it->setCheckState(visible ? Qt::Checked : Qt::Unchecked);
    filling_ = was;
    applyVisibility();''')
print('ok 2')
