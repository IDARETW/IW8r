# 132. Rendering paths audit: what the game draws that the viewport does not (Replay 1.20)

Read-only audit, 2026-11-14. Nothing was built, run or edited; only the source trees were read. Scope: View > Game
Lighting (the `game0.x > 0.5` branch of `ps_tex` and the HDR post chain in `src/viewport/render.cpp`), with notes on
Lit Preview. IW8 only. The first-person view model is out of scope.

Sources: the viewport (`src/viewport/render.cpp`, `render.h`, `camera_geometry.cpp`), what feeds it
(`src/qtapp/camera_view.cpp`, `src/qtapp/main_window.cpp`), the core classifiers (`src/core/techset_info.cpp`,
`material_blend.cpp`, `iw8_mapents.cpp`), the build side (`src/core/compile_iw8.cpp`, `custom_materials.h`) and the
converter's world shader `C:\zsw-iw8r\scriptables\src\convert\iw3_iw8\resources\map_surface_realtime.hlsl` ("MSR") and
`iw3_render_assets.cpp` ("RA"). Research notes are cited by number. Line numbers are those of the files as read today.

Labels: **evidenced** = read in the code cited; **UNVERIFIED** = plausible, not proven from code or research.

## 0. Summary

The opaque lit world of a stock map is close to the game: lightmaps, the stock GGX sun, probes with box parallax,
SSR, local lights with IES and shadows, transmission, wrap diffuse, emissive constants, world layers (modes 0/1),
mesh decals and decal volumes, analytic and volumetric fog, veil, vignette, white balance, exposure and the LUT are
all ported (research 95, 97-99, 104, 106, 107, 109, 111, 120, 126, 127).

What is not accounted for falls into five groups:

1. **The stock reference has no translucent pass.** Glass (`bar`), blended (`bad`, sort 25/30) and additive
   (`unlit ... bad_tca`) stock surfaces, on the world and on props, are drawn in the opaque pass: cut at alpha 0.5 or
   fully opaque, and lit with the lit-surface model. `unlit_` effect cards get lightmap / probe light instead of their
   own colour. This is the largest visible difference on common maps (windows, light glows, signs, effect sheets).
2. **The mapper's own material shader state is ignored by Game Lighting**, although the build writes it: unlit,
   emission, additive (drawn lit), multiply (drawn as alpha blend), opaque-alpha blended surfaces (drawn at 60 %),
   the alpha-test cut (0.3 in the viewport, 0.5 in the build), glass reflection, sort key and polygon offset.
3. **Things the game draws that the viewport never draws:** the stock map's placed effects (createfx: fire, smoke,
   dust, glow sprites), the mapper's own decal volumes (`iw8r_decal`), distortion (heat haze, ripples).
4. **Approximations with a visible error:** stock prop lighting (one SH sample per placement, no L2, lightmapped
   props on the probe fallback), model-layered materials on props (layer 0 only), own-map local lights (no shadows,
   IES or bulb), the LDR 256-texel sky, back faces never culled, SSAO for GTAO.
5. **Not researched (UNVERIFIED):** stock UV animation (scrolling materials), detail colour (slot 36), parallax
   residual of displaced surfaces, static-model vertex colour and SELFVIS, stock water.

## 1. Table of paths

V = viewport (Game Lighting), B = build of an editor map. "ok" = matches the game's rule as researched.

