# 40. Stock scripted objects: which scripts they need, and how a custom map gets them (roadmap 6.13)

Status: research only. Nothing was built, run or written except this file. The game and ACTS were not run; the stock
entity dumps and GSC source were read. Every behaviour claim that depends on the running game is marked UNVERIFIED.

## 0. Sources and caveats

- Stock entities: `data/stock_dumps/mp_shipment` (611 blocks) and `data/stock_dumps/mp_backlot2` (3,234 blocks), and the
  cache `%USERPROFILE%\AppData\Local\IW8 Radiant Suite\IW8 Radiant Suite\stock_cache\mp_shipment`. The `fields.entityString.bytes`
  hex was decoded (scratch only) and keys were resolved with `data/generated/iw8_canonical_keys.tsv`. Same method as doc 10 and doc 32.
- GSC source: `E:\mw124\tools\dvar_search_web\src\combined\rawfile_source\scripts\mp` (cited below as `RAW/`). Its README says it was dumped
  from "IW8 8.19 build". It is NOT the Replay 1.20 build. The x64-zt comments refer to decompiled 1.20 scripts of common_base_mp, but
  that tree is not in the repo. Every GSC citation is therefore to the 8.19 dump; Replay 1.20 parity is UNVERIFIED.
- Most gametype identifiers and key names are encrypted in that decompile as `_encstr_<hex>`. Section 6 lists the names inferred for them and the evidence. Anything not in section 6 is named by its position only.
- Fork: `E:\mw124\x64-zt` (cited as `X64/`). Editor: `E:\IW8-Radiant` (cited as `ED/`).

## 1. Summary table

Counts are over both stock zones (shipment + backlot2). "Works today" is the editor's current state, with no in-game test.

