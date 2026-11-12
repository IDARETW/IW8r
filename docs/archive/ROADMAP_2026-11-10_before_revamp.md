# IW8 Radiant Suite: layered roadmap to a full suite

Target: an editor that gives MW2019 (Replay 1.20.4.7623265) mappers what iw3xo-radiant gives CoD4 mappers. That means the
stock Radiant editing set, a textured and lit live preview, asset browsers, one-button build/deploy/launch, and live link.
It also needs the IW8-only systems that IW3 never had: PBR layers, probes, IES lights, vision, volumetrics, spawnList,
scriptables and more.

Sources: [FEATURE_MATRIX.md](FEATURE_MATRIX.md) (row-level status),
[research/01](research/01_iw3xo_feature_audit.md) (iw3xo inventory),
[research/09](research/09_iw8_render_features.md) (IW8 render systems), [research/15](research/15_iw8_extraction_contract.md),
[research/17](research/17_x64zt_buildextraction.md) and [research/18](research/18_x64zt_stock_assets.md) (build route).

**How to read this.** Each layer is a coherent tier of the suite and has a mapper-facing exit test. A layer depends only on
the layers marked as its prerequisites. Several layers can be worked in parallel.
- Size: **S** = one focused session, **M** = a few sessions, **L** = a week or more of sessions, **XL** = multi-week or open
  research.
- Flags:
  - **R** = blocked on research (an unknown in the game, x64-zt or ACTS).
  - **U** = needs an owner decision or owner action: an in-game test, a change in the owner's x64-zt / proxy repos, or writing
    to the game install.

## Layer map

| Layer | Goal | Prerequisites | Size | Parallel with |
|---|---|---|---|---|
| **L0 Foundation** | Lossless documents, undo, Qt shell, D3D view, IW8 readers, offline build pipeline | none | done | n/a |
| **L1 In-game ground truth** | Editor-built maps load and play correctly in Replay | L0 | M (+U) | L2, L3 |
| **L2 Stock Radiant editing parity** | Every stock CoD Radiant editing tool | L0 | L | L1, L3 |
| **L3 Viewport and preview** | iw3xo-class textured, lit, model-showing preview | L0 (shares the UV module with L2) | L | L1, L2 |
| **L4 IW8 assets: browsers and builds** | Real stock materials and models in the editor and in builds; custom assets | L1, L3 | L (+U) | L5 |
| **L5 IW8 lighting and atmosphere** | Lights, IES, sun, probes, vision, fog, volumetrics, decals | L1, L3, L4 (materials) | XL (+R) | L6 |
| **L6 Gameplay and IW8 entity systems** | Spawns, modes, scripts, scriptables, FX/sound placement, stock-map editing | L1 | L | L5, L7 |
| **L7 Live link and iteration** | Editor ↔ Replay link, camera sync, hot update, one-key build/deploy/launch | L1 | L (+U) | L5, L6 |
| **L8 Advanced authoring** | FX preview/editor, mesh painter, terrain, prefab editing | L3, L4, L6 | L | L9 |
| **L9 Product polish** | Setup, preferences, help, performance at stock scale, distribution | all (ongoing) | M, ongoing | everything |

Recommended order: start **L1** now (it needs one in-game test from the owner). At the same time build the shared
foundations of **L2 + L3**: the UV projection module, face selection, texture lock and the textured view. Then **L4**
(textured builds), then L5 / L6 / L7 in parallel, then L8. L9 runs alongside throughout.

---

## L0. Foundation (done)

The lossless `iwmap 4` + IW8 entity-string model, with byte-exact round trips and undo (compound steps too). The Qt 6 + ADS
shell with a Direct3D 11 camera and 2D grid views. The IW8 key table, the ACTS MapEnts/spawnList reader and the x64-zt IW8D
reader/writer. Brush tools (create, resize, clip, hollow, subtract, rotate/flip, prism, make entity), patch tools (create,
invert, transpose, natural, rows/columns, thicken), vertex mode, the Surface panel, prefabs (insert, render, explode, save),
Find/Replace and Check Map. Material and model name browsers. The native IW8 extraction compiler with brush models, F7 build
through x64-zt, ACTS validation, deploy with backup, the CLI, and core (96) and GUI (80) tests. Details in
[MILESTONES.md](MILESTONES.md).

## L1. In-game ground truth (gate for everything that ships to the game)

Exit test: the sample room and a three-room test map load from F6 in Replay. The player spawns on a TDM spawn. Walls and
player clip block. A `trigger_multiple` fires (script print). The map shows its title in the F6 browser.

