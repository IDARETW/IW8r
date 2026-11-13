import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''    r.section("game lighting (View > Game Lighting)");
    if (CameraView* cv = w.cameraView()) {''', '''    r.section("game lighting options (View > Game Lighting Options, plan 1.10)");
    {
        // Each part of Game Lighting is a checkable command saved in the settings; toggling it twice restores it.
        const std::pair<const char*, const char*> toggles[] = {{"GameAo", "game/ao"},
                                                               {"GameSsr", "game/ssr"},
                                                               {"GameStockLights", "game/stockLights"},
                                                               {"GameLightShadows", "game/lightShadows"},
                                                               {"GamePropLods", "game/propLods"}};
        for (const auto& [id, key] : toggles) {
            QAction* a = w.action(id);
            r.check(a && a->isCheckable(), QString("%1 is a checkable Game Lighting option").arg(id));
            if (!a) continue;
            const bool was = a->isChecked();
            a->trigger();
            r.check(a->isChecked() != was && Settings::instance().value(key, true).toBool() == !was,
                    QString("%1 toggles and saves %2").arg(id, key));
            a->trigger();
            r.check(a->isChecked() == was && Settings::instance().value(key, true).toBool() == was,
                    QString("%1 back on its saved value").arg(id));
        }
        for (const char* id : {"GameAoStrength", "GameExposureOverride", "GameRebake"})
            r.check(w.action(id) != nullptr, QString("%1 is in View > Game Lighting Options").arg(id));
        if (CameraView* cv = w.cameraView()) {
            const double before = cv->gameExposureOverride();
            cv->setGameExposureOverride(12.0);
            r.check(cv->gameExposureOverride() == 12.0, "an exposure override is kept by the camera");
            cv->setGameExposureOverride(before);
        }
        QMenu* gm = w.findChild<QMenu*>("gameLightingMenu");
        r.check(gm && gm->findChildren<QMenu*>().size() >= 1, "the Game Lighting Options menu has its Debug View submenu");
    }

    r.section("game lighting (View > Game Lighting)");
    if (CameraView* cv = w.cameraView()) {''')
print('ok')