| Object (classname) | Keys in stock | Handled by (RAW/) | Zone (see 4) | Works on a custom map today? | How to make it work |
|---|---|---|---|---|---|
| DOM capture trigger (`trigger_radius`, 6) | gameobj `dom onslaught`, targetname `flag_primary`, label `_a`/`_b`/`_c`, radius 120, height 128, nophysics | `gametypes/dom.gsc:351`; gate `gameobjects.gsc:17-53` (token `dom`) | stock gametype scripts (see 4) | Likely. `dom_flags` template writes these. Not owner-tested. | Keep template. Flag prop optional (5.a, R3). |
| DOM flag prop (`script_model`) | none in stock | none needed: `obj_dom.gsc:9-20` spawns its own marker when the trigger has no target | n/a | Not needed | Optional decoration only (X64 `iw3_fastfile.cpp:10170-10196`). |
| Hardpoint zone (`trigger_multiple` brush, 18) | gameobj `hardpoint`, targetname `hardpoint_zone`, label `1`-`N`, target `hardpoint_<n>_chevrons` (no such entity) | `koth.gsc:805-838` (getzonearray); `hq.gsc:848-886`; gate `gameobjects.gsc` (token `hardpoint`) | stock gametype | Likely for koth. `koth.gsc` never reads `.target`, so the missing chevron target is harmless there. | Keep template. Add an hqloc radio for hq mode (R2). |
| HQ radio (`script_model`, 18 radios; 36 hqloc entities with the 18 `script_brushmodel` hqloc) | gameobj `hqloc`, targetname `hqloc`, target | `hq.gsc:885` (getentarray of targetname); gate list `hq.gsc:11` | stock gametype | Gap: no template | Add template (R2). Converter precedent: X64 `maps.cpp:668-672`. |
| SD site (`trigger_use_touch` brush, 4 labelled) | gameobj `bombzone`, targetname `bombzone`, label `_a`/`_b`, `script_bombmode_original 1`, target `pfN_auto` chain | `sd.gsc:470`; `obj_bombzone.gsc:17, 118, 124` | stock gametype | Gap: template has no target chain. `obj_bombzone.gsc:118/124` read `var_5[0].target`; with no target this is likely a script error (UNVERIFIED). | Add a target chain to a defuse trigger (R1). |
| SD bomb and pickup (`script_model` `sd`, `trigger_multiple` `sd`) | gameobj `sd`; targetname `sd_bomb`, `sd_bomb_pickup_trig` | gate list `sd.gsc:11-14`; targetname lookups encrypted | stock gametype | Partial: template has both. Bomb behaviour UNVERIFIED. | Keep. Verify in game. |
| CTF flags (`trigger_radius`, `script_model`, zone `trigger_radius`, 12) | gameobj `ctf`; targetnames `ctf_trig_<team>`, `ctf_flag_<team>`, `ctf_zone_<team>`; `target ctf_flag_<team>` | `ctf.gsc:500-509` (getflagpos, createteamflag); gate `ctf.gsc:9-10` | stock gametype | Likely. Team names confirmed by the fork's own comment (X64 `iw3_fastfile.cpp:10224-10228`). Base origins from `flag_primary` labels not placed (see 5.a). | Keep. Optionally add `flag_primary` labelled triggers (R4). |
| Cyber EMP (`script_model` cyber_emp, `trigger_use` pickup, `trigger_use_touch` cyber_empzone/defuse, gameobj `cyber`) (32 cyber objects) | gameobj `cyber`, `bombzone`; targetnames `cyber_emp`, `cyber_emp_pickup_trig`, `cyber_empzone_<team>`, `cyber_emp_defuse_<team>` | `cyber.gsc:345-492` (four getent of defuse/zone names), gate `cyber.gsc:11-13` | stock gametype | Gap: no template. Classes accepted by the editor (`compile_iw8.cpp:489-508`). | Template (R4). |
| DD bombzone (`trigger_use_touch` dd_bombzone, `script_model` exploder, `script_brushmodel` dd_bombzone_clip_a/b/c) (30) | gameobj `dd_bombzone`; targetnames `dd_bombzone`, `pfN_auto`, `exploder`, clips | `dd.gsc:14, 490-527`; gate `dd.gsc:11-14` | stock gametype | Gap: no template | Template (R4). Exploder path UNVERIFIED. |
| Grind location (`trigger_radius`, 4) | gameobj `grind`; targetname `grind_location` | `grind.gsc:375-389` | stock gametype | Gap: no template | Template (R4). |
| Ground zone (`trigger_multiple`, 16) | gameobj `grnd`; targetname `grnd`; labels | `grnd.gsc:322-325, 385-417`; gate `grnd.gsc:11-13` | stock gametype | Gap: no template | Template (R4). |
| Callout area (`trigger_multiple`, targetname `callout_area`, noteworthy = area id, 25+) | noteworthy area names (`gate`, `van`, `laundry`, ...) | `callouts.gsc:13, 79, 97` | always-loaded or map zone (4) | Gap: area ids are map-specific | Template per map area (R4). |
| Helicopter route (`script_struct_heli` ks_heli_entrance/goal/path, 21) | targetname chain via `target` | `killstreaks/helicopter.gsc:207, 221, 1182, 1239` | killstreak scripts (4) | Accepted class (`compile_iw8.cpp:503`), no template; chain must be hand-made | Template (R4). |
| Heli pilot and height mesh (`script_brushmodel`, noteworthy) | noteworthy `heli_pilot_mesh`, `heli_height_mesh` | UNKNOWN (encrypted); helicopter scripts | killstreak scripts | Not templated | Hand-author only after mapping (R7). |
| Infil (`script_struct_infil_*`, targetname `mp_infil`, noteworthy `infil_van_hackney` / `infil_lbravo`, `script_model` `infil_door_*`, `export_volume`) (backlot2 only) | as listed | `infilexfil/infilexfil.gsc:105, 272-274, 1214-1229` | infil scripts | Refused: `script_struct_infil_*`, `export_volume`, `reflection_probe` are not accepted by the editor (`compile_iw8.cpp:489-508`) and the converter list (`maps.cpp:179-216`) | Needs class work in both (F3), then verification. Not recommended for 6.13. |
| Rugby (`script_struct_mp_rugby_endzone`, `script_struct_mp_rugby_jugg_crate`, `trigger_multiple_mp_rugby_endzone`, targetnames `allies_endzone`, `axis_endzone`) (14) | targetname, target | `gametypes/rugby.gsc:174, 185, 240` | rugby scripts | Refused by the editor's class list (`compile_iw8.cpp:489-508`); `trigger_multiple_mp_rugby_endzone` is not in the `trigger_multiple_bcs_` prefix rule | Class work in both (F3). Not for 6.13 first pass. |
| Battle-chatter location (`trigger_multiple_bcs_<loc>`, 18) | none on the entity; location by classname | `battlechatter_trigs.gsc` (lookups encrypted) | battle chatter scripts | Accepted by prefix (`compile_iw8.cpp:508`); no template | Hand-author if needed. Not templated. |
| Minimap corners (`script_origin` targetname `minimap_corner`, 4) | targetname | `compass.gsc:12` | always-loaded or map (4) | Works: the editor writes exactly two (`ED/src/core/compile_iw8.cpp:1459-1472`) | None. |
| Camera intro (`script_struct` targetname `camera_intro`, 2) | targetname | `spawncamera.gsc:32` | camera scripts | Accepted class, no template | Template optional (R4). |
| Out-of-bounds (`trigger_multiple` OutOfBounds, kill_border_trigger, 6) | targetname | `outofbounds.gsc` (lookups encrypted) | always-loaded or map | Editor's compat layer supplies its own OOB volume (`X64/src/convert/iw3_iw8/resources/iw3_compat.gsc:53-78`) | Not needed for custom maps. |
| `script_struct` targetname `delete_on_load` (291) | targetname | `engine/utility.gsc:1604-1606` deletes these before indexing (identity UNVERIFIED) | engine | Stock only | Do not author. |
| `script_struct` targetname `pfN_auto` (230), `script_brushmodel` `clipN...`, `playerN...`, `mountN`, `mantleN`, `ladderMetal/Wood`, `nosight...`, `stairs...`, script_struct `weapon_pickup` (floor, 20), `dz_flare` (16), `nuke_expl_pos`, `airstrikeheight`, `heli_leave`, `care_package_col`, `tactical_cover_col`, `tactical_ladder_col`, `smoke_grenade_sight_clip_N`, `white_phosphorus_damage`, `dcover_blocker`, `airdrop_pallet`, `airdrop_crate` | various | UNKNOWN: names are encrypted or not in the nine gametype files read here | UNKNOWN | Not templated | Do not template until mapped (R7). |

