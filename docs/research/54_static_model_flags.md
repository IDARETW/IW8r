# 54. Static model lighting, shadow and cull flags (roadmap 5.22)

Scope: placed models (misc_model, and script_model placed as static models) in the IW8 build. Question: which
per-placement lighting, shadow and cull flags can the editor write, which the x64-zt converter already reads,
and which need a fork change. Research only. Nothing was built into the game, so every in-game effect is
UNVERIFIED unless a line says otherwise.

Sources read (read-only): `E:\IW8-Radiant\src\core\compile_iw8.cpp`, `E:\mw124\x64-zt\src\convert\iw3_iw8\*`,
`E:\mw124\x64-zt\src\games\iw8\common\replay_render.*`, `E:\mw124\x64-zt\src\games\iw8\assets\replay_locallights.cpp`,
`E:\mw124\atian-cod-tools\config\data\mw19\schema.json`, and the stock mp_shipment gfx_map export at
`E:\IW8-Radiant\out\acts-ab\export-head\gfx_map\mw19replay\mp_shipment\assets\gfx_map\maps\mp\mp_shipment.d3dbsp.0.asset.json`.

## 1. What the editor writes today

- `compile_iw8.cpp:1442-1485` reads a misc_model's `model`, `angles` and `modelscale`. Nothing else. Custom
  models become `world.models[]` placements; stock names are skipped (1462-1467).
- `compile_iw8.cpp:1791-1798` writes each placement as exactly
  `{"model", "origin", "axis", "scale", "reflection_probe"}`. No `mins`/`maxs`, no `model_flags`, no
  `ground_lighting`, no `cull_dist`, no `lod_scale`, no `shadow_lights`.
- `compile_iw8.cpp:1185-1215` gives each placed model an invisible solid box brush (a world brush, not a model
  flag). This is separate from the model's own collision (see section 3, no_collision).

Consequence: today every placement is lit by the grid, has the sun shadow on, has no distance cull and has the
default LOD scale. None of the keys below reach the build yet.

## 2. What the converter reads per placement (x64-zt, iw3_iw8 path)

The loop is `iw3_fastfile.cpp:3768-3844`. Each `world.models[]` entry is read as:

| Key in world.json | Read at | Effect | Notes |
|---|---|---|---|
| `model` | 3770 | model index | `,name` = external model (3771-3782) |
| `reflection_probe` | 3790-3794 | required, range-checked | throws if missing |
| `shadow_lights` | 3788-3789 | `shadowLights` (authored local-light membership) | empty array = no local light casts |
| `origin` | 3795 | required | |
| `mins`, `maxs` | 3796-3816 | gate for the whole lighting block below | without them the placement uses bounds centre (12050) |
| `primary_light` | 3803 | uint8 | role UNVERIFIED |
| `model_flags` | 3804 | `groundLit = (model_flags & 1) != 0` | bit 0 only |
| `ground_lighting` | 3805 | `groundLighting`, packed GfxColor uint32 | |
| `lighting_colors` | 3806-3815 | 336 hex chars = 168 bytes, else throws | per-model colours |
| `scale` | 3817-3821 | required, > 0, else throws | |
| `axis` | 3822-3825 | required, 3 rows | |
| `lod_scale` | 3829-3830 | `LodScaleIndex`, one of 16 values, else throws | table at 2683-2694 |
| `cull_dist` | 3834-3841 | `ReplayLodDistance`, see section 4 | `<= 0` throws (3837-3838) |

Note: the converter throws on `cull_dist: 0` and on `lod_scale` values outside the table. The writer in
`smodel_flags.cpp` therefore omits both keys at their defaults.

## 3. Flag table

Bits: the GfxStaticModel flags byte is per model (shared by every placement of that XModel), so it cannot carry a
per-placement flag. Per-placement state lives in the collection record (16 bytes, one per placement, byte 14 =
sun-shadow mask) and the instance flags byte (one per placement, LOD scale index). Schema references are to
`mw19\schema.json`; the same enums are in `E:\mw124\x64-zt\src\games\iw8\structs.hpp:12057-12095`.