| # | Path | Game (source) | Viewport | Build (own maps) | Gap |
|---|---|---|---|---|---|
| 1 | Opaque world, stock | stock lit PS (95) | ok: `ps_tex` stockRef, render.cpp:1036-1204 | n/a | - |
| 2 | Opaque world, own | MSR iw3 style (117) | ok: render.cpp:1205-1245 | yes | - |
| 3 | Stock lightmaps | 94, 95 | ok: render.cpp:994-1017 | n/a | - |
| 4 | Own lightmaps / unbaked fill | MSR:1434-1437, 117 | ok: render.cpp:985-988, 1018-1021 | yes | - |
| 5 | World layered terrain, modes 0/1 | 97, 107 s3 | ok: render.cpp:874-928, camera_view.cpp:1196-1231 | n/a | - |
| 6 | World layers, modes 2/3 (normal/gloss only) | 107 s3.1-3.2 | **skipped**: camera_view.cpp:1207-1208 | n/a | G9 |
| 7 | Model-layered materials (props, `tmo/tm2o/...`) | 107 s3 | **layer 0 only**: materialConstantsFor uses `layer[0]`, camera_view.cpp:1163; no layer path for instanced props (render.cpp:4071-4112) | n/a | G9 |
| 8 | Alpha-tested foliage, stock | cut 128/255 (100) | ok: render.cpp:905-918 (props soft-edged) | n/a | - |
| 9 | Alpha test, own | MSR:1359-1360 `clip(a - .5)` | **0.3**: render.cpp:905-908, 729 | yes (0.5) | G5 |
| 10 | Two-sided / back faces | stock: tangent frame negated (100); MSR:1186-1192 (flag 21 only) | normal always flipped to viewer (render.cpp:948); **no culling anywhere** (render.cpp:1960, 3484) | cullFace none only when `twoSided` (compile_iw8.cpp:1115) | G11 |
| 11 | Stock glass (`bar`), blended (`bad`, sort 25/30) | blended with reveal, after opaque (66, 109 s0 step 3) | **opaque pass**, cut at 0.5 or opaque (render.cpp:5012, 5031-5064; 907-918); props too (render.cpp:4058) | n/a | G1 |
| 12 | Stock additive / `unlit_` effect cards | unlit colour, additive (66) | **lit and opaque/cut** (same; no `unlit` test in render.cpp or camera_view.cpp) | n/a | G2 |
| 13 | Own blended (glass, decal sheet) | MSR:1770 alpha = texel.a; glass reflection MSR:1777-1806 | alpha, but **0.6 when alpha is 1** (render.cpp:920); no reflection | yes | G4 |
| 14 | Own additive / multiply | MSR:1378-1379, 1626-1629: colour as is, unlit; state compile_iw8.cpp:1116-1117 | additive drawn **lit** (render.cpp:1286); multiply drawn as **alpha blend** (main_window.cpp:465) | yes | G4 |
| 15 | Own unlit / emission | MSR:1626-1635 (light = 1 or emission gain) | **lit, no emission**: `CustomMaterial::unlit/emission` never read outside compile | yes (compile_iw8.cpp:1110, 1129-1134) | G4 |
| 16 | Stock emissive (slot 18 x const 36) | 107 s5 | ok: render.cpp:1187, camera_view.cpp:1170-1189 | n/a | - |
| 17 | Mesh decals, stock (world + props) | 98 | ok; prop decals cut beyond 4,000 units (render.cpp:5120-5131) | n/a | G15 |
| 18 | Decal volumes, stock | 99 | ok: render.cpp:880-888 | n/a | - |
| 19 | Decal volumes, own (`iw8r_decal`) | MSR `ApplyDecals` (1385-1387), 80/82 | **not drawn** (only compile_iw8.cpp:3162-3237 reads the class) | yes | G6 |
| 20 | Own decal sort key / polygon offset | stateBits (compile_iw8.cpp:1118, 1128) | ignored; blended batches by distance (render.cpp:4925-4940) | yes | G4 |
| 21 | Static models: LODs, cull distance | 119 | ok: render.cpp:4085-4087, main_window.cpp:7603-7625 | n/a | - |
| 22 | Static models: probe-lit (LGV) | per-vertex trilinear L2 SH in the box (105 s1.4) | **one DC + L1 sample per placement** (main_window.cpp:7380-7394; render.cpp:486-494) | n/a | G7 |
| 23 | Static models: lightmapped (`tml/`, flag 2) | lmapLookup + per-vertex lightmap UV (105 s3.2) | **probe fallback** (main_window.cpp:7384, 7705-7706) | n/a | G7 |
| 24 | Static models: SELFVIS, vertex colour | 105 (SELFVIS per vertex; `_VERTCOL` types) | ignored (stockRef albedo ignores `i.col`, render.cpp:938) | n/a | G16 UNVERIFIED |
| 25 | Own models | MSR InstanceAmbient | ok: render.cpp:1027-1030 | yes | - |
| 26 | Scripted objects (scriptables) | definition parts + states | first model of the definition, static (main_window.cpp:7743-7757) | n/a | G15 |
| 27 | Animated `script_model` | 61 | not played (ROADMAP 4.21) | yes (script) | G15 |
| 28 | Sky | sky PS, HDR hemi-octahedral (33, 109 s3) | 8-bit, 256-texel cube, exposure clips above the 99.5th percentile (camera_view.cpp:717-739; sky_image.h:21-22) | own: sky unlit in MSR | G10 |
| 29 | Sun sprite / flare | off or pointing down on the MP maps read (109 s3) | none | style iw3: none | - |
| 30 | Analytic fog | 111 | ok: render.cpp:321-356 | yes | - |
| 31 | Volumetric fog, boxes, masks, lamps | 109 s2, 126, 127 | ok; ambient is a constant, not the light grid (render.cpp:685) | yes | G17 |
| 32 | Local lights, stock | 106 | ok: render.cpp:732-864, 1189-1204 | n/a | - |
| 33 | Local lights, own | MSR LocalLighting 1063-1177: tube, IES, sphere form factor, shadow maps | **point light, N.L / d^2, no shadow, no IES, no bulb** (render.cpp:1246-1267) | yes | G8 |
| 34 | Reflection probes | 95, 108 | ok: render.cpp:384-438 | own: probe / default grey | - |
| 35 | SSR | 104 | ok (24-step march): render.cpp:1089-1138 | n/a | - |
| 36 | AO | GTAO, `ao = lerp(1, vis, r_ssaoStrength)` (107 s4.3) | own 12-sample SSAO (render.cpp:1369-1413) | GTAO | G14 |
| 37 | Veil, vignette, white balance, exposure, LUT, grain | 95 s3, 109, 120 | ok: render.cpp:691-722; auto exposure camera_view.cpp:2630-2663 | yes | - |
| 38 | TAA / SMAA | 109 s0 step 6 | none (soft cut-outs stand in, render.cpp:910-915) | yes | low |
| 39 | Placed effects, stock (createfx) | particle systems (50) | **not drawn**: only counted, iw8_mapents.cpp:150 | n/a | G3 |
| 40 | Placed effects, own (`fx_origin`) | particle systems (50, 67) | untextured coloured quads, only with the effect preview on (camera_view.cpp:1614-1656) | yes (real FX) | G3 |
| 41 | Distortion / refraction | `dist_10_distortion` (66), own MSR `MAP_UVD` | **hidden** (techset_info.cpp:68-69) | yes for IW7 sources | G13 |
| 42 | UV animation (scroll) | own build: `MAP_UV_ANIMATION` (RA:3541); stock UNVERIFIED | none (no time input in `vs_tex`/`ps_tex`) | only for extractions (no editor key) | G16 UNVERIFIED |
| 43 | Detail colour (slot 36), parallax residual, sheen | 66 slot table; 98 s"PD" | none | n/a | G16 UNVERIFIED |
| 44 | Water | stock UNVERIFIED; own: IW3 `MAP_WATER` route exists (RA:7300-7315) but the editor's water tool material is not a water techset (ROADMAP 6.24) | none | no water shading (UNVERIFIED) | G16 |
| 45 | Foliage wind | per-model reactive motion | preview only, off by default (render.cpp:457-463) | not written (ROADMAP 5.23) | low |
| 46 | Own custom surface shader / filter | MSR IW8R_CUSTOM | ok: render.cpp:1209-1220, 1270-1284 | yes | - |
| 47 | Lit Preview (not Game Lighting) | - | an editing aid: fixed bump light (render.cpp:1303), ad-hoc gloss, linear light falloff (1318-1340) | - | G18 |

