# 139. Effect preview: flares, distortion and per-particle light

Research 138 left three things out of the effect preview: flares (element 3, the `ecf/` sets), distortion (the
`dist_10` sets, heat haze) and the lighting of lit particles (one map-wide light). This note records what the data
says about each, what the preview now does, and what is still inferred. Labels: VERIFIED (read in the data or the
fork's decoded shader), INFERRED (fits the data, not read in the game's code).

IDA was not reachable while this was written (after a restart of the owner's machine). `ParticleEmitter_BuildSpriteVertices`
(0x141BA5920 in the Replay base exe, research 138 section 12) was not read. Each INFERRED rule below can be settled
there.

## 1. Flares

### 1.1 The data (VERIFIED)

`vfx/iw8_mp/level/shipment/vfx_shipment_sunflare` (copied to `tests/data/vfx_sim/vfx_shipment_sunflare.12.asset.json`)
has 6 emitters. Each is one flare state (`elementType` 3) with INIT_SPAWN, INIT_ATTRIBUTES, INIT_FLARE, INIT_MATERIAL and
INIT_OCCLUSION_QUERY, and an EMISSIVE_GRAPH. Each emitter has flags 0x200400 and countMax 1.

| Emitter | Material | Size | m_position | Radial scale x / y | Notes |
|---|---|---|---|---|---|
| 0 | `ecf/vfx_postfx_dirty_lens_main_1` | 70 x 70 | 1 | 1-1.5 / 1-1.5 | m_direction (1, 0, 0) |
| 1 | `ecf/vfx_postfx_flare_anamorphic_3` | 200 x 18 | 0.9 | 1 / 0.5 | m_flareFlags 4 |
| 2 | `ecf/vfx_postfx_ray_1` | 35 x 35 | 1 | 1-3 / 1-2 | m_angularRotCount -1, m_radialRot 0.1 .. -0.2 |
| 3 | `ecf/vfx_postfx_flare_streaks_hard_1` | 35 | 1 | 1-2 / 1-2 | m_angularRotCount -1 |
| 4 | `ecf/vfx_postfx_glow_1` | 10 | 1 | 1-2 / 1-2 | |
| 5 | `ecf/vfx_postfx_flare_streaks_hard_2` | 49 | 1 | 1 / 1 | |

- Every emitter has `m_depthScaleRange` 0 .. 280 and `m_depthScaleValue` 0 .. 1.
- Every emitter has the occlusion query `m_scale` 1.5, `m_worldSize` (1000, 1000), `m_depthBias` 0 and
  `m_offscreenRaycast` true.
- INIT_FLARE carries four curves (scale 1 each):
  - curves 0 and 1 have 3 to 9 points, symmetric about 0.5 with their least value there (0.449 for the lens, 0 for
    the anamorphic streak and the rays);
  - curve 2 is 1 or 0 throughout;
  - curve 3 is 1 throughout, or 0 to 1 on the anamorphic streak.
- The ranges are lerp ends: the ray's radial rotation is stored 0.1 then -0.2. The reader keeps that order
  (`readEnds`), while random ranges are ordered (`readRange`).
- The `ecf/` materials are unlit effect sets (`unlit_6_effect_bad_ta0_802_..._100020023`). Their image is in texture
  slot 18 and constant 36 is 1.
- Shipment places the sun flare more than once (near the ground, e.g. (74.8, 121.4, 35.1) and (217.6, 1952.7, 119.5)),
  each with angles 0 0 0 and delay -4000 ms.

### 1.2 Where a flare's source is

- **Sun flares: INFERRED.** The placements say nothing about the sun: they sit near the ground with angles 0. A source at
  the placement would be hidden by the world almost everywhere. The x64-zt converter's notes treat sun flares as their own
  kind (`export_effects.cpp:573-605`: sun flares and ghosts stay Replay flares, glares at their source become billboards).
  So the preview takes the sun as the source of an effect whose name holds `sunflare` / `sun_flare`: far along the sun's
  direction from the eye. One placement per such effect draws; each would put its pieces on the same spot.
- **Other flares (glares, ghosts):** the source is their effect's position.

### 1.3 The screen layout (`vfxFlareSprite`, INFERRED)

From the converter's notes (`m_position` 1 = at the source; other values are ghosts along the line from the source
through the screen's centre; the radial scales size the piece at its source; `m_depthScaleValue` applies when the
depth-scale range is set) and the shapes of the curves:

- **Centre** = source (NDC) x `m_position`.
- **Radial distance:** r = the source's distance from the screen's centre in screen heights, 1 at a corner. The curves'
  parameter is t = 0.5 + 0.5 r, because the stock curves are symmetric about 0.5.
- **Size:**
  - half height = size.y / 100 x lerp(scaleY.min, scaleY.max, curve1(t)) x depth scale;
  - half width = the same with size.x, scaleX and curve 0, divided by the aspect ratio.
  - So the attribute size reads as percent of the screen height: a 200 x 18 anamorphic streak spans the screen at 18 %
    of its height, which is what such a streak looks like.
- **Depth scale** = lerp(value.min, value.max, curve3((distance - range.min) / (range.max - range.min))). A sun at
  infinity gets the full size.
- **Angle** = lerp(radialRot.min, radialRot.max, curve2(t)), plus the source's angle about the centre when
  `m_angularRotCount` is not 0, so that rays turn toward the centre.
- **Not used yet:** `m_direction`, `m_flareFlags` (4 on the anamorphic streak; it stays horizontal because its rotation
  count is 0) and the emitter flags.

### 1.4 Occlusion (INFERRED from Replay's `cs_flare_occlusion`, a 3x3 screen mask at the source)

- **Test:** the share of a 3x3 grid over the source's occlusion area (`m_worldSize` x `m_scale` seen at the source, at
  least a pixel) where the scene's depth is not nearer than the source. The flare's colour is multiplied by that share.
- **Sky box:** the editor's sky box is drawn 60,000 units out and writes depth, so the source depth used by the test is
  clamped to 50,000 units: the world hides a sun at infinity, the sky does not.
- **Off screen:** a source off the screen draws nothing (the game's off-screen ray cast is not modelled).

### 1.5 Drawing

- Flares are screen-space quads in their own pass after the distortion, without the depth buffer bound: the effect state
  of these sets draws over the world.
- The vertex shader reads the depth texture for the occlusion test.
- They use the unlit sets' additive output (the emissive image x the particle colour x constant 36).
- `IW8R_NO_FLARE_OCCLUSION` turns the test off, for comparisons.

## 2. Distortion

### 2.1 The rule

Research 132 G13 decoded the world distortion pixel shader (VERIFIED). The preview applies that rule to effect quads:
- k = saturate(m.z a - 0.25) x 4/3, where m is the slot 157 image and a the vertex alpha;
- D = c127.x k (2 m.x - 1) T + c127.y k (2 m.y - 1) B, where T and B are the quad's unit tangent and bitangent;
- the pixel shows the scene drawn so far at its world point + D, x the vertex colour, with alpha saturate(4 m.z a).

The core function is `vfxDistortion` (tests: `vfx_distortion_follows_the_world_rule`). The effect pixel shader itself was
not disassembled: INFERRED that it is the same.

### 2.2 The mask channel (VERIFIED in the data; the reading is INFERRED)

The world image `water_wall_waves_dist` is BC1 sRGB (DXGI 72): no alpha, so the mask is in blue. The effect image
`vfx_dist_heatshimmer_16f_loop` is BC3 sRGB (DXGI 78). Over all of its 4,096 blocks:
- the blue endpoints are 0;
- the alpha endpoints average 76 and reach 246;
- red and green average 136 and 118 (offsets about 0.5).

With blue as the mask, the effect would never move the scene. The loader therefore moves alpha into blue when blue is
empty and alpha varies, so the shader stays as decoded. The RGB channels of an sRGB image are read linear, as the
game's sampler does.

### 2.3 The material

- Texture slot 157 and constant 127 (xy, inches) come from the material cache since constRules 9; distortion sets
  resolve again once. The atlas is constant 94 (`vfx_dist_heatshimmer_heavy`: 4 x 4).
- Stock values: `elcq/vfx_dist_heatshimmer_heavy` c127 = (2, 2); `twc/dub_water_wall_waves_dist` (1, 1). Research 132
  lists 2 to 30 for the effect sets.

### 2.4 Drawing

- Distortion batches draw after every other particle (sortKey 36 after the effects' 35). The target drawn so far (the
  HDR scene, or the frame) is copied once, and each quad shows the copy at the moved point, blended by its alpha.
- `IW8R_NO_DISTORTION` turns the pass off, and `IW8R_DIST_DEBUG` shows the image as it is read.

### 2.5 Seen (bench)

- Shipment places no distortion effect (research 138 section 6). Hideout's tandoor (`vfx_hide_tandori`, material
  `vfx_dist_heatshimmer_heavy`) moves the tile wall behind it by a few pixels at 70 units: 32,433 pixels differ by more
  than 8, at most by 164.
