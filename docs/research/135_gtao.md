# 135. GTAO (Replay 1.20): the `cs_gtao` kernel, its prepass, the temporal filter, and a pixel-shader port

**Why.** Game Lighting's AO is a stand-in (`kAoShader` in `src/viewport/render.cpp`: 12 hemisphere samples in world
space, a radius and a power setting, a 4x4 depth-aware blur, a 6000-10000 unit fade). The game uses GTAO (research 107
section 4.3, research 132 G14). This note decodes the game's compute shaders into a spec that can be ported to one
full-screen pixel shader, and says what the port cannot reproduce.

**Labels.** VERIFIED = read in the disassembly or the Replay exe (cited). INFERRED = names, meanings or values that
come from usage, arithmetic or the IW8 game-test decompile ("GT"). UNKNOWN = not established. The DXBC has no RDEF
block, so register names are ours. Nothing was run in the game.

## Summary

1. **Passes** (section 1). The floatZ resolve writes linear depth / 6 with a max-depth mip chain (research 104). A
   prepass (`cs_gtao_prepass`) runs the same GTAO kernel at low resolution (INFERRED eighth) on **normals rebuilt
   from depth**, and stores the visibility moments (v, v²). Two separable 3-tap filters blur them. The main
   `cs_gtao` runs at **full resolution** (INFERRED): one thread per 2x2 quad, G-buffer vertex normals. Per quad it
   computes 1 to 4 pixels, depending on the local variance, and blends with the reprojected history. Its output
   (RGBA8: `0.537028 v`, history count / 255, `0.537028 v`, `0.537028 v`) is what the lit PS reads (t95).
2. **Kernel** (section 2). **4 slices** (8 half-directions), **4 steps per direction** (32 depth taps per pixel).
   Slice angle `(i + rot) pi/4`, where rot comes from a 256x256 blue-noise texture plus a golden-ratio offset every
   2 frames. Steps are quadratic: `t = ((j + jit)/4)^2`.
3. **Radius**: `50 / (z tanHalfFov)` in uv, which is **100 game units** (inches) in view space. It is clamped to
   **512 px at 1080p** (`512 cb7[47].zw` texels). The sampling starts 2 px from the pixel. Steps sample the
   **max-depth mips 1 to 4** (never mip 0): `lod = 1 + min(3, t R_px / 45.25)`.
4. **Falloff**: weight 1 out to 50 units from the pixel, 0 at 200 units (100 units for samples nearer than 50 units).
   **Thickness**: a running horizon decays when a later sample lies 5-15 units below the horizon sample (along N).
   Depth bias: the centre z is scaled by 0.9975.
5. **Visibility**: XeGTAO-style cosine-weighted arc integral, `(cos n + 2 h sin n - cos(2h - n)) / 4` per side,
   weighted by the projected normal length. It is **normalised by the unoccluded integral** `cos n + n sin n` (not
   by the slice count). There is no bent normal and no power. The store is `0.537028 v`; the lit PS multiplies by
   1.8621 (research 107).
6. **Distance fade** (r_ssaoDistanceFade = 8, 4, VERIFIED): `v = lerp(1, v, saturate((R_px/2 - 4) / 4))`. The AO
   is gone when the 100-unit radius covers less than 8 px. It is full from 16 px.
7. **Denoise is temporal only** (section 4). Moment rejection uses an 8x8 history mean against the low-res moments.
   An accumulation weight rises to 0.95. A "skip" mode holds the history; a random 1/8 of 16x16 tiles refresh with
   weight 0.9 each frame. There is no full-resolution spatial filter.
8. **Port** (section 5): the same kernel in a pixel shader, with depth-derived normals (as the game's own prepass
   does), a 4x4 interleaved rotation/jitter and the existing 4x4 depth-aware blur. Radius 100, power 1, the
   game's falloff, thickness, clamp and fade.

## How it was obtained

