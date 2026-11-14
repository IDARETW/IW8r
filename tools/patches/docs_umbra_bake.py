import io
import sys

# Docs for the occlusion bake (plan 2.4, research 118). Usage: docs_umbra_bake.py "<the converter's summary line>"
# "<the read-back line>" (both from the real build, so the notes carry measured numbers).

ROOT = 'E:/IW8-Radiant/'
built = sys.argv[1] if len(sys.argv) > 1 else ''
readback = sys.argv[2] if len(sys.argv) > 2 else ''


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Occlusion bake for your own maps (off by default).** Compile > Occlusion bake (worldspawn `iw8r_umbra_bake 1`)
  makes the build give the map real occlusion data: the free space is cut into cells along the map's opaque box
  brushes, the cells are joined by portals, and the game's Umbra then stops drawing what lies in cells it cannot
  reach. Without it the map keeps the data that hides nothing, which is what every build so far has used. Checked
  offline: the data passes the same checks as the stock maps' own, a ray test over sixteen rooms never finds an
  object that a straight line reaches but the bake hides (472 sightings, 0 missed, half the objects hidden), and a
  real build of Cinder Rail loads in the game's loaders. On Cinder Rail it hides little (the world is a few large
  merged surfaces); maps of closed rooms gain most. **Not seen in game: leave it off for a map you need to rely on**
  (research 118).

''')
patch('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- 2026-11-14, plan 2.4 (occlusion bake, opt-in): src/core/umbra_bake.h (header only, namespace umbrabake: occluder
  boxes -> free boxes = cells, portals = shared rectangles both ways, per-cell object lists by real box, objects keep
  scene-sized bounds, a midpoint view tree with one cell per leaf and the nearest cell for solid / outside; a tree
  over budget = cut again with half the cells). Tests umbra_bake_* (UmbraTome parse / validate / query, ray test,
  built-zone read-back through IW8R_TEST_GFXMAP_JSON). Converter x64-zt (worktree, deployed; old exe
  out/zt_backup/zonetool_5bb98fb9.exe): umbra_bake.hpp (copied by tools/patches/zt_umbra_bake.py),
  replayrender::SetUmbraOccluders / UmbraBakeSummary, world key "umbra_occluders" / "umbra_max_cells". Editor:
  compile_iw8.cpp writes the opaque axis-aligned box brushes as umbra_occluders when worldspawn iw8r_umbra_bake = 1;
  Compile panel checkbox compileOcclusionBake. Real build out/agent/occ/mp_cr_occ.map (Cinder Rail copy):
  ''' + built + '''; read back: ''' + readback + '''. Research 118. NOT seen in game; default off.
''')
patch('docs/PLAN_REMAINING.md', '''**2.4 A real occlusion bake for custom maps.**
''', '''**2.4 A real occlusion bake for custom maps.** DONE 2026-11-14 as an opt-in (research 118: cells and portals from
the opaque box brushes, the conservativeness test, a real build read back). Open: non-box occluders and models as
occluders, the per-brush occluder key, gates, the world's merged surfaces (they limit what can be hidden), and the
in-game look.
''')
patch('docs/USER_GUIDE.md', '''- **Compile > Atmosphere**: exponential fog''', '''- **Occlusion bake** (Compile panel, off by default): the build cuts the map's free space into cells along its
  opaque box brushes, and the game then skips what lies in cells the camera cannot see into. It helps maps with
  rooms, corridors and closed streets; an open map gains little. Only brushes that are axis-aligned boxes with opaque
  materials hide anything. It is checked offline only: if something is missing in the game with it on, turn it off
  and build again.
- **Compile > Atmosphere**: exponential fog''')
patch('docs/research/README.md', '''| 117 | [Own map world shading](117_own_map_world_shading.md) |''', '''| 118 | [Occlusion bake](118_occlusion_bake.md) | An Umbra tome with real cells and portals for converted maps: the design, the layout written, the offline checks (reader, ray test), a real build of Cinder Rail |
| 117 | [Own map world shading](117_own_map_world_shading.md) |''')
patch('docs/features/FEATURES.md', '''### Deploy and rollback''', '''- Occlusion bake (Compile panel, off by default): real occlusion cells and portals from the map's opaque box brushes, so the game skips what lies behind walls. Checked with the stock tome reader and a ray test; a real build loads in the game's loaders. (not yet seen in game)

### Deploy and rollback''')
p = ROOT + 'docs/TEST_CHECKLIST.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '## What is not expected to work yet'
assert s.count(a) == 1
add = ('## In game (optional, 2026-11-14): the occlusion bake' + nl + nl +
       'Only if you want the extra speed on a map with rooms. Compile > Occlusion bake on, build, play.' + nl + nl +
       '- [ ] The map loads and nothing is missing from any spot (walk through doors, look along streets, look over walls' + nl +
       '      from high places, spectate).' + nl +
       '- [ ] If anything disappears, turn Occlusion bake off and build again: the map is then as before. Tell me where it' + nl +
       '      happened.' + nl + nl)
s = s.replace(a, add + a)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
