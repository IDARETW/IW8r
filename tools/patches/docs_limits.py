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

# ---- research 89: the limits rule and Warzone objects
p = 'docs/research/89_warzone_work.md'
s, nl = load(p)
s = rep(s, nl, '''## 5. Tests
''', '''## 5. Scriptable physics limits, and Warzone objects on an own map

`ScriptableMapEnts.physicsLimits` has six numbers (client and server: main rigid bodies, detailed rigid bodies,
motions). Research 86 had a rule only for the core maps ("each limit = the door instances"), and the Warzone maps
did not fit, so no definition seen only on those maps could be built. `tools/scriptable_limits_study.py` tried every
definition property against the stored limits of the 21 exported maps. Exact on all 21:

| Limit | Counts the instances whose definition has |
|---|---|
| main rigid bodies and motions (four of the six numbers) | flag bits 13 and 14 (`flags & 0x6000 == 0x6000`) |
| detailed rigid bodies (two numbers) | flag bits 13 and 15 (`flags & 0xA000 == 0xA000`) |

Doors have all three bits (so the old rule held on core maps); signs and fences count only in the main limits; the
two limits differ on Quarry (540 / 517) and Verdansk (5,138 / 4,127). An equivalent form of the first rule is "bit 13
and a collision event".

Consequences:

- The catalogue (`tools/build_scriptable_catalog.py`) checks the rule on every map, writes `physics_limit` and the new
  last column `physics_limit_detail` from the flags, and marks every observed definition verified: 509 instead of 317.
  No definition that was verified before changed a number. 502 definitions can now be carried from a stock map, and
  `scriptable_auto_ascender` joins `hardpoint_chevron` as placeable by name.
- Converter (fork 9e3abdde): `physics_limit_detail` per instance (default: the value of `physics_limit`), written
  into the two detailed numbers. The editor writes the key only when it differs, so earlier builds stay byte-identical.
- Warzone objects: a verified definition of `common_br_mp` (the buy station `br_plunder_box`, `brloot_rock`, two ammo
  boxes, the assassination tablet) is placed **by name** on a map made for the br game type (worldspawn `iw8r_br` or a
  `br_prematch_insertion_point`), and left out with the reason on any other map. This rests on the game loading
  `common_br_mp` for a br match on an own map, which is not known (the exe names that zone only in its shader
  preload list; the level load list is data).
- Extraction sites (`brloot_plunder_extraction_site_01`) live in stock map zones and are carried like doors; the
  converter warns that four of their assets (map menu icons, one effect, two models) are only in `common_br_mp`.

Proofs: `tools/scriptable_limits_test.py` (main-only, both and no limit: the six numbers read back as 2 / 1; the buy
station reads back as a name reference; 0 failed), `tools/scriptable_carry_test.py` with a sign, an extraction site
and a pot plant (each definition read back equals the stock one), `tools/scriptable_offline_test.py` (maps without
scriptables byte-identical), core test `compile_iw8_scriptable_br_zone_and_limits`.

## 6. Tests
''', p)
s = rep(s, nl, '''## 6. Open

1. Buy stations and vehicles on a custom map; whether `common_br_mp` loads for one.
''', '''## 7. Open

1. Whether `common_br_mp` loads for an own map in a br match (buy stations and loot items depend on it); vehicles
   (`script_struct_mp_<vehicle>` entities, research 87).
''', p)
save(p, s)

