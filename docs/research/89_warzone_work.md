# 89. Warzone: working record (game modes, small maps, loot points)

Date: 2026-11-12. Owner decision (asked which large features the finished product must include): "Stock map editing,
warzone-specific editing and map/mode creation". Research 87 is the survey; research 88 is the stock map half. This
file records what was built for Warzone, how each part was checked, and what is still open.

Nothing in this file has been seen in game.

## 1. Game modes (roadmap 13.1 to 13.3)

A mode is a game type script plus settings (dvars). The four Warzone playlists are all the script `br`.

- Core `src/core/game_modes.*`: 12 built-in presets (six core game types, four Warzone variants, two for small maps)
  and user presets (`<id>.iw8mode.json` in the app data folder `game_modes`). The Warzone presets hold the dvars of the
  private server's playlist catalogue entry for entry.
- Tools > Game Modes: Use for F5 (the editor sends `set <dvar> <value>` lines before it starts the match), Carry in
  this map (worldspawn `iw8r_mode_dvars`, `iw8r_br`; the generated map script sets them right after the load script).
- Converter: `iw3compat_setdvar` passes names the editor marks with `iw8r:`, letter case kept (fork bee970c0,
  6270d567); the map script may call `br_gametypes::disablefeature` (f8b4d871).

## 2. A small map in the Warzone game type (roadmap 13.4)

Built: eight `mp_dm_spawn_start`, one `script_struct` `br_prematch_insertion_point` (Tools > Game Modes > Insert
Warzone start entities), the switches the stock tutorial map uses for a map without circle, gulag or plane, the map
listed for `br` in `map.json`, and now loot points (section 4). Test map `out/scr-br/mp_scrbr.map`.

Open: buy stations and vehicles (scriptables that live in `common_br_mp`; whether the game loads that zone for a custom
map is not known), and everything only the game can show (does the match start, does loot appear).

## 3. The loot block of a map

`MapEnts.spawnGroupLoot` (research 87 section 2.4) is compiled data, not entity text:

| Part | Record | Meaning |
|---|---|---|
| points | 28 bytes: origin, angles, `u16 cluster`, `u16 zone` | a place where loot may appear |
| clusters | `u16 zone`, point list, box | authored boxes that group points |
| zones | name (script string), point list, cluster list, box | authored named boxes (`map_quarry`, `map_dam`, ...) |

Cluster values of a point: an index, `0xFFFD` (a free point outside every cluster), `0xFFFB` (a loot cache point),
`0xFFFF` (one point of mp_br_tut). Zone value: 0 = none, else the zone number (1-based).

Rules, checked with `tools/warzone_loot_rules.py` and the scripts quoted in this session on mp_br_tut (5 points),
mp_bm_tut (1), mp_backlot2 (7), mp_br_quarry (2,924) and mp_donetsk (Verdansk, 23,446):

1. The boxes of clusters and zones are authored volumes: they are not the bounds of their points.
2. A cluster lists exactly the points that name it, ascending.
3. A zone lists exactly the points that name it **and are not cache points**. Stock cache points name a zone they are
   not inside (Quarry 81, Verdansk 606, the tutorial map 1); with that rule every zone list of all five maps is
   reproduced exactly.
4. A zone lists the clusters whose first point names it.
5. Every clustered point is inside its cluster's box; every zoned point that is not a cache point is inside its
   zone's box; "the first box that holds the point" gives the stock cluster of every clustered point and the stock
   zone of every such zoned point (Quarry: 2,480 of 2,480 and 2,428 of 2,428).
6. `ScriptableMapEnts.lootInstanceCount` equals the point count, and `serverInstanceCount`, `serverFixedPartCount`
   and `serverUsablePartCount` include it (all 21 exported maps). `ffMemCost` is 1240 on every stock map.
7. Small maps differ in shape: mp_backlot2 has 3 clusters and no zone, the two tutorial maps have one zone and no
   cluster.

## 4. Editing loot points

### Stock maps (converter `editzone`, fork 93b027ba and 5798fb0e)

`"loot_points": [{"origin": [x, y, z], "angles": [p, y, r], "cluster": n | "free" | "cache" | "auto",
"zone": n | "auto"}]` in the edits file replaces the map's points. Clusters and zones stay as authored; their lists
are rebuilt by rules 2 to 4; `"auto"` applies rule 5; the four counts of rule 6 move by the difference.

Editor (`src/core/stock_loot.*`, `src/qtapp/main_window_stock_edit.cpp`):

