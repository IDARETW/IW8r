# 95. Lit world shader math (Replay 1.20), from the shipped bytecode

**Why.** The editor viewport should reproduce the in-game look of a lit world surface. This note rebuilds Replay's
forward lit pixel shader, the post chain (exposure, tone map, colour LUT, display encode) and the inputs each one
reads, from the game's own DXBC.

**Labels.** VERIFIED = read in the disassembly or decoded data (cited). INFERRED = names, meanings or defaults that
come from usage, from the IW8 game-test decompile (below), or from arithmetic. The bytecode has **no RDEF block**, so
no register, constant buffer or field has a shipped name. Every name is INFERRED. The arithmetic is VERIFIED.

## How it was obtained

ACTS (`tools/stock_edit_test.py` `acts()`, profile `mw19replay`) exports material, techset, pixelshader,
vertexshader, computeshader and image assets. Shaders come out as `.cso` (DXBC, `ps_5_0` / `cs_5_1`). They were
disassembled with `d3dcompiler_47!D3DDisassemble` via ctypes (script `out/agent/shader_research/py/dxbc_dis.py`).
dxc was not needed because no blob was DXIL. Work files are in `out/agent/shader_research/` (22 MB):

| What | Zone | Asset | Disassembly |
|---|---|---|---|
| Material `tm2co/me_curb_stone_01` | techsets_mp_backlot2 | `mat/.../material/tm2co/me_curb_stone_01.0.asset.json` | |
| Its techset (actual name `tm2co/lit_3_lit_rpl_ta1_800800_1002000000000010_0_1_1_0_0_13810015b_0_0_1_0_0`; the `_0_2_1_..._3818015f` name in the brief is a different set) | techsets_mp_backlot2 | `ts/...` | |
| Static-model lit PS (curb stone, `MTL_TYPE_STATIC_MODEL_SECONDUV_VERTCOL_SELFVIS`) | techsets_mp_backlot2 | pixelshader `033abcdbb572cef353c3338a104cda55` | `sh/.../pixelshader/033abc...0.asm` |
| **World (BSP) lit PS**, the most common `tw/lit_3_lit_rpl_*` shader (`MTL_CONSTANT_LAYOUT_WORLD_LIT_LMAP`) | techsets_mp_backlot2 | pixelshader `01631f08811f0f1923c11aaa9718d79b` | `shw/.../pixelshader/01631f...0.asm` (below: **W:line**) |
| Sky PS | techsets_mp_backlot2 | `8a61dcf2a27ec6cd1904606be8e60d3b` | `shw/.../8a61dc...0.asm` |
| Post: `cs_tonemap`, `cs_universal_clut_generate`, `cs_logaccum*`, ... (180 compute shaders) | code_post_gfx | computeshader | `cs/.../computeshader/*/*.asm` |
| `ggx_env_brdf_lut` (64x64 BC5), `gtso_lut` (16x16x16 BC4) | code_post_gfx | image | `img/...`, decoded to `envbrdf.npy`, `gtso.npy` |

All 791 techsets of techsets_mp_backlot2 are exported in `allts/`. The `tw/` (MTL_TYPE_WORLD) opaque lit sets mostly
share PS `01631f08...`.

