# 111. The analytic fog (Replay 1.20)

**Why.** The owner reports that the fog on mp_rust is too bright and too thick in Game Lighting. The editor's fog
(`GameFog` in `src/core/game_look.*`, `gameFogApply` in `src/viewport/render.cpp`) was an approximation calibrated
on one map (mp_m_speedball). Research 109 asked for the exact analytic fog of `cs_post_opaque_processes` (PO:34-345)
to be ported first. This note decodes it. The CPU reference is `src/core/game_fog.{h,cpp}` and its tests are in
`tests/game_fog_tests.cpp`.

**Labels.** VERIFIED = read in the shader disassembly or the Replay exe (cited). INFERRED = names or meanings taken
from the IW8 game-test decompile ("GT", `rb_fog.cpp`, `r_fog_spline.cpp`, `cg_visionsets_common.cpp`) or from usage.
Nothing was run in the game.

## How it was obtained

- **Shaders.** The research 109 disassemblies in `out/agent/post109/asm/` were read:
  - `cs_post_opaque_processes.288.asm` (tag PO);
  - `cs_blend_fog_splines.191.asm` (tag BF).
  - Line numbers below are lines of those files. PO:34-345 is the first of the 2x2 pixels each thread writes.
- **Replay exe.** Read statically with research 109's `py/pe.py` and `py/disx.py` (pefile + capstone; IDA was not
  used). Three functions:
  - **vision to refdef fog**, 0x1723430-0x1723b0e. It was found through the `2.84` constant at .rdata 0x251aa80. GT
    name: `CG_VisionSetApplyToRefdef` (fog part).
  - **R_SetFrameFog**, 0x19a9940-0x19a9e3f. It was found through the circle constant 0.72134751 at .rdata
    0x259a9a0. It fills cb7[131..151].
  - **R_FogSpline_GetBlendParameters**, 0xef1360-0xef1651, and **RB_FogSpline_GetImage**, 0xef0f90.
- **GT.** The GT decompile has the same three functions with names. Replay's code matches GT instruction for instruction,
  except that Replay has no "live fog spline" debug branch.
- **The cb7 base.** cb7 starts at `viewInfo + 0x1890`. So cb7[131] is at +0x20c0, and cb7[42] is at +0x1b30, which
  matches research 107.
- **Visions.** Values come from the world cache's `<map>.vision.0`, named with `E:\mw124\Dvars.txt`. Defaults come
  from `visionDefFields` (research 109 `visdef.txt`).
- **Numbers.** Computed by `out/agent/fog111/fog_ref.py`, a double-precision Python copy of the formulas, with the
  editor's current `gameFogApply` next to it. The C++ tests check the C++ against these numbers.

## 1. Where it runs (VERIFIED)

- **Pass.** `cs_post_opaque_processes` fogs every opaque pixel and the sky after the opaque lit pass. It composites
  the volumetric fog in the same pass (research 109 section 2.4):

  ```hlsl
  out = scene * (T_fog * T_vol) + (L_fog * T_vol + S_vol) * cb7[42].x      // PO:411-438
  ```

  - `scene` is pre-exposed (multiplied by `cb7[42].x`, the radiometric scale). `L_fog` is in plain radiance units.
    So the analytic fog's in-scatter is in the same units as the lit shader's output before `cb7[42].x`.
  - The editor does not pre-expose, so `out = scene * T + L` is exact for the editor.
- **Sky test** (PO:43-47).
  - The pixel is sky when `|floatZ * 6| >= min(cb7[171].z * 1e8, 392999)`. `cb7[171].z` is INFERRED to come from
    the far plane.
  - For a sky pixel, depth is replaced by `cb7[138].x` (for the volumetric slice only). The analytic fog then uses
    `fogSkyDistance` instead (section 4).
- **Transparents** are fogged in their own shaders from the same cb7[131..138] (INFERRED; not read).

