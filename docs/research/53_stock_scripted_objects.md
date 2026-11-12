# 53. Stock scripted objects: classnames, keys, handlers and custom-map status (roadmap 6.13)

Status: research and data. Read-only on game files. The stock GSC is a decompile (8.19 dump), so every handler
path is UNVERIFIED for Replay 1.20 until checked in game. "works_in_custom_map" is a reading of the stock scripts,
not a test: no custom map was run.

Companion files: `tools/build_scripted_objects.py` (writes the TSV), `data/asset_catalog/iw8_scripted_objects.tsv`
(68 rows), `src/core/stock_scripted_catalog.{h,cpp}` (lookup), `tests/stock_scripted_catalog_tests.cpp`.
Doc 40 is the earlier survey of the same objects and remains the reference for the gate and encrypted-name tables.

## 0. Sources

- Entities: `data/stock_dumps/mp_shipment` (611 entities) and `data/stock_dumps/mp_backlot2` (3234), hex
  `entityString.bytes` decoded with `data/generated/iw8_canonical_keys.tsv`. Nodes (`node_*`, 2888), `worldspawn`
  and `info_player_start` are not scripted objects and are excluded: 952 entities are classified.
- Scripts: stock GSC dump `E:\mw124\tools\dvar_search_web\src\combined\rawfile_source\scripts` (cited as `scripts/`).
  Most identifiers are `_encstr_<hex>`; their meaning comes from doc 40 section 6 and is marked where used.
- Editor and converter: doc 40 sections 5 and 8; doc 41 sections 1.4 and 4.
- No door entities exist in either stock map entity string, so no door kind is listed (`scripts/mp/door.gsc` has no
  stock placement in these two maps).

## 1. Method

Each stock entity is counted under the first matching rule in `RULES` (builder). A rule has a kind, a classname
pattern, a key (or `-`) and a value pattern. The TSV keeps every row with count > 0, so a kind can appear on several
rows. The C++ `ScriptedObjectCatalog::match` returns every matching row, so an entity can match more than one kind.

## 2. Result

| Works in custom map | Rows | Entities |
|---|---|---|
| yes | 1 | 4 |
| no | 9 | 34 |
| unknown | 58 | 914 |

Entities by kind (count is entities; a kind row can add to the same kind):

