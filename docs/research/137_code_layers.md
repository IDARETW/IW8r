# 137. Code layers of model-layered materials: the clear coat (Replay 1.20)

**Why.** Research 136 section 1.5 left the "code layers" (layer token field b = 1, e.g. `0_1_180153`, `0_1_100553`)
undecoded, and the editor skips them (`camera_view.cpp:1371`). This note reads their blend in the game's pixel
shaders, says what slot 45 and code constants 15 / 19 are, counts the materials, and gives a port recipe for the prop
fold (research 136 section 4).

**Labels.** VERIFIED = read in the disassembly or the techset / material exports (cited). INFERRED = from usage,
arithmetic or naming, not read in a shader. UNKNOWN = not established. The DXBC has no RDEF block, so register names
are ours. Nothing was run in the game, and the IDA bridge was down for this session (the exe was not read).

## How it was obtained

- Shaders: ACTS `-a pixelshader -n <hash>` on `techsets_mp_hideout.ff` and `techsets_mp_m_speedball.ff` (exports in
  `C:\zsw-iw8r\codelayers\{h,b}`), plus the existing listings below; disassembled with
  `out/agent/shader_research/py/dxbc_dis.py`. Line numbers are the same in the raw listing and its NUL-stripped copy.
- Techset arguments and material constants: `out/agent/r133/techs.tsv`, `mats.tsv` (hideout, rust, speedball,
  shipment). Warzone: ACTS `-a techset,material` on `techsets_mp_donetsk.ff` (7 of 22,219 assets failed, see its
  `manifest.json`), `techsets_common_br_mp.ff` and `techsets_mp_br_quarry.ff`, surveyed with r133's `tssurvey.py` /
  `matsurvey.py` into `C:\zsw-iw8r\codelayers\techs_wz.tsv`, `mats_wz.tsv`. (The materials live in the `techsets_*`
  zones; the map zones `mp_donetsk.ff` / `common_br_mp.ff` gave no material assets.) Counts: `codelayers\py\codesurvey.py`.

| Tag | PS | Technique set (lit technique) | Listing |
|---|---|---|---|
| CL1 | `7eb91b25` | `tmo/..._801000_1002001000000010_0_2_1_..._0_1_180153_0_0_2_0_0` (`tmo/container_dumpster_large_01_rubble`) | `C:\zsw-iw8r\layers\h2\...\7eb91b25...0.asm` |
| CL2 | `9557fc85` | `mo/..._800840_3002001000000010_0_3_1_..._17814415b_..._0_1_100553_...` (`mo/veh8_civ_lnd_decho`) | `out\agent\toon_ship\ps\...\9557fc85...131.asm` |
| CL3 | `740e8753` | the same with `13814415b` (`mo/veh8_civ_lnd_walfa_*`, `_victor40`) | `...\740e8753...96.asm` (same folder) |
| CL4 | `9ba2fb76` | `tmo/..._800800_3002001000000010_0_3_1_...` (`tmo/veh8_civ_lnd_walfa_*_no_thermal`) | `C:\zsw-iw8r\codelayers\h\...` |
| CL5 | `99db0efc` | `tm2o/..._808800_..._0_3_1_..._0_1_180143_...` (`tm2o/uk_siding_metal_corrugated_01_2sided_brown`) | same |
| CL6 | `30f76783` | `m2o/..._804800_..._0_2_1_..._0_1_180163_...` (`m2o/stationary_trainyard_post_set_01_glass_lit_spdball`) | `C:\zsw-iw8r\codelayers\b\...` |

Also checked: `fee59b73` (`mo/` dumpster), `15ffe38d` (`mo/veh8_civ_lnd_decho_interior_netting`). Every code-layer
shader above contains the coat constant `0.035614`; none of the 24 non-code layered shaders in `C:\zsw-iw8r\layers` do.

## 0. Summary

1. **A code layer is a clear coat**: a second, colourless specular lobe with its own normal and gloss and a fixed F0
   of 0.035614, laid over the finished base. It changes only the ambient / reflection-probe specular. The base's
   ambient diffuse and ambient specular are scaled by `(1 - b Ec)^2`, and `b Ec` of the coat's reflection is added.
   Albedo, the base normal, cavity and gloss, the sun and local lights are untouched (the coat normal reaches them only
   in the thermal-vision sum).
