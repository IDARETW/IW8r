# 133. The G16 render paths: UV animation, detail maps, parallax, static-model vertex colour and SELFVIS, water

**Why.** Research 132 section G16 lists stock rendering paths whose game behaviour was never read, so the viewport's
Game Lighting shader cannot match them. This note reads each one from the game's own technique sets, shaders and
material bodies (Replay 1.20, four MP maps) and gives a recipe for `src/viewport/render.cpp`.

**Labels.** VERIFIED = read in a shader disassembly or an asset export (cited). INFERRED = meaning from usage, from
arithmetic, or from the IW8 game-test decompile (not the Replay binary). IDA was not used.

## How it was obtained

- ACTS (`-a techset,pixelshader,vertexshader,material`) on `techsets_mp_hideout`, `techsets_mp_rust`,
  `techsets_mp_m_speedball` and the **stock** `techsets_mp_shipment` (the copy in `out/agent/toon_ship/stock/`; the
  live file in the game folder is an installed edit). The map zones `mp_*.ff` hold no material bodies (0 exported):
  every material of a map is in its `techsets_` zone. 6,062 material bodies and 10,258 techniques were surveyed.
  Hideout's export had 2 failures out of 5,212 assets; nothing below depends on them.
- Shaders disassembled with `out/agent/shader_research/py/dxbc_dis.py`. The disassemblies cited here are kept, NUL
  bytes stripped, in **`out/agent/r133/asm/`** (file names `VS_<hash8>_*.asm`, `PS_<hash8>_*.asm`); line numbers
  below refer to those files.
- Survey scripts and tables in `out/agent/r133/`: `matsurvey.py` -> `mats.tsv` (per material: techset, type, texture
  slots, constants by index, the lit technique's vsData/psData), `tssurvey.py` -> `techs.tsv` (per technique: VS/PS,
  argument list), `users.py` (which techsets bind a slot), `glbcol.py` (vertex colour statistics of cached GLBs).
