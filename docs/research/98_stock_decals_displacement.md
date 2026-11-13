# 98. Stock decal surfaces and displacement (Replay 1.20): blend, opacity, draw order, height offset

**Why.** The viewport draws stock world surfaces opaque with colour, normal, layers (research 97) and the lightmap
(research 94). Speedball's ground also has blended world surfaces (dirt, grass, stains and puddles laid over the base
ground, material sortKey 10 to 17) and a few displaced surfaces (`dn` layers). In the editor the blended ones come out
as opaque patches. This note reads from the shipped shaders how Replay draws both kinds.

**Labels.** VERIFIED = read in the disassembly, the techset/material export or the world data (cited). INFERRED = from
names, the IW8 game-test decompile, statistics or arithmetic, not from a Replay shader. The DXBC has no RDEF block, so
register names are ours.

## How it was obtained

- ACTS `-a material,techset,pixelshader,vertexshader,hullshader,domainshader -l` on
  `zone/techsets_mp_m_speedball.ff` (2,288 assets; 4 techsets of 5 layers fail to export, as in research 97). The
  shaders were disassembled with `out/agent/shader_research/py/dxbc_dis.py`. The full export was deleted afterwards.
  `out/agent/decals/` keeps the cited disassemblies (`asm/<hash8>.asm`, NUL-stripped), six material exports (`mat/`),
  every world techset's techniques, shaders and state bits (`techsets_world.txt`) and the scripts (`py/`).
- World data: the editor's world cache of mp_m_speedball (`%LOCALAPPDATA%/IW8 Radiant Suite/.../world_cache/mp_m_speedball`,
  gfx_map + gfx_map_trzone), read by `py/sb_survey.py` (`sb_survey.txt`) and `py/disp_check.py` (`disp_check.txt`).
- Engine state decode: `R_HW_CreateBlendStateDesc` and `Load_DeriveMaterialPipelineState` in the IW8 game-test
  decompile (`E:\mw124\mw164proxy\$dst\game-test-src-cleaned\code_source\src\gfx_d3d\r_setstate_d3d.h:160-283`,
  `r_material.cpp:670-850`), the enum names in `E:\mw124\x64-zt\src\games\iw8\structs.hpp:3040-3160`, dvars in
  `r_dvars.cpp` and `r_scene.cpp:5060-5086`. That build is not Replay. Only bit layouts and defaults come from it.

Shaders cited (prefix of the hash; line numbers are in `out/agent/decals/asm/<prefix>.asm`):

| Tag | Hash | Technique (lit = `TECHNIQUE_LIT_FORWARDPLUS_BITMASK`) |
|---|---|---|
| D | PS `39852264` | `twc/lit_3_lit_bad_tca_812002_1002000000000010_...` lit (single-layer vertex-colour decal); VS `45c84098` is the opaque twc VS |
| O | PS `3430ea7f` | opaque `twc/lit_3_lit_rpl_ta1_..._1002000000000010_...` (reference) |
| L2, L3 | PS `a887254e`, `362e22a2` | `twc4/lit_3_lit_bad_tca_812402_..._0_2_2_...` and `_0_3_3_` (layered decals); VS `e2fa6708` is the opaque twc4 2-layer VS |
| T, T4 | PS `21993ec0`, `459a0d77` | `tw/..._bad_tca_802002_...` and `tw4/..._bad_tca_810802_...` (no colour stream) |
| W | PS `7db93595` | `twc/..._bad_tca_812002_100a...` (wrap diffuse) |
| S | PS `36e2f517` | `twc/..._bad_tca_815402_1002000000000012_...` (SSR) |
| VD, PD | VS `f06c0749`, PS `216e2c87` | `twc4/lit_3_lit_rpl_ta1_802000_1002000000000018_0_3_3_...` lit (displacement, no tessellation) |
| VT, HS, DS | VS `9a82745f`, HS `a16af270`, DS `2a7a2c67` | the same techset, `TECHNIQUE_WORLDTESS_LIT_FORWARDPLUS_BITMASK` (PS is also `216e2c87`) |

## 1. Which surfaces are blended (VERIFIED)

- Every world lit technique of the zone whose techset has the `_bad_` blend token has blendBits `0x28050`. Every
  `_rpl_` / `_alt_` world lit technique has blendBits 0 (`techsets_world.txt`: 14 of 14, and 71 of 71). The blended
  techsets have **only** the lit technique: no depth prepass, no shadow map technique (techniqueMask has one bit).
  So decals write no depth and cast no shadows.
