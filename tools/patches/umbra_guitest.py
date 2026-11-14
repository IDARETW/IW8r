import io

# GUI test section "umbra visibility" (plan M2.2): the menu toggles, their saved settings, and the state without a
# stock map reference.


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b, 1)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('E:/IW8-Radiant/src/qtapp/qt_gui_test.cpp', '''    r.section("shader editor");''', '''    r.section("umbra visibility");
    {
        QAction* cull = w.action("UmbraCullByTome");
        QAction* cells = w.action("UmbraShowCells");
        r.check(cull && cells && cull->isCheckable() && cells->isCheckable(), "View > Umbra Visibility has Cull by Tome and Show Cells and Portals");
        if (cull && cells) {
            const bool wasCull = w.umbraCull(), wasCells = w.umbraOverlay();
            if (!cull->isChecked()) cull->trigger();
            r.check(w.umbraCull() && iw8r::qt::Settings::instance().value("view/umbraCull").toBool(), "Cull by Tome turns on and is saved");
            if (!cells->isChecked()) cells->trigger();
            r.check(w.umbraOverlay() && iw8r::qt::Settings::instance().value("view/umbraOverlay").toBool(), "Show Cells and Portals turns on and is saved");
            // Without a stock map reference there is nothing to cull and nothing is drawn.
            r.check(!w.cameraView()->tomeCull() && ctx.scene.tomeLines.empty(), "without a stock map reference nothing is culled or drawn");
            w.setUmbraView(false, false);
            r.check(!cull->isChecked() && !cells->isChecked() && !iw8r::qt::Settings::instance().value("view/umbraCull").toBool(),
                    "turning both off unchecks the menu and is saved");
            w.setUmbraView(wasCull, wasCells);
        }
    }

    r.section("shader editor");''')
print('ok')