## 2. Ranked gaps

### G1 (high). Stock translucent surfaces are drawn opaque

- Game: blended (`bad`) and glass (`bar`, "alpha blend with reveal") sets draw after the opaque pass with blending
  (research 66 table "Blend tokens"; research 109 section 0, frame order step 3 "Transparents and effects"). Glass and
  other blended model surfaces have sort keys 25 / 30 and are not decals (main_window.cpp:394-396).
- Viewport, world: the stock reference's base loop (render.cpp:5031-5064) draws every batch that is not a layer,
  decal or hidden class, with `batchFlags.y = 0` (render.cpp:5012) and the opaque depth state. In `ps_tex` that is
  the cut-out branch: `clip(t.a - 0.5)` (render.cpp:907-918). The colour image's alpha for a blended material is the
  opacity mask when there is one, else forced to 255 (camera_view.cpp:1515-1531). So unmasked glass is fully opaque
  and masked glass is a hard 0.5 cut, and both are lit as an opaque dielectric of gloss 0.3.
- Viewport, props: `drawReferenceTexInstances` sets `batchFlags.y = -1` (soft cut-out) for every prop mesh
  (render.cpp:4058); `InstancedTexMesh` has no blend field (render.h:127-153). Prop glass, lamp glows and effect
  sheets get the same treatment.
- Visible: windows, vehicle glass, visors and glass doors are solid panes; see-through grime sheets and translucent
  plastics are opaque or speckled.