- Opening a stock map reads the loot block from the same ACTS export as the entities (values or raw bytes).
- File > Edit Loot Points turns the points into entities of class `iw8r_loot_point` (keys `loot_cluster`, `loot_zone`,
  `loot_zone_name` for information).
- File > Rebuild Stock Zone writes every loot point entity of the document into `loot_points`. A point moved out of
  its cluster's or zone's box, or with a number the map does not have, is written as `"auto"` (the Console says how
  many). A cache point keeps its zone. Loot point entities never enter the entity text.
- A document without loot point entities leaves the map's loot as it is; only worldspawn `iw8r_no_loot 1` writes an
  empty list. (So Undo after Edit Loot Points cannot remove the loot of a map by accident.)

Proof, `tools/warzone_loot_edit_test.py <work> [--zone ...]` (19 checks each, all pass):

| | mp_br_quarry | mp_donetsk |
|---|---|---|
| stock points passed back unchanged | points, every cluster list, every zone point list, every zone cluster list, boxes, names and counts equal the stock block | the same, 23,446 points |
| 10 deleted, 1 moved, 2 added | lists follow, the far point is free, the near point gets the zone of its neighbour, counts move by -8 | the same |
| ACTS loader test | same per-asset results as the stock zone | the same |
| install patch | 4.1 MB | 81 MB |

### A writer fault found on the way (fork 93b027ba)

A fresh rewrite of mp_br_quarry lost the scriptable `ks_ac130_mp` and three effects it uses: the stock zone holds
that name twice, as a name stub used early and as the full definition later, and the writer pointed the definition at
the stub. The writer now writes a definition whose stub came first. `IW8_REWRITE=fresh zonetool verifyzone` reports
"the written zone loads with the same assets" for mp_br_quarry, mp_shipment and mp_br_tut, and names differing assets
when it fails. Every edit of Quarry or Verdansk made before this fix lacked that killstreak definition.

### Custom maps (fork 6e1b1eb5)

`iw8r_loot_point` entities of an own map become `loot_points` of the collision JSON
(`{"origin", "angles", "cache"}`), and the converter writes them as `spawnGroupLoot` with no cluster and no zone:
free points, or cache points for `loot_cluster` `cache`. The counts follow rule 6.

Proof, `tools/warzone_custom_loot_test.py <extraction> <map id> <work>` on `out/scr-br` (all pass): a map without
loot points is byte-identical between the old and the new converter; with five points ACTS reads them back (origin,
angles, free or cache), the loot count is 5, the three server counts rise by 5, every other field of `map_ents` is
unchanged, and the zone loads with 0 failed.

2026-11-12 later: the build also writes one loot zone over the points (fork c34c7188, collision JSON `loot_zone`, editor
worldspawn `iw8r_loot_zone`, default `map_quarry`), because the loot tables pick the loot set by the zone name: points
have zone 1, the zone lists every point except the cache points, its box holds them all
(`tools/warzone_custom_loot_test.py`, 0 failed).

Not known: whether the br scripts put loot on points of a map that has no zone and no cluster (stock maps always have
one of the two), and whether the loot item definitions are loaded for a custom map. `ffMemCost` is written as 216
(the converter's value for any scriptable data), stock is 1240.

## 5. Scriptable physics limits, and Warzone objects on an own map

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
  boxes, the assassination tablet) is accepted on a map made for the br game type (worldspawn `iw8r_br` or a
  `br_prematch_insertion_point`), and left out with the reason on any other map. **Corrected 2026-11-12 (research
  93)**: a custom map never loads `common_br_mp`, in any game type: the extra zone of a map is chosen by its name in
  `mp/levelZoneParents.csv` (11 of 12 rows are Warzone maps), and a map without a row gets `common_core_mp`. So the
  definition is now **carried** from `common_br_mp` into the map zone (Real stock scripted objects, on by default).
  Proof: a copy of `out/scr-br` with one `br_plunder_box` built through `IW8Radiant.exe --compile`: "46 stock asset(s)
  added to the map zone, 1 instance(s) given their definition", ACTS loads the map zone with 0 failed and exports the
  definition `br_plunder_box` from it. One material the definition names (`mo/offhand_vm_stim_body`) is not carried
  (the converter warns).
- Extraction sites (`brloot_plunder_extraction_site_01`) live in stock map zones and are carried like doors; the
  converter warns that four of their assets (map menu icons, one effect, two models) are only in `common_br_mp`.

Proofs: `tools/scriptable_limits_test.py` (main-only, both and no limit: the six numbers read back as 2 / 1; the buy
station reads back as a name reference; 0 failed), `tools/scriptable_carry_test.py` with a sign, an extraction site
and a pot plant (each definition read back equals the stock one), `tools/scriptable_offline_test.py` (maps without
scriptables byte-identical), core test `compile_iw8_scriptable_br_zone_and_limits`.

