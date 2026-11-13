# 99. Stock decal volumes (Replay 1.20): record layout, projection, opacity, and a CPU projector for the viewport

**Why.** Stock maps place projected box decals (red wall paint stripes, stencil letters, dirt, burn marks, bullet
holes) as *decal volumes*. They are not geometry: the lit pixel shaders of world surfaces and static models blend them
into albedo, normal and gloss. The editor's stock world view does not draw them, so Speedball's walls lack their red
and green paint bands. This note reads the layout and the projection from the shipped compute and pixel shaders and
adds a core reader and a CPU projector (`src/core/stock_decal_volumes.h`).

**Labels.** VERIFIED = read in the disassembly or the exported data (cited). INFERRED = from names, the IW8 game-test
decompile (another build), statistics or arithmetic. The DXBC has no RDEF block, so register names are ours. Research
80 holds the containers (GfxWorld tables, transient zone block, Umbra visibility); this note upgrades its record layout
from "checked on stock data" to VERIFIED and answers its open items 1 and 2.

## How it was obtained

- Compute shaders: the `code_post_gfx` computeshader export already on disk
  (`out/agent/shader_research/cs/.../cs_decal_volume_generate.hlsl/*.cso`, `cs_decal_volume_cluster_pass_single`),
  disassembled with `out/agent/shader_research/py/dxbc_dis.py`. Kept: `out/agent/dvol/cs/*.asm`. Cited as **G**
  (`cs_decal_volume_generate_draw_data.55.asm`), **C** (`cs_decal_volume_cull.49.asm`).
- World pixel shader: **O** = PS `3430ea7f` (opaque `twc/lit_3_lit_rpl_ta1_...`, `out/agent/decals/asm/3430ea7f.asm`),
  the decal loop at lines 160-418.
- Which lit shaders have the loop: ACTS `-a techset` and `-a pixelshader` on `techsets_mp_m_speedball.ff`, every
  `TECHNIQUE_LIT_FORWARDPLUS_BITMASK` PS scanned for the t56 (stride 256) reads (`out/agent/dvol/py/lit_ps.py`,
  result `out/agent/dvol/exp/lit_ps.txt`). The exports were deleted afterwards.
- Decal materials and masks: ACTS `-a material,decalvolumematerial,decalvolumemask` on `techsets_mp_m_speedball.ff`
  and `-a decalvolumemask,decalvolumematerial` on `mp_m_speedball.ff`. Kept (small): `out/agent/dvol/exp/map/`.
- World data: the editor's world caches of mp_m_speedball, mp_hideout, mp_m_king, mp_shipment
  (`%LOCALAPPDATA%/IW8 Radiant Suite/.../world_cache/<map>/assets/gfx_map*`), read by `out/agent/dvol/py/decode.py`
  (Python reference decoder) and by the new core reader through `out/agent/dvol/verify/verify_dvol.exe`.
- Scene constants: game-test `gfx_d3d/r_decal_volume.cpp:12848-12900` (dvars) and `:13000-13034`
  (`decalVolumesParams`, `Params1`, `Params2`), `:8650-8765` (the draw-data struct names). INFERRED for Replay; only
  names, defaults and the mapping to cb7 rows come from there.

## 1. Data layout (question 1)

### Where, in the ACTS JSON (VERIFIED, Speedball export)