- On Speedball the map surfaces with a `_bad_` techset are exactly the sortKey 10 to 17 ones: 15 materials, 19
  surfaces, 207 triangles (`out/agent/sb_audit.txt`, `sb_survey.txt`). sortKey 2 is opaque world, 510 sky and caulk.
- GfxWorld of mp_m_speedball: `sortKeyLitDecal 7, sortKeyEffectDecal 14, sortKeyTopDecal 17, sortKeyDistortion 24,
  sortKeyEffectAuto 35, sortKeyEffectDistortion 36, sortKey2D 41` (`sb_survey.txt`; their use was not traced).

The surfaces (world cache, `sb_survey.txt`). Colour bytes are R G B A ranges over the surface's vertices.

| Surface | Material | sortKey | Layers | Colour record |
|---|---|---|---|---|
| 319, 320 | `twc/dirt_soil_erosion_02_rvl` | 10 | 1 | RGB 255, A 0..255 |
| 321 | `twc4/30n_4n` | 10 | 2 | R 0..125, G 100..247, B 0, A 0 |
| 322 | `twc/grass_winter_01_rev` | 11 | 1 | RGB 255, A 0..247 |
| 323 | `twc4/40n_39n_16n` | 11 | 3 | R 0..253, G 0..68, B 49..159 |
| 324 | `twc/dirt_rocky_pebbles_01_wet_rev_nodis` | 12 | 1 | RGB 255, A 0..187 |
| 325 | `twc4/5n_6n` | 12 | 2 | R 0..187, G 0..255 |
| 326 | `twc/wall_stain_01_dec` | 13 | 1 | RGB 255, A 0..54 |
| 327, 328 | `twc/ee_rock_dirt_muddy_ground_a_01_world_rvl` | 15 | 1 | RGB 255, A 0..255 |
| 329 | `tw4/72n_72n_48n` | 15 | 3 | none |
| 330 | `tw/ee_periph_city_dark_grass_01` | 15 | 1 | none |
| 331, 332 | `tw/water_puddle_tiling_01_rvl_ssr` | 16 | 1 | none |
| 333 | `tw4/48n_48n` | 16 | 2 | none |
| 334 | `twc/water_puddle_muddy_01_rvl_ssr` | 16 | 1 | RGB 255, A 0..255 |
| 335, 336 | `twc/grass_dirt_twigs_leaves_01_rvl` | 17 | 1 | RGB 255, A 0..255 |
| 337 | `twc4/16n_94n` | 17 | 2 | R 0..95, G 0..255 |

## 2. Render state (VERIFIED decode of the exported state bits)

`defaultStateBits` of the blended lit technique: otherBits `0xC11` (13 techsets) or `0xC01` (1:
`tw/lit_3_lit_bad_tca_802002_...`), blendBits `0x28050`. Opaque lit is otherBits `0x801`, blendBits 0. The depth
prepass (`TECHNIQUE_FIRST`) is `0xE01`.

| Field | Bits | Decal value | Meaning |
|---|---|---|---|
| src RGB | blend 0-3 | 0 | ONE |
| dst RGB | blend 4-7 | 5 | INV_SRC_ALPHA |
| op RGB | blend 8-10 | 0 | ADD |
| src / dst / op alpha | blend 11-14 / 15-18 / 19-21 | 0 / 5 / 0 | ONE / INV_SRC_ALPHA / ADD |
| blend enable | `(blend & 0x3FFFFF) != 0` | yes | (0 = opaque, blending off) |
| write mask | blend bits 32+ | 0 | not independent: RGBA on every target |
| cull | other 0-1 | 1 | same as every world technique (back faces, INFERRED) |
| polygon offset | other 4-5 | 16 = `GFXS_POLYGON_OFFSET_DECAL` (0 on `tw/..._802002`) | rasterizer DepthBias = `-r_polygonOffsetBias`, SlopeScaledDepthBias = `-r_polygonOffsetScale`, clamp `-r_polygonOffsetClamp` |
| depth write | other 9 | off | opaque lit is also off (it follows the prepass) |
| depth test | other 10-11 | 3 = `GFXS_DEPTHTEST_LESSEQUAL` | opaque lit is 2 = EQUAL, prepass 3 |

