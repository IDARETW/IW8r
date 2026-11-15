import io

# Menus (second review, 2026-11-15):
#  - File: Recent right under Open; the stock map commands in File > Stock Map; exports in File > Export; prefabs in
#    File > Prefabs;
#  - separators between the groups of the Tools, View and Compile menus;
#  - Select Linked beside Select Connected in Edit ("Select Linked (whole target chain)");
#  - clearer labels: Split Patch, Cap Patch, Revert Last Deploy, Gizmo: Show in Camera View;
#  - workspaces 1-9 are commands (Alt+Shift+1..9 can be rebound, listed in Tools > Hotkeys and the palette);
#  - CSG Hollow / Subtract / Cylinder say why nothing happened;
#  - Game Lighting is on again at start-up when it was on when the editor closed (not in Safe Mode);
#  - Tools > Hotkeys: a key already used by another command asks before it is moved over.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b in s:  # applied already
        return
    assert s.count(a) == count, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


M = 'src/qtapp/main_window.cpp'
# ---- File
patch(M, '''    addCommand(file, "OpenStockZone", "Open IW8 Stock &Map (ACTS)...", QKeySequence("Ctrl+Shift+O"), [this] { openStockZoneDialog(); });
    addCommand(file, "RelightStockMap",''', '''    recentMenu_ = file->addMenu("Recent");
    addCommand(file, "OpenStockZone", "Open IW8 Stock &Map (ACTS)...", QKeySequence("Ctrl+Shift+O"), [this] { openStockZoneDialog(); });
    // What works on an open stock map: relighting, its compiled data, rebuilding and installing its zone.
    QMenu* stockMenu = file->addMenu("Stoc&k Map");
    stockMenu->setObjectName("stockMapMenu");
    file->addSeparator();
    addCommand(stockMenu, "RelightStockMap",''')
patch(M, '''    addCommand(file, "RelightStockSky", "Relight Stock Map Sky''', '''    addCommand(stockMenu, "RelightStockSky", "Relight Stock Map Sky''')
patch(M, '''    addCommand(file, "RelightStockSun", "Relight Stock Map Sun''', '''    addCommand(stockMenu, "RelightStockSun", "Relight Stock Map Sun''')
patch(M, '''    addCommand(file, "EditStockSpawns", "Edit Compiled Spawns (stock map)", QKeySequence(), [this] { editStockSpawns(); });
    addCommand(file, "EditLootPoints", "Edit Loot Points (Warzone stock map)", QKeySequence(), [this] { editLootPoints(); });
    addCommand(file, "EditPlacedObjects", "Edit Placed Objects (Warzone buy stations)", QKeySequence(), [this] { editPlacedObjects(); });
    addCommand(file, "LoadStreamedWorld",''', '''    stockMenu->addSeparator();
    addCommand(stockMenu, "EditStockSpawns", "Edit Compiled Spawns (stock map)", QKeySequence(), [this] { editStockSpawns(); });
    addCommand(stockMenu, "EditLootPoints", "Edit Loot Points (Warzone stock map)", QKeySequence(), [this] { editLootPoints(); });
    addCommand(stockMenu, "EditPlacedObjects", "Edit Placed Objects (Warzone buy stations)", QKeySequence(), [this] { editPlacedObjects(); });
    addCommand(stockMenu, "LoadStreamedWorld",''')
patch(M, '''    addCommand(file, "RebuildStockEntities", "Rebuild Stock Map With These Entities...",''',
      '''    stockMenu->addSeparator();
    addCommand(stockMenu, "RebuildStockEntities", "Rebuild Stock Map With These Entities...",''')
patch(M, '''    addCommand(file, "ExportOverview", "Export Overview Image (PNG)...",''', '''    QMenu* exportMenu = file->addMenu("E&xport");
    exportMenu->setObjectName("exportMenu");
    addCommand(exportMenu, "ExportOverview", "Export Overview Image (PNG)...",''')
patch(M, '''    addCommand(file, "ExportCameraImage", "Export Camera Image (high resolution)...",''',
      '''    addCommand(exportMenu, "ExportCameraImage", "Export Camera Image (high resolution)...",''')
patch(M, '''    addCommand(file, "ExportTurntable", "Export Turntable of Selection (PNG frames)...",''',
      '''    addCommand(exportMenu, "ExportTurntable", "Export Turntable of Selection (PNG frames)...",''')
