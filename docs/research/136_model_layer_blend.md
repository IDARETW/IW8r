# 136. Model-layered materials: layer weights, UV sets and blend modes (Replay 1.20)

**Why.** Research 132 G9 (status 2026-11-14): the viewport draws the multiply layers (colour mode 1) of model-layered
stock materials (`tmo`, `tmco`, `tm2o`, `tm2co`, `mo`, `mco`, `m2o`, `m2co`) but leaves out the colour-lerp layers
(mode 0). With weight 1 they covered whole surfaces on King: `mout_container_wall_single` layer 1 (flags 0x47) turned
the wall into door texture, and `tm2co/sign_un_variation_01_combined` showed its whole alphabet atlas. Modes 2/3 (normal,
cavity and gloss only) are not drawn either. This note reads the per-layer weight, UV set and blend from the game's pixel
shaders and gives a port recipe for the prop path.

**Labels.** VERIFIED = read in the disassembly, the techset/material export, the editor's caches or a cached GLB
(cited). INFERRED = from usage or arithmetic, not read in a shader. The DXBC has no RDEF block, so register names are
ours. Nothing was run in the game.

## How it was obtained

- Shaders: ACTS `-a pixelshader,vertexshader -n <hash>` (and `-a techset -n <name>`) on `techsets_mp_rust.ff`,
  `techsets_mp_m_speedball.ff`, `techsets_mp_hideout.ff`, `techsets_mp_m_king.ff`; disassembled with
  `out/agent/shader_research/py/dxbc_dis.py`. Exports and listings: `C:\zsw-iw8r\layers\{r,b,h,h2,h3,k,k2}`. Line
  numbers below are the same in the raw listing and its NUL-stripped copy (`tr -d '\000'`).
- Material and techset data: `out/agent/r133/mats.tsv`, `techs.tsv` (hideout, rust, speedball, shipment); the
  editor's material cache `index.json` (King, bm_tut) and model cache GLBs (King).
- Scripts (read only, `C:\zsw-iw8r\layers\py`): `laysurvey.py` (layer kinds and flags per material), `glbstat.py`,
  `glbgroups.py`, `glbtris.py` (GLB COLOR_0 / TEXCOORD_1 per part), `alphasurvey.py` (vertex alpha of layered models).

| Tag | PS | Techset (lit technique `TECHNIQUE_LIT_FORWARDPLUS_BITMASK`) | Notes |
|---|---|---|---|
| M2 | `fb71be8a` | `tmo/..._1002..._0_2_1_..._3810015f_2_1_2_0_0` | research 107; no COLOR input |
| M3 | `98b22267` | `tmo/..._3002..._0_3_1_..._410005b_..._3810014b_2_0_2_0_0` | slot 126 mask, layer 1 is an overlay |
| C3 | `fd48562f` | `tm2co/..._1002..._0_3_1_..._3810015f_0_0_2_0_0_0_0_3818015f_0_0_2_0_0` | King's `tm2co/mout_container_wall_single` (Rust export, same name) |
| K3 | `d3f4111e` | `tm2o/` with the same tokens as C3 (King export) | King's `tm2o/mout_container_wall_single` |
| S3 | `d0eca7d3` | `tm2co/..._0_3_1_..._3818015f_..._3810015f_0_0_2_0_0` | `sign_un_variation_01_combined` |
| T3 | `210058e9` | `tm2co/..._0_3_1_..._38180157_3_1_2_0_0_0_0_410005f_0_0_2_0_0` | UV1 layer + overlay with vertex colour |
| O2 | `73fb5ac1` | `m2o/..._0_2_1_..._418005f_0_0_2_0_0` | overlay on UV1 |
| Q4 | `a6f2dedc` | `tmo/..._3002..._0_4_1_...` | 4 layers, slot 126 |
| V | VS `7dd512ac` | the VS of C3, S3, T3 | COLOR, SECONDARY_TEXCOORDS, SECONDARY_NORMAL |

