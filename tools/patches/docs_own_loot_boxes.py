import io


def rep(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


rep('docs/research/89_warzone_work.md', [
 ('''  editor's edits file run through the converter (`loot_clusters_added` 1, `loot_zones_added` 1). Not seen in game.
''', '''  editor's edits file run through the converter (`loot_clusters_added` 1, `loot_zones_added` 1). Not seen in game.
- Own maps (x64-zt 66abea4e): the same entities become the collision JSON `loot_boxes`; the converter gives every
  point the first cluster and zone whose box holds it, and writes the clusters (40-byte records: zone, pointCount,
  points, bounds) and zones (56 bytes) with their lists by the stock rules (section 3). Without boxes the single zone
  of worldspawn `iw8r_loot_zone` stays. Proof: `tools/warzone_custom_loot_boxes_test.py out/scr-br/iw8build/mp_scrbr/extraction
  mp_scrbr <work>` (two zones, two clusters, six points incl. a cache point and one outside: every cluster, zone and
  list as wanted, names read back, ACTS 0 failed); `tools/warzone_custom_loot_test.py` still passes (a map without
  loot byte-identical); core test `compile_iw8_loot_points`.
'''),
 ('''2. Zones and clusters on a custom map (one zone box over the map would match the tutorial maps).
''', '''2. (Zones and clusters on a custom map: done offline, section 6d.)
'''),
])
rep('docs/features/FEATURES.md', [
 ('''- Worldspawn `iw8r_no_loot` set to 1 removes all loot from the map.
''', '''- Several loot areas on your own map: `iw8r_loot_cluster` and `iw8r_loot_zone` brush entities (as on stock maps)
  give each point the first cluster and zone whose box holds it. (tested offline; not yet seen in game)
- Worldspawn `iw8r_no_loot` set to 1 removes all loot from the map.
'''),
])
rep('CHANGELOG.md', [
 ('''  offline on mp_br_tut and mp_br_quarry. Not seen in game.
- **Buy stations.**''', '''  offline on mp_br_tut and mp_br_quarry. The same entities work on your own maps (several clusters and named
  zones instead of one zone over every point). Not seen in game.
- **Buy stations.**'''),
])
print('ok')