Counts of stock objects by `script_gameobjectname` (both zones, 182 total): hqloc 36, cyber 32, dd_bombzone 30, bombzone 23, hardpoint 18, grnd 16, ctf 12, `dom onslaught` 6, sd 4, grind 4, airdrop_pallet 1.

## 2. Inventory of script-driven entities (step 1)

Full combinations are in the aggregate below. Each row is classname / gameobjectname / noteworthy / targetname family, with the count in both zones (shipment + backlot2 = total). `target` = 'y' when the entity has a `target` key.

| Total | Classname | gameobjectname | noteworthy | targetname family | target |
|---|---|---|---|---|---|
| 291 | script_struct | - | - | delete_on_load | n |
| 230 | script_struct | - | - | pfN_autoN | n |
| 24 | script_brushmodel | - | - | clipNxNxN | n |
| 20 | script_struct | - | floor | weapon_pickup | n |
| 18 | trigger_multiple | hardpoint | - | hardpoint_zone | y |
| 18 | script_model | hqloc | - | hqloc | y |
| 18 | script_brushmodel | hqloc | - | pfN_autoN | n |
| 16 | trigger_multiple | grnd | - | grnd | n |
| 16 | script_struct | - | - | dz_flare | n |
| 8 | script_struct_heli | - | - | ks_heli_entrance | n |
| 8 | script_struct_heli | - | - | ks_heli_goal | n |
| 8 | script_brushmodel | - | - | playerNxNxN / mountN / mantleN (4 each in one zone) | n |
| 6 | script_struct_mp_rugby_jugg_crate | - | - | rugby_jugg_crate | n |
| 6 | trigger_multiple | - | - | dcover_blocker | n |
| 6 | trigger_radius | dom onslaught | - | flag_primary | n |
| 6 | trigger_use_touch | dd_bombzone | - | dd_bombzone | y |
| 6 | trigger_use_touch | dd_bombzone | - | pfN_autoN | n |
| 6 | script_model | dd_bombzone | - | pfN_autoN | y |
| 6 | script_model | dd_bombzone | - | exploder | n |
| 6 | script_model | cyber | - | cyber_emp | n |
| 6 | trigger_use | cyber | - | cyber_emp_pickup_trig | n |
| 5 | script_struct_heli | - | - | ks_heli_path | n |
| 4 | script_origin | - | - | minimap_corner | n |
| 4 | script_struct_mp_rugby_endzone | - | - | rugby_endzone | y |
| 4 | trigger_multiple_bcs_containers | - | - | (none) | n |
| 4 | script_brushmodel | - | - | ladderMetalN / ladderWoodN / nosightNxNxN | n |
| 4 | script_model | cyber | - | pfN_autoN / exploder | n |
| 4 | script_brushmodel | cyber | - | (none) | n |
| 4 | trigger_use_touch | bombzone | - | bombzone | y |
| 4 | trigger_use_touch | bombzone | - | pfN_autoN | n |
| 4 | script_brushmodel | bombzone | - | (none) | n |
| 4 | script_model | bombzone | - | pfN_autoN (y) / exploder (n) | mixed |
| 4 | trigger_radius | grind | - | grind_location | n |
| 3 | trigger_multiple | - | - | kill_border_trigger / OutOfBounds | n |
| 3 | trigger_use_touch | bombzone | - | bombtrigger | n |
| 2 | script_brushmodel | - | heli_height_mesh | heli_pilot_mesh | n |
| 2 | script_struct | - | - | camera_intro | y |
| 2 | trigger_multiple_mp_rugby_endzone | - | - | pfN_autoN | n |
| 2 | trigger_radius | ctf | - | ctf_trig_allies / ctf_trig_axis | y |
| 2 | script_model | ctf | - | ctf_flag_allies / ctf_flag_axis | n |
| 2 | trigger_radius | ctf | - | ctf_zone_allies / ctf_zone_axis | n |
| 2 | trigger_use_touch | cyber | - | cyber_empzone_allies / axis | y |
| 2 | trigger_use_touch | cyber | - | cyber_emp_defuse_allies / axis | n |
| 2 | script_model | sd | - | sd_bomb | n |
| 2 | trigger_multiple | sd | - | sd_bomb_pickup_trig | n |
| 2 | trigger_multiple | - | - / center / gate / van / ... | callout_area | n |
| 2 | script_brushmodel | dd_bombzone | - | dd_bombzone_clip_a/b/c | n |
| 2 | script_struct | - | - | nuke_expl_pos, airstrikeheight, heli_leave, care_package_col and others (1-2 each) | mixed |
| 1 | trigger_radius | - | - | flag_goal_attackers / flag_goal_defender / flag_arena | n |
| 1 | script_brushmodel | airdrop_pallet | - | pfN_autoN | n |
| 1 | script_model | - | - | airdrop_crate | y |
| 1 | script_struct_infil_van_hackney_alpha / lbravo_alpha | - | infil_van_hackney / infil_lbravo | mp_infil | y |
| 1 | export_volume | - | - | mp_backlot2_infil_landing | y |
| 1 | script_model | - | - | infil_door_left / right / chain | n |
| 1 | reflection_probe | - | van_probe | pfN_autoN | n |
| 1 | collmap_scriptable | - | - | vehN_civ_lnd_* (8 names, one per zone family) | n |

