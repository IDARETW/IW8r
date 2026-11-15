# 138. Particle preview: what the stock effect data says about how particles look

Scope: research for drawing a stock map's placed effects in the viewport the way the game does (roadmap 8.1, research 132
G3). Read-only. Nothing was built, run in game, or committed. The IDA bridge was tried twice and was unavailable ("bridge
worker could not start"), so nothing here comes from the exe.

Sources and short names used below:

- **Exports**: the 440 `*.asset.json` under `%LOCALAPPDATA%\IW8 Radiant Suite\IW8 Radiant Suite\x64zt_output\`
  `_stock_cache\mp_shipment-0507CF8E24D3\mw19replay\mp_shipment\assets\vfx\vfx\` (with `$native` bytes), and
  **vfx_cache**: the 13 exports the editor made for Shipment's placed list (`...\IW8 Radiant Suite\vfx_cache\mp_shipment\`,
  no `$native` keys, same field names). Surveyed with read-only Python scripts (session scratchpad, not kept).
- **Schema**: `E:\mw124\atian-cod-tools\config\data\mw19\schema.json` (line numbers of the struct names).
- **vfx.cpp**: `E:\mw124\x64-zt\src\games\iw8\neutral\vfx.cpp`. It is generated (line 1: "Emitted by
  tools/gen_iw8_neutral.py"): a field-by-field copy of each struct. It confirms the layout but holds no semantics.
- **IW3 conv**: `E:\mw124\x64-zt\src\convert\iw3_iw8\iw3_fastfile.cpp` and `iw3_render_assets.cpp`, the fork's IW3 to
  Replay converter. It *writes* Replay modules from IW3 effects whose meaning is known, and its comments record owner
  game tests (2026-10-14). **IW7 conv**: `E:\mw124\x64-zt\src\convert\iw7_iw8\export_effects.cpp`. These are the best
  semantic evidence available; the semantics they encode are labelled INFERRED (game-tested by the fork) unless the
  data alone proves them.
- **Material exports**: ACTS `Material` exports of 72 effect materials in `out\agent\toon_ship\mat_techsets_mp_shipment\`
  and `out\agent_techset\all\...\techsets_mp_emporium\assets\material\`; **techset exports**: 175 distinct
  `unlit_6_effect_bad_*` technique sets (plus distortion sets) in `out\agent\toon_ship\ts`,
  `out\agent\shader_research\allts`, `out\agent\post109\mt`, `out\scriptable_study\_ts`.

Labels: **VERIFIED** = read from the data or the schema (counts given), **INFERRED** = from the fork's converters, names
or a consistent pattern, **NOT DETERMINED** = open (section 12).

## Summary

1. A state holds exactly 3 module groups (vfx.cpp:499 `read_fixed_with<3>`): group 0 = init modules, group 1 = update
   modules (graphs, forces), group 2 = test modules (child effects on impact/death/age). VERIFIED (survey: every init
   type only in g0, every graph only in g1, every test only in g2).
2. Match modules on `moduleType.value` or `moduleData.active_member`, not `moduleType.name`: the ACTS names are the first
   enum alias (value 0 prints `PARTICLE_MODULE_INIT_BEGIN` = atlas, 24 `..._SPAWN_SHAPE_FIRST` = box; relative velocity
   type 3 prints `..._TYPE_COUNT` = LOCAL_WITH_BOLT_INFO). VERIFIED (schema 19777-19853, 21548-21560).
3. A curve value is `lerp(controlPoints by time) * scale`; times run 0..1 over the particle's life; `invTimeDelta` is
   `1/(t_i - t_{i-1})`. VERIFIED from the data (curve times, invTimeDelta) and the fork's writer (VfxCurve, iw3 conv 8613).
4. Every graph stores two curve sets (A = first half, B = second half). `m_flags & 0x10` = pick per particle between
   A and B; clear = set A only. INFERRED.
5. `firstCurve` true (97 % of colour/size graphs): the graph value replaces the attribute; false: it multiplies the value
   from INIT_ATTRIBUTES. INFERRED (IW7 conv 757-764, an in-game observation).
6. Colour graph = 8 curves (RGBA x 2); size graph = 6 (x, y, z x 2), x = width, y = height (or tail length); velocity
   graph = 6 (xyz x 2) in inches/s, `m_flags & 0x80` = world axes. INFERRED (IW3 conv 8744-8926).
7. Emitter flag `0x2` is burst mode: countMax == burst.max on all 870 emitters with it, and no continuous emitter has it.
   VERIFIED (data) + IW7 name `USE_BURST_MODE` (x64-zt `iw7\structs.hpp:8730`). Without it the emitter spawns
   `particleSpawnRate` per second for `emitterLife` (0 = as long as the effect runs). `vfx_sim`'s looping rule (0x8/0x10)
   is wrong for the placed Shipment effects, which have neither bit.
8. Sprite atlas: rows/columns are Material fields `textureAtlasRowCount` / `textureAtlasColumnCount` (schema 8893/8899);
   constant 94 = (columns, rows, 1/columns, 1/rows) on 53/53 atlas materials; `packedAtlasDataSize` is 0 on all 80
   exports (uniform grids). VERIFIED. Frame choice is in INIT_ATLAS: `m_startFrame`, `m_loopCount`, `m_randomIndex`,
   `m_playOverLife`, two curves (frame position over life, or frames per second). INFERRED.
9. 174 of 175 exported effect technique sets share one fixed blend (`defaultStateBits.blendBits` 163920 = 0x28050).
   The pixel shader decides: unlit sets (feature hex ending `23`, only the emissive slot 18) write alpha 0 and add; lit
   sets (ending `b`, albedo slot 0) write premultiplied colour and alpha. VERIFIED (blend bits) + INFERRED (shader
   behaviour, fork disassembly notes, iw3_render_assets.cpp:8919-8934). The token `tca`/`ta0` does not decide additive.
10. Distortion effect materials are `dist_10_distortion` (slot 157, constant 127 = scale, sortKey 36): 5 materials in 14
    of the 440 effects, none of them on Shipment's placed list. Skip them in a first port.
11. Orientation comes from `ParticleStateDef.elementType`: 0 billboard (645 states), 10 tail / velocity-aligned (131),
    8 oriented sprite with `m_orientationQuat` (53), 3 flare (18), 7 model (237), 9 runner = child effect (91), 5/6 lights,
    2 decal, 11 vector field. INFERRED names (IW7 conv 76-82 + IW3 conv 9127-9190), VERIFIED counts.
12. Spawn shapes: box (458 modules; `m_offset` is always the centre of `m_dimensionsMin/Max`, 458/458 VERIFIED), sphere
    (240, `m_radius` range), cylinder (179, `m_radius`, `m_halfHeight`, `m_normalAxis`), ellipsoid (6). The shape base
    sits one level deeper (`value."".""`).
13. Shipment's 13 placed effects need only: billboards and tails, box/sphere/cylinder spawn, INIT_ATTRIBUTES, colour/
    size/velocity/rotation graphs, force, gravity, drag, atlas, mirror, and the lit and unlit blend classes. Skippable:
    the seagull runner, the drips' impact test, the 6-emitter sun flare.
14. The placed effect's orientation matters (most velocity graphs are in effect axes), but `CameraView::StockEffect`
    keeps only the origin; `Iw8PlacedEffect` (`src/core/iw8_mapents.h:29-35`) has the angles.

## 1. Layout and names

`asset.fields` = ParticleSystemDef (schema 17672). `emitterDefs.values[i]` = ParticleEmitterDef (17954, 160 bytes;
vfx.cpp:384-412). `stateDefs.values[s]` = ParticleStateDef (18285: `moduleGroupDefs`, `elementType`, `flags` u64;
vfx.cpp:495-504). `moduleGroupDefs.values[g]` (18322: `moduleDefs`, `numModules`, `disabled`). `moduleDefs.values[m]` =
ParticleModuleDef (18353, 240 bytes): `moduleType {value, name}` and `moduleData {active_member, value}` (union 18378;
dispatch vfx.cpp:2692). Inside `value`, the key `""` is the ParticleModule base `{m_type, m_flags}` (19758); for spawn
shapes `value.""` is the shape base and `value."".""` the module base. Vectors are `<field>.v.m128_f32[0..3]`; the
material-data vectors are `<field>.v` plus `__s1`/`__s2` views. Ranges are `{min, max}` (18105). Emitter
`gravityOptions` is `PARTICLE_GRAVITY_OPTION_NONE` on all 1251 emitters.

## 2. Colour, size and alpha over life (question 1)

**Curves** (ParticleCurveDef 18198, control point 18229; vfx.cpp:354-361): `m_curves[k].controlPoints.values[] =
{time, value, invTimeDelta}`, `numControlPoints`, `scale`. Evaluate (INFERRED linear; `invTimeDelta` VERIFIED, e.g.
`vfx_shipment_distant_fog` spawn curve: times 0, 0.125, 0.25, 0.85, 1 with invTimeDelta 0, 8, 8, 1.667, 6.667):

```
f(t): t <= t0 -> v0; t >= tn -> vn; else i with t_{i-1} <= t <= t_i:
      v_{i-1} + (t - t_{i-1}) * invTimeDelta_i * (v_i - v_{i-1});   result * scale
```

The fork's writer stores `value / scale` with `scale = max |value|` (iw3 conv 8613-8643). Curve times start at 0 and end
at 1 on 11,479 of 11,538 graph curves (the rest start up to 0.188 or end from 0.966; hold the end values outside): t =
age / life.

**Graph modules** (all in group 1; struct and vfx.cpp reader line):

| Module (value) | Schema | vfx.cpp | Curves: set A / set B | Other fields |
|---|---|---|---|---|
| colorGraph (34) | 20667 | 1807 | 0-3 RGBA / 4-7 RGBA | `firstCurve`, `m_modulateColorByAlpha` |
| sizeGraph (51) | 20102 | 2347 | 0-2 xyz / 3-5 xyz | `firstCurve`, `m_sizeBegin`, `m_sizeEnd` |
| intensityGraph (42) | 20429 | 2038 | 0 / 1 | `firstCurve` |
| emissiveGraph (37) | 20573 | 1896 | 0-2 rgb, 3 intensity, 4 extra / 5-9 | `firstCurve` (IW7 conv 1367-1374) |
| velocityGraph (53) | 20027 | 2408 | 0-2 xyz / 3-5 xyz | `m_velocityBegin`, `m_velocityEnd` |
| rotationGraph (48) | 20225 | 2246 | 0 / 1 | `m_useRotationRate` |
| forceDragGraph (40) | 20479 | 1985 | 0 / 1 | |
| scaleByDistance (50) | 20145 | - | 0 / 1 | `m_maxDistance`, `m_scaleBegin/End` |
| sizeLerp (52), colorLerp (35) | 20071, 20636 | 2383 | none | `m_sizeBegin/End`, `m_colorBegin/End` (float4) |

- Channel order RGBA: INFERRED (IW3 conv 8860-8867 writes IW3's BGRA bytes as Replay R, G, B, A; the alpha curve 3 is
  the fade-in/out in every Shipment smoke). Size x/y: x = width, y = height (`vfx_shipment_distant_fog` x 1066 / y 535,
  a wide fog sheet); z is the model scale (IW3 conv 8869-8892), unused by sprites. INFERRED.
- Two sets: `m_flags & 0x10` is set on 454 size graphs (449 with differing sets) and clear on 100 (93 of them with
  differing sets anyway). Rule: 0x10 = per-particle random lerp A..B, clear = A only. INFERRED (the IW3 conv sets
  0x10 only when the sets differ, 8808-8809, 8903-8904, 8921-8922; `vfx_shipment_core_vapor` emitter 1 has 0x10 clear
  and set B = the default white 1).
- `firstCurve`: colour 480 true / 30 false, size 546/8, intensity 65/27, emissive 191/5. True = the graph writes the
  value; false = it multiplies the particle's value (IW7 conv 757-764: without it "a size graph squared IW7's start
  size", an observed game result). INFERRED.
- `m_flags & 0x1000`: exactly the 30 + 1 size graphs and 7 + 1 velocity graphs with non-zero `m_sizeBegin` /
  `m_velocityBegin`, which then equal set A at t = 0 and 1 (constant graphs). VERIFIED correlation; meaning INFERRED
  (precomputed lerp). A port can always evaluate the curves.
- Intensity: multiplies the colour as an HDR scale (e.g. `vfx_shipment_smoke_stack` emitter 1: 5 at birth to 1).
  INFERRED; it cannot be shown in an 8-bit vertex colour (section 11).

Real values (`vfx_shipment_distant_fog`, vfx_cache `.9.asset.json`; same numbers in the stock cache `.441`): size
graph flags 16, firstCurve true, set A x = 1066.284 / 1140.642 / 1215.0 at t = 0 / 0.5 / 1, set B x = 861.192 /
953.532 / 1045.872; colour set A alpha 0 / 0.5297 / 0, RGB 0.9098; set B RGB 0.6902, alpha peak 0.3213.

## 3. Velocity, gravity, drag, rotation (question 2)

Effect space is Z-up, inches, seconds (INFERRED; the IW3 conv multiplies IW3's units-per-ms velocities by 1000, 8758).

- **INIT_ATTRIBUTES** (1; schema 22639; vfx.cpp:593): `m_sizeMin/Max`, `m_colorMin/Max`, `m_velocityMin/Max` (float4)
  and `m_useNonUniformInterpolationForColor/Size` (false on 1176/1180). In 1180 modules, 429 effects. Missing in some
  states (`vfx_shipment_distant_fog` has none); defaults then apply. One random per vector for size and colour when the
  non-uniform flag is false, per component for velocity: INFERRED.
- **initRelativeVelocity** (18; 21524; vfx.cpp:1244): `m_velocityType` value 0 LOCAL (233), 1 WORLD (3), 2
  RELATIVE_TO_EFFECT_ORIGIN (149), 3 LOCAL_WITH_BOLT_INFO (430), 5 ..._ORIGIN_WITH_BOLT_INFO (18); `m_useBoltInfo`.
  Local = effect axes; bolt info = the axes of the tag the effect plays on (same as local for a placed effect). Radial
  (2) = velocity along a frame whose X points from the origin to the spawn point: INFERRED from the name.
- **velocityGraph**: inches/s per axis over life, flag 0x80 = world axes, else effect axes (IW3 conv 8748-8750, 8796:
  "graph flag 0x80 marks the world one"). `vfx_shipment_distant_fog`: flags 144 (0x80 + 0x10), world x 0..10, y
  -45..-33.4, z 0..20. How it combines with the INIT_ATTRIBUTES velocity (sum or replace): NOT DETERMINED.
- **gravity** (41; 20460; vfx.cpp:2015): `m_gravityPercentage {min,max}`, a fraction of world gravity pulling -Z world.
  652 modules (1.0 on 414). World gravity 800 in/s^2 (the engine's g_gravity default): INFERRED.
- **force** (39; 20504; vfx.cpp:1960): `m_forceMin/Max` float4, a constant acceleration in in/s^2; flags 0x80 = world
  (drips: `m_forceMin` z -250 with flags 128; smoke stack x 60..80 without it, a drift along the effect's X). INFERRED.
- **forceDragGraph**: drag coefficient over life (core vapor: 5 and 15). Form `dv/dt = -k v`: INFERRED.
- **Rotation** (2D sprites): `initRotation` (19; 21493; vfx.cpp:1268) `m_rotationAngle`, `m_rotationRate` ranges in
  radians and rad/s (values like +-0.1745 = 10 degrees, 0..6.2832). `rotationGraph` with `m_useRotationRate` true (all
  55 in Shipment) = rate over life, rad/s (smoke stack set A 0.611 -> 0.127). INFERRED units. `initRotation3D` /
  `rotationGraph3D` are for models.

## 4. Spawn shape (question 3)

Base ParticleModuleInitSpawnShape (schema 20982; vfx.cpp:1446) at `value.""`: `m_axisFlags` (63 ALL on 809, 61, 31, 15,
14, 62 seen), `m_spawnFlags` (1 = ONLY_ON_SURFACE on 59, 2 USE_CURVE_VALUES, 8 IS_DETERMINISTIC, 16 UNIFORM; 21058),
`m_normalAxis` (X/Y/Z), `m_spawnType` (21036), `m_volumeCubeRoot`, `m_calculationOffset`, `m_offset` (float4).

| Module (value) | Schema / vfx.cpp | Fields | Count (modules, effects) |
|---|---|---|---|
| initSpawnShapeBox (24) | 21258 / 1480 | `m_dimensionsMin/Max` (absolute: `m_offset` = their midpoint, 458/458) | 458, 288 |
| initSpawnShapeSphere (28) | 20944 / 1626 | `m_radius {min,max}` (min 0 on 204/240) | 240, 69 |
| initSpawnShapeCylinder (25) | 21202 / 1516 | `m_radius`, `m_halfHeight`, `m_hasRotation`, `m_directionQuat`; axis = `m_normalAxis` | 179, 75 |
| initSpawnShapeEllipsoid (26) | 21171 | `m_radiusMin/Max` float4 | 6, 2 |
| mesh (27) | 21090 | not in the 440 | 0 |

The shape curves all have scale 0 in Shipment (they apply with USE_CURVE_VALUES, never set here). Axis flags: a set bit
allows that half-axis (POS_X 1, NEG_X 2, ...; 21071), so 61 = no NEG_X. Sphere: random direction, radius uniform in
`[min, max]`, `max` only with ONLY_ON_SURFACE. INFERRED. Real: `vfx_shipment_distant_fog` box offset (-60, 0, 100),
dims (-760, -700, -100)..(640, 700, 300); `vfx_shipment_smoke_stack` emitter 0 sphere radius 0..50 at (300, 0, 200).

`initSpawn` (23; 21308; vfx.cpp:1416), in every state, holds one curve. 490 copy one shape (0 to 0.1, ramp to 1 at
0.25, 1 to 0.75, 0 at 1) and 476 another (1 to 0.75, 0 at 1). Its axis is NOT DETERMINED (a distance fade against
`fadeOutMaxDistance` fits `vfx_shipment_distant_fog`, 14000, but is unproven). The IW3 conv writes a constant 1 (8662).
Preview: ignore it.

## 5. Sprite atlas and flipbook (question 4)

- **Material** (schema 8810): `textureAtlasRowCount` (8893) and `textureAtlasColumnCount` (8899), u8; `packedAtlasData`
  null on all 80 effect exports. Constant 94 = (columns, rows, 1/columns, 1/rows): e.g. `elcq/vfx_animal_insect_fly_loop_1`
  rows 2, cols 1, c94 (1, 2, 1, 0.5); `elcq/vfx_spark_flame_anim` rows 1, cols 4, c94 (4, 1, 0.25, 1). VERIFIED 53/53.
- **Techset**: the second hex token (`unlit_6_effect_bad_ta0_802_<hex2>_...`) has bit 0x1000 exactly on the atlas
  materials (69/69 VERIFIED) and bit 0x2000 on frame-blended ones (17; the VS passes the next frame and a fraction, the
  PS mixes them: iw3_render_assets.cpp:4626-4630, INFERRED).
- **initAtlas** (0; schema 22706; vfx.cpp:555-567): `m_startFrame`, `m_loopCount`, `m_randomIndex`, `m_playOverLife`,
  `m_curves[2]`, `m_flags & 0x10` (random between the curves). 530 modules, 150 effects. Patterns (all 530):

| startFrame, loopCount, randomIndex, playOverLife | Count | Curves | Reading (INFERRED) |
|---|---|---|---|
| 0, -1, true, false | 212 | fps x scale (1 x 0 = still, 20/17.5, 120 decaying...) | random start frame, loop at fps(t) |
| 0, 1, false, true | 169 | ramp 0 -> 1 (scale 1), some eased | frame = curve(t) x frame count |
| 0, 1, true, false | 51 | mostly scale 0 | random still frame |
| 5, 1, false, true | 36 | ramp | as above from frame 5 |
| 0, 10, true, false | 20 | fps with a step (0 until t = 0.42, then 60) | delayed loop, 10 times |
| others | 42 | | |

  Reading: with `m_playOverLife`, position p = c(t) in 0..1 (c = curve 0, or lerp(curve 0, curve 1, r) with 0x10) and
  frame = startFrame + floor(p x (N - startFrame)) clamped to N - 1. Otherwise fps(t) = c(t) (curve x scale),
  frame = start + floor(integral of fps), start random in 0..N-1 when `m_randomIndex`; `m_loopCount` -1 wraps forever,
  k > 0 wraps k times then holds the last frame. The IW7 conv stores IW7's single play rate in both curves (768-785),
  which supports "curve = frames per second" when `m_playOverLife` is false.
- **UVs** (INFERRED row-major, row 0 at the top): col = f % cols, row = f / cols,
  `u = (col + u0) / cols`, `v = (row + v0) / rows`.
- **initMirrorTexture** (12; 21737; vfx.cpp:1086): `m_mirrorHorizontal/Vertical` NONE 0 / STANDARD 1 (always flip) /
  RANDOM 2 (flip per particle). 409 modules. INFERRED.

## 6. Blending, lighting, distortion (question 5)

- All effect materials use `unlit_6_effect` (69) or `dist_10_distortion` (3), `materialType` MTL_TYPE_EFFECT_QUAD (66),
  _FLARE (5), _LMAP (1), sortKey 35 (effects) and 36 (distortion).
- **Blend state**: the draw technique (`TECHNIQUE_EMISSIVE`) of the exported `unlit_6_effect_bad_*` sets has
  `defaultStateBits.blendBits` 163920 (0x28050) and `otherBits` 3073 or 3072, for `ta0` and `tca` alike (174 of 175
  sets; the one `bad_ta0_1008803` set has 0x2a054). VERIFIED; the bit layout of the word is not decoded. So `tca` does
  not mean additive, and `materialBlendFromTechset` (`src/core/material_blend.cpp:48`: unlit `bad_tca` = Additive)
  misclassifies lit `tca` smoke such as `elcq/vfx_vol_smoke_billowy_c` and the additive `ta0` flares.
- **Who decides**: the shader. Unlit sets (feature hex, the 13th token, ends in `23`, e.g. `100000023`, `100060023`)
  never read the colour alpha and write output alpha 0, so they add the colour; lit sets (ending in `b`, e.g.
  `10000020b`, `10004005b`) write the lit colour premultiplied by the albedo alpha `a`, and `a x (1 - absorption x
  transmittance)` as alpha (iw3_render_assets.cpp:8919-8934, the fork's disassembly of PS b0ec5d88 and a9864c07).
  INFERRED. With 0x28050 this fits a premultiplied state (one, inverse source alpha): additive and alpha blend in one.
- **Classes by texture slot** (72 materials, VERIFIED): unlit = only slot 18 (emissive `_e`, x constant 36) on 13, with
  slot 68 on 1: fire, sparks, flares, glows, UI rings. Lit = slot 0 (albedo) plus 9 (normal), 27 (opacity), 68
  (transmission), 145, 56: 55 materials (smoke, dust, debris, insects, drips). The two classes split the 69 exactly.
  Opacity: slot 27's alpha when present, else slot 0's (research 66: a `&` packed slot 0 holds specular in alpha).
  INFERRED.
- **Depth**: state hex `803` (vs `802`) is the depthless family "drawn over the world" (flares, laser dots, UI rings;
  iw3_render_assets.cpp:4635-4639). INFERRED. Effect materials draw without depth write and without culling
  (the fork's effect material state, `E:\mw124\x64-zt\src\convert\iw7_iw8\export_materials.cpp:1530-1547`). INFERRED.
- **Lighting**: lit particles take per-particle lighting from the scene (PS a9864c07); unlit ones do not. The preview
  has no particle lighting array; use the scene's ambient or a constant (section 11).
- **Distortion**: `elcq/dist_10_distortion_rpl_ta0_*`, texture slot 157, constant 127 = (x, y) scale (e.g.
  `elcq/vfx_fluid_dist_ripple_a` (30, 30)). Research 132 G13 decoded the world distortion PS (offset the scene by the
  image's xy, weight by z x vertex alpha); the effect PS is assumed the same, NOT re-checked. In the 440: 5 materials
  (`vfx_dist_blast_wave_loop_1`, `vfx_dist_noise_sheet_1`, `vfx_dist_ring_4`, `vfx_dist_wave_sml_1`,
  `vfx_fluid_dist_ripple_a`) in 14 effects (40 mm impacts, water blasts, `vfx_wall_vent_steam`, rain splashes); none
  is placed on Shipment. Skip their quads (the game shows only a shimmer).
- **INIT_MATERIAL** (11; 21773; vfx.cpp:1056): `m_linkedAssetList.assetList.values[k].value` = Material reference
  (1 material on 602 of 635 modules; 2-4 on 30: per-particle choice, NOT DETERMINED), `m_renderOptions`,
  `m_shaderGraphOptions` (0 in Shipment), `m_materialData` (Particle_MaterialData 21809: feather, falloff, eye offset,
  dissolve parameters for the shader).

## 7. Billboard orientation (question 6)

`ParticleStateDef.elementType` (IW7 conv 76-82 maps IW7's enum onto Replay's; IW3 conv 9127-9190 writes them):

| Value | Element | States / effects | Orientation (INFERRED) |
|---|---|---|---|
| 0 | billboard sprite (and IW7 cloud) | 645 / 367 | camera-facing, rotated by the particle angle |
| 10 | tail | 131 / 60 | long axis along the velocity; `initTail` (29; vfx.cpp:1659) `m_tailLeading`, `m_scaleWithVelocity` (length = size.y x speed), `m_rotateAroundPivot`, `m_averagePastVelocities`, `m_maxParentSpeed` |
| 8 | oriented sprite | 53 / 29 | fixed plane, `initOrientedSprite.m_orientationQuat` (x, y, z, w) in effect axes, e.g. (0, 0, 0.7071, 0.7071) |
| 3 | flare | 18 / 8 | screen-space lens flare (`initFlare`, `initOcclusionQuery`, `ecf/` flare materials; IW7 conv 868-899) |
| 7 | model | 237 / 222 | XModel (`initModel`), shells and debris with physics |
| 9 | runner | 91 / 58 | child effect emitter (`initRunner`) |
| 5 / 6 | omni / spot light | 38 / 13 | light (`initLightOmni/Spot`) |
| 2 | decal | 22 / 20 | projected decal (`initDecal`) |
| 11 | vector field | 9 / 9 | force field, nothing drawn |

State flag bits (Replay names from the game-test PDB, IW7 conv 34-56): 1 << 2 spawn shape, 3 position curve, 4/5
rotation 1D/3D curve, 6/7 rotation 1D/3D init, 8-9 velocity curves, 11-14 mirror texture, 17 player facing (18 lock up
vector), 19 occlusion query, 22 emissive curve, 23 intensity curve, 32 child effects, 39 size curve, 40 size lerp.
`initPlayerFacing` does not occur in the 440; `initCameraOffset` occurs 14 times.

## 8. Emitter timing (needed for any preview)

- **Burst mode** = emitter `flags & 0x2` (870 emitters): spawn `particleBurstCount` once at `emitterDelay`;
  `particleCountMax == particleBurstCount.max` on 870/870. VERIFIED.
- **Continuous** (no 0x2, 381 emitters): spawn at `particleSpawnRate` per second from `emitterDelay` for `emitterLife`
  (0 = while the effect plays). `particleCountMax` = ceil(rate x life max) on 403 emitters, so it is a pool cap: drop new
  spawns while that many are alive. INFERRED. `particleBurstCount` on continuous emitters is not a per-event count
  (`vfx_veh_fire_linger_decho_rebel` emitter 0: burst 10-15, countMax 2).
- Shipment examples: `vfx_shipment_smoke_stack` emitter 0 flags 0x200800, rate 3, life 4-5, countMax 15, emitterDelay
  2.2; `vfx_shipment_distant_fog` flags 0x200000, rate 0.4, life 15, countMax 5. Other bits by IW7 name (INFERRED):
  0x200000 FORCE_HALF_RES, 0x400000 FORCE_FULL_RES, 0x80 HAS_LIGHTS, 0x800 HAS_TRANS_SHADOWS.
- A createfx placement with a negative `delayMs` repeats the effect (`Iw8PlacedEffect`, iw8_mapents.h:34).

## 9. Emitter-local transforms and child effects (question 7)

- Effect space = the placement's axes (createfx angles, X forward, Z up); local velocities, forces, spawn offsets and
  oriented quats rotate with it; world-flagged (0x80) graphs and forces do not. INFERRED. Shapes carry their own
  `m_offset`; cylinders `m_directionQuat` when `m_hasRotation`. The system-level `editorPosition/Rotation` and
  `gameTweakPosition/Rotation` are identity/zero in the Shipment exports: ignore.
- Child effects: runners (element 9, `initRunner.m_linkedAssetList`) and group-2 tests (`testImpact` 10, `testAge` 6,
  `testDeath` 1, `testTimeInState` 1; IW3 conv 8939-9022) spawn other ParticleSystemDefs. Physics (`physicsRayCast` 412,
  `physicsLight` 217), lights, decals, models, sounds and vector fields can all be skipped by a first port. On Shipment
  that loses the seagulls (runner) and the drips' splash on impact.

## 10. Survey of the 440 Shipment effects (question 8)

1251 emitters, 1257 states (6 emitters have 2 states). Modules by `active_member` (modules / effects):

- Init (group 0): initSpawn 1257/437, initAttributes 1180/429, initRelativeVelocity 833/364, initMaterial 635/173,
  initAtlas 530/150, initSpawnShapeBox 458/288, initRotation 410/138, initMirrorTexture 409/132, initSpawnShapeSphere
  240/69, initModel 237/222, initSound 234/234, initRotation3D 231/213, initSpawnShapeCylinder 179/75, initTail 137/64,
  initRunner 91/58, initOrientedSprite 59/32, initLightOmni 38, initOcclusionQuery 24, initDecal 22, initFlare 19,
  initCameraOffset 14, initLightSpot 13, initVectorField 9, initKillWrapBox 6, initSpawnShapeEllipsoid 6.
- Update (group 1): gravity 652/324, sizeGraph 554/158, colorGraph 510/148, physicsRayCast 412/238, sizeLerp 271/260,
  velocityGraph 249/98, physicsLight 217/203, emissiveGraph 196/66, forceDragGraph 140/69, scaleByDistance 95/51,
  intensityGraph 92/49, force 70/44, rotationGraph 55/35, parentVelocityGraph 36/26, colorLerp 20/17,
  vectorFieldGraph 9, emissionGraph 8, rotationGraph3D 6.
- Tests (group 2): testImpact 10, testAge 6, testDeath 1, testTimeInState 1.
- Priority for a sprite preview, by effects reached: material + attributes + spawn shapes, then colour and size graphs
  and the atlas (about 150 effects each), rotation and mirror, velocity graph, gravity, tails, oriented sprites.

Duplicates of one type in a state (62 cases: 23 colour graphs, 21 spawn shapes, 5 size graphs ...): in 52 of them
exactly one copy lacks `m_flags & 0x1`; in the other 10 no copy has it. Bit 0x1 also sits on single modules: 436
gravity, 412 relative-velocity, 396 ray-cast, 237 size-lerp. Meaning NOT DETERMINED (section 12).

Shipment's placed list (vfx_cache, 13 effects; the names come from `dump.effects`, main_window.cpp:4715-4729):

| Effect | Elements | Materials |
|---|---|---|
| vfx_shipment_smoke_stack | 2 x billboard | vfx_vol_smoke_wispy_swirl_loop_1, vfx_vol_smoke_thick_anim_1 |
| vfx_shipment_distant_fog | billboard | vfx_vol_weather_cloud_misty_sheet_1 |
| vfx_shipment_core_vapor, _sml | billboards | vfx_vol_smoke_wispy_mist_loop_1, vfx_vol_smoke_soft_billow_loop_1 |
| vfx_shipment_knee_high_mist | billboard (2 materials) | vfx_vol_smoke_soft_billow_loop_1 |
| vfx_shipment_boat_exhaust | 2 x billboard | vfx_vol_dust_turbulent_corner_anim_1, vfx_vol_smoke_wispy_swirl_loop_1 |
| vfx_mist_ground_drift | tail + billboard | same two |
| vfx_moths, vfx_trash_flys | tail | vfx_animal_insect_moth_loop_1 (2x2), vfx_animal_insect_fly_loop_1 (2x1) |
| vfx_shipment_cont_door_drips, _edge_drips | 2 x tail (+ impact test) | vfx_vol_weather_rain_drop_sheet_norm_1 (1x4) |
| vfx_shipment_sunflare | 6 x flare | ecf/vfx_postfx_* |
| vfx_birds_circling_seagulls | runner | (child effect) |

Only the moth, fly and rain-drop materials have an export on disk, and all three are lit. The smoke, mist and dust
materials are not exported (lit by their kind, NOT DETERMINED); they must come through the material cache (zones in
`data\asset_catalog\iw8_materials.tsv`, mostly `techsets_common*`).

## 11. Port recipe

**Reader (`buildVfxSim`)**: walk `stateDefs.values[0]`, groups 0 and 1, match `moduleType.value`. Do not require
`$native`. When a state has two modules of one type, take the one without `m_flags & 0x1` (heuristic, section 12).

**`src/core/vfx_sim.h` additions**:

```cpp
struct VfxCurve { std::vector<float> t, v; float scale = 0; float eval(float x) const; };  // lerp(points) * scale
struct VfxGraph {                     // one module: set A = curves[0..n), set B = curves[n..2n)
    std::vector<VfxCurve> a, b;
    bool random = false;              // m_flags & 0x10
    bool replace = true;              // firstCurve (true when the module has none)
    bool world = false;               // m_flags & 0x80 (velocity graph, force)
    bool present = false;
};
// VfxEmitterSim, new members:
bool burst;                           // flags & 0x2 (replaces `looping` for the spawn rule)
int countMax; double fadeOutMaxDistance;
int element;                          // stateDefs[0].elementType
int relVelType;                       // initRelativeVelocity.m_velocityType value (default 0)
double attrSizeMin[3], attrSizeMax[3]; float attrColorMin[4], attrColorMax[4];   // INIT_ATTRIBUTES
VfxGraph color, size, velocity, rotation, intensity, drag; double forceMin[3], forceMax[3]; bool forceWorld;
double gravityMin, gravityMax;        // m_gravityPercentage
double rotAngleMin, rotAngleMax, rotRateMin, rotRateMax;
struct { int type; double offset[3], dimMin[3], dimMax[3], radiusMin, radiusMax, halfHeight; int axis; bool surface; } shape;
struct { int start, loops; bool randomIndex, overLife, random; VfxCurve c0, c1; } atlas;
int mirrorH, mirrorV;                 // 0 none, 1 standard, 2 random
bool tailScaleWithVelocity; double orientQuat[4];
std::vector<std::string> materials;   // all INIT_MATERIAL references
// VfxParticle, new members:
double size[2]; double angle; double vel[3]; int frame; bool flipU, flipV; int material;
```

**`sampleVfx`**: per particle draw randoms from `unitHash` with fixed salts: life, spawn point (2-3), velocity per
axis, attribute size and colour (one each), one per graph module for A..B, rotation angle and rate, atlas start, fps
and mirror. Then:

1. Spawn times: burst = `burstCount` at `emitterDelay`; continuous = `emitterDelay + k / rate` while
   `k / rate < emitterLife` (or forever when 0), skipping spawns while `countMax` are alive.
2. Position = spawn point (shape around `m_offset`, effect axes) + integral of velocity. Integrate numerically from
   birth with a fixed step (1/30 s, capped at life): `v = v0 (attributes, rotated by the velocity type) + graph(t)`
   (sum, NOT DETERMINED), `v += (force + gravity x 800 (-Z world)) dt`, `v -= drag(t) v dt`.
3. size = graph replace ? graph(t).xy : attr.xy x graph(t).xy; colour likewise (RGBA); intensity multiplies RGB.
4. angle = angle0 + integral of rotation rate (initRotation rate + rotationGraph when `m_useRotationRate`).
5. frame per section 5.

Integration costs O(age / step) per particle; Shipment's placed emitters keep at most 100 alive (countMax 1-100).

**Draw path (`CameraView::appendEffectParticles`)**:

- Carry the placement angles: add `angles` to `CameraView::StockEffect` (and read them for `fx_origin` entities);
  transform effect-space positions and local directions by the placement axes.
- Per material: texture = `textureFor(material)` (slot 0) for lit, `emissiveFor(material)` (slot 18) for unlit; class
  from the techset (unlit `...23` / lit `...b` / `dist_10` skip / `ecf` flare skip); atlas rows and columns. The
  material cache (`src/qtapp/material_cache.cpp`) keeps neither field today: add `textureAtlasRowCount` /
  `textureAtlasColumnCount` to its index, and fix `materialBlendFromTechset` for `unlit_6_effect` (section 6).
- Quads: billboard = camera right/up rotated by `angle`; tail = long axis along the screen-projected velocity, width
  size.x, length size.y (x speed with `m_scaleWithVelocity`); oriented = the quad plane from `m_orientationQuat`
  (which axis is the normal: NOT DETERMINED). Quad half-extent = size / 2 (the current code's choice; NOT DETERMINED).
- UVs: the frame's cell (section 5), flipped by the mirror flags.
- Batches: one `SceneBatch` per (texture, class) in `extraBatches_`. Lit: alpha blend (`blend` 1), vertex colour =
  particle RGBA, sorted far to near; unlit: additive (`blend` 2, `blendAdd_` one/one) with RGB x alpha in the vertex
  colour. A premultiplied state (one, inverse source alpha) would let both share one sorted batch per texture, as the
  game does. Depth test on, depth write off. `GpuTexVertex` colour is 8-bit: intensity above 1 saturates; clamp it.
- Lit particles: multiply by a constant light (for example the stock ambient) so smoke is not drawn full white; compare
  with Lit Preview and Game Lighting against an in-game shot.
- Cull an emitter beyond its `fadeOutMaxDistance` (when > 0) instead of the fixed 12000.

**Core tests** (`tests/vfx_sim_tests.cpp`; copy the fixtures into `tests/data/vfx_sim/`; values from the files named):

1. Curve evaluation, `vfx_shipment_distant_fog.9.asset.json` (vfx_cache, 27,892 bytes, no `$native`): size set A x =
   1066.284 / 1140.642 / 1215.0 at t = 0 / 0.5 / 1, set B x = 861.192 / 953.532 / 1045.872, colour set A alpha at 0.5 =
   0.5297 (+-1e-3); size graph random (flags 16) and replace (firstCurve); velocity graph world (flags 144) with set A
   (0, -45, 0), set B (10, -33.42 at t 0, 20); box offset (-60, 0, 100), dims (-760, -700, -100)..(640, 700, 300); emitter
   continuous (no 0x2), rate 0.4, life 15, countMax 5; atlas random index, still frame (curve scale 0).
2. `vfx_shipment_smoke_stack.11.asset.json` (68,760 bytes): emitter 0 delay 2.2, rate 3, countMax 15, sphere radius
   0..50 at (300, 0, 200), atlas fps between 17.5 and 20, loop -1; velocity graph z set A 30 / 10.6857 / 4.2267, set B
   50 / 17.8096 / 7.0446; rotation rate set A 0.6109 / 0.2156 / 0.127; emitter 1 atlas over life, curve 0 = 0 / 0.5 / 1.
   Check that no particle of emitter 0 exists before t = 2.2 and that alive counts never exceed countMax.
3. Burst rule (synthetic, or a small burst export such as `20mm_resting`): flags 0x2, burst N spawns N once and nothing
   after; flags 0 with emitterLife 0 still spawns at t = 60 s.
4. Aliases: a module with `moduleType {value 0, name "PARTICLE_MODULE_INIT_BEGIN"}` reads as the atlas; velocity type
   value 3 named `..._TYPE_COUNT` reads as local-with-bolt.
5. Materials (copy from `out\agent\toon_ship\mat_techsets_mp_shipment\...\material\elcq\`): `vfx_spark_flame_anim` =
   unlit/additive, rows 1, cols 4, frame 2 UV x 0.5..0.75, no frame blend (hex2 201004); `vfx_vol_smoke_billowy_c` = lit,
   8 x 8, frame blend (3004); `vfx_animal_insect_fly_loop_1` = lit, rows 2, cols 1, frame 1 UV y 0.5..1;
   `vfx_fluid_dist_ripple_a` = distortion (skipped).

## 12. Not determined

- **IDA**: the bridge was down; none of ParticleEmitter_BuildSpriteVertices (0x141BA5920, named in IW7 conv 870) or the
  module update functions were read. Each item below can be settled there.
- Sprite size: full width or half-extent (flies 0.25-0.4, moths 1.2-1.4 inches fit either).
- How the velocity graph combines with INIT_ATTRIBUTES / relative velocity (sum or replace); exact radial frame of
  RELATIVE_TO_EFFECT_ORIGIN; whether force without 0x80 is in effect axes.
- World gravity value for particles (800 assumed) and the gravity module's bit 0x1 (on 436 of 652 gravity modules; 418
  of the 436 gravity modules in states with a physics module have it).
- `m_flags & 0x1` in general: "disabled copy" fits the duplicate pairs (core vapor: the flagged size graph, firstCurve
  false, would multiply a 50-inch attribute into 340-800 inch vapour); it does not fit 396 flagged ray-cast modules.
  With two unflagged copies (10 cases), apply both in order.
- INIT_SPAWN curve's axis (emitter time, distance, or detail level).
- Atlas: row order (top-down assumed), whether a play-over-life atlas counts from startFrame to N-1 or to N, the
  meaning of `m_flags & 0x1` on 7 atlases, and curve 1 when 0x10 is clear.
- Second-set semantics when 0x10 is clear (ignored assumed), and the per-component vs one-random rule.
- Shader math of lit particles (absorption x transmittance from slot 68, the per-particle lighting) and of the effect
  distortion PS; the intensity graph's role on lit materials.
- Oriented sprite: which local axis the identity quaternion's quad faces.
- Tail: width/length axes and `m_tailLeading` (head or tail at the particle position).
- Multiple materials in one INIT_MATERIAL (random per particle or per state).
- Flares: the screen-space flare vertex rule (`m_position`, `m_radialScaleX/Y`, depth scale; IW7 conv 870-875).

## 13. Port status (2026-11-15)

Ported as the recipe says, with these choices and findings:

- `src/core/vfx_sim.*`: modules by `active_member`, curves and graphs, burst flag, alive cap (the newest kept), spawn
  shapes with axis masks, start velocity types 0 / 1 / 2, gravity x 800, force, drag (stepped), rotation, atlas
  (`vfxAtlasFrame`), mirror, materials, placement axes (`vfxAxesFromAngles`, `sampleVfx(..., axes)`). The velocity
  graph is added to the start velocity (the open question; the smoke stack rises as expected with it). Tests
  `vfx_sim_curves_and_graphs`, `vfx_sim_burst_flag_cap_and_spawn_shapes`, `vfx_sim_atlas_frames_and_axes`, opt-in
  `vfx_sim_shipment_exports_real_data` (`IW8R_VFX_SHIPMENT`), which checks the values of section 11.
- Slot 68 of the lit smoke / mist / dust sets is a one-channel BC4 sheet that reads 255 at the cells' edges and about 3
  inside the smoke (`vfx_vol_smoke_thick_anim_1`): a transmittance. Their slot 0 is BC1 with punch-through alpha only.
  The sprite's alpha is slot 0's alpha x (1 - T); with T used as a density the cells showed square edges.
- Draw: `SceneRenderer::drawParticles`, its own shader (float colours, so the intensity graph's HDR values reach the
  HDR scene), one premultiplied blend for both classes, depth test without write, after the deferred blended scene.
  Lit sprites take the map's ambient plus a share of the sun in Game Lighting; unlit ones their constant 36.
- Size is taken as the full width (half extents = size / 2), tails trail behind the particle along the screen-space
  velocity, oriented sprites span the quaternion's y and z axes (INFERRED, section 12).
- Not drawn: models (7), runners (9), lights, decals and vector fields. Flares (element 3, `ecf/` sets), distortion and
  the per-particle light of lit sprites: research 139.
- Bench: `IW8R_BENCH_FX=<seconds>` and the camera `fx <effect name part> N [distance] [yaw] [height] [fov]`; the
  report line `effect_particles`. Shipment: the smoke stack draws as soft rising plumes; 754 particles around the
  knee-high mist, 370 in the distant fog view; frames 4.1-10.7 ms.
