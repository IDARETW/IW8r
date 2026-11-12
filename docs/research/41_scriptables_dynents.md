# 41 Scriptables and dynamic entities (roadmap 6.14, 6.15)

Date: 2026-11-10. Read-only research. No game was run and ACTS was not run on game files. Evidence is x64-zt source, existing dumps and traces under `E:\IW8-Radiant\out` and `E:\IW8-Radiant\data`, and the ACTS docs. Items marked UNVERIFIED need a game or build check.

Roadmap text (`docs/ROADMAP.md:267-268`): 6.14 "Native scriptables: catalogue of stock scriptable names, placement, state preview, `collmap_scriptable`"; 6.15 "Dynamic entities and physics props (fork `replay_dynentity`): placement with a physics preset".

## 0. Summary

- **Placeable today:** nothing that is a native scriptable or a dynamic entity. The editor places `misc_model` (static decor), and `script_model` (live or static, depending on targeting). The "make destructible" and "make scripted" commands are script_model plus `script_origin` IW3-compat objects, not `ScriptableDef` instances (`src/core/scripted_objects.cpp`).
- **Stock scriptable names:** no list exists in ACTS config or in the editor catalogs. The data is in the existing verifyzone traces: 654 unique pool-63 names across 17 of 76 zones. `tools/build_asset_catalog.py` can emit them with a one-line POOLS change and no game access (section 2).
- **Stock placement format is known:** `MapEnts.scriptableMapEnts.mapInstances` holds 72-byte `ScriptableInstance` records. Stock mp_shipment has 75 of them. x64-zt never writes them (section 1.3). This is the main fork gap for 6.14.
- **Dynamic entities:** the x64-zt writer (`replay_dynentity.cpp`) only emits clutter (type 1) from IW3-unlinker collision tables (`dynamic_models`). No map entity becomes a dynent. The editor writes both tables empty (`compile_iw8.cpp:1623-1624`). A dynent needs the editor to write those tables and the `physic/<name>` files (section 3).
- **Fork changes:** (a) emit `ScriptableInstance` and `CollMapLookupEntry` in `zone/map_zone.h` `emitMapEntsBody`; (b) a converter input for scriptable instances; (c) for dynents, no fork change is strictly needed, only editor output. Details in section 5.

## 1. Scriptable assets (`ScriptableDef`)

### 1.1 Asset type and layout

- Asset type `ASSET_TYPE_SCRIPTABLE = 63`, root type `ScriptableDef`, stream name `scriptable`: `E:\mw124\x64-zt\src\games\iw8\zone\asset_streams.hpp:63, 185`.
- `ScriptableDef` is 0x78 (120) bytes; `name` (`const char*`) is at offset 0: `E:\mw124\x64-zt\src\games\iw8\structs.hpp:9470-9495`, static asserts at `:21941-21955`. Fields include `nextScriptableDef`, `parts`, `partCount`, `maxNumDynEntsRequired`, `animationTreeDef[2]`, `viewRadius`, `models`, `spatialActivationMode`.
- ACTS agrees: `E:\mw124\atian-cod-tools\docs\mw19\pools.md:77` gives `63 | scriptable | 0x78 | ScriptableDef`. The existing dumps agree too: header bytes 0-7 are `IW8D`, version 1, then type `0x3F` (63). Checked on `E:\IW8-Radiant\out\m2-x64zt\dump\mp_shipment\scriptable\com_junktire.iw8`.
- Pool numbering: x64-zt and ACTS pools.md use the same numbers (xmodel 9, material 11, image 19, map_ents 29, scriptable 63, dynentitylist 113). The enum in `E:\mw124\atian-cod-tools\research\mw19\replay_pools.json` (declaration text, `ASSET_TYPE_SCRIPTABLE = 0x3B`) is a different declaration. Do not use it for pool ids.

### 1.2 Can x64-zt write or reference them?

- **Write:** yes, as a zone asset. `neutral/scriptable.cpp` (generated from `tools/neutral_families/scriptable.json`) has to/from neutral for the whole part and state tree. `zone/writer.cpp:367-370` runs `validate_scriptable` before every scriptable is written (`part flatId`, cyclic part tree, nonzero counts).
- **Reference by name:** yes. An asset that the zone does not hold is written as a stub with its name (`zone/writer.cpp` ~355-395, `stub(...)`). `ScriptableInstance.initialDef` is an asset reference, so a map can name a stock def and the loader resolves it from its required zones. UNVERIFIED for every stock def: the dump shows the reference by name (section 1.3), not that a loader accepts a stub for every name.
- **Author a new def:** no authoring path exists. `from_neutral` exists, but nothing builds a neutral scriptable from editor data. Size L.