- **Premultiplied alpha.** With ONE / INV_SRC_ALPHA the frame becomes `src.rgb + dst.rgb * (1 - src.a)`. The shader
  outputs `src = (lit * b, b)` (section 3), so the result is `lit * b + below * (1 - b)`.
- Polygon offset values (INFERRED): game-test defaults are `r_polygonOffsetBias -1`, `r_polygonOffsetScale -1.0`,
  `r_polygonOffsetClamp 0` (`r_dvars.cpp:569-571`), so DepthBias +1, slope +1.0, no clamp. A positive bias moves the
  decal toward the camera only with a reversed depth buffer. The Replay dvar values and depth direction were not read.
- In the techset name, the 0x10000 bit of the first hex token (`812002` vs `802002`) matches the polygon offset on all
  14 blended world techsets (VERIFIED correlation, INFERRED meaning).

**Draw order.** The game sorts materials by sortKey first (`Material_Compare` -> `Material_CompareBySortKeyAndTechnique`,
game-test `r_material_load_obj.cpp:131`, INFERRED for Replay). The GfxWorld surface array is already in ascending
sortKey order: surfaces 313 to 337 are sortKey 2, 2, 2, 2, 10, 10, 10, 11, 11, 12, ..., 17 (VERIFIED,
`sb_survey.txt`). So: all opaque world first, then the blended surfaces in ascending (sortKey, surface index).

## 3. Pixel shader of a blended surface

### Textures and constants (VERIFIED, technique args and disassembly)

Same slots and psData layout as the opaque world shaders (research 95 / 97). Single-layer `twc` and `tw` decal: t1 =
slot 0 (colour, spec code in alpha), t2 = slot 9 (normal + gloss + cavity), **t3 = slot 27 (alpha in red, reveal in
green)**, t4 = slot 59 (tint mask). cb3 = psData rows 0..3: row 1 `.x` flags, row 2 `.x` byte 3 = k (args put constant
index 0, 9, 54, 72 in rows 1, 2, 3, 0, as research 97). Layered decals add slots L, 9 + L, 27 + L, 59 + L per layer.
**Layer 0 of a blended surface has its own alpha + reveal map at slot 27** (L2: t3 = slot 27, t7 = slot 28). On
opaque surfaces layer 0 has none.

### Opacity (VERIFIED: D:115-139, L2:125-145 and 209-224, T:126-133, L3:149-232)

For each layer L (L = 0 included), with that layer's flags and k:

```
w      = twc  (single layer): vertex colour byte 3 / 255 (COLOR.a)                       D:123, D:131
         twc4 (layered):      vertex colour byte L / 255 (WEIGHTS[L]; layer 0 = R)      L2:129-140, L2:218
         tw, tw4:             1 (compiled in: k * 0, so the ramp is a step at reveal > 0) T:129-130
(a, r) = (flags & 24) ? tex[27+L](uvL).rg : (0, 0)                                       D:113-118
lo     = saturate(1 - w - k sqrt(w));  hi = saturate(1 - w + k sqrt(1 - w))
b_L    = ((flags & 16) ? saturate((r - lo) / (hi - lo)) : w) * ((flags & 8) ? a : 1)     D:124-139
```

This is the layer formula of research 97 (`stockLayerBlend`), now applied to layer 0 too. The shader's fast sqrt and
reciprocal give a step `r > lo` when `hi == lo`.

Surface opacity (VERIFIED L2:224, 317; L3:332, 465; T4:297; bafe2973:323): `B = b_0`, then for each L >= 1 with
`b_L >= 1/255`: `B = max(B, b_L)`. Colour layers still lerp over each other by `b_L` as in research 97, so a layer
can show where layer 0 is transparent.

### Colour (VERIFIED: D:182, 845, 870, 1233-1248, 1256, 1282-1283; O:1220)

- Albedo, normal, gloss, specular code and cavity are decoded exactly as in the opaque shader (research 95). Layers
  blend as in research 97. Single-layer `twc` albedo is multiplied by the vertex colour RGB (D:182; the VS gives
  sRGB-to-linear of bytes 0..2, research 97). On Speedball every blended `twc` vertex has RGB 255. `twc4` decals have
  no vertex tint (WEIGHTS only).
- Lighting (lightmap, probes, sun, lights, decal volumes) is the opaque code, with the decal's own normal and its own
  lightmap UVs. Every term is multiplied by `B` before the sum: ambient diffuse (D:1244), indirect specular (D:845),
  sun (D:1248) and each local light's diffuse and specular (D:1233-1234). So `o0.rgb = lit * B`.