| Flag | Bit / value | Level | Meaning | Evidence (file:line) | Converter today |
|---|---|---|---|---|---|
| STATIC_MODEL_FLAG_LIGHTPROBE_LIGHTING | GfxStaticModel.flags 0x01 | model | lit from the light grid | structs.hpp:12059; replay_render.cpp:2333 (always set) | always on (not an input) |
| STATIC_MODEL_FLAG_LIGHTMAP_LIGHTING | 0x02 | model | lit from lightmap | structs.hpp:12060; schema StaticModelFlags | never set by converter; stock mp_shipment: 0 of 277 models |
| STATIC_MODEL_FLAG_ANIMATED_OPACITY | 0x04 | model | animated opacity | structs.hpp:12061 | never set; stock: 14 of 277 models |
| STATIC_MODEL_FLAG_REACTIVEMOTION | 0x08 | model | reactive sim bends the model | structs.hpp:12062; replay_render.cpp:2333; iw3_fastfile.cpp:5196-5203 | from XModel reactive parts (per model) |
| STATIC_MODEL_FLAG_SPLINED | 0x10 | model | splined | structs.hpp:12063 | not set |
| STATIC_MODEL_FLAG_DISABLE_VOLUME_DECALS | 0x20 | model | no volume decals | structs.hpp:12064 | not set |
| STATIC_MODEL_FLAG_CLUTTER_TINT | 0x40 | model | clutter tint | structs.hpp:12065 | not set |
| STATIC_MODEL_COLLECTION_FLAG_SUN_SHADOW_1..x | collection byte 14: 1, 2, 4, 8, 16 | placement | sun shadow for sun index 1..5 | structs.hpp:12085-12095; replay_render.cpp:2340-2350 | mask = `(1 << min(lastSun, 5)) - 1`, the same for every placement |
| STATIC_MODEL_COLLECTION_FLAG_UMBRA_OCCLUDER | collection byte 14: 128 | placement | Umbra occluder | structs.hpp:12094 | not set (the writer writes only the sun mask) |
| instance flags byte | 0..15 | placement | LOD distance scale index | replay_render.h:255-258; replay_render.cpp:2353-2359 | from `lod_scale`, default index 6 (1.0) |
| (no field) | - | placement | per-instance cull distance | schema GfxSModelInstanceData (24 bytes: translation, orientation, scale) | not representable |

### 3.1 Per flag: converter exposure and fork need

- **groundLighting** (`model_flags` bit 0 + `ground_lighting` colour). Exposed. Reaches the lighting volume:
  `iw3_lightgrid.cpp:1397-1415` (colour in every texel, alpha = top byte), `1417-1430` (used when there is no
  grid; default colour 0xFF282828), `1484-1489` (grid path: ground-lit placements take the colour and skip the
  grid lookup). The input struct carries `groundLit` and `groundLighting` (`iw3_fastfile.cpp:12052-12053`).
  Requires `mins`/`maxs` in the JSON (3796). Texel channel order vs. the colour the mapper means is UNVERIFIED.
- **noShadow**, local lights. Exposed. `shadow_lights: []` makes the placement cast into no local light
  (`replay_locallights.cpp:387-398`; membership check 325-326). Indices refer to the converter's local light list,
  so `[]` is the only value the editor can write without knowing the indices. The editor does not write
  `shadow_lights` today.
- **noShadow**, sun. NOT exposed. Every placement gets the same sun mask (`replay_render.cpp:2340-2350`, all
  collections write `collection[0x0E] = sunShadowMask`). Stock evidence in section 5: 482 of 9,535 stock
  placements have byte 14 = 0, so stock does use a per-placement sun-off state. Needs a fork (section 4).
- **noShadow**, material level. The material flag 0x40000 (`noCastShadow`, `iw3_render_assets.cpp:8268-8270`) is
  per material and shared by every placement of that model, so it is not a placement flag. Not used.
- **cullDistance**. Exposed, but only per model. `ReplayLodDistance` multiplies by 2 and caps at 1,000,000
  (`iw3_fastfile.cpp:2667-2677`). The model's last LOD distance (`lods.back().distance`) is raised to the
  largest `cull_dist` of its placements (3839-3840). It never lowers a LOD distance, and it cannot cull one
  placement early. A per-placement cull needs a format field that Replay does not have (schema
  GfxSModelInstanceData has no cull). UNVERIFIED in game.
- **lodScale**. Exposed, per placement, fully. `lod_scale` maps to the instance byte (3829-3830, 2353-2359).
  Stock uses 4.0 (index 15) on 5,006 placements, 1.0 (index 6) on 4,523 and 2.0 (index 11) on 6 (section 5).
- **noCollision**. NOT exposed. The converter pushes a collision instance for every placement of a model
  (`iw3_fastfile.cpp:5251-5254`). The editor's own invisible box brush (`compile_iw8.cpp:1185-1215`) is a
  separate world brush, so it needs its own skip. In-game effect UNVERIFIED.
- **lightmap**, **animated_opacity**, **splined**, **disable_volume_decals**, **clutter_tint**. NOT writable per
  placement. They are model-level bits, hard-coded at `replay_render.cpp:2333` (1, or 1|8 for reactive). A key
  here would change every placement of the model. Not recommended. Stock shows LIGHTMAP is unused (0 of 277
  models), so it adds nothing for this mapping.

## 4. Fork changes (exact places)

These are needed only for the sun shadow and for no collision. Everything else is exposed today.

1. `E:\mw124\x64-zt\src\convert\iw3_iw8\iw3_fastfile.cpp:194-206`: add `bool noSunShadow{}` (and `bool noCollision{}`)
   to `SourceStaticModelInstance`.
2. Same file, `3788-3789`: read `instance.value("no_sun_shadow", false)` and `instance.value("no_collision", false)`
   next to `shadow_lights`.
3. `E:\mw124\x64-zt\src\games\iw8\common\replay_render.h:247-259`: add the same two fields to `StaticModelInstance`.
4. `E:\mw124\x64-zt\src\convert\iw3_iw8\iw3_fastfile.cpp:5247-5250`: copy them into the pushed
   `StaticModelInstance` (the brace initialiser needs the new fields at the end).
