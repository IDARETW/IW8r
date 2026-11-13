# 103. Stock super-terrain layers: how Verdansk blends its ground materials near the camera

**Why.** The editor draws Verdansk (`mp_donetsk`) and Quarry (`mp_br_quarry`) super terrain with each surface's baked
`combinedAlbedoMap` (research 102), which is blurry up close. The game blends the real ground materials near the
camera. This note decodes that blend and adds a CPU path (`src/core/stock_terrain_layers.{h,cpp}`) that turns it into
layer batches.

**Verdict.**

1. **Weights are per-layer images, not per-vertex data.** Every layer L > 0 of a surface has a BC4 weight image,
   `mp_donetsk_layer_mask_<surface>_<L>`. It has one texel per height-map texel and is sampled bilinearly at the pixel's
   height-map position. These images are the 507 `layer_mask` entries of `flattenedImages`. Layer 0's mask is the
   builtin `$white`.
2. **The per-triangle `layerMask.data` u32 picks which layers the pixel shader evaluates**: bit L means surface layer L.
   The shader loops over the set bits from low to high and lerps each layer over the result so far. Layers whose bit is
   clear are skipped even when their mask is non-zero.
3. **The blend factor** is the mask value pushed through the same reveal ramp as the tw4 world layers (research 97).
   The ramp uses the layer's reveal texture (G channel) at the layer UV and k = `alphaRevealSoftEdge`. It is then
   multiplied by the alpha (R channel) of that texture.
4. **The switch to the combined albedo is per pixel**, at a camera distance of 17,368.23 inches times
   `r_st_combined_bias_high` (default 1.0, so about 441 m). Nodes beyond that distance are drawn with the
   `_combined_only` material.
5. The layer textures are the layer material's own images: colour, normal + gloss, alpha + reveal (and a displacement
   image). The layer UV is `stFromXYZ` applied to the world position.

**Labels.** VERIFIED = read in the Replay 1.20 disassembly (pixel shaders from `code_post_gfx.ff`, dvar registration
in `game_dx12_ship_replay.exe`) or checked against the exported data (numbers cited). INFERRED = from names, statistics,
or the decompiled *game-test* source (`E:\mw124\tools\dvar_search_web\src\combined\game-test-src-cleaned`), which
is a different build of the engine. The DXBC has no RDEF block, so register names are ours.

## How it was obtained

- Layer tables: the existing stterrain export (`world_cache/mp_donetsk/terrain/.../stterrain/mp_donetsk.0.asset.json`),
  read with `out/agent/stl/py/peek2.py` (surface fields and layer tables) and `nodes.py` (resident node meshes and
  per-triangle masks of surfaces 17 and 1).
- Layer masks: `acts ... -a image -n mp_donetsk_layer_mask_17_1,...` plus the `--xpak` list, on `mp_donetsk.ff`
  (streamed from `pak_magma1.xpak`; 27 images in about 1 s; 528,540 bytes each). Surface 1's 26 masks were exported the
  same way. The exports were deleted after the checks. To re-export, use the command in section 6.
- Terrain materials and techsets: `-a material` / `-a techset` on `code_post_gfx.ff`. The material names come from the
  exe's string table (`twh/lit_superterrain` ... 44 names). Shaders: `-a pixelshader,vertexshader` on
  `code_post_gfx.ff`, disassembled with `out/agent/shader_research/py/dxbc_dis.py`. Copies are in
  `out/agent/stl/asm/` and the techset JSON is in `out/agent/stl/ts2/`.
- Layer materials: `-a material -n mc/...` on `techsets_mp_donetsk.ff` (`out/agent/stl/mc_techsets_mp_donetsk/`).
  They are not in `mp_donetsk.ff`.
- Dvar defaults: `out/agent/stl/py/dvarfind.py`. It finds the obfuscated dvar name strings in the exe (names from
  `dvar_comments.txt`) and disassembles the `Dvar_RegisterFloat` call that references them.

