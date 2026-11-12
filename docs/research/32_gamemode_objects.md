# 32. Game-mode objects in stock Replay 1.20 map_ents (roadmap 6.2)

Evidence for the game-mode templates (`src/core/gamemode_templates.*`). Target: MW2019 Replay 1.20.

## 1. Method and sources

- Source dumps (already in the repo, produced by ACTS from the 1.20 zones; manifests say `complete: true`, `success: true`):
  - `data/stock_dumps/mp_shipment/mw19replay/mp_shipment/assets/map_ents/maps/mp/mp_shipment.d3dbsp.0.asset.json`
    (entity string 62,892 bytes, 611 entity blocks; `fastfile` `E:/IW8/Builds/1.20-replay/zone/mp_shipment.ff`)
  - `data/stock_dumps/mp_backlot2/.../mp_backlot2.d3dbsp.0.asset.json` (295,431 bytes, 3,234 blocks; `mp_backlot2.ff`)
- The `fields.entityString.bytes` hex was decoded to text in `%TEMP%` (scratch only; nothing written to the repo or to
  `E:\IW8\Builds`). I did not rerun ACTS for this report; the dumps above are its output.
- Keys: numeric canonical ids resolved with `data/generated/iw8_canonical_keys.tsv`. Relevant ids: 212 classname, 709 origin,
  80 angles, 652 model, 845 script_noteworthy, 1070 target, 1072 targetname, 43840 script_gameobjectname,
  43887 script_label, 43684 script_bombmode_original (all three checked in the TSV). Other stock keys are written by name
  (`spawnflags` 925, `radius` 782, `height` 489, `nophysics` 689, `script_exploder` 841 in the TSV).
- Spawn names: `script_strings.json` of both dumps (spawnList names are script strings) and doc 10.
- Not found in any of the above: `hqloc` (KOTH) is present but out of scope for 6.2 (section 7).

## 2. Domination (flags)

Verified in both zones (shipment: 3 entities, backlot2: 3 entities).

| Classname | Keys (stock values) | Notes |
|---|---|---|
| `trigger_radius` | `script_gameobjectname "dom onslaught"`, `script_label "_a"`/`"_b"`/`"_c"`, `targetname "flag_primary"`, `radius 120`, `height 128`, `nophysics true` | Three per zone; all share `targetname flag_primary`. One is shipment `_a` at (-736, 2476, 33). |

- The flag prop is UNKNOWN: no `script_model` or `script_brushmodel` with `script_gameobjectname "dom..."` exists in either zone.
  The template emits only the three triggers.
- Spawns for DOM: `mp_dom_spawn` (69), `mp_dom_spawn_allies_start` (22), `mp_dom_spawn_axis_start` (22), `mp_dom_spawn_secondary` (26) in doc 10.

## 3. Hardpoint (zones)

Verified in both zones (shipment: 5 zones, backlot2: 13 zones).

| Classname | Keys (stock values) | Notes |
|---|---|---|
| `trigger_multiple` (brush, `model ?N`) | `script_gameobjectname "hardpoint"`, `script_label "1"`..`"5"`, `targetname "hardpoint_zone"`, `target "hardpoint_<n>_chevrons"` | Zone volume is a brush in the stock map (its `model ?N` index). |

- `hardpoint_<n>_chevrons` is the `target` value; no entity with that targetname exists in either zone, so the target is
  UNKNOWN and the template omits it.
- The zone brush size is UNKNOWN. The template uses a placeholder box (section 8).

## 4. Search & Destroy (sites, bomb)

Verified in both zones.

| Classname | Keys (stock values) | Notes |
|---|---|---|
| `trigger_use_touch` (brush) | `script_gameobjectname "bombzone"`, `targetname "bombzone"`, `script_label "_a"` / `"_b"`, `script_bombmode_original 1`, `target "pf<n>_auto1"` | Labelled sites carry `script_bombmode_original 1`. Shipment and backlot2 each have two labelled sites. The `target` chain (scripted exploder) is UNKNOWN and omitted. |
| `script_brushmodel` | `script_gameobjectname "bombzone"`, `spawnflags 1` | Site body (UNKNOWN role, omitted). |
| `script_model` | `model "sd_equipment_bombsite_crate"` / `_d`, `script_exploder`, `script_gameobjectname "bombzone"`, `spawnflags 4` | Crates and exploder pieces (UNKNOWN role, omitted). |
| `script_model` | `model "offhand_wm_briefcase_bomb"`, `script_gameobjectname "sd"`, `targetname "sd_bomb"`, `spawnflags 4`, `angles` | The bomb (shipment `angles 0 45 0`, backlot2 `357 90 3`). |
| `trigger_multiple` (brush) | `script_gameobjectname "sd"`, `targetname "sd_bomb_pickup_trig"` | Pickup volume around the bomb. |