- Fix: give `InstancedTexMesh` and the reference batches a blend mode (the techset is already parsed:
  `materialBlendFromTechset`, `parseTechset(...).glass`). Draw those batches after the opaque reference and the props,
  with depth test and no depth write, `blend_` (or the additive state) and `batchFlags.y = 1` / `2`, sorted far to
  near. For `bar` glass use the reveal / opacity image already loaded as the mask (slot 27). The game's glass also
  reflects the probe; the stockRef branch can keep its probe term and output premultiplied alpha.

### G2 (high). `unlit_` stock materials are lit

- Game: `unlit_6_effect` sets (51 materials; research 66) output their colour (x their constants) unlit; additive
  ones (`unlit ... bad_tca`) add it.
- Viewport: no code path tests the `unlit` family. `parseTechset` knows it (`TechsetFamily::UnlitEffect`,
  techset_info.cpp:37), but `ps_tex` shades every non-sky pixel of the stock reference with the lightmap / probes /
  sun (render.cpp:1036-1204), and the additive output `c * outA` (render.cpp:1286) is still the lit colour.
- Visible: light glows, sign faces, fake light shafts and effect cards are dark or grey instead of bright.
- Fix: pass the family to the renderer (a flag in the per-batch `layerP`/`matX` row, which is free:
  render.cpp:513 `matX`), and in the Game Lighting branch return `albedo x (emissive constant if any)` before the
  lighting, then fog and post. Pair it with G1 so additive cards also blend additively.

### G3 (medium-high). Placed effects: stock createfx not drawn, own `fx_origin` only as plain quads

- Game: a stock map's createfx placements (fire, smoke, steam, falling dust, lamp glow sprites) play from the map's
  effect list; an own map's `fx_origin` plays its real effect (research 50, 67).
- Viewport: the stock list is read as a count only (`count("createFxEffects", ...)`, iw8_mapents.cpp:150); nothing
  places them. Own `fx_origin` entities draw, only with the effect preview on, as untextured camera-facing quads in
  the particle colour over a 1x1 white texture (camera_view.cpp:1614-1656).
- Visible: stock maps lose their fires, smoke columns and glow sprites; own effects look like coloured squares.
- Fix: read the createfx records (positions, effect names) from the map_ents export into read-only `fx_origin`-like
  points and run them through `appendEffectParticles`. Give particles their material's image and blend (the VFX
  export names its materials, research 50 section 1) instead of `whiteTex_`. Keep it off by default if the cost is high.

### G4 (high on own maps that use them). Own material shader state ignored by Game Lighting

The build writes it all (compile_iw8.cpp:1106-1134) and the converter draws it (MSR:1378-1379, 1626-1640, 1770-1806):

| State | Build / game | Viewport | Evidence |
|---|---|---|---|
| `unlit` | colour as is (light = 1) | lit like any surface | `CustomMaterial::unlit` is read only in compile_iw8.cpp:1110, 1124 |
| `emission` > 1 | light = emission gain (MAP_EMISSION) | none | compile_iw8.cpp:1129-1134; no emissive lookup for own materials (main_window.cpp:501-504 reads the stock cache only) |
| additive (`blendMode` 1) | one/one, unlit | additive of the **lit** colour | render.cpp:1286; main_window.cpp:465 |
| multiply (`blendMode` 2) | zero/srccolor, unlit | **alpha blend** | main_window.cpp:465 maps 2 to blend 1 |
| blended, alpha 1 | opaque (alpha = texel.a) | 60 % coverage | render.cpp:919-921 vs MSR:1770 |
| blended glass reflection | spec map x probe, premultiplied | none | MSR:1777-1806 |
| `sortKey`, `polygonOffset` | stateBits | ignored | render.cpp:4925-4940 sort by distance |

- Fix: give `blendFor` a fourth value for multiply and an `unlitFor(material)` callback (both from
  `CustomMaterial`). In `ps_tex` Game Lighting, when unlit (or additive / multiply, which MSR also draws unlit):
  `c = albedo x (emission > 1 ? emission : 1)`, then fog and post; the multiply batch uses the existing `blendMul_`
  state (render.cpp:5159). Drop the 0.6 fallback in the Game Lighting branch (keep it only for Lit Preview if wanted).
  Sort blended own batches by `sortKey` first, then distance.

### G5 (medium). Own alpha-test cut at 0.3, the build cuts at 0.5

