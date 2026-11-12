# 94. Stock IW8 (Replay 1.20) baked lightmaps

Read-only research, 2026-11-12. Goals: (a) draw stock maps in the editor viewport with their real lightmaps;
(b) later bake and write lightmaps for the editor's own maps.

Tags: **VERIFIED** means read in data or code (cited). **INFERRED** means reasoned from that evidence but not proven.
Struct line numbers refer to `C:\zsw-iw8r\scriptables\src\games\iw8\structs.hpp` (written "S:<line>"). Converter files are
under `C:\zsw-iw8r\scriptables\src`. Work files, scripts and PNGs are in `E:\IW8-Radiant\out\agent\lightmap_research`.

## 0. Summary

- A stock map's lightmaps are **not image assets**. They are raw pixels in the world's transient zone
  (`GfxWorldTransientZone.tempLightmapData`, S:12808). Each lightmap has **three planes**:
  BC4 ambient fraction `f`, R11G11B10F irradiance `C` and a direction plane (BC5 octahedral, or RGBA8 on AB maps).
- The irradiance `C` is linear float light in Replay units: no sRGB, no RGBM, no exponent (`bakedLightScale` = 1.0).
  For a viewport with flat normals, **lit = albedo x C** is the whole lightmap term (INFERRED, section 1.4).
- A world vertex's lightmap UV is a **float2 in [0,1]** in the aux stream at `GfxWorldSurfData.lmapCoordOffset`,
  stored right after the 4-byte tangent frame. V = 0 is the first row of the image. `GfxSurface.lightmapIndex` picks
  the lightmap; 510 means "none" (shadow casters, sky).
- Static models are probe-lit by default. A lightmapped one is a separate `tml/` XModel with `tml/` materials
  (`MTL_TYPE_STATIC_MODEL_LMAP`), model flag 2, and a per-instance rectangle (`lmapTransforms` / `lmapLookup`).
- ACTS's `gfx_map_trzone` export returns `tempLightmapData: null`. The pixels must be read from the decompressed
  zone (ACTS `-d`) by finding the 0xF10-byte root (section 5).
- The x64-zt fork **already writes native lightmaps** (one lightmap, REGULAR_UNCOMPRESSED, BC4 + R11G11B10F + BC5) for
  the IW3 route (`games\iw8\assets\write_maps.cpp:41-87`). Editor maps get none only because `compile_iw8.cpp` writes
  `lightmap_uv [0,0]` and no `lightmap` key (doc 55).

## 1. Which data holds the lightmaps, and how to decode a texel

### 1.1 Tables (VERIFIED)

| Field | Where | mp_shipment | mp_backlot2 |
| --- | --- | --- | --- |
| `GfxWorldDraw.lightmapType` | S:12906, enum S:12436 | 1 REGULAR_UNCOMPRESSED | 2 AB_LIGHTING |
| `GfxWorldDraw.lightmapCount` | S:12907 | 1 | 1 |
| `lightmapReindexData.imagePixelSize` | S:12403 | 4 | 4 |
| `reindexCount` / elements `{index,posX,posY,width,height}` | S:12388 | 3 x `{0,0,0,1024,1024}` | 3 x `{0,0,0,2048,1024}` |
| `packedLightmapCount` / `{imageWidth,imageHeight}` | S:12397 | 1 / 1024x1024 | 1 / 2048x1024 |
| `lightmapTransientIndex[]` | S:12910 | [0] | [0] |
| `bakedLightScale` | S:12912 | 1.0 | 1.0 |
| `lmapLookupCount` | GfxWorld | 0 | 10 |

The reindex table has **three elements per lightmap**, one per plane: mp_donetsk has 258 lightmaps and 774 elements
(VERIFIED). `lightmapTransientIndex[i]` names the transient zone that carries lightmap i.

Survey of the editor's world cache (`%LOCALAPPDATA%\IW8 Radiant Suite\...\world_cache`, VERIFIED):

