# 15. IW8 extraction contract for a native source

Scope: the extraction folder that `zonetool::convert::iw3_iw8::convert_extraction` reads, as used by the x64-zt fork (`E:\mw124\x64-zt`, branch `iw8-conversion`). This is a read-only reading of the code. Nothing was built or run, and no code was changed. Paths are relative to `E:\mw124\x64-zt\` unless they are absolute. Line numbers are from this checkout. Doc 08 §5 cites older numbers (for example, it puts `PrepareFastfile` at 10427; here it is at 10600).

Pipeline: `convert_extraction` (`src/convert/iw3_iw8/convert.cpp:2711`) → `convert_map` (`:2586`) → `buildIw3` (`:2461`) → `iw3::PrepareFastfile` (`src/convert/iw3_iw8/iw3_fastfile.cpp:10600`), which writes the map's files into a scratch folder under `maps/mp/` → `buildMap` (`convert.cpp:2157`), which reads them back through `dumpsrc::DumpSource` (`src/games/iw8/dump/dump_source.cpp:53-80`) → zone writers.

## 0. Summary

1. **Required:** `complete` at the extraction root and `skeleton/complete` (external extractions only). In `extracted/`: `maps/mp/<m>.d3dbsp.replay-world.json`, `.replay-collision.json`, `.replay-comworld.json` and `.ents`, each exactly once or for the map. Also `xmodel/<name>.json` for every entity model, `materials/<name>.json` for every surface material, and `images/<name>.dds` for each reflection-probe cube and each non-built-in texture.
2. **Optional:** the light grid (`.lightgrid.json` plus `.bin` files, or `.gpulightgrid.bin`), `world.lightmaps`, `sun/<m>.sun`, `maps/clienttriggers.json`, `maps/mp/<m>.gsc`, the fog, grading, volumetrics and light-region keys, and the script, xanim and physics-model lists.
3. **Dead or not read on this route:** `<map>_iw8_ents.txt` (`convert.cpp:2204-2213` looks for it next to `prepared.root`, a fresh scratch folder that no code writes into). `vision/` (generated, `iw3_fastfile.cpp:10925-10931`), `techsets/`, `techniques/`, and `scripts/`.
4. **The `.ff` path:** `PrepareFastfile` requires `options.fastfile` to exist and end in `.ff` (`iw3_fastfile.cpp:10603-10606`), but never opens it when an extraction is given. Section 6 gives the two-hunk change that removes this requirement.
5. **Stock by name:** not supported. A surface material name must resolve to `materials/<name>.json` under the extraction or a search path. The only exceptions are two IW3 stubs (`mc/lambert1`, `mc/gfx_missing_fx`) and built-in `$` image names. Replay stock technique sets are used only through the converter's own generated world materials, and only when `world.replay_stock_world` is true.
6. **No baked light:** no light grid means no GPU grid (`write_maps.cpp:442`), with no synthesized default. No lightmaps means the atlas path, unlit baked surfaces, and three 8192² RGBA8 atlases allocated (`iw3_render_assets.cpp:5904-5906`). The sun comes from `world.sun` and the `primary_lights` table, which is always written. In-game lighting results are NOT-VERIFIED.

Sample extractions in `E:\mw124-conversion-builds` are both **incomplete** (no root `complete`). The Discovery sample has no `images/` folder. The Frontier sample has no `materials/` folder. Neither can be built as-is, but both show the real key shapes (Section 2 uses them).

---

## 1. Folder layout

Root: the folder that `cacheDirectory` names (`iw3_fastfile.cpp:10627-10633`).

| Path | Required | Read by (x64-zt) |
|---|---|---|
| `complete` | REQUIRED for an external extraction. Its content is not read. | `iw3_fastfile.cpp:10617-10623` ("the extraction ... is not complete") |
| `extracted/` | REQUIRED. Read in place, not copied. Its parent is `exportRoot`. | `iw3_fastfile.cpp:10630-10632`; `exportRoot` is `worldPath` ×3 parents (`:10647`) |
| `skeleton/complete` | REQUIRED for an external extraction (throws otherwise). | `iw3_fastfile.cpp:10818-10819` |
| `skeleton/xmodel/<model>.json` | OPTIONAL, per entity model. A missing file warns "keeps one bone". | `ReadSourceSkeleton`, `iw3_fastfile.cpp:2854-2870` |
| `scripts/` | Not read by `PrepareFastfile`. | none found in `src/convert/iw3_iw8` |

Under `extracted/` (paths written as `extracted/...`):

| Path | Required | Read by |
|---|---|---|
| `maps/mp/<m>.d3dbsp.replay-world.json` | REQUIRED, exactly one `*.replay-world.json` under `extracted/` (search is recursive). `<m>` is the map name. | `FindSingleExport` (`iw3_fastfile.cpp:817-840`), read at `:10643`, `:10655` |
| `maps/mp/<m>.d3dbsp.replay-collision.json` | REQUIRED, exactly one | `:10644`, `:10656` |
| `maps/mp/<m>.d3dbsp.replay-comworld.json` | REQUIRED, exactly one | `:10645`, `:10657` |
| `maps/mp/<m>.d3dbsp.ents` | REQUIRED. Read from the world file's folder, named `<sourceAsset>.ents` (`:10703-10706`, throws "IW3 map entity export is missing"). | `:10703-10706` |
| `maps/mp/<m>.gsc` | OPTIONAL (the map script; read with the result ignored). | `:10711-10714` |
| `maps/mp/<m>.d3dbsp.gpulightgrid.bin` | OPTIONAL. If present, used as the native grid as-is. | `iw3_lightgrid.cpp:1195-1210` |
| `maps/mp/<m>.d3dbsp.lightgrid.json` (+ `.raw.bin`, `.colors.bin`, `.entries.bin`, `.rows.bin`, `.sh.bin`) | OPTIONAL. Without it there is no grid (`:1211-1218`) and no entity light volume (`:1702-1706`). | `iw3_lightgrid.cpp:1211, 1241, 1434, 1702` |
| `maps/mp/<m>.d3dbsp.lightmap_*.bin` | OPTIONAL. Referenced only by `world.lightmaps[].file`. | `iw3_render_assets.cpp:6232-6257` |
| `maps/clienttriggers.json` | OPTIONAL (no file: no client triggers). | `ReadClientTriggers`, `iw3_fastfile.cpp:9818-9822`, `:12364` |
| `sun/<m>.sun` | OPTIONAL (no file: Replay's stock sun flare values). | `ReadSunFlare`, `iw3_fastfile.cpp:10462-10500`, `:12920`; also `iw3_vision.cpp:339` |
| `xmodel/<name>.json` and `xmodel/<name>.replay.json` | REQUIRED for every model named by `world.models[].model`, and by every `script_model` / `trigger_radius` `model` value. Missing and external: throws (`:10719-10742`). | `ReadSourceModel` (`:2960-3000`), `MissingEntityModels` (`:664-675`) |
| `images/<name>.dds` (or `.iwi`) | REQUIRED for each non-built-in image a used material names, and for each reflection-probe cube (`images/<probe image>.dds`). | `LoadImage` (`src/convert/iw3_iw8/iw3_render_assets.cpp:1272-1318`); probe at `:5524-5537` |
| `materials/<name>.json` | REQUIRED for each surface material name, except `mc/lambert1` and `mc/gfx_missing_fx` (built-in stubs). | `ReadMaterial` (`iw3_render_assets.cpp:2539-2561`), `FindMaterial` (`:1954-1972`) |
| `physic/<name>.json` | REQUIRED only if a dynamic model or physics model names it. | `iw3_fastfile.cpp:3437` |
| `vision/<m>.vision` | Not required. The converter generates the output vision (`:10925-10931`); `iw3_vision.cpp:275-282` reads an authored one only when present. | |
| `techsets/`, `techniques/`, `replay_shaders/`, `shader_bin/` | Not read by the IW3 map route. No `techsets/` file read exists in `src/convert/iw3_iw8` (grep). Material technique names are regex-matched only (`iw3_render_assets.cpp:2577, 2629-2657`). | |
| `fx/`, `impactfx/`, `sound/`, `soundaliases/`, `lights/`, `mp/mapstable.csv`, `zone_source/` | Read only when a name in the map refers to them. Not exercised by the minimal example. UNVERIFIED for an empty set. | |

Two more points:

- **`<map>_iw8_ents.txt` is dead on this route.** `buildMap` looks for it in `dumpDirectory` and its parent (`convert.cpp:2204-2213`). On the `build-iw3` route `dumpDirectory` is `prepared.root`, a scratch folder that `PrepareFastfile` creates (`iw3_fastfile.cpp:455-470`). Nothing in `src/` writes the file, so the lookup always falls through to `loadEnts`.
- **The IW7 front end writes the same layout.** `src/convert/iw7_iw8/convert.cpp:30-150` writes `output\.iw7-extract\<zone>\` with root `complete` (`:140-141`), `skeleton/complete` (`:140`), `extracted/`, and then calls `iw3_iw8::convert_extraction` (`:149`).

---

## 2. Map JSON schemas as consumed

Notation: `float` and `uint` are JSON numbers. Vectors are JSON arrays of length 3 unless stated otherwise. Units are Replay/IW3 world units; the IW7 front end writes IW7 units, and no conversion is applied on this route (IW3 light units are handled by `MatchIw8Lighting` only with `--style iw8`, `iw3_fastfile.cpp:10668-10686`). All three files are read with `schema` = 1 (`:10659`) and must agree on `name` (`:10663-10666`).

### 2.1 `<m>.d3dbsp.replay-world.json` (top-level object)

| Key | Type | Required | Notes (file:line) |
|---|---|---|---|
| `schema` | int | REQUIRED, must be `1` | `:10659` |
| `name` | string `"maps/mp/<m>.d3dbsp"` | REQUIRED; must match the collision and comworld `name`. The source map name is this minus `.d3dbsp` (`sourceMap`). | `:10647-10666`, `:10648-10651` |
| `bounds` | `[[minx,miny,minz],[maxx,maxy,maxz]]` | REQUIRED | `:3728`, `:6689`, `ReadBrushModels` |
| `sun` | object: `angles` `[pitch,yaw,roll]` deg; `color` `[r,g,b]`; `intensity` float; `ambient` `[r,g,b]` (used only as fallback colour) | REQUIRED (`angles`, `color`, `intensity`) | `BuildLighting` `iw3_fastfile.cpp:8172-8224`; `SunDirection` `:8162-8170` |
| `sun_primary_light_index` | uint, `< primary_light_count` | REQUIRED | `:10694-10699`, `:8199` |
| `primary_light_count` | uint, `2..65535`, equals `primary_lights.size()` (in comworld) | REQUIRED | `:10693-10699` |
| `surfaces` | array of surface objects (2.2), `1..65535` | REQUIRED (empty throws) | `:2332-2360`, `:6587` |
| `dpvs` | `{planes:[], nodes:[uint...] (non-empty), cells:[...] (non-empty, <= 65535)}` | REQUIRED | `ReadVisibilityGroups` `:2515-2530` |
| `dpvs.cells[]` | `{bounds, portals:[], reflection_probes:[uint], surfaces:[uint], models:[uint], trees:[...]}` | `bounds`, `portals`, `reflection_probes`, `trees` REQUIRED; `surfaces`, `models` used by the writer | `:2551-2582`, `:6591-6640` |
| `dpvs.cells[].trees[]` | `{bounds, surfaces:[uint], models:[uint]}`; `childCount` makes it "native topology" | REQUIRED fields `bounds`, `surfaces`, `models` | `:2551-2565` |
| `reflection_probes` | array, `1..256`. Index 0 is the default probe. Entries `{image: string, origin: [3] float}` (`|origin| <= 100000`) | REQUIRED. A single entry still reads `image`, which must be non-empty and have `images/<image>.dds`. | `:5508-5537` |
| `models` | array (optional) of `{model: string, origin:[3], axis:[[3],[3],[3]], scale: float, reflection_probe: uint, mins:[3]?, maxs:[3]?, primary_light: uint8?, model_flags: uint?, ground_lighting: uint?, shadow_lights: [uint]?}` | OPTIONAL | `:3743-3825` |
| `brush_models` | array (optional) of `{bounds:[[3],[3]], start: uint, count: uint}` (`start`/`count` index `surfaces`) | OPTIONAL. Needed for `*N` brush models and triggers. | `:3685-3712` |
| `lightmaps` | array of `[primary, secondary]` pairs (optional) | OPTIONAL. If absent: no baked lighting (Section 5). | `:5737-5744`, `:6232-6252` |
| `light_regions` | array (optional), per light, of hull objects | OPTIONAL | `:11783-11790` |
| `iw7_specular` (bool), `bc7_images` (bool), `translucent_glass` (bool), `spatial_surfaces` (bool) | flags | OPTIONAL. Defaults false. | `iw3_render_assets.cpp:5261`, `:6398`; `iw3_fastfile.cpp:6302`, `:6327` |
| `replay_stock_world` (bool) | flag, default false. Selects Replay's stock world technique sets for every needed world material. | OPTIONAL | `iw3_render_assets.cpp:7767-7770` |
| `fog` (object), `grading_clut` (object), `iw7_tonemap` (object), `iw7_volumetrics` (object) | IW7 fields | OPTIONAL | `:10868`, `:10890`, `:10673`, `:10921` |
| `script_models`, `xanims`, `physics_models` | arrays | OPTIONAL | `:11193`, `:11217`, `:11318` |
| `glass_panes` | array | OPTIONAL | `:6737` |
| `sky` (string), `volumetrics`, `lightmap_encoding`, `reflection_probes` extra fields | | Not read by this route (`sky` is absent from every reader; `lightmap_encoding` has no reader in `src/convert/iw3_iw8`) | grep |

Sample shapes (from the Discovery sample, `zt-discovery-snow-20261029\extract\maps\mp\mp_discovery_night.d3dbsp.replay-world.json`): `"sun":{"ambient":[0.5,0.5,0.7],"angles":[-21.0,31.0,0.0],"color":[0.0,0.0,0.0],"intensity":1.3}`, `"sun_primary_light_index":1`, `"reflection_probes":[{"image":"*reflection_probe0","origin":[0.0,0.0,0.0]},...]`, `"lightmaps":[[{"bytes":4194304,"file":"maps/mp/...lightmap_0_0.bin","format":50,"height":2048,"levels":1,"width":2048},{"bytes":8388608,"file":"...lightmap_0_1.bin","format":21,"height":2048,"levels":1,"width":1024}],...]`.

Note that the sample's reflection probe image names begin with `*` (`*reflection_probe0`). `iw3_render_assets.cpp:5528-5537` replaces `*` with `_` when it builds the DDS path, so the DDS on disk is `images/_reflection_probe0.dds`.

**`lightmaps[]` details.** Each pair is `[primary, secondary]`. `primary.format` must be 50 and `primary.width`/`height` give `width*height` bytes (`iw3_render_assets.cpp:6236-6245`). `secondary.format` must be 21, `secondary.height` must be even, and the file holds `width*(height/2)*8` bytes (`:6233-6250`). The secondary's stored height is twice the texel rows. Both `file` paths are relative to `extracted/`. The atlas rectangle of lightmap `i` is assigned in order (`:5737-5794`), and `surface.lightmap` indexes that order (`iw3_fastfile.cpp:3960-3961`).

### 2.2 `world.surfaces[]` (one per draw surface)

Read by `RawSurfaceFrom` (`iw3_fastfile.cpp:2238-2285`) into `RawSurface` (`:2192-2206`), then `ReadWorldSurface` (`:2400-2450`).

| Key | Type | Required | Notes |
|---|---|---|---|
| `material` | string | REQUIRED | Must resolve to `materials/<name>.json` (Section 4). |
| `reflection_probe` | uint, `<= 65535` | REQUIRED | `:2243`, `:2412-2414`. Must be `< reflection_probes.size()` (`:6600-6604`). |
| `lightmap` | int | OPTIONAL, default `-1` | `-1` or `31` mean unlit (`:2242`, `:2410-2411`). Otherwise an index into `lightmaps` (`:3960-3961`). |
| `vertices` | array (non-empty) | REQUIRED | `:2248-2249`, `:2415` |
| `indices` | array of uint, count `% 3 == 0` | REQUIRED | `:2415` |
| `deformed` (bool), `shadow_lights` ([uint]) | | OPTIONAL | `:2244-2247` |
| `sky_object` (bool) | | Declared at `:2198` but never assigned by `RawSurfaceFrom`, so it is ignored as written. | `:2238-2285` |

Vertex object (`RawSurfaceFrom`, `:2255-2284`): `position [3]`, `normal [3]`, `tangent [3]`, `binormal_sign` (number), `uv [2]`, `lightmap_uv [2]` are all REQUIRED. `color [4]` (ints 0..255, `:2265-2270`), `layer_uvs` and `layer_transforms` are OPTIONAL. Values are read as doubles and converted to float; a non-finite value throws (`:2347-2352`, the `FloatVector` helper).

Normal and tangent encoding: **plain floats, not packed**. The IW7 front end unpacks its 10-bit packed normals to floats (`src/convert/iw7_iw8/export_world.cpp:40-54`), and writes `binormal_sign` as ±1 (`:56-70`). On this route, `normal` is normalized (`iw3_fastfile.cpp:2425`). A zero-length tangent is replaced by a cross product of the normal (`:2427-2430`). `|binormal_sign| < 0.5` throws (`:2431-2434`); otherwise the sign is set to ±1.

Triangle winding and the binormal convention were not verified against the renderer. The IW7 sample has tangent `(0,1,0)` with `binormal_sign -1` for a face with normal near `(0,0,1)`. The minimal example in Section 7 uses sign `+1` throughout, which is consistent with `binormal = cross(normal, tangent) * sign`. In-game check: NOT-VERIFIED.

### 2.3 `<m>.d3dbsp.replay-collision.json`

Read by `ReadCollision` (`src/convert/iw3_iw8/iw3_fastfile.cpp:7093-7150`, then the brush, triangle and dynamic readers at `:7166-7360`, `:3562-3640`).

| Key | Type | Required | Notes |
|---|---|---|---|
| `schema` | int `1` | REQUIRED | `:10660` |
| `name` | string | REQUIRED, same as world | `:10663-10666` |
| `submodels` | array of `{leaf: uint, mins:[3], maxs:[3]}`, non-empty | REQUIRED. Index 0 is the world; index `N` is brush model `*N`. | `:7103-7112`, `:7133-7136` |
| `leaf_brush_nodes` | array of `{brushes:[uint], count:int, children:[2 uint]}` | REQUIRED (non-empty array). Index 0 with `count 0` and `children [0,0]` is what the IW7 front end writes. | `:7104-7107`, `CollectLeafBrushes` `:6888-6920` |
| `brushes` | array of brush objects | REQUIRED (may be empty) | `:7095-7100` |
| `vertices`, `triangles` | arrays | REQUIRED (may be empty) | `:7345-7346`, `:7154` |
| `triangle_contents`, `triangle_contents_schema` | arrays/string | OPTIONAL. If present, schema must be `"material-partitions-v1"`. | `:7347-7356` |
| `triangle_surfaces` | array | OPTIONAL (IW7). Length must equal `triangles`. | `:7356-7360` |
| `contents_encoding` | string | OPTIONAL. `"replay"` means contents are Replay bits masked by `kReplayContents`; absent means IW3 bits converted by `ConvertContents`. | `:7099-7101`, `ConvertContents` `:6927-6945`, `kReplayContents` `src/games/iw8/common/replay_havok.h:22` (`0x27F7FFFB`) |
| `materials` | array of `{surfaceFlags: int32, ...}` | OPTIONAL. If present and non-empty, **every brush must also carry `axial_materials` (2×3) and `side_materials` (one per plane)** or the reader throws. Omit it unless you need per-face materials. | `:7110-7128`, `:7198-7205` |
| `submodel_count` | uint | REQUIRED only when `dynamic_brushes` exists | `:3640` |
| `dynamic_models`, `dynamic_brushes`, `dynamic_model_count`, `dynamic_brush_count` | | OPTIONAL. If absent, the counts must be 0. | `:3562-3575`, `:3628-3640` |

Brush object (`:7161-7183`):

- `contents` (uint, REQUIRED): IW3 bits by default. Solid is `1`; `ConvertContents` keeps `contents & 0x33683` and sets `1` when bit `0x10` is set (`:6927-6945`). With `"contents_encoding":"replay"`, Replay bits are kept with `kReplayContents` (`replay_havok.h:22`), and solid is still `1`.
- `mins`, `maxs` (REQUIRED) and `planes` (REQUIRED, array of `[a,b,c,d]`). Each plane has the inside on the side `n·p <= d`, so `n` points out. The IW7 writer says so explicitly: "the inside is below the distance" (`src/convert/iw7_iw8/export_collision.cpp:46-47`). The reader adds the six bounding planes from `mins`/`maxs` (`iw3_fastfile.cpp:7170-7183`), and `HullFromPlanes` requires 4 to 256 planes (`:7209-7215`).
- `surface_type` (uint) and `surface_flags` (uint, low 19 bits used) are OPTIONAL and used only by IW7-style extractions (`:7228-7234`).
- `ladder_planes` (array of `[a,b,c,d]`) is OPTIONAL. Its vertical planes become climbable faces (`:7250-7290`).
- `axial_materials`, `side_materials`: see `materials` above.

Value ranges that matter: the solid bit for a hull is `1`; a brush with planes that enclose no volume is dropped with a warning (`:7208-7230`, "left out ... whose planes enclose no volume"); submodel count `<= 65535` (`:3642-3644`).

Triggers: a brush model used as a trigger is a submodel with its own brushes. `buildMap` requires a native Havok shape for each trigger model (`convert.cpp:1722-1736`).

### 2.4 `<m>.d3dbsp.replay-comworld.json`

Read as `commonWorld` (`iw3_fastfile.cpp:10645-10657`). Samples: `zt-discovery...\mp_discovery_night.d3dbsp.replay-comworld.json` (5 lights) and `zslK\mp_iw7_frontier\...\mp_frontier.d3dbsp.replay-comworld.json` (144 lights).

| Key | Type | Required | Notes |
|---|---|---|---|
| `schema` | int `1` | REQUIRED | `:10659` |
| `name` | string `"maps/mp/<m>.d3dbsp"` | REQUIRED | `:10663-10666` |
| `in_use` | bool | Not read by the IW3 route (grep) | |
| `primary_lights` | array, `2..65535` entries (see below) | REQUIRED | `:10688-10699`, `convert.cpp:956-961` (`< 2` or `> 65535` throws) |
| `ies_profiles` | array of `{definition, profile, texels}` | OPTIONAL. Each `definition` is registered as a light definition. | `:10690-10692` |
| `scriptable_primary_light_count`, `first_scriptable_primary_light` | uint | OPTIONAL (IW7 script-driven lights) | `iw3_fastfile.cpp:8219-8222`, `convert.cpp:1135-1141` |

Each `primary_lights[i]` (read in `convert.cpp:982-1100`; the same object is copied into `lighting.json` by `BuildLighting`, `iw3_fastfile.cpp:8215-8216`):

| Key | Type | Required | Notes |
|---|---|---|---|
| `type` | uint `0..3` | REQUIRED | 0 = empty, 1 = directional (the sun), 2 = spot, 3 = omni. `convert.cpp:985-988`. Empty lights need no direction. |
| `exponent` | uint `0..255` | REQUIRED | `:988-990` |
| `can_use_shadow_map` | bool | REQUIRED | `:991` |
| `color` | `[3]` float, each `>= 0` | REQUIRED | `:994-1001` |
| `direction` | `[3]` float | REQUIRED. Length must be non-zero for type != 0. Normalized. | `:1003-1010` |
| `origin` | `[3]` float | REQUIRED | |
| `up` | `[3]` float | OPTIONAL. Used when `authored_up` is true. | `:1041-1060` |
| `radius` | float `>= 0` | REQUIRED | `:1066` |
| `cos_half_fov_outer`, `cos_half_fov_inner` | float | REQUIRED. Spot (type 2) needs `0 < outer < inner <= 1`. | `:1068`, `:1088-1094` |
| `rotation_limit`, `translation_limit` | float | REQUIRED | `:1069-1070`, `:1088` |
| `definition` | string `<= 255` chars | REQUIRED. `""` for type 0 and 1; `"light_point_linear"` for spot and omni in samples. | `:1074`, `:1087` |
| `entity_id` (uint), `bulb_radius` (default 1), `bulb_length [3]`, `volumetric`, `needs_dynamic_shadows`, `shadow_softness` (0.55), `shadow_bias`, `shadow_area` (0.0018), `distance_falloff` (0.2), `fade_offset [2]`, `uv_intensity`, `ir_intensity`, `intensity` | | OPTIONAL. Defaults from `src/games/iw8/zone/map_zone.h:1097-1130`. `intensity` (if present) marks the light as authored (`convert.cpp:1081-1085`). | `convert.cpp:1047-1085` |

The sun is the light at `sun_primary_light_index`, and `BuildLighting` takes its colour and intensity from it when it has `type == 1` (`iw3_fastfile.cpp:8189-8198`). A colour of `(1,1,1)` gives intensity 1 with `sun.intensity` not used. Light 0 is conventionally empty; the IW7 front end writes `type 0` there.

### 2.5 Ladders, clips and contents, in brief

- Player clips and item clips: IW3 contents bits are converted with `ConvertContents` (`iw3_fastfile.cpp:6927-6945`). Solid `0x10` (IW3 glass) becomes `1`. A non-solid player clip with `0x10000` is masked to `0x30202` or `0x30000`.
- Ladders: a brush's `ladder_planes` marks climbable faces (`:7250-7290`). The ladder model's rung spacing is rebuilt to 12 units (`:3168-3186`, `:7793`).
- Sky: a sky surface is recognized by its material (`techniqueSet` containing `sky`, `iw3_render_assets.cpp:2649-2651`) or by the `skyObject` material flag (`:2689`), not by the surface's `sky_object` key (Section 2.2).

---

## 3. Entities

### 3.1 How they enter the build

1. The IW3 route reads `extracted/maps/mp/<m>.d3dbsp.ents`. This is the IW3 text format: `{ "key" "value" ... }` blocks (`ParseEntityFields`, `iw3_fastfile.cpp:4000-4060`). Keys are compared after `lowercase()` in the converter (`maps.cpp`, `iw3ToIw8EntityString`).
2. `PrepareFastfile` applies these text transforms, in order: `WithIw3ModeAssets` (`:10148`), `WithCompletedModeSpawns` (`:10212`), `WithDeathrunSpawns` (`:10261`), `WithIw3CtfObjectives` (`:10305`), `WithoutLegacyGlassEntities` and `WithoutUnscriptedWindowStates` (`:12927-12928`).
3. It writes the result to `result.root/maps/mp/<map>.d3dbsp.ents` (`iw3_fastfile.cpp:12927-12931`, folder `mapDirectory` at `:11490`).
4. `buildMap` reads that file through `DumpSource::loadEnts` (`dump_source.cpp:67-90`), then `convert::iw3ToIw8EntityString` (`src/games/iw8/dump/maps.cpp:517-814`) turns it into Replay's MapEnts text. This is called from `convert.cpp:2286`. The output goes to `MapEnts.entityString` (`convert.cpp:1690`).
5. `convert::validateIw8EntityString` (`maps.cpp:500-515`) requires these output classes: `worldspawn`, `mp_tdm_spawn`, `mp_tdm_spawn_allies_start`, `mp_tdm_spawn_axis_start`, and `script_model`. Without them the build throws.

### 3.2 Key spelling

Input keys are IW3 names, matched case-insensitively. Only names in `iw3KeyToIw8KeyId` (`maps.cpp:25-70`) or the Havok opaque-string table are kept. The table has 28 names, including `ambient`, `angles`, `classname`, `destructible_type`, `diffusefraction`, `height`, `model`, `northyaw`, `origin`, `radius`, `script_exploder`, `script_linkname`, `script_linkto`, `script_noteworthy`, `spawnflags`, `suncolor`, `sundirection`, `sunlight`, `target`, `targetname`, `_color`, `script_accel`, `script_airspeed`, `script_bombmode_original`, `script_fxid`, `script_gameobjectname`, `script_label`, `physasset`. Any other key is dropped with a count-and-warn (`maps.cpp:683-685` and the `unnamedKeys` block after the loop).

The IW7 front end converts its token-hashed keys to these names with `with_key_names` (`src/convert/iw7_iw8/export_entities.cpp:21-60`). Its keys that have no token text stay as `_id_XXXX`. The Frontier sample still contains such keys (`"_id_258D" "1"` etc.), and they are dropped on this route.

`northyaw` on `worldspawn` is dropped (`maps.cpp:680-684`). The output always writes north 90 (`:715-718`).

### 3.3 Classnames

- Accepted: the list in `isReplayEntity` (`maps.cpp:179-215`): `worldspawn`, `info_player_start`, `mp_global_intermission`, the `mp_*_spawn` classes for CTF, DM, DOM, SD, TDM, and their team starts, `script_brushmodel`, `script_model`, `script_origin`, `script_struct`, `trigger_damage`, `trigger_hurt`, `trigger_multiple`, `trigger_radius`, `trigger_use`, `trigger_use_touch`. It also accepts the `shipped` and `modes` lists (`:217-260`) and `trigger_multiple_bcs_*`.
- Projected away without error: `isModeSpecificSpawn` (`mp_ctf_spawn_allies`, `mp_ctf_spawn_axis`, `mp_sab_*`, `maps.cpp:264-283`); `isSourceOnlyMarker` (`fx_origin`, `node_negotiation_*`, `node_pathnode`, `maps.cpp:286-300`); `misc_model` (because placements come from `world.models`, `maps.cpp:569-574`); and `trigger_damage` without `script_exploder`.
- Rejected: any other class throws "IW3 entity conversion rejected unsupported entity class(es)" (`maps.cpp:610-635`, `:645-660`).
- `info_notnull` becomes `script_origin` (`maps.cpp:570-573`).

### 3.4 Spawns and spawnList

- **No `spawnList` is generated on this route.** No code in `src/convert/iw3_iw8` or `src/games/iw8` fills `MapEnts.spawn_list` (grep). The IW7 front end converts its `spawnList` records into entity text (`src/convert/iw7_iw8/export_entities.cpp:1922-1935`). Replay's spawn selection reads the `mp_*_spawn` entities of `entityString`.
- **Mode spawns are completed from other spawns.** If fewer than 8 `mp_tdm_spawn` exist and at least 8 `mp_dm_spawn` do, the DM spawns are copied to TDM (`iw3_fastfile.cpp:10212-10236`). Domination spawns are copied from TDM when there are none (`:10237-10249`). Team starts are copied likewise (`:10244-10250`).
- **Deathrun** `mp_jumper_spawn` and `mp_activator_spawn` are rewritten (`:10261-10301`).
- **CTF** flags come from the map script's `addobj("allied_flag"...)` or from `allied_flag` / `axis_flag` entities. Spawns are made from TDM (`:10305-10452`).
- **Default set.** `defaultEntities` (`convert.cpp:689-697`) is used only in `buildMap` when no entity text exists. On this route `.ents` is always written, and an empty one throws (`iw3_fastfile.cpp:10704-10706`). So the default set is unreachable here.
- **Required for a playable match:** two or more `mp_tdm_spawn`, plus `mp_tdm_spawn_allies_start` and `mp_tdm_spawn_axis_start`, plus `script_model` (for validation). The `info_player_start` class is accepted but not required.

### 3.5 Triggers from brush models

- A `trigger_*` entity with `"model" "*N"` is a trigger on brush model `N` (`maps.cpp:627-640`). The index of the trigger in `triggers` is written as `"?k"` for key `0x028C` (`:686-692`). `brushModelIndex` reads `N` (`:341-352`).
- Each trigger needs `submodels[N]` in the collision file, with brushes assigned to it by the leaf tree (`iw3_fastfile.cpp:7133-7150`). Its bounds are used for a native Havok shape (`convert.cpp:1722-1736`).
- A trigger that moves gets `37683 "TriggerModelStaticDummyDefault"` (or `triggermodeldummydefault` for `white_phosphorus_damage`) unless it authors its own `physasset` (`maps.cpp:713-726`).
- A `script_brushmodel` (in the class list) uses `brush_models[N]` surfaces, not a trigger.

### 3.6 Entity models

Every `script_model` and `trigger_radius` `model` value (not starting with `*`) must have `xmodel/<name>.json` under the extraction (`iw3_fastfile.cpp:10719-10742`). `ReadSourceModel` then needs `type` `"rigid"` (or `"animated"` with a skeleton), `lods` (1..6, each with `distance`), `collLod` (optional, `-1` default), and `xmodel/<name>.replay.json` with `schema` 1, `name`, `contents` (optional), `bone_count` (optional), `root_bone` (optional), and `lods[]` with `distance` and `surfaces[]` (`:2960-3032`). Each render surface needs its own material file (Section 4).

---

## 4. Materials

### 4.1 Can a surface reference a stock Replay material by name, with no files?

**No, not on this route.**

- `ReadMaterial` (`src/convert/iw3_iw8/iw3_render_assets.cpp:2539-2561`): when `FindMaterial` returns nothing, the name must be `mc/lambert1` or `mc/gfx_missing_fx` (both get `$white` colour and `$identitynormalmap` normal). Any other name throws "missing IW3 material ... add the matching IW3 main and raw directories with --search-path".
- `FindMaterial` (`:1954-1972`) searches `materials/<name>.json` under the extraction root and each source path. The source paths are the fastfile's folder, `--search-path`, and the `IW3_GAME_ROOT`/`COD4_ROOT` roots (`src/convert/iw3_iw8/convert.cpp:2527-2549`). The Replay install's zones are not among them.
- `techniqueSet` is required (`iw3_render_assets.cpp:2577`). The name is only regex-matched (`:2629-2657`) for `shadowcaster`, `sky`, alpha-test (`_a`, `_t`), blend (`_b`), and similar tags. No technique-set file is opened (grep for `techsets/` in `src/convert/iw3_iw8` finds none).
- Built-in image names need no file (`iw3_render_assets.cpp:1277-1290`): `$white`, `$black`, `$water_specular`, `$identitynormalmap`. `$gray` is checked later (`:1304-1306`).

What Replay stock content can be used:

- **Replay's own stock world technique sets.** With `world.replay_stock_world = true` (`:7767-7770`), `buildStockWorldMaterials` builds `w/mw120r_<map>_stock_<id>` materials, one per source material name (`:7330-7352`). Their technique sets come from `iw8::stock::Library`, which reads `<replay>\zone` (`replay_stock_library.h:16-30`; `iw3_render_assets.cpp:7375`, `:7430`; `options.replayZones` at `convert.cpp:2511`). The author still supplies a source material per name, so this is not stock-by-name.
- **Name-only references** (`iw8::stock::RegisterNameReference`, `src/games/iw8/assets/replay_stock.cpp:1421-1424`) are used only for `vfxReferences` (`convert.cpp:1797-1801`). Those are FX-driven and resolve against the common zone at load time. They are not a surface material path. NOT-VERIFIED for world surfaces.

### 4.2 Minimum material for a surface you supply

`materials/<name>.json` as read by `ReadMaterial` (`iw3_render_assets.cpp:2539-2690`):

- `techniqueSet` (string, REQUIRED).
- `textures` (array, OPTIONAL): each `{name, semantic, image}`. Semantics mapped to slots: `colorMap` (colour), `normalMap`, `specularMap` (response), `detailMap` (by semantic or name), `replayColorMap`, `replayNormalGlossMap`, `replayRevealMap`, `maskMap` (`:2660-2682`).
- `gameFlags` (array; `"CASTS_SHADOW"` is read, `:2567-2576`), `surfaceTypeBits` (uint), `stateBits` (array of `{cullFace, ...}`, OPTIONAL, `:2637-2650`), `constants` (OPTIONAL), `replayLayers`, `iw7Flow`, `iw7Distortion`, `unlitMask`, `replayEmission`, `skyObject`, and similar IW7 fields (all OPTIONAL).

The minimal example (Section 7) uses `techniqueSet` plus two textures with built-in names. Whether it builds without `stateBits` (cull mode default), `response`, or `constants` is NOT-VERIFIED.

Image rules: a texture named `$...` is built in. Any other name needs `images/<name>.dds` (or `.iwi`) under the extraction or a source path (`iw3_render_assets.cpp:1296-1311`). Names are checked by `validImageName`.

---

## 5. Lighting with no baked data

What exists without any optional lighting input:

- `lighting.json` is always written from `world.sun` and `commonWorld.primary_lights` (`iw3_fastfile.cpp:12895-12896`, `BuildLighting` `:8172-8224`). So the sun and primary lights always exist.
- `sun/<m>.sun` is optional. Without it, Replay's stock sun flare values apply: no sprite, no flare, stock blind and glare (`iw3_fastfile.cpp:10462-10500`).
- `vision/<m>.vision` is generated (`:10925-10931`).

What depends on baked input:

- **Lightmaps (`world.lightmaps`).** Without them, `plan.lightmaps` is empty and `nativeWorld` is false, so the atlas path allocates three 8192×8192 RGBA8 atlases (`iw3_render_assets.cpp:5902-5906`, about 768 MiB). Each surface's `lightmap` is -1, so `baked` is false (`iw3_fastfile.cpp:3962-3963`). Surfaces with stock world materials that need a lightmap throw (`:3985-3989`), but this only happens for stock materials on lit surfaces; with no lightmaps the atlas path is used.
- **Light grid.** Without `gpulightgrid.bin` or `lightgrid.json`, `PrepareNativeLightGrid` warns "source map has no exported light grid" and writes nothing (`iw3_lightgrid.cpp:1195-1218`). `replaylightgrid::Load` then returns an empty grid, and `gw[0x7C0] = lightGrid ? 1 : 0` sets the GPU light grid flag to 0 (`src/games/iw8/assets/write_maps.cpp:322`, `:442`; `src/games/iw8/common/replay_lightgrid.cpp:30-44`). No default grid is synthesized. Entities get no light volume (`iw3_lightgrid.cpp:1699-1706`).
- **Indirect light.** `write_maps.cpp:440-446` notes that a zero secondary scale disables indirect light. The converter writes 1.0 (`:446`).

**Minimum for a lit, playable map.** A sun in `world.sun` with a matching `primary_lights[sun_primary_light_index]` of `type 1`, at least two primary lights, and surfaces with materials. Lightmaps are optional, but without them the surfaces are not baked. Whether Replay's world shader then produces a visible, correctly lit result is NOT-VERIFIED in game (doc 08 §7 lists no x64-zt map as loaded in game yet).

---

## 6. Build command and the `.ff` requirement

### 6.1 Entry points

- C++: `zonetool::convert::iw3_iw8::convert_extraction(zone_path, extraction, conversion)` (`convert.cpp:2711-2716`). It calls `convert_map(zone_path, conversion, extraction)`.
- `convert_map` sets `args.positional = {zone_path, map}` (`:2596-2604`), `args.extraction = extraction` (`:2651`), `options.fastfile = zone_path` (`:2507`), and `options.extraction = args.extraction` (`:2510`). It sets `replayExecutable` from `--replay`, or from the target game folder when unset (`:2615-2618`).
- `buildIw3` (`:2461`) requires a non-empty `replayExecutable` (`:2467-2469`). It calls `PrepareFastfile` (`:2556-2560`), then `buildMap` on `prepared.root` (`:2566-2575`).

`conversion.options` keys read on this route (from `convert_map`):

- `map`: output map name (default is the stem of `zone_path`, `:2600-2603`).
- `replay`: the Replay executable (required; `buildIw3` throws without it). Its folder's `zone` is the stock source (`:2511`).
- `metadata`, `style` (`iw3` default; `iw8` enables MatchIw8Lighting), `havok` (default `native`), `detail_collision`, `bc7_images`, `writer`, `dump`, `navigation`, `acts`, `stock_cache`, `render_cache`, `search_path`, `iw3_mode_assets` (`:2615-2700`).
- `unlinker`, `gsc_tool`: not needed for an external extraction (`:2648-2650`, no Unlinker call because `cached` is true, `iw3_fastfile.cpp:10617-10623`). `--acts` absent gives a warning (`convert.cpp:1558`).

`conversion.output_folder` is the output directory. The CLI sets it to `output` (`src/cli/zone_commands.cpp:101`).

### 6.2 Command line

There is no CLI option for an extraction folder. The two CLI routes both need a real zone file in a game install:

- `zonetool.exe -game iw3 -path "<CoD4>" dumpzone <map .ff> --target iw8 ...` needs the IW3 `.ff` and the Unlinker (`docs/IW8_CONVERSION.md` "Commands"; `src/cli/zone_commands.cpp:13-27` uses `game_files::find_zone`).
- `IW7_REUSE_EXTRACTION=1 zonetool.exe -game iw7 -path "<IW7>" dumpzone <zone> --target iw8 --map <m> --replay <exe> ...` reuses `output\.iw7-extract\<zone>\` when it has `complete` (`src/convert/iw7_iw8/convert.cpp:36-41`). It still resolves `<zone>` in the IW7 install first (`require_zone`), and the source zone path is still passed to `PrepareFastfile`.

So a native editor needs the C++ entry (`convert_extraction`) or the code change below.

### 6.3 Where the `.ff` requirement bites

`PrepareFastfile`:

```
iw3_fastfile.cpp:10603  if (!std::filesystem::is_regular_file(options.fastfile, error) ||
iw3_fastfile.cpp:10604      options.fastfile.extension() != ".ff")
iw3_fastfile.cpp:10606      throw std::runtime_error("IW3 input must be a readable .ff file");
```

With an extraction, the file's contents are never read. Only the existence and extension matter. Other uses of `options.fastfile` on the external path are either gated by `!cached` or tolerant of a missing path:

- `:10638` (log only, inside the Unlinker branch).
- `:10756` (`RunUnlinker`, inside `if (!cached ...)`).
- `:10825-10826` (skeleton Unlinker, inside the `skeleton/complete` block, which the external path throws out of first, see below).
- `:10845` `sound::ExtractIwdMembers(options.fastfile.parent_path(), ...)`. It returns 0 when the directory is missing (`src/convert/iw3_iw8/iw3_sound.cpp:2116-2120`).
- `convert.cpp:2534` and `iw3_fastfile.cpp:639`: add the parent folder to search paths; harmless when empty.

A second blocker, for external extractions only: `skeleton/complete` is required (`iw3_fastfile.cpp:10818-10819`). Without it, the code throws before anything else. Per-model skeleton files are optional (`:2854-2870`).

### 6.4 Smallest code change (not applied)

Two hunks, both in `src/convert/iw3_iw8/iw3_fastfile.cpp`:

1. **Lines 10603-10604.** Skip the `.ff` check when an extraction is given:
   `if (options.extraction.empty() && (!std::filesystem::is_regular_file(options.fastfile, error) || options.fastfile.extension() != ".ff"))`
2. **Lines 10818-10820.** Stop requiring `skeleton/complete` for an external extraction, and do not run the skeleton Unlinker for it:
   - delete or relax the throw at `:10818-10819` for `external`;
   - change `:10820` from `if (!std::filesystem::is_regular_file(skeletonRoot / "complete", error))` to `if (!external && !std::filesystem::is_regular_file(skeletonRoot / "complete", error))`.

With both, `PrepareFastfile` on an external extraction reads only `complete`, `extracted/`, and the files listed in Section 1. Nothing is written into the extraction (outputs go to `result.root`, `:10612-10614` and `:11490`). The entity-model requirement (`:10719-10742`) and the material and image requirements (Section 4) still apply, and they are content checks, not code gates.

Callers still have to pass a `zone_path` string. It can be any name, as long as it is non-empty and used only for `map`. (With the change, a placeholder file is not needed, but `convert_map` still derives the map name from its stem.)

---

## 7. Minimal complete example: 512×512 room

Layout:

```
room_extraction\
  complete                                     (content "1")
  skeleton\complete                            (content "1")
  extracted\maps\mp\room.d3dbsp.replay-world.json
  extracted\maps\mp\room.d3dbsp.replay-collision.json
  extracted\maps\mp\room.d3dbsp.replay-comworld.json
  extracted\maps\mp\room.d3dbsp.ents
  extracted\images\room_probe.dds              (binary: 64x64 cube, B8G8R8A8, 7 mips)
  extracted\materials\room\grey.json