- Build: `if (kind == 3) clip(texel.a - .5)` (MSR:1359-1360), the same in the depth prepass.
- Viewport: `cutAt = game0.x > 0.5 && batchFlags2.z > 0.5 ? 0.5 : 0.3` (render.cpp:908) and the same in `ps_prepass`
  (render.cpp:729): own materials keep 0.3 even in Game Lighting.
- Visible: own foliage, fences and grates look thicker (more opaque texels) than in game.
- Fix: in Game Lighting use 0.5 for every batch (`game0.x > 0.5 ? 0.5 : 0.3`), in both `ps_tex` and `ps_prepass`,
  and in `ps_shadow_clip` (render.cpp:724).

### G6 (medium). Own decal volumes are never projected in the viewport

- Build: `iw8r_decal` entities become world.json decal volumes (compile_iw8.cpp:3162-3237); the converter's world
  shader applies them (`ApplyDecals`, MSR:1385-1387; research 80, 82).
- Viewport: no code outside the compiler reads the class (grep of `iw8r_decal` in `src/`): the volume shows only as a
  point entity.
- Fix: the stock decal-volume path already exists (`RefDecalVolume`, camera_view.cpp:231-339; mask and remap in
  render.cpp:880-888). Build the same per-receiver geometry for `iw8r_decal` boxes (material, size, opacity,
  feather, draw order, UV rotate are the keys compile reads) and draw it in the decal pass.

### G7 (medium). Stock prop lighting is a single sample; lightmapped props use the probe fallback

- Game: probe-lit props sample their light-grid box per vertex, trilinear over 1 to 64 probes, L0 + L1 + L2, and
  SELFVIS; lightmapped props (`tml/`, model flag 2) read the lightmap through `lmapLookup` and per-vertex lightmap
  UVs (research 105 sections 0, 1.4, 3.2; Hideout has 236 lightmapped instances).
- Viewport: `propLightFor` returns one 12-float record per placement (DC + three first-order slots;
  main_window.cpp:7380-7394), evaluated in `vs_tex_inst` with the band-1 weight only (render.cpp:486-494). Props
  without the probe flag get DC r = -1 and fall to `stockProbeLight` x the map's mean light (main_window.cpp:7384,
  7705-7706; render.cpp:1031-1032).
- Visible: large props (buildings, containers, walls as models) are flat-lit end to end; lightmapped rugs and floor
  pieces read too bright or too dark against the floor around them.
- Fix: upload the LGV atlas (`StockLgvAtlas`, already parsed in core `stock_lightgrid.*`) as two 3D textures plus the
  per-box transform / packing as a structured buffer, and evaluate per pixel (or per vertex) with the existing
  slot order and band weights (stock_lightgrid.h:63-71). For flag 2 props, carry the instance's `lmapLookup` rect and
  the model's packed lightmap UVs (unorm16 pairs, research 105 s3.2) into the existing `lmRects` path.

**Status 2026-11-14 (G7):** probe-lit props of the main transient zone are lit per vertex from the atlas, as the
game's static-model VS does (research 105 section 1.4): the block's transform params (`StockLgvAtlas::xform`, equal to
the exported box rows) and the packing go to the GPU as five float4 per box, texture 0 as R11G11B10F and texture 1 as
RGBA8 (`SceneRenderer::setStockLgvAtlas`, vs_tex_inst `lgvLight`: DC, all eight slots, band weights 1, 4/9, 1/16). The
instance record is `-3, box, centre DC, slots 0 / 2` (the fallback when no atlas is bound). Proven active by a one-off
tinted build on Hideout (every probe-lit prop); against the old centre sample, Hideout's frame changes 0.4 % of
pixels by more than 8 and Speedball's 2 % (gradients across multi-probe boxes); Hideout 8.9 ms, Speedball 5.6 ms.
`IW8R_NO_PROP_LGV_GPU` = the centre sample (A/B). 2026-11-15: Warzone tile props too: the main zone's atlas and every
loaded tile's (Verdansk widths 64 / 32 / 16 / 8, depths up to 16; 82 grids cached, 1,216 slices in all) are stacked
into one (core `packStockLgvAtlases`: the widest width, depths in turn, a narrower atlas at the corner of its slices,
texture 1's tiles re-laid, each box's packing turned to the stack; test: the stack samples as its sources), rebuilt
off the UI thread when tiles arrive; a prop names `firstBox[its transient zone] + lightingIndex`. 2026-11-15 later: the thin push
(flag 4; run as with `cb7[185].x` = 0, INFERRED for MP: the box flags ride in the spare w of the box's uvScale row,
`IW8R_NO_LGV_THINPUSH`), and the 236 lightmapped `tml/` props of Hideout: ACTS fork 834568b9 exports XSurface.lmapCoords
(+80) as `_LMAPUV` (checked on `furniture_rug_01_flat_04_lod010v41`: 1,733 vertices, u 0.025-0.645, v 0.025-0.713, as
research 105 section 3.2), core reads GfxWorld.lmapLookup into `StockModelInstance::lmapPage / lmapRect`, a prop of
flag 2 gets the record `-4, page, offset, scale`, its vertices carry the lightmap uv (two unorm16), vs_tex_inst writes
the world's encoding (page + u) and the stock lightmap branch lights it; the model cache fetches again the drawn
lightmapped models cached before (`lmapuv`). Proven with a one-off tinted build (the rugs take the path) and an A/B
(`IW8R_NO_PROP_LIGHTMAP`): indoor rugs take the floor's light instead of the brighter probe fallback.