## 2. Vision to refdef (VERIFIED, exe 0x1723430)

The vision struct offsets come from `visionDefFields`. `lin()` is the exe's sRGB-to-linear at 0x1724740:
`c <= 0.03928 ? c / 12.92 : ((c + 0.055) / 1.055)^2.4`. Note the threshold is 0.03928, not 0.04045.

```c
v4      = 2^(fogInscatteringEv - 2.84) * 0.001                       // 0x1723448-0x17234ab (powf at 0x142214dc0)
// fog spline blend set (0x17234f0-0x17235df): up to 9 (weight byte, asset) entries.
//   start = sum(w/255 * start_k), maxDist = sum(w/255 * maxDist_k), halfSum = sum(w/255 * half_k), where an entry
//   that is not a spline asset (node type != 3) takes the vision's own fogStartDist / fogHalfPlaneDistance / fogMaxDistance
density = halfSum != 0 ? max(ln2 / halfSum, 0) : 0                     // 0x17235e5-0x1723618
skyDistance = fogSkyDistance                                            // 0x17234c0 (not blended)
ray_c   = lin(fogRayleighAlbedo_c) * (1 - fogMieBlendStrength) * density // 0x1723626-0x172368e
mie_c   = lin(fogMieAlbedo_c) * fogMieBlendStrength * density            // 0x1723694-0x17236ef
ext_c   = max((ray_c + mie_c) * (1 - fogExtinctionDesaturation) + fogExtinctionDesaturation * density, 1.19209e-7)
ray_c  *= v4 / ext_c;  mie_c *= v4 / ext_c                               // 0x17236f5-0x17237a1
maxOpticalDepth_c = 1.442695 * maxDist * ext_c                           // 0x17237a7-0x17237d3
mieAnisotropy = fogMieAnisotropy                                         // 0x17237db
sunDir  = normalize(fogSunDir)  (a zero vector stays zero)               // 0x17237e7-0x1723869
skyFalloffScale = -(pi/2) / range;  skyFalloffBias = (pi/2 - minRad) / range,
    minRad = fogSkyMinAngle * deg, range = max(fogSkyMaxAngle * deg - minRad, 1.19e-7)   // 0x1723871-0x17238ae
heightBase = fogHeightFogBaseHeight
heightDensity = fogHeightFogHalfPlaneDistance > 0 ? ln2 / fogHeightFogHalfPlaneDistance : 0   // 0x17238b6-0x17238de
heightMaxOffset = fogHeightFogMaximumOffset;  heightAdditional = fogHeightFogAdditionalFog
atmospheric = fogAtmosphericScattering > 0                               // 0x17238fe
```

- **Plain fog.** When `atmospheric` is false (0x1723917-0x1723ac3), the same slots are overwritten:
  - `ray = lin(fogColor) * fogColorIntensity` and `mie = lin(fogSunColor) * fogSunColorIntensity`;
  - `maxDist = fogMaxOpacity` and `skyDistance = fogSkyIntensity * fogMaxOpacity`;
  - `maxOpticalDepth = (1.4427 * max(ln2 / fogDensity - 1e-5, 0), 1.4427 * max(ln2 / fogSunDensity - 1e-5, 0), 0)`;
  - `heightDensity = 0` and `mieAnisotropy = 1 - fogMaxOpacity`;
  - `sunFalloffScale = 1 / (cos(begin) - cos(end))` (100 if begin <= end) and `sunFalloffBias = -scale * cos(end)`.
- **Fields the atmospheric fog never reads** (VERIFIED in this function; no other reader found in GT):
  `fogColor`, `fogColorIntensity`, `fogMaxOpacity`, `fogSkyIntensity`, `fogDensity`, `fogSun*`. All three maps
  use the atmospheric fog.