- `o0.a = 1 - (1 - B) = B` (D:870, 1247). The opaque shader writes `o0.a = 1` (O:1220). `o1 = o0.a` (D:1283; second
  target, use not traced).
- Discard (D:1256): outside the thermal path, a pixel is dropped when `max(o0.r, o0.g, o0.b, B) <= 1e-6`.

### HLSL for the viewport

```hlsl
// Inputs: w0..w3 from the vertex colour per family (above), layer constants as research 97, textures per slot.
float coverage = 0;
float3 albedo; /* layer 0 */ float b0 = LayerOpacity(c[0], w[0], tex27_0.Sample(s, uv0).rg);
albedo = Colour0(uv0) * tint0 * (isTwc ? vtxColor.rgb : 1);   // linear
coverage = b0;
for (L = 1; L < N; ++L) {                                       // research 97 blend, b = LayerOpacity(c[L], w[L], ...)
    float b = LayerOpacity(c[L], w[L], tex27_L.Sample(s, uvL).rg);
    if (b >= 1.0 / 255) { BlendLayer(L, b, albedo, normal, gloss, spec, cavity); coverage = max(coverage, b); }
}
float3 lit = Lighting(albedo, normal, gloss, spec, cavity);    // the opaque path unchanged
if (max(max(lit.r * coverage, lit.g * coverage), max(lit.b * coverage, coverage)) <= 1e-6) discard;
return float4(lit * coverage, coverage);
// State: blend ONE / INV_SRC_ALPHA (colour and alpha), depth test LESS_EQUAL, depth write off, a small depth bias
// toward the camera (editor depth is standard Z with LESS_EQUAL: negative DepthBias / SlopeScaledDepthBias, about
// -1 / -1, tune), no shadow casting. Draw after all opaque surfaces, ascending (sortKey, surface index).
```

Equivalently `return float4(lit, coverage)` with SRC_ALPHA / INV_SRC_ALPHA gives the same colour.

## 4. The name suffixes and techset tokens

The suffixes are artist names. The shader does not read them. What they correlate with on Speedball:

| Suffix | Materials | What the data shows |
|---|---|---|
| `_rvl`, `_rev` | erosion, muddy ground, twigs, puddles; `grass_winter_01_rev`, `..._wet_rev_nodis` | flags bit 16 (reveal ramp) set on all of them (0x17 or 0x1F) (VERIFIED). "reveal" (INFERRED). |
| `_dec` | `wall_stain_01_dec` | flags 0x1F: alpha mask (`wall_stain_01_c&wall_stain_01_r`, red = colour alpha) times reveal (VERIFIED). |
| `_ssr` | both water puddles | techset hex token B ends in 2 (`...0012`) (VERIFIED). Gloss remap (20, 235) gives gloss about 0.92 to 1.0, normal strength 4/256 (a flat mirror). |
| `_nodis` | `dirt_rocky_pebbles_01_wet_rev_nodis` | maxDisplacement 0, no `0018` token; its reveal map is `dirt_rocky_pebbles_01_light_h`, a height map ("no displacement" copy of a displaced material, INFERRED). |
| `_h` reveal maps | erosion, pebbles | the green reveal channel is a height map, so the vertex alpha floods the decal in from the low areas (INFERRED from names; arithmetic VERIFIED). |

Techset tokens (VERIFIED on every world lit techset of the zone, `techsets_world.txt`; `parseStockTechsetName`):
`<family>/lit_3_lit_<blend>_<x>_<hexA>_<hexB>_0_<N>_<N>_...`

| Token | Value | Effect |
|---|---|---|
| blend | `rpl` opaque, `alt` alpha-tested, `bad` blended | blendBits 0 / 0 / 0x28050 |
| hexA | bit 0x10000 on `bad` | polygon offset decal |
| hexB last digit | 0 / 2 / 8 | plain / screen-space reflection (PS reads t96 and t5) / displacement (WORLDTESS techniques + displaced VS) |
| hexB bit 0x0008000000000000 (`100a...`) | set | one extra psData row (`cb3[1 + 3N]`): RGB wrap diffuse (section 6) |
| first `N` | layer count | |

## 5. Displacement (`dn` layers)

### Where it is (VERIFIED)