| Material (code_post_gfx) | materialType | Lit PS | Use |
|---|---|---|---|
| `twh/lit_superterrain` | `MTL_TYPE_SUPERTERRAIN` | `5a4c3a9f` | layered, loop mask = the node's `layerMask` |
| `twh5/lit_superterrain` | `..._VERTEX_LAYER_MASK` | `afc8b731` | layered, loop mask = the triangle's `layerMask.data` |
| `twhx/...`, `twh5x/...` | `..._GPUGEN`, `..._GPUGEN_VERTEX_LAYER_MASK` | `5a4c3a9f` / `afc8b731` | the same, for 33 x 33 generated nodes (VS `a30b8ffa`) |
| `twh/lit_superterrain_combined_only` | `MTL_TYPE_SUPERTERRAIN` | `0c2d0e67` | combined albedo only |
| `twh3/..._combined_only` | `..._3D_VERT_POS` | | root nodes (stored heights) |
| `*l/` (`twhl`, `twh5xl`, ...), `*_cutout`, `*_tess` | | | lightmapped, cut-out triangles, displacement |

## 1. Layer data (VERIFIED from the export)

Per surface (`StDiskTerrainSurface`):

| Field | Content |
|---|---|
| `layerMaterials[L]` (`StTerrainMaterialLayer`, 224 bytes) | `material` (e.g. `,mc/dirt_soil_01`), `materialTextureIdxs[4]` (u16 into `flattenedImages`, 0xFFFF = none), `stFromXYZ[2]`, `layer` (= L on all 529 layers), `decalSort`, `himipRadiusInvSq`, `layerData` (`HLSL_TerrainLayerDataCode`: `textureTransform` = `stFromXYZ`, `flags`, `alphaRevealSoftEdge`, `textureScale`, `colorTint`, grass fields) |
| `layerMaskMapIdxs[L]` (u16) | index into `flattenedImages`: 0 = `,$white` for layer 0 on every surface; `mp_donetsk_layer_mask_<s>_<L>` for the others; `,$black` (index 238) for 7 unused layers |
| `combinedMapStreamRadiusInvSq` | 4.72e-9 (14,549 inches) or 1.18e-9 (29,097 inches) on the 65,536-inch surfaces: the streaming radius of the combined maps, not the switch distance |

`flattenedImages` (668 entries): index 0 `$white`, 1-508 the layer masks (507 images, plus `$black` at 238), 509-667
the layer textures, named `<image>~<hash>` as ACTS prints them (for example `dirt_soil_01_c&defaultspec~6923169343254932927`).
The editor's material cache stores these images as `<name with & and ~ replaced by _>.dds`.

`materialTextureIdxs` (VERIFIED in the game-test `R_ST_SetupMaterialLayerInternal`, and in the data: the layer
material `mc/dirt_soil_01` lists the same images at slots 0 and 9):

| Index | Image | Material slot of the `mc/` layer material |
|---|---|---|
| 0 | colour, spec code in alpha (`*_c&defaultspec`) | 0 |
| 1 | normal + cavity + gloss (`*_n&*_o&*_g`) | 9 |
| 2 | alpha (R) + reveal (G) (`white&*_r`, `grass_winter_01_c&grass_winter_01_r`) | 27 |
| 3 | displacement (`grass_frozen_01_h`) | - |

The `mc/` materials (`MTL_TYPE_MODEL_VERTCOL`, techsets `mc/lit_3_lit_*`) carry the soft edge in psData too: on
`mc/grass_winter_01_rev_top`, row 2 byte 3 is 0x26 = 38, which is `alphaRevealSoftEdge` 0.15 x 255 (VERIFIED).

Verdansk layer flags: 0x10247 x 411, 0x243 x 60, 0x102c7 x 31, 0x10647 x 27, 0x241 x 8, 0x2c3 x 3, 0x643 x 1.
No layer has 0x20000. Tints are white on 529 of 541 layers.

## 2. The layer masks (VERIFIED)

- GfxImage `mp_donetsk_layer_mask_<s>_<L>`: DDS DX10, **DXGI 80 (BC4_UNORM)**, `paddedResolution`² texels (1028² on a
  1025-texel surface), 1 mip. Texel (i, j) lies at the height texel (i, j): world `origin.xy - halfWldSize + (i, j) *
  surfaceMapWorldSize`, with row 0 at -y. The padding replicates the edge.
