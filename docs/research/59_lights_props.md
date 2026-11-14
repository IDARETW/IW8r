# 59. Lights that do not light, and props that do not collide

Date: 2026-11-10. Read-only research. Trees read: `E:\IW8-Radiant` (editor), `E:\mw124\x64-zt` (converter, not edited, not built, no branch change). No game was run and no ACTS or zonetool command was run. Line numbers are for these trees as read today. Items marked **UNVERIFIED** need an in-game or build check.

Related docs: 24 (light entities), 39 (sun), 41 (dynamic entities; partly superseded, see B.1), 55 (unlit baseline), 34 (script models).

## 0. Summary

**A. Light entity beside an explosion does not light the ground.**
- The editor writes the light as a ComWorld primary light and nothing else (`compile_iw8.cpp:683-842`). The converter reads it, calibrates its intensity from its radius, and stamps it into ComWorld and GfxWorld (`convert.cpp:953-1096`, `:1228-1298`, `:1851-1871`).
- The fork's own classification of a light is not a "realtime" flag. It is `spawnFlags` (byte 0 of the ComWorld record), `needsDynamicShadows` (byte 3), and the GfxWorld "mutable / static / moving scriptable" index ranges. The editor can set none of these except `canUseShadowMap` (through `iw8r_shadows`). `needs_dynamic_shadows` and the scriptable range are not written by the editor (`map_zone.h:1196-1229`, `:1161-1177`, `compile_iw8.cpp:660-668`).
- The default build is `--style iw3`, so the light's `light_unit_scale` is 1 (`iw3_fastfile.cpp:10698-10708`, `iw3_vision.cpp:447`). The light's brightness is therefore `falloff * (radius/2)^2` in IW3 units with no further scale (`convert.cpp:1228-1298`).
- Surfaces the editor writes are unbaked (no lightmap, flag bit 4 clear, no light grid). What Replay does with a local light on an unbaked surface is the biggest unknown (doc 55 §2.3). This is the most likely reason a light has no effect on the ground, but it is UNVERIFIED.
- Ranked checks and fixes are in section A.6.

**B. `dynamic_prop` stays static and players pass through it (bullets hit it).**
- The editor now does write dynamic props (`compile_iw8.cpp:1527-1575`, `:1964-1965`). Doc 41 §3.3 is out of date on this point.
- The converter makes them **simulated clutter** only when the physics preset file parses (`iw3_fastfile.cpp:3365-3420`, `:5046-5049`, `:12734-12745`). Clutter is not spawn-active, so it stays put until something hits it. Stock clutter is also written this way (`replay_dynentity.cpp:41-45`, `:69-74`), so "static until shot" is probably the stock behaviour (UNVERIFIED in game).
- Player collision is missing because clutter and script-model bodies get collision contents `0xB180` (`iw3_fastfile.cpp:5088-5093`). `0xB180` has no bit in the converter's player mask `0x2014011` (`:12839`). The converter comment above `:5093` says this is intentional ("players still pass props, as in IW3"). That contradicts the owner's test. Bullets still hit because of other bits (which bits is UNVERIFIED).
- The fix for player collision is on the fork side (`iw3_fastfile.cpp:5093`, set bit `0x1`). The editor cannot express it.

## 1. Method

- Editor: `src/core/compile_iw8.cpp` (lights `:632-842`; dynamic props `:1527-1575`; collision JSON `:1964-1965`; physic files `:2014`), `src/core/build_preflight.cpp:470-510`, `src/core/unlit_baseline.cpp`.
- Fork: `src/convert/iw3_iw8/convert.cpp`, `iw3_fastfile.cpp`, `iw3_vision.cpp`, `iw3_render_assets.cpp`, `iw3_lightgrid.cpp`; `src/games/iw8/zone/map_zone.h`, `src/games/iw8/assets/replay_locallights.h`, `replay_dynentity.cpp`, `write_maps.cpp`, `src/games/iw8/common/replay_havok.cpp`, `replay_havok.h`, `structs.hpp`.
- Stock comparison: `%USERPROFILE%\AppData\Local\IW8 Radiant Suite\IW8 Radiant Suite\stock_cache\mp_shipment` (see A.5), and `E:\IW8-Radiant\out\m2-x64zt\dump\mp_shipment\com_map\maps\mp\mp_shipment.d3dbsp.iw8`.

