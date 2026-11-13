# 100. Stock ground clutter (Replay 1.20): what drives the GPU scatter, where it is used, and why Speedball's grass is not it

**Why.** On mp_m_speedball the in-game ground inside the arena is a soft carpet of grass blades, where the editor
shows flat ground with dark speckled patches (owner screenshot `out/compare/_ingame_view.png`, editor
`out/agent/cam_pd2/game_a.png`). The question was whether Replay scatters that grass at run time on the GPU (the
`cs_gpu_generation` "clutter" compute shaders), what data drives it, and how to reproduce it on the CPU.

**Verdict.**

1. Replay's GPU clutter is real and data-driven per map, but **only super-terrain maps use it**: Verdansk
   (`mp_donetsk`: 678 clutter collections, 43 models, 24 terrain surfaces) and, INFERRED, Quarry. It is driven by the
   map's `stterrain` asset (per-layer parameters, a baked per-tile sample bitmask, shared sample points, the terrain
   height map) and by `GfxWorld.smodels.clutterCollections` (which model each layer draws).
2. **mp_m_speedball has none**: `clutterCollectionCount` 0, `clutterCollections` null, and an empty `stterrain`
   (surfaceCount 0). The same holds for all 15 MP zones checked. **Speedball's grass is 1,164 placed static models**
   (953 of them `tm/foliage_grass_long_patch_02_a_02_reactive`), which the editor already reads and draws. Their
   positions match the in-game grass areas (image in section 6).
3. So the gap on Speedball is **how the grass props are shaded**, not missing geometry. Section 3.3 gives the stock
   foliage shader's rules (alpha test, two-sided normals, vertex colour, transmission, distance dither). Section 5
   lists what the viewport needs.
4. The new core module `src/core/stock_clutter.{h,cpp}` reads the clutter data and ports the compute shader exactly.
   On Speedball it returns 0 instances, which is correct. It is the path for Verdansk once the `stterrain` asset can
   be exported (section 4).

**Labels.** VERIFIED = read in the disassembly, the techset arguments or the exported data (cited). INFERRED = from
names, struct definitions, arithmetic or the data's shape, not from a shader. The DXBC has no RDEF block, so register
names are ours.

## How it was obtained

- Compute shaders: the `code_post_gfx` computeshader export already on disk
  (`out/agent/shader_research/cs/mw19replay/code_post_gfx/assets/computeshader/cs_gpu_generation.hlsl/`), disassembled
  with `out/agent/shader_research/py/dxbc_dis.py`. NUL-stripped copies are kept in `out/agent/clt/cs/*.txt`.
  **G** = `clutter_gen_pass.219.txt` (253 lines). The two reactive-motion variants (220, 221) and the
  `cs_reactivemotion_copy_frame_data` passes were diffed against G. Their placement code is the same; they only add the
  reactive-motion bookkeeping.
- Struct layouts: x64-zt `src/games/iw8/structs.hpp` (`GfxClutterCollection` :12076, `GfxStaticModelCollection`
  :12097, `GfxSModelInstanceData` :12142, `GfxClutterCollectionGPUData` :12157, `GfxWorldStaticModels` :12263-12301,
  `StTerrainClutterLayer` :16611, `StClutterSampleBitmask` :16627, `StDiskTerrainSurface` :16636-16673,
  `StClutterSamplePoints` :16681, `StTerrain` :16693). The shader's offsets were checked against them (section 2).
