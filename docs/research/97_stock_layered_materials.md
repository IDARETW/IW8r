# 97. Stock layered world materials (tw4 / twc4): how Replay 1.20 blends their layers

**Why.** The viewport draws stock terrain and other layered world surfaces with one colour texture. In game they blend
up to 8 texture layers per pixel, so the ground (dirt, grass, rock on mp_br_tut) looks flat and pale in the editor.
This note reads the blend from the shipped shaders and the data the editor needs to reproduce it.

**Labels.** VERIFIED = read in the disassembly, the techset arguments or the exported data (cited). INFERRED = from
usage, statistics or the converter fork, not from a shader. The DXBC has no RDEF block, so register names are ours.

## How it was obtained

- Shaders: ACTS `-a pixelshader,vertexshader -n <hash>` on `techsets_mp_backlot2.ff` and `techsets_mp_br_tut.ff`,
  disassembled with `out/agent/shader_research/py/dxbc_dis.py` (research 95). Work files: `out/agent/layers/`
  (`sh/` backlot2, `shb/` and `shc/` br_tut; `*.asm.txt` are NUL-stripped copies).
- Materials and techsets: ACTS `-a material,techset` on `techsets_mp_br_tut.ff` into `out/agent/layers/brtut/`. The
  12 techsets of 5 to 8 layers fail to export ("Cannot write Replay asset"); every 2 to 4 layer set exports.
- World data: the editor's world cache of mp_br_tut (`world_cache/mp_br_tut`, gfx_map + gfx_map_trzone), read by
  `out/agent/layers/py/survey.py`, `colours.py`, and by the core reader through `out/agent/layers/verify/verify_layers.cpp`.

| Shader | Techset (lit technique `TECHNIQUE_LIT_FORWARDPLUS_BITMASK_*`) | Layers |
|---|---|---|
| VS `e2fa6708`, PS `083b8e24` | `twc4/lit_3_lit_rpl_ta1_802000_1002000000000010_0_2_2_..._1_0_13810015b_0_0_3_0_0` (backlot2; br_tut `twc4/12n_13n`) | 2 |
| VS `61edd874`, PS `e3c5849f` (backlot2) / `b5991c2a` (br_tut) | `twc4/..._802000_..._0_3_3_..._2_0_13810015b_0_0_3_0_0` (br_tut `twc4/12n_13n_35n`) | 3 |
| VS `c7a4bde7`, PS `8a014b3c` / `e42c6d75` | `twc4/..._0_4_4_...` (br_tut) | 4 |
| VS `8488a27d`, PS `9e8dd712` | `tw4/lit_3_lit_rpl_ta1_804400_..._0_2_2_..._0_1_3_0_0` (br_tut `tw4/4n_18n`) | 2, no vertex colour |
| VS `45c84098`, PS `3430ea7f` | `twc/lit_3_lit_rpl_ta1_802400_..._0_1_1_...` (br_tut `twc/dirt_rocky_gray_01`) | 1, vertex colour |

The two 3-layer PS hashes differ only in the lightmap path (br_tut's reads an extra `t113` array); the layer code is
the same.

## 1. Vertex streams (VERIFIED, VS e2fa6708 / 61edd874 / 8488a27d)

The VS reads the surface's `GfxWorldSurfData` (structured t13, 88 bytes) at byte 4 (layerCount, xyzOffset,
tangentFrameOffset, lmapCoordOffset) and byte 20 (colorOffset, texCoordOffset, normalTransformOffset[0..]), and the aux
stream (raw t15) per vertex id `j`:

| Output | Source |
|---|---|
| `TEXCOORDS0.xy/.zw`, `TEXCOORDS1.xy/.zw` | float2 per layer at `texCoordOffset + 8 * layerCount * j` (layer L at `+ 8L`): **every layer has its own UV stream**, not a transform of layer 0 |
| `WEIGHTS.xyzw` (twc4 only) | colour bytes at `colorOffset + 4j`: `.x = byte0/255` (R), `.y` byte 1 (G), `.z` byte 2 (B), `.w` byte 3 (A). **0 when colorOffset is 0** (`movc ..., 0`) |
| `NORMAL_TRANSFORM1..` | 4 bytes at `normalTransformOffset[L-1] + 4j`, each `byte * 2/255 - 1`: a 2x2 matrix (identity `0xFF8080FF` when the offset is 0) |
| `NORMAL`, `TANGENT` | the packed tangent frame (as single-layer surfaces) |
| `LMAPCOORDS` | float2 at `lmapCoordOffset + 8j` (research 94) |