Confirming shaders (same rules): `1a0924d7`, `c305cdad`, `0c487bf9`, `ed78cbaa` (tm2co); `12272871`, `0f9d19d5`,
`494785e3` (tm2o); `a815a40e` (m2o); `517b6a59`, `f09c9eae`, `89d200af` (tmco); `914763b5` (mco); `7bee1e86`,
`c70700b5` (tmo); M4 `fd62a749` (mo); `7eb91b25` (code layer, section 1.5).

## 0. Summary

1. **One weight per vertex, shared by every layer: w = vertex colour alpha** (byte 3 / 255) in the vertex-colour
   families (`tmco`, `tm2co`, `mco`, `m2co`: the `c` in the prefix). Families without a colour stream (`tmo`, `tm2o`,
   `mo`, `m2o`) have **w = 1 compiled in**. Layer L's blend factor is then
   `b = (flags&16 ? ramp(reveal, w, k) : w) * (flags&8 ? alpha : 1) * mask`, the world rule of research 97 with this w.
   The vertex RGB multiplies the albedo **after** all layers.
2. **Mask.** A techset whose hexB starts with `3` (`3002...`) binds **slot 126**, sampled at the untransformed UV0:
   channel R weights layer 1, G layer 2, B layer 3. It is always applied. Hex `1002...` sets use slot 123 (layer 1) and
   124 (layer 2) under flags & 32. All 586 + 52 such bindings in the four zones are `$white`.
3. **UV set.** A layer token with bit **0x80000** (`3818...`, `3878...`, `418...`) samples **UV1** (TEXCOORD_1) in the
   SECONDUV families (`tm2o`, `tm2co`, `m2o`, `m2co`). Its normal is turned into the UV0 frame by a per-vertex 2x2
   (`SECONDARY_NORMAL`). The bit is ignored where the family has no second UV. All other layers sample UV0. Every layer
   has its own UV transform (row 1+3L), and that transform applies to the UV set the layer uses.
4. **Modes** (standard layers, token `38......`): `mode = flags>>7 & 3`. 0 lerps colour and spec code, 1 multiplies
   the colour, 2 and 3 leave colour and spec code alone (mode 3 does not even sample a colour). In every mode the
   normal, cavity and gloss blend by flags 1/2/4 and `nmode = flags>>9 & 3` (0 lerp, 1 add, 2 none). This is research
   107 s3, confirmed in C3/S3/T3.
5. **Overlay layers** (token `04......`, 157 of 887 layer slots) ignore the mode bits. They are compiled **soft-light**:
   `t = 0.5 + b (sRGB(c tint) - 0.5)`, `albedo = albedo^2 (1-2t) + 2t albedo`. Their normal is always added; cavity and
   gloss move toward `min(below, layer)`. Flags only gate them (8, 16, 32). The editor's decoder reads their flags (0 or
   8) as mode 0, so drawn as lerps they would paint the detail texture over the whole model.
6. **King.**
   - On `tm2co/mout_container_wall_single`, layer 1 has b = vertex alpha. The GLBs carry alpha 0 on the corrugated
     panels and 255 on the door parts (wall_32: 392 of 604 triangles at 0). Drawing with w = 1 made the panels door.
   - The `tm2o` variant (no colour) is only on the door models; there w = 1 is the game's own value, and "all door" is
     right.
   - The sign's alphabet layer samples UV1, which picks one letter: A, B or H by model. It was drawn at UV0, so the
     whole atlas showed.
   - The editor lacks three inputs: vertex alpha in the layer pixel shader (the vertex buffer has it), TEXCOORD_1
     (the GLB has it, the importer drops it) and the slot-126 image (not cached).

## 1. The layer code (VERIFIED)

### 1.1 Inputs by family