- Shaders: `out/agent/shade107/cs/` (from research 107). **G:n** = line n of `cs_gtao.233.asm` (1,223 lines).
  **P:n** = `cs_gtao_prepass.237.asm`. **H:n** / **V:n** = `cs_gtao_prepass_filter_horizontal.238` / `_vertical.239`.
  The cinematic (`cs_gtao_cinematic_quality.234`), MSAA (`cs_gtao.235`) and floatZ resolve
  (`cs_resolve_floatz_downsample.179`) .cso files under
  `out/agent/shader_research/cs/mw19replay/code_post_gfx/assets/computeshader/` were disassembled with
  `out/agent/shader_research/py/dxbc_dis.py` into a scratch folder and diffed against G. **C:n** = line n of the
  cinematic asm.
- Replay exe (`game_dx12_ship_replay.exe` 1.20, RVAs, read with `out/agent/shade107/py/pe.py` + capstone; IDA was
  not reachable): `R_RegisterSSAODvars` 0x19b12c0-0x19b142e; `R_SSAO_Generate` 0x19b1650-0x19b1c24 (prepass
  branch 0x19b167c-0x19b178b, main bindings 0x19b1790-0x19b1a8c, CB0 fill and dispatch 0x19b1aac-0x19b1bf4); the
  prepass wrapper 0x18a6960 (isPrepass = 1 at 0x18a6977); the main wrappers 0x18a67c0 and 0x18a6890;
  `R_SetSSAOInfo` 0x19b1c40 (research 107).
- GT: `gfx_d3d/rb_ssao.cpp` (whole file); `r_scene.cpp:11118-11127` (the AO target), 12957-12985 (viewSpaceScaleBias,
  viewAxis, viewMatrix); `rb_postaa.cpp:2180-2206` (temporalSamplingParams); `r_image.cpp:1135-1151, 1230` (default
  GTAO image, `blue_noise`).

## 1. Pipeline

| Pass | Shader | Resolution, dispatch | Reads | Writes |
|---|---|---|---|---|
| floatZ resolve | `cs_resolve_floatz_downsample` | full; mips 1-3 max, 1/8 min | HW depth | floatZ = `z / 6`, negative for viewmodel pixels (HW depth > 63/64); the sky fails the GTAO's `x 6 < 65500` test; max chain (research 104) |
| prepass | `cs_gtao_prepass` | moments RT (INFERRED 1/8); 1 thread per 2x2, 8x8 groups | t11 floatZ max (1/8), t5 blue noise, cb7 | u2: `(v, v^2, v, v)` per pixel (P:533-550) |
| filter H, V | `..._filter_horizontal/vertical` | moments RT; 1 thread per texel | t8 (moments) | u2: blurred `(m1, m2, m1, m1)` (H/V:30-45) |
| GTAO | `cs_gtao` (cinematic in screenshots, MSAA with VRS) | full (INFERRED, GT `r_scene.cpp:6064` sceneWidth x sceneHeight, format `GFX_RENDERTARGET_FORMAT_AO`, UAV unorm); `((W>>1)+7)>>3 x ((H>>1)+7)>>3` groups of 8x8 | t0 floatZ (mip 0 + max mips 1-4), t1 tangent basis (world **vertex** normal 10:10:10, research 104), t2 previous AO, t3 half-res velocity, t4 previous half-res velocity, t5 blue noise, t7 moments, cb7 | u0: `(0.537028 v, n/255, 0.537028 v, 0.537028 v)` (G:1192-1221) |
| lit PS | world PS t95 | full | the AO target ("persistent image 10", ping-ponged "Frontend SSAO [0]/[1]") | `ao = lerp(1, 1.8621 x, r_ssaoStrength)` (research 107) |

- Bindings VERIFIED in Replay: main slots 0, 1, 2, 3, 4, 6, 8, 7, u0, t5 and cb7 (0x19b1790-0x19b1bd4); prepass slots
  10, 11 and u2 (0x19b1703-0x19b1786). Slot 7 holds the moments RT when it is valid, otherwise the previous AO
  (0x19b1a1e-0x19b1a63, as GT `rb_ssao.cpp:548-554`). Slots 6 (MDAO) and 8 (prepass AO) are bound but **not
  declared** by `cs_gtao.233`. The prepass does not read slot 10 (floatZ min).
- Shader choice: one boolean picks between two shaders (0x19b17d4-0x19b17ee; GT: `R_Screenshot_InProcess` selects
  the cinematic variant). The MSAA variants read t1 per MSAA sample with a VRS order from CB0[3..4] (diff VERIFIED).