- **Layer L's weight is colour byte L** (VERIFIED for L = 1..3): the 2-layer PS reads only `v1.y`, the 3-layer PS
  `v1.yz`, the 4-layer PS `v1.yzw`. Byte 0 (R) is never read by a layered PS.
- tw4 (`MTL_TYPE_WORLD_LAYERED`) has no WEIGHTS output; its PS has the weight compiled in as 1 (`mul r5.w, k, 0`
  makes the reveal ramp a step at 0). Every tw4 surface on br_tut has colorOffset 0, every twc4 surface has a colour
  record (VERIFIED, survey). So "white when absent" gives tw4 its shader weight of 1.
- **5 to 8 layers**: the colour record is **8 bytes per vertex** (VERIFIED: `(texCoordOffset - colorOffset) /
  vertexCount` is 4.0 for every surface of 1-4 layers and 8.0 for every surface of 5-8 layers, 20 of 20). Bytes above
  `layerCount - 1` are 0 on every vertex and byte 0 is 255 on every vertex of those surfaces (colours.py), so byte L is
  layer L's weight there too (INFERRED: their shaders could not be exported).
- The layer normal transform rotates/mirrors a layer's tangent-space normal into layer 0's frame: br_tut surface 272
  (`twc4/14n_17n`) stores `008080ff` = ((-1, 0), (0, 1)) and its layer 1 u runs opposite to layer 0's (VERIFIED in data).

## 2. The blend (VERIFIED, PS 083b8e24 lines 104-292; the same per layer in e3c5849f and 8a014b3c)

Texture slots (VERIFIED, `MTL_ARG_MATERIAL_TEXTURE` of the 2-layer lit technique: t1 = slot 0, t4 = 1, t2 = 9, t5 =
10, t6 = 28, t3 = 59, t7 = 60; the 3/4-layer shaders continue the pattern and br_tut's 8-layer material lists slots
0-7, 9-16, 28-34, 59-66):

| Layer L | colour (+ spec code in alpha) | normal + gloss + cavity | alpha + reveal | tint mask |
|---|---|---|---|---|
| slot | L | 9 + L | 27 + L (L >= 1) | 59 + L |

Per-layer constants (VERIFIED: the shader reads `cb3[1+3L]`, `cb3[2+3L]`, `cb3[3+3L]`; `cb3` is the material's
`constantBufferTable[].psData`, 16 x (1 + 3N) bytes; the `MTL_ARG_MATERIAL_CONST` args put constant-table index L in
row 1+3L, 9+L in row 2+3L, 54+L in row 3+3L, 72 in row 0):

| Row | Content |
|---|---|
| 0 | `.x` float (= the layer count on every material read; meaning INFERRED), `.y` bit 0 reflection probes, `.zw` as research 95 |
| 1 + 3L | `.x` **flags**; UV transform `.y` half2 (cos, sin), `.z` half2 scale, `.w` half2 offset (as research 95) |
| 2 + 3L | `.x` bytes 0..2 tint RGB/255, **byte 3 = reveal sharpness k x 255**; `.y` thermal (ignore) |
| 3 + 3L | `.x` spec remap `s = a(b0+1)/256 + b1/256`; `.y` gloss remap (c0, c1) and normal strength `(b2+1)/256` |

Layer 0 is decoded exactly like the single-layer world PS (research 95: tint and tint mask, spec code, gloss, normal).
Then, for each layer L = 1 .. N-1 **in order, over the result so far**:

```
uvL     = transform_L(TEXCOORDS_L)                          // row 1+3L, own UV stream
w       = WEIGHTS[L]                                        // colour byte L / 255 (tw4: 1)
(a, r)  = (flags & 24) ? tex[27+L](uvL).xy : (0, 0)         // red = alpha, green = reveal
lo      = saturate(1 - w - k*sqrt(w))                       // k = row 2+3L byte 3 / 255
hi      = saturate(1 - w + k*sqrt(1 - w))
ramp    = saturate((r - lo) / (hi - lo))                    // shader: fast sqrt (asint>>1 + 0x1fbd1df5) and rcp (0x7ef311c2 - asint)
b       = ((flags & 16) ? ramp : w) * ((flags & 8) ? a : 1)
if b >= 1/255:
    cL  = tex[L](uvL).rgb * tint_L                          // or lerp(1, tint, tex[59+L].r) when flags & 64
    sL  = tex[L](uvL).a remapped by row 3+3L .x
    mode = (flags >> 7) & 3
      0: albedo = lerp(albedo, cL, b);  s = lerp(s, sL, b)
      1: albedo = albedo * lerp(1, cL, b)                   // multiply (s unchanged)
      2, 3: albedo unchanged                                // (a dead soft-light path exists in the bytecode)
    nmode = (flags >> 9) & 3                                // 2: normal, cavity and gloss below unchanged
      nL  = decode(tex[9+L]) (hemi-octahedral .yw, strength row 3+3L), then nL.xy = (dot(nL.xy, T.xy), dot(nL.xy, T.zw)), T = NORMAL_TRANSFORM_L
      normal.xy = (flags & 1) ? (nmode==0 ? lerp(n.xy, nL.xy, b) : n.xy + b*nL.xy) : n.xy
      cavity    = (flags & 2) ? lerp(cav, tex[9+L].z, b) : cav
      gloss     = (flags & 4) ? lerp(g, remap(tex[9+L].x), b) : g
```

The lit part after the blend (lightmap, probes, sun, lights) is the single-layer shader of research 95 unchanged.

Reading of the ramp (INFERRED meaning, VERIFIED arithmetic): the vertex weight w moves a window of width about k
across the reveal map's height-like green channel: w = 0 hides the layer, w = 1 shows it wherever reveal > 0, and k
is the softness of the edge (k = 0: a hard step at 1 - w). This is the same ramp the converter fork already uses for
IW7 reveal layers (`map_surface_realtime.hlsl:1279-1292`, `iw3_render_assets.cpp:7920-7934`).

**Single-layer `twc/` (WORLD_VERTCOL), VERIFIED (VS 45c84098, PS 3430ea7f line 155):** the VS outputs `COLOR.rgb =
sRGB-to-linear(colour bytes 0..2)`, `.a = byte 3 / 255`, and the PS multiplies the albedo by `COLOR.rgb`. On br_tut the
ground material `twc/dirt_rocky_gray_01` has RGB 255 on every vertex (no visible change), but the rule matters for
other maps. Alpha's use in that PS was not traced.

## 3. Real data: mp_br_tut

### Surfaces (survey.py; core reader verify_layers.exe)

- 782 surfaces: 262 with 1 layer, 197 x 2, 219 x 3, 83 x 4, 12 x 5, 4 x 6, 2 x 7, 3 x 8. **520 layered.** 519 have
  a colour record (all twc4 and twc; none of the 53 tw4).
- Families: twc4 467 surfaces, tw 210, tw4 53, twc 34, wc 18. 207 distinct layered materials.
- Core reader (`readStockWorld`): 274 meshes, 126,201 vertices; by layer count 67 / 69 / 70 / 47 / 12 / 4 / 2 / 3
  meshes for 1..8 layers; layeredSurfaces 520, colorSurfaces 519; no warnings. Read in 3.3 s.
- The ground of the course: `twc/dirt_rocky_gray_01` (single layer) on the pad, and the dirt/grass blends
  `twc4/12n_13n` (2 layers), `twc4/12n_13n_35n` (3), `twc4/12n_13n_49n_38n`, `twc4/12n_13n_35n_36` (4) around it.

Sample, `twc4/12n_13n_35n` (3 layers, 582 vertices, 446 triangles), core reader output (uv with v flipped):

```
v0 (4832, 9165, 234.5)  colour RGBA (139 164 0 0)    uv0 (7.4638 -1.5088)  uv1 (3.7319 -0.2544)  uv2 (3.7319 -0.2544)  nt ff8080ff ff8080ff
v1 (6128, 11093, 338.5) colour RGBA (142 128 128 0)  uv0 (9.3398 -2.7706)  uv1 (4.6699 -0.8853)  uv2 (4.6699 -0.8853)
v2 (6448, 10837, 394.5) colour RGBA (162 89 255 0)   uv0 (9.0921 -3.0874)  uv1 (4.5461 -1.0437)  uv2 (4.5461 -1.0437)
layer 1 weight (G): 0: 22, <.33: 67, <.66: 254, <1: 18, 1: 221 vertices
layer 2 weight (B): 0: 70, <.33: 10, <.66: 44, <1: 8, 1: 450 vertices
```

Layers 1 and 2 share a UV stream here (layer 0 tiles twice as often); on `twc4/14n_17n` they differ (mirrored).

### Material `twc4/12n_13n_35n` (techsets_mp_br_tut, VERIFIED export)