The rest of the 134 combinations are single-count variants of the rows above (mostly `callout_area` per named area).

## 3. How the stock scripts find and keep or delete these entities

### 3.1 Startup gate: `gameobjects::main` deletes entities whose gameobject name is not in the gametype's list

- Every gametype calls `scripts\mp\gameobjects::main(list)` with a list made of the gametype's own name plus the object names it uses:
  `dom.gsc:11-12` (getgametype), `sd.gsc:11-14`, `ctf.gsc:9-10` (literal), `dm.gsc:8-9` (getgametype only), `hq.gsc:11-14`, `koth.gsc:11-14`,
  `cyber.gsc:11-13`, `dd.gsc:11-14`, `grnd.gsc:11-13`, `grind.gsc:8-10`. The same call exists in about 40 gametype files (grep of `gameobjects::main`).
- `gameobjects.gsc:7` takes every entity (`getentarray()`). For each one with `script_gameobjectname` (line 17):
  - a leading negation prefix (line 23) inverts the test;
  - the value is split on a separator (line 30; the separator decodes to a space, inferred, section 6) and each token is compared for equality with the list;
  - no match means `delete()` (lines 50 and 53).
- Result: `dom onslaught` survives only in a gametype whose list contains `dom`. `dm.gsc:8-9` lists only the gametype name, so in DM every gameobject entity is deleted. This is the stock behaviour that the editor's `iw3compat_remove_gameobjects` copies for script_models only (`X64/src/convert/iw3_iw8/resources/iw3_compat.gsc:270-282`).
- Also `gameobjects.gsc:4455-4462` (`getleveltriggers`) fetches triggers by classname and targetname for mine, hurt, radiation, ball, no-zone and drop-to-navmesh behaviour.

### 3.2 Struct gate: `load.gsc` filters script_structs by substring

- `load.gsc:13` installs `filterstructs` as `level.struct_filter`.
- `load.gsc:223-282` returns 0 (drop) when a struct's `script_gameobjectname` does not match the current gametype. The match is `issubstr` (`mp/utility/game.gsc:1633-1639`), a different rule from the token equality in 3.1.
- `engine/utility.gsc:1586-1625` (`init_struct_class`) indexes structs by targetname, using the filter at 1610-1614. It drops any struct whose targetname is the delete marker at line 1604 (identity UNVERIFIED; see section 6).

### 3.3 Lookups are `getentarray(value, key)` on the engine side

- The builtin is `getentarray` / `getent` (`X64/src/convert/iw3_iw8/iw8_gsc_builtins.inc:314-315`). The argument order is value first, key second. Stock calls it as `getentarray("hqloc", "targetname")`-style with an encrypted key (section 6, B2CE = targetname). The fork's compat layer uses the same order: `getentarray("mw120r_exploder", "targetname")` (`iw3_compat.gsc:287-296`).
- `getstructarray` is not a builtin. It is `scripts\engine\utility::getstructarray(value, key)` (`engine/utility.gsc:1540-1548`), reading `level.struct_class_names[key][value]`, which 3.2 fills with four keyed indexes (`engine/utility.gsc:1592-1595`). The fork's `iw3compat_getstructarray` mirrors this with four keys (`iw3_compat.gsc:616-668`).

