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


rep('docs/research/89_warzone_work.md', [
 ('''## 7. Tests
''', '''## 6d. New loot clusters and zones on a stock map (2026-11-12)

The clusters and zones of a stock map are authored boxes (section 3); a new point could only join one of them. Now
the editor adds boxes of its own:

- Editor: brush entities `iw8r_loot_cluster` and `iw8r_loot_zone` (key `name`, lower-case; empty: the map's first zone
  name). Their brushes give the box. Core `documentLootBoxes` / `lootBoxesJson` (stock_loot.*); they never enter the
  entity text and are not built as added brushes.
- Converter (x64-zt 6980d996): editzone `"loot_boxes": {"clusters": [{mid, half}], "zones": [{name, mid, half}]}`
  appends the boxes after the stock ones, before `loot_points` is applied, so "auto" points find them (the stock
  boxes still win where they overlap: first box rule). A new cluster's `zone` field is the first zone whose box holds
  its middle (1-based, the field the stock clusters carry); a zone name becomes a script string of the zone.
- A zone name is what the loot tables pick the loot by, so a new zone should reuse a name of the map's tables
  (`map_quarry`, `map_hospital`, ... on Quarry; `tutorial_scavenger` on mp_br_tut). Whether a new name with no table
  rows gives loot is not known.
- Proof: `tools/warzone_loot_boxes_test.py` (mp_br_tut and mp_br_quarry: one zone and one cluster added, three new
  points and a cache point; the stock boxes and lists unchanged, the new cluster takes the new zone and lists the
  three points, the new zone lists them and the cluster but not the cache point, counts +4, ACTS tests the zone as
  the stock zone); core test `stock_loot_new_boxes`; GUI section "new loot clusters and zones on a stock map"; the
  editor's edits file run through the converter (`loot_clusters_added` 1, `loot_zones_added` 1). Not seen in game.

## 7. Tests
'''),
 ('''3. New clusters or zones on a stock map (today a new point can only join an existing box). Removing a placed
   object,''', '''3. (New clusters and zones on a stock map: done offline, section 6d.) Removing a placed object,'''),
])

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
for i, line in enumerate(lines):
    if line.startswith('| 13.5 |'):
        a = 'Open: new clusters or zones; removing a placed object'
        assert a in line, line[-300:]
        lines[i] = line.replace(a, 'New loot clusters and zones (2026-11-12, research 89 section 6d): brush entities iw8r_loot_cluster / iw8r_loot_zone, converter editzone loot_boxes; proven offline on mp_br_tut and mp_br_quarry (tools/warzone_loot_boxes_test.py). Open: removing a placed object')
io.open(p, 'w', encoding='utf-8', newline='').write(nl.join(lines))

rep('docs/USER_GUIDE.md', [
 ('''   map keeps its own loot; worldspawn `iw8r_no_loot` `1` is the way to remove all of it.
''', '''   map keeps its own loot; worldspawn `iw8r_no_loot` `1` is the way to remove all of it.
   New loot areas: draw a brush (or several) where the area should be, make it an `iw8r_loot_cluster` (a group of
   points) or an `iw8r_loot_zone` entity (an area the loot tables know by its `name`; reuse one of the map's own zone
   names such as `map_quarry`). Loot points inside it then belong to it. The map's own areas win where they overlap.
'''),
])

rep('docs/features/FEATURES.md', [
 ('''A point that is moved out of its area finds its new cluster and zone by position. (not yet seen in game)
''', '''A point that is moved out of its area finds its new cluster and zone by position. (not yet seen in game)
- New loot areas on a stock map: an `iw8r_loot_cluster` or `iw8r_loot_zone` brush entity (the box of its brushes;
  a zone has a `name` the loot tables know) is added after the map's own clusters and zones, and the loot points
  inside it join it. Proven offline on mp_br_tut and mp_br_quarry. (not yet seen in game)
'''),
 ('''- Warzone: new clusters and zones on stock maps, and new game mode scripts (row 13.6).''',
  '''- Warzone: new game mode scripts (row 13.6).'''),
])

rep('CHANGELOG.md', [
 ('''- **Buy stations.**''', '''- **New loot areas on stock maps.** `iw8r_loot_cluster` and `iw8r_loot_zone` brush entities add loot clusters and
  named loot zones after the map's own (converter editzone `loot_boxes`); loot points inside them join them. Proven
  offline on mp_br_tut and mp_br_quarry. Not seen in game.
- **Buy stations.**'''),
])
print('ok')
