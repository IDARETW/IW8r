# 10 IW8 entity-class inventory (replay-1.20.4.7623265)

Stock map_ents inventory for the IW8 Radiant, built from real 1.20 replay zone data. Machine-readable output: `E:\IW8-Radiant\data\iw8_entity_classes.json`. Raw dumps: `E:\IW8-Radiant\data\stock_dumps\<zone>`.

## Command used

Found in `E:\mw124-conversion-builds\validate_mapsc.sh` (line 29), the same pattern as `replay_stock.cpp` (`-a <types>`):

```
acts.exe --noUpdater fastfile -r mw19replay -g "E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe" --oodle "E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll" -a map_ents -o <outdir> "E:/IW8/Builds/1.20-replay/zone/<zone>.ff"
```

`-a map_ents` restricts the export to the map_ents asset type, so the export is limited to that one type. Run per zone with `<outdir> = E:\IW8-Radiant\data\stock_dumps\<zone>`. Output lands in `<outdir>\mw19replay\<zone>`. Same form as the existing hideout dump in `E:\mw124-conversion-builds\_ents_stock\hideout`. The `--test`, `-n`, and `-l` flags were not used.

## Zones

| Zone | .ff present | Result | Entities (trigger/script/pathnode records) | spawnList records | Mapped spawn names |
|---|---|---|---|---|---|
| mp_shipment | yes | dumped, exit 0 | 611 | 611 | 39 |
| mp_backlot2 | yes | dumped, exit 0 | 3234 | 430 | 29 |
| mp_crash2 | no (only `.xpak`, no `.ff`) | skipped | - | - | - |
| mp_hackney_yard | no (only `.xpak`, no `.ff`) | skipped | - | - | - |
| mp_runner | no (only `.xpak`, no `.ff`) | skipped | - | - | - |

Entity string length matched `numEntityChars` in both zones (mp_shipment 62892, mp_backlot2 295431). Each entity string ends with one trailing NUL byte, which the parser ignores. `spawnList` indexes into `script_strings.json` resolved with no out-of-range index in either zone.

Sources: `mp_shipment`, `mp_backlot2`. Total classes: 35. Total entities: 3845. Spawn records: 1041 (39 distinct names).

## Top 40 classes

Only 35 classes exist in the two zones, so the table lists all of them. Keys are the top 8 by count per class.

