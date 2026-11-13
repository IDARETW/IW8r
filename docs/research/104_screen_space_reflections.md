# 104. Screen-space reflections (Replay 1.20): the trace, the combine with the probes, the dvars

**Why.** Puddles, wet layers and some glossy props in game show the scene reflected in them, not only the reflection
probe. Research 98 section 6 found that techsets whose hexB ends in `2` lerp the probe toward a screen trace (t96)
and the previous frame, and left the trace undecoded. This note decodes the whole path from the shipped bytecode, the
Replay exe and the IW8 game-test decompile, and gives a recipe for the editor's forward D3D11 viewport.

**Labels.** VERIFIED = read in the disassembly, the techset export or the Replay exe (cited). INFERRED = names,
meanings and engine-side behaviour that come from the IW8 game-test decompile (not Replay) or from arithmetic. The
DXBC has no RDEF block, so register names are ours.

## Summary

1. **Two passes.** The depth prepass of an SSR material writes the vertex normal (packed 10:10:10) to a second render
   target and sets a bit per 32x32-pixel tile in a mask (VERIFIED). After the prepass, the compute shader
   `cs_ssr_trace` runs one thread per **quarter-resolution** texel (mode Deferred LQ, the default; half resolution,
   twice the steps, in Deferred HQ). Each thread marches **12 steps** (HQ 24) of quadratic spacing in screen space
   along the reflection of the vertex normal, against the linear depth buffer ("floatZ") with a mip picked by distance.
   It writes `(t, hit, confidence, 0)`, where t is the fraction of the screen-space segment from the pixel to the
   ray's vanishing point (VERIFIED).
2. **The combine is in the lit pixel shader.** The lit PS of an SSR material reads t and hit from t96 at a random
   neighbour (8 to 24 pixels away), applies that t to its own normal-mapped reflection ray, projects the start and the
   far end of that ray into the **previous frame** and samples the previous frame's HDR scene there. The mip of that
   sample follows a roughness cone (VERIFIED). The weight is `hit x screen-edge fade x forward fade`. The specular is
   `Fenv x lerp(S, ssr, w)`, where S is the probe term after specular occlusion (VERIFIED). Rough surfaces (the probe
   fallback `fb >= 1`) and materials with probes off get no SSR (VERIFIED).