## A. Light entities

### A.1 What the editor writes

| Step | Where |
|---|---|
| Light classes `light`, `light_spot`, `light_omni` | `compile_iw8.cpp:632` |
| A spot is `light_spot`, or `light` with a resolved `target` or an fov key; otherwise an omni (type 3) | `:741` |
| Optional keys: `iw8r_bulb_radius`, `iw8r_shadow_bias`, `iw8r_falloff`, `iw8r_fade`, `iw8r_shadows`, `iw8r_intensity_units`, `iw8r_uv_intensity`, `iw8r_ir_intensity` | `:634-649` (table `kLightKeys`) |
| `iw8r_shadows 1` sets `can_use_shadow_map` on spots only | `:812-818` |
| JSON written for each light: `type`, `exponent 0`, `can_use_shadow_map`, `color`, `direction`, `origin`, `radius`, `cos_half_fov_outer/inner`, `rotation_limit 0`, `translation_limit 0`, `definition`, plus the optional keys | `:660-668` (`primaryLightJson`) |
| `definition` is `light_point_linear` for every light unless `iw8r_ies` is set | `:666-668` |
| Sun at index 1; lights at index 2 and up; table padded to 256 by the converter | `:1806-1810`, `:1919`; `convert.cpp:1119-1132` |
| The compiler writes the `light_point_linear` definition file (16 bytes) when a light exists | `compile_iw8.cpp:2114` (doc 24 §6) |
| Light entities are kept out of `.ents` | `compile_iw8.cpp:1645` |

The editor **does not** write `needs_dynamic_shadows`, `spawnFlags`, `is_volumetric` (only through the `volumetric` key the converter reads, which the editor never sends), `scriptable_primary_light_count`, `first_scriptable_primary_light`, `up`, or `bulb_length`. The converter reads all of them (`convert.cpp:1054-1061`, `:1139-1142`).

Colour: `_color` or `color`, divided by 255 if any component is above 1, times `intensity` (`compile_iw8.cpp:705-716`); `intensity` is read at `:718-721`. The `intensity` key scales the colour only; it does not reach the converter as an intensity (doc 24 §5.1).

### A.2 What the converter does with it

- Parse: `convert.cpp:953-1096`. `can_use_shadow_map` (`:983-989`), colour, direction, origin, radius, cones, definition. `needs_dynamic_shadows` is read at `:1061` with default from the source. `volumetric` at `:1060`.
- `iw8r_intensity_units` becomes `authoredIntensity` (`:1076-1080`). Without it, the intensity is calibrated (next item).
- Calibration `calibrateLocalLights` (`:1228-1298`): for each light of type 2 or 3, `reference = radius/2`; for spots the reference is the nearest triangle hit along the axis (around `:1244-1275`), at least 16 units (around `:1282`). `falloff = 1 - reference/radius` (around `:1284`). Intensity = `falloff * reference^2 * lightUnitScale` (`:1298`). The comment at `:1221-1227` says the shader gives `intensity * colour * N.L / d^2` inside a window that is full up to 80% of the radius (UNVERIFIED for Replay).
- Local lights go to GfxWorld as `iw8::locallights::Light` (`:1851-1871`). The direction is negated (`:1861`). `dynamicShadows = needsDynamicShadows` (`:1868`).
- Slot rule: index 0 empty, index 1 sun, others local. Padded to 256 (`:1119-1132`).
- Scriptable (GSC `pl#`) lights are the last entries of the table, with `scriptable_primary_light_count` and `first_scriptable_primary_light` in the comworld JSON (`:1099-1102`, `:1139-1142`). The editor writes neither.

### A.3 What the ComWorld record gets

`src/games/iw8/zone/map_zone.h`, `emitComWorldBody` (`:1152-1240`):
- `useForwardPlus = 1` (`:1167`), `isInUse = 1` (`:1166`), bake quality 3 (`:1168`).
- Index 0 gets all zero. Index `sunPrimaryLightIndex` gets the sun (`:1268-1275`).
- Per light (`:1199-1229`): `type` at byte 1; `canUseShadowMap` at byte 2; `intensity` at `+0x10` (`:1201`); colour at `+0x20`; direction at `+0x2C`; origin at `+0x44`; radius at `+0x50`; cones at `+0x6C`, `+0x70`.
- For spot and omni (`:1217-1229`):
  - byte 3 `needsDynamicShadows` = type 2 and `canUseShadowMap` and `needsDynamicShadows` (`:1220`).
  - byte 0 `spawnFlags` = `canUseShadowMap ? (needsDynamic ? 0 : 32) : 40` (`:1229`).
  - The comment at `:1226-1228` says the shipped mp_hideout lights (124 of 124) use 0, 32 or 40 in exactly this way.
