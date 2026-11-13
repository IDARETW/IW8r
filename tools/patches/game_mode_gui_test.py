import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''    r.section("game lighting (View > Game Lighting)");''', '''    r.section("game mode view (View > Game Mode, research 40)");
    {
        // A domination flag and a bomb site: each mode shows only its own; All Modes shows both again.
        const Id flag = ctx.doc.addEntity({{"classname", "trigger_radius"}, {"origin", "0 0 0"}, {"script_gameobjectname", "dom onslaught"},
                                           {"radius", "120"}, {"height", "128"}}, {}, "t");
        const Id site = ctx.doc.addEntity({{"classname", "script_model"}, {"origin", "64 0 0"}, {"script_gameobjectname", "bombzone"},
                                           {"model", "tm/com_pallet_2"}}, {}, "t");
        ctx.documentEdited();
        QAction* dom = w.findChild<QAction*>("gameMode_dom");
        QAction* sd = w.findChild<QAction*>("gameMode_sd");
        QAction* all = w.findChild<QAction*>("gameMode_all");
        r.check(dom && sd && all && w.findChild<QAction*>("gameMode_war"), "View > Game Mode lists the modes and All Modes");
        if (dom && sd && all) {
            dom->trigger();
            r.check(!ctx.scene.hiddenByMode.count(flag) && ctx.scene.hiddenByMode.count(site), "Domination: the flag shows, the bomb site is hidden");
            r.check(Settings::instance().value("view/gameMode").toString() == "dom", "the mode is saved");
            sd->trigger();
            r.check(ctx.scene.hiddenByMode.count(flag) && !ctx.scene.hiddenByMode.count(site), "Search and Destroy: the bomb site shows, the flag is hidden");
            bool drawn = false;
            for (const auto* g : ctx.scene.items()) drawn |= g->ref.entity == flag;
            r.check(!drawn, "a hidden mode's entity is not drawn");
            all->trigger();
            r.check(ctx.scene.hiddenByMode.empty(), "All Modes shows every mode's objects");
        }
        ctx.doc.undo();
        ctx.doc.undo();
        ctx.documentEdited();
    }

    r.section("game lighting (View > Game Lighting)");''')
print('ok')
