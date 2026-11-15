import io

# Preferences in sections (2026-11-15 review): one long list of rows becomes Editing / Assets / Build / Play in game
# groups; the folder fields say what they are for and how to list several.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if s.count(a) == 0 and b in s:
        return
    assert s.count(a) == count, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


M = 'src/qtapp/main_window.cpp'
patch(M, '''    QDialog dlg(this);
    dlg.setWindowTitle("Preferences");
    dlg.setObjectName("preferencesDialog");
    auto* form = new QFormLayout(&dlg);
    auto num = [&](double lo, double hi, double v, int dec) {''', '''    QDialog dlg(this);
    dlg.setWindowTitle("Preferences");
    dlg.setObjectName("preferencesDialog");
    // Sections: Editing, Assets, Build, Play in game (F5).
    auto* outer = new QVBoxLayout(&dlg);
    const auto section = [&](const QString& title) {
        auto* box = new QGroupBox(title, &dlg);
        outer->addWidget(box);
        return new QFormLayout(box);
    };
    QFormLayout* editing = section("Editing");
    QFormLayout* assets = section("Assets");
    QFormLayout* build = section("Build");
    QFormLayout* playForm = section("Play in game (F5)");
    auto num = [&](double lo, double hi, double v, int dec) {''')
patch(M, '''    auto list = [&](const QStringList& l) {
        auto* e = new QLineEdit(l.join(';'), &dlg);
        e->setPlaceholderText("folders separated by ;");
        return e;
    };
    auto* mats = list(cur.materialRoots);
    auto* models = list(cur.modelRoots);
    auto* prefabs = list(cur.prefabRoots);
    form->addRow("Camera speed (units/s)", speed);
    form->addRow("Camera field of view", fov);
    form->addRow("Autosave every (minutes)", autosave);
    form->addRow("Toast notifications", toasts);
    form->addRow("Texture lock", texLock);
    form->addRow("Fog preview", fog);
    form->addRow("Custom model scale (OBJ units -> map)", scale);
    form->addRow("Extra material folders", mats);
    form->addRow("Extra model folders", models);
    form->addRow("Prefab folders", prefabs);''', '''    auto list = [&](const QStringList& l, const QString& tip) {
        auto* e = new QLineEdit(l.join(';'), &dlg);
        e->setPlaceholderText("folders separated by ;");
        e->setToolTip(tip + "\\nSeveral folders: separate them with ; (for example D:/art/materials;E:/shared/materials).");
        e->setMinimumWidth(360);
        return e;
    };
    auto* mats = list(cur.materialRoots, "Folders with your own materials (images and .material files), shown in the Materials panel "
                                         "next to the map's own and the stock ones.");
    auto* models = list(cur.modelRoots, "Folders with your own models (OBJ, glTF), shown in the Models panel.");
    auto* prefabs = list(cur.prefabRoots, "Folders with prefabs (.map files), shown in the Prefabs panel next to <map folder>/prefabs. "
                                          "Double-click a prefab there to place it.");
    speed->setToolTip("How fast the camera flies with W A S D (the mouse wheel changes it while flying)");
    autosave->setToolTip("A copy of the map is saved this often into the autosave folder (File > Autosave History); 0 = off");
    texLock->setToolTip("Textures stay fixed to brushes when they are moved or rotated");
    scale->setToolTip("OBJ / glTF files are often in metres or centimetres: this scales them to map units");
    editing->addRow("Camera speed (units/s)", speed);
    editing->addRow("Camera field of view", fov);
    editing->addRow("Autosave every (minutes)", autosave);
    editing->addRow("Toast notifications", toasts);
    editing->addRow("Texture lock", texLock);
    editing->addRow("Fog preview", fog);
    assets->addRow("Custom model scale (OBJ units -> map)", scale);
    assets->addRow("Extra material folders", mats);
    assets->addRow("Extra model folders", models);
    assets->addRow("Prefab folders", prefabs);''')
patch(M, '''    form->addRow("Build", realScriptables);''', '''    build->addRow("Scripted objects", realScriptables);''')
patch(M, '''    form->addRow("Build: collision", collisionLayout);
    form->addRow("F5: game mode", playMode);
    form->addRow("F5: bots", playBots);
    form->addRow("F5", playCover);
    form->addRow("", playHost);
    form->addRow("F5: host command", playHostCmd);
    form->addRow("", playRequest);''', '''    build->addRow("Collision", collisionLayout);
    playForm->addRow("Game mode", playMode);
    playForm->addRow("Bots", playBots);
    playForm->addRow("", playCover);
    playForm->addRow("", playHost);
    playForm->addRow("Host command", playHostCmd);
    playForm->addRow("", playRequest);''')
patch(M, '''    note->setWordWrap(true);
    note->setStyleSheet("color: gray");
    form->addRow(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted) return;
    Preferences p;''', '''    note->setWordWrap(true);
    note->setStyleSheet("color: gray");
    outer->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    outer->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted) return;
    Preferences p;''')
print('ok')
