import io

# The GUI test of the tools moved into the side bar panels and of the new IW8 Spawns, Undo History and Map Check panels.

ROOT = 'E:/IW8-Radiant/src/'


def slice_replace(p, start, end, new):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    if new in s:
        return
    assert s.count(start) == 1 and s.count(end) == 1, (p, s.count(start), s.count(end))
    a = s.index(start)
    b = s.index(end)
    assert a < b
    s = s[:a] + new + s[b:]
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


NEW = r'''    section("side bar: the tools in their panels");
    {
        // Owner 2026-11-15: no separate Tools side panel (the Tools menu stays in the menu bar); each panel's dots menu
        // holds the tools that belong to it.
        check(!w.findChild<QWidget*>("toolsPanel") && w.action("MeasureTool") && w.action("CheckMap"),
              "there is no Tools side panel, and the Tools menu still has its commands");
        for (AssetBrowser* b : {w.materialBrowser(), w.modelsBrowser(), w.effectsBrowser(), w.soundsBrowser(), w.imagesBrowser()}) {
            auto* tb = b ? b->findChild<QToolButton*>("assetTools") : nullptr;
            bool all = tb && tb->menu() && !b->toolActions().isEmpty();
            if (all)
                for (QAction* a : b->toolActions()) all &= tb->menu()->actions().contains(a);
            check(all, QString("an asset panel's dots menu holds its kind's tools (%1)").arg(b ? b->toolActions().size() : 0));
        }
        const auto hasTool = [](const QList<QAction*>& tools, const char* command) {
            for (QAction* a : tools)
                if (a->objectName() == command) return true;
            return false;
        };
        w.showDock("IW8 Spawns");
        QTest::qWait(30);
        auto* sp = w.findChild<SpawnsPanel*>();
        w.showDock("Undo History");
        QTest::qWait(30);
        auto* hp = w.findChild<HistoryPanel*>();
        w.showDock("Map Check");
        QTest::qWait(30);
        auto* mc = w.findChild<MapCheckPanel*>();
        check(sp && hasTool(sp->tools(), "GenerateSpawns") && hasTool(sp->tools(), "GameModes") && hp && hasTool(hp->tools(), "RecordMacro") &&
                  mc && hasTool(mc->tools(), "LeakCheck") && hasTool(mc->tools(), "CheckMap"),
              "IW8 Spawns has Generate Spawns and Game Modes, Undo History the macros, Map Check the map's checks");
    }

    section("side bar: IW8 Spawns");
    if (auto* sp = (w.showDock("IW8 Spawns"), QTest::qWait(30), w.findChild<SpawnsPanel*>())) {
        EditorContext& ctx = w.context();
        const size_t undo0 = ctx.doc.undoPosition();
        const int before = sp->spawnCount();
        ctx.doc.addEntity({{"classname", "mp_tdm_spawn_allies_start"}, {"origin", "100 200 0"}, {"angles", "0 90 0"}}, {}, "test spawn");
        ctx.doc.addEntity({{"classname", "mp_tdm_spawn"}, {"origin", "300 200 0"}}, {}, "test spawn");
        ctx.doc.addEntity({{"classname", "mp_dom_spawn"}, {"origin", "300 -200 0"}}, {}, "test spawn");
        ctx.documentEdited();
        QTest::qWait(30);
        sp->setMode("tdm");
        const int tdm = sp->visibleCount();
        sp->selectRow(0);
        QTest::qWait(30);
        const QString title = sp->stripTitle();
        const Vec3 cam = ctx.cam.pos;
        check(sp->spawnCount() == before + 3 && sp->modes().contains("tdm") && sp->modes().contains("dom") && tdm >= 2 &&
                  title.startsWith("mp_tdm_spawn") && sp->stripFacts().contains("TDM") && sp->countText().contains(" of "),
              QString("IW8 Spawns lists the map's spawns by mode (%1 spawns, modes %2; TDM %3; strip \"%4\": %5)")
                  .arg(sp->spawnCount()).arg(sp->modes().join(" ")).arg(tdm).arg(title, sp->stripFacts()));
        check(ctx.doc.selection().size() == 1 && (cam.x != ctx.cam.pos.x || cam.y != ctx.cam.pos.y || cam.z != ctx.cam.pos.z),
              "a spawn picked in the list is selected and the views go to it");
        const Id added = sp->addHere();
        QTest::qWait(30);
        const Entity* e = ctx.doc.findEntity(added);
        check(e && e->classname() == title.toStdString() && sp->spawnCount() == before + 4,
              "Add one here adds another spawn of the picked kind at the camera");
        sp->setMode("");
        while (ctx.doc.undoPosition() > undo0) ctx.doc.undo();
        ctx.documentEdited();
        QTest::qWait(30);
        check(sp->spawnCount() == before, "the spawns list follows undo");
    }

    section("side bar: Undo History");
    if (auto* hp = (w.showDock("Undo History"), QTest::qWait(30), w.findChild<HistoryPanel*>())) {
        EditorContext& ctx = w.context();
        const size_t undo0 = ctx.doc.undoPosition();
        ctx.doc.addEntity({{"classname", "info_null"}, {"origin", "0 0 64"}}, {}, "Place info_null");
        const Id world = ctx.doc.map().entities.front().id;
        ctx.doc.setKeyValue(world, "iw8r_history_test", "1");
        ctx.documentEdited();
        QTest::qWait(30);
        const QString placed = hp->summaryOf(undo0 + 1), keyed = hp->summaryOf(undo0 + 2);
        hp->setFilter("info_null");
        const int found = hp->visibleSteps();
        hp->setFilter("");
        check(placed.contains("1 entity added") && keyed.contains("key") && found == 2 && !hp->stripTitle().isEmpty() &&
                  hp->countText().contains("step"),
              QString("Undo History says what each step touched (\"%1\", \"%2\"), searches the steps (%3) and shows the current one (\"%4\", %5)")
                  .arg(placed, keyed).arg(found).arg(hp->stripTitle(), hp->countText()));
        while (ctx.doc.undoPosition() > undo0) ctx.doc.undo();
        ctx.documentEdited();
    }

    section("side bar: Map Check");
    if (auto* mc = (w.showDock("Map Check"), QTest::qWait(30), w.findChild<MapCheckPanel*>())) {
        mc->recheck();
        const int total = mc->findingCount();
        int build = 0;
        for (const QString& l : mc->lines()) build += MapCheckPanel::kindOf(l) == "build";
        mc->setChip("build");
        const int shown = mc->visibleCount();
        mc->selectRow(0);
        for (int r = 0; r < total && !mc->stripTitle().startsWith("Build"); ++r) mc->selectRow(r);
        const QString strip = mc->stripTitle();
        mc->setChip("all");
        check(total > 0 && shown == build && strip.startsWith("Build") && !mc->countText().isEmpty() &&
                  MapCheckPanel::severityOf("Lights (warning): x") == "warning" && MapCheckPanel::kindOf("Spawns (warning): x") == "gameplay",
              QString("Map Check lists the map's findings by kind (%1 findings, %2 build notes shown; strip \"%3\"; count %4)")
                  .arg(total).arg(shown).arg(strip, mc->countText()));
    }

    section("side bar: flush asset panels");'''

slice_replace('qtapp/qt_gui_test_sidebar.cpp', '    section("side bar: Tools panel");\n', '    section("side bar: flush asset panels");', NEW)

s = io.open(ROOT + 'qtapp/qt_gui_test_sidebar.cpp', encoding='utf-8', newline='').read()
assert 'w.showDock("Tools")' in s
s = s.replace('w.showDock("Tools");', 'w.showDock("Map Check");')
io.open(ROOT + 'qtapp/qt_gui_test_sidebar.cpp', 'w', encoding='utf-8', newline='').write(s)
print('ok')