- Speedball: `twc4/14n_15dn_16n` (layer 1 displaced, surfaces 313-314, 9 triangles) and `twc4/14n_4n_15dn_16n`
  (layer 2, surfaces 315-316, 25 triangles), **34 triangles in all**, a riverbed dirt path. maxDisplacement 40 on both.
  No other world material of the zone has maxDisplacement != 0 or a `0018` techset.
- Displaced layer L: texture slot 95 + L (`riverbed_dirt_path_01_d`), sampled red. Material constant index 45 + L =
  `(40, -20, 0, 0)` = (maxDisplacement, -maxDisplacement / 2). It is in the **vertex** constant buffer at row 1 + 3N
  (vsData, both buffers; psData is in buffer 1, research 97).
- GfxWorldSurfData `displacementOffset[L]` (byte 56 + 4L; VT:82, VD:121) points at 4 bytes per vertex in the aux
  stream: two binary16, `lo` and `hi`. Default when the offset is 0: lo 0, hi 1 (VD:121-125, the constant 0x3C000000).
  Speedball data (`disp_check.txt`): lo 0 to 0.416, painted per vertex (on surfaces 313 to 315 `lo = 0.663 * w_L`
  where non-zero, 0 elsewhere; surface 316 has other ratios), hi
  0.005 to 0.023, the same order as the layer's UV per inch measured from the mesh (0.009 to 0.015). Meaning of hi
  (UV density) INFERRED.

### Replay displaces even without tessellation (VERIFIED)

The techset has two sets of techniques. The standard `TECHNIQUE_LIT_FORWARDPLUS_BITMASK` and depth prepass use
displacing vertex shaders (VD). The `TECHNIQUE_WORLDTESS_*` set adds HS/DS (`MTL_TESSELLATION_STAGES`). Both lit
techniques use the same PS (PD).

Vertex shader VD (lines 121-151), per vertex j:

```
param = cb7[114].z                                   // r_tessellationHeightScale, * -1/256 when r_tessellationHeightAuto (game-test r_scene.cpp:5060)
amp   = lo / (param >= 0 ? 1 : hi) * |param|         // VD:138-141
h     = tex[95+L].SampleLevel(uvL, lod).r            // VD:136, uvL = the displaced layer's own UV; lod from distance, hi and r_tessellationLodBias
d     = amp * (h * scale + bias)                     // VD:142-144, (scale, bias) = (40, -20)
P'    = P + normalize(N_vertex) * d                  // VD:149-151
DISPLACEMENT_EYE_BIAS.w = amp * bias - d             // VD:150 (residual for the PS)
DISPLACEMENT_SCALE.y    = amp * scale
```

Mid-grey (h = 0.5) leaves the vertex in place, so the stored mesh is the mean surface.

### Tessellation (VERIFIED structure, factors summarised)

- HS (a16af270:59-63): triangle domain, `partitioning_integer`, `output_triangle_cw`, max tess factor 64. Edge factor
  from the edge length over the camera distance times `cb7[114].x` (rg.tessellationFactor), faded by distance with
  `cb7[115].y` / `.z` (cutoff - falloff, 1 / falloff) and limited by `cb7[115].x` (HS:330-367, 497-498).
  Subdomain instancing (`SUBDOMAIN_REMAP`, `r_subdomainLimit` 3 = up to 256) multiplies it. Dvars (game-test
  defaults, INFERRED for Replay): `r_tessellation` 1, `r_tessellationFactor` 60, `r_tessellationCutoffFalloff` 320,
  `r_tessellationHeightScale` 1, `r_tessellationHeightAuto` 1, `r_tessellationLodBias` 0.5, `r_tessellationHybrid` 1.