## 4. Which script files live where

- The fork's manifests show `scriptfile` assets in the stock map zone: `mp_shipment.ff` has 158 (`stock_cache/.../manifest.json`, `loaded_by_type`). The always-loaded worldwide zones dumped (`ww_common`, `ww_common_mp`, `ww_common_base_mp`, `ww_common_core_mp`, in `E:\mw124-conversion-builds\_stock_cache`) have none.
- `docs/research/14_x64zt_verifyzone_coverage.md:166` (row 52, scriptfile): present in `common_base_mp`, `global_mp`, `mp_aniyah_tac`, `mp_backlot2`, `mp_bm_tut` and six more. So the always-loaded candidates for stock MP scripts are `common_base_mp` and `global_mp`.
- `X64/src/convert/iw3_iw8/iw3_scripts.cpp:134-137` says the stock scripts `scripts\mp\damage`, `scripts\mp\weapons` and `scripts\engine\trace` are in common_base_mp (decompiled 1.20). That supports the same zone for gametype scripts, but it is not proven.
- Which gametype file is in which zone: UNVERIFIED. The check needs an ACTS `-a scriptfile` listing of `common_base_mp.ff`, `global_mp.ff` and `mp_shipment.ff`. This was not run (task rule: read only).

## 5. How a custom map gets the behaviour

### (a) Naming: the entity is found by the stock script if the name and key match and the gate lists it

- Requirements, all three:
  1. The entity keeps its keys and class (editor: `compile_iw8.cpp:489-508` class list; keys written as text, see 5.d).
  2. The gametype's gate lists its gameobject token (3.1), or the struct gate's substring matches (3.2).
  3. The lookup key/value matches (3.3). For targetnames these are the exact names in section 1.
- Evidence the fork already relies on this: the IW3 converter renames to Replay's names and adds a DOM flag prop with `script_gameobjectname "dom"` (`X64/src/convert/iw3_iw8/iw3_fastfile.cpp:10170-10196`); it writes targetname `hqloc` on the HQ radio (`X64/src/games/iw8/dump/maps.cpp:668-672`) so `hq.gsc:885` finds it; it renames CTF team flags to `ctf_flag_<team>` for `ctf.gsc` (`iw3_fastfile.cpp:10224-10228`); and `iw3_compat.gsc:35-50` plus `map_script.cpp:84-89` make every editor map run `maps\mp\_load::main`, so the gate and the compat exploder/destructible logic run.
- The editor templates already do (a): `ED/src/core/gamemode_templates.cpp:44-120`.
- Caveat: the gate only runs in gametypes that call `gameobjects::main`. A name works only in the modes that keep it (section 7).

### (b) Calling stock functions from the map's GSC

- The fork already emits far calls from converted map scripts to stock functions by qualified name: `scripts\mp\damage::playerkilled_internal`, `scripts\mp\weapons::deleteexplosive`, `scripts\engine\trace::_bullet_trace` (`X64/src/convert/iw3_iw8/iw3_scripts.cpp:134-150`). This is the proof that the map zone can call stock script functions when they are loaded.
- The useful stock functions for custom maps are small utilities such as `scripts\engine\utility::getstructarray(name, "targetname")` (`engine/utility.gsc:1540`). The gametype setup functions (dom, sd, ctf, hq) are not designed as an API. Their names are encrypted in the decompile, so they cannot be called by name from this source without decoding (UNVERIFIED that they are callable).
- The editor's own snippets (`ED/src/core/script_check.cpp:159-340`) use only `getentarray` and `getent` on `targetname`. That is the (a) mechanism, written by the user.

### (c) Carrying stock script files in the map zone

- Not supported for stock names today:
  - `X64/src/games/iw8/assets/replay_stock.cpp:186-196`: the stock copier maps only Material, XModel, XModelSurfs, ParticleSystemDef, PhysicsFX pipeline and shape, PhysicsAsset, VectorField, GfxLightDef. Any other type throws "unsupported referenced asset type".
  - `replay_stock.cpp:959-963`: `Stub()` has no reference layout for scriptfile or rawfile and throws.
- Pieces that exist for other purposes:
  - Name-only references: `RegisterNameReference` (`replay_stock.cpp:1421-1424`, header `replay_stock.h:52-53`). It writes a zeroed header with the name and "the loader resolves against the common zone that owns the asset". It is type-agnostic apart from `Stub()`.
  - Compiled-script writer: `replay_script::emitCompiledScript` (`X64/src/games/iw8/assets/replay_script.h:26-30`), used by the IW3 converter for the converted map script (`X64/src/convert/iw3_iw8/convert.cpp:1954-1958`).
  - Raw file writer: `iw8::rawfile::Register` (`X64/src/games/iw8/assets/replay_rawfile.cpp:13-44`), used at `convert.cpp:1772`.
