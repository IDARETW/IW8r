import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/qtapp/main_window.cpp', [
 ('''    form->addRow("Build", realScriptables);
''', '''    form->addRow("Build", realScriptables);
    // 14.1 (research 90): the collision layout of maps that do not set worldspawn iw8r_collision themselves.
    auto* collisionLayout = new QComboBox(&dlg);
    collisionLayout->setObjectName("prefsCollisionLayout");
    collisionLayout->addItem("Automatic: streamed tiles for a map wider than 20000 units", "auto");
    collisionLayout->addItem("Always one piece (all collision in the map zone)", "one");
    collisionLayout->addItem("Always streamed tiles (1,024 tile zones and the combined package)", "tiles");
    collisionLayout->setCurrentIndex(std::max(0, collisionLayout->findData(QString(collisionLayoutName(
        parseCollisionLayout(Settings::instance().value("build/collisionLayout", "auto").toString().toStdString()))))));
    collisionLayout->setToolTip("How a build lays out the collision. One piece: every core map does this. Streamed tiles: the game's own layout for "
                                "Warzone-size maps (32 x 32 tiles of 8192 units; a client loads the tiles near the player, a host the combined "
                                "package). The editor cuts the tiles by itself. A map can set its own with worldspawn iw8r_collision "
                                "(auto, one, tiles). Tiles are built like the stock maps' own but not seen in game yet.");
    form->addRow("Build: collision", collisionLayout);
'''),
 ('''    Settings::instance().setValue("build/realScriptables", realScriptables->isChecked());
''', '''    Settings::instance().setValue("build/realScriptables", realScriptables->isChecked());
    Settings::instance().setValue("build/collisionLayout", collisionLayout->currentData().toString());
'''),
])
print('ok')
