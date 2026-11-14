# Research reports

Evidence gathered for IW8 Radiant Suite. Each report cites file:line or tool output. Items marked UNKNOWN are
not guessed. The target build throughout is MW2019 Replay 1.20.4.7623265
(`game_dx12_ship_replay.exe`, MD5 `1c238fe327f2ecc3b0db924c5b425439`).

| # | Report | Use |
|---|---|---|
| 01 | [iw3xo-radiant feature audit](01_iw3xo_feature_audit.md) | Parity reference: which mapper features iw3xo added and which it inherits from CoD4 Radiant |
| 02 | [Radiant text grammar (`iwmap 4`)](02_iwmap4_format.md) | Grammar validated on 2,182 files; the editor's source container |
| 03 | [Legacy mw120rproxy build pipeline](03_build_pipeline_interface.md) | Older IW8 writer and its CoD4-based route (superseded; not used) |
| 04 | [IW8 asset access](04_iw8_asset_access.md) | ACTS is the stock Replay 1.20 reader; where the stock zones live |
| 05 | [Replay 1.20 runtime evidence](05_runtime_evidence_1_20.md) | RVAs proven by the proxy's hooks; live-link entry points |
| 06 | [Test assets and in-game evidence](06_test_assets.md) | Fixtures and owner-recorded in-game loads |
| 07 | [Editor feel reference](07_editor_feel_reference.md) | Stock camera, selection, grid, hotkey and undo behaviour |
| 08 | [x64-zt IW8 side](08_x64zt_iw8_conversion.md) | IW8 dump/build commands, `.iw8` container, MapEnts dump, extraction contract |
| 09 | [IW8 render features](09_iw8_render_features.md) | IW8-only lighting/render data the editor must author |
| 10 | [IW8 entity inventory](10_iw8_entity_inventory.md) | Stock classes and keys; `data/iw8_entity_classes.json` is now rebuilt by `iw8r iw8-classes` |
| 11 | [ACTS Replay baseline](11_acts_replay_baseline.md) | ACTS build/test procedure and the before-numbers for the layout work |
| 12 | [ACTS research switches review](12_acts_research_switches_review.md) | Review of the opt-in `ACTS_*` switches (findings fixed in the fork) |
| 13 | [iw8 level maker (third-party reference)](13_iw8_level_maker_reference.md) | Extra reference only, mixed reliability (see below) |
| 14 | [x64-zt verifyzone coverage](14_x64zt_verifyzone_coverage.md) | Per-pool real-zone evidence for the ported Replay layouts |
| 29 | [Probes, static models and sky](29_probes_models_sky.md) | Reflection probes per entity, `misc_model` placements and sky limits in the x64-zt extraction |
| 32 | [Game-mode objects](32_gamemode_objects.md) | DOM, hardpoint, S&D, CTF classnames/keys from the stock map_ents; backs `src/core/gamemode_templates.*` |
| 33 | [Sky route](33_sky_route.md) | What x64-zt needs for `world.sky` (legacy cube DDS), the stock hemi-octahedral BC6H skies, conversion and build check |
| 94 | [Stock lightmaps](94_stock_lightmaps.md) | Where a stock map's lightmap lives, its three planes and per-vertex UVs |
| 95 | [Lit shader math](95_lit_shader_math.md) | The stock world shader, the post chain, and the converter's world shader for editor maps (Game Lighting) |
| 96 | [Stock world removal](96_stock_world_removal.md) | Removing stock world triangles from drawing, Havok collision and the sun shadow |
| 97 | [Stock layered materials](97_stock_layered_materials.md) | How tw4 / twc4 blend up to 8 texture layers: weights, reveal ramp, constants |
| 98 | [Stock decals and displacement](98_stock_decals_displacement.md) | Blended world decals (sortKey 10-17) and `dn` displacement layers |
| 99 | [Stock decal volumes](99_stock_decal_volumes.md) | Projected box decals (paint stripes, stencils): layout, projection, opacity |
| 100 | [Stock ground clutter](100_stock_ground_clutter.md) | GPU clutter (Verdansk terrain only); MP grass is placed props and how the game shades them |
| 101 | [Stock displacement data](101_stock_displacement_data.md) | Per-vertex displacement records, height maps, amplitude; CPU subdivide and displace |
| 102 | [Stock super terrain](102_stock_super_terrain.md) | Verdansk / Quarry terrain asset (stterrain): surfaces, node meshes, height maps, streamed levels |
| 103 | [Stock super-terrain layers](103_stock_terrain_layers.md) | How Verdansk blends its ground materials near the camera: per-layer weight images, reveal ramp, combined-albedo switch |
| 104 | [Screen-space reflections](104_screen_space_reflections.md) | How the game traces reflections (quarter-res screen march on the vertex normal, previous-frame colour) for `...2` techsets, the combine with the probe, the r_ssr dvars, and an editor recipe |
| 105 | [Static model lighting](105_static_model_lighting.md) | How placed props are lit: per-instance light grid volumes (box table in the trzone export, SH probe atlas in the decompressed zone), lightmapped `tml/` props, SELFVIS, the tetrahedral grid, and an editor recipe |
| 106 | [Stock local lights](106_stock_local_lights.md) | How Replay lights a stock map's spot and omni lights at runtime: every one is a runtime light (not in the lightmap), the nearest 256 z-binned per 8x8 tile, the 96-byte light record, tube/sphere shading with IES and the window falloff, exponential spot shadows, per-map counts, and an editor recipe |
| 107 | [Stock shading terms](107_stock_shading_terms.md) | The remaining Game Lighting terms from the shaders: foliage/thin-surface transmission (slot 68 x constant 18), `100a` wrap diffuse, layer colour modes 2/3 (no colour change) and layer gloss/normal, the rough-probe fallback `cb7[186]` and AO strength `cb7[47]` from the Replay exe, emissive (slot 18 x constant 36), with editor recipes |
| 108 | [Warzone reflection probes](108_warzone_reflection_probes.md) | How Verdansk / Quarry keep probes: all records in the main zone, images split by `probeImageIndirection` into shared (streamed XPak images), zone 0 (main `.dec`) and per-tile exclusive buckets (tile `.dec`; the ACTS pixel export is wrong), tile activation lists, the 96-per-view / 128-slot limits and lod-first order, the per-tile counts, and an editor recipe with a v2 cache and a camera-local subset |
| 109 | [Bloom, volumetric fog, sun, post](109_bloom_fog_sun_post.md) | The rest of Replay's MP frame after the tone curve: the veil (bloom) mip chain with its kernels and exe-read weights (energy-conserving lerp by the vision's veilStrength), the froxel volumetric fog (on for MP; 4 depth cascades to 2180 units, HG sun lobe from `hg_lut`, light-grid ambient, composited with the analytic fog before the post chain), why no sun sprite/flare shows on speedball/hideout, the vignette, film grain (x r_filmGrainAtten 0.25), no chromatic aberration or sharpening, per-map values and an editor recipe |
| 110 | [Umbra tome](110_umbra_tome.md) | The Umbra 3 occlusion tome of stock maps (`GfxWorld.umbraTomeData`, ImpTome 0xD6000014): header, tile tree and view trees (2-bit BFS codes with a three-level rank directory), leaf and LOD tiles, 36-byte cells, 16-byte portals (face, internal / coarse / gate / outside bits, plane, rectangle, per-portal growth; gate boxes), clusters, objects (bounds, squared near/far distances, user-ID classes, run-length object lists on mp_m_*), slot paths; the game's portal loop from the DW exe; the core reader `src/core/umbra_tome.*` with an offline portal-rectangle visibility query; per-zone counts and CRCs for eight MP zones and Verdansk |
| 111 | [Analytic fog](111_analytic_fog.md) | Replay's exact distance + height fog (`cs_post_opaque_processes`): vision to refdef (linear Rayleigh/Mie albedos over extinction, desaturation greys the extinction only) and R_SetFrameFog's cb7[131..138] from the exe, the shader step by step (Schlick-type Mie lobe on `fogSunDir`, additional uniform fog, sky band), the fog spline LUT and its blend pass (zero on stock MP), the plain-fog and Warzone circle branches, rust/speedball/hideout numbers against the editor, why rust is too bright in the editor, and the CPU reference `src/core/game_fog.*` |
| 113 | [Material tint modes](113_material_tint_modes.md) | Why the speedball containers draw pink: the lit PS tint rule (cb3[2].x bytes 0..2 = RGB, `lerp(1, tint, slot59.r)` when `cb3[1].x & 64`, flag bit 3 unused, byte 3 unread) re-verified in the 2-layer smodel PS 7e79ed15; the editor reverses NaN constant words read from ACTS `bits` (material_cache.cpp:601), hitting every tint with byte 3 = FF and blue >= 0x80; real vs reversed container colours computed from the BC1/BC4 images |
| 114 | [Custom pixel shader interface](114_custom_pixel_shader_interface.md) | What a hand-written replacement for the stock lit PS must match (world `tw/lit_3_lit_rpl_ta1_804000_...` PS 01631f08, opaque model `tmo/..._800800_...` PS bce5a6cd): ps_5_0 DXBC, the input/output signatures, the b2/b3/b7/b9 + t/s registers and what each technique argument binds, which techniques stay stock (prepass owns RT1 = packed normal), a fxc-verified skeleton ([114_custom_ps_skeleton.hlsl](114_custom_ps_skeleton.hlsl)) with identical signatures, and the traps (pipeline-state hashes and `.psob` bundles, names, root-signature registers, RT1/depth) |
| 115 | [Stock lit shader filter](115_stock_lit_shader_filter.md) | A custom shader on a stock map: a colour filter spliced into the game's compiled lit pixel shaders (DXBC splice, checksum, editzone lit_filter; Shipment) |
| 116 | [Stock zone install](116_stock_zone_install.md) | Why a patch of a stock zone stops the game ("Disc read error [3.1]": signed zones need signed patches) and the install as whole unsigned zones (iw8r_stock, editzone stock_zones) |

## Third-party "iw8 level maker" (report 13)

Used purely as extra reference, with each claim cross-checked:
- **Agrees** with our evidence: canonical key ids 212/709/80/652/1070/1072/845 (re-confirmed in stock `mp_shipment`), the
  five-zone package layout, and the `lighting.json` sun schema.
- **Do not copy:** its entity text is CRLF without the NUL terminator and puts each entity on one line, while stock Replay
  1.20 is LF, NUL-terminated and multi-line. It writes spawns as entities, while stock IW8 keeps them in `spawnList`.
  It uses 5-number classic `.map` faces and a solid-colour BC6H probe.
- No licence in the folder. Its bundled `gsc-tool.exe` is GPLv3 and is not vendored.