### G8 (medium). Own-map local lights are simplified

- Build: Replay's own `LocalLighting` (MSR:1063-1177): tube segment, IES profile strip, smoothstep window, sphere form
  factor with the bulb radius, shadow maps for lights with `iw8r_shadows 1` (research 59 item 37), transmission.
- Viewport: a per-pixel loop over `lights[64]` with `w^2(3 - 2w) / d^2 x N.L` and a gloss-0.3 Blinn lobe, no shadow,
  no IES, no bulb or tube (render.cpp:1246-1267).
- Visible: lamps light through walls and floors, IES patterns are missing, a lamp close to a wall is too bright.
- Fix: feed own lights to the stock path: the converter's 96-byte record is the same layout as `GameLight`
  (render.cpp:734-735, MSR:1032-1037), and `gameLocalLight` already does tube, IES, window and form factor. Pack own
  lights into `gameLights` / `lightTiles` and give shadowed spots a slot in the existing local shadow maps
  (render.cpp:741-762).

### G9 (medium). Layers left out: model-layered props, and colour modes 2/3 everywhere

- Game: model-layered materials (`tmo`, `tm2o`, `tmco`, ...) blend up to 3 layers on one UV set; mode 0 lerps the
  colour (265 layer slots in the two zones counted), mode 1 multiplies (15), modes 2/3 change only normal, cavity and
  gloss (43) (research 107 s3.1-3.2).
- Viewport: props bind one material with `layer[0]` constants (camera_view.cpp:1163) and no layer pass
  (render.cpp:4071-4112). World layers with mode 2 or 3 are skipped entirely (camera_view.cpp:1207-1208), so their
  normal / gloss changes are lost.
- Visible: crates, slabs, bombsite props miss their dirt / paint variation layer; gloss patterns from mode-2 layers
  are missing.
- Fix: decode with `decodeStockLayeredMaterial(bytes, N)` as research 107 s3.3 says, and draw the extra layers of a
  prop mesh as additional instanced passes with the reveal step rule (w = 1). For mode 2/3, a pass that writes only
  normal / gloss is not possible in a forward renderer; fold the layer into the base shader (sample layer normal and
  gloss in `ps_tex` when a second layer row is bound).

**Status 2026-11-14 (G9):** model-layered materials are detected from the technique set name (material cache
`modelLayers`, the raw lit buffer `layerPsData`) and their **multiply** layers are drawn on props (a layer pass
between the props and their decal meshes). Colour-lerp layers are left out: drawn with weight 1 they covered whole
surfaces on King (`tm2o/mout_container_wall_single` layer 1 flags 0x47: all door texture; the
`tm2co/sign_un_variation_01_combined` alphabet layer, flags 0xf: the whole letter atlas), so the game weights or
maps them with data the GLB export does not carry (a second UV set or vertex weights; UNVERIFIED). Modes 2/3
(normal / gloss only) are not folded into the base shader yet.