- Argument notation (`techs.tsv`): `C:shader:row:index` code constant, `MC:` material constant (index = constant
  slot of the material's `constantTable`), `MT:` material texture slot, `T:` code texture. Shader 2 = vertex,
  16 = pixel, 18 = both (VERIFIED by the registers each stage declares). The row is the cb2 row (code) or cb3 row
  (material) of that stage.

## 0. The time source (code constant 2)

- VERIFIED (techset args): every stock technique that animates binds **code constant 2** (`C:2:4:2`, `C:18:5:2`,
  ...). 315 techniques on the four maps do, all of them UV animation, flag/foliage waves, or effects.
- VERIFIED (asm): the shaders read only its `.w`: `mul r0.x, cb2[4].w, cb3[5].z` (VS_5c664a22:199),
  `mad o1.xy, cb3[5].xyxx, cb2[4].wwww, ...` (VS_5c664a22:206), `add r0.y, cb2[5].w, cb3[4].y` (VS_8c127290:47).
- INFERRED (game-test `r_draw_material.cpp` `R_SetGameTime` / `R_UpdateMaterialTime`, `r_scene.cpp:8499`):
  `consts[2] = (sin 2pi frac(t), cos 2pi frac(t), frac(t), fmod(t, 43200))` with `t = sceneDef.floatTime = time_ms *
  0.001`. So **`.w` is game time in seconds, wrapping every 12 hours.** The clean values in the data support seconds
  and radians: the box fan rotates at `1.74533` = 100 degrees per time unit.
- Gfx entities (`mo/` script models) can carry their own `materialTime`; the decompile restarts the same constant per
  entity (`R_UpdateMaterialTime`). For the editor, one global clock is enough.

## 1. UV animation (scrolling / rotating materials)

### Which materials (VERIFIED, `mats.tsv`, constant 63 present)

| Map | Material | Technique set | Constant 63 (u/s, v/s, rad/s, -) |
|---|---|---|---|
| hideout | `tmo/me_sign_dvd_store_02` (static model, emissive 4x) | `tmo/lit_3_lit_rpl_ta1_800000_..._93810017b_...` | (-0.27, 0, 0, 0) |
| hideout, rust | `mo/equipment_box_fan_blade_spin` (blended model) | `mo/lit_3_lit_bad_tca_802_..._938100158_...` | (0, 0, 1.74533, 0) |
| shipment | `twc/scotland_cloud04` (blended world) | `twc/lit_3_lit_bad_tca_804002_..._93810014b_...` | (-0.015, 0, 0, 0) |
| shipment | `twc/uk_periph_water_01` (water, section 5) | `twc/lit_3_lit_rpl_ta1_801010_1002000000010012_..._193810015b_...` | (0, 0.001, 0, 0) |
| hideout | `mo/electronics_train_control_monitor_static` (unlit, emissive technique) | `mo/unlit_6_effect_rpl_ta1_2188_...` | (0, -0.3, 0, 0) |
| shipment | `twc/dub_water_wall_waves_dist` (distortion, hidden in the editor) | `twc/dist_10_distortion_...` | (-0.035, -0.398, 0, 0) |

Effects (`elcq/`, rain sheets, heat haze) also use it; they are not lit surfaces. Speedball has no lit material with
UV animation. Small `x`/`y` residues such as `1.3e-08` show the tool stores (speed cos a, speed sin a).

### Technique-set token (VERIFIED)

The third hex field of the name (`13810015b` plain) gets bit **0x800000000** (`9381...`): on all 2,070 parsed lit
techsets of the four maps, that bit is set if and only if the lit technique binds `MC:2:<row>:63` (5 of 5, 0
mismatches). The water's `193810015b` also has bit 0x1000000000, which every SSR techset has (`113810015b`), see
section 5.

### The maths (VERIFIED, vertex shader; identical in world, static-model and model shaders)

Constant 63 is a **vertex** constant (`MC:2:5:63` for `tmo`, `MC:2:4:63` for `mo` / `twc`). The VS (VS_5c664a22:156-158,
199-206; VS_8d6e464d:80-88; VS_d4c0e559:161-163, 205-212):

```
T     = cb2[time row].w                              // seconds (section 0)
a     = T * c63.z                                    // rotation, radians
(s,c) = sincos(a)
d     = uv - 0.5
uv'   = (c*d.x - s*d.y,  s*d.x + c*d.y) + 0.5        // rotate about the texture centre
uv'  += T * c63.xy                                   // then scroll, UV units per second
```

The pixel shader then applies the material's own UV transform (psData row 1: scale, rotation, offset; research 95)
to the animated coordinate (PS_9786960c:104-114 work on the interpolated `v2`). So: **animate in the VS, transform
in the PS**. All textures of the material (colour, normal, emissive, opacity) use the animated UV.

### Editor state

- `material_cache.cpp:611` keeps only constants 18, 36, 103, 104 as floats (`Entry::consts`); constant 63 is read
  from the ACTS body but dropped. vsData is not kept at all. The constant table literal is enough (63 is a plain
  vec4).
- `vs_tex` (render.cpp:454-476) has no time; the only clock is `wind.z` (b3, the sway preview, off by default).

### Recipe

1. Cache: add 63 (and 79-82, 110, 111 for sections 5/6) to the kept constants at material_cache.cpp:611 and bump
   `constRules` (line 227) so cached entries resolve again.
2. Per batch: `float4 uvAnim` = constant 63 (zero = static). Global: `float4 gameTime = (sin, cos, frac, fmod(t,
   43200))` from a wall clock in seconds (add to the `Game` cbuffer b4 next to `game0`).
3. `vs_tex`: if `any(uvAnim != 0)`, apply the formula above to `i.uv` before output. If the editor bakes the psData
   UV transform into vertex UVs on the CPU, move that transform after the animation (or the rotation centre is
   wrong); for these five materials the transform is identity.
4. The viewport must redraw while an animated batch is visible (a 30 Hz timer, or only with a "Animate materials"
   toggle so idle CPU stays zero).

## 2. Detail colour and detail normal maps

- **No stock lit shader on the four maps samples a detail colour or detail normal map.** VERIFIED by the slot survey
  (`users.py MT:36`): texture slot 36 is bound by **one** lit techset, `mco/lit_3_lit_alt_tom_808810_..._13830f15b_...`
  (8 materials, all `mco/mtl_highway_flag0*`, two per map). There it holds `highway_flag_0_c` and is read only for
  **thermal vision**:
  - `sample ... r4.w, v2.xyxx, t5.yzwx` (PS_46ecf30e:175, red channel, second UV set) ->
    `mad_sat r11.x, r4.w, r12.z, r11.y` (line 176) with `r12.z`, `r11.y` = bytes of `cb3[2].y` (lines 143-145), the
    psData bytes research 95 identifies as thermal heat.
  - `r11.x` reaches only the thermal output `r2` (lines 1380-1399); line 1400 `movc r0, r0.z, r2, r1` selects it only
    when `cb7[13].x` asks for thermal (lines 112-119).
  - Slot 104 (`_infrared` on other materials) is the same family. Research 66's label "36 detail colour" is wrong for
    these maps: it is a thermal heat map.
- No texture in any slot is named `*detail*` as a separate map (the matches are layer textures of layered bodies).
- The only "two scales of one normal map" path is the water's dual scrolling normal (section 5.3).
- Constant 37 (8 techsets, care packages, lit glass) is a colour multiplier `(1..16, ..., 1)`, not a detail scale
  (values VERIFIED, use not traced).

**Recipe: nothing to add.** Ignore slot 36 / 104 outside a thermal view.

## 3. Parallax and heightmap displacement

Research 98 section 5 covers the world displacement data (slot 95 + L, constant 45 + L, the aux `lo`/`hi` stream, the
VS / HS / DS). This section adds the exact pixel maths and two uses research 98 did not list.

### Where (VERIFIED, `mats.tsv` maxDisplacement != 0 and lit args)

| Map | Materials | What runs |
|---|---|---|
| speedball | `twc4/14n_15dn_16n`, `twc4/14n_4n_15dn_16n` (34 triangles, hexB `...0018`) | displacing VS + tessellation + **pixel parallax** (PS binds slot 96: `MT:16:12:96`) |
| hideout | six `twc4/168n_*_236dn*` blends (hexB ends `0`) | displacing VS (`MT:2:0:96`) and WORLDTESS; **no** pixel parallax (PS 09a33829 / 6916afc4 bind no height slot) |
| hideout | `tmo/stone_wall_01` (static model) | VS displacement only (`MT:2:3:95`, PS bce5a6cd = the plain model PS) |
| hideout | `mo/skl_sand_dune_01_blend` | VS displacement (`MT:2:2:95`) |
| shipment | `tw/frag_metal_plates_rusty` | displacing VS + WORLDTESS (`MT:2:0:95`, `MT:8:0:95`) |

The displaced layer's token is bit 0x1000 of its third hex field (`13811015b`, VERIFIED on all of the above).

### Static-model displacement (VERIFIED, VS_36fc68f3:231-235)

```
h  = t95.SampleLevel(uv, 0).r * c45.x + c45.y        // c45 = (2, -1) on stone_wall_01: +-1 unit
P += normalize(N) * h                                // r4 = normalised r7 (the vertex normal, INFERRED from use)
```

No amplitude stream, no tessellation. The editor already holds the model's normals and UVs; it needs slot 95's image
and constant 45.

Status 2026-11-15: drawn in `vs_tex_inst` (per-mesh record row 30 = on, scale, bias; the height image at VS t43 through
a `"\x07"` data key; the material cache keeps constant 45, constRules 7). The UV is taken as the raw UV0 (INFERRED).
A/B: `IW8R_NO_SMODEL_DISP`.

### Pixel parallax of the residual (VERIFIED, PS_216e2c87:100-170)

Inputs: `E = DISPLACEMENT_EYE_BIAS.xyz` (eye vector), `bias = DISPLACEMENT_EYE_BIAS.w`, `scale = DISPLACEMENT_SCALE.y`
(both from the VS, research 98), `N = normalize(NORMAL)`, height image slot 96 (`t12`), `cb7[114].w` =
`r_tessellationHybrid` (INFERRED name, default 1).

```
// lines 108-137: screen-space derivatives give the eye vector's image in UV space for each UV set
g      = (E projected on the surface, via ddx/ddy of the camera-relative position and of N)   // r3.xy
dUV0   = g * d(uv0)/d(screen),  dUV1 = g * d(uv1)/d(screen),  dLM = g * d(lmUV)/d(screen)   // r4.xy, r5.xy, r7.xy
s      = 1 / sqrt(max(0.01 |E|^2, |E| dot(E, N)))                                           // lines 141-151
h0     = scale * t12(uv1).r + bias                                                          // lines 152-153
h1     = scale * t12(uv1 + s h0 dUV1).r + bias                                              // lines 154-157
f      = saturate(1 / (2 - h1 / (h0 == 0 ? 1 : h0)))                                        // lines 158-163
k      = s * h0 * f * cb7[114].w                                                            // lines 164-166
uv0 += k dUV0;  uv1 += k dUV1;  lmUV += k dLM;  P += k E                                    // lines 167-170
```

Two taps, no loop. Everything after uses the shifted UVs and position.

### Recipe

- Low priority: 34 triangles on Speedball have parallax; elsewhere only vertex displacement of 1-2 units.
- The CPU path `stock_displacement.*` already displaces world surfaces. Add the static-model rule above for
  `tmo`/`mo` bodies with slot 95 (keep slot 95 + constant 45 in the cache).
- Pixel parallax: only after the CPU displacement stores the per-vertex residual; then port the block above for the
  `...0018` techsets.

## 4. Static-model vertex colour (VERIFIED)

- The `c` in the prefix (`tmco`, `tm2co`, `tmlc`, `mco`, `m2co`, ...) = a `*_VERTCOL_*` material type. On the four
  maps: hideout 313 vertex-colour bodies (172 static-model), rust 125 (95), speedball 86 (58), shipment 102 (59).
- Pixel shader (PS_033abcdb, `tm2co`/`tmco` set): input `COLOR` v1, mask `xyz` used (header). After the tint:
  `mul r7.xyz, r7.xzwx, v1.xyzx` (line 154): **albedo *= vertex RGB**. The alpha is not read by the PS. The plain
  model PS (bce5a6cd, `tmo`) has no COLOR input (research 114 table).
- Vertex shader (VS_bab2e306:158-173, 217-225, 693): 4 bytes per vertex from a raw buffer (t66 via the surface table
  t63/t67, so per model surface, shared by all placements, INFERRED); RGB **sRGB to linear**
  (`(c/255 + 0.055)/1.055)^2.4`, linear below 10.0164/255 with `c * 0.000304`), alpha = byte / 255.
- The alpha is the wave weight of the flag / foliage vertex animation (section 6; VS_8c127290:94-99 multiplies the
  wave height by colour byte 3 / 255). INFERRED the same in the foliage VS (VS_227eb095:291, `r3.w`). The water VS itself outputs colour alpha 1
  (VS_8c127290:164) after using byte 3 for the waves.
- World `twc` surfaces do the same (PS_9786960c:194 `mul r5.xyz, r5.xyzx, v1.xyzx`; VS_8c127290:151-162 the same
  decode; research 98 D:182). `twc4` bodies use the colour as layer weights instead (research 97/98).
- Data in the editor's cached GLBs (`glbcol.py` over 4,000 LOD0 files of the model cache): 1,083 of 6,197
  primitives carry `COLOR_0`; every primitive whose `extras.sourceFlags` has bit 0x1 has it and none without
  (flags 65, 193, 73, 67, 69, 77, 81 vs 64, 192, 72, ...). 2.66 M coloured vertices, 96 % with luminance >= 0.9, but
  whole props are strongly tinted: `tm_sign_telecom_03` mean 0.33, `tm_kitchen_01_hanging_metal_spoon_clusters_*`
  0.30, `tm_foliage_grass_dry_arid_clump_01_reactive` 0.33, `tm_appliance_soda_fountain_single_01` 0.0.

### Editor state

- `glb_mesh.cpp` reads POSITION, NORMAL, TEXCOORD_0 only (line 261); `COLOR_0` is dropped.
- render.cpp:938: `albedo = srgbToLinear3(t.rgb) * (stockRef ? 1 : i.col.rgb)`: the stock world's vertex colour is
  not applied either (its `i.col` carries an editor shade), although `StockWorld::vertexColor` is loaded.

### Recipe

1. `glb_mesh`: read `COLOR_0` (UNSIGNED_BYTE normalised or float) into the prop vertex colour.
2. Apply only when the material is a VERTCOL type (the cache keeps `materialType`; or the `c` in the techset prefix):
   `albedo *= srgbToLinear3(vertexRGB)`. GLB bytes are the raw stored bytes (INFERRED: ACTS copies them), so
   linearise in the shader as the game's VS does.
3. Stock world: pass `StockWorld::vertexColor` RGB for single-layer `twc` bodies in a separate attribute (not `i.col`)
   and multiply the same way.

## 5. Stock water

### What exists on the four maps (VERIFIED)

There is **no dedicated water shader** (no refraction, no depth fade, no transparency) on these maps. Water-like
surfaces are ordinary lit materials with extra features:

| Map | Material | Features (from the techset arguments and asm) |
|---|---|---|
| shipment | `twc/uk_periph_water_01` (MTL_TYPE_WORLD_VERTCOL, opaque `rpl`) | UV animation (section 1), **dual scrolling normal map**, SSR, vertex colour tint, **vertex waves** |
| shipment | `tw/asphalt_road_01_wet` | SSR only (hexB `...0012`) |
| speedball | `tw/water_puddle_tiling_01_rvl_ssr`, `twc/water_puddle_muddy_01_rvl_ssr` | blended decal + SSR (research 98) |
| shipment | `twc/dub_water_wall_waves_dist` | `dist_10_distortion` (screen distortion, scrolling), hidden in the editor (research 132 G13) |

The only code textures lit techniques bind are 79, 6, 0, 9, 10 (`techs.tsv`). In the water PS, code texture 6 (`t4`,
the resolved previous frame) is sampled only inside the SSR reprojection with `cb7[124..129]` (PS_9786960c:848-939),
the path of research 104. It is not a refraction.

### 5.1 Material data of `twc/uk_periph_water_01` (VERIFIED, `mats.tsv`)

- Textures: 0 `water_metropolis_01_c` (+ defaultspec), 9 `water_metropolis_01_n` (+ white gloss), 59 `$black`.
- vsData (lit buffer, 144 B): rows 4-8 = constants 79 `(0.04, 0, 0.1, 0.5)`, 80 `(0, 1.2566, 6.2832, 0.3)`,
  81 `(1, 0, 0, 0.3)`, 82 `(1, 0, 0, 0)`, 63 `(0, 0.001, 0, 0)` (args `MC:2:4..8`).
- psData (176 B): rows 0-3 as research 95 (72, 0, 9, 54), row 9 = constant **110 `(1, 0.5, 0.000873, 0.049992)`**,
  row 10 = constant **111 `(1, 2, -0.001745, 0.099985)`** (args `MC:16:9:110`, `MC:16:10:111`). 110.zw =
  0.05 x (cos 1 deg, sin 1 deg), 111.zw = 0.1 x (cos -1 deg, sin -1 deg): two scroll vectors in UV per second.
- Token: hexB bit **0x10000** (`1002000000010012`): set on this techset only, and it is the only lit techset binding
  110/111 (1 of 1). hexB last digit 2 = SSR (research 98).

### 5.2 Vertex waves (VERIFIED, VS_8c127290:39-99, 125-184)

`T = cb2[5].w`, `c79..c82 = cb3[4..7]`, `c63 = cb3[8]`.

```
speed  = c79.x + c79.w * tri(frac(c79.z * 0))        // line 39: the world VS multiplies by literal 0 -> c79.x + c79.w
phase  = speed * (T + c79.y)                         // lines 47-48
uv     = UV animation (section 1, c63)               // lines 50-65
q      = 1 - uv
h  = sin(phase*c80.z + dot(q, c80.xy)) * c80.w       // lines 66-70
   + sin(phase*c81.z + dot(q, c81.xy)) * c81.w       // 72-82
   + sin(phase*c82.z + dot(q, c82.xy)) * c82.w       // 73-87
h *= vertexColour.a                                  // lines 94-99 (byte 3 / 255)
P += h * N_vertex                                    // lines 125-146
slope = (cos(..)*c80.xy*c80.w + cos(..)*c81.xy*c81.w + cos(..)*c82.xy*c82.w) * alpha * 0.025   // lines 71-89, 163
N' = normalize(N + slope.x * T' + slope.y * B')      // lines 163-184, then the tangent frame is rebuilt
```

`tri(x) = |1 - 2 frac(x)| * 2 - 1`. In the static-model and model versions of the same VS family the `0` is the sum of
the placement origin components / 4096, which gives each placement its own speed (VS_227eb095:253-268, INFERRED
meaning). The 23 lit VS of flags and foliage on the four maps (`tmco` grass and trees, `*_flag*`, tarps,
`mtl_highway_flag0`) all have this three-sine shape on constants 79-82 (`sincos` x 3 each). On the water the
amplitude is 0.3 + 0.3 units: a slow bob, visually minor.

### 5.3 Dual scrolling normal map (VERIFIED, PS_9786960c:104-187, 466-474)

```
uv     = psData UV transform (row 1) of the animated TEXCOORDS                    // 104-114
albedo = t1(uv).rgb * tint * vertexRGB                                            // 115-137, 194
uvA    = uv * c110.y + T * c110.zw                                                 // 138-139  (T = cb2[5].w)
uvB    = uv * c111.y + T * c111.zw                                                 // 140-141
nA = t2(uvA), nB = t2(uvB)                                                        // 145, 152 (slot 9, s3 wrap)
fadeX  = saturate((mipCount - 1 - lod(t2, uvX)) * 0.5)                            // 142-144, 176-179
dX     = octahedral normal of nX (research 95 decode with strength k)             // 146-175
n.xy   = dA.xy * fadeA * c110.x + dB.xy * fadeB * c111.x                         // 180-183
gloss  = saturate(((nA.x*(c0+1)+c1) * c110.x + (nB.x*(c0+1)+c1) * c111.x) / 256)  // 147-149, 153, 186-187, 466
cavity = nA.z * c110.x + nB.z * c111.x                                             // 184-185
nT     = normalize(n.xy, 1 - 0.6 |n.xy|^2)                                         // 470-474, as research 95
```

The fade flattens each layer as its mip level nears the last one (distant water goes flat and glossy). With both
weights 1 and gloss remap from cb3[3].y, the sum is saturated: the water is near-mirror.

### Recipe

1. Cache constants 110, 111 (and 63, 79-82). Per batch: two `float4` (`dualN0 = c110`, `dualN1 = c111`), flag on.
2. In the Game Lighting PS for that batch: sample the normal map twice with `uvA`/`uvB` above, sum the decoded
   tangent normals weighted and faded (`CalculateLevelOfDetail` and `GetDimensions` give the same lod and mip count),
   sum the gloss, then light as usual. Use the existing probe + SSR path (render.cpp:1089-1138) for the reflection.
3. Albedo x linearised vertex RGB (section 4). Vertex waves optional (sub-unit).
4. No refraction, transparency or depth fade is needed for stock MP water on these maps.

**Status 2026-11-14:** recipe items 1-3 are in the editor. The material cache keeps constants 110 / 111
(constRules 5), the facts carry them (`dualNormal`), a world batch's record holds them in rows 12-13 of its 256-byte
slot (`matDual0` / `matDual1`), and the Game Lighting PS decodes the two scrolling layers with the mip fade, sums the
gloss (each remapped, then saturated) and the cavity. The SSR and UV-animation paths were already there. The gloss
sum's byte units (`/ 256`, `c0 + 1`) are mapped onto the material's own gloss remap (INFERRED equivalent). Bench
(Shipment, camera `3000 -600 400 10 -14 80`): the water shows finer ripples and a stronger sun glint;
`IW8R_NO_DUAL_NORMAL=1` = one layer (A/B); 2.47 against 2.34 ms. The vertex waves on the water (0.3 units) are not
drawn (world batches have no wave path; sub-unit).

## 6. Flag / foliage vertex animation (found on the way)

Constants 79-82 (vertex rows) drive the three-sine wave of section 5.2 on 46 lit techsets (163 materials with
constant 100, 26 with 105 in related foliage sets; `users.py`). It shares the time source of section 0 and the vertex
colour alpha of section 4. The viewport's sway preview (`foliage_sway`, render.cpp:457-463) is a different, made-up
motion. A port of 5.2 per vertex (position + normal, alpha-weighted, per-placement speed) would replace it; the flag
constants range up to 7.5-19.6 units amplitude (`mco/mtl_highway_flag0` row 82 `(19.58, 5.25, 7.54, 1.7)`), so flags
visibly wave. Not decoded further here: the per-placement speed (t76 origin) and the foliage-only constants 100/105.

## 7. SELFVIS (static-model self visibility)

Research 105 section 4 already has the format, the data and the PS use; this note confirms the PS lines and the
editor gap only.

- VERIFIED (PS_033abcdb:83-87, 454-466, 487-495): `vis = poly(c = dot(normalize(SELFVIS.xyz), N)*.5+.5, w =
  SELFVIS.w)` (the research 95 polynomial), `ao = min(|SELFVIS.xyz| * vis, gtaoTerm)` (line 462), then the
  multi-bounce `ao / (1 - min(albedo, .85)(1 - ao))` on the baked vertex lighting (lines 487-495). `1 - ao` also feeds
  the specular occlusion term (`sqrt(1 - ao)`, lines 499-504; INFERRED meaning).
- 1,573 / 619 / 370 / 616 bodies on hideout / rust / speedball / shipment are SELFVIS types.
- Editor: not available. The GLB export skips vertex +8 (research 105 5.5), so it needs the ACTS change described
  there; the PS part is one `min()` with the viewport's own AO.

**Status 2026-11-14 (SELFVIS):** in the editor. ACTS fork 3667413c (pushed, deployed; old exe in
`build\bin\Release\prev-20261114-selfvis`) writes the vertex +8 word as the GLB attribute `_SELFVIS` (VEC4
UNSIGNED_BYTE, not normalised); checked on `military_shooting_targets_05_lod010v61`: 141 vertices, amount 0.65 / 0.95 /
1.00, bent normal . normal 0.89 (= research 105's table). Core `decodeSelfVisibility` and `ImportedMesh::selfVis`
(turned like the normals) with `sourceFlags`; the model cache records `selfvis` per entry and fetches again, in the
background, the drawn models cached before (`ModelCache::refreshGeometry`: geometry step only, the old GLB stays until
the new one is in; their LOD GLBs are fetched again). Prop vertices carry it as four signed bytes (surfaces flagged
0x40), vs_tex_inst turns it by the placement, and the stock PS takes `min(|sv| vis(c, w), screen AO)` into the
multi-bounce ambient and the specular occlusion. Hideout bench: 1,448 models fetched again in about 3 minutes (6 had
no attribute); dense grass in shade loses most of its ambient, open surfaces barely change; 9.0 ms.
`IW8R_NO_SELFVIS=1` = neither the fetch nor the term (A/B).

## 8. Implementation recipe, by visual impact

1. **SELFVIS** (research 105 5.5 + section 7): every static model's ambient; grass loses up to 64 % (mean |SELFVIS|
   0.36). Needs an ACTS GLB attribute, then `ao = min(|sv| * vis(c, w), ssao)` in the prop path.
2. **Static-model and world vertex colour** (section 4): load `COLOR_0` in `glb_mesh`, linearise, multiply albedo for
   VERTCOL materials; pass `StockWorld::vertexColor` RGB for single-layer `twc`. Cheap; 1 in 6 prop primitives,
   a few strongly tinted.
3. **Flag / foliage waves** (sections 5.2, 6): one time constant + constants 79-82 per batch; replaces the made-up sway
   with the game's motion. Needs the redraw timer of item 4.
4. **UV animation** (section 1): constant 63 per batch + the global time; 5 lit materials and 1 emissive on these maps
   (DVD sign, fan blade, clouds, water). Add a redraw timer while one is on screen.
5. **Shipment periphery water** (section 5): dual scrolling normals + gloss sum + SSR path + vertex tint; one material.
6. **Displacement** (section 3): static-model VS rule (stone wall, sand dune), then pixel parallax for the 34
   Speedball triangles.
7. **Detail colour / normal**: nothing; slot 36 / 104 are thermal-only (section 2).

Cache change for 1-6: keep constants 45, 63, 79-82, 110, 111 (material_cache.cpp:611) and slot 95's image, bump
`constRules`; time = `(sin, cos, frac, fmod(t, 43200))` of wall-clock seconds.

## What is not determined

- The Replay values of code constant 2 were not read from the exe (the meaning is from the game-test build and the
  data; the shaders only use `.w`).
- The number of placed surfaces / triangles per animated material (only material bodies were counted).
- The per-placement wave speed source in the static-model VS (t76 + 0 .. +20 is read as origin / 4096 and scale;
  INFERRED) and the foliage constants 100 / 105.
- Whether ACTS writes GLB `COLOR_0` as raw (sRGB) bytes; the recipe assumes it.
- Slot 104 in the flag PS is sampled (line 173) but its value is overwritten before use in the paths read.
