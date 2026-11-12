import io, re

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

p = 'docs/research/79_custom_map_game_modes.md'
s, nl = load(p)
assert s.count('Compile > Map Info > Game modes') == 2
s = s.replace('Compile > Map Info > Game modes', 'Compile dock > Map Info... > Game modes')
save(p, s)

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
hit = [i for i, l in enumerate(lines) if l.startswith('| F.11 |')]
assert len(hit) == 1
cells = lines[hit[0]].split(' | ')
assert len(cells) == 4, len(cells)
status = ('cause found and fix written, client build needed (2026-11-11, research 79, which replaces research 74). '
          'Cause, read in the source of the client that actually runs (DW_MapAvailability.cpp AppendCustomMaps): every custom map gets a copy of Shipment\'s row of the native map table with its game type list set to the fixed text "war", and map.json was read for the title and the ladder spacing only. '
          'Fix, client: the same file now reads "gameTypes" from map.json (lower-case names with spaces) and writes that list into the row; without the key nothing changes. Applied to the client source and compile-checked; NOT built (the client build is blocked by the client project\'s state) and not run. '
          'Fix, editor: after every build the package\'s map.json gets "gameTypes" with the modes the map has spawns and objects for (war, dm, dom, sd, ctf; core map_gametypes, 3 tests), the build log has a "game types:" line, and worldspawn iw8r_gametypes (Compile dock, Map Info, Game modes) names the list by hand for other modes. '
          'Checked on the check map: its map.json now has "gameTypes": "war". Whether a listed mode then plays is NOT SEEN in game')
assert '|' not in status
lines[hit[0]] = ' | '.join(cells[:3]) + ' | ' + status + ' |'
save(p, nl.join(lines))

p = 'docs/TEST_CHECKLIST.md'
s, nl = load(p)
a = '## In game: fixes after the second playtest (2026-11-11 evening), not seen in game'
assert s.count(a) == 1
sec = [
 '## In game: game modes of a custom map (2026-11-11), needs the new client build, not seen in game',
 '',
 '- [ ] In the editor first: build any map. The Compile dock log has a line `game types: ...`, and the package\'s',
 '      `map.json` has a `"gameTypes"` line.',
 '- [ ] With a client built from the changed `DW_MapAvailability.cpp`: a map with only TDM spawns still offers Team',
 '      Deathmatch only.',
 '- [ ] Add Primitives > Game Modes > Spawn Set: Domination and Domination Flags (3), build and deploy. The log says',
 '      `game types detected: war dom`. The lobby offers Domination for the map, and the three flags can be captured.',
 '- [ ] Compile dock > Map Info... > Game modes = `war conf`: the lobby offers Kill Confirmed. Say whether it plays.',
 '',
]
s = s.replace(a, nl.join(sec) + nl + a)
save(p, s)

p = 'docs/USER_GUIDE.md'
s, nl = load(p)
a = '### F5 from a closed game'
assert s.count(a) == 1
sec = [
 '### Game modes of your map',
 '',
 'The game offers a custom map in the modes that its `map.json` lists. The editor writes that list after every',
 'build. A mode is listed when the map has the spawns and objects for it:',
 '',
 '| Mode | The map needs |',
 '|---|---|',
 '| Team Deathmatch (`war`) | 2 `mp_tdm_spawn` and one start for each team |',
 '| Free-for-all (`dm`) | 2 `mp_dm_spawn` |',
 '| Domination (`dom`) | Domination spawns, both starts and the three flags |',
 '| Search and Destroy (`sd`) | attacker and defender spawns, two bomb sites, the bomb and its pickup trigger |',
 '| Capture the Flag (`ctf`) | CTF spawns, both starts, and flag, trigger and zone for both teams |',
 '',
 'Primitives > Game Modes inserts each of these sets. The build log shows the result in a line that starts with',
 '`game types:`. To list other modes yourself, open Map Info... in the Compile dock and fill in **Game modes**, for',
 'example `war conf`. The editor cannot check modes it does not know.',
 '',
 'This needs a game client that reads the list. A client without it offers Team Deathmatch only. Not seen in game yet.',
 '',
]
s = s.replace(a, nl.join(sec) + nl + a)
save(p, s)

p = 'docs/PROGRESS.md'
s, nl = load(p)
a = '  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak'
assert s.count(a) == 1
add = [
 '  - GAME MODES (F.11, research 79): the running client hard-coded "war" per custom map row',
 '    (DW_MapAvailability.cpp AppendCustomMaps). Client now reads map.json "gameTypes" (applied in E:\\DW\\IW8, diff in',
 '    docs/research/79_client, compile-checked, not built). Editor: core map_gametypes (detection + iw8r_gametypes',
 '    override + withGameTypes), CompilePanel::writePackageGameTypes after every finished run, Map Info field.',
 '    Tests: core 1074 / 0.',
]
s = s.replace(a, nl.join(add) + nl + a)
save(p, s)
print('ok')
