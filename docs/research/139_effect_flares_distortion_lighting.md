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
