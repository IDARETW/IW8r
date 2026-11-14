# 127. Volumetric boxes (local media) and fog on see-through surfaces (Replay 1.20)

**Why.** The owner asked for every volumetric capability. Two gaps were open: what the fields of a world volumetric
box mean (the editor's `iw8r_volume`), and how see-through surfaces fog themselves. This note answers both with
checkable facts, and gives the HLSL to paste into the converter's surface shader.

**Labels.** VERIFIED = read in a shader disassembly, the Replay exe, a stock export or source (cited). INFERRED =
from the game-test decompile ("GT", `E:\mw124\mw164proxy\$dst\game-test-src-cleaned\code_source\src\gfx_d3d`) or
from reasoning. Nothing was run in the game. The IDA bridge was down; the exe was read with research 109's
`out/agent/post109/py/disx.py` (pefile + capstone).

**Sources.**

- Scatter shaders: `out/agent/post109/asm/cs_volumetric_forwardplus_scatter_local.476/478/480/482.asm` (tag L476 etc.).
  Variant index = local media (bit 0) | lamps (bit 1) | sun (bit 2), so 476 = local, 478 = local + lamps, 480 = local
  + sun, 482 = all (GT `R_VOL_EvaluateScattering`, `r_volumetric.cpp:2197-2224`).
- Replay `R_VOL_AddVolumetric` at RVA 0x18B6EC0 (the converter's comment, `replay_render.cpp:1434`), disassembled.
- Stock records: ACTS `gfx_map` exports of mp_aniyah_tac, mp_village2, mp_hideout, mp_emporium, mp_backlot2,
  mp_m_speedball, mp_shipment and mp_rust (`tools/acts_export.py <zone> gfx_map <scratch>`), and the aniyah visions
  (`rawfile`). Scratch output was deleted afterwards.
- Translucent shaders: the shipment map shader zone's pixel shaders (`out/agent/toon_ship/ps/.../pixelshader`),
  copied to scratch and disassembled with `out/agent/shader_research/py/dxbc_dis.py`. Line numbers below are lines of
  those disassemblies (tag P07 = `077107f8...7.asm`, tag P4D = `4de524e0...63.asm`). Techniques from `out/agent/toon_ship/ts`.

## Part A. Volumetric boxes

### A.1 The record and what the exe makes of it (VERIFIED, exe 0x18B6EC0-0x18B71xx)

The asset record is `GfxVolumetric`, 264 bytes (the converter's `EmitVolumetrics`, `replay_render.cpp:3199-3236`).
Each frame the exe turns every visible box into a 60-byte box (`GfxOrientedBoundingBox`) and a 144-byte GPU record:

| GPU offset | Value | Source |
|---|---|---|
| 0x00 | density, copied as is | record +0x54 |
| 0x04 | `-0.1 * clamp(heightFade, 0, 1)` | +0x5C (maxss 0, minss 1, mulss -0.1) |
| 0x10 | anisotropy if flags bit 2, else NaN | +0x60 |
| 0x14-0x1C | color if flags bit 4, else NaN | +0x48 |
| 0x20 | `1 / falloff` | +0x58 |
| 0x24-0x2C | `-(halfSize - falloff) / falloff` per axis | +0x3C, +0x58 |
| 0x30-0x3C | mask image slot per mask, -1 if none | mask type must be 2 and the image set |
| 0x40-0x4C | mask axis per mask (raw) | mask +0x04 |
| 0x50-0x8F | per mask: `1/scale.x, 1/scale.y, (offset + t * scroll) / scale + 0.5` | mask +0x10..+0x27 |

- At most 256 boxes per frame (count >= 0x100 returns at once).
- GT also skips a box with flags bit 1 (`r_volumetric.cpp:1592`). Replay's prologue has no such test (0x18B6EDC-0x18B6EF2);
  the converter forbids bit 1 anyway.
- `t` is GT's frame time modulo 43200 (`r_volumetric.cpp:1742`); seconds INFERRED.

### A.2 How the scatter pass uses it (VERIFIED, L476:236-347, the same loop in L478, L480, L482)

Per froxel at world point P, with the box centre C, axes X Y Z and half size h:

```hlsl
float3 l = float3(dot(P - C, X), dot(P - C, Y), dot(P - C, Z));            // L476:265-270
float3 e = saturate(abs(l) * rcpFalloff + scaledFalloffHalfSize);          // = saturate((|l| - (h - falloff)) / falloff)
float  edge = max(1 - length(e), 0);                                       // L476:271-275: 1 inside, 0 at the faces
float  m = 1;                                                              // each used mask multiplies (L476:277-333)
//   uv = axis 0: (l.y, l.z); 1: (l.x, l.z); 2: (l.x, l.y)   (axis & 3; the "negative" bit 4 is ignored, L476:282)
//   m *= maskImage.SampleLevel(s14, float2(uv.x / scale.x, -uv.y / scale.y) + offsetTerm, 0).r
float  up = clamp(l.z + h.z, 0, 2 * h.z);                                  // height above the box's bottom face
sigma += density * edge * m * exp(heightFadeExponent * up);                // L476:334-342
```

`sigma` starts as the vision's air, `volumetricAirDensity * 0.01 * exp(max(P.z - base, 0) * hExp)` (L476:222-227,
research 109). After the loop the froxel is shaded only with the vision's settings:

```hlsl
scat = albedoVision * sigma * (sunBrightness * phase(gVision) * pi * sunRadiance * shadow * dz   // L480:660-679
                               + ambientBrightness * ambientVolume);                              // L480:686-690
tau  = (sigma + absorptionVision) * dz;                                                          // L476:437-438
```

So:

- **Density** is an extinction (and scattering) coefficient **per world unit**, added to the air with **no x 0.01**.
  A box of 0.0002 equals a vision air of 0.02. VERIFIED.
- **Falloff** is a distance in world units: the density ramps from full at `falloff` units inside each face to zero
  at the face. The corners are rounded (the length of the three ramps). A falloff larger than a half size thins the
  whole box. VERIFIED.
- **Height fade** is clamped to 0..1. Density falls as `exp(-0.1 * heightFade * height above the box's bottom face)`,
  measured along the box's own Z axis. It halves every `6.93 / heightFade` units (heightFade 1: every 6.9 units;
  0.01: every 693). 0 = uniform. VERIFIED.
- **Colour and anisotropy have no effect.** No local variant reads GPU offsets 0x08-0x1F (all four read offsets 0, 32,
  48, 64, 80-128 only: L476:257-323, L480:270-336, L482:275-341). The scattered colour is the vision's
  `volumetricAlbedo` (CB0[15].yzw, L476:364-365, L480:377-378) and the phase uses the vision's `volumetricSunAnisotropy`
  (CB0[16].z, L480:664-670). VERIFIED.
- **Absorption** is the vision's, added in every froxel; a box has no absorption of its own. VERIFIED.
- **It is lit like the air.** Sun (vision sun brightness, the sun shadow, the vision's g), light grid ambient (vision
  ambient brightness; no slice thickness, research 109 section 2.3), and lamps with `isVolumetric` (478/482, vision
  spot and omni brightness). A box in an unlit room with ambient brightness 0 only darkens. VERIFIED (shader).
- **Emission:** none. VERIFIED (no other term).
- **Reach.** Boxes count only inside the froxel grid, about 2180 units from the eye (`r_volumetricDepth`, research
  109 section 2.2). INFERRED.
- **Masks** multiply the density by the red channel of their image, projected along the chosen axis; `scale` is world
  units per repeat, `offset` world units, `scroll` world units per second (INFERRED from `t`). VERIFIED for the maths.

### A.3 Do boxes alone start the pass? Yes, INFERRED (GT), with the pieces VERIFIED

- GT `R_VOL_AnyVisible` and the three pass gates (`r_volumetric.cpp:579, 706, 931, 1796-1803`) run the pass when
  `airDensity > 0` **or** `R_VOL_UseLocalMedia`, which is true when `volumeCount` (boxes added this frame) is non-zero
  (`:3596-3604`). When off, the translucent shaders get a black scatter and a white extinction image (`:2513-2517`).
  Not read in Replay (IDA down).
- A world box is added only when its `dpvs.volumetricVisData` bit is set (GT `R_VOL_AddVolumetrics`, `:1743-1773`).
  The Umbra query consumer sets that bit for user IDs of class 3 (GT `r_umbra_dpvs.cpp:1932-1942`). The converter puts
  every box in the tome as `0x30000000 + index`, listed by every cell in an occlusion bake (`replay_render.cpp:3560-3561,
  3630-3635`). VERIFIED in source. That the one-cell tome also lists them is INFERRED.
- The scatter shaders themselves never read the air density as a switch: a froxel with sigma 0 adds nothing. VERIFIED.
- With air 0 on a built map, a box is shaded with the vision's table defaults (research 31, research 126): albedo
  1 1 1, sun 1, spot 1, omni 1, ambient 1, g 0.15, absorption 0.

### A.4 Stock boxes (VERIFIED, ACTS exports)

| Map | Boxes | Typical record |
|---|---|---|
| mp_aniyah_tac | 3 | flags 0, axis-aligned, half 351 x 145..218 x 390..434, density **0.0002**, falloff **20**, heightFade 0, anisotropy 0, colour 1 1 1, no masks (type 0) |
| mp_village2 | 7 | flags 4, half 233..259 x 93..348 x 70..146 (flat layers at z 582-728), density **0.01 / 0.02 / 0.04**, falloff **8**, heightFade 0, anisotropy 1, colour .99 .78 .45, four opacity masks (`clouds_med_vol` 800/200 units, `clouds_hard_vol` 400 units, scroll 50-75 units/s, axis 2 and one axis 6) |
| mp_hideout, mp_emporium, mp_backlot2, mp_m_speedball, mp_shipment, mp_rust | 0 | |

The aniyah boxes sit in a vision with air 0.005 (sun 0.3, ambient 0, g 0.7): each box is 4 times the air (0.0002
against 0.00005). Village2's dense boxes are thinned by the product of four cloud masks. No stock box uses heightFade.
`volumetricMaskAtlasImage` is null on both maps.

### A.5 Mapping for the editor's `iw8r_volume` (compile_iw8.cpp:3010-3058)

| Key | Write | Range and advice |
|---|---|---|
| mins / maxs / angles | `center`, `axes`, `half_size` (as today) | the box's own Z is the height-fade axis |
| `density` | `density` as is: extinction per world unit | **today's default 0.2 is about 1000 x stock**: 256 units of it pass e^-51. Use 0.0002 (light haze) to 0.005 (thick); warn above 0.05. In vision terms: density = air value x 0.01 |
| `falloff` | `falloff`, world units | > 0; stock 8 to 20; keep it at most the smallest half size |
| `height_fade` | `height_fade`, 0..1 | the game clamps it; offer `height_half` (units) instead and write `6.931 / height_half` (needs height_half >= 6.93) |
| `color` | `color` with flags 0 | no effect in Replay 1.20; tint with the vision's `volumetricAlbedo`. Say so in the UI |
| `anisotropy` | `anisotropy` with flags 0 | no effect; the vision's `volumetricSunAnisotropy` rules |
| masks (optional) | type 2, axis 0/1/2, image, scale, offset, scroll | image must be resident; red channel; scale > 0 |

**Suggested default box:** half size 128 (as today), density **0.001** (T through 256 units 0.77), falloff **32**,
height_fade 0, flags 0, colour 1 1 1, anisotropy 0, no masks. Light comes from the vision: with a dark map set
`volumetricAmbientBrightness` above 0 or flag lamps `iw8r_volumetric`, or the box only darkens.

## Part B. Fog on see-through surfaces

### B.1 Stock translucent shaders fog themselves (VERIFIED)

Of the 218 shipment pixel shaders, 37 read cb7[131..137] and t110 / t111 (scan of the disassemblies). Two were read:

| Shader | Technique | Blend bits | Kind |
|---|---|---|---|
| P07 `077107f8` | `twc/lit_3_lit_bad_ta1_2202_...` (MTL_TYPE_WORLD_VERTCOL, TECHNIQUE_LIT_FORWARDPLUS_BITMASK) | 0x28050 = ONE / INVSRCALPHA (premultiplied) | lit translucent world |
| P4D `4de524e0` | `elc/unlit_6_effect_bad_tca_8802_...` (MTL_TYPE_EFFECT_LMAP) | 0x8010 = ONE / ONE | unlit additive effect |

Blend bits decoded with the converter's layout note (`iw3_render_assets.h:128-140`). The techniques list no code
texture for t110, t111 or t120, so the engine binds them per view. INFERRED from the argument list.

### B.2 The analytic fog in P07 (VERIFIED, P07:1612-1758)

- Inputs: `len` = eye-to-pixel distance (P07:1613), unit direction, `dz` = world z of the eye-to-pixel vector (P07:1734),
  `cosS = dot(dir, cb7[132].xyz)` (P07:1615).
- Path `len + cb7[132].w` (P07:1614), shortened only by the Warzone circle when `cb7[141].z > 0` (P07:1616-1713; off on MP).
- Plain branch (`cb7[137].z < -0.5`, P07:1715-1725) and atmospheric branch (P07:1727-1757) are **the same formulas as
  research 111 section 4 and 5, without the sky branch**. The spline LUT is a 1D texture **t120** sampled with s8 at
  `x(2 - x) * 0.992188 + 0.003906` (P07:1728-1733). `src/core/game_fog.cpp:210-257` matches it line for line for a
  non-sky pixel.

### B.3 Order and the volume (VERIFIED, P07:1891-1997 and P4D:901-968)

```hlsl
// C = the shader's premultiplied colour (diffuse x coverage + specular), a = coverage (written to o0.w and o1)
float3 c  = C * T + a * L;                                  // P07:1891-1893
float  Tv = t111.SampleLevel(s8, float3(uv, slice), 0).x;   // P07:1894-1901, slice of position.w - 4 (-4 when position.z > 0.984375)
float3 Sv = bicubic(t110, uv, slice) * cb7[42].y;           // P07:1902-1956 (four bilinear taps, cb7[187].yz, cb7[188].xy)
c = c * Tv + a * Sv;                                        // P07:1957-1959
out = float4(min(c * cb7[42].x, 32255), a);                 // P07:1971, 1997 (cb7[187].x adds a debug tint, 0 in play)
```

- **Pre-exposure:** the whole colour, fog light included, is multiplied by `cb7[42].x` at the end; the scatter image
  is first multiplied by `cb7[42].y` (it is stored pre-scaled, research 109). VERIFIED.
- **Additive (P4D):** the same order: `(C * T + a * L) * Tv + a * Sv` (P4D:901-926), but one bilinear tap of t110, and
  the slice is clamped to the column's last filled slice from the 2D code texture t112
  (`(min(gather x 255) - 0.5) / 128`, P4D:910-920). Here `a` is `1 - mean(lerp(1, cb3[2].rgb x tint, w))`
  (P4D:618-621): the material's own coverage. Under ONE / ONE nothing dims the background, so a stock additive effect
  only stays correct when its coverage is 0. INFERRED: for ONE / ONE layers use `a = 0`, that is `C * T * Tv`.

### B.4 What map_surface_realtime.hlsl does today

- `MAP_VOLUMETRIC_EXTINCTION` (lines 148-175, 1798-1840, 1860-1862) only for materials with row 2.w set
  (`iw7VolumetricExtinction`: IW7 god-ray cards). IW3 maps compile none (`g_extinctionDefines`,
  `iw3_render_assets.cpp:5662-5665`).
- Blended: `color * alpha + reflection * fogFactor`, then `* Tv + Sv * alpha`. Additive: `* Tv` only. Both match the
  stock order for the volume part. The slice, extinction and B-spline scatter match P07.
- **Missing:** the analytic fog (`T`, `L`) for every blended, additive and multiply surface; the volume for every
  such surface (not only row 2.w ones) once the air or boxes are on; no fog for a multiply layer from the native fog.
  `fogFactor` (`MAP_FOG`, the baked IW3 fog) must stay off whenever the native fog is on, or it fogs twice.

### B.5 HLSL to paste

**Rows.** cb7[42], [48], [49], [131..137], [187], [188]. Under the new define `MAP_LIGHTING_ROWS` must be at least
**189** (the stock shaders declare 213):

```hlsl
#if (defined(MAP_GAME_FOG) || defined(MAP_VOLUMETRIC_EXTINCTION)) && MAP_LIGHTING_ROWS < 189
#undef MAP_LIGHTING_ROWS
#define MAP_LIGHTING_ROWS 189
#endif
```

Change the guard of the volume block at line 148 to
`#if (defined(MAP_VOLUMETRIC_EXTINCTION) || defined(MAP_GAME_FOG)) && !defined(MAP_COVERAGE)`.

**The fog function** (after `cbuffer ReplayLighting`):

```hlsl
#if defined(MAP_GAME_FOG) && !defined(MAP_COVERAGE)
#ifdef MAP_GAME_FOG_SPLINE
Texture1D<float4> fogSplineLut : register(t120);   // Replay's fog spline LUT; zero unless a vision names a fogSpline
#endif
// Replay's analytic fog for a translucent pixel (PS 077107f8 lines 1612-1758; research 111 sections 4-5): no sky
// branch, no Warzone circle. T is the transmittance, L the in-scatter in Replay radiance (this shader's units after the
// division).
void GameFogTranslucent(float3 relativePosition, out float3 T, out float3 L) {
    float len = length(relativePosition);
    float3 dir = relativePosition / max(len, 1e-6);
    float path = len + replayLighting[132].w;
    float cosS = dot(dir, replayLighting[132].xyz);
    if (replayLighting[137].z < -0.5) {
        float sunF = saturate(cosS * replayLighting[137].x + replayLighting[137].y);
        float2 t = max(exp2(-path * replayLighting[133].xy), replayLighting[134].w);
        float tt = saturate(lerp(t.x, t.y, sunF));
        T = tt;
        L = lerp(replayLighting[135].xyz, replayLighting[134].xyz, sunF) * (1 - tt);
    } else {
        float x = saturate(path * replayLighting[137].w);
#ifdef MAP_GAME_FOG_SPLINE
        x = max(x - fogSplineLut.SampleLevel(gtaoSampler, x * (2 - x) * 0.992188 + 0.003906, 0).x, 0);
#endif
        float k = relativePosition.z * replayLighting[133].w;
        float H = abs(k) > 1e-6 ? (1 - exp2(-k * 1.442695)) / k * replayLighting[135].w : replayLighting[135].w;
        H += replayLighting[136].w;
        T = exp2(-x * H * replayLighting[133].xyz);
        float g = replayLighting[134].w;
        float m = 1 - g * cosS;
        float pR = (1 + cosS * cosS) * 0.059683;
        float pM = (1 - g * g) * 0.079577 / (m * m);
        L = (replayLighting[134].xyz * pM + replayLighting[135].xyz * pR) * (1 - T);
    }
    L /= MAP_LIGHT_UNIT_SCALE;
}
#endif
```

**Where it applies** (replacing lines 1798-1810, 1836-1841 and 1858-1862; opaque and cutout surfaces stay as they
are, the native pass fogs them):

```hlsl
#if defined(MAP_GAME_FOG) && !defined(MAP_COVERAGE) && MAP_DEBUG_MODE == 0
    float3 fogT = 1, fogL = 0;
    float volumetricTransmittance = 1;
    float3 volumetricInScatter = 0;
    if ((kind == 2 || (flags & 768) != 0) && !skyPlaced) {
        GameFogTranslucent(input.relativePosition, fogT, fogL);
        float slice = VolumetricSlice(input.position);
        volumetricTransmittance = VolumetricExtinction(screenUV, slice);
        if (kind == 2)
            volumetricInScatter = VolumetricScatter(screenUV, slice) * replayLighting[42].y / MAP_LIGHT_UNIT_SCALE;
    }
#endif
    // multiply layers (512): fade toward neutral (INFERRED, no stock multiply shader read)
    //   return float4(lerp(1, texel.rgb, input.color.a * dot(fogT, 1.0 / 3) * volumetricTransmittance), 1);
    // additive layers (256), ONE / ONE: dim only, no in-scatter
    //   color *= fogT * volumetricTransmittance;
    //   return float4(min(color * alpha * MAP_LIGHT_UNIT_SCALE * replayLighting[42].x, 32255.0), alpha);
    // blended (kind 2), ONE / INVSRCALPHA, premultiplied:
    if (kind == 2)
        color = color * alpha + reflection;                 // no * fogFactor under MAP_GAME_FOG
    color = color * (fogT * volumetricTransmittance) + alpha * (fogL * volumetricTransmittance + volumetricInScatter);
    return float4(min(color * MAP_LIGHT_UNIT_SCALE * replayLighting[42].x, 32255.0), alpha);
```

**Converter side.** Define `MAP_GAME_FOG` for the generated surface shaders when the final vision has
`fogAtmosphericScattering` 1, air density > 0, or the map has `volumetrics`; never together with `MAP_FOG` (research
126 change 3). With the fog off the function is harmless: the plain branch with `fogMaxOpacity` 0 gives T = 1, L = 0
(cb7[134].w = 1), and with the pass off the extinction image is white and the scatter black (A.3). Keep the existing
row 2.w path for IW7 god-ray cards (they are additive: same lines).

## Open points

- `R_VOL_AnyVisible` / `R_VOL_UseLocalMedia` and the pass gates were read in GT only, not in Replay.
- That the one-cell Umbra tome (no occlusion bake) lists the volumetric IDs, so a box gets its vis bit, is INFERRED.
- The mask sampler's address mode (s14) and the time unit of `scroll` were not read.
- The meaning of an additive effect's coverage `a` and the multiply-layer rule are INFERRED.
- t120 binding for the converter's techniques is INFERRED from the stock argument list; keep `MAP_GAME_FOG_SPLINE` off
  until a built map uses a fog spline.
- No box and no fogged glass was seen in game.

## Reproduce

```
py -3 -I tools/acts_export.py mp_village2 gfx_map C:\...\Temp\iwb\vol2\mp_village2     (asset draw.volumetrics)
py -3 -I tools/acts_export.py mp_aniyah_tac rawfile <scratch>                            (visions)
py -3 -I out/agent/post109/py/vision_dump.py <vision> volumetric
py -3 -I out/agent/post109/py/disx.py 0x18B6EC0 0x18B7100                               (R_VOL_AddVolumetric)
copy out/agent/toon_ship/ps/.../pixelshader/*.cso to scratch; py -3 -I out/agent/shader_research/py/dxbc_dis.py <scratch>
```

## Masks on a built map (added 2026-11-14, main session)

Built, proven offline, not seen in game.

- Keys `mask`, `mask2`, `mask3`, `mask4` of an `iw8r_volume` (core `src/core/volume_mask.*`):
  `"<picture> [x|y|z] <sizeU> [<sizeV> [<scrollU> <scrollV> [<offsetU> <offsetV>]]]"`. The build writes them as the
  box's `masks` (type 2, the only kind the game draws; axis 0, 1 or 2; scale = the sizes in units; offset; scroll)
  and each picture as `images/iw8r_vmask_<picture>.dds`; the converter's existing volumetric path makes the picture
  a resident image of the map's zone.
- Pictures: `<map folder>/volume_masks`, then the editor's `data/volume_masks` (`clouds_soft`, `clouds_hard`: tileable
  noise made by `tools/gen_volume_masks.py`; stock mp_village2 uses its own `clouds_med_vol` / `clouds_hard_vol` at
  800, 400 and 200 units with scrolls of 50 to 75 units a second).
- The viewport (Game Lighting) samples them by the rule of A.2: `uv = (u / sizeU, -v / sizeV) + (offset + t * scroll)
  / size + 0.5`, u and v the two box axes other than the mask's, the red channel, repeated.
- Still not read from the exe (A.6): the sampler's address mode (taken as repeat: a scrolling picture needs it) and
  the unit of the scroll's time (taken as seconds).
- Proof `tools/volume_mask_build_test.py`: the built world's box has both masks with the keys' axes, sizes, offsets
  and scrolls, masks 2 and 3 unused; both images are in the map's zone, and a striped test picture comes back pixel
  for pixel.