**Status 2026-11-15 (G9):** research 136 decoded the weights. Each layer is classified from the technique set name
(core `parseModelLayerTokens`: standard / overlay / code, second UV set, slot-126 mask); props carry the second UV set
(GLB TEXCOORD_1) and the vertex alpha as the weight (vc.w = 1 + a). A mesh's first two colour layers are folded into its
own shading pass, in albedo space as the game's PS does: lerp (mode 0), multiply (1) and the soft-light overlay, with the
reveal ramp, the alpha and the slot-126 mask (record rows 16-23, images at t36 and t44-t47); further layers are drawn as passes
(lerp and overlay on LOD0 only). King's container walls keep painted metal on their alpha-0 panels and take the door
layers on the frame; Aniyah Tac 11.5 ms, Hideout 9.4 ms (separate passes had cost 6.7 ms on Aniyah). Later the
same night the fold took the layers' normal, cavity and gloss too (flags 1 / 2 / 4, normal mode lerp / add / none,
overlays add and pull cavity and gloss toward the lower value; rows 24-25, normal maps at t48 / t49), so colour modes
2 / 3 are drawn; Hideout 9.4 ms, Aniyah 11.8 ms. Left by decision: code layers (not decoded) and the per-layer tint
masks (slot 59 + L). A/B: `IW8R_NO_LERP_LAYERS`, `IW8R_NO_LAYER_FOLD`.

### G10 (medium-low). The sky is an 8-bit 256-texel cube

- Game: the sky PS samples the HDR hemi-octahedral BC6H image (research 33); the sun is whatever the image holds plus
  the fog and volumetric lobes (109 s3.4).
- Viewport: `skyCubemapFromDds` with `faceSize = 256` to B8G8R8A8 faces (camera_view.cpp:717-739); HDR values above
  the 99.5th percentile, mapped to 0.9, clip (sky_image.h:21-22), and `gameSkyScale.x` scales it back.
- Visible: the bright area around the sun in the sky picture is flat and clipped, so the veil around it is weaker
  than in game; the sky is soft at high zoom.
- Fix: upload the source as a float16 cube (or the BC6H hemi-octahedral image as is, and sample it with the octa
  mapping already used for probes, render.cpp:376-383) and drop the exposure step.

### G11 (low-medium). Back faces are never culled

- Game / build: one-sided surfaces are culled; own materials disable culling only when `twoSided`
  (compile_iw8.cpp:1115); the converter flips the back-face normal only with flag bit 21 (MSR:1186-1192).
- Viewport: every rasterizer state uses `D3D11_CULL_NONE` (render.cpp:1960, 3484, 5328), and the normal is always
  turned to the viewer (render.cpp:948).
- Visible: one-sided patches, terrain edges and models seen from behind show in the editor and vanish in game.
- Fix: a second triangle state with `D3D11_CULL_BACK` for Game Lighting, used for own batches whose material is not
  two-sided and for stock batches whose techset is not two-sided (the cull state is in the techset stateBits; for
  stock, UNVERIFIED which field ACTS exports).

### G12 (low-medium). Own blended glass reflection

Part of G4 (table row "blended glass reflection"): the build adds `specular map x lerp(envParms) x probe` premultiplied
(MSR:1785-1806); the viewport draws no reflection on blended own surfaces. Fix with G4 using `spec0` / `probeCube`
already bound (render.cpp:308-309).

### G13 (low). Distortion is hidden

- Game: `dist_10_distortion` (heat haze, water ripples; 2 stock materials, research 66); own maps from IW7 sources
  get `MAP_UVD` (RA:8468-8498).
- Viewport: `MaterialDraw::Hidden` for the distortion family (techset_info.cpp:68-69).
- Fix: a post pass that offsets `hdrScene` lookups by the material's normal map where distortion surfaces cover the
  screen. Low priority (few materials).

**Status 2026-11-15 (G13): decoded, left hidden by decision.** The world distortion PS (`bd3434da17d1d564`,
techsets_mp_rust, `twc/dist_10_distortion_*`; VERIFIED from its disassembly) samples the distortion image (slot 157)
`m`, takes `k = saturate(m.z x vertexAlpha - 0.25) x 4/3`, displaces the pixel's point by the world vector
`D = c127.x k (2 m.x - 1) T + c127.y k (2 m.y - 1) B` (T the vertex tangent, B = N x T), projects `P + D` with the
view-projection rows (homogeneous add, then clamped to the viewport, `cb7[156]`), and writes the resolved scene there
x the vertex colour (alpha `saturate(4 m.z vertexAlpha)`; the technique replaces). INFERRED: `cb2[4].xy` is the
clip-to-UV scale. On the only stock world user, Shipment's `twc/dub_water_wall_waves_dist`, constant 127 is (1, 1):
the scene moves by at most one unit, which no comparison shot can show, so the batch stays hidden. The effect
materials (`elcq/vfx_dist_*`, c127 2 to 30) belong to placed effects, whose quads the viewport does not distort.
Static-model displacement (research 133 section 3): its two stock users (`tmo/stone_wall_01`, `mo/skl_sand_dune_01_blend`)
both have constant 45 = (2, -1), a shift of at most one unit along the normal; left undrawn by decision.

