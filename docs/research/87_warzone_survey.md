# 87. Warzone (battle royale) in Replay 1.20: what it is made of

Date: 2026-11-12. Read-only survey for the owner's request "warzone-specific editing and map/mode creation". No design
here: facts with sources. Nothing was run in the game. Nothing outside `tools\warzone_*.py`, `out\warzone_study\` and
this file was written.

Abbreviations used for sources:

- `[ZONE]` = `E:\IW8\Builds\1.20-replay\zone\`
- `[WS]` = `E:\IW8-Radiant\out\warzone_study\` (ACTS exports and decoder output made for this survey)
- `[ME]` = `[WS]mapents\` (output of `tools\warzone_mapents.py`)
- `[ST]` = `[WS]code_post_gfx\mw19replay\code_post_gfx\assets\stringtable\` (string tables exported from `code_post_gfx.ff`)
- `[G120]` = `E:\mw124\tools\dvar_search_web\src\combined\gsc\1.20.4-replay\Init\scripts\` (decompiled Replay 1.20.4
  scripts with plain strings; see "About the script sources" below)
- `[G138]` = `E:\mw124\tools\dvar_search_web\src\combined\gsc\1.38\Fulldump\scripts\` (decompiled later build 1.38)
- `[DW]` = `E:\DW\IW8\`
- `[X64]` = `E:\mw124\x64-zt\src\`

Labels: **VERIFIED** = read in data or code of this install. **INFERRED** = follows from verified facts but was not
read directly. **UNVERIFIED** = not checked.

## About the script sources (read this first)

- The folder the brief named, `...\combined\rawfile_source\scripts\`, mixes builds. Its `mp\gametypes\` holds scripts
  of much later seasons (`br_gametype_rumble.gsc`, `br_gametype_kingslayer.gsc`, `br_gametype_mendota.gsc`), and its
  strings are encrypted (`_encstr_...`). None of its 240 `.gscbin` files that also exist in this install is byte
  equal to the install's copy (compared with a scratch script: 234 differ in `common_base_mp`, 6 in `global_mp`).
  Do not use it for 1.20 facts.
- `[G120]` is a decompile of Replay 1.20.4 with plain strings (643 files, `common_base_mp` / `global_mp` scripts,
  only one map: `mp_frontend3`). Its strings match the install: for example `br_circle.gsc` lines 6 to 27 use
  `circle`, `mp_quarry2`, `mp_prison`, `mp_lumber`, `br_final_circle_override`, `ui_br_minimap_radius` in that order,
  and the install's `scripts/mp/gametypes/br_circle.gsc` (from `common_base_mp.ff`) holds the same literal strings in
  the same order (`[WS]common_base_mp_script_strings.tsv`). This survey quotes `[G120]` for logic. Byte identity with
  the install's scripts was not checked (no 1.20 decompiler run here: `gsc-tool 1.5.1 -m decomp -g iw8` stops on the
  install's `.gscbin` with "couldn't resolve function name at index 0x33996").
- The BR map scripts (`mp_donetsk.gsc`, `mp_br_quarry.gsc`, `mp_br_tut.gsc`) are not in `[G120]`. `[G138]` has
  `mp_br_quarry.gsc` and `mp_br_tut.gsc` from build 1.38. Their literal strings match the 1.20 map scripts closely
  (compare `[WS]mp_br_quarry_script_strings.tsv`), but 1.38 is not 1.20: every claim drawn from `[G138]` is marked.
- Literal strings of every 1.20 script in the install were extracted with `tools\warzone_gscbin_strings.py` into
  `[WS]*_script_strings.tsv` (header `GSC\0`, three u32, then zlib; 88,331 strings in `common_base_mp`). Field and
  function names are mostly token ids, not strings, so a missing string proves nothing.

---

## 1. Maps

### 1.1 What is installed (VERIFIED, `tools\warzone_zone_files.py`, `[WS]zone_files.tsv`)

| Map / zone | Purpose (from its own scripts) | main `.ff` | `.sabs` | Transient zones | Total on disk |
|---|---|---|---|---|---|
| `mp_donetsk` | Verdansk, the Warzone map | 364.6 MB (+ `.fp` 83.8 MB) | 1,079 MB + 17 `donetsk_<poi>.sabs` (169.5 MB) | 749 `mp_donetsk_NNNNN_tr.ff` (1,244 MB), 1,024 `mp_donetsk_cg_NNNNN_tr.ff` (589 MB), `mp_donetsk_cg_ls_tr.ff` (1,022 MB) | 5,027 MB (includes a stray `mp_donetsk_cg_ls_tr.rar`, 414 MB) |
| `mp_br_quarry` | BR practice against bots on a cut of Verdansk (`br_practice_match`, `add_practice_bots`, voice lines `dx_brm_uktl_brquarry_*` in its map script) | 301.3 MB | 1,114 MB | 749 numbered (197 MB), 1,024 `cg` (92 MB), `cg_ls` (42 MB) | 1,788 MB |
| `mp_br_tut` | Warzone tutorial (strings `MP_BR_TUT/*`, `tutorial_zone`, `tut_loot`) | 131.8 MB | 30.6 MB | none | 174 MB |
| `mp_bm_tut` | Blood Money / Plunder ("dmz") tutorial (strings `dmztut_*`, `DMZTut`, `player_purchased_balloon`) | 111.8 MB | 7.3 MB | none | 155 MB |
| `common_br_mp` | shared BR assets (240 scriptable definitions, 14 string tables, no scripts) | 27.7 MB | 320.6 MB | none | 474 MB |
| `mp_shipment` (core, for scale) | | 187.8 MB | 28.3 MB | none | 225 MB |

- Each map also has `srv_`, `techsets_`, `eng_`, `ww_` companions. `srv_mp_donetsk.ff` is 43.7 MB; the other `srv_`
  zones are near empty. Every `.xpak` is a 32 KB stub (also research 35).
- `[ST]mp\mapinfo.csv.563.csv` has rows for maps that are not installed: `mp_br_mechanics` (line 70), `mp_lc_br_tut`
  (72), `mp_br_syrk` (66), `cp_br_syrk` (68), `cp_donetsk` (119). No zone file exists for them in `[ZONE]`.
- `mp_quarry2` and the `_gw` ground-war variants are xpak-only (research 35, `iw8_stock_maps.tsv`).

### 1.2 How Verdansk is split and streamed (VERIFIED unless marked)

- ACTS load of `mp_donetsk.ff`: 116,307 assets, among them 282 `gfx_map_trzone` and 282 `fx_map_trzone` records,
  33,086 `streamkey`, 25,194 `xmodelsurfs`, 18,518 `material`, 14,095 `image`, 9,954 `xmodel`, 158 `scriptable`,
  32 `soundbanktransient`, 17 `vehicle` (`[WS]mp_donetsk\mw19replay\mp_donetsk\manifest.json`, `loaded_by_type`).
  `mp_br_quarry.ff` has the same 282 transient-zone records and 73,055 assets.
- The world geometry is in the 749 numbered `_tr` zones; collision is in 1,024 `cg` tiles or the one combined
  `cg_ls` package. `MapEnts.numEdgeLists` is 1,024 on both big maps (1 on every small map, `[ME]fields_compare.tsv`).
- The client picks the collision set: `[DW]integration\replay\DW_MapAvailability.cpp:697-720`
  (`CL_TransientsCollisionMP_SetTransientMode`): mode 2 asks for `<map>_cg_ls_tr`; when
  `zone\mp_donetsk_cg_ls_tr.ff` is absent and `mp_donetsk_cg_00000_tr.ff` exists it falls back to the numbered tiles.
- Map entities are split too: `dynEntityListsCount` is 208 on Verdansk and 23 on Quarry (1 on the others). The editor
  export holds one `dynentitylist` asset per zone; the other lists are INFERRED to live in the transient zones.
- The editor today opens only the main zone of `mp_donetsk` and skips the streamed `_tr` zones with a warning
  (`docs\ROADMAP.md:201`, `:211`; `docs\PROGRESS.md:418`: "the full Verdansk geometry would be 4-5 GB of JSON").

### 1.3 Mapinfo rows (VERIFIED, `[ST]mp\mapinfo.csv.563.csv`)

Column numbers are 0-based. Column meanings are not in the file (no header).

| Row (line) | col 17 | col 19 / 20 | col 22 | col 28 | col 29 |
|---|---|---|---|---|---|
| `mp_shipment` (37) | empty | empty | `loading_mp_shipment_bg` | `0` | `32` |
| `mp_donetsk` (69) | `1` | `15000` / `25000` | `loading_mp_donetsk_bg` | `1` | `43` |
| `mp_br_quarry` (116) | `1` | `15000` / `25000` | `loading_mp_br_quarry_bg` | `1` | `0` |
| `mp_br_tut` (114, also 71) | `1` | `500` / `1000` | `loading_mp_br_tut_bg` | `1` | empty |
| `mp_bm_tut` (115, also 73) | `1` | `500` / `1000` | `loading_mp_br_tut_bg` | empty | empty |

- Column 28: the DW client says "Native PRIVATE_MATCH.MapSupportsCustomGames uses this column" and forces `0` for
  `mp_donetsk` (`[DW]integration\replay\DW_MapAvailability.cpp:745-747`). So `1` there keeps a map out of private
  matches (INFERRED from that comment).
- Column 17 = `1` on every BR map and empty on core maps: a BR flag (INFERRED, meaning UNVERIFIED).

---

## 2. Map data: what a BR map_ents holds

Tool: `tools\warzone_mapents.py out\warzone_study\mapents mp_shipment mp_br_tut mp_br_quarry mp_bm_tut mp_donetsk
mp_backlot2` (inputs: the ACTS exports already in `out\scriptable_study\<zone>\`). Outputs per map in `[ME]`:
`_entities.tsv`, `_families.tsv`, `_scriptables.tsv`, `_loot.tsv`, `_summary.json`, plus `fields_compare.tsv`.
Cross-reference of every entity name with the 1.20 script strings: `tools\warzone_script_refs.py` ->
`[WS]script_refs.tsv`.

### 2.1 Sizes (VERIFIED, `[ME]*_summary.json`, `[ME]fields_compare.tsv`)

| | shipment | backlot2 | bm_tut | br_tut | br_quarry | donetsk |
|---|---|---|---|---|---|---|
| Entity string (chars) | 62,892 | 295,431 | 27,951 | 67,325 | 2,160,317 | 7,985,624 |
| Entities | 611 | 3,234 | 203 | 637 | 10,170 | 47,545 |
| Distinct classnames | 24 | 33 | 20 | 16 | 35 | 39 |
| spawnList records | 611 | 430 | 34 | 41 | 542 | 899 |
| Scriptable map instances | 75 | 138 | 78 | 90 | 984 | 5,545 |
| Scriptable definitions used | 9 | 17 | 9 | 8 | 43 | 132 |
| `lootInstanceCount` = loot points | 0 | 7 | 1 | 5 | 2,924 | 23,446 |
| Loot clusters / zones | 0 / 0 | 3 / 0 | 0 / 1 | 0 / 1 | 7,998 / 553 | 7,966 / 553 |
| `dynEntCountTotal` | 187 | 202 | 130 | 150 | 1,145 | 9,201 |
| `serverInstanceCount` | 66 | 139 | 14 | 29 | 3,887 | 28,896 |
| client trigger models | 37 | 262 | 1 | 1 | 15,637 | 15,210 |
| `createFxEffectTotal` | 179 | 1,087 | 39 | 0 | 55,527 | 143,065 |
| sub models (`numSubModels`) | 79 | 68 | 77 | 122 | 1,653 | 1,638 |

### 2.2 Entity families (VERIFIED, `[ME]<map>_families.tsv`; scripts that use a name: `[WS]script_refs.tsv`)

The entity string of a BR map holds few BR objects. Most BR gameplay content is in the scriptable instances and the
loot block (2.3, 2.4), or is spawned by script.

Classnames found only on BR maps (Quarry / Verdansk counts):

| Classname | br_quarry | donetsk | Used by (1.20 strings / [G120]) |
|---|---|---|---|
| `node_cover_left` / `_right` / `_stand` / `_crouch` / `_crouch_window` / `_prone`, `node_exposed`, `node_concealment_*` | 1,582 | 36,192 | AI and bots (`scripts/mp/bots/*`) |
| `info_volume` (46 `location_volume`, others named by area: `quarry`, `dam`, `gulag`, `super`, ...) | 70 | 99 | `br.gsc:986` (`getentarray("location_volume","targetname")`), `br_vehicles` |
| `volume_prefab_edge_exclusion` | 76 | 76 | none found |
| `script_struct_mp_atv` / `_tacrover` / `_jeep` / `_cargotruck` / `_littlebird` (targetnames `atv_spawn`, `tacrover_spawn`, `jeep_spawn`, `cargotruck_spawn`, `littlebird_spawn`) | 66 / 50 / 36 / 20 / 11 | same | `br_vehicles` and vehicle spawn scripts |
| `script_struct_mp_spawn_selection_camera` / `_area` | 7 / 0 | 23 / 8 | spawn selection (`spawns_<area>`) |
| `script_struct_mp_ladder_top` | 6 | 6 | none found |
| `script_struct_mini` | 0 | 27 | none found |
| `info_grenade_hint` | 0 | 110 | none found |
| `light_omni` (`script_gameobjectname=br`) | 2 | 2 | none found |

BR targetnames and noteworthies (all on `script_struct` unless noted):

| Name | Key | br_tut | br_quarry | donetsk | Used by |
|---|---|---|---|---|---|
| `br_prematch_insertion_point` | targetname | 3 | 5 | 5 | `br.gsc:1221` prematch spawn areas (`mp_bm_tut` has 1) |
| `gulag` | targetname | 1 | 6 | 6 | `br_gulag.gsc:105` arenas |
| `gulag_tutorial` | targetname | 0 | 3 | 0 | `br_gulag.gsc:109` (bot practice) |
| `prison_spawn`, `fight_spawn`, `cell_spawn`, `gulag_center`, `spectator`, `floor_*` | noteworthy (children of a `gulag` struct) | 0 | 144 / 72 / 18 / 9 / 6 | 96 / 48 / 12 / 6 / 6 | `br_gulag.gsc:139-170` |
| `brloot_rock` (scriptable, noteworthy `jail_rock`) | scriptable | 0 | 387 | 258 | gulag rocks (`br_gulag` strings) |
| `extract_pad_<poi>` (25 names) | targetname | 0 | 25 | 25 | Plunder extraction (`br_plunder.gsc:118`, `extract_pad`) |
| `dropBagLocation` / `dropBagCluterNode` (sic) | noteworthy | 0 | 565 / 113 | 565 / 113 | loadout drops, `br_rewards.gsc:291-292`, `arm.gsc:978-979` |
| `equipment_bombsite_crate` model, `script_gameobjectname=br` | model | 1 | 81 | 597 | the loot cache points (see 2.4) |
| `ammo_restock_location` (`script_model`) | targetname | 0 | 17 | 17 | `ammorestock.gsc:8` |
| `ai_encounters` | targetname | 0 | 8 | 8 | `br_ai_encounters.gsc:119` |
| `bank_entry_detector` (`trigger_multiple`) | targetname | 0 | 6 | 6 | `br_bank.gsc:12` |
| `infil_plane` | noteworthy | 0 | 1 | 1 | `br_infils.gsc:6` (hidden if present) |
| `vehicle_volume` / `vehicle_volume_simplified` | noteworthy | 0 | 6 / 18 | 35 / 18 | `br.gsc:1231` (prematch fallback), `br_vehicles` |
| `ascend_begin` / `descend_begin` | noteworthy | 0 | 17 / 17 | 17 / 17 | ascender scriptables (2.3) |
| `heli_fuel`, `fuel_veh`, `veh_fuel` (`trigger_multiple`) | targetname | 0 | 7 / 10 / 3 | same | none found in strings |
| `OutOfBounds` (`trigger_multiple`, also `trigger_hurt`) | targetname | 1 | 8 | 1 | map scripts and `br_c130` (plane path trace) |
| `locale_area_trigger` | targetname | 0 | 12 | 12 | `arm` |
| `mtmc_dom` (`trigger_radius`, `script_gameobjectname=mtmc`) | targetname | 0 | 10 | 10 | `mtmc.gsc` (a core mode laid on Verdansk) |
| `tutorial_zone`, `tut_loot`, `tut_door`, `danger_target`, `supply_station`, `lootcache_area`, `mount_area`, ... | targetname | yes | 0 | 0 | `mp_br_tut` map script only |

Not found in any BR entity string: a plane-path entity, circle centres, buy stations as entities, contracts as
entities, redeploy balloons. Those come from scripts and the other blocks (2.3, 2.4, section 6).

Spawn records (`spawnList`, VERIFIED with a scratch decoder over `spawnList.spawns`): Verdansk 899 = `mp_tdm_spawn`
342, `mp_ctf_spawn` 229, `mp_dm_spawn_start` 100, `mp_ctf_spawn_*_start` 64 + 64, `mp_tdm_spawn_*_start` 50 + 50.
Quarry 542 = the same without the CTF ones. `br.gsc:1111` reads `mp_dm_spawn_start` (fallback
`mp_tdm_spawn_allies_start`) as `level.startingspawns`.

### 2.3 Scriptable instances (VERIFIED, `[ME]<map>_scriptables.tsv`)

A leading comma marks a name-only reference to a definition in another zone (research 86).

| Definition | Zone that defines it | br_tut | bm_tut | br_quarry | donetsk |
|---|---|---|---|---|---|
| `,br_plunder_box` (the buy station / kiosk) | `common_br_mp` | 1 | 1 | 120 | 120 |
| `brloot_plunder_extraction_site_01` / `_02` | map zone | 0 | 1 / 0 | 23 / 2 | 23 / 2 |
| `,brloot_rock` | `common_br_mp` | 0 | 0 | 387 | 258 |
| `,scriptable_auto_ascender` / `scriptable_auto_descender` | `common_mp` / map zone | 0 | 0 | 17 / 17 | 17 / 17 |
| `,brloot_ammo_762`, `,brloot_ammo_919`, `,brloot_assassination_tablet` | `common_br_mp` | 2, 1, 0 | 5, 5, 1 | 0 | 0 |
| `light_blinking_slow_br_infil` | map zone | 0 | 0 | 2 | 2 |
| doors, fences, street signs, lights (`scriptable_door_*`, `ee_*_lm`, ...) | map zone | rest | rest | rest | rest (e.g. 612 hollow wooden doors) |

- `br_plunder_box` is the kiosk: `br_armory_kiosk.gsc` (install strings) uses `br_plunder_box`,
  `getallspawninstances`, `classname`, `scriptable_br_plunder_box`, `setspawninstances`, and reads
  `mp/brKioskPurchases.csv` or `mp/brKioskPurchases_DMZ.csv`.
- No `br_loot_cache` (the loot chest) is a map instance on any map. Caches and ground loot are spawned at run time
  into the loot slots (2.4).

### 2.4 The loot block `spawnGroupLoot` (VERIFIED unless marked)

Layout (`[X64]games\iw8\structs.hpp:10677-10701`): `SpawnGroupPoint` 28 bytes = origin (3 floats), angles (3 floats),
`cluster` u16, `zone` u16. `SpawnGroupCluster` 40 bytes = `zone` u16, `pointCount`, `points` (u16 indices), bounds.
`SpawnGroupZone` 56 bytes = `name` (script string), point list, cluster list, bounds. The large point arrays come out
of ACTS as hex; `warzone_mapents.py` decodes them.

What the values mean (decoded with scratch scripts over `[ME]`):

- `lootInstanceCount` in `scriptableMapEnts` equals `spawnGroupLoot.pointCount` on all six maps (0, 7, 1, 5, 2,924,
  23,446). Each loot point reserves one scriptable slot (INFERRED from the equality; research 86 already found the
  loot count is added into `serverInstanceCount`, `serverFixedPartCount` and `serverUsablePartCount`).
- `point.cluster` is a cluster index, or one of three special values: `0xFFFD` (65533, free point: Verdansk 2,905,
  Quarry 363), `0xFFFB` (65531: Verdansk 606, Quarry 81), `0xFFFF` (65535: one point in br_tut). On Quarry and
  Verdansk every cluster's point list names exactly the points that carry its index (19,935 and 2,480 checked, 0
  mismatches).
- Every `0xFFFB` point sits exactly on a `script_struct` with model `equipment_bombsite_crate` and
  `script_gameobjectname br` (606 of 606 on Verdansk, 81 of 81 on Quarry, rounded positions). Those structs are the
  loot-cache markers. `[ST]mp\loot_cache_set_global.csv.447.csv` lines 1-4: `req br_loot_cache chest`,
  `chance cachePoint 34`, `item br_loot_cache 97`. So `0xFFFB` = cache point (INFERRED).
- No other loot point has an entity at its position. The ordinary ground-loot points exist only in this compiled
  block (INFERRED: they are made from editor data that is not kept in the entity string).
- `point.zone` is 1-based: 0 = no zone, `n` = `zones[n-1]` (every zone's point list matches points with zone value
  index + 1: Verdansk 20,670, Quarry 2,428, br_tut 4, bm_tut 1). Verdansk has 2,170 points with zone 0.
- Zone names (script strings): Verdansk and Quarry 553 zones with 53 distinct names (`map_airfield`, `map_bank`,
  `map_gulag`, `map_quarry`, `map_layover`, ...). br_tut: 1 zone `tutorial_scavenger`. bm_tut: 1 zone `loot_cache`.
  Quarry carries all 553 Verdansk zones and 7,998 clusters, but 7,009 of the clusters are empty: it is a cut of
  Verdansk's data (also: it uses `compass_map_mp_donetsk`, `mp_donetsk_callouts.csv`, the `mp_donetsk*.vision`
  files; `[WS]mp_br_quarry\...\stringtable`, `rawfile`).
- The zone names are the keys of the loot tables (section 3): `[ST]mp\loot_item_defs.csv.454.csv` has 62 `zone` rows
  such as `"zone","map_bank","mp/loot_set_plunder.csv","mp/loot_cache_set_global.csv"` (line 4);
  `loot_item_zones_tutorial.csv.468.csv` maps `tutorial_scavenger` to `mp/loot_set_tutorial_scavenger.csv`.
- mp_backlot2 (a core map) also has 7 loot points and 3 clusters, zone 0 only: the block is not BR-only.

### 2.5 Other MapEnts fields (VERIFIED, `[ME]fields_compare.tsv`, base mp_shipment)

- Non-zero only on BR maps: `spawnGroupLoot.zoneCount` (1 on the tutorials, 553 on the big maps; 0 on shipment and
  backlot2); `numEdgeLists` 1,024 (1 elsewhere); `dynEntityListsCount` 23 / 208 (1 elsewhere); four
  `DYNENT_TYPE_CLUTTER` entries in the dynentitylist of Quarry and Verdansk (none elsewhere);
  `clientSideEffects.exploderCount` 26, `exploderNameTotal` 1 and `oneshotSoundCount` 2,362 on Verdansk.
- The reserved dynamic entities are not BR-specific: every one of the six maps has 64 `DYNENT_TYPE_SCRIPTABLEPHYSICS`
  entries with `scriptableMapIndex` 500000 and two 64-entry reserved tables; `reservedInstanceCount` and
  `totalInstanceCount` are 0 everywhere; `ffMemCost` 1240 everywhere.
- Equal on all six maps: `splineList` (0), `clientModels` (0), `clientEntAnchors` (0), `mayhemScenes` (0),
  `spawners` (0), `audioPASpeakers` (0), `dynEntMaxClientHistoryCount` (10000).
- `physicsLimits` on BR maps are much larger than the door count (Verdansk 5,138 / 4,127; Quarry 540 / 517); research
  86 could not explain them.

---

## 3. Loot

### 3.1 Where loot is defined (VERIFIED)

- Item definitions are scriptables named `brloot_*` in `common_br_mp` (240 definitions in that zone: ammo, armor,
  plates, 140 weapon variants `brloot_weapon_<gun>_<rarity>`, killstreaks, offhands, cash `brloot_plunder_cash_*`,
  tablets, `br_loot_cache`, `br_loot_cache_lege`, `br_plunder_box`, `brloot_rock`, `veh8_mil_air_acharlie130`, ...;
  `out\scriptable_study\common_br_mp\...\assets\scriptable\`).
- The loot tables are string tables in `code_post_gfx.ff`, not in `common_br_mp` (ACTS `-a stringtable`, `[ST]mp\`):
  `loot_item_defs*.csv` (14 variants: base, `_s3`, `_cash`, `_dmz`, `_brpractice`, `_gg`, `_ss`, `_qa`, ...),
  `loot_set_*.csv` (about 90), `loot_cache_set_*.csv` (8), `loot_item_zones_*.csv` (2), `loot_overrides_dmz_*.csv`,
  `brloot.csv`, `brloottable.csv`, `br_cash_placement.csv`.
- `common_br_mp` has the BR string tables for the kiosk, missions and the class table: `brkioskpurchases.csv`,
  `brkioskpurchases_dmz.csv`, `brmissions.csv`, `brmission_rewards*.csv`, `brmission_reward_groups*.csv`,
  `brmission_reward_scalers.csv`, `brtacmap_legend*.csv`, `br_party_colors.csv`, `br_pause_menu.csv`,
  `classtable_br.csv` (`[WS]common_br_mp\mw19replay\common_br_mp\assets\stringtable\mp\`).

### 3.2 Table format (VERIFIED, `[ST]mp\`)

- `loot_item_defs.csv` (252 rows): `zone` rows (62) give `zone name, loot set file, cache set file`; `item` rows (130)
  give `scriptable name, type, rarity, count, ...` (line example: `"item","brloot_ammo_12g","ammo","0","8",...`);
  `depends` rows (53) say an item brings another (`brloot_weapon_famas_epic` -> `brloot_ammo_762` 100);
  `raritydef` rows (7) give colours and effects per rarity.
- `loot_set_global.csv` (117 rows): `chance point 60` ("% of non-clustered points that will be populated"),
  `chance cluster 76`, `chance clusterSolo 70`, `req` rows, `chance <category> <weight>` rows, `item <scriptable>
  <weight> <#rarity> <#category>` rows.
- `loot_set_cache_contents_*.csv`: what a cache chest gives (`chance ammo 2`, `chance health 31`, ...).

### 3.3 How loot is placed (VERIFIED in code strings; flow INFERRED)

- The engine places loot, not GSC. The exe (`game_dx12_ship_replay.exe`, strings read with a scratch script) has the
  dvar `loot_table_name` with the help text "Loot Table filename. e.g. mp/brloot_test.csv. Leave blank to avoid
  generating loot.", plus `loot_seed`, `loot_table_filter`, `loot_table_zones` ("Zone map file name"), the functions
  `BG_SpawnGroup_Loot_FindScriptableDefByName` and errors such as "'%s' is not a valid scriptable def. Please make
  sure it is included as precache_scriptable in zone_source", the point types `dropkit`, `cache`, `kiosk`, `tablet`,
  `weapon`, and the script functions `GetLootSpawnPoint`, `DisableLootSpawnPoint` ("is not a cache or quest point"),
  `GetQuestPoints`.
- The playlists set the table: `[DW]data\playlists\5800\catalog.json` gametype entries set `loot_table_name` and its
  obfuscated twin `RKMMNSQKO` to `mp/loot_item_defs_s3.csv` (trios, quads) or `mp/loot_item_defs_cash.csv`
  (`brInf`). `br_pickups.gsc:13` ([G120]) reads `RKMMNSQKO` with default `mp/loot_item_defs.csv`.
- Scripts then work on what was spawned: `br_lootcache.gsc:4-7` hooks the cache scriptable's `body` part;
  `br_pickups.gsc` handles pickups (`spawnscriptable` at line 1366 for drops); quests take points with
  `getquestpoints` (`br_quest_util.gsc:967`) and `disablelootspawnpoint` (`br_dom_quest.gsc:52`); kiosks are
  re-instanced by `br_armory_kiosk.gsc:244-253` and `br_functional_poi.gsc:59-64`.
- Older script-placed loot also exists: `plunder.gsc:382` spawns caches at `script_struct` `loot_cache` (half of
  them). No BR map has that targetname (0 on all six).

---

## 4. Modes

### 4.1 The game type table (VERIFIED, `[ST]mp\gametypestable.csv.325.csv`)

The table has exactly 32 rows (the DW client reads at most 32: `DW_MapAvailability.cpp:35-36`, `:759`). BR rows:

- line 21: `"br","LUA_MENU_MP/BR","LUA_MENU_MP/BR_DESC","icon_br_mode_wz","0","0","Alternate","0",...`
- line 22: `"br_dmz","LUA_MENU_MP/PAYBACK","LUA_MENU/BLOOD_MONEY_DESC","icon_br_mode_plunder","0","0","Alternate","0",...`

No `plunder`, `brtdm`, `rumble`, mini royale or solos / trios rows. Those are playlist variants of `br` (4.3).

### 4.2 Scripts (VERIFIED, file list from ACTS `-a scriptfile` of `common_base_mp`, `[WS]common_base_mp\`)

All gametype scripts are in `common_base_mp.ff` (550 script files); `common_br_mp.ff` holds none. 61 `br_*.gsc`
files exist, among them `br.gsc` (the game type), `br_gametypes.gsc` (sub-mode switch), `br_gametype_dmz.gsc`,
`br_gametype_evac.gsc`, `br_gametype_extraction.gsc`, `br_gametype_juggernaut.gsc`, `br_gametype_prop.gsc`,
`br_gametype_sandbox.gsc`, `br_circle`, `br_c130`, `br_gulag`, `br_infils`, `br_pickups`, `br_lootcache`,
`br_plunder`, `br_armory_kiosk`, `br_vehicles`, `br_respawn`, `br_publicevent_*`, `br_*_quest`, and also
`plunder.gsc`. Gametype-specific helper scripts: `scripts/mp/bots/bots_gametype_br.gsc`,
`scripts/mp/agents/gametype_br.gsc`.

There is no `scripts/mp/gametypes/br_dmz.gsc` in the install although `br_dmz` has a table row. Whether the row can
start a match is UNVERIFIED.

### 4.3 How variants are chosen (VERIFIED)

- The sub-mode is the dvar `scr_br_gametype`, read by `br_gametypes.gsc:11` ([G120]); values `extract`, `evac`,
  `dmz`, `sandbox`, `prop`, `jugg`, or empty / `none` = plain BR (`:16-39`). Install strings of
  `br_gametypes.gsc` list the same names.
- Features are switched off by name: `br_gametypes::disablefeature` / `isfeaturedisabled`, used for `circle`
  (`br_circle.gsc:6`), `gulag` (`br_gulag.gsc:11`), `kiosk` (install strings of `br_armory_kiosk`).
- Other mode dvars read by `br.gsc` (install strings): `scr_br_gulag`, `scr_br_infilC130`, `scr_br_circle_disable`,
  `scr_br_respawn_token`, `scr_br_teammaxfill`, `scr_br_loadout_option`, `scr_br_prematchffa`, `scr_br_winlimit`,
  `scr_br_roundlimit`, `scr_br_alt_mode_*`; `br_minplayers`; `scr_br_teamsize` (playlists).
- The private-server playlist catalogue (`[DW]data\playlists\5800\catalog.json`, version 14, line 1, single-line
  JSON; 43 game types) has four BR game types, all with `"script": "br"`:

| Game type | teamSize | Key dvars | Playlist (all on `mp_donetsk`) |
|---|---|---|---|
| `brplund` | 3 | `loot_table_name mp/loot_item_defs_s3.csv`, `scr_br_teamsize 3`, `scriptable_force_large_mode 1` | 25 `BRTRIOS_NAME` |
| `brplundQuad` | 4 | same with teamsize 4 | 89 `BRQUADS_NAME` |
| `brInf` | 1 | `loot_item_defs_cash.csv`, teamsize 1 | 71 `BRSOLOSTIM_NAME2` |
| `dmz` | 3 | `scr_br_gametype dmz`, `br_kiosk_items_filename mp/brKioskPurchases_DMZ.csv`, `scr_br_timelimit 1800`, `ui_blood_money 1` | 104 `BLDMNY_NAME` (Blood Money) |

  So the playlist "game type" name is only a label; the script file is the `script` field (`br`), and the
  variant is made of dvars. `[DW]src\playlist_catalog.cpp:404-409` treats any mode whose script is `br` as a squad
  mode.
- The engine builds the script path as `scripts/mp/gametypes` + `/%s%s` with `.gsc`, and also loads
  `scripts/mp/bots/bots_gametype_%s.gsc` and `scripts/mp/agents/gametype_%s.gsc` when they exist (exe strings at
  0x23DD090: `scripts/mp/bots`, `%s/bots_gametype_%s%s`, `scripts/mp/agents`, `%s/gametype_%s%s`,
  `scripts/mp/maps`, `%s/%s/%s%s`, `scripts/mp/gametypes`, `%s/%s%s`). The DW symbol map names the loader
  `G_ScrMP_LoadGameTypeScript_Shared` at 0x140EA0A10 (`[DW]validation\debug-symbol-fixture\game_dx12_ship_replay_dw.map`).

### 4.4 What this build's private match can start today

- VERIFIED IN CODE (not run): Verdansk is offered in the private-match map picker by the DW client
  (`DW_MapAvailability.cpp:745-747` forces mapinfo column 28 to `0` for `mp_donetsk`), and its game type list is
  replaced by every loaded game type name (`:750-784`, Multiplayer mode with the local backend only). With the
  32-row table above that list includes `br` and `br_dmz` (INFERRED).
- `mp_br_quarry`, `mp_br_tut`, `mp_bm_tut` have no special handling in `[DW]`; their mapinfo column 28 is `1` (quarry,
  tut) or empty (bm_tut). Whether they show in the picker: UNVERIFIED.
- Docs: `[DW]docs\DW_PLAYTEST_FIXES_20261007.txt:16` "mp_donetsk is available to DW MP local/custom selectors", `:17`
  "not proof that every BR loading failure is resolved", `:30` asks to select BR Verdansk and keep the exact fatal
  error. `[DW]docs\DW_HOST_SCALING_EXPERIMENT_20261010.txt:521-531`: a BR pregame with 151 bots reached its countdown
  and then failed (crash in `Glass_GetInitialPieceCenter`, fix rebuilt at `:533-537`). `[DW]docs\replay-dedicated.txt:1423-1424`:
  "BR/squad lobby documents remain disabled". No document records a finished BR match in a private match.
- How a match is started: private match lobby `set LSTLQTSSRM 1` (xblive_privatematch) + `xstartprivatematch`, then
  `NSQLTTMRMP` (ui_mapname) and `MOLPOSLOMO` (ui_gametype), then the party start (`[DW]integration\replay\DW_Devmap.cpp:494`,
  `:366-367`, `:510`; command line `--devmap <id> [--gametype <name>] [--bots <n>]`, `:3`). `--gametype br` passes
  the name check (`:262`). The file says it "has not been linked or run" (`:34-38`). The editor uses the same two
  dvars (`src\core\devmap_plan.cpp:69-77`, research 74). The BR dvars of a public playlist (`loot_table_name`,
  `scr_br_teamsize`, ...) are not set by that route: `[DW]src\matchmaking_services.cpp:333-336` "Playlist-zero
  private games use the stock host's own rules". So a private `br` match on Verdansk would run without
  `loot_table_name` unless it is set by hand, and the engine help text says a blank table means no loot (INFERRED).
- The dedicated server is fixed to `mp_shipment` / `war` (`[DW]integration\dedicated\ReplayDedicatedWorld.cpp:2687-2688`)
  and refuses squad playlists (`[DW]src\matchmaking_services.cpp:1655-1660`).
- BR support code in the client: `br_minplayers` forced to 2 (`DW_LobbyControls.cpp:409-425`), squad size dvar
  `replay_dw_squadSize` (`DW_BotRoster.cpp:497`), a Gulag navigation overlay zone loaded before `mp_donetsk`
  (`DW_BotStyleZone.cpp:553`, `:620-748`), BR bot loot logic (`integration\host-scale\SV_HostBot_Loot.cpp:189`).

---

## 5. Creating a new mode or variant

VERIFIED facts:

- A variant needs no script: a playlist entry is a game type label + `script` + dvars (4.3). The BR scripts already
  read many dvars and feature switches. But a private match does not apply playlist dvars (4.4), so a variant there
  would have to set its dvars some other way (map script `setdvar`, as `mp_br_tut` does: 1.20 strings
  `scr_br_loadout_option`, `scr_game_matchstarttime`, `br_kiosk_disabled_items`; `[G138]mp\maps\mp_br_tut\mp_br_tut.gsc:58-69`).
- The map script can change BR behaviour: `mp_br_tut` sets `level.br_circle_disabled = 1`, `level.usegulag = 0`,
  `level.skipprematchdropspawn = 1` and disables the circle feature (`[G138]...mp_br_tut.gsc:70-74`, where the 1.38
  decompile mislabels the `br_gametypes` call as `give_fists("circle")`; the 1.20 strings
  of `mp_br_tut.gsc` contain `circle`, `disablefeature`, `skipprematch`).
- The converter writes exactly one script file per map: `scripts/mp/maps/<map>/<map>.gsc` (`[X64]convert\iw3_iw8\convert.cpp:2234-2238`,
  fallback with the same name `:2252-2262`), compiled with `gsc-tool -m comp -g iw8 -s pc` (`iw3_scripts.cpp:866-867`)
  from IW3-style input translated by `scripts::Translate`. The zone writer itself takes any number of assets and only
  refuses a duplicate type + name (`[X64]games\iw8\zone\iw8_zone.cpp:106-108`). So a second script file in a map zone
  needs converter code only (INFERRED).
- Calls from the map script into stock scripts are limited to a whitelist, `kReplayFunctions`
  (`iw3_scripts.cpp:137-151`: `scripts/mp/damage::playerkilled_internal`, `scripts/mp/weapons::deleteexplosive`,
  `scripts/engine/trace::_bullet_trace`, `scripts/engine/scriptable_door::system_init`). A BR map script calls
  `br_c130::setc130heightoverrides`, `br_circle::...`, `br_gametypes::disablefeature`, which are not on it.
- gsc-tool's iw8 engine writes a far call to a path with no token id as id 0 plus the path string; no
  `scripts/mp/gametypes/*` path has a token id (`atian-cod-tools\deps\gsc-tool\src\gsc\engine\iw8_token.cpp`, grep;
  `assembler.cpp:382-395`). Some stock scripts are numbered, not named (`2323.gscbin`, `2324.gscbin` in the
  Verdansk zone are the infil scripts; `[WS]mp_donetsk\...\scriptfile\`).
- The engine finds a game type by `scripts/mp/gametypes/<name>.gsc` (4.3). Asset lookup is by type and name
  (`DB_FindXAssetHeader`, research 05). The DW client already replaces stock scripts by loading its own zone before
  `common_base_mp` (`[DW]integration\replay\DW_BotStyleZone.cpp:702-709`, "Same-group duplicates are discarded by
  native DB_LinkXAssetEntry" at `:704`); its patched copies include `scripts/mp/gametypes/br_weapons.gsc`
  (`[DW]tools\prepare-bot-style-scripts.py:50-54`).
- Custom map packages live in `<game>\mods\mw120r\maps\<id>\` (`DW_MapAvailability.cpp:115-119`, `:455-485`); their
  map row lists game types from `map.json` `"gameTypes"` (research 79). A name must also be one of the 32 rows of the
  game type table to be offered (`DW_MapAvailability.cpp:759-784` for Verdansk; research 79 for custom maps).

Not proven (each needs a game run or IDA):

- Whether a script file named `scripts/mp/gametypes/<new>.gsc` inside a level zone is found when the game type is
  loaded (load order of the level zone against the game type load), and whether a level-zone copy of a stock name
  (`br.gsc`) would win over `common_base_mp`. DW's own note: `later_level_zone_assets_not_live_verified=True`
  (`[DW]tools\prepare-bot-style-scripts.py:195`).
- Whether a name that is not in `mp/gametypestable.csv` can be started at all. Adding a row means changing
  `code_post_gfx.ff` or the client.
- Whether string far calls (id 0 + path) from a custom script into `scripts\mp\gametypes\br_*` resolve in Replay.

---

## 6. Minimum for a small custom map in the `br` game type

Reference: `mp_br_tut` (no circle, no gulag, no plane) and `mp_br_quarry` (full flow). What the scripts read at start
([G120] unless marked):

| Need | Read by | Without it |
|---|---|---|
| `level.br_level` struct with `br_mapbounds[2]`, `br_mapcenter`, `br_mapsize`, `br_circleradii[]`, `br_circleclosetimes[]`, `br_circledelaytimes[]`, `br_circleminimapradii[]`, and `c130_heightoverride` / `c130_sealeveloverride` (via `br_c130::setc130heightoverrides`) | set by the map script (`[G138]mp\maps\mp_br_quarry\mp_br_quarry.gsc:81-118`; 1.20 strings of `mp_br_quarry.gsc` and `mp_donetsk.gsc` contain `br_mapboundsfull`, `br_circleshowdelaydanger`, `br_circleshowdelaysafe`, `applycirclesettings`); used by `br_circle.gsc:12-28` and `br_c130.gsc:35, 66, 224-280` | `initcircle` returns early when `level.br_level` is undefined (`br_circle.gsc:12-13`); `br_c130` indexes `level.br_level.*` without a check (lines above): plane path fails (INFERRED: script error) |
| Circle arrays consistent | `br_circle` asserts "level.br_level.br_circleCloseTimes not defined" and size checks (1.20 strings) | assert / error |
| Or turn the features off | `level.br_circle_disabled`, `level.usegulag = 0`, `disablefeature("circle")`, `level.skipprematchdropspawn` (`[G138]...mp_br_tut.gsc:70-74`) | n/a |
| Prematch spawn areas: `script_struct` `br_prematch_insertion_point` (key `radius`, default 5000; `minradius` default 500), or `level.br_prematchspawnlocations`, or ents with noteworthy `vehicle_volume` | `br.gsc:1218-1240` | empty prematch spawn list |
| Spawn points `mp_dm_spawn_start` (or `mp_tdm_spawn_allies_start`) | `br.gsc:1107-1118` | no starting spawns |
| `mp_global_intermission` | map scripts move it (`[G138]...mp_br_quarry.gsc:59-61`) | UNVERIFIED |
| Minimap: `compass::setupminimap("compass_map_<map>")` and corners | map script (1.20 strings `compass_map_mp_br_tut`, `compass_map_mp_donetsk`) | UNVERIFIED |
| Out of bounds: `trigger_multiple` / `trigger_hurt` `OutOfBounds`, optional `kill_border_trigger` | map scripts when `scr_br_outOfBounds` is not 0; `br_c130` traces the plane path against them | none needed for start (INFERRED) |
| Gulag: `script_struct` `gulag` whose `target` children carry noteworthy `prison_spawn`, `fight_spawn`, `cell_spawn`, `gulag_center`, `spectator` | `br_gulag.gsc:103-117`, `:139-170`; skipped when `level.usegulag` is false (`:8-9`, default from dvar `gulag`, `br.gsc:531`) | empty arena list |
| Kiosks: `,br_plunder_box` scriptable instances (definition in `common_br_mp`) | `br_armory_kiosk` | no buy stations (INFERRED) |
| Loot: `spawnGroupLoot` points / zones and `lootInstanceCount`, plus `loot_table_name` set to a table whose zone names match | engine (3.3) | no loot (engine help text) |
| Plunder extraction: scriptables `brloot_plunder_extraction_site_01/02` with targetname `extract_pad` | `br_plunder.gsc:1261-1273`; skipped when `scr_br_plunder` is 0 (`:6-10`) | no extraction sites |
| Vehicles: `script_struct_mp_<vehicle>` with targetname `<vehicle>_spawn` | `br_vehicles` (install strings) | no vehicles |
| `infil_plane` (noteworthy) | `br_infils.gsc:6-10`, only hidden if present | nothing |
| `location_volume` (`info_volume`) | `br.gsc:986` callouts | no callouts (INFERRED) |
| mapinfo row with column 28 `0`; zone `common_br_mp` loaded | DW client (1.3); the map's zone list | map not offered / `,br_plunder_box` unresolved (INFERRED) |

Which zones a custom map loads besides its own (whether `common_br_mp` is loaded for any map in a `br` match, or only
for maps whose mapinfo marks them BR) was not determined.

---

## 7. Editing stock Warzone maps

VERIFIED numbers (Verdansk, then Quarry):

- Entities 47,545 / 10,170; entity string 7.99 MB / 2.16 MB; of the entities 36,192 / 1,582 are AI cover nodes and
  5,949 / 6,277 are `script_struct` (5,217 on Verdansk carry `transient_prefab`, 5,225 `transient_entity` keys:
  entities tied to transient zones).
- Scriptable instances 5,545 / 984 of 132 / 43 definitions; loot points 23,446 / 2,924; clusters 7,966 / 7,998; loot
  zones 553 / 553; spawns 899 / 542; triggers 252 / 235; client triggers 15,210 / 15,637; createfx effects
  143,065 / 55,527; sub models 1,638 / 1,653.
- Zone sizes: section 1.1 (Verdansk about 5 GB of files, Quarry 1.8 GB).

What is different from a core map:

- Geometry is in 749 transient zones (282 `gfx_map_trzone` records in the main zone); collision in 1,024 tiles or
  one 1 GB package. The editor reads only the main zone (1.2). ACTS needed a raised node cap to export Verdansk's
  `gfx_map` (4,107,040 nodes; research 64 line 113).
- 208 dynamic-entity lists (core maps: 1).
- The ordinary loot points have no entities in the entity string (2.4), so an editor that rebuilds the map from
  entities loses them unless it reads and writes `spawnGroupLoot` itself. The converter writes no loot block today
  (`[X64]games\iw8\zone\map_zone.h:925-947`: "No loot and no reserved instances"; `spawnGroupLoot` stays zero).
- Scriptable definitions come from three places: the map zone, `common_br_mp` (`,br_plunder_box`, `,brloot_rock`),
  `common_mp` (`,scriptable_auto_ascender`).
- Quarry is a cut of Verdansk: same loot zones, callouts, vision files and minimap (2.4).
- The map script of a BR map is small (Verdansk 6.5 KB, Quarry 10 KB compiled) but calls many `br_*` scripts; the
  editor's converter can only call whitelisted stock functions (5).

---

## Not determined

- Whether a private match on Verdansk with `ui_gametype br` loads and plays, and with what loot (no `loot_table_name`
  in that route). Needs a game run (the owner tests only when they choose).
- Whether `br_dmz` (table row without a script of that name) can start.
- The meaning of mapinfo columns 17, 19, 20 and 29, and of the loot point value `0xFFFF`.
- Which editor-side entity becomes an ordinary loot point (`0xFFFD` or clustered). Stock data keeps no entity for
  them; the compiler input is not in the install.
- How the 208 dynamic-entity lists of Verdansk map to transient zones (only list 0 was exported).
- The property that sets `physicsLimits` on BR maps (also open in research 86).
- Whether a level-zone script file can add or replace a game type script, and whether string far calls into
  `scripts\mp\gametypes\br_*` resolve (section 5). Needs IDA on `G_ScrMP_LoadGameTypeScript_Shared` or a live test.
- Which common zones are loaded for a custom map in a `br` match (`common_br_mp` or not).
- The exact 1.20 text of the BR map scripts: only literal strings were read; logic comes from 1.38 (`[G138]`), and
  `[G120]` was not compared byte for byte with the install.
- Whether `mp_br_quarry`, `mp_br_tut`, `mp_bm_tut` appear in the private-match map picker.

## Tools and outputs (all read-only on the game folder and `E:\DW\IW8`)

- `tools\warzone_zone_files.py` -> `[WS]zone_files.tsv` (file names and sizes only).
- `tools\warzone_mapents.py` -> `[WS]mapents\` (entity, family, scriptable, loot and field tables).
- `tools\warzone_gscbin_strings.py` -> `[WS]*_script_strings.tsv` (literal strings of 1.20 scripts).
- `tools\warzone_script_refs.py` -> `[WS]script_refs.tsv` (entity names against script strings).
- ACTS exports made with `tools\acts_export.py` (types `scriptfile`, `stringtable`, `rawfile`) for `common_br_mp`,
  `common_mp`, `common_core_mp`, `common_base_mp`, `global_mp`, `global_core_mp`, `common`, `mp_donetsk`,
  `mp_br_quarry`, `mp_br_tut`, `mp_bm_tut`, and `stringtable` for `code_post_gfx`, `global`, `ui`, `ui_mp`,
  `ui_boot`, `comms_mp`, `common_stream_mp`, `global_stream_mp`. `[WS]` totals about 38 MB.
