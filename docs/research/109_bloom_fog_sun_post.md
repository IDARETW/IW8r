# 109. Bloom (veil), volumetric fog, the sun and the last post effects on a stock MP map (Replay 1.20)

**Why.** PLAN_REMAINING item 1.8. The viewport's Game Lighting branch already matches the lit shader, the exposure and
the tone curve (research 95 section 3). This note decodes the rest of what Replay puts on screen for a stock MP map
(mp_m_speedball, mp_hideout):

1. the bloom / veil chain;
2. the volumetric fog;
3. the sun disc and flare;
4. vignette, film grain, chromatic aberration and sharpening.

Each part gives the math, the constants and their sources, and an editor recipe.

**Labels.** VERIFIED = read in the disassembly, the Replay exe, an asset export or the world cache (cited). INFERRED =
names, meanings or defaults that come from usage, from arithmetic, or from the IW8 game-test decompile ("GT",
`E:\mw124\mw164proxy\$dst\game-test-src-cleaned\code_source\src`). The DXBC has no RDEF block, so register and field
names are ours. Nothing was run in the game.

## How it was obtained

- **Compute shaders** come from research 95's `code_post_gfx` export (`out/agent/shader_research/cs`). They were
  disassembled again (`dxbc_dis.py`) into `out/agent/post109/asm/` with the NUL bytes stripped. They are:
  - the veil chain: `cs_downsample_veil.456` (linear), `.457` (tonemap/Karis), `cs_upsample_veil.458`;
  - `tonemap.443`;
  - the fog and volumetric composite: `cs_post_opaque_processes.288`;
  - the volumetric passes: `cs_volumetric_forwardplus_*` 473-482 and `cs_vol_sh_sample_lightgrid_*` 459-462.
- **Pixel shaders** were exported with ACTS from `code_post_gfx.ff` after a `material,techset` export (`mt/`):
  - `filmgrain` PS `6d0c1799`;
  - `composite_postfx` PS `0ddfcf1f`;
  - `smaa_filmic_filtering` PS `98525fea`;
  - `smaa_temporal_supersampling` PS `d4f25d0c`;
  - `lens_profile_distortion` PS `1f51d574`.
- **Image**: `hg_lut` (64x64x16 BC4), exported with `--xpak code_post_gfx.xpak`.
- **Replay exe** (`game_dx12_ship_replay.exe` 1.20.4.7623265): read statically with pefile + capstone (`py/pe.py`,
  `disx.py`, `dvarreg.py`). The IDA bridge was down ("IDA Assist is not reachable"). The exe holds its own
  token-to-name table, so registrations can be found by token. The vision field table `visionDefFields` (218 entries
  of 40 bytes at RVA 0x462A110) gives every field's default (`py/visdef.py`).
- **Visions**: the world cache's `vision/<map>.vision.0`. Tokens were named with `E:\mw124\Dvars.txt`
  (`py/vision_dump.py`).
- **Sun records**: GfxWorld `sun` from the world cache's `gfx_map` JSON.

Shader line citations are line numbers in `out/agent/post109/asm/<file>`. Short tags used below:

| Tag | Shader |
|---|---|
| TM | `tonemap.443.asm` |
| D4 | `cs_downsample_veil.457.asm` |
| D8 | `cs_downsample_veil.456.asm` |
| UP | `cs_upsample_veil.458.asm` |
| PO | `cs_post_opaque_processes.288.asm` |
| AC | `..._accumulate_global.473.asm` |
| SC | `..._scatter_global.479.asm` |
| FG | `ps_6d0c1799...asm` |
| CP | `ps_0ddfcf1f...asm` |

## 0. Summary

**Order of the frame (INFERRED from GT `rb_draw3d.cpp`, `rb_tonemap.cpp`, `rb_postfx.cpp`; each stage VERIFIED in its
shader):**

1. Opaque lit pass.
2. `cs_post_opaque_processes`: the analytic fog plus the volumetric fog, composited onto the HDR scene.
3. Transparents and effects.
4. The scene mip chain and the veil.
5. `cs_tonemap`: veil mix, then vignette, then white balance, then exposure, then the LUT.
6. SMAA filmic T2x.
7. Lens effects (off on these maps).
8. Display composite with film grain, then UI.

**Values for the two maps:**

| | mp_m_speedball | mp_hideout | Source |
|---|---|---|---|
| veilStrength | **0.08** | **0.087** | vision; default 0 |
| tonemapGrainStrength (x r_filmGrainAtten 0.25) | 0.1 -> **0.025** | 0.2 -> **0.05** | vision; exe |
| vignette intensity / size / falloff | **0.4** / 0.99 / 1 | **0.05** / 1 (default) / 1 (default) | vision; defaults from exe |
| volumetricAirDensity (x 0.01 = sigma per unit) | 0.02 -> 2e-4 | 0.1 -> 1e-3 | vision; exe |
| volumetric height base / half distance | 0 / 0 (uniform) | 0 / **20** (ground layer) | vision |
| volumetric sun brightness / anisotropy g / albedo | 0.5 / **0.8** / (1, .96, .88) | 0.4 / 0.35 / (.71, .93, 1) | vision |
| volumetric spot / omni / ambient brightness | 1 / 0 / 1 | 0.7 / 0.8 / 1 | vision |
| chromaStrength, chromaRadius | unset (0) | 0, 0 | vision; default 0 |
| sun sprite / flare | size 16, flare alpha 1, but **sunFxPosition straight down** | size 0, alpha 0 (none) | GfxWorld |

**Findings:**

1. **Veil (section 1).** The scene is downsampled to mips 1..8, Karis-weighted on mip 1 only. Mips 3..8 are then
   upsample-accumulated into a quarter-res veil with weights 0.025, 0.075, 0.15, 0.2, 0.25, 0.3, read in the exe. The
   veil is an energy-conserving lerp: `hdr' = (1-v)*hdr + v*veil`. There is no threshold or knee. At 1080p half of
   the veil's energy is beyond 128 px.
