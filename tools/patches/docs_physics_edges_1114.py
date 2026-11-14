import io
import re
import sys

# Docs: the physics bounds fix, the second pass of the field audit (research 124 section 7) and the edge list
# research (125), 2026-11-14. Arguments: the converter commit, core checks, core tests, GUI checks, repo commit before.

ROOT = 'E:/IW8-Radiant/'
ZT, CHECKS, TESTS, GUI, PREV = sys.argv[1:6]


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

- **The physics world of a built map is as large as a stock map's** (research 124). The game freezes a moving body
  that leaves the box its physics world is sized from. Every small stock map stores the same large box (51,200 units
  up, about 31,000 around); a built map's ended about 9,000 units up and 16,000 around its geometry, below and inside
  where killstreak aircraft fly. Converter (x64-zt `''' + ZT + '''`, deployed); offline proof
  `tools/physics_bounds_build_test.py` (25 proofs in the set). Rebuild a map to get it. Not seen in game.
- **The stock comparison now covers collision, map entities, navigation, paths, edges and glass** (research 124
  section 7). One more fault found and fixed (the box above). One real gap found and not yet built: **weapon mounting
  does not work on a built map** (research 125). The game finds a mount spot only from mount edges stored in the map,
  and a built map has none (stock maps hold 578 to 862 edges). Mantling is not affected: without edges the game looks
  for the ledge itself.

''')
patch('docs/ROADMAP.md', '''| 6.8 | Ladders, mantles, water volumes | stock | partial:''',
      '''| 6.8 | Ladders, mantles, water volumes | stock | partial (2026-11-14, research 125: mantling needs no edge data as long as the map's edge query types leave the mantle bit clear, which the converter does; **weapon mounting needs mount edges in the map's edge list and a built map has none, so mounting does not work on a built map: not built yet**; the record layouts and the minimum a converter must write are in the note):''')
s = read('docs/PLAN_REMAINING.md')
nl = '\r\n' if '\r\n' in s else '\n'
a = ('''    map's), and that the killstreak of the report fits the Personal Radar drone, which skips enemies with the
    Ghost-like perk. Only the game can say which it was.''').replace('\n', nl)
assert s.count(a) == 1
s = s.replace(a, a + nl + ('''  - weapon mounting on a built map (6.8, found 2026-11-14 by the field audit, research 124 / 125): OPEN, not built.
    The game takes mount spots only from mount edges in the map's edge list; the converter writes ladder edges only.
    To build: mount edges from the map's own brush edges (the layouts, the octree and the flags are in research
    125), with the rules checked against the stock maps' edges.''').replace('\n', nl))
write('docs/PLAN_REMAINING.md', s)
patch('docs/USER_GUIDE.md', '''- Killstreak visuals: a radar killstreak showed no pings in one playtest.''',
      '''- Weapon mounting (leaning a weapon on cover) does not work on a built map yet: the game needs mount edges that
  the build does not write (found 2026-11-14 by comparing built maps with stock ones; research 125). Mantling works.
- Killstreak visuals: a radar killstreak showed no pings in one playtest.''')
patch('docs/TEST_CHECKLIST.md', '''- [ ] Killstreaks, radar:''', '''- [ ] Killstreak aircraft that fly high or start far out (gunship, cruise missile, advanced UAV, airstrike planes)
      move normally: the map's physics world is now as large as a stock map's (research 124; rebuild first).
- [ ] Killstreaks, radar:''')
patch('docs/HANDOVER.md', '''24 end-to-end proofs''', '''25 end-to-end proofs''')
patch('docs/research/README.md', '''| 124 | [Built zone field audit](124_built_zone_field_audit.md) |''',
      '''| 125 | [The edge list: mantle and mount](125_edge_list_mantle_mount.md) | What mantling, weapon mounting and ladders read from a map's edge list in the Replay exe: mantling falls back to a trace, mounting needs mount edges (a built map has none); query type bits, record layouts, what a converter must write |
| 124 | [Built zone field audit](124_built_zone_field_audit.md) |''')
patch('docs/research/45_nav_water_doors.md', '''### 3.3 Mantle
''', '''### 3.3 Mantle

Update 2026-11-14: research 125 settles the open point below from the Replay exe. Mantling needs no edge data while
the map's edge query types leave the mantle bit clear (the converter does); weapon mounting needs mount edges.
''')


def prog(s):
    nl = '\r\n' if '\r\n' in s else '\n'
    m = re.search(r'  Parking state 2026-11-14 \(after the field audit\):[^\r\n]*', s)
    assert m
    line = m.group(0)
    new = line.replace('(after the field audit): repo at the commit after 38dae11', '(after the physics bounds fix): repo at the commit after ' + PREV)
    new = new.replace('converter live at 5ecf08a0', 'converter live at ' + ZT)
    new = re.sub(r'suites core [0-9,]+ checks / [0-9,]+ tests and GUI [0-9,]+ checks', 'suites core %s checks / %s tests and GUI %s checks' % (CHECKS, TESTS, GUI), new)
    new = new.replace('24 offline proofs pass', '25 offline proofs pass')
    assert new != line and ZT in new and '25 offline' in new
    note = ('''  Field audit, second pass (research 124 section 7): tools/zone_field_audit.py --stock <ACTS output> for col_map,
  map_ents, navmesh, aipaths, edgelist, glass_map, tacgraph (stock exports are not kept: make them with ACTS -a ...
  from mp_shipment, mp_m_speedball, mp_rust into a scratch folder). Fixed: clipmap broadphase box at least the stock
  box (converter ''' + ZT + ''', map_zone.h; tools/patches/zt_broadphase_stock.py; proof
  tools/physics_bounds_build_test.py). OPEN, next: mount edges (research 125 by an Opus agent, read-only IDA): the
  converter writes ladder edges only (edgeListUsedQueryTypes 4); mounting queries bucket 0 of the edge list and has
  no trace fallback; mantle falls back to Mantle_TraceLedgeFace while bit 2 is clear.
''').replace('\n', nl)
    return s[:m.start()] + note + new + s[m.end():]


write('docs/PROGRESS.md', prog(read('docs/PROGRESS.md')))
print('ok')