- **Empty `fogSpline`.** A vision whose `fogSpline` is "" still holds one entry of weight 255 that is not a spline
  asset (GT `CG_VisionSet_LoadAssetIndex` returns index 0 for an empty name; INFERRED for Replay). Otherwise
  start, max distance and density would all be 0 and these maps would have no fog.

## 3. Constants cb7[131..138] (VERIFIED, R_SetFrameFog 0x19a9940)

| cb7 | .x | .y | .z | .w | exe |
|---|---|---|---|---|---|
| 131 | skyDistance - startDist | skyFalloffScale | skyFalloffBias | skyDistance | 0x19a9b45-0x19a9b77 |
| 132 | sunDir.x | sunDir.y | sunDir.z | -startDist | 0x19a9b7f-0x19a9bbc |
| 133 | maxOpticalDepth.r | .g | .b | max(heightDensity, 1.19e-7) | 0x19a9bc2-0x19a9bf1 |
| 134 | mie.r | mie.g | mie.b | mieAnisotropy | 0x19a9bfc-0x19a9c26 |
| 135 | rayleigh.r | rayleigh.g | rayleigh.b | exp(-max(camZ - heightBase, -heightMaxOffset) * heightDensity) | 0x19a9b1f-0x19a9b40, 0x19a9c2c-0x19a9c54 |
| 136 | 0 | 0 | 1 | heightAdditional | 0x19a9c5a-0x19a9c6e |
| 137 | sunFalloffScale | sunFalloffBias | atmospheric ? 0 : -1 | 1 / max(maxDistance, 0.001) | 0x19a9c78-0x19a9cc4 |
| 138 | sqrt(2) * min(worldHalfSize.x, .y) | 0 | 0 | 0 | 0x19a997a-0x19a99a0 |
| 139-151 | the Warzone circle fog (section 6) | | | | 0x19a9cca onwards |

- **Off branch.** When the `r_fog` dvar is off (pointer at .data 0x10559c68, INFERRED name), all fog constants are
  zeroed, cb7[135].w becomes 1, and T3 gets the zero image (0x19a99b9-0x19a9ada).
- **cb7[138].x** comes from the world bounds, or the voxel-tree zone bounds. The analytic fog does not use it.
- **camZ** is `viewInfo + 0x108`, INFERRED to be the camera origin z.

## 4. The shader (VERIFIED, PO:39-214)

Notation: `c131` = cb7[131]. `exp2` is DXBC `exp`. `dir` is the unit view direction. `len` is the eye-to-pixel
distance (PO:48-60: view z times the frustum vector).

```hlsl
// geometry of the ray (PO:56-66)
float  dz   = dir.z * len;                        // the world-z extent from the eye (whole ray, not from start)
float  path = len + c132.w;                        // len - startDist (may be negative)
float  skyF = 0;
if (sky) { dz = dir.z * c131.x;  path = c131.x;    // fogSkyDistance - startDist
           skyF = saturate(dir.z * c131.y + c131.z); }   // = saturate((zenithApprox - minAngle) / range),
                                                         //   zenithApprox = pi/2 * (1 - dir.z)
float  cosS = dot(dir, c132.xyz);                  // to fogSunDir
// PO:67-161: circle fog, only when cb7[141].z > 0 (off on MP; section 6)

if (c137.z < -0.5) { /* plain fog, section 5 */ }
else {                                             // atmospheric (PO:181-214)
    float x  = saturate(path * c137.w);           // normalised distance, clamped at fogMaxDistance
    x = max(x - T3.SampleLevel(lin, x * (2 - x) * 0.992188 + 0.003906, 0).x, 0);   // spline LUT (section 7)
    float k  = dz * c133.w;                       // height density x vertical extent
    float H  = abs(k) > 1e-6 ? (1 - exp2(-k * 1.442695)) / k * c135.w : c135.w;     // mean height-fog factor
    H += c136.w;                                  // fogHeightFogAdditionalFog: uniform, no height falloff
    float3 T = exp2(-x * H * c133.xyz);           // = exp(-ext * min(len - start, maxDist) * H)
    if (sky) T = T * skyF + (1 - skyF);
    float  pR = (1 + cosS * cosS) * 0.059683;                                  // Rayleigh: 3/(16 pi) (1 + cos^2)
    float  pM = (1 - g * g) * 0.079577 / ((1 - g * cosS) * (1 - g * cosS));     // g = c134.w; Schlick-type lobe
    float3 L  = (c134.xyz * pM + c135.xyz * pR) * (1 - T);
}
// composite: scene * T (* T_vol) + L (* T_vol, + S_vol)                         (PO:411-438)
```