- World data: the editor's world caches of `mp_m_speedball` and `mp_donetsk` (`%LOCALAPPDATA%/IW8 Radiant Suite/IW8
  Radiant Suite/world_cache/<map>/assets/gfx_map/...`), scanned with `out/agent/clt/py/*.py` (mmap and partial decode,
  never a full load) and read by the new core reader through `out/agent/clt/verify/verify_clutter.exe`.
- stterrain: ACTS `-a stterrain` on 16 map zones (section 1.3). Kept: `out/agent/clt/exp/stterrain_mp_m_speedball.json`
  and the Verdansk failure record `exp/stterrain_mp_donetsk_failure.jsonl`.
- Grass shading: ACTS `-a material,techset` on `techsets_mp_m_speedball.ff`, then `-a pixelshader,vertexshader` for
  the four shaders of `tmco/lit_3_lit_alt_ta1_848810_1012000201000051_0_1_1_0_0_138f0035b_0_0_1_0_0`. Disassembly is
  in `out/agent/clt/asm/`: **P** = prepass PS `5d2f1d69...2.txt`, **L** = lit PS `88d201db...3.txt`, **V** = lit VS
  `bd696749...1.txt`. The material and techset JSON are kept in `out/agent/clt/exp/`. The full exports were deleted.

## 1. What drives clutter (question 1)

### 1.1 The chain (VERIFIED)

| Asset / field | Content | Read by |
|---|---|---|
| `gfx_map` `smodels.clutterCollectionCount`, `clutterCollections` (12 bytes each) | `u32 maxInstanceCount, u16 smodelCollectionIndex, u16 terrainSurfaceIndex, u16 clutterLayerIndex, u16 pad` | G:36-39 reads `clutterLayerIndex` at `collection * 12 + 8` (& 0xFFFF) from raw t1 |
| `gfx_map` `smodels.collections[smodelCollectionIndex]` | a `GfxStaticModelCollection` with flag 32 `STATIC_MODEL_COLLECTION_FLAG_CLUTTER`; on Verdansk `firstInstance` 0, `instanceCount` 0 (filled at run time) | draw side |
| `gfx_map` `smodels.models[collection.smodelIndex].model` | **the XModel drawn**, e.g. `#tm/ee_foliage_grass_lm_short_01` | draw side |
| `gfx_map` `smodels.clutterInstancePool` `{start, size}` | the slice of `smodelInstanceData` the GPU writes (Verdansk: start 1,096,414, size 150,000) | G:47-48, 63-64 (`CB0[1].y` = pool start) |
| `stterrain` `surfaces[t].clutterLayers[l]` (`StTerrainClutterLayer`, 84 bytes) | placement parameters, section 2.1 | G structured t2, stride 84 |
| `stterrain` `surfaces[t].clutterSampleBitmask.bitmask` | u32 words, 64 per tile per layer: **the baked density** | G raw t4 |
| `stterrain` `clutterSamplePoints.samplePoints` | float2 per sample, shared by all surfaces | G raw t3 |
| `stterrain` `surfaces[t].heightMap` and `objToWld` | the height image (t0) and the surface placement (CB1[2] rows 0, 1) | G:100-102, 158-165 |
| `stterrain` `surfaces[t].combinedAlbedoMap` (INFERRED: t5) | the ground colour copied to `clutterTintData` per instance | G:243-249 |

The output buffers are the same ones static models use. U0 is `smodelInstanceData` (24 bytes: int3 translation x 4096,
two u32 of packed 16-bit quaternion, float scale; G:231-242). U1 is `clutterCollectionGPUData` `{firstInstance,
instanceCount}` (G:47-48, 62). U2 is `clutterTintData` (vec3 per instance; G:248). So a clutter instance is drawn like
any static model, with its model's own LODs and materials.

### 1.2 Per map (VERIFIED, `out/agent/clt/verify.txt`)

| Map | `clutterCollections` | Models | Terrain surfaces | Sum of `maxInstanceCount` |
|---|---|---|---|---|
| mp_m_speedball | null (count 0) | 0 | 0 | 0 |
| mp_donetsk (Verdansk) | 678 | 43 | 24 (up to 43 layers on one surface) | 816,629 (pool 150,000) |

Verdansk's top models by budget: `tm/ee_foliage_grass_lm_short_01` 320,997, `tm/ee_foliage_grass_lm_short_03`
169,880, `tm/ee_foliage_grass_lm_med_01` 55,037, `..._short_01_dead` 45,025, `tm/ee_rock_riverbed_cluster_02_sm_01_wet`
34,614, `..._med_01_dead` 27,071, `tm/ee_foliage_tree_birch_sapling_03` 17,707, and so on: grass cards, small rocks,
reeds, flowers and saplings. All 678 rows have `instanceCount` 0 and flag 32 in their static model collection.

### 1.3 stterrain per zone (VERIFIED, ACTS dev-3.3.0 `-a stterrain`)

`surfaceCount` 0 and `sampleCount` 0 on mp_m_speedball, mp_br_tut, mp_m_speed, mp_hideout, mp_aniyah_tac, mp_backlot2,
mp_village2, mp_rust, mp_emporium, mp_bm_tut, mp_shipment, mp_m_king, mp_m_fork and mp_t_gun_course (each export is
an 850-byte asset with every pointer null). mp_donetsk and mp_br_quarry have a real terrain, but ACTS reports the asset
**unavailable**: 64 `pointer_extent_required` issues, all on `surfaces/0/grid/levels/3/nodes/*/mesh/vd0/data` and
`.../layerMask/data` (the node vertex blobs have no count field). So no JSON is written.

### 1.4 Not in materials (VERIFIED)

The MP world materials have no clutter field. `tw/ee_periph_city_dark_grass_01` (MTL_TYPE_WORLD) exports name,
flags, technique set, 4 textures, 4 constants and one constant buffer. Nothing in its texture or constant tables
names clutter or density. The only per-layer grass parameters in the engine are super-terrain ones
(`HLSL_TerrainLayerDataCode.grassHeight .. grassBend`, structs.hpp `StTerrainMaterialLayer.layerData`), so they also
exist only on super-terrain maps. **Clutter is not derived from material layers at run time.** Density is the baked
bitmask. How the tools bake it, perhaps from layer weights, is UNKNOWN.

## 2. How the compute pass places instances (question 2; VERIFIED, G)

### 2.1 The layer record (84 bytes; G:40, 78-82 against structs.hpp :16611)

| Offset | Field | Shader use |
|---|---|---|
| 0 | `sampleOffset` | first sample point (G:84) |
| 4 | `bitmaskOffset` | first bitmask word (G:52, 55) |
| 8, 12 | `tileCount` x, y | tile index = `ty * tileCount.x + tx` (G:51) |
| 16, 20 | `tileSizeUV` | layer UV of a tile; `asuint(tileSizeUV.x)` also seeds the hash (G:95, 109) |
| 24, 28 | `layerSize` | local extent in inches (G:98) |
| 32-40 | `angleMin` xyz | G:154-156 |
| 44-52 | `angleMax` xyz | G:154-156 (`r4.w` = +44, `r5.yz` = +48, +52) |
| 56, 60 | `zOffsetMinMax` | G:150-151 |
| 64, 68 | `scaleMinMax` | G:152-153 |
| 72 | `maxInstanceCount` | CPU budget (not read by G) |
| 76 | `tileInstanceCountOffset` | CPU side (not read by G; read by variant 220) |
| 80 | `useSurfaceNormal` | G:230 |

### 2.2 Dispatch and selection

- Groups of 64 threads. Group (x, y) is one tile, plus the CPU's tile offset `CB0[1].zw` (G:50). Group z is one of 32
  blocks of 64 samples, so **sample s = z * 64 + thread, 0 to 2047 per tile** (G:68).
- Bitmask: two u32 per (tile, z) at word `bitmaskOffset + tile * 64 + z * 2` (G:52-56). The whole group exits when both
  words are 0 (G:57-59). A thread places an instance when bit `s % 32` of word `s / 32` is set (G:69-76).
- Output slot: `poolStart + (CB1[2][1].x >> 4) + atomic prefix` (G:60-67, 88-94). The order on the GPU depends on the
  atomics, so the CPU port enumerates in (tile, sample) order instead.
- Which tiles are dispatched (distance, frustum) is decided on the CPU and is not in the shader (UNKNOWN). The CPU port
  takes a centre, a radius and an instance cap instead.

### 2.3 Placement (exact)

```
layerUv = (samplePoints[sampleOffset + s] + (tx, ty)) * tileSizeUV                     // G:83-87, 95
local.x = (layerUv.x - 0.5) * layerSize.x;  local.y = (0.5 - layerUv.y) * layerSize.y  // G:96-98
hUv     = (layerUv.x, 1 - layerUv.y) * CB1[2][2].z + CB1[2][2].w                        // G:99-100
height  = heightMap(hUv).r * CB1[2][2].y + CB1[2][2].x                                  // G:101-102 (sample_l, mip 0)
seed    = FNV-1a: h = (tx ^ 0x811c9dc5) * 0x01000193; h = (h ^ ty) * 0x01000193;
          h = (h ^ s) * 0x01000193; h ^= asuint(tileSizeUV.x)                          // G:103-109