## 6. Buy stations on a stock map (the converter source calls this "section 8")

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

## 6b. Vehicles on an own map (2026-11-12)

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

## 6c. The streamed world of Verdansk and Quarry in the reference view (2026-11-12)

A surface of a big map's `gfx_map` names its transient zone (`transientZone` 0 .. 303 on Verdansk); every zone whose
buffers the main zone does not hold is streamed from `<map>_<NNNNN>_tr.ff` (checked: surface zone 30 is in
`mp_donetsk_00030_tr`, whose `gfx_map_trzone` has `transientZoneIndex` 30; the main zone's empty records 587 .. 867
are something else). `surfaceBounds` (56 bytes per surface, Bounds first) places each tile. ACTS writes a small array
as decoded `values` instead of hex `bytes` (5 of 12 tiles tried); the reader takes both.

- Core `readStreamedWorldZones` / `streamedZonesNear` (stock_world.*); `readStockWorld` also reads tiles exported to
  `<cache>/<zone>/streamed/<tile>`.
- Editor: File > Load Streamed World Around Camera exports the tiles within `stock/streamRadius` (default 12000 units,
  at most `stock/streamLimit` = 48) of the camera, one ACTS run each, keeps them in the world cache and redraws the
  reference.
- Proof: `IW8R_STREAM_AT="0 0 8000 12" IW8Radiant.exe --world-cache-test mp_donetsk` (12 tiles around the centre:
  the reference grows from 9,663 to 285,919 triangles; 258 streamed zones listed), core test
  `stock_world_streamed_zones`.

## 6d. New loot clusters and zones on a stock map (2026-11-12)

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
- Own maps (x64-zt 66abea4e): the same entities become the collision JSON `loot_boxes`; the converter gives every
  point the first cluster and zone whose box holds it, and writes the clusters (40-byte records: zone, pointCount,
  points, bounds) and zones (56 bytes) with their lists by the stock rules (section 3). Without boxes the single zone
  of worldspawn `iw8r_loot_zone` stays. Proof: `tools/warzone_custom_loot_boxes_test.py out/scr-br/iw8build/mp_scrbr/extraction
  mp_scrbr <work>` (two zones, two clusters, six points incl. a cache point and one outside: every cluster, zone and
  list as wanted, names read back, ACTS 0 failed); `tools/warzone_custom_loot_test.py` still passes (a map without
  loot byte-identical); core test `compile_iw8_loot_points`.

## 7. Tests

- Core: `tests/stock_loot_tests.cpp` (reader for both export forms, entities and edits, the edits file),
  `compile_iw8_loot_points` in `tests/compile_iw8_tests.cpp`, `tests/game_modes_tests.cpp`.
- GUI: `src/qtapp/qt_gui_test_loot.cpp` (mp_br_tut through ACTS: read, Edit Loot Points, edit, the edits file, Undo).

## 8. Open

1. `common_br_mp` does not load for an own map (research 93): buy stations and the loot item definitions of
   the map's loot table are carried now (research 93, "Built on this"); two ATV tyre textures are missing. Whether the vehicles drive is not seen in game.
   The map script's "no circle" switch no longer calls `br_gametypes::disablefeature` (research 92: that far call
   cannot link and would break the map at load); `level.br_circle_disabled = 1` stays.