### G14 (low). SSAO stands in for GTAO

- Game: GTAO visibility with `ao = lerp(1, vis, r_ssaoStrength)` and the multi-bounce form (107 s4.3).
- Viewport: a 12-sample hemisphere SSAO with a 4x4 blur (render.cpp:1369-1413, 1480-1492); the combine is right.
- Visible: different contact darkening radius and strength in corners. Fix: port the GTAO slice march if the owner
  compares corners; otherwise leave.

**Status 2026-11-15 (G14): done.** Research 135 decoded `cs_gtao`; the viewport runs its kernel as a pixel shader: a
linear view depth max pyramid (mips 0-4; the editor's clip w is scaled, so depth and the half-field tangents are taken
over the length of the projection's w row), 4 slices x 2 sides x 4 quadratic steps on mips 1-4, the 100-unit radius
(clamped to 512 px at 1080p), falloff 50..200, the thickness decay, the cosine-weighted arc integral, the 8..16 px
distance fade, normals from depth with the smaller-step rule, a 4 x 4 interleaved rotation / jitter and the existing
4 x 4 blur in place of the game's temporal filter. Corners darken over a wider band than the old SSAO (Hideout room:
corner 0.73 against 0.85 of white); same cost (Hideout 9.2 ms, Speedball 5.8 ms). `IW8R_AO_SSAO` = the old SSAO.

### G15 (low). Distance and budget cuts, scripted objects

- Prop decals are not drawn beyond 4,000 units (render.cpp:5120-5131); props past a 4,000,000-triangle budget fall
  back to stand-ins (main_window.cpp:7494, 7636-7641); scriptables draw their definition's first model in its default
  state (main_window.cpp:7743-7757); animated `script_model`s are not played (ROADMAP 4.21). Performance choices; note
  them in the Game Lighting status when they apply.

### G16 (UNVERIFIED). Not researched

- **Stock UV animation (scrolling).** The converter supports `uvAnimParms` for extractions (RA:3541, research 68
  line 258); no research note reads whether stock IW8 world materials scroll (conveyors, waterfalls, screens).
  Nothing in `vs_tex`/`ps_tex` takes a time. Check the stock material constants for an animation row first.
- **Detail colour (slot 36), sheen (128), thickness (77-78), impostors (159-164)** are listed in research 66's slot
  table; no research gives their shader math and the viewport samples none.
- **Displacement parallax residual** (research 98 "PD" lines 141-170): the viewport displaces on the CPU
  (`stock_displacement.*`) but has no per-pixel parallax.
- **Static-model vertex colour and SELFVIS** (`MTL_TYPE_STATIC_MODEL_..._VERTCOL_SELFVIS`, research 105): SELFVIS is
  dropped by the GLB export; what the vertex colour does in the static-model PS is not decoded; the viewport ignores
  both (render.cpp:938).
- **Water.** Stock water shading is not researched; an own map's water tool material is drawn as an ordinary surface
  and the converter's IW3 water route (`MAP_WATER`, RA:7300-7315) needs a `water` material kind the editor does not
  write (ROADMAP 6.24).

### G17 (low). Volumetric ambient

- Game: the volumetric ambient comes from the light grid per froxel (`cs_vol_sh_sample_lightgrid_*`, research 109 s2).
- Viewport: one constant (`volP.y * volAmb.rgb * 4`, render.cpp:685). Visible only in dense fog indoors vs outdoors.

### G18 (by design). Lit Preview is not a game model

Lit Preview shades with baked vertex colours, a fixed bump light direction (render.cpp:1303), ad-hoc gloss
exponents and a linear light falloff with a roll-off (render.cpp:1318-1340). It cannot be 1:1 without becoming Game
Lighting. Suggest stating in the View menu tooltip that Game Lighting is the comparison mode.

## 3. Order of work suggested

1. G1 + G2 together (one translucent / unlit pass for the stock reference and props).
2. G4 + G5 + G12 (own material state; small shader and callback changes, the build is the reference).
3. G6 (own decal volumes through the stock decal-volume path).
4. G8 (own lights through `gameLocalLight`).
5. G7, G9 (prop lighting and layers; data already parsed in core).
6. G3 (effects), G10 (sky), G11 (culling), then the rest.