r[k]    = xorshift32(h) (h ^= h<<13; h ^= h>>17; h ^= h<<5), float(h) * 2^-32, k = 0..4 // G:110-149 (literal 0x2f800000, 5 uses)
scale   = lerp(scaleMin, scaleMax, r0)                                                  // G:152-153
angle   = lerp(angleMin, angleMax, (r1, r2, r3))                                        // G:154-156
local.z = height + lerp(zOffsetMin, zOffsetMax, r4)                                     // G:150-151, 157
world   = rotate(CB1[2][0] (quaternion), local) + CB1[2][1].yzw                         // G:158-165
q       = euler(angle * 0.5), normalised                                                // G:166-179
          x = sa sb cc - ca cb sc, y = ca sb sc + sa cb cc, z = sa cb sc - ca sb cc, w = sa sb sc + ca cb cc
          (a, b, c = half angles x, y, z). Angle x alone turns about +Y, y about -Z, z about -X.
if useSurfaceNormal:
  N     = normalize(h(-1,0) - h(+1,0), h(0,-1) - h(0,+1), 2 * CB1[2][3].x)               // G:180-193, texel offsets
  up    = rotate(q, (0,0,1))                                                            // G:194-201
  align = shortest arc up -> N; identity if dot > 0.999; half turn about
          normalize(cross(X, up)) (or cross(Y, up)) if dot < -0.999                     // G:202-222
  q     = align * q                                                                     // G:223-230
