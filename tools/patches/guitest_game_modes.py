import io
p = 'src/qtapp/qt_gui_test.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = ('            for (int k = 0; k < 3; ++k) ctx.doc.undo();' + nl + '            ctx.documentEdited();' + nl + '        }' + nl)
assert s.count(a) == 1
b = a + '''        {
            // Game modes (research 87): a preset carried by the map, the Warzone start entities, and the F5 preset.
            const std::vector<GameModePreset>& modes = builtinGameModes();
            const GameModePreset* small = findGameMode(modes, "br_small_map");
            const size_t pos = ctx.doc.undoPosition(), entsBefore = ctx.doc.map().entities.size();
            const bool baked = small && w.bakeGameMode(*small);
            std::string dvars, br;
            for (const Entity& e : ctx.doc.map().entities)
                if (e.isWorldspawn()) {
                    if (const std::string* v = e.value("iw8r_mode_dvars")) dvars = *v;
                    if (const std::string* v = e.value("iw8r_br")) br = *v;
                }
            r.check(baked && dvars == "scr_br_teamsize=1; scr_br_circle_disable=1; scr_br_gulag=0" && br == "nocircle nogulag skipdrop",
                    QString("a game mode preset is carried by the map (dvars '%1', switches '%2')").arg(qs(dvars), qs(br)));
            const int added = w.insertWarzoneStart();
            bool brListed = false;
            for (const GameTypeCheck& g : gameTypeChecks(ctx.doc.map())) brListed |= g.gametype == "br" && g.supported;
            r.check(added == 9 && ctx.doc.map().entities.size() == entsBefore + 9 && brListed,
                    QString("the Warzone start adds 8 spawns and a prematch area, and the map then counts as a br map (%1 added)").arg(added));
            while (ctx.doc.undoPosition() > pos && ctx.doc.undo()) {}
            ctx.documentEdited();
            // The F5 preset: stored, read back as dvars, and restored to what it was.
            Settings& st = Settings::instance();
            const QVariant oldPreset = st.value("devmap/modePreset"), oldDvars = st.value("devmap/modeDvars"), oldType = st.value("devmap/gametype");
            DevmapRunner::setModePreset("br_trios", "br", "loot_table_name=mp/loot_item_defs_s3.csv; scr_br_teamsize=3; bad name=1");
            const auto f5 = DevmapRunner::modeDvarsSetting();
            const bool stored = DevmapRunner::modePresetSetting() == "br_trios" && DevmapRunner::gametypeSetting() == "br" && f5.size() == 2 &&
                                f5[1].first == "scr_br_teamsize" && f5[1].second == "3";
            st.setValue("devmap/modePreset", oldPreset);
            st.setValue("devmap/modeDvars", oldDvars);
            st.setValue("devmap/gametype", oldType);
            r.check(stored && w.action("GameModes") != nullptr, "the F5 game mode preset stores its game type and dvars (the bad pair is dropped)");
        }
'''.replace('\n', nl)
s = s.replace(a, b)
for inc in ('"game_modes.h"', '"map_gametypes.h"', '"devmap_launch.h"'):
    if '#include ' + inc not in s:
        first = s.index('#include "prefab_browser.h"')
        s = s[:first] + '#include ' + inc + nl + s[first:]
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