2. **Weight**: `b = (flags & 8 ? tex[27+L].r : 1) * mask126[L-1]` (hexB `3...` only). Flags 16 is never set, and
   `w = 1` (no code layer is in a vertex-colour family). No code constant, damage or dirt amount enters it. On a static
   placed model the coat is **on**, so the editor's skip is not a correct default. The effect is subtle: about 4-7 %
   face-on, up to 35-60 % reflectance at grazing angles (INFERRED, Schlick at F0 0.0356).
3. **Slot 45 and code constants 15 / 19 belong to other features**, not the code layer (correcting 136 s1.5). Slot 45 is
   the thermal map. 19 is the per-instance thermal (heat bias, scale). 15 is a per-instance index into an override table
   for layer 0's UV transform and remaps. 45 and 19 reach only the thermal-vision output.
4. **Variants** by the token hex: 0x10 = coat normal + gloss map (slot 9+L, remap constant 54+L). Without 0x10 the coat
   uses the vertex normal and a fixed gloss' of 0.193774 (gloss 0.35). 0x08 = alpha map (flags 8, slot 27+L). 0x20 =
   emissive (slot 19 x constant 37 for L = 1).
5. **Selection**: token b = 1 occurs exactly when hexB has bit 0x1000000000 (45 of 45 lit techniques, none without it
   in 7,839), and it is always the last layer.
6. **Use**: 35 materials (14 in the four MP zones, 24 in the Warzone zones, 3 shared), mostly `veh8_civ_lnd_*` car
   bodies (dirt is their layer 1, a standard layer). 15 of the 862 `veh8` materials in the Warzone zones have one.
7. **Port**: a coat block beside the two folded layers, evaluated after the ambient combine at `render.cpp:1446`. Also
   fix the remap row: constant 54+L sits on rows 6, 7, 10 or 13, not on row 3+3L.

## 1. The code layer (VERIFIED; CL1 unless noted)

### 1.1 Selection and inputs

- Token `<a>_1_<hex>_0_0_2_0_0` at layer L. The lit techniques with hexB bit 0x1000000000 are exactly those with a
  b = 1 token: 11 of 2,086 in `techs.tsv` and 34 of 5,753 in `techs_wz.tsv`. In all of them it is the last layer.