Techset `twc4/lit_3_lit_rpl_ta1_802000_1002000000000010_0_3_3_0_0_13810015b_0_0_1_0_0_1_0_13810015b_0_0_3_0_0_2_0_13810015b_0_0_3_0_0`,
layerCount 3, sub-materials `tw/dirt_rocky_gray_01_wet`, `tw/grass_winter_01_rev`, `tw/ee_periph_city_dark_grass_01`.

| Layer | colour | normal + gloss | alpha + reveal | flags | k | gloss remap |
|---|---|---|---|---|---|---|
| 0 | `dirt_rocky_gray_01_wet_c&defaultspec` (BC1 sRGB 2048) | `dirt_rocky_02_n&dirt_rocky_02_g` | - | 0x07 | - | 223/256, 33/256 |
| 1 | `grass_winter_01_c&defaultspec` (1024) | `grass_winter_01_n&_o&_g` | `grass_winter_01_c&grass_winter_01_r` (BC1) | 0x1F (normal, cavity, gloss, alpha, reveal; lerp) | 38/255 = 0.149 | 36/256, 21/256 |
| 2 | `ee_periph_city_dark_grass_01_c&defaultspec` (512) | `identitynormalmap&black` | `white&grass_flat_long_r` (BC1) | 0x17 (normal, cavity, gloss, reveal; lerp) | 51/255 = 0.200 | 256/256, 0 |

psData (160 bytes): `00004040 01000000 4e36d93f 4e36d93f | 07000000 003c0080 003c003c 00000000 | ffffffff bb2fff00
00bc0000 00000000 | 09000000 de21d800 0000804f 00000000 | 1f000000 ... | ffffff26 ... | 09000000 2315cc00 ... | 17000000
... | ffffff33 ... | 09000000 ff00ff00 ...`. Every UV transform is identity, every tint white.

Displacement layers (`twc4/12n_129dn_74n`, slot 96 `riverbed_dirt_path_01_d`, techset flag `...0018`) have two
constant buffers: buffer 0 is vertex-only (psDataSize 0) and **the pixel constants are in buffer 1** (VERIFIED).

### CPU check (out/agent/layers/py/render_patch.py)

Top-down, albedo only, sRGB: panels = today (layer 0, no vertex colour) | the blend above | weights (R, G, B =
layers 1, 2, 3). Box mips picked per triangle from its UV density; bilinear.

- `out/agent/layers/terrain_compare.png` (x -1024..7168, y -512..7680, 8 units/px): the course pad (bottom left) and
  the terrain north of it. Today: uniform dark dirt. Blend: grass with dirt paths, rock patches and soft edges, the
  look of the game.
- `out/agent/layers/terrain_blend.png`: the blend panel alone.
- `out/agent/layers/course_compare.png` (x -2048..2048, y -1536..2560, 4 units/px): the pad; mostly single-layer
  `twc/dirt_rocky_gray_01` and concrete, so the blend only adds decals of grime and tyre marks there.

Missing images (grey) in the renders are materials the editor cache had not exported (BC7 metals, some decals).

## 4. Editor data (implemented 2026-11-12)

- `src/core/stock_world.h`: `StockWorld::layerCount`, `layerUv` (every layer, v flipped like `uv`),
  `vertexColor` / `vertexColorHigh` (colour bytes 0-3 / 4-7, white when none), `layerNormalTransform`,
  `layeredSurfaces`, `colorSurfaces`, `stockLayerWeight()`. The vertex key includes the colour record.
- `src/core/stock_layered_material.h`: psData decoding (`decodeStockLayeredMaterial[Hex]`), slot helpers, and the CPU
  reference of the blend (`stockLayerUv`, `stockLayerBlend`, `stockLayerBlendColor`).
- `src/qtapp/material_cache.h`: `layerImages`, `layerColorImages`, `layerConstants`; layered bodies export every
  layer's colour, normal and alpha+reveal image and keep psData (index key `layerRules` 1).

## What is not determined

- The 5 to 8 layer shaders (export fails): weight bytes 4-7 and the same per-layer code are INFERRED.
- Row 0 `.x` (equals the layer count on the materials read) and the twc vertex alpha are not traced.
- The soft-light colour mode path is dead in the 2-layer bytecode; modes 2 and 3 were not seen in a material.
- Techset name tokens per layer (`_0_0_3_0_0`, `_0_1_3_0_0`, `_1_0_2_0_0`) select shader permutations; their exact
  meaning was not decoded (the shader reads the flags at run time, so the editor does not need them).
