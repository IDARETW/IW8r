# 06 - Test assets inventory (MW2019 map editor, target Replay 1.20.4.7623265)

Target build: `E:\IW8\Builds\1.20-replay` (Replay 1.20.4.7623265).
Scope: read-only inventory of sources. No executables were run. Only this file was written.
Prepared 2026-11-08. Counting rules are given in section 1.

Key caveats up front:

- The owner checklist (`ZONETOOL_TEST_CHECKLIST.md`) says "Replay" but never records the 1.20.4.7623265 version or an exe hash for any owner session. The results in section 4 are attributed to the package build named in each entry, not to a verified exe version.
- No owner result exists for `mp_test`, the one small test map. It is the first map the checklist asks the owner to test.
- `E:\mw124-conversion-builds\_ents_nk` is **not** stock data. Its manifest points at converter output (`x64zt/output/mp_nuketown/mp_nuketown.ff`).
- `mods\mw120r\maps` is a directory junction to `/d/mw124-conversion-builds/game_maps` (D: drive), so the deployed custom maps live on D:, not in the Replay install folder.

---

## 1. Small .map files for edit / save / reopen tests

Location: `E:\CoD4\game_build\Call of Duty 4 Modern Warfare\map_source\` (format `iwmap 4`).

Counting rules (awk over the file, script kept in the scratchpad):
- entities = depth-0 `{` blocks;
- brushes = depth-1 `{` blocks containing face lines `( x y z ) ...` and no patch keyword;
- patches = depth-1 `{` blocks containing `curve`, `terrain` or `patchDef`;
- faces = face lines; materials = token after the third `( ... )` group of each face line (patch materials not counted).

| File | Bytes | Entities | Brushes | Patches | Faces | Materials (count) |
|---|---|---|---|---|---|---|
| `mp_test.map` | 12,654 | 44 | 11 | 0 | 66 | sky_chechnya 30, ch_tile_floor05 18, ch_tile_floor03 12, lightgrid_volume 6 (4 distinct) |
| `mp_test_mw2019.map` | 11,509 | 17 | 14 | 0 | 84 | sky_chechnya 36, ch_tile_floor05 24, clip 12, ch_tile_floor03 6, lightgrid_volume 6 (5 distinct) |
| `test.map` | 11,209 | 29 | 12 | 0 | 72 | sky_chechnya 36, ch_tile_floor05 24, ch_tile_floor03 6, lightgrid_volume 6 (4 distinct) |
| `test_traverses.map` | 35,515 | 116 | 33 | 0 | 196 | case512brown3 86, trigger 60, sky_aftermath 30, clip_player 10, clip_ai 6, caulk 4 (6 distinct) |
| `mp_backlot.map` | 12,380 | 68 | 0 | 0 | 0 | none (shell; geometry is in `mp_backlot_geo.map`) |
| `mp_backlot_geo.map` | 1,655,560 | 3,424 | 737 | 158 | 4,515 | caulk 1055, portal_nodraw 557, nodraw_decal 450, me_cinderblock_wall2 363, clip 276, me_decal_adobe_bottom_01 213, me_brick_01 193, mantle_on 186, me_pinewood 183, ch_concretewall02 173 |
| `blackout.map` | 698,227 | 3,378 | 316 | 0 | 1,940 | aitrig 404, flag 300, trigger 294, ambient 234, clip_nosight_metal 208, indoor_outdoor 96, volume 87, spawner_trigger 72, clip_ai 59, kill 30 |
| `blackout_geo.map` | 8,185,134 | 22,098 | 720 | 192 | 4,468 | clip_player 1006, lightgrid_volume 687, hint 492, ch_concretewall02 327, clip 250, caulk 245, clip_nosight_rock 226, clip_nosight 162, clip_ai 158, ch_concretewall08 150 |

Notes for the edit / save / reopen test:

- `mp_test.map` is the smallest map that has real geometry, a spawn set, a `misc_prefab` dependency, and a reflection probe. Its prefab references (`prefabs/mp/spawn_obj_tdm_aliies.map`, `prefabs/mp/spawn_obj_tdm_axis.map`) live in the same `map_source\prefabs\` folder, so a reopen test needs that folder present.
- `mp_test_mw2019.map` has no `misc_prefab` entries and no external prefab references (its 17 entities are `info_player_start`, `misc_model` x2, `mp_tdm_spawn` x9, allies/axis start spawns, and `worldspawn`). It is the simplest edit target: 14 brushes, 17 entities.
- `test.map` (12 brushes) is the other option with no patches.
- No patch keyword appears in the four small maps or in `mp_backlot.map`. Patches appear only in the two `_geo` maps (`blackout_geo.map` 192, `mp_backlot_geo.map` 158).
- `map_source\snapshots\mp_test.map.0` to `.4` (10,884 to 13,062 bytes, 2007-09-07) are earlier saves of `mp_test.map`. They were not counted.

---

## 2. Real IW8 stock data already extracted on disk

Stock = extracted from a stock fastfile in `E:\IW8\Builds\1.20-replay\zone\` (checked against each manifest's `fastfile` field). Every JSON/JSONL row has `"status":"ok"` where shown.

### 2a. Stock entity dumps (`map_ents`)

| Path | Format | Source fastfile | Stock map | Rows / entries | Examples |
|---|---|---|---|---|---|
| `E:\mw124-conversion-builds\_ents_stock\hideout\mw19replay\mp_hideout\assets.jsonl` | JSONL, 1 row | `zone\mp_hideout.ff` (manifest `complete:true`, `loaded_assets` 18,642) | mp_hideout | 1 row (`map_ents`, `maps/mp/mp_hideout.d3dbsp`, payload 5,868,041 B) | `{"type":"map_ents","type_id":29,"status":"ok","name":"maps/mp/mp_hideout.d3dbsp","field_issues":[],"field_nodes":84363,...}` |
| `...\mp_hideout\assets\map_ents\maps\mp\mp_hideout.d3dbsp.0.asset.json` | JSON (`mw19-asset-json`, pool 29 MapEnts) | same | mp_hideout | 1 file, 5.87 MB; 812 `classname` hits in the escaped entity string | `{"format":"mw19-asset-json","format_version":1,"profile":"replay-1.20","pool":"map_ents","pool_id":2...` |
| `...\mp_hideout\script_strings.json` | JSON array of strings | same | mp_hideout | array | `[ null, "death", "iw8_pi_cpapa", "iw8_sh_romeo870", "", "melee", ...` |
| `E:\mw124-conversion-builds\_ents_nk\mw19replay\mp_nuketown\assets.jsonl` | JSONL, 1 row | **`x64zt/output/mp_nuketown/mp_nuketown.ff` (converter output, not stock)** | mp_nuketown (converted) | 1 row, `map_ents`, `maps/mp/mp_nuketown.d3dbsp` | `{"type":"map_ents","type_id":29,"status":"ok","name":"maps/mp/mp_nuketown.d3dbsp",...` |
| `...\mp_nuketown\script_strings.json` | JSON array | converter output | mp_nuketown (converted) | array | `[ null, "", "mp_dm_spawn", "mp_tdm_spawn_allies_start", "mp_tdm_spawn", ...` |

Note: `_ents_nk` has 0 literal `classname` hits in its map_ents JSON. The entity string is stored in a non-plain form (`numEntityChars` 71,416), so it was not decoded.

### 2b. Stock common zones (`rumble_*`)

| Path | Format | Source fastfile | Rows | Example |
|---|---|---|---|---|
| `E:\mw124-conversion-builds\_ents_stock\rumble_common_mp\mw19replay\common_mp\manifest.json` | JSON manifest | `zone\common_mp.ff` | `loaded_assets` 10,479; `assets.jsonl` has 0 rows | `"fastfile": "E:/IW8/Builds/1.20-replay/zone/common_mp.ff"`, `"complete": true` |
| `...\rumble_common_core_mp\mw19replay\common_core_mp\manifest.json` | JSON manifest | `zone\common_core_mp.ff` | `loaded_assets` 2,213; `assets.jsonl` 0 rows | same pattern |
| `...\rumble_common_mp\...\script_strings.json` | JSON array of tag and anim strings | `common_mp.ff` | array | `[ null, "tag_origin", "tag_sync", "tag_view", "tag_ads", "tag_torso", "j_mainroot", ...` |

### 2c. Stock mp_shipment nav, image and cache dumps

| Path | Format | Source | Rows / entries | Examples |
|---|---|---|---|---|
| `E:\mw124-conversion-builds\_stock_ship_nav\mw19replay\mp_shipment\assets.jsonl` | JSONL, 3 rows | `zone\mp_shipment.ff` (`loaded_assets` 10,478) | 3 (`aipaths`, `navmesh`, `tacgraph`, all `maps/mp/mp_shipment.d3dbsp`) | `{"type":"aipaths","type_id":26,"status":"ok","name":"maps/mp/mp_shipment.d3dbsp",...` |
| `...\_stock_ship_nav\...\assets\aipaths\...\mp_shipment.d3dbsp.0.asset.json` and `navmesh`/`tacgraph` JSON | JSON | same | 3 | file names as above |
| `...\_stock_ship_nav\shipment_graph.bin` | binary | same | 62,468 bytes | not parsed |
| `E:\mw124-conversion-builds\_stock_ship_images\mw19replay\mp_shipment\assets\image\` | DDS (DX10) files | `zone\mp_shipment.ff` | 197 files in `assets\image` (217 `.dds` under the folder); `assets.jsonl` 1,710 rows | `$outdoor.0.dds` (404 B); `al_qatala_buckle_big_a_black_thermalmap.23.dds`; `codcaster_compass_map_mp_shipment.268.dds` |
| `E:\mw124-conversion-builds\_stock_cache\` (10 dirs: `mp_shipment-01F60FCAD157`, `mp_shipment-0507CF8E24D3`, `mp_shipment-6CB6CCCFCED5`, `eng_mp_shipment-C4931AF36064`, `ww_common*`, `ww_mp_shipment*`) | JSON per asset, JSONL journals, `complete.txt`, `acts.log` | `zone\mp_shipment.ff`, `eng_mp_shipment.ff`, `ww_common*.ff`, `ww_mp_shipment.ff` | 515 JSON asset files; 495 JSONL rows; `complete.txt` has 10 lines | `stock-export-1\|mp_shipment.ff\|187760776\|...\|xmodelsurfs\|fx_rifle_shell10v40\|fx_rifle_shell_lod110v40` |

### 2d. Stock mp_backlot2 xmodels and glass

| Path | Format | Source | Rows / entries | Examples |
|---|---|---|---|---|
| `E:\mw124-conversion-builds\_stock_backlot_xmodels\mw19replay\mp_backlot2\assets\xmodel\` | JSON per asset (`mw19-asset-json`, pool 9 XModel) | `zone\mp_backlot2.ff` (`loaded_assets` 12,784) | 1,058 JSON files; `assets.jsonl` 1,056 rows | `automotive_truck_loading_ramp_short.0.asset.json`; `body_mp_eastern_fireteam_east_sg_no_sling.1.asset.json`; `door_wood_ornate_01_blue_double_l.9.asset.json` |
| `E:\mw124-conversion-builds\_stock_backlot_glass\mw19replay\mp_backlot2\` | JSON per asset (`fx_map`, `glass_map`, `physicsfxshape`) + `assets.jsonl` | same | 16 JSON; `assets.jsonl` 14 rows | `glass_map\maps\mp\mp_backlot2.d3dbsp.12.asset.json`; `physicsfxshape\dismembered_gib_2.0.asset.json`; `phys_equipment_tv_01_destr_s1_f1.3.asset.json` |

### 2e. Asset-name lists (TSV) - these are converter output, not stock

These TSVs list names of the converted IW8 packages. Names carry the `mw120r/` prefix or a `generated variant` origin. They are inventories of the converter's output, not of stock data.

| Path | Format | Rows (data) | Columns | Example line |
|---|---|---|---|---|
| `E:\mw124-conversion-builds\discovery-zt-107.iw8-assets.tsv` (and `discovery_iw8-zt-107.iw8-assets.tsv`) | TSV | 5,925 | zone, type, name, origin | `mp_discovery	image	mw120r/mp_discovery_0_f0344dc6c9add064	texture of material w/mw120r_mp_discovery (generated variant)` |
| `E:\mw124-conversion-builds\mp_nuketown.iw8-assets.tsv` (also `nuketown-zt-107.iw8-assets.tsv`, `nuketown_iw8-zt-106.iw8-assets.tsv`) | TSV | 3,670 | same | `mp_nuketown	image	mw120r/mp_nuketown_0_4ffb5be3ac7e05d4	texture of material w/mw120r_mp_nuketown (generated variant)` |
| `E:\mw124-conversion-builds\gulag-zt-L6.iw8-assets.tsv` | TSV | 3,597 | same | `mp_gulag_cod4	image	mw120r/mp_gulag_cod4_0_599545042932a2cb	...` |
| `E:\mw124-conversion-builds\mp_nuketown.iw3-assets.tsv` | TSV (IW3 source inventory) | 5,378 | zone, type, name, fate, detail | `common_mp	techniqueset	sm2/2d	shared-unused	IW3 shared zone common_mp ...` |

Origins for the Discovery and Nuketown IW8 TSVs: about 1,120 and 740 are "generated material variant", about 860 and 480 are texture-of-material rows, and 267 are "stock effect of surface-fx table void (copied from mp_shipment)". These are the only stock-sourced rows in those lists.

### 2f. Other extracted text that is not stock

- `E:\mw124-conversion-builds\_script_tests\local\<map>\mp_<map>.gsc` and `compiled\iw8\*.gscbin` (161 files). Header reads `// Converted from IW3 map scripts by iw8-zonetool for Replay 1.20.` These are converted scripts, not stock IW8 scripts.
- `E:\IW8\Builds\1.20-replay\main\mp_test.psob` (318,424 bytes, 2026-10-01, magic `\0\0\0PSO`). Provenance not established; not parsed. Other `.psob` files in the same folder (`mp_nuketown.psob`, `mp_nuked.psob`, `mp_4doffice.psob`) follow the same pattern.
- `E:\mw124-conversion-builds\_map_images.log` and `_map_images\map_images.txt` list 11 map ids (`mp_aerodrome` to `mp_poolday`) and the loading and featured material writes. These are converter logs.

---

## 3. Existing compiled / converted map outputs

### 3a. CoD4 fastfiles (`E:\CoD4\game_build\Call of Duty 4 Modern Warfare\zone\english\`)

| File | Date | Size (bytes) | Note |
|---|---|---|---|
| `mp_backlot.ff` | 2007-10-04 | 39,998,207 | stock CoD4 |
| `mp_shipment.ff` | 2007-10-04 | 29,040,480 | stock CoD4 |
| `common_mp.ff` | 2007-10-04 | 13,517,381 | stock CoD4 |
| `mp_backlot_load.ff` | 2007-10-04 | 258 | stock CoD4 |
| `mp_shipment_load.ff` | 2007-10-04 | 261 | stock CoD4 |
| `mp_mw120r_lighting_v20.ff` | 2026-09-07 | 2,596,560 | **custom** (not stock); purpose not documented in sources read |

- No `mp_test*.ff` exists under `E:\CoD4` (searched recursively). The custom test map has no CoD4 `.ff`.
- `map_source\` holds only `.map` sources, prefabs and snapshots (no compiled output).

### 3b. IW8 five-fastfile packages and `map.json`

Deployed folder: `game_maps` on the D: drive, reached through the junction `E:\IW8\Builds\1.20-replay\mods\mw120r\maps`. Each package is `<map>.ff`, `eng_`, `srv_`, `ww_`, `techsets_` plus `map.json`. Sizes are in bytes.

| Package | Location | Date | Main FF size | Other files | map.json |
|---|---|---|---|---|---|
| mp_test | `D:\...\game_maps\mp_test\` | 2026-10-29 | `mp_test.ff` 258,102,414 | eng 350,598; srv 172; techsets 508,056; ww 295,637 | `{"id":"mp_test","title":"mp_test"}` (44 B) |
| mp_test (baseline) | `E:\mw124-conversion-builds\baselines\mp_test-20261107\` | 2026-11-07 | `mp_test.ff` 258,102,414 | same five plus `SHA256SUMS`, `gate.txt`, `README.txt` | 44 B |
| mp_nuketown | `D:\...\game_maps\mp_nuketown\` | 2026-10-29 | `mp_nuketown.ff` 953,267,155 | eng 350,632; srv 172; techsets 1,136,767; ww 295,637; `preview.rgba` 4,194,320 | 74 B, title "Nuketown", compassRotation 90 |
| mp_nuketown (proxy backup A) | `E:\IW8\Builds\1.20-replay\.proxy\backups\mw120r-map-20261017-173632-156\mp_nuketown\` | 2026-10-17 | `mp_nuketown.ff` 1,167,492,402 | eng 801; srv 172; techsets 1,322,014; ww 172; preview 4,194,320 | 74 B |
| mp_nuketown (proxy backup B) | `E:\IW8\Builds\1.20-replay\.proxy\backups\mw120r-map-20261017-174638-252\mp_nuketown\` | 2026-10-17 | `mp_nuketown.ff` 1,167,492,912 | same pattern | 74 B |
| mp_nuketown_c | `D:\...\game_maps\mp_nuketown_c\` | 2026-10-28 | `mp_nuketown_c.ff` 958,869,921 | eng 350,648; srv 172; techsets 1,116,511; ww 295,637; preview 4,194,320 | 86 B |
| mp_discovery | `D:\...\game_maps\mp_discovery\` | 2026-10-29 | `mp_discovery.ff` 1,489,573,206 | eng 350,654; srv 172; techsets 1,665,981; ww 295,637; preview | 92 B |
| mp_discovery_8 | `D:\...\game_maps\mp_discovery_8\` | 2026-10-28 | `mp_discovery_8.ff` 1,489,249,139 | eng 350,670; srv 172; techsets 1,703,835; ww 295,637; preview | 84 B |
| mp_4doffice | `D:\...\game_maps\mp_4doffice\` | 2026-10-29 | `mp_4doffice.ff` 681,340,518 | eng 350,638; srv 172; techsets 897,792; ww 295,637; preview | 77 B |
| mp_iw7_brkneck | `D:\...\game_maps\mp_iw7_brkneck\` | FF and techsets 2026-11-08; others 2026-11-07 | `mp_iw7_brkneck.ff` 2,973,142,801 | `techsets_mp_iw7_brkneck.ff` 1,426,690,396; eng 350,658; srv 172; ww 295,637; preview 614,416 | 59 B |
| mp_nuketowwn | `D:\...\game_maps\mp_nuketowwn\` | 2026-09-12 | `mp_mp_nuketowwn.ff` 355,848,784 (name has `mp_mp_`) | srv 10,863,217; eng, techsets, ww 172 each (looks like stubs) | 54 B |

Notes:

- `mp_nuketowwn` is the only package where eng, techsets and ww are 172 bytes and the main FF name has a doubled `mp_` prefix. Treat it as an incomplete or stale package.
- The `game_maps` folder holds 8 package folders plus archives. Archives (not inspected): `mp_test.rar` 28,118,944 (2026-09-10), `mp_iw7_frontier.rar` 605,216,242 (2026-10-22), `mp_4doffice.rar` 54,099,985, `mp_dr_sm64.rar` 45,592,222, `mp_nuked.rar` 59,762,784.
- `maps_parked` (also on D:) holds 15 converted IW3 packages: mp_aerodrome(_8), mp_agroprom(_8), mp_arnhem(_8), mp_broadcast(_8), mp_crash_253, mp_dr_sm64, mp_greenband(_8), mp_gulag_cod4(_8), mp_poolday. No Frontier, Scorch or Breakneck package is in `maps_parked`.
- The IW7 Frontier package is present only as `mp_iw7_frontier.rar` in `game_maps`. No folder for it exists.

---

## 4. In-game evidence on the 1.20 Replay build

Sources: `E:\mw124\mw120rproxy\ZONETOOL_TEST_CHECKLIST.md` (the only file with owner results). `REPLAY_WORKSTATION_TESTS.txt` (2026-10-03) is a test procedure with no run results; it states "This cleanup/deployment pass did not launch the game." `IMPORTED_MAPS.md` is a conversion guide with no owner results.

Classes: **LOADED** = owner reports the map loaded and played; **FAILED** = owner reports a load or play failure for that build; **NOT-VERIFIED** = no owner load or play result found.

| Map (converted) | Owner-recorded result (short quote) | file:line | Date (section heading) | Class |
|---|---|---|---|---|
| mp_iw7_frontier (42b9b9b build) | "The owner confirms that this Frontier package loads into a Team Deathmatch match." | `ZONETOOL_TEST_CHECKLIST.md:808` | 2026-10-25 (heading line 798) | **LOADED** |
| mp_iw7_frontier (a6a61b7 build) | "Owner test: shadows still appear only near the camera." | `ZONETOOL_TEST_CHECKLIST.md:795` | 2026-10-25 (heading line 772) | **LOADED** (played) |
| mp_iw7_frontier (3492e9e build) | "The owner still saw the error on Scorch and Frontier afterward." (`unknown function` at TDM script load) | `ZONETOOL_TEST_CHECKLIST.md:871`, `:800` | 2026-10-25 early morning (heading 868); 2026-10-25 (heading 798) | **FAILED** (superseded by 42b9b9b) |
| mp_iw7_frontier (300ac8a build, lights) | "Frontier lights (300ac8a): fixed ... you confirmed it in game." | `ZONETOOL_TEST_CHECKLIST.md:1051` | undated "Done today" section (line 1049), after the 2026-10-23 deploys | **LOADED** |
| mp_iw7_frontier (current package) | Only startup: "The owner restarts Windows and confirms Frontier opens and runs." (not a map load) | `ZONETOOL_TEST_CHECKLIST.md:853` | 2026-10-25 (heading 846) | NOT-VERIFIED (as a map load) |
| mp_iw7_scorch (42b9b9b build) | "The owner confirms Scorch reaches TDM. The sun effects, scripted motion, ground textures, and minimap still fail." | `ZONETOOL_TEST_CHECKLIST.md:813` | 2026-10-25 (heading 798) | **LOADED** (with listed failures) |
| mp_iw7_scorch (earlier build) | "The owner reports `unknown function` during TDM script loading on both Frontier and Scorch." | `ZONETOOL_TEST_CHECKLIST.md:800` | 2026-10-25 (heading 798) | **FAILED** (fixed by 42b9b9b) |
| mp_iw7_brkneck (b28905c) | "The owner's test of b28905c was the last game test." Reports listed: "whole items cycling textures, sky-debris trails, the bright light cone." | `ZONETOOL_TEST_CHECKLIST.md:229`, `:242` | 2026-11-06 night (heading 227) | **LOADED** (played; visual defects listed) |
| mp_iw7_brkneck (b2ac0c6 to 3e48788, pre-ea50c10) | "Live test in your match (22:03): map_refresh now works ..." | `ZONETOOL_TEST_CHECKLIST.md:27` | 2026-11-07 22:05 (heading 19) | **LOADED** (played) |
| mp_iw7_brkneck (map_refresh) | "The first live map_refresh crashed the game (Fatal Error 0x1411DBCF8 ...)" | `ZONETOOL_TEST_CHECKLIST.md:21` | 2026-11-07 22:05 (heading 19) | **FAILED** (command crash; fixed) |
| mp_iw7_brkneck (cad8877 build) | "drawing the refracting magnifying glass crashed the game again ~20 s later" | `ZONETOOL_TEST_CHECKLIST.md:28` | 2026-11-07 22:05 (heading 19) | **FAILED** (reverted in ea50c10) |
| mp_iw7_brkneck (hinged clutter, since 246412c) | "Your Fatal Error (0x141843764 in game_dx12_ship_replay_local_dw.exe, 0xC0000005)" | `ZONETOOL_TEST_CHECKLIST.md:71` | 2026-11-07 night (heading 69) | **FAILED** (fixed in zslL package) |
| mp_iw7_brkneck (ea50c10, zslL, current) | "test this one" - no owner result after it | `ZONETOOL_TEST_CHECKLIST.md:9`, `:79` | 2026-11-07 (heading 9 and 69) | **NOT-VERIFIED** |
| mp_discovery (earlier build, before 2026-10-29 rebuild) | "Discovery (your report): the snow on the dome hangars." | `ZONETOOL_TEST_CHECKLIST.md:286` | 2026-10-29 later (heading 275) | **LOADED** (earlier build). Current 2026-10-29 package: NOT-VERIFIED |
| mp_test | "Check that it loads and plays." (request only) | `ZONETOOL_TEST_CHECKLIST.md:343` | 2026-10-28/29 (heading 309, Stage 1 at 341) | **NOT-VERIFIED** |
| mp_nuketown (IW3) | "Please compare Nuketown with before." (request); "IW3 Nuketown and Discovery: fresh builds come after..." (open item) | `ZONETOOL_TEST_CHECKLIST.md:967`, `:1060` | 2026-10-24 evening (heading 946); open list at 1057 | **NOT-VERIFIED** |
| mp_nuketown_c, mp_4doffice, mp_discovery_8, other IW3 packages | none found | (no entry) | - | **NOT-VERIFIED** |
| mp_dr_sm64 (Super Mario 64) | "Test: each of those four loads and spawns you" (request only) | `ZONETOOL_TEST_CHECKLIST.md:1012` | Overnight 2026-10-24 (heading 988) | **NOT-VERIFIED** |
| mp_nuketowwn (Sept 2026 package) | "your mp_nuketowwn" in a switch-maps list only | `ZONETOOL_TEST_CHECKLIST.md:1001` | Overnight 2026-10-24 (heading 988) | **NOT-VERIFIED** |
| IW8 test replay workstation (custom map load step) | Procedure only; "This cleanup/deployment pass did not launch the game" | `REPLAY_WORKSTATION_TESTS.txt`, header block | 2026-10-03 | **NOT-VERIFIED** |

Summary of classes:

- LOADED on an identified build: mp_iw7_frontier (42b9b9b; and the 300ac8a lights build), mp_iw7_scorch (42b9b9b), mp_iw7_brkneck (b28905c and the b2ac0c6 to 3e48788 range), mp_discovery (earlier build).
- FAILED on an identified build (later fixed): mp_iw7_frontier (3492e9e, unknown-function), mp_iw7_scorch (earlier build, unknown function), mp_iw7_brkneck (map_refresh, cad8877, hinged clutter).
- NOT-VERIFIED: mp_test, mp_nuketown, mp_nuketown_c, mp_4doffice, mp_discovery_8, mp_dr_sm64, mp_nuketowwn, mp_iw7_brkneck current package (ea50c10 and zslL), mp_iw7_frontier current package, and all other converted maps. None has an owner result in the sources read.

Limits of this section:

- The checklist does not record the exe version or hash for these sessions, so "on the 1.20 Replay build" is an attribution by context, not a verified version match.
- Only the checklist lines quoted here were located by search; the file is 100 KB and was grepped, not read in full.

---

## Gaps and next steps (for the test plan)

1. `mp_test.map` (CoD4 source) plus `game_maps\mp_test` (IW8 package, 2026-10-29; the same file sizes as `baselines\mp_test-20261107`, hashes not compared) is the best edit / save / reopen fixture. No owner load result exists for it; the first in-game load is still open.
2. The Frontier and Scorch packages reported loading in TDM are not in `game_maps` or `maps_parked`. Only `mp_iw7_frontier.rar` exists. A reproducible LOADED fixture would need that rebuild.
3. `mp_nuketowwn` looks stale or incomplete. Do not use it as a fixture.
4. Stock fixtures for the editor: `_ents_stock\hideout` (mp_hideout map_ents), `_stock_ship_nav` (mp_shipment aipaths/navmesh/tacgraph), `_stock_backlot_xmodels` (1,058 mp_backlot2 xmodels).
