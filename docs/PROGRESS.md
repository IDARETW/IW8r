# Progress note (handoff)

## Latest (2026-11-13)

- M1.4 gtso_lut (2026-11-13): game_look_assets cachedCodePostImage (shared ACTS export of a code_post_gfx image into
  AppLocalData/game_look), loadGtsoLut + decodeBc4VolumeDds (16x16x16 BC4 -> R8), SceneRenderer::setGtsoLut (Texture3D
  R8 at t18, gameSun.w = 1 when bound). ps_tex: so = gtsoLut(max(sqrt(1-ao), sqrt(1-cavity)), a2, sat(Nf . R)) with the
  lut sampler (raw coordinates, as W:796), else min(ao, cavity). GUI test: table read + a one-block decode. Speedball
  8.1 ms, 4 % of pixels change slightly (snowy tree tops, roofs).
- M1.5 core DONE (2026-11-13): core stock_lightgrid (parseStockLgvBoxes from the trzone export, the atlas block cut
  from the .ff.dec right after the box table or by the root's signature, parse / sample / eval; tests with a 544-byte
  speedball fixture). StockModelInstance gets lightingIndex / worldTransient / modelFlags (modelStaticIndirection).
  WorldCache: third output lightgrid.iw8rlg of the main-zone decompress job (needsLightGrid re-runs the job for zones
  cached before; verdicts kept as lightgrid_error; lightGridReady). MainWindow::loadStockLightGrid samples every box
  at its centre into refPropVolLight_ (12 floats: DC, slots 0/2/3); rebuildReferenceProps gives each LIGHTPROBE
  placement of transient zone 0 its box (InstancedTexMesh::light); the instance stream is 24 floats (rows + light);
  vs_tex_inst evaluates E(n) per vertex (as the game) into t with lm = (-2, -2) (no extra interpolant), ps_tex uses it
  x bakedLightScale where no lightmap. IW8R_NO_PROP_LGV = old probe light (A/B). Bench line prop_light; the bench now
  waits for the world cache jobs. Hideout 12,131 / 12,367 lit, 12.9 ms; Speedball 3,476 / 3,476, 8.0-8.1 ms (on and
  off within noise; budget 8). Open: SELFVIS, lightmapped tml props, Warzone tile grids. Research 106 (local lights)
  running in a helper agent.
- **THE PLAN: docs/PLAN_REMAINING.md** (owner-approved 2026-11-13): Milestone 1 lighting (1.1 streamed tiles follow
  the camera, 1.2 HDR target, 1.3 SSR, ... 1.11 build-side lighting), 2 Umbra (decode, view, bake), 3 web viewer
  (streamed editor render, every command), 4 remaining pieces, 5 handover. Work it in order; mark items done there.
- M1.3 DONE (2026-11-13): SSR per research 104 section 8.3 inside ps_tex (24 quadratic steps on the previous frame's
  half-size linear depth t17, hit on the normal-mapped ray, cone mip of the previous HDR scene t16 (4 mips), edge and
  forward fades; b10 prevVP / prevFwd / prevSize.z valid). keepFrameForSsr() after the lit scene (ps_prevz max 2x2,
  ps_prevcolor avg 2x2 + GenerateMips). Flags: base layerP.z, layers layerTint.w (StockMaterialFacts::ssr from
  parseStockTechsetName: hexB bit 1, test for the ...3 glass case). Gloss cutoff g' >= 0.3 (cb7[186] unknown).
  IW8R_NO_SSR, IW8R_SSR_SHOW (state / weight / alpha of SSR pixels), IW8R_SSR_ALL (test: every surface a mirror of the
  hits). Bench line ssr_batches. Speedball: 19 base + 53 layer batches; the visible change at the compare camera is
  under one 8-bit step (dielectric puddles at that angle). Research 105 (static model lighting) landed for 1.5.
- M1.4 part 1 (2026-11-13): the stock normal decode (normalised octa, z = 1 - 0.6|xy|^2), sun specular x cavity, probe
  reflections on layer and decal passes (not the terrain patch: layer params [15] = 1; that cost 3.7 ms on Verdansk;
  IW8R_PERF_OFF=layerprobes). Verdansk raised camera 25.9 ms. Research 104 (SSR) landed (helper agent).
- M1.2 DONE (2026-11-13): HDR scene target. render.cpp hdrTex_/hdrRtv_/hdrSrv_ (R16G16B16A16_FLOAT), sceneRtv_,
  hdrFrame_ (game on, not capture; IW8R_NO_HDR), gameAo.w = 1: ps_tex skips gamePost (sky, lit, multiply layers
  linear), drawPost() (vs_post/ps_post: gamePost where HDR alpha > 0) after the textured props, then plain instanced
  props, flat, lines, overlays to the frame. blend_ alpha = MAX (marks drawn pixels). Meter: ps_meter_hdr on the HDR
  scene (luminance, centre weight). Bench line game_ao ... hdr 1. Speedball 7.5 ms (same), Hideout 12.3 (+1.1).
- M1.1 DONE (2026-11-13): camera-following tiles. Renderer extras are keyed groups (SceneRenderer::setReferenceExtras:
  own VB per group, per-batch state kept for unchanged key+version, world groups = base/layer batches that draw and
  cast; refTexVb_ / refTexNearOnly_ / refExtraOutside), CameraView::setReferenceExtraGroups (shared batches),
  MainWindow::updateStreamedTiles (1 s timer, 1,500-unit move or streamDirty_, 2 builds at a time, drop past 1.3 R,
  LRU eviction by <tile>/used), terrain hole filler (StockTerrainBuildOptions::onlyCutCells, vertex alpha 253, b9
  tile boxes, tileFillHidden) and the fine cutout for the patch while streaming. Bench waits for the tiles at a fixed
  camera (streamed_tiles lines). Verdansk raised camera: 49 tiles, 24.4 ms; Speedball unchanged. Limits: tiles are not
  in the flat / wireframe views, 2D backdrops or picking; decal volumes are not projected on tile geometry; tile
  displacement not applied.
- (earlier) M1.1 groundwork (2026-11-13): owner hint confirmed: with Verdansk's streamed tiles (IW8R_STREAM_AT test path,
  36 tiles within 17,000 of -24900 -1700) the grey sheets are buildings, lots and roads. Done so far: core
  StockWorldSurfaceTable kept on StockWorld + buildStockWorldTile (test stock_world_streamed_tile_from_table),
  readStockWorld/WorldCache::load includeStreamed flag; BC1/2/3/7 stock images uploaded compressed
  (texture_decode loadDdsBlocks, SceneRenderer::createTextureBlocks, texRawAlpha_: raw spec alpha, no alpha test:
  rawSpecAlpha() = batchFlags.y -2 or layerP.y 2, forced at upload; IW8R_NO_BC_TEXTURES); texture budget default
  40 % of video memory (1-4 GB); sun shadows cast from the textured reference's base batches (decals and layers
  skip; IW8R_FLAT_SHADOW_REF for A/B). Diagnostics: IW8R_REF_NEAR="x y r file" (batches and extras near a point,
  OVER = footprint covers it), IW8R_ALBEDO_SHOW, bench lines texture_memory / terrain_patch. Verdansk raised camera
  with tiles 25.9 ms. NEXT: renderer extras as keyed groups (own VB each), world batches in extras, camera-following
  tile loader with stock/streamCacheGB LRU, fine cutout once tiles cover the holes.
- M0 done: screen-space AO (render.cpp kAoShader / drawAo / invert4; depth R24G8_TYPELESS + depthSrv_; ps_prepass
  with the shading pass's cut-out and terrain cut; pre-pass draws props >= 24 px only (bigInst, IW8R_PREPASS_PX), the
  shading pass writes depth; ps_tex reads t14, gameAo in b4). IW8R_NO_AO, IW8R_AO_SHOW, IW8R_AO_RADIUS (40),
  IW8R_AO_POWER (1). Bench: Hideout 12.0 ms (10.1 without), Aniyah Tac 18.1 (14.9), Verdansk ground 24.4, Speedball 7.2.
- Last on 2026-11-13: Verdansk near-camera terrain layers (research 103, core stock_terrain_layers): MainWindow::
  updateTerrainLayers (700 ms timer, rebuild after 2,000 units of movement, radius 6,000, stride 2) exports each
  surface's layer masks with ACTS once, builds the patch on a thread and pushes it as decal extras (terrainLayer
  batches; the base layer writes depth). The coarse super terrain carries vertex alpha 254 and ps_tex discards it
  inside 0.74 x radius (gameTerrainCut, b4; stock reference batches only). GPU clutter on Verdansk drawn (research
  100, core stock_clutter). Bench mp_donetsk "-24900 -1700 -200 45 -12 90": 19.9 ms. Tests: core 1231 / 141991
  checks, GUI 961, smoke ok. Prop LODs: not used, screen culling instead (documented decision).
- Terrain layer radius: default 6,000; IW8R_TERRAIN_LAYER_RADIUS (1,000-40,000; game = 17,368). Measured from a raised
  camera (-24900 -1700 1800 45 -12 90): 21.1 ms / 1.2 M layer triangles at 6,000 vs 23.7 ms / 5.6 M at 17,368 (stride 2
  within 4,000 of a surface, 4 to 9,000, then 8); mean image change 3.9/255, and far water planes show through the
  coarse far mesh. Bench check "picture=DIFFERENT" now prints max / over8: every map gives max 1-4, over8 0 % (auto
  exposure drift between the kept and the rebuilt frame, not a geometry fault). IW8R_BENCH_NEAR=<r> lists the scene
  items near a fixed bench camera. Stock MP maps keep spawns outside the entity string: use "ent node_pathnode N".
- Owner 2026-11-13: Verdansk terrain layers default to 17,368 (game distance); keep the viewport sharp (no TAA blur);
  next: the rest of the lighting (ambient occlusion, reflections, ...). Terrain layer batches skip the 4,000-unit
  extras cull (params x = 1). Patch cells: fine cutout OR the coarse mesh's cell (StockTerrainBuildOptions::
  cutoutStride). IW8R_TERRAIN_LAYER_STRIDE=2|4|8, IW8R_TERRAIN_FINE_CUTOUT=1 for A/B. Raised cam: 21.3 ms, 5.6 M
  layer triangles. Light-grey flat areas at 8-15k from that camera predate this (world ground, to look at).
- Speedball vs the owner's shot, whole ground (out/agent/grass_cmp.py <png>): dirt median 123 vs 125, haze band
  130/136/143 vs 131/137/136. Grass reads brighter and noisier, but dirt shows the same 2.25x pixel-to-pixel contrast
  (17.8 vs 7.9) while plain concrete matches (4.0 vs 4.9): the game image is softened overall by TAA, not a grass
  shading fault. Not imitated (a post blur would blur editing). Placed props now use a soft cut-out (coverage from
  fwidth(alpha), blended, depth written; batch flag y = -1) and foliage LOD-out (last LOD distance; instance cull).
- out/agent/mp_cinderrail is a kept map project (TEST_ME.md for the owner), not scratch.
- Re-checked 2026-11-13 at eye level (Game Lighting): King 2.3 ms, Shipment 4.6, Hideout 9.6, Speedball 5.7. Speedball
  vs the owner's shot (same features): lit concrete +3..9 %, dirt +3 %, far field +10 % (haze), grass -15 %.
- Later on 2026-11-13: displacement drawn (research 101); Warzone super terrain drawn (ACTS fork 8b5ee11d deployed:
  build\bin\Release\data\mw19\schema.json; research 102; core stock_super_terrain); stand-in boxes out of the game views;
  prop screen culling at 4 px (Verdansk 37 -> 15.5 ms). Frame times with Game Lighting: Speedball 4.3-5.7, King 4.0,
  Shipment 5.2, Rust 5.8, Speed 5.7, Backlot 7.1, Hideout 7.2, Emporium 7.3, Fork 7.4, Village 11.4, Verdansk 15.5,
  Aniyah Tac 18 ms. Installer rebuilt (no ini).
- Known limits (updated): terrain layers are blended only within 6,000 units (beyond, the baked albedo, as the game
  does past about 17,000); prop LODs not used (screen culling instead).

- Converted maps' sky in game, proven offline: a build holds only the converter's six sky faces (lightmap UV y 1.25 = kind 1; brush sky faces are skipped), and replay_world_vertex.hlsl draws kind-1 vertices camera-relative at infinity (x64-zt 700d7c6f, 2026-10-22, in the live converter). Checked on mp_iw8r_show2 (built 2026-11-12) with ACTS gfx_map.
- Speed: every map in the install 4-18 ms a frame (Aniyah Tac 17.5, Village 8.9, Hideout 7.2, Speedball 4.3).

- Prop decal volumes (only volumes that miss the world), viewport speed (Hideout 56 -> 17 ms), grass shading,
  research 100 (clutter). Bench: IW8R_PERF_OFF=probes,layers,propdecals,probegrid,instcull; IW8R_DECAL_LOG.
- Displacement now drawn (research 101): displaced meshes subdivided on the CPU (4-inch edges, 1.5 M triangle budget) and moved by their height maps; IW8R_NO_DISPLACEMENT=1 turns it off. Earlier note: displacement (`dn` layers: 0-4.5 % of world triangles, about 2 inches of movement with the
  game's auto height scale; the game tessellates them, the editor shows their normal maps); GPU clutter on Verdansk
  (ACTS cannot export its terrain asset yet: "pointer_extent_required").

## Latest (2026-11-12 evening): Game Lighting against the owner's Speedball screenshot

- Compare spot: IW8R_BENCH_CAM="-1080 230 330 -4 -27 120" (the owner plays at FOV 120, horizontal like the bench),
  out/compare/speedball_ingame.webp. Run out/agent/mapcam.ps1 -Name <dir> -Map <zone> -Cam "<cam>" (copies the exe to
  out/audit_bin; model and material caches shared in %TEMP%\iwb: without IW8R_*_CACHE_ROOT each bench copies 1.2 GB).
- Atmosphere: the vision's fog (core GameFog / gameFogFromVision; fogDensity is the half distance, as the converter
  notes) with Rayleigh / Mie inscatter at fogInscatteringEv, height falloff, sky fog, extinction desaturation.
- Stock sky drawn in Game Lighting: the world's sky material (tw/sky_<map>); its first pixel constant (39.1 on
  Speedball, read from techsets_<map>.ff with ACTS) times the HDR image, undoing the 8-bit preview's exposure.
- Shadows: textured props (trees, poles, crates) now cast; a near cascade (2800-unit box ahead of the camera, slice 1
  of a two-slice shadow array) next to the whole-scene map.
- Layered stock ground (research 97, agent): layers above 0 drawn blended over their base with their own UVs, weight
  and reveal ramp; the metal base layer no longer shows through. Colour mode multiply is not drawn yet.
- Later: per-map layered names (tw4/twc4 numbered blends differ per map) resolved in the map's own zone and cached
  as "<zone>|<name>" (Speedball had taken mp_bm_tut / backlot2 blends); normal maps on the stock world and its
  layers (per-triangle tangents; layer weight in the tangent length); multiply layers (puddles, edge wear); Lit
  Preview on a stock map shows the game look (scene.gameLookAuto); props shade with their model normals; the
  viewport sky box follows the camera (no parallax). Bench lines: game_sky, stock_layers.
- Then: the vision's grading CLUT (gradingclut<name>, PQ space before the RRT) with base exposure +1.0 EV
  (Speedball percentiles match); decals blended in sort order (research 98); material tints from psData row 2
  with the slot-59 mask (REMONT building, blue containers); atmospheric fog by fogHalfPlaneDistance; sky fog by
  fogSkyIntensity and the zenith band; metered auto exposure on stock maps while their sky is drawn; Shipment's sky
  (only tw/sky_* is the sky box; sky_<map> fallback). Bench: out/agent/mapcam.ps1 -Name -Map -Cam (caches in the
  session scratchpad: E: is nearly full).