```

The DDS is required even though no image is named elsewhere. A single probe entry still reads its image (`iw3_render_assets.cpp:5524-5537`). Use a neutral grey cube, as `export_default_probe` writes `images/iw7_default_reflection_probe.dds` (`src/convert/iw7_iw8/export_materials.cpp:1975-1995`).

The room: floor top at z=0, spanning x and y in [-256, 256]. Walls are 16 thick and 256 tall, and their inner faces sit at ±256. Sun from `pitch -50, yaw 45`, which gives a direction toward the sun of `(0.4547, 0.4547, 0.7660)`.

**`extracted/maps/mp/room.d3dbsp.replay-world.json`** (one line):

```json
{"schema":1,"name":"maps/mp/room.d3dbsp","bounds":[[-272,-272,-16],[272,272,256]],"sun":{"ambient":[0.5,0.5,0.5],"angles":[-50,45,0],"color":[1,1,1],"intensity":1},"sun_primary_light_index":1,"primary_light_count":2,"reflection_probes":[{"image":"room_probe","origin":[0,0,0]}],"dpvs":{"planes":[],"nodes":[1],"cells":[{"bounds":[[-272,-272,-16],[272,272,256]],"portals":[],"reflection_probes":[0],"surfaces":[0,1,2,3,4],"models":[],"trees":[{"bounds":[[-272,-272,-16],[272,272,256]],"surfaces":[0,1,2,3,4],"models":[]}]}]},"surfaces":[{"material":"room/grey","reflection_probe":0,"indices":[0,1,2,0,2,3],"vertices":[{"position":[-256,-256,0],"normal":[0,0,1],"tangent":[1,0,0],"binormal_sign":1,"uv":[0,0],"lightmap_uv":[0,0]},{"position":[256,-256,0],"normal":[0,0,1],"tangent":[1,0,0],"binormal_sign":1,"uv":[1,0],"lightmap_uv":[0,0]},{"position":[256,256,0],"normal":[0,0,1],"tangent":[1,0,0],"binormal_sign":1,"uv":[1,1],"lightmap_uv":[0,0]},{"position":[-256,256,0],"normal":[0,0,1],"tangent":[1,0,0],"binormal_sign":1,"uv":[0,1],"lightmap_uv":[0,0]}]},{"material":"room/grey","reflection_probe":0,"indices":[0,1,2,0,2,3],"vertices":[{"position":[-256,256,0],"normal":[0,-1,0],"tangent":[1,0,0],"binormal_sign":1,"uv":[0,0],"lightmap_uv":[0,0]},{"position":[256,256,0],"normal":[0,-1,0],"tangent":[1,0,0],"binormal_sign":1,"uv":[1,0],"lightmap_uv":[0,0]},{"position":[256,256,256],"normal":[0,-1,0],"tangent":[1,0,0],"binormal_sign":1,"uv":[1,1],"lightmap_uv":[0,1]},{"position":[-256,256,256],"normal":[0,-1,0],"tangent":[1,0,0],"binormal_sign":1,"uv":[0,1],"lightmap_uv":[0,1]}]},{"material":"room/grey","reflection_probe":0,"indices":[0,1,2,0,2,3],"vertices":[{"position":[-256,-256,0],"normal":[0,1,0],"tangent":[-1,0,0],"binormal_sign":1,"uv":[0,0],"lightmap_uv":[0,0]},{"position":[256,-256,0],"normal":[0,1,0],"tangent":[-1,0,0],"binormal_sign":1,"uv":[1,0],"lightmap_uv":[0,0]},{"position":[256,-256,256],"normal":[0,1,0],"tangent":[-1,0,0],"binormal_sign":1,"uv":[1,1],"lightmap_uv":[0,1]},{"position":[-256,-256,256],"normal":[0,1,0],"tangent":[-1,0,0],"binormal_sign":1,"uv":[0,1],"lightmap_uv":[0,1]}]},{"material":"room/grey","reflection_probe":0,"indices":[0,1,2,0,2,3],"vertices":[{"position":[256,-256,0],"normal":[-1,0,0],"tangent":[0,-1,0],"binormal_sign":1,"uv":[0,0],"lightmap_uv":[0,0]},{"position":[256,256,0],"normal":[-1,0,0],"tangent":[0,-1,0],"binormal_sign":1,"uv":[1,0],"lightmap_uv":[0,0]},{"position":[256,256,256],"normal":[-1,0,0],"tangent":[0,-1,0],"binormal_sign":1,"uv":[1,1],"lightmap_uv":[0,1]},{"position":[256,-256,256],"normal":[-1,0,0],"tangent":[0,-1,0],"binormal_sign":1,"uv":[0,1],"lightmap_uv":[0,1]}]},{"material":"room/grey","reflection_probe":0,"indices":[0,1,2,0,2,3],"vertices":[{"position":[-256,-256,0],"normal":[1,0,0],"tangent":[0,1,0],"binormal_sign":1,"uv":[0,0],"lightmap_uv":[0,0]},{"position":[-256,256,0],"normal":[1,0,0],"tangent":[0,1,0],"binormal_sign":1,"uv":[1,0],"lightmap_uv":[0,0]},{"position":[-256,256,256],"normal":[1,0,0],"tangent":[0,1,0],"binormal_sign":1,"uv":[1,1],"lightmap_uv":[0,1]},{"position":[-256,-256,256],"normal":[1,0,0],"tangent":[0,1,0],"binormal_sign":1,"uv":[0,1],"lightmap_uv":[0,1]}]}]}
```

Surface order: floor, north, south, east, west. The walls' `lightmap_uv` values are placeholders only: no `lightmap` key is given, so the default `-1` applies. Winding is not verified.

**`extracted/maps/mp/room.d3dbsp.replay-collision.json`**: the floor and the four walls as brushes 0 to 4. Each brush has six planes that describe its box. No `materials` key, so no axial or side materials are needed.

```json
{"schema":1,"name":"maps/mp/room.d3dbsp","submodel_count":1,"submodels":[{"leaf":0,"mins":[-272,-272,-16],"maxs":[272,272,256]}],"leaf_brush_nodes":[{"brushes":[],"children":[0,0],"count":0}],"brushes":[{"contents":1,"mins":[-256,-256,-16],"maxs":[256,256,0],"planes":[[-1,0,0,256],[1,0,0,256],[0,-1,0,256],[0,1,0,256],[0,0,-1,16],[0,0,1,0]]},{"contents":1,"mins":[-272,256,0],"maxs":[272,272,256],"planes":[[-1,0,0,272],[1,0,0,272],[0,-1,0,-256],[0,1,0,272],[0,0,-1,0],[0,0,1,256]]},{"contents":1,"mins":[-272,-272,0],"maxs":[272,-256,256],"planes":[[-1,0,0,272],[1,0,0,272],[0,-1,0,272],[0,1,0,-256],[0,0,-1,0],[0,0,1,256]]},{"contents":1,"mins":[256,-256,0],"maxs":[272,256,256],"planes":[[-1,0,0,-256],[1,0,0,272],[0,-1,0,256],[0,1,0,256],[0,0,-1,0],[0,0,1,256]]},{"contents":1,"mins":[-272,-256,0],"maxs":[-256,256,256],"planes":[[-1,0,0,272],[1,0,0,-256],[0,-1,0,256],[0,1,0,256],[0,0,-1,0],[0,0,1,256]]}],"vertices":[],"triangles":[],"dynamic_model_count":0,"dynamic_brush_count":0,"dynamic_models":[],"dynamic_brushes":[]}
```

Check the planes: a plane `[a,b,c,d]` has `d = n·p` on its face, with the inside `n·p <= d`. For the north wall `mins=[-272,256,0] maxs=[272,272,256]`, the planes are `x>=-272` as `[-1,0,0,272]`, `x<=272` as `[1,0,0,272]`, `y>=256` as `[0,-1,0,-256]`, `y<=272` as `[0,1,0,272]`, `z>=0` as `[0,0,-1,0]`, and `z<=256` as `[0,0,1,256]`. The reader adds the same six bounding planes from `mins`/`maxs` (`iw3_fastfile.cpp:7170-7183`).

**`extracted/maps/mp/room.d3dbsp.replay-comworld.json`**:

```json
{"schema":1,"name":"maps/mp/room.d3dbsp","in_use":true,"primary_lights":[{"type":0,"exponent":0,"can_use_shadow_map":false,"color":[0,0,0],"direction":[0,0,0],"origin":[0,0,0],"radius":0,"cos_half_fov_outer":0,"cos_half_fov_inner":0,"rotation_limit":0,"translation_limit":0,"definition":""},{"type":1,"exponent":0,"can_use_shadow_map":false,"color":[1,1,1],"direction":[0.4547,0.4547,0.766],"origin":[0,0,0],"radius":0,"cos_half_fov_outer":0,"cos_half_fov_inner":0,"rotation_limit":0,"translation_limit":0,"definition":""}]}
```

**`extracted/maps/mp/room.d3dbsp.ents`** (IW3 text, names as keys):

```
{
"classname" "worldspawn"
"sundirection" "-50 45 0"
}
{
"classname" "mp_tdm_spawn"
"origin" "-128 0 16"
"angles" "0 0 0"
}
{
"classname" "mp_tdm_spawn"
"origin" "128 0 16"
"angles" "0 180 0"
}
{
"classname" "mp_tdm_spawn_allies_start"
"origin" "-160 0 16"
"angles" "0 0 0"
}
{
"classname" "mp_tdm_spawn_axis_start"
"origin" "160 0 16"
"angles" "0 180 0"
}
{
"classname" "script_model"
"origin" "0 0 16"
"angles" "0 0 0"
}
```

`script_model` has no `model`, so it names no xmodel and needs no model file. The converter's own default set uses the same entity (`convert.cpp:689-697`). It is included only because `validateIw8EntityString` requires one.

**`extracted/materials/room/grey.json`**:

```json
{"techniqueSet":"room_grey","textures":[{"name":"colorMap","semantic":"colorMap","image":"$white"},{"name":"normalMap","semantic":"normalMap","image":"$identitynormalmap"}]}
```

**`skeleton/complete`** and **`complete`**: any content, e.g. `1`.

Not in the example: `sun/room.sun` (optional), `maps/clienttriggers.json` (optional), `maps/mp/room.gsc` (optional), lightmaps, light grid, `xmodel/`, `physic/`.

The example was checked against the reader code above, not built. It is a read check, not a tested build.

Expected build call: `convert_extraction(<any existing .ff path>, <room_extraction>, conversion)` with `conversion.options` = `{map: room, replay: <game_dx12_ship_replay.exe>, style: iw3}`. Without the Section 6.4 change the `.ff` path must exist. The `replay` path must point to a Replay install with `zone\` for the stock libraries and collision tools.

---

## 8. UNKNOWN and NOT-VERIFIED

1. Winding order of triangles and the binormal sign convention relative to Replay's world shaders (Section 2.2, Section 7).
2. Whether the room example builds, and what it looks like. NOT-VERIFIED.
3. Whether a material with only `techniqueSet` and two textures builds with default cull, blend and response values (Section 4.2).
4. The renderer's behaviour for lightless surfaces on the atlas path (Section 5).
5. Whether a stock Replay material can be named on a surface without a source material file. The code says no (Section 4.1).
6. Whether `sky_object` on surfaces is ever needed (Section 2.2).
7. The exact sun-shadow requirements (`replaysunshadow` cache, `iw3_fastfile.cpp` and `iw3_render_assets.cpp`); not traced in this pass.
8. The IW7 front end's handling of `primary_lights` entries that the native path would not write (for example `ies_profiles` and `scriptable_primary_light_count`); the native path can omit them.
9. Whether `buildMap`'s `_iw8_ents.txt` branch can ever be used (Section 1): no producer and no pre-placeable folder were found.
10. The samples under `E:\mw124-conversion-builds` are incomplete extractions (no root `complete`, no `materials/` in the Frontier sample, no `images/` in the Discovery sample). Neither is a complete reference set.