- DS (2a7a2c67:205-332): per new vertex, `d = amp * (h * scale + bias)` (DS:253-267, the same as VD), applied along
  the barycentric blend of the control-point normals (weighted by each point's `amp * scale`). A Phong tessellation
  term is added, of strength `0.75 * saturate(lo * (1 - saturate(row.w)))` (VT `o4.w`; row.w = 0 on Speedball)
  (DS:305-322). `DISPLACEMENT_EYE_BIAS.w = amp * bias - d` (DS:303).

### Pixel shader PD (lines 141-170): parallax for the residual

`hp = DISPLACEMENT_SCALE.y * tex[95+L](uvL).r + EYE_BIAS.w`, the height the vertex/domain stage did not cover. It is
refined once at the offset UV and scaled by `cb7[114].w` (`r_tessellationHybrid`). It then offsets **all** layer UVs,
the lightmap UV and the view position along the eye vector (two-tap parallax). With no vertex displacement, this
alone would give full parallax.

### For the editor

Speedball has 34 displaced triangles. A flat mesh is the mean surface, so skipping displacement is a small error
there. For maps with more: displace the existing vertices in the VS by `normalize(N) * amp * (h * 40 - 20)`, with
`heightAuto` on (`stockDisplacementSceneParam(1, true)`: `amp = lo / hi / 256`). On surface 313 that is at most
±1.7 inches; without heightAuto it is ±8.3. Add the PD parallax only if needed. The world reader does not read the
displacement stream yet (stock_world.h comment).

## 6. Other things a colour + normal + layers + lightmap viewport still misses (short)

- **Specular and gloss** (research 95): colour alpha = spec code, normal map x = gloss, z = cavity (specular
  occlusion). Wet layers and puddles depend on them (puddle gloss about 1, flat normal).
- **Wrap diffuse** (`100a` techsets, VERIFIED W:561-574, 914, 1203): `cb3[1 + 3N].xyz` = per-channel wrap (Speedball
  pebbles `(0.690, 0.650, 0.510)`, `.w` 0.648 not read). Diffuse factor `ndotl < w ? (ndotl + w)^2 * 3 / (4w(w^2 + 3))
  : 3 ndotl / (w^2 + 3)`, saturated (`stockWrapDiffuse`). It is used on the lightmap directional term (W:561, right
  after the t114/t115 samples), the sun and local lights. Speedball: 2,352 triangles (the `*_5n*` twc4 ground blends
  and the `_nodis` decal). Meaning (soft subsurface-like terminator) INFERRED.
- **Screen-space reflection** (hexB `...2`, VERIFIED S:869-940): the probe reflection is lerped toward a screen
  trace (t96, previous-frame colour t5). Puddle and wet layers. About 1,455 Speedball triangles (twc4 opaque `0012`
  blends included). The editor can only approximate this with the probe.
- **twc vertex tint**: single-layer `twc` albedo times sRGB-to-linear vertex RGB (opaque and decal alike).
- **Decal volumes** (t56/t57 loop in every world PS, research 80/82) modify albedo, normal and gloss of all world
  surfaces. They are separate from the sortKey decals above.
- Screen AO (GTAO) and specular occlusion LUT (research 95) are engine terms, not material terms.

## 7. Editor data and helper

- Needed per blended surface: the colour record (already `StockWorld::vertexColor`, bytes 0..3), every layer UV,
  the slot 27 + L image of every layer **including layer 0**, read as red = alpha and green = reveal, psData, sortKey
  and the techset name (blend token). Today `material_cache.cpp` (lines 504-542) keeps slot 27 of a blended set as
  the opacity `mask` (research 66), and psData and `layerReveal` only for bodies of 2 or more layers (slots 28+). So
  single-layer decals lack psData (flags, k), and layer 0's slot 27 is not treated as alpha + reveal.
- `src/core/stock_decal.h` / `.cpp` (new, tests in `tests/stock_decal_tests.cpp`): `decodeStockBlendBits`,
  `decodeStockOtherBits`, `parseStockTechsetName`, `stockDecalLayerWeight`, `stockDecalRevealSlot`,
  `stockDecalCoverage`, `stockDecalComposite`, `stockDecalDiscards`, `stockWrapDiffuse`,
  `stockDisplacementSceneParam`, `stockDisplacementAmplitude`, `stockDisplacementOffset`. The per-layer opacity is
  `stockLayerBlend` (stock_layered_material.h) with the decal weights above. `xmake build -y iw8r_tests`: 1,193 tests,
  140,735 checks, 0 failures (2026-11-12).

## What is not determined

- The Replay values of `r_polygonOffset*`, `r_tessellation*` and the depth direction (reversed or not). Defaults are
  from the game-test build.
- How `sortKeyLitDecal` / `sortKeyTopDecal` split the draw lists. Ascending sortKey order is INFERRED from the
  comparator and the sorted surface array.
- The second render target written with `o1 = alpha`.
- Exact tessellation factors (only the inputs and limits were read) and the meaning of `hi`. The SSR trace itself
  was not decoded.
- The 5 to 8 layer blended or displaced shaders (none on Speedball; the 5-layer opaque techsets fail to export).