patch(M, '''    addCommand(file, "ExportMapPackage", "Export Map Package (.zip)...",''', '''    addCommand(exportMenu, "ExportMapPackage", "Export Map Package (.zip)...",''')
patch(M, '''    addCommand(file, "RebuildStockZoneFast", "Rebuild Stock Zone (fast)...", QKeySequence(), [this] { rebuildStockZoneFast(); });
    addCommand(file, "InstallStockMap", "Install Edited Stock Map Into Game...", QKeySequence(), [this] { installEditedStockMap(); });
    addCommand(file, "RestoreStockMap", "Restore Stock Map In Game...", QKeySequence(), [this] { restoreStockMap(); });''',
      '''    addCommand(stockMenu, "RebuildStockZoneFast", "Rebuild Stock Zone (fast)...", QKeySequence(), [this] { rebuildStockZoneFast(); });
    stockMenu->addSeparator();
    addCommand(stockMenu, "InstallStockMap", "Install Edited Stock Map Into Game...", QKeySequence(), [this] { installEditedStockMap(); });
    addCommand(stockMenu, "RestoreStockMap", "Restore Stock Map In Game...", QKeySequence(), [this] { restoreStockMap(); });''')
patch(M, '''        QMenu* look = file->addMenu("First-Person Look (weapon and hands)");''',
      '''        QMenu* look = stockMenu->addMenu("First-Person Look (weapon and hands)");''')
patch(M, '''    addCommand(file, "HideStockProps", "Hide Stock Props In Selection", QKeySequence(), [this] { hideStockPropsInSelection(); });
    addCommand(file, "ShowStockProps", "Show All Stock Props", QKeySequence(), [this] { showAllStockProps(); });
    addCommand(file, "RebuildStockZone", "Rebuild Stock Zone (full dump, slow)...",''',
      '''    stockMenu->addSeparator();
    addCommand(stockMenu, "HideStockProps", "Hide Stock Props In Selection", QKeySequence(), [this] { hideStockPropsInSelection(); });
    addCommand(stockMenu, "ShowStockProps", "Show All Stock Props", QKeySequence(), [this] { showAllStockProps(); });
    addCommand(stockMenu, "RebuildStockZone", "Rebuild Stock Zone (full dump, slow)...",''')
patch(M, '''    recentMenu_ = file->addMenu("Recent");
    rebuildRecentMenu();''', '''    rebuildRecentMenu();''')
patch(M, '''    addCommand(file, "InsertPrefab", "Insert &Prefab...", QKeySequence(), [this] { insertPrefabDialog(); });''',
      '''    QMenu* prefabMenu = file->addMenu("&Prefabs");
    prefabMenu->setObjectName("prefabsMenu");
    addCommand(prefabMenu, "InsertPrefab", "Insert &Prefab...", QKeySequence(), [this] { insertPrefabDialog(); });''')
for cmd in ['ExplodePrefab', 'PrefabParameters', 'SaveSelectionAsPrefab', 'SaveSelectionAsMapPrefab', 'OpenBundleFolder',
            'RotatePrefabStep', 'ConvertPrefabMap', 'ExportPrefabZip', 'ImportPrefabZip', 'MakePrefabUnique']:
    patch(M, '''    addCommand(file, "%s",''' % cmd, '''    addCommand(prefabMenu, "%s",''' % cmd)
# ---- Edit / labels
patch(M, '''    addCommand(tools, "SelectLinked", "Select Linked (target / targetname chain)", QKeySequence("Ctrl+Shift+L"), [this] {''',
      '''    addCommand(edit, "SelectLinked", "Select Linked (whole target chain)", QKeySequence("Ctrl+Shift+L"), [this] {''')
patch(M, '''    addCommand(patch, "PatchCap", "Cap", QKeySequence(),''', '''    addCommand(patch, "PatchCap", "Cap Patch", QKeySequence(),''')
patch(M, '''    addCommand(patch, "PatchSplit", "Split", QKeySequence(),''', '''    addCommand(patch, "PatchSplit", "Split Patch", QKeySequence(),''')
patch(M, '''    addCommand(compile, "UndoDeploy", "Undo Last Deploy", QKeySequence(), [this] {''',
      '''    addCommand(compile, "UndoDeploy", "Revert Last Deploy", QKeySequence(), [this] {''')