**Name source (INFERRED only).** `E:\mw124\mw164proxy\$dst\game-test-src-cleaned\code_source\src\gfx_d3d\` is a
decompile of an IW8 game-test build with symbols: `rb_tonemap.cpp`, `r_universal_clut.cpp`, `r_colorimetry.cpp`,
`r_dvars.cpp`, `r_scene.cpp`, `r_image.cpp`, `rb_backend.cpp`. It is not the Replay binary. Its names and dvar
defaults are used only where they match the Replay bytecode exactly. The CB layouts and constants below match, for
example the 0.18 / 2.84 / 180 exposure constants and the CLUT CB order.

## 1. Inputs of the world lit pixel shader (PS 01631f08)

Vertex inputs (VERIFIED, input signature): `SV_POSITION` (v0.w = linear view depth), `TEXCOORDS` v1.xy,
`LMAPCOORDS` v2.xy, `NORMAL` v3, `TANGENT` v4 (w = bitangent sign). The pixel position is rebuilt from depth:
`P = (cb7[92]*sx + cb7[93]*sy + cb7[94]) * v0.w` with `s = 1 - 2*screenUV`, camera-relative (W:81-86).
`V = -normalize(P)`.

### Material (per-draw `cb3`, 64 bytes = `MaterialConstantBufferDef.psData`; textures from the material table)

| Register | Material slot | Content (VERIFIED from use) |
|---|---|---|
| t1 | textureTable 0 (`*_c&defaultspec`) | rgb albedo; **a = specular/metal code `s`** |
| t2 | 9 (`*_n&*_g`) | **x = gloss**, **y,w = normal** (rotated hemi-octahedral), **z = cavity** (specular-only occlusion) |
| t3 | 59 (`*_t`) | r = tint mask (only if `cb3[1].x & 64`) |

`cb3` packing, VERIFIED on the curb stone's psData `0000803f 01000000 4e36d93f 4e36d93f | 47000000 003c0080 003c003c
00000000 | ffffffff b12fff00 00bc0000 00000000 | 09000000 e300d900 00000080 4f000000`:

- `cb3[0].y bit0`: reflection probes on (1 here). `cb3[0].x/.zw` (1.0, 1.697, 1.697): not read by this PS.
- `cb3[1].x`: flags (0x47; bit 6 = tint mask). UV transform, all half2: `cb3[1].z` = scale (1,1), `cb3[1].y` =
  (cos, sin) = (1, -0), `cb3[1].w` = offset (0,0). `uv' = (c*u*su + s*v*sv, -s*u*su + c*v*sv) + offset` (W:96-106).
- `cb3[2].x` bytes 0..2 = tint RGB/255. Without a mask, `albedo *= tint`. With a mask, `albedo *= lerp(1, tint, t3.r)`
  (W:118-128). `cb3[2].y` bytes are thermal-vision heat (ignore).
- `cb3[3].x`: spec remap `s = (t1.a*(b0+1) + b1)/256`. Curb: b0 = 9 so `s = 10/256 = 0.039` for a white alpha
  (a dielectric 0.04).
- `cb3[3].y`: gloss remap `gloss = (t2.x*(b0+1) + b1)/256` (curb 228/256 scale). Byte 2 is normal strength
  `k = (b2+1)/256` (curb 0.85).

### Engine inputs (registers VERIFIED; names INFERRED)

| Register | Use in the shader | INFERRED identity |
|---|---|---|
| t0 (structured, 8 B, index `cb9[0].y`) | lightmap atlas rect per surface | code buffer 40 |
| t114 / t115 (2D arrays) | lightmap colour / lightmap direction+ambient fraction | baked lightmaps |
| t103, t102, t104, t105 | light-grid fallback, only when `cb7[194].x == 1` (mask t103 at world XY) | lightgrid volume (heightfield t102, index 3D t104, SH L1 array t105) |
| t95 | screen AO, `ao = 1 + cb7[47].x*(saturate(t95*cb7[47].y) - 1)` | GTAO (persistent image 10) |
| t85 | screen-space sun visibility (|value|), skipped (=0) when tile class (t5 word >> 28) is 11 and `cb7[83].w > 0` | `cs_sunvis` output (persistent image 0) |
| t124 (2D array, s11) | octahedral reflection probes, parallax boxes t45, params t44, clusters t46/t47 (`cb7[48..52]`) | reflection probes |
| t123 | 2D LUT `(A,B)` | `ggx_env_brdf_lut` (persistent image 38) |
| t121 | 3D LUT | `gtso_lut`, ground-truth specular occlusion (persistent image 36) |
| t49/t50/t52, t41/t93/t94, t122 | clustered local lights, their shadows, 1D falloff | primary/dynamic lights |
| t56/t57, t116-t119 | decal volumes (albedo/normal/mask/emissive atlases) | decal volumes |
| cb7[42] | `.x` multiplies the final colour | `radiometricScale` (pre-exposure; .y = 1/x) |
| cb7[43] | `.z` x baked diffuse, `.w` x indirect specular | `lightingScale` = (diffuseColorScale, specularColorScale, r_secondaryDiffuseScale x GfxWorldDraw.bakedLightScale, r_secondarySpecularScale); dvars default 1 |
| cb7[56] | xyz sun direction (towards the sun), w != 0 = sun on | `sunLightDir` |
| cb7[57] | rgb sun radiance | `sunLightColorAndUV` = intensity x linear sRGB colour (w = UV, thermal) |
| cb7[58] | thermal IR/heat | `sunLightIrAndHeat` (ignore) |
| cb7[186] | rough-probe fallback `fb = saturate(g'*x + y)` | |
| cb7[12..16], cb7[5].z | thermal / night-vision output path (`cb7[13].x` = 1 or 2) | ignore (0 in normal view) |