| Export | Field | Content |
|---|---|---|
| gfx_map | `decalVolumeMaterialCount` 16, `decalVolumeMaskCount` 45, `mapDecalVolumeCount` 225, `decalVolumeCollectionCount` 34 | counts |
| gfx_map | `decalVolumeMaterials.values[i].material.name` | Material carrier, e.g. `,i/decal_paint_01_red` (leading comma = reference) |
| gfx_map | `decalVolumeMasks.values[i].blendMapOverride.name` | GfxDecalVolumeMask, e.g. `decal_mask_stain_06_c` |
| gfx_map | `decalVolumeCollections.values[i]` | `{transientZoneIndex, decalCount, firstZoneDecal}` |
| gfx_map_trzone | `transientZoneIndex`; `decalVolumes.values[0]` (GfxDecalVolumeTransientData) | `decalVolumeCount`, `firstCollection`, `collectionCount` |
| gfx_map_trzone | `...decalVolumes.values[k]` (GfxStaticDecalVolumePacked, 44 bytes) | `obbCenter.v[3]`, `obbQuat`, `obbHalfSize`, `opacityParams`, `uvMatrixAndOffset[3]`, `packedMaterialInfo`, `reserved` |
| gfx_map_trzone | `...collectionBounds.values[c]` | `{midPoint.v, halfSize.v}` per collection of the zone |
| gfx_map_trzone | `...decalVolumeUids.values[k].uid` | one u32 per record |

A **collection** is a run of records in one transient zone (`firstZoneDecal .. + decalCount`), the unit of Umbra
visibility (research 80). It does not change how a decal looks. Speedball: 34 collections cover all 225 records.

A decal material is three assets (research 80): the carrier Material (techsets zone), a **GfxDecalVolumeMaterial**
of the same name (`decalvolumematerial`, in `techsets_<map>.ff` and `<map>.ff`) and optionally per placement a
**GfxDecalVolumeMask** (`decalvolumemask`, `<map>.ff`) whose `blendMap` image is `<mask>~atlasbc4` (all 45 Speedball
masks). Speedball's `i/decal_paint_01_red` (VERIFIED export):

```
channels  [0] decal_paint_01_c&defaultspec~15253967373450432551   colour (rgb) + spec code (a)
          [1] decal_paint_01_n&decal_paint_01_g~6931389124105404131 normal + gloss
          [2..5] null                                               no opacity map: the shape is the volume's mask
flags 0x40D (colour 4, normal 8, bit 0, 0x400), colorTint (0.5776, 0.0578, 0.0395), alphaDissolveParms 0,
emissiveScale 1, atlas 1 x 1
```

The other paints use the same two images with another tint (`red3` 0.305, 0, 0; `green` 0.076, 0.202, 0.091; `white`
0.716; `blue`, `yellow`). Materials with an opacity map (flag 0x40 and channel 2 set): `bullet_holes_wood_01`,
`footprint_mud_01`, snow and tyre decals.

### The 44-byte record (VERIFIED, G:32-131)

| Field | Decode |
|---|---|
| `obbCenter` | float3, world inches |
| `obbQuat` | bits 30-31 = index of the dropped part (0 x .. 3 w). Bits 0-9 and 10-19: kept parts 1, 2 = `(raw * 2/1023 - 1) * sqrt(1/2)`; bits 20-28: kept part 3 = `(raw * 2/511 - 1) * sqrt(1/2)` (G:41-46). Kept parts fill x, y, z, w in order around the dropped one (G:51-55). Dropped part = `sqrt(1 - min(1, a^2 + b^2 + c^2))` (G:47-50). Bit 29 is not read. |
| rotation | **axis[j] = column j of the quaternion's rotation matrix** (G:56-69, below) |
| `obbHalfSize` | three unsigned small floats read as `f16tof32(bits << 4)` for bits 0-10 and 11-21 and `f16tof32(bits << 5)` for 22-31 (G:70-76): 5-bit exponent bias 15, 6 / 6 / 5 mantissa bits. Half size along axis 0, 1, 2. Multiplied by a scene vec3 `CB0[19].xyz` (G:104; game-test `r_decalVolumesScale`, default 1 1 1) |
| `opacityParams` | bytes / 255: e0 (byte 0), e1 (byte 1), scale (byte 2), feather (byte 3) (G:77-95) |
| `uvMatrixAndOffset` | six binary16, low half first: m00, m01 (word 0), m10, m11 (word 1), offU, offV (word 2) (G:96-103) |
| `packedMaterialInfo` | bits 0-10 material index, 11-21 mask index (0x7FF none), 22-23 normal blend mode (G:100, 132); bits 29-31 user draw order + 4 (C:215-217, top of the cull sort key); bits 24-28 not read by any decal CS of 1.20 |
| `reserved` | not read |