- The converter never copies `spawnFlags` from the source. Byte 0 is derived only from the two shadow flags.
- `scriptablePrimaryLightCount` at `+0x40` and `firstScriptablePrimaryLight` at `+0x44` (`:1161`, `:1177`, `:1178`). Both are 0 for the editor's maps.

### A.4 What the GfxWorld record gets (the "static / mutable / moving" classification)

`write_maps.cpp:316-362` (`emitGfxMapBody`):
- `lastSunPrimaryLightIndex` at `0x14`, `primaryLightCount` at `0x18` (`:350-351`).
- `firstMutablePrimaryLight` `0x1C` and `mutablePrimaryLightCount` `0x20` are set to the scriptable count (`:356-358`). For the editor's maps this count is 0.
- `firstStaticScriptablePrimaryLight` `0x2C`, `staticScriptablePrimaryLightCount` `0x30`, `firstMovingScriptablePrimaryLight` `0x34` (`:359-362`).
- Struct names: `structs.hpp:13337-13348`.

Reading of these names: "static", "mutable" and "moving" refer to whether a light is driven by script, not to whether it is baked. The light-binning comment in `replay_locallights.h:40-48` says static lights are binned into the per-frame light set. This reading is an inference from names and comments (UNVERIFIED).

So, in the fork's model, a plain `light` is a **static** light in ComWorld and GfxWorld. It is not a script light, and it has no dynamic-shadow flag unless the editor asks for one through the converter's `needs_dynamic_shadows` input, which the editor cannot send today.

### A.5 Stock comparison

- `stock_cache\mp_shipment` holds only `map_ents` (`...\stock_cache\mp_shipment\mw19replay\mp_shipment\assets\map_ents`). It has no `com_map` export, although `manifest.json` lists `com_map: 1` as loaded (UNVERIFIED why it was not exported). So no stock comworld exists in the cache.
- A stock `com_map` dump exists at `E:\IW8-Radiant\out\m2-x64zt\dump\mp_shipment\com_map\maps\mp\mp_shipment.d3dbsp.iw8` (4534 bytes) and a JSON summary at `out\acts-baseline\mp_shipment\mw19replay\mp_shipment\assets.jsonl` (`payload_bytes 39093`). The JSON has no field values. I did not decode the light array from the binary (UNVERIFIED).
- The only stock light values I can cite are the fork's own comments: mp_hideout spawn flags 0/32/40 (`map_zone.h:1226-1228`), and `needsDynamicShadows` on 37 of 65 shadow-mapped lights (`replay_locallights.h:29-31`).
- Stock scriptable lights are the tail of the table with `pl#` (`maps.cpp:603-608`, `map_zone.h:1139-1141`, `convert.cpp:1139-1142`). Doc 24 §4 says `pl#` lights are the only light entities accepted in `.ents`.

### A.6 Why a light may not light the ground: ranked