- Order (INFERRED from GT): prepass, filter H, filter V, then GTAO, on async compute after the depth prepass
  (research 104 section 1). The Replay prepass wrapper passes floatZ min/max and the moments handle; the main
  wrappers pass the moments handle (0x18a67c0, 0x18a6960). The frame-level calls go through a job table, so the
  per-frame order is INFERRED.
- No history (first frame, teleport): t2 = the 4x4 default GTAO image `0x00880088` = (0.533, 0, 0.533, 0), which
  means v = 0.99 and count 0 (GT `r_image.cpp:1135-1151`, INFERRED).

## 2. The GTAO kernel (main pass; the prepass differences are in 2.8)

### 2.1 Per pixel: position, normal, skip (VERIFIED)

- Each thread covers the quad `2 tid + {0,1}^2`. `texel = CB0[0].zw`, and the quad centre is `(2 tid + 1) texel`
  (G:43-48). The 4 depths are a gather of t0 at the quad centre, x 6 (G:184-185). The pixel index i has bit 0 = x and
  bit 1 = y (G:224-229).
- Sky: `z6 >= 65500` gives `v = 1` (stored 0.537028; G:222, 441).
- `z = |floatZ x 6|`. View position `P = ((uv cb7[91].xy - cb7[91].zw) z, z)` (G:260-261). cb7[91] =
  viewSpaceScaleBias = `(2 tanX, -2 tanY, tanX, -tanY)`, where `tanX = tanHalfFovY x aspect` (GT
  `r_scene.cpp:12959-12962`, row INFERRED from usage). So `P = ((2u-1) tanX z, (1-2v) tanY z, z)`: x right, y up,
  z forward. `V = normalize(-P)` (G:262-264).
- Normal: the t1 gather (G:186) at index i, `n = bits(0-9, 10-19, 20-29) x 2/1023 - 1`, rotated by cb7[95..97]
  (G:239-255). cb7[95..97] = rows 0-2 of the transposed viewMatrix (world to view; GT order viewSpaceScaleBias,
  viewAxisX/Y/Z, viewMatrix at `r_scene.cpp:12959-12985`; INFERRED, the shader declares cb7 up to row 97). The
  normal is not renormalised.
- Bias: `Pb = (P.xy, 0.9975 P.z)` for sample offsets; V uses P (G:270, 302).

### 2.2 Radius (VERIFIED arithmetic; the units follow from cb7[91])

```
Ruv  = min(512 * cb7[47].zw * texel, 50 / (z * float2(tanX, tanY)))   // G:87, 256-259; cb7[91].zw*(1,-1) = (tanX, tanY)
```

- One uv unit spans `2 z tanX` in view space, so Ruv is **100 units** of lateral view-space distance at the pixel's
  depth. The slices are round in pixels: `Ruv.x W = Ruv.y H = R_px` unless clamped.
- Clamp: cb7[47].zw = `(W / (displayAspect x 1080), H / 1080)` (research 107 4.3, VERIFIED 0x19b1cb3-0x19b1cf4),
  i.e. (W/1920, H/1080) at 16:9. With `texel = 1/W, 1/H`, the clamp is `R_px <= 512 H/1080`, which is 512 px at
  1080p (0.47 screen heights).
- Example, `tanY = 0.5`, 1080p: `R_px = 50 x 1080 / (0.5 z)`. The clamp acts below z = 211 units. The fade (2.7) starts
  at z = 6,750 and is complete at 13,500.

### 2.3 Slice directions and noise (VERIFIED)

```
f      = CB0[1].x                                   // frameCount & 0xFFF (Replay 0x19b1afa-0x19b1b05)
k      = floor(f / 2)                                // G:217
xn     = 2 px + (1 - (f & 1)) + 0.5,  yn = py + 0.5  // G:218-234 (px, py = pixel index)
xi     = blueNoise.SampleLevel(s6, (xn, yn) / 256, 0).xy   // G:235-236, t5 = image "blue_noise" (GT r_image.cpp:1230)
rot    = xi.x + 6.472136 k                          // 4 phi;  G:237
jit    = frac(xi.y + 414.216705 k)                   // 256 phi; G:237-238
phi_i  = (i + rot) * pi/4,  i = 0..3                 // G:278-281; dir = (cos, sin) in uv (y down)
```