- Source bytecode: `RAW/gametypes/*.gscbin` exists for the 8.19 dump (for example `dom.gscbin`). Its compatibility with Replay 1.20 is UNVERIFIED.
- Name collision: `RequireNew` throws if the map zone already has the name (`replay_stock.cpp:1029-1032`). A copied `dom.gsc` would collide with the always-loaded copy, and how the engine picks between two copies is UNVERIFIED.
- What the editor does today: the map's own GSC (`worldspawn iw8r_script`) is carried in the map zone (`ED/src/core/compile_iw8.cpp:1776-1790`). Stock scripts are not carried.

### (d) Keys in the editor build path (UNVERIFIED)

- The editor writes keys as text (`gamemode_templates.cpp` `templateMapText`). Key ids are assigned by the x64-zt converter from the table in `X64/src/games/iw8/dump/maps.cpp:25-62`. Keys not in the table are dropped silently (`maps.cpp:685`, `unnamedKeys`). The table includes the keys used here: `script_gameobjectname`, `script_label`, `script_noteworthy`, `script_bombmode_original`, `target`, `targetname`, `script_exploder`, `footprint_mask_group` (via the havok table). Whether the editor build uses this same path is UNVERIFIED; `compile_iw8.cpp` only writes text.

## 6. Encrypted names: inferred identities and evidence

Inferred values are a reading aid. Confidence is the author's.

| Token (`_encstr_` + hex) | Inferred value | Evidence | Confidence |
|---|---|---|---|
| `B2CE0BA1D0FB19FDC54613D9BF` | `targetname` (the lookup key) | Second argument of nearly every getent/getentarray (`dom.gsc:351`, `ctf.gsc:355`, `koth.gsc:807`, `obj_bombzone.gsc:17`) | high |
| `AC110A7F14873B5B3D073009` | `classname` | `gameobjects.gsc:4458` pairs `85FE0DD5...` (trigger_hurt) with it | medium-high |
| `974C0246...` | separator (space) | `gameobjects.gsc:30` splits `dom onslaught` tokens; noteworthy splitting in `koth.gsc:685-688` | medium |
| `AD4C0212...` | negation prefix `!` | `gameobjects.gsc:23`, `load.gsc:236` | medium |
| `82170DCC8D2CB37D389C2DB685275E` | `flag_primary` | `dom.gsc:351`, `ctf.gsc:355`, `X64/.../iw3_fastfile.cpp:10188` (the converter writes targetname `flag_primary` for DOM triggers) | high |
| `979A09DA67D0262B70CD82` | `bombzone` | `sd.gsc:439` compares it to gameobjectname, `sd.gsc:470` looks it up as targetname; the dump has bombzone triggers with both | medium |
| `8E1904B3F501` | `ctf` | `ctf.gsc:9` (literal gametype name) | medium |
| `8B1F0A8163D2D61AC0589B81` | `ctf_flag_` prefix | `ctf.gsc:509`, `662`; `iw3_fastfile.cpp:10224-10228` | high |
| `81650A3E6BD0BFBAE11357BF` | `ctf_trig_` prefix | `ctf.gsc:502`; the template and dump agree | medium |
| `8CE606A1C5D8EDD8` | `hqloc` (targetname) | `hq.gsc:885`; `X64/.../maps.cpp:668-672` writes targetname `hqloc` | high |
| `B67B0A1A2C279107DBB49BA3` | `hardpoint` (gameobject) | `hq.gsc:12`, `koth.gsc:12`; the dump has 18 hardpoint objects | medium |
| `8E4A0FABC16B29497522C86039483F206F` | `hardpoint_zone` | `koth.gsc:838`, `hq.gsc:881`; the dump has hardpoint_zone targetnames | medium |
| `A34C02BB...`, `A74C0222...`, `A14C023B...`, `AC4C024A...`, `B84C027E...` | labels `1` to `5` | `hq.gsc:981-987`, `grnd.gsc:393-417` order; dump hardpoint labels are 1 to 5 | medium |
| `844E0C99C3A166200DFE2298E5ED` | `dd_bombzone` | `dd.gsc:14`, `dd.gsc:527`; dump | medium |
| `827F0FE12BAF302029942F2F81CB80080B` | `grind_location` | `grind.gsc:375`; dump | medium-low |
| `8A6209D638F52D37334BC6` | `mp_infil` | `infilexfil.gsc:274`; dump | medium-low |
| `83CD0DD81636B1F6BAD1F5859CAC2C` | `callout_area` | `callouts.gsc:13`; dump | medium-low |
| `A7EE0FE3BB593A1C48B68DD5AE585F3225` | `delete_on_load` | `engine/utility.gsc:1604`; 291 dump structs | medium-low |
| `A65908C41BED1BD62B4E` | UNKNOWN | in the lists of sd, cyber and dd (`sd.gsc:13`, `cyber.gsc:12`, `dd.gsc:13`) | none |
| `8A1804CF18C5`, `B27706C1CF833659`, `B40101` | none/ignore markers | `gameobjects.gsc:53-67`, `load.gsc:270-277`; `B40101` is the empty value (`game.gsc:1635`) | medium |