Persistent code image `i` binds at register `t(85+i)`: 0 → t85, 10 → t95, 36 → t121, 38 → t123 (INFERRED; every case
agrees with the image list in `r_image.cpp:1223`/`rb_backend.cpp:7482`).

## 2. The lighting equation (world PS, all VERIFIED unless marked)

### Surface decode

```
s      = (t1.a*(b0+1)+b1)/256                       // cb3[3].x
metal  = saturate(s - 0.1) * 1.111111               // W:415-421
albedo = t1.rgb * tint                              // texture is sampled raw; it must be an sRGB format (INFERRED)
diffuseColor = albedo * max(1 - metal, 0)
f0     = metal * albedo + min(s, 0.1)               // dielectric f0 = s (<= 0.1), metal -> albedo
gloss  = saturate((t2.x*(c0+1)+c1)/256)             // cb3[3].y
g'     = 1 - sqrt(1 - gloss)                        // W:422-424
a2     = 2 / (2 + 2^(20*g'))                        // GGX alpha^2 (Blinn power 2^(20g') -> 2/(p+2)), W:444-447
e      = ((t2.yw - 0.5)*k + 0.5)*2 - 1              // k = (cb3[3].y byte2 + 1)/256
o      = ((e.x+e.y)/2, (e.x-e.y)/2);  o.z = 1-|o.x|-|o.y|;  (nx,ny) = normalize(o).xy
nT     = normalize(nx, ny, 1 - 0.6*(nx^2+ny^2))     // the octa z is dropped, W:425-428
N      = normalize(nT.x*T + nT.y*B + nT.z*Ngeom),  B = cross(Ngeom, T)*T.w
cavity = t2.z
NdotV  = saturate(dot(N, V))
```

### Baked diffuse: lightmap (W:459-536)

```
w0,w1  = t0[cb9[0].y]   (uint2)
size   = (w1 & 0xffff, w1 >> 16) * cb7[212].y ;  off = (w0 & 8191, (w0>>13) & 8191) * cb7[212].y ;  layer = w0 >> 26
uv     = LMAPCOORDS * size + off
L      = t114[layer](uv).rgb * cb7[43].z
q      = t115[layer](uv)                 // .xy = hemi-octa direction (same 45-degree encode), .z = ambient fraction
dT     = normalize((q.x+q.y)-1, (q.x-q.y), 1-|..|-|..|)  // tangent-space dominant direction (decode as for the normal)
dW     = normalize(dT.x*T + dT.y*B + dT.z*Ngeom)
Lamb   = q.z * L
Ldir   = (1 - q.z) * L / max(dT.z, 0.001)
E(n)   = Ldir * saturate(dot(n, dW)) + Lamb          // irradiance-like term
aoMB   = ao / (1 - min(diffuseColor,0.85)*(1-ao))   // per channel; ao from GTAO only (multi-bounce)
Ediff  = aoMB * E(N)
Lr     = E(R),  R = reflect(-V, N)                  // used by the specular below
```

Light-grid path (only `cb7[194].x == 1`, mask t103): L1 SH from t105 slices 0..5, blended by t104. Basis
`(0.282095, -0.488603*y, 0.488603*z, -0.488603*x)`, evaluated as `max(0, dot(SH, basis(N)))` and
`max(0, dot(SH, basis(R)))`, scaled by `cb7[43].z` (W:538-604). Stock MP maps use the lightmap path.