- 4 slices over a half turn, each walked both ways, so 8 directions per pixel. The per-pixel rotation is a
  fraction of the slice gap. The temporal offset is a golden-ratio sequence that steps every 2 frames; the frame
  parity alternates between two noise columns.

### 2.4 Steps (VERIFIED)

```
start  = uv +- dir * 2 * texel                        // 2 px out; G:283, 327 (r0.zw = 2 CB0[0].zw)
t_j    = ((j + jit) / 4)^2,  j = 0..3                  // quadratic, t < 1; G:290-292
uv_s   = start +- t_j * Ruv * dir                       // G:284, 296
lod    = 1 + min(3, t_j * length(Ruv * (W, H) / 2) / 32)   // = 1 + min(3, t R_px / 45.25); G:265-268, 293-295
z_s    = floatZ.SampleLevel(s7, uv_s, lod) * 6         // max-depth mips 1..4; G:297-299
```

### 2.5 Per sample: falloff, horizon, thickness (VERIFIED, G:299-324; side 2 G:342-368)

```
S     = (uv_s * cb7[91].xy - cb7[91].zw) * |z_s|, |z_s|
d     = S - Pb;  len = |d|                                   // len by the 0x1fbd1df5 sqrt trick
w     = lerp(saturate(2 - len/50), saturate(4/3 - len/150), saturate(|z_s|/50 - 1))
w     = (z6 * z_s6 > 0) ? w : 0                              // world vs viewmodel never occlude each other
h     = w * (dot(d, V)/len + 1) - 1                          // = lerp(-1, cos, w)
if (h > hmax) hN = -dot(d, N)                                // remember the horizon sample's height over the tangent plane
decay = saturate(0.1 * (-hN - dot(d, N)) - 0.5) * saturate(0.2 * dot(d, N) + 1)
hmax  = max(h, hmax - decay)                                 // start: hmax = -1, hN = 200
```

- Falloff: for samples deeper than 100 units, full weight to 50 units, then linear to 0 at 200. Samples nearer than
  50 units fade out between 50 and 100.
- Thickness: the running horizon drops by up to 1 (cosine) per step when a later sample is 5 units (start) to
  15 units (full) lower along N than the horizon sample. The decay is full while the later sample is at or above the
  tangent plane and fades to none when it is 5 units or more below it. Thin occluders stop casting a long horizon.
  Cinematic: decay x 0.25 (C:323-324).

### 2.6 Slice integral (VERIFIED, G:371-438)

```
D      = (cos phi, -sin phi, 0)                     // the uv direction in view space (y up)
axis   = cross(D, V)                                // not normalised
pn     = N - axis * dot(N, axis);  pl = |pn|
cn     = saturate(dot(pn, V) / pl)
n      = (dot(D, pn) >= dot(D, V) * dot(V, pn)) ? -acos(cn) : acos(cn)   // sin n = -/+ sqrt(1 - cn^2)
h0     = n + clamp(-acos(hmax_plus)  - n, -pi/2, pi/2)                  // the +dir side
h1     = n + clamp( acos(hmax_minus) - n, -pi/2, pi/2)                  // the -dir side
vis   += pl * ((cn + 2 h0 sin n - cos(2 h0 - n)) + (cn + 2 h1 sin n - cos(2 h1 - n))) / 4
norm  += pl * (cn + n sin n)
v      = vis / norm;   stored 0.537028 v           // G:438-439
```

- acos is Lagarde's `(pi/2 - 0.156583|x|) sqrt(1 - |x|)`, mirrored for x < 0. The sqrt is the bit trick.
- `cn + n sin n` is the unoccluded value of the two arcs (h = n -+ pi/2), so an open pixel gives v = 1 for any normal.
- No bent normal, no power, no strength in the CS. Strength and multi-bounce are applied in the lit PS
  (research 95, 107).

### 2.7 Distance fade (VERIFIED G:88-91, 180-183, 1190-1191; values VERIFIED from the dvar)

```
Rq   = min over axes of (W/2, H/2) * min(clamp, 50 / (zq * (tanX, tanY)))   // zq = the depth in the half-res velocity texel
fade = saturate((Rq - CB0[2].x) * CB0[2].y) = saturate((Rq - 4) * 0.25)
v    = lerp(1, v, fade)       // per quad, before the history blend
```