**In words:**

- **Transmittance.** It is `exp(-ext * d_eff * H)`:
  - `d_eff` is the path past `fogStartDist`, capped at `fogMaxDistance`;
  - `H` is the eye's height factor, averaged over the ray's vertical extent, plus a uniform `additionalFog`.
- **Extinction** is per channel. `fogExtinctionDesaturation` blends it toward grey. It does **not** desaturate the
  scene.
- **In-scatter** is the single-scattering solution under constant light:
  `L = v4 * (rayleigh * pR + mie * pM) / ext * (1 - T)`.
  - It has no sun colour and no fogColor.
  - The phase functions are measured from the vision's `fogSunDir`, not from the map's sun. Speedball's
    `fogSunDir` points down (z = -0.81).
- **Mie lobe.** It is `(1 - g^2) / (4 pi (1 - g cos)^2)`: a Schlick-type lobe, **not** Henyey-Greenstein (power 2,
  not 1.5).

## 5. Plain fog branch (VERIFIED, PO:166-180; not used by the three maps)

```hlsl
float  sunF = saturate(cosS * c137.x + c137.y);
float2 t    = max(exp2(-path * c133.xy), c134.w);                 // fog and sun-fog transmittance, floor 1 - maxOpacity
float  T    = saturate(lerp(t.x, t.y, sunF));
if (sky) T  = 1 - c131.w * skyF;                                   // 1 - fogSkyIntensity * maxOpacity * band
float3 L    = lerp(c135.xyz, c134.xyz, sunF) * (1 - T);            // fogColor x intensity .. fogSunColor x intensity
```

This is the only place where `fogMaxOpacity`, `fogSkyIntensity` and `fogColor` act.

## 6. Circle fog, cb7[139..151] (VERIFIED structure; INFERRED meaning; off on MP)

- **Gate.** PO:67-161 runs only when `cb7[141].z > 0`. R_SetFrameFog writes cb7[141].z (+0x2168, 0x19a9e28) as
  `0.72134751 / max(distanceHalfPlane * distanceHeightFalloff, eps) * distanceScale * distanceDensity`.
- **Inputs.** They come from `viewInfo + 0xbc0..0xc20`, GT `viewInfo->brCircle`: the Warzone (battle royale)
  circle.
- **What it computes.**
  - It intersects the ray with a vertical cylinder (centre, radius, height).
  - It optionally reads a height/noise map T6 (cb7[146..150], `r_fogHeightMap*` dvars).
  - It adds its own exponential fog with inner/outer colours.
  - It can shorten the analytic path (`path *= 1 - circleAmount * cb7[145].y`).
  - It merges with the analytic result in log space (PO:216-345).
- **On MP.** The BR circle is not set on MP maps, so the density is 0 and the branch is skipped. INFERRED: no MP
  script sets it. Only Warzone uses it, and it was not decoded further. The CPU reference leaves it out.

## 7. The fog spline LUT T3 (VERIFIED: BF shader, exe 0xef1360 and 0xef0f90)

- **What it encodes.** 128 texels over the normalised distance `x = saturate((len - start) / maxDist)`.
  - Texel `i` sits at `x(2 - x) = i / 127`, so the near range gets more texels.
  - The fog uses `max(x - LUT(x), 0)` as its distance. A spline asset reshapes optical depth against distance:
    less fog up close, a later onset, and so on.