| Prefix (material type) | PS inputs | w | Second UV |
|---|---|---|---|
| `tmo`, `mo` (STATIC_MODEL_SELFVIS, MODEL) | TEXCOORDS | 1 (M2:175-180, M4:177-179, `7bee1e86`:176-178) | none |
| `tm2o`, `m2o` (`..._SECONDUV_...`) | + SECONDARY_TEXCOORDS, SCEONDARY_NORMAL (sic) | 1 (K3:184-186, 303-305) | yes |
| `tmco`, `mco` (`..._VERTCOL_...`) | + COLOR (xyzw) | COLOR.a (`517b6a59`:181-192, `914763b5`:184-195) | none |
| `tm2co`, `m2co` (`..._SECONDUV_VERTCOL_SELFVIS`) | COLOR + both secondaries | COLOR.a (C3:189-200) | yes |

VS V (tm2co):
- COLOR = one u32 per vertex. Bytes 0-2 are sRGB-decoded into `.rgb` (V:174-177, 243-250); `.a` = byte 3 / 255
  (V:178-180, `mov o1.w` at V:727).
- SECONDARY_TEXCOORDS = a half2 from its own per-surface stream (V:185-194).
- SECONDARY_NORMAL = 4 bytes, `b * 2/255 - 1` (V:195-201, 251-259). This is the model counterpart of the world's
  NORMAL_TRANSFORM (research 97 s1).

ACTS writes these streams to the GLB as follows:
- The colour u32 goes to `COLOR_0` (UNSIGNED_BYTE normalised), so `COLOR_0.a` is byte 3, the game's w.
- The second UV goes to `TEXCOORD_1` (`atian-cod-tools/src/core/acts/tools/mw19/mw19_mesh.cpp:155-162, 209-234`).
- SECONDARY_NORMAL is not exported.

### 1.2 Standard layer L >= 1 (token `38......`; C3 layer 1 = C3:168-297, layer 2 = C3:299-423)

```
uv      = transform_L(token & 0x80000 && family has UV1 ? UV1 : UV0)        // row 1+3L .yzw (C3:168-178; UV1: C3:299-309)
(a, r)  = flags & 24 ? tex[27+L](uv).rg : (0, 0)                         // C3:179-184, mip bias + cb7[175].y
k       = (row 2+3L .x >> 24) / 255                                       // C3:186-188
lo, hi  = saturate((1-w) - k sqrt(w)), saturate((1-w) + k sqrt(1-w))      // C3:189-195 (fast sqrt 0x1fbd1df5)
b       = (flags & 16 ? saturate((r - lo) / (hi - lo)) : w)               // C3:196-202 (fast rcp 0x7ef311c2)
        * (flags & 8 ? a : 1)                                             // C3:203-205
        * maskL                                                           // 1002: flags & 32 ? tex[122+L](UV0 raw).r : 1 (C3:206-211, 330-335)
                                                                          // 3002: tex[126](UV0 raw)[L-1], always (M3:108, 186, 267; Q4:110, 188, 302, 367)
if b >= 1/255:                                                            // C3:212
    tintL = row 2+3L .x bytes 0..2 / 255; flags & 64: tintL = lerp(1, tintL, tex[59+L](uv).r)   // C3:214-225
    nL    = octa(tex[9+L](uv).yw), strength row 3+3L .y byte 2           // C3:226-246
            UV1 layer: nL.xy = (dot(nL.xy, SN.xy), dot(nL.xy, SN.zw))    // C3:371-372 (SN = SECONDARY_NORMAL)
    mode  = flags >> 7 & 3
    if mode != 3: c = tex[L](uv).rgb * tintL                             // C3:248-251
      0: albedo = lerp(albedo, c, b); s = lerp(s, specRemap(tex[L].a), b)   // C3:252-262
      1: albedo *= lerp(1, c, b)                                          // C3:264-267, 284
      2: unchanged (the soft-light path is computed and discarded)       // C3:268-284
    bN, bC, bG = (flags & 1, 2, 4) ? b : 0                                // C3:287-288
    if nmode != 2:                                                        // C3:297
      n.xy   = nmode == 0 ? n.xy (1 - bN) + bN nL.xy : n.xy + bN nL.xy   // C3:289-292
      cavity = lerp(cavity, tex[9+L].z, bC)                               // C3:293-294
      gloss  = lerp(gloss, glossRemap(tex[9+L].x), bG)                   // C3:295-296
after all layers: albedo *= COLOR.rgb (vertex-colour families)            // C3:425, T3:339
```