| # | Item | Ref | Status | Size | Notes |
|---|---|---|---|---|---|
| 1.1 | Owner test: F7 → Deploy → F6 for the sample room; record the result in the tracker | IW8 | partial: packages listed and selectable in F6 (owner, 2026-11-08); load/play not yet reported | S, **U** | First real signal. Everything below may change with it |
| 1.2 | Owner test of M2.1 (edited stock `mp_shipment` entities) | IW8 | open | S, **U** | Validates the stock-map edit path |
| 1.3 | Game/proxy log panel: read Replay/proxy logs after a run and surface errors | iw3xo+ (console) | done: Replay Log dock (proxy, engine, exceptions, DW client, crash logs; problem/map filters) | S | Debug loop for 1.1 |
| 1.4 | Launch Replay from the editor (exe + proxy custom-map selection, no injection yet) | iw3xo+ | done: Compile > Launch Replay (owner-triggered; exe configurable) | S, **U** | Owner approves starting the game |
| 1.5 | IW8 contents table: player/weapon clip, nosight, no-collision, ladder, water, detail; tool-material semantics | stock | done: solid, player clip, item clip, AI no-sight, nonColliding, detail; water/missile bits UNKNOWN | M | Compiler + `contents` line editing |
| 1.6 | Surface types from materials → collision tags (footsteps, impacts) | stock | done: brush `surface_type` from the dominant face material (keyword table) | M | `surfaceTypeBits` / collision materials |
| 1.7 | Leak / sealing check with a pointfile line in both views | stock | done: voxel flood fill, Tools > Leak Check draws the pointfile; compile warns | M | Flood fill over brush BSP |
| 1.8 | Unbaked-lighting baseline: make sure a map without lightmaps renders visibly (ambient/sun defaults) | IW8 | open | S, **R** | Depends on what 1.1 shows |
| 1.9 | Gametype sanity: TDM/FFA/DOM spawn sets, `mp_global_intermission`, Check Map rules for spawn overlap and spawns in solids | IW8 | done: spawn-in-solid, overlap, floating, intermission, per-mode minimums (Check Map, compile log) | S | |
| 1.10 | `map.json` editor (title, description, compass rotation, minimap scale) + `preview.rgba` | IW8 | done: Compile > Map Info (title, description, compass, minimap scale, ladders, exposure) | S | |
| 1.11 | Minimap / compass image from a top-down render | iw3xo~ | done: DXT5 compass image + two minimap_corner script_origins written by the compiler (in game NOT VERIFIED) | M | Clears the compass warning |
| 1.12 | Deploy while Replay runs (rescan with R in F6); Compile and Deploy (Ctrl+F7) | IW8 | done: a loaded map's locked package is left in place with a clear message | S | Owner request 2026-11-08 |

## L2. Stock Radiant editing parity

Exit test: block out and detail a three-room map with a door, stairs, an arched doorway and a terrain patch, without
editing text or leaving the editor.