2. (Zones and clusters on a custom map: done offline, section 6d.)
3. (New clusters and zones on a stock map: done offline, section 6d.) (Removing a placed object without a dynamic entity:
   done offline 2026-11-12, x64-zt aacfad65: editzone `scriptable_instances.remove` drops the records and renumbers the
   dynamic entities that name later instances; the 64 reserved physics entries name 500000, no instance, and stay; the
   spatial tree holds split planes only. `tools/warzone_instances_remove_test.py` on Quarry: two of 120 buy stations
   removed, counts and the six physics limits -2, all 864 renumbered dynamic entities still name the same instance, ACTS
   as stock, an instance with a dynamic entity refused.) Moving or adding one that has a dynamic entity (doors,
   extraction sites) is open. Found (tools/dynent_pose_rules.py, mp_backlot2, mp_shipment, mp_br_tut, mp_br_quarry):
   the dynamic entity's initialPose origin equals the instance origin and its quaternion is AnglesToQuat of the
   instance angles for every door-like entry (864 of 868 on Quarry; the other 4 are physics entries naming buy
   stations). What blocks a move is the precomputed spatial population: entries are chained into buckets through
   spatialNode.spatialNodeNext with a spatialPartitionIndex (0 .. about 150 on Quarry), and which tree defines those
   partitions: MapEnts.dynEntSpatialPopulation.tree (Quarry: 173 nodes, 87 leaves, 173 buckets; node i: axis 3 = leaf,
   else origin[axis] < dist -> i + 1, else rightChild). 89 entries are in the no-spatial list (index 0,
   dynEntNoSpatialCount 89); of the other 843, walking the tree from the origin gives the stored index for 523; the
   other 320 are not on the walked path at all. 319 of them are the doors (priority DYNENT_PRIORITY_SPARSE_OCCLUDER;
   16 distinct values 6 .. 36 on Quarry, no insertion radius reproduces them): for a sparse occluder the index names
   another structure, not this tree. The ordinary entries (DYNENT_RADIAL_PRIORITY_LEVEL_LOW: signs, fences, props)
   follow the walk, 523 of 524, and the 46 bucket chains of DynEntityList.dynEntSpatialPopulation (partitionIds[b] =
   the partition of bucket b) list exactly the ordinary entries of each partition in descending entry order, the tail
   the last (all 46 on Quarry). Built (x64-zt, "move" of an instance with ordinary dynamic entities): new pose, the
   walked partition, unlink / relink in descending order with the tails; refused for sparse occluders, transient
   entries, an entry placed apart from its instance (the 4 buy-station physics entries of Quarry) and a partition
   without a bucket. Proof tools/dynent_move_test.py on Quarry (a short and a far move: pose, partition, all chains
   exact, everything else unchanged, ACTS as stock). Doors: their index names an area of about 20 doors (16 areas on
   Quarry, a kd split this tool could not reproduce exactly); a door moves only inside the box of the stock doors of its
   area (+64) and keeps the index (proof: an 8-unit door move accepted with its pose and area, a far one refused).
   Earlier plan: (1) moving an ordinary entry = new pose (origin, AnglesToQuat) + the walked
   index + relinking its spatial bucket chain, after a check that the chains (DynEntityList.dynEntSpatialPopulation
   buckets / bucketTails and spatialNode.spatialNodeNext) list exactly the entries of each index on stock zones;
   (2) doors need the structure their index names found first (sparse occluder data of the GfxWorld or the clipMap is
   the likely place).
   Update (2026-11-12, x64-zt 45112b43 / 8ed35d9a): copying and removing instances WITH dynamic entities is built.
   Remove drops the instance and every entry naming it (the 64 reserved 500000 entries and clutter stay); add copies
   the entries of "like" with the new pose, instance index and walked partition, inserted before the reserved block.
   Every entry index reference is rebuilt (no-spatial chain, both populations' bucket chains in descending order with
   tails; a bucket is added for a new partition, an empty one dropped), counts follow (list counts, currentPopulation,
   MapEnts totals, no-spatial and pose-part counts, the GfxWorld dynamic entity client arrays: clients = MapEnts total
   + 10 on 4 maps). Doors: their entries follow an exact walk of the SECOND tree (MapEnts.dynEntSpatialPopulation[1]),
   319 of 319 on Quarry, so doors copy and remove with the same code. Node extents hold their entries (z = the range
   of the origins); copies and moves grow them along their path. Copies keep targetname and script_noteworthy.
   Proof tools/dynent_copy_remove_test.py: Quarry 21 PASS (4 removals incl. a door and a no-spatial extraction site,
   6 copies incl. two doors and a new bucket, 860 renumbered entries name the same instances, all chains exact, ACTS as
   stock), Shipment 20 PASS. Refused: streamed entries; removals on maps whose transient zones name later instances
   (the converter checks every <zone>_<id>_tr.ff; Verdansk); entries placed apart from their instance.
4. Verdansk in the reference view: its world is 749 streamed zones; the view loads one zone.
5. New game type scripts (roadmap 13.6). Read from the recompiled game exe with its linker map (`tools/dw_disasm.py`,
   read only): `G_ScrMP_LoadGameTypeScript_Shared` formats `scripts/mp/gametypes/<type>.gsc` (cp: `scripts/cp/...`)
   and loads it with `AddScriptAndLabel` by that name, the same lookup as any script asset, after the level zones
   are loaded. So a map zone that held a script asset of that name would offer it; whether it wins over the stock
   one of the same name (both loaded), and how a new type name gets into the game type list
   (`Com_GameInfo_LoadGameTypesList`, a string table of `code_post_gfx`), still needs the game.