- With w = 1 compiled in, the ramp reduces to `saturate(r * rcp(k * 0))`, a step at r > 0 (M2:175-180). With w = 0,
  lo = hi = 1 and b = 0.
- Layers run in order 1, 2, 3, each over the result so far (C3 block 1, then block 2).
- Slots, by layer: colour L; normal + gloss 9+L; alpha + reveal 27+L; tint mask 59+L; mask 123/124 or 126. Rows:
  flags + UV transform 1+3L; tint + k 2+3L; spec/gloss remap and normal strength 3+3L; row 0 shared. These are research
  97's slots and rows, read from the args of C3 (`MT:16:4:1 ... MC:16:7:2`).

### 1.3 Overlay layer (token `04......`: `410005b`, `410005f`, `418005f`) (VERIFIED, O2:164-251, M3:160-240, T3:249-338)

```
uv, a, r, b       as 1.2 (same w, same mask rule; only flags 8, 16, 32 are read: O2:181, M3:177, T3:266)
if b >= 1/255:
    s      = sRGB_encode(tex[L](uv).rgb * tint_L)                         // O2:198-205, 230-236 (0.798354 = 255 x 0.0031308)
    t      = 0.5 + b (s - 0.5)                                            // O2:237-238
    albedo = albedo^2 (1 - 2t) + 2t albedo                                // O2:239-243 (soft light; b = 0 or s = .5: unchanged)
    n.xy  += b nL.xy                                                      // O2:244, always add (UV1 layers turned by SN: O2:228-229)
    cavity = lerp(cavity, min(cavity, tex[9+L].z), b)                     // O2:245-247
    gloss  = lerp(gloss, min(gloss, glossRemap(tex[9+L].x)), b)           // O2:248-250
    spec code unchanged; no tint mask
```

### 1.4 Modes 2 and 3

There is nothing new beyond research 107 s3.1-3.2: the colour and spec code are unchanged, and normal, cavity and gloss
follow the block above. In the four zones: mode 2 has 42 layer slots, mode 3 has 21. Mode 3 layers bind no colour slot.

### 1.5 Code layers (token field b = 1: `0_1_180153`, `0_1_100553`, ...)

There are 14 of these (vehicle `_dirt`/`_rubble` sets). Their techsets set hexB bit `0x1000000000`, bind slot 45 and
code constants 15/19, and bind **no colour slot for the layer** (`7eb91b25` args: slots 0, 9, 10, 28, 59). Their
blend loop (`7eb91b25`:198-228) was not decoded. INFERRED: normal/gloss only. They are left out of the port below.

## 2. What selects the weight, UV and mode, and where the editor has it