| Kind | Classname / key = value | Entities | Handled by (stock script) | Custom map |
|---|---|---|---|---|
| dom_capture_trigger | trigger_radius, script_gameobjectname `dom onslaught` | 6 | `gametypes/dom.gsc:351,367`; gate `gameobjects.gsc:17` | unknown |
| hardpoint_zone | trigger_multiple, script_gameobjectname `hardpoint` | 18 | `gametypes/koth.gsc:805-838`, `hq.gsc:848-886` | unknown |
| hq_radio | script_model, targetname `hqloc` (and gameobject `hqloc`) | 18 | `gametypes/hq.gsc:885` | unknown |
| hq_radio_brush | script_brushmodel, gameobject `hqloc` | 18 | gate only | unknown |
| sd_site | trigger_use_touch targetname `bombzone` (4), gameobject `bombzone` (7), `script_bombmode_original 1` (6); script_model gameobject `bombzone` (8), script_brushmodel gameobject `bombzone` (4) | 29 (rows) | `gametypes/sd.gsc:470`; `obj_bombzone.gsc:17,118,124` (reads `.target` chain) | unknown |
| sd_bomb | script_model, targetname `sd_bomb` / gameobject `sd` | 2 | `sd.gsc:11-14` gate | unknown |
| sd_pickup | trigger_multiple, targetname `sd_bomb_pickup_trig` / gameobject `sd` | 2 | `sd.gsc` (lookup encrypted) | unknown |
| ctf_flag | script_model, targetname `ctf_flag_*` / gameobject `ctf` | 4 | `gametypes/ctf.gsc:509,662` | unknown |
| ctf_trigger | trigger_radius, targetname `ctf_trig_*` / gameobject `ctf` | 4+4 | `ctf.gsc:502,524`; gate `ctf.gsc:9-10` | unknown |
| ctf_zone | trigger_radius, targetname `ctf_zone_*` | 4 | `ctf.gsc` (encrypted) | unknown |
| flag_goal | trigger_radius, targetname `flag_goal_*`, `flag_arena` | 3 | arena or ball/blitz (UNVERIFIED which) | unknown |
| cyber_emp | script_model, targetname `cyber_emp` | 6 | `gametypes/cyber.gsc:460-492` | unknown |
| cyber_emp_pickup | trigger_use, targetname `cyber_emp_pickup_trig` | 6 | `cyber.gsc:345,388` | unknown |
| cyber_zone / cyber_defuse | trigger_use_touch, `cyber_empzone_*` / `cyber_emp_defuse_*` | 4 / 4 | `cyber.gsc:460-492` | unknown |
| cyber_gameobject | script_model, script_brushmodel, gameobject `cyber` (the trigger_use rows are cyber_emp_pickup and cyber_zone) | 8+4 (rows) | gate `cyber.gsc:11-13` | unknown |
| dd_bombzone | trigger_use_touch, gameobject `dd_bombzone` (6); script_model, gameobject `dd_bombzone` (12) | 6 + 12 (rows) | `dd.gsc:527`; gate `dd.gsc:11-14` | unknown |
| dd_bombzone_clip | script_brushmodel, targetname `dd_bombzone_clip_*` | 6 | UNVERIFIED | unknown |
| grind_location | trigger_radius, targetname `grind_location` (gameobject `grind` overlaps, so no separate row) | 4 | `gametypes/grind.gsc:375,712`; gate `grind.gsc:8-10` | unknown |
| grnd_zone | trigger_multiple, gameobject or targetname `grnd` | 16 | `gametypes/grnd.gsc:322`; gate `grnd.gsc:11-13` | unknown |
| callout_area | trigger_multiple, targetname `callout_area` | 28 | `mp/callouts.gsc:13` | unknown |
| heli_route | script_struct_heli, targetname `ks_heli_*` | 21 | `killstreaks/helicopter.gsc:207,221,1182,1239` | unknown |
| heli_mesh | script_brushmodel, `heli_height_mesh` / `heli_pilot_mesh` | 2+2 | name lookup encrypted | unknown |
| killstreak_marker | script_origin `airstrikeheight` | 2 | `events.gsc:2828` | unknown |
| killstreak_marker | script_origin `heli_leave`; script_struct `nuke_expl_pos`; script_brushmodel `care_package_col` | 1+2+2 | UNKNOWN (no reader mapped) | unknown |
| airdrop | script_model `airdrop_crate`; script_brushmodel gameobject `airdrop_pallet` | 1+1 | UNKNOWN (in no gate list read) | unknown |
| minimap_corner | script_origin targetname `minimap_corner` | 4 | `scripts/mp/compass.gsc:12` (generic) | **yes** |
| camera_intro | script_struct targetname `camera_intro` | 2 | `spawncamera.gsc:32` | unknown |
| dz_flare, weapon_pickup_floor | script_struct `dz_flare`, noteworthy `floor` | 16, 20 | UNKNOWN | unknown |
| exploder | script_model, script_exploder (any value), targetname `exploder` | 28 + 14 | `common/exploder.gsc:52-101,82-83` (generic common script) | unknown |
| pf_chain_node | script_struct / script_brushmodel / script_model, targetname `pf*_auto*` | 230 | target chain read by `obj_bombzone.gsc:118,124` and dd (UNVERIFIED) | unknown |
| rugby_endzone | script_struct_mp_rugby_endzone, trigger_multiple_mp_rugby_endzone | 4 + 4 | `gametypes/rugby.gsc:174,185,240` | no |
| rugby_jugg_crate | script_struct_mp_rugby_jugg_crate | 6 | `rugby.gsc` (rugby only) | no |
| infil | script_struct_infil_*, script_model `infil_door_*` | 2 + 3 | `infilexfil/infilexfil.gsc` (backlot2 only) | no |
| export_volume | export_volume | 1 | not an accepted class (doc 40 s1) | no |
| reflection_probe | reflection_probe | 1 | not an accepted class (doc 40 s1) | no |
| collmap_scriptable | collmap_scriptable | 13 | not read by the IW8 Replay route (doc 41 s1.4) | no |
| nav_clip_brush | script_brushmodel `clip*`, `player*`, `mantle*`, `mount*`, `ladder*`, `nosight*`, `stairs*`, `tactical_*_col`, `smoke_grenade_sight_clip_*` | 72 | UNKNOWN (stock clip volumes) | unknown |
| out_of_bounds | trigger_multiple `OutOfBounds`, `kill_border_trigger` | 3 + 3 | `outofbounds.gsc` (encrypted); X64 `iw3_compat.gsc:53-78` | unknown |
| hurt_trigger | trigger_hurt | 3 | `gameobjects.gsc:4458` (classname lookup) | unknown |
| battle_chatter_location | trigger_multiple_bcs_* | 18 | `battlechatter_trigs.gsc` (encrypted) | unknown |
| intermission | mp_global_intermission | 2 | UNKNOWN | unknown |
| stock_delete_marker | script_struct targetname `delete_on_load` | 291 | `engine/utility.gsc:1604` (identity UNVERIFIED); do not author | unknown |
| dcover_blocker, white_phosphorus_damage | trigger_multiple | 6, 2 | UNKNOWN | unknown |