1. **Ground outside the light's reach (check first, cheap).** Default radius is 400 (`compile_iw8.cpp:628`). Full brightness only up to about 80% of the radius, and nothing beyond it (`convert.cpp:1221-1227`, UNVERIFIED for Replay units). An explosion placed 400+ units from the light, or a light placed high above the ground, lights nothing. Test: set `radius 1200` and move the light to 150 units above a flat floor.
2. **Unbaked surface does not take local lights (most likely, UNVERIFIED).** The editor writes no lightmap, so `lightmap = -1` and the flag bit 4 is clear (`iw3_fastfile.cpp:2407-2411` and `:3962-3977`; doc 55 §2.3). The shader branch for bit 4 clear is not in these trees. The surface technique is the forward+ lit technique (`iw3_render_assets.cpp:3582`, `:3754`; `replay_render.cpp:255`), so the local-light pass should apply. Whether it does for an unbaked surface is not verified.
3. **Intensity is small in the default build.** `lightUnitScale = 1` under `--style iw3` (`iw3_fastfile.cpp:10698-10708`; `iw3_vision.h:103-109`). A light of radius 400 has intensity `0.5 * 200^2 = 20000` (`convert.cpp:1298`). At 200 units the irradiance in the converter's own model is `20000 / 200^2 = 0.5`. A sun of luminance 1 gives 1 on a flat floor. The light is therefore visible only close to the ground, and its absolute scale depends on the Replay unit conversion, which is UNVERIFIED.
4. **No dynamic-shadow flag.** The editor cannot set `needs_dynamic_shadows`. Without it, spot shadows (if `iw8r_shadows 1`) are the "static shadow map" case (spawn flags 32, `map_zone.h:1229`). Stock spot lights that shadow entities are the dynamic case. Whether the difference matters for the ground is UNVERIFIED.
5. **Explosion FX.** The FX may hide the light or be the brightest thing on screen. Not checked.
6. **Light grid absent.** The editor writes no grid (doc 55 §2.5). The light grid samples primary lights for baked entity lighting (`iw3_fastfile.cpp:11637-11660`). It does not light world surfaces in this path. Affects models and players, not the ground (UNVERIFIED).

### A.7 Fixes

**Editor side (no fork change):**
- Add a key `iw8r_dynamic_shadows` (spot only, needs `iw8r_shadows 1`) and write `"needs_dynamic_shadows":true` in `primaryLightJson` (`compile_iw8.cpp:660-668`, call sites `:837`). The converter already reads it (`convert.cpp:1061`), copies it (`:1868`), and writes byte 3 and spawn flags 0 (`map_zone.h:1220`, `:1229`). This is the only realtime-related flag the editor can set. It is UNVERIFIED that it changes what the ground receives.
- Add a diagnostic: warn when a light's radius does not reach any floor of its map (or a simple distance check against `solid` brushes). Preflight `build_preflight.cpp` is the place.
- Test knob that exists today: `iw8r_intensity_units` (`compile_iw8.cpp:634-649`, `convert.cpp:1079`). It replaces the radius calibration with the value times `lightUnitScale`, so the brightness can be set directly (remember the colour is also multiplied by `intensity`).
- Optional: `iw8r_volumetric` writing `"volumetric":true` (`convert.cpp:1060`, `map_zone.h:1221`). Not needed for the ground.

**Fork side (only after tests, UNVERIFIED):**
- If test 2 (unbaked surface) shows no local-light effect, the change is in the surface encoding (`iw3_fastfile.cpp:3962-3977`): a baked or neutral lightmap for unbaked surfaces, or a shader-side change. This is a larger change and is not recommended before test 1 and the `needs_dynamic_shadows` test.
- No fork change is needed for the editor's light to be read or stamped. The converter path is complete for `needs_dynamic_shadows`, `volumetric` and `intensity`.

## B. `dynamic_prop` and live script models

### B.1 What the editor writes (correction to doc 41)

- Doc 41 §3.3 says the editor writes empty dynamic tables at `compile_iw8.cpp:1623-1624`. That is out of date. The code now collects every `dynamic_prop` point entity (`compile_iw8.cpp:1527-1575`) and writes `dynamic_model_count`, `dynamic_models`, `dynamic_brush_count` (`:1964-1965`).
- Each entry: `{"type":1,"model":<name>,"quaternion":[x,y,z,w],"origin":[...],"physics_preset":"iw8r_<material>_<mass*10>"}` (`:1568-1569`).
- Defaults: `mass` 10 (`:1553`), `material` `plastic` (`:1559`). Material is lowercased and non-alphanumerics become `_` (`:1560-1563`).
- Physic file: `PHYSIC\mass\<kg>\sndAliasPrefix\<material>`, written to `extracted/physic/<preset>` (`:1565`, `:2014`).
- Model: a custom model, or an exported stock model through `stockBuild` (`:1540-1545`).
- Quaternion: `anglesToDynQuaternion` (`:563-...`), basis as the columns of R, order x, y, z, w. This matches the converter's `BasisFromQuaternion` (`iw3_fastfile.cpp:3314-3331`) by my reading of the formulas. The game-side convention is UNVERIFIED.
- Not written: `physics_system` (no hinged pieces), `contents`, `spawnActive`.
- Preflight does not check dynamic-prop models (`build_preflight.cpp:470-510` checks only `world.json` models). A missing xmodel fails only in the converter.