- Before the alpha-to-blue step the difference was at most 2: the mask was read from the empty blue channel.

## 3. Per-particle light

- **What is known (research 138):** lit particles take per-particle lighting from the scene, which the game computes per
  particle (PS a9864c07); unlit ones do not.
- **What the preview does (INFERRED shares):** in Game Lighting a lit sprite takes
  - the stock light grid at its centre, averaged over all directions (a particle has no normal): `stockLgvAverage` =
    0.282095 x dc (the L1 and L2 terms average to 0; test `stock_lgv_average_is_the_mean_over_directions`), x the map's
    baked scale;
  - plus the sun's share (0.15 + 0.2 x the sun's height, as before), shadowed at the particle's centre by the scene's sun
    shadow map (3x3 PCF in the vertex shader, near cascade first). One value lights the whole sprite.
- **Fallbacks:** without a grid (own maps) the map-wide ambient replaces the grid's light. `IW8R_NO_PARTICLE_LIGHT`
  restores the one map-wide light (sky + the sun's share), for comparisons.
- **Seen:** on Shipment's smoke stack, 261 of 368 particles took the grid; the plume darkens where it passes through
  the shadowed side of the stack.

## 4. Not done

- Models (element 7), runners (9: child effects), lights (5, 6), decals (2) and vector fields (11) are still not drawn.
- Flares: `m_direction`, `m_flareFlags`, the off-screen ray cast and the occlusion fade times are not modelled.
- Distortion: the effect pixel shader was not disassembled.
- All of these can be read in IDA: `ParticleEmitter_BuildSpriteVertices` and the `ecf/` / `elcq/dist_10` shaders.

## 5. The lamp glare: dirty lens pieces, alpha and the emissive graph (2026-11-15)

Owner report (Hideout, Game Lighting, Effect Preview): the glare of the lights was "square-like" and spun quickly when
a lamp was passed or seen at some angles.

### 5.1 Cause (seen in the data)

- Hideout places 20 `vfx_hide_fluorescent_lf` (one flare piece, `ecf/vfx_postfx_dirty_lens_main_1`, size 100, alpha 1)
  and 5 `vfx_hide_bulb_light_flare` (a dirty lens piece of size 70 with `m_angularRotCount` 10, a rainbow glow, a
  rainbow ring of alpha 0.027 and a soft streak billboard).
- The dirty lens set's slot 18 image is `vfx_postfx_screen_grime_1_e` (2048 x 1024, BC1 sRGB): lens grime over the whole
  frame. Its slot 145 image is `vfx_postfx_glow_round_3_e`, a round glow. The preview drew only slot 18 on the piece's
  square quad: a square of grime up to the height of the screen, 8x (fluorescent) to 36x (ring) too bright because the
  alpha and the emissive graph were not used. With the angular rotation the square turned with the source's angle about
  the screen's centre, which sweeps half a turn when a lamp passes near the middle of the view.

### 5.2 The pixel shaders (VERIFIED, fxc /dumpbin of the exported techsets in `out\scriptable_study\_ts\techsets_common`)

- `ecf/unlit_6_effect_bad_ta0_802_80004_...` (dirty lens) TECHNIQUE_EMISSIVE: PS `3ce5dd9d...`, VS `6b28c7d9...`. Its
  arguments: material texture 18 -> t1, material texture 145 -> t3, material constant 36 -> cb3[2]. The PS:
  - mask = t3.r at the quad's UV (TEXCOORD0.zw, which the VS sets to the vertex UV);
  - a = saturate(visibility x vertex alpha x mask), discard when not above 0;
  - t1 is read at the vertex UV, or, when bit 256 of the emitter's GPU flags (code buffer 49, offset 188) is set, at the
    screen's UV fitted to 16:9 (the middle of a wider image: v' = v x (16/9) / aspect + (1 - (16/9) / aspect) / 2 when the
    frame is wider, the same on u when narrower);
  - colour = t1 x const 36 x exposure x emissive rgb x (visibility x intensity + a second term) x a.