Exact rows, handler strings and maps are in the TSV; this table is a summary.

## 3. What "works" means here

- **yes** (1 row, minimap corners): generic MP script (`compass.gsc`), not tied to a gametype or map, and the editor
  already writes exactly two corners (doc 40 s1). Still needs an in-game check.
- **no**: the class is refused by the editor or converter, or the script is map-specific (infil, rugby, reflection
  probe, export volume, collmap scriptable). A custom map cannot get these from the editor as they stand.
- **unknown**: everything else. Gametype objects (dom, hardpoint, sd, ctf, cyber, dd, grnd, grind) run only in the
  gametype that keeps them through the gate (`gameobjects.gsc:17`, doc 40 s3.1 and s7), so a custom map gets them
  only in that mode and only if the editor template writes the right names. Those templates exist for dom, hardpoint,
  sd, ctf; the rest have none (doc 40 s8).

## 4. Open questions (need a game run or a fuller decompile)

1. Does `gameobjects::main` gate apply to a custom map in each mode (doc 40 s3.1)? Owner test per gametype.
2. Encrypted names: `ctf_zone`, `sd_pickup`, `cyber` defuse/zone lookups, `outofbounds`, `battlechatter` and the
   killstreak markers (`heli_leave`, `nuke_expl_pos`, `care_package_col`) have no readable handler.
3. `pf*_auto*` chains: which script reads them other than `obj_bombzone.gsc` and dd (UNVERIFIED).
4. `script_bombmode_original 1` meaning (UNVERIFIED).
5. Replay 1.20 parity of every handler path: the dump is the 8.19 build.
6. Stock labels 1-8 on script_struct (36 entities, mp_shipment) and `script_label` on triggers 6-13 (backlot2): not
   covered by any rule above (they fall under `pf_chain_node` or `grnd_zone`); role unknown.

## 5. Notes for the UI owner

- Use `ScriptedObjectCatalog::match` on the entity's keys (including `classname`). Several rows can match one entity;
  show the kind and the `works_in_custom_map` value of each.
- The TSV is generated. Re-run `py -3 -I tools/build_scripted_objects.py` after changing the rules, not the TSV.