CB0[2] = `(fadeEnd, 1/(fadeStart - fadeEnd))` with r_ssaoDistanceFade = (8, 4): 0 below an 8 px radius, 1 above 16 px.

### 2.8 Variants

- **Cinematic** (screenshots): 16 slices (`pi/16`), 16 steps (`/16`), decay x 0.25, else identical (diff VERIFIED,
  C:269-369 and the same at the other 3 pixel copies).
- **Prepass** (P): the same per-sample and integral code (P:127-282, line for line). The differences: depth from t11
  (floatZ max at prepass resolution) at **mip 0**; the radius clamp is `64 cb7[47].zw^2 texel` (P:39-40, 112; equal
  to the main clamp at 1080p when the prepass is 1/8 resolution); no distance fade; only the farthest and the
  nearest pixel of each quad are computed (P:62-285, 286-506), and the other two copy the one whose depth is closer
  (P:507-532). **The normal comes from depth** (P:86-110):

```
dzx = |c - l| < |r - c| ? c - l : r - c;   dzy = |c - u| < |b - c| ? c - u : b - c   // one-sided, the smaller step
pxW = z_xn * 2 tanX / W;  pxH = z_yn * -2 tanY / H      // pixel size at the chosen neighbour's depth
(rx, ry) = ((2u-1) tanX, (1-2v) tanY)
N   = normalize(-dzx pxH, -dzy pxW, pxH pxW + dzx pxH rx + dzy pxW ry)   // = cross(dP/dx, dP/dy), faces the eye
```

## 3. Constants

| Constant | Meaning | Value | Evidence |
|---|---|---|---|
| CB0[0] | (W, H, 1/W, 1/H) of the target | full res (main), moments RT (prepass) | VERIFIED 0x19b1b47-0x19b1b7e |
| CB0[1].x | frameCount & 0xFFF | per frame | VERIFIED 0x19b1afa-0x19b1b1f |
| CB0[1].yzw | r_ssaoLowFrequencyBlendParams0 = (0.05, 0.4, 0.25) | **not read** by 233/237 (the shader uses baked literals) | VERIFIED 0x19b1b27-0x19b1b4f, 0x19b1386; grep of G, P |
| CB0[2].xy | (fadeEnd, 1/(fadeStart - fadeEnd)) | (4, 0.25) | VERIFIED 0x19b1b0a-0x19b1b54, dvar 0x19b1305-0x19b1318 |
| CB0[3] | params0 copy / VRS sample order (MSAA only) | - | VERIFIED write, unused by 233 |
| cb7[45].x | temporalSamplingParams.x = frame & 1 with TAA modes 2-3, else 0 | the refresh-tile hash sign (2x - 1) | INFERRED (GT rb_postaa.cpp:2186-2201), used G:99-100 |
| cb7[47].zw | ssaoParams.zw = (W/(aspect 1080), H/1080) | 1, 1 at 1080p | VERIFIED (research 107) |
| cb7[91] | viewSpaceScaleBias (2tanX, -2tanY, tanX, -tanY) | per view | INFERRED |
| cb7[95..97] | world-to-view rotation rows | per view | INFERRED |
| r_ssaoLowFrequencyBlendParams1 | (skipUpdateRate 0.125, skipMomentThreshold 0.666, skipAlphaMulThreshold 1.0, skipConvergenceFrameCount 16) | default; baked into the shader as 0.125, 1/0.666, 16 | VERIFIED 0x19b13c0-0x19b13e7; literals G:105, 157, 164 |
| Literals | 50 (radius / 2), 512, 2 px start, 0.9975, 50/150/200 falloff, 0.1/0.5/0.2 decay, 0.537028, 65500 sky | - | VERIFIED (lines above) |

UNKNOWN: the sampler states s5-s10 (filter and addressing; INFERRED point or linear clamp, with s8/s10 bilinear),
the blue-noise texel values (256x256 INFERRED from the 1/256), the exact prepass resolution, and the condition behind
the constant `atomic_umax g1, 0, 0` (G:212-215, which makes the minimum 1 pixel per quad in this permutation).