- Bindings: CL1 args `MT:16:3:10 MT:16:4:28 MC:16:4:1 MC:16:5:10 MC:16:6:55`. That is the coat map at slot 9+L, alpha /
  reveal at slot 27+L, and the flags + UV row, tint + k row and remap row. **No colour slot L.** No code constant is
  bound for the coat (CL1's C args are 164, 0, 1, 4 and 103 only).

```
uvC    = transform_L(UV0, or UV1 when hex & 0x80000 in a `2` family)      // CL1:149-159; CL5:233-238 (v6 = UV1)
(a, r) = flags & 24 ? tex[27+L](uvC).rg : (0, 0)                           // CL1:161, 166, 214-218
b      = (flags & 16 ? step(r > 0) : 1) * (flags & 8 ? a : 1)             // CL1:162-165 (w = 1: rcp(k*0)), 219-222
       * mask126[L-1]   (hexB 3...: t0 = slot 126 at raw UV0)              // CL2:115, 373 (decho: L = 2, G)
if hex & 0x10:  m = tex[9+L](uvC)                                          // CL1:167
    glossC = (m.r * (byte0 + 1) + byte1) / 256      (remap row .y)        // CL1:168-176
    nC.xy  = octa((m.ga - .5) * (byte2 + 1)/256 + .5)                      // CL1:171-188
    cavC   = m.b                                                           // CL1:224
else:   nC.xy = 0 (vertex normal), gloss' = 0.193774, cavC = 1            // CL5:639-641, 968, 973, 1235-1237
```

The tint row is not read beyond k (CL1:162). The token's cm / nm fields are 0 and unused. The loop at CL1:198-228 only
collects the two layers: L = 0 at 202-211, coat at 213-226 into `r7.z` = b, `r8.w` = gloss, `r9.z` = cavity, `r16.xy` =
normal. Nothing is blended there.

### 1.2 The combine (after the base is complete)

```
gp'C = 1 - sqrt(1 - sat(glossC))                                           // CL1:522-525 (base: 507-510)
NC   = normalize(Tv nC.x + Bv nC.y + Nv (1 - 0.6 |nC|^2))                   // CL1:526-536: the vertex frame only,
                                                                           //   the base normal map is not included
RC   = reflect(-V, NC); nvC = sat(NC . V)                                  // CL1:571-574
SC   = env radiance along RC at gp'C: the light-grid ambient along RC      // CL1:588-591
       (no probes), else the probes with the cb7[186] fade and the gtso    // CL1:600-612, 789-853, 868-886
       spec occlusion at (max(sqrt(1-ao), sqrt(1-cavC)), alphaC, sat(Nsv . RC))
(A, B)C = envBRDF_LUT(nvC, gp'C);   Ec = 0.035614 A + B                    // CL1:888-892
T    = 1 - b Ec                                                            // CL1:895-896
spec = (b Ec SC + T^2 (f0 A0 + B0) Sbase) * cb7[43].w                      // CL1:893-894, 897-906, 931
diff = albedoD * T^2 * kd * (1 - max(f0 A0 + B0)) * ambientIrradiance     // CL1:907-930, 932
out  = diff + spec + T * decalEmissive                                     // CL1:932-933
```

- `0.035614` is the coat's F0. INFERRED: an IOR of about 1.465. `Ec` is a scalar, so the coat reflection is
  untinted.
- Sun: its colour uses the base normal `r12.xzw` (CL1:956-1008). The coat normal enters only `r9.w` (956, 1002).
- Local lights: their colour uses the base normal (1215-1320). The coat normal enters only `r20.w` (1182-1214, 1321).
- `r9.w` / `r20.w` feed the thermal output (1330-1344, `cb7[13].x`).
- Quirk: without probes, both lobes take the light-grid ambient along RC, and the base's probe fallback uses it too
  (588-591, 865-867).
- CL2-CL5 repeat this exactly (CL2:688-702, 1058-1103; CL4:1010-1020; CL5:1238-1254). Exponents are `exp` =
  2^x, as in the editor's `exp2(20 gp)`.

### 1.3 Variants (hex bits; VERIFIED where a shader is cited, else from the bindings)

| hex | Coat normal / gloss | b | Extra | Materials (examples) |
|---|---|---|---|---|
| `100553` | map at slot 9+L (`macro_clearcoat_glossy_n&_g`), remap const 54+L | mask126 ch L-1 (hexB 3), else 1 | - | vehicle bodies, `decho_interior_netting` (CL2-CL4, `15ffe38d`) |
| `180153` | map (`*_microtile_n&white`: gloss 1, strength 0.78, UV x16) | 1 | UV1 in `2` families | `container_dumpster_large_01_rubble` (CL1), `uk_tool_box_large_*` |
| `18015b` | map | `tex[27+L].r` (flags 8) | - | `medical_bed_01`, `me_sign_hospital_signage_01_russian_2uv` (+ `tw/` world variant) |
| `180143`, `100143` | vertex normal, gloss' 0.193774, cavity 1 | 1 | - | `uk_siding_metal_corrugated_01_2sided_brown` (CL5), `medical_defibrillator_wall_01`, `vfx_ui_disconnected_player` |
| `180163` | as `180143` | 1 | emissive: `tex[19](uvC).rgb * const37.rgb` (8, 8, 8), added as `b (1 - Ec) E` (not x T) | `stationary_trainyard_post_set_01_glass_lit*` (CL6:174-175, 208-210, 856, 871, 897) |

INFERRED: hex 0x08 = alpha map, 0x10 = coat map, 0x20 = emissive. The emissive slot / constant follow layer 0's 18 / 36
as 18+L / 36+L, but only L = 1 was seen. All code layers have flags 0 or 8 (survey of all 59 per-zone entries).

### 1.4 Where the coat's constants sit

- Rows 1+3L (flags, UV) and 2+3L (tint, k) are as in research 136.
- **Constant 54+L (the coat remap) is not on row 3+3L**: it is on row 6 in CL1, row 7 in `f88d3778`, `3c9ffe23` and
  `40c1ad73`, row 10 in CL2-CL4 and `82046867`, and row 13 for L = 3 (`b529cd60`, `ea57bd48`; args `MC:16:13:57`).
  Row 9 of CL2's buffer is empty.
- Standard 3-layer sets keep constant 56 on row 9 (C3, S3, M3).
- The decho's raw psData row 10 = `000000ff 00ff38b9 4f800000 0`: gloss = (m.r x 186 + 56) / 256, so 0.22..0.95, with
  strength 1. Its constant table holds the same bytes as `56=`.
- Constant 37 (emissive) sits on row 6 in CL6.

## 2. Slot 45 and code constants 15 and 19 (VERIFIED in CL2 / CL3 unless noted)

| Input | Bound when (lit techniques of the four MP zones) | Read | What it is |
|---|---|---|---|
| slot 45 (`*_thermalmap`) | base token bit 0x4000 (every technique binding 45 has it; with or without code layers) | CL2:386 (raw UV0) | thermal map: `heat = t45.r * hi16(row2.z) * C19.y + (lo16(row2.z) >= 0 ? lo16 : cb7[12].x) + C19.x` (CL2:386-394; decho: 294.25 + C19.x + 27.8 C19.y x map) |
| code 19 | techset field 5 bit 0x40 (`800840`, `280942`, ...) | CL2:392-393 (`cb1[1].xy`), CL3:372-373 (`cb1[0].xy`) | per-instance (heat bias, heat scale) of the thermal map |
| code 15 | some entity (`mo`, `mco`, `mso`) sets: base token bit 0x40000000 (decho `17814415b`) and others | CL2:116-135, 150-155 | `cb1[0].z` = record index into the global structured buffer t48 (184-byte records). If record dword 6 bit 31 is set, the record's layer-0 UV transform (+48, +96) and spec / gloss remaps (+24, +36) replace rows 1 and 3. |

- The heat only reaches the thermal output: CL2:1501-1515, taken when `cb7[13].x` = 1, or = 2 inside the screen mask
  t11. INFERRED: 294 = ambient kelvin, and C19 = engine / burn heat.
- Static-model techsets (`tmo`, `tm2o`, ...) bind none of 15, 19 or t48: CL4's args have no `C:16:*:15/19`, and it has
  no `cb1` and no t48. A vehicle placed as a static map model has none of them.
- On entities the engine's fill was not read (UNKNOWN; IDA unavailable). The editor's default is right without it: the
  material's own layer-0 rows (override bit clear) and no thermal.
- **Neither constant touches the code layer**: CL2's coat weight reads only slots 29 and 126 and rows 7-8 (CL2:245-250,
  361-373).