5. `E:\mw124\x64-zt\src\games\iw8\common\replay_render.cpp:2350`: change
   `collection[0x0E] = sunShadowMask;` to `collection[0x0E] = instance.noSunShadow ? 0 : sunShadowMask;`.
   This is the only per-placement sun gate found. The sun-shadow bake (`convert.cpp:1623-1661`,
   `iw3_render_assets.cpp:6124-6141`) takes world casters only.
6. `E:\mw124\x64-zt\src\convert\iw3_iw8\iw3_fastfile.cpp:5251-5254`: skip `collisionModel.instances.push_back`
   when `noCollision` is set. Also the editor box (`compile_iw8.cpp:1185-1215`) must be skipped for the same
   placement (editor side, not a fork).

Stock uses byte 14 = 129 (sun 1 + umbra) on 6,197 placements, so a fork that writes `0` for no-sun-shadow
placements and keeps `sunShadowMask` for the rest matches stock's shape. Whether to also set the umbra bit (128)
is UNVERIFIED and not part of this change.

## 5. Stock sample (mp_shipment, ACTS export)

Source: `out/acts-ab/export-head/.../mp_shipment.d3dbsp.0.asset.json`, `fields.smodels`. Helper script was a
scratch file, not committed. Counts are exact for this export.

- 277 models, 9,535 collections, each with `instanceCount = 1`, one instance per collection.
- Collection byte 14 (sun/umbra flags): 129 x 6,197; 1 x 2,308; 128 x 548; 0 x 482.
- Instance flags byte (LOD index): 15 (4.0) x 5,006; 6 (1.0) x 4,523; 11 (2.0) x 6.
- GfxStaticModel flags per model: 1 (LIGHTPROBE) x 263; 5 (LIGHTPROBE|ANIMATED_OPACITY) x 13;
  13 (LIGHTPROBE|ANIMATED_OPACITY|REACTIVEMOTION) x 1. No model uses LIGHTMAP, SPLINED, DISABLE_VOLUME_DECALS or
  CLUTTER_TINT.
- Not sampled: the per-placement ground-lighting colours (in the lighting volume, not in the flags). Stock's
  mechanism for ground lighting is therefore UNVERIFIED.

## 6. Recommended misc_model keys

Values are strings as the editor's entity keys store them. Defaults are omitted. Each row says what works today.

| Key | Value | Maps to (world.json) | Works today? | Converter path |
|---|---|---|---|---|
| `no_shadow` | `1` | `shadow_lights: []` and `no_sun_shadow: 1` | local part yes (once the editor writes `shadow_lights`); sun part needs fork items 1-5 | 3788; replay_locallights 389-391; fork 2-5 |
| `ground_lighting` | `1` | `model_flags: 1`, `mins`, `maxs`, `ground_lighting` colour | yes, with `mins`/`maxs` | 3796-3805, 12052-12053, iw3_lightgrid 1484-1489 |
| `ground_color` (proposed, not in SModelFlags) | `r g b` 0-255 | `ground_lighting` = `0xFF000000 \| r<<16 \| g<<8 \| b` | UNVERIFIED channel order; default 0xFF282828 if omitted | iw3_lightgrid 1397-1415, 1428-1430 |
| `cull_dist` | `<IW3 units>`, > 0 | `cull_dist` | yes, but only raises the model's last LOD distance (section 3.1) | 3834-3841, 2673-2678 |
| `lod_scale` | one of 0.25 .. 4 (16 values) | `lod_scale` | yes | 3829-3830, 2683-2694 |
| `no_collision` | `1` | `no_collision: 1` | NO, fork items 2, 4, 6 and an editor box skip | 5251-5254 |

Keys not recommended: `lightmap`, `animated_opacity`, `splined`, `disable_volume_decals`, `clutter_tint`
(model-level, section 3). Reactive motion follows from the XModel, not a key.

Editor wiring (for the person doing the build side, not done here): `compile_iw8.cpp:1442-1485` reads the keys
with `readSModelFlags`; `1791-1798` adds `mins`/`maxs` (from `boundsOf`), `model_flags`, `ground_lighting`,
`cull_dist`, `lod_scale`, `shadow_lights` and `no_sun_shadow`/`no_collision` only when set.

## 7. UNVERIFIED list

- Any in-game effect of any key. Nothing was run.
- Channel order of `ground_lighting` (the converter's texel byte order, `iw3_lightgrid.cpp:1397-1403`) vs. the
  RGB the mapper means.
- That no sun-shadow path other than collection byte 14 gates static model casters.
- How stock places ground lighting per placement (not decoded from the lighting volume).
- Whether `cull_dist` should be a per-model LOD extension at all, or should map to `lod_scale` against the model's
  last LOD distance.
- Whether Replay uses the collision instances (`5251-5254`) for placement collision in game.
- Whether the editor's world.json is the document `iw3_iw8` reads for every build. Inferred from the x64-zt
  comments quoted in `compile_iw8.cpp` (1435-1440) and the reader at `iw3_fastfile.cpp:3743`; not run end to end.
- The meaning of `primary_light` (`iw3_fastfile.cpp:3803`).