2. **Volumetric fog (section 2).** It is on for both maps: `r_volumetrics` 1, the DCONST `r_volumetricsA` 1,
   `r_volumetricsDisableHack` 1, and air density > 0. It uses a froxel grid of about 160 x 90 x 128 slices in 4 depth
   cascades (128, 384, 640, 1024 units), so it reaches about 2180 units.
   - It holds a sun term (shadowed, HG phase from `hg_lut`, saturating at 1) and an ambient light-grid term. Both are
     integrated front to back.
   - It is composited in the same pass as the analytic fog, before transparents and the post chain:
     `out = scene*T_fog*T_vol + fogIn*T_vol + S_vol`.
   - **This is on top of the editor's analytic fog and is visible.** Speedball loses 35 % (T_vol 0.65) beyond 2180
     units, including the sky, and gains a bright sun halo of about 15 degrees. Hideout has a dense layer below z = 0.
3. **Sun (section 3).**
   - The sky shader draws no sun term.
   - The sun sprite/flare system is off on hideout. On speedball it is defined but points straight down, so it is
     invisible.
   - What shows the sun on these maps is the sky image itself, the volumetric HG lobe, the analytic fog's Mie lobe and
     the veil.
4. **Vignette, grain, chromatic aberration, sharpening (section 4).**
   - Vignette: decoded with the exe's constant fill. Speedball's corners are x0.60 in linear HDR.
   - Grain: multiplicative white noise on the display value, damped in highlights and shadows, strength x0.25 by
     default.
   - Chromatic aberration: off on both maps.
   - Sharpening: no sharpening pass exists, only DOF bokeh and the TAA history term.
5. **A side finding.** Hideout's vision sets **white balance method 4** (D illuminant 5500 K, green-magenta 2.2). So
   cs_tonemap's CB0[0..2] matrix is not identity there. Research 95 assumed identity. This matrix is not decoded here.

## 1. Bloom / veil

### 1.1 Where `veil` comes from (VERIFIED)

- **Vision.** `veilStrength` (token `MMQLTMKTPK`): min 0, max 1, **default 0** (`visionDefFields[137]`). Speedball sets
  0.08 and hideout 0.087. The GT dvar text calls 0.087 "the physically correct value". A map that does not set the
  field has no veil.
- **CPU.** `R_SetPerceptualInfo` is inlined at RVA 0x197C02E-0x197C11B.
  - `r_veilUseTweaks` is a DCONST byte at .rdata 0x25444A8 and is 0. So the strength is the scene parameter
    `sceneParms+0x2E8` (from the vision), and `veilEnabled = strength > 0`.
  - `r_veil` is a DCONST byte at 0x25444AA and is 1. If it were 0 the veil would be forced off.
  - The tweak values exist but are unused: `r_veilStrength` 0 (0x25444AC) and `r_nvg_veilStrength` 0.03.
- **Shader.** The tonemap pass binds CB0[3] = (veil, 1-veil, localStrength, localEvBounds) (GT `rb_tonemap.cpp:749-755`).
  T4 = the veil texture, or black when the veil is disabled.

### 1.2 The chain (VERIFIED: shaders; mip counts and weights read in the Replay exe)

**Mip counts.** The functions are at RVA 0x18760A0, 0x18760B0 and 0x1876090. GT names are
`R_Perceptual_GetSceneDownsampleVeilMip`, `...VeilMipCount` and `...TotalMipCount`.

```
exposureMip = halvings until w <= 160 and h <= 90          (1080p: 4)
veilMip     = min(2, exposureMip)                          (2 = quarter res)
veilCount   = min( min(floor(log2(min(W, H))), 9) - veilMip, 7 )   (7 for any height >= 512)
scene mips  = 1 .. max(exposureMip, veilMip + veilCount - 1)        (1..8; size = maxSceneRt >> mip, at least 1)
```

At 1080p the mips are: 1 = 960x540, 2 = 480x270, 3 = 240x135, 4 = 120x67, 5 = 60x33, 6 = 30x16, 7 = 15x8, 8 = 7x4.
The source is the HDR scene colour at tonemap time, after fog, volumetrics and transparents (`rb_tonemap.cpp:445`).
The format is `GFX_RENDERTARGET_FORMAT_SCENE_MIP`.

**Downsample** (one thread per destination texel, CB0[0] = (dstW, dstH, 1/dstW, 1/dstH)). In GT
`R_Perceptual_DownsampleCS`, CB0[1].zw = (max(displayH/1080/aspectRatioScenePixel, 1), max(displayH/1080, 1)), the
kernel's resolution scale; it is 1 at 1080p. So `d = CB0[0].zw * CB0[1].zw` is one destination texel.

```hlsl
// interleaved gradient noise, D8:30-39 (CB1[7] = cb7; cb7[45].z = frame counter, cb7[45].x = 0/1 sign flip: INFERRED)
float2 p = (px + 0.5 + cb7[45].z * 5.588238) * (cb7[45].x * 2 - 1);
float  n = frac(52.982918 * frac(dot(p, float2(0.06711056, 0.00583715))));
// tap k: radius and weight alternate (outer, inner); rotation by the noise
static const float R_OUT = 1.886476, R_IN = 0.939981, W_C = 0.479925, W_OUT = 0.075948, W_IN = 0.302964;
float2 tap(int k, float stepAngle, float n) { float a = (k + 0.5 + 0.5 * n) * stepAngle; return float2(sin(a), cos(a)) * ((k & 1) ? R_IN : R_OUT); }

// cs_downsample_veil.456 ("linear", mips 2..8), D8:30-98: 8 taps at 45 degrees, normalised (0.501110 = 1 / 1.995573)
float3 c = W_C * src.SampleLevel(lin, uv, 0);
for (int k = 0; k < 8; ++k) c += ((k & 1) ? W_IN : W_OUT) * src.SampleLevel(lin, uv + tap(k, PI / 4, n) * d, 0);
dst = clamp(c * 0.501110, -32255, 32255);

// cs_downsample_veil.457 ("tonemap", mip 1 only; firefly control), D4:30-117: 4 taps at 90 degrees, each weighted
// by a Karis factor 1/(1 + max3(w*s)) (the 0x7EF311C2 - asint() reciprocal), 4 overlapping 3-tap groups averaged
float4 s[5]; s[4] = float4(W_C * centre, W_C);
for (k = 0..3) s[k] = float4(wk * sample(uv + tap(k, PI/2, n) * d), wk);   // wk = W_OUT for k even, W_IN for k odd
for (all i) s[i] *= 1 / (1 + max3(s[i].rgb));
dst = clamp(0.25 * ( G(4,0,1) + G(4,1,2) + G(4,2,3) + G(4,3,0) ), -32255, 32255);  // G = sum(s.rgb) / max(sum(s.w), 1e-5)
```