- **Which image is bound** (0xef0f90):
  - non-atmospheric: the zero image (.data 0x10c778a8);
  - two or more blend entries: the blended image;
  - otherwise: entry 0's image, or the zero image when it has none.
- **The three maps.** All of them leave `fogSpline` empty, so **T3 is all zero and the LUT term vanishes**.
- **Blend parameters** (0xef1360): per entry k (at most 8):

  ```
  p_k = (w_k/255, maxDist_k * ln2/half_k * sum(w half)/ln2, 1/maxDist_k (0 if <= 0), -start_k/maxDist_k)
  CB0[0] = (count, 1/maxDist, maxDist, start)
  ```

  `maxDist` and `start` are the refdef's weighted sums.
- **Blend shader** (BF:24-157), one thread per texel:

  ```hlsl
  float u = 1 - sqrt(1 - i * 0.007874);        // inverse of the x(2-x) mapping
  float rel = u * maxDist, dist = rel + start;
  float acc = 0;
  for (k < count) { float z = dist * p_k.z + p_k.w;
                    float val = z - Spline_k.SampleLevel(lin, saturate(z) * (2 - saturate(z)) * 0.992188 + 0.003906).x;
                    acc += p_k.x * (rel - p_k.y * val); }
  out[i] = i == 0 ? 0 : acc / maxDist;
  ```

  With one entry this gives back that entry's own curve. The pass runs only when 2 or more entries blend (GT
  `RB_FogSpline_ProcessBlend`, INFERRED for Replay).
- **CPU.** `gameFogSplineBlend` and `gameFogLutSample` implement both steps. The editor has no spline asset curves:
  the `fogSpline` asset format was not decoded.

## 8. Sky handling (VERIFIED)

- **Path.** The sky's fog path is `fogSkyDistance - fogStartDist`, both along the ray and for the vertical extent:
  `dz = dir.z * (skyDist - start)`.
- **Band.** The band factor `skyF` is linear in `dir.z`. It is 0 above zenith angle `fogSkyMinAngle` and 1 below
  `fogSkyMaxAngle`, using `zenith ~ 90 deg * (1 - dir.z)`. The sky's transmittance is `lerp(1, T, skyF)`.
- **Upward rays.** For them the height factor becomes tiny. `fogHeightFogAdditionalFog` then dominates.
  - mp_rust: 0.032 times its huge sky path makes the low sky fully fogged.
  - mp_hideout: 0.069.
- **fogSkyIntensity** has no effect in the atmospheric fog.

## 9. Numbers: the game against the editor's current gameFogApply