- How the shader samples it (PS `afc8b731` lines 74-77 and 199; `out/agent/stl/asm/afc8b731...4.txt`): `uv = (offToSurfaceMapX/Y . (camRel.xy, 1)) *
  CB3[202 + surfaceMapSetIndex].z`, then `sample_l` at LOD 0 and `.x`. The front end (game-test
  `R_ST_ComputeOffsetToSurfaceMap`) builds `offToSurfaceMap` as `(world - origin + half) / (mapResolution *
  texelSize) + 0.5 / mapResolution`. So texel i is sampled at its centre exactly at grid point i. The `.z` scale is
  INFERRED to be `mapResolution / paddedResolution`.
- Orientation, checked on the 39,941 resident triangles (levels 3 to 5) of surface 17 against their `layerMask.data`
  bits:

| Mask orientation | Triangles whose bits equal "mask > 0 in the triangle's texel box" |
|---|---|
| as stored (row 0 = -y) | 28.6 % exact; frequencies and per-bit correlation below |
| flipped in y | 1.5 % |
| transposed | 1.5 % |

- **Bit L is layer L** (VERIFIED by statistics). Over the same triangles, how often each bit is set matches how often
  layer L's mask is non-zero (layer 1: 0.75 / 0.77, 8: 0.97 / 0.97, 10: 0.98 / 0.99, 23: 0.96 / 0.96, 28: 0.14 /
  0.15). Every bit L has its highest phi correlation with layer L (0.56 to 0.95). Bits 5 and 12 (black or empty masks)
  are never set.
- Exact equality is only 29 % because the box test is not the generator's rule. The mismatches are neighbours: the
  median distance from an extra bit to the nearest non-zero texel is 2.2 texels. INFERRED rule: a bit is set when the
  layer has weight inside the triangle and is not hidden by later layers. **Bit 0 is cleared on 12,711 triangles; 91 %
  of them are covered at every texel by some layer at weight 1**, while 88 % of the triangles that keep bit 0 are not
  covered.

## 3. The pixel shader blend (VERIFIED, PS `afc8b731` lines 65-350: loop 185-345, per-triangle mask line 78)

Inputs: `v1.xyz` = world position minus the camera (`CB1[2][4]` = camera position), `SV_PrimitiveID`. T13 (stride 172)
holds the tile data: `flags` at 0, `layerMask` at 4, `offToSurfaceMapX/Y` at 48 / 64, `layerOffset` (low 16 bits) at
152, `surfaceMapSetIndex` (bits 16-18) at 168. T12 (stride 108) holds the layer data, packed by
`HLSL::TerrainLayerData::SetFrom` (game-test `superterrain_cshared_pack.hlsl`):

| Offset | Content |
|---|---|
| 0, 16 | `textureTransform[0]`, `[1]` (= `stFromXYZ` rows) |
| 32 | `alphaLayer` = the mask image's bindless index (from `layerMaskMapIdxs[layer]`) |
| 36, 40 | `textureScale`, `detailScale` (half2) |
| 44 | `flags & 0xFFFFFF` \| `min(255, int(255 * alphaRevealSoftEdge)) << 24` |
| 48 | `normalMapPackedScaleBias` \| `detailScaleHeight` byte << 24 |
| 52, 56, 60, 64 | colour, tint mask, normal, reveal bindless indices (24 bits) |
| 84 | `albedoMapPackedScaleBias` (spec remap) \| half displacement scale << 16 |