Rotation, written by G:56-69 and stored at draw-data offsets 48, 128, 144 (G:283, 291-296):

```
axis0 = (1 - 2y^2 - 2z^2, 2xy + 2wz, 2xz - 2wy)      projection direction (points INTO the receiver)
axis1 = (2xy - 2wz, 1 - 2x^2 - 2z^2, 2yz + 2wx)      u direction
axis2 = (2xz + 2wy, 2yz - 2wx, 1 - 2x^2 - 2y^2)      v direction (image rows go down along +axis2)
```

**Opacity remap (VERIFIED G:83-95).** If `e0 >= e1` or `scale <= 0` the shader uses (e0, e1, scale) = (0, 1, 1).
Then `A = scale / (e1 - e0)`, `B = -e0 * scale / (e1 - e0)`. This settles research 80's open question: the 4 shipment
records with e0 = e1 (51, 51, 102, 255) and Speedball's are simply the default curve. Feather: `f = byte3 / 255`;
draw data stores `-1/f`, or `-100000` when f = 0 (G:91-95, a hard edge).

### Speedball example: a red paint stripe (record 4, VERIFIED export, decoded by both decoders)

```
obbCenter (-1039.5, -415.0, 203.5)  obbQuat 0x4FF7FDFF  obbHalfSize 0x9D2D2460  opacityParams 0x00A5FF00
uvMatrixAndOffset 0x4644 0x3A848000 0xB188AA00   packedMaterialInfo 0x84800801
-> axis0 (-1.00, -0.001, 0.003)  axis1 (-0.001, 1.00, -0.001)  axis2 (-0.003, -0.001, -1.00)
   half (6, 200, 26): a 400 x 52 inch band, 12 inches deep, projected along -X onto the wall at x ~ -1045
   material 1 = i/decal_paint_01_red, mask 1 = decal_mask_stain_06_c, normal mode 2, draw order 0
   e0 0, e1 1, scale 165/255 -> A 0.647, B 0; feather 0
   uv m00 6.266, m11 0.814, offset (-0.047, -0.173): the paint texture tiles 6.3 times along the band;
   the mask is stretched once over the whole band (it uses the raw box UV)
```

Speedball's 28 `decal_paint_01_red` volumes: 22 are such wall bands (half 6 x 77..222 x 18..27, uv m00 2.4 to 6.9),
on the walls at x = -1045 (y -615 .. 950), y = -607 (x -1050 .. 850), y = 941 / 793 / 788, x = 1081 and 967, and
one 45-degree wall at (1033, -552). Six others are short pieces on wood props (records 95, 145, 146, 174) and a sign
(144, mask `decal_mask_paint_06_c`, feather 0.25). The `red3`, `green` volumes are 0.5-deep stencil letters
(`decal_mask_stencil_letters_*`, e1 = 25/255, so A = 10.2: a hard threshold) on crates.

### Statistics (VERIFIED, `exp/sb_decode.txt`, `exp/other_decode.txt`)

| Map | Materials | Masks | Collections | Volumes |
|---|---|---|---|---|
| mp_m_speedball | 16 | 45 | 34 | 225 |
| mp_hideout | 59 | 60 | 165 | 1104 |
| mp_m_king | 13 | 23 | 31 | 181 |
| mp_shipment | 15 | 16 | 7 | 46 |

Speedball: normal blend mode 2 on all 225; draw order 0 on 222, +3 on 3; 117 identity UV; 181 project horizontally
(walls), 23 straight down. No colour, fade distance or angle parameter is stored per volume: tint is the material's,
the angle fade and edge fade are scene constants, and distance fade comes from the camera (G:262-267).

## 2. How the game projects (question 2)