| # | Item | Ref | Status | Size |
|---|---|---|---|---|
| 2.1 | **Shared UV projection module** (Radiant axes, texture fields) used by the compiler, the viewport and texture lock | stock | done: `tex_projection` shared by compiler, viewport and texture lock (output byte-identical) | S |
| 2.2 | **Face selection** (Ctrl+Shift+click a face), per-face material apply, Surface panel per face | stock | done: Ctrl+Shift+click faces; Shift+M, Surface panel and Fit act on faces | M |
| 2.3 | Texture lock for move / rotate / scale | stock | done: move, drag, nudge, rotate, flip (Shift+T) | M |
| 2.4 | Surface inspector: fit, justify, natural, step sizes, lightmap fields | iw3xo~ | partial: Fit, Natural, Justify L/R/T/B/C; lightmap fields open | M |
| 2.5 | Free rotate, arbitrary rotation dialog, scale dialog (lock axes) | stock | done: Brush > Arbitrary Rotation (one undo step), Scale; texture lock aware | S |
| 2.6 | 3-point clipper, CSG merge, auto-caulk, split coplanar | stock | done: CSG Merge, 3-point clipper with Clip (Enter), Split (Shift+Enter, keeps both halves) and Flip, Brush > Auto Caulk (core auto_caulk.*) | M |
| 2.7 | Primitives: cone, sphere, arbitrary-sided cylinder dialog | stock | done: Primitives menu (cone, sphere, stairs, arch, ramp) | S |
| 2.8 | Edge drag mode (E) | stock | done: E in a grid view, midpoint drag, one undo step | M |
| 2.9 | Extrude selection to a traced face | iw3xo+ | done: Brush > Extrude Selected Face (grid distance) | S |
| 2.10 | Select modes: complete tall, partial tall, touching, inside, Alt+Shift cycle, select connected, key/value select dialog, hide unselected | stock | done: complete/partial tall, touching, inside, select connected (Ctrl+E), hide unselected (Ctrl+H), Alt+Shift click cycle | M |
| 2.11 | **Layers** (the `iwmap` layer lines): panel, show/hide/lock, move selection to layer | iw3xo~ | done: Layers dock (visibility, select, move selection, new layer; header edits undoable) | M |
| 2.12 | Region (set from selection / brush, save region) | stock | done (View > Region: from selection, from XY view, off; outside hidden in all views) | S |
| 2.13 | Brush flags UI: detail / structural / clip types / non-colliding (`contents`, `toolFlags`) | stock | done: Brush menu Make Detail (Ctrl+M), Make Structural, Toggle Non-Colliding; `src/core/brush_flags.*` (detail, nonColliding, weaponClip); `toolFlags` still preserved only | S |
| 2.14 | Patch: cap, cap texture, weld/connect, redisperse, split, lock verts, curve-edit dialog | stock | done: cap, weld, redisperse, split (Patch menu); lock verts open | M |
| 2.15 | Terrain patches, noise generator, terrain thickening | iw3xo+ | done: Create Terrain, Terrain Noise, Terrain Smooth, Terrain Brush (XY view, LMB raise / Shift lower, one undo per stroke) | M |
| 2.16 | Entity definitions for IW8 classes: key docs, defaults, sizes, colours, validation (from stock maps + docs) | stock (`.def`) | done: data/iw8_entity_defs.json (98 classes); inspector shows description, build status, key help, missing required keys with Add, invalid value tint; Check Map validation | M |
| 2.17 | Entity show modes (names, angles, connections, model origins), entity colour, group dialogs | stock | done: View > Entity Display (names: targetname/model/class, facing arrows, model origin crosses; remembered), connections drawn, light group in the inspector; GUI section "entity display modes" | S |
| 2.18 | Rope / wire generator | iw3xo+ | done: Tools > Create Rope (between two selected) | S |
| 2.19 | Measure / ruler tool, coordinate readout, snap-to-grid of selection | stock | done: Tools > Measure Tool (Alt+M, drag in a 2D view: length and per-axis deltas, last ruler stays), coordinate readout in the status bar, Edit > Snap Selection to Grid (Ctrl+Shift+G); GUI section "measure tool and snap to grid" | S |
| 2.20 | Save selected, save region | stock | done (File > Save Selected / Save Region; `src/core/region.*`) | S |

## L3. Viewport and preview (iw3xo's renderer, for IW8 data)

Exit test: the sample map shows textured, normal-mapped and sun-shadowed in the camera view, with its `script_model` drawn
as the real model at the right size. Opening `mp_shipment` shows its full world geometry and entities.