| # | Class | Count | Maps | Common keys (count) |
|---|---|---|---|---|
| 1 | `node_pathnode` | 2096 | mp_shipment, mp_backlot2 | classname (2096), footprint_mask_group (2096), origin (2096), angles (38), spawnflags (10) |
| 2 | `script_struct` | 563 | mp_shipment, mp_backlot2 | classname (563), footprint_mask_group (563), origin (563), targetname (561), angles (527), model (329), _color (230), script_label (36) |
| 3 | `node_negotiation_end` | 293 | mp_shipment, mp_backlot2 | angles (293), classname (293), footprint_mask_group (293), origin (293), targetname (293) |
| 4 | `node_negotiation_begin` | 293 | mp_shipment, mp_backlot2 | angles (293), animscript (293), classname (293), footprint_mask_group (293), origin (293), target (293), targetname (293) |
| 5 | `node_3d_pathnode` | 206 | mp_backlot2 | angles (206), classname (206), footprint_mask_group (206), origin (206) |
| 6 | `script_brushmodel` | 109 | mp_shipment, mp_backlot2 | classname (109), footprint_mask_group (109), model (109), origin (109), targetname (101), spawnflags (100), physasset (86), script_gameobjectname (33) |
| 7 | `trigger_multiple` | 78 | mp_shipment, mp_backlot2 | classname (78), footprint_mask_group (78), model (78), origin (78), physasset (78), targetname (78), script_gameobjectname (36), script_label (34) |
| 8 | `script_model` | 62 | mp_shipment, mp_backlot2 | classname (62), footprint_mask_group (62), model (62), origin (62), targetname (62), script_gameobjectname (58), angles (53), spawnflags (44) |
| 9 | `trigger_use_touch` | 31 | mp_shipment, mp_backlot2 | classname (31), footprint_mask_group (31), model (31), origin (31), physasset (31), script_gameobjectname (31), targetname (31), target (14) |
| 10 | `script_struct_heli` | 21 | mp_shipment, mp_backlot2 | _color (21), classname (21), footprint_mask_group (21), origin (21), radius (21), script_goalyaw (21), script_linkto (21), targetname (21) |
| 11 | `trigger_radius` | 21 | mp_shipment, mp_backlot2 | classname (21), footprint_mask_group (21), height (21), origin (21), radius (21), targetname (21), script_gameobjectname (18), nophysics (13) |
| 12 | `collmap_scriptable` | 13 | mp_shipment, mp_backlot2 | classname (13), model (13), targetname (13) |
| 13 | `script_origin` | 7 | mp_shipment, mp_backlot2 | classname (7), origin (7), targetname (7), footprint_mask_group (6), radius (2) |
| 14 | `script_struct_mp_rugby_jugg_crate` | 6 | mp_shipment, mp_backlot2 | angles (6), classname (6), footprint_mask_group (6), model (6), origin (6), targetname (6) |
| 15 | `trigger_use` | 6 | mp_shipment, mp_backlot2 | classname (6), footprint_mask_group (6), model (6), origin (6), physasset (6), script_gameobjectname (6), targetname (6) |
| 16 | `script_struct_mp_rugby_endzone` | 4 | mp_shipment, mp_backlot2 | classname (4), footprint_mask_group (4), origin (4), spawnflags (4), target (4), targetname (4), angles (1) |
| 17 | `trigger_multiple_mp_rugby_endzone` | 4 | mp_shipment, mp_backlot2 | classname (4), footprint_mask_group (4), model (4), origin (4), physasset (4), targetname (4) |
| 18 | `trigger_multiple_bcs_containers` | 4 | mp_shipment, mp_backlot2 | classname (4), footprint_mask_group (4), model (4), origin (4), physasset (4), angles (1) |
| 19 | `trigger_hurt` | 3 | mp_shipment, mp_backlot2 | classname (3), dmg (3), footprint_mask_group (3), model (3), origin (3), physasset (3) |
| 20 | `info_player_start` | 3 | mp_shipment, mp_backlot2 | angles (3), classname (3), origin (3), footprint_mask_group (2) |
| 21 | `worldspawn` | 2 | mp_shipment, mp_backlot2 | _primarylightcount (2), __smuid (2), classname (2), csm_enable (2), csm_max_resolution (2), csm_sample_size (2), forwardplus (2), highresgpulightgrid (2) |
| 22 | `mp_global_intermission` | 2 | mp_shipment, mp_backlot2 | angles (2), classname (2), footprint_mask_group (2), origin (2) |
| 23 | `trigger_multiple_bcs_center` | 2 | mp_shipment, mp_backlot2 | classname (2), footprint_mask_group (2), model (2), origin (2), physasset (2) |
| 24 | `trigger_multiple_bcs_dumpster` | 2 | mp_shipment | classname (2), footprint_mask_group (2), model (2), origin (2), physasset (2) |
| 25 | `trigger_multiple_bcs_forklift` | 2 | mp_shipment | classname (2), footprint_mask_group (2), model (2), origin (2), physasset (2) |
| 26 | `trigger_multiple_bcs_backalley` | 2 | mp_backlot2 | classname (2), footprint_mask_group (2), model (2), origin (2), physasset (2) |
| 27 | `trigger_multiple_bcs_destroyedbldg` | 2 | mp_backlot2 | classname (2), footprint_mask_group (2), model (2), origin (2), physasset (2) |
| 28 | `reflection_probe` | 1 | mp_backlot2 | angles (1), classname (1), footprint_mask_group (1), index (1), origin (1), script_noteworthy (1), spawnflags (1), targetname (1) |
| 29 | `script_struct_infil_van_hackney_alpha` | 1 | mp_backlot2 | angles (1), classname (1), footprint_mask_group (1), model (1), name (1), origin (1), script_noteworthy (1), script_team (1) |
| 30 | `script_struct_infil_lbravo_alpha` | 1 | mp_backlot2 | angles (1), classname (1), footprint_mask_group (1), model (1), name (1), origin (1), script_noteworthy (1), script_team (1) |
| 31 | `export_volume` | 1 | mp_backlot2 | classname (1), filterpreset (1), footprint_mask_group (1), model (1), origin (1), target (1), targetname (1), volume_intersection_test (1) |
| 32 | `trigger_multiple_bcs_construction` | 1 | mp_backlot2 | classname (1), footprint_mask_group (1), model (1), origin (1), physasset (1) |
| 33 | `trigger_multiple_bcs_garage` | 1 | mp_backlot2 | classname (1), footprint_mask_group (1), model (1), origin (1), physasset (1) |
| 34 | `trigger_multiple_bcs_laundromat` | 1 | mp_backlot2 | classname (1), footprint_mask_group (1), model (1), origin (1), physasset (1) |
| 35 | `trigger_multiple_bcs_mainstreet` | 1 | mp_backlot2 | classname (1), footprint_mask_group (1), model (1), origin (1), physasset (1) |

## Spawn names

Top spawn names from `spawnList` (count across both zones):

| Name | Count |
|---|---|
| `mp_koth_spawn` | 82 |
| `mp_ctf_spawn` | 73 |
| `mp_tdm_spawn` | 70 |
| `mp_dom_spawn` | 69 |
| `mp_dm_spawn` | 66 |
| `mp_dd_spawn_attacker` | 29 |
| `mp_tdm_spawn_secondary` | 26 |
| `mp_dom_spawn_secondary` | 26 |
| `mp_koth_spawn_secondary` | 26 |
| `mp_dm_spawn_secondary` | 26 |
| `mp_ctf_spawn_secondary` | 26 |
| `mp_dd_spawn_defender` | 25 |
| `mp_tdm_spawn_allies_start` | 22 |
| `mp_tdm_spawn_axis_start` | 22 |
| `mp_dom_spawn_allies_start` | 22 |
| (24 more) | |

## Keys with ids not in the table

`iw8_canonical_keys.tsv` has no entry for these ids. They are written as `id:<n>` in the JSON:

| Id | Zone | Occurrences | Context |
|---|---|---|---|
| 0 | mp_backlot2 | 3 | Appears on `script_struct` records. Id 0 is absent from the table. |

All other key ids in both zones resolved to names.

## Failures and notes

- `mp_crash2`, `mp_hackney_yard`, `mp_runner` were skipped. Each has only `.xpak` files in `zone` (no `<zone>.ff`), and acts.exe takes the `.ff` as input. Those zones' map_ents would need a different input path, which was not attempted.
- Both acts.exe runs exited 0 without prompting for input and well under the 10-minute limit.
- The parser treats the entity string as `{ ... }` blocks of `<key-id> "<value>"` lines. Lines that do not match are counted as `bad_lines`. The only ones were the trailing NUL bytes.
- `spawnList` count (1041) is separate from entity count. Spawn records are script spawn points and are not entities.
- Stock zones only, with no custom maps.

