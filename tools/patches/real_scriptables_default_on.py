import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/qtapp/prefab_browser.cpp', [
 ('// Edit > Preferences > Real stock scripted objects; the environment variable IW8R_REAL_SCRIPTABLES (1 or 0) overrides\n// the saved choice for one run (headless builds and tests).\n',
  '// Edit > Preferences > Real stock scripted objects, on unless the mapper turns it off (owner decision 2026-11-12:\n// "turn it on by default"); the environment variable IW8R_REAL_SCRIPTABLES (1 or 0) overrides the saved choice for\n// one run (headless builds and tests).\n'),
 ('    return Settings::instance().value("build/realScriptables", false).toBool();',
  '    return Settings::instance().value("build/realScriptables", true).toBool();'),
])
patch('src/qtapp/main_window.cpp', [
 ('    auto* realScriptables = new QCheckBox("Real stock scripted objects: carry stock doors, cones and cars into the map (experimental)", &dlg);',
  '    auto* realScriptables = new QCheckBox("Real stock scripted objects: carry stock doors, cones and cars into the map", &dlg);'),
 ('    realScriptables->setChecked(Settings::instance().value("build/realScriptables", false).toBool());',
  '    realScriptables->setChecked(Settings::instance().value("build/realScriptables", true).toBool());'),
 ('"the game\'s real door. Built exactly like the stock map\'s own data, but not seen in game yet. "\n                                "Off: such objects are left out and the door prefab builds its scripted stand-in.");',
  '"the game\'s real door. Built exactly like the stock map\'s own data, but not seen in game yet: if a map with such "\n                                "an object does not load, turn this off and build again. "\n                                "Off: such objects are left out and the door prefab builds its scripted stand-in.");'),
])
patch('src/qtapp/qt_gui_test.cpp', [
 ('int runQtGuiTest(MainWindow& w, const QString& samplePath, const QString& outDir) {\n',
  'int runQtGuiTest(MainWindow& w, const QString& samplePath, const QString& outDir) {\n'
  '    // Real stock scripted objects are on by default for mappers. The checks below cover the off path (left-out\n'
  '    // objects, the door prefab\'s fallback contents) and switch the on path in where they test it, so the run does\n'
  '    // not depend on the saved preference.\n'
  '    if (qEnvironmentVariableIsEmpty("IW8R_REAL_SCRIPTABLES")) qputenv("IW8R_REAL_SCRIPTABLES", "0");\n'),
])
patch('src/qtapp/qt_gui_test_bundles.cpp', [
 ('              "the shipped stock door is listed with the badge FALLBACK: " + pb->badgeReasonAt(doorRow));\n',
  '              "the shipped stock door is listed with the badge FALLBACK: " + pb->badgeReasonAt(doorRow));\n'
  '        {\n'
  '            // With real stock scripted objects on (the default for mappers) the door builds its real contents: no badge.\n'
  '            const QByteArray saved = qgetenv("IW8R_REAL_SCRIPTABLES");\n'
  '            qputenv("IW8R_REAL_SCRIPTABLES", "1");\n'
  '            pb->refresh();\n'
  '            const int onRow = pb->rowOfFile(shippedDoor);\n'
  '            const QString onBadge = onRow >= 0 ? pb->badgeAt(onRow) : QString("missing");\n'
  '            qputenv("IW8R_REAL_SCRIPTABLES", saved.isEmpty() ? QByteArray("0") : saved);\n'
  '            pb->refresh();\n'
  '            check(onRow >= 0 && onBadge != "FALLBACK" && pb->badgeAt(pb->rowOfFile(shippedDoor)) == "FALLBACK",\n'
  '                  "with real stock scripted objects on the stock door has no FALLBACK badge (badge \'" + onBadge + "\')");\n'
  '        }\n'),
])
# wording: bundle + checklist + guide
patch('data/prefabs/bundles/doors/stock_door_wood_single.iw8prefab/prefab.json', [
 ("With Edit > Preferences > Real stock scripted objects turned on it is the game's real door, copied from the stock map into yours. With it off it is built from the stock door's own parts (model, sounds, timing, prompt words) and a script.",
  "It is the game's real door, copied from the stock map into yours (Edit > Preferences > Real stock scripted objects, on by default). With that switch off it is built from the stock door's own parts (model, sounds, timing, prompt words) and a script."),
])
patch('docs/USER_GUIDE.md', [
 ("  definitions live inside stock map files. Turn on **Edit > Preferences > Real stock scripted objects** and the\n  build copies the definition with its models, collision and materials from the stock map into yours (it reads the\n  game folder, a few seconds per stock map). With the switch off such an entity is left out and the log says so.\n\nThe switch is off by default because nothing built this way has been seen in game yet. The built data is checked\nagainst the stock map's own (the definition read back from your map equals the stock one), but whether the game\naccepts it can only be seen by loading the map once.",
  "  definitions live inside stock map files. The build copies the definition with its models, collision and\n  materials from the stock map into yours (it reads the game folder, a few seconds per stock map). The switch for\n  this is **Edit > Preferences > Real stock scripted objects**, on by default; with it off such an entity is left\n  out and the log says so.\n\nNothing built this way has been seen in game yet. The built data is checked against the stock map's own (the\ndefinition read back from your map equals the stock one). If a map with such an object does not load, turn the\nswitch off and build again: that gives the previous behaviour back."),
 ("  from the stock door's own model and sounds plus a script. Turn on Edit > Preferences > Real stock scripted objects\n  and the badge goes away: the prefab then builds the game's real door, copied from the stock map (not seen in game\n  yet). The build log says which one was built.",
  "  the game's real door, copied from the stock map (not seen in game yet). With Edit > Preferences > Real stock\n  scripted objects turned off it has the badge FALLBACK and is built from the stock door's own model and sounds plus\n  a script. The build log says which one was built."),
])
patch('docs/TEST_CHECKLIST.md', [
 ('- [ ] Edit > Preferences: tick "Real stock scripted objects". In the Prefabs dock the stock door no longer has\n      the FALLBACK badge.\n',
  '- [ ] Edit > Preferences: "Real stock scripted objects" is ticked (it is on by default since 2026-11-12, the\n      owner\'s choice). In the Prefabs dock the stock door has no FALLBACK badge.\n'),
])
print('ok')
