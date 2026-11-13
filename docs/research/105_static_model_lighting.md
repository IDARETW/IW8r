# 105. How Replay 1.20 lights static models (placed props)

Read-only research, 2026-11-13. Goal: light the editor's stock props the way the game does, instead of one map-wide
ambient (`indirect = stockProbeLight(...) * luminance(gameSky.rgb)` in `src/viewport/render.cpp`, the stockRef branch
of ps_tex).

**Labels.** VERIFIED means read in shader bytecode, struct definitions or decoded data (cited). INFERRED means
reasoned from that evidence but not proven. Nothing was run in the game.

**Sources.**
- VS = the static-model lit vertex shader `bab2e3065c45cc9e3b3a4b73a6d72f0f` (curb stone, techset `tm2co/...`,
  `MTL_TYPE_STATIC_MODEL_SECONDUV_VERTCOL_SELFVIS`), disassembled in research 95:
  `out/agent/shader_research/sh/mw19replay/techsets_mp_backlot2/assets/vertexshader/bab2e306...0.asm`. Line numbers
  are written "VS:n".
- Structs: `E:\mw124\x64-zt\src\games\iw8\structs.hpp` ("S:n") and the zone stream order in
  `src\games\iw8\zone\asset_streams.cpp` ("Z:n").
- The world cache exports (`%LOCALAPPDATA%\IW8 Radiant Suite\IW8 Radiant Suite\world_cache\<zone>\assets\`): the
  `gfx_map` (GfxWorld) and `gfx_map_trzone` (GfxWorldTransientZone) JSON of mp_m_speedball, mp_hideout and mp_shipment.
- mp_m_speedball and mp_hideout were decompressed with ACTS (`fastfile -d --noAssetDump`, as the world cache's lightmap
  job does), read, and the `.dec` files deleted. Scripts and the two extracted light-volume blocks (5 MB) are kept in
  `out/agent/smodel_lighting` (section 7).

## 0. Summary

- A probe-lit prop (model flag 1, every `tm*/` material) is lit by its own small **light grid volume (LGV)**: a 3D box
  of 1 to 64 L2 spherical-harmonic probes (1x1x1 for 21 to 71 % of volumes) that the bake fitted around the prop. The
  vertex shader maps each vertex's world position into the box, samples the probes trilinearly from a 3D atlas,
  and evaluates the irradiance for the vertex normal. That value is the `AMBIENT_COLOR` interpolant (VERIFIED, VS).
- The box table (`staticModelVolumes`: world-to-box transform, size, first probe) is in ACTS's `gfx_map_trzone` export
  already. The probe **atlas** (`tempLightGridVolumeData`) is exported as null, like the lightmap pixels. It follows the
  box table directly in the decompressed zone. It is 64 x 64 x D texels in two textures, R11G11B10F for L0 and RGBA8
  for 24 normalised L1/L2 values, plus per-box packing params (VERIFIED on speedball and hideout).
- Instance i finds its box through `smodels.modelStaticIndirection[i].lightingInstanceIndirection` (and
  `.worldTransient`). On all three maps 99.7 to 100 % of probe-lit instances have their bounds centre inside their box;
  97.7 to 99.9 % are exactly at its centre (VERIFIED).
- The decoded light is plausible. On hideout, props with world geometry overhead have a median E(up) luminance of
  **0.71**. Props under open sky have **3.37**: 4.7 times brighter and bluish (VERIFIED decode; the
  indoor/outdoor split is a ray test).
- Lightmapped props (model flag 2, `tml/` materials) use the same index for `lmapLookup`, plus per-vertex
  `PackedLmapCoords` = two unorm16 (VERIFIED layout, mapping checked on 3 placements).
- The **tetrahedral GPU light grid** (also fully in the trzone export) is not read by the static-model shader. It is
  INFERRED to light dynamic models. Its probes agree with the LGVs (hideout DC correlation 0.93), so it is the fallback
  for editor-placed or moved props.
- **SELFVIS** is per vertex in the model's vertex record (u32 at +8 of the 20-byte vertex): a bent-normal direction, a
  visibility amount and a cone value. ACTS's GLB export drops it (VERIFIED).
- Recipe (section 5): extract the LGV block in the world cache's existing decompress pass. Give each prop instance
  its box transform and atlas UV scale and bias. Sample and evaluate the SH in the VS or PS. Then
  `indirect = E(N) * bakedLightScale`. The simplest first step is a per-instance SH at the box centre.

## 1. Where a prop's light lives (question 1)

### 1.1 The chain (VERIFIED)

```
instance i  (GfxWorld.smodels: collections[c] covers instances firstInstance .. +instanceCount)
  -> smodels.modelStaticIndirection[i] = { u32 lightingInstanceIndirection, u16 splinedModelInstanceIndex, u16 worldTransient }   (S:12113)
  -> model flags (smodels.models[collections[c].smodelIndex].flags, S:12057):
       flag 1 LIGHTPROBE : k = lightingInstanceIndirection indexes transientZone[worldTransient].gpuLightGrid.staticModelVolumes[k]
                           and that zone's probe atlas (packing / transform params [k])
       flag 2 LIGHTMAP   : k indexes GfxWorld.lmapLookup[k] and smodels.lmapTransforms[k] (section 3.2)
```

| Map | Instances | Volumes | LGV probes | 1x1x1 volumes | Atlas | Instances sharing a volume | lmapLookup |
|---|---|---|---|---|---|---|---|
| mp_m_speedball | 3,476 | 3,476 | 17,289 | 71 % | 64x64x8 | 0 (identity: k = i) | 0 |
| mp_hideout | 12,367 | 11,939 | 47,055 | 66 % | 64x64x16 | 241 on 49 volumes (32 `decor_drug_brick_01` of one stack share one box) | 236 |
| mp_shipment | 9,535 | 9,523 | 40,890 | 21 % | not decompressed | a few | 0 |

- The largest volume holds 64 probes (4x4x4, 4x16x1, ...). On hideout the 236 lightmapped instances use k = 0..235,
  all distinct, which is exactly `lmapLookupCount`. They overlap the volume index space, so a flag-2 index never means
  a volume (VERIFIED).
- `smodels.instances` (`GfxStaticModelInstance.lgvIndex_transientIndex`, S:12108) is null in all three exports. The VS
  reads an 8-byte per-instance record (t74, VS:247) holding a volume index and a zone, and a per-zone record (t0,
  VS:338/405) holding the zone's place in a runtime 3D atlas and its first volume. INFERRED: both are built at load
  from `modelStaticIndirection` and the zone atlas. The editor does not need them.
- Bounds check (`vt2.py`): the instance's collection-bounds centre, put through its volume's transform, lands in
  [-0.05, 1.05]^3 for 12,100 / 12,131 (hideout), 3,476 / 3,476 (speedball) and 9,535 / 9,535 (shipment) probe-lit
  instances. It lands at (0.5, 0.5, 0.5) +- 0.01 for 11,851, 3,469 and 9,521. The boxes are oriented with the model.

### 1.2 The box table, in the trzone export (VERIFIED)

`gfx_map_trzone` `fields.gpuLightGrid.staticModelVolumes` (S:12635, 64 bytes; ACTS writes `values` on small maps, hex
`bytes` on big ones):

| Offset | Field | Meaning |
|---|---|---|
| 0x00 | u32 firstProbe | running probe count (contiguous: speedball 0..17,289) |
| 0x04 | u16 m_volumeSize[3] | probes per axis |
| 0x0A | u16 flags | `LGV_SMODEL_FLAGS_AXIS_MASK` 3, `THINPUSH` 4 (speedball 39, hideout 744, shipment 21 volumes) |
| 0x0C | u32 singleProbe | = firstProbe for 1x1x1 volumes, else 0xFFFFFFFF |
| 0x10 | vec4 volumeTransform[3] | rows of a 3x4 world-to-box matrix: `coord = M * (p, 1)`, the box is [0,1]^3 |

`staticModelVolumeCount` is beside it. `SMLGppZone` is null on all three maps. `tempLightGridVolumeData` is null
(ACTS does not export the temp data).

### 1.3 The probe atlas, in the decompressed zone (VERIFIED)

Zone order (Z:4733-4766): `staticModelVolumes`, then `SMLGppZone` (null here), then the
`GfxLightGridVolumeAtlasDataTemp` root (S:12725, 240 bytes), then its pixels and its two parameter arrays.
To find it, search the `.dec` for the exported box table bytes; the root starts at the end of the table.
- mp_m_speedball: table at 0x4DF7F4F, root at 0x4E2E44F (of a 187,044,158-byte `.ff.dec`).
- mp_hideout: root at 0xB8A4484 (366,118,498 bytes).

| Root offset | Field | speedball | hideout |
|---|---|---|---|
| 0x00 | u32 m_widthAndHeight | 64 | 64 |
| 0x04 | u32 m_depth | 8 | 16 |
| 0x08 | u32 m_numVolumes | 3,476 (= staticModelVolumeCount) | 11,939 |
| 0x0C | u16 m_worldTransient | 0 | 0 |
| 0x10 | GfxImageRaw m_textures[0] (0x28 B) | format 32 R11G11B10F, flags 0x810003, 131,072 B, 64x64x8, 1 element, 1 level, pixels -2 | 262,144 B, 64x64x16 |
| 0x38 | GfxImageRaw m_textures[1] | format 6 R8G8B8A8, 786,432 B, **128x192x8** | 1,572,864 B, 128x192x16 |
| 0x60 / 0xA8 | m_packingParams / m_transformParams pointers | -2 (follow) | -2 |

The data follows at root + 0xF0, with no padding:
1. Texture 0: W x W x D x 4 bytes, x fastest, then y, then z.
2. Texture 1, the same order.
3. `LightGridVolumePackingParams[numVolumes]` (32 B, S:12707): vec3 uvScale, u8 volumeSize[3], u8 basisType (1 on
   every volume), vec3 uvBias, u32 firstStaticProbeIndex (0 on every volume; meaning unknown).
4. `LightGridVolumeTransformParams[numVolumes]` (64 B, S:12716): vec4 rows[3], equal to the exported volumeTransform on
   3,476 / 3,476 volumes, then the box flags and three zero words (runtime history state).

Packing (VERIFIED): `uvBias * (64, 64, D)` is the first probe's texel centre (volume 0: 62.5, 49.5, 6.5).
`uvScale * (64, 64, D)` is `volumeSize - 1` (0 for single probes). So probe (i, j, k) of a box is texel
`first + (i, j, k)`, and box coordinate 0 / 1 falls on the first / last probe centre. Boxes need no gutter: the VS
clamps the coordinate (dp4_sat, VS:402-404), so it never filters outside its own texels.

Texture 1 is six 64 x 64 x D tiles in a 2 x 3 layout: tile t sits at (64 (t & 1), 64 (t >> 1)) (VS:428-441).

### 1.4 From texels to irradiance (VERIFIED, VS:398-578)

```
coord = saturate(M * (P, 1))                      // P = the vertex's world position (VS:400-404)
uvw   = coord * uvScale + uvBias                  // VS:406-427 (plus the runtime atlas placement, not needed here)
DC    = t108(uvw).rgb                             // texture 0, R11G11B10F, trilinear
for tile t = 0..5:  q[4t .. 4t+3] = t109(uvw.x/2 + (t&1)/2, uvw.y/3 + (t>>1)/3, uvw.z) * 2 - 1    // UNORM8 -> -1..1, filtered
value k = 0..23:   channel c = k % 3, slot j = k / 3
sh_c[j] = q[k] * DC_c * (j in {0, 2, 3} ? sqrt(3) : sqrt(5))
E_c(n)  = 0.282095 DC_c
        + 4/9  * 0.488603 * ( sh_c[0] n.z - sh_c[2] n.y - sh_c[3] n.x )
        + 1/16 * ( 0.315392 (3 n.z^2 - 1) sh_c[1] - 1.092548 n.x n.z sh_c[4] - 1.092548 n.y n.z sh_c[5]
                 + 0.546274 (n.x^2 - n.y^2) sh_c[6] + 1.092548 n.x n.y sh_c[7] )
AMBIENT_COLOR.rgb = max(E(n), 0) * cb7[43].z      // n = world vertex normal (VS:507-576)
```

- In standard real-SH order with the signs of the converter's `EvaluateShBasis` (`iw3_lightgrid.cpp:269`: Y00, -y, z,
  -x, xy, -yz, 3z^2-1, -xz, x^2-y^2), slots 0..7 hold coefficients 2, 6, 1, 3, 7, 5, 8, 4. The band weights are
  1, 4/9, 1/16, the squares of the cosine-lobe factors 1, 2/3, 1/4. INFERRED: the stored SH is pre-convolved once, or
  the engine uses this as a softer lobe.
- The filtered normalised bytes are multiplied by the filtered DC (as the VS does). Bytes and DC are not
  pre-multiplied.
- The VS also fits an ambient fraction `a` (`AMBIENT_COLOR.w`) and a dominant direction (`HIGHLIGHT_DIR_VIS`) to the
  channel-mixed SH (VS:525-578; not transcribed). The PS (research 95) rebuilds
  `E(n) = AMB (1-a) sat(n.dW) / max(dW.Ngeom, .05) + AMB a`. At the geometric normal this equals `AMB` whenever
  `dW.Ngeom >= .05` (INFERRED from that formula). So for a flat normal `E(Ngeom)` is the whole term, as `C` is for the
  lightmap.
- **Thin push** (VS:344-372, only when `cb7[185].x == 0`): for a box with flag 4, axis `a = flags & 3`,
  `d = M[a] . (camera, 1)`. `M[a].w` gets `+4 sat(4(d-1))` when d > 1 and `-4 sat(-4d)` when d < 0. That pushes the
  whole model onto the box face on the camera's side, so a thin wall or sign shows the probes of the side being
  looked at (INFERRED purpose).
- Reference values for a decoder test (mp_m_speedball, box centre):

| Volume | Model | Texel | Raw DC | DC (r, g, b) | E(up) | E(down) |
|---|---|---|---|---|---|---|
| 0 | tm/foliage_grass_long_patch_02_a_02_reactive | (62, 49, 6) | 0x966584AC | 13.5, 14.0, 14.25 | 4.833, 5.320, 6.234 | 2.961, 2.801, 2.072 |
| 994 | tm/p7_tools_nail | (17, 40, 6) | 0x9D276CEE | 27.5, 27.25, 26.0 | 8.595, 8.777, 8.929 | 7.054, 6.767, 5.938 |

Volume 0's tile bytes: (151,158,177,137) (139,141,123,122) (123,119,120,121) (145,143,141,140) (139,137,119,120)
(121,145,145,143).

### 1.5 Decoded values (VERIFIED decode; plausibility INFERRED)

Box centres, luminance of E (Rec.709), before `cb7[43].z`:

| Map | Prop | Position | E(up) rgb | E(down) lum | Where |
|---|---|---|---|---|---|
| hideout | tm/building_roof_lip_01_white_16 | (264, -550, 360) | 5.56, 6.14, 7.49 | 4.09 | roof edge, open sky |
| hideout | tm/building_roof_lip_01_tan_64 | (-642, 750, 344) | 3.70, 3.95, 4.74 | 2.43 | roof edge |
| hideout | tm/ee_foliage_grass_flowers_lm_short_01_green | (1141, -513, 2) | 1.95, 2.46, 3.40 | 2.40 | ground |
| hideout | tm/military_ied_shell_01 | (100, 1259, 144) | 0.42, 0.30, 0.22 | 0.16 | under a roof (warm bounce) |
| hideout | tm/uk_metal_frame_warehouse_05 | (-4, 423, 117) | 0.09, 0.09, 0.08 | 0.15 | inside the warehouse |
| hideout | tm/me_accessory_dvd_single_03 | (-771, 1000, 85) | 0.10, 0.12, 0.10 | 0.36 | indoors |
| speedball | tm/p7_tools_nail | (682, 546, 82) | 8.59, 8.78, 8.93 | 6.77 | open |
| speedball | tm/com_junktire (stack) | (-552, -582, 51) | 0.01 | 0.01 | inside a tyre pile |

- **Indoor/outdoor test** (hideout, 800 random probe-lit props within 6,000 units of the centre; `cover.py` casts a
  vertical ray from the box centre against the world triangles, ignoring sky, clip and shadowcaster surfaces):
  - 417 props with nothing overhead: median E(up) 3.37 (quartiles 2.10 / 4.40).
  - 383 props with geometry overhead: median 0.71 (0.29 / 1.64).
- Speedball (mostly open) over all 3,476: median 4.82, 10th percentile 2.20, 90th percentile 6.53. Outdoor probes are
  blue-tinted (B > G > R) and brighter from above than below.
- 0.23 % (speedball) and 0.36 % (hideout) of probe-lit props decode to black (< 0.01): a single probe inside geometry
  (wall toppers inside a wall, conduits on a wall of a dark room). The game samples the same zero (INFERRED; it has no
  fallback in the VS).
- Units: the VS scales E by `cb7[43].z`, and the world PS scales the lightmap `C` by the same register (research 95,
  W:459-536). So LGV E and lightmap C share one unit and one `bakedLightScale` (VERIFIED). The magnitudes agree
  (INFERRED): open props 3.4 against lit floors of 2 to 3.6 (research 94); covered props 0.71 against hideout's
  lightmapped indoor rugs, whose texels average 0.7 to 1.7.

## 2. The other light grids (question 2)

### 2.1 The tetrahedral GPU light grid (`gpuLightGrid.gpuLightGrid`, all in the trzone export)

| Field (S:12602) | speedball | hideout | Notes |
|---|---|---|---|
| probeCount / probes (GfxSHCompressedProbeData, 32 B) | 56,809 | 133,791 | hex bytes |
| probePositions (vec3) | 56,809 | 133,791 | adaptive lattice: 32/64/128/256-unit steps (speedball), 16/32/... (hideout) |
| zones (92 B, S:12537) | 1 | 1 | numProbes, firstProbe, numTetrahedrons, firstTetrahedron, voxel fields, fallbackProbeData (29 halves) |
| tetrahedrons (u32 indexFlags[4]) | 308,126 | 734,365 | probe index in bits 0-30, bit 31 a flag (set on ~91 k / ~299 k) |
| tetrahedronNeighbors (u32[4]) | 308,126 | 734,365 | 0xFFFFFFFF = hull face (19,726 / 44,388) |
| tetrahedronVisibility (64 B) | 97,039 | 307,585 | `tetrahedronCountVisible`; light visibility (converter comment), not decoded |
| voxelStartTetrahedron (u32) | 2,756,114 | 2,992,236 | start tetrahedron of a walk per voxel; 0xFFFFFFFF = fallback probe |
| boundingVolume vec4[7] | | | 14-DOP: 3 axis + 4 diagonal planes as (normal, min, max). Speedball x, y in [-3072, 4096], z in [0, 1024] |

Probe record (layout as the converter writes it, `iw3_lightgrid.cpp:408-451`; the decode below is VERIFIED against
stock data):
- dword 0: DC as R11G11B10F (halves cut to 6/6/5-bit mantissas).
- Bytes 4 + 8c + (i - 1), i = 1..8: coefficient i of channel c in standard order, as `n * 127 + 127.5`, where
  `n = coef / (DC_c * (i < 4 ? sqrt3 : sqrt5))`.
- Byte 28: a visibility/trace byte (0 on 17 % / 30 % of probes, 255 on 23 % / 20 %).
- Byte 29: 255. Bytes 30-31: 0.

**L1 sign.** The converter's comment says stock probes store L1 negated. The stock data disagrees. Single-probe props
inside the grid were compared: the LGV against the grid interpolated at the same point (barycentric over the
containing tetrahedron, `run5.py`).
- hideout (193 props): DC correlation 0.925, median ratio LGV/grid 1.11. Literal (not negated) grid L1 against
  LGV L1: x +0.56, y +0.67, z +0.69. Cross pairs: -0.10 and 0.00.
- speedball (117 props): DC correlation 0.66, ratio 1.51. Ground-level grass is darker in the coarser grid. Raised props
  agree within about 10 %, for example a wall topper: LGV up/down 5.41/3.63, grid 5.35/2.50.

So the stock bytes decode **literally**, with the same basis signs as the LGV (INFERRED from data; the dynamic-model
shader was not disassembled). The negation in `PackProbe` may compensate for the IW3 source convention. That was not
checked; it is a fork question.

**Use.** VS bab2e306 binds no tetrahedron buffer (its resources are t0-t2, t63-t70, t74-t76, t102-t105, t108-t109;
VERIFIED). So static models never read this grid. It is INFERRED to light dynamic models: characters, script models
and weapons. To sample it, find the tetrahedron that holds P, then blend the 4 probes' SH by barycentric weights. The
game walks from `voxelStartTetrahedron`. On a CPU, a uniform grid of tetrahedron bounding boxes is enough (`gpugrid.py`).
Outside the hull, use `zones[0].fallbackProbeData`: 27 SH halves plus (1.0, 1.0); coefficient order not checked.

### 2.2 The texture light grid t102-t105 (not on MP)

VS:583-690 (and the world PS, research 95). It fills the weight `1 - w` that the LGV leaves.
- `w = 1` unless `cb7[194].x == 1`. Then `w` = the t103 mask at the world XY, faded to 0 beyond radius `cb7[194].w`
  from `cb7[107].xy` (a 192-unit ramp).
- Lookup:
  - P' = P + 30 N
  - uv = (P'.xy - cb7[189].xy) * cb7[191].xy
  - t102.xy gives a height range (remapped by cb7[193]), and zrel = sat((P'.z - lo) / (hi - lo))
  - t104(uv, zrel).xy gives two blend weights (remapped by cb7[192])
  - L1 SH = slices (0, 1) + w0 * slices (2, 3) + w1 * slices (4, 5) of the t105 array
- So it is a 2D grid with a height-relative vertical blend (VERIFIED arithmetic).

No GfxWorld field in the three MP exports can feed it (lightGridType SINGLE everywhere). INFERRED: a large-world
(Warzone) feature, where LGVs stay near the camera and the far props use this grid. Not decoded further.

## 3. Which props use which path (question 3)

### 3.1 Flags and materials (VERIFIED counts; letter meanings INFERRED)

| Model flags (S:12057) | Path | Material techset prefixes (smodels.materials) | hideout instances |
|---|---|---|---|
| 1 LIGHTPROBE (+4 ANIMATED_OPACITY, +8 REACTIVE, +16 SPLINED) | LGV (section 1) | `tmo` 673, `tm2o` 208, `tmco` 127, `tm2co` 41, `tmso` 8, `tmsco` 1 | 12,131 |
| 2 LIGHTMAP | lightmap (3.2) | `tml` 12, `tmlc` 5 | 236 |

- The `2` in `tm2*` is **SECONDUV** (a second texture UV set), **not** a lightmap. The curb `tm2co` techset
  (`MTL_TYPE_STATIC_MODEL_SECONDUV_VERTCOL_SELFVIS`) runs this LGV vertex shader (VERIFIED).
- `l` = `MTL_TYPE_STATIC_MODEL_LMAP[_VERTCOL]` (research 94 section 3), `c` = vertex colour, `o` = SELFVIS
  (`tmco` foliage is `..._VERTCOL_SELFVIS`, research 100), `s` presumably subdivision (INFERRED).
- speedball has only `tmo`, `tm2o`, `tmco` and `tm2co`; no static model there is lightmapped.

### 3.2 Lightmapped props (`tml/`)

- Per instance: `GfxWorld.lmapLookup[k]` (GfxModelLightmapInst, 20 B): lmapIndex, offset (u, v), scaleWidth,
  scaleHeight, a normalised rectangle in the world lightmap. Example, hideout [0]: lightmap 0, offset
  (0.7419, 0.2505), scale 0.0078125 (16 texels of 2048). `smodels.lmapTransforms[k]` is the same rectangle in fixed
  point (research 94 section 3).
- Per vertex (VERIFIED in the decompressed hideout zone): `XSurface.lmapCoords` (S:1943) is streamed inline after the
  XSurface's XSurfaceShared record (Z:1759-1770). It holds `vertCount` u32, followed by the rigid vertex lists.
  - Example `furniture_rug_01_flat_04_lod010v41`: XSurface at 0x1AB9174, flags 0x48, 1,733 vertices, 2,190 triangles,
    lmapCoords at 0x1AB9244. After 1,733 u32 comes the XRigidVertList {0, 1733, 0, 2190}.
  - Decoded as two unorm16: u in 0.025..0.645, v in 0.025..0.713 (1,214 distinct). As half2 they are NaN. So
    **PackedLmapCoords = (u16 u, u16 v) / 65535** (low half u).
- Mapping `lmUV = offset + uv * (scaleWidth, scaleHeight)`, no flip, in the world lightmap `lmapIndex`. Checked with
  the cached world lightmap on the 3 rug placements (k = 26, 30, 32; 22 x 22-texel rectangles):
  - (u, v) lands on a black texel for 0, 0 and 1.6 % of vertices.
  - (v, u): 4 to 10 %. (u, 1 - v): 19 to 25 %. The rectangle as a whole is 25 to 32 % black.
  - The mapping is INFERRED from this test; the tml pixel shader was not read.
- ACTS exports only the shared vertex buffer for xmodelsurfs (`.shared.bin`) and the GLB. Neither has lmapCoords
  (not in the shared buffer). They need the zone's XSurface table.

## 4. SELFVIS (question 4)

- **Where**: per vertex, in the model, shared by every instance. An XModelSurfs vertex is 20 bytes in the shared
  buffer (`x64-zt core/assets/xmodel_data.hpp:202-212`, `neutral/xmodel_surfs.cpp:148-158`):
  - +0: u64 position (3 x 21 bits, scaled by the surface bounds)
  - **+8: u32 self visibility**
  - +12: half2 UV
  - +16: u32 tangent-frame quaternion
- **Format** (VS:226-246, 692; matches `XSurfaceUnpackSelfVisibility`, `x64-zt core/shared.cpp:253-260`):

```
x = (b & 511) / 511 * 2 - 1,  y = ((b >> 9) & 511) / 511 * 2 - 1      // octahedral, z = 1 - |x| - |y|, folded when z < 0
dir = normalize(x, y, z)  (model space; the VS turns it by the instance rotation)
SELFVIS.xyz = dir * ((b >> 18) & 127) / 127          // length = visibility amount
SELFVIS.w   = ((b >> 25) & 127) / 127                 // cone value used by the PS polynomial
```

- A surface flag selects an animated override from t70 (VS:78-157, Mayhem self-vis; not used by static props).
- The PS (research 95) uses `AO = min(|SELFVIS.xyz| * vis(c, w), gtao)`, with
  `c = dot(normalize(SELFVIS.xyz), N) * .5 + .5`.
- **Values** (VERIFIED: raw buffers exported with `acts ... -a xmodelsurfs` and xpaks, read at +8, compared with the
  cached GLB normals; `selfvis.py`):

| Surface | Vertices | \|SELFVIS\| min / mean / max | w mean | dot(dir, normal) mean |
|---|---|---|---|---|
| military_shooting_targets_05_lod010v61 (flat board) | 141 | 0.65 / 0.95 / 1.00 | 0.17 | 0.89 |
| debris_junk_tire_01_lod000v41 (tyre) | 1,200 | 0.22 / 0.79 / 0.97 | 0.24 | 0.87 |
| foliage_grass_long_patch_02_a_reactive_lod011v60 | 664 | 0.06 / 0.36 / 0.91 | 0.27 | 0.84 |

  The direction is a bent normal (always on the normal's side). The amount drops in concave and dense parts.
- **ACTS GLB**: `atian-cod-tools src/core/acts/tools/mw19/mw19_mesh.cpp:180-206` reads +0, +12 and +16 (plus colour
  and second UV) and skips +8. The cached GLBs have POSITION, NORMAL, TEXCOORD_0 and COLOR_0 only (VERIFIED). Their
  `extras.sourceFlags` are 64 (0x40) on the board and the tyre and 69 on the grass. INFERRED: 0x40 marks a
  self-visibility surface in Replay's numbering (ACTS's `reference_types.hpp` gives 0x80, a different build).

## 5. Editor recipe (question 5)

### 5.1 Data to read

1. **Box table**: already cached, in `world_cache/<zone>/assets/gfx_map_trzone/<zone>.0.asset.json`,
   `fields.gpuLightGrid.staticModelVolumes` (values or hex) and `staticModelVolumeCount`.
2. **Atlas and packing**: from the `.ff.dec` the world cache already writes for the lightmap
   (`src/qtapp/world_cache.cpp` `startDecompress` / `lightmapActsEnded`). In the same pass, before the `.dec` is deleted:
   - Find the box-table bytes; the root starts after them. If `SMLGppZone` is not null, scan for the root's signature
     instead: u32 64, u32 depth <= 64, u32 numVolumes == count, textures {32, 64x64xD} and {6, 128x192xD}, pointers -2.
   - Copy 240 + W W D 4 + 2W 3W D 4 + 96 N bytes into a small cache file, for example `lgv.iw8rlg` (speedball 1.5 MB,
     hideout 3.7 MB; `savelgv.py` does this).
   - Warzone tiles: every streamed `_tr` zone has its own box table and atlas (`m_worldTransient` = the zone). Do the
     same per tile, keyed by `modelStaticIndirection.worldTransient`.
3. **Per instance**: extend `StockModelInstance` (`src/core/stock_smodels.h`) with `lightingIndex` and
   `worldTransient` (from `smodels.modelStaticIndirection`: values, or hex u32 + u16 + u16) and the model's `flags`
   (from `smodels.models[].flags`). The reader already walks collections in the same order.

### 5.2 Exact path (GPU, the game's own math)

- Upload texture 0 as a `Texture3D` `DXGI_FORMAT_R11G11B10_FLOAT` (W x W x D, raw bytes) and texture 1 as
  `R8G8B8A8_UNORM` (2W x 3W x D). Use a linear clamp sampler.
- Per prop instance, add to the instance stream: the 3 box rows (float4), `uvScale.xyz` + a kind (0 none, 1 LGV,
  2 lightmap, 3 per-instance SH), and `uvBias.xyz`. The instanced prop VS (`VSInInst`, render.cpp:353) already has
  `INSTROW0..2`; add `INSTROW3..7` or a structured buffer indexed by `SV_InstanceID`.
- In the VS, or the PS for normal-mapped response: run section 1.4 with the world position and the shading normal.
  - Per vertex matches the game (pass `E(Ngeom)` as a new interpolant).
  - Per pixel costs 7 taps and about 40 ALU, and also gives `E(R)` for the environment term.
- In ps_tex's stockRef branch, where no lightmap lit the point (render.cpp:559-562):

```
if (!lmOk && kind == LGV) {
    indirect = shE(N) * gameSkyScale.y;          // gameSkyScale.y = the map's bakedLightScale, as for the lightmap (cb7[43].z)
    Lr       = shE(R) * gameSkyScale.y;           // instead of indirect - stockB ... for the environment/probe relight
} else if (!lmOk && gameSky.w > 0.5 && probeCount.x > 0.5)
    indirect = stockProbeLight(i.w, N, 0.0) * luminance(gameSky.rgb);   // today's fallback, kept for props with no data
```

  The rest of the branch stays: kd, aoMB with the screen AO, probes relit by luminance(Lr), and the sun.
- Optional: the thin push (section 1.4) with the editor camera, for flag-4 boxes.

### 5.3 Simplest first step (CPU, per instance)

- Decode each box's SH at its centre (coord 0.5, 0.5, 0.5) on the CPU when the map loads. That is exact for every
  1x1x1 box (21 to 71 % of boxes; most small props), and loses only the gradient across big boxes (rocks,
  buildings, up to 4x4x4).
- Pass 27 floats per instance (or DC + L1, 12 floats, if L2 is dropped: L2 has weight 1/16). Evaluate `shE(N)` in the
  PS. No 3D textures needed.

### 5.4 Fallbacks

- **Lightmapped props (flag 2)**: they have no box. Simple: the mean of the non-black texels in their `lmapLookup`
  rectangle, as a flat ambient. Exact: per-vertex PackedLmapCoords (section 3.2) through the existing stock-lightmap
  path. That needs an XSurface-table reader on the `.dec`, or an ACTS export of `lmapCoords`.
- **Editor-placed props, stock props the user moved, and missing LGV data**: sample the tetrahedral grid (section 2.1)
  at the instance's bounds centre on the CPU. Same SH format and units, no decompress needed.
  - A moved stock prop keeps its stock box in the game unless the stock-edit build rewrites the box (the box is world
    space, and the VS clamps the coordinate). So the game would light it from its old place (INFERRED).
  - Which one the viewport should show for moved props is an owner decision.
- **Black boxes** (0.2 to 0.4 %): keep them as the game does (INFERRED). A debug overlay could mark E < 0.01.

### 5.5 SELFVIS (optional, needs an ACTS change, not made here)

- `mw19_mesh.cpp` would emit the u32 at vertex +8 as a custom attribute (for example `_SELFVIS`, UNSIGNED_INT or 4 x
  float) on surfaces with that flag. `glb_mesh` / `model_import` would carry it.
- The prop VS would turn its direction by the instance rotation. The PS would then multiply the indirect term by
  `min(|sv| vis(c, w), screenAO)` with research 95's polynomial.
- Without it, the screen AO alone stands in. For dense grass the self visibility is the bigger term (mean 0.36).

### 5.6 Checks for an implementation

- Decoder unit test against section 1.4's table (speedball volumes 0 and 994). The extracted blocks are in
  `out/agent/smodel_lighting/*_lgv.bin`, but tests use fixtures under `tests/data`, so copy a small cut there.
- Viewport check on mp_hideout with Game Lighting: warehouse and indoor props become dark and warm; roof props stay
  bright and blue. Compare against the owner's in-game shots (lit preview and game lighting, as usual).

## 6. Not established

- The `(a, dW)` fit of VS:525-578 (not transcribed; only needed for the PS's normal-mapped lobe).
- `firstStaticProbeIndex` (0 everywhere), `basisType` (1 everywhere), the tetrahedron flag bit, the visibility
  records and byte 28 of grid probes, the fallback probe's coefficient order.
- The band weights the dynamic-model shader applies to the tetrahedral grid (assumed the same as the LGV).
- The t102-t105 data source (Warzone) and `cb7[185]`, `cb7[194]` values on MP. INFERRED 0 and not 1 (no data to bind).
- The tml lightmap pixel shader. Its use of the direction plane for props is assumed to match the world shader.
- In-game appearance of any of this (no run).

## 7. Reproduce

Scripts in `out/agent/smodel_lighting` (Python 3 + numpy). Paths to the world cache are inside the scripts.

| Script | What it does |
|---|---|
| `savelgv.py <dec> <trzone json> <out>` | cuts the box table + atlas root + pixels + params out of a `.ff.dec` |
| `lgvdec.py` | the decoder: R11G11B10F, the 2x3 tile coefficients, trilinear `sample_sh`, `irradiance` (VS formula) |
| `atlas.py`, `run1.py` | root header, texture descriptors, packing / transform params against the export |
| `run2.py` | every instance's SH at its box centre: E up / down / side, darkest and brightest (writes `<map>_rows.json`) |
| `vt2.py` | instance bounds centre against its box (section 1.1) |
| `cover.py` | the indoor / outdoor ray test (section 1.5) |
| `gpugrid.py`, `run5.py`, `run6.py`, `gridstats.py` | tetrahedral grid decode, point location, LGV comparison, statistics |
| `run7.py`, `counts.py` | index spaces (lightmapped vs probe-lit), per-map counts |
| `lmcheck.py` | lmapLookup and the cached lightmap for the rug test (section 3.2) |
| `selfvis.py` | SELFVIS from an ACTS `.shared.bin` + the cached GLB |

ACTS command lines (output roots kept short; long paths fail):
- `acts --noUpdater fastfile -r mw19replay -g <replay exe> --oodle <oo2core dll> -d --noAssetDump -l -o <dir> <zone>.ff`
  writes `<zone>.ff.dec`, about 2x the `.ff`. The `.fp.dec` and `.fp.patched.dec` it also writes can be deleted; the
  patched one equals `.ff.dec`.
- `acts ... --xpak <each zone\*.xpak> -a xmodelsurfs -n <surfs names> -l -o <dir> <zone>.ff` writes the raw
  `.shared.bin` vertex buffers.

The decompressed zones and the xmodelsurfs exports were deleted after use.
