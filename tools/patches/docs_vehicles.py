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
s = rep(s, nl, '''## 7. Tests
''', '''## 6b. Vehicles on an own map (2026-11-12)

The stock Warzone maps place vehicles with structs in their entity text (no compiled data): `script_struct_mp_atv`,
`_tacrover`, `_jeep`, `_cargotruck`, `_littlebird`, each with `model`, `vehicletype`, `targetname` `<kind>_spawn` and
`script_gameobjectname` `br` (66 / 50 / 36 / 20 / 11 on Quarry and on Verdansk). The battle royale scripts spawn the
vehicle there. On a stock map they are ordinary entities and were already editable.

The drivable vehicle definitions (17: `atango_physics_mp`, `tromeo_physics_mp`, `decho_physics_mp`, ...) are in the
map zones of `mp_br_quarry` / `mp_donetsk`; `common_base_mp` (always loaded) holds 15 others (killstreak and support
vehicles); `common_br_mp` one. A definition does not name its model: script spawns the model by name with the type.

- Converter (fork 94ac6fea): collision JSON `carry_assets` [{zone, type, name}] carries any stock asset with
  everything it uses (the machinery of the stock scriptables); a carried model that an entity names joins the level
  model table. `script_struct_mp_*` classes are accepted.
- Editor (`src/core/stock_vehicles.*`, `data/asset_catalog/iw8_vehicles.tsv`): every entity with a `vehicletype`
  makes the build carry the definition and its model from the definition's stock zone (`lbravo_physics` resolves to
  `lbravo_physics_mp`); entity classes for the five kinds; a warning when the map is not a Warzone map.
- Proof: `tools/vehicle_carry_test.py` (maps without vehicles byte-identical; with one ATV the definition reads back
  equal to the stock one with strings resolved, the model is in the zone and in the level model table, ACTS 0
  failed for the map and techsets zones). Two tyre textures of the ATV come from `common_br_mp` (a Warzone match loads
  it; the converter warns). Carrying the ATV into the stock tutorial map with `editzone`: 107 + 14 assets, 0 failed.

## 7. Tests
''', p)
s = rep(s, nl, '''1. Whether `common_br_mp` loads for an own map in a br match (buy stations and loot items depend on it); vehicles
   (`script_struct_mp_<vehicle>` entities, research 87).
''', '''1. Whether `common_br_mp` loads for an own map in a br match (buy stations, loot items and two ATV textures depend on
   it), and whether the vehicles drive (nothing of this is seen in game).
''', p)
save(p, s)

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
for i, line in enumerate(lines):
    if line.startswith('| 13.4 |'):
        a = 'Open: vehicles, whether the game loads `common_br_mp` for a custom map'
        assert a in line
        lines[i] = line.replace(a, 'Vehicles (2026-11-12, research 89 section 6b): vehicle spawn structs carry the stock vehicle definition and its model from mp_br_quarry (`carry_assets`, fork 94ac6fea; `tools/vehicle_carry_test.py`: equal to stock, 0 failed). Open: whether the game loads `common_br_mp` for a custom map')
save(p, nl.join(lines))

p = 'docs/USER_GUIDE.md'
s, nl = load(p)
s = rep(s, nl, '''them into the map's loot table. For a buy station, place a `scriptable` entity with `scriptable` `br_plunder_box`.
Vehicles are not built yet. Not seen in game.
''', '''them into the map's loot table. For a buy station, place a `scriptable` entity with `scriptable` `br_plunder_box`.
For vehicles, place `script_struct_mp_atv`, `_tacrover`, `_jeep`, `_cargotruck` or `_littlebird` entities (their keys
are filled in); the build copies the stock vehicle and its model into your map. Not seen in game.
''', p)
save(p, s)

p = 'docs/TEST_CHECKLIST.md'
s, nl = load(p)
s = rep(s, nl, '''- [ ] Buy station on your own Warzone map:''', '''- [ ] Vehicles on your own Warzone map: place a `script_struct_mp_atv` and a `script_struct_mp_jeep` on open ground.
      Build: the log says "2 vehicle spawn(s); 4 stock asset(s) carried". In a br match, say whether the vehicles
      are there and drive.
- [ ] Buy station on your own Warzone map:''', p)
save(p, s)

p = 'docs/PROGRESS.md'
s, nl = load(p)
s = rep(s, nl, '''    REPOSITORY: the project is in the private GitHub repo IDARETW/IW8r''', '''    VEHICLES (research 89 section 6b): converter live at 94ac6fea (old exe zonetool_44d21398.exe): collision JSON
    carry_assets; editor core stock_vehicles.* + data/asset_catalog/iw8_vehicles.tsv, entity classes
    script_struct_mp_atv / tacrover / jeep / cargotruck / littlebird. Proof tools/vehicle_carry_test.py.
    REPOSITORY: the project is in the private GitHub repo IDARETW/IW8r''', p)
save(p, s)
print('ok')