### 1.3 How a map places one (MapEnts)

The stock placement is a binary list in `MapEnts`, not an entity class in the entity string.

- Field layout: `E:\mw124\x64-zt\src\games\iw8\iw8_map_structs.h:247` (`scriptableMapEnts[216]` inline at 0x1F8). ScriptableMapEnts counts: `totalInstanceCount`, `mapInstanceCount`, `reservedInstanceCount`, `lootInstanceCount`, `serverInstanceCount`, `serverFixedPartCount`, `serverUsablePartCount`, `physicsLimits`, `mapInstances`.
- `ScriptableInstance` (stride 72): `initialDef` (ptr, 8), `initialOrigin` (vec3), `initialAngles` (vec3), six `scr_string_t` (classname, target, targetname, script_linkname, script_linkto, script_noteworthy), `lightIndices[5]`, `ffMemCost`, `mapEntLookup`, `flags`, `padding[1]`: `E:\mw124\x64-zt\src\games\iw8\structs.hpp:10618-10634`.
- Neutral read/write exists: `E:\mw124\x64-zt\src\games\iw8\neutral\mapents.cpp:797-834` (map instances), `:1140, :1187, :1199` (collmap lookups, `numCollmaps`), `:1868-1889` (instance).

Stock values, read from `E:\IW8-Radiant\data\stock_dumps\mp_shipment\mw19replay\mp_shipment\assets\map_ents\maps\mp\mp_shipment.d3dbsp.0.asset.json` (`asset.fields.scriptableMapEnts`):

- `mapInstanceCount` 75, `totalInstanceCount` 0, `serverInstanceCount` 66, `serverFixedPartCount` 609.
- 9 distinct defs: `,hardpoint_chevron` (37, leading comma is the cross-zone marker), `com_trafficcone_01` (12), `veh8_civ_lnd_decho_rebel_darkblue` (11), `veh8_civ_lnd_victor40_police_holdexplode` (4), `com_junktire` (4), `uk_security_camera_redlight_01` (4), `decor_rope_thick_coiled_01` (1), `decor_rope_thick_pile_01` (1), `equipment_military_radio_old_02` (1).
- `classname` resolves through `data\stock_dumps\mp_shipment\...\script_strings.json` to `scriptable_<def name>` (e.g. index 8005 = `scriptable_com_trafficcone_01`). The def name is the classname with the `scriptable_` prefix.
- `targetname` is set on some (`allies_endzone_vis`, `police_arrive_scriptable_car`, `hardpoint_1_chevrons`...). `target` is empty on all 75. `flags` are 0 on all 75. `lightIndices` are 0 on all 75.
- `collmapLookups`: 20 entries of 16 bytes (`targetName` string, `brushIndex`), indices 59-78 (e.g. `veh8_mil_lnd_atango_physics_mp` 59).

What x64-zt writes for a converted map: `E:\mw124\x64-zt\src\games\iw8\zone\map_zone.h:979-985` writes an empty scriptable tree ("one empty spatial leaf"). Grep finds no write of `ScriptableInstance`, `mapInstanceCount`, `numCollmaps` or `collmapLookups` in the native route (only neutral). So converted maps have 0 scriptable instances and 0 collmap lookups. UNVERIFIED whether any other path uses the neutral mapents writer.

### 1.4 Entity-string side and `collmap_scriptable`

- The stock entity string has `collmap_scriptable` records (doc 10 row 12, `E:\IW8-Radiant\docs\research\10_iw8_entity_inventory.md:46`: 13 across mp_shipment and mp_backlot2). Decoded from the mp_shipment entity string (hex in the dump, keys from `E:\IW8-Radiant\data\generated\iw8_canonical_keys.tsv`: 212 classname, 652 model, 1072 targetname): six records with `model "*53"` to `"*58"` and targetnames `veh8_civ_lnd_victor40`, `..._ai_obstacle`, `..._static_dst`, `veh8_civ_lnd_decho_rebel`, `..._ai_obstacle`, `..._static_dst`.
- These six targetnames do not appear in the 20 `collmapLookups` names (intersection is empty). UNVERIFIED how the two relate. The lookups list different vehicles (e.g. `veh8_mil_lnd_atango_physics_mp`).
- The only code that reads `collmap_scriptable` is the IW7 converter: `E:\mw124\x64-zt\src\convert\iw7_iw8\export_entities.cpp:347-400` (`collmap_target`, `collmap_models`). The IW8 Replay route does not.
- Editor: `E:\IW8-Radiant\data\iw8_entity_defs.json` marks `collmap_scriptable` `buildAccepted: false` ("Not accepted by the converter").
- Converter class list: `E:\mw124\x64-zt\src\games\iw8\dump\maps.cpp:179-205` (`isReplayEntity`, a closed list of 25 classes; `:362` adds the shipped list). No scriptable or collmap class is in it.