| Map | Type | Packed size | lmapLookup |
| --- | --- | --- | --- |
| mp_shipment, mp_br_tut, mp_m_fork, mp_m_king, mp_rust | REGULAR_UNCOMPRESSED | 1024x1024 | 0, 0, 18, 0, 1 |
| mp_aniyah_tac, mp_hideout, mp_m_speedball | REGULAR_UNCOMPRESSED | 2048x2048 | 206, 236, 0 |
| mp_emporium | REGULAR_UNCOMPRESSED | 2048x1024 | 17 |
| mp_backlot2, mp_village2 | AB_LIGHTING | 2048x1024 | 10, 72 |
| mp_donetsk (Warzone) | REGULAR | 258 lightmaps, 64x64 to 1024x512, 868 transient zones | 26,221 |

Correction (2026-11-12, core stock_lightmap on the zone): mp_donetsk's main zone holds one root with one lightmap of
type 0 whose irradiance plane is BC6H (format 42); the others are in the streamed zones. The editor refuses it with
that reason. Shipment and Backlot2 read back with the numbers below (C mean, max, D mean) exactly.

### 1.2 Pixel planes (VERIFIED from the decompressed zones)

`GfxLightmapAtlasDataTemp` (S:12517, 0xF10 bytes): `transientZone, numLightmaps, firstLightmapIndex, lightmapType`,
then 32 x `GfxLightmapRaw` (S:12512), each `textures[3]` of `GfxImageRaw` (0x28 bytes: `GfxImageDesc` S:12493 +
textureId + pixels pointer).

mp_shipment (`mp_shipment.ff`, decompressed offset 0x88BC494):

| Plane | Format (S:3231) | Bytes | Size | Mips | Contents |
| --- | --- | --- | --- | --- | --- |
| 0 | 39 GFX_PF_BC4 | 524,288 | 1024x1024 | 1 | ambient fraction `f` (0..1) |
| 1 | 32 GFX_PF_R11G11B10F | 4,194,304 | 1024x1024 | 1 | irradiance `C` (linear float RGB) |
| 2 | 40 GFX_PF_BC5 | 1,048,576 | 1024x1024 | 1 | direction, octahedral, 2 channels |

All three: flags 3, depth 1, numElements 1 (no array layers), textureId 0, pixel pointer -2 (follows).

mp_backlot2 (AB_LIGHTING, offset 0x6C0827E): plane 0 BC4 2048x1024, plane 1 R11G11B10F 2048x1024, plane 2 **format 6
GFX_PF_R8G8B8A8** 2048x1024 (8 MB). In the RGBA8 plane, R and G look like the octahedral direction (means 0.48 and
0.52, smooth). B is a third value with mean 0.18. A is always 255. Its BC4 plane is mostly 0 or 1, unlike shipment's
smooth fraction. What B and this BC4 mean for AB lighting is **not established**.

The pixels follow the root directly in the decompressed file, in plane order, with no padding (VERIFIED: BC4/BC5
decode is smooth only at shift 0; the next bytes after the third plane are the next record).

Stats for mp_shipment `C`: mean (1.73, 1.83, 2.02), p99 (5.06, 5.25, 5.63), max 8.5. All values finite. mp_backlot2:
mean 2.3, max 23.75.

### 1.3 Decoding one texel

- `f` = BC4 value / 255 (standard BC4 unorm).
- `C` = R11G11B10F unpacked as the DXGI unsigned small floats: R bits 0-10 (6-bit mantissa, 5-bit exponent, bias 15),
  G bits 11-21, B bits 22-31 (5-bit mantissa). Linear light. No scale (bakedLightScale 1.0), no exponent plane, no
  sRGB.