| Data | Meaning | Editor cache (`material_cache/index.json`) |
|---|---|---|
| prefix `c` (materialType `..._VERTCOL_...`) | w = vertex alpha, then albedo *= vertex RGB | `materialType`; props: `main_window.cpp:7700-7719` |
| prefix `2` (`..._SECONDUV_...`) | UV1 stream exists | `materialType` |
| techset name `_0_<N>_1_0_0_` | layer count N | `modelLayers`, `techset` |
| hexB first digit `3` / `1` | mask slot 126 (R, G, B = layers 1-3) / slots 123-124 by flags & 32 | `techset`; slot 126 image **not cached** (name only, in `textures`) |
| layer token `<a>_<b>_<hex>_<cm>_<nm>_<c>_0_0` | b = 1: code layer; hex >> 24 == 4: overlay; (hex >> 27) & 7 == 7: standard; hex & 0x80000: UV1 | `techset` (parse it) |
| row 1+3L .x flags | 1/2/4 normal, cavity, gloss; 8 alpha; 16 reveal; 32 mask; 64 tint mask; >>7 mode; >>9 nmode | `layerPsData` (`decodeStockLayeredMaterial`) |
| row 2+3L | tint RGB, k (byte 3) | `layerPsData` |
| slots L, 9+L, 27+L | colour, normal + gloss + cavity, alpha (R) + reveal (G) | `layers[].color/normal/reveal` |
| slot 59+L (flags & 64) | tint mask | layer 0 only (`tintMask`); **not cached** for L >= 1 |

Parsing the layer token (VERIFIED against the shaders listed above):
- Split the techset name on `_`. Field 6 is hexB and field 8 is N.
- Layer L >= 1 occupies fields `18 + 8(L-1)` onward: a, b, hex, cm, nm, c, 0, 0.
- The `cm`/`nm` fields repeat the flags only for standard layers. The shader reads the flags (C3 is shared by
  techsets whose cm/nm differ: `tm2co/..._3810015f_0_0_2..._3818015f_0_0_2` and `..._0_1_2..._2_1_2` both use C3).
- INFERRED: hex bit 0x10 = the layer has a normal + gloss map. M3 layer 2 (`3810014b`) binds no slot 11 and has no
  normal code (M3:282-321), and `ed78cbaa` (`3818014f`) leaves SECONDARY_NORMAL unused.

Counts over the four zones (`laysurvey.py`; one entry per material per zone, 887 layer slots L >= 1):

| Kind | Slots | Notes |
|---|---|---|
| standard | 716 | mode 0: 601, 1: 52, 2: 42, 3: 21 (a few modes come from shifted rows, flags 0xffffffff) |
| overlay | 157 | flags 0x0 (most), 0x8, 0x9 |
| code (b = 1) | 14 | |
| UV1 layers | 281 | the largest group: `tm2o` standard mode 0 with alpha (152: details and decals on UV1) |
| vertex-alpha weighted | 234 | c families |
| slot-126 masked | 320 | hexB `3...` |

Lit techsets (`techs.tsv`):
- hexB `3...`: 214, all with slot 126.
- hexB `1...`: 373. 368 of them bind slot 123 (64 of those also bind 124); 5 bind neither.

Vertex alpha on the 30 cached single-material vertex-colour layered models (`alphasurvey.py`): 21,633 vertices at 0,
10,440 at 255, 6 between. In practice w is a per-triangle selector, not a gradient.

## 3. King: why weight 1 painted whole surfaces

**`tm2co/mout_container_wall_single`** (C3; psData rows 4, 7: flags 0x47, 0x4f).
- Layer 1 (door, UV0) has no alpha or reveal, so b = w.
- Layer 2 (door details) samples UV1 with b = w x slot 29.r.
- Cached GLBs (`glbgroups.py`), all with TEXCOORD_1 and COLOR_0. RGB is 255 or 252/252/248; alpha is constant per
  triangle:

| Model | Alpha 0 (painted metal, layer 0 only) | Alpha 255 (door layers) |
|---|---|---|
| `tm/mout_container_wall_32` | 306 vertices, 392 triangles (the corrugated panel; UV1 collapsed to (0, 1)) | 234 vertices, 212 triangles |
| `tm/mout_container_end_frame_closed` | 768 vertices, 976 triangles | 1360 vertices, 1390 triangles |

- **Missing input: COLOR_0.a.** The prop vertex buffer already has it (`PropVertex::color` byte 3,
  `main_window.cpp:7715-7719`). The layer pixel shader uses `w = 1` for props (`render.cpp:1003`, vs_tex_inst sets
  `vc.w = 1` at `render.cpp:558`).