3. **Only flagged materials.** It applies only to techsets whose hexB last digit has bit 1 set (`...2`, also `...3`
   on one glass set). Those techsets carry technique flag 1, the prepass outputs above and a different lit PS. The
   ordinary world PS (research 95's `01631f08`) has no t96 or previous-frame input (VERIFIED on backlot2).
4. **Dvars (Replay exe, VERIFIED).** `r_ssrMode` = {Off, Clipspace Scale, **Deferred LQ**, Deferred HQ}, default 2.
   `r_ssrPositionCorrection` 1.0 [-2, 2]. `r_ssrFadeInDuration` 0.05 s [0.01, 1]. `r_ssrFadeInStrength` 2.0
   [0, 10]. `r_ssrWaterTrace` exists by name only (section 6).
5. **Editor recipe** (section 8): trace inside `ps_tex` against the *previous* frame's linear depth and HDR colour
   (both copied at the end of each frame). Use the game's step pattern, thickness test, cone mip and fades. Combine as
   `Fenv x lerp(S, ssr, w)`. Camera motion is handled by tracing in the previous frame's own screen space. A miss
   falls back to the probes.

## How it was obtained

- Compute shaders: the `code_post_gfx` export already on disk,
  `out/agent/shader_research/cs/mw19replay/code_post_gfx/assets/computeshader/cs_ssr.hlsl/` (`cs_ssr_trace.427`,
  `cs_ssr_trace_hq.428`, `cs_ssr_trace_water.429`, `cs_ssr_trace_water_hq.430`) and `cs_ssr_msaa.hlsl/`
  (`cs_ssr_trace.431`, `cs_ssr_trace_hq.432`), plus `cs_floatz_downsample.hlsl/`. They were disassembled with
  `out/agent/shader_research/py/dxbc_dis.py`, NUL-stripped. **T:n** = line n of `cs_ssr_trace.427`.
- World shaders: the techset `tw/lit_3_lit_rpl_ta1_802400_1002000000000012_0_1_1_0_0_113810015b_0_0_1_0_0`
  (backlot2 export `out/agent/shader_research/allts/.../techset/tw/...496.asset.json`). Its shaders were exported with
  `acts.exe --noUpdater fastfile -r mw19replay -g <replay exe> --oodle <oo2core_7_win64.dll> -a pixelshader,vertexshader
  -n 3c7457d32eaed2137290582793ad979a,8e763cd63dc029fd397f9ff4d4332109,5dcb00679f553a3b1c24639b2b767e8f,47bcf20f23abd85485b0ec19a9e94884
  -l -o <short temp dir> zone\techsets_mp_backlot2.ff`, then disassembled the same way. **P:n** = prepass PS
  `3c7457d3...` (46 lines). **L:n** = lit PS `8e763cd6...` (1,339 lines). The prepass VS `5dcb0067` outputs only
  SV_POSITION and the decoded vertex NORMAL. The scratch export (65 KB) was deleted; the command above recreates it,
  and the line numbers are those of the NUL-stripped `.asm`.
- Technique arguments and flags: the backlot2 techset JSON (`args`, `flags`, `colorRtCount`, `renderTargetFormats`),
  tabulated over all 793 techsets.
- Replay exe: the dvar registrations, disassembled with capstone from `game_dx12_ship_replay.exe` (read only):
  `r_ssrMode` at 0x140EDEAE0 (names table 0x1423E5C18), the three float dvars at 0x1419304BD-0x141930553, with
  their default, min and max loaded into xmm14 / xmm10 / xmm7 / xmm9 at 0x14193026B / 0x14192FD43 / 0x14192F23D /
  0x14192EF70. IDA was not reachable.
- Game-test decompile (`E:\mw124\mw164proxy\$dst\game-test-src-cleaned\code_source\src\`), used for names and
  engine-side flow only. `gfx_d3d/rb_ssr.cpp` (trace dispatch, CB0 layout, shader choice);
  `gfx_d3d/r_scene.cpp` 8749-8845 (`R_SetScreenSpaceReflectionInfo`), 10281-10286 and 11160-11192 (render target
  sizes and bindings), 5621-5690 (`R_GetResolvedSceneRt`), 1715-1740 (the SSR render feature) and 8080-8087 (the
  radiometric scale); `gfx_d3d/rb_draw3d.cpp` 2236 (trace after the prepass), 3170-3195 and 3480-3495 (the resolves);
  `gfx_d3d/rb_perceptual.cpp` 424-560 (the blur pyramid); `cgame/cg_drawtools.cpp` 2414-2431 (`CG_UpdateSSR`).

## 1. Resources and frame order

| Resource | Content | Evidence |
|---|---|---|
| Tangent-frame RT (prepass `SV_TARGET1`, format code 1) | `uint` packed world vertex normal: `n = saturate(N*0.5+0.5)*1023`, x bits 0-9, y 10-19, z 20-29 | VERIFIED P:28-35; read back T:75-79 (`*0.001955 - 1`) |
| SSR mask (`u6`, code RW texture 82) | 1 bit per 32x32 full-res tile; a 32-bit word covers 256x128 pixels (8x4 tiles); `atomic_or` when `cb7[220].x != 0` | VERIFIED P:36-44; size `(w>>8, h>>7)` in r_scene.cpp:11171 (INFERRED for Replay) |
| floatZ (code image 0, the trace's T0) | linear view depth / 6, sign = first-person viewmodel pixel, sky >= 392999/6 (fp16 max, INFERRED) | VERIFIED T:66-68 (`*6`, `< 392999`), same position formula as research 95 |
| floatZ mips | `cs_floatz_downsample` writes a max chain (U0-U3) and a min chain (U4-U7); the min chain goes to `m_floatZEighthMipMinRt`, so floatZ's own mips are INFERRED to be the **max** (farthest) depth; `cb7[46].x` = its mip count (`mipSizes.x`) | VERIFIED max/min in `cs_floatz_downsample_interior`; routing INFERRED (r_scene.cpp:10894, 10970) |
| SSR trace RT = persistent image 11 = **t96** | `(t, hit, conf, 0)`; LQ `w/4 x h/4`, HQ `w/2 x h/2`, 1 mip, format `GFX_RENDERTARGET_FORMAT_SSR_TRACE` (unknown) | shader factor 4 / 2 VERIFIED (T:70 vs HQ); sizes and binding INFERRED (r_scene.cpp:10281-10286, 11170-11192) |
| Previous frame colour = code image 6 (lit PS **t4**) | "Resolved Scene": half resolution, 4 mips, mip 0 a downsample of the scene colour, mips 1-3 Gaussian-blurred (glossy pyramid); pre-exposed HDR (x radiometric scale of its frame) | binding VERIFIED (techset arg `MTL_ARG_CODE_TEXTURE index 6 -> t4`); content INFERRED (r_scene.cpp:5624-5690, rb_perceptual.cpp:480-530) |
| Previous water trace = persistent image 12 (t97) | the water trace of the previous frame | INFERRED (r_scene.cpp:11147, 11197) |

Order (INFERRED from rb_draw3d.cpp). Depth prepass (normals and mask) → floatZ resolve and downsample → async compute
(GTAO, static velocity, `RB_SSR_DeferredTrace`) → forward opaque lit (SSR materials read t96 and code image 6) →
lit transparents → emissive effects → "Resolve Distortion" (scene colour → Resolved Scene, the next frame's SSR
source) → distortion → post. The resolve runs when a visible material has the SSR technique flag (render feature 2)
or distortion is on. On a teleport the resolved scene is cleared.

## 2. The trace (`cs_ssr_trace`, VERIFIED unless marked)

CB0 (208 bytes, layout INFERRED from rb_ssr.cpp). `[0]` = (trace w, h, 1/w, 1/h). `[1].x` =
`temporalSamplingParams.z` (a per-frame value). `[2]` = (2f·aspect, -2f, f·aspect, -f), f = tanHalfFovY (water
trace only). `[3..6]` = the transposed viewProjection (rows x, y, z, w; the trace uses 3, 4 and 6). `[7..9]` = a
rotation (water trace only). `[12]` = VRS sample order (MSAA variant only). `CB1[7]` is the scene constant buffer
(`cb7` of the lit shaders). Dispatch: 8x8 threads per group, `ceil(w/8) x ceil(h/8)` groups.

```
// one thread per trace texel (LQ: quarter res, HQ: half res)
if (!(mask[x>>6, y>>5] & 1 << ((x>>3 & 7) | (y>>3 & 3) << 3))) { out = 0; return; }   // T:35-44 (HQ: >>7, >>6, >>4)
n   = lerp(IGN(px + 5.588238 f), H(px) + 1.618034 f, 0.05)                 // T:46-61, f = CB0[1].x
      IGN(p) = frac(52.982918 * frac(dot(p, (0.067111, 0.005837))));  H = frac(frac(x*.55455)*x+y) * (frac(y*.308517)*y+x))
uv  = ((px + 0.5) + (n, frac(12.944272 n)) * 0.5 - 0.5) / size             // T:62-65, a random point in the texel
d   = floatZ.SampleLevel(s5, uv, 0) * 6                                      // T:66-67
if (|d| >= 392999) { out = 0; return; }                                       // T:68, sky
N   = unpack(tangentFrame.Load(uv * fullRes))                                // T:70-79, world vertex normal
P   = (cb7[92] * (1-2uv.x) + cb7[93] * (1-2uv.y) + cb7[94]) * |d|           // T:80-87 (d < 0: viewmodel rows 100-102)
I   = P / |P|;  R = normalize(reflect(I, N))                                  // T:88-96
if (dot(R, I) <= 0) { out = 0; return; }                                      // T:97-99, only rays leaving the eye
A   = (uv, 1/|d|)                                                            // T:100-102
E   = project(P + 65500 R) -> (uvE, 1/wE),  uv = ndc * (0.5, -0.5) + 0.5     // T:103-110, CB0 rows 3, 4, 6
D   = E - A
A.xy += D.xy * max(1/fullW, 1/fullH) / |D.xy|                                 // T:111-117, start one pixel out
len = |D.xy * fullRes|                                                       // T:122-125, segment length in pixels
j   = frac(n + 0.5);  zPrev = |d|;  hit = false
for (i = 0; i < 12; ++i) {                                                   // T:132-173 (HQ: 24 steps, /24)
    t   = ((i + j) / 12)^2                                                    // quadratic: dense near the start
    Q   = A + t D;  zr = 1 / Q.z                                             // perspective-correct ray depth
    lod = cb7[46].x > 0 ? min(cb7[46].x, t * len / 256) : 0                  // coarser depth mips far along the ray
    zs  = floatZ.SampleLevel(s7, Q.xy, lod) * 6
    hit = |zs| * 1.002 < zr                                                  // ray behind the surface (0.2% bias)
       && |zr - |zs|| < max(|zr - zPrev|, 48)                                // thickness: one step's depth, at least 48 units
       && !(zs < 0 && d >= 0)                                                // a world ray never hits the viewmodel
       && t > 1 / len                                                        // at least one pixel travelled
    zPrev = zr
    if (hit) break;
}
t    = saturate((1/zr - A.z) / D.z)                                          // T:174-179 (the last step's t)
uvH  = A.xy + t D.xy;  hit = hit && all(|uvH - 0.5| < 0.5)                   // T:180-186, on screen
conf = 1 - saturate(50 * |zr - |zs|| / |zs|)                                 // T:187, 208-209
out  = (t, hit ? 1 : 0, conf, 0)                                             // T:210-215 (a miss still stores t, conf)
```

- There is **no hierarchical traversal**. It is a fixed 12- or 24-step march with a distance-based mip.
- The thickness is in game units (inches).
- The far end is 65500 units out, so t spans the whole ray up to its vanishing point.
- The thermal-vision branch (T:194-207, `cb7[13].x == 2`) rejects hits on a heat mask; ignore it.
- `cs_ssr_trace_hq` differs only in the resolution factors and the step count (diff VERIFIED).
- The MSAA variants (`cs_ssr_msaa`, used with VRS) read the normal per sample with an ordered sample pattern
  (`CB0[12]`).
- The water variants are in section 6.

## 3. The combine in the lit pixel shader (L = PS `8e763cd6`, VERIFIED unless marked)

The SSR lit PS is the research 95 world PS plus two inputs: t96 and the previous frame colour at t4. The thermal mask,
which is t4 in the plain PS `01631f08` (its line 89), moves to t5 (L:94). Slot numbers shift with the material
texture count, so research 98's decal PS has the previous frame at t5. Inside the probe block:

```
if (cb3[0].y & 1) {                                        // L:612, probes on for this material
  fb = saturate(g' * cb7[186].x + cb7[186].y)              // L:622, rough fallback
  if (fb < 1) {                                            // L:624, rough surfaces skip probes AND SSR
    P = probes(...)                                        // L:625-807, research 95
    R = reflect(I, N)                                      // L:461-463, the normal-mapped N
    fwd   = (cb7[124].w, cb7[125].w, cb7[126].w)           // L:808-811, column w of the previous viewProj = previous forward
    wF    = saturate(4 * dot(fwd, R))                      // L:836, fades rays that face the camera plane
    n2    = same noise family as the trace, frame cb7[45].z, pixel x (2 cb7[45].x - 1)          // L:812-829
    uvT   = (pixel + (2 n2 - 1) * (24 - 16 g')) * cb2[7].zw                                    // L:626, 830-832
    (t,h) = t96.SampleLevel(s10, uvT, 1 - g').xy           // L:833-834, a random neighbour's t and hit
    if (t >= 0.9999) wF = 0                                // L:835-837
    if (wF > 0) {
      r  = 1 - g'
      c  = max(0, min(1.01737 r^3 + 0.070128, 1) * t + 1.48707 r - 0.284052)   // L:840-846, cone
      Pp = P_camrel + cb7[128].xyz                         // L:852, into the previous eye's frame
      uv0 = prevUV(Pp);  uv1 = prevUV(Pp + 65500 R)        // L:853-867
      uvH = lerp(uv0, uv1, t) + (n2 - 0.5) * min(0.03 c, 1)                   // L:868-870
      e   = 2 uvH - 1
      edge = saturate((1 - e.x^2) * (1 - e.y^2) * cb7[129].x)                 // L:871-875
      w   = wF * h * edge                                  // L:876-877
      ssr = t4.SampleLevel(s11, uvH, 3 * min(c, 1)).rgb * cb7[42].w           // L:878-880
    } else { w = 0; ssr = 0; }
    if (cb7[220].x == 0) { ... section 4 ... }             // L:884-903
  }
  P' = lerp(P, Lr, fb);  so = gtso(...);  S = lerp(0.18 * lerp(P', Lr, 0.85), P', so)   // L:907-916, research 95
}
Fenv = f0 * A + B                                          // L:921-923
spec = Fenv * lerp(S, ssr, w)                              // L:927-932
color += spec * cb7[43].w                                  // L:955
```

`prevUV(X)`: `clip.xyw = X.x*cb7[124] + X.y*cb7[125] + X.z*cb7[126] + cb7[127]` (the `.xyw` columns), then
`uv = clip.xy / clip.w * cb2[4].xy + cb2[4].zw` (`cb2[4]` INFERRED = (0.5, -0.5, 0.5, 0.5) for a full-screen view).

Constant identities, INFERRED from `R_SetScreenSpaceReflectionInfo` and research 95:

| Constant | Meaning |
|---|---|
| `cb7[124..127]` | `ssrPrevFrameViewProjectionMatrixR0..R3`, the previous frame's viewProjection, camera-relative, row-vector |
| `cb7[128]` | `prevEyePositionTransform` = (eye now - eye before) x `r_ssrPositionCorrection`, w = the dvar |
| `cb7[129].x` | `ssrFadePS.x` = ssrFade x `r_ssrFadeInStrength` x (render width / height); about 2 x 16/9 = 3.56 once settled |
| `cb7[42].w` | 1 / previous frame's radiometric scale (`radiometricScale` = (now, 1/now, before, 1/before), r_scene.cpp:8084-8087). It turns the stored pre-exposed colour back into radiance |
| `cb7[220].x` | `ssrMaskWrite` = `r_ssrMode >= 2` |

What this means:

- The trace uses the smooth vertex normal. The PS reuses its t with the **normal-mapped** reflection vector.
- The neighbour lookup (8 to 24 pixels) and the uv jitter `0.03 c` are noise that the game's temporal AA averages.
- `t96.z` (confidence) is not read by this PS.
- The SSR colour **replaces** S, the term after specular occlusion. It gets no gtso and no 0.18 / 0.85 ambient mix.
  It also gets no `fb` blend, because rough pixels never reach the SSR code.
- The edge fade reaches full weight where `(1-x^2)(1-y^2) >= 1/3.56`. On the centre line that is `|x| <= 0.85`.
- The forward fade gives zero weight to rays at or behind the camera's image plane. It is full when R is within about
  75 degrees of the view direction (`cos >= 0.25`).
- When SSR was invalidated (a scope transition and similar), `CG_UpdateSSR` restarts `ssrFade` at 0 and raises it
  by `frametime / r_ssrFadeInDuration` up to 1. `cb7[129].x` grows with it, so the reflection fades in over about
  50 ms (INFERRED).

## 4. Mode 1, "Clipspace Scale" (VERIFIED code, mode mapping INFERRED)

With `cb7[220].x == 0` (mode Off or Clipspace Scale), t96 is bound to black (t = 0, h = 0, so the term above is 0).
The PS instead takes the probe parallax hit point `X = sum_i w_i * boxHit_i` (L:759-761, 799: the per-probe box
intersection, or `P + 8096 R` without a box). It projects X into the previous frame and lerps the probe colour
toward the previous frame there:

```
P = lerp(P, t4.SampleLevel(s11, prevUV(X + cb7[128]), 3 - 3 g') * cb7[42].w, wF * edge)   // L:885-902
```

There is no depth test. Mode Off zeroes `cb7[129].x` (`ssrFade` is only used when `r_ssrMode != 0`), so nothing
shows. Here P is changed *before* specular occlusion, unlike the deferred modes.

## 5. Which materials (VERIFIED on backlot2)

- In the 793 backlot2 techsets, technique flag bit 0 of `TECHNIQUE_LIT_FORWARDPLUS_BITMASK` is set exactly on the
  sets whose **hexB last hex digit has bit 1 set**. That is 10 sets ending in `2` (world `tw`, layered `twc4`, model
  `mco`) and one glass set ending in `3`
  (`g/lit_3_lit_bar_ta1_1006_1002000000080403_...`).
  - Digits 0 (693 sets), 1 (72) and 5 (6) never have the flag.
  - In the game-test, this flag raises render feature 2 ("SSR", r_scene.cpp:1727). That feature keeps the resolved
    scene up to date.
  - The editor's `parseStockTechsetName` (`src/core/stock_decal.cpp:82`) tests `(b & 0xF) == 2`. That misses the
    `...3` glass case. `b & 2` matches the data.
- The SSR techsets differ from their plain twins:
  - Their depth prepass has the second render target (normal) and the mask UAV.
  - Their lit PS has t4 (code image 6) and t96. Example: `tw/..._1002000000000012_...` uses prepass PS `3c7457d3`
    and lit PS `8e763cd6`; the plain world set uses `01631f08` (research 95), which has no t96, t4 or `cb7[124..129]`.
- Speedball has about 1,455 SSR triangles: the puddle and wet blends (research 98 section 6).

## 6. Water and `r_ssrWaterTrace`

- `cs_ssr_trace_water(_hq)` runs after the lit transparents (rb_draw3d.cpp:2512-2522, INFERRED) when
  `viewInfo->ssrWaterTrace` is set.
  - It starts from the water surface depth (`waterFloatZ`, T1 slot 2, a gather of 2x2 depths).
  - It rebuilds the water normal from the depth gradient, choosing the smaller difference on each axis, then rotates
    it with `CB0[7..9]` (diff VERIFIED).
  - The march is the same. The result is kept as the "previous frame water trace" (persistent image 12).
- `r_ssrWaterTrace` appears in the exe's dvar name list (0x23A1CD8) next to the other four. No description or
  registration was found statically. The game-test sets `ssrWaterTrace = 0` (r_scene.cpp:5011). Its default in
  Replay is not determined.
- Water shaders were not read.

## 7. Dvars (Replay exe, VERIFIED)

| Dvar (hashed name) | Default | Range | Description string |
|---|---|---|---|
| `r_ssrMode` (`NNTTTQSRN`) | 2 = Deferred LQ | Off, Clipspace Scale, Deferred LQ, Deferred HQ | "Screen Space Reflection mode" |
| `r_ssrPositionCorrection` (`MPPMOTSLNN`) | 1.0 | -2 .. 2 | "...position correction blend factor" |
| `r_ssrFadeInDuration` (`LMTMRLMOT`) | 0.05 | 0.01 .. 1 | fade-in after the reflection source buffer is invalidated (dual-view scope transitions) |
| `r_ssrFadeInStrength` (`MLMMSONQMQ`) | 2.0 | 0 .. 10 | "Strength of the screen-space reflection fade-in." |
| `r_ssrWaterTrace` | ? | ? | name only (section 6) |

Meanings (INFERRED from the decompile and the shader):

- PositionCorrection scales the camera translation added before reprojection. 1 = exact reprojection of the
  translation; 0 = sample the previous frame as if the camera had not moved.
- FadeInDuration and FadeInStrength drive `cb7[129].x`: strength x fade x aspect, which is also the edge-fade
  sharpness.
- The float dvars have flags 0x44; `r_ssrMode` registers with flags 1 (the game-test used 0).

## 8. Recipe for the editor viewport

The editor draws forward into a linear HDR target (`hdrScene`, t15, then `ps_post`). Its depth is D24S8, readable
as R24 (`depthSrv_`) only outside the pass that writes it. The pixel normal exists only inside `ps_tex`. S is
computed in the `stockRef` branch of `ps_tex` (`S = lerp(0.18 * mixed, P, so)`).

### 8.1 Approach: trace the previous frame from inside `ps_tex`

Do the whole trace in the previous frame's screen space with the previous frame's depth and colour. Then:

- No pass has to read the depth buffer it is writing.
- No G-buffer or normal target is needed.
- Camera motion is exact by construction. The game reprojects only the colour lookup; here the trace itself runs in
  the frame where the colour lives.

The costs:

- Geometry that moved since the last frame is reflected one frame late, which a follow-up redraw fixes.
- Surfaces visible now but not in the last frame give misses, which fall back to the probe.

### 8.2 End-of-frame copies (after the last lit draw, before `ps_post`)

1. `prevColor`: R16G16B16A16_FLOAT, **half resolution, 4 mips** (the game's Resolved Scene). Make it with a
   downsample pass from the HDR target, then `GenerateMips` (box filter instead of the game's Gaussian: acceptable).
   Store linear radiance as drawn (the editor does not pre-expose, so the game's `cb7[42].w` factor is 1).
2. `prevZ`: R32_FLOAT linear view depth (clip w, game units) from `depthSrv_`, sky = 1e9. It can be full or half
   resolution, with 4-5 mips of the **max** depth (the game's floatZ chain), built in small passes.
3. Keep `prevViewProj` (row-major), `prevFwd` = `(prevViewProj[0][3], [1][3], [2][3])` (column w), `prevSize` and a
   `prevValid` flag in the game constant buffer.

### 8.3 In `ps_tex`

Do this only for techsets with the SSR bit (`hexB & 2`), only when probes are drawn for the material (the existing
`probeCount.x > 0.5` branch), and only for glossy pixels: the game's `fb < 1`, with `cb7[186]` unknown (research 95).
Use `g' >= 0.3` until it is measured.

```hlsl
// X = i.w (world), Ng = normalize(i.n) (vertex normal), R = reflect(-Vs, N) (normal-mapped), gp = g'
float2 ssrUV(float3 X) { float4 c = mul(float4(X, 1), prevViewProj); return c.xy / c.w * float2(0.5, -0.5) + 0.5; }
float  ssrW (float3 X) { return mul(float4(X, 1), prevViewProj).w; }
float wF = saturate(4 * dot(prevFwd, R));
float3 ssr = 0; float w = 0;
float3 Rg = reflect(-Vs, Ng);                                   // trace ray: the vertex normal, as the game (current eye)
// dot(Rg, -Vs) > 0: the game's "ray leaves the eye" test; dot(prevFwd, Rg) > 0 keeps the far end in front of the
// previous camera (w > 0)
if (prevValid && wF > 0 && dot(Rg, -Vs) > 0 && dot(prevFwd, Rg) > 0) {
    float3 A = float3(ssrUV(X), 1 / ssrW(X));
    float3 E = float3(ssrUV(X + 65500 * Rg), 1 / ssrW(X + 65500 * Rg));
    float3 D = E - A;
    float len = length(D.xy * prevSize);
    A.xy += D.xy * (1.0 / max(len, 1e-3));                      // one pixel out
    float zPrev = 1 / A.z, t = 1, j = 0.5;                        // fixed j: there is no TAA to remove noise
    bool hit = false;
    [loop] for (int k = 0; k < 24; ++k) {                         // the game's HQ count; 12 for LQ
        float tk = (k + j) / 24.0; tk *= tk;
        float3 Q = A + tk * D; float zr = 1 / Q.z;
        float zs = prevZ.SampleLevel(pointClamp, Q.xy, min(4.0, tk * len / 256)).r;
        hit = zs * 1.002 < zr && abs(zr - zs) < max(abs(zr - zPrev), 48.0) && tk > 1 / len
              && all(abs(Q.xy - 0.5) < 0.5);
        zPrev = zr;
        if (hit) { t = tk; break; }
    }
    if (hit && t < 0.9999) {
        float r = 1 - gp;
        float c = max(0, min(1.01737 * r * r * r + 0.070128, 1) * t + 1.48707 * r - 0.284052);
        float2 uvH = lerp(ssrUV(X), ssrUV(X + 65500 * R), t);  // reuse t on the normal-mapped ray
        float2 e = uvH * 2 - 1;
        float edge = saturate((1 - e.x * e.x) * (1 - e.y * e.y) * 2.0 * prevSize.x / prevSize.y);
        w = wF * edge;
        ssr = prevColor.SampleLevel(linearClamp, uvH, 3 * min(c, 1)).rgb;
    }
}
// replaces "+ Fenv * S" in c = ...:
c = diffAlbedo * indirect * kd * (1.0 - fmax) * aoMB + Fenv * lerp(S, ssr, w);
```

- Drop the game's random neighbour pick (the ±(24 - 16g') pixel lookup) and the `0.03 c` uv jitter. They need TAA.
  The cone mip still softens rough reflections.
- To match the game's distortion by the normal map more closely, the step offset `j` can follow a 4x4 Bayer pattern
  instead of 0.5, at the cost of fine noise.
- The cost is 24 depth taps plus 1 colour tap per SSR pixel, and only SSR materials pay it.

### 8.4 Camera motion and invalidation

- **Moving camera.** Nothing extra is needed, because the trace runs in the previous frame. This matches the game's
  default `r_ssrPositionCorrection = 1`.
- **Invalidate** (`prevValid = false`, which gives probe only) on:
  - the first frame, a resize or a FOV change;
  - a map load or a lighting-mode toggle;
  - a frame drawn without Game Lighting / HDR;
  - a camera jump larger than, say, 2,000 units (the game clears its resolved scene on a teleport).
- After an invalidation, or after an edit that changes geometry, request **one extra redraw** so that the next frame
  has a valid previous frame. The game's 50 ms fade-in can be skipped, because the editor draws on demand.
- The previous frame already contains the reflections, so repeated redraws converge to multi-bounce, as in game.
- Fog: the editor's HDR colour is fogged and the SSR term is added before the surface's own fog, so reflected
  geometry is fogged twice. The game's resolved scene is taken after its effects, so it likely does the same. Leave
  it.

### 8.5 Closer to the game, if wanted later

- Run the trace as its own pass between the existing depth pre-pass (drawn when AO is on) and the main pass. The
  prepass would write the vertex normal to a second target, as P:28-35 does. The pass writes `(t, hit)` at quarter
  or half resolution.
- `ps_tex` would then read t96-style data and reproject the colour as in section 3. That uses current-frame depth,
  so there is no one-frame lag.
- It needs the prepass on every SSR frame, a normal target and, for the neighbour sampling, a temporal filter. Not
  worth it before 8.3 is compared with in-game shots (Speedball puddles, Lit Preview and Game Lighting).

## What is not determined

- `cb7[186]` (the rough fallback, so the gloss cutoff for SSR) and the sampler states s5 / s7 / s10 / s11 (point or
  linear). The format of the SSR trace target and of floatZ (fp16 INFERRED from the 392999 = 6 x 65500 sky test).
- Whether floatZ's mips are the max chain (INFERRED from the min chain's separate target).
- Replay's render target sizes and frame order. They are game-test decompile flow; the shader factors 4 / 2 agree.
- `r_ssrWaterTrace`'s default and the water material side. The consumer of `t96.z` (confidence) and of the previous
  water trace (t97).
- `cb2[4]` and `cb2[8].x` values (clip-to-uv and resolution scale), and the exact `cb7[45]` (temporal parameters).
- The SSR glass and model lit shaders were not disassembled. They are INFERRED to follow the world shader.