The weights are a Gaussian of sigma about 0.98 destination texels, sampled at the two radii: 0.302964/0.479925 =
exp(-0.94^2/2s^2) and 0.075948/0.479925 = exp(-1.886^2/2s^2) both give s = 0.98 (INFERRED reading). The mip-1 Karis
pass runs only when `r_veilTonemap` is on. GT's default is 1; in Replay it is the viewInfo flag bit 53 at 0x18B5A6A
(value INFERRED 1).

**Upsample** (`cs_upsample_veil.458`, UP:30-75). The destination is twice the source. Offsets are in source texels
(UP:31 doubles d). CB0[1].xy = (wIn, wOut), from GT `R_Perceptual_UpsampleCS` data[4..5].

```hlsl
float3 b = W_C * src.SampleLevel(lin, uv, 0);
for (int k = 0; k < 4; ++k) b += ((k & 1) ? W_IN : W_OUT) * src.SampleLevel(lin, uv + tap(k, PI / 2, n) * 2 * d, 0);
dst = b * 0.807919 * wIn + add.SampleLevel(lin, uv, 0) * wOut;   // 0.807919 = 1 / 1.237749
```

**The accumulation.** Loop at RVA 0x18B0B90. The first wIn is weights[count-1]; later wIn values are 1.0 (.rdata
0x275E798). wOut is weights[i-1]. The weights are `s_veilWeights` = {0, 0.025, 0.075, 0.15, 0.2, 0.25, 0.3}: the
.rdata 0x25444F0 table is stored to 0xFA8DBE0 by 0x18761A0, which the tonemap calls each frame at 0x18B5A10. This is
GT's `r_veilFalloffWeight1/2` (0.25 0.75 1.5 / 2 2.5 3) normalised by their sum 10, constant-folded. The pass runs only
when the viewInfo veil flag is set (0x18B5B90).

```
V7 = Up(S8) * 0.30 + S7 * 0.25
V6 = Up(V7) + S6 * 0.20
V5 = Up(V6) + S5 * 0.15
V4 = Up(V5) + S4 * 0.075
V3 = Up(V4) + S3 * 0.025
V2 = Up(V3)                (+ S2 * 0: the quarter-res mip itself is not used)
```

### 1.3 Applying it (VERIFIED, TM:45-64)

```hlsl
float3 hdr = scene.Load(px).rgb * localTonemap;                  // localStrength = 0 on both maps -> factor 1 (TM:46-59)
float3 v   = clamp(veilTex.SampleLevel(lin, px * invScreen, 0).rgb, 0, 32255);   // V2, quarter res, bilinear (TM:60-62)
hdr = cb0[3].y * hdr + cb0[3].x * v;                             // (1 - veil) * hdr + veil * V2 (TM:63-64)
// then vignette (section 4.1), white balance, exposure and the LUT (research 95)
```

The veil is mixed in linear HDR before exposure, so it does not depend on the exposure. There is no threshold and no
knee. Everything blooms in proportion: a lerp of 8 % toward a very wide blur of the frame. It lifts dark areas next to
bright ones and halos the sky and the sun.

### 1.4 The resulting point-spread function (computed, `py/veil_ref.py`, 1080p, noise rotation averaged)

| radius (px) | 8 | 16 | 32 | 64 | 128 | 256 | 512 |
|---|---|---|---|---|---|---|---|
| energy inside | 1.7 % | 5.7 % | 14 % | 29 % | 48 % | 70 % | 92 % |

The PSF fits a sum of Gaussians, one per scene mip 3..8, with sigma 8.8, 17.6, 35, 70, 141 and 282 px and weights 0.020,
0.073, 0.145, 0.220, 0.200 and 0.452. The weights sum to 1.10. The excess over 1.0 comes from edge clamping on the 7x4
and 15x8 mips (INFERRED; the sampler address mode was not read).

### 1.5 Editor recipe

The viewport already renders the linear HDR scene into `hdrScene` (t15) and applies `gamePost` in `ps_post`
(`render.cpp:472-522`).

1. **Simplest correct step: the real chain.**
   - Create 8 R16G16B16A16F targets sized `(W >> k, H >> k)` for k = 1..8, and 6 for V7..V2. Short side >= 512 gives 7
     veil mips. Below that, use the count formula.
   - Write three small pixel shaders from 1.2: Down8, DownKaris for mip 1, and Up.
   - Use a fixed rotation n = 0.5. The game rotates per pixel per frame and its TAA averages that out. The editor has no
     TAA, so per-pixel noise would show.
   - In `ps_post`, before `gamePost`: `h = (1 - veil) * h + veil * V2.SampleLevel(linear, uv)`, with
     `veil = veilStrength` from the vision (0 if unset).
   - Cost: 14 tiny passes, under 0.2 ms.
2. **Approximation, if the chain is not wanted.** Blur `hdrScene` with the six Gaussians of 1.4, scaled by
   viewport height / 1080, using separable passes on mips 3..8. Weight them by the fitted weights normalised to 1.