### B.2 What the converter does

- Read: `ReadDynamicEntities` (`iw3_fastfile.cpp:3562-3625`). Type must be 1 (`:3586`). Model name, with the leading `,` removed (around `:3588-3596`). Quaternion and origin (`:3621-3622`).
- Physics preset: `ReadPhysicsPreset` (`:3365-3420`). Reads `physic/<name>` under `root` (`:3365-3375`). Requires `PHYSIC` as the first field (around `:3380-3390`), `mass > 0` (`:3399`), and a material from keywords in the sound prefix or the name (`:3404-...`). Default material is PM_Plastic (`:3404`).
- Clutter gate: `clutter = nativePhysics && sourceModel.clutter && !collisionModels && !scriptModelPhysics` (`:5046-5047`). Physics LOD 0 is used when the model has none (`:5048-5049`).
- Dynent entry: `noPhysics = collisionLod == SIZE_MAX && !clutter` (`:12735`). Clutter gets `rigidBodies` and `collisionFlags = 2` (`:12739-12744`).
- Collision body: clutter gets `collision_filter_info = source.contents` (`replay_havok.cpp:2165`, under the `serializedBodyPolicy` of `:2146`), `motion_type = 2` (`:2172`), `mass` (`:2173`), `system_properties = !clutter` (`:2160`), and the simulation category 4 (Dynamic) or 5 (DynamicDebris) chosen by the caller (`iw3_fastfile.cpp:5177`). Debris is mass 2 kg or less (doc 41 §3.1).

### B.3 Why a prop does not simulate in game