## 4. Filtering after the kernel

### 4.1 Prepass moments blur (VERIFIED H/V:30-45)

Three bilinear taps at -1.5, 0, +1.5 texels along x (H), then y (V), averaged (/3) over `.xy`. That is a separable
`[1 1 2 1 1] / 6` over 5 texels (INFERRED bilinear s8). Output `(E[v], E[v^2])`.

### 4.2 Adaptive pixels per quad (VERIFIED G:187-215, 443-479, 1153-1189)

- The quad's depths are sorted. **Pixel A = the farthest** is always computed; B = the nearest; C, D = the other two.
- The group reduces A's `(ao, ao^2)` over 4x4 threads (8x8 px, XOR 1, 8, 9, then 2, 16, 18), takes
  `var/mean`, and atomic-maxes it over the 8x8-thread group. It computes `1 + [> 0.035] + [> 0.3] + [> 0.8]` pixels
  (thresholds in visibility units; 1231.79/65535 = 0.035 x 0.537028 etc.).
- A pixel that is not computed copies A or B, whichever depth is closer (with 1 pixel computed, all four take A).
  In smooth areas the game's AO is therefore close to quarter resolution before the temporal blend.

### 4.3 Temporal accumulation (VERIFIED G:49-179, 1192-1221; meaning INFERRED)

```
// per quad
m      = T3[quad] (half-res velocity, uint): x = bits 0-10, y = 11-21, as m/1023 - 1, then
         |uv| = 0.040104 sat(2|m| - 1)^2 + 0.001563 min(2|m|, 1) (y: 0.071296, 0.002778)  // <= 80 px at 1080p, linear to 3 px
zq     = f10(bits 22-31)                                   // 5e5m float, INFERRED the linear depth
uvP    = quadCentre - mv;  pv = umin(gather T4 @ uvP)       // the previous velocity's nearest texel
valid  = (zq < zPrev + 0.05 zq) ? 1 : saturate(1 - 2 |mv - mvPrev|_px)       // G:130-138
valid *= min(1, 8 - 0.1 max|mv|_px) * all(0 < quadCentre - 3 mv < 1)        // G:139-148
(m1, m2) = T7.SampleLevel(s10, quadCentre).xy;  var = saturate(m2 - m1^2)   // G:49, 92-94
hMean  = mean of T2.x over the reprojected 8x8 px (gather + LDS)            // G:106-129
// tol, s, c: G:149-163;  n, w, refresh, a, n': G:95-105, 164-179
tol    = 0.35 (0.537 m1) + sqrt(0.25 x 0.537 var) + 0.05
s      = smoothstep01(min(1, saturate(1 - |hMean - 0.537 m1| / tol) / 0.666))
c      = (0.5 + 0.5 s) * valid
n      = T2.y @ uvP * 255
w      = 1 - 0.05 / (1 - x),  x = asfloat(int(1065292416 - 778061 (n + 2)))   // Schraudolph exp, ~0.996 x 0.9377^(n+2)
         // w(0) = 0.60, w(10) = 0.91, w(30) = 0.94, limit 0.95
refresh = frac(52.982918 frac(dot((tid/8 + 5.588238 f)(2 cb7[45].x - 1), (0.067111, 0.005837)))) < 0.125
if (c <= 0.95)                { a = w c;  n' = log(((a - 0.95)/(a - 1))) / log(0.95) - 1 }
else if (refresh && w <= 0.95) { a = 0.9;  n' = +inf }
else                           { a = 1;    n' = n - 16 }          // hold the history
// per pixel
out.x  = saturate(lerp(vFaded x 0.537028, T2.x.SampleLevel(s8, pixelCentre - mv), a));  out.y = saturate(n' / 255)
```

- When the history's 8x8 mean agrees with the fresh low-resolution GTAO mean, the pixel keeps its history. Each
  frame a random 1/8 of the 16x16 tiles take 10 % of the new value. When it disagrees, the blend weight drops to
  `w x c` (0.5 w at worst), and disocclusions (`valid = 0`) take the new value.
- A static view converges to the average of the per-frame kernel over the noise sequence. It is not stable within a
  few frames: about 10 refreshes at 1/8 rate (INFERRED from the rates).

