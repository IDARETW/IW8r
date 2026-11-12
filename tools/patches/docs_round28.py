import io

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

p = 'docs/ROADMAP.md'
s, nl = load(p)
a = '## Owner decisions and actions on the path (U)'
assert s.count(a) == 1
sec = [
 '## L13. Warzone and game modes (owner scope decision 2026-11-12)',
 '',
 'Asked which large features the finished product must include, the owner answered: "Stock map editing, warzone-specific',
 'editing and map/mode creation". Stock map editing is L11 (rows 11.2, 11.4, 11.5). This layer is the Warzone half. The',
 'facts are in research 87 (survey of the Warzone maps, loot, modes and what a small map needs). Nothing here has been',
 'seen in game.',
 '',
 '| # | Item | Status | Size |',
 '|---|---|---|---|',
 '| 13.1 | Game mode presets: a mode is a game type script plus dvars (Warzone Trios, Quads, Solos and Blood Money are all the script br) | done offline (2026-11-12): core `game_modes.*` (12 built-in presets: the six core game types, the four Warzone variants of the private server playlist, two for small maps; user presets as `<id>.iw8mode.json` in the app data folder `game_modes`; 3 core tests). Tools, Game Modes: list, edit, Save as my preset, Delete. | M |',
 '| 13.2 | A preset for F5: its dvars reach the game | done offline (2026-11-12): Use for F5 stores the preset; the editor then starts the match itself and sends `set <dvar> <value>` lines before the map and game type (core `startMatchCommands` with mode dvars; a mode with dvars turns the client\'s own start request off, because that request names only map and game type). GUI and core checks. Whether the game accepts each dvar by that name is not seen. | S |',
 '| 13.3 | A map carries its mode settings | done offline (2026-11-12): Carry in this map writes worldspawn `iw8r_mode_dvars` and `iw8r_br`; the generated map script sets the dvars right after the load script, as the stock Warzone tutorial map sets its own. The converter\'s dvar filter used to drop them; it now passes names the editor marks (fork bee970c0). Checked through a full build: the converted script holds the calls and compiles. | S |',
 '| 13.4 | A small custom map in the Warzone game type | partial (2026-11-12): what the br script needs to start is built: starting spawns and a prematch area (Tools, Game Modes, Insert Warzone start entities: 8 `mp_dm_spawn_start`, 1 `script_struct` `br_prematch_insertion_point`), the switches the stock tutorial map uses for a map without circle, gulag or plane (`level.br_circle_disabled`, `level.usegulag`, `level.skipprematchdropspawn`, `br_gametypes::disablefeature("circle")`; fork f8b4d871 lets the map script make that call), and the map is listed for br in `map.json` (game type detection). A test map builds with every zone 0 failed. Open: loot (a custom map has no loot points), buy stations, vehicles, and whether the game loads `common_br_mp` for a custom map; these need converter work and looks in game. | L |',
 '| 13.5 | Editing the stock Warzone maps | partial (2026-11-12): the stock map tools of L11 run on them (`editzone` on mp_br_tut 3 s, mp_br_quarry 5 s): entities, spawns, added models, hidden props, install as a patch. Open: Verdansk\'s world is 749 streamed zones (the reference view loads one zone), and loot points are compiled-only data (23,446 on Verdansk) that the editor does not show or write yet. | L |',
 '| 13.6 | New game mode scripts (a mode that is more than dvars) | open: research 87 section 5. The engine loads `scripts/mp/gametypes/<name>.gsc`; a name must also be one of the 32 rows of the game type table in `code_post_gfx`. Whether a script inside a map zone is found as a game type, or can replace a stock one, is not known and cannot be settled offline. What works today without it: behaviour in the map\'s own script (it runs under every game type). | L, **R** |',
 '',
]
s = s.replace(a, nl.join(sec) + nl + a)
save(p, s)

p = 'docs/USER_GUIDE.md'
s, nl = load(p)
a = '### Game modes of your map'
assert s.count(a) == 1
sec = [
 '### Game mode presets and Warzone',
 '',
 'Tools > Game Modes lists mode presets. A mode in this game is a game type script plus settings (dvars): Warzone Trios,',
 'Quads, Stimulus Solos and Blood Money are all the script `br` with different settings. A private match applies no',
 'playlist settings by itself, so a preset brings them:',
 '',
 '- **Use for F5**: F5 then starts that game type, and the editor sends the preset\'s settings before the match starts.',
 '- **Carry in this map**: the map itself sets them at every start (worldspawn keys `iw8r_mode_dvars` and `iw8r_br`).',
 '  Use this for a map that is meant for one mode.',
 '- **Save as my preset** keeps your own mode: change the id, title, game type and the `name=value` lines first.',
 '',
 'For a small map of your own in the Warzone game type: pick "Warzone on a small map", press Carry in this map, then',
 'Insert Warzone start entities (eight start spawns and a prematch area at the 2D view centre; move the spawns onto the',
 'floor). The build then lists the map for `br`. Such a map has no circle, no gulag, no plane and no loot. Not seen in',
 'game.',
 '',
]
s = s.replace(a, nl.join(sec) + nl + a)
save(p, s)

p = 'docs/TEST_CHECKLIST.md'
s, nl = load(p)
a = '## In game: an edited stock map (2026-11-12), not seen in game'
assert s.count(a) == 1
sec = [
 '## In game: game modes and a small Warzone map (2026-11-12), not seen in game',
 '',
 '- [ ] Tools > Game Modes. Pick "Warzone on a small map", Carry in this map, Insert Warzone start entities. Move the',
 '      eight spawns onto the floor. Build (F7): the log has "game types detected: ... br" and two lines about the',
 '      mode dvars and the Warzone switches.',
 '- [ ] In the game, start the map with the game type br. Say what happens: (a) the match starts and you spawn,',
 '      (b) it loads and stops with a script error (which one), (c) the map is not offered for br.',
 '- [ ] Tools > Game Modes > "Warzone: Battle Royale Trios" > Use for F5 is meant for the stock Warzone maps: the',
 '      editor sends its settings before it starts the match.',
 '',
]
s = s.replace(a, nl.join(sec) + nl + a)
save(p, s)

p = 'docs/PROGRESS.md'
s, nl = load(p)
a = '  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak'
assert s.count(a) == 1
add = [
 '  - 2026-11-12, WARZONE AND GAME MODES (roadmap L13, research 87): core game_modes.* (presets), devmap_plan',
 '    startMatchCommands with mode dvars, map_script modeDvars / br switches (worldspawn iw8r_mode_dvars, iw8r_br),',
 '    map_gametypes br detection, src/qtapp/main_window_game_modes.cpp (Tools > Game Modes: showGameModes,',
 '    bakeGameMode, insertWarzoneStart), DevmapRunner::setModePreset / modeDvarsSetting. Converter (live, branch',
 '    iw8radiant/scriptables = iw8radiant/build-extraction): f8b4d871 (br_gametypes::disablefeature callable),',
 '    bee970c0 (iw3compat_setdvar passes "iw8r:"-marked names). Test map out/scr-br/mp_scrbr.map.',
 '    Old live converter exes: out/zt_backup/zonetool_<commit>.exe (one per step).',
 '    OPEN for Warzone: loot points (compiled spawnGroupLoot, not shown or written), buy stations and vehicles on a',
 '    custom map, Verdansk streamed zones in the reference view, new game type scripts (13.6, needs the game).',
 '    OPEN for stock maps: added brushes as a carried model (research 88 next steps).',
]
s = s.replace(a, nl.join(add) + nl + a)
save(p, s)
print('ok')