3. **Check.** With veil 0.08, a pixel of radiance 0 next to a sky of radiance 10 that fills half the frame should rise
   to about 0.08 x 5 = 0.4 before exposure.

## 2. Volumetric fog

### 2.1 Is it on for MP? Yes (VERIFIED)

- **Refdef enable.** RVA 0x1723044-0x1723072 sets
  `refdef.volumetrics.enabled = r_volumetrics && r_volumetricsA && r_volumetricsDisableHack`.
  - `r_volumetrics` (`QPLMKRON`, "Enable volumetric fog rendering") is registered with default **1** at 0x1930EB3.
  - `r_volumetricsA` is a DCONST byte at .rdata 0x257AAAC and is **1**.
  - `r_volumetricsDisableHack` (`OLONSLNOPK`) is registered with default **1** at 0x192C987.
- **Air density.** The refdef copies the vision's air density unchanged (0x1723078). The pass runs when the air density
  is > 0, or when local media (FX fog volumes) are present: GT `R_VOL_AnyVisible`.
- **Defaults and map values.** The vision default is 0 (`visionDefFields[138]`). Speedball sets 0.02 and hideout 0.1,
  so both maps draw it.
- **Not to be confused with** the old screen-space `r_volumeLightScatter*` (vision default 0, unset on both maps: off)
  and the FX flare system (section 3.3).

### 2.2 Grid and slices (VERIFIED)

- **Grid size.** About 12x12 pixels per froxel: 160 x 90 at 1080p; the width is capped at 1920, or 1344 on low
  settings (GT `R_InitVolumetric`). The grid has 128 depth slices (AC:48 `uge 128`) in 4 cascades of 32 slices.
- **Cascade widths** are `cb7[48]` = `r_volumetricDepth` (`MPOKKOPMTN`), registered at 0x192C9A9-0x192CA03 with
  default **(128, 384, 640, 1024)**, min 1, max 4096. Stock SP scripts set the same value (research 60); MP scripts do
  not change it. `cb7[49]` = 1/width.
- **The same 32-per-cascade grid** is used by the probe and light clusters (world PS `asm/W_01631f08.asm:622-627`; research 108).

```hlsl
float sliceDepth(float s /*0..1*/) { return 4 + dot(saturate((s - float4(0, .25, .5, .75)) * 4), cb7[48]); }  // AC:59-62
float sliceCoord(float d)          { float e = d - 4;                                                           // PO:349-354
    return dot(saturate((float4(e, e - cb7[48].x, e - cb7[48].x - cb7[48].y, e - cb7[48].x - cb7[48].y - cb7[48].z)) * cb7[49]), 0.25); }
// slice z thickness dz = cb7[48][z / 32] / 32 (4, 12, 20, 32 units); range 4..2180 units (about 55 m)
```

Depth comes from the float-Z images stored /6 (fp16 range): x6 at PO:43 and SC:203-204. The sky's depth is replaced by
`cb7[138].x` (PO:44-47), which lands beyond the last slice, so **the sky gets the full volume**.

### 2.3 Per-froxel scattering (VERIFIED: SC = the sun variant; 481/482 add primary lights, 476/478/480/482 local media)

The constant fill is GT `R_VOL_SetConstantsCommon`. The Replay copy is at 0x18B9596-0x18B965A and VERIFIES two things:
`airDensity * 0.01` (0x18B95A1), and `heightFogExponent = -ln2 / max(halfPlane, 0.1)` only when halfPlane != 0, else 0
(0x18B962D-0x18B965A). The CB0 slot meanings below are INFERRED from usage plus GT field order:

- CB0[15].yzw = albedo;
- CB0[16] = (airDensity x 0.01, absorption, sunAnisotropy, ...);
- CB0[18] = (sun, spot, omni, ambient) brightness;
- CB0[19].zw = (heightFogBase, heightFogExponent).

```hlsl
float3 P   = eye + ray * d;                       // jittered froxel centre (128-entry random table + frame CB0[15].x), SC:184-225
float  dz  = cb7[48][slice / 32] * 0.03125;                                                     // SC:220-226
float  rho = exp(max(P.z - hBase, 0) * hExp);     // hExp = -ln2/half, 0 if half == 0          // SC:227-231
float  sig = airDensity * 0.01 * rho;                                                           // SC:232
// sun: cascaded + static (T8 tree) + cloud shadow visibility (SC:233-490), phase from hg_lut, x pi, x sun radiance cb7[57]
float  c    = dot(cb7[56].xyz, normalize(P - eye)) * sign(g);                                   // SC:496-504
float  ph   = hgLut.SampleLevel(lin, float3(0.5 + 0.5 * c, 1 / (4 * PI), abs(g)) + jitter, 0);  // SC:505-512
float3 sun  = vis * sunBrightness * ph * PI * cb7[57].rgb * dz;                                 // SC:513-517
float3 amb  = ambBrightness * ambVolume.SampleLevel(lin, froxelUvw, 0).rgb;   // light grid, no dz  SC:524-526
float3 scat = albedo * sig * (sun + amb);                       // stored x radiometricScale      SC:527-529, 572
float  tau  = (sig + absorption) * dz;                                                          // SC:573-575
```

- **The phase LUT** (`hg_lut`, `py/hg_lut.py`). At the row the shader reads (v = 1/4 pi), it equals
  **min(HG(c, g), 1)** within the BC4 quantisation (scale 0.99, error about 10 % for g <= 0.55). Henyey-Greenstein is
  `HG = (1-g^2)/(4 pi (1+g^2-2gc)^1.5)`. The UNORM texture clips the forward peak at 1. For speedball's g = 0.8 the
  phase is flat at 1 within **14.9 degrees** of the sun. The name `rgp.hgPhaseFunction` is GT `r_volumetric.cpp:2256`.
- **The ambient volume** (`cs_vol_sh_sample_lightgrid_aniso.459`, 725-819) is the light grid's L2 SH convolved with HG.
  Band 1 is scaled by g and band 2 by g^2 (lines 730-738). It is evaluated along `cb7[94]`, then multiplied by
  `cb7[43].z` (baked light scale) and `cb7[42].x`. The direction meaning is INFERRED: `cb7[94]` is the camera's frustum
  centre vector.