## 3. Materials with code layers (one entry per material per zone)

| Zone (`techsets_*.ff`) | Model-layered | Code layers | By hex |
|---|---|---|---|
| mp_hideout | 399 | 11 | 100553 x8 (decho, decho_brown, netting, walfa x5), 180153 x2, 180143 x1 |
| mp_shipment | 130 | 4 | 100553 x4 (decho, decho_darkblue, netting, victor40) |
| mp_m_speedball | 124 | 1 | 180163 (glass_lit_spdball) |
| mp_rust | 253 | 0 | - |
| mp_donetsk | 2,198 | 24 | 100553 x15, 18015b x3, 180153 x2, 100143 x2, 180143 x1, 180163 x1 |
| mp_br_quarry | 1,079 | 19 | 100553 x13, 180153 x2, 18015b x1, 100143 x1, 180143 x1, 180163 x1 |
| common_br_mp | 45 | 0 | - |

- Unique materials: 14 in the MP zones and 24 in Warzone, 35 in all.
- Vehicles: `veh8_civ_lnd_decho*`, `_walfa*`, `_victor40`, `_skilo*` (4 layers, coat at L3, mask B), `_techo*`.
  They come as `mo/` (entity) and some also as `tmo/` (static). The `*_no_thermal` variants drop slot 45 and code
  constant 19.