```
P      = v1 + camera                                           // world position
if (tile.flags & 16) and !(tile.flags & 32):                   // flag meanings INFERRED
    combined = dot(v1, v1) >= (17368.228516 * CB3[210].w)^2    // CB3[210].w INFERRED = combinedBias
else combined = (tile.flags & 16) != 0
if combined: albedo = combinedAlbedo(uvMap'), normal/gloss from combinedNog   // uvMap' = (P - origin + half) / (2 half), INFERRED
else:
  colour = 0; normal = geometric; gloss = 0; cavity = 0
  mask = twh5: layerMaskBuffer[SV_PrimitiveID]   (T5, u32 per triangle)
         twh:  tile.layerMask                     (the node's layerMask)
  for each set bit L of mask, lowest first:
    D    = layerData[tile.layerOffset + L]
    uv   = (D.row0 . (P, 1), D.row1 . (P, 1))
    w    = maskImage[D.alphaLayer].SampleLevel(uvMap, 0).x
    b    = w
    if L != 0:
      ramp = 1
      if flags & 0x10000:                                       // alpha + reveal image
        (a, r) = revealImage.Sample(uv).xy                      // R alpha, G reveal
        if flags & 4: ramp = saturate((r - lo) / (hi - lo)), lo = sat(1 - w - k sqrt(w)), hi = sat(1 - w + k sqrt(1 - w)),
                      k = flags >> 24 / 255; b = 1          // (fast sqrt 0x1fbd1df5, fast rcp 0x7ef311c2, as research 97)
        b *= a
      if flags & 0x20000: b = b > 0.501961 ? 1 : 0
      b *= ramp
    s    = (flags & 0x400) ? 1 / (1 + 16 n), n = floor(sqrt(|row0|^2 * |v1|^2)^(1/2) / 4) (blended over the last fifth) : 1
    c    = colourImage.Sample(uv * s);  c.a = (c.a * (bias0 + 1) + bias1) / 256      // spec code, D+84 bytes
    colour = lerp(colour, c, b)
    nT   = normalImage.Sample(uv * s): gloss .x remapped by D+48 bytes 0..1, normal .yw scaled about 0.5 by (byte 2 + 1)/256,
           hemi-octahedral; tangent frame = stFromXYZ rows made orthogonal to the geometric normal
    normal = lerp(normal, nT, b); gloss = lerp(gloss, ., b); cavity = lerp(cavity, nT.z, b)
```

- The loop always starts from black. When a triangle's mask has no bit 0, the first layer it lists is what shows
  where that layer's weight is below 1. The generator clears bit 0 only under full coverage (section 2), so this is
  rarely visible.
- The PS never reads `colorTint` (no layer-data load past offset 84).
- `flags & 0x400` keeps scale 1 within `16 * 16 / |row0|` = 2,048 inches at 1/128 per inch. Farther away it shrinks
  the UV (anti-tiling). Only cliff layers have it (0x10647).
- After the loop come the decal-volume loop and the lighting, which are the same as the world shaders (research 95, 98).

## 4. Where the game switches to the combined albedo

| Level | Rule | Status |
|---|---|---|
| Pixel | layers when `|P - camera|^2 < (17368.228516 * bias)^2`, else the combined maps | VERIFIED literal (PS `afc8b731` line 135); bias register INFERRED |
| Node | `R_ST_PopulateDrawSurfsForNode`: the `_combined_only` material when the node's bounds are at least `sqrt(301655360) * combinedBias` = 17,368 inches away, when the node has stored heights (`vertPos3d`, the root nodes), or when the layer textures are not streamed in yet | INFERRED (game-test decompile) |
| Surface | `R_ST_CombinedMapUsageForSurface`: ALL beyond the same distance, NONE when the whole box is inside it, else per node | INFERRED (game-test) |
| bias | `r_st_combined_bias_high`: `Dvar_RegisterFloat` default **1.0**, range 0..8 (exe RVA 0xEDBC61..0xEDBC92). `r_st_combined_bias_low`: default **0.5** (0xEDBC29..0xEDBC5C), used under `CL_TransientsWorldMP_UseLowAlwaysloadedFlagging`. Both are divided by the ADS zoom factor (`invFovScale`) | VERIFIED defaults; selection INFERRED |

So the game draws the real layers up to **17,368 inches (about 441 m)** from the camera, or 8,684 in low-memory mode.

## 5. Real data: Verdansk around (-24900, -1700), a grass field

The point lies in surface 17 (origin (-24576, 4096), 16,384 inches, 16-inch texels, 29 layers), 2,400 inches north of
surface 1. Surface 1 has the same first 15 layer materials. All numbers come from the `stock_terrain_layers_real_data`
test (`out/agent/stl/real.log`), with a 6,000-inch radius.

**At the point itself**: layer 28 `mc/gravel_medium_01_rev_top1` has weight 1.0 (the point is on a railway bed),
layer 8 `mc/grass_brown_02_dec` 0.996, layer 10 `mc/grass_brown_dry_01_rev_02` 0.387, layer 23 `mc/dirt_soil_01_dec`
0.004. In the 1,000-inch box around it, layer 8 has a mean weight of 0.79 and covers 86 % of the box, and layer 7
`grass_winter` 0.23.