store   int3(world * 4096), quaternion -> 4 x u16 (round((clamp(c,-1,1)+1) * 32767.5)), scale  // G:231-242
tint    = combinedAlbedo(layerUv.x, 1 - layerUv.y, mip CB1[2][1].x & 15).rgb            // G:243-249, if CB0[1].x & 1
```

INFERRED: the angles are radians (sincos of half the value). `CB1[2]` is built per terrain surface from
`StDiskTerrainSurface` (row 0 `objToWld.quat`, row 1 `.yzw` `objToWld.origin`, row 2 height scale and bias and the
height map's UV scale and bias, row 3 `.x` the world size of one height texel). The shader proves the arithmetic; how
the CPU fills those rows from `heightOffset` / `heightScale` / `surfaceMapWorldSize` is not in it.

### 2.4 LOD and fade

The compute pass has no distance term. Instances are ordinary static model instances, so the model's own LOD distances
apply (Verdansk's clutter models are not in the model cache yet). Speedball's grass props use the same static-model
foliage shaders, which dissolve with distance (section 3.3).

## 3. The models and how they are shaded (question 3)

### 3.1 Verdansk clutter models

There are 43 models (section 1.2): `tm/ee_foliage_grass_lm_{short,med,tall}_0N[_dead]`, `tm/foliage_grass_48_square_lm`,
`tm/foliage_grass_reeds_0N`, `tm/ee_foliage_grass_flowers_lm_short_01`, `tm/foliage_flower_camomile_*`,
`tm/ee_rock_riverbed_cluster_02_*`, `tm/ee_foliage_tree_birch_sapling_0N`, `tm/uk_foliage_small_weeds_0N` and a few
more. The export prints them with a leading `#`, which the reader strips.

### 3.2 Speedball's grass is placed props (VERIFIED, `verify.txt`)

| Instances | Model |
|---|---|
| 953 | `tm/foliage_grass_long_patch_02_a_02_reactive` (model flags 13: probe lit, animated opacity, reactive motion) |
| 51 / 9 / 2 | `tm/ee_foliage_grass_wild_long_128` / `_256` / `_64` |
| 46 | `tm/foliage_feather_grass_patch_small_01_reactive` |
| 40 | `tm/foliage_grass_long_patch_02_a_02` |
| 22, 11, 12, 5, 5, 5, 2, 1 | weeds overgrowth patch, long grass bush, strip weeds (reactive and not), long patch 02_c, small weeds 03, broadleaf, patch wall |

That is 1,164 instances of 14 models. `tm/foliage_grass_long_patch_02_a_02_reactive` is a 52 x 50 x 17 inch patch
of 458 triangles (LOD distances 350, 800, 1600, 2200; material `tmco/foliage_grass_long_01_green_low_reactive`; model
cache `detail/*.json`).

### 3.3 Stock foliage shading (VERIFIED unless marked; techset `tmco/lit_3_lit_alt_ta1_848810_1012000201000051_...`, MTL_TYPE_STATIC_MODEL_VERTCOL_SELFVIS)

- **Two passes.** A depth prepass (P) does the cut-out. The lit pass (L) runs with `GFXS_DEPTHTEST_EQUAL` (techset
  state), so only the prepass discards.
