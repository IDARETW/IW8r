import io
import os

# Owner 2026-11-15: "we don't need a separate tools menu, what we need to do with that is implement any appropriate tools
# into already-existing menus or into new menus, and we can keep the tools tab at the top of the app". The Tools side
# panel goes; the Tools menu in the menu bar stays as it is; the side bar panels' dots menus get the tools that belong
# to them, and the map checks get a new side bar panel (Map Check, map_check_panel.h):
#   Materials: Material Info, Editable Copy, Techset Info, Replace Material in Map, Shader Editor, Adjust Image,
#              Availability Audit, Cache Manager
#   Models:    Model Info, Replace Model Material, Scatter by Rules, Scatter Models, Model Paint Mode, Drop to Floor,
#              Availability Audit, Cache Manager
#   Effects:   Effect Editor, Availability Audit, Cache Manager       Sounds: Sound Settings
#   Images:    Adjust Image, IES Profiles, Availability Audit, Cache Manager
#   IW8 Spawns: Generate Spawns, Game Modes                          Undo History: Record Macro, Run Command Script
#   Map Check: Check Map (the window), Leak Check, Clear Leak Line, Asset Usage, Unused Custom Assets
# Left in the menu bar only: Create Rope, Measure, Tool Paths, Hotkeys, the web server, freeze reports.

ROOT = 'E:/IW8-Radiant/src/'


def rw(p, pairs):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        if b in s and a not in s:
            continue
        assert s.count(a) == 1, (p, s.count(a), a[:90])
        s = s.replace(a, b)
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


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


rw('qtapp/main_window.cpp', [
('#include "tools_panel.h"\n', '#include "map_check_panel.h"\n'),
('''    // The Tools menu's commands in groups (tools_panel.h; filled once the menu exists, at the end of the Tools menu).
    sidePanels.push_back(makeDock("Tools", new ToolsPanel(), true));''',
 '''    // The map's Check Map findings with the check tools (map_check_panel.h; owner 2026-11-15: the Tools side panel's
    // tools go into the panels they belong to, the map checks into a panel of their own).
    sidePanels.push_back(makeDock("Map Check", new MapCheckPanel(&ctx_), true));'''),
('''{"Stock Map", 0xE909}, {"Undo History", 0xE81C}, {"Tools", 0xEC7A},''', '''{"Stock Map", 0xE909}, {"Map Check", 0xE73E}, {"Undo History", 0xE81C},'''),
])

NEW_BLOCK = '''    {
        // Owner 2026-11-15: no separate Tools side panel; the Tools menu stays in the menu bar, and each side bar panel's
        // dots menu holds the tools that belong to it (the same actions as the menu's). Closed panels' widgets are out of
        // the window's tree, so they are found through their docks.
        const auto a = [this](const char* command) { return action(command); };
        const auto panel = [this](const char* name) -> QWidget* {
            ads::CDockWidget* d = dock_ ? dock_->findDockWidget(name) : nullptr;
            return d ? d->widget() : nullptr;
        };
        materials_->setToolActions({a("MaterialInfo"), a("CloneStockMaterial"), a("TechsetInfo"), a("ReplaceMaterialInMap"),
                                    a("ShaderEditor"), a("AdjustImage"), a("AssetAudit"), a("CacheManager")});
        models_->setToolActions({a("ModelInfo"), a("ReplaceModelMaterial"), a("ScatterByRules"), a("ScatterModels"), a("ModelPaintMode"),
                                 a("DropToFloor"), a("AssetAudit"), a("CacheManager")});
        effects_->setToolActions({a("EffectEditor"), a("AssetAudit"), a("CacheManager")});
        sounds_->setToolActions({a("SoundSettings")});
        images_->setToolActions({a("AdjustImage"), a("IesBrowser"), a("AssetAudit"), a("CacheManager")});
        if (auto* sp = qobject_cast<SpawnsPanel*>(panel("IW8 Spawns"))) sp->setTools({a("GenerateSpawns"), a("GameModes")});
        if (auto* hp = qobject_cast<HistoryPanel*>(panel("Undo History"))) hp->setTools({a("RecordMacro"), a("RunCommandScript")});
        if (auto* mc = qobject_cast<MapCheckPanel*>(panel("Map Check")))
            mc->setTools({a("CheckMap"), a("LeakCheck"), a("ClearPointfile"), a("AssetUsage"), a("ListUnusedCustomAssets")});
    }

    QMenu* help = menuBar()->addMenu("&Help");'''
slice_replace('qtapp/main_window.cpp', '''    {
        // Owner 2026-11-15: the Tools menu in the side bar. The Tools panel lists every entry in groups with a line on''',
              '''    QMenu* help = menuBar()->addMenu("&Help");''', NEW_BLOCK)

rw('qtapp/feature_shots.cpp', [
('''"IW8 Spawns", "Undo History", "Tools"}) {''', '''"IW8 Spawns", "Map Check", "Undo History"}) {'''),
])

# The tests: the panels' menus hold their tools; no Tools panel; the new panels.
rw('qtapp/qt_gui_test_sidebar.cpp', [
('#include "tools_panel.h"\n', '#include "map_check_panel.h"\n#include "panels.h"\n'),
('''        for (const char* name : {"Materials", "Models", "Effects", "Sounds", "Images", "Tools"}) {''',
 '''        for (const char* name : {"Materials", "Models", "Effects", "Sounds", "Images", "Map Check", "IW8 Spawns", "Undo History"}) {'''),
('''            else panel = w.findChild<ToolsPanel*>();''', '''            else if (QString(name) == "Map Check") panel = w.findChild<MapCheckPanel*>();
            else if (QString(name) == "IW8 Spawns") panel = w.findChild<SpawnsPanel*>();
            else panel = w.findChild<HistoryPanel*>();'''),
])

for f in ('qtapp/tools_panel.cpp', 'qtapp/tools_panel.h'):
    if os.path.exists(ROOT + f):
        os.remove(ROOT + f)
print('ok')