**Dominant layer** (largest `w_L * prod_{K>L}(1 - w_K)`) over the 12,000-inch square, from
`out/agent/stl/dominant_layer_mp_donetsk_-24900_-1700.png`: `mc/grass_brown_02_dec` 43.2 %, `mc/grass_winter_01_rev_top`
11.5 %, `mc/grass_brown_dry_01_rev_02` 9.5 %, `mc/dirt_rocky_pebbles_ee_01_rev` 4.6 %, `mc/dirt_soil_01_dec` 4.1 %,
gravel 6.1 %, base `mc/dirt_soil_01` 2.4 %. `out/agent/stl/albedo_vs_dominant_s17.png` sets the surface 17 part of the
combined albedo next to it: the railway beds (gravel), the two grey pads, the dirt tracks and the green patches line up.
The paved roads in the combined albedo are not in the masks. They are INFERRED to be world geometry baked into the
albedo. A Python decode of the masks agrees with the C++ dominant layer at 290 of 291 random points.

Layer sets at stride 1 (every height texel), default culling (`minContribution` 4/255):

| Surface | Base triangles | Layer sets | Layer triangles | Build |
|---|---|---|---|---|
| s17 | 655,412 | 25 | 2,486,404 (3,381,001 with cover culling only; 2,192,756 at 16/255) | 337 ms |
| s1 | 223,258 | 16 | 773,137 (1,053,281 cover only) | 98 ms |
| s17, stride 2 | 163,936 | 25 | 680,376 | 79 ms |
| s1, stride 2 | 55,818 | 16 | 210,072 | 23 ms |

Largest s17 sets (stride 1): L8 `grass_brown_02_dec` 514,063 triangles (mean w 0.74, 47.7 % of vertices at 1), L10
`grass_brown_dry_01_rev_02` 519,364 (mean 0.30), L7 `grass_winter_01_rev_top` 266,510 (mean 0.62), L1
`dirt_soil_01_b_dec` 153,926 (0.66). **A 6,000-inch radius at stride 1 is too many triangles** (3.1 M on s17 alone). Use
stride 2 there, or a radius of about 3,000 at stride 1 (a quarter of the count).

## 6. Editor path (`src/core/stock_terrain_layers.h`)

```cpp
StockTerrainLayerTable table;
parseStockTerrainLayers(stterrainJsonText, table);              // flattenedImages, layer tables, mask names
stockTerrainLayerMaskImageNames(table, surface);                // -> acts ... -a image -n <names> --xpak ...
readStockTerrainLayerMaskDds(path, masks[L]);                   // BC4 -> 8-bit, masks indexed by layer
StockTerrainMesh mesh;  buildStockTerrainMesh(...);             // research 102 (any stride)
StockTerrainLayerPatchOptions o;  o.centerX = x; o.centerY = y; o.radius = 6000;
StockTerrainLayerPatch p;  buildStockTerrainLayerPatch(terrain, table, mesh, masks, o, p);
// p.base: the kept triangles (material = layer 0, uv = layer 0 UV, p.albedoUv), then p.layers in order:
//   set.mesh (same positions / normals, uv = the layer's own UV, idx a subset of the base triangles), set.weight
//   (per vertex mask value in [0, 1]), set.revealRamp / revealK / alphaReveal / hardEdge, set.material,
//   set.colorImage / normalImage / revealImage.
```

- Emit `p.base` as the base batch with material `p.baseMaterial` (layer 0, `mc/dirt_soil_01` on every Verdansk
  surface), and each `p.layers[i]` as a layer batch over it, in order. Weight = `set.weight` (tangent length 1 + w).
  When `set.revealRamp`, use the reveal map `set.revealImage` (G) with k = `set.revealK`; the arithmetic is the tw4
  ramp already in `render.cpp`. When only `alphaReveal`, weight * R. Otherwise the weight is used directly (0x243 /
  0x241 layers such as `mc/asphalt_crack_01`). Then switch to the combined albedo beyond 17,368 inches, or at the
  patch radius.
- Exact with respect to the game, up to two things: the weights interpolate linearly over each triangle instead of
  bilinearly per pixel (exact at the vertices; stride 1 vertices are the mask texels), and the triangle sets come
  from the masks, not from the game's `layerMask.data` (whose generator rule is INFERRED).
- Culling, INFERRED: a layer's triangle is dropped when every vertex is under a later covering layer at weight 1, or
  when its largest vertex contribution `w * prod_{later covering K}(1 - w_K)` is below `minContribution` (4/255).
  A covering layer has no alpha image, or an alpha channel that is `white&`. With `cullHidden = false` every
  triangle with weight is kept.