- `ecf/unlit_6_effect_bad_ta0_802_4_...` (the other flares) is the same without t3; `elcq/unlit_6_effect_bad_ta0_802_4_...`
  (unlit effect quads) multiplies by saturate(vertex alpha) too and reads only the alpha of the particle colour: its RGB
  comes from a second packed vertex colour.
- INFERRED: that packed colour is the particle colour x the emissive graph (curves 0-2 RGB x curve 3 intensity: a fire
  such as `vfx_veh_fire_linger_med` has an orange colour graph and a white emissive graph of up to 4.5); that bit 256 is set for the
  dirty lens pieces (the grime image is a full-frame 2:1 picture; the material differs from the other flares only by
  slot 145, sortKey 29 and surface flag 0x8000000). Where the game builds flare vertices was not found in IDA
  (`RBT_Flare_Draw` only binds tables; the pieces are built on the GPU), so the size and turn rules of section 1.3 stay
  INFERRED.

### 5.3 What the preview does

- `vfx_sim`: reads `emissiveGraph` (5 curves per set); `VfxParticle::emissive` = RGB x intensity.
- Unlit sprites and flares add the particle colour x the emissive graph's colour when the emitter has one (else the
  particle colour, as before), x saturate(alpha); flares x saturate(visibility x alpha). `IW8R_OLD_FLARES` draws the
  flare pieces as before (no mask, turned, alpha 1, no emissive graph) for comparisons.
- A flare set with a slot 145 image (`ParticleMaterial::screenMask`): slot 18 at the screen's UV (16:9 fit), x the
  slot 145 red channel (read linear) at the quad's UV; the piece is not turned (a round mask shows no turn; the grime
  stays on the screen). The cache does not export slot 145: the image is taken from the material cache when another
  material cached it (Hideout: the glow flares' `glow_round_3_e`), else pow(saturate(1 - 2r), 3) stands in (fitted to
  that image read linear: mean error 0.011).
- Seen (bench, Hideout, `fx fluorescent_lf 0 220 180 -20 90`, occlusion test off): before, rotated squares of grime and
  full-strength rainbow rings added up to 90 levels over 410,000 pixels; now round glows and faint rings add at most 10
  levels. How bright the game draws them also depends on its exposure factor for effects (cb7[5].x) and a fade by the
  piece's brightness in EV in the vertex shader, neither modelled.
- Replay in the preview: a one-shot effect plays again every `delay` seconds of its fx_origin, else after its run plus
  1.5 s; a looping effect runs on (it used to restart every 60 s, all its particles vanishing at once).

### 5.4 No plain squares (owner: "some vfx still show as squares")

- Cause: a particle whose material the cache was still resolving, or whose image was still being read, was drawn on a
  plain white quad (lit sets: a grey square in the scene's light), and a flare piece without its image on a white
  screen square. Seen on the compact showcase (row 8) with the bench's cache, where the fire and smoke materials were
  not resolved: stacked grey squares; now nothing until they are.
- Now: nothing is drawn until the material and its images are in (`particleTexture` returns -2 while reading); a
  material without any image draws a soft round sprite of the particle's colour; a sprite image with no edge of its
  own (lit: no slot 27 / 68 opacity read and an opaque colour image; unlit: bright up to its cells' borders) gets a soft
  round edge per atlas cell (logged once per material, "Effect preview: ... drawn with a soft round edge"; none of
  the sets drawn on the Hideout tandoor bench or the showcase needed it). `IW8R_PLAIN_PARTICLE_QUADS` restores the old
  fallback for comparisons.