Static models (curb PS) get the same `E(n)` shape from vertex data: `Lamb = AMBIENT_COLOR.rgb*a`,
`Ldir = AMBIENT_COLOR.rgb*(1-a)/max(dot(HIGHLIGHT_DIR, Ngeom),0.05)`, `dW = HIGHLIGHT_DIR_VIS.xyz`. Their AO is
`min(|SELFVIS.xyz| * vis, gtao)` with `c = dot(normalize(SELFVIS.xyz),N)*0.5+0.5`, `w = SELFVIS.w` and
`vis = saturate((1.04951c - 0.049585c^2 - 0.007353) + w(2.96123c^2 - 2.96096c + 0.006773) + w^2(1.85757c - 2.85775c^2 + 0.022416))`
(curb asm lines 450-466, 481-494).

### Indirect specular: probes, specular occlusion (W:606-803)

```
if (cb3[0].y & 1):
    fb = saturate(g'*cb7[186].x + cb7[186].y)
    P  = sum_i w_i * probe_i(Rparallax_i, mip) * luma709(Lr)   // probes are luminance-normalised and relit by the lightmap
         mip = 5 - 5g' (lerp floor/floor+1); octa uv = o*0.5+0.5 scaled by 1-(4<<m)/128, offset (4<<m)/256 (4-texel gutter, 256 base, INFERRED)
         w_i = box fade, accumulated front to back until the weights reach 1
    P' = lerp(P, Lr, fb)
    so = gtso_lut( max(sqrt(1-ao), sqrt(1-cavity)), a2, saturate(dot(Ngeom, R)) )
    m  = lerp(P', Lr, 0.85)
    S  = lerp(0.18*m, P', so)
else:
    S  = Lr
(A,B) = ggx_env_brdf_lut(NdotV*63/64+0.5/64, g'*63/64+0.5/64)
Fenv  = f0*A + B
```

### Ambient combine (W:804-829)

```
t  = ((-0.442435g' + 1.7905)g' - 2.24983)g' + 0.948867      // roughness-like polynomial
c  = sqrt(0.5*NdotV + 0.5)
f  = 1.05*(1 - (1-NdotV)^5)
kd = lerp(f, c*(c + min(4t^2,1))*max(1.25-1.6t, 0.55-0.2t), t) + 0.38t^2   // rough diffuse with retro-reflection
color = diffuseColor * Ediff * kd * (1 - max3(Fenv))
      + Fenv * S * cb7[43].w
      + decalEmissive * (1 - max3(Fenv))
```

The `kd` shape matches Activision's published multi-scatter/retro-reflective diffuse (Chan, "Material Advances in
Call of Duty: WWII"). That attribution is INFERRED; the polynomial is VERIFIED.

### Sun (W:837-908)

```
if (cb7[56].w != 0):
    vis = |t85(screenUV)|  (0 if tile class 11 and cb7[83].w > 0)
    Ls  = cb7[56].xyz; H = normalize(Ls + V); NdotL = dot(N, Ls)
    D'  = a2 / (NdotH^2*(a2-1) + 1)^2                          // GGX, no 1/pi
    Vis = 1 / ((|NdotL| + sqrt(a2 + (1-a2)NdotL^2)) * (NdotV + sqrt(a2 + (1-a2)NdotV^2)))   // separable Smith-GGX
    F   = f0 + (1-f0)*(1-|VdotH|)^5                            // Schlick
    (Al,Bl) = ggx_env_brdf_lut(NdotL, g')
    sun = cb7[57].rgb * vis * saturate(NdotL) * ( diffuseColor * (1 - max3(f0*Al+Bl))
                                                + F * D' * Vis * cavity * 1.0390625 )
```

Neither term has a 1/pi, so lights and the lightmap are in "radiance x pi" units. The sun gets no GTAO and no `kd`.
The cavity scales only the specular.

### Local lights (W:910-1197, summary)