patch(M, '''    QAction* gizmoToggle = addCommand(view, "ToggleGizmo", "Transform Gizmo (camera view)", QKeySequence(Qt::Key_G), [this] {''',
      '''    QAction* gizmoToggle = addCommand(view, "ToggleGizmo", "Gizmo: Show in Camera View", QKeySequence(Qt::Key_G), [this] {''')
# ---- Brush: say why nothing happened
patch(M, '''    addCommand(brush, "CSGHollow", "CSG Hollow", QKeySequence(), [this] { if (hollowSelection(ctx_)) ctx_.documentEdited("Hollowed (wall = grid size)"); });
    addCommand(brush, "CSGSubtract", "CSG Subtract", QKeySequence("Shift+U"), [this] { if (subtractSelection(ctx_)) ctx_.documentEdited("Subtracted"); });
    addCommand(brush, "MakeCylinder", "Cylinder (8 sides) From Selection", QKeySequence(), [this] { if (prismFromSelection(ctx_, 8)) ctx_.documentEdited("Cylinder"); });''',
      '''    addCommand(brush, "CSGHollow", "CSG Hollow", QKeySequence(), [this] {
        if (hollowSelection(ctx_)) ctx_.documentEdited("Hollowed (wall = grid size)");
        else ctx_.setStatus("CSG Hollow: select one or more brushes first");
    });
    addCommand(brush, "CSGSubtract", "CSG Subtract", QKeySequence("Shift+U"), [this] {
        if (subtractSelection(ctx_)) ctx_.documentEdited("Subtracted");
        else ctx_.setStatus("CSG Subtract: select the brushes to cut with, touching the brushes to cut");
    });
    addCommand(brush, "MakeCylinder", "Cylinder (8 sides) From Selection", QKeySequence(), [this] {
        if (prismFromSelection(ctx_, 8)) ctx_.documentEdited("Cylinder");
        else ctx_.setStatus("Cylinder: select a brush first (its bounds become the cylinder)");
    });''')
# ---- Game Lighting restored at start-up
patch(M, '''    gameToggle->setCheckable(true);
    {''', '''    gameToggle->setCheckable(true);
    if (Settings::instance().value("view/gameLighting", false).toBool() && !qEnvironmentVariableIsSet("IW8R_SAFE_MODE")) {
        // On when the editor was closed with it on (Safe Mode starts with the plain views).
        ctx_.scene.gameLook = true;
        gameToggle->setChecked(true);
    }
    {''')
# ---- Workspaces as commands
patch(M, '''                QAction* a = ws->addAction(n, this, [this, n] {
                    const QByteArray st = Settings::instance().value("workspaces/state/" + n).toByteArray();
                    if (st.isEmpty() || !dock_->restoreState(st, kDockLayoutVersion))
                        return ctx_.setStatus("Workspace " + n + " was saved by an older version (new panels); save it again");
                    ctx_.setStatus("Workspace " + n);
                });
                if (i < 9) a->setShortcut(QKeySequence(QString("Alt+Shift+%1").arg(i + 1)));''', '''                // The key is the WorkspaceN command's (Tools > Hotkeys can change it); shown here after a tab.
                const QAction* cmd = i < 9 ? action(QString("Workspace%1").arg(i + 1)) : nullptr;
                const QString key = cmd ? cmd->shortcut().toString(QKeySequence::NativeText) : QString();
                ws->addAction(key.isEmpty() ? n : n + "\\t" + key, this, [this, n] { applyWorkspace(n); });''')
patch(M, '''        QMenu* ws = view->addMenu("Workspaces");
        ws->setObjectName("workspacesMenu");''', '''        QMenu* ws = view->addMenu("Workspaces");
        ws->setObjectName("workspacesMenu");
        for (int i = 0; i < 9; ++i)
            addCommand(nullptr, QString("Workspace%1").arg(i + 1), QString("Workspace %1").arg(i + 1), QKeySequence(QString("Alt+Shift+%1").arg(i + 1)), [this, i] {
                const QStringList names = Settings::instance().value("workspaces/names").toStringList();
                if (i >= names.size()) return ctx_.setStatus(QString("No workspace %1 yet (View > Workspaces > Save Current Layout As...)").arg(i + 1));
                applyWorkspace(names[i]);
            });''')