- Layer 1 also has flags & 64: its tint `c1ab96` applies through slot 60, which the editor does not cache.

**`tm2o/mout_container_wall_single`** (K3; same psData and tokens, no colour stream).
- The model cache index lists it only on `tm/mout_container_end_door_left`, `_right` and `_end_frame_hinged` (King)
  and `_end_frame_open` (bm_tut).
- K3 has w = 1 compiled in, so layer 1 covers these models entirely in the game too. "All door" on the door models is
  correct. Layer 2 needs UV1 there (K3:290).

**`tm2co/sign_un_variation_01_combined`** (S3; flags 0x07, 0x0f, 0x07).
- Layer 1 is the alphabet atlas (slot 1; alpha = slot 28 `sign_un_variation_alphabet_01_c`, R = white letters). It
  samples **UV1** (S3:173). Layer 2 is the yellow plate (`sign_un_variation_01_c`, UV0, b = w).
- Per part (`glbtris.py`): the back and sides have alpha 0 (rusted base). The plate, rivets and one letter quad have
  alpha 255.
- The letter quad's UV1 picks one atlas cell: `_a_01` u 0.01.., `_b_01` u 0.18..0.36 v 1.00..1.19 (B), `_h_01` row 2.
- **Missing inputs: TEXCOORD_1 and COLOR_0.a.** Drawn at UV0 with w = 1, the letter mask spans the whole sign.
- **Open point.** By S3 the plate (layer 2, b = w = 1) is applied after the letters and covers them on every alpha-255
  triangle. The letter quad's UV0 maps the plate texture to match the plate behind it, so the letters cannot show. The
  art (one letter per variation) says they should. Either the game shows a blank plate here, or one of the inputs
  differs in the game. A look at a UN sign on King would settle it. The port should follow the shader.

## 4. Port recipe (prop path)

### 4.1 Per-vertex inputs

| Input | Game | GLB (ACTS) | Editor today | To do |
|---|---|---|---|---|
| UV0 | TEXCOORDS | TEXCOORD_0 | `PropVertex::u, v` | - |
| vertex alpha (w) | COLOR.a | COLOR_0.a (c families) | in `PropVertex::color`; non-VERTCOL props are white, so w = 1 (correct) | pass it to the PS: `o.vc.w = 1 + vcol.a` for props, `w = i.vc.w - 1` |
| vertex RGB | COLOR.rgb, after the layers | COLOR_0.rgb | applied to the base | apply it to layer colours too (they are under the multiply) |
| UV1 | SECONDARY_TEXCOORDS | TEXCOORD_1 (SECONDUV surfaces) | **dropped** (`glb_mesh.cpp:261-270` reads TEXCOORD_0 only) | `ImportedMesh::uv1`, `PropVertex::u1, v1`, a VS input and a PS interpolant; flip v like uv0 |
| UV1 normal frame | SECONDARY_NORMAL | **not exported** | - | derive it per pixel from the screen derivatives of uv1, as the base derives its frame from uv0 (`render.cpp:981`), or use identity |
| tangents | packed frame | not exported | screen derivatives | - |

### 4.2 Layer pass (`camera_view.cpp:1351-1379`, `render.cpp:4342-4398`, PS `render.cpp:1000-1042`)

1. **Classify each layer** from the techset token (section 2): standard / overlay / code, and the UV set (bit 0x80000
   and a `2` prefix). Do not trust `colorMode` for overlay or code layers. Code layers and modes 2/3 get no colour
   pass. Pass the UV set in `layerUvO.z` and the kind in `layerP.w` (0 lerp, 1 multiply, 2 overlay).
2. **Weight**: `w` = vertex alpha (4.1). Keep the ramp, alpha and step rules of `render.cpp:1003-1010`; the
   `max(hi - lo, 1e-4)` guard matches the shader's step at w = 1.
