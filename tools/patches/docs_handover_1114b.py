import io

# HANDOVER and PROGRESS after the second half of 2026-11-14: the proofs were run again on the last build, and the
# hand-over text names what was added since it was written.

ROOT = 'E:/IW8-Radiant/'


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


H = 'docs/HANDOVER.md'
patch(H, '''lights, probes, spawns, game type templates and your own game type scripts, mode rules, Warzone objects (loot points,
clusters and zones, buy stations, vehicles), traversals for bots, map scripts. F7 builds the game's zones and checks
them with the game's own loaders (ACTS).''', '''lights, probes, spawns, game type templates and your own game type scripts, mode rules, Warzone objects (loot points,
clusters and zones, buy stations, vehicles), traversals for bots, map scripts. F7 builds the game's zones and checks
them with the game's own loaders (ACTS). Brush > Paint Mesh on Faces lays a paintable mesh on brush surfaces for
blended materials (Patch > Vertex Paint Mode paints it).''')
patch(H, '''the baked look when Bake lightmaps is on, the flat sky light when it is off. On a stock map it draws the map's own
lightmaps, probes, lamps, sun and exposure; brushes you add to a stock map take that map's light grid.''',
      '''the baked look when Bake lightmaps is on, the flat sky light when it is off. On a stock map it draws the map's own
lightmaps, probes, lamps, sun, exposure and white balance (the warm cast of Hideout or Verdansk); brushes you add to
a stock map take that map's light grid. Stock props change detail level and disappear at the distances the game
uses.''')
patch(H, '''- `bin\\release\\iw8r_tests.exe`: about 180,000 checks in 1,308 tests of the core (map format, geometry, builds, bakes,
  decoders, the install logic, the occlusion bake).
- `IW8Radiant.exe --gui-test tests\\data\\iw8_sample_room.map --out <dir>`: about 1,050 checks of the editor itself,
  the web server and its page included.''', '''- `bin\\release\\iw8r_tests.exe`: about 181,500 checks in 1,346 tests of the core (map format, geometry, builds, bakes,
  decoders, the install logic, the occlusion bake).
- `IW8Radiant.exe --gui-test tests\\data\\iw8_sample_room.map --out <dir>`: about 1,080 checks of the editor itself,
  the web server and its page included.''')
patch(H, '''20 end-to-end proofs (the 19 converter ones passed on 2026-11-14 before the last two were added or rerun; the two that build through the editor pass on the current build). Each one edits or builds a real map with the
  live converter, reads the result back with ACTS, compares it with what was asked, and checks that the game's own
  loaders accept the zone (about 12 minutes).''', '''20 end-to-end proofs; all 20 passed on the last build of 2026-11-14 (commit `ecb71fd`). Each one edits or builds a real map with the
  live converter, reads the result back with ACTS, compares it with what was asked, and checks that the game's own
  loaders accept the zone (about 11 minutes).''')
patch(H, '''(more than 110 notes, the latest numbered 118) and `CHANGELOG.md`.''', '''(more than 110 notes, the latest numbered 120) and `CHANGELOG.md`.''')
patch('docs/PROGRESS.md', '''  run_offline_proofs.py: 19 of 19 pass with converter 24d1aec7.''',
      '''  run_offline_proofs.py: 19 of 19 pass with converter 24d1aec7; run again after ecb71fd (the worker texture
  decode): 20 of 20 pass, 630 s in all.''')
print('ok')
