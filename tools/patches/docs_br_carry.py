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


# Research 92 names the owner's local tool folder; keep only the tool's name and version.
rep('docs/research/92_gsc_far_calls.md', [
 ('''`C:\\Users\\user\\Downloads\\iw8 level maker\\gsc-tool.exe`''', '''the gsc-tool binary the build is configured with'''),
])

rep('docs/research/89_warzone_work.md', [
 ('''  boxes, the assassination tablet) is placed **by name** on a map made for the br game type (worldspawn `iw8r_br` or a
  `br_prematch_insertion_point`), and left out with the reason on any other map. This rests on the game loading
  `common_br_mp` for a br match on an own map, which is not known (the exe names that zone only in its shader
  preload list; the level load list is data).''', '''  boxes, the assassination tablet) is accepted on a map made for the br game type (worldspawn `iw8r_br` or a
  `br_prematch_insertion_point`), and left out with the reason on any other map. **Corrected 2026-11-12 (research
  93)**: a custom map never loads `common_br_mp`, in any game type: the extra zone of a map is chosen by its name in
  `mp/levelZoneParents.csv` (11 of 12 rows are Warzone maps), and a map without a row gets `common_core_mp`. So the
  definition is now **carried** from `common_br_mp` into the map zone (Real stock scripted objects, on by default).
  Proof: a copy of `out/scr-br` with one `br_plunder_box` built through `IW8Radiant.exe --compile`: "46 stock asset(s)
  added to the map zone, 1 instance(s) given their definition", ACTS loads the map zone with 0 failed and exports the
  definition `br_plunder_box` from it. One material the definition names (`mo/offhand_vm_stim_body`) is not carried
  (the converter warns).'''),
 ('''1. Whether `common_br_mp` loads for an own map in a br match (buy stations, loot items and two ATV textures depend on
   it), and whether the vehicles drive (nothing of this is seen in game).''', '''1. `common_br_mp` does not load for an own map (research 93): buy stations are carried now; the loot item
   definitions (`brloot_*`, 240 in that zone) the loot tables spawn on loot points are not, so an own map's loot
   points may give no items; two ATV tyre textures are missing. Whether the vehicles drive is not seen in game.
   The map script's "no circle" switch no longer calls `br_gametypes::disablefeature` (research 92: that far call
   cannot link and would break the map at load); `level.br_circle_disabled = 1` stays.'''),
])

rep('docs/USER_GUIDE.md', [
 ('''  `brloot_assassination_tablet`). They live in the game's Warzone file, so the build accepts them only on a map made
  for the Warzone game type (Tools > Game Modes) and places them by name.''', '''  `brloot_assassination_tablet`). They live in the game's Warzone file, which a custom map does not load, so the build
  copies them into the map (Edit > Preferences > Real stock scripted objects, on by default). It accepts them only on
  a map made for the Warzone game type (Tools > Game Modes).'''),
])

rep('docs/features/FEATURES.md', [
 ('''The build places it by name on a map made for the Warzone game type. The build log says "1 scriptable instance(s) name a definition of the Warzone zone". Whether the game loads the Warzone zone for an own map is not known. (not yet seen in game)''',
  '''A custom map never loads the game's Warzone zone (read from the game exe, research 93), so the build copies the buy station's definition, models and materials into the map. Built offline and checked with ACTS. (not yet seen in game)'''),
 ('''Whether the game loads the Warzone zone for an own map is not known.''', '''Loot items on an own map: the loot item definitions live in the Warzone zone, which a custom map does not load (research 93), and are not copied yet.'''),
])

rep('CHANGELOG.md', [
 ('''own Warzone maps
  place `br_plunder_box` by name.''', '''own Warzone maps
  carry `br_plunder_box` into the map (a custom map never loads the Warzone zone: research 93).'''),
 ('''- **Build preflight fix**''', '''- **Fix: "no circle" no longer breaks a Warzone map.** The generated map script called
  `br_gametypes::disablefeature`, a far call that cannot link (research 92) and would drop the match or exit the game
  at load. The map now sets only `level.br_circle_disabled`.
- **Build preflight fix**'''),
])

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
done = False
for i, line in enumerate(lines):
    if line.startswith('| 13.4 |'):
        a = 'common_br_mp` are placed by name on a map made for br'
        assert a in line, line[:200]
        lines[i] = line.replace(a, 'common_br_mp` are carried into the map on a map made for br (research 93, 2026-11-12: a custom map never loads common_br_mp; ACTS-checked offline)')
        done = True
assert done
io.open(p, 'w', encoding='utf-8', newline='').write(nl.join(lines))
print('ok')