# ---- USER_GUIDE
p = 'docs/USER_GUIDE.md'
s, nl = load(p)
s = rep(s, nl, '''- **Always loaded**: `hardpoint_chevron`, the marker of a Hardpoint zone. It is placed by name and always builds.
- **From a stock map**: doors, traffic cones, cars, tyres, radios and other world objects (317 definitions). Their
''', '''- **Always loaded**: `hardpoint_chevron`, the marker of a Hardpoint zone, and `scriptable_auto_ascender`. They are
  placed by name and always build.
- **Warzone objects**: the buy station `br_plunder_box` (also `brloot_rock`, `brloot_ammo_762`, `brloot_ammo_919`,
  `brloot_assassination_tablet`). They live in the game's Warzone file, so the build accepts them only on a map made
  for the Warzone game type (Tools > Game Modes) and places them by name.
- **From a stock map**: doors, traffic cones, cars, tyres, radios, signs, fences and other world objects (502
  definitions). Their
''', p)
s = rep(s, nl, '''them into the map's loot table. Buy stations and vehicles are not built yet. Not seen in game.
''', '''them into the map's loot table. For a buy station, place a `scriptable` entity with `scriptable` `br_plunder_box`.
Vehicles are not built yet. Not seen in game.
''', p)
save(p, s)

# ---- TEST_CHECKLIST
p = 'docs/TEST_CHECKLIST.md'
s, nl = load(p)
s = rep(s, nl, '''- [ ] Loot on your own map: place a few `iw8r_loot_point` entities on the floor (one with `loot_cluster` `cache`),
      build, start with br. Say whether items or a supply box appear there.
''', '''- [ ] Loot on your own map: place a few `iw8r_loot_point` entities on the floor (one with `loot_cluster` `cache`),
      build, start with br. Say whether items or a supply box appear there.
- [ ] Buy station on your own Warzone map: a `scriptable` entity with `scriptable` `br_plunder_box`. Build: the log
      says "1 scriptable instance(s) name a definition of the Warzone zone". In a br match, say whether the station
      stands there and opens. If the map fails to load with it, that is the answer to "does the game load the
      Warzone file for an own map": remove the entity and tell me.
''', p)
save(p, s)

# ---- ROADMAP 13.4
p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
for i, line in enumerate(lines):
    if line.startswith('| 13.4 |'):
        a = 'Open: buy stations, vehicles, whether the game loads `common_br_mp` for a custom map, and whether the br scripts put loot on points without a zone; these need looks in game.'
        assert a in line
        lines[i] = line.replace(a, 'Buy stations (2026-11-12, research 89 section 5): the rule for the scriptable physics limits was found (two definition flag pairs, exact on all 21 exported stock maps; fork 9e3abdde), which makes every observed definition buildable (509, was 317); `br_plunder_box` and four other definitions of `common_br_mp` are placed by name on a map made for br (`tools/scriptable_limits_test.py`). Open: vehicles, whether the game loads `common_br_mp` for a custom map, and whether the br scripts put loot on points without a zone; these need looks in game.')
save(p, nl.join(lines))

# ---- PROGRESS
p = 'docs/PROGRESS.md'
s, nl = load(p)
s = rep(s, nl, '''    OPEN for Warzone now: buy stations and vehicles on an own map, zones / clusters on an own map, new clusters or
    zones on a stock map, Verdansk streamed zones in the reference view, new game type scripts (13.6).
''', '''    PHYSICS LIMITS (research 89 section 5): ScriptablePhysicsLimits = instances by definition flags (main and
    motion: bits 13+14; detailed: bits 13+15), exact on all 21 exported maps (tools/scriptable_limits_study.py).
    Converter live at 9e3abdde (old exe zonetool_6e1b1eb5.exe): "physics_limit_detail" per instance. Catalogue
    rebuilt: 509 verified (was 317), new last column physics_limit_detail; ScriptableDefinition::physicsLimitDetail,
    brZone(). compile_iw8.cpp: a common_br_mp definition (br_plunder_box, ...) is placed by name on a br map
    (worldspawn iw8r_br or a br_prematch_insertion_point), left out elsewhere. Proofs: tools/scriptable_limits_test.py,
    scriptable_carry_test.py with a sign / extraction site / pot plant, scriptable_offline_test.py.
    OPEN for Warzone now: vehicles, whether common_br_mp loads for an own map, zones / clusters on an own map, new
    clusters or zones on a stock map, Verdansk streamed zones in the reference view, new game type scripts (13.6).
''', p)
save(p, s)
print('ok')
