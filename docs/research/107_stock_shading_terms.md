# 107. Stock shading terms still approximated by Game Lighting (Replay 1.20): transmission, wrap diffuse, layer modes, probe fallback and AO constants, emissive

**Why.** PLAN_REMAINING section 1.4 lists the stock shading terms that the viewport's Game Lighting branch
(`src/viewport/render.cpp`, `ps_tex`, the `stockRef` path) still approximates. This note reads each one from the game's
own pixel shaders, and reads the two engine constants (`cb7[186]`, `cb7[47]`) from the Replay exe. For each term it
gives the exact math, the data that feeds it and an editor recipe with expected values.

**Labels.** VERIFIED = read in the disassembly, the techset/material export, the world cache or the Replay exe (cited).
INFERRED = names, meanings or defaults that come from usage, the IW8 game-test decompile ("GT") or arithmetic. The
DXBC has no RDEF block, so register names are ours. Nothing was run in the game.

## How it was obtained

- ACTS `-a material,techset` on `techsets_mp_m_speedball.ff`, `techsets_mp_hideout.ff`, `techsets_mp_backlot2.ff`;
  then `-a pixelshader -n <hash>` for the shaders below. Disassembled with
  `out/agent/shader_research/py/dxbc_dis.py` (research 95). NUL-stripped copies are in `out/agent/shade107/asm/`.
- Images: ACTS `-a image -n <name>~<hash> --xpak <zone xpaks>` on `mp_m_speedball.ff` (the name needs the `~hash`
  suffix; a name with `&` must be passed alone). The material cache's cached DDS files were read for the rest.
- Replay exe: a static read with `pefile` + `capstone` (`out/agent/shade107/py/pe.py`, `cb7_values.py`). IDA was not
  reachable this session. RVAs are for `game_dx12_ship_replay.exe` 1.20.4.7623265.
- The world cache's vision rawfiles: `world_cache/<map>/vision/.../rawfile/vision/<map>.vision.0`.