- **The ambient term has no slice thickness** (SC:524-528). Only the sun term is multiplied by dz. So the ambient
  in-scatter sums about 128 slices x sigma, which is small. This is VERIFIED arithmetic; whether it is intended is
  unknown.
- **Temporal reprojection.** The scatter output is blended with last frame's (AC:94-116). The temporal factors are
  0.95 by default (registrations at 0x192CAB3-0x192CB39). The editor can skip this and march without jitter.

### 2.4 Integration and composite (VERIFIED)

**Integration** (AC:48-132): one thread per froxel column, front to back. It stops at the column's farthest occupied
slice (T1[16]).

```hlsl
float tauSum = 0; float3 S = 0;
for (z = 0; z < 128; ++z) { S += min(exp(-tauSum), 1) * scat[z]; tauSum += tau[z];
                            outScatter[z] = S; outTrans[z] = min(exp(-tauSum), 1); }   // AC:117-129
```

**Composite**, in the same pass as the analytic fog. PO:355-438 is the first of the 2x2 pixels each thread handles:

```hlsl
float  Tv = transVol.SampleLevel(lin, float3(uv, sliceCoord(depth)), 0);          // PO:355 (T2)
float3 Sv = bicubicXY(scatVol, float3(uv, sliceCoord(depth))) * cb7[42].y;        // PO:356-410 (T1, 4-tap B-spline in xy)
float3 inscatter = fogIn * Tv + Sv;               // fogIn, fogT: the analytic fog of PO:34-345 (r9, r8)
out = scene * (fogT * Tv) + inscatter * cb7[42].x;                                  // PO:411-438
```

So the volume sits between the camera and the analytic fog. It applies to opaque pixels and the sky, before
transparents, the veil and the tone map. The editor fogs in the forward pass (`gameFogApply`), so it can apply the
volume afterwards on the already fogged HDR value: `out = fogged * Tv + Sv`. This is the same thing.

### 2.5 What it does on the two maps (computed from the vision values, `py/post_ref.py`; not compared with the game)

| | speedball | hideout |
|---|---|---|
| density | uniform 2e-4 per unit | 1e-3 per unit below z = 0, halving every 20 units above |
| T_vol for anything beyond 2180 units, level ray from z = 64 | **0.647** | 0.789 (11.5 degrees up: 0.985; from z = -64: **0.113**) |
| sun in-scatter, unshadowed, level ray, at 0 / 15 / 30 / 90 / 180 degrees from the sun | **16.4 / 16.1 / 3.7** / 0.22 / 0.08 | 1.7 / 1.6 / 1.2 / 0.39 / 0.19 |
| ambient in-scatter | 0.022 x L_amb | 0.013 x L_amb (z = 64) to 0.074 x L_amb (z = -64) |

In-scatter values are in scene radiance units (x radiometricScale on screen). Sun radiance is 30.7 for speedball and
26.9 for hideout (`game_look.cpp:305-306`). A sunlit 0.3-albedo surface is about 6 units.

- **Speedball** dims everything beyond about 55 m by a third, the sky included. Looking toward an unshadowed sun, it
  adds a bright flat halo about 15 degrees wide. Shadowed froxels give shafts.
- **Hideout** has a thin layer that matters only near and below z = 0. Hideout's stock lights sit at z -135 to 292
  (research 106 data), so low ground and the river bed are hazed.
- **Caution.** The editor's analytic fog was calibrated on 2026-11-12 against an in-game speedball screenshot
  (`game_look.h:93`). That screenshot already contained this volume and the veil. Adding the volume on top of the
  calibrated fog may count the haze twice. Port the exact analytic fog from PO:34-345 first, or re-calibrate.

### 2.6 Editor recipe

March per pixel in `ps_post`, or in a quarter-res pass upsampled bilinearly. Use the linear depth the viewport already
keeps for SSR.

```hlsl
// inputs from the vision: air, absorb, albedo, sunB, ambB, g, hBase, hHalf; cb: sun dir L (toward sun), sun radiance
float3 volumetric(float3 eye, float3 V, float D, float3 fogged) {
    const float4 W = float4(128, 384, 640, 1024);
    float hExp = hHalf != 0 ? -0.693147 / max(hHalf, 0.1) : 0;
    float T = 1; float3 S = 0;
    for (int z = 0; z < 128; z += STEP) {                       // STEP 4 (32 samples) is close enough to start
        float s = (z + 0.5 * STEP) / 128, d = 4 + dot(saturate((s - float4(0, .25, .5, .75)) * 4), W);
        if (d > D) break;
        float dz = W[z / 32] / 32 * STEP;
        float3 P = eye + V * d;
        float sig = air * 0.01 * exp(max(P.z - hBase, 0) * hExp);
        float c = dot(L, V);
        float ph = min((1 - g*g) / (12.566371 * pow(1 + g*g - 2*g*c, 1.5)), 1);
        float3 sun = sunShadow(P) * sunB * ph * 3.141593 * sunRadiance * dz;
        float3 amb = ambB * ambientAt(P) * STEP;                 // per-slice term, no dz (2.3)
        S += T * albedo * sig * (sun + amb);
        T *= exp(-(sig + absorb) * dz);
    }
    return fogged * T + S;                                       // D = 1e9 for the sky
}
```

- **Shadow**: the viewport's sun shadow map (cascade/near map) gives vis.
- **Ambient**: `ambientAt` = the light grid's SH DC x 0.282095 x bakedLightScale. The editor reads the light grid
  already (research 105). A constant sky ambient is an acceptable first step, because the term is small.
- **Order in `ps_post`**: volumetric, then veil, then vignette, then `gamePost`, then grain.
- **Spot and omni lights**: local lights also scatter (variants 477/478/481/482, with `volumetricSpot/OmniBrightness`).
  These were not decoded and should be left for later.

