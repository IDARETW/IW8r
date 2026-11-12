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


rep('docs/research/91_gametype_scripts.md', [
 ('''## What the editor can build (most certain first)
''', '''## 5. Built on this (2026-11-12)

Option 1 below: **game mode rules** on top of a stock game type.

- Core `ModeRules` (game_modes.*): max health (1 .. 1000), move speed scale (0.2 .. 3), one weapon instead of the
  loadout, and a clip that refills every half second. Text form `health=50; speed=1.2; weapon=iw8_pi_golf21_mp;
  ammo=infinite` (worldspawn `iw8r_mode_rules`; preset JSON key `rules`).
- Map script (map_script.cpp): `main` starts `iw8r_mode_rules`, which waits for `level waittill("connected", player)`
  and gives each player a thread that waits for `spawned_player` (the stock notify, [G120] e.g. `br.gsc`), waits 0.05 s
  (after the game type's own spawn handling, `common.gsc:172` calls `level.modeonspawnplayer` on spawn), then sets
  `self.maxhealth` / `self.health`, `setmovespeedscale`, `takeallweapons` + `giveweapon(makeweapon(name))` +
  `switchtoweaponimmediate`, and threads the clip refill (`getcurrentweapon`, `weaponclipsize`, `setweaponammoclip`).
  Every builtin is in the converter's list (`iw8_gsc_builtins.inc`) and in no dropped list.
- Editor: Tools > Game Modes has a Rules line; Carry in this map writes `iw8r_mode_rules` (removed again by a preset
  without rules). Rules need the map to carry them (F5 alone sends only dvars).
- Fixed on the way: the build preflight treated `self <method>()` (and `level` / `game`) as a function call and flagged
  every builtin method called on `self`.
- Proof: core `game_mode_rules_text`, `game_mode_rules_preset_json`, `game_mode_rules_map_script`,
  `preflight_methods_on_self_and_level`; GUI check "a preset's rules are carried by the map"; a copy of the sample
  room with all four rules built through the real pipeline (`IW8Radiant.exe --compile`): preflight 0 errors, the
  converter translated the rules unchanged (functions renamed `mw120r_<map>_...` as usual) and gsc-tool compiled the
  map script (15,285 bytes). Not seen in game.

## What the editor can build (most certain first)
'''),
])

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
for i, line in enumerate(lines):
    if line.startswith('| 13.6 |'):
        cells = line.split(' | ')
        assert len(cells) >= 4, cells
        cells[2] = ('partial (2026-11-12, research 91): read from the game exe: a map zone cannot replace a stock game type '
                    'script (the stock copy wins), a new game type name can be started (no table check), and the map script '
                    'runs after the game type main, so it can change how a stock game type plays. Built: game mode rules on top '
                    'of a stock game type (Tools, Game Modes, Rules: health, speed, one weapon, infinite ammo; worldspawn '
                    'iw8r_mode_rules; built through the real pipeline offline). Open: a new game type script under a new name '
                    '(needs a second script file in the map zone and the far-call question of research 87 section 5)')
        lines[i] = ' | '.join(cells)
io.open(p, 'w', encoding='utf-8', newline='').write(nl.join(lines))

rep('docs/USER_GUIDE.md', [
 ('''- **Save as my preset** keeps your own mode: change the id, title, game type and the `name=value` lines first.
''', '''- **Save as my preset** keeps your own mode: change the id, title, game type and the `name=value` lines first.
- **Rules** change how the game type plays, for every player at each spawn: `health=50` (max health), `speed=1.2`
  (move speed), `weapon=iw8_pi_golf21_mp` (only this weapon instead of the loadout), `ammo=infinite`. Separate them
  with `;`. Rules work only when the map carries them (Carry in this map, worldspawn `iw8r_mode_rules`): the map's
  own script applies them after the game type has started.
'''),
])

rep('docs/features/FEATURES.md', [
 ('''- Carry in this map: the map sets its mode settings at every start (worldspawn `iw8r_mode_dvars` and `iw8r_br`). Use it for a map that is meant for one mode. (not yet seen in game)
''', '''- Carry in this map: the map sets its mode settings at every start (worldspawn `iw8r_mode_dvars` and `iw8r_br`). Use it for a map that is meant for one mode. (not yet seen in game)
- Mode rules: a preset can change how its game type plays, for every player at each spawn: max health, move speed, one
  weapon instead of the loadout, infinite ammo (Rules line; worldspawn `iw8r_mode_rules`). The map script runs after
  the game type's own start (read from the game exe, research 91), so the rules apply on top of it. Built through the
  real pipeline offline. (not yet seen in game)
'''),
 ('''- New game mode scripts (a mode that is more than dvars) are not built. The engine loads `scripts/mp/gametypes/<name>.gsc`. Whether a script inside a map zone is found as a game type is not known. Behaviour in the map's own script works under every game type.
''', '''- A new game type script under a new name is not built. From the game exe (research 91): a map zone cannot replace a stock game type script, but a new name can be started and would be loaded from the map zone. It still needs a second script file in the map zone and calls into stock scripts, whose resolution is not known. Mode rules and behaviour in the map's own script work under every stock game type.
'''),
 ('''- Warzone: new game mode scripts (row 13.6).''', '''- Warzone: new game type scripts under a new name (row 13.6; mode rules on stock game types are built).'''),
])

rep('CHANGELOG.md', [
 ('''- **Game mode presets**''', '''- **Mode rules.** Tools > Game Modes > Rules: max health, move speed, one weapon instead of the loadout and
  infinite ammo, applied to every player at each spawn by the map's own script, which runs after the game type's start
  (read from the game exe: docs/research/91). Built through the real pipeline offline; not seen in game.
- **Build preflight fix**: methods called on `self` (`self takeallweapons()`) are no longer flagged as unknown
  functions.
- **Game mode presets**'''),
])
print('ok')
