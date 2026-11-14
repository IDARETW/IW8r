import io
import sys

# Docs for the airspace fix (research 121, 2026-11-14). Argument: the converter commit.

ROOT = 'E:/IW8-Radiant/'
ZT = sys.argv[1]


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    write(p, s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Killstreak views on a built map: the cause was found and fixed** (research 121). In the playtest of
  2026-11-11 players were invisible from the Chopper Gunner and the UAV's aircraft was invisible. Read in the game's
  code: when a camera is outside the box of a map's occlusion data, the game draws the whole static map but no
  player, vehicle or other moving object. Every stock map's occlusion data reaches at least 131,072 units from the
  map's origin, above and below; a built map's ended at its walls.
  - The earlier attempt (2026-11-12, `iw8r_airspace`) grew a different structure and never reached the occlusion
    data. Writing its offline proof showed that: 0 of 3 points above a built map were covered.
  - A built map now gets the reach of a stock map: 131,072 units above, around and below its geometry. Worldspawn
    `iw8r_airspace "<up> [out]"` changes it; `"0"` gives the old tight box.
  - Converter (x64-zt `''' + ZT + '''`, deployed): the occlusion data covers the map's visibility cell, with and
    without the occlusion bake.
  - New offline proof `tools/airspace_build_test.py` (four builds read back from their zones; 23 proofs in the set)
    and core tests. Not seen in game.

''')

patch('docs/ROADMAP.md', '''Builds now extend that cell 6144 units up and 2048 out (worldspawn iw8r_airspace; 0 turns it off); the world bounds and light grid keep the map's size. A guess until seen in game.''',
      '''Builds extended that cell from 2026-11-12 (worldspawn iw8r_airspace), which did not reach the occlusion data the game asks. Update (2026-11-14, research 121): the cause was read in the Replay exe. A camera outside the box of the map's occlusion data fails the visibility query; the game then draws the whole static map and tests every player and vehicle against a cleared buffer. Stock occlusion data reaches at least 131072 units from the origin on every axis; a built map's ended at its geometry. Fixed: the build gives the cell that reach (above, around and below; iw8r_airspace "<up> [out]", 0 turns it off) and the converter (''' + ZT + ''') makes the occlusion data cover the cell. Offline proof tools/airspace_build_test.py: air points up to 30000 units away are in a cell and see every object, with and without the occlusion bake. The world bounds and light grid keep the map's size. The invisible players and aircraft: fixed offline, NOT SEEN in game. The UAV's missing sweep and pings remain open.''')
patch('docs/ROADMAP.md', '''What is left in this queue (F.7 reverb, F.9 the wide spot, F.10 probes, F.11 game modes, F.14 killstreak visuals) cannot be settled offline''',
      '''What is left in this queue (F.7 reverb, F.9 the wide spot, F.10 probes, F.11 game modes, F.14 the UAV's sweep and pings; the invisible players and aircraft of F.14 have a verified cause and a fix since 2026-11-14, research 121) cannot be settled offline''')
patch('docs/ROADMAP.md', '''every built map is a single visibility cell (extended upward for aircraft, row F.14)''',
      '''every built map is a single visibility cell, and its occlusion data reaches as far as a stock map's (131072 units above, around and below, for aircraft and their cameras: row F.14, research 121)''')
patch('docs/ROADMAP.md', '''level.mapsize and level.mapcorners are set by every built map script (row F.14). | M, **R** |''',
      '''level.mapsize and level.mapcorners are set by every built map script (row F.14). Update (2026-11-14): the airspace is built and proven offline (research 121): the occlusion data of a built map reaches 131072 units above, around and below it, as stock maps' does. | M, **R** |''')
patch('docs/PLAN_REMAINING.md', '''  - killstreak airspace and compass (6.21 / F.14): open; research 75 found no per-map key for it. Game.''',
      '''  - killstreak airspace (6.21 / F.14): done offline (2026-11-14, research 121): the cause of the invisible players and
    aircraft was read in the Replay exe (a camera outside the occlusion data's box), a built map's occlusion data now
    reaches as far as a stock map's, proof `tools/airspace_build_test.py`. Game: the look. The UAV's sweep and pings
    on the minimap stay open (no cause found offline).''')
patch('docs/HANDOVER.md', '''- Items the game must answer before more work makes sense (reverb zones, killstreak airspace, intro cameras) are''',
      '''- Items the game must answer before more work makes sense (reverb zones, the UAV's minimap sweep, intro cameras) are''')
patch('docs/HANDOVER.md', '''22 end-to-end proofs''', '''23 end-to-end proofs''')
patch('docs/MILESTONES.md', '''- Items the game must answer first (reverb zones, killstreak airspace, intro cameras) are listed in ROADMAP.md.''',
      '''- Items the game must answer first (reverb zones, the UAV's minimap sweep, intro cameras) are listed in ROADMAP.md.''')
patch('docs/TEST_CHECKLIST.md', '''- [ ] Killstreaks: the UAV aircraft is visible in the sky, and enemies are visible from the Chopper Gunner view
      (the visibility cell now reaches 6144 units above the map; a guess until seen).''',
      '''- [ ] Killstreaks: the UAV aircraft is visible in the sky, and enemies are visible from the Chopper Gunner view
      (fixed 2026-11-14: the map's occlusion data now reaches as far as a stock map's, research 121; rebuild the
      map first).''')
patch('docs/USER_GUIDE.md', '''- Killstreak visuals: the UAV shows no sweep or pings, and players are invisible in the chopper gunner view.''',
      '''- Killstreak visuals: the UAV shows no sweep or pings. Players were invisible in the Chopper Gunner view and the
  UAV's aircraft was invisible; the cause was found and fixed on 2026-11-14 (rebuild the map; not seen in game yet).
  Worldspawn `iw8r_airspace "<up> [out]"` sets how far the map's occlusion data reaches past its geometry (default
  131072 units, as in stock maps).''')
patch('docs/research/118_occlusion_bake.md', '''map lists the nearest cell (INFERRED need: what the game does for a point with no cell is not known).''',
      '''map lists the nearest cell. What the game does for a camera with no cell is known since research 121: the query
  fails, the static map is drawn in full and no player or vehicle is drawn. The scene box the bake gets now includes
  the map's airspace (131072 units by default), so the air above and around a map is cells too.''')
patch('docs/research/75_killstreak_map_data.md', '''## 5. Not determined

''', '''## 5. Not determined

Update 2026-11-14: research 121 answers the first two items below for the invisible aircraft and players (the box of
the occlusion data, read in the Replay exe), and section 4.1 item 2 is built in that form.

''')
patch('docs/research/README.md', '''| 120 | [White balance](120_white_balance.md) |''', '''| 121 | [The occlusion data's box and the airspace](121_occlusion_box_airspace.md) | What Replay does with a camera outside a map's occlusion data (static map drawn, no players or vehicles), how far stock occlusion data reaches (131072 units), and the airspace a built map gets |
| 120 | [White balance](120_white_balance.md) |''')
patch('docs/PROGRESS.md', '''  - Research 75 (killstreaks): compile_iw8.cpp writes the DPVS cell bounds grown by worldspawn iw8r_airspace
    (default 6144 up, 2048 out); world bounds and the surface tree stay tight.''',
      '''  - Research 75 (killstreaks): compile_iw8.cpp writes the DPVS cell bounds grown by worldspawn iw8r_airspace
    (default 6144 up, 2048 out until 2026-11-14; now 131072 up / around / below, research 121, and the converter
    makes the tome cover the cell); world bounds and the surface tree stay tight.''')
print('ok')