### 2.7 Addendum 2026-11-14: the constants in Replay, and a measured comparison

**The constant block (VERIFIED, Replay `R_VOL_SetConstants` at `0x1418B94B0`, read in Hex-Rays; 320 bytes).** It
confirms the slot meanings that section 2.3 inferred:

- CB0[8] = (grid width, grid height, 1 / width, 1 / height);
- CB0[13].w = 128 (the slice count);
- CB0[15].yzw = three floats of the view's volumetric settings (the albedo);
- CB0[16] = (airDensity x 0.01, the next setting (absorption), two more (the anisotropy among them));
- CB0[17].w = a setting squared;
- CB0[18] = four floats copied as one block, only for the scatter stage (the sun, spot, omni and ambient brightness);
- CB0[19].zw = (height base, `-ln2 / max(halfPlane, 0.1)` or 0 when halfPlane is 0).

No further scale appears in the fill. So the editor's march (section 2.6) uses the game's constants as they are.

**Against the owner's picture** (`out/compare/speedball_ingame.webp`, bench camera `-1080 230 330 -4 -27 120`, mean
luminance of 8 x 4 blocks of the frame, 0 .. 255):

| | mean block error | sky row | lower rows | view / game |
|---|---|---|---|---|
| volumetric off (the default) | 11.3 | 9.2 | 12.0 | 1.055 |
| volumetric on | 16.8 | 20.3 | 15.6 | 0.951 |

With the volume on, the sky comes out too dark (the volume takes a third of it, as the shader says it does), while
the editor's sky without the volume already matches the game's picture. So the sky's own brightness in the editor is
about a third too low, and the missing volume hides it: the two errors cancel. Turning the volume on needs the sky's
radiance settled first (its intensity and the conversion exposure of the sky image, research 95 / 103), which this
one picture cannot separate from the volume's sun halo. Until then the option stays off: the picture is closer to the
game without it.

## 3. Sun disc and flare

### 3.1 No sun in the sky shader (VERIFIED)

The stock sky PS `8a61dcf2` (`asm/SKY_8a61dcf2.asm`) only samples the octahedral sky image x `cb3[0].x` x
`radiometricScale`, plus a dither and the thermal path. It has no `cb7[56]` / `cb7[57]` term. Any disc in the sky is in
the sky image itself, which the editor already draws.

### 3.2 The sun sprite and flare (GfxWorld `sun`; drawing code from GT `rb_sky.cpp`, INFERRED for Replay)

`r_drawSun` (`LTROMKSMSS`, "Enable sun effects") is registered with default **1** at 0x1930E84. `r_sun_from_dvars`
defaults to 0 (GT), so the level's record is used. GT `RB_AddSunEffects` / `RB_DrawSunFlare`:

- **Sprite.** Drawn if `spriteMaterial` exists, `spriteSize > 0` and `dot(cameraForward, sunFxPosition) > 0`. It is an
  emissive billboard at infinity in direction `sunFxPosition`, `spriteSize x viewportHeight / 480` pixels square
  (`0.0020833334 = 1/480`), and depth tested.
- **Flare.** Drawn if `flareMaterial` exists and the dot is > `flareMinDot`.
  - `t = saturate((dot - flareMinDot) / (flareMaxDot - flareMinDot))`;
  - `alpha = t x flareMaxAlpha x intensity`, where intensity eases toward the occlusion-query visibility of the sun
    point over `flareFadeIn/OutTime` ms;
  - `size = (flareMinSize + t x flareMaxSize) x H / 480`;
  - the vertex colour byte is round(alpha x 255).

The sun records (VERIFIED, world cache GfxWorld JSON):

| map | spriteSize | flare min/max size, max alpha | sunFxPosition | visible |
|---|---|---|---|---|
| mp_m_speedball | 16 | 0 / 350, 1.0 | (0, 0, **-1**) | no: it points straight down, behind the floor (depth test and occlusion query) |
| mp_hideout | 0 | 0 / 0, 0 | (0, 0, 1) | no: size 0, alpha 0 |
| mp_backlot2, aniyah_tac, rust | 0 | 0 / 0, 0 | real sun direction | no |
| mp_village2, emporium | 0 | 0 / 0, 0 | (0, 0, 1) | no |
| mp_shipment, mp_m_fork | 16 | 12 / 0.5, 0.7 (shipment); 0 / 350, 1 (fork) | (0, 0, -1) | no |
| mp_m_king | 16 | 0 / 350, 1.0 | (-0.46, 0.45, 0.77) | **yes** |

(0, 0, -1) is what AngleVectors gives for pitch 90: an unset sunflare angle (INFERRED). A script can move it with
`setsunflareposition`. No stock script call was found in the repo's script indexes (UNVERIFIED for the zones' GSC).

Sprite and flare materials: `,sun` and `,sun_flare` (not exported).

### 3.3 Lens flares

The `r_flare*` dvars and `cs_flare_occlusion` belong to the **FX** flare system: flare elements of effects such as lamp
glints. They are occluded by depth and by the volumetric extinction (GT `R_Flare_CalculateOcclusion` takes the volume
images). They are not a sun lens flare. The editor does not play map FX, so there is nothing to add here.

### 3.4 What shows "the sun" on these maps, and the editor recipe

On speedball and hideout the sun is shown by four things:

1. the sky image (already drawn);
2. the volumetric HG lobe of section 2: a flat plateau of about 15 degrees on speedball, with shafts where shadowed;
3. the analytic fog's Mie lobe (`fogSun*`, already in `gameFogApply`);
4. the veil spreading all of them.

So **no sun sprite is needed for these two maps**. The recipe:

- Section 2.6 gives the halo.
- For maps where the record is live (mp_m_king), draw a screen-facing quad at `sunFxPosition`, of size
  `spriteSize x H / 480` px. It uses the `,sun` material's colour map (export needed), is additive, is depth tested
  against the far plane only, and has no fog.
