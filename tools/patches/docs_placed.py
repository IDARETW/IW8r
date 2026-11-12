import io

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

def rep(s, nl, a, b, p):
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    return s.replace(a, b)

p = 'docs/research/89_warzone_work.md'
s, nl = load(p)
s = rep(s, nl, '''## 6. Tests
''', '''## 6. Buy stations on a stock map (section 8 in code comments)

The placed scriptables of a stock map are `ScriptableMapEnts.mapInstances` (72-byte records: definition, origin,
angles, class name and five name strings, flags). Most instances also have a dynamic entity with a second copy of the
placement, and dynamic entities name their instance by index (Verdansk keeps 208 dynamic entity lists in transient
zones), so the list cannot be shortened or reordered. The spatial tree of the block is a tree of the map's space, not
a list of instances (research 86), so a new record needs no tree change.

An instance whose definition has no dynamic entity is only that record. On the Warzone maps that is the buy station
`br_plunder_box` (120 on Quarry, 120 on Verdansk, 1 on the tutorial map), the ammo boxes of the tutorial map and two
gas tanks on Verdansk.

- Converter (fork 927eb63a), `editzone` `"scriptable_instances": {"move": [{"index", "origin", "angles"}], "add":
  [{"like", "origin", "angles", "server_instanced_parts", "usable_parts", "physics_limit", "physics_limit_detail"}]}`.
  A move rewrites origin and angles of a record. An added record copies the definition, class name and flags of
  instance `like` (its five names are cleared); the instance count, the three server counts and the six physics limits
  rise by the definition's numbers. An instance that a dynamic entity of the zone names is refused with the reason.
- Editor (`src/core/stock_placed.*`): opening a stock map reads the instances; File > Edit Placed Objects makes
  `scriptable` entities (key `iw8r_stock_index`) of those whose definition is verified and has no dynamic entity.
  On a rebuild a changed origin or angles is a move, an entity without a valid index (a pasted copy included) is a new
  object copied from the first stock instance of its definition, and everything that cannot be done is said in the
  Console (deleted objects stay; doors and other objects with physics entries cannot be moved or added).
  `scriptable` entities never enter the entity text of a stock map.

Proof, `tools/warzone_instances_edit_test.py <work>` on mp_br_quarry (13 checks pass): one buy station moved and two
added read back in place, the other 983 instances are unchanged, counts 984 -> 986 and +2 on the server counts, limits
540 / 517 -> 542 / 519, ACTS tests the zone like the stock one, a door index and an index beyond the list are refused.
The edits file the editor writes for mp_br_tut goes through the converter the same way (GUI check, then by hand).

## 7. Tests
''', p)
s = rep(s, nl, '''## 7. Open
''', '''## 8. Open
''', p)
s = rep(s, nl, '''3. New clusters or zones on a stock map (today a new point can only join an existing box).
''', '''3. New clusters or zones on a stock map (today a new point can only join an existing box). Removing a placed
   object, and moving or adding one that has a dynamic entity (doors, extraction sites).
''', p)
save(p, s)

p = 'docs/USER_GUIDE.md'
s, nl = load(p)
s = rep(s, nl, '''   copy, or delete some; the rebuild writes them like any other entity. Buy stations are compiled objects and cannot
   be moved on a stock map yet.
''', '''   copy, or delete some; the rebuild writes them like any other entity.
9. Buy stations: File > Edit Placed Objects turns the buy stations of the map into `scriptable` entities (Quarry and
   Verdansk have 120 each). Move one, or copy one and move the copy for an extra station; the rebuild writes it and the
   Console counts what was moved and added. A station cannot be removed (move it out of the way), and doors and other
   objects with moving parts cannot be edited this way yet.
''', p)
save(p, s)

p = 'docs/TEST_CHECKLIST.md'
s, nl = load(p)
s = rep(s, nl, '''## In game: camera view speed and prefab models (2026-11-11 late), editor only, not seen
''', '''- [ ] Buy stations of a Warzone map: on mp_br_quarry, File > Edit Placed Objects (120 orange points). Move one a
      few metres and copy another to a new place. Rebuild (the Console says "1 placed object(s) moved, 1 added"),
      install, start the map: say whether the station stands at the new place and whether the extra one works.

## In game: camera view speed and prefab models (2026-11-11 late), editor only, not seen
''', p)
save(p, s)

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
for i, line in enumerate(lines):
    if line.startswith('| 13.5 |'):
        a = 'Open: Verdansk\'s world is 749 streamed zones (the reference view loads one zone); new clusters or zones.'
        assert a in line
        lines[i] = line.replace(a, 'Buy stations (2026-11-12, research 89 section 6): File > Edit Placed Objects turns the placed objects without a dynamic entity (the buy station `br_plunder_box`, 120 on Quarry and on Verdansk) into `scriptable` entities; a moved one and a copied one are written back (`editzone` `scriptable_instances`, fork 927eb63a; `tools/warzone_instances_edit_test.py` on Quarry: counts and limits follow, the other 983 instances unchanged, ACTS like stock). Vehicle spawns are ordinary entities and were already editable. Open: Verdansk\'s world is 749 streamed zones (the reference view loads one zone); new clusters or zones; removing a placed object, and editing one that has a dynamic entity (doors).')
save(p, nl.join(lines))

p = 'docs/PROGRESS.md'
s, nl = load(p)
s = rep(s, nl, '''    OPEN for Warzone now: vehicles, whether common_br_mp loads for an own map, zones / clusters on an own map, new
''', '''    PLACED OBJECTS ON STOCK MAPS (research 89 section 6): converter live at 927eb63a (old exe
    zonetool_9e3abdde.exe): editzone "scriptable_instances" {move, add} for instances without a dynamic entity
    (buy stations). Editor: core stock_placed.* (readStockPlacedObjects, placedObjectEdits), File > Edit Placed
    Objects (main_window_stock_edit.cpp editPlacedObjects), prepareStockEdit writes the key; `scriptable` entities
    never enter a stock map's entity text (splitStockDocument). Proof: tools/warzone_instances_edit_test.py.
    OPEN for Warzone now: vehicles, whether common_br_mp loads for an own map, zones / clusters on an own map, new
''', p)
save(p, s)
print('ok')