| # | Item | Ref | Status | Size |
|---|---|---|---|---|
| 3.1 | Textured view: per-face UVs (2.1), albedo from ACTS DDS exports, texture cache, mipmaps | iw3xo+ | done: textured camera; images from custom materials, exported stock assets, and the ACTS material cache (fetched on demand, cached on C:) | M |
| 3.2 | PBR-approximate shader: albedo, normal, gloss/specular, alpha test/blend; missing-material checker | iw3xo+ | partial: missing-material checker; normal/gloss shading open | M |
| 3.3 | View modes: wireframe, flat, textured, lit, fullbright, normals; selection outline/tint settings | stock / iw3xo+ | flat only | S |
| 3.4 | Fake sun with shadows, sun direction visualizer, ambient | iw3xo+ | done (preview): Compile > Sun & Sky edits sundirection/suncolor/skyname; lit preview draws a sun arrow and per-face sun shadows (ray from each face centre through the play collision world; whole faces, no soft edges) | M |
| 3.5 | **XModel rendering**: ACTS xmodel export → mesh cache; `script_model` / static models / prefab models drawn and picked | iw3xo+ | done: stock xmodels draw as their LOD0 mesh (ACTS model cache) and custom OBJ models | L |
| 3.6 | Transform gizmo: translate/rotate/scale, world/local, grid snap | iw3xo+ | done: move (G), rotate (R) and scale (T) handles, world or local space (Shift+R: moves follow the selected entity's angles), grid snap, one undo step each; GUI sections "rotate gizmo", "shape models and scale gizmo", "local-space gizmo" | M |
| 3.7 | Camera context menu (stamp prefab, extrude), camera toolbar, speed and FOV UI | iw3xo+ | done: right-click without moving in the camera opens a menu (select under cursor, move selection here, add shape model here, copy position, drop to floor, make destructible/scripted, enter prefab, camera speed and FOV presets, gizmo/lit/textured toggles); speed and FOV also in Preferences | S |
| 3.8 | Sky preview (skybox cubemap from stock sky images) | iw3xo+ | done: the textured camera draws the worldspawn skyname (stock sky from the ACTS cache, converted with the build's cube layout) as an unfogged box around the map; sky tool faces are hidden so it shows through; probe captures include it | S |
| 3.9 | **Stock map reference geometry**: load a stock map's GfxWorld (ACTS) as read-only geometry next to its entities | iw3xo+ (load d3dbsp) | partial: View > Stock Map Reference draws a stock zone's world (ACTS gfx_map, cached; mp_shipment 418k tris) grey in the camera; props, UVs/textures, grid views open | L |
| 3.10 | Build preview: draw the compiled map's world back from the built zones; collision hull overlay | iw3xo+ | done: View > Show Compiled Collision (hulls: solid cyan, player clip orange, ladder green) and View > Show Compiled World (the build's render triangles, magenta; readCompiledSurfaces), both reload after every compile | M |
| 3.11 | Renderer scale: GPU buffers per item, culling, 10k+ brushes and million-triangle stock worlds at interactive rates | iw3xo+ | partial: camera geometry cached between frames (rebuilt only on scene/selection/shading changes) and GPU vertex buffers reused; per-item GPU buffers and culling open | M |
| 3.12 | **Play preview**: walk the map as the player (P): collision against brushes/patches with the build's contents rules, CoD-style movement (walk, sprint, crouch, prone, jump, step 18, slide, mantle), noclip | iw3xo+ (gameview) | done (editor-side mover with Replay 1.20 walk-profile values; mantle from brush ledges; not compared in game) | M |

## L4. IW8 assets: browsers and builds

Exit test: build a map that uses 20 stock materials and 10 stock props. Both look right in the editor and in game.
Import one custom texture and use it.

| # | Item | Ref | Status | Size |
|---|---|---|---|---|
| 4.1 | Material thumbnails for every stock material: batch ACTS export cache on C:, albedo picked by techset slot semantics, not by name | iw3xo+ | 371 of 46k | M |
| 4.2 | Material browser: favourites/lists, surface-type and usage filters, reload | iw3xo+ | search + apply | S |
| 4.3 | Model browser with a 3D preview (3.5) and drag-and-drop placement | iw3xo+ | done: Models browser with a shaded 3D preview (model cache mesh, triangles, size, zone; exports on demand) and Place / double-click placement; drag-and-drop open | M |
| 4.4 | Prefab browser with rendered thumbnails, drag-and-drop stamp | iw3xo+ | done: Prefabs dock (<map dir>/prefabs, map_source/prefabs, Preferences prefab folders) with top-down outline thumbnails, filter, double-click stamp (one undo step) | M |
| 4.5 | FX, sound-alias and image browsers | iw3xo+ | done (owner check pending): Effects dock (1020 common-zone vfx, places fx_origin; verified in the built level vfx table), Images dock (50,639 catalog images, exported with ACTS on demand and previewed) and Sounds dock (11,707 aliases of the always-loaded banks from x64-zt verifyzone soundbank traces, data/asset_catalog/iw8_common_sounds.tsv; loops shown blue; places sound_emitter); thumbnails in the Materials/Models/Images docks | M |
| 4.6 | **Textured builds**: stock images → extraction `materials/<name>.json` + `images/*.dds` (colour, normal, specular) | IW8 | done (colour): Compile > Stock textures turns cached stock colour images into build materials (iw8r_stock/<name>, max 1024); x64-zt + ACTS 0 failed; normal/gloss maps open | M |
| 4.7 | Stock xmodels on `script_model` in builds (name-only reference from `ww_common*`, research/18) | IW8 | stock models on script_model built as iw8r_stock_* copies (model key rewritten); name-only reference still needs an x64-zt change | M, **U** (x64-zt change) |
| 4.8 | Static models (`misc_model` → `world.models`, lighting and shadow flags) | stock | stock models built as textured copies from the ACTS model cache (iw8r_stock_*; LOD0 materials from materialHandles); custom models done (doc 29); lighting and shadow flags open | M |
| 4.9 | Stock materials by name in builds (full IW8 PBR instead of re-built textures) | IW8 | open | L, **U**, **R** |
| 4.10 | Custom textures: PNG/TGA → BC-compressed DDS, custom material editor (techset family, textures, surface type) | stock (converter) | done: File > Import Texture (PNG/TGA/JPG/BMP) -> `.iw8mat.json` library next to the map; builds as BGRA8 DDS with mips | M |
| 4.11 | Custom models: OBJ/GLB → xmodel extraction JSON (rigid, LODs, collision LOD) | stock (Maya export) | done (OBJ): File > Import Model places a script_model; the compiler writes xmodel JSON; x64-zt built it, ACTS 0 failed; glTF and in-game winding check open | L, **R** |

## L5. IW8 lighting and atmosphere (no IW3 equivalent for most)

Exit test: a lit interior with two spot lights, one using an IES profile, a reflection probe and graded fog. It looks the
same in the editor preview and in game.

| # | Item | Ref | Status | Size |
|---|---|---|---|---|
| 5.1 | Light entities (omni/spot): colour, intensity, radius, cone, shadows → comworld primary lights + `GfxLight`; light preview in the viewport | IW8 / iw3xo+ | done (build): light / light_spot / light_omni -> primary lights after the sun; x64-zt + ACTS pass; editor preview open | M |
| 5.2 | IES profiles per light (`ies_profiles`), profile browser and preview | IW8 | open | M |
| 5.3 | Sun: worldspawn keys, cascades, sun flare, `casts_sun0_shadow` model flags | IW8 | direction + colour | S |
| 5.4 | Sky selection and rotation | IW8 / iw3xo+ | done (build): Sun & Sky picks a stock sky (sky_mp_*); the editor exports it with ACTS and converts it to the cube x64-zt reads (doc 33); rotation and in-game look unverified | S |
| 5.5 | Reflection probes: oriented-box volumes, priority, feather; capture offline from the editor renderer or in game via L7 | IW8 / iw3xo+ | done offline (in-game check pending): authored volume, priority, feather (fork 58782e8f) and Compile > Capture Reflection Probes: the camera renderer captures every probe (six 64x64 faces in the converter cube layout, markers/outlines hidden) into <build>/probe_capture; the compile uses captures taken at the probe position ("3 of 3 reflection probe(s) use the editor capture") | L, **R** |
| 5.6 | Vision editor (fog, grading CLUT, exposure, tonemap; 218 fields) with viewport preview; fog splines | IW8 / iw3xo+ | partial: fog + film grade + exposure + grading LUT (.cube -> grading CLUT) + named vision overrides (worldspawn iw8r_vision "name=value; name=x y z", Atmosphere dialog field, x64-zt fork 15f63b8e applies them: "applied 2 vision override(s)") editable in Compile > Atmosphere; fog previewed; native fog object and fog splines open | L |
| 5.7 | Volumetrics volumes | IW8 | open | M |
| 5.8 | Decal volumes (box entities + decal volume materials) | IW8 | open | M |
| 5.9 | Layered materials: vertex-colour blend painting on brushes/patches | IW8 | colour tokens preserved | L |
| 5.10 | Visibility: portal/cell authoring, MDAO volumes; Umbra occlusion | IW8 | one cell | L, **R** |
| 5.11 | Baking: light grid and lightmaps (an editor-side GPU baker, or a documented dynamic-only mode) | stock (cod4rad) | none | XL, **R** |
| 5.12 | Glass (`GlassWorld`) and foliage/reactive motion flags | IW8 | open | M |

## L6. Gameplay and IW8 entity systems

Exit test: a map playable in TDM and Domination, with a working door, one destructible, ambient FX and sound, and correct
spawns. A stock map's spawns are moved and played.

| # | Item | Ref | Status | Size |
|---|---|---|---|---|
| 6.1 | **spawnList** write-back for stock maps (M2.2): editable spawn records, regenerated on save | IW8 | done offline (owner in-game check pending): File > Open IW8 Stock Map, File > Edit Compiled Spawns turns the 611 spawnList records into entities (one undo step), File > Rebuild Stock Zone (scratch) splits the document back (entity string + spawnList, src/core/stock_spawns.*) and runs tools/m2_rebuild_mapents.ps1 -Spawns: mp_shipment with one spawn moved +16 rebuilt, ACTS --test complete with 0 failed, spawnList read back 611/611 as edited, entity string unchanged. Installing the zone into the game stays manual | M |
| 6.2 | Game-mode objects: DOM flags, hardpoint zones, SD sites, CTF; prefab templates | IW8 | partial: Primitives > Game Modes inserts 8 templates (DOM flags, hardpoint zones, S&D sites + bomb, CTF, 4 spawn sets) as one undo step; classnames/keys verified in stock maps (doc 32); trigger brush sizes are placeholders, flag prop / chevrons / in-game behaviour UNKNOWN | M |
| 6.3 | Map script: GSC generation and an in-editor script tab (gsc-tool compile, errors) | stock | minimap-only script | M |
| 6.4 | Doors / movers (`script_brushmodel` + script), exploders | stock | brush models build | M |
| 6.5 | Scriptables, destructibles, dynamic entities, physics props | IW8 | partial: through the x64-zt compatibility layer every map script now loads (maps\mp\_load::main): Primitives > Scripted Objects > Make Destructible (script_model targetname destructable, breaks with sparks after 40 damage; fork 074be493 keeps them live, verified live in the built package) and Make Scripted (live script_model + script_origin, made solid at load, driven by the map GSC: worldspawn iw8r_script / user_main). Native IW8 ScriptableDef state machines and dynent physics props open | L, **R** |
| 6.6 | FX placement (createfx → `FxWorld`) | iw3xo+ | done (build): fx_origin (fx = stock vfx/ name, delay, angles) -> generated map script (loadfx + spawnfx/triggerfx or playfx loop), compiled by x64-zt with gsc-tool; x64-zt fork keeps stock vfx/ names (abf4bc6f); verified in the built zone (level vfx table); not seen in game | M |
| 6.7 | Ambient sound, reverb zones, audio PA speakers, client triggers, splines | IW8 | partial: ambient sound done offline (sound_emitter entity: soundalias, loop, delay; the map script spawns a script_origin and plays playloopsound / repeating playsound; the compiler writes extracted/sound/<map>.stock_sounds.txt and x64-zt fork fac03046 registers those stock aliases with the compat layer; translated script verified, in-game check pending). Reverb zones / client audio triggers open: they come from extracted/maps/clienttriggers.json (ReadClientTriggers) and need audio zones in the map sound bank | L |
| 6.8 | Ladders, mantles, water volumes | stock | partial: ladder tool material → collision `ladder_planes` (x64-zt keeps the widest vertical faces) and climbing in play preview; mantles from brush ledges in play preview (Replay mantles from collision at runtime); water = non-colliding drawn volume + water surface type only: x64-zt has no swim-volume route (UNKNOWN in Replay) | M |
| 6.9 | Bot navigation (navmesh / path nodes) | stock | partial: x64-zt generates the navmesh, nav graph, tactical graph and pathnodes for every build; editor node_pathnode and node_negotiation_begin/end now pass through (verified in the built zone: 2 placed markers, 31 pathnodes in the shipped entity string); navmesh preview in the editor open | L, **R** |
| 6.10 | Fast entity-only rebuild for stock maps (the M2 path in the Compile menu) | IW8 | script only | S |

## L7. Live link and iteration speed

Exit test: move a `script_model` in the editor and see it move in Replay within a second. F5 runs build, deploy, and load
the map.

| # | Item | Ref | Status | Size |
|---|---|---|---|---|
| 7.1 | Live-link server in mw120rproxy (TCP), editor client and status | iw3xo+ | done without the proxy: Live Link Direct attaches to the running exe (any build, .rplmap for custom ones), reads the player view, writes the player origin and queues console commands; status in the Live Link dock. The proxy TCP server is not needed | M, **U** (proxy repo) |
| 7.2 | Console / dvar send (`Cbuf_AddText`) and one-key load map | iw3xo+ | done offline: Live Link Direct sends any console command; Compile > Load Map in Game (Ctrl+F8) / the Live Link "Load map in game" button send live/loadMapCommand (default "map %1") for the compiled map id. In-game confirmation pending | S |
| 7.3 | Camera sync editor ↔ game (after the set-origin/angles semantics are verified) | iw3xo+ | partial: Direct link reads the player origin/angles (verified on the owner's local_dw exe) and moves the editor camera; editor→game sets the player origin (angles not written); untested in game | M, **R** |
| 7.4 | Hot update of script entities without a rebuild | iw3xo+ | done offline: every map script runs iw8r_hot_update (dvar iw8r_hot = "seq targetname x y z pitch yaw roll"; translated and compiled by gsc-tool); Live Link "Hot update" sends moved script_model / script_origin placements (GUI section "one-key load map"). In-game confirmation pending | M |
| 7.5 | F5 pipeline: incremental build cache → deploy → reload in game | iw3xo+ | done: Compile > Build, Deploy and Load in Game (F5): F7 + deploy + Live Link load command; Ctrl+F7 compile and deploy | M |
| 7.6 | In-game capture for probes and screenshots (feeds 5.5) | iw3xo+ | partial: probe capture from the editor renderer (5.5) instead of the game; in-game capture and screenshots open | M, **R** |

## L8. Advanced authoring

Exit test: paint 50 rocks on a terrain slope, preview a fire effect in the viewport, and edit the inside of a prefab in place.

| # | Item | Ref | Status | Size |
|---|---|---|---|---|
| 8.1 | FX preview playback (simplified ParticleSystemDef sim) | iw3xo+ | open | L |
| 8.2 | FX editor (modules, curves) with x64-zt vfx writing | iw3xo+ | open | XL |
| 8.3 | Mesh painter (models/prefabs, random scale/yaw, weights, surface-aligned) | iw3xo+ | done: Tools > Scatter Models (radius scatter) and Tools > Model Paint Mode (left-click surfaces in the camera: N static models within a radius, random yaw/scale, one undo step per click); GUI section "model paint" | M |
| 8.4 | Terrain sculpt (raise/lower/smooth/noise brushes on terrain patches) | iw3xo+ | done: Terrain Mode Raise / Smooth / Flatten / Noise and Radius Larger/Smaller (`smoothAround`, `flattenAround`, `noiseAround` in `patch_ops2`); core tests `terrain_sculpt_tests.cpp`; GUI section "terrain brush modes" | M |
| 8.5 | Prefab enter/leave editing, nested prefabs, instance overrides | stock / iw3xo+ | partial: Edit > Enter / Leave Prefab (nested), every instance updates, and prefabs are expanded into builds (core flattenPrefabs: brushes as func_group, entities kept, nested to 8 levels; stock models and textures inside prefabs included; core test compile_world_prefabs_expanded); per-instance overrides open | M |
| 8.6 | Drop to floor / physics settle for props | iw3xo+ | done: Tools > Drop Selection To Floor | S |
| 8.7 | Batch tools: model replace, find/replace keys, selection sets | stock | done: Edit > Find / Replace Key Values (Ctrl+Shift+F; model replace with key "model", any key), Edit > Selection Sets (save / restore), Find / Replace Materials (Ctrl+F); core key_replace.*, GUI section "batch tools" | S |

## L9. Product polish (ongoing)

Exit test: a new mapper installs the suite, points it at their Replay folder, and builds the tutorial map without help.

| # | Item | Ref | Status | Size |
|---|---|---|---|---|
| 9.1 | First-run setup: detect Replay, ACTS, x64-zt, Oodle; validate the exe MD5 | iw3xo+ | done: Help > First-Run Setup (also at startup when the Replay exe is missing): `src/core/setup_detect.*` (game folders under IW8\Builds, game exes incl. renamed recompiled ones, RPLYCMP2 / MZ classification, MD5, Oodle DLL), `src/qtapp/setup_wizard.*` (Detect, Browse, Save = Tool Paths keys); tests `tests/setup_detect_tests.cpp`, GUI section "first-run setup". The MD5 is computed (`md5File`) but not yet checked against the stock hash in the UI | S |
| 9.2 | Preferences window, themes and editable colours, toolbar with icons (editable) | iw3xo+ | done: Edit > Preferences; View > Theme (Dark, Light, Midnight high contrast; accent colour picker; src/qtapp/theme.*), main toolbar with icons (New, Open, Save, Undo/Redo, gizmo modes, textured/lit, Compile, F5, Play, Web Server) and View > Customize Toolbar (pick and reorder any command); GUI section "themes, toolbar, safe mode" | M |
| 9.3 | Toast notifications, a log/console window with filters, a hotkey cheat sheet, help | iw3xo+ | done (log/console + toasts): `EditorLog` (`src/qtapp/editor_log.*`, 5000 entries; status, compile, Qt warnings), Console dock (`log_panel.*`: level and text filters, Clear, Copy all, auto-scroll), toast overlay (`toast_host.*`, max 3, 4 s, View > Toast Notifications = ToggleToasts); GUI test section "console log"; cheat sheet (F1) and help already existed | S |
| 9.4 | Performance and memory at stock-map scale; large-map test corpus | iw3xo~ | done (measured): `--smoke-test` perf.txt has open/frame/edit/grid timings and working set. 10k brushes + 200 entities: open 231 ms, camera 7.3 ms steady (was 17.4), 9.4 ms after an edit, grid zoomed out 21 ms, 259 MB. mp_shipment stock world as reference (418k tris): camera 4.1 ms, 164 MB steady, 659 MB peak while parsing the ACTS JSON (open: stream the parse) | M |
| 9.5 | Crash handler, safe mode (reset layout/settings), autosave recovery | stock | done: crash handler (stack + minidump), autosave recovery, safe mode (--safe-mode or Shift at start: default settings in memory, nothing written; Help > Restart in Safe Mode), Help > Reset All Settings (keeps IW8Radiant.ini.bak) | S |
| 9.6 | Packaging: zip/installer with the Qt runtime, licences (Qt LGPL, ADS LGPL), no GPL data shipped | n/a | done (zip): `tools\package_release.ps1` → out\package (13 MB zip; editor, CLI, Qt runtime, licences, sample map; needs VC++ redist); installer open | S |
| 9.7 | Tutorial map, mapper documentation, sample prefabs | iw3xo+ | done: docs/TUTORIAL.md + samples/tutorial/iw8r_tutorial.map (tools/make_tutorial.py: arena with building, ramp, cover, props, prefabs, light, probe, sound, fx, TDM/FFA/DOM spawns and flags, matching sky and sun; compiles clean) and sample prefabs (crate_stack, lamp_post, cover_wall, spawn_pair); shipped in the package; docs/USER_GUIDE.md | M |
| 9.8 | CI: core + GUI tests on every change; optional full build test | n/a | local scripts | S |

## L10. Browser access (owner request 2026-11-10)

The editor hosts a local web server so the suite can also be used from a browser (same machine or the LAN), driving the
same document, compile and live-link back ends as the desktop window.

Exit test: with the editor running, a browser on another device opens the server address, shows the map from above,
selects and moves an entity, edits a key, starts a compile and watches its log, and the desktop window shows the same
changes (one undo history).

| # | Item | Ref | Status | Size |
|---|---|---|---|---|
| 10.1 | Embedded HTTP server in the editor (off by default; port and LAN access in Preferences; token for non-local clients) | new | done: src/qtapp/web_server.* (QTcpServer, or QSslServer with web/tls: HTTPS, TLS 1.2+, PEM cert/key in web_tls, tools/make_web_cert.ps1 self-signed), Tools > Web Server / Web Server Settings, starts with the editor when web/enabled; localhost-only unless LAN; remote requests need HTTP Basic auth (PBKDF2-SHA256 salted hash, 8-failure lockout) or the session token; UPnP port mapping (src/qtapp/upnp.*); owner confirmed access from outside | M |
| 10.2 | JSON API over the live document: entities, key values, brushes (bounds/faces), selection, undo/redo, save | new | done: /api/state, /api/map (items as 3D polygons + entities with keys), /api/log, POST select/deselect/key/move/undo/redo/save/compile; GUI section "web server API and auth" | M |
| 10.3 | Browser client: 2D top/side views (canvas), entity list and inspector, select, move, edit keys | new | done: data/web (index.html, app.js, style.css): canvas top/front/side views with grid, pan/zoom, click select (Shift adds), drag-move with grid snap, entity list with filter, key inspector (edit, add, Ctrl+Delete removes), Undo/Redo/Save/Compile, log; phone-width layout | L |
| 10.4 | Live updates to the browser (server-sent events or WebSocket): document revision, selection, status/log | new | done: /api/events server-sent events (revision, status, log); the page refreshes on every document change from either side | M |
| 10.5 | Browser 3D view (WebGL) with the textured scene (thumbnails / material colours) | new | partial: View > 3D in the browser (data/web/view3d.js, WebGL 1): flat-shaded items in their colours, outlines, selection, orbit/pan/zoom, click-pick (ray vs triangles); textures open | L |
| 10.6 | Compile, deploy, live-link and asset browsers from the browser (thumbnail packs over HTTP) | new | partial: server API for compile, deploy (/api/deploy), F5 (/api/build), load in game (/api/load), asset search (/api/assets, unbuildable left out), thumbnails (/api/thumb), place/apply (/api/place, /api/material); browser UI for them open (owner: desktop first) | M |

---

## Owner decisions and actions on the path (U)

- In-game tests (1.1, 1.2, then each layer's exit test): only the owner can run Replay and confirm.
- x64-zt converter changes in the owner's repo: stock xmodels by name (4.7), stock materials by name (4.9), and anything the
  in-game test of 1.1 shows is wrong in the converter. Each lands on a branch in a separate worktree, never pushed.
- mw120rproxy live-link server (7.1): a change in the owner's proxy repo.
- Writing to the Replay install: Deploy (done, owner-triggered) and Launch (1.4).

## Research items (R)

The lighting baseline without bakes (1.8), probe capture (5.5, 7.6), Umbra and visibility (5.10), the light grid and lightmap
baker (5.11), scriptables (6.5), bot navigation (6.9), custom model export (4.11), and camera-sync semantics (7.3). Each gets
a `docs/research/NN_*.md` report before code.