Eye at z = 64 unless stated. T is the transmittance (the editor's is grey: 1 - amount). L is the added radiance in
scene units (a black surface's fogged value). Values are r g b. The editor's Henyey-Greenstein term is evaluated for
the same direction, as if the map's sun were the fog sun.

**mp_rust** (v4 = 292.9; extinction = 0.89 x ln2/4000; cb7[135].w = 0.770):

| ray | game T | game L | editor T | editor L | editor desat |
|---|---|---|---|---|---|
| level, 1000, cos 0 | 0.941 | 0.31 0.26 0.21 | 0.937 | 0.68 0.64 0.59 | 0.05 |
| level, 2000, cos 0 | 0.83 | 0.88 0.76 0.61 | 0.82 | 1.95 1.82 1.67 | 0.15 |
| level, 4000, cos 0 | 0.65 | 1.83 1.58 1.27 | 0.63 | 4.02 3.77 3.46 | 0.32 |
| level, 8000, cos 0 | 0.40 | 3.16 2.73 2.20 | 0.37 | 6.83 6.39 5.87 | 0.54 |
| level, 16000, cos 0 | 0.15 | 4.45 3.85 3.12 | 0.13 | 9.43 8.84 8.11 | 0.74 |
| level, 4000, toward fogSunDir (cos 0.94) | 0.65 | 4.52 3.93 3.18 | 0.63 | 9.20 8.73 8.05 | 0.32 |
| level, 4000, away (cos -0.94) | 0.65 | 2.54 2.16 1.74 | 0.63 | 3.60 3.37 3.09 | 0.32 |
| eye z 1000, level, 4000 | 0.974 | 0.14 0.12 0.09 | 0.990 | 0.11 0.10 0.10 | |
| sky, horizon | 0.000 | 5.22 4.53 3.68 | 0.000 | 11.9 11.1 10.2 | |
| sky, 10 deg up | 0.11 | 4.66 4.04 3.28 | 0.83 | 2.04 1.91 1.75 | |
| sky, 20 deg up | 0.51 | 2.55 2.21 1.79 | 0.93 | 0.84 0.79 0.72 | |
| sky, 45 deg up | 1.00 | 0 | 1.00 | 0.06 | |

**mp_m_speedball** (v4 = 90.2; extinction = 0.94-0.96 x ln2/2550):

| ray | game T | game L | editor T | editor L |
|---|---|---|---|---|
| level, 2000, cos 0 | 0.78 | 1.00 1.05 1.05 | 0.76 | 1.23 1.27 1.30 |
| level, 4000, cos 0 | 0.47 | 2.38 2.50 2.50 | 0.45 | 2.89 2.99 3.05 |
| level, 8000, cos 0 | 0.17 | 3.71 3.87 3.91 | 0.15 | 4.42 4.57 4.67 |
| level, 4000, cos 0.59 | 0.47 | 4.16 4.25 3.94 | 0.45 | 3.67 3.75 3.67 |
| sky, horizon | 0.081 | 4.11 4.26 4.32 | 0.081 | 2.40 2.48 2.53 |
| sky, 20 deg up | 0.35 | 2.90 3.03 3.06 | 0.22 | 2.04 2.12 2.16 |
| sky, 80 deg up | 0.77 | 1.25 1.35 1.45 | 0.60 | 0.98 1.02 1.05 |

**mp_hideout** (v4 = 207.1; extinction = ln2/12000, grey; height base -250, half 3500, max offset 63, additional
0.069):

| ray | game T | game L | editor T | editor L |
|---|---|---|---|---|
| level, 4000, cos 0 | 0.885 | 0.75 1.02 1.33 | 0.892 | 0.89 1.09 1.28 |
| level, 8000, cos 0 | 0.70 | 1.96 2.66 3.46 | 0.72 | 2.32 2.86 3.35 |
| level, 16000, cos 0 | 0.44 | 3.67 4.97 6.48 | 0.64 (capped by fogMaxOpacity 0.36) | 2.97 3.65 4.28 |
| sky, horizon | 0.49 | 3.34 4.53 5.90 | 0.73 | 22.0 27.1 31.7 (x fogSkyIntensity 10) |
| sky, 10 deg up | 0.83 | 1.10 1.49 1.94 | 0.82 | 14.9 18.3 21.5 |
| sky, 20 deg up | 1.00 | 0.01 | 0.91 | 7.8 9.6 11.2 |

## 10. Why mp_rust looks too bright and too thick in the editor

The numbers show it. The transmittance is close (the editor is 2-3 % thicker), but:

1. **In-scatter is 2.2 to 2.7 times too bright** across the fog sun direction at every distance: 4.0 against 1.8 at
   4000 units. It is 2 times too bright toward that direction and 1.4 times away from it.
   - The editor multiplies the **sRGB** albedos (0.52 / 0.48 / 0.43) by `v4 / 4 pi`.
   - The game linearises them (0.23 / 0.20 / 0.16). It multiplies by real phase functions and divides by the
     extinction, which with desaturation 0.85 is mostly grey density, so the albedo does not cancel.
   - Speedball's albedos are near 1, where sRGB and linear agree. That is why the speedball calibration came out
     close (the editor is about 20 % over) and rust does not.
   - The editor's tint is also too grey. The game's rust haze is warmer (r/b 1.43 against the editor's 1.16).
2. **The editor desaturates the scene** by `fogExtinctionDesaturation x amount`: 32 % at 4000 units and 74 % at
   16000 on rust. The game never desaturates. The field only makes the extinction grey. Washed-out grey ground under
   a bright veil reads as thick fog.
3. **Smaller differences:**
   - The editor ignores `fogHeightFogAdditionalFog` (rust 0.032). So its sky fog fades out a few degrees above the
     horizon (T 0.83 at 10 degrees). The game keeps the low sky fogged (T 0.11 at 10 degrees, 0.51 at 20) with a
     darker haze.
   - The editor's phase uses the map's sun, not `fogSunDir`.
   - Its Mie lobe is HG without 1/(4 pi).

**Other maps.**

- **mp_hideout.**
  - The editor caps the fog at `fogMaxOpacity` 0.36, which the atmospheric fog ignores. Far ground is too clear.
  - It multiplies the sky haze by `fogSkyIntensity` 10, which the atmospheric fog also ignores. The low sky is 5 to
    14 times too bright.
- **mp_m_speedball.** The editor is about 20 % too bright on the ground. On the sky its haze is about 40 % too dim,
  but it hides more of the sky above the horizon (T 0.22 against 0.35 at 20 degrees).

## 11. Editor recipe (porting `game_fog` to HLSL)

1. **On map load.** `gameFogVisionFromText(vision)`, then `gameFogRefdef`.
2. **Per frame.** `gameFogConstants(refdef, eyeZ)` fills a cbuffer of 7 float4s, cb7[131..137].
   - cb7[135].w depends on the eye height, so refill it when the camera moves.
3. **In the pixel/post shader.** Port `gameFogEvaluate` line by line. Pass the unit direction, the eye-to-pixel
   distance and the sky flag. Then `c = c * T + L`.
   - Drop the editor's desaturation, `fogMaxOpacity`, `fogSkyIntensity` and `fogColor` for atmospheric visions.
   - Use the vision's `fogSunDir` for the phase.
   - Use the plain branch when `fogAtmosphericScattering` is 0.
4. **T3.** Bind nothing (zero) unless a vision names a `fogSpline` asset. No stock MP vision checked does.
5. **Volumetric fog.** Apply research 109's volumetric fog after this: `c = c * T_vol + S_vol`. The old fog was
   calibrated on a screenshot that also contained the volume.

## 12. Not established

- **Empty fogSpline.** That it yields one weight-255 entry is GT behaviour, INFERRED for Replay. The Replay loop
  itself is VERIFIED to need a non-zero weight.
- **Names, not formulas.** These are INFERRED: the `r_fog` dvar at 0x10559c68, `camZ` (viewInfo + 0x108), and
  `cb7[171].z` in the sky test.
- **The circle fog** (cb7[139..151]) was not decoded beyond its gate and inputs. Its absence on MP is INFERRED.
- **The fogSpline asset format** and the curves of any shipped spline asset were not read.
- **Transparents.** Whether transparent and effect shaders use the same formula was not checked.
- **No in-game comparison.** The table is the formula. It does not include the volumetric fog (research 109), which
  adds its own haze within 2180 units.
- **Eye height.** The tables use eye z = 64. The real eye height above each map's ground was not checked.

## 13. Reproduce

- `py -3 -I out/agent/fog111/fog_ref.py` prints sections 3 and 9.
- `out/agent/fog111/refdef.txt` and `setfog.txt` are the annotated disassemblies of 0x1723430 and 0x19a9940.
  Make them with `py -3 -I out/agent/post109/py/disx.py <lo> <hi>`.
- Tests: `tests/game_fog_tests.cpp`, 7 tests and 39 checks.