- Skip the flare unless an occlusion test is available. The game's flare fades with a GPU occlusion query.

## 4. Vignette, film grain, chromatic aberration, sharpening

### 4.1 Vignette (VERIFIED: shader TM:65-87; constant fill in the exe 0x197FDCF-0x197FF3D)

The vision fields and defaults (`visionDefFields[184-193]`) are:

| field | default |
|---|---|
| vignetteIntensity | 0 |
| vignetteSquareAspectRatioWeight | 0 |
| vignetteSizeX / Y | 1 |
| vignetteFalloff | 1 |
| vignetteFalloffStart | 0 |
| vignetteBoxSizeX / Y | 0 |
| vignetteOffsetX / Y | 0 |
| vignetteImage | none |

With intensity <= 0 the parameters are zeroed and the shader skips the vignette (TM:65).

The constants are `cb7[214..216]` (viewInfo + 0x25F0..0x261C):

```
ax = max(tanHalfFovX / tanHalfFovY, 1), ay = max(tanHalfFovY / tanHalfFovX, 1), k = image ? 1 : 0.70710677   (0x197FDEB)
cb7[214] = (2 (w ax + (1-w) k) / sizeX, 2 (w ay + (1-w) k) / sizeY, intensity, 0)        w = squareAspectRatioWeight
cb7[215] = (offsetX, offsetY, boxX, boxY)
cb7[216] = (1/max(falloff, 0.001), -falloffStart/max(falloff, 0.001), image ? 1 : 0, 0)
```

```hlsl
float2 p = (px * invScreen - 0.5) * cb7[214].xy + cb7[215].xy;                         // TM:68-69
float  r = cb7[216].z > 0 ? vignetteImage.SampleLevel(lin, p * 0.5 + 0.5, 0).r          // TM:70-72
                          : length(p - clamp(p, -cb7[215].zw, cb7[215].zw));             // TM:74-78
float  t = saturate(r * cb7[216].x + cb7[216].y);
float  v = t + 0.6 * (t * t * (3 - 2 * t) - t);                                          // lerp(t, smoothstep, 0.6), TM:80-84
hdr *= 1 - v * cb7[214].z;                                                               // TM:85-86, linear HDR, after the veil
```

With k = 0.7071 and no box, r = 1/size at the screen corners. The results (16:9, `post_ref.py`):

| map | centre | middle of an edge | corner |
|---|---|---|---|
| speedball | 1 | 0.693 | **0.600** |
| hideout | 1 | 0.962 | 0.950 |

The darkening is in linear HDR before the tone curve.

**Editor recipe**: the four lines above in `ps_post` before `gamePost`, with `invScreen` and the aspect of the viewport.

### 4.2 Film grain (VERIFIED: FG:28-49, CP:76-110)

- **Where.** At the display composite: the `filmgrain` material, or flag bit 4 of `composite_postfx` (CP:34). It runs
  after tonemap, SMAA and lens effects, on the display-encoded colour (sRGB 2.4 in SDR), and before the UI
  (GT `RB_CompositeToDisplay`).
- **Strength** = `cb7[160].y`:
  - in Replay this is `viewInfo+0x2294`, which is the grain strength if the tone map is on, else 0 (0x197FDA5-0x197FDC2);
  - the grain strength = vision `tonemapGrainStrength` x `r_filmGrainAtten` (0x197BF00-0x197BF14);
  - `r_filmGrainAtten` (`LKOLMLQRRT`) defaults to **0.25** (registration 0x1876C87-0x1876CBC), and the in-game
    "Film Grain" option writes it (GT `gamerprofile.cpp`);
  - with HDR output only, the strength is also multiplied by `r_hdrFilmGrainStrengthScale` (0x197BF84).
  - The vision default is 0. Speedball gets 0.1 x 0.25 = 0.025; hideout gets 0.2 x 0.25 = 0.05.

```hlsl
float  T = cb2[4].w;                                   // code constant 2: a time value (INFERRED) -> the noise animates
float  a = frac(uv.y * T) * 1024 - 512, b = frac(uv.x * T) * 1024 - 512;                 // FG:28-30
float  pp = b * frac(b * 0.554550) + a, qq = a * frac(a * 0.308517) + b;                  // FG:31-33
float  n  = frac(pp * qq) * 2 - 1;                                                        // white noise in [-1, 1], FG:34-36
float3 c  = display.Sample(s, uv).rgb;                                                    // FG:37
float  m  = max(min(max(c.r, c.g), c.b), min(c.r, c.g));                                  // median of r, g, b, FG:38-41
float  k  = m + 0.1 * saturate(1 - m);
float  w  = max(1 - k * k, 0);                                                            // FG:42-45
c += c * n * w * strength;                                                                // FG:46-48
```

The grain is multiplicative (none on black) and fades toward white. With strength 0.025 the largest change is about
±1.8 % of the pixel value.

**Editor recipe**: after `gamePost` in `ps_post`, with T = a frame counter; a static seed is fine for screenshots.
Expose it behind a toggle, defaulting to the game's x0.25.

### 4.3 Chromatic aberration (off on both maps)

GT `rb_chroma.cpp:280` runs the lens pass only when one of these holds:

- `r_lensProfileOverrideMode != 0`, or
- `lensProfile.mode != 0` (weapon optics), or
- radial motion blur is on, or
- `chromaticAberration.strength > 0 && radius > 0`.

The vision gives `chromaRadius`, `chromaStrength` and `chromaAberration` (defaults 0, `visionDefFields[87-89]`).
Speedball leaves them unset and hideout sets them to 0. So nothing applies in hip-fire MP view. The pass (lens profile
distortion PS `1f51d574` plus convolution) was not decoded further. **Editor: nothing to do.**

### 4.4 Sharpening (none)

- The Replay exe has no sharpening pass. The only "Sharpen" strings are DOF's: `RBT_DOF_Sharpen` and the bokeh unsharp
  mask. There is no CAS.