## 2. Stock scriptable names

### 2.1 What exists

- **ACTS config:** `E:\mw124\atian-cod-tools\config\data\asset_types.json:799-800` (name only). `E:\mw124\atian-cod-tools\config\data\mw19\schema.json:15234` (ScriptableDef schema). No name list.
- **Editor catalogs:** `E:\IW8-Radiant\data\asset_catalog\` has models, materials, images, sounds, vfx, kits and stock maps. No scriptable file.
- **Stock dumps:** `E:\IW8-Radiant\data\stock_dumps\mp_shipment\...\manifest.json:67` says `"scriptable": 13`, but no scriptable folder is in the dump. The 11 dumped `.iw8` files are in `E:\IW8-Radiant\out\m2-x64zt\dump\mp_shipment\scriptable\` (com_junktire, com_trafficcone_01, decor_rope_thick_coiled_01, decor_rope_thick_pile_01, equipment_military_radio_old_02, player_death_fx, uk_security_camera_redlight_01, veh8_civ_lnd_decho_rebel_darkblue, veh8_civ_lnd_victor40_police_holdexplode, veh8_mil_air_lbravo_flyable, veh_atv_mp). The count is 11, not 13. UNVERIFIED which two are missing.
- **Existing traces (the usable source):** `E:\IW8-Radiant\out\x64zt-verify\runs\trace_*.txt` (76 zones, `A <pool> <name>` lines, written by x64-zt `IW8_READ_TRACE`, `games/iw8/zone/loader.cpp:28, 229-233`). Pool 63 names: 654 unique in 17 zones. All 11 dumped names are present. 317 of the 654 names are also xmodel names. 211 start with `brloot_` (BR loot and killstreak items). Sample zones: `hardpoint_chevron` in 8 zones, `soldier_scriptable_base` in 10, `com_trafficcone_01` in mp_hideout and mp_shipment.
- A scratch extraction of those 654 names (name, zones) was written to `C:\Users\user\AppData\Local\Temp\claude\2b25dc13d65a86cc\f955f107-10f4-450a-a1df-e62aedc1d809\scratchpad\scriptable_names_scratch.tsv`. It is not a repo file.

### 2.2 How to build a catalogue

`E:\IW8-Radiant\tools\build_asset_catalog.py` (53 lines) reads the traces and writes `name<TAB>zones` files. Its `POOLS` map is `{9: 'models', 11: 'materials', 19: 'images'}` (line 14). Adding `63: 'scriptables'` writes `data/asset_catalog/iw8_scriptables.tsv`. The leading `,` cross-zone marker is already stripped (`lstrip(',')`). Re-run: `py -3 -I tools\build_asset_catalog.py` (reads only the traces, no game files).

Limits:
- Traces list only what each verifyzone loaded. 17 of 76 zones contain pool-63 reads, so the catalogue is "scriptables the verified zones load", not every stock def.
- The full set needs an ACTS export per zone with `-a scriptable` (same form as doc 10's `-a map_ents` command). Not run here; it reads game fastfiles.
- Pool 63 in the trace is the same numbering as x64-zt (section 1.1), so the builder's pool mapping is consistent with the dumps.

## 3. Dynamic entities (`replay_dynentity`)

### 3.1 What the fork builds

- Struct `DynamicEntity` (`E:\mw124\x64-zt\src\games\iw8\assets\replay_dynentity.h:11-22`): `model`, `quaternion[4]`, `origin[3]`, `noPhysics`, `rigidBodies`, `collisionFlags`, `basis` (model or brush), `brushModel`.
- Asset `dynentitylist0`, type 113 (`ASSET_TYPE_DYNENTITYLIST`), written by `RegisterDynamicEntityList` (`replay_dynentity.cpp:15, 215-220`). Header writes list counts (`:101-110`), then definitions of 0x50 bytes (`WriteDefinitions`, `:47-88`):
  - model alias at 0x00 (`:60`), quaternion at 0x10 (`:61`), origin at 0x20 (`:62`), basis at 0x31 (`:65`).
  - type is always `DYNENT_TYPE_CLUTTER = 1` at 0x32 (`:66`). Models use radial priority low, spatial leaf 0, clientId none (`:67-74`). Brushes are always active and spawnActive (`:75-81`).
  - `physicsAssetRigidBodiesCount` at 0x48 (`:83`), `noPhysics` at 0x4A (`:84`), `collisionFlags` at 0x4D (`:85`).
- Validation (`RegisterDynamicEntityList`, `:194-214`): a model must have an xmodel, a brush must not, quaternion unit within 0.01, origin and quaternion finite.
- Only clutter is written. ACTS's schema lists `DYNENT_TYPE_SCRIPTABLEINST = 3` and `DYNENT_TYPE_SCRIPTABLEPHYSICS = 4` (`E:\mw124\atian-cod-tools\config\data\mw19\schema.json:7778-7779`), and the fork never writes them.
- Physics presets: an IW3 preset file `physic/<name>` (`PHYSIC\mass\<kg>\sndAliasPrefix\<alias>...`) becomes a clutter body: mass, and a Havok material picked from the sound prefix or name (`E:\mw124\x64-zt\src\convert\iw3_iw8\iw3_fastfile.cpp:3335-3420`, `ReadPhysicsPreset`). Props up to 2 kg are debris (`:3335-3337`). The `physics_system` file gives bodies and hinges (`:3599-3601`). Bake: `:5044-5049`, `:5177-5182`.
- Clutter counts are stamped into MapEnts: `E:\mw124\x64-zt\src\games\iw8\zone\map_zone.h:877-891` (`dynEntCount`, `dynEntCountTotal`, `dynEntNoSpatialCount` for brushes, `dynEntMaxPosePartCount`).

### 3.2 How the converter decides an entity becomes a dynent

No map entity becomes a dynent. The decision is made from the IW3-unlinker collision file:

- `ReadDynamicEntities` (`E:\mw124\x64-zt\src\convert\iw3_iw8\iw3_fastfile.cpp:3562-3670`), called at `:11152` with the `*.replay-collision.json`.
- For each `dynamic_models` entry (`:3580-3625`): `type` must be 1 or it throws "IW3 destructible dynamic models require Scriptable data" (`:3586`). Fields: `model` (leading `,` stripped), `physics_system` (optional, `:3599`), `physics_preset` (optional, `:3611`), `quaternion` (4 values, `:3621`), `origin` (3 values, `:3622`).
- Quaternion: `BasisFromQuaternion` (`:3313-3331`) reads the four values as x, y, z, w and normalises. The unit check is at `:3318-3321`.
- Brushes: `dynamic_brushes` (`:3640-3666`), `type` 1 again (`:3646`), `brush_model` must equal `physics_brush_model`.
- Output: `result.dynamicEntities` (`:12718-12746`). Model name is `mw120r/<map>/dynmodel_<N>` (`:12726`). `clutter` is true when `physics_preset` gave a clutter body. `noPhysics = collisionLod == SIZE_MAX && !clutter` (`:12724-12725`). Clutter gets `rigidBodies` from the physics system and `collisionFlags = 2` (`:12729-12733`).
- The conversion calls `CountDynamicEntities` and `RegisterDynamicEntityList` (`E:\mw124\x64-zt\src\convert\iw3_iw8\convert.cpp:1744-1745, 1806`).
- Contract: `E:\IW8-Radiant\docs\research\15_iw8_extraction_contract.md:134` (`dynamic_models`, `dynamic_brushes`, counts are OPTIONAL; if absent the counts must be 0). Also line 49 (`physic/<name>.json` REQUIRED only when a dynamic model names it).

Editor side: `E:\IW8-Radiant\src\core\compile_iw8.cpp:1623-1624` always writes `"dynamic_model_count":0,"dynamic_brush_count":0,"dynamic_models":[],"dynamic_brushes":[]`.

Stock reference: mp_shipment MapEnts has `dynEntCount` [187, 0] and `dynEntNoSpatialCount` [129, 0]. The dynentitylist dump (`E:\IW8-Radiant\out\m2-x64zt\dump\mp_shipment\dynentitylist\dynentitylist0.iw8`, 15717 bytes, type 113 at byte 8) is consistent with 187 x 0x50 definitions plus list data (arithmetic only, UNVERIFIED). Its model references are asset aliases, not names, so the stock clutter model names are not recovered yet.

### 3.3 What the editor would have to write for one dynent

Proposal (no fork change needed for model clutter if the converter input above is produced):

1. Entity class `dynamic_prop` (point kind): keys `model` (xmodel name, must exist as `xmodel/<name>.json`, doc 15 §3.6), `origin`, `angles`, `physics_preset` (name of a `physic/<name>` file), optional `physics_system`.
2. Compile step: replace the hard-coded empty lists at `compile_iw8.cpp:1623-1624` with one entry per `dynamic_prop`:
   `{"type":1,"model":"<name>","quaternion":[x,y,z,w],"origin":[x,y,z],"physics_preset":"<name>","physics_system":"<name>"}`, plus `dynamic_model_count`.
3. Write `physic/<name>` text files for each preset: `PHYSIC\mass\<kg>\sndAliasPrefix\<alias>\`. Mass must be positive (`:3399`).
4. Quaternion from `angles`: the editor's Euler convention to x, y, z, w is UNVERIFIED. Check it against a known stock clutter record before trusting it.
5. Live script_models are not dynents. They keep a keyframed body (doc 34 §2) and stay `solid()`-covered (roadmap 6.15 wording).
6. Dynent without `physics_preset`: no clutter body, so `noPhysics` = true (`:12724-12725`), i.e. a static-looking dynent. The model must still be in the zone's xmodel list (`replay_dynentity.cpp:60` `assetAlias(XMODEL)`).

## 4. Current editor behaviour

- `src/core/scripted_objects.cpp`:
  - `makeDestructible` (`:23-34`): sets classname `script_model`, targetname `destructable`, adds `footprint_mask_group default`. This is the IW3-compat "destructible" path (x64-zt keeps targetname `destructable` live, `compile_iw8.cpp:1025` lists it). No `ScriptableDef` is involved.
  - `makeScripted` (`:38-69`): gives a unique targetname (`scripted_prop_N`) and adds a `script_origin` 8 units above the origin (`:59-65`). Comment: untargeted script_models are promoted to static props by x64-zt.
  - Wired from `src/qtapp/main_window.cpp:1764` and `:1770`.
- `src/core/compile_iw8.cpp`:
  - `acceptedPointClass` (`:493-...`): the editor's list of classes the converter accepts. No scriptable, collmap or dynamic class.
  - `staysEntity` rule (`:1016-1031`): script_model stays live when it has a gameobject or exploder key, is targeted, or has one of the special names. Others get a static collision box (`:1033-1040`).
  - `misc_model` placements (`:1248-1262`): become world.models placements. Stock names are skipped.
  - script_model in `.ents` (`:1371-1376`): adds `footprint_mask_group default` when missing (doc 34 §1: 205 of 205 stock).
  - Placeholder script_model when none exists (`:1441-1444`).
  - Solid targetnames for the map script (`:1722-1728`).
  - Collision JSON with empty dynamic tables (`:1623-1624`). No code reads or writes `collmap_scriptable` or scriptable instances.
- `data/iw8_entity_defs.json`: `collmap_scriptable` `buildAccepted false`, `misc_model` "Not built" (doc 15 §3.3 wording), script_model `buildAccepted true`.

## 5. Fork changes needed (file and function)

| # | Change | Where | Size |
|---|---|---|---|
| a | Emit `ScriptableInstance` records (stride 72) into `MapEnts.scriptableMapEnts.mapInstances`, set `mapInstanceCount` and `serverInstanceCount` | `E:\mw124\x64-zt\src\games\iw8\zone\map_zone.h` `emitMapEntsBody` (~815-1000, currently writes the empty tree at 979-985). Or route through `neutral\mapents.cpp` `from_neutral` (`:797, :814`) | M |
| b | Emit `CollMapLookupEntry` (16 bytes) and `numCollmaps` | same function | M (depends on the collmap relation, section 1.4) |
| c | Converter input for instances: a JSON list of `{def, origin, angles, classname, targetname, target}`, read in the IW3 path, and the instance's stub reference to a stock def | `E:\mw124\x64-zt\src\convert\iw3_iw8\` (new reader beside `ReadDynamicEntities`, `iw3_fastfile.cpp:3562`) and `zone\writer.cpp` stub path | M |
| d | Accept a new editor class if it is an entity in the map: add to the closed list | `E:\mw124\x64-zt\src\games\iw8\dump\maps.cpp:179-205` `isReplayEntity` (25 classes; a second list of 29 shipped classes follows near `:213`) (only if the class goes through entity text) | S |
| e | Dynamic props: no fork change for model clutter. The editor output in section 3.3 is enough. Scriptable-physics dynents (types 3, 4) would need new writer code in `replay_dynentity.cpp` `WriteDefinitions` | `replay_dynentity.cpp:47-88` | L for type 3/4, not needed for 6.15 |
| f | Author new `ScriptableDef` assets | none exists; `neutral\scriptable.cpp` `from_neutral` is the entry point | L |

The IW7 converter already has a precedent for scriptables: it does not carry `ScriptableDef` but turns breaks into GSC (`E:\mw124\x64-zt\src\convert\iw7_iw8\export_effects.cpp:3875-3900`, `iw7_destructible` calls). That route is cheaper than (a) for destructibles, but it does not give stock scriptable instances.

## 6. Concrete next steps for the editor

1. **Catalogue (no game access):** add `63: 'scriptables'` to `POOLS` in `tools/build_asset_catalog.py` and run it. Result: `data/asset_catalog/iw8_scriptables.tsv`, 654 names, read-only browser list. Add the 11 mp_shipment dump names as a "verified" subset.
2. **Reference placements:** a read-only "stock placements" list per zone from `data/stock_dumps/<zone>/...map_ents.asset.json` (`scriptableMapEnts.mapInstances`). mp_shipment: 75 placements, 9 defs. Use it to check a map's names against stock.
3. **Entity class `scriptable`** (point kind): key `scriptable` (def name), `origin`, `angles`, `targetname`, `target`. Add to `data/iw8_entity_defs.json` with `buildAccepted false` until fork change (a) lands. Editor preview: draw a marker and the def name; no state preview yet.
4. **Fork (a, b, c):** write instances and collmap lookups; test on a showcase map (`samples/dev/`), then check with ACTS `-a map_ents` (run by the user).
5. **Dynamic props:** entity `dynamic_prop` (section 3.3). Change `compile_iw8.cpp:1623-1624` to write the entries and `physic/<name>` files. Need a list of stock preset names first (UNVERIFIED; not found in the repo).
6. **`collmap_scriptable`:** keep `buildAccepted false` until the collmap relation in section 1.4 is understood. It is a brush entity that becomes a collision submodel plus a lookup, so it is not a point class.
7. **State preview (6.14):** parse the stock scriptable dumps (`out\m2-x64zt\dump\mp_shipment\scriptable\*.iw8`) against `structs.hpp:9470` and the part and state structs. UNVERIFIED that the dump file layout matches the in-memory struct; the header is confirmed, the body is not.

## 7. UNVERIFIED list

- Pool 63 is the scriptable type in the game build (agreed by x64-zt, ACTS pools.md, dump headers and trace lines; no game run).
- Stock scriptable count: 13 (manifest) vs 11 dumps. Which two are missing.
- Whether a stock def name not held by the map's zone loads through the x64-zt stub path.
- Whether any path other than the neutral mapents code writes `ScriptableInstance` or `CollMapLookupEntry` (none found by grep).
- The six `collmap_scriptable` targetnames and the 20 `collmapLookups` names do not overlap; the mapping from `*N` brush to `brushIndex` (entity `*53-*58` vs lookup 59-78) is unknown.
- Quaternion order: x64-zt reads x, y, z, w (`iw3_fastfile.cpp:3327-3330`). The game-side convention is not checked.
- Meaning of `type 1` in the IW3 unlinker tables (the converter only accepts 1; the name "Scriptable data" suggests a scriptable-driven dynent).
- Stock dynent model names: not recovered (the dump stores asset aliases).
- Whether live script_models block players in game (doc 34 §4).
- The editor's Euler-to-quaternion convention for `dynamic_prop`.

## 8. Housekeeping

- My first shell command in this task may have created an empty stray file at `/ents.py` (`$TMPDIR` was unset). A later cleanup command was blocked by the safety check, so it was not removed. Delete it manually if it exists.
- `docs/PROGRESS.md` was not updated because this task was read-only and asked for one file only. Add a line there when this doc is accepted.
