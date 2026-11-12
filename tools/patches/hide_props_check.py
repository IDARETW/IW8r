import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/qtapp/main_window.h', [
 ('''    int showAllStockProps();  // clears the list; returns how many were hidden
''', '''    int showAllStockProps();  // clears the list; returns how many were hidden
    // GUI test seam: the reference props of the open stock map, as the background loader would hand them over.
    void setReferencePropsForTest(std::vector<StockModelInstance> props);
'''),
])
patch('src/qtapp/main_window_stock_edit.cpp', [
 ('''int MainWindow::showAllStockProps() {
''', '''void MainWindow::setReferencePropsForTest(std::vector<StockModelInstance> props) {
    referenceZone_ = stockZone_;
    refPropZone_ = stockZone_;
    refPropUse_ = stockModelUsage(props);
    refProps_ = std::make_shared<std::vector<StockModelInstance>>(std::move(props));
}

int MainWindow::showAllStockProps() {
'''),
])
patch('tests/stock_spawns_tests.cpp', [
 ('''TEST(stock_spawns_shipment_round_trip) {
''', '''// Brushes are not built into a stock map: the entity text never holds them (a helper brush for Hide Stock Props that
// was left in the document used to be written into it).
TEST(stock_spawns_split_leaves_brushes_out) {
    const std::string brush =
        "// brush 0\\n{\\n"
        " ( 16 16 0 ) ( -16 16 0 ) ( -16 -16 0 ) caulk 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\\n"
        " ( -16 -16 32 ) ( -16 16 32 ) ( 16 16 32 ) caulk 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\\n"
        " ( -16 -16 32 ) ( 16 -16 32 ) ( 16 -16 0 ) caulk 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\\n"
        " ( 16 16 32 ) ( -16 16 32 ) ( -16 16 0 ) caulk 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\\n"
        " ( 16 -16 32 ) ( 16 16 32 ) ( 16 16 0 ) caulk 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\\n"
        " ( -16 16 32 ) ( -16 -16 32 ) ( -16 -16 0 ) caulk 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\\n"
        "}\\n";
    const std::string text = "iwmap 4\\n// entity 0\\n{\\n\\"classname\\" \\"worldspawn\\"\\n\\"fog\\" \\"1\\"\\n" + brush + "}\\n"
                             "// entity 1\\n{\\n\\"classname\\" \\"func_group\\"\\n" + brush + "}\\n"
                             "// entity 2\\n{\\n\\"classname\\" \\"script_model\\"\\n\\"model\\" \\"crate\\"\\n\\"origin\\" \\"1 2 3\\"\\n}\\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    size_t prims = 0;
    for (const Entity& e : pr.map->entities)
        for (const EntityItem& it : e.items) prims += it.isPrimitive() ? 1 : 0;
    CHECK(prims == 2);
    std::string ents, err;
    std::vector<SpawnRecord> spawns;
    splitStockDocument(*pr.map, ents, spawns, &err);
    CHECK(ents.find("caulk") == std::string::npos && ents.find(" ( ") == std::string::npos && ents.find("func_group") == std::string::npos);
    CHECK(ents.find("worldspawn") != std::string::npos && ents.find("\\"fog\\" \\"1\\"") != std::string::npos && ents.find("crate") != std::string::npos);
    CHECK(err.find("2 brush(es)") != std::string::npos);
    // The text parses again as two entities without primitives.
    Id next2 = 1;
    ParseResult again = parseMap(ents, next2);
    CHECK(again.map.has_value() && again.map->entities.size() == 2);
}

TEST(stock_spawns_shipment_round_trip) {
'''),
])
patch('src/qtapp/qt_gui_test_loot.cpp', [
 ('''    for (int i = 0; i < 12 && ctx.doc.modified(); ++i) ctx.doc.undo();
    check(ctx.doc.text() == stockText, "Undo takes the placed object entities out again");
}
''', '''    for (int i = 0; i < 12 && ctx.doc.modified(); ++i) ctx.doc.undo();
    check(ctx.doc.text() == stockText, "Undo takes the placed object entities out again");

    // 11.5: stock props inside the selected helper brush are hidden; the brush itself never reaches the entity text.
    section("hide stock props in the selection (11.5)");
    {
        std::vector<StockModelInstance> props(4);
        const uint32_t rows[4] = {40, 3, 12, 7};
        const Vec3 at[4] = {Vec3{20, -20, 5}, Vec3{0, 0, 0}, Vec3{500, 0, 0}, Vec3{10, 10, 10}};
        for (int i = 0; i < 4; ++i) {
            props[size_t(i)].model = "test_prop";
            props[size_t(i)].index = rows[i];
            props[size_t(i)].origin = at[i];
        }
        w.setReferencePropsForTest(props);
        AABB box;
        box.add(Vec3{-32, -32, -32});
        box.add(Vec3{32, 32, 32});
        const auto prim = makeBrushPrimitive(makeBox(box, " caulk 128 128 0 0 0 0"), "\\n");
        check(prim.has_value(), "a helper brush is made");
        if (!prim) return;
        ctx.doc.addEntity({{"classname", "func_group"}}, {*prim}, "Helper brush");
        const int hidden = w.hideStockPropsInSelection();
        std::string list;
        for (const Entity& e : ctx.doc.map().entities)
            if (e.isWorldspawn())
                if (const std::string* v = e.value("iw8r_hide_smodels")) list = *v;
        check(hidden == 3 && list == "3 7 40", QString("File > Hide Stock Props In Selection hides the 3 props inside the brush (rows %1)").arg(QString::fromStdString(list)));
        check(w.hideStockPropsInSelection() == 0, "a second call finds nothing new");
        QString program, err;
        QStringList args, notes;
        const bool prepared = w.prepareStockEdit(work + "/hidden", &program, &args, &err, &notes);
        QFile jf(work + "/hidden/edits.json"), ef(work + "/hidden/edit.ents");
        const QByteArray json = jf.open(QIODevice::ReadOnly) ? jf.readAll() : QByteArray();
        const QByteArray ents = ef.open(QIODevice::ReadOnly) ? ef.readAll() : QByteArray();
        check(prepared && json.contains("\\"hide_static_models\\":[3,7,40]"), "the edits file lists the hidden rows: " + err);
        check(!ents.isEmpty() && !ents.contains("caulk") && !ents.contains("func_group"), "the helper brush left in the document is not written into the entity text");
        check(notes.join("; ").contains("1 brush(es)"), "the rebuild notes say the brush is left out");
        check(w.showAllStockProps() == 3, "File > Show All Stock Props brings them back");
        bool gone = true;
        for (const Entity& e : ctx.doc.map().entities)
            if (e.isWorldspawn() && e.value("iw8r_hide_smodels")) gone = false;
        check(gone, "the hidden list is cleared");
        w.setReferencePropsForTest({});
        for (int i = 0; i < 12 && ctx.doc.modified(); ++i) ctx.doc.undo();
        check(ctx.doc.text() == stockText, "Undo returns the document to the stock text");
    }
}
'''),
 ('#include "stock_placed.h"\n', '#include "brush_ops.h"\n#include "stock_placed.h"\n#include "stock_smodels.h"\n'),
])
print('ok')
