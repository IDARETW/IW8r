import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


SH = 'src/viewport/scene.h'
patch(SH, '''    std::set<Id> hiddenByRegion; // primitives and point entities outside the active Region (see core/region.h)''',
      '''    std::set<Id> hiddenByRegion; // primitives and point entities outside the active Region (see core/region.h)
    // View > Game Mode (core game_mode_view): the gametype shown ("" = every mode) and the entities it removes.
    std::string gameMode;
    std::set<Id> hiddenByMode;''')
S = 'src/viewport/scene.cpp'
patch(S, '''            if (filtered || hidden.count(it.id) || hidden.count(e.id) || hiddenByLayer.count(it.id) || hiddenByRegion.count(it.id)) continue;''',
      '''            if (filtered || hidden.count(it.id) || hidden.count(e.id) || hiddenByLayer.count(it.id) || hiddenByRegion.count(it.id) ||
                hiddenByMode.count(e.id))
                continue;''')
patch(S, '''            if (filtered || hidden.count(e.id) || hiddenByRegion.count(e.id)) continue;''',
      '''            if (filtered || hidden.count(e.id) || hiddenByRegion.count(e.id) || hiddenByMode.count(e.id)) continue;''')

CG = 'src/viewport/camera_geometry.cpp'
patch(CG, '''        if (showSpawns) {
            for (const auto& s : scene.spawns) {''', '''        if (showSpawns) {
            const GameModeView* mode = findGameModeView(scene.gameMode);
            for (const auto& s : scene.spawns) {
                if (mode && !gameModeKeepsSpawn(*mode, s.name)) continue;''')
s = io.open(CG, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
i = s.index('#include "')
s = s[:i] + '#include "game_mode_view.h"' + nl + s[i:]
io.open(CG, 'w', encoding='utf-8', newline='').write(s)

G = 'src/qtapp/grid_view.cpp'
patch(G, '''        for (const auto& s : ctx_->scene.spawns)
            p.drawRect(''', '''        const GameModeView* mode = findGameModeView(ctx_->scene.gameMode);
        for (const auto& s : ctx_->scene.spawns)
            if (!mode || gameModeKeepsSpawn(*mode, s.name)) p.drawRect(''')
s = io.open(G, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
i = s.index('#include "')
s = s[:i] + '#include "game_mode_view.h"' + nl + s[i:]
io.open(G, 'w', encoding='utf-8', newline='').write(s)

EH = 'src/qtapp/editor_context.h'
patch(EH, '''    void refreshRegion();  // recomputes scene.hiddenByRegion from `region` (called by documentEdited)''',
      '''    void refreshRegion();  // recomputes scene.hiddenByRegion from `region` (called by documentEdited)
    void refreshGameMode();  // recomputes scene.hiddenByMode from scene.gameMode (called by documentEdited)''')
E = 'src/qtapp/editor_context.cpp'
patch(E, '''void EditorContext::documentEdited(const QString& status) {
    refreshRegion();''', '''void EditorContext::refreshGameMode() {
    std::set<Id> out;
    if (const GameModeView* mode = findGameModeView(scene.gameMode))
        for (const auto& e : doc.map().entities)
            if (!e.isWorldspawn() && !gameModeKeepsEntity(*mode, e.classname(), e.value("script_gameobjectname"))) out.insert(e.id);
    if (out != scene.hiddenByMode) {
        scene.hiddenByMode = std::move(out);
        scene.invalidate();
    }
}

void EditorContext::documentEdited(const QString& status) {
    refreshRegion();
    refreshGameMode();''')
patch(E, '''#include "region.h"''', '''#include "game_mode_view.h"
#include "region.h"''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''    QAction* spawnsToggle = addCommand(view, "ToggleCompiledSpawns", "Show IW8 Compiled Spawns", QKeySequence(), [this] {''',
      '''    {
        // View > Game Mode: show what one gametype keeps (its objectives, triggers, script models and brushmodels, and
        // its spawns); the game deletes every other mode's gameobjects when a match starts (research 40).
        QMenu* gmMenu = view->addMenu("Game Mode");
        gmMenu->setObjectName("gameModeMenu");
        auto* group = new QActionGroup(gmMenu);
        const QString saved = Settings::instance().value("view/gameMode", QString()).toString();
        const auto choose = [this](const QString& id, const QString& label) {
            Settings::instance().setValue("view/gameMode", id);
            ctx_.scene.gameMode = id.toStdString();
            ctx_.refreshGameMode();
            ctx_.documentEdited(id.isEmpty() ? QString("Game Mode: every mode's objects shown")
                                             : QString("Game Mode: %1 (%2 entities of other modes hidden)").arg(label).arg(ctx_.scene.hiddenByMode.size()));
        };
        QAction* all = gmMenu->addAction("All Modes", this, [choose] { choose(QString(), "All Modes"); });
        all->setObjectName("gameMode_all");
        all->setCheckable(true);
        all->setChecked(saved.isEmpty() || !findGameModeView(saved.toStdString()));
        group->addAction(all);
        gmMenu->addSeparator();
        for (const GameModeView& m : gameModeViews()) {
            const QString id = QString::fromStdString(m.id), label = QString::fromStdString(m.label);
            QAction* a = gmMenu->addAction(label, this, [choose, id, label] { choose(id, label); });
            a->setObjectName("gameMode_" + id);
            a->setCheckable(true);
            a->setChecked(saved == id);
            group->addAction(a);
        }
        if (findGameModeView(saved.toStdString())) ctx_.scene.gameMode = saved.toStdString();
    }
    QAction* spawnsToggle = addCommand(view, "ToggleCompiledSpawns", "Show IW8 Compiled Spawns", QKeySequence(), [this] {''')
s = io.open(M, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('#include "stock_terrain_layers.h"', '#include "stock_terrain_layers.h"' + nl + '#include "game_mode_view.h"', 1)
io.open(M, 'w', encoding='utf-8', newline='').write(s)
print('ok')
