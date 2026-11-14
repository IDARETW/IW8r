import io

# Docs: plan 2.2 finished (lights, probes, decal groups and displaced terrain in the Umbra view) and research 122
# (the UAV / Personal Radar sweep and pings), 2026-11-14.

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


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **View > Umbra Visibility > Cull by Tome now covers everything the game's visibility query decides.** Besides
  surfaces and props it leaves out the stock map's lamps, reflection probe boxes and decal groups that the game
  leaves out from the camera, and the displaced terrain is culled like every other surface (it kept none of its
  surface numbers before). The status bar counts decal groups too. Hideout from one spot: 28 lamps in view, 15 probe
  boxes and 556 decal batches are left out.
  - When the camera is outside the map's occlusion data the status bar now says what the game does there: it draws
    the whole map and no players or vehicles (research 121).
- **The missing radar pings of the playtest: narrowed, not solved** (research 122). Read in the game's code and
  scripts: nothing a map provides can switch the sweep or the pings off, and the build's minimap set-up gives the
  game exactly the values a stock map gives. The killstreak of the report fits the Personal Radar drone, which flies
  1,500 units above the ground: that put it outside the old occlusion data (invisible; fixed today). That drone never
  pings an enemy who has the Ghost-like perk, and it draws no sweep line in the stock game either.

''')

patch('docs/PLAN_REMAINING.md', '''**2.2 A visibility view in the editor.** Part 1 DONE 2026-11-13 (View > Umbra Visibility: cull by tome for surfaces
and props, cells and portals overlay, status bar counts, saved settings). Left by decision (table "Open points closed by decision"): culling lights, probes and decal
collections; surface ids on displaced terrain.''', '''**2.2 A visibility view in the editor.** DONE. Part 1 2026-11-13 (View > Umbra Visibility: cull by tome for surfaces
and props, cells and portals overlay, status bar counts, saved settings). Part 2 2026-11-14: the stock map's lamps,
reflection probe boxes and decal groups are culled by the same query, and displaced terrain keeps its surface
numbers, so it is culled too. Nothing open.''')
s = read('docs/PLAN_REMAINING.md')
nl = '\r\n' if '\r\n' in s else '\n'
start = s.index('| 2.2 culling lights, probes and decals in the Umbra view; surface ids on displaced terrain |')
end = s.index(nl, start) + len(nl)
s = s[:start] + s[end:]
write('docs/PLAN_REMAINING.md', s)
patch('docs/PLAN_REMAINING.md', '''    reaches as far as a stock map's, proof `tools/airspace_build_test.py`. Game: the look. The UAV's sweep and pings
    on the minimap stay open (no cause found offline).''', '''    reaches as far as a stock map's, proof `tools/airspace_build_test.py`. Game: the look. The missing radar pings
    stay open: research 122 found nothing a map provides that can switch them off (the minimap set-up equals a stock
    map's), and that the killstreak of the report fits the Personal Radar drone, which skips enemies with the
    Ghost-like perk. Only the game can say which it was.''')

patch('docs/ROADMAP.md', '''The invisible players and aircraft: fixed offline, NOT SEEN in game. The UAV's missing sweep and pings remain open.''',
      '''The invisible players and aircraft: fixed offline, NOT SEEN in game. The missing sweep and pings (research 122, 2026-11-14): the game's sweep and ping code and the stock scripts were read; no map data can switch them off, and the build's setminimap arguments equal a stock map's for the same corners. The report fits the Personal Radar drone (helper_drone.gsc), which flies 1500 units above the ground (outside the old occlusion data: invisible), draws no sweep line in the stock game, and never pings an enemy with the Ghost-like perk. Open only for which killstreak and which bot class it was.''')
patch('docs/ROADMAP.md', '''That is the right size for the small and medium maps this tool builds;''',
      '''View > Umbra Visibility culls a stock map's surfaces (displaced terrain included), props, lamps, probe boxes and decal groups by the map's own occlusion data (2026-11-14). That is the right size for the small and medium maps this tool builds;''')
patch('docs/research/README.md', '''| 121 | [The occlusion data's box and the airspace](121_occlusion_box_airspace.md) |''',
      '''| 122 | [The UAV's minimap sweep and enemy pings](122_uav_radar_sweep.md) | What the client's radar sweep and pings read (the radar flag, the minimap rectangle, the map info row), why a built map's minimap set-up cannot switch them off, and the Personal Radar drone's script path |
| 121 | [The occlusion data's box and the airspace](121_occlusion_box_airspace.md) |''')
patch('docs/research/110_umbra_tome.md', '''A ray-cast conservativeness check (plan 2.4) is still to be written.''',
      '''A ray-cast conservativeness check (plan 2.4) is still to be written.

**The view's other classes (2026-11-14).** `umbraViewVisibility` also returns bit sets for primary lights (by
`primaryLights` row), reflection probe instances and decal volume collections, and the viewport leaves out the stock
lamps, probe boxes and decal batches whose bit is clear. The map-wide fallback probe is kept. Shipment lists 22 of its
24 light rows (row 0 is empty, row 1 the sun), 18 of 19 probe instances and 6 of 7 decal collections. Displaced
terrain keeps the surface of each source triangle (`StockDisplacedMesh::triOrigin`), so it is culled as its surface.''')
patch('docs/USER_GUIDE.md', '''- Killstreak visuals: the UAV shows no sweep or pings. Players were invisible in the Chopper Gunner view and the
  UAV's aircraft was invisible; the cause was found and fixed on 2026-11-14 (rebuild the map; not seen in game yet).''',
      '''- Killstreak visuals: a radar killstreak showed no pings in one playtest. Nothing in a map can switch them off
  (research 122); the Personal Radar drone does not ping enemies with the Ghost perk and has no sweep line. Players
  were invisible in the Chopper Gunner view and the radar aircraft was invisible; the cause was found and fixed on
  2026-11-14 (rebuild the map; not seen in game yet).''')
patch('docs/TEST_CHECKLIST.md', '''- [ ] Killstreaks: the UAV aircraft is visible in the sky, and enemies are visible from the Chopper Gunner view''',
      '''- [ ] Killstreaks, radar: with a UAV (not the Personal Radar) enemies without the Ghost perk ping on the minimap
      and the sweep line crosses it; with the Personal Radar the drone is visible about 1,500 units above you
      (research 122 says what each should do).
- [ ] Killstreaks: the UAV aircraft is visible in the sky, and enemies are visible from the Chopper Gunner view''')
print('ok')