- Families: `mo`, `tmo`, `tm2o`, `m2o`, and one world `tw/me_sign_hospital_signage_01_russian_2uv_ed`. There are none
  in `tmco` / `tm2co` / `mco` / `m2co`.

## 4. Port recipe (prop fold)

### 4.1 Data

1. **Classify**: `parseModelLayerTokens` already returns `ModelLayerKind::Code` (b = 1). Also keep the token hex:
   0x10 map, 0x08 alpha, 0x20 emissive. The code layer is the last layer.
2. **Remap row**: read the coat's gloss scale / bias and normal strength from **constant 54+L**, not row 3+3L.
   `material_cache.cpp:672-683` already rebuilds single-layer rows from `constBytes`. Do the same for layered
   `layerPsData`: row 1+3L <- const L, 2+3L <- const 9+L, 3+3L <- const 54+L. This is harmless for standard sets, where
   these already coincide. Keep constant 36+L for the emissive kind. Without 0x10: gloss' 0.193774, no map.
3. **Images**: the coat map is slot 9+L (`normalFor` takes layer keys without a colour image). Alpha is slot 27+L
   (only `18015b` has a non-white one). The mask is slot 126, as today. The emissive is slot 18+L.

### 4.2 Fold slot (`camera_view.cpp:1368-1407`, `render.h:319-345`)

- Replace the `continue` at `camera_view.cpp:1371` with a coat `PropLayer`: `drawKind = 4` (never a pass:
  `setReferenceInstLayers` passes only kinds < 2.5), no colour texture, `reveal` = slot 27+L, `normal` = the coat map
  or -1.
- Give it **its own block** instead of one of the two fold slots. The skilo's coat is the third layer, and the coat
  never changes albedo, so it does not compete in the colour order. The 512-byte record has rows 26-31 free, and
  `foldL[10]` ends at row 25. For example:
  - row 26: on, flags, mask channel + 1, kind (1 coat, 2 coat + emissive);
  - row 27: the UV transform (cos, sin, scale u, v);
  - row 28: offset u, v, UV1, map (0 / 1);
  - row 29: normal strength, gloss scale, gloss bias, 0;
  - row 30: the emissive rgb (const 36+L).
- Textures: t52 coat map, t53 alpha, t54 emissive. No `register(t32-35, t37-39, t43, t52+)` is declared in
  `render.cpp`.
- Weight: ~~the `foldWeight` rule applies unchanged~~ (corrected in section 6: `w = 1`, not the vertex alpha).
  Flags 16 never occurs.

### 4.3 Shader (`ps_tex`, stock props, Game Lighting)

- Do **not** touch `nxy`, `nmCavity` or `g` in the fold loop (`render.cpp:1226-1259`). Compute the coat lobe separately:

```
NC  = map ? normalize(T nC.x + B nC.y + Nf (1 - .6 |nC|^2)) : Nf   (T, B, Nf: the frame at render.cpp:1262-1274)
gpC = map ? 1 - sqrt(1 - sat(m.r * scale + bias)) : 0.193774;   cavC = map ? m.b : 1
// after `c = diffAlbedo * indirect * kd * (1 - fmax) * aoMB + Fenv * S;` (render.cpp:1446), before the sun:
abC = gameEnvBrdf(sat(dot(NC, Vs)), gpC);   Ec = 0.035614 * abC.x + abC.y;   RC = reflect(-Vs, NC)
LrC = (indirect - stockDirN) + stockB * sat(dot(RC, stockDw))
SC  = as S (render.cpp:1375-1392) with RC, gpC, a2C and cavC in place of R, gp, a2, cavS
T   = 1 - b * Ec;   c = c * T * T + b * Ec * SC;   kind 2: c += b * (1 - Ec) * emissive.rgb * const36+L.rgb
```

- The sun, transmission and the local lights stay as they are (the game gives the coat no direct light).
- SSR: the game's coat has none in this shader, so leave S's SSR on the base only.
- Lit Preview without Game Lighting: skip the coat. It only affects ambient specular.
- A/B: `IW8R_NO_COAT`.

### 4.4 Expected results and checks