- Direction: BC5 bytes (x, y) / 255. With a = 2x - 1 and b = 2y - 1: ox = (a + b) / 2, oy = (a - b) / 2,
  oz = 1 - |ox| - |oy|; normalise. This is the inverse of the converter's `EncodeOctDirection`
  (`convert\iw3_iw8\iw3_render_assets.cpp:456`). On shipment no texel decodes below the hemisphere (|ox|+|oy| <= 1
  for all), the mean is (0.14, -0.01, 0.83), and D.z has median 0.89. This fits a **tangent-space** direction (the
  converter's comment says tangent space; INFERRED).

Decoder: `out\agent\lightmap_research\lmdecode.py` (BC4, BC5, R11G11B10F, octahedral).

### 1.4 How the shader combines them (INFERRED)

The converter's comment on Replay's stock world pixel shader (PS a3f48686, disassembled by the fork's author;
`iw3_render_assets.cpp:472-474`):

    light = (1 - f) * C / max(D.z, 0.001) * sat(N . D)  +  f * C

Here N is the tangent-space normal from the normal map. For a flat normal (N = (0,0,1)), the first term is
(1 - f) * C, so **light = C**. A viewport without normal maps can multiply albedo by `C` and ignore `f` and `D`.
With normal maps it can use the formula above. The data agrees with the reading of `f` as "ambient fraction": on
the shipment yard, texels with f < 0.2 have median luminance 2.15, and texels with 0.5 < f < 0.9 have 0.004 (the
floor under containers).

Not established: whether direct sun is in `C`, or whether Replay adds the sun at runtime (compressed sun shadow,
doc 39). The lit floor is about 2-3.6 units; no sun-intensity comparison was made.

### 1.5 PNG exports (for viewing; tone-mapped)

In `E:\IW8-Radiant\out\agent\lightmap_research\`:

- `mp_shipment_lightmap_irradiance.png`: plane 1 `C`, exposure x0.247, Reinhard, gamma 2.2.
- `mp_shipment_lightmap_bc4.png`: plane 0 `f`.
- `mp_shipment_lightmap_bc5.png`: plane 2, raw bytes in R and G.
- `topdown_yard.png`: proof. The shipment container yard seen from above. Each world triangle is coloured by the `C`
  texel its lightmap UVs hit. The yard, containers and shadows are coherent.
- `mp_backlot2_lightmap_*.png`: the three AB planes.

## 2. How a world surface finds its texel (VERIFIED)

- `GfxSurface.lightmapIndex` (uint16, S:11996). shipment: 1,037 surfaces use 0. Two use **510**: `,tw/shadowcaster`
  (it still has a UV stream) and `,tw/sky_mp_shipment` (no UV stream). backlot2 is the same: 3,388 use 0 and two use
  510. The draw-surface key keeps 9 bits (`GfxDrawSurfFields.lightmapIndex : 9`, S:2551), so 510 is a "no lightmap"
  sentinel (INFERRED).
- `GfxWorldSurfData` (S:12012) is shared by surfaces (293 entries for 1,039 surfaces). Aux-stream layout per surfData
  block: `tangentFrameOffset` (4 B/vertex), then `lmapCoordOffset` (**8 B/vertex = float32 u, float32 v**), then
  `colorOffset` (4 B, when present), then `texCoordOffset` (8 B x layerCount), then `normalTransformOffset[]`. Measured
  over 291 blocks: tangent-to-lmap is always 4 B/vertex. lmap-to-tex is 8 B with no colour and 12 B with colour.
- Vertex k of a surface: `firstVertex = (tris.posOffset - surfData.xyzOffset) / 12`; the UV is the float2 at
  `aux[lmapCoordOffset + 8 * (firstVertex + k)]`. The aux stream is `drawVerts.auxData` of `gfx_map_trzone`.
- The UV is normalised to the lightmap (its reindex rectangle; on every map seen the rectangle is the whole packed
  image). **No V flip**: v = 0 is the first row of the stored image (the flipped mapping gave scrambled patches).
  Texel = (u x W, v x H). Sample bilinear at (u x W - 0.5, v x H - 0.5). On shipment, u and v span 0.0 to 0.9994;
  the data does not clamp them half a texel in from the edge.

### Worked example: mp_shipment surface 446

Surface 446: `,twc4/12n_124n`, 22 vertices, 14 triangles, floor at z = 24, lightmapIndex 0. Its surfData is 101:
layerCount 2, xyzOffset 6684436, tangentFrameOffset 11575540, lmapCoordOffset 11575628, colorOffset 11575804,
texCoordOffset 11575892. firstVertex = 0. Its first triangle is surface indices (3, 4, 5):

| Vertex | Position | aux offset | Raw bytes | Lightmap UV | Texel (x1024) | C at texel | f | BC5 | D |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | (-1064, 1204, 24) | 11575628 | `38e63f3f3ee7933e` | (0.749607, 0.288874) | (767.6, 295.8) | (3.22, 3.56, 4.13) | 0.224 | (166,146) | (0.30, 0.11, 0.95) |
| 1 | (-1121.46, 1206.95, 24) | 11575636 | `099d433fb347943e` | (0.764115, 0.289610) | (782.5, 296.6) | (3.44, 3.84, 4.50) | 0.212 | (170,144) | (0.32, 0.14, 0.94) |
| 3 | (284, 1228, 24) | 11575652 | `7e198a3c330a0e3e` | (0.016858, 0.138711) | (17.3, 142.0) | (1.64, 1.84, 2.19) | 0.133 | (115,72) | (-0.41, 0.26, 0.87) |
| 4 | (280, 1264, 24) | 11575660 | | (0.025719, 0.137376) | (26.3, 140.7) | | | | |
| 5 | (304, 1272, 24) | 11575668 | | (0.027984, 0.143283) | (28.7, 146.7) | | | | |

Triangle (3, 4, 5): centroid UV (0.02352, 0.13979); bilinear `C` = (2.27, 2.46, 2.77). From v3 to v4 the edge is 36.2
units in the world and 9.2 texels, so the density is about **4 units per texel**. One surface can lie in several
charts (v0-v2 and v3-v5 are far apart in the atlas); only triangles are contiguous.

Script: `out\agent\lightmap_research\worked.py`.

## 3. Static models (VERIFIED unless marked)

- Stock default: probe lighting. mp_shipment has 277 models: 263 with flag 1 (LIGHTPROBE), 13 with flag 5, 1 with
  flag 13. lmapTransforms 0, lmapLookup 0.
- mp_backlot2 has 6 models with flag **2 STATIC_MODEL_FLAG_LIGHTMAP_LIGHTING** (S:12057) and **without** flag 1:
  `tml/furniture_rug_01_flat_08`, `..._02`, `tml/decor_blankets_01_stack_pattern_1_03`,
  `tml/uk_building_exterior_concrete_stairs_64_center_01`, `tml/building_wall_bottom_damage_01`,
  `tml/building_wall_bottom_damage_end_b`. They have 10 placements in total. The XModel is a **separate `tml/` copy**.
  Its materials use `tml/` techsets of type `MTL_TYPE_STATIC_MODEL_LMAP` (589832, S:2454) and constant layout
  `MTL_CONSTANT_LAYOUT_SMODEL_LIT_LMAP` (`techsets_mp_backlot2.ff`, 4 such techsets).
- Per placement: `GfxModelStaticIndirection.lightingInstanceIndirection` (S:12113) gives 0..9 for those 10 placements.
  It indexes `GfxWorldStaticModels.lmapTransforms[]` (S:12120) and `GfxWorld.lmapLookup[]` (`GfxModelLightmapInst`,
  S:13137) in parallel. (Other placements use the same field for their own lighting slot; INFERRED.)
- `GfxModelLightmapInst {lmapIndex, offset(u,v), scaleWidth, scaleHeight}`: a normalised rectangle in the lightmap.
  Example, placement 0: offset (0.46338, 0.30176), scale (0.005371, 0.010742), which is an 11 x 11 texel square at
  pixel (949, 309) of the 2048x1024 image. Squares are 11x11 or 8x8.
- `GfxLightmapTransformPacked` is the same rectangle in fixed point: `topLeft = (u * 2048) << 12 | (v * 2048)`,
  `bottomRight` likewise for (u + scaleWidth, v + scaleHeight). Example: 3887722 = 0x3B526A gives 949 and 618 (618 /
  2048 = 0.30176). All 10 entries match lmapLookup exactly. The unit is 1/2048 on both axes, although the image is
  1024 tall. Bits 24-31 hold the lightmap index (named `_indexLow` / `_indexHigh`; all 0 here; INFERRED).
- Per vertex: `XSurface.lmapCoords` (`PackedLmapCoords { uint32 packed }`, S:1717 and S:1943), presumably a 0..1 UV
  that is then mapped by `offset + uv * scale`. **Not verified:** ACTS writes only the shared geometry buffer for
  `xmodelsurfs` (`furniture_rug_01_flat_04_lod010v41`, served from `pak_magma1.xpak`), not the XSurface table, so the
  packing (2 x unorm16 or half2) was not read.
- `GfxMapEntLookup.lmapLookup` (brush-model entities) exists. All three backlot2 entries are 0.

## 4. What a custom map must write for the game to use lightmaps

### 4.1 World data

1. **GfxWorldDraw**: `lightmapType` (1 REGULAR_UNCOMPRESSED is the simplest; 9 of 12 cached stock maps use it),
   `lightmapCount`, `lightmapReindexData` (`imagePixelSize` 4; three elements per lightmap
   `{index, posX, posY, w, h}`; `packedLightmap {w, h}` per lightmap), and `lightmapTransientIndex[count]`.
2. **Transient zone**: `tempLightmapData` = the 0xF10 root, then each plane's pixels: BC4 `f`, R11G11B10F `C`, BC5
   direction. Use 1 mip, flags 3.
3. **Surfaces**: `GfxSurface.lightmapIndex` = the lightmap; 510 for surfaces without one. Write
   `GfxWorldSurfData.lmapCoordOffset` and a float2 UV per vertex in the aux stream, after the tangent frame.
4. **Materials (world)**: no special variant is needed. Every stock world techset type (`tw` WORLD, `twc`
   WORLD_VERTCOL, `tw4` WORLD_LAYERED, `twc4`) has its `TECHNIQUE_LIT_FORWARDPLUS_BITMASK` technique on constant layout
   `MTL_CONSTANT_LAYOUT_WORLD_LIT_LMAP` (S:2918). Example: `twc4/lit_3_lit_rpl_ta1_800800_...`; techsets of
   shipment and backlot2, VERIFIED. So `lit_3_lit` world techsets always read the lightmap. A lightmap of zeros would
   draw the surface black (INFERRED from the shader formula).
5. **Static models (optional)**: a `tml/` model copy with `tml/` (STATIC_MODEL_LMAP) materials; model flag 2; per
   placement `lmapTransforms` + `lmapLookup` + indirection; `XSurface.lmapCoords`. Probe lighting (flag 1, the
   converter's fixed choice, doc 54) remains the simpler route.

### 4.2 What the x64-zt fork already does (VERIFIED)

- `games\iw8\assets\write_maps.cpp:41-87` `emitNativeLightmapTemp`: writes the 0xF10 root (numLightmaps 1, type 1,
  formats from the JSON, flags 3, 1 mip) and the pixels in stream 7, aligned to 16. Lines 423-438 stamp the GfxWorldDraw
  fields (imagePixelSize 4, reindexCount 3, packed 1, type 1, count 1). Lines 583-597 write the three reindex elements
  and the packed size. Lines 663-668 write `lightmapTransientIndex = 0`. **It supports only one lightmap**
  (`throw ... requires one lightmap`).
- `games\iw8\common\replay_render.cpp:1605-1661` reads `nativeLightmaps[{width, height, images[3]{format, pixels}}]`.
  The formats must be exactly **39, 32, 40**, with sizes blocks x 8, w x h x 4 and blocks x 16. Lines 1855-1866 and
  2092 handle the per-surface `lightmapIndex` (default 0, at most 511). Lines 1993-2007 write each vertex's
  `lightmapUV` as two float32 after the layer UVs.
- `convert\iw3_iw8\iw3_render_assets.cpp:6490-6524`: from IW3 lightmaps (`world.lightmaps`: pairs with primary format 50
  and secondary format 21 = two stacked BGRA8 layers, A colour + dirX, B colour + dirY; optional `lightmap_encoding
  {log_scale, maximum, constant_ambient}`), it encodes the native planes (`EncodeNativeLightmapPixel`, line 468) and
  writes `<map>_native_lightmap_0.bc4/.r11g11b10f/.bc5`. Several source lightmaps are packed into one native image
  (lines 6534-6575). `iw3_fastfile.cpp:4141` `StockLightmapUv` writes UVs normalised to that image, half a texel
  inside each source rectangle, for surfaces drawn with stock world materials.
- The editor's maps go through this IW3 route (doc 55). They get no lightmap only because `compile_iw8.cpp:2587` writes
  `"lightmap_uv":[0,0]` and no `lightmap` per surface, and the world has no `lightmaps` array.

**Shortest route to baked maps (INFERRED):** the editor bakes one atlas, writes per-surface `lightmap` and per-vertex
`lightmap_uv` (0..1 within that lightmap), and writes `world.lightmaps` in the IW3 layout (or the native three-plane
files directly). The converter already produces the game's native table. Still to check: which materials the
converter uses for editor surfaces (generated `w/mw120r_*` techsets bind their own lightmap copies at slots 0x3E/0x3F,
`replay_render.cpp:179-193`; stock world materials read the native table) and that the result renders in game.

## 5. Getting stock lightmaps into the editor (route)

1. ACTS `gfx_map_trzone` gives `tempLightmapData: null` (VERIFIED on shipment and backlot2; the loader runs on temp
   memory). Pixels came from `acts.exe ... fastfile -d --noAssetDump -l -o <dir> <map>.ff`, which writes
   `<map>.ff.dec` (shipment 385 MB, backlot2 332 MB; the backlot2 `.fp` patch does not change these bytes).
2. Find the root by signature (`find_lm.py`): `u32 tz < 1536, u32 1 <= num <= 32, u32 first < 512, u32 type <= 2`,
   then `GfxImageRaw[0]`: format, flags, totalSize, u16 w, h, depth = 1, elements, levels, pointer -2. Exactly one hit
   on each map. Pixels follow at root + 0xF10, plane after plane, for each lightmap.
3. Check `numLightmaps`, the formats and the sizes against `GfxWorldDraw` (`packedLightmap`, `lightmapType`) from the
   gfx_map JSON the editor already caches.
4. A cleaner option is for ACTS (or the fork's neutral reader, `games\iw8\neutral\gfxworld.cpp:4716`
   `read_gfx_lightmap_atlas_data_temp`) to export `tempLightmapData`. Neither does so today for Replay.
5. Viewport shading: `albedo x C(u, v)`, with C sampled bilinear from an RGB16F/R11G11B10F texture, then the editor's
   exposure and tone map. The UV reader goes in `src\core\stock_world.cpp` next to the existing `texCoordOffset` code
   (offset `lmapCoordOffset + 8 * (firstVertex + k)`, float2).

## 6. Open items

1. AB_LIGHTING: the meaning of the RGBA8 plane's B channel and of the binary BC4 plane (mp_backlot2, mp_village2).
2. REGULAR (compressed) plane formats: mp_donetsk only. Its `_tr.ff` zones are 49 KB stubs (empty after `-d`), so
   the data is elsewhere (XPak or the main zone). Not read.
3. `PackedLmapCoords` packing for lightmapped static models (needs an XSurface table export).
4. Whether `C` includes the direct sun; the unit scale between bake output and Replay units (the converter has a
   `unitScale` hook, `iw3_render_assets.cpp:431`).
5. The exact shader formula is from the converter's comment on a disassembled shader; it was not read here (IDA was not
   reachable).

## 7. Reproduce

Scripts in `out\agent\lightmap_research`: `find_lm.py` (root scan), `extract.py <dec> <root hex> 0` (shipment planes
+ PNGs), `extract_bl2.py`, `surf_extract.py <map>` (surfaces, surfData and streams from the world cache),
`render_yard.py` (top-down proof), `worked.py` (the table in section 2). The large decompressed zones, stream dumps and
techset exports were deleted after use (E: is nearly full).