## 7. What each stock gametype keeps (from the gate lists in 3.1)

| Gametype (file) | Gate list | Keeps gameobject tokens |
|---|---|---|
| dom (`dom.gsc:11-12`) | getgametype = dom | `dom`, so `dom onslaught` |
| sd (`sd.gsc:11-14`) | sd, bombzone (inferred), A65908 | `sd`, `bombzone` |
| ctf (`ctf.gsc:9-10`) | ctf | `ctf` |
| dm (`dm.gsc:8-9`) | getgametype only | the gametype name only (every object is deleted) |
| cyber (`cyber.gsc:11-13`) | cyber, A65908 | `cyber` |
| dd (`dd.gsc:11-14`) | dd, dd_bombzone, A65908 | `dd`, `dd_bombzone` |
| hq (`hq.gsc:11-14`) | hqloc, hardpoint, A11904E823B6 | `hqloc`, `hardpoint` |
| koth (`koth.gsc:11-14`) | koth, hardpoint, A11904E823B6 | `koth`, `hardpoint` |
| grnd (`grnd.gsc:11-13`) | grnd, AF5B0923... | `grnd` |
| grind (`grind.gsc:8-10`) | grind, 8218041D... | `grind` |

Consequence: `hqloc` radios survive only in hq, not koth (koth's list has no hqloc). Hardpoint zones survive in both. Bombzones survive in sd and dd, and cyber's list shows A65908 (its meaning is UNKNOWN). Every gameobject name in the stock dump survives in at least one gametype. `airdrop_pallet` is in none of the nine lists read (UNVERIFIED for the others).

## 8. What the editor's templates cover, and what they miss

| Template (`ED/src/core/gamemode_templates.cpp`) | Entities | Matches stock | Gap |
|---|---|---|---|
| `dom_flags` (line 44) | 3 trigger_radius, labels `_a`/`_b`/`_c` | Yes: classnames, keys and radius/height from doc 32 | No optional flag prop (not needed; `obj_dom.gsc:9-20` spawns its own marker). |
| `hardpoint_zones` (line 59) | 5 trigger_multiple brushes, labels `1`-`5` | Yes | No hqloc radio (needed for hq mode, and the converter pairs them). Chevron target omitted (safe for koth). Box sizes are placeholders. |
| `sd_sites` (line 74) | 2 bombzone triggers, bomb model, pickup trigger | Partly | No `target` chain on the bombzone triggers (`obj_bombzone.gsc:17, 118, 124`). No bombzone brushmodel or crates (`sd.gsc:439-456` removes a brushmodel only if present). |
| `ctf_flags` (line 99) | Both teams: trigger, flag, zone | Yes (names confirmed) | No `flag_primary` labelled triggers, which `ctf.gsc:352-372` reads for default base origins. Effect UNVERIFIED. |
| spawn sets (line 133) | TDM, DOM, CTF, SD spawns | Yes for DOM, CTF, SD per doc 32 | No koth, dd, cyber or rugby spawn sets. `gameplay_checks.cpp` does not check hardpoint spawns (UNVERIFIED need). |

Not templated at all: HQ radio, cyber EMP, dd bombzones, grind location, ground zones, callout areas, helicopter route, camera intro. Refused by the class list: infil, rugby, endzone triggers, `collmap_scriptable`, `reflection_probe`, `export_volume`.

Existing checks (`ED/src/core/gameplay_checks.cpp:142-158`) cover only script_models that will be built as static props. There is no check for gameobject names, mode coverage, chains or radio counts.

## 9. Recommendations

Editor (no fork change):

- R1. `sd_sites`: add a target chain from each bombzone trigger to a defuse trigger (`obj_bombzone.gsc:118, 124`). The exact chain shape (`pfN_auto` script_struct or script_origin, and what it targets) is UNVERIFIED; take it from the stock dump (`pf1_auto1` on bombzone triggers) and test in game.
- R2. `hardpoint_zones`: add one HQ radio per zone for hq mode: `script_model`, targetname `hqloc`, `script_gameobjectname hqloc`, model `military_hq_crate_01_proxy` (doc 32 section 7). Keep the converter's rule that one radio sits inside one zone (`maps.cpp:543-546`).
- R3. `dom_flags`: add an optional flag prop as an option, not a default: `script_model`, `script_gameobjectname "dom"`, model `prop_flag_neutral`, the same keys as `iw3_fastfile.cpp:10193-10194`. The model must exist in a loaded zone or be carried (UNVERIFIED).
- R4. New templates, in priority order: cyber EMP (32 objects; `cyber.gsc:460-478`); DD bombzones (`dd.gsc:490-527`); grind location; ground zones (`grnd.gsc`); helicopter route (`script_struct_heli` with a `target` chain, `helicopter.gsc:207, 1182`); callout areas (script_noteworthy from the map's area names, `callouts.gsc:79`); camera intro (`spawncamera.gsc:32`).
- R5. `ctf_flags`: optionally add `flag_primary` labelled triggers `_a`/`_b` as base origins (`ctf.gsc:352-372`). Check in game first.
- R6. Add checks to `gameplay_checks.cpp` after line 158, using the gate table in section 7:
  - C1: an unknown `script_gameobjectname` token is a warning (every mode deletes it).
  - C2: a gameobject name that appears in only some modes is an info line naming those modes.
  - C3: hqloc radio count equals hardpoint zone count, and each radio is inside one zone (`maps.cpp:543-546`).
  - C4: a bombzone trigger without a `target` is an error for sd and dd (`obj_bombzone.gsc:118`).
  - C5: `dom onslaught` triggers have labels `_a`/`_b`/`_c` and targetname `flag_primary` (`dom.gsc:351, 451-468`).
  - C6: a class the editor refuses (infil, rugby, endzone, collmap_scriptable, reflection_probe, export_volume) is an error, not a silent drop (`compile_iw8.cpp:489-508`).
  - C7: `script_struct_heli` without a `target` chain is a warning (`helicopter.gsc:207`).
  - C8: `callout_area` triggers without `script_noteworthy` are a warning (`callouts.gsc:79`).
- R7. Map the pfN_auto, clip, mount, mantle, weapon_pickup, dz_flare, equipment-clip and airdrop names before templating any of them. Needs a `scriptfile` dump of the stock zones (ACTS, not run here).
- R8. Script > Insert (`script_check.cpp`): add a "Stock gametype object" snippet that documents the gate list, so the user can see which mode keeps a name. No behaviour change.
- R9. Update doc 32 sections 2-4 and 8: the DOM flag prop is optional (not "UNKNOWN required"), and the hardpoint chevron target is not needed for koth.

Fork (x64-zt), only if (c) is wanted:

- F1. Name-only stock scripts: in `src/games/iw8/assets/replay_stock.cpp`, add a `Stub()` layout (line 946-963) and a `PreloadFootprint()` case (line 972) for ASSET_TYPE_SCRIPTFILE (and RAWFILE), and allow the type in `AssetType()` (186-196). Then `RegisterNameReference(writer, ASSET_TYPE_SCRIPTFILE, name)` (line 1421-1424) would reference a script from its owning common zone. The header size and layout must come from an ACTS scriptfile dump (UNVERIFIED). Test that the loader resolves a scriptfile reference by name.
- F2. Copy route: export the stock bytecode (`.gscbin`) for the wanted script, and call `replay_script::emitCompiledScript` (`replay_script.h:26-30`) into the map zone. Add a guard for names the zone already has (`RequireNew`, line 1029). The 8.19 bytecode is not evidence for 1.20 (UNVERIFIED).
- F3. Classes: to allow infil, rugby, endzone and other classes, change `isReplayEntity` (`maps.cpp:179-216`) and the editor's `acceptedPointClass` (`compile_iw8.cpp:489-508`) together. Verify in game before enabling.
- F4. No fork change is needed for (a) or (b).

## 10. Unverified (collected)

- Replay 1.20 GSC equals the 8.19 `rawfile_source` (section 0). Not checked.
- Which zone holds each gametype script (section 4). Needs an ACTS scriptfile listing (not run).
- The encrypted identities in section 6 (confidence noted per row).
- That the editor build path uses the same key table as the IW3 converter (5.d).
- In-game behaviour of every template, including the SD defuse failure in R1 (`obj_bombzone.gsc:118` with an empty target list).
- Whether the DOM flag model exists in a loaded zone (R3).
- Whether a scriptfile name reference resolves in the loader (F1).
- Meaning of `spawnflags 4` and `spawnflags 1` on stock script_models (doc 32 section 4).
- Which gametype keeps `airdrop_pallet` (none of the nine lists read).

## 11. Not done

- `docs/PROGRESS.md` was not updated. The task said to write only this file. The owner's memory asks for PROGRESS.md to be kept current, so that note should be added by the owner or in the next pass.
- No ACTS runs, no game runs, no edits to editor or fork source.
