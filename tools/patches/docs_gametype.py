import io


def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')


def rep(p, pairs):
    s, nl = load(p)
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


s, nl = load('docs/research/92_gsc_far_calls.md')
s = s.rstrip() + nl + nl + '''## Built on this (2026-11-12)

- `data/generated/iw8_stock_scripts.tsv` (`tools/build_stock_script_index.py`): the 632 stock GSC files of
  `common_base_mp` and `global_mp`, 391 stored by number (path from gsc-tool's token table) and 241 by name.
- New game type scripts (roadmap 13.6, core `gametype_script.*`): worldspawn `iw8r_gametype_script` names a GSC file
  beside the map; its file name is the game type name. The build refuses it (and says why, with line numbers) when the
  name is not new or not 1 to 11 of a-z 0-9 _, when there is no `main()`, or when a far call goes to a stock file stored
  by name or to no stock file. Converter (x64-zt c5e182f3) compiles `extracted/scripts/mp/gametypes/<name>.gsc` as it is
  into the map zone; `map.json` lists the name. Tools > Game Modes > New game type script writes a template (team kills
  with the Team Deathmatch spawns) that calls only numbered files.
- The "no circle" switch no longer writes the `br_gametypes::disablefeature` call, and the converter's stock call list
  no longer has it (x64-zt e57497bd).
- Proof: core tests `gametype_script_*`; the sample room with the template built through `IW8Radiant.exe --compile`:
  "compiled game type script scripts/mp/gametypes/teamkills.gsc (1225 bytes gscbin)", ACTS loads the map zone with 0
  failed and exports both scripts; the compiled file holds no path string and the numeric ids 1857 (globallogic, 2
  calls), 1851, 1852, 1912 (spawnlogic, 25) and 2398 (utility/game, 12). Whether the lobby offers the name and the
  match runs is not seen in game.
''' + nl
io.open('docs/research/92_gsc_far_calls.md', 'w', encoding='utf-8', newline='').write(s)

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
for i, line in enumerate(lines):
    if line.startswith('| 13.6 |'):
        a = 'Open: a new game type script under a new name (needs a second script file in the map zone and the far-call question of research 87 section 5)'
        assert a in line, line[-300:]
        lines[i] = line.replace(a, 'New game type scripts under a new name (research 92): worldspawn iw8r_gametype_script, far calls checked against the stock script index (numbered files only), compiled into the map zone (x64-zt c5e182f3), listed in map.json; Tools, Game Modes, New game type script writes a template. Built and ACTS-checked offline. Open: what the lobby shows for a new name, and the match itself (game)')
        lines[i] = lines[i].replace('| L, **R** |', '| L, **R** |')
io.open(p, 'w', encoding='utf-8', newline='').write(nl.join(lines))

rep('docs/USER_GUIDE.md', [
 ('''- **Rules** change how the game type plays,''', '''- **New game type script** makes a game type of your own: it writes `gametypes/<name>.gsc` beside the map (a team
  game where kills score up to the score limit) and points worldspawn `iw8r_gametype_script` at it. Edit it, build,
  and start a match with that game type (Use for F5 with the name as game type). Every build checks the script: the
  name must be new (a map cannot replace a stock game type), it needs `main()`, and it may call only stock scripts the
  game stores by number (`scripts\\mp\\globallogic`, `gameobjects`, `gamescore`, `spawnlogic`, `utility\\game`, ...);
  a call into `scripts\\mp\\gametypes\\...` would end the match at load, so the build leaves such a script out and says
  which line.
- **Rules** change how the game type plays,'''),
])

rep('docs/features/FEATURES.md', [
 ('''- A new game type script under a new name is not built. From the game exe (research 91): a map zone cannot replace a stock game type script, but a new name can be started and would be loaded from the map zone. It still needs a second script file in the map zone and calls into stock scripts, whose resolution is not known. Mode rules and behaviour in the map's own script work under every stock game type.
''', '''- New game type scripts: Tools > Game Modes > New game type script writes a team-kills template beside the map (worldspawn `iw8r_gametype_script`); the build checks it (a new name, `main()`, calls only into stock scripts the game stores by number: research 92) and compiles it into the map zone; `map.json` lists the name. Built and ACTS-checked offline; whether the lobby offers it and the match runs is not seen in game.
'''),
 ('''- Warzone: new game type scripts under a new name (row 13.6; mode rules on stock game types are built).''',
  '''- Game types: what the lobby shows for a game type of the map's own (row 13.6; built offline, not seen in game).'''),
])

rep('CHANGELOG.md', [
 ('''- **Mode rules.**''', '''- **New game types.** Tools > Game Modes > New game type script: a game type of your own under a new name, compiled
  into the map zone (converter c5e182f3). Every build checks its far calls against the stock script index (only files
  the game stores by number link: research 92). Built and ACTS-checked offline; not seen in game.
- **Mode rules.**'''),
])
print('ok')