### 4.4 No other filter

No other shader is known to read the AO target before the lit PS. Of the disassembled CS (`cs_post_opaque_processes`,
the floatZ, tone-map, veil and light-sensor shaders), none contains 0.537028 or 1.8621 (grep VERIFIED; the other
code_post_gfx CS were not disassembled, INFERRED). The lit PS reads its own pixel (research 95). The only spatial smoothing in the
game is the quad fill (4.2) and the bilinear history fetch.

## 5. Port recipe (editor, full-screen PS, no G-buffer)

**Inputs** the editor has: `depthTex` (HW depth), `aoVp` / `aoIvp` (row-major, world to clip), `aoEye`, the size.
**Derive** per frame (CPU or shader), for a symmetric perspective without jitter: `R = normalize(col0.xyz)`,
`U = normalize(col1.xyz)`, `F = normalize(col3.xyz)` of aoVp; `tanX = 1/length(col0.xyz)`, `tanY = 1/length(col1.xyz)`.
Passing R, U, F, tanX, tanY in the cbuffer is simpler. World units are game units, so every literal carries over.

```hlsl
static const float PI = 3.14159265, HALF_PI = 1.57079633;
float  viewZ(float2 uv, float lod) {              // max-depth pyramid of linear z (mip 0 = full res), 1e6 = sky
    return zPyr.SampleLevel(pointClamp, uv, lod); }
float3 viewPos(float2 uv, float z) { return float3((2*uv.x - 1)*tanX*z, (1 - 2*uv.y)*tanY*z, z); }
float  fastAcos(float x) { float r = (HALF_PI - 0.156583*abs(x)) * sqrt(1 - abs(x)); return x >= 0 ? r : PI - r; }

float ps_gtao(V i) : SV_Target {
    int2   px = int2(i.pos.xy);
    float2 size = aoPrm.zw, texel = 1 / size, uv = (px + 0.5) * texel;
    float  z = viewZ(uv, 0);
    if (z >= 65500) return 1;
    float3 P = viewPos(uv, z), V = normalize(-P), Pb = float3(P.xy, 0.9975 * z);
    float3 N = normalFromDepth(px, P);                 // section 2.8 formula, or cross of the min-step neighbours; dot(N, V) > 0
    float2 Ruv = min(512 * (size.y / 1080) * texel, 50 / (z * float2(tanX, tanY)));
    float  lodK = length(Ruv * size * 0.5) / 32;
    uint   q = interleave4x4(px);                      // 0..15, e.g. a 4x4 Bayer index
    float  rot = (q + 0.5) / 16, jit = frac(q * 0.618034 + 0.5);   // the editor's 16 noise states (see below)
    float  vis = 0, norm = 0;
    [unroll] for (int s = 0; s < 4; ++s) {
        float  phi = (s + rot) * (PI / 4);
        float2 dir = float2(cos(phi), sin(phi));
        float  hc[2];
        [unroll] for (int side = 0; side < 2; ++side) {
            float sg = side ? -1 : 1, hmax = -1, hN = 200;
            [unroll] for (int j = 0; j < 4; ++j) {
                float  t = (j + jit) * 0.25; t *= t;
                float2 su = uv + sg * dir * (2 * texel + t * Ruv);
                float  zs = viewZ(su, 1 + min(3, lodK * t));
                float3 d = viewPos(su, zs) - Pb;
                float  len = length(d), dn = dot(d, N);
                float  w = lerp(saturate(2 - len / 50), saturate(4.0 / 3 - len / 150), saturate(zs / 50 - 1));
                float  h = w * (dot(d, V) / len + 1) - 1;
                if (h > hmax) hN = -dn;
                float  decay = saturate(0.1 * (-hN - dn) - 0.5) * saturate(0.2 * dn + 1);
                hmax = max(h, hmax - decay);
            }
            hc[side] = hmax;
        }
        float3 D = float3(dir.x, -dir.y, 0), ax = cross(D, V);
        float3 pn = N - ax * dot(N, ax);
        float  pl = length(pn), cn = saturate(dot(pn, V) / pl);
        float  n = (dot(D, pn) >= dot(D, V) * dot(V, pn)) ? -fastAcos(cn) : fastAcos(cn), sn = sin(n);
        float  h0 = n + clamp(-fastAcos(hc[0]) - n, -HALF_PI, HALF_PI);
        float  h1 = n + clamp( fastAcos(hc[1]) - n, -HALF_PI, HALF_PI);
        vis  += pl * 0.25 * ((cn + 2*h0*sn - cos(2*h0 - n)) + (cn + 2*h1*sn - cos(2*h1 - n)));
        norm += pl * (cn + n * sn);
    }
    float v = vis / max(norm, 1e-6);
    float Rq = min(Ruv.x * size.x, Ruv.y * size.y) * 0.5;      // half-res px, as the game
    return lerp(1, v, saturate((Rq - 4) * 0.25));                // stores v directly (no 0.537028)
}
```

