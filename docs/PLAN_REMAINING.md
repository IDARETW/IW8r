# IW8 Radiant — plan for the remaining development

## Context
Owner direction (2026-11-13): finish lighting and everything that goes with it, fully integrated (viewport, GUI and
the compiled map's lighting); then the Umbra system; then bring the web viewer level with the Windows GUI; then every
other missing piece; hand over a finished product. Owner hint: "transient tiles may be to blame": Verdansk and Quarry
keep most world geometry in 749 streamed `mp_donetsk_NNNNN_tr.ff` tiles (2.7 GB), which the editor loads only by hand
(File > Load Streamed World Around Camera: 12k radius, 48 tiles, full reference reload). That is the likely source of
the flat grey areas and holes in the Verdansk reference.
Owner decisions: Umbra = decode, then a visibility view, then a real bake. Web 3D = stream the editor's own render.
Lighting = viewport and build side (proven offline; the in-game look stays with the owner).

On approval this plan is copied to `docs/PLAN_REMAINING.md`, and docs/PROGRESS.md points to it, so later sessions
follow the same milestones.

## Working rules (all milestones)
- **Research first.** Any reverse engineering gets a note `docs/research/NNN_*.md` (labelled VERIFIED / INFERRED) and a
  line in the README index.
- **Code and tests.** Logic goes in a core module `src/core/*` with tests in `tests/*`. Wiring goes in viewport / qtapp.
- **Builds.** Only `tools/dev_rebuild_editor.ps1 -NoLaunch`, holding `out/build.lock`.
- **Patch scripts.** Multi-line source edits go through `tools/patches/*.py`, run with `py -3 -I`.
- **Gate for every step.**
  - `tools/run_all_tests.ps1` passes.
  - Benches pass, run with `out/agent/mapcam.ps1` (bench cameras below).
  - CHANGELOG and PROGRESS are updated.
  - Commit and push to IDARETW/IW8r with no co-author trailer.
- **Bench cameras.**

  | Map | Camera |
  |---|---|
  | Speedball (matches owner's screenshot) | `-1080 230 330 -4 -27 120` |
  | Hideout, Aniyah Tac, Shipment, King | `ent node_pathnode 30` |
  | Verdansk, ground level | `-24900 -1700 -200 45 -12 90` |
  | Verdansk, raised | `-24900 -1700 1800 45 -12 90` |

- **Frame budgets** (Game Lighting, all features on):

  | Map | Budget |
  |---|---|
  | Speedball | ≤ 8 ms |
  | Hideout | ≤ 14 ms |
  | Aniyah Tac | ≤ 20 ms |
  | Verdansk | ≤ 28 ms |

- **x64-zt converter changes.** Work in the `C:\zsw-iw8r\scriptables` worktree. Local commits only, never pushed, no
  trailer. Deploy the live converter the way it is done today (keep the old exe in `out/zt_backup`).
- **ACTS changes.** Fork worktree; commit and push to IDARETW/atian-cod-tools (owner asked); deploy schema-only changes
  by copy.
- **Off limits.**
  - The game folder and `E:\DW\IW8` are read only.
  - Never start or attach to the game.
  - In-game checks go into `docs/TEST_CHECKLIST.md` for the owner.
- **Helper agents.** Opus for independent pieces, each with its own files; Haiku for cleanup only. I verify every
  report.
- **Installer.** Rebuild at the end of each milestone (`tools/build_installer.ps1 -Zip`, then confirm there is no ini).

---

## Milestone 0 — Land the work in progress (DONE a7bb3ae)
- **What is in the working tree, not yet committed:**
  - screen-space AO (render.cpp `kAoShader`, `drawAo`, `invert4`);
  - a readable depth buffer (`R24G8_TYPELESS`);
  - `ps_prepass`, drawing only props large on screen (`bigInst`, `IW8R_PREPASS_PX` = 24);
  - the main pass writing depth;
  - the debug switches `IW8R_AO_SHOW` and `IW8R_NO_AO`, and the bench line `game_ao`.
- **Benches measured so far:** Hideout 12.0 ms, Aniyah Tac 18.1 ms, Verdansk ground camera 24.4 ms.
- **To finish:** run the tests, then commit and push. Record the AO in CHANGELOG / PROGRESS together with the decision
  to leave small props out of the pre-pass.

## Milestone 1 — Lighting, complete and integrated
Order: the streamed world first (owner hint), then the HDR foundation, then the features, then the GUI, then the build
side.

**1.1 Streamed world tiles follow the camera (Warzone maps).** DONE 2026-11-13 (see PROGRESS; limits listed there).
- **Automatic loading.** Turn the manual command into a background, camera-following loader. Reuse
  `MainWindow::loadStreamedWorldAroundCamera` (`main_window_stock_edit.cpp:541`), `WorldCache::requestStreamed`,
  `readStreamedWorldZones` / `streamedZonesNear` (`src/core/stock_world.cpp`), and the tile lightmap jobs
  (`world_cache.cpp:735`). It follows the same pattern as `updateTerrainLayers`: a QTimer, rebuilding after the camera
  has moved some distance.
- **Incremental drawing.** Tiles are appended as their own world batches, the way decal extras are
  (`appendReferenceDecals` and the extras), instead of reloading the whole reference.
- **Cache budget.** Add the setting `stock/streamCacheGB` (default 3 GB, since disk space is tight), with LRU eviction
  of `<world_cache>/<zone>/streamed/<tile>`.
- **Also:**
  - Tiles get their lightmaps and their props (static models in the tiles).
  - Re-check the terrain cut and `cutoutStride` once the real tile geometry fills the holes.
  - Confirm what the 1,024 `mp_<map>_cg_*` zones hold (research 90 says collision tiles).
- **Done when:**
  - From the raised Verdansk camera there are no grey sheets and no ground holes.
  - The frame stays within budget.
  - The cache stays within its budget.
  - Tests cover tile selection, eviction and the extras append.

**1.2 HDR scene target (the foundation for SSR, bloom and exposure parity).** DONE 2026-11-13.
- **New pipeline.** Game Lighting draws the scene into an `R16G16B16A16_FLOAT` target with linear output. A post pass
  then applies `gamePost` (exposure, log shaper, LUT) into the 8-bit frame. Editor overlays (lines, markers, gizmo,
  selection) are drawn after the post pass and are never tone-mapped.
- **Unchanged:** the other modes (Lit, Textured and so on).
- **Exposure metering.** It moves onto the HDR target: log-average with the `(1-r)^tonemapFocus` weight (research 95
  section 3). It no longer runs through the inverse grey curve on the 8-bit frame.
- **Files:** `src/viewport/render.cpp` (`drawFrame` order, `ps_tex` game branch output, `meterFrame`, a new post
  shader), `camera_view.cpp`.

**1.3 Screen-space reflections.** DONE 2026-11-13 (research 104).
- **Research.** Decode the game's SSR: the `r_ssr*` dvars, the techsets ending in `…2` (research 98 section 6), and the
  code_post_gfx SSR shaders. Export them through ACTS and disassemble with `out/agent/shader_research/py/dxbc_dis.py`
  (as research 100 did). The result is a new research note.
- **Implementation.** A trace over the previous HDR frame and the depth, blended with the probe by the game's rule, with
  a probe fallback. It applies to stock and own materials.
- **Controls:** a toggle and a quality setting.

**1.4 Stock shading parity: the small items.**
- **Research 107 (2026-11-13) DONE:** lit-buffer constants, emissive x c36, transmission (ambient + sun), wrap diffuse
  (lightmap + sun), probe rough fallback. Wrap and transmission for local lights DONE 2026-11-14. Left by decision
  (table "Open points closed by decision"): the normal and gloss of detail layers on props.
- **Material and occlusion:**
  - normal decode `z = 1 - 0.6|xy|²`;
  - read `gtso_lut` from the install (cached, like the env BRDF) and use the game's specular occlusion (DONE);
  - sun specular × cavity;
  - wrap diffuse for the `100a` techsets (`src/core/stock_decal.h` helper).
- **Probes:** on layer and decal passes too (DONE), with the cb7[186] rough-surface fallback (research 107). More
  than 128 boxes: no MP map has more than about 70 (the cached probe files); only Warzone would, and its probes are in
  the streamed tiles (index.json: "reflectionProbeCount 5620 does not match its records"), so it moves to 1.5b.
- **Foliage:** transmission (slot 68 × constant 18, research 100 section 3.3).
- **Layers:** colour modes 2/3, and layer gloss / normal.
- **Glass:** f0 .04 with gloss ≥ .9 plus probe reflection.
- **Effects:** emissive, not surface-lit.
- **Sun:** disc and flare.
- **AO:** a strength setting.

**1.5 Lighting for static models and props.** Core DONE 2026-11-13 (research 105): probe-lit props take their own
light grid box (Hideout 12,131 of 12,367 placements). Left by decision (table "Open points closed by decision"): SELFVIS (needs an ACTS change), the 236 lightmapped `tml`
props (still the probes), Warzone tile grids (tile props fall back to the probes).
- **Research.** The smodel vertex lighting (`AMBIENT_COLOR`, `HIGHLIGHT_DIR`, `SELFVIS`), the light grid (t102-t105:
  SH L1, index, heightfield, mask), and `PackedLmapCoords` for lightmapped `tml/` props. Use ACTS gfx_map exports and
  the Replay 1.20 disassembly.
- **Implementation.**
  - Props sample the light grid instead of the one map-wide value.
  - SELFVIS AO on models and foliage.
  - Lightmapped props draw with their lightmap.
- **Done when:** a prop indoors is as dark as the world around it.

**1.5b Warzone baked light per tile.** DONE 2026-11-13: tile light grids and the Warzone probes (research 108; the
game's tile activation rule is replaced by the nearest-boxes subset). The main gfx_map lists every prop, but a prop's light grid box is in the
transient zone `worldTransient` (research 105 section 5.1), and the reflection probes are in the tiles too. The tile
lightmap job already decompresses each tile: cut its light grid block there as well (keyed by the transient index),
decode the tile probes, and draw tile props and tile ground with them. Done when Verdansk props and wet roads near the
raised camera use their own tile's light and probes.

**1.6 Local lights.** DONE 2026-11-13 (research 106: every spot / omni at runtime, nearest 256, IES, tile-binned;
64 shadow map slots). Left by decision (table "Open points closed by decision"): needsDynamicShadows (entities in light maps), the screen-space shadow mask path.
- **Stock maps.** Load their own primary lights (ComWorld light types other than the sun), which today are skipped at
  `main_window.cpp:7734`.
- **Culling.** Clustered or tiled: a structured light list with per-tile indices, up to 254 lights.
- **Shadows.** Spot shadow maps for the nearest shadowed lights.
- **Light keys honoured:** IES (`iw8r_ies`), `iw8r_falloff`, `iw8r_bulb_radius`, `iw8r_shadows`. A spot wider than 150°
  becomes an omni light (`compile_iw8.cpp:2645`).
- **Specular:** GGX with the material's f0 and gloss.

**1.7 Emissive and decals.**
- **Emissive:** stock world and props get emissive (slot 18) with the material's emissive scale, in HDR.
- **Decal volumes:** normal, gloss, cavity and emissive (research 99 :177-179). The user's own `iw8r_decal` entities
  are shaded in Game Lighting.

**1.8 Atmosphere and post.** Part 1 DONE 2026-11-13 (research 109: bloom veil and vignette). Film grain DONE 2026-11-14. Left by decision (table "Open points closed by decision"): volumetric
fog, white balance.
- **Volumetric fog / veil** from the stock vision, and for own maps the user's `iw8r_volume`.
- **Bloom and veil.**
- **Own maps:** honour `iw8r_exposure_adjust` and `iw8r_vision`.
- **Previews that must work in Game Lighting:** Colour Grade, HDR fog, and the Lit Brightness dialog as an exposure
  override.
- **Sun shadows:** 3-4 cascades fitted to the view on big maps.

**1.9 Own brushes match what the build will produce.** Part 1 DONE 2026-11-13 (research 117: the unbaked look, the
multi-bounce occlusion). Brushes added to a stock map DONE 2026-11-14 (the map's tetrahedral light grid, research
117 section 5). Normal and specular maps of own materials DONE 2026-11-14 (the converter's SourceSunSpecular rule;
see PROGRESS). Own-map models take the build's light grid light at their place DONE 2026-11-14.
- **Unbaked maps.** Show the preview bake only when `iw8r_bake_lightmaps` is set; otherwise show the unbaked path the
  game takes (research 55).
- **Specular.** Own brushes get environment specular, using the converter shader's formula
  (`map_surface_realtime.hlsl:1470-1573`).
- **Brushes added to a stock map.** Light them as the carried `script_model` will be lit (light grid + probes), not by
  a lightmap.
- **Own-map models.** Match the light grid that the build writes (1.11).

**1.13 Stock prop LODs (performance; found 2026-11-13).** DONE 2026-11-13 (Aniyah 10.3 ms, Speedball 6.1 ms).
Left by decision (table "Open points closed by decision"): the game's FOV / lodScale factor on the distances. Aniyah draws 30 M triangles a frame because every prop is
LOD0 (GPU profile: props 10.2 of 23.5 ms). The model detail already lists each LOD's surfaces and distance, and
ModelCache::requestLod fetches one LOD of one model per ACTS run. Fetch the LODs in batches, keep one mesh per LOD, and
let the instance cull pick the game's LOD by distance (and FOV). Done when Aniyah is within 20 ms and Verdansk
within 28 ms with no visible popping at the bench cameras.

**1.10 GUI and settings.** Part 1 DONE 2026-11-13 (View > Game Lighting Options, saved settings, GUI test). The
compile panel's bake settings DONE 2026-11-13 (Compile > Bake Settings, core bake_options). Shadow quality DONE
2026-11-13 (Sun Shadow Detail: 1024 / 2048 / 4096). Nothing open in 1.10.
- **View > Game Lighting submenu:** AO (on/off, strength, radius), SSR (on/off, quality), shadow quality, an exposure
  override (EV), Re-bake Now (`rebakeGameLighting`), and debug views (AO buffer, SSR mask).
- **Saved settings:** these, and the Game Lighting toggle itself, are saved in Settings. Environment variables stay as
  bench overrides only.
- **Compile panel:** bake settings (sky, texel size, rays), and the per-face lightmap scale passed through to the build
  (ROADMAP 5.21).

**1.11 Lighting of a compiled custom map.** Part 1 DONE 2026-11-13: the light grid is baked from the map (sky and bounce
per cell, 56 directional samples). 2026-11-14: the bounce takes the surface's colour and the later bounces are
added (lightmap; the light grid takes the colour). Left by decision (table "Open points closed by decision"): a
HDR probes, the parity proof. The per-face lightmap scale is DONE 2026-11-14 (in the bake itself; no converter change).
- **Lightmap bake:**
  - the sky from the vision / sky colour;
  - bounce from real albedo (the texture averages);
  - several bounces;
  - multithreaded or GPU;
  - per-face lightmap scale. The converter must read it, which is an x64-zt change.
- **Light grid from the bake** (sky, bounce, lights) instead of a flat ambient (ROADMAP 5.20).
- **Reflection probes:**
  - captured with Game Lighting on, in game light units, HDR / BC6H, at a higher resolution;
  - this fixes F.10 ("probes make no visible change").
- **Offline proof:**
  - Open the built zone as a stock map, so the editor draws it with its own lightmaps, probes and grid.
  - Compare that with the editor's preview of the source map: per-region luminance within ±10 %.
  - Read the built zone back with ACTS / `tools/verify_map_output.py`.

**1.12 Docs.**
- A research note for each decode.
- A USER_GUIDE lighting section.
- Fix the stale lines: ROADMAP 5.11, USER_GUIDE:613, the `render.cpp:525` comment.
- Add the owner's in-game look items to TEST_CHECKLIST.

**Milestone 1 is done when:**
- Every item in the survey's gap table is done, or recorded as a documented decision.
- Speedball matches the owner's shot on matched features within ±10 %.
- Every map is within its frame budget.
- Every feature has a GUI toggle, a saved setting and a test (core maths tests, plus a GUI test section that checks
  toggles and saved settings).
- The built-zone parity check passes on the sample map and on one stock-edit map.

## Milestone 2 — The Umbra system
**2.1 Decode the tome.** DONE 2026-11-13 (research 110, core umbra_tome with a visibility query and tests on nine
stock zones).
- **What is decoded:** the Umbra 3 `ImpTome` (v0xD6000014) cell nodes, portals, view trees, clusters and LOD tiles.
- **Sources:**
  - the game exe's Umbra runtime (query consumer at 0x18CCA80) through IDA;
  - the IW7 struct names in x64-zt `src/games/iw7/structs.hpp`;
  - the header work already in `tools/stock_world_remove_test.py` and research 96 :111-183.
- **Deliverables:**
  - a research note;
  - `src/core/umbra_tome.{h,cpp}`: a parser for the header, tiles, cells, portals and objects;
  - a visibility query: camera point → visible object user IDs (classes surface / smodel / light / probe / decal);
  - tests on stock tomes (mp_shipment, at least 5 more MP zones, Verdansk's main zone): CRC, counts and
    self-consistency.

**2.2 A visibility view in the editor.** Part 1 DONE 2026-11-13 (View > Umbra Visibility: cull by tome for surfaces
and props, cells and portals overlay, status bar counts, saved settings). Left by decision (table "Open points closed by decision"): culling lights, probes and decal
collections; surface ids on displaced terrain.
- **Overlay:** tiles, cells and portals.
- **"Cull by tome" mode:** the stock reference draws only what the tome lets the game draw from the camera.
- **Statistics:** visible objects by class.
- Menu entries and saved settings.
- This shows what the game hides, offline.

**2.3 Stock-map edits keep the tome correct.** DECISION 2026-11-14: not built. Removing stock world geometry keeps
opening the whole map's occlusion (everything is drawn, as it has been since research 96). That costs frame time on
such a map and nothing else. A local update would rewrite single tiles of a stock tome and their links to the
neighbouring tiles; an error there makes objects vanish in the game, which only a playtest shows, and the owner does
no playtests. The safe form stays until someone can look in the game. The decoder (2.1) and the bake (2.4) hold what
a later attempt needs.
- **Local tile replacement.** Replace only the tiles that an edit touches (with one-cell tiles, or re-baked tiles once
  2.4 exists), instead of opening the tome across the whole map.
- **Editor side.** `stockWorldRemovalJson` (`src/core/stock_zone_edit.cpp:273-297`) writes the `"occlusion"` key. This
  depends on the x64-zt editzone change.
- **Warzone.** Allow removal of streamed (transient) entries once the tile and streamed data are handled (ROADMAP 13.5).
  Blocked at install (research 116, 2026-11-13): the client loads no unsigned zone of a streamed tile type, so an
  edited tile cannot go into the game. Decision recorded: edits stay in the main map zone; a tile edit needs a client
  change by the owner.
- **Stale text to fix:** CHANGELOG :66-68, ROADMAP 11.5, `main_window_stock_edit.cpp:314-315`.

**2.4 A real occlusion bake for custom maps.** DONE 2026-11-14 as an opt-in (research 118: cells and portals from
the opaque box brushes, the conservativeness test, a real build read back). 2026-11-14 also: slanted and many-sided
opaque brushes count by the box inside them (core occluder_box). Left by decision (table "Open points closed by decision"): thin slanted walls, terrain and models as
occluders, the per-brush occluder key, gates, the world's merged surfaces (they limit what can be hidden), and the
in-game look.
- **Bake.** Voxelize the occluders (opaque structural brushes, large models with collection flag 128). Flood the empty
  space into cells and portals per tile, and encode a multi-tile tome with real per-object bounds.
- **Where the code goes.** The bake algorithm goes in core `src/core/umbra_bake.*`, which emits a neutral description.
  The x64-zt encoder replaces `BuildConservativeUmbraTome` and keeps `validateUmbraTome`.
- **Contribution culling.** Keep object-contribution culling conservative: props once vanished with it, and
  `r_umbraMinObjectContribution` was the fix.
- **Per-brush and per-model controls:** occluder yes/no keys, and door gates later.
- **Conservativeness test.** From sampled camera positions, every object that ray casting can see must be visible
  through the tome. Any miss fails the test.
- **Report.** The fraction of objects culled per sample position, as the offline performance proxy.

**Milestone 2 is done when:**
- The decoder passes on every stock MP zone.
- The visibility view works.
- Stock edits change only local tiles.
- Custom maps get a baked tome that passes the conservativeness test.
- The owner's in-game look (performance and nothing missing) is on TEST_CHECKLIST.

## Milestone 3 — Web viewer at the level of the Windows GUI
ROADMAP L10, paused until now, is resumed by this request. Server: `src/qtapp/web_server.{h,cpp}`. Client:
`data/web/*`. Wiring: `main_window.cpp:4503-4592`.

**3.1 Remote 3D view.** DONE 2026-11-13 as frames on demand (GET /api/frame, POST /api/pick, the page's "Editor view
(live)"). Left by decision (table "Open points closed by decision"): streamed tiles and terrain layers follow only the editor's own camera.
- **Render API.** SceneRenderer gets "render this camera into an offscreen target", reusing the device's resources.
  There is no second renderer and no texture duplication.
- **Per client:** each browser has its own camera (orbit / fly) and size, sent by POST.
- **Streaming.** The editor renders on demand and streams JPEG/WebP frames (multipart HTTP, or a binary WebSocket if
  Qt WebSockets is available). It throttles to the client and sends nothing while the tab is hidden.
- **Full parity:** every camera mode, Game Lighting, the stock reference, terrain and AO.
- **Picking:** done server-side by ray at the click.

**3.2 Protocol.** DONE: part 1 2026-11-13 (lean model boxes; `GET /api/map?since=` differences), the log backlog
with filters and the undo history list 2026-11-14. Send incremental scene diffs by item revision instead of re-sending the whole `/api/map` on every
revision. This is needed for Verdansk's 23k entities. Log backlog and filters through `/api/log`. An undo history list.

**3.3 Every command from the browser.** Part 1 DONE 2026-11-13: the command registry (GET /api/commands, POST
/api/command) and the generated menu bar with search. Part 2 DONE 2026-11-13: web forms (GET /api/forms, GET
/api/form, POST /api/form) for Sun & Sky, Atmosphere, Lightmap Bake Settings, Map Info and Game Modes (preset +
action; presets are made in the desktop dialog) and Check Map (report + fix all). Items 3 and 4 of the list below
are plain commands and run from the web menu already. 2026-11-14 also: vision overrides (item 1, in the Atmosphere
form), Preferences (7), prefabs in the Assets panel (8), Surface / UV (9), Material / Model / Effect Info (10, the
info button of the Assets panel), compile options and Build Budget (5). All ten are in the page.
- **Command registry.** Expose the registry that `addCommand` builds (id, label, menu path, checkable, checked,
  enabled) as `/api/commands` and `POST /api/command`. The web menu bar is generated from it, so every action is
  reachable.
- **Dialogs.** Web forms built from a shared JSON form schema, in owner-scope order:
  1. Sun & Sky / Atmosphere / Vision.
  2. Game Modes.
  3. Edit Loot Points / Placed Objects / Load Streamed World.
  4. Stock-edit actions: Rebuild, Hide / Show props, Relight.
  5. Compile options and Build Budget.
  6. Check Map / Map Doctor.
  7. Preferences.
  8. Prefabs.
  9. Surface / UV (basic).
  10. Material / Effect Info.

**3.4 Editing tools.** DONE 2026-11-14 for: drawing a brush, resize handles, placing any entity class (on the
surface in the live view), the inspector with entity definitions; clone / delete / copy / paste / rotate run as
commands from the web menu. 2026-11-14 also: vertex and edge handles (POST /api/vertices), layers (the Layers form;
hidden layers leave the page's views), region (View > Region from the web menu; the page draws what the editor
shows), face picking (Ctrl + click in the live view; the Surface form fits and justifies the picked faces), patch
control points (Tool: Vertices; GET /api/handles). Nothing open.
- **2D:** create a brush by dragging, resize handles, clone / delete / copy / paste, rotate / scale, vertex and edge
  modes, region, layers.
- **Selection and placement:** face picking with surface fit; entity placement of every class and template at the 3D
  pick point (not z = 0).
- **Inspector:** shows entity definitions (key help, validation, required keys).

**3.5 Security unchanged.** PBKDF2 auth, lockout, optional TLS, LAN off by default. Firewall and router stay with the
owner. Nothing ever prints the `[web]` section.

**3.6 Tests.** Extend the GUI test's web section to cover the commands, form round trips, a decoded render frame and
diffs. Do a browser check of `localhost` in the built-in browser pane.

**Milestone 3 is done when:**
- Every row of the gap table is "yes", or "partial" with a stated reason.
- FEATURES.md and USER_GUIDE have a web section.

## Open points closed by decision (2026-11-14)
The lines marked "Left by decision" above stay as they are for this hand-over. None of them stops a workflow. Each is a
refinement of how close the viewport is to the game, or of how much the occlusion data hides, and each can only be
judged against the running game, which is not used any more. They are listed so that nobody has to find them again.

| Point | What is there now | Why it stays |
|---|---|---|
| 1.4 the normal, cavity and gloss of detail layers on layered prop materials | Sun, lightmap and lamp wrap and transmission; layers with colour mode 2 or 3 leave the colour alone, as in the game; the AO strength is a setting at the game's value | 43 layer slots on props in the three maps read (crate foam, concrete slabs); they need the prop materials' layer tables, which the material export does not give. |
| 1.5 SELFVIS, the 236 lightmapped `tml` props, Warzone tile grids for tile props | Props take their own light grid box; the rest fall back to the probes | SELFVIS needs a new ACTS export; the others touch 2 % of placements. |
| 1.6 `needsDynamicShadows`, the screen-space shadow mask | Every spot and omni light with shadow maps for the nearest 64 | Affects moving entities and contact shadows only. |
| 1.8 volumetric fog, white balance | Bloom veil, vignette, film grain (2026-11-14), exponential fog, the stock vision's exposure and grade | The volumetric fog constants are not calibrated (it stays off rather than wrong); white balance is not decoded. |
| 1.11 HDR probe capture | Bounces in the surfaces' colours with the later bounces estimated, a lightmap scale per face, the texel size as a Bake Setting (all 2026-11-14), 8-bit probes captured before every build | HDR probes change glossy reflections by a little and need the converter to take another image format. |
| 1.11 the parity proof (draw the built zone as a stock map and compare) | `tools/lightmap_bake_build_test.py` proves the baked data reaches the built zone; View > Game Lighting uses the converter shader's own formulas (research 117) | The proof as planned would compare the wrong things: a built map is drawn by the converter's world shader, and the editor's stock-map path models the game's stock shaders, not that one. |
| 1.13 the game's FOV / lodScale factor on LOD distances | Stock LOD distances as stored | Changes only the distance at which a LOD switches; the frame budgets are met without it. |
| 2.2 culling lights, probes and decals in the Umbra view; surface ids on displaced terrain | Surfaces and props are culled by the tome | A viewer refinement: the view already answers "what does the game hide here". |
| 2.3 local occlusion updates for edited stock maps | The whole map's occlusion opens when stock geometry is removed | See 2.3: a mistake makes objects vanish in the game. |
| 2.4 thin slanted walls, terrain and models as occluders, gates, splitting merged surfaces | Opaque structural brushes (boxes, and other shapes by the box inside them); a detail brush is left out, which is the per-brush switch (2026-11-14) | The bake is opt-in and unseen in the game; widening it before a first look would only widen what could go wrong. |
| 3.1 streamed tiles and terrain layers around the browser's camera | They follow the editor's own camera | Two cameras asking for different tiles would evict each other's from the cache; fly the editor's camera there first. |

## Milestone 4 — The remaining pieces
Ordered by the owner's scope. ROADMAP rows are in brackets; each row there has the full state and the proofs.
State on 2026-11-14, one line each. "Offline" = built and proven without the game; "game" = only a look in the game
can take it further (docs/TEST_CHECKLIST.md has the steps).

- **Stock map editing**
  - native brushes inside a stock world (11.4): done offline (added brushes are built as models of a helper map and
    carried in); game.
  - Warzone removals (11.5 / 13.5): done offline for the main map zone; streamed tiles cannot be edited (the client
    loads no unsigned tile, research 116): decision recorded in 2.3.
  - name-only `script_model` references (4.7): not built; stock models are carried as `iw8r_stock_*` copies, which
    works for every model.
  - world materials from other maps (4.9 / 4.15): partial by design (research 51): a name only resolves against zones
    loaded while the map plays; other materials are rebuilt from their images.
  - streamed JSON parse memory (9.4): done (measured).
  - catalogue gaps (4.25): partial (research 52); game for the smallest image parts.
- **Warzone**
  - doors outside their area box, editing streamed entries: blocked with the streamed tiles (see above).
  - `common_br_mp` for an own map, the ATV textures, vehicles and buy stations on a custom map (13.4): done offline
    (start entities, switches, loot, vehicles, buy stations); game.
- **Map and mode creation**
  - templates and the flag prop (6.2), preset dvars (13.2), infil and match cameras (6.19), scripted objects (6.13),
    water, ladders and mantles (6.8), doors (6.24), nav view and blockers (6.23), animated models (4.21): built
    offline, each "partial" in ROADMAP only for its look in the game.
  - new game types (13.6): partial (research 91): a new game type name starts and the map script shapes it; a stock
    game type script cannot be replaced by a map zone.
  - killstreak airspace and compass (6.21 / F.14): open; research 75 found no per-map key for it. Game.
  - fonts and LUI (4.27): HUD from the map script works with stock icons; own fonts and menus are not built
    (deferred, as the plan allowed).
- **Audio and effects**
  - the looping sound on an `fx_origin` (F.6), impacts (F.12): fixed offline; game.
  - the reverb zone (F.7), PA speakers and music states (6.22), ambient sound (6.7): built offline; reverb was not
    heard in the second playtest (F.7 open); game.
  - the Effect Editor (8.2): done. Hot update (7.7): built; needs the running game.
- **Assets**
  - techset state words (12.1), winding (4.11), physics assets (4.20), the tint rule (F.13): done offline.
  - layered blend painting (5.9 / 8.11): vertex paint on patches is built; painting layer blends on brushes is not.
- **Live Link:** camera angles (7.3), capture (7.6 / 7.11), overlay fields (7.15): built against the owner's client;
  each needs the running game to go further.
- **Performance and UX**
  - per-item buffers (3.11): done (kept camera geometry, changed per item).
  - the region outline: done 2026-11-14 (2D and camera views).
  - preview decode off the GUI thread, one process per validation run: not built; neither blocks a workflow
    (decode is budgeted per frame, validation runs per zone).
- **Packaging**
  - RELEASE_CHECK decisions 1-4: settled with defaults on 2026-11-14 (docs/RELEASE_CHECK.md).
  - exe MD5 check (9.1): done (First-Run Setup).
  - final installer: rebuilt with every milestone; clean-machine install: the owner's.
- **Docs and tests**
  - FEATURE_MATRIX, MILESTONES, stale ROADMAP rows and home-folder paths in the docs: done 2026-11-14.
  - USER_GUIDE web section: done 2026-11-13 / 14.
  - fixtures: a missing fixture counts as a skip in the suite's last line (tests/test_harness.h), so a silent skip
    is no longer possible; `out/m2-x64zt` and `out/m2-script-check` are present.

## Milestone 5 — Handover
State on 2026-11-14:
- **Release:** done. `out\installer\IW8RadiantSuite-Setup-0.1.0.exe` and `out\package\IW8RadiantSuite-<date>.zip`,
  rebuilt after the last change; no ini and no game file inside; the packaged exe passes its start-up test.
- **Docs:** done. HANDOVER.md, USER_GUIDE.md, FEATURES.md, KEYS_REFERENCE.md, FEATURE_MATRIX.md and MILESTONES.md
  are current. The pictures of FEATURES.md (`--feature-shots`) are from 2026-11-12; the web page and Game Lighting
  have changed since, the other pictures are still what the editor shows.
- **Owner checklist:** done. docs/TEST_CHECKLIST.md starts its In game part with "the short list" (six items).
- **Parking:** done. The editor is left open on a compiled map; PROGRESS.md and HANDOVER.md are current.
- **What the hand-over cannot contain:** a look in the game. Everything built since 2026-11-11 is proven offline
  only; HANDOVER.md section 4 says which six looks would settle it.

## Verification (per milestone)
- `tools/run_all_tests.ps1` passes (core, GUI, smoke); the counts never drop.
- Every map is within its frame budget at the bench cameras (`out/agent/mapcam.ps1`; the bench report lines
  `game_still`, `game_ao`, `base_materials`, `layer_pass`).
- **Milestone 1:** Speedball against the owner's shot (`out/agent/grass_cmp.py` plus matched-feature means); Verdansk
  raised camera free of grey sheets and holes; built-zone parity check.
- **Milestone 2:** tome decode tests on stock zones; conservativeness test on baked sample maps.
- **Milestone 3:** web GUI tests; built-in browser check on `localhost` (render stream, commands, forms).
- Every converter change is proven by built-zone read-back (`tools/verify_map_output.py` plus the zonetool log).