- Decho / walfa / victor40 bodies, where mask G = 1 (INFERRED: painted panels): a white, sharper env sheen at grazing
  angles over paint and dirt, and about 7 % darker ambient face-on. The sunlit look is unchanged.
- Dumpster rubble: a mirror coat (gloss 1) with a microtile normal at UV x16. It sparkles at grazing angles.
- Corrugated brown and the defibrillator: a uniform rough coat (gloss 0.35), a faint sheen.
- Glass lit: emissive x 8 under the coat.
- Core tests:
  - the token hex bits from the names in section 1.3;
  - constant 56 -> canonical row 9 for CL2's buffer (row 10 raw);
  - `Ec(nv = 1, gp' = 1) ~ 0.036`.

## 5. What is not determined

- **Engine values of code constants 15 / 19** on vehicle entities (IDA bridge unavailable). Not needed for the coat or
  the visible colour.
- **The `veh8_*_r` / `_r_dirt` mask contents**: read for the walfa only (section 6). Other vehicles' masks are not
  exported yet.
- **Hex bits 0x400 and 0x100000** of `100553` / `180153`: unknown. No shader difference was seen between `100553` (CL2)
  and `180153` (CL1) beyond the UV-set bit 0x80000.
- **The `tw/` world code layer** (`me_sign_hospital_signage_01_russian_2uv_ed`): the world shader was not read.
  INFERRED: the same coat.
- **The emissive kind at L >= 2** (slot 18+L / constant 36+L): only L = 1 was seen.
- **4-layer shaders** (`b529cd60`, `ea57bd48`, `82046867`): not disassembled. Mask channel B and row 13 are from their
  arguments.

## 6. Port status and findings (2026-11-15)

The coat is in the editor (`render.cpp` coat block after the ambient combine, `setReferenceInstLayers` record rows 26-29,
t52 coat map, t53 alpha; `IW8R_NO_COAT` A/B). Findings from the port:

- **Weight, corrected**: the coat block does **not** use `foldWeight` (section 4.2 said it applies unchanged). That rule
  starts from the vertex alpha `w`, while a code layer has `w = 1` (section 1.1). Meshes whose vertex alpha is not 1
  would lose the coat. The block now computes `b = (flags & 8 ? alpha.r : 1) x mask126[ch]` directly.
- **Walfa: the coat is off** (VERIFIED from the image, INFERRED for the game result). `veh8_civ_lnd_walfa_r_dirt`
  (slot 126 of all three `walfa*_no_thermal` materials) is BC1 sRGB 2048x2048 with **G = 0 and B = 0 in every block**;
  only R (dirt, layer 1) varies. With the coat at L = 2 reading mask G, `b = 0` everywhere, so these cars show no coat.
  The INFERRED "G = coat over the body paint" (section 4.4) does not hold for the walfa.
- **Slot-126 masks are stored sRGB**: of the cached ones, 188 are BC1_UNORM_SRGB (72) and 47 R8G8B8A8_UNORM_SRGB (29),
  none UNORM. The game samples them through that format, so its shader sees linear values. Reveal / alpha maps
  (slot 27+L) are BC1_UNORM (71) or BC4 (80), and tint masks (59+L) BC4: linear as stored. The editor used to read
  the 126 masks' raw bytes, which made every masked layer too strong (the walfa's dirt: a raw 0.5 is 0.21 linear). They
  now load through a `"\x07<image>"` key that keeps the file's sRGB format (`ddsIsSrgb`; A/B `IW8R_RAW_LAYER_MASKS`).
- **Where it shows** (Hideout, Game Lighting, coat on vs `IW8R_NO_COAT`):
  - `tm2o/uk_siding_metal_corrugated_01_2sided_brown` (hex `180143`: no map, gloss' 0.193774): the dark panel behind
    the grid lifts by 2-3 levels.
  - `tmo/container_dumpster_large_01_rubble` (hex `180153`: microtile map): the rubble fill brightens by 2-4 levels,
    up to 10.
  - The walfa cars: no change (mask G = 0).
- In the cached materials only these five have a code layer (three walfa colours, the siding, the rubble). The others in
  section 3 load when their zones' materials are resolved.