- SD spawns: `mp_sd_spawn_attacker`, `mp_sd_spawn_defender` (script strings in both zones, `mp_sd_spawn_attacker` and
  `mp_sd_spawn_defender` also in `iw8_entity_classes.json`). Counts are not in doc 10's top list.
- `spawnflags 4` and `spawnflags 1` meanings are UNKNOWN; the template copies the stock values.

## 5. Capture the Flag

Verified in both zones (shipment and backlot2 both have the same two-team set).

| Classname | Keys (allies / axis) | Notes |
|---|---|---|
| `trigger_radius` | `script_gameobjectname "ctf"`, `targetname ctf_trig_allies` / `ctf_trig_axis`, `target ctf_flag_allies` / `ctf_flag_axis`, `radius 64`, `height 128` | Flag pickup trigger. Axis copy has `angles 0 180 0`. |
| `script_model` | `model "highway_flag0"`, `script_gameobjectname "ctf"`, `targetname ctf_flag_allies` / `ctf_flag_axis`, `spawnflags 4` | Flag prop. Axis copy has `angles 0 180 0`. Its origin is 16 units below the trigger origin (stock: trigger z 32, flag z 16). |
| `trigger_radius` | `script_gameobjectname "ctf"`, `targetname ctf_zone_allies` / `ctf_zone_axis`, `radius 64`, `height 128` | Capture zone at the trigger origin. |

- CTF spawns: `mp_ctf_spawn` (73), `mp_ctf_spawn_allies_start`, `mp_ctf_spawn_axis_start`, `mp_ctf_spawn_secondary` (26) in doc 10.

## 6. Spawn sets (`mp_*_spawn*` entities)

- The stock `.ents` text has NO `mp_*_spawn*` entity: spawns live in `spawnList` (doc 10, doc 26 section 3).
- Route for editor maps: doc 26 section 3 says the M5 extraction path (`map_zone.h:1343-1366`) regenerates `spawnList` from
  `mp_*_spawn*` entities in the `.ents` text, and `replay_spawns.h:41-69` takes classname (must start `mp_`, contain `_spawn`),
  origin (709), angles (80), target (1070), script_noteworthy (845). So the spawn-set templates write spawn entities.
  The in-game effect (spawns from the editor build) is UNKNOWN until an owner test.
- Names used by the templates: TDM `mp_tdm_spawn`, `mp_tdm_spawn_allies_start`, `mp_tdm_spawn_axis_start`
  (`gameplay_checks.cpp` needs 2 + 1 + 1); DOM `mp_dom_spawn`, `mp_dom_spawn_allies_start`, `mp_dom_spawn_axis_start`;
  CTF `mp_ctf_spawn`, `mp_ctf_spawn_allies_start`, `mp_ctf_spawn_axis_start`; S&D `mp_sd_spawn_attacker`, `mp_sd_spawn_defender`.
- Spawn positions and angles in the template are layout choices, not stock values.

## 7. Seen but not templated

- `hqloc` (`script_model` `military_hq_crate_01_proxy` + `script_brushmodel` `script_gameobjectname hqloc`, `spawnflags 1`):
  the King of the Hill HQ. KOTH is not in roadmap 6.2 scope.
- `grnd` (`trigger_multiple`, `script_gameobjectname grnd`, `targetname grnd`): rugby/ground zones (not 6.2).
- `dd_bombzone` (Demolition): not 6.2 scope.
- Key id 0 `"dom" "0"` on one backlot2 entity: the key-table parser artefact noted in doc 13 (not a flag).

## 8. Placeholder sizes (UNKNOWN in stock)

Stock brush extents of trigger brushes are not in the dumps (the brushes are in the BSP). The template boxes are placeholders
and must be resized in the editor:
- Hardpoint zone: 192 x 192 x 128.
- SD bomb site: 160 x 160 x 96. SD bomb pickup: 96 x 96 x 64.
- Radius triggers (DOM, CTF): stock radii and heights as above, not placeholders.

## 9. Summary

| Item | Status |
|---|---|
| Classnames and keys above (DOM, hardpoint, SD, CTF, spawn names) | Verified in both stock zones (triggers, models, script strings) |
| Spawn entity route for editor maps | Verified in x64-zt source (doc 26); in-game UNKNOWN |
| DOM flag prop, hardpoint `chevrons` target, SD crate/exploder/site body roles, `spawnflags` 1/4 meaning | UNKNOWN (omitted or copied) |
| Trigger brush sizes | UNKNOWN (placeholders, section 8) |
| Placement layout (offsets between parts) | Editor choice |
| In-game behaviour of the editor-built objects | UNKNOWN (owner test needed) |