1. **Spawn state.** Model dynents are written with radial priority low, spatial leaf 0, no client (`replay_dynentity.cpp:69-74`). `spawnActive` is set only for brushes (`:80`). The comment at `:41-45` says stock mp_hideout clutter is also not spawn-active and is activated when shot. So a clutter prop sits still until hit (UNVERIFIED in game). This is not a bug in the editor's output. It is the stock behaviour the fork copies.
2. **Silent fallback to static.** If `physic/<preset>` is missing or does not parse, `ReadPhysicsPreset` returns nothing (`:3365-3390`, `:3399`) and the prop has no clutter body. Its `noPhysics` is then true when the model has no collision LOD (`:12735`). No warning is raised at that point. The editor writes the file, so a missing file would need a build-folder problem (UNVERIFIED how often this happens).
3. **Mass.** `mass` is clamped by the editor to `0 < mass < 100000` (`compile_iw8.cpp:1553-1557`); the converter's clutter check rejects mass above 10000 (`replay_havok.cpp:2521`). A prop of 10000 kg or more would fail in the converter (UNVERIFIED the editor's cap is reached).
4. **Engine-side activation.** Nothing in the editor or the fork activates clutter. Explosions and bullets are the expected trigger (UNVERIFIED that an explosion's force reaches a clutter body).

### B.4 Why players do not collide (bullets do)

- Clutter physics contents are set to `0xB180` (`iw3_fastfile.cpp:5088-5093`, comment at `:5088-5090`). The same value is used for `collisionModels` and `scriptModelPhysics` (`:5092`).
- The converter's player trace mask is `0x2014011` (`:12839`; the navigation build uses it as "world collision as players meet it").
- `0xB180` = `0x8000 | 0x2000 | 0x1000 | 0x0100 | 0x0080`. The player mask is `0x2000000 | 0x10000 | 0x4000 | 0x0010 | 0x0001`. The intersection is empty, so a clutter or script-model body is not a player obstacle.
- The bullet mask is not in these trees. Bullets hit the body through some bit in `0xB180` (which one is UNVERIFIED).
- The comment above `:5093` says the empty intersection is intended: "players still pass props, as in IW3". That claim is UNVERIFIED and contradicts the owner's test.
- Replay's named contents bits are in `replay_havok.h:20-22` (`kReplayContents = 0x27F7FFFB`). Bit `0x1` is solid, `0x10` glass, `0x10000` player clip, `0x2000000` in the player mask (names beyond solid and glass are UNVERIFIED).

### B.5 Live script_models

- Live script models (keyframed bodies, `scriptModelPhysics = true`) are built with `BuildPreparedStaticModels(..., "entitymodel", ..., true)` (`iw3_fastfile.cpp:12157-12158`). They take the same `0xB180` contents (`:5092`). They do not block players for the same reason.
- The keyframed body keeps glass out (`:5116-5119`), so bullets pass windows.
- Editor side: `staysEntity` decides live versus static (`compile_iw8.cpp:1207-1216`). A static one gets a collision box (around `:1217-1225`). No editor key sets solidity for a live script model.

### B.6 Fixes

**Fork side (the only place player collision can be set):**
- `src/convert/iw3_iw8/iw3_fastfile.cpp:5088-5093`: for `clutter` and `scriptModelPhysics` (not for `collisionModels`), use `contents = 0xB181` (adds bit `0x1`, solid, which is in the player mask). Expected result: players collide with the prop and bullets still hit it. Side effects to test: the body is solid for every trace that uses bit `0x1`. The navigation build only reads world hulls (`:12834-12880`), so nav is not affected. The lightgrid sight trace reads world brushes and triangles, not model bodies (`:11540-11567`). UNVERIFIED in game.
- Do not change `collisionModels` (static props), because that changes every static prop, and the comment above `:5093` says it was a deliberate choice for IW3 props. That is the owner's call.
- A stock comparison is needed first: the `contents` of a shipped clutter body (mp_hideout) in its `physicsasset` dump. Not found in these trees.

**Editor side:**
- Add a preflight check for `dynamic_prop` models: `xmodel/<name>.json` and `xmodel/<name>.replay.json` must exist (the check exists for `world.json` models at `build_preflight.cpp:486-510`; extend `modelWhere` to the collision JSON `dynamic_models`).
- Document the material keywords the converter recognises (`iw3_fastfile.cpp:3404-3420`): plastic, wood, metal, glass, paper, rubber, brick, concrete, cloth, and their aliases. Others fall back to plastic.
- Document the mass limits (`compile_iw8.cpp:1553-1557`, `replay_havok.cpp:2521`).
- For "it should fall on load": not supported. The fork writes clutter as not spawn-active (`replay_dynentity.cpp:80`). A fork change would be needed (UNVERIFIED that a spawn-active clutter body is safe; stock does not do it).

## C. Test plan (in game, owner)

1. Light: a plain omni with `radius 1200`, `intensity` 2, floor 150 units below. If the ground lights, the problem is reach or intensity (A.6 items 1 and 3). If not, go to item 2.
2. Light with `iw8r_intensity_units` set (for example 20000) to remove calibration. If it still does not light an unbaked floor, A.6 item 2 is the cause.
3. After the editor change `iw8r_dynamic_shadows` (A.7), repeat test 1 with a spot and `iw8r_shadows 1`.
4. Prop: shoot a `dynamic_prop` and check whether it moves. Walk into it and check whether the player stops (B.4). Both results are needed before the fork change in B.6.
5. Build logs to read: `local light %zu: IW3 color ... -> intensity ...` (`convert.cpp:1299`), `dynamic_prop(s) written as physics clutter` (editor warning, `compile_iw8.cpp:1574`), and the converter's `iw3: clutter %s moves in ...` (`iw3_fastfile.cpp:3614`). A missing `clutter` line means the physic file did not parse (B.3 item 2).

## D. UNVERIFIED list

- Whether a static local light (no dynamic shadow) lights an unbaked surface in Replay (A.6 item 2). The bit-4 shader path is not in these trees.
- The brightness scale of `falloff * reference^2` in Replay units (A.2, A.6 item 3).
- Meaning of spawn-flag bits 8 and 32 (`map_zone.h:1226-1228`).
- The bullet mask, and which `0xB180` bit it uses (B.4).
- Whether the `0x1` fix in B.6 leaves other traces unchanged.
- The stock `com_map` light array and stock clutter contents: not decoded (A.5, B.6).
- The game's quaternion and Euler convention for `dynamic_prop` (B.1).
- Whether an explosion's force moves a clutter body (B.3 item 4).
- How often `physic/<preset>` fails to parse in a real build (B.3 item 2).

## E. Doc maintenance

- Doc 41 §3.3 and §4 should be updated to say that `dynamic_prop` is written (`compile_iw8.cpp:1527-1575`, `:1964-1965`). Not done here, as this task allowed one new file only.
- `docs/PROGRESS.md` was not updated for the same reason. Add a line when this doc is accepted.