- **Alpha test**: the opacity map is texture slot 27 (`foliage_grass_long_01_opacity`, P's t0). Its **red** channel is
  the alpha, and with material flag 16 its green channel is a reveal ramp (P:50-75; flags = psData row 1 `.x` = 0x0F
  for this material: 8 is set, 16 is clear). A pixel is kept when `alpha > 0.501961` (128/255; P:76-81). The
  prepass VS writes `TEXCOORDS.w = 1` (`26d088bc` line 344), so no vertex alpha takes part. The colour map's alpha is a
  spec code (`&defaultspec`), **not** the cut-out.
- **Distance dither**: P:35-49 discards when `IGN(screen + TAA jitter) >= (viewDepth - cb7[171].z) * cb7[175].x`, where
  IGN is interleaved gradient noise (constants 0.067111, 0.005837, 52.982918). The meaning of the two code constants is
  INFERRED (fade end and -1/range). The grass dissolves with a per-pixel dither that TAA smooths. **That is a large
  part of the "soft carpet" look**.
- **Two-sided**: both P and L flip the vertex normal on back faces (`SV_IsFrontFace ? 1 : -1`; P:82-87, L:79-89).
- **Vertex colour**: V outputs `COLOR.rgb = byte 0 (R) / 255` as one grey value and `COLOR.a = 1` (V:164-170: byte 0 = `r0.x & 255`, divided by 255; 524-525).
  L multiplies the albedo by it (L:164). The reactive grass stores sway data in G/B (cache GLB: R = 255, G mean 1.3,
  B mean 46). **Multiplying by RGB would kill the green channel.**
- **Textures of L** (techset arguments): t2 = slot 0 colour, t3 = slot 9 normal + gloss, t4 = slot 59 tint mask
  (`$black`), t5 = slot 68 `foliage_grass_long_01_green_low_abs_256`. Slot 68 is multiplied by constant 18 (1, 1, 1 on
  this material) and added to the probe and light terms as back-lit **transmission** (L:162-163, 748-753, 857, 1223).
- **Normal map flattening**: the decoded tangent-space normal gets `z = 1 - 0.6 * |xy|^2` before renormalising
  (L:182-185). Blades read as mostly facing their card normal.
- **Self-visibility**: the SELFVIS interpolant (a per-vertex visibility cone) darkens probe light (L:90-95, 192-208).
  Probe lit (model flag 1).
- **Wind**: the VS has material constants 79-82, 100 and 105 (0.3; 3.62, 0.25, 5.65, 7.5; 8.25, -1.45, 9.42, 3; 19.58,
  5.25, 7.54, 1.7; 0.1; 0.5), INFERRED to be the flag-wave / reactive sway parameters (research 56). They were not
  decoded further.

## 4. The core module (`src/core/stock_clutter.h`)

| API | What |
|---|---|
| `readStockClutterCollections(exportPathOrDir, out, err)` / `parseStockClutterCollections(json, ...)` | `StockClutterCollection {index, maxInstanceCount, smodelCollectionIndex, terrainSurfaceIndex, clutterLayerIndex, smodelIndex, model}` from a gfx_map export. It parses only the `smodels` members it needs: Verdansk's 422 MB export reads in 0.8 s. `null` gives 0 rows and success (Speedball). |
| `parseStockClutterTerrain(json, StockClutterTerrain&)` | stterrain JSON: surfaces (`objToWld`, `layers`, `bitmask`, height scale and offset) and `samplePoints`. Accepts `values` or hex `bytes` arrays. **Tested on synthetic JSON only**, since no stock stterrain with data can be exported today. |
| `decodeStockClutterLayer` / `encodeStockClutterLayer` | the 84-byte record |
| `clutterSeed`, `clutterXorshift`, `clutterUnit`, `clutterEulerQuat`, `clutterAlignQuat`, `quatMultiply`, `quatRotate`, `quatAxes` | the shader arithmetic, exposed for tests |
| `ClutterHeightmap` + `heightmapGround(map)` | the shader's height read (bilinear, clamp addressing INFERRED; v flip; texel-offset normal) |
| `StockGroundGrid` (`build(StockWorld, cell, keepMaterial, minUpZ)`, `addTriangle`, `finish`, `sample(x, y, zMax, z, n)`) + `meshGround(grid, frame, zMaxAbove)` | ground from the editor's own world mesh when the height image is missing (translation and yaw of the frame honoured) |
| `scatterStockClutterLayer(layer, model, collection, samplePoints, bitmask, frame, ground, opt, out)` | one layer, exact placement |
| `scatterStockClutter(collections, terrain, groundForSurface, opt, out)` | a whole map |
| `ClutterScatterOptions {centreX, centreY, radius, maxInstances (200,000), keepEvery}` | density cap: tiles are visited nearest first, only tiles under the circle are looked at, instances outside the radius are dropped, the cap stops the scatter, and `keepEvery` thins deterministically by sample index |
| `StockClutterScatter {models, instances, tilesVisited, samplesDropped, capped}`; `StockClutterInstance {model, collection, tileX, tileY, sample, origin, quat, axis, scale}` | the result. `axis` uses the same column convention as `StockModelInstance::axis` (stock_smodels.h). |

Tests: `tests/stock_clutter_tests.cpp`, 12 tests. They cover the RNG against an independent Python model
(`out/agent/clt/py/ref.py`), the euler axes, the shortest-arc cases, the record round trip, height-map sampling and
slope signs, exact placement, determinism, the cap, the radius, `keepEvery`, dropped samples, a short bitmask, surface
normal alignment, gfx_map JSON in values, bytes and null forms with a bad index, stterrain JSON in values and bytes
forms, the ground grid (roof versus ground, `zMax`, material filter, face-down triangles) and the real Speedball cache
(0 collections; it skips when the cache is missing). `xmake build -y iw8r_tests`: **1,212 tests, 140,976 checks, 0
failures** (2026-11-12).

## 5. What the main session needs

**Speedball (and every 6v6 map):** there is no GPU clutter to add. The viewport already places the 1,164 grass and
weed props. To get closer to the in-game look, change how alpha-tested `tmco` foliage props are shaded (section 3.3):

1. Cut out on the **opacity map (slot 27) red channel > 128/255**, not on the colour map's alpha. The material cache
   already carries it as `mask`. Check that the viewport's foliage path uses it.
2. Draw two-sided and **flip the normal on back faces** before lighting. Dark speckles are what back faces lit with
   the front normal look like (INFERRED cause of the editor look; not traced in the viewport code).
3. If model vertex colour is ever applied, use **byte 0 (R) only as a grey factor**. G and B are reactive-motion data
   on `_reactive` models.
4. Optional, for softness: add slot 68 × const 18 as a transmission term on the shadowed side, soften the normal map
   (`z = 1 - 0.6|xy|^2`), and approximate the distance dither plus TAA with alpha-to-coverage under MSAA or a
   distance fade.
5. Wind sway: 1,016 of the props are reactive (`_reactive`, model flag 8). `foliage_sway.h` (research 56) can drive a
   preview. The true per-material wave constants are listed in section 3.3 but not decoded.

**Verdansk (later):** call `readStockClutterCollections(worldCacheDir)`, then a stterrain export, then
`scatterStockClutter` with `heightmapGround` (from the surface's `heightMap` image) or `meshGround` over the terrain
mesh, with a camera-centred radius and an instance cap. Fetch each `model` through the model cache, draw it as an
alpha-tested static model, and optionally tint it by the ground colour under it (`clutterTintData`). Blocker: ACTS must
export the `stterrain` asset of mp_donetsk / mp_br_quarry. It needs the extent of `StMesh.vd0.data` and
`layerMask.data` (from `vertexCount` and the vertex stride, or `triCount`), which are not count fields today.
The terrain mesh itself (the super-terrain nodes) is also not in the editor's StockWorld, so a mesh ground on Verdansk
needs that export too.

## 6. Verification on Speedball

- `verify_clutter.exe <world_cache> <out>` (`out/agent/clt/verify/`, built by `build.bat` against
  `bin/release/iw8r_core.lib`). Output is in `out/agent/clt/verify.txt`: Speedball has 0 collections; Verdansk has
  678 collections, 43 models and 24 surfaces; Speedball has 1,164 placed grass/weed props from 14 models.
- `out/agent/clt/sb_grass_topdown_arena.png` (x -1300..1300, y -900..1300; grey = world, green discs = grass props at
  25-inch footprint radius, black dots = other props) and `sb_grass_topdown.png` (whole grass extent). The arena
  shows grass all around the edges and in islands, with the bare dirt ring and tyre tracks in the middle. That is the
  same pattern as the owner's screenshot (`out/agent/clt/crop_ingame.png` against `crop_editor.png`, same region):
  the placed props are where the in-game grass is. The difference in the screenshots is shading, not density.

## 7. Open items

- How the CPU picks the tiles to dispatch, and the per-layer budget handling (`maxInstanceCount`,
  `clutterTileInstanceCounts`). Not in the shader.
- The exact CB1[2] rows from `StDiskTerrainSurface` fields (section 2.3 INFERRED).
- The meaning of the distance dither constants `cb7[171].z` and `cb7[175].x`, and their values (code constants,
  per view).
- Quarry's clutter collections (no world cache of mp_br_quarry was available). Only its stterrain export failure was
  seen.