3. **Mask**:
   - hexB `3...`: export and cache slot 126. Multiply b by its channel L-1 at the untransformed UV0.
   - hexB `1...`: ignore flags & 32 (slots 123/124 are always `$white`).
4. **Tint mask**: when flags & 64, cache slot 59+L and use `tint = lerp(1, tint, mask.r)` at the layer UV.
5. **Mode 0 (lerp)**: draw it (remove the skip at `camera_view.cpp:1364`). Use the existing alpha-blended pass with
   alpha = b, in layer order, before the multiply layers that follow it. The spec code lerp is approximated by shading
   the layer with its own remaps.
6. **Mode 1 (multiply)**: as today, with the new w, mask and UV set.
7. **Overlay**: `result = shaded x f`, with `f = 2t + a (1 - 2t)` and `t = 0.5 + b (sRGB(c tint) - 0.5)`. Here `a` is
   the linear albedo below the layer. Draw it with the multiply blend and sample layer 0's colour (times its tint) in
   the pass for `a`. A cheaper first step is `a = 0.5`, which gives `f = 1 + b (s - 0.5)`. That is exact at a = 0.5
   and about 12 % off at a = 0.1, t = 0.7 (1.20 against 1.36).

### 4.3 Modes 2/3 and the normal / cavity / gloss of every layer

A blended forward pass cannot change the normal under an already shaded pixel. Two ways:

- **(a) Fold into `ps_tex`** (exact; research 132 G9). For a prop mesh with layers, bind each layer's normal + gloss
  image, its reveal/alpha image and the mask. Then evaluate section 1.2's b and the nmode/cavity/gloss rule in the base
  shader before lighting. With N <= 3 that is up to 6 more textures. Constants already exist per layer (`PropLayer`
  params).
- **(b) Re-shade pass** (approximate). Draw a pass with layer 0's colour, the layer's normal + gloss image and
  alpha = b x (flags & 1 or 4). It is exact for gloss-only layers and for nmode 0 when lighting is close to linear in
  the normal. nmode 1 (add) is approximated.

Modes 2/3 are 63 of 887 slots and are mostly subtle detail normals; (a) is the target, (b) a stopgap.

### 4.4 Expected results and checks

- King `tm/mout_container_wall_32`: the 392 alpha-0 triangles show painted metal (layer 0 tint `c1ab96` through slot
  59), and the 212 alpha-255 triangles show door plus details (UV1). `end_frame_closed`: 976 metal, 1390 door.
- Door models (`tm2o`): unchanged (all door), now with the UV1 details.
- UN signs: rusted back and sides. On the front, the plate covers the letter (section 3); keep the shader's order.
- Overlay layers (`tmo/cinderblock_01`, `tmo/building_support_wood_beam_05`, flags 0): a soft modulation of the base,
  never a texture swap.
- Core test: parse the token kind and UV bit from the techset names in this note. For example, S3's name gives layer 1
  standard UV1 and layer 2 standard UV0; M3's gives layer 1 overlay, layer 2 standard, mask 126.

## 5. What is not determined

- **Sign letters** (section 3): shader and GLB data say they are covered; the art suggests otherwise. Not checked in
  the game.
- **SECONDARY_NORMAL source.** The XSurface field ACTS would need was not identified (the VS reads it through the
  per-surface table at `t63 + 0x100004 + 8 x surface`, V:195-201). INFERRED: the u32 after the second-UV offset.
- **Code layers** (b = 1, slot 45, code constants 15/19): not decoded.
- **4-layer `1002` sets** (a slot 125 for layer 3?): none in these zones.
- **A vertex-colour material on a surface without a colour stream**: the game's default COLOR was not read. INFERRED
  white, so w = 1.
- **Fractional vertex alpha**: 6 vertices out of 32k on the models checked. The k ramp only matters there, or with a
  reveal map.