patch(M, '''void MainWindow::showDock(const QString& name) {''', '''void MainWindow::applyWorkspace(const QString& n) {
    const QByteArray st = Settings::instance().value("workspaces/state/" + n).toByteArray();
    if (st.isEmpty() || !dock_->restoreState(st, kDockLayoutVersion))
        return ctx_.setStatus("Workspace " + n + " was saved by an older version (new panels); save it again");
    syncSideActions();
    ctx_.setStatus("Workspace " + n);
}

void MainWindow::showDock(const QString& name) {''')
# ---- separators: Tools
for cmd in ['AssetUsage', 'DropToFloor', 'RecordMacro', 'IesBrowser', 'GenerateSpawns']:
    patch(M, '''    addCommand(tools, "%s",''' % cmd, '''    tools->addSeparator();
    addCommand(tools, "%s",''' % cmd)
patch(M, '''        QAction* web = addCommand(tools, "WebServer",''', '''        tools->addSeparator();
        QAction* web = addCommand(tools, "WebServer",''')
# ---- View
patch(M, '''    addCommand(view, "StockReference", "Stock Map Reference...",''', '''    view->addSeparator();
    addCommand(view, "StockReference", "Stock Map Reference...",''')
patch(M, '''    QAction* texToggle = addCommand(view, "ToggleTexturedView",''', '''    view->addSeparator();
    QAction* texToggle = addCommand(view, "ToggleTexturedView",''')
# ---- Compile
for cmd in ['MapAtmosphere', 'LoadMapInGame', 'HudImagesFolder', 'BuildDeployLoad']:
    patch(M, '''    addCommand(compile, "%s",''' % cmd, '''    compile->addSeparator();
    addCommand(compile, "%s",''' % cmd)

H = 'src/qtapp/main_window.h'
patch(H, '''    void resetForNewDocument();      // forgets the open document's scene caches and stock map state''',
      '''    void resetForNewDocument();      // forgets the open document's scene caches and stock map state
    void applyWorkspace(const QString& name);  // View > Workspaces''')

P = 'src/qtapp/panels.cpp'
patch(P, '''        connect(edit, &QKeySequenceEdit::editingFinished, this, [action, edit, command] {
            action->setShortcut(edit->keySequence());
            Settings::instance().setShortcut(command, edit->keySequence());
        });''', '''        connect(edit, &QKeySequenceEdit::editingFinished, this, [this, action, edit, command, actions] {
            // A key another command has: ask, and take it from that command (two commands on one key both stop working).
            const QKeySequence seq = edit->keySequence();
            if (!seq.isEmpty())
                for (auto o = actions.begin(); o != actions.end(); ++o) {
                    if (o.value() == action || o.value()->shortcut() != seq) continue;
                    if (QMessageBox::question(this, "Hotkeys",
                                              QString("%1 is already the key of %2.\\n\\nUse it for %3 instead (%2 is left without a key)?")
                                                  .arg(seq.toString(QKeySequence::NativeText), o.value()->text().remove('&'), action->text().remove('&'))) !=
                        QMessageBox::Yes) {
                        const QSignalBlocker block(edit);
                        edit->setKeySequence(action->shortcut());
                        return;
                    }
                    o.value()->setShortcut(QKeySequence());
                    Settings::instance().setShortcut(o.key(), QKeySequence());
                    for (int r = 0; r < table_->rowCount(); ++r)
                        if (table_->item(r, 0) && table_->item(r, 0)->text() == o.key())
                            if (auto* other = qobject_cast<QKeySequenceEdit*>(table_->cellWidget(r, 1))) {
                                const QSignalBlocker block(other);
                                other->clear();
                            }
                }
            action->setShortcut(seq);
            Settings::instance().setShortcut(command, seq);
        });''')
G = 'src/qtapp/grid_view.cpp'
patch(G, '''        menu.addAction("CSG Hollow", this, [this] { if (hollowSelection(*ctx_)) ctx_->documentEdited("Hollowed"); });
        menu.addAction("CSG Subtract", this, [this] { if (subtractSelection(*ctx_)) ctx_->documentEdited("Subtracted"); });''',
      '''        menu.addAction("CSG Hollow", this, [this] {
            if (hollowSelection(*ctx_)) ctx_->documentEdited("Hollowed");
            else ctx_->setStatus("CSG Hollow: select one or more brushes first");
        });
        menu.addAction("CSG Subtract", this, [this] {
            if (subtractSelection(*ctx_)) ctx_->documentEdited("Subtracted");
            else ctx_->setStatus("CSG Subtract: select the brushes to cut with, touching the brushes to cut");
        });''')
print('ok')