- **Depth.** Use linear view z (`dot(worldAt(uv, d) - eye, F)`, equal to clip w), with the editor's 1.5-step
  depth offset taken off as `ps_scenez` does (render.cpp:1623-1643). The raw offset depth puts small steps at the
  creases between faces of different slope.
- **Max-depth pyramid.** Build mips 1-4 with a max of 2x2, as the game's floatZ chain does (`ps_prevz` already
  makes mip 1). Using mip 0 for every step is simpler, but thin near occluders (rails, wires, foliage cards) then
  darken more than in game, because the game never samples mip 0 along a slice.
- **Noise and denoise.** The game averages its noise over time (4.3). The editor gets the same average from a 4x4
  interleaved (rot, jit) set of 16 states followed by the existing `ps_ao_blur` (4x4, view-distance tolerance
  `0.05 L + 1`), which covers exactly one period. Optional, for a static camera: accumulate frames with weight
  `min(0.95, n/(n+1))` and step `rot += 6.472136`, `jit += 414.216705` (mod 1) per frame, the game's sequence.
- **Combine.** As now: `aoV = lerp(1, v, r_ssaoStrength)`, then the multi-bounce and gtso (research 95, 107). Drop
  the AO radius and power settings: the game has a fixed 100-unit radius and no power. The game fade (8 to 16 px)
  replaces the 6000-10000 unit fade.
- **Skip** the viewmodel sign test (no viewmodel), the adaptive quad shading and the history machinery.
- **Cost.** 32 depth taps plus 4 neighbour loads per pixel, about 70 M taps at 1080p, the same order as the game's
  worst case.

**Expected deviations** (all INFERRED, nothing measured):

1. **Normals.** The game uses the interpolated *vertex* normal from the prepass RT1 (not normal mapped); depth gives
   the *face* normal. They are identical on planar brushes and on terrain far from creases. On smooth-shaded props
   the facet normal is off by up to half the facet angle (for example 11 degrees on a 16-sided cylinder). That moves
   n and the h clamps, so curved props show faint faceting and a few percent of difference. At silhouettes the
   one-sided min step (the game prepass's rule) keeps the error to isolated pixels.
2. **Resolution.** The game computes 1 to 4 pixels per quad and fills the rest by depth, then blends history with a
   bilinear fetch. The editor's per-pixel result is slightly sharper at contact edges. After the 4x4 blur it is
   slightly softer in open areas.
3. **Temporal lag.** In game the AO trails the camera for a few frames (history hold) and is noisy after
   disocclusions. A deterministic editor image matches the converged game image, not a moving one.
4. **Fast maths.** The bit-trick sqrt and Lagarde acos are within about 1-3 %; use them for a bit-faithful port.

## 6. Open points

- Replay's per-frame call order (prepass, the filters, then the main pass) is INFERRED. The bindings and
  wrappers are VERIFIED, but the job table that calls them was not followed. If the moments were ever invalid, t7
  would be the previous AO and the rejection test would compare against (ao, count). The shader's design rules this
  out in normal play (INFERRED).
- The meaning of the velocity's 10-bit field (INFERRED linear depth) and the half-res velocity layout come only from
  this shader. A port does not need them.
- The `GTAO & MDAO` mode (r_ssao 3) combines MDAO elsewhere. `cs_gtao.233` does not read t6. UNKNOWN, not needed
  for MP maps (mode 1).
- Sampler states and the blue-noise image are UNKNOWN (an ACTS image export of `blue_noise` would settle the noise).