- Several surfaces can lie within the radius (`stockTerrainSurfacesNear`, smallest first). Build one patch per surface
  and use the cutout map as the base mesh does, so the overlapping periphery surfaces stay hidden.
- Mask export (about 1 s per surface, 0.5 MB per mask on 1025² surfaces, 2 MB on 2049²):
  `acts.exe --noUpdater fastfile -r mw19replay -g <game_dx12_ship_replay.exe> --oodle <oo2core_7_win64.dll> --xpak <every
  zone/*.xpak over 64 KB> -a image -n mp_donetsk_layer_mask_17_1,...,mp_donetsk_layer_mask_17_28 -l -o <short dir>
  zone/mp_donetsk.ff`.

**Materials the viewport must resolve** (all ordinary `MTL_TYPE_MODEL_VERTCOL` materials in `techsets_mp_donetsk.ff`;
slots 0 colour, 9 normal, 27 alpha + reveal), around the test point: `mc/dirt_soil_01` (base), `mc/dirt_soil_01_b_dec`,
`mc/dirt_rocky_pebbles_ee_01_dry_rev`, `mc/dirt_mud_footprints_02_flipped_wet_rvl`, `mc/mud_dead_grass_a_rev`,
`mc/dirt_rocky_pebbles_ee_01_rev`, `mc/grass_winter_01_rev_top`, `mc/grass_brown_02_dec`, `mc/cp_town_grass_03_rvl2`,
`mc/grass_brown_dry_01_rev_02`, `mc/dirt_roots_01_dsp_rvl`, `mc/anh_dirt_dry_compacted_01_light_rvl`, `mc/cliff_01`,
`mc/leaves_fallen_dead_01_autumn_rvl`, `mc/leaves_fallen_dead_01_rvl`, `mc/asphalt_crack_01`,
`mc/cement_ground_patchwork_cracked_wet_rvl`, `mc/asphalt_crack_01_wet_rvl`, `mc/dirt_mud_03_rev`,
`mc/dirt_soil_01_dec`, `mc/grass_hay_02_rev`, `mc/water_puddle_muddy_01_rvl_ssr`, `mc/gravel_medium_01_rev_top1`, and
on surface 1 also `mc/snow_powder_03_rev`, `mc/snow_slush_02_rvl`, `mc/asphalt_crack_dark_01_wet_rvl`,
`mc/asphalt_crack_01_puddle_rvl`, `mc/dirt_rocky_gray_01_wet_rev`, `mc/gravel_medium_01_rev`,
`mc/grass_dirt_twigs_leaves_01_rvl`. Alternatively, load the images named in the set (`colorImage`, ...) directly.

## 7. Tests

`tests/stock_terrain_layers_tests.cpp`:

- Parse: ACTS-shaped JSON with value and hex index arrays, builtin masks, and flags.
- BC4: both palettes; a DDS with partial edge blocks; R8; errors.
- Blend arithmetic: the ramp, alpha, hard edge, layer 0, contributions, UV.
- Patch on a synthetic 8 x 8-cell surface: triangle counts with and without culling, weights, UVs, the dominant
  counts, the radius, missing-mask warnings.
- An opt-in real-data run: `IW8R_STL_VERIFY=<mask dir>` (plus optional `IW8R_STL_JSON`, `IW8R_STL_IMAGES`,
  `IW8R_STL_POINT="x y r"`, `IW8R_STL_OUT=<file>`). It reads the editor's world cache and prints section 5.

Full suite: 141,991 checks, 0 failures.

## 8. Open items

- Quarry (`mp_br_quarry`) was not checked. Its stterrain has the same structure (research 102), so the same
  `mp_br_quarry_layer_mask_<s>_<L>` names are expected.
- The generator rule for `layerMask.data` (section 2), the meaning of the tile flags 16 / 32, and the register that
  holds `combinedBias` are INFERRED.
- Layer flags 1, 2, 0x40, 0x80, 0x200 are set but not read by the lit PS. They are probably read by the clutter /
  displacement / VS paths.
- The far-UV scaling of 0x400 layers and the per-pixel bilinear weights are not reproduced. Neither matters within
  2,048 inches at stride 1.