| Tag | Shader | Techset (lit technique `TECHNIQUE_LIT_FORWARDPLUS_BITMASK`) |
|---|---|---|
| **L** | PS `88d201db` | `tmco/lit_3_lit_alt_ta1_848810_1012000201000051_..._138f0035b_0_0_1_0_0` (Speedball grass, MTL_TYPE_STATIC_MODEL_VERTCOL_SELFVIS). Line numbers are the same as research 100's L. |
| Lw | PS `eba993c4` | `twc/lit_3_lit_rpl_ta1_809014_1002000000000011_..._13810035b_...` (backlot2 tarp, lightmapped world transmission) |
| W2 | PS `e2969e82` | `twc4/lit_3_lit_rpl_ta1_802000_100a000000000010_0_2_2_...` (Speedball opaque 2-layer wrap) |
| Wm | PS `258373a9` | `tmo/lit_3_lit_rpl_ta1_800800_100a000000000010_0_1_1_...` (static model wrap) |
| M2 | PS `fb71be8a` | `tmo/..._0_2_1_0_0_13810015b_0_0_1_0_0_0_0_3810015f_2_1_2_0_0` (model 2-layer, layer 1 colour mode 2) |
| M3 | PS `98b22267` | `tmo/..._0_3_1_..._3810014b_2_0_2_0_0` (model 3-layer, layer 2 colour mode 2) |
| M4 | PS `fd62a749` | `mo/lit_3_lit_rpl_ta1_801000_180a000003000010_0_2_1_..._38100157_3_1_2_0_0` (model 2-layer, colour mode 3, wrap) |
| E | PS `83cc321c` | `tmo/lit_3_lit_rpl_ta1_800800_1002000000000010_0_1_1_0_0_13810017b_0_0_1_0_0` (`tmo/light_ceiling_bulb`) |
| G | CS `cs_gtao.233` | code_post_gfx (research 95's export) |
| also | PS `3452bd8d`, `b52eadf1` | other transmission variants; `3452bd8d` = L with a different CB2 layout, `b52eadf1` = L without vertex colour |

## 0. Summary

1. **Transmission** (slot 68 x constant 18). It is selected by techset hexB **bit 0** (the last hex digit is odd). There
   are three terms. Ambient: `T x (1 - max3 Fenv) x (1 - 10^-a2) x E(-N)`, where E(-N) is the baked light seen from the
   back side. Sun: a GGX lobe of the surface's own roughness around the straight-through direction, using `dot(L, -N)`,
   the same sun visibility and no Fresnel. Local lights get the same lobe. T is not scaled by vertex colour or albedo.
   (sections 1.1-1.5)
2. **Wrap diffuse.** It is selected by hexB bit `0x0008000000000000` (`100a` / `180a` / `184a`); constant 103 rgb is
   the per-channel wrap. The research 98 formula is confirmed. It applies to the baked directional term, the sun and
   local lights, and never to the reflection term. Two additions: the sun shadow visibility is raised to a per-channel
   power (a coloured penumbra), and for local lights the wrap merges with the sphere-light horizon. (section 2)
3. **Layer colour modes 2 and 3 leave the colour and spec code unchanged.** This is VERIFIED in four shaders. A
   soft-light path exists in the bytecode but cannot be reached. Mode 3 layers have no colour texture at all. The
   layer's normal, cavity and gloss still apply by flags 1/2/4 and the normal mode. On world materials only modes 0 and
   1 occur; modes 2 and 3 occur only on model-layered materials. (section 3)
4. **`cb7[186]` = (-10, 1 + 2d, 0, 0)**, with d = dvar `LRLSQPKNL` ("Gloss threshold for HQ secondary specular (in
   internal mip range)", default 0.5). So **fb = saturate(2 - 10 g')**, the same on every map. **`cb7[47]` =
   (vision `r_ssaoStrength`, 1.8621, ...)**. GTAO stores visibility x 0.537028 = 1/1.8621, so
   **ao = lerp(1, GTAO visibility, r_ssaoStrength)**. The strength is **1 on all 13 MP maps checked** (0 on the
   frontends). (section 4)
5. **Emissive** (slot 18 x constant 36) adds `E x (1 - max3 Fenv)` to the shaded colour, in the scene's radiance unit.
   It is unlit, gets no AO or shadow, and goes through the pre-exposure and the tone map's exposure like everything
   else. Constant 36 ranges from 0 to 800 (1 for a dim bulb, 15-30 for tubes, 180-800 for strong fixtures). No world
   (`tw*`) material in these zones has one. (section 5)
6. **A side finding** (section 6). For about 11 % of lit materials (62 of 567 on Speedball, mostly alpha-tested
   foliage), the editor's material cache reads the depth-prepass constant buffer instead of the lit one. And when a
   material has constant 18, 27 or 36, constant 54 (spec/gloss remap) is not in psData row 3.

## 1. Transmission (grass, foliage, tarps, cloth, flags)

### 1.1 Which materials (VERIFIED)

- Techset: hexB bit 0. The lit technique then binds `MTL_ARG_MATERIAL_TEXTURE` index **68** and
  `MTL_ARG_MATERIAL_CONST` index **18**. All 19 Speedball techsets that bind slot 68 have bit 0 set. One more has
  bit 0 without slot 68: a 4-layer `mo` set, not decoded. (`ts_sb.tsv`, `tsscan.py`)
- Families: `tmco`, `tmo`, `mo`, `twc`, `tw` (opaque `rpl`, alpha-tested `alt` and blended `bad`/`bar`).
- Materials with slot 68 in the speedball + hideout + backlot2 techset zones: 206 non-effect materials. By name: 47
  foliage, 25 tarp, 22 tree, 20 flag, 16 grass, 15 cloth, 6 curtain. Six `tw`/`twc` world materials use it (tarps,
  glass). Constant 18 is (1, 1, 1) on 143 of them; tarps and cloth carry a colour, e.g. (0.745, 0.509, 0.546).
  (`trans_survey.txt`)
- Glass (`g/`, `tw/glass_*`, `bar` blends) also binds slot 68. Only the opaque and alpha-tested shaders (L, Lw,
  `3452bd8d`, `b52eadf1`) were read, so glass's use of slot 68 is UNKNOWN.

### 1.2 Data

- **T = tex68.rgb x c18.rgb** (L:162-163; t5 = slot 68, cb3 row 3 = constant 18 in this techset). The grass image
  `foliage_grass_long_01_green_low_abs_256` is BC1 **sRGB**, 512 x 256. Over the alpha-tested texels kept, the linear
  median is (0.262, 0.235, 0.102), against albedo (0.168, 0.171, 0.084) (`grass_stats.txt`). T is **not** multiplied by
  the vertex colour (only the albedo is, L:164) and not by the tint.
- Material constants of the grass, lit buffer 1 (`mat/foliage_grass_long_01_green_low_reactive...json`):
  - row 0 = c72 (1, 0, 1.697, 1.697). **.y = 0: probes off** for this material, so S = E(R) with no specular occlusion
    (L:516).
  - row 1 = c0 (flags 0x0F).
  - row 2 = c9 (tint white).
  - **row 3 = c18 (1, 1, 1, 1)**.
  - **row 4 = c54**: spec code b0 = 9, gloss remap (255, 0), normal strength 0xDA.

### 1.3 Math (VERIFIED, L unless noted)

Notation as research 95:
- `g' = 1 - sqrt(1 - gloss)`
- `a2 = 2 / (2 + 2^(20 g'))` (L:216)
- N = the normal-mapped normal, flipped on back faces (L:79). The tangent frame and the SELFVIS direction are flipped
  too.
- `nv = saturate(N.V)` (L:212)
- `R = reflect(-V, N)` (L:225)
- `Fenv = f0 A(nv, g') + B(nv, g')`
- `aoMB = ao / (1 - min(diffuseColor, .85)(1 - ao))`

```hlsl
// Ambient (L:718-755)
kTr      = 1 - exp2(-3.32193 * a2);                        // = 1 - 10^-a2 (L:749-751)
ambTrans = T * (1 - max3(Fenv)) * kTr * Eback;             // L:748-753
ambient  = diffuseColor * Efront * kd * (1 - max3(Fenv))   // research 95, L:746-747
         + ambTrans + Fenv * S * cb7[43].w;                // L:754-755
// Efront = E(N) * aoMB * cb7[43].z;  Eback = E(-N) * aoMB * cb7[43].z      (L:421-427, 481-510)

// Sun (L:756-871), only where the sun visibility vis = |t85| > 0
soft(x)  = x < 1e-6 ? 250000 * sq(max(x, -1e-6) + 1e-6) : x;                 // L:785-790, 823-828 (~max(x,0))
sunDiff  = sunColor * diffuseColor * saturate(soft(N.L)) * vis * (1 - max3(f0*A(N.L,g') + B(N.L,g')));
sunSpec  = sunColor * F * D(N.H) * Vis(N.L, nv) * saturate(N.L) * vis * cavity * 1.0390625;   // research 95
nlb      = dot(L, -N);                                     // L:822
Hb       = normalize(L - R);                               // L:830-833: V mirrored through the surface plane
Db       = a2 / sq(sq(dot(-N, Hb)) * (a2 - 1) + 1);        // L:834-838
Visb     = 1 / ((|nlb| + sqrt(a2 + (1 - a2) nlb^2)) * (nv + sqrt(a2 + (1 - a2) nv^2)));    // L:839-847
kb       = 1 - max3(f0 * A(nlb, g') + B(nlb, g'));         // L:849-854; u = nlb unclamped (sampler clamp INFERRED)
sunTrans = sunColor * T * vis * soft(nlb) * Db * Visb * 1.0390625 * kb;         // L:829, 848, 855-859
sun      = sunDiff + sunSpec + sunTrans;                   // L:860-864
```

- **N.L sign.** The transmission uses `dot(L, -N)`. It is nonzero only when the sun is behind the shading normal,
  which is the normal already flipped toward the viewer.
- **Shadow.** The same screen-space sun visibility as the front side (the pixel's own `t85`). There is no separate
  back-side shadow.
- **View dependence.** The lobe peaks when the viewer looks straight through the blade at the sun (`Hb = -N` gives
  L = -V). Its width is the surface's GGX roughness. It has no Fresnel F, no cavity and no `kd`. The ambient term's
  only view dependence is `(1 - max3 Fenv)`.
- **Local lights** (L:1158-1231). This is the same lobe. The representative point on the light is picked along the
  eye ray continued through the surface (`r10` = -V, L:1167-1199) instead of along R (front specular, L:1094-1107).
  Then `Hb = normalize(Lrep - R)`. The result is multiplied by the light's colour, distance cap, falloff and shadow,
  and by the sphere-energy factor `a2^0.75 / (a2^0.75 + 0.375892 s)` (L:1212-1215, `s` = bulb radius / distance).
  The light record itself is research 106.
- **World variant Lw** (lightmapped twc tarps). It has the same ambient, sun and light terms (Lw:878-886, 955, 990,
  1360). The back irradiance is the lightmap evaluated on the other side, as below.

### 1.4 Where E(-N) comes from (VERIFIED)

- **Lightmapped world (Lw:514-560).** `E(n) = q.z L + (1 - q.z) L / max(dT.z, .001) x saturate(soft(n . dW))`
  (research 95). Front `n = N` (Lw:543-551), back `n = -N` (Lw:552-560). Both are times aoMB.
- **Probe-lit props (L:250-432).** Every transmission shader takes the `LGV_POSITION_INDEX` interpolant and evaluates
  the prop's light grid volume **per pixel**. That is the research 105 box, atlas and t108/t109, but **L1 only**: tiles
  0, 1.zw and 2. The VS of non-transmission props evaluates L1 + L2 instead. Per channel c:

```hlsl
L0  = DC_c;  L1 = sqrt(3) * DC_c * (qx, qy, qz)             // q = filtered byte*2-1: z = tile0.rgb, y = (tile1.z, tile1.w, tile2.x), x = tile2.yzw
rho = min(|L1| / L0, sqrt(3));   a = saturate(0.7698 rho);   s = max(rho, 1.299038) * 1.154701 - 1
k   = a * L0 / ((1 - s) * 1.772454)
zh  = (L0,  (1 - s*s) * k * 0.943575,  s * (1 - s*s) * k * 0.362577)        // L:298-325 (zh0 simplifies to L0)
d   = normalize(-L1.x, -L1.y, L1.z);   c = dot(d, n)
E_c(n) = max(0, 0.282095 zh0 + 0.488603 zh1 c + zh2 (0.946175 c^2 - 0.315392))   // front L:331, back L:332 (c -> -c)
```

  Where the light grid volume mask `cb7[194]` path is active it blends with the tetrahedral/heightfield grid SH
  (L:439-513). The grid side also evaluates N, -N and R (L:486-506).
- Check against research 105 (Speedball volume 0, the reactive grass patch, box centre). This PS form gives
  E(up) = (5.375, 6.086, 7.594) and E(down) = (2.786, 2.601, 1.838). The VS form gives (4.833, 5.320, 6.234) and
  (2.961, 2.801, 2.072). The two fits differ by up to about 20 %. (`transmission_ref.py`)

### 1.5 Editor recipe

1. **Data (material cache).** For techsets with hexB bit 0:
   - keep slot 68 (an sRGB image; sample it through an sRGB view or `srgbToLinear3`);
   - keep constant 18 rgb from the material's `constantTable` (index 18);
   - read psData from the lit buffer, not the first buffer (section 6).
2. **Shader.** Only for `stockRef` pixels whose batch has a transmission map. N is already flipped toward the viewer
   in `ps_tex`.
   ```hlsl
   float3 T    = tex68.rgb * c18.rgb;
   float  kTr  = 1.0 - exp2(-3.32193 * a2);
   float3 Eb   = (lmOk ? lightmapE(-N) : propLgvE(-N)) * aoMB;   // lightmap: a.rgb + b * saturate(dot(-N, Dw))
   c += T * (1.0 - fmax) * kTr * Eb;
   float  nlb  = dot(gameSun.xyz, -N);
   if (nlb > 0.0) {
       float3 Hb = normalize(gameSun.xyz - R);
       float  hb = dot(-N, Hb);
       float  Db = a2 / sq(hb * hb * (a2 - 1.0) + 1.0);
       float  Vb = 1.0 / ((nlb + sqrt(a2 + (1.0 - a2) * nlb * nlb)) * (nv + sqrt(a2 + (1.0 - a2) * nv * nv)));
       float2 ab = gameEnvBrdf(nlb, gp);
       c += gameSunCol.rgb * T * visibility * nlb * Db * Vb * 1.0390625 * (1.0 - max3(f0 * ab.x + ab.y));
   }
   ```
   - For props, `propLgv` today is one value per instance. Evaluate the same SH at -N for `Eb` (a second per-instance
     value is enough for a first step).
   - Local lights: the same lobe per light, with the light's attenuation.
3. **Expected values** (`transmission_ref.py`, `transmission_ref.txt`).
   - Inputs:
     - the grass median texel: albedo (0.1683, 0.1714, 0.0844), T (0.2623, 0.2346, 0.1022), gloss 0.3059 (g' 0.1669,
       a2 0.1652, kTr 0.3164), spec code 10/256;
     - Speedball sun 30.6723 x (1.00331, 1.00331, 0.95747) at 45 degrees elevation, vis 1;
     - LGV volume 0 above; ao 1; bakedLightScale 1.
   - Case 1. A vertical blade with N = (-1, 0, 0) and the sun behind it, the viewer looking straight through at the sun
     (V = -L, nv = 0.707):
     - ambTrans = **(0.336, 0.302, 0.128)**
     - sunTrans = **(16.15, 14.45, 6.01)**
     - ambient diffuse (0.535, 0.563, 0.275)
     - sun diffuse 0
   - Case 2. The viewer turned 30 degrees off that line: sunTrans **(10.17, 9.09, 3.78)**.
   - Case 3. The same blade lit from the front: sunDiff (3.56, 3.63, 1.70) and sunTrans 0.
   - Reading: a back-lit blade looked at toward the sun is about 4.5 times brighter than the same blade lit from the
     front. That is the bright rim on in-game grass seen against the sun.
   - kTr against gloss: 0 -> 0.785, 0.1 -> 0.680, 0.2 -> 0.517, 0.3059 -> 0.316, 0.4 -> 0.170, 0.5 -> 0.074. Glossy
     materials get almost no ambient transmission.

## 2. Wrap diffuse (`100a` techsets)

### 2.1 Selection and data (VERIFIED)

- hexB bit **`0x0008000000000000`**, already decoded by `parseStockTechsetName` as `wrapDiffuse`. The lit technique
  then has one more `MTL_ARG_MATERIAL_CONST`, index **103**, in the last psData row (row `1 + 3N` on layered
  materials, research 98). Only `.xyz` is read (W2:672, 1023, 1039-1041, 1121-1123, 1302). `.w` is not read.
- 169 materials in the three zones (135 `twc4`, 11 `mo`, 9 `tm2o`, 5 `tm2co`, 5 `tmo`, 2 `twc`, 1 `tw`, 1 `tw4`).
  - Values: (0.69, 0.65, 0.51) on 74, (0, 0, 0) on 65 (plain diffuse), (0.15, 0.15, 0.15) on 23, snow
    (0.435, 0.552, 0.65) on 5. (`wrap_ref.txt`)
  - `184a` sets (bit `0x0040000000000000` as well) add constant **104**: a per-channel blend of the shading normal
    toward the vertex normal, `N_c = normalize(N + c104_c (Nvert - N))`, for the sun's N.L (M4:864-879). This was
    VERIFIED structurally on one shader; the snow value is (0.8, 0.8, 0.8).

### 2.2 Formula (VERIFIED)

```hlsl
float wrapF(float x, float w) {          // w = max(c103_c, 1e-6); W2:672-685 (lightmap), 1023-1036 (sun), 1307-1319 (lights)
    return saturate(x < w ? sq(max(x, -w) + w) * 3.0 / (4.0 * w * (w * w + 3.0)) : 3.0 * x / (w * w + 3.0));
}
```

- **Baked light, directional part only.** Lightmap (W2:641-689):
  `E_c = Lamb_c + Ldir_c x wrapF(N . dW, w_c)`, then times aoMB. The reflection term `Lr = Lamb + Ldir saturate(R . dW)`
  is not wrapped (W2:688-689). Static-model vertex light (Wm:478-506): the same with `AMBIENT_COLOR` / `HIGHLIGHT_DIR`.
- **Sun** (W2:1023-1084):
  ```hlsl
  m   = min3(w);  k_c = 1 + 1.35 (w_c - m);  e = (k_b k_g, k_b k_r, k_g k_r)       // W2:1039-1043
  sunDiff_c = sunColor_c * diffuseColor_c * pow(min(vis, 1), e_c) * wrapF(N.L, w_c) * (1 - max3(f0 A(N.L) + B(N.L)))
  ```
  - The pow is a bit trick: `asfloat(int(e (asint(v) - 1065292416) + 1065292416))`, within about 4 % of `v^e`
    (W2:1044-1047).
  - The env BRDF is sampled at the raw, unwrapped N.L.
  - The specular is unchanged: `vis x saturate(N.L)`.
- **Local lights** (W2:1297-1327). The wrap merges with the sphere-light horizon softening that every lit shader
  already applies:
  - `s = bulbRadius / distance`, `h = (0.77 s + 0.33 s^2)`;
  - `w'_c = clamp(sqrt(w_c^2 + h^2), 1e-6, 1.25)`;
  - diffuse x `wrapF(N.L, w'_c)`;
  - the light's shadow x falloff product gets the same per-channel power `e_c`.

  Without `100a` the plain shaders use `wrapF(N.L, clamp(h, 1e-6, 1.25))` (L:1070-1090), the same function.

### 2.3 Editor recipe and expected values

- Data: the techset bit (have it) and constant 103 rgb (the material's `constantTable`, or psData's last row).
- Apply `stockWrapDiffuse` per channel:
  1. to the lightmap's `b x saturate(dot(N, Dw))` term in the `stockRef` branch (not to `Lr`);
  2. to the sun diffuse, with `pow(visibility, e_c)`;
  3. to local lights with `w'`.
- Expected values (pebbles w = (0.69, 0.65, 0.51); `wrap_ref.txt`):

| N.L | wrapF r, g, b | plain |
|---|---|---|
| 1.0 | 0.8630, 0.8766, 0.9202 | 1 |
| 0.5 | 0.4428, 0.4459, 0.4602 | 0.5 |
| 0.0 | 0.1489, 0.1424, 0.1173 | 0 |
| -0.3 | 0.0476, 0.0413, 0.0199 | 0 |

  Shadow exponents e = (1.189, 1.243, 1.478). At vis 0.5: exact `vis^e` = (0.439, 0.423, 0.359); the shader's
  fast pow gives (0.453, 0.440, 0.381).

## 3. Layered materials: colour modes 2 and 3, layer gloss and normal

### 3.1 The modes (VERIFIED)

- `mode = (flags >> 7) & 3` and `nmode = (flags >> 9) & 3` of the layer's row `1 + 3L`.x (research 97). The techset
  name repeats them per layer: `..._<hex>_<colourMode>_<normalMode>_<c>_0_0`.
  - Speedball `tmo/ee_floor_concrete_slab_01`, layer 2: flags 0x0307 -> mode 2, nmode 1; token `3810015b_2_1_2`.
  - Snowball LOD, layer 1: flags 0x0387 -> mode 3; token `_3_1_2`.
  - (`layer_tokens.txt`, `mat/`)
- Compiled code (M2:224-263, M3:282-320; the world 2-layer PS of research 97 has the same shape):
  ```
  if (mode != 3) {
      c = tex[L].rgb * tint
      if (mode == 0)      albedo = lerp(albedo, c, b), s = lerp(s, sL, b)
      else                albedo = (mode == 1) ? albedo * lerp(1, c, b)          // multiply
                                 : (mode != 2) ? softLight(albedo, c, b)          // only reachable for mode 1, then discarded
                                 : albedo                                         // mode 2: unchanged
  }                                                                               // mode 3: unchanged, colour not even sampled
  ```
  - **Mode 2: colour and spec code unchanged.**
  - **Mode 3: unchanged.** Mode-3 layers bind no colour texture (the snowball LOD techset has slots 0 9 10 28 59 123
    only), and M4 contains no colour fetch for the layer (M4:192-232).
  - The dead soft-light formula, for the record (M2:246-259): `t = lerp(.5, sRGB(c), b)`,
    `out = albedo^2 (1 - 2t) + 2 t albedo`.
- Where they occur (`layer_tokens.txt`, speedball + backlot2 techsets).
  - World `tw`/`tw4`/`twc4` layers use only modes 0 and 1: 0 on 422 layer slots, 1 on 120. These are layers 1 and
    up, counted per techset per zone.
  - Modes 2 and 3 occur only on model-layered materials (`tmo`, `tmco`, `tm2o`, `tm2co`, `mo`, `mco`, `m2o`): mode 2
    on 36 layer slots, mode 3 on 7, against 265 for mode 0 and 15 for mode 1. Examples: crate foam, concrete slab,
    bombsite crates, snowball.
  - In practice they are "detail" layers. They add a normal, cavity or gloss without a colour.

### 3.2 Layer gloss, cavity and normal (VERIFIED, M2:264-274, 553-561; matches research 97 for world layers)

```
bN = (flags & 1) ? b : 0;   bC = (flags & 2) ? b : 0;   bG = (flags & 4) ? b : 0
if (nmode != 2):
    n.xy   = (nmode == 0) ? lerp(n.xy, nL.xy, bN) : n.xy + bN * nL.xy     // nL = the layer's decoded, normalised octa xy (its own strength)
    cavity = lerp(cavity, tex[9+L].z, bC)
    gloss  = lerp(gloss, (tex[9+L].x (c0+1) + c1) / 256, bG)               // unsaturated; layer 0 unsaturated too
after all layers: gloss = saturate(gloss);  n.z = 1 - 0.6 |n.xy|^2;  N = normalize(T n.x + B n.y + Ngeom n.z)
```

Model-layer specifics (VERIFIED, M2:157-190):
- One UV set (`TEXCOORDS`), transformed by each layer's row `1 + 3L` (no per-layer UV streams).
- Weight w = 1 (no vertex weights), so the reveal ramp is a step: `b = (flags & 16 ? (reveal > 0) : 1) x (flags & 8 ? alpha : 1)`.
- `flags & 32` multiplies b by slot 123's red channel, sampled at the **untransformed** UV0 (a per-layer mask).
- No `NORMAL_TRANSFORM`.
- Row 0 .x = the layer count (2 and 3 on the materials read).

### 3.3 Editor recipe

- For layers with mode 2 or 3, do not change the albedo or the spec code (no multiply, no lerp). Still apply the
  normal, cavity and gloss rules above.
- Model-layered materials have `layerCount` 0 in the material export. Detect them from the techset name (`_0_<N>_1_`
  with N >= 2, or row 0 .x) and decode with `decodeStockLayeredMaterial(bytes, N)`. Use the single UV set.
- Expected: concrete slab layer 2 (flags 0x0307) changes only normal (additive), cavity and gloss.
  `bombsite_crate_01_foam` layer 1 (flags 0x0307, slot 1 `dirt_ground_01_c`) does not show the dirt colour in game.

## 4. `cb7[186]` (rough-surface probe fallback) and `cb7[47]` (AO)

### 4.1 Where the scene constants live (VERIFIED, Replay exe)

- `R_SetSSAOInfo` (Replay 0x19b1c40, the GT function of the same name at `rb_ssao.cpp:684-729`) writes
  `ssaoParams` at viewInfo + 0x1b80.
- The scene-constant setup writes `lightingScale` at viewInfo + 0x1b40 (0x197cbbc) and the fallback pair at
  + 0x2430 / + 0x2434 (0x197cc36, 0x197cc46).
- With the constant buffer starting at viewInfo + 0x1890, these are **cb7[47]**, **cb7[43]** and **cb7[186]**, which
  matches the shader uses of research 95. `cb7[42]`, written at + 0x1b30 (0x19773d6), fits too.

### 4.2 `cb7[186]` (VERIFIED arithmetic, `cb7_values.py`)

```
d  = dvar LRLSQPKNL  "Gloss threshold for HQ secondary specular (in internal mip range)"
     registered at 0x192b6db-0x192b70c: default 0.5 (xmm7 = [0x275e790]), min 0, max 5, flags 0x40; the pointer is at 0x10559ce8
x  = 1 / (0.2 (4.5 - 5.0)) = -10                    // 0x197cc06-0x197cc36, constants 4.5 [0x23c5160], 5.0 [0x23c77f0], 0.2 [0x23b25c8], xmm11 = 1.0
y  = -x * 0.2 * (5 - (4.5 - d)) = 1 + 2d            // 0x197cc3e-0x197cc46 (xmm13 = sign mask)
z, w = 0                                            // 0x197cc0b
cb7[186] = (-10, 2, 0, 0)  at the default
fb = saturate(2 - 10 g')
```

- In the shader's probe-mip terms (`mip = 5 - 5 g'`, research 95), the probe fades out between mip `4.5 - d` and
  `5 - d`. With the default: fb = 1 (lightmap reflection only, and the probe loop is skipped) for g' <= 0.1 (gloss
  <= 0.19). fb = 0 (probes) for g' >= 0.2 (gloss >= 0.36).
- Values: gloss 0.1 -> 1, 0.2 -> 0.944, 0.3059 -> 0.331, 0.4 -> 0.
- It is not a map value. Only the dvar changes it; whether a Replay config sets it is UNKNOWN.

### 4.3 `cb7[47]` and the AO (VERIFIED)

- Replay 0x19b1c40-0x19b1cfd: `ssaoParams = (strength, (mode & 1) ? 1.8621 : 1.0, width / (aspect x 1080), height / 1080)`.
  - `mode` = `r_ssao` (0x19b12c0; enum Off / GTAO / MDAO / GTAO & MDAO, **default 1 = GTAO**), forced to 0 when the
    strength is 0.
  - `strength` = the vision's `r_ssaoStrength` (GT `cg_visionsets_common.cpp:585-587`; hashed key `NQNPKMQRLT`,
    x64-zt `replay_vision_names.inc:15`).
- `cs_gtao` stores visibility **x 0.537028** (G:1146-1148, 1190-1191), and 0.537028 x 1.8621 = 1.0000. So the lit
  PS's `ao = 1 + cb7[47].x (saturate(gtao x cb7[47].y) - 1)` is **`ao = lerp(1, v, r_ssaoStrength)`**, where v is the
  GTAO visibility (0..1).
- Per map (`vision_ssao.txt`): `r_ssaoStrength` = **1** on mp_aniyah_tac, backlot2, br_quarry, donetsk, emporium,
  hideout, m_fork, m_king, m_speed, m_speedball, rust, shipment and village2. It is 0 on mp_frontend, frontend1 and
  frontend3.

### 4.4 Editor recipe

- **Probes.** Compute `fb = saturate(2 - 10 gp)`. When `fb >= 1`, skip the probe lookup and use `Lr`. Otherwise
  `P' = lerp(P, Lr, fb)` before the 0.18 / 0.85 mix. This applies on layer and decal passes too. The current branch
  uses the probe at every gloss.
- **AO strength.** A setting defaulting to the map vision's `r_ssaoStrength` (1 on MP maps):
  `aoV = lerp(1, aoEditor, strength)`, before the multi-bounce and gtso use. The editor's own AO stands in for GTAO's
  visibility. No 1.8621 factor is needed because the editor stores visibility directly.

## 5. Emissive

### 5.1 Math (VERIFIED, E)

```
Em    = tex18.rgb * c36.rgb               // E:153-154; c36.w is used only by thermal vision (E:155-156)
Em    = lerp(Em, decalEmissive x tint x w, decalCoverage)   // decal volumes, E:419-422 (t119 atlas)
color = diffuseColor Ediff kd (1 - max3 Fenv) + Fenv S cb7[43].w + Em (1 - max3 Fenv)   // E:700-732
out   = (color + sun + lights) x cb7[42].x                  // E:1093-1110, then the tone map's exposure (research 95)
```

- It is **not lit**: no albedo, AO, shadow or `cb7[43]` scale. It is Fresnel-weighted by `1 - max3 Fenv` (about 0.96
  for a dielectric seen head-on). It is in the same radiance unit as the lit result: the unit of `cb7[57]` x N.L and
  of lightmap C (research 106 section 3.3).
- **Exposure applies.** There is no compensation: an emissive of 20 looks as bright as a lit surface of value 20.
- Selection: material texture slot 18 with constant 36. In these techsets the token `...17b` / `...37b` / `...77b` has
  bit 0x20 set where the plain set has `...15b` (INFERRED from the slot lists). Every emissive image in the material cache
  is BC1 **sRGB** (74 of 74 `_e` images).
- Constant 36 over 65 non-effect materials in speedball + hideout + backlot2 (`emissive_survey.txt`):
  - 1 on 18 (e.g. `tmo/light_ceiling_bulb`);
  - 0 on 3 (lights off);
  - 15-35 for fluorescent tubes and lamps (`me_light_ceiling_fluorescent_tube_on_*` 15-20);
  - 80-200 for exterior fixtures;
  - 450 for care-package lights;
  - 800 x (1, 0.56, 0.27) for `light_exterior_cafe_light_fixtures_glass_01_e`;
  - backlot2's distant mountain "billow" props 20-43.

  No `tw`/`twc` world material in these zones has slot 18. Only models do (`mo`, `tmo`, `tmso`).
- Scale: on Speedball the sun is 30.7, so a sunlit 0.5-albedo floor is about 10-15 units, and a fluorescent tube at
  20 matches it.

### 5.2 Editor recipe

- Keep constant 36 rgb (`constantTable` index 36) with the emissive image.
- In the Game Lighting branch:
  `c += srgbToLinear3(emissive.rgb) * c36.rgb * (stockRef ? 1.0 - fmax : 1.0)`,
  before `gameFogApply` and the post pass. Today the branch adds the texel without constant 36 (`render.cpp:734`), so a
  450 fixture shows 450 times too dark.
- Expected:
  - `tmo/light_ceiling_bulb`, white texel: 1 x 0.96 = **0.96**.
  - `tmo/uk_storage_wall_light_01_on`: 180 x 0.96 = **173**.
- **Effect materials** (`ecf/`, `elcq/`, MTL_TYPE_EFFECT_*) draw through `TECHNIQUE_EMISSIVE` with shaders from
  outside the map's techset zone (not found in `common`, `common_core_mp` or `code_pre_gfx`). They were **not
  decoded**. None is a world surface or static model on speedball, hideout or backlot2 (gfx_map material families).

## 6. Side finding: material constants in the editor's cache (VERIFIED)

- **Which buffer.** `material.constantBufferIndex[34]` (`TECHNIQUE_LIT_FORWARDPLUS_BITMASK` = 34) names the lit
  technique's constant buffer. `material_cache.cpp:552-557` keeps "the first buffer with psData".
  - For 62 of 567 Speedball lit materials these differ (`litbuffer.txt`). That is 14 `tmco` and 37 `tmo`, mostly
    alpha-tested foliage and sandbags, whose buffer 0 belongs to the depth prepass (48 bytes: rows 0-2 only).
  - The grass therefore has no spec/gloss remap and no transmission constant in the cache.
- **Which row.** psData rows follow the lit technique's `MTL_ARG_MATERIAL_CONST` `dest`. Constant 54 is row 3 only
  when no other constant precedes it.
  - It is row 4 with c18 (transmission) or c36 (emissive), row 5 or 6 with more (`dest` survey over Speedball's lit
    techsets: 33 of 283 have c54 at row 4 to 6).
  - `decodeStockLayeredMaterial(bytes, 1)` assumes row 3.
- **Recipe.** Read the material's `constantTable` by index (0, 9, 18, 36, 54, 72, 103, 104: literal values, packed
  bytes for 0/9/54). Or map rows through the lit technique's args. Pick the buffer by `constantBufferIndex[34]`.

## 7. What is not determined

- **Glass.** Glass's use of slot 68 and bit 0 in the blended `bar`/`bad` glass shaders was not read.
- **Effects.** The effect (particle) shaders are not in the map techset zones, so `TECHNIQUE_EMISSIVE` of `elcq`/`ecf`
  materials is undecoded.
- **Gloss-threshold dvar.** Whether any Replay config changes `LRLSQPKNL` from 0.5 is UNKNOWN. IDA was not available
  to search its other writers; the static scan found one reader (0x197cc06) and the registration.
- **Sampler address modes.** The env-BRDF sampler's address mode is INFERRED (clamp). It matters for the negative
  `nlb` and wrap N.L inputs.
- **Lobe fit.** The PS LGV lobe fit (section 1.4) is VERIFIED arithmetic; its derivation (a ZH3-style hallucinated
  zonal L2) is INFERRED. Speedball's `bakedLightScale` was assumed 1 in the examples.
- **Constant 104 and the 4-layer set.** Constant 104 (`184a`) was read structurally on one shader. The 4-layer `mo`
  set with bit 0 and no slot 68 was not decoded.
- **Unused fields.** The meaning of constant 103 `.w` (not read by any shader seen) and of constant 36 `.w` outside
  thermal vision.

## 8. Reproduce

Scripts in `out/agent/shade107/py` (Python 3, numpy, Pillow, pefile, capstone). Outputs are in `out/agent/shade107/*.txt`.

| Script | What it does |
|---|---|
| `tsinfo.py <techset.json>` | techniques, shaders, state bits, args (type, stage, dest, index) |
| `tsscan.py <techset dir> > ts_*.tsv` | per lit technique: PS hash, material texture slots, constant indices (`ts_sb.tsv`, `ts_bl2.tsv` kept) |
| `matinfo.py <material.json>` / `--techset-substr <s> <dir>` | textures, constants (float + hex), psData rows per buffer |
| `layer_tokens.py ts_sb.tsv ts_bl2.tsv` | per-layer name tokens (colour / normal mode), which techsets use modes 2 and 3 |
| `trans_survey.py`, `emissive_survey.py`, `wrap_ref.py`, `litbuffer.py <material dirs>` | sections 1.1, 5.1, 2.1/2.3, 6 |
| `dds.py`, `grass_stats.py <abs.dds> <n&g.dds> <material_cache/images>` | grass texel statistics over the kept alpha-test texels |
| `transmission_ref.py` | CPU reference of section 1 (LGV PS evaluation, ambient, sun) and the expected values |
| `vision_ssao.py <world_cache>` | `r_ssaoStrength` per map |
| `pe.py` (copy of research 106's), `dispscan.py <hex disp>`, `cb7_values.py` | static exe reads: stores to viewInfo + disp, the two constant fills, the dvars |

Kept data:
- `asm/` (disassemblies);
- `cs/` (GTAO);
- `mat/` (7 material exports);
- `ts/` (3 techsets);
- `img/` (the grass `_abs_256` DDS).

The full material and techset exports and the 2048 x 1024 normal map were deleted (about 90 MB).

ACTS command lines (short output roots):
- `acts --noUpdater fastfile -r mw19replay -g <replay exe> --oodle <oo2core dll> -a material,techset -l -o <dir> <zone>\techsets_mp_m_speedball.ff`
- `acts ... -a pixelshader -n <hash>[,<hash>...] -l -o <dir> <techsets zone>.ff`
- `acts ... --xpak <each zone\*.xpak over 64 KB> -a image -n "<name>~<hash>" -l -o <dir> <zone>\mp_m_speedball.ff`
