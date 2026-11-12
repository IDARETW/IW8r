# 46. Decal tool, decal volumes, volumetrics, glass and foliage/reactive motion: converter status and editor path

Date: 2026-11-10. Scope: roadmap 5.7 (Volumetrics volumes), 5.8 (Decal volumes), 8.10 (Decal tool), 5.12 (Glass and foliage/reactive motion flags), with 5.23 (wind) where it overlaps 5.12. Read-only research: no game file was read or run, no build was run, nothing was edited except this file.

Sources:
- `docs\research\09_iw8_render_features.md` sections 2, 10, 14, 15 (the starting claims).
- `docs\research\31_atmosphere_route.md` sections 3.7 and 3.9 (volumetrics and decal volumes, already traced).
- x64-zt fork, read only: `E:\mw124\x64-zt\src\convert\iw3_iw8\` (`iw3_render_assets.cpp|.h`, `iw3_fastfile.cpp`, `convert.cpp`), `E:\mw124\x64-zt\src\convert\iw7_iw8\` (the IW7 exporter that produces the same keys), `E:\mw124\x64-zt\src\games\iw8\` (`common\replay_render.cpp`, `neutral\gfxworld.cpp`, `assets\write_maps.cpp`, `dump\xmodel_dump.h`).
- Editor, read only: `E:\IW8-Radiant\src\core\compile_iw8.cpp`, `core\custom_materials.h`, `core\collision_world.cpp`, `cli\compile_cmd.cpp`.

Notation: `R:` = `iw3_render_assets.cpp`, `P:` = `iw3_fastfile.cpp`, `C:` = `convert.cpp`, `E:` = editor `src\core\compile_iw8.cpp`. Line numbers are from the checkouts read today. Items not confirmed by reading code are marked UNVERIFIED.

---

## 0. Summary

| Feature | Converter writes it today? | Input the converter reads | Editor path that works with today's converter | Fork change needed |
|---|---|---|---|---|
| 5.7 Volumetric boxes | Yes, from an extraction key. No IW3 source produces it. | `world.json` `volumetrics[]` (`R:5682`), copied to the render mesh (`P:6146`), validated by Replay's reader (`replay_render.cpp:1406-1465`) | Editor writes `world.json` `volumetrics` from an `iw8r_volume` entity (box from mins/maxs, same pattern as reflection probe volumes, `E:1252-1279`). Units of density and falloff UNKNOWN. | None for the basic box. Optional: extend the converter to accept editor keys directly. |
| 5.8 Decal volumes (GfxStaticDecalVolumePacked placements in the world) | No. No producer exists. The neutral writer round-trips the arrays, but nothing fills them. | None | None. Use the surface decal path (8.10) instead and rename the roadmap item, or keep it open. | Large: a producer (new world key), a GfxWorld writer, and packing bit layouts that are UNKNOWN. |
| 8.10 Decal tool (projected decal) | Yes, as ordinary blended or alpha-tested world surfaces. The stock "paint decal" path exists but is unreachable for editor maps (section 4.2). | A world surface whose material is a custom library material (`E:1572-1582`) with a tag in its techset name | Phase A (no converter change): a thin nonColliding patch with an alpha-tested custom material. Phase B: add a blend tag `_b0` to the custom material techset (one editor line). | None for A or B. A dedicated decal input (fork) would make it robust. |
| 5.12a Glass panes | Yes, when the pane is a `script_brushmodel` whose material is a blended (glass-kind) material. Breakable panes need `--replay`. | `script_brushmodel` `*N` brushes whose surfaces are glass-kind (`P:5665-5725`). Or the IW7 `glass_panes` table (`P:5537`, extraction only). | Phase B tag (above) plus the existing `script_brushmodel` brushes. Editor already emits `*N` brush models (`cli\compile_cmd.cpp:95`). | None for basic panes. Fracture or hole layout: UNKNOWN, no input. |
| 5.12b Foliage and reactive motion | Yes, for static model placements only. | Model JSON `reactiveMotion` {`wind`[4], `parts`[]} (`P:2980-2990`), material `iw7ReactiveMotion` true (`R:2591`), vertex colours (UNVERIFIED in the IW3 route) | Editor writes model JSON `reactiveMotion` and the material key. Editor models are static placements (`E:1657-1673`), so they qualify. | None for static placements. Global wind field: none exists (UNKNOWN whether Replay has one). |
| 5.12c Cloth flag waves | Yes, static placements only. | Material `iw7FlagWave` (12 floats) and `iw7FlagClassic` (`R:2579-2589`) | Editor writes the two material keys. | None for static placements. |

Four facts shape every row:

1. The converter is extraction-driven. It reads a folder of `*.replay-world.json`, `*.replay-collision.json`, `*.replay-comworld.json` and the materials and models beside them (`P:10665-10677`, `P:2332`). Every editor feature therefore has to be a key the converter already reads, or a fork change.
2. The editor's `world.json` has none of the keys below. Its top-level keys are `schema, name, sky, bounds, sun, sun_primary_light_index, primary_light_count, reflection_probes, models, grading, dpvs, brush_models, surfaces` (`E:1694-1700`).
3. The editor's custom material techset has no blend tag, so every editor surface is opaque or alpha-tested. Its techset is `iw8rcm<hash>` with `_a1` only for alpha test (`E:894-896`). No editor material can be glass-kind or stock-decal today.
4. The editor writes no `lightmap` key on surfaces (`E:1616-1626`), and no `lightmaps` array in `world.json`. Both stock decal and the native lightmapped world need a baked lightmap (section 4.2). So the editor's world always takes the atlas route (`R:5905`).

---

## 1. Shared facts about the editor's current output

- Surfaces: `{material, reflection_probe, indices, vertices[position, normal, tangent, binormal_sign, uv, lightmap_uv]}`. No lightmap index (`E:1616-1630`). The converter's default for a missing surface lightmap is -1 (`P:2404`). This is the "unlightmapped" value (`P:2405-2411`).
- Materials: `materialFor(editorMaterial)` returns the custom library name (`buildName` or `name`) when the library has it, else the grey material (`E:1572-1582`). The custom material JSON is written by `customMaterialJson` (`E:892-905`). Its only tags come from `alphaTest` (`_a1`) and `twoSided` (a `stateBits` entry with `cullFace none`, `E:896`).
- Patches become world surfaces with their own material (`E:357-369`, `E:1028-1029`). Patches collide, unless their text contains `nonColliding` or their material name contains `trigger` or `hint` (`core\collision_world.cpp:65-72`).
- Models: placements go to `world.json` `models` as static placements, with `model`, `origin`, `axis`, `scale`, `reflection_probe` (`E:1657-1673`). Static placements are the only place reactive motion and cloth waves apply (section 6).
- Brush models: `script_brushmodel` brushes are compiled as `*N` (`cli\compile_cmd.cpp:95`, `E:494`, `E:1012-1014`).
- Custom library properties: `alphaTest`, `twoSided` (`core\custom_materials.h:25-26`). Nothing for blend, sort key, decal, or glass.

---

## 2. Feature 5.7: volumetric boxes

### 2a. Status today

- Writes: yes, from a `world.json` key. The IW3 converter reads `world.volumetrics` (`R:5682-5731`), copies each entry, replaces each mask `image` with a resident image name, and writes the array into the mesh JSON `volumetrics` (`P:6146`). Then `replay_render.cpp:1406-1465` builds the Replay `Volumetric` records.
- Producer on record: only the IW7 exporter (`convert\iw7_iw8\export_world.cpp:1975-2026`). It writes `live_path`, `flags` (always 0), `center`, `axes`, `half_size`, `color` (always white), `density`, `falloff`, `masks`. It does not write `height_fade` or `anisotropy`. No IW3 source has volumetrics (`R:5682` reads only the key).
- Replay's reader rules (`replay_render.cpp:1406-1465`):
  - `flags & ~6` must be 0 (`:1436`).
  - `density` required and >= 0. `falloff` required and > 0. `half_size` each > 0.
  - `axes` must be exactly 3 vectors of 3 (`:1426`).
  - `masks`: at most 4. Only `type` 2 (opacity) draws. `axis` 0 to 2, plus 4 for negative (`:1451`). `scale` non-zero. Each mask image is loaded by name.
  - At most 256 volumes (`:1410`).
- Global scatter is a different input: `world.iw7_volumetrics` (`P:10940-10958`, `V:706-754`), and vision overrides at `extracted\vision\<map>.overrides.json` (`P:10940-10958`). The override file is what the IW8 Radiant vision editor writes (`P:10945-10947`). This covers the look of the air, not the boxes.

### 2b. Stock decal route

Not applicable.

### 2c. Editor: what it should write

Each `volumetrics[]` entry, exactly as the converter and Replay read it:

```
{
  "live_path": "",               // optional, <= 255 chars
  "flags": 0,                    // optional; only bits 2 and 4 allowed
  "center": [x, y, z],           // world units
  "axes": [[1,0,0],[0,1,0],[0,0,1]],   // orthonormal box axes, exactly 3 x 3
  "half_size": [hx, hy, hz],     // each > 0
  "color": [1, 1, 1],
  "density": 0.2,                // >= 0; UNITS UNKNOWN
  "falloff": 64.0,               // > 0; UNITS UNKNOWN
  "height_fade": 0.0,            // optional; default 0
  "anisotropy": 0.0,             // optional; default 0
  "masks": []                    // optional, <= 4, type 2 only
}
```

Editor entity proposal, following the existing reflection probe pattern (`E:1252-1279`, `mins`/`maxs` relative to origin). Key names use the editor's `iw8r_` prefix (`E:715`, `E:1684`). Names are a proposal, not existing keys:

- `classname "iw8r_volume"` (or a `script_brushmodel`-free box entity with `mins`/`maxs`) with `origin`, and keys `iw8r_density`, `iw8r_falloff`, `iw8r_height_fade`, `iw8r_anisotropy`, `iw8r_color`.
- Box: `center = origin + (mins + maxs) / 2`, `half_size = (maxs - mins) / 2`, `axes` from `angles` (identity when angles are zero).
- Write each one into `world.json` `volumetrics`. Mask images: DDS under `images/` with a name the converter resolves (`R:5686`).

### 2d. Fork changes

None needed for the basic box. Optional: nothing. The converter already validates the key set and will throw on a bad entry (`replay_render.cpp:1436-1441`). The editor must check `density`, `falloff`, `half_size` and `axes` before writing, so that a bad entry does not stop the build.

UNVERIFIED: the units of `density`, `falloff`, `height_fade` (doc 31 section 3.7 says the same). The in-game look of a box is also UNVERIFIED.

---

## 3. Feature 5.8: decal volumes (world placements)

### 3a. Status today

- The world arrays exist in the neutral schema and round-trip. `gfx_world_draw` carries `decal_volume_materials`, `decal_volume_masks` and `decal_volume_collections`, plus the counts and `material_and_mask_indirection` (`games\iw8\neutral\gfxworld.cpp:1235-1300`, writer at the same place). `gfx_static_decal_volume_packed` has `obb_center`, `obb_quat`, `obb_half_size`, `opacity_params`, `uv_matrix_and_offset`, `packed_material_info`, `reserved` (`games\iw8\neutral\gfxworld.cpp:5640-5660`, schema `core\assets\gfxworld_data.hpp:2202-2212`).
- No producer. Nothing in `convert\iw3_iw8`, `convert\iw7_iw8` or `games\iw8\assets` fills these arrays (a search for `decal_volume`, `decalVolume` and `static_decal` finds only the schema, the neutral round-trip, and the FX material path below). `games\iw8\assets\write_maps.cpp:365-368` leaves the decal sort range zeroed.
- FX decal path only: `R:8533-8577` builds `decalVolumeMaterial` and `decalVolumeDefinition` for an IW3 FX material whose `cameraRegion` is `decal`. It is attached to a particle effect, not placed in the world (`R:2141-2186`, `R:4565-4583`, doc 31 section 3.9). This is effect decals, not map decals.
- Correction to doc 09 section 2: "x64-zt writes it: yes" is true only in the sense that the neutral round-trip carries the arrays. Nothing produces a placement, so the feature is not writable from editor data.

### 3b. Stock decal route

Not applicable.

### 3c. Editor

Nothing cheap. Placement bits are UNKNOWN: `obbQuat`, `obbHalfSize`, `opacityParams`, `uvMatrixAndOffset`, `packedMaterialInfo` are packed `uint32` (`gfxworld_data.hpp:2202-2212`) with no bit layout in the source. Replay's `decalVolumeMaterial` also needs a six-channel definition (`R:8567-8576`), not a custom library material.

Recommendation: rename the roadmap item from "Decal volumes (box entities)" to "Decal surfaces" (the 8.10 route, section 4). Keep "decal volumes" open as a fork item.

### 3d. Fork changes (if decal volumes are wanted later)

1. A producer: a world key (for example `decal_volumes`) read in `R:` next to the volumetrics block (`R:5682`), and a `GfxWorldDraw` placement writer in `games\iw8\assets\write_maps.cpp` near `:365`. Currently the decal ranges are zeroed.
2. Bit layouts for the packed fields, from a shipped Replay asset (UNKNOWN).
3. A decal volume material writer: the six-channel definition (`R:8567-8576`) for each non-FX material.

UNVERIFIED: whether Replay needs a non-empty `decal_volume_collections` for a placement to draw.

---

## 4. Feature 8.10: the Decal tool (surface decals)

### 4.1 The converter's decal route (what triggers it)

Stock decal (IW3 lit blended decal to Replay's paint technique). All of the following must hold for a material to become a stock decal:

1. The material is glass-kind. That needs a blended techset name (`(^|_)b[0-9]`, `R:2657`) or a stateBits pass with `srcalpha`/`invsrcalpha`, depth write off (`R:2831-2852`). Then `kind = glass` (`R:2897-2899`).
2. `material.blend == 0`, so it is not additive or multiply (`R:2851-2853`, `R:7566`).
3. Flag 2048 is set. The converter sets it when the material is glass-kind, not additive or multiply, and its name does not contain `glass` (`R:2901-2906`).
4. Flag 128 is not set (the unlit techset name or no lit pass, `R:2956-2967`), and flag bits 22 (reveal, `R:23`) and 20 (emission, `R:59`) are not set (`R:7566-7568`).
5. `emission == 1` and the material is not part of a layered set (`R:7566-7568`, `R:7638`).
6. `--replay` is given (`R:7569-7570`), and `buildStockWorldMaterials` runs. It runs only if `world.replay_stock_world` is true or the map has a shadow-only caster or distortion material (`R:7767-7768`, `R:7201-7207`). The key is not written by any code in the fork or the editor. It is an extraction-side flag (doc 15 section 2).
7. The decal branch builds `twc/paint_lines_01_thick_yellow_clean` with `replayColorMap` (`R:2673` for the semantic, `R:7573-7585`, `stockColour` at `R:7296-7310`). No check guards an empty `replayColor`. UNVERIFIED: whether an empty `replayColor` throws or yields a blank colour.
8. The stock material is applied per surface only if the surface is lightmapped (`P:6421-6425`): `surface.lightmap >= 0` and no layer UVs.

Native decal (no stock route). Any glass-kind material that is not a stock decal is drawn as a native or atlas world variant with the kind `glass`, `glass_b<n>`, `_d<row>`, `_bias` (`iw3_render_assets.h:102-121`, `P:3918-3935`). This is the route an editor decal takes today once it is glass-kind.

### 4.2 Why the stock decal route is unreachable for editor maps today

- Surfaces need `lightmap >= 0` (point 8 above). The editor writes no lightmap index (`E:1616-1630`), so `lightmap = -1` (`P:2404`).
- The lightmaps come from `world.json` `lightmaps` (`R:5737-5794`), which the editor does not write. With no lightmap atlas, `nativeWorld = !plan.lightmaps.empty() && !atlasWorld` is false (`R:5905`), so the whole world takes the atlas route.
- Consequence: `replay_stock_world` changes only the lightmap unit scale (`R:6217`) and builds stock world materials that no editor surface selects (`P:6421-6425`). Do not set `replay_stock_world` for editor maps. It has side effects and no decal benefit.
- If the editor later bakes lightmaps, the stock decal route becomes reachable. That is a much larger job (doc 09 section 5 and 31).

### 4.3 Atlas route for an editor decal (what it draws today)

- A glass-kind material on an editor map goes to the atlas world path. Atlas materials include glass-kind surfaces (`R:5467-5474`). Its variant kind is `glass` plus `_b<n>` if blended (`iw3_render_assets.h:102-121`).
- UNVERIFIED: the look (alpha from the colour map alpha, depth order, fade). The fork has no test for an editor-authored decal.

### 4.4 Proposal: an editor Decal tool that works with today's converter

Two phases. Both use only keys the converter already reads.

Placement (both phases):
- Click a surface: hit point P, normal N. Build a tangent frame from N and the editor's existing projection (the patch projection in `E:375`).
- Create a flat patch (2 x 2 control grid, so a quad) of width W and height H, rotated by the chosen angle, centred at `P + N * offset`. Default `offset` UNVERIFIED (0.5 to 1.0 unit is a guess; the converter does no depth bias for decals, `R:2803-2822`).
- Patch text includes `nonColliding` (`core\collision_world.cpp:67`), so the decal does not block players.
- Patch material = a custom library material created for the decal (the editor's Decal browser is a filtered view of the custom library, doc 21).

Phase A (no converter or editor-JSON change beyond a flag): custom material with `alphaTest: true`. The techset becomes `iw8rcm<hash>_a1` (`E:894`), so `authoredAlpha` gives `kind = cutout` (`R:2655-2656`, `R:2897-2899`). Result: a hard-edged decal. Its colour map alpha is the mask (the editor already does this for alpha-tested images, `E:1779`). Blends in no way. Safe for stains, posters, signs and cracks.

Phase B (one editor line): add a blend tag to the custom material techset, for example `iw8rcm<hash>_b0` instead of `_a1`. Then `blended` is true (`R:2657`), `kind = glass`, flag 2048 is set (unless the name contains `glass`), and the atlas glass variant draws it blended (section 4.3). Soft edges and translucency. UNVERIFIED: whether the alpha ramp the colour map carries is respected in the atlas glass variant.

Optional (both phases): `sortKey` in the material JSON (`R:2798-2822`) sets the layer rank for coplanar layers, in flags bits 12-17 (`R:2810-2822`). Editor JSON does not write this key today. UNVERIFIED whether the rank reaches the atlas variant.

Mapper test for Phase A: place a 128 x 128 decal on a lit wall, build, load in game, confirm the decal shows on the wall, does not block a walk through it (`nonColliding`), and does not flicker at a distance.

### 4.5 Fork changes for a robust decal (not required)

- A dedicated `replayDecal: true` material key (pattern of `iw7ReactiveMotion`, `R:2591`), so the converter does not infer decal from the stateBits or name heuristics. It would also skip the `replay_stock_world` requirement. Functions: `ReadMaterial` (`R:2539`) and the decal gate (`R:7566`).
- Then the stock route would need lightmaps (section 4.2). The native and atlas variants remain the editor route.

---

## 5. Feature 5.12a: glass

### 5a. Status today

- Panes are written from two sources:
  - Extraction table `world.glass_panes` (IW7), via `BuildIw7GlassPanes` (`P:5537-5600`, call at `P:6759-6760`).
  - IW3 path `BuildGlassPanes` (`P:5665`, call at `P:6761`): each `script_brushmodel` entity with `model` `*N` whose brush has a glass-kind, non-additive, non-shattered surface (`P:5709-5721`). Pane geometry comes from the brush bounds (`P:5722-5742`). It needs a textured face in its plane (`P:5742`, `P:5770`), or a tilted frame (`P:5744-5755`).
- Size range: half extents 1/8 to 32767/32 units (`P:5759-5762`). Outside it, the brush stays an unbreakable brush model (with a warning).
- Physics: glass needs `--replay` (`C:1667-1668`). The fx map with the panes is written at `C:1814`.
- Translucent glass: names beginning with `glass` plus `translucent_glass` put the material in the translucent camera region (`R:7074-7082`). This is an extraction key only (UNVERIFIED who writes it).
- Editor: `script_brushmodel` brushes are already compiled as `*N` (`cli\compile_cmd.cpp:95`, `E:494`). A glass pane is therefore a `script_brushmodel` brush whose faces use a glass-kind custom material (Phase B tag, section 4.4).

### 5b. Stock decal route

Not applicable (glass panes do not use it).

### 5c. Editor: what it should write

1. Brush: a thin `script_brushmodel` box (thickness at least 1/4 unit, larger face at least 1/4 unit each side, and no more than 1023 units half extent). The textured face is the largest face (`P:5742`, the textured face in the pane plane).
2. Material: custom material with the blend tag (`_b0`). Name must not start with `glass` if it is to be a decal, but a pane can be named `glass_*` (the name rule only affects flag 2048 and the translucent route).
3. Optional `translucent_glass` in `world.json` (extraction key). UNVERIFIED that the editor should write it.

### 5d. Fork changes

- None for basic panes.
- Fracture and hole layouts: UNKNOWN. No input in the converter (doc 09 section 14). Would need a new input and a Replay piece-table writer.
- UNVERIFIED: whether an editor pane with a blended material and no `glass` name draws translucent on the converted map.

---

## 6. Feature 5.12b and 5.23: foliage, reactive motion, cloth flags and wind

### 6a. Status today

- Reactive motion (per static model): the model JSON key `reactiveMotion` is read at `P:2980-2990` into `Iw8ReactiveMotion` (`games\iw8\dump\xmodel_dump.h:122-137`):
  - `wind` (4 floats: area, amplitude, frequency scales, then 1; default `{0.1, 0.2, 0.3, 1}`, `xmodel_dump.h:135`).
  - `parts[]` each with `center` (3), `stiffness` (float), `pivot` (3). Parent is `0xFFFF` (no hierarchy). Rest rotation is identity (`xmodel_dump.h:124-128`).
- The material key `iw7ReactiveMotion` (`R:2591`) marks a surface as reactive. Only static placements bend: `reactiveModel = !entityDraw && sourceModel.reactiveMotion` (`P:4676`). Reactive surfaces get XSurface flag 4 (`P:4931-4936`), and the model gets XModel flag 8 and the placement gets `STATIC_MODEL_FLAG_REACTIVEMOTION` (`P:5196-5203`).
- Vertex encoding: the IW7 exporter writes the part index in vertex green and the weight in vertex blue (`convert\iw7_iw8\export_models.cpp:297-306`). UNVERIFIED that the IW3 route (`P:4940-4960`) carries these colours into the packed vertex. The IW3 route writes a metadata word at vertex offset 8 (`P:4944-4950`). The editor's OBJ importer would need to write colours.
- Cloth and flag waves: material key `iw7FlagWave` (12 floats) and `iw7FlagClassic` (`R:2579-2589`). A techset name `mc_*_flag_*` also marks a flag wave (`R:2578`). Only static model placements get the wave (`R:8209-8213`). World brush surfaces do not move.
- Foliage classification by name (`R:3021-3050`): an opaque material whose name or colour map contains `leaf|foliage|tree|bush|grass|...` becomes cut-out if its colour alpha is split. This is the converter's existing foliage heuristic. It is not wind.
- Wind globally: none found. The vision field list has no wind, reactive, or foliage field (`replay_vision_names.inc`, grep). UNKNOWN whether Replay has a global wind field.

### 6b. Stock decal route

Not applicable.

### 6c. Editor: what it should write

For a foliage model placed as a static placement (`world.json` `models`):
1. In the model JSON (custom or stock): `reactiveMotion` = `{ "wind": [area, amp, freq, 1], "parts": [{"center": [x,y,z], "stiffness": s, "pivot": [x,y,z]}] }`.
2. Surfaces of the foliage material: material key `"iw7ReactiveMotion": true` (added to the custom material JSON, `E:892-905`).
3. The model's mesh: vertex green = part index, vertex blue = weight (UNVERIFIED for the IW3 route, see 6a).
4. Cloth: `"iw7FlagWave": [12 floats]`, and `"iw7FlagClassic"` if needed. Static placements only.

Mapper test: place a reactive bush, build, walk past it in game, confirm the stems bend. Compare with the same bush without the key.

### 6d. Fork changes

- None for static reactive models or flag cloth.
- Wind as a per-map setting: none exists. A global field would be a new fork input and a Replay writer (UNKNOWN which Replay field, if any).
- DObj (entity) foliage does not bend (`R:8201-8212` and `P:4676`). That is a design limit of the converter, not an editor issue.

---

## 7. Fork changes, consolidated (only what is needed)

| Change | Needed for | Named functions and lines |
|---|---|---|
| None for 5.7 basic boxes | 5.7 | `R:5682-5731` (reader), `P:6146`, `replay_render.cpp:1406-1465` |
| None for 8.10 phases A and B | 8.10 | `R:2539` (`ReadMaterial`), `R:2657`, `R:2897-2906` |
| None for 5.12a basic panes | 5.12a | `P:5665` (`BuildGlassPanes`), `P:5537` (`BuildIw7GlassPanes`) |
| None for static reactive and flag cloth | 5.12b | `P:2980`, `P:4676`, `P:4931`, `R:2578-2591`, `R:8209` |
| Dedicated `replayDecal` material key (optional) | 8.10 robust | `R:2539`, `R:7566` |
| Decal volume producer, placement writer, packed bit layouts | 5.8 | `R:5682` (pattern), `games\iw8\assets\write_maps.cpp:365`, `games\iw8\neutral\gfxworld.cpp:5640-5660`, `replay_render.cpp` (new reader) |
| Fracture or hole layout input for glass | 5.12a | UNKNOWN |
| Global wind field | 5.23 | UNKNOWN (no Replay field found) |
| Baked lightmaps from editor (makes the stock decal route reachable) | 8.10 quality | `R:5737-5794`, `R:5905`, `P:6421-6425` (large) |

---

## 8. Mapper-test checklist (one per row)

| # | Test | Pass |
|---|---|---|
| 1 | Volume box, density 0.2, falloff set, over a corridor | Haze inside the box only (units UNVERIFIED) |
| 2 | Phase A decal, 128 x 128, on a lit wall, nonColliding | Visible on the wall, no collision |
| 3 | Phase B decal (`_b0`), same placement | Soft edge, translucent, no flicker |
| 4 | Glass pane: thin `script_brushmodel` with a `_b0` material, build with `--replay`, shoot it | Breaks, collision pane present |
| 5 | Reactive bush (static placement), `reactiveMotion` plus `iw7ReactiveMotion` | Stems bend when walked past |
| 6 | Cloth with `iw7FlagWave` on a static placement | Cloth moves |

---

## 9. UNVERIFIED register (this doc)

1. Units of `density`, `falloff`, `height_fade` (volumetrics).
2. In-game look of a Phase A cut-out decal and a Phase B glass-kind decal on an atlas map.
3. Whether an empty `replayColor` throws in the stock decal branch (`R:7573-7585`). Not reachable for editor maps today (section 4.2).
4. Whether a surface `sortKey` rank reaches the atlas variant.
5. Default decal offset and z-fight tolerance.
6. Whether the editor's colour map alpha survives the custom material image path (`E:1779`) for blended materials.
7. Vertex colour encoding in the IW3 static model route for reactive parts (section 6a).
8. Whether Replay has a global wind field.
9. Glass fracture or hole layout inputs.
10. Whether `translucent_glass` should be written by the editor, and what it changes for a `glass_*` pane.
11. Packed bit layouts for `GfxStaticDecalVolumePacked`.
12. Whether a non-empty `decal_volume_collections` is needed for a placement to draw.
13. Whether `nonColliding` in patch text is the exact token the editor writes (`core\collision_world.cpp:67`). Needs a check against the editor's map writer.

---

## 10. Notes on doc 09

- Section 2 (decal volumes): "x64-zt writes it: yes" should read "the neutral schema round-trips the arrays (`gfxworld.cpp:1235-1300`); no converter producer exists (doc 31 section 3.9 agrees)".
- Section 14 (glass) and section 10 (volumetrics): the claims hold, but the inputs are extraction keys (`world.glass_panes`, `script_brushmodel`, `world.volumetrics`), which the editor does not write today.
- Section 15 (foliage): the reactive record and flag wave inputs are as described. Static placements only.