Clustered (`t52` depth bins over 32-unit depth slices, `t50` 8x8-pixel tile masks, `cb7[52]`, `cb7[54]`). Each light
(t49, 96 B) is a segment or bulb with a representative point. Falloff `t122(1D)` uses a half-range angular remap
and a window `saturate(r*(1 - d*invR))^2*(3-2x)`. Shadows come from the t94 screen mask (when `cb7[109].x < 0`) or
the t93 shadow array with ESM `exp(k*(z - zs))`. The BRDF is the same GGX/Smith/Schlick with a sphere-light
energy factor. The editor can treat these as point/spot lights with the sun's BRDF.

### Output (W:1198-1216)

```
out = min((ambient + sun + localLights) * cb7[42].x, 32255)     // HDR, pre-exposed by radiometricScale
```

Sky PS (`8a61dcf2`): octahedral sky image (slot 151) x `cb3[0].x` x `cb7[42].x`, plus a dither in the low mantissa
bits (VERIFIED).

### Shipped LUTs (VERIFIED decode)

`ggx_env_brdf_lut` (u = NdotV, v = g', rg = A,B) agrees with a numerical GGX split-sum, using the shader's own
`a2(g')`, within about 0.01 for g' >= 0.5 and NdotV >= 0.5. It differs by up to about 15% for rough or grazing
cases and has small non-monotone patches (`py/ref.py`, `py/scan.py`). **Use the shipped LUT, not a recomputed
one.** The same applies to `gtso_lut`.

## 3. Post chain to the screen

### Exposure (`cs_logaccum*`, `rb_tonemap.cpp`)

The lit output is radiance x `radiometricScale` (cb7[42].x). By default (`r_radiometricScale = -1`) this is the
previous frame's scene exposure, clamped to [0.001, 131072] (INFERRED). The tone map multiplies by
`exposureAndRadiometricScale` (= exposure / radiometricScale, readback slot 14, VERIFIED in the tonemap as
`CB1[1][3].z`), so the pre-exposure cancels out. The net LUT input is:

```
x = WB3x3 * HDR_radiance * exposure * cb7[158].x
```

`cb7[158].x` is INFERRED to be `2^tonemapModeExposureAdjust`, and `r_tonemapUniversalBaseExposureAdjust` defaults
to **+1.5 EV**, so the factor is 2.83. The 0.18 / 2.84 / 1000 constants below are VERIFIED in `logaccumfinish`.

**Manual exposure** (vision `tonemapAuto <= 0.5`):

```
exposure = min( 180 * 2^(2.84 - EV + exposureAdjust), 180 * 2^(2.84 - maxEV) )
```

- `EV` = vision `tonemapExposure`, `exposureAdjust` = `tonemapExposureAdjust`, `maxEV` = `tonemapMaxExposure`.
- This is `0.18 / (0.001 * 2^(EV-2.84))`, the reflected-light meter equation with K = 14 for kilo-nit units
  (INFERRED interpretation).
- EV 10.3319 gives exposure 1.

**Auto exposure**:

```
avg = exp(mean ln luma709) over a centre-weighted downsample (weight (1-r)^tonemapFocus)
t   = max(ln(avg/L_dark) / ln(L_light/L_dark), 0),  L_X = 0.001*2^(X_Ev - 2.84)
key = ( t^p * (k_light - k_dark) + k_dark )^2.2,  k_X = (0.18*2^XExposureAdjust)^(1/2.2)
      p fits the mid point: t_mid^p = (k_mid - k_dark)/(k_light - k_dark)
key = clamp(key, 0.18*2^minAdj, 0.18*2^maxAdj)
exposure = key/avg
```

The result is then blended over time by `adaptSpeed`, and the exposureAdjust and maxEV clamps apply. CB0[4..5] order
is dark/mid/light EV, then dark/mid/light/min/max adjust (order INFERRED).

### `cs_tonemap` (VERIFIED, tonemap.443.asm)

```
c  = hdr * localTonemap(lnL_local) ;  c = (1-veil)*c + veil*clamp(bloomVeil,0,32255)   // CB0[3] = (veil, 1-veil, localStrength, localEvBounds)
c  = vignette(c)                                                                         // CB2[7][214..216]
x  = (CB0[0..2] 3x3) * c * cb7[158].x * exposureAndRadiometricScale
u  = saturate((log2(x + 2^-10) + 10) / 20) * (63/64) + 0.5/64                            // 20-stop log shaper
out = universalClut64(u)            // the LUT holds tone map + grading + display encode; then the UI composite
```

`localStrength` defaults to 0 (local tone mapping off). The white balance is identity unless the vision sets
`toneWhitePoint`.

### `cs_universal_clut_generate` (the curve), SDR path (VERIFIED arithmetic)

The CB0 layout is VERIFIED against `R_UniversalClut_GenerateSingle`. Inputs: `[0].xyz` size (64), `[0].w` output
colorimetry, `[1].x` input type, `[1].y` grading flags, `[1].z` ACES RRT weight, `[1].w` red modifier, `[2]` = (min
lum, max lum, gamma, SDR emulation), `[3].x` = SDR target nits. For each LUT texel:

1. `x = 2^(20u - 10) - 2^-10`, then Rec.709 to Rec.2020 (0.6274 0.3293 0.0433 / 0.0691 0.9195 0.0114 / 0.0164
   0.0880 0.8956).
2. Optional grading (`colorGradingFlags`):
   - Bit 0: up to 8 vision 32^3 CLUTs blended in PQ space, `PQ(x/1024)`, ST.2084 constants.
   - Bit 1: analytical three-key gain/saturation by PQ luma, then lift/gamma/gain.
   - Identity when the vision has none.
3. ACES 1.0 RRT, weighted by `r_universalTonemapUseAcesRrt` (default **1.0**):
   - Rec.2020 to AP0, then glow (0.05, mid 0.08, sigmoid of saturation).
   - Red modifier: pivot 0.03, scale 0.82, weight `r_universalTonemapAcesRedModifier` (default **0**).
   - AP0 to AP1, clamp, 0.96 desaturation.
   - `segmented_spline_c5_fwd` with the stock RRT coefficients: the icb table is
     coefsLow (-4, -4, -3.1573766, -0.48525, 1.8477325, 1.8477325) and coefsHigh (-0.7185482, 2.0810307, 3.6681241,
     4, 4, 4), min/mid/max log2 -17.4739 / -2.4739 / 15.5261.
   - AP1 to XYZ to Rec.2020.
   - `out = lerp(32*x, RRT(x), weight)`.
4. Display mapping in nits: the optional gamut matrix (daltonize), then `y = 10000*(y/10000)^gamma`
   (`r_displayMappingSdrGamma` = 1). Then `y /= sdrTargetLum` (`r_displayMappingSdrTargetLum` = **333**).
5. Shoulder: per channel, `y' = y < 0.15 ? y : 0.15 + 0.85*(1 - exp(-(y-0.15)/0.85))`. Then hue preserve
   `y = y * luma2020(y')/luma2020(y)`. Then desaturate to luma by
   `max(0.8*(1.1*h(L) - 0.1), gamutExcess*0.5)`, with
   `h(L) = L < 2.5 ? 0.2L : 0.5 + 0.5(1 - e^(-2(0.2L - 0.5)))`.
6. Rec.2020 to Rec.709, saturate, then encode by colorimetry:
   - 0: `y^(1/2.2)`
   - **1: `y^(1/2.4)`** (default `r_colorimetrySdr` = 1)
   - 2: BT.709 OETF (`4.5y` below 0.018, else `1.099y^0.45 - 0.099`)
   - 3: HDR10 PQ, an ICtCp path (not used for SDR)
   - other: linear
7. `out = saturate(lerp(minLum, maxLum, y))`. In SDR the default is min = `r_blacklevel` = 0 and max = 1.

**Editor recipe (SDR).**

```
display = clut( radiance * exposure(vision EV) * 2^1.5 )
```

Here `clut` = steps 1-7 with no grading, RRT weight 1, red modifier 0, gamma 1, target 333 nits, and encode
`^(1/2.4)`. Bake the steps into a 64^3 LUT. Add film grain (vision `tonemapGrainStrength`), bloom veil and SMAA only
if wanted.

## What cannot be determined statically

- Runtime values: the vision EV / auto-exposure settings per map (read from the map's vision set, `tonemap*` fields),
  `GfxWorldDraw.bakedLightScale` (in the map's GfxWorld), the sun intensity and colour (from the primary light
  record), and the vision grading CLUTs.
- Whether Replay keeps the game-test defaults: RRT weight 1, red modifier 0, SDR target 333, base exposure +1.5 EV,
  colorimetry 1. These are INFERRED from the decompile, not read from Replay's exe.
- The colorimetry enum ordinals: the shader cases are VERIFIED; enum value 1 = the default is INFERRED.
- `cb7[158].x`'s exact identity, the probe resolution (256 assumed from the 4-texel gutter maths), the meaning of
  `cb3[0].x/zw` and `cb3[3].zw`, and the meaning of the `1.0390625` sun specular factor (a constant, purpose unknown).
- The local-light and decal parts were read for their structure only; their per-record layouts (t49 96 B, t56 256 B)
  are not fully decoded.

## Editor maps in game: the converter's world shader (VERIFIED from the converter source and a built map)

The sections above are the **stock** world shader. An editor-built map does not draw its world with it. The editor's
maps go through the converter's IW3 route (doc 55), and with a lightmap present that route gives every world surface a
generated native world material `w/mw120r_<map>_world_<id>` (the built gfx_map of tools/lightmap_bake_build_test.py:
all world surfaces on lightmapIndex 0 with these materials). Its pixel shader is
`x64-zt src/convert/iw3_iw8/resources/map_surface_realtime.hlsl`, built with `MAP_SOURCE_SUN_MASK 1`,
`MAP_SOURCE_CHANNELS 1`, `MAP_NATIVE_WORLD`, `MAP_LIGHTMAP_TEXTURES` (+ `MAP_LIGHTMAP_LOG` for log-encoded pairs) and,
in the iw8 style, `MAP_IW8_LIGHTING` and `MAP_LIGHT_UNIT_SCALE` (iw3_vision.cpp). The source is in the fork, so the
viewport can follow it line by line. Its light (main, around line 1336-1615):

- Lightmap (`SourceIndirect`): the two IW3 layers sampled bilinear at the lightmap UV (A rgb + slope X in A.a, B rgb +
  slope Y in B.a), log-decoded `(2^(x log2(1 + sM)) - 1) / s`; slope `(a * 4.08 - 2.08, b * 4.0645 - 2.0645)`;
  `indirect = A * Nt.z + B * sat(dot(normalize(slope, 1), Nt))` with Nt the tangent-space shading normal; times
  `MAP_INDIRECT_GAIN` (1).
- Sun visibility: the baked sun depth where it covers the point, else min(visibility, the primary byte) (the IW3 sun
  mask; the editor writes its sun visibility there).
- Specular (iw8 style): F0 .04 and gloss .15 unless the material gives a specular map; probe reflection scaled by the
  indirect luminance, specular occlusion LUT, environment BRDF; indirect diffuse times 1 - max(F).
- `indirect *= cb[43].z * gtao / bounce`; `light = indirect + visibility * (N.L + transmitted) * sun` with
  `sun = cb[57] / MAP_LIGHT_UNIT_SCALE` (the realtime sun), colour = albedo x light, plus the realtime local lights
  `(albedo x diffuse + specular) / MAP_LIGHT_UNIT_SCALE`, then fog, then `x MAP_LIGHT_UNIT_SCALE x cb[42].x` into the
  stock post chain (exposure and tonemap of the sections above).

Consequence for the bake: the sun and the converted lights are realtime, so the lightmap holds only the sky, their
bounce and the sun visibility (`BakeScene::sunDirect = false`, `BakeLight::direct = false` for the lights within the
primary light limit). Consequence for the viewport: a 1-to-1 preview of an editor map follows this shader (the
lightmap formula above, the realtime sun with the visibility, the local lights), then the stock exposure and tonemap.