### Pipeline (VERIFIED structure)

`cs_decal_volume_cull` culls the records by screen size and occlusion and writes a sort key
`(drawOrder + 4) << 21 | byte << 13 | index` (C:211-217). `cs_decal_volume_generate_draw_data` turns each visible record
into a 256-byte draw record (G:280-304). The cluster passes bin records per screen cluster (32 x 32 pixels x 32 depth
slices, game-test `r_decalVolumesClusterXYSize`). Every lit pixel shader that has the loop reads its cluster's list
from t57 and the draw records from t56 (O:160-418) and applies the decals in list order, each over the previous result.

### The draw record (VERIFIED G:239-304; names from game-test `r_decal_volume.cpp:8650-8765`, INFERRED)

| Offset | Content |
|---|---|
| 0 | `worldToDecal[0]` = `(0.5/h1) * axis1`, `w = 0.5 - 0.5 dot(axis1, c - cam)/h1` -> **u** |
| 16 | `worldToDecal[1]` = `(0.5/h2) * axis2` -> **v** |
| 32 | `worldToDecal[2]` = `(1/h0) * (-axis0)` -> **depth** |
| 48 | `-axis0` (the decal normal), `.w` flags |
| 64 | mask atlas rect (xy), opacity-map atlas rect (zw) |
| 80 | `(A * dist, B * dist, -1/feather, 1)`, `dist = saturate(max(|cam - c| - max half, 0) * k + b)` (camera distance fade) |
| 96 / 112 | UV matrix; `(offU, offV, alphaDissolveParms)` |
| 128 / 144 | `axis1`, `axis2` (decal tangent frame), `.w` normal and colour remap bytes |
| 160 | colour tint (the material's `colorTint`) |
| 176-224 | colour / normal / emissive atlas rects, emissive scale, displacement |

Flags (VERIFIED G:255-261 and G:156-210 with O:194, 223, 248, 290): `material.flags & ~0x61` plus: 1 = normal add
(mode 1, or mode 2 and material bit 0; mode 0 clears it); 32 = a raw-UV mask is bound; 64 = the opacity map
(channel 2) is sampled at the material UV; 512 = skip. When a volume has no mask, an identity UV transform and A = 1,
B = 0, the shader moves the opacity map into the raw-UV mask slot (sets 32, clears 64; G:146-155, 179-210).

### Per pixel (VERIFIED O:199-412)

P = the pixel's world position (camera relative), Ng = the interpolated vertex normal (v4), not the normal map.

```
u = dot(row0, P), v = dot(row1, P), depth = dot(row2, P)                                     O:202-204
inside  = floor(u) == 0 && floor(v) == 0 && floor(|depth|) == 0                              O:205-209
a       = saturate(dot(-axis0, Ng) + cb7[118].z)                                             O:210-211
skip unless inside && a > cb7[118].x                                                         O:212-214
m       = (flags & 32 ? t118(maskRect(u, v)).r : 1)                                          O:223-235 (raw u, v)
m      *= 1 - saturate(256 * (max(|2u - 1|, |2v - 1|) - cb7[118].w))                         O:236-241 edge fade
op      = m * saturate(m * A + B)                                                            O:242-243
skip unless op > cb7[116].x                                                                  O:244
(mu, mv)= frac(m00 u + m01 v + offU, m10 u + m11 v + offV)                                   O:246-256
op     *= (flags & 64 ? t118(opacityRect(mu, mv)).r : 1)                                     O:260-275
op     *= saturate((1 - |depth|) / feather)       (feather 0: saturate(100000 (1 - |depth|)))  O:274-276
if (flags & 128) op = saturate(op * alphaDissolve)        (static decals: draw[80].w = 1)    O:279-282
op     *= saturate((a - cb7[118].x) * cb7[118].y)                                            O:277-283 angle fade
skip unless op > cb7[116].x                                                                  O:284
if (flags & 4)  albedo = lerp(albedo, t116(colourRect(mu, mv)).rgb * tint, op);  spec code likewise (bytes of draw[144].w)
if (flags & 8)  gloss, cavity lerp by op; decal normal n = hemi-oct(t117.yw) in the frame (axis1, axis2, -axis0),
                projected on the surface tangent frame; surface normal.xy = (flags & 1) ? n.xy + its xy : lerp(n.xy)  O:313-370
if (flags & 16) emissive += op * (tint * t119 * scale - emissive)                            O:385-412
```

Scene constants (arithmetic VERIFIED; values INFERRED from game-test defaults, `r_decal_volume.cpp:13007-13034`):
`cb7[118] = (cos 86 deg = 0.0698, 1 / (cos 61 deg - cos 86 deg) = 2.409, normal bias 0, 1 - 0.0078125)`, alpha
threshold `cb7[116].x = 1/255`. So the angle fade is 0 at 86 degrees between the decal normal and the surface and full
at 61 degrees or less; back faces get nothing; the edge fade only touches the outer 0.4 % of the box.

### UV orientation (VERIFIED arithmetic, INFERRED atlas copy)

`u = 0.5 + 0.5 dot(axis1, p - c)/h1`, `v = 0.5 + 0.5 dot(axis2, p - c)/h2`, `depth = dot(-axis0, p - c)/h0`. The
atlas lookup is `rect.xy + (mu, mv) * rect.wh` (O:261-267, the rect in 1/4096 units), so (0, 0) is the image's first
row. `axis1 x axis2 = axis0` (right handed), so for a viewer looking along axis0 (into the surface) u runs right and v
runs down: the picture is upright and not mirrored. On record 4 this gives u along +Y and v along -Z, i.e. the image's
top row at the top of the band. That the game copies images into the atlas unflipped is INFERRED.

### Receivers (VERIFIED by shader presence, `exp/lit_ps.txt`)

The loop is in every Speedball world lit PS except the sky (`tw/sky_3_sky...`): opaque, layered (`tw4`, `twc4`) and
the blended `_bad_` decal surfaces (research 98) too. It is in 137 of 153 static-model lit techniques
(`MTL_TYPE_STATIC_MODEL_*`); the 16 without it are alpha-tested `tmco`/`tmo` sets with the hexB `...0201...` token
(foliage-like) and the two `card` sets. No dynamic-model (`MTL_TYPE_MODEL_*`) lit PS has it (29 of the 29 whose PS is
in this zone; 20 more use shaders of other zones; models have a separate object-space grid path, game-test
`r_decalVolumesObjSpaceModel`). INFERRED: the static-model loop is the same
math (not read line by line).

### Material textures (question 2, last part)

| Atlas | Source | Read | Use |
|---|---|---|---|
| t116 (persistent image 31, "primary") | channel 0 `<colour>&<spec>` | rgb, a | albedo = rgb * colorTint; a = spec code |
| t117 ("secondary") | channel 1 `<normal>&<gloss>` | .x gloss, .yw hemi-oct normal, .z cavity | normal / gloss / cavity |
| t118 ("tertiary", BC4) | the volume's mask `blendMap`, and channel 2 | .r | opacity |
| t119 ("quaternary") | emissive | rgb | emissive |

The four bindings come from game-test `R_DecalVolumes_SetupFrame` (`codePersistentImages[31..34]`, INFERRED for
Replay; the register use is VERIFIED). No decal material has a technique, a blend state or a depth state of its own:
the decal is a lerp inside the receiver's shader, before lighting.

## 3. Editor core (implemented 2026-11-12)

`src/core/stock_decal_volumes.h/.cpp` (new), tests `tests/stock_decal_volumes_tests.cpp` (7 tests).

- `parseStockDecalVolumes(gfxMapJson, trzoneJsons, set)` / `readStockDecalVolumes(zoneDir, set)`: key scan (files are
  read in 8 MB blocks, only the decal values are parsed). `StockDecalVolumeSet` = material names, mask names,
  collections (with bounds), volumes. `StockDecalVolume`: material and mask names and indices, centre, quaternion,
  unit axes, half sizes, the opacity bytes and the effective A / B, UV matrix and offset, normal blend mode, draw
  orders, collection, uid, raw words.
- Decoders: `decodeStockDecalQuat`, `stockDecalAxesFromQuat`, `decodeStockDecalHalfSize`, `decodeStockDecalVolumeRecord`.
- Maths: `stockDecalCoord` (u, v, depth), `stockDecalMaterialUv`, `stockDecalVolumeBounds`, `stockDecalAngleFade`,
  `stockDecalEdgeFade`, `StockDecalSceneParams` (the cb7 defaults above).
- Materials: `parseStockDecalMaterial` (decalvolumematerial JSON), `parseStockDecalMask` (decalvolumemask JSON),
  `stockDecalShading` (the flag logic, including the opacity-map move), `stockDecalOpacity` (the per-pixel opacity
  above, a CPU reference).
- Projector: `StockDecalReceivers` (a BVH over receiver triangles: `addWorld(world, meshMask)`, `addMesh(mesh,
  lightmapUv, lightmapIndex)` for any world-space mesh such as transformed static models, `build`), then
  `projectDecalVolume(receivers, volume, batch)` (bounds query, separating-face reject, Sutherland-Hodgman against the
  six planes in box-normalised space, fan triangulation) and `projectDecalVolumes(receivers, set)` (one batch per
  volume in draw order). `projectDecalVolume(world, volume, batch)` builds a tree for one call. Output vertex:
  position, receiver normal, box (u, v), material (mu, mv) before wrapping, depth, angle fade, the receiver's lightmap
  UV and index. Polygons whose corners all have fade 0 are dropped (option).

### Verification on real data (`out/agent/dvol/verify/verify_dvol.exe <world_cache/map>`, outputs `exp/verify_*.txt`)

Receivers: every world mesh except `tw/sky_*` and `tw/caulk_shadow`.

| Map | Volumes | World tris | Batches | Output tris | Empty volumes | Project time | Tree build |
|---|---|---|---|---|---|---|---|
| mp_m_speedball | 225 | 38,254 | 91 | 2,372 | 134 | 1.2 ms | 10 ms |
| mp_hideout | 1,104 | 267,921 | 874 | 40,379 | 230 | 16.9 ms | 73 ms |
| mp_m_king | 181 | 82,188 | 118 | 6,395 | 63 | 4.1 ms | 24 ms |

Reading the decal tables takes 73 to 465 ms (the world itself 0.7 to 3.1 s). Speedball per material (volumes / with
triangles / triangles): `decal_paint_01_red` 28 / 23 / 845, `dirt_soil_erosion_01_dark` 53 / 32 / 344,
`footprint_mud_01` 9 / 9 / 794, `puddle_muddy_01` 4 / 4 / 104, `rust_01` 12 / 7 / 60, `burn_mark_01` 5 / 5 / 49,
`paint_01_white` 7 / 3 / 34, `paint_01_yellow` 2 / 2 / 36, `sand_dusty_01` 3 / 3 / 33, `dirt_rocky_pebbles_01_wet`
1 / 1 / 67, `bullet_holes_wood_01` 47 / 2 / 6, and 0 triangles for `paint_01_green` (18), `paint_01_red3` (18),
`bullet_holes_concrete_01` (12), `paint_01_blue` (4), `bullet_holes_metal_01` (2).

Every empty volume was checked: on Speedball 107 of the 134 touch no world triangle at all and all 134 have a static
model origin within 256 inches (plywood, containers, crates: the stencil letters and the bullet holes sit on props); the
other 27 reach only world triangles that are perpendicular to the decal (best facing 0.00, e.g. a wall decal box that
also crosses the floor), which the angle cutoff removes. So on Speedball 22 of the 28 red paint volumes, the wall
bands, draw on world geometry (24 to 65 triangles each); the 6 others need static-model receivers.

`xmake build -y iw8r_tests`: 1,200 tests, 140,844 checks, 0 failures (2026-11-12).

## 4. Drawing them in the viewport

- **Batch per volume, after all opaque world (and static model) draws**, in `projectDecalVolumes` order (ascending
  draw order, then record index; the list order inside a cluster is INFERRED to follow the cull sort key). Depth test
  LESS_EQUAL, no depth write, a small depth bias toward the camera (as research 98's decal surfaces), or
  `offsetAlongNormal` about 0.05 inch. They cast no shadows and write no depth.
- **Blend** `SRC_ALPHA / INV_SRC_ALPHA` with `rgb = decalAlbedo * light`, `a = op`. The game lerps albedo before
  lighting; drawing the decal lit with the receiver's own lighting (the batch carries its lightmap UV and index)
  gives `lerp(albedo, decal, op) * light` exactly for the diffuse part. In an unlit view use the albedo alone.
- **Textures, per material name** (`set.volumes[i].material`, e.g. `i/decal_paint_01_red`): read the
  `decalvolumematerial` asset of that name (techsets zone or map zone). Channel 0 = colour (sRGB) `* colorTint`, with
  a **WRAP** sampler at (mu, mv). Channel 2 (flag 0x40) = opacity, `.r`, WRAP at (mu, mv). Per volume, mask
  `set.volumes[i].mask` -> the `decalvolumemask` asset's `blendMap` (`<mask>~atlasbc4`), `.r`, **CLAMP** at the raw
  (u, v). Images in the material cache are looked up by these names (the `~hash` suffix is part of the image name).
- **Pixel opacity** (port of `stockDecalOpacity`, `s = stockDecalShading(volume, material)`):

```hlsl
float m = (s.mask ? MaskTex.Sample(clampS, float2(u, v)).r : 1.0);     // s.maskFromVolume or s.maskFromOpacityMap
m *= 1.0 - saturate(256.0 * (max(abs(2*u - 1), abs(2*v - 1)) - (1.0 - 0.0078125)));
float op = m * saturate(m * A + B);
op *= (s.opacityMap ? OpacityTex.Sample(wrapS, float2(mu, mv)).r : 1.0);
op *= saturate((1.0 - abs(depth)) * (invFeather > 0 ? invFeather : 100000.0));
if (s.dissolve) op = saturate(op * alphaDissolve);
op *= fade;                                              // per-vertex angle fade from the projector
clip(op - 1.0 / 255.0);
float3 decal = ColourTex.Sample(wrapS, float2(mu, mv)).rgb * tint;   // only when s.colour (flag 4)
return float4(decal * light, op);
```

- **Receivers to leave out**: sky and caulk meshes; the blended `_bad_` sortKey 10-17 surfaces (research 98): the
  game applies the decal to them too, but a separate batch over a half-transparent surface and over the opaque surface
  under it applies it twice. Static models can be added with `addMesh` once their triangles are in world space
  (needed for 107 of Speedball's 225 volumes, including 6 red paint pieces and all stencils).
- Normal, gloss and cavity changes (flag 8) are not in the batch; for a colour + lightmap view they can be ignored.

## What is not determined

- The Replay values of the scene constants (cut-off angle, range, normal bias, edge fade, alpha threshold, distance
  fade); the game-test defaults are used.
- The order of records inside a cluster list (sort direction of the cull key), and the meaning of the key's middle
  byte (from a 4-byte per-record buffer at cull time).
- Whether the atlas copy flips images (assumed not), the samplers' address modes (the shader wraps the material UV
  itself with `frc`, so WRAP on the original image is equivalent).
- The material record words that hold the spec / gloss remap bytes and the material's own blend-map slot (word 8 low
  half, used when a volume has no mask); channels 3-5 are null on every Speedball material.
- The static-model PS loop was found by its buffer reads, not read line by line.
