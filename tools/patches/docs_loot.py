import io, re

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

def rep(s, nl, a, b, p):
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    return s.replace(a, b)

# ---- ROADMAP rows 13.4 and 13.5
p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
for i, line in enumerate(lines):
    if line.startswith('| 13.4 |'):
        a = 'Open: loot (a custom map has no loot points), buy stations, vehicles, and whether the game loads `common_br_mp` for a custom map; these need converter work and looks in game.'
        assert a in line
        lines[i] = line.replace(a, 'Loot points (2026-11-12, research 89): `iw8r_loot_point` entities are built into the map\'s loot table (free points, or cache points with `loot_cluster` `cache`; no clusters or zones, as the small stock maps; fork 6e1b1eb5; `tools/warzone_custom_loot_test.py`: read back by ACTS, counts follow the stock rule, 0 failed). Open: buy stations, vehicles, whether the game loads `common_br_mp` for a custom map, and whether the br scripts put loot on points without a zone; these need looks in game.')
    if line.startswith('| 13.5 |'):
        a = 'Open: Verdansk\'s world is 749 streamed zones (the reference view loads one zone), and loot points are compiled-only data (23,446 on Verdansk) that the editor does not show or write yet.'
        assert a in line
        lines[i] = line.replace(a, 'Loot points (2026-11-12, research 89): File > Edit Loot Points turns the map\'s loot points into `iw8r_loot_point` entities; Rebuild Stock Zone writes them back (`editzone` `loot_points`, fork 93b027ba / 5798fb0e): clusters and zones stay as authored, their lists and the four counts are rebuilt by the stock rules, a moved or new point finds its cluster and zone by position. `tools/warzone_loot_edit_test.py`: the stock points passed back give the stock block exactly on mp_br_quarry (2,924 points) and mp_donetsk (23,446); edits follow; ACTS tests the result like the stock zone. A writer fault found on the way is fixed (a fresh rewrite of Quarry lost the scriptable `ks_ac130_mp`). An unedited stock map no longer carries models its own entities name. Open: Verdansk\'s world is 749 streamed zones (the reference view loads one zone); new clusters or zones.')
save(p, nl.join(lines))

# ---- USER_GUIDE
p = 'docs/USER_GUIDE.md'
s, nl = load(p)
s = rep(s, nl, '''2. Map > Edit Compiled Spawns turns the spawn points into entities you can move, add and delete.
''', '''2. File > Edit Compiled Spawns turns the spawn points into entities you can move, add and delete.
''', p)
s = rep(s, nl, '''Not possible yet: new brushes inside a stock map. Not seen in game.
''', '''7. Loot points (Warzone maps): File > Edit Loot Points turns the map's loot points into green `iw8r_loot_point`
   entities (Quarry has 2,924, Verdansk 23,446). Move, delete or copy them; a new point needs only an origin. The
   rebuild writes them back and the Console says how many. A point you move out of its area finds its new cluster and
   zone by position; `loot_cluster` `cache` makes a supply box point. If you undo back to no loot point entity, the
   map keeps its own loot; worldspawn `iw8r_no_loot` `1` is the way to remove all of it.

Not possible yet: new brushes inside a stock map. Not seen in game.
''', p)
s = rep(s, nl, '''floor). The build then lists the map for `br`. Such a map has no circle, no gulag, no plane and no loot. Not seen in
game.
''', '''floor). The build then lists the map for `br`. Such a map has no circle, no gulag and no plane. For loot, place
`iw8r_loot_point` entities where items should lie (`loot_cluster` `cache` for a supply box point); the build writes
them into the map's loot table. Buy stations and vehicles are not built yet. Not seen in game.
''', p)
save(p, s)

# ---- TEST_CHECKLIST
p = 'docs/TEST_CHECKLIST.md'
s, nl = load(p)
s = rep(s, nl, '''- [ ] Tools > Game Modes > "Warzone: Battle Royale Trios" > Use for F5 is meant for the stock Warzone maps: the
      editor sends its settings before it starts the match.
''', '''- [ ] Tools > Game Modes > "Warzone: Battle Royale Trios" > Use for F5 is meant for the stock Warzone maps: the
      editor sends its settings before it starts the match.
- [ ] Loot on your own map: place a few `iw8r_loot_point` entities on the floor (one with `loot_cluster` `cache`),
      build, start with br. Say whether items or a supply box appear there.
''', p)
s = rep(s, nl, '''- [ ] File > Restore Stock Map In Game puts the stock map back (do this before playing online or with others).
''', '''- [ ] File > Restore Stock Map In Game puts the stock map back (do this before playing online or with others).
- [ ] Loot of a Warzone map: File > Open IW8 Stock Map > mp_br_quarry (or mp_br_tut, which is small). File > Edit
      Loot Points: green points appear (Quarry 2,924). Delete the points of one building and add a few in another
      place. Rebuild, install, start the map in Warzone: say whether loot is gone there and appears at the new points.
''', p)
save(p, s)

# ---- PROGRESS
p = 'docs/PROGRESS.md'
s, nl = load(p)
s = rep(s, nl, '''  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak
''', '''  - 2026-11-12 (later), LOOT POINTS (roadmap 13.4 / 13.5, research 89 = the Warzone work record): converter live
    at 6e1b1eb5 (old exes out/zt_backup/zonetool_6270d567 / 93b027ba / 5798fb0e.exe). editzone "loot_points"
    (stock maps; lists and counts rebuilt by the stock rules, exact on Quarry and Verdansk), collision JSON
    "loot_points" (own maps; free / cache points, no clusters or zones), and a writer fix: a fresh rewrite keeps a
    definition whose name stub was used first (Quarry lost scriptable ks_ac130_mp before). Editor: core stock_loot.*
    (reader, iw8r_loot_point entities, edits list), File > Edit Loot Points (main_window_stock_edit.cpp
    editLootPoints), prepareStockEdit writes the list when the document holds loot point entities (none = the map's
    loot stays; worldspawn iw8r_no_loot 1 = empty list), compile_iw8.cpp writes own-map points. Also fixed: an
    unedited stock map carried models its own entities name (stockEditCarryList stockModels; mp_br_tut gave 13).
    Proofs: tools/warzone_loot_edit_test.py, tools/warzone_custom_loot_test.py, tests/stock_loot_tests.cpp,
    src/qtapp/qt_gui_test_loot.cpp. verifyzone with IW8_REWRITE=fresh names differing assets.
    OPEN for Warzone now: buy stations and vehicles on an own map, zones / clusters on an own map, new clusters or
    zones on a stock map, Verdansk streamed zones in the reference view, new game type scripts (13.6).
  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak
''', p)
save(p, s)
print('ok')