- Late evening: stock decal volumes (research 99: paint stripes, stains; projected on the world, ACTS
  decalvolumematerial / decalvolumemask, image exporter), King's bakedLightScale (1/64), the sky exporters look in
  the open map's zones (Speed), sky brightness from the world's own sky material, probe fallback for far terrain,
  material cache ACTS runs in a short temp folder (260-character paths), a dangling reference in the stock batch
  builder fixed. Checked every map with a zone in this install (Aniyah Tac, Backlot, Emporium, Fork, Hideout, King,
  Rust, Shipment, Speed, Speedball, Village) from a path-node camera: IW8R_BENCH_CAM="ent node_pathnode N [yaw] [fov]".
  Bench caches live in a short temp path on purpose (AppData\Local\Temp\iwb). Installer rebuilt (no ini).
- Checked: Speedball (against the owner's shot), Shipment (overcast port, fog), Hideout (desert, pale sky),
  King (indoor; exposure meters to EV ~7.5 inside). Open: Speedball wall paint stripes (decal volumes?), Hideout's
  far ridge dark, displacement not drawn (Speedball: 34 triangles, negligible), converted maps' sky in game
  believed at infinity but unseen.

## Resume here (2026-11-12, afternoon)

- Owner goal: finished product (stock map editing, Warzone editing, map and mode creation); no game tests unless
  absolutely needed. Commit and push to IDARETW/IW8r as work lands (identity "IW8 Radiant <iw8radiant@localhost>",
  no co-author trailer). Converter work in C:\zsw-iw8r\scriptables (branch iw8radiant/scriptables), live copy
  E:\mw124\x64-zt fast-forwarded and rebuilt after each converter commit (old exe in out/zt_backup). Live is 43d1d757.
- Read from the game exe today (docs/research): 91 (game type scripts: a map zone cannot replace a stock one, a new
  name can be started, the map script runs after the game type main), 92 (GSC far calls link at load by exact name;
  numbered stock files link, named ones need matching name forms; a failed link drops the match or exits the game),
  93 (a custom map never loads common_br_mp: the extra zone follows the map name in mp/levelZoneParents.csv).
- Built today (all offline-proven, none seen in game): loot clusters and zones on stock and own maps
  (iw8r_loot_cluster / iw8r_loot_zone; tools/warzone_loot_boxes_test.py, warzone_custom_loot_boxes_test.py); mode rules
  (worldspawn iw8r_mode_rules: health, speed, one weapon, infinite ammo); Warzone definitions and the loot item
  definitions of the map's loot table carried from common_br_mp into own maps (tools/br_loot_items_carry_test.py);
  the "no circle" switch no longer writes the unlinkable br_gametypes::disablefeature call; Bridge Two Faces; Blend By
  Slope; terrain blends of stock maps textured (material cache map zone fallback); the feature list pictures redone
  (IW8Radiant.exe --feature-shots); preflight no longer flags methods called on self.
- Later the same afternoon: new game type scripts (worldspawn iw8r_gametype_script, core gametype_script.*, converter
  c5e182f3; far calls checked against data/generated/iw8_stock_scripts.tsv), prefab sharing (bundle zips with
  deflate, plain .map to bundle, wall placement, rotate by yaw step), stock sun colour / intensity (converter
  5ecee331, File > Relight Stock Map Sun), worldspawn keys documented (entity defs + KEYS_REFERENCE). Live converter
  is 5ecee331. Package and installer rebuilt (out/package/IW8RadiantSuite-20261112.zip, out/installer).
- Then: traversals for bots (Primitives > Traversal For Bots, core traversal.*, nine stock animations). All eleven
  offline proofs pass against the live converter (tools/run_offline_proofs.py <work>; ~5 min). Installer rebuilt.
- Then: removing placed objects (buy stations) from stock maps (converter aacfad65, live; tools/
  warzone_instances_remove_test.py); door moves researched (tools/dynent_pose_rules.py, research 89 section 8 item 3).
- Then: moving placed objects with dynamic entities (converter e7d8d0e3 / 1396c861, live): ordinary ones get pose,
  partition and chain; doors move inside their area. tools/dynent_move_test.py. Installer rebuilt.
- Then: stock sky from another map (iw8r_sky_from) and retexturing (iw8r_stock_retexture), converter 2594f62e
  replace_materials (live); xmake now passes /bigobj (main_window.cpp hit the section limit).
- Then (owner asked: lightmap baking plus a viewport that matches the game): core ray_bvh.* and lightmap_bake.*
  (charts, atlas, sun / lights / sky / one bounce, IW3 pair encoding), the build hook (worldspawn iw8r_bake_lightmaps,
  compile_iw8.cpp before the surface JSON). Editor maps draw with the converter's own world shader
  (map_surface_realtime.hlsl, not the stock tw/ shader): light = lightmap (A N.z + B sat(N.D)) + realtime sun x
  visibility + realtime local lights, so the bake leaves the direct sun and lights out (research 95 section "Editor
  maps in game"). Proof tools/lightmap_bake_build_test.py (in run_offline_proofs.py).
- Then View > Game Lighting (Ctrl+Alt+L): core game_look (exposure, ACES LUT, converter light units), viewport
  game_lightmap.* (background bake of the camera's polygons, UVs found by position / normal / polygon centre),
  render.cpp ps_tex game branch (b4, t6-t9), camera_view prepareGameLook / maybeBakeGameLook. GUI test section "game
  lighting"; --viewport-bench has game_still / game_moving (synth_10000: 11.5 ms still, 8.0 ms moving; textured 10.7 /
  7.1). The owner asked for game-like frame rates (about 200 fps on Shipment). Not compared with the game: the
  absolute calibration (EV 10, +1.5 EV base, light window scale 1) is INFERRED (research 95).
- Then stock maps in Game Lighting: core stock_lightmap (decompressed-zone root scan, BC4/BC5/R11G11B10F/RGBA8,
  lightmap.iw8rlm cache), stock_world lightmapUv, WorldCache lightmap job (requestLightmap / lightmapReady), reference
  vertices with UVs, render.cpp stock branch (batchFlags2.z/.w, setStockLightmap half floats), core game_look
  stockMapLooks (shipped maps' sun + auto exposure) and the renderer's metering pass (setGameMetering,
  takeMeteredLuminance). Bench reference scene loads the zone's lightmap. Open: Warzone (type 0, BC6H, streamed);
  AB_LIGHTING's extra channels; own brushes inside a stock edit are baked in editor units, not the stock map's;
  stock probes / specular / retro-reflective diffuse are not drawn.
- Then copy / remove of placed objects with dynamic entities (x64-zt 45112b43 + 8ed35d9a, live; old exe
  out/zt_backup/zonetool_2594f62e.exe): core stock_placed allows them, tools/dynent_copy_remove_test.py (in
  run_offline_proofs.py); warzone_instances_remove_test / _edit_test updated for the new behaviour. Doors follow an
  exact walk of MapEnts.dynEntSpatialPopulation[1] (319 of 319 on Quarry), so the door-move area box could be replaced.
  Checked: an editzone output of Quarry loads one asset fewer than the stock zone in ACTS (73054 vs 73055) because
  ACTS loads the stock .ff plus its .fp, whose patched scriptable is counted twice (base + override); the rebuilt zone
  holds the patched copy once. The exported scriptable lists are identical (62 / 62): not a loss.
- Then stock sun direction (x64-zt f9c14d9f, live; old exe out/zt_backup/zonetool_8ed35d9a.exe; the live checkout's
  gitignored build/convert-iw3-iw8.vcxproj got stock_sun.cpp by hand, as the worktree's): editzone sun.direction,
  src/convert/iw3_iw8/stock_sun.*, replay_sunshadow Bake tolerance; editor iw8r_sun_direction, Relight dialog field;
  tools/stock_sun_direction_test.py in run_offline_proofs.py. Compile panel Bake lightmaps checkbox.
- Then stock world removal (x64-zt 373d6ad2 / 626bfcc9, live; old exe zonetool_f9c14d9f.exe; live vcxproj got
  stock_world_remove.cpp by hand): iw8r_remove_world brush class, editzone remove_world_surfaces, research 96,
  tools/stock_world_remove_test.py in the proofs. Stock reflections: core stock_probes (BC6H probes from the
  decompressed zone, probes.iw8rprb), WorldCache probesFile / probesReady, render.cpp stockProbeLight (box parallax,
  priority order, feather, two mips), game_look_assets (ggx_env_brdf_lut from the install via ACTS, cached in
  AppLocalData/game_look). Ground War (11.10) deferred by the owner's scope decision.
- Then occlusion for removals (x64-zt 78f04f41, live; old exe zonetool_626bfcc9.exe): when removed triangles belong
  to Umbra occluder surfaces (flag 64), editzone replaces the Umbra tome with the converter's conservative one-cell tome
  (replayrender::BuildConservativeUmbraTome, stock objects and user ids kept); key "occlusion" auto / open / stock.
  Local tile editing needs the Umbra portal encoding decoded (not done).
- Then Warzone lightmaps: type 0 (BC6H C), one lightmap per transient zone (research 94 section 8); stock_lightmap
  reads many, stock_world keeps lightmapIndex per vertex (and fixed: Verdansk surfData as hex bytes, vertex sharing
  across tiles); WorldCache lightmapFile(zone, index) / lightmapIndices, tile jobs after streamed exports;
  game_lightmap stockLightmapAtlas packs them; reference vertices carry index + u; render.cpp LightmapRects (b6).
  Checked: mp_br_tut (1 lightmap, 125,825 vertices), mp_donetsk with 12 tiles (13 lightmaps, all 391,888 lightmapped
  vertices have their file; atlas 2048 x 1282; 0.44 ms per frame). mp_donetsk uses the mp_br_quarry look.
- Then the owner reported about 5 fps (Game Lighting on and off) with mp_rust open as a stock map. Reproduced with
  --viewport-bench stock:<zone .ff> (new; opens the zone as File > Open Stock Zone does, ACTS kept on) and the new
  moving_window scenario (cameraMoved + event loop each step): 182 ms per step, the grid views drawing every model
  triangle. Fixed (grid_view.cpp: models as boxes unless selected; camera moves repaint only the marker area):
  about 6 ms per step for the window. The camera itself draws about 17.9 M triangles on mp_rust (7 ms on an idle GPU).
  The owner accepted the handover (2026-11-12) and is now doing some of the optional in-game looks (docs/HANDOVER.md
  section 4, plus the frame rate after restarting the editor) before calling it finished: wait for that report and fix
  what comes back.
- Tests at the last commit: core 139,820 checks / 0, GUI 948 / 0.
- Open (in scope): copying or removing a placed object that has a dynamic entity;
  11.5 removing stock world geometry; 11.6 the sky and the sun direction of a stock map; 11.10 Ground War scale.
- Disk: E: had 3.5 GB free; this session's scratch output under out/ was deleted (owner copies kept).

## Resume here (2026-11-11, after the owner playtest)

- **Round 3 (2026-11-11 evening): second playtest fixes and L12 asset editing.** Read docs/ROADMAP.md "L12. Asset
  editing depth" and the F rows (each ends with "After playtest 2"). The owner will not test until the suite is assumed
  complete. Helper agents (Haiku) follow docs/HELPER_RULES.md: own files only, builds one at a time under the
  out/build.lock folder (mkdir to take, rmdir to release). The main session takes the same lock.
  - Damage: radiusdamage(player.origin, 32, dmg, dmg) (map_script.cpp, trigger poll and damage zones); dodamage had no
    effect in game.
  - Moving brush models: compile_iw8.cpp compileIw8ExtractionBrushVisuals (chain: Extraction > Movers > BrushVisuals >
    Variants > Flat) builds a model from the brush faces, registers it like a stock model copy, adds a script_model
    "<name>_vis" (iw8r_no_collision) and the script links it (iw8r_link_visual). The brush keeps collision; key
    iw8r_no_draw drops its surfaces.
  - iw8r_no_collision 1: a script_model the script only names (level.iw8r_keep) so the converter keeps it live.
  - Make Ladder: thin slab at x +2.2..+4.2 from the model origin, model tm/ladder_old_wood_01_<72|144|160>.
  - Prefab browser: drag re-enabled after setMovement(Static); thumbnails use PrefabBrowser::setModelMesh.
  - Shader state: CustomMaterial blendMode / unlit / emission / tint / sortKey / polygonOffset; customMaterialJson
    writes stateBits + stateBitsEntry (technique 7 lit, 4 unlit), constants colorTint / emissionScale (research 68).
  - Helper modules wired into Tools: Make Editable Copy of the Current Material (material_clone), Adjust Image
    (image_adjust), Techset Info (techset_detail / techset_dialog), Asset Usage, Sound Settings (sound_settings, read by
    the build next to each own sound).
  - The JSON reader is src/core/json_lite.h (HELPER_RULES.md said json.h; fixed there).
  - One GUI check ("reference culling ... picture identical") is timing-sensitive since props are textured: it failed
    once and passed on rerun. Tests: core 1031 / 0, GUI 585 / 0.
  - Check map out/agent/mp_iw8r_fix1 rebuilt (0 failed assets). Scratch scripts for it were lost; fix1_add2.py and
    fix1_repair.py in the scratchpad made the last additions. Inserting a brush into the worldspawn by text: put it
    after a brush's closing brace, not before the first closing brace found.
  - Second helper wave: techset_export (requestTechsetExport, wired into Techset Info), material_replace (Tools >
    Replace a Material in the Whole Map), vision_fields + vision_editor_dialog (Compile > Vision Editor, 218 fields).
    Model slots: key iw8r_mat:<model material> (compile_iw8.cpp Variants, scene.cpp buildModelGeom, Tools > Replace One
    Material of the Selected Model). Camera tint for custom materials (CameraView::setCustomTintSource; it must stay in
    a plain public section of the header: under "public slots" moc tries to register it and the build breaks).
    Never name a variable `slots` in Qt code. The GUI culling check now re-renders until two rounds agree.
  - Owner report (same evening): the built-in glass showed the missing-material checker in the camera. Its image only
    existed inside a build folder; the camera now makes its own copy under the app data folder and draws it blended
    (main_window.cpp setMaterialImages / blendFor). blendFor also returns additive for custom additive materials.
  - Tests: core 1051 / 0, GUI 588 / 0. Check map rebuilt. Package and installer rebuilt (out/package, out/installer).
  - Hinged doors (12.13): map_script.h DoorHinge / doorHingeOf, Door::hinged / toggle; compile_iw8.cpp parses
    "hinge <angle> [time]" and "rotate <angle> [time] [hold]" (both about the hinge now: the old rotate turned about the
    map origin) and gives a door without a trigger its own use volume "<name>_use"; scene.cpp previews the swing;
    Primitives > Scripted Objects > Make Hinged Door. The check map's door1 is now "hinge 100 0.6".
  - The asset dialogs have create...() seams (no exec) and are driven by src/qtapp/qt_gui_test_assets_a.cpp / _b.cpp,
    called from qt_gui_test.cpp; Make Editable Copy is tested on a real cached material. Tests: core 1052 / 0, GUI 630 / 0.
  - Live shader preview (12.12): core material_preview + MaterialPreviewWidget in the Material Editor
    (material_editor.cpp refreshPreview fills a MaterialPreviewInput). Software renderer; not identical to the game.
  - Impacts (research 71): x64-zt fork commit 062ec6b4 (replay_havok.cpp SurfacePhysicsMaterialCrc: the shape tag's
    physics material follows the surface type; CRC-32 of names such as PM_Wood). zonetool.exe rebuilt with MSBuild
    (build\x64-zt.sln, Release x64). After a converter change delete <map>/iw8build/<id>/zones.key, or the editor
    reuses the old zones. Model collision hulls get surfaceOfModel() in compile_iw8.cpp.
  - Sound (research 70): the converter's role gain (loops 0.18) is divided out in customSoundAliasJson
    (converterRoleGain). Tests: core 1068 / 0, GUI 631 / 0.
  - Research 72 (reverb: the test zone was above the player; no tool fault), 73 (probes: flat grey images unless
    captured; CompilePanel::setBeforeExtraction now captures before every build), 74 (modes: the proxy hard-codes
    "war"; needs a proxy change). A Fable agent is writing docs/research/76_cold_start_devmap.md (start a devmap run
    from a closed game; owner request).
  - Research 75 (killstreaks): compile_iw8.cpp writes the DPVS cell bounds grown by worldspawn iw8r_airspace
    (default 6144 up, 2048 out); world bounds and the surface tree stay tight.
  - OWNER CORRECTION 2026-11-11: the proxy DLL is not installed. The game runs from the recompiled
    game_dx12_ship_replay_dw.exe / _local_dw.exe (a .map and .pdb sit beside it). Research that cites the proxy source
    (03, 05, 19, 74) is unproven for it. The editor still writes .proxy\control.ini on deploy.
  - COLD START (roadmap 7.14; research 76 and 77; owner: "the map loading screen should be the first thing i see").
    Editor side built: core devmap_plan (xpartygo 1, LaunchLog sign-in signal, pickLaunchExe, healthIsDwHost,
    startRequestArgs, coverWanted), qtapp devmap_launch (100 ms tick, host check and start, exe choice, timeline
    stamps "F5 +N s:", DW_DEVMAP client lines) and devmap_cover (the card over the game window; real-desktop check:
    IW8Radiant.exe --cover-check --out <file>). Client side is only a proposal in docs/research/77_client
    (DW_Devmap.cpp + 2 diffs for E:\DW\IW8). DO NOT apply, build or deploy it from this session: the normal client
    build (build-game.ps1) PUBLISHES a release to the primary server; -ValidateOnly compiles without publishing.
    The owner decides. Patch scripts of this round are kept in tools/patches/. Tests: core 1071 / 0, GUI 647 / 0.
  - Client side of the cold start: DW_Devmap.cpp and its two registration lines ARE now in E:\DW\IW8 (owner: "get
    the devmap integration ready to use"). The non-publishing client build is blocked by the DW project's own state
    (see memory dw-client-build-rules); the owner has to build and install the client. An Opus agent is writing
    DW_MapDebug.cpp (a map-development debug overlay dvar) and docs/research/78_map_debug_overlay.md.
  - Proxy leftovers removed from what the mapper sees: deploy and launch messages no longer name the F6 browser; the
    Live Link dock hides the proxy sections and writes no control.ini unless XInput9_1_0.dll is in the game folder
    (LiveLinkPanel::proxyInstalledIn; setting live/proxyControls forces them on); Quick match starts the match
    (7.12). Tests: core 1071 / 0, GUI 652 / 0.
  - GAME MODES (F.11, research 79): the running client hard-coded "war" per custom map row
    (DW_MapAvailability.cpp AppendCustomMaps). Client now reads map.json "gameTypes" (applied in E:\DW\IW8, diff in
    docs/research/79_client, compile-checked, not built). Editor: core map_gametypes (detection + iw8r_gametypes
    override + withGameTypes), CompilePanel::writePackageGameTypes after every finished run, Map Info field.
    Tests: core 1074 / 0.
  - MAP DEBUG OVERLAY (roadmap 7.15, research 78): client DW_MapDebug.cpp (Opus agent; in E:\DW\IW8, registered,
    compile-checked, not built). Editor: core map_debug_dump (presets, filter cleaning, dump parser,
    compareDumpWithMap), LiveLinkPanel Debug overlay row + requestEntityDump / dumpReport. The overlay also is the
    tool for the open questions: crosshair surface type (F.12), audio zone (F.7), entity survival (spawns, corners).
    Cover card stall limit: devmap::kCoverStallMs (150 s). Tests: core 1077 / 0, GUI 656 / 0.
  - WHAT THE OWNER MUST DO for all three client features (cold start, game modes, overlay): build the client from
    E:\DW\IW8 and install it. This session cannot: the non-publishing build is blocked in prepare (memory
    dw-client-build-rules) and installing writes into the game folder.
  - CLIENT BUILD STATE (2026-11-11 evening). Owner: "we should be updating game_dx12_ship_replay_local_dw.exe, the
    droplet is down and we dont need to publish anything there". tools/dw_candidate_build.py gets through prepare
    (job E:\DW\IW8\data\dw-client\jobs\1794440667375391600 is prepared and kept) and then stops at the start of the
    build: "Configured codec tool is unavailable: C:\Users\user\AppData\Local\ReplayCompilerWorkstation\
    script-source-packs\mw169_mp_frontendc3s5\manifest.json" (sha256 86dd0e83... in compiler-base
    replay-project.local.json). That folder is an owner-supplied pack; AppData\Local last changed on 2026-11-08 16:10,
    so it went then (not found on C:, D:\offload or E:). NEXT: when the owner has put it back, run
    dw_candidate_build.py --resume <that job>, then dw_candidate_install.py <that job> (backs up, replaces the game
    folder dw exe + map + pdb, remakes the local exe). E: was full (0 bytes) during this: a full build needs 9 GB.
    Freed: old out/gui-c* folders, out/audit-bin*, this session's failed job folders; a Haiku agent cleaned further
    (out/cleanup_report.md).
  - MATERIALS ON OWN MODELS (F.16 third report): brush_tools applyMaterialToSelection now also sets iw8r_material on
    selected model entities; compile_iw8 CompileOptions::customModelVariants + the Variants pre-pass build a
    re-skinned copy "<model>_m<hash>" of a model-folder model. (A struct field named "slots" breaks Qt builds:
    it is bySlot.) Tests: core 1078 / 0.
  - CLIENT BUILD: the pack was never missing. Builds run from the Codex app see AppData\Local through its package
    folder (...\Packages\OpenAI.Codex_2p2nqsd0c76g0\LocalCache\Local). From this session a junction
    C:\Users\user\AppData\Local\ReplayCompilerWorkstation -> that folder is needed while building; remove it after
    (cmd /c rmdir, never a recursive delete). Link error fixed: data globals that are not in the linker catalog
    must be declared as gl_<older-build VA> (DW_MapDebug.cpp). The pipeline can also stop with "SQLite committed
    while checking snapshot reuse" when another tool writes the analysis database: just run it again.
  - CLIENT EXE INSTALLED 2026-11-11 19:22 (unpublished candidate b77d6e60..., job
    E:\DW\IW8\data\dw-client\jobs\1794441713356806500). How: tools/dw_candidate_build.py (prepare + compile + link),
    then the game folder dw exe was swapped to the published release for verification (one gate,
    test-gulag-carryforward-integration.py, reads the installed exe in its own process), then
    dw_candidate_build.py --verify <job> --waive-gameplay (five integrity gates; test-map-publication.py needs the
    droplet, which is down; 23 gameplay gates had passed before it in a full run, list in
    out/dw_candidate_full_run_progress.json), then tools/dw_candidate_install.py <job>. Previous exe + map + pdb:
    out/dw_backup/20261111-191715-nov8-candidate (rollback: dw_candidate_install.py --rollback <that folder>).
    The junction to the Codex toolchain folder was removed again. data/dw-client/latest-job.json now names this job
    as awaiting publication: nothing was published. NOT RUN in game.
  - TEST FIXTURES: out/m2-x64zt (x64-zt dumpzone mp_shipment) and out/m2-script-check (ACTS map_ents dump of the
    stock mp_shipment zone) are read by core tests, which skip silently without them. They were deleted by mistake
    during the disk cleanup of 2026-11-11 and regenerated the same evening. Core check count with them: 136,686.
  - F5 options in Edit > Preferences (DevmapRunner::PlayOptions / playOptions / setPlayOptions over the devmap/*
    settings). Tests: core 1078 / 0 (136,686 checks), GUI 663 / 0.
  - 4.25: data/asset_catalog/iw8_streamed_images.tsv now covers 21 stock MP zones (19,396 images; Haiku helper;
    per-zone tables + merge.py kept in out/streamed_exports). Test summary now prints "N test part(s) SKIPPED" when a
    fixture is missing (tests/test_harness.h test::skipf). Packaged editor checked standalone: it compiled the
    sample room from out/package/IW8RadiantSuite (ok).
  - F5 BUG FOUND BY THE OWNER (2026-11-11 20:00, "the game never starts when pressing f5"): the host probe sent
    HTTP/1.0 and the real host answers that with 400 invalid_request, so F5 started the host, never recognised it and
    gave up after 60 s. Fixed: HTTP/1.1 with Host (devmap_launch.cpp probeHost); the GUI fake host is now as strict.
    New real check that starts nothing: IW8Radiant.exe --f5-check --out <file> (exe pick, start-request marker,
    host probe against the real host). Lesson: check a network client against the real server, not only a fake.
  - RESEARCH 80 (decal volumes, 5.8): plan ready, converter producer of 300 to 450 lines + entity iw8r_decal; not
    built. RESEARCH 81 (heightfield, 5.25): closed as explained; the converter already writes it (rain occlusion
    only). Side fact from 81, not studied: editor-built maps have voxelTreeCount 0 where stock mp_shipment has 1.
    The IDA MCP tools did not answer either agent; both read the exe on disk with a disassembler.
  - DECALS (5.8): editor half done (compile_iw8.cpp decal loop -> world.json decal_volumes; entity iw8r_decal in
    the overrides JSON; build_preflight skips decal_volumes when it collects surface materials). Converter half: an
    agent works in the worktree C:\zsw-iw8r\decals on branch iw8radiant/decal-volumes and will write research 82;
    it must not touch E:\mw124\x64-zt or the live zonetool. Adopting it = merge the branch, rebuild zonetool when the
    owner is not mid-test, delete zones.key. Tests: core 1079 / 0 (136,702 checks).
  - OWNER PLAYTEST 3 (2026-11-11 night): F5 worked after the probe fix (the new client exe ran and the map loaded;
    the owner did not report on the cold start screens, the overlay or game modes). Reports: (1) re-skinned shape
    cubes had no player collision: fixed, compile_iw8.cpp boundsOf resolves a variant to its base model; (2) their
    texture is upside down in the editor camera only: fixed, main_window.cpp flips v of imported models for the
    viewport; (3) the door must be a real stock door delivered as a prefab, and prefabs must be bundles (model +
    script behaviour + data, drag and drop, real geometry in the viewport): research agent writing
    docs/research/83_stock_door_and_prefab_bundles.md. Do NOT run the GUI suite while the owner's game runs: some
    checks read the running game (one fails: "without a game the Live Link commands say why").
  - RESEARCH 83 (stock door + prefab bundles): a real stock door is a native scriptable instance; needs converter
    steps F1-F4 (rows 6.14 / 6.24). Bundle format designed: folder <name>.iw8prefab with prefab.json, prefab.map,
    thumb.png, script.gsc, fallback.*, materials/ models/ sounds/ effects/; per-placement iw8r_pid and name
    scoping; script included once per prefab kind. Editor work list with sizes in section B of the document.
    AGENTS RUNNING at the time of writing (2026-11-11 21:00): showcase 2 (tools/make_dev_showcase2.py,
    docs/DEV_SHOWCASE.md), decal producer (worktree C:\zsw-iw8r\decals, research 82), stock prop loading fix
    (research 84; edits model_cache / acts_runner / two functions of main_window.cpp), viewport optimisation
    (research 85; owns src/viewport and camera_view), prefab bundle core (see below).
  - SHOWCASE 2 (owner: "make the showcase tighter with clear indication of what the current asset is, and start
    adding more asset types"): samples/dev/mp_iw8r_show2.map + .gsc (93 numbered stations in a 2432 x 2784 arena,
    sign boards with seven-segment numbers, a message per station, names s<NN>_...), generator
    tools/make_dev_showcase2.py, test sheet docs/DEV_SHOWCASE.md. Headless build ok (0 preflight errors, 0 failed).
    The old showcase samples/dev/iw8r_dev_showcase.map is untouched. The file is named after its map id because
    the id comes from the file name cut to 15 characters (iw8r_dev_showcase2 would have collided with the old one).
    Lesson from it: a stock model used only by dynamic_prop is not fetched for the build (worked around there).
  - SCRIPTABLES (owner: main session does this itself): see docs/research/86_scriptable_objects_plan.md; worktree
    C:\zsw-iw8r\scriptables, commit ce642bde.
  - STOCK MAP GEOMETRY (F.17 third report, research 84): MainWindow::openStockZone now calls loadStockReference
    (the menu path never did; earlier checks used the IW8R_SMOKE_REFERENCE switch). Helper agent: marker cubes for
    props not ready / failed, permanent status label (stockPropStatus / updateStockPropLabel), ModelCache batch
    chain (startBatch / finishBatch / batchEnded), core stock_prop_fetch.*, headless --stock-props-test <zone> and
    --model-chain-test. Lesson: verify the path the owner actually uses, not a test switch.
    Tests at 21:45: core 1103 / 0 (137,302 checks), GUI 664 / 0.
  - PREFAB BUNDLES built (owner request; design research 83; format docs/PREFAB_BUNDLES.md). Core:
    src/core/prefab_bundle.*, name scoping (iw8r_pid), MapScriptExtras::prefabScripts, compile expansion,
    prefabBundleFromSelection; shipped bundles data/prefabs/bundles/gameplay/jump_pad.iw8prefab and
    doors/stock_door_wood_single.iw8prefab (its main contents are the native door = waits on the scriptable
    converter work; the script-driven fallback builds today). Window: Prefabs dock lists bundles with badges,
    drag / drop / clone give new pids, Entity dock parameter panel (prefab_bundle_panel.*), Save Selection As
    Prefab (prefab_save_dialog.*), Explode / Edit / Open Bundle Folder / Make Unique; prefab members of the main
    window moved to src/qtapp/main_window_prefabs.cpp (main_window.cpp hit compiler limit C1128).
    STILL OPEN: a placed prefab draws its models as 16-unit boxes and its brushes in one flat colour in the camera
    (scene.cpp buildPrefab); to do after the viewport agent finishes (it owns scene.cpp), with an Opus agent.
    OWNER 2026-11-11 night: "use opus agents from now on". Tests: core 1110 / 0 (139,077 checks), GUI 846 / 0.
  - LATE 2026-11-11 (goal: finished product, no more game tests):
    DECALS: the editor writes a decal-form twin (<name>_dcl) of a library material named by an iw8r_decal
    (compile_iw8.cpp customDecalMaterialJson, decalCustom); live converter = fork 8ff480fa then 04995144.
    SCRIPTABLES (research 86): converter commit 04995144 on iw8radiant/scriptables, fast-forwarded into the live
    iw8radiant/build-extraction and built (old exes: out/zt_backup/zonetool_062ec6b4.exe, zonetool_8ff480fa.exe).
    Editor: src/core/scriptable_catalog.*, CompileOptions::scriptables, class scriptable in
    data/iw8_entity_defs_overrides.json, catalogue data/asset_catalog/iw8_scriptables.tsv. Tools:
    tools/acts_export.py, build_scriptable_catalog.py, scriptable_counts.py, scriptable_dynents.py,
    scriptable_offline_test.py (exports kept in out/scriptable_study, about 21 maps). Only hardpoint_chevron is
    placeable; F3 (carry a definition into a map: doors, cones, cars) is NOT done and needs game tests.
    FIXES: stock model used only by dynamic_prop is now fetched for the build (compile_panel.cpp); File > Package
    packs placed prefab bundles whole (map_package.cpp resolveBundle / addBundle); a duplicated block in
    compile_panel.cpp removed. build_cache needs no change: the converter cache key hashes the whole extraction.
    Shell note: in this Bash tool a heredoc turns two backslashes into one and can fail on quotes; write patch
    scripts with the Write tool (tools/patches/*.py).
  - STATE AT THE END OF 2026-11-11 (handover build):
    Viewport (research 85, two helper agents): kept geometry with ranged uploads; prefab / bundle models, a
    dynamic_prop and a scriptable draw as models; editor markers out of shadows and captures. New files:
    src/viewport/camera_geometry.cpp, dirty_ranges.h, reference_markers.h, scriptable_model.h,
    src/qtapp/qt_gui_test_prefab_models.*; benchmark tools tools/run_viewport_bench.py, make_perf_maps.py,
    compare_bench_images.py, viewport_bench_table.py (data in out/perf, 1.4 GB, can be deleted).
    Known limits from the helper: a nested prefab resolves its model path like a top-level one; a prefab
    placement modelscale moves but does not resize inner models (same as the build); brush faces inside prefabs
    are untextured in the camera.
    Tests on the final build: core 1114 tests / 139,154 checks / 0 failures, GUI 879 / 0. Headless builds:
    mp_scrtest (3 scriptable instances) and mp_iw8r_show2 (decal built) ok, every loader line 0 failed; the
    packaged editor builds its sample map.
    RELEASE: out/package/IW8RadiantSuite, out/package/IW8RadiantSuite-20261111.zip,
    out/installer/IW8RadiantSuite-Setup-0.1.0.exe, all rebuilt after the last source change.
    NOT DONE, needs the game: real stock doors / cones / cars (research 86 step F3); everything marked
    "not seen in game" in docs/TEST_CHECKLIST.md.
  - 2026-11-12, REAL STOCK SCRIPTED OBJECTS (research 86 step F3 done offline):
    Converter commit 02182e8a (branch iw8radiant/scriptables, fast-forwarded into the live iw8radiant/build-extraction,
    live zonetool.exe rebuilt; old exe out/zt_backup/zonetool_04995144.exe). New in the fork: zone/writer
    asset_closure / zone_asset_name / rewrite_fastfile with a prepare hook; convert.cpp CarryScriptableDefinitions,
    NewAdditions, writeZone prepare; ScriptableInstance::carryZone (JSON carry_zone).
    Editor: setting build/realScriptables (Preferences checkbox, env IW8R_REAL_SCRIPTABLES=1/0 overrides for one
    run), PrefabBrowser::realScriptables / buildSupport, CompileOptions::carryScriptables,
    ScriptableDefinition::carryZone / carryable / door, MapScriptExtras::stockDoors (system_init call).
    Proofs: tools/scriptable_carry_test.py (door, cone, car equal the stock definitions read back with ACTS),
    tools/scriptable_offline_test.py (no-carry maps byte-identical), out/scr-door (the door prefab end to end,
    switch off = fallback, on = main contents, 0 failed).
    Also: brush faces inside placed prefabs are textured in the camera (scene.cpp addPrefabContents).
    NEEDS ONE GAME LOOK: docs/TEST_CHECKLIST.md, first "In game" section about real stock doors.
  - 2026-11-12 (later): converter db5914c2 (live): iw3compat_minimap() also sets level.mapsize / level.mapcorners.
    Old live exes are in out/zt_backup (zonetool_<commit>.exe: 062ec6b4, 8ff480fa, 04995144, 02182e8a).
    Rows 5.10 and 5.11 reworded to what is built (one cell; light grid yes, lightmaps no).
    WHAT IS LEFT is of two kinds only: (1) looks in game (docs/TEST_CHECKLIST.md; the real stock door first),
    (2) large features that need converter work plus game iteration and were not started: lightmap baking,
    portal / cell visibility, editing stock maps (11.4, 11.5), Ground War scale (11.10).
  - 2026-11-12, OWNER DECISIONS (asked with two questions): (1) the finished product must include "Stock map
    editing, warzone-specific editing and map/mode creation" (not lightmaps, not Ground War scale); (2) real stock
    scripted objects: "Turn it on by default" (done: build/realScriptables defaults to on; GUI tests force it off
    and switch it on where they test it).
    STOCK MAP EDITING (research 88, working record): converter 3c6606d6 (live; old exe
    out/zt_backup/zonetool_db5914c2.exe) adds `zonetool editzone <zone> <edits.json> <out>`: entity text with spawn
    entities compiled into the spawn list, hide_static_models, carry of assets from other stock zones. Editor:
    src/core/stock_zone_edit.*, src/qtapp/main_window_stock_edit.cpp (prepareStockEdit, rebuildStockZoneFast),
    File > Rebuild Stock Zone. Proofs: tools/stock_edit_test.py, tools/stock_edit_editor_check.py.
    Next for stock maps: a command to pick props to hide, install / restore actions the owner runs, added brushes
    as a carried model, the same for the Warzone maps.
    WARZONE: research 87 (survey by a helper agent) is the starting point; nothing built yet.
  - 2026-11-12 (stock map editing, continued): converter 1ad5320d live (old exe zonetool_3c6606d6.exe): editzone
    writes .fp patches. Editor: File > Hide Stock Props In Selection / Show All Stock Props, File > Install Edited
    Stock Map Into Game / Restore Stock Map In Game (src/core/stock_install.*, main_window_stock_edit.cpp:
    lastStockRebuild_, stockBackupDir). The two install actions are for the owner only: never run them from a
    session. Research 87 (Warzone survey, by a helper) is written; tools/warzone_*.py are its decoders.
  - 2026-11-12, WARZONE AND GAME MODES (roadmap L13, research 87): core game_modes.* (presets), devmap_plan
    startMatchCommands with mode dvars, map_script modeDvars / br switches (worldspawn iw8r_mode_dvars, iw8r_br),
    map_gametypes br detection, src/qtapp/main_window_game_modes.cpp (Tools > Game Modes: showGameModes,
    bakeGameMode, insertWarzoneStart), DevmapRunner::setModePreset / modeDvarsSetting. Converter (live, branch
    iw8radiant/scriptables = iw8radiant/build-extraction): f8b4d871 (br_gametypes::disablefeature callable),
    bee970c0 + 6270d567 (iw3compat_setdvar passes "iw8r:"-marked names, case kept). Test map out/scr-br/mp_scrbr.map.
    The Warzone presets copy the dvars of E:\DW\IW8\data\playlists800\catalog.json (read only).
    Old live converter exes: out/zt_backup/zonetool_<commit>.exe (one per step).
    OPEN for Warzone: loot points (compiled spawnGroupLoot, not shown or written), buy stations and vehicles on a
    custom map, Verdansk streamed zones in the reference view, new game type scripts (13.6, needs the game).
    OPEN for stock maps: added brushes as a carried model (research 88 next steps).
  - 2026-11-12 (later), LOOT POINTS (roadmap 13.4 / 13.5, research 89 = the Warzone work record): converter live
    at 6e1b1eb5 (old exes out/zt_backup/zonetool_6270d567 / 93b027ba / 5798fb0e.exe). editzone "loot_points"
    (stock maps; lists and counts rebuilt by the stock rules, exact on Quarry and Verdansk), collision JSON
    "loot_points" (own maps; free / cache points, no clusters or zones), and a writer fix: a fresh rewrite keeps a
    definition whose name stub was used first (Quarry lost scriptable ks_ac130_mp before). Editor: core stock_loot.*
    (reader, iw8r_loot_point entities, edits list), File > Edit Loot Points (main_window_stock_edit.cpp
    editLootPoints), prepareStockEdit writes the list when the document holds loot point entities (none = the map's
    loot stays; worldspawn iw8r_no_loot 1 = empty list), compile_iw8.cpp writes own-map points. Also fixed: an
    unedited stock map carried models its own entities name (stockEditCarryList stockModels; mp_br_tut gave 13).
    Proofs: tools/warzone_loot_edit_test.py, tools/warzone_custom_loot_test.py, tests/stock_loot_tests.cpp,
    src/qtapp/qt_gui_test_loot.cpp. verifyzone with IW8_REWRITE=fresh names differing assets.
    PHYSICS LIMITS (research 89 section 5): ScriptablePhysicsLimits = instances by definition flags (main and
    motion: bits 13+14; detailed: bits 13+15), exact on all 21 exported maps (tools/scriptable_limits_study.py).
    Converter live at 9e3abdde (old exe zonetool_6e1b1eb5.exe): "physics_limit_detail" per instance. Catalogue
    rebuilt: 509 verified (was 317), new last column physics_limit_detail; ScriptableDefinition::physicsLimitDetail,
    brZone(). compile_iw8.cpp: a common_br_mp definition (br_plunder_box, ...) is placed by name on a br map
    (worldspawn iw8r_br or a br_prematch_insertion_point), left out elsewhere. Proofs: tools/scriptable_limits_test.py,
    scriptable_carry_test.py with a sign / extraction site / pot plant, scriptable_offline_test.py.
    PLACED OBJECTS ON STOCK MAPS (research 89 section 6): converter live at 927eb63a (old exe
    zonetool_9e3abdde.exe): editzone "scriptable_instances" {move, add} for instances without a dynamic entity
    (buy stations). Editor: core stock_placed.* (readStockPlacedObjects, placedObjectEdits), File > Edit Placed
    Objects (main_window_stock_edit.cpp editPlacedObjects), prepareStockEdit writes the key; `scriptable` entities
    never enter a stock map's entity text (splitStockDocument). Proof: tools/warzone_instances_edit_test.py.
    ALSO: splitStockDocument leaves brushes / patches out of a stock map's entity text (a helper brush left in the
    document used to be written into it); GUI check for Hide Stock Props (MainWindow::setReferencePropsForTest).
    The editor window left open for the owner (mp_iw8r_show2.map, started 2026-11-12 00:16) runs an older build:
    restart it to get File > Edit Loot Points / Edit Placed Objects.
    STOCK PATCHES + COLLISION TILES (2026-11-12 late, roadmap L14, research 90): converter live at 44d21398 (old exes
    zonetool_927eb63a / 9a86b345.exe). editzone "stock_patches" reads a zone with the saved stock patch while an edit
    is installed (stock maps ship .fp files with real map data); the editor passes the saved patches
    (installedStockPatchOverrides) and opens stock maps from a clean copy while an edit is installed
    (stockZoneForReading). Collision tiles: collision JSON "collision_tiles" (converter cuts the 32 x 32 grid, writes
    1,024 tile zones + <map>_cg_ls_tr + streaminginfo); editor core collision_layout.*, worldspawn iw8r_collision,
    Preferences "Build: collision", package/validation of the tile zones. New converter commands: havokinfo,
    streaminfo; tools/dw_disasm.py reads the recompiled game exe with its linker map (read only). FINDING: the
    private client accepts only <map>_cg_ls_tr of a custom map's tile zones (research 90 section 7).
    VEHICLES (research 89 section 6b): converter live at 94ac6fea (old exe zonetool_44d21398.exe): collision JSON
    carry_assets; editor core stock_vehicles.* + data/asset_catalog/iw8_vehicles.tsv, entity classes
    script_struct_mp_atv / tacrover / jeep / cargotruck / littlebird. Proof tools/vehicle_carry_test.py.
    ADDED BRUSHES ON STOCK MAPS (research 88 step 4): converter live at 9f1472de (old exe zonetool_94ac6fea.exe):
    carry entries may name a "folder". Editor: core stockOverlayModels (compile_iw8.cpp), main_window_stock_edit.cpp
    StockOverlayPlan / overlayMapText / runStockEdit (the helper map mp_iw8rovl is built with IW8Radiant.exe
    --compile, then edits.json gets its package folder). Proof tools/stock_overlay_test.py; GUI check in
    qt_gui_test_loot.cpp.
    HIDDEN PROPS LOSE COLLISION (research 88 step 6): converter live at 63d4f338 (old exe zonetool_9f1472de.exe):
    editzone remove_static_model_collision; editor sends the hidden props of the stock reference
    (StockPropCollision). Proof tools/smodel_collision_remove_test.py.
    STREAMED WORLD (research 89 section 6c): core readStreamedWorldZones / streamedZonesNear, WorldCache::
    requestStreamed (tiles kept under <world_cache>/<zone>/streamed), File > Load Streamed World Around Camera.
    Proof: IW8R_STREAM_AT="x y radius [limit]" with --world-cache-test.
    REPOSITORY: the project is in the private GitHub repo IDARETW/IW8r (branch main); commit and push finished work
    there, no co-author trailer. docs/features/FEATURES.md is the feature list; its pictures come from
    IW8Radiant.exe --feature-shots (src/qtapp/feature_shots.cpp, real fonts, off screen).
    OPEN for Warzone now: vehicles, whether common_br_mp loads for an own map, zones / clusters on an own map, new
    clusters or zones on a stock map, Verdansk streamed zones in the reference view, new game type scripts (13.6).
  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak
    visuals, gloss scale and a camera preview of additive / multiply (12.6), decoding technique set state words (12.1).

- **Owner playtest 2026-11-11 and the fix queue (read this first).** The owner tested the showcase and mp_iw8r_nov11
  in the editor and in game; results are marked in docs/TEST_CHECKLIST.md and the faults are rows F.1 to F.18 at the top
  of docs/ROADMAP.md ("Fix queue from the owner playtest"). The owner will NOT playtest again unless they choose to:
  prove every fix offline (built zone, zonetool log, compiled GSC, tests). Fixed offline so far, none seen in game:
  - F.1 triggers: the script tests players against the trigger volumes itself (map_script.cpp kTriggerGsc,
    readTriggerVolumes; MapScriptExtras::triggerVolumes). `iw8r_trigger("name")` returns the object that gets
    notify("trigger", player); `iw8r_in_trigger(player, "name")` is the inside test. Behaviours, doors and vision
    volumes use them. A user script should call iw8r_trigger instead of getent for triggers.
  - F.2 / F.3: keys `iw8r_spin "<seconds> [yaw|pitch|roll]"` and `iw8r_bob "<height> [seconds]"` (script_model,
    script_brushmodel, misc_model). compile_iw8.cpp compileIw8ExtractionMovers turns a misc_model with one into a named
    script_model before the build; staysEntity and the solid list know these models are live.
  - F.4: builds write worldspawn `northyaw 90` (the game's north was +X, the editor's is +Y).
  - F.5: blended custom materials keep the colour image's alpha (compile_iw8.cpp image jobs: alphaTest || blend).
  - F.6: authored effects: every element is a burst (the converter has no looping emitters, research 50); looping
    elements are spread over `authoredFxPeriodMs` (1 s) and the map script replays the effect every period.
  - F.8: patches get back faces (Gather::patchBackFaces; worldspawn iw8r_patch_one_sided 1 turns it off).
  - F.15: Prefabs dock: shaded three-quarter thumbnails, names, drag into the views (asset kind "prefabs"),
    double-click places at the middle of the camera view.
  - F.4 rest: a Time of Day preset aims the sun at its sky's sun in the same undo step (main_window.cpp, sunFromSky).
  - F.9: a spot with fov_outer >= 150 is built as an omni (compile_iw8.cpp localPrimaryLights), with a build note.
  - F.12: surface types: the last material word in a name wins (iw8_contents.cpp iw3SurfaceTypeForMaterial), and stock
    materials use their own type from the material cache's surfaceFlags (bits 19+, iw3SurfaceTypeForStockSurfaceFlags;
    CompilePanel::setStockSurfaceSource).
  - F.16: material drag with a live preview (CameraView assetHovered / assetHoverEnded; MainWindow previewMaterialAt,
    endMaterialPreview, applyMaterialTo). On a model the key iw8r_material; compile_iw8.cpp
    compileIw8ExtractionVariants builds "<model>_iw8rmat_<hash>" copies; scene.cpp buildModelGeom shows it.
  - F.18: File > Compare With Last Saved Backup (.bak).
  - F.17: stock map props: MainWindow::rebuildReferenceProps fetches every missing prop model in batches of 150 and
    redraws when the model cache has been quiet for 2.5 s (refPropTimer_); ModelCache::setFallbackZones tries the
    map's own zone for unlisted models; a LOD0 surface name with a comma fails alone (buildGeometryRound). Headless
    check: IW8R_SMOKE_REFERENCE=mp_backlot2 IW8R_SMOKE_SETTLE_MS=200000 with --smoke-test and an existing --out
    folder (the smoke test does not create it); scratchpad backlot_props.py counted 935 of 939 cached.
  - F.7: nothing changed in code. The check map has new broadband test sounds (wind bed, electrical buzz) because the
    110 / 220 Hz sines may simply not have been audible. Reverb is still unexplained.
  - F.13: stock material tints: core material_tints.* reads data/asset_catalog/iw8_material_tints.tsv (made by
    tools/build_material_tints.py: ACTS exports every material body of 44 zones, about 10 minutes; constant index 9
    holds R, G, B multipliers, FFFFFF neutral). Baked into the build's colour PNG (compile_panel.cpp, a .tint marker
    file remakes it) and applied to camera textures (camera_view.cpp). Checked in the build: cone top 141 32 9, base 76.
  - F.6 rest: fx_origin soundalias / sound_loop (map_script.cpp readSoundPlacements).
  - F.3 rest: View > Behaviour Preview (Scene::behaviourTime, scene.cpp behaviourPose; the camera's wind timer
    invalidates the scene each tick). Only iw8r_spin / iw8r_bob models. The GUI test covers the toggle only: the sample
    map has no meshed model at that point, so the motion itself is untested.
  - F.2 rest: a build note for _dyn models placed as misc_model. F.18 rest: Primitives > Scripted Objects > Make Ladder.
  - fx_origin iw8r_damage "<amount> [radius] [interval]" (map_script.cpp readTriggerVolumes: a nameless damage
    cylinder). Preflight script messages name the mapper's own file line first (build_preflight.cpp, after checkScript).
    out/agent/mp_iw8r_scr1 is the reproduction of the owner's spin script (it compiles as written).
  - Behaviour Preview also moves script_brushmodels (scene.cpp brushBehaviourPose / applyBrushPose: lift, bob,
    slide and rotate doors in a loop, yaw spin); GUI-tested with a lift. Tests: core 986 / 0, GUI 585 / 0.
  - Stock reference props are textured: render.* InstancedTexMesh / setReferenceTexInstances / vs_tex_inst (one mesh
    per model material, drawn instanced with ps_tex); CameraView::setReferenceTexInstances resolves their textures with
    the usual per-frame budget; MainWindow::rebuildReferenceProps splits each model by material. They do not cast sun
    shadows in the lit preview (only the untextured instanced path is in the shadow pass).
  - Heredocs still turn backslash-n into a line break inside Python and C++ strings: use the Write / Edit tools for
    any text with backslashes.
  Check map for all of it: out/agent/mp_iw8r_fix1 (built, 0 failed assets). The owner's own edits are in
  out/agent/mp_iw8r_nov11 (they added two juggernaut models): do not overwrite it.
  Tests after these: core 986 / 0, GUI 584 / 0. Still open, all needing the game or new research: F.7 reverb (and
  whether the bed and hum are heard with the new test sounds), F.9 why a 154 degree spot did not show, F.10 probes,
  F.11 modes (proxy side), F.14 killstreak visuals, 
  (nothing else offline).

- **Owner decision (2026-11-11): no CoD4 porting or CoD4 asset use.** Roadmap row 11.9 (port wizard) and its checklist
  item were removed; L11 is stock map remix only. The trial port copy (out/agent/mp_cod4backlot) was deleted. The
  existing File > Port CoD4 Map code is left in place until the owner says otherwise. The converter's IW3 effect route
  used by the Effect Editor (8.2) carries only the mapper's own effects (no CoD4 assets).

- **6.12 inspector-only behaviours (2026-11-11)**: trigger keys `iw8r_teleport` "<destination targetname>",
  `iw8r_jump` "<vx vy vz>", `iw8r_damage` "<amount> [interval]" and script_brushmodel `iw8r_lift` "<dz> [time] [wait]"
  (MapScriptExtras teleports / jumpPads / damageZones / lifts; parsed in compile_iw8 next to iw8r_door, warnings for
  bad keys) each generate a map-script thread (players only, per-player cooldown). In the entity defs overrides, the
  regenerated defs and the entity reference. The generated script compiles with gsc-tool 1.6.2. Roadmap 6.3 done;
  6.4 / 6.12 partial only for in-game verification. Tests: core 952 / 0, GUI 542 / 0.
- **4.8 ground lighting (2026-11-11)**: misc_model `ground_lighting` "1" (converter grey) or "r g b" (0..1 / 0..255)
  -> placement `mins`/`maxs` (origin box), `model_flags` 1, packed `ground_lighting` (a<<24|b<<16|g<<8|r);
  core `parseGroundColor` in smodel_flags. Real build out/agent/mp_iw8r_behtest (also has jump pad, teleporter,
  damage zone): zonetool logs "1 ground-lit" and compiles the map script. Tests: core 954 / 0.
- **3.18 LOD preview (2026-11-11)**: View > LOD Preview (ToggleLodPreview). MainWindow::stockLodFor picks the LOD per
  placement (core previewLodFor = lodForDistance(d / lod_scale); Collision Only mode -> collLod); modelSource draws
  `<model>#lod<N>` from ModelCache::requestLod / loadModelLod (one ACTS xmodelsurfs run, glb/<name>.lod<N>.glb; per-LOD
  materials now kept in model detail: XModelLod surfIndex + materials); past the last LOD -> entity box; a 150 ms
  timer re-picks on camera moves. GUI-tested with a real fetch. Tests: core 955 / 0, GUI 548 / 0.
- **4.12 / 4.13 / 4.1 asset catalog and caches (2026-11-11)**: `--asset-audit effects|images` (ACTS per zone, up to 4
  listed zones): effects 1,020 ok; images 44,502 ok / 6,137 not in zone (data/asset_catalog/iw8_effects_status.tsv,
  iw8_images_status.tsv); Effects / Images browsers read them. Tools > Asset Availability Audit runs any kind as a child
  process. `--thumb-pack materials|models` (thumb_pack.*) filled <app data>/thumbs/materials.full.pack (29,650, 74 MB)
  and models.full.pack (6,490, 13.5 MB); browsers fall back to them (ThumbStore::getAny). Tools > Cache Manager
  (cache_manager.*): sizes, purge (open caches at the next start: runCacheStartupTasks in main.cpp), size cap
  cache/capGB (core cache_trim.*), Move Caches (copy + mklink /J at the next start). Material cache index now drops
  entries whose colour DDS is gone. Long runs use a copy of bin/release in out/audit_bin so rebuilds are not blocked.
- **11.8 stock copies build textured (2026-11-11)**: Compile panel + headless prefetch collect stock materials named
  by custom models (OBJ usemtl); checked with out/agent/mp_iw8r_stockchunk (two model materials in the zone).
- **3.20 culling (2026-11-11)**: core draw_chunks.* (Morton order, 2,048-tri chunks, frustumFromViewProj, merged
  ranges); render.cpp culls the stock reference (flat + textured) and scene textured batches > 4,096 triangles.
  GUI: mp_m_king 41 / 138 chunks skipped with a pixel-identical picture. Tests: core 962 / 0, GUI 557 / 0.
  Then: View > Texture Memory Budget (view/textureBudgetMB; past it new camera textures load at 128 px) and
  instanced reference props (InstancedMesh / setReferenceInstances, vs_inst with INSTROW0-2, also in the shadow
  pass; MainWindow builds one mesh per model + rows per placement). 3.20 done. GUI 560 / 0.
- **8.2 Effect Editor (2026-11-11, research 67)**: core fx_author.* (editor file <map dir>/fx/<name>.iw8fx.json ->
  the converter's IW3 effect graph + IW3 effect material + DDS per element; preview simulation), compile_iw8 fxRoots /
  fxDefaultSprite (data/fx/iw8r_soft_particle.png; Compile panel sets both), preflight accepts fx/<name>.iw3.json,
  Tools > Effect Editor (fx_editor.*). Real build out/agent/mp_iw8r_fxtest: "converted 1 of 1 map-reachable source FX
  graph(s) to native Replay VFX". In game NOT VERIFIED. Tests: core 967 / 0, GUI 567 / 0.
  Then multi-key curves: AuthoredFxElement::keys ([t, size, r, g, b, a], 2..16, sampleAuthoredFxKey), "Curve keys"
  table in the dialog, 9 visual samples in the graph; mp_iw8r_fxtest rebuilt with a 4-key smoke curve: converted,
  preflight 0 warnings. Tests: core 969 / 0, GUI 569 / 0.
  Then element kinds: "light" (IW3 omni, radius = size) and "model" (custom model, size = scale; compile adds the
  model to customModelPaths before the model import). mp_iw8r_fxtest builds with sprite=2 model=1 omni=1, converted.
  8.2 done (in game NOT VERIFIED). Tests: core 972 / 0, GUI 571 / 0.
- **6.22 reverb / audio zones (2026-11-11, research 63 section C)**: core audio_zones.* (soundHashName = Replay
  HashName, 59 stock room types from an ACTS export of mp_shipment.all, readAudioSetup, soundTableJson,
  clientTriggersJson); compile_iw8 writes extracted/sound/soundtable.json + extracted/maps/clienttriggers.json;
  entity defs keys (worldspawn iw8r_reverb; trigger_multiple iw8r_audio_zone / iw8r_reverb / iw8r_audio_priority).
  Real build out/agent/mp_iw8r_audiotest: 2 zones, 2 reverbs, 2 matched audio triggers. Zone ambience dropped (stock
  aliases silent in the map bank). Tests: core 975 / 0.
  Then occlusion / filter presets: core audio_presets.inc (generated by scratch gen_presets.py from the ACTS export of
  mp_shipment.all + x64-zt kEntChannels): "default" for every zone, iw8r_audio_interior 1 -> "interior". Real build:
  5 filters, 167 occlusion filters, no unknown channel names.
  Then custom sounds: core custom_sounds.* + CompileOptions::soundRoots (<map dir>/sounds): sound_emitter aliases and
  zone iw8r_ambient / iw8r_ambient_oneshots naming the map's own WAV / MP3 get extracted/sound/iw8r/<file> and
  extracted/soundaliases/<name>.iw3.json (IW3 channel auto for emitters, ambient 2D for beds); stock_sounds.txt keeps
  only stock aliases. mp_iw8r_audiotest: 4 aliases, 4 sounds in the bank. Tests: core 979 / 0.
  PA speakers: 0 in 8 stock MP maps (not built); custom music states need a converter reader (fork).
- **11.8 Stock Reference to Brushes (2026-11-11)**: core stock_world_brushes.* (triangle -> prism, top face = stock
  material with fitTexParams to the triangle UVs, v flipped; caulk elsewhere), MainWindow::exportReferenceBrushes
  (func_group, cap 4,000). Build out/agent/mp_iw8r_stockbrush: 2,051 collision brushes, stock materials textured.
  Tests: core 981 / 0, GUI 573 / 0.
- **7.10 build time report + zone reuse (2026-11-11)**: CompilePanel::markStage / buildTimeLine ("build time: ..." at
  the end of every log); zonesKey = hashDirectory(extraction) + zonetool size/mtime + map id in <build>/zones.key after
  a validated build; equal key + existing .ff -> Zones / Validate skipped (0.3 s vs 6.5 s on mp_iw8r_audiotest); fail()
  removes the key; Compile > Force Full Rebuild; setting compile/reuseZones. Tests: core 982 / 0.
- **5.16 sun sprite / flare (2026-11-11)**: core sun_flare.* (iw8r_sun_sprite -> extracted/sun/<id>.sun with
  r_sun_fx_position from sundirection); the converter's ReadMapSun / ReadSunFlare take it (mp_iw8r_fxtest log). Core 983 / 0.
  5.16 closed: csm_* keys not needed (the converter's bake ladder already picks 1 / 2 units per texel and crops).
- **5.12 glass (2026-11-11)**: blended material on a script_brushmodel = native breakable glass (converter log
  "replaced 1 IW3 scripted glass entities with native glass"). Built-in iw8r_glass (compile_panel, like water) and
  MainWindow::makeGlassPane (applyMaterialToSelection + groupSelection + targetname glass_N, one undo group). GUI 575 / 0.

- **Roadmap pass 3 (2026-11-11, early)**: done: 2.28 (Edit > Copy / Cut / Paste via the system clipboard, core
  `clipboard_ops.*`; File > Open in New Window), 3.15 (Colour Grade Preview: iw8r_film + iw8r_lut 3D LUT in the camera
  shaders; CB b0 now 224 bytes), 3.16 (nearest captured probe as a cube reflection on glossy surfaces, t5), 3.19 / 11.3
  (reference props as real meshes; fixed a misplaced "reference cleared" status), 4.17 (File > New / Edit Custom
  Material with a sphere + wall preview), 5.2 / 5.13 (Tools > IES Profile Browser, polar plot, data/ies starter set),
  6.16 (surface type -> impact family / scrape alias, core surfaceFeedback), 8.1 (View > Effect Preview in Camera),
  8.9 (Effect Info bounds + on-demand ACTS vfx export into <app data>/vfx_cache). Tests: core 950 / 0, GUI 539 / 0.
  Left: 10 open + 49 partial rows, mostly in-game verification (U/R), fork work (decal volumes, scriptables, audio
  zones, heightfield), or large items (FX editor 8.2, GPU culling 3.11/3.20, web UI 10.5/10.6, cache packs 4.1/4.13).

- **Roadmap pass 2 (2026-11-10, night)**: done: 1.16 (Build Budget dock), 2.24 / 8.8 (Convert Selection to Static
  Model; Prefab Parameters dialog for iw8r_ovr / iw8r_mat), 3.17 + 5.24 (emissive slot-18 maps glow in the camera:
  material cache maskRules 4 re-resolves once; custom materials' "blend" key), 4.2 / 4.22 (browser tags, usage counts,
  Replace Everywhere, List Unused Custom Assets), 4.18, 5.3, 6.10 (Rebuild Stock Map With These Entities), 6.15 (Check
  Map: missing models), 7.8 (select what the player looks at / move to player / camera to player; in game UNVERIFIED),
  7.13 (log lines jump to script lines, Replay Log double-click), 8.10 (Decal Mode click-to-place; blended stock /
  custom materials build with the converter's _b0 techset tag; the build reads the slot-27 opacity from red or alpha:
  the Ukraine tree texture now builds with 53k transparent texels), 8.12 (Generate Stairs / Building; core
  `structure_generators.*` by a Haiku helper), 9.8 (run_all_tests.ps1 -Watch), 9.11 (Search Help Shift+F1, First Map
  Guide). Tests: core 946 / 0, GUI 522 / 0.

- **Roadmap pass (2026-11-10, night)**: rows done this pass: 1.17 (Set Loadscreen From Camera -> loadscreen_<id>.dds ->
  converter preview.rgba), 2.4 + 2.21 (Surface panel lightmap scale; patch vertex locks; Patch Inspector), 3.9 / 3.13
  (Collision Only camera mode, selection tint), 3.21 (Export Camera Image up to 8K, turntable), 3.22 (Four-View
  Layout with per-view pan/zoom via GridView::setFixedAxis), 4.26 (map package zip export/import, core
  `map_package.*`), 5.15 + 6.18 (View > Heat Map: light overlap / spawn density; Tools > Generate Spawns and
  `iw8r generate-spawns`, core `spawn_generator.*`), 6.25 (Links dock node view, core `link_graph_layout.*`), 8.15
  (camera path frame export), 9.12 (Map Doctor: [fix] lines + Fix All in Check Map, core `map_doctor.*`), 9.14
  (autosave history + low-disk guard, core `autosave_history.*`). 3.19: textured stock reference works (the smoke
  settle loop never painted; now renders real frames; the ACTS session found it). mp_donetsk loads its main zone
  (258 streamed _tr zones skipped; the full Verdansk geometry would be 4-5 GB of JSON: the owner's call). Tests:
  core 932 / 0 failures, GUI 488 / 0. Haiku helpers wrote the four new core modules + tests (reviewed and wired here).

- **F5 one-key devmap (2026-11-10, research 65)**: the old F5 sent `map <id>`, which Replay 1.20 MP does not have
  (`map` / `devmap` are SP-only commands; MP `SV_DevMap_f` is dead code). F5 now runs `DevmapRunner`
  (`src/qtapp/devmap_launch.*`) on the pure plan `src/core/devmap_plan.*` (+ `tests/devmap_plan_tests.cpp`): reads the
  game state through the direct link (new `DirectLink` reads: mode, front end, party in/host, server, ui_mapname,
  systemlink, private match; RVAs checked against both DW exes' `.rplmap`) and drives the lobby start: game not
  running -> deploy + launch with `+mpMode 1`; MP menu -> `set LSTLQTSSRM 1` + `xstartprivatematch`; lobby we host ->
  `set NSQLTTMRMP <id>`, `set MOLPOSLOMO war`, `xpartygo` (or `sv_systemlinkStartMatch`); same map running on the DW
  client -> deploy + `map_reload`; other map -> `xpartybackout`, deploy, start. Ctrl+F8 uses the lobby start too.
  Settings `devmap/launchArgs|gametype|reload`. Core 901 tests / 0 failures; GUI 439 checks, 1 failure not from this
  work (Material Info texture-table strings, techset session). **Nothing tried in game**: the configured game folder
  is `E:\IW8\Builds\1.20-replay`, so no deploy / launch was done. Owner test list: research 65 section 6 (set Tool
  Paths > launch to `game_dx12_ship_replay_local_dw.exe` or `E:\DW\IW8\Run_Replay_DW_Local_Server.cmd`; the stock exe
  has no custom-map routing now that `XInput9_1_0.dll` is disabled).
- **Techset coverage (2026-11-10, research 66)**: foliage leaves drew as solid cards (olive tree). Slot 27 is now the
  opacity mask of every alt/bad/bar set (alpha read when it varies), `bar` = glass blend, nodraw (0x80) materials are
  shadow-only, info carriers / lighting proxy cards / distortion hidden, and the sun shadow pass clips by alpha.
  New `src/core/techset_info.*` (+ tests), Material Info shows family / blend / draw class / flags. Cache maskRules 3
  re-resolves old entries once. Verified on `out/treetest/mp_olive.map` and `mp_ukr.map` screenshots.
- **Agent map-making prep (2026-11-10)**: `docs/AGENT_MAP_GUIDE.md` is the guide for Codex / scripted map building
  (format, valid assets, prefab paths, headless build, screenshots, checklist). Headless `--compile` now runs
  `MainWindow::prefetchStockAssets` first (prefabs expanded, models + their materials fetched and waited on;
  `IW8R_PREFETCH_MS`, default 10 min) and writes `prefetch: ...` as line 1 of compile-log.txt, naming unavailable
  models; it also creates the `--out` folder. Verified on `out/prefetch-test/mp_prefetchtest.map` (ok, 3/4 models:
  `automotive_truck_loading_ramp_short` is `ok` in the status TSV but the model index says "mp_backlot2: not in zone").
  A background agent is working on one-key devmap (compile + launch straight into the map), research 65.

- **ACTS / preview speed pass (2026-11-10, separate session; research 64)**: done and deployed, apart from one editor
  rebuild.
  - ACTS fork `IDARETW/atian-cod-tools`, branch `codex/mw2019-complete`, pushed at `3c2198a1`. The new `acts.exe` /
    `acts-common.dll` are in `E:\mw124\atian-cod-tools\build\bin\Release` (old ones in `prev-20261110\` there).
    Same command line and output files; one-shot runs are 3 to 9 times faster; new `--serve` mode keeps zones loaded
    (fork `docs/mw19/fastfile.md`, "Serve mode"). The work tree `E:\mw124\acts-perf` (branch `perf/replay-speed`,
    same commit, with its own `build-perf`) can be removed with `git worktree remove`; its `deps` folder is a copy.
  - Editor: `src/qtapp/acts_runner.*` (new) runs every ACTS export of the material, model, sky / image and world
    caches through up to two `acts --serve` processes; an old `acts.exe` falls back to one process per request.
    Previews decode only the mip level they need (`app::loadDdsPreview`). Settings: `acts/serve`, `acts/servers`,
    `acts/cacheMb`.
  - `bin\release\IW8Radiant.exe` (built 20:18 by the editor session) contains these changes: checked with
    `--material-cache-test` on that exe and the deployed `acts.exe` (3 materials, empty scratch cache, 0.76 s). An
    editor window opened before that build keeps its older code until it is restarted.
  - Measured, empty cache: 36 materials from many zones 110.9 s -> 8.2 s; 3 models 15.0 s -> 0.54 s. Tests: core
    128,135 checks / 0 failures, GUI 427 / 0 (isolated build in `C:\iw8r-perf`, safe to delete).
  - Later the same evening (fork `3c2198a1`): the world exports of `mp_aniyah_tac` and `mp_donetsk` failed on ACTS
    limits, with the old exe too (JSON output budget equal to the read budget; 4,000,000 node cap against
    4,107,040 nodes). Output budget is now 768 MiB, node cap 8,000,000; both export (aniyah_tac is in the world
    cache: 854,396 triangles; donetsk was checked at the ACTS level only: 283 records, 432 MB). The in-use
    `acts.exe` / `acts-common.dll` were moved aside as `*.inuse-204312` in the ACTS `build\bin\Release`; delete
    them once no editor from before 20:43 is running. The runner now ends its servers when `acts.exe` changes.
  - Not done: preview decode still runs on the GUI thread (bounded per frame); `compile_panel` validation and the
    `map_ents` export still start one process per run.
- **ROADMAP.md** (revised today; old text in `docs/archive/`): 137 rows done or partial, about 60 open. Browser work (L10)
  is paused by the owner; the HTTPS server stays enabled for them (port 8780). `tools/dev_rebuild_editor.ps1 -NoLaunch`
  does NOT close running editors (only a launching rebuild does): to pick up a new build, close the old window first
  (CloseMainWindow), then start one editor on `samples/dev/iw8r_dev_showcase.map`; check `curl -sk https://127.0.0.1:8780/`.
  Do not start a second editor while one is running (six piled up on 2026-11-10).
- **Haiku agents**: the owner allows up to 10 at a time. Pattern: each agent gets its own new core files + tests (or a
  tool script / research doc), never `src/qtapp`; I review, run the tests and wire the UI. Agents often cannot finish a
  clean full test run while others build; rerun `bin/release/iw8r_tests.exe` yourself after each batch.
- **Built today and tested** (core 683 tests, GUI 394 checks, all passing; roadmap 158 rows done / partial, 40 open):
  rendering (normal + gloss, Contents mode, per-pixel lights), assets (stock normal maps in builds, Material / Model
  Info, drag and drop, favourites / recent / kits, glTF / GLB import, convex hull collision for placed models in builds),
  scripting (Script dock with 9 behaviours, preflight that stops bad builds, build report / budgets / zone
  dependencies, log-line selection), lighting (Time of Day, IES profiles on spot lights via iw8r_ies, sky from panorama,
  volumetric boxes iw8r_volume), gameplay (triggers and callouts, S&D template with the stock chain, Match Cameras
  template, physics props dynamic_prop, Check Map link / light / spawn / callout / sun / killstreak notes, navmesh
  overlay from the converter's debug dump), editing (Arrange, Spline, Chamfer / Inset, brush to patch, terrain height
  maps / erosion, roads, scatter by rules, decals on a face, prefab overrides, tool flags, lightmap scale), workflow
  (command palette, workspaces, Outliner, camera bookmarks and paths, command scripts, compare maps + patches, stock map
  list with props in the reference view, overview PNG export, Test Checklist dialog writing docs/test_results.json,
  entity class reference).
- **End-to-end checks** (out/snippet-test and the showcase): scripts with all behaviours, panorama sky, IES spot light,
  dynamic props and a volumetric box all compile; preflight 0 errors; ACTS 0 failed on all five zones; the showcase
  builds 21 placed models as 73 hull brushes. Nothing of today's work is seen in game: Help > Test Checklist.
- **Research written today**: 35-49 (stock maps, map UI / fonts, kits, IES, sun / shadows, stock scripted objects,
  scriptables / dynents, keys / killstreaks, CoD4 port, incremental builds, nav / water / doors, decals / volumetrics /
  glass, quick match / live atmosphere, stock map remix, match cameras).
- **Fork 719151ff (done)**: HUD materials named by map scripts (`precacheshader` / `setshader`) are listed in the level
  `mat` table, so they load with the map (compile log: "N HUD material(s)"). The HUD snippet tells the author to add
  `precacheshader("...")` at the top of user_main. Research 51: name-only references only work for always-loaded zones.
- **UV editor (2.27, partial)**: Brush > UV Editor (`src/qtapp/uv_editor.*`), drag / wheel / Ctrl+drag, one undo step;
  per-vertex patch UVs are still open.
- **Batch of 2026-11-10 night** (core 775 tests, GUI 406 checks, all passing): macro recording (9.10 done,
  Document::macroSink), Texel Density camera mode (5.21, core texel_density), CoD4 port wizard (11.9, core port_wizard +
  File > Port CoD4 Map), hot fx / sound / sun channels in every map script (7.7 / 7.9, core hot_channels; the effect
  channel plays only effects the map loaded, never a late loadfx), FX particle preview in Effect Info (8.1, core
  vfx_sim), model LODs / bones / tags in Model Info (3.18, core xmodel_info; the model cache keeps
  model_cache/detail/<name>.json). LOD rule: each LOD is drawn up to its distance (IW XModelGetLodForDist), not from it.
  mp_snip builds with the channels: gsc-tool ok, preflight 0 errors, ACTS 0 failed.
- **Second batch, same night** (core 808 tests, GUI 408 checks): blended / additive stock materials in the textured
  camera (3.17, core material_blend), tag markers on selected stock models (3.18), misc_model lod_scale / no_shadow in
  builds (5.22, research 54, core smodel_flags), streamed-image states in Material Info (4.25, research 52), stock
  scripted objects in Check Map (6.13, research 53, core stock_scripted_catalog). Note: an agent overwrote
  src/core/scripted_objects.* (Make Destructible); restored from the session transcript and tested.
- **Third batch** (core 864 tests, GUI 422 checks). Owner feedback handled: lit preview too dark with lights too hot
  (now ambient 0.55 / sun 0.75 / lights 0.6 with squared falloff; View > Lit Preview Brightness, saved as lit/*); UV
  editor rebuilt (vertex handles, Unwrap to nets, core uv_unwrap; a brush face stays one projection: format limit);
  Sun From Sky (core sky_sun; automatic when the worldspawn sky changes, not on undo / redo or map open;
  `iw8r sky-sun`); compile "0 failed" lines no longer toast as errors (CompilePanel::lineLevel). Stock skies give a sun
  near yaw 270 in our cube frame while stock mp_shipment's sun sits at its sky's yaw 0 (`skyrotation`): our camera and
  build agree, the in-game orientation of a custom map's sky is UNVERIFIED (test checklist). Also 2D reference
  backdrops, Stock Reference to Model, vertex paint with per-vertex colour in builds (sample room build: ACTS 0 failed),
  Check Map lighting rules (research 55), Wind Preview (research 56; smoke IW8R_SMOKE_WIND=1 IW8R_SMOKE_WIND_TIME=s).
- **Other session**: "ACTs caching and preview performance" (started 2026-11-10 evening) works on ACTS / cache
  optimisation in this repo. Ask it before editing material_cache.*, model_cache.*, stock_sky.*, world_cache.* or
  ACTS command lines; editors may be running from either session (check titles before closing one).
- **Installer built** (Inno Setup 6 installed with winget on the owner's OK): out/installer/IW8RadiantSuite-Setup-0.1.0.exe.
- **Patch UVs**: file t pairs = repeats x 1024 (measured on CoD4 mp_backlot_geo.map: 8 per unit on 128-unit materials,
  16 on 64-unit ones; T3 T4 lightmap at 1 per 16 units). patch_ops patchGrid converts; makePatchPrimitive writes file
  units; builds and the camera use them. Older editor patches (repeats, or tools/make_dev_showcase.py world units with
  a 1/64 lightmap pair) keep their look. tools/make_dev_showcase.py now writes file units.
- **Owner in-game test 2026-11-10 and fixes** (core 876 tests, GUI 427 checks): sun / shadow orientation correct in game;
  lod_scale, vertex paint, sound emitters and hot fx worked. Fixed: black viewmodel (light grid written, research 58,
  core light_grid, floor bands keep ACTS's trzone export under budget), no_shadow sun part and player collision for
  script models / dynamic props (fork 954f7db7), live sun crash (sun channel removed, research 60), shadows ending close
  (map script setclientdvar cascade multipliers 3 / 2), lit preview too dark (light encoded / 2 with exposure 1.5 and a
  soft shoulder) and without shadows (GPU sun shadow map, render.cpp), UV editor (mesh layout view by default: nothing
  changes until a vertex is dragged; unwrap is an explicit button), compact toolbar (flat drawn icons, short labels),
  browser tiles (name + folder, elided; preview fitted). Still open: a light entity did not light the unbaked ground
  (research 59: no realtime switch in the fork; test with a bigger radius / iw8r_intensity_units next), props seen
  static (clutter wakes when shot, research 59).
- **Roadmap batch after the test** (core 880, GUI 429): Live Link Quick match + Screenshot (7.12 / 7.11), File > Relight
  Stock Map (11.6, stock_vision + m2 script -Vision; mp_shipment rebuilt, ACTS 0 failed), animated script models
  (iw8r_anim, fork c6b94021), vision / fog volumes (trigger iw8r_vision, fork 68e84f76), doors without scripting
  (script_brushmodel iw8r_door + trigger), markers and tool faces never cast or receive lit-preview shadows (owner;
  memory note), Time of Day keeps its own sun when the sky changes too. Research 61-63 (+ 64 from the ACTS session).
- **Materials availability audit: finished** (both halves stopped): 29,629 ok, 16,356 grey (no exportable colour image),
  150 missing from Replay (`data/asset_catalog/iw8_materials_status.tsv` + `_b.tsv`).

Last updated: 2026-11-08 ~15:30 (atmosphere, live link, clipper/edge/terrain, stock world reader, region + save subsets). Read this first when picking up
the work in a new session, then [ROADMAP.md](ROADMAP.md) (layered plan, per-item status) and
[MILESTONES.md](MILESTONES.md) (results, in-game tracker).

## State

The Qt 6 editor (`bin\release\IW8Radiant.exe`) edits Radiant `iwmap 4` maps and IW8 entity strings losslessly. It has:
- brush, patch, prefab, layer, face-selection, texture-lock, surface (fit/justify), select-mode, find/replace and
  leak-check tools, and a translate gizmo;
- a textured D3D11 camera, with images from custom materials, exported stock assets and an on-demand ACTS material cache;
- custom texture import (File > Import Texture) and custom OBJ model import and placement (File > Import Model);
- compile through x64-zt `buildextraction` (F7, including custom materials and models), ACTS validation, deploy (also
  while Replay runs; Ctrl+F7 = compile + deploy), a Replay Log dock, a Live Link dock (control.ini camera sync) and Launch Replay;
- 3-point clipper, edge drag (E) and a terrain brush (Raise / Smooth / Flatten / Noise modes, radius; roadmap 8.4); lit preview (Ctrl+L); play preview (P).

Open issue (2026-11-08, found while testing 8.4): the GUI test hangs in the "tool materials: ladder" section, in its
`while (undo position > pos) Ctrl+Z` loop, so the run never reaches the summary line. It reproduces with the terrain
section disabled, so the cause is elsewhere (a change in the tree after the 15:15 run). Terrain modes section: all checks ok.

Tests:
- core: `bin\release\iw8r_tests.exe`, 303 tests, 0 failures (20223 checks; includes `tests\region_tests.cpp`, `tests\compile_world_tests.cpp`, doc 29, and `tests\gamemode_templates_tests.cpp`, doc 32);
- GUI: `bin\release\IW8Radiant.exe --gui-test tests\data\iw8_sample_room.map --out out\gui-test`, 265 checks, 0 failures on the 15:56 run (includes the game mode templates section); `IW8R_FULL_COMPILE=1` adds more.

Owner 2026-11-08: deployed maps are listed and selectable in F6. Loading and play are not yet reported.

Game-mode templates (roadmap 6.2, doc 32): Primitives > Game Modes (`GameModeTemplate_<id>`) inserts DOM flags, hardpoint
zones, S&D sites and bomb, CTF flags and four spawn sets as one undo step at the grid centre. Source:
`src/core/gamemode_templates.*` (`templateMapText`, `insertGameModeTemplate`), tests `tests\gamemode_templates_tests.cpp`,
GUI section "game mode templates". Not verified: the DOM flag prop, the hardpoint `chevrons` target, trigger brush sizes
(placeholders, resize them), and in-game behaviour of editor-built objects.

## Build / run / test

```
xmake f -m release --qt=E:\Qt\6.8.3\msvc2022_64 -y
xmake build -y iw8r_tests && bin\release\iw8r_tests.exe
xmake build -y IW8Radiant          (or tools\dev_rebuild_editor.ps1 -NoLaunch, which renames a locked exe)
bin\release\IW8Radiant.exe --gui-test tests\data\iw8_sample_room.map --out out\gui-test
set IW8R_FULL_COMPILE=1   (adds F7 + x64-zt + ACTS + scratch deploy to the GUI test, ~1 min)
bin\release\IW8Radiant.exe --smoke-test tests\data\iw8_sample_room.map --out out\smoke   (screenshot.png)
bin\release\IW8Radiant.exe --material-cache-test <mat>[,<mat>] --out <dir>
powershell -ExecutionPolicy Bypass -File tools\dev_rebuild_editor.ps1   (build + open a new editor window for the owner)
iw8r compile <map> -o <dir> --map <id> [--materials <dir>]... [--models <dir>]... [--model-scale s]
```

## Rules that bit before

- `samples\iw8\iw8_sample_room.map` is the **owner's working copy** (they edit it in the editor). Tests use
  `tests\data\iw8_sample_room.map` (regenerate: `py -3.11 -I tools\make_iw8_sample_room.py -o tests\data\iw8_sample_room.map`).
- Never write into `E:\IW8\Builds\1.20-replay` from tools or tests. Deploy and Launch are owner-triggered. Test deploys use
  a scratch folder.
- E: has under 5 GB free. x64-zt output, the material cache and custom assets of untitled maps live under
  `C:\Users\user\AppData\Local\IW8 Radiant Suite\IW8 Radiant Suite\`.
- x64-zt repo `E:\mw124\x64-zt` is shared with another Claude session ("IW8 ZoneTool"):
  - Do branch work in a separate worktree only.
  - No AI co-author trailer (its AGENTS.md).
  - Our route lives on branch `iw8radiant/build-extraction`, commit `190aa793`, not pushed.
  - Its `build\` folder is ours, but rerun `generate.bat --games=iw3,iw8` before rebuilding.
- Shell heredocs mangle `\\` and `\n` in Python patch scripts. Put patch scripts in a file in the scratchpad (Write
  tool) and use raw strings.
- ADS un-parents non-current tab widgets. Bump `kDockLayoutVersion` in `main_window.cpp` when adding a dock (now 7).
- `windows.h` defines `small`, so don't use it as a variable name.
- GUI-test drags must not land on half-grid points (pixel rounding decides the snap).
- Custom assets resolve from `<map dir>\materials` (`*.iw8mat.json`) and `<map dir>\models` (`<name>.obj`), plus
  Settings `assets/materialRoots`, `assets/modelRoots` and `assets/modelScale`.

## Region and save subsets (roadmap 2.12, 2.20)

- `src/core/region.*` (namespace iw8r): `itemsOutside` (prims whose bounds miss the box, point entities outside it),
  `mapTextSubset` (keep predicate, lossless text), `mapTextInRegion`. Tests: `tests\region_tests.cpp`.
- View > Region: Set From Selection, Set From XY View (visible grid area; depth axis full), Off. `EditorContext::region`
  feeds `SceneCache::hiddenByRegion` (refreshed in `documentEdited`). Save Region keeps a brush that straddles the box.
- File > Save Selected... / Save Region...; `MainWindow::saveSelectedAs` / `saveRegionAs` take a path (GUI test
  `region:` section writes `out\gui-test\region\`). Not drawn yet: the region box outline.

## Integrated this round (all wired into menus, GUI-tested)

These were core modules from the parallel agents:
- `brush_ops2` (CSG merge, extrude, edge move, 3-point clip plane) and `primitives` (cone, sphere, stairs, arch, ramp).
- `patch_ops2` (cap, weld, redisperse, split, terrain, noise, smooth, raise/lower) and `generators` (rope, scatter, drop to
  floor).
- `entity_defs` (Check Map), `glb_mesh` + `model_cache` (stock model meshes in the view).
- `minimap` (compiler writes `compass_map_<id>.dds` + corners), lights in `compile_iw8`, and `spawnlist` +
  `iw8r iw8d-spawns`.
- Probes, static models, sky (doc 29): one world.json probe per `reflection_probe` entity (`images/<id>_probe<N>.dds`),
  each surface on its nearest probe; custom `misc_model` entities become `world.models[]`; `world.sky` stays empty because
  x64-zt needs the sky cubemap files. Open: x64-zt skips source probe 0 when there are two or more probes (doc 29 section 1),
  which needs an in-game check.
- Sky (doc 33, 2026-11-08): worldspawn `skyname` is built. Stock skies (`sky_mp_*`, 2D hemi-octahedral BC6H 2048^2) are
  exported by `StockSkyCache` (`src/qtapp/stock_sky.*`, ACTS into the material cache images/) and converted by
  `src/core/sky_image.*` into the legacy B8G8R8A8 cube x64-zt reads (`images/<name>.dds`, `world.sky = <name>`).
  `iw8r compile --sky-dds <dds>`; `IW8Radiant.exe --stock-sky-test <sky> --out <dir>`; UI list: `iw8r::qt::stockSkyNames()`.
  x64-zt built mp_iw8r_sky with sky_mp_shipment offline. Open: orientation and brightness in game (doc 33 section 2 UNKNOWNs).

Play preview (P) is lead work: `src/core/collision_world.*`, `src/core/player_move.*`, `tests/play_preview_tests.cpp`, and
`CameraView::startPlay/stopPlay/tickPlay`.

## In flight (2026-11-10 late evening)

Nothing: every agent of the last batches is reviewed and wired (light keys, effect info, hotkey presets, frustum core,
installer script). data/iw8_entity_defs.json is generated: put editor-authored classes in
data/iw8_entity_defs_overrides.json and run `py -3 -I tools/gen_iw8_entity_defs.py`, then tools/build_entity_reference.py.
Older entries below are history.

Done since the restart (all GUI- or core-tested):
- 2.17 entity display, 3.7 camera context menu, 2.6 auto-caulk, 9.5 safe mode, 9.2 themes + toolbar. Fixed a real rotate
  gizmo bug: looking down, the edge-on Y ring overlapped the Z ring's 0-degree point and could be grabbed instead
  (the ring facing the camera now wins ties).
- Owner web server switched to HTTPS (web/tls=true, bin/release/web_tls self-signed cert for localhost, 192.168.1.x, (public address)); verified local 200, LAN 401/200, plain HTTP refused, UPnP re-mapped.
- L10 browser access (owner request): web_server.* + upnp.* + data/web; enabled for the owner (Settings web/*:
  user iw8rd, salted hash only; LAN + UPnP, port 8780; router mapped 8780 -> 192.168.1.x, public (public address)).
  Verified: local 200, LAN 401 without / 200 with login, browser client connected (95 entities, select round trip).
  Also 2.19 measure/snap, 3.6 scale gizmo, 3.8 sky preview.
- 7.4 hot update (dvar iw8r_hot + iw8r_hot_update thread in every map script; Live Link "Hot update"), 7.5 F5
  build/deploy/load, 5.5 probe capture (CameraView::captureCube, Compile > Capture Reflection Probes,
  CompileOptions::probeCaptureDir; headless: IW8R_CAPTURE_PROBES=1 IW8R_CAPTURE_SETTLE_MS=20000 --compile).
  Reverb zones / client audio triggers (6.7) researched only: they need x64-zt sound tables (extracted/sound/
  soundtable.json, IW7 schema) and Replay reverb room types, which cannot be verified offline.
- 2026-11-10: Primitives > Shape Models (cube, sphere, cylinder, cone, torus, capsule, plane, wedge, pyramid; core
  shape_models.*, OBJ in <map>/models, misc_model; `iw8r shape-obj all <material> <dir>`); all nine built in the scratch
  showcase (26 static models). Scale gizmo (T): models modelscale uniform, brushes along the axis
  (scaleSelectionAbout). 7.2 load map, 8.3 paint mode, 8.5 enter/leave prefab, 8.7 key replace + selection sets.
  Model availability audit complete: 6,490 / 6,654 ok, 164 missing (hidden). Materials audit running
  (out/audit-materials.log, resumable: rerun the same --asset-audit command).
- Availability catalog: `IW8Radiant.exe --asset-audit materials|models --out <tsv> [--limit N] [--batch N]` (asset_audit.*,
  scratch cache root wiped per batch, resumable) -> data/asset_catalog/iw8_{models,materials}_status.tsv (running from
  out/audit-bin, logs out/audit-*.log). Browsers hide "missing" behind "Buildable only" and tint "grey" materials.
- 6.5 Scripted Objects menu (scripted_objects.*): Make Destructible / Make Scripted. Fork 074be493 keeps
  targetname destructable script_models live. Showcase generator: a destructible pallet in the live row.
- Availability: browsers grey materials/models the caches could not resolve (tooltip "NOT AVAILABLE in a build: why");
  the compile log separates "not cached yet" from "not available in Replay and build grey (pick another)". Entries
  already resolved with the zone fallback (maskRules 2) are no longer re-fetched each session.
- 2026-11-09 night: live script_models (targeted, not gameobject/exploder) are made solid at load by the map script
  (iw8r_make_solid -> ent solid(); x64-zt gives their keyframed body 0xB180 contents = bullets only). Worldspawn
  iw8r_script = the map's own GSC (defines user_main(), started after the load script), carried in the zone; the
  showcase generator writes one (spin / bob / blink the three live script_models). Stock spawn editing 6.1 done
  offline (File > Edit Compiled Spawns / Rebuild Stock Zone). Thumbnail store src/qtapp/thumb_store.* (JPEG packs
  in AppLocalData/thumbs). Asset audit (300 materials / 150 models, out/audit-*): 91% of materials resolve fully,
  misses are images only referenced from techsets_mp_donetsk; all model misses were the 8,906 streaming-zone
  weapon/attachment models, now left out of the Models browser. Next: per-asset availability catalog (full audit,
  needs a probe-only cache mode so 46k materials do not fill the disk) and browser status marks.
- 2026-11-09 evening: stock textures streamed from other zones (global_stream_mp, common_base_mp) now export: the
  material cache adds each image's own catalog zones after the material's; entries cached as "not exported" and
  entries from before maskRules 2 are re-resolved once (slow first open: ACTS re-exports). Foliage cut-out masks
  are `<x>_alpha` images (BC4), not just `_sdf`; the camera now uses the mask as alpha and draws stock colour
  opaque otherwise (the `_c&_s` alpha is specular: that hid the dryer body in the editor). Probe priority/feather
  (reflection_probe keys priority, feather) -> world.json -> fork 58782e8f. The map script always starts with
  maps\mp\_load::main() (-> iw3compat_load_main: exploders, destructibles, other modes' gameobjects removed,
  out-of-bounds kill), as stock maps do. Camera: tool volumes are wire boxes. Smoke test: IW8R_SMOKE_TEXTURED=1,
  IW8R_SMOKE_SETTLE_MS=<ms> (model textures still came out flat in the off-screen smoke window; check in the editor).
- 2026-11-09 later: Sounds dock + ambient sound (4.5/6.7). x64-zt verifyzone traces now list soundbank aliases
  ("S <alias> <loop> <distMax>", fork 46c7a416); tools/build_sound_catalog.py -> data/asset_catalog/iw8_common_sounds.tsv
  (11,707 aliases from common_mp/common_base_mp/code_post_gfx; traces in out/sound-trace). sound_emitter entities
  (soundalias, loop, delay) compile into the map script and extracted/sound/<map>.stock_sounds.txt (fork fac03046);
  verified offline in .iw3-scripts/<id>/<id>.gsc (level.mw120r_sounds["emt_smokestack_lp"] = 2, gscbin compiled).
  Point entities have shapes in the camera (lights octahedron in _color, probes, fx tall diamond, nodes flat diamond,
  sound_emitter green). Atmosphere dialog edits iw8r_vision. Fixed a shutdown crash: the ACTS caches now disconnect
  their process before waiting for it (finished handlers touched deleted widgets). Vision overrides committed
  (fork 15f63b8e). The owner saved samples/dev/iw8r_dev_showcase.map at 19:08 from an older editor session
  (sundirection -45 60 0, own fog): it is the owner's working copy now; do not regenerate over it without asking.
- 2026-11-09 owner reports from the showcase in game: water and ladder boxes were white (both compiled to iw8r/grey,
  colour `$white`). Now `ladder` (exact last path component) is a non-drawn tool material (collision + ladder faces
  kept) and the Compile panel adds a built-in `water` custom material (`<build>/builtin_materials`, blue rippled PNG)
  unless the user's library defines one. Shadows "the wrong way": the converter and we both treat `sundirection` as
  toward-the-sun; the showcase had the sun NE while the shipment sky paints it SW, so the showcase now uses
  `-40 235 0`. Camera: model meshes no longer draw per-triangle outlines; stock models in the camera use
  ModelCache::loadModel (textured, shadowcaster meshes dropped). Browsers: thumbnails for materials/models/images
  (GUI section "browser thumbnails").
- Brush flags UI (roadmap 2.13): Brush > Make Detail (Ctrl+M), Make Structural, Toggle Non-Colliding write `contents detail;` / `contents nonColliding;` via `src/core/brush_flags.*` (one undo step each; `tests/brush_flags_tests.cpp`, GUI section "brush flags").
- Probes/static models/sky (doc 29); probe 0 is the default probe and entity probes are 1..N.
- Live Link dock (control.ini; "move editor camera there"), 3-point clipper, edge drag (E), terrain brush, inspector key
  help + light group, lit preview check.
- Atmosphere (doc 31): Compile > Atmosphere / the Compile panel button edits worldspawn `iw8r_fog` ("start halfway r g b")
  and `iw8r_film` (10 numbers). The compiler writes `extracted/maps/mp/<id>.gsc` (one `setExpFog`) and
  `extracted/vision/<id>.vision` (`r_film*` only). x64-zt logs `iw3: exponential fog ...` / `iw3: film ...` and
  "converted vision (... fog yes, grading CLUT ...)"; built OK offline. Exposure stays Map Info `iw8r_exposure_adjust`.
  Not checked in game.
- Stock world geometry reader + cache (3.9, agent): `src/core/stock_world.*`, `src/qtapp/world_cache.*`,
  `--world-cache-test <zone>`. View > Stock Map Reference shows it in the camera (grey, sky skipped);
  smoke check: `IW8R_SMOKE_REFERENCE=mp_shipment IW8R_SMOKE_CAMERA="-6000 -9000 4000 -35 45" IW8Radiant.exe --smoke-test ...`.
- Play preview movement (agent + lead wiring): mantle (Space at a 16-72 unit ledge, Bezier path, Replay 1.20 durations and
  exit velocity; Space in the air = high mantle), slide (sprint + C, 0.4 s), sprint (Shift, forward only, 2 s meter),
  sprint leap, chained-jump falloff, landing friction, eye-height lerp; ground accel 9 / crouch 12 / prone 19, friction 5.5,
  walk 187.9 (190 x 1.15 x 0.86). The camera status bar shows the mode. Approximations listed in `src/core/player_move.h`
  and `player_mantle.h` (ledges from brushes, kinematic mantle path, no sprint-over mantle).
- Ladders (6.8): brushes with the `ladder` tool material write `ladder_planes` (every brush plane; x64-zt
  iw3_fastfile.cpp:7250-7284 keeps the widest vertical faces and sets surface flag 0x8). Play preview climbs them
  (W into the face; looking down climbs down; Space pushes off; climb speed 0.55 x walk is an APPROXIMATION, the
  reference has no ladder speed). Not checked in game. Water: x64-zt has no swim-volume input; `water` brushes stay a
  drawn, non-colliding volume with the water surface type.
- Camera fog preview from `iw8r_fog` (View > Fog Preview, on by default; same 50%-at-halfway curve as setExpFog).
- Camera modes (3.3): View > Camera Mode = Textured / Flat / Wireframe / Fullbright / Normals (`SceneCache::cameraMode`,
  saved as `view/cameraMode`; `texturedView` follows it). Lit Preview stays separate. Stock map reference layer (3.9) is
  still drawn filled in Wireframe and Normals (camera_view.cpp, not changed).
- Earlier movement values from the walk simulator (reference only): accel 10, air 1, friction 6, crouch 0.45,
  prone 0.2 (Z), sprint 1.4, M4 0.86, prone eye 11, slope 45.57 deg; capsule traces.

Owner fixes 2026-11-08 ~15:45 (from in-game reports):
- Stock textures were white in game: a missing `<map dir>\materials` made MaterialLibrary::load return false and the
  compile panel dropped the whole library. Missing roots are now skipped and a partial library is kept.
- Stock models (script_model / misc_model) are built as untextured `iw8r_stock_*` copies from the ACTS model cache
  (CompileOptions::stockModels); the entity model key is rewritten. Model materials are not carried yet.
- Running-game detection (deploy, Live Link) accepts any exe running from the game folder (renamed Replay exe).
- Deploy over an installed map: when the folder cannot be renamed (a handle inside it: Explorer, antivirus), the six
  files are swapped one by one; a really locked file is named and nothing changes.
- GUI test: all undo loops are bounded (a failing check no longer hangs the run); 234 checks (242 with full compile).

Roadmap 9.3 (log console and toasts):
- `EditorLog` (`src/qtapp/editor_log.*`, singleton, 5000 entries) takes status (`EditorContext::setStatus`), compile output
  (`CompilePanel::append`; "warning..." = Warning, error/fail words = Error) and Qt warnings (`qInstallMessageHandler`, installed in main.cpp).
- Console dock (`log_panel.*`, after Replay Log): level checkboxes, text filter, Clear, Copy all (the filtered rows), auto-scroll.
- Toasts (`toast_host.*`): bottom-right child of the main window, max 3, 4 s, Warning/Error entries and the compile
  result; View > Toast Notifications (`ToggleToasts`) saved as `view/toasts`. GUI test section "console log".

Round 2026-11-08 ~16:30:
- Stock models are textured: the model cache records LOD0 material names (xmodel materialHandles[lodInfo[0].surfIndex+i],
  leading comma stripped; cache entries without them re-export once) and names the GLB meshes; builds add them to the
  stock-texture set, flip v for the model writer, and the camera view textures models. Cut-out stock textures (>2%
  transparent pixels: chainlink, foliage) get alphaTest + twoSided.
- Live Link "Direct (no proxy)": `src/core/live_direct.*` attaches to the exe running from the game folder, reads the
  `.rplmap` of recompiled exes (RPLYCMP2; e.g. game_dx12_ship_replay_local_dw.exe) to resolve native RVAs, reads the
  player (g_entities 0xBC20F00 -> +0x150 client, origin +0x30, angles +0x1CC), appends console commands to
  s_cbufMainBuffer (0xD216D70) while it is idle, and writes the player origin. `iw8r live-read <pid>` checks it
  read-only; verified against the owner's running local_dw exe (origin read OK). Command send and move not yet tried in
  game.
- Ladders, region/save selected, camera modes, brush flags, terrain sculpt modes, tool material menu, F1 shortcut sheet,
  packaging (`tools\package_release.ps1`) and local CI (`tools\run_all_tests.ps1`) landed this round.

Round 2026-11-08 ~16:00 (owner reports: models untextured and tipped over):
- Stock models were rotated twice (the panel converted Y-up to Z-up and the model writer does it again): the panel now
  passes the model cache's Y-up mesh unchanged (ModelExportOptions::zUp does the rotation).
- Material cache fallback: a material whose body is in no candidate zone (e.g. mo/lm_accessory_don_currency_01_a,
  referenced by a common_br_mp xmodel) takes the colour image named `<stem>_c&...` from iw8_images.tsv; failed
  "not in zone" entries are retried once per session.
- Live Link "Follow game": the editor camera copies the in-game position and view ~30 times a second (direct link).
- Compile > Sun & Sky: sundirection / suncolor / skyname as one undo step; the lit preview draws a yellow sun arrow.
  The sky build route is being researched (doc 33).

Round 2026-11-08 ~16:20 (owner: dryer half invisible, fence links filled, models do not collide):
- Stock `_c&_s` colour images carry SPECULAR in alpha, not opacity: the >2% transparent-pixel cut-out heuristic is
  gone (it alpha-tested the dryer body away). Cut-out materials are recognized by a `_sdf~` texture in the material
  body (chainlink: cp_disco_chainlink_fence_01_sdf, BC4): the material cache records it (`mask`; entries without the
  key re-resolve once), and the compile panel writes PNG alpha = sdf >= 0.5, with alphaTest + twoSided.
- Model collision: x64-zt promotes untargeted script_models to static models and gives props contents 0xB180
  (outside the player mask, IW3 parity); in game they stopped no bullets either. The compiler now adds an oriented
  solid box per placed model (misc_model, script_model without targetname) to the world collision
  (CompileOptions::modelCollisionBoxes, default on; flat cards get a 2-unit slab). Not checked in game.

Round 2026-11-09 (owner: crash compiling the sample map, editor would not reopen):
- Crash: CompilePanel::prepareStockMaterials (cut-out mask) read a QImage that shared the decoded mask buffer after
  that buffer went out of scope (scaled()/convertToFormat() return shallow images when nothing changes). Fixed with a
  deep copy, RGBA8888 forced and a size guard. The full compile of a copy of the owner's map (stock models, masks,
  model boxes) now runs through x64-zt and ACTS validation.
- Crash handler (9.5): `src/qtapp/crash_handler.*` writes `%LOCALAPPDATA%\IW8 Radiant Suite\IW8 Radiant Suite\crashes\
  crash-<time>.txt` (symbolized stack with file:line from the PDB) + `.dmp`; the next start names the report in the
  status bar. dbghelp is linked.
- Sun & Sky lists the stock skies (stock_sky.h stockSkyNames(): sky_mp_*). The sky agent stopped on a usage limit after
  finishing the route (doc 33); its PROGRESS entry is above.

Round 2026-11-09 (continued): Edit > Preferences (9.2), docs/USER_GUIDE.md + Help > Mapper's Guide (9.7), View > Show
Compiled Collision (3.10), lit-preview sun shadows (3.4). Volatile (test) settings now keep values in memory for the run
(they used to drop every write). GUI test 270 checks, core 310 tests.

Round 2026-11-09 (continued 2): Prefabs dock (4.4, dock layout 8), Models browser 3D preview (4.3,
src/qtapp/model_preview.*). tools/dev_rebuild_editor.ps1 now closes the previous editor windows gracefully before
opening the new build (owner request). GUI test 278 checks.

Verification rule (owner, 2026-11-09): check map-affecting changes in the BUILT package, not only the extraction:
`py -3 -I tools/verify_map_output.py <x64-zt package dir> [--expect-class node_pathnode>=1] [--expect-model ...]
[--expect-material ...]` dumps the shipped entity string, xmodels and materials with ACTS; the x64-zt log gives
collision hull, navigation and PhysicsAsset counts. Converter renames: model materials -> w/mw120r_<id>_model_<hash>,
alpha-tested -> w/mw120r_<id>_foliage, untargeted script_models -> static mw120r/<id>/smodel_N.
Verified this way: nav markers (2 placed -> 31 node_pathnode records), model collision boxes (hulls 28 -> 36,
navigation hulls 25 -> 33), stock models as 3 static smodels with their own materials and PhysicsAssets.

Round 2026-11-09 (continued 3): performance (9.4/3.11). `--smoke-test` writes perf.txt. Camera geometry is cached
(key: scene generation, doc revision, selection hash, shading/overlay flags) and unchanged vertex buffers are not
re-uploaded: 10k brushes steady 17.4 -> 7.4 ms. FX placement (6.6) researched only (ROADMAP row): needs an ACTS
vfx -> x64-zt NativeFx (extracted/vfx/<name>.json) exporter plus a createfx script writer.

Round 2026-11-09 (continued 4):
- Dev showcase map: `py -3 -I tools/make_dev_showcase.py` -> samples/dev/iw8r_dev_showcase.map (+ custom materials and
  models beside it), 15 labelled rows covering every asset variation. Headless build through the editor path:
  `bin\release\IW8Radiant.exe --compile samples\dev\iw8r_dev_showcase.map --out out\sc-compile` (writes
  compile-log.txt / compile-result.txt). Owner checklist: docs/TEST_CHECKLIST.md.
- gsc-tool is a tool path (gsc_tool, default the owner's level-maker copy); builds now compile the map script.
- x64-zt fork commit abf4bc6f (branch iw8radiant/build-extraction, local only): loadfx of stock vfx/ names is kept.
  Effect placement (fx_origin -> generated map script) verified in the built zone (level vfx table 4 effects).
- Builds pass --iw3_mode_assets off (no IW3 Domination flag prop; it broke builds with DOM triggers).
- Grading LUT: worldspawn iw8r_lut (.cube) -> grading CLUT (converter log "colour grading from ... 17^3").
- Script models (doc 34): decor placement is misc_model; model boxes follow the converter's static promotion; live
  script_models get footprint_mask_group; Check Map explains promotion.

Round 2026-11-09 (continued 5): probe volumes (reflection_probe mins/maxs -> world.json volume; x64-zt fork 3395867e
reads it; teal box in the camera), Effects dock (1020 common-zone vfx; a common effect verified in the built level vfx
table). x64-zt fork branch iw8radiant/build-extraction now has abf4bc6f + 3395867e (local, not pushed).

Images dock (4.5): StockSkyCache gained an any-image mode (full catalog, packed names kept, safe file names in
AppData\...\image_browser); run folders are short hashes because long packed names crossed the 260-char path limit
(ACTS then reports "failed"). Dock layout version 10.

Known issue: a **fresh** x64-zt stock cache fails in ACTS (mp_shipment vfx/physicsfxshape/surfacefx/lightdef: 20 failed
assets, "Replay asset export has failures"). Builds that reuse `out\x64zt-extraction-test\output\_stock_cache` work.
A new output folder (or a new machine) hits it; check the ACTS version or the manifest before a clean setup.

Round 2026-11-08 ~16:00, roadmap 9.1 (first-run setup), done and tested:
- `src/core/setup_detect.*` (namespace iw8r): `classifyExe` (Missing / NotPe / Stock / Recompiled by the RPLYCMP2 marker
  at 0x40), `md5File` / `md5Hex` (RFC 1321, stock hash `kStockReplayMd5`), `candidateGameDirs(driveRoots)` (`<root>/IW8/Builds/<x>`
  and `<root>/<any>/IW8/Builds/<x>` holding a game exe), `gameExesIn`, `oodleIn`, `isExistingFile`, `windowsDriveRoots`.
  Tests `tests\setup_detect_tests.cpp` (8 tests).
- `src/qtapp/setup_wizard.*`: Help > First-Run Setup (`FirstRunSetup`), and at startup from main.cpp when the Replay exe
  file is missing (not in --gui-test / --smoke-test). Detect, Browse, Save writes `tools/acts|replay|oodle|zonetool|launch`.
  The MD5 is not yet checked in the UI.
- GUI test section "first-run setup" (before the shortcut sheet section) on a fake `out\gui-test\setup_root`.
- Results: iw8r_tests 303 tests, 20223 checks, 0 failures; GUI test 265 checks, 0 failures.

## Open UI follow-ups

- Spawn editing UI for stock maps (6.1).
- Stock world: props (smodels), textures/UVs (aux stream), grid-view outline (3.9).
- Fog preview in the camera view; native world.json fog, LUT import, volumes (doc 31 section 4).

## Reports written this round

`docs/research/19_replay_test_loop.md` (logs, launch, proxy check), `20_material_texture_cache.md` (ACTS images:
techsets zone holds material bodies, images need all populated pak xpaks), `21_custom_materials.md` (BGRA8 DDS with mips
accepted; the normal-map green sign is unverified), `22_custom_models.md` (xmodel JSON contract; winding unverified in
game).

## Next tasks (in order)

1. Owner in-game test of a built map: spawn, collision, trigger, custom texture, custom model (winding, UVs, normal-map
   sign). Record it in the MILESTONES tracker.
2. Gizmo rotate/scale handles (3.6). Stock xmodel preview in the viewport via ACTS export (3.5).
3. Stock textures in builds are done for colour maps (Compile > Stock textures). Open: Replay normal/gloss packing (`_n_..._g` images) for normal maps.
4. L2 leftovers: edge drag (2.8), 3-point clip and CSG merge (2.6), cone/sphere (2.7), extrude (2.9), patch cap/weld (2.14),
   entity definitions (2.16), Alt+Shift select cycle (2.10).
5. L3: lit preview with sun shadows (3.4), stock map world geometry (3.9), build preview (3.10).
   3.9 reader and cache are in (`src/core/stock_world.*`, `src/qtapp/world_cache.*`, `--world-cache-test <zone>`; mp_shipment: 418,667 triangles, 9.4 s ACTS, 388 MB cache). Props (smodels), the aux normal/UV stream and the editor view are still open. See `docs/research/30_stock_world_geometry.md`.
6. L5 lights (5.1) and L6 spawnList write-back (6.1), then L7 live link (proxy `.proxy\control.ini` channel and the DW
   `tools/live/live.py` command file are candidates, see research 19).