- The filmic SMAA T2x resolve (PS `98525fea`) is TAA with neighbourhood variance clipping.
  - Its history resampling adds a Catmull-like term `(f^2 - f) x w x 0.2` (lines 186-187), where f is the fraction of
    the reprojected position.
  - This term is 0 for a static camera, and the editor runs no TAA.
- **Editor: nothing to add.**

### 4.5 Other vision post fields on the two maps (VERIFIED: off)

| Field | Value on these maps |
|---|---|
| `tonemapLocalStrength` | 0 (default; local tone mapping off) |
| `r_glare_*Amount` | 0 |
| `r_dustIntensity` | 0 |
| `cloudShadowOpacity` | 0 |
| `r_volumeLightScatter` | 0 |
| `droneCamera*`, `analog*` | effect-only visions |

## 5. Editor recipe (all items, in order)

1. **Data.** Read from the vision, with the exe defaults when unset (the table in `visdef.txt`): `veilStrength`, the
   `vignette*` fields, `tonemapGrainStrength`, and the 10 `volumetric*` fields. Read `sunFxPosition`, `spriteSize` and
   `flare*` from GfxWorld.
2. **Pass order**, matching the game:
   1. forward pass (lit, analytic fog);
   2. volumetric (2.6) on the HDR target;
   3. veil chain (1.5) from that HDR;
   4. `ps_post`: veil mix, then vignette (4.1), then `gamePost` (exposure + LUT), then grain (4.2).
3. **Simplest first step:** veil and vignette. They are exact, cheap and visible on both maps. Then grain. Then the
   volumetric, after the analytic fog is ported exactly from PO:34-345, so the haze is not counted twice (2.5).
4. **Checks (`post_ref.py`):**
   - speedball corner vignette 0.600, edge 0.693;
   - speedball T_vol = 0.647 beyond 2180 units;
   - hideout T_vol = 0.113 for a level ray at z = -64;
   - the veil PSF has 48 % of its energy within 128 px at 1080p.

## 6. Not established

- **Volumetric constants.** The CB0 slot meanings of the volumetric constants (albedo, absorption, brightness, height
  base) are INFERRED from usage plus GT. Only `airDensity x 0.01` and the height exponent were read in the Replay fill.
- **Ambient volume details.** Its direction (`cb7[94]`) and the per-slice ambient term without dz are VERIFIED
  arithmetic, but their meaning or intent is INFERRED.
- **Not decoded:** the static sun-shadow tree (T8) and the cloud-shadow inputs of the scatter, and the spot/omni
  volumetric variants.
- **The analytic fog** inside `cs_post_opaque_processes` (PO:34-345) was read for its structure and outputs only. The
  editor's fog is still the calibrated model of `game_look.h`.
- **Magnitudes.** No in-game comparison was made. The volumetric and veil numbers are computed from the formulas.
- **Speedball's sky image** was not inspected for a painted sun.
- **Sun sprite/flare in Replay.** The drawing code is GT (INFERRED for Replay), and a runtime `setsunflareposition` from
  the map scripts is UNVERIFIED.
- **Veil details.** The veil kernel's resolution scale `CB0[1].zw` and the CB0[3] fill of `cs_tonemap` are from GT, not
  read in Replay. The PSF's 1.10 sum depends on the sampler address mode (not read).
- **Hideout's white-balance matrix** (method 4, D illuminant 5500 K, tint 2.2; GT `R_WhiteBalance_SetMatrix`
  `r_whitebalance.cpp:237`) is not decoded. It affects cs_tonemap's CB0[0..2] on hideout.

## 7. Reproduce

Scripts are in `out/agent/post109/py` (Python 3, numpy, scipy, pefile, capstone). Outputs are the `*.txt` files in
`out/agent/post109`.

| Script | What it does |
|---|---|
| `pe.py` | research 106/107's static exe helpers (sections, xrefs, disassembly) |
| `disx.py <lo> <hi>` | disassembly with the RIP-relative float or string each instruction reads |
| `dvarreg.py "<text or token>" [n]` | finds a dvar string's references and disassembles the registration |
| `visdef.py [filter]` | dumps `visionDefFields` (name, offset, type, min, max, default) -> `visdef.txt` |
| `vision_dump.py <vision.0> [filter]` | names a vision's tokens with Dvars.txt -> `vision_mp_*.txt` |
| `veil_ref.py [W H]` | CPU veil chain on an impulse: PSF energy and Gaussian fit -> `veil_ref.txt` |
| `hg_lut.py <hg_lut.0.dds>` | BC4 decode of the phase LUT compared with HG -> `hg_lut.txt` |
| `post_ref.py` | vignette factors, grain strength, froxel slices, T_vol and in-scatter estimates -> `post_ref.txt` |

Kept data:

| Folder or file | Content |
|---|---|
| `asm/` | all disassemblies cited |
| `cs/`, `ps/` | the shader blobs |
| `img/` | `hg_lut.0.dds` |
| `mt/` | code_post_gfx materials and techsets, 4.9 MB |
| `cpg_assets.jsonl` | the code_post_gfx asset list |

ACTS commands (short output roots):

```
acts --noUpdater fastfile -r mw19replay -g <replay exe> --oodle <oo2core_7_win64.dll> -a material,techset -l -o mt <zone>\code_post_gfx.ff
acts ... -a pixelshader -n 6d0c17999c3223c8214374be9a0a44d0,0ddfcf1ff5ebb511c9eccff486499628,98525fea56f4dcf960df289da9b9cfc1,d4f25d0ca29c9515d7b07e027ea717e7,1f51d574c46ac195333a8570051598ff -l -o ps <zone>\code_post_gfx.ff
acts ... --xpak <zone>\code_post_gfx.xpak -a image -n hg_lut -l -o img <zone>\code_post_gfx.ff
acts ... -d --noAssetDump -l -o lst <zone>\code_post_gfx.ff      (asset list only; the 130 MB .dec files were deleted)
```
