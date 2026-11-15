# IW8 Radiant Suite: feature matrix

The layered plan for the remaining work is [ROADMAP.md](ROADMAP.md).

State on 2026-11-14: the rows below were brought up to date from [ROADMAP.md](ROADMAP.md), [HANDOVER.md](HANDOVER.md),
[TEST_CHECKLIST.md](TEST_CHECKLIST.md) and the change log. "In game" results are the owner's playtests of 2026-11-11;
nothing built after them has been seen in the game ([HANDOVER.md](HANDOVER.md) section 4).

Parity is defined from the mapper's side. For every workflow a CoD4 mapper has in iw3xo-radiant, and every workflow IW8 data
needs that IW3 never had, this lists the reference (what iw3xo added versus what it inherits from the CoD4 Mod Tools Radiant),
the IW8 implementation path, its dependencies, a test a mapper can run, and the status.

**Ref** (from [research/01](research/01_iw3xo_feature_audit.md) and [research/07](research/07_editor_feel_reference.md)):
`stock` = inherited from CoD4Radiant.exe (iw3xo only re-skins it), `iw3xo+` = added by iw3xo, `iw3xo~` = stock but enhanced
by iw3xo, `IW8` = needed only because of IW8 data; there is no IW3 equivalent.

**Status**: `M1 ✔` = implemented and tested in this repository (see [MILESTONES.md](MILESTONES.md)); `Mn` = planned milestone;
`research` = blocked on an unknown listed in [ARCHITECTURE.md](ARCHITECTURE.md) §8; `done ✔` = built and covered by
tests; `offline` = checked without the game (built zone, ACTS, tests); `partial: …` names what is missing;
`in game: not checked` = no look in the game yet. Milestones: M2 IW8 MapEnts write-back to a
fastfile; M3 brush and patch tools; M4 IW8 asset browsers and textured preview; M5 native IW8 compile, package and deploy;
M6 IW8 lighting and render tooling; M7 live link; M8 FX, scriptables, prefabs, painting.

## A. Everyday feel: camera, selection, grid, hotkeys, undo

| Feature | Ref | IW8 implementation path | Dependencies | Mapper test | Status |
|---|---|---|---|---|---|
| Free-fly camera: arrows fwd/back/turn, `,` `.` strafe, D/C up/down, A/Z pitch | stock | `EditorContext::tickCamera`, 16 ms timer, Shift ×3 | none | Hold ↑ in the camera view: it flies forward; release, it stops | M1 ✔ |
| Mouse look (RMB), Ctrl+RMB forward + yaw, wheel move | iw3xo~ | `CameraView` with a locked cursor; WASD/QE fly while RMB is held (modern addition) | none | Hold RMB and drag: view rotates; add W: flies along the view | M1 ✔ |
| Camera speed / FOV settings, persisted | iw3xo~ | *View* sliders, `IW8Radiant.ini` | none | Set speed 2000, restart: speed kept | M1 ✔ |
| Center camera on selection (End) | iw3xo+ | `frameSelection` (camera + 2D centre) | none | Select a far brush, press End: it fills the view | M1 ✔ |
| 2D grid views XY/XZ/YZ, Ctrl+Tab cycles | stock | `GridView` (QPainter), edge coordinates, major/minor lines | none | Ctrl+Tab ×3 returns to XY | M1 ✔ |
| Zoom to cursor, pan (RMB/MMB) | iw3xo+ | `GridView::wheelEvent` keeps the world point under the cursor | none | Wheel over a corner: the corner stays under the cursor | M1 ✔ |
| Grid sizes 0.5–512 (`~`, 1–9; 128 via menu), `[` `]` | stock | `Document::setGrid/gridUp/gridDown` | none | Press 5: status bar shows Grid 16 | M1 ✔ |
| Snap to grid on move, toggle (Ctrl+Alt+G) | stock | Snapped drag delta, snapped nudge | none | Drag a brush 30 units at grid 16: it moves exactly 32 | M1 ✔ (GUI test) |
| Shift+click select / toggle (2D, 3D); click select in 3D | stock | Ray vs brush planes, patch quads, entity boxes | none | Shift+click a brush in 2D: it turns red; again: deselected | M1 ✔ (GUI test) |
| Brush entity selects as a unit | stock | `Document::selectionUnit` | none | Click one brush of a trigger: all its brushes select | M1 ✔ |
| Deselect (Esc), invert (I), delete (Backspace), clone (Space) | stock | `Document` ops, one undo step each | none | Space clones by one grid step and selects the copy | M1 ✔ |
| Nudge selection (Alt+arrows) | stock | `translateSelection` by the grid in view axes | none | Alt+→ at grid 8 moves the brush +8 X | M1 ✔ |
| Select cycle (Alt+Shift click), touching/inside/complete-tall, by class/key | stock | Ray hit list; AABB containment queries | none | Alt+Shift click cycles through stacked brushes | done ✔ (GUI test): Alt+Shift click cycle, complete / partial tall, touching, inside, select connected (Ctrl+E) |
| Hide / show selected, filters (by class, contents, material) | stock / iw3xo~ | Visibility flags in `SceneCache`, filter dock | none | Hide all triggers: none drawn, none pickable | partial: H / Shift+H, Hide Unselected (Ctrl+H), View > Filter by class prefix, Layers dock, View > Game Mode; filters by contents and by material not built |
| Undo/redo, exact, unlimited-ish (1,024) | stock / iw3xo~ (limit 512) | Snapshot transactions; byte-exact restore; drag = one step | none | Edit, Ctrl+Z, save: file identical to the original | M1 ✔ (byte-exact) |
| Undo history panel, jump to any step | iw3xo+ (debug only) | `HistoryPanel` | none | Click the first entry: every later edit undone | M1 ✔ |
| Rebindable hotkeys, stock defaults, saved | iw3xo+ | `QAction` + `QKeySequenceEdit`, `IW8Radiant.ini` | none | Rebind Clone to C, restart: C clones | M1 ✔ |
| Dockable, tabbed, floating panes with saved layout | iw3xo+ | Qt Advanced Docking System | Qt 6.8, Qt-ADS 4.4.1 | Float the Grid pane onto a second monitor | M1 ✔ (layout saved and versioned; View > Reset Panel Layout) |
| Recent files, drag-and-drop open, unsaved-changes prompt | stock | `Settings`, `dropEvent`, `confirmDiscard` | none | Drop a `.map` on the window: it opens | M1 ✔ |
| Transform gizmo (move/rotate/scale, world/local, snapping) | iw3xo+ | Viewport gizmo over the `Document` transform ops | rotation/scale ops (M3) | Drag the gizmo's Z arrow: brush rises in grid steps | done ✔ (GUI test): move (G), rotate (R), scale (T), world or local space, grid snap, one undo step |

## B. Brush editing

| Feature | Ref | IW8 implementation path | Dependencies | Mapper test | Status |
|---|---|---|---|---|---|
| Lossless brush storage (3-point planes, all face fields) | stock | `map_text` regenerates only the plane points | none | Move then undo a brush: file byte-identical | M1 ✔ |
| Move brushes on grid | stock | Translate plane points; texture fields kept (world-anchored, texture lock off) | none | Drag the pillar +32 X: only its 6 face lines change | M1 ✔ |
| Create brush by drag in 2D | stock | New 6-plane box, current IW8 material | material browser (M4) for the default | Drag a rectangle in XY: a 64-tall box appears | M3 ✔ (GUI test) |
| Resize by face drag, vertex/edge drag | stock | Plane edits with windings recomputed; reject non-convex | none | Drag a face outward 16: brush grows 16 | done ✔ (GUI test): face drag, vertex drag (V), edge drag (E) |
| Clipper (2/3 point), split | stock | Plane insertion; split = two brushes | none | Clip a box diagonally: two convex halves | done ✔ (GUI test): 2-point and 3-point clipper, Clip, Split, Flip |
| CSG hollow / subtract / merge | stock | Convex decomposition on plane sets | none | Hollow a 256 cube at grid 8: six 8-thick walls | done ✔ (GUI test): hollow, subtract, Brush > CSG Merge, Brush > Auto Caulk |
| Rotate / flip / scale (free and arbitrary) | stock | Point transforms with texture-lock option | texture lock | Rotate 90°: footprint swaps axes | done ✔: rotate 90 / flip, Brush > Arbitrary Rotation, Brush > Scale; texture lock aware |
| Texture lock (moves, rotations) | stock | Adjust per-face offsets with the move | IW8 UV projection definition (M5) | Move with lock on: texture stays on the brush | done ✔: move, drag, nudge, rotate, flip (Shift+T) |
| Surface inspector (shift, scale, rotate, fit) | iw3xo~ | Per-face field editor over the preserved suffix tokens | IW8 UV meaning (M5) | Fit a face: texture spans it once | done ✔ (GUI test): shift / scale / rotate fields, Fit, Natural, Justify, lightmap scale |
| Primitives (cylinder, cone, sphere, n-sided) | stock | Plane generators | none | Make an 8-sided cylinder | done ✔: n-sided prism; Primitives menu: cone, sphere, stairs, arch, ramp |
| Extrude brush to traced face | iw3xo+ | Ray to a face, extend planes | gizmo | Extrude a floor to a wall | done ✔: Brush > Extrude Selected Face |
| Detail / structural / clip / non-colliding flags | stock | Preserve `contents …;` and IW8 content mapping in the compiler | M5 contents table | Mark a brush player-clip: it blocks players, renders nothing | done ✔ (edit: Make Detail, Make Structural, Toggle Non-Colliding; build: contents table); in game: clip, player clip and weapon clip worked for the owner (2026-11-11); detail and non-colliding flags not checked |

## C. Patch editing

| Feature | Ref | IW8 implementation path | Dependencies | Mapper test | Status |
|---|---|---|---|---|---|
| Lossless `curve` / `mesh` patches incl. colour and UV tokens | stock | Rewrite only `v x y z` tokens | none | Move a patch 4 up: colour `c …` and `t …` tokens unchanged | M1 ✔ |
| Patch control net display and picking | stock | Quads from row/column control points | none | Shift+click a patch in 3D: it selects | M1 ✔ (net, not tessellated) |
| Curve tessellation preview | stock | Biquadratic evaluation per 3×3 sub-patch | none | A 3×3 curve renders as an arch | M3 ✔ |
| Vertex edit, insert/delete rows and columns, cap, thicken, weld | stock / iw3xo~ | Patch matrix ops on the vertex grid | none | Insert a column: width grows by 2 | done ✔ (core + GUI tests): create mesh / cylinder / bevel / endcap, invert, transpose, natural, insert / delete row and column, thicken, vertex drag, cap, weld, redisperse, split, lock vertices, Patch Inspector |
| Terrain patch, noise generator, thicken | iw3xo+ | Terrain grid ops | none | Apply noise 8: vertices jitter within ±8 | done ✔: Create Terrain, Terrain Noise / Smooth, Terrain Brush and sculpt modes (core terrain_sculpt tests) |
| IW8 surface for patches | IW8 | Compiler tessellates to IW8 world surfaces (M5); IW8 `StTerrain` is a separate system | M5 | A curved arch appears in game | done ✔ offline (tessellated into world surfaces, built with back faces); in game: patches seen from both sides by the owner (2026-11-11, second playtest) |

## D. Entities

| Feature | Ref | IW8 implementation path | Dependencies | Mapper test | Status |
|---|---|---|---|---|---|
| Key/value inspector: edit, add, remove; undoable | stock / iw3xo~ | `EntityInspector` table → `Document::setKeyValue/removeKey` | none | Change spawn angles to `0 90 0`, Ctrl+Z restores `0 45 0` | M1 ✔ (GUI test) |
| IW8 runtime key spelling preserved (`212 "worldspawn"`) | IW8 | `KeyForm::Canonical` lines keep the numeric key; new keys use ids when the entity does | key table | Edit an origin in a stock entity string: line stays `709 "…"` | M1 ✔ |
| IW8 canonical key names (56,738 ids) | IW8 | `tools/gen_iw8_keytable.py` → `KeyTable` | local gsc-tool | Inspector shows `classname`, not 212 | M1 ✔ |
| Point entity boxes, facing arrows, class colours | stock | `buildPointEntity` (player-sized spawns) | entity defs (M2) | Spawn boxes are 32×32×72 with a yaw arrow | M1 ✔ |
| Move point entities on grid | stock | `origin` rewritten in its existing spelling | none | Drag a spawn 16: only its origin line changes | M1 ✔ |
| IW8 entity class list with keys and examples | IW8 | `data/iw8_entity_classes.json` from stock MapEnts (35 classes, mp_shipment + mp_backlot2) | ACTS dumps | Class dropdown offers `script_struct`, `trigger_multiple`, … | done ✔: 113 class definitions (data/iw8_entity_defs.json); class dropdown, key help, key suggestions, Check Map validation |
| IW8 compiled spawns (`spawnList`) shown | IW8 | ACTS `spawnList` decode → overlay + IW8 Spawns panel | ACTS | Open stock mp_hideout: 501 green spawn boxes | M1 ✔ (read-only) |
| IW8 spawns editable and written back | IW8 | Spawn entities ↔ `SpawnPointEntityRecord` list (40 bytes) | M2 MapEnts writer | Move a stock spawn, build, spawn there in game | done ✔ offline (File > Stock Map > Edit Compiled Spawns, File > Stock Map > Rebuild Stock Zone; the spawn list reads back as edited); in game: not checked |
| Triggers (`trigger.models`, hulls, slabs) regenerated from brushes | IW8 | Compiler emits brush model `*N` (collision submodel with trigger contents); x64-zt bakes the native `TriggerModel/Hull/Slab` | M5 | A `trigger_multiple` brush fires in game | done ✔ offline (brush models; the map script tests players inside each trigger shape); in game: jump pad and teleporter triggers worked for the owner (2026-11-11, second playtest); hurt and damage triggers did no damage then, fixed offline, not checked since |
| Splines, client triggers, audio PA speakers / prop nodes | IW8 | Dedicated editors for `splineList`, `clientTrigger`, `audioPA*` | M2 decode | Edit a client audio trigger volume | partial: reverb zones and client audio triggers built offline (in game: not checked); PA speakers not built (stock MP maps have none); spline editor not built |
| Entity show modes, connections (target/targetname lines) | stock | Line overlay between targets | none | Select a trigger: a line to its target | done ✔ (GUI test): connection lines, View > Entity Display (names, facing arrows, model origins) |

## E. Materials

| Feature | Ref | IW8 implementation path | Dependencies | Mapper test | Status |
|---|---|---|---|---|---|
| IW8 material names on faces, preserved | IW8 | Face suffix verbatim; sample uses Replay names (`tm2co/…`) | none | Inspector lists `tm2co/me_plaster_barrier_fence_wall` | M1 ✔ |
| Material browser (search, favourites, thumbnails) | iw3xo+ | Stock list from ACTS / x64-zt material dumps; thumbnails from image dumps (BC decode) | ACTS image export, BC1–7 decoder | Search "plaster": thumbnails; apply to a face | done ✔: 46,164 stock names, search, apply, In this map, favourites, recent, tags; packed thumbnails for every material with a colour image (29,650) |
| Textured, lit viewport (PBR approximation) | iw3xo+ (custom shader) | IW8 material constants and layers → D3D11 preview shader (albedo, normal, gloss) | material + image readers; techset slot names | A metal face shows a specular highlight | done ✔ (editor): albedo, normal and gloss, light preview, View > Game Lighting; normal and gloss checked by the owner in the editor and in game (2026-11-11) |
| Layered materials (vertex-colour blend) | IW8 | Show and edit layer blend channels; per-vertex colour on patches/brushes | IW8 layer semantics ([research/09](research/09_iw8_render_features.md) §1) | Paint layer 2 on a wall: blend moves in game | partial: Patch > Vertex Paint Mode and Blend By Slope write vertex colour and alpha into builds; vertex paint marked OK in game by the owner (2026-11-11); vertex colour on brushes not built |
| Missing-material placeholder | iw3xo+ | Checker shader plus a list of unresolved names | browser | A typo'd material shows the checker and a warning | done ✔: missing-material checker in the camera; Check Map and the build preflight name unresolved materials |
| Surface types (footsteps, impacts) | stock | IW8 surface type table → collision tags | M5 | Footsteps on wood sound wooden | done ✔ offline (surface type per material, collision tags, converter physics materials); in game: footsteps not checked; on 2026-11-11 every impact looked like stone, the converter fix since then is not checked |

## F. Prefabs and placement

| Feature | Ref | IW8 implementation path | Dependencies | Mapper test | Status |
|---|---|---|---|---|---|
| `misc_prefab` references kept | stock | Preserved as entities | none | Prefab references survive a round trip | M1 ✔ (lossless) |
| Enter/leave prefab, stamp prefab, prefab browser with thumbnails | iw3xo+ | Multi-document editing; prefab cache | thumbnail renderer | Stamp a window prefab 5× | done ✔ (GUI test): insert, render, explode, save as prefab, Edit > Enter / Leave Prefab, nested prefabs, overrides and variants, Prefabs dock with pictures and drag-and-drop |
| Model placement (`script_model`, static models) from a model browser | iw3xo+ | ACTS xmodel GLB export → preview mesh; drag into the scene | ACTS `--geometry` | Drag a crate in: correct size and orientation | done ✔: Models browser with a 3D preview, Place, drag-and-drop; stock and custom models draw as meshes; static stock models seen in game by the owner (2026-11-11) |
| Mesh painter (scatter models) | iw3xo+ | Surface-aligned scatter with random scale/yaw | model browser | Paint 50 rocks on a slope | done ✔ (GUI test): Tools > Scatter Models, Tools > Model Paint Mode, Tools > Scatter rules |

## G. Asset browsers (stock IW8 data)

| Feature | Ref | IW8 implementation path | Dependencies | Mapper test | Status |
|---|---|---|---|---|---|
| Open stock map entities through ACTS | IW8 | *File → Open IW8 Stock Map*: `acts fastfile -r mw19replay -a map_ents`, cached, opened lossless | ACTS, Replay exe, Oodle | Pick `mp_shipment.ff`: 611 entities and 611 spawns load | M1 ✔ (GUI test) |
| ACTS Replay layouts proven against the Replay exe for every pool | IW8 | Port x64-zt's loader-verified layouts into ACTS; regenerate the schema; stock-zone suite | ACTS fork | Every pool reports native layout evidence; stock zones still load with 0 failures | in progress |
| Model, image, material, FX, sound browsers | iw3xo+ | ACTS exports (GLB, DDS/raw, JSON, SAB) indexed per zone | ACTS | Browse `common_mp` models with previews | done ✔: Materials, Models, Images, Effects, Sounds and Prefabs docks with drag-and-drop; owner used the model browser and dragged models, effects and sounds (2026-11-11) |

## H. Lighting and rendering (IW8-specific tooling)

IW8 has no IW3 equivalent for most of these ([research/09](research/09_iw8_render_features.md)). Lights are `ComPrimaryLight`/`GfxLight`
records, probes are oriented boxes, the light grid is tetrahedral SH, and fog and grading come from vision sets.

| Feature | Ref | IW8 implementation path | Dependencies | Mapper test | Status |
|---|---|---|---|---|---|
| Primary lights: omni/spot, colour, intensity, radius, cone, shadows | IW8 (iw3xo has light preview) | Light entities → `ComPrimaryLight` + `GfxLight` via the extraction `replay-comworld.json` | M5 compiler | Spot at 2 m aimed down, intensity 3: lit cone in game | done ✔ offline (light entities to primary lights, full light key set, light preview); in game: white, red, blue omni and spot lights seen by the owner (2026-11-11); a changed wide spot did not show (ROADMAP F.9, partial) |
| IES profiles per light | IW8 | `ies_profiles` JSON (128 texels per profile) | x64-zt lighting adapter | Assign profile X: its pattern on the floor | done ✔ offline (light key iw8r_ies, Tools > IES Profile Browser; GUI test); in game: not checked |
| Sun: direction, colour, cascades, `casts_sun0_shadow` model flags | IW8 / iw3xo~ (sun preview) | Worldspawn sun keys → sun primary light; viewport sun preview | M5 | Rotate the sun 30°: pillar shadow moves | done ✔ (Sun & Sky, Sun From Sky, sun sprite and flare, lit preview with sun shadows); in game: shadow direction and far shadows seen by the owner (2026-11-11) |
| Reflection probes (oriented boxes, priority, feather) | IW8 / iw3xo+ (cubemap probes) | Probe volume entities → `GfxReflectionProbeInstance`; capture via in-game render (M7) or offline | probe capture: research | Two overlapping probes with priority 1/2 blend correctly | done ✔ offline (volumes, priority, feather; captured by the editor before every build); in game: no visible change on 2026-11-11, open (ROADMAP F.10) |
| GPU light grid (tetrahedral SH) and static-model lighting volumes | IW8 | Grid volume entities; bake = research | baker: research | A prop darkens when moved into a shadowed room | done ✔ offline (light grid baked from the map; ground lighting on static models); in game: the earlier flat grid lit the gun and models (owner, 2026-11-11); the baked grid not checked |
| Baked lightmaps (typed planes) | IW8 / stock (cod4rad) | Unbaked mode first; baker = research | baker: research | Interior gets indirect light | done ✔ offline (Compile > Bake lightmaps, opt-in; Compile > Bake Settings); in game: not checked |
| Fog, fog splines, volumetrics, colour grading, exposure | IW8 / iw3xo+ (filmtweaks/postfx) | Vision-set editor (218 fields) and volumetric volume entities; viewport preview | vision table ([research/09](research/09_iw8_render_features.md) §11) | exposureAdjust +1 doubles interior brightness | partial: fog, film grade, grading LUT, exposure, Vision Editor (218 fields), vision volumes and volumetric boxes built offline; sky, fog and film grade seen in game by the owner (2026-11-11); volumetrics and vision volumes in game: not checked; fog splines not built |
| Decal volumes | IW8 | Box entities → `GfxStaticDecalVolumePacked` + decal volume materials | material browser | A grime box shows the decal only inside | done ✔ offline (iw8r_decal, Brush > Decal Mode; the converter writes the decal volume); in game: not checked |
| Visibility: DPVS cells, Umbra occluders, MDAO | IW8 | One conservative cell first (x64-zt behaviour), then occluder authoring | Umbra tome baking: research | A room behind a wall is culled | partial: Compile > Occlusion bake (opt-in; cells and portals from box occluders) done ✔ offline, in game: not checked; non-box occluders and MDAO volumes not built |
| Sky selection and rotation | IW8 / iw3xo+ (sky in browser) | Worldspawn sky keys → sky surfaces | M5 | Rotate sky 90°: sun lines up with the flare | done ✔ (stock sky, Sky From Panorama, sky rotation as the game draws it); stock sky seen in game by the owner (2026-11-11) |
| Glass | IW8 / stock | Glass material brushes → `GlassWorld` | M5 | Shoot a pane: it fractures | done ✔ (Primitives > Scripted Objects > Make Glass Pane, built as native glass); in game: shatters, and is see-through after the fix (owner, 2026-11-11) |
| Foliage / reactive motion / wind | IW8 | Model flags → `ReactiveMotionModelInfo` | model browser | A bush bends when walked through | partial: the converter sets the reactive flag from stock models' own parts; View > Wind Preview is editor-only; in game: not checked |

## I. FX

| Feature | Ref | IW8 implementation path | Dependencies | Mapper test | Status |
|---|---|---|---|---|---|
| Effect placement (createfx / `FxWorld`) | iw3xo+ | FX origin entities → `FxWorld` via the extraction | FX browser | A looping fire plays in game | done ✔ (fx_origin to the map script, stock vfx names); stock effects seen in game by the owner (2026-11-11) |
| Effect browser and playback in viewport | iw3xo+ | ACTS vfx JSON → simplified particle preview | ACTS vfx export | Preview a smoke column in the editor | done ✔: Effects dock, Effect Info particle preview, View > Effect Preview in Camera |
| Effects editor | iw3xo+ | IW8 `ParticleSystemDef` module editor | x64-zt vfx writer | Change a colour curve, rebuild, see it | done ✔ offline (Tools > Effect Editor, the map's own sprite effects); in game: the authored campfire was not visible on 2026-11-11, fixed offline, not checked since |

## J. Collision and physics

| Feature | Ref | IW8 implementation path | Dependencies | Mapper test | Status |
|---|---|---|---|---|---|
| World collision from brushes (Havok) | stock (cod4map) | Brushes → extraction `replay-collision.json` → x64-zt native Havok bake | M5 | Walk into a wall: blocked | done ✔; walls, static models and clips block in game (owner, 2026-11-11) |
| Player/weapon clip, ladders, surface types in collision | stock | Contents/material flags → collision tags | M5 | Player clip blocks players, not bullets | done ✔; clip, player clip, weapon clip and the ladder worked in game (owner, 2026-11-11); impact surface types: see Surface types |
| Collision preview in editor | iw3xo+ (PhysX) | Show the compiled collision hull overlay from a build | M5 | Toggle collision: the overlay matches brushes | done ✔: View > Show Compiled Collision and Show Compiled World, reloaded after every build |
| Physics props (prefab physics), drop to floor | iw3xo+ | `PhysicsAsset` refs on models; editor drop uses ray casts | M4 | Drop a crate: it lands on the floor | done ✔: dynamic_prop physics props, Tools > Drop Selection To Floor; a dynamic traffic cone moved in game (owner, 2026-11-11) |

## K. Compilation

| Feature | Ref | IW8 implementation path | Dependencies | Mapper test | Status |
|---|---|---|---|---|---|
| Build map from the editor (one button, log, errors) | iw3xo+ (wraps cod4map/cod4rad) | Native IW8 compiler: brushes → convex polygons → IW8 world surfaces + collision brushes + entities → x64-zt extraction contract (`replay-world/collision/comworld.json`, `complete`) → `convert_extraction` | x64-zt fork: relax the `.ff`-path check ([ARCHITECTURE.md](ARCHITECTURE.md) §8) | Build the sample room: 5 zones + `map.json`, ACTS validation passes | M5 ✔ offline: F7 = native extraction + x64-zt buildextraction + ACTS --test (GUI test); in game: editor-built test maps loaded and were played by the owner (2026-11-11) |
| Leak / sealing check, point file | stock | Flood fill from entities over the brush BSP | M5 | Remove a wall: leak line drawn to the void | done ✔: Tools > Leak Check draws the pointfile; the build warns |
| Incremental entity-only rebuild | IW8 | Rewrite only the `map_ents` asset (M2 path) | M2 | Change a spawn: rebuild takes seconds | done ✔ offline (File > Stock Map > Rebuild Stock Map With These Entities; Rebuild Stock Zone in seconds; unchanged zones reused); in game: not checked |
| No CoD4 tools in the pipeline | IW8 | Compiler is IW8-native; IW3 converters are not used | n/a | Build works with no CoD4 install | M5 ✔ partial: no CoD4 install or data (all-null impact table); the backend is x64-zt's converter fed with an editor-written extraction |

## L. Packaging and deploy

| Feature | Ref | IW8 implementation path | Dependencies | Mapper test | Status |
|---|---|---|---|---|---|
| Write the five Replay zones + `map.json` | iw3xo+ (linker wrapper) | x64-zt IW8 writer | M5 | Output folder holds exactly 5 `.ff` + `map.json` | M5 ✔ (sample room, 5 zones + map.json) |
| Validate before deploy | IW8 | ACTS load of the built zones (`--test`) + x64-zt `verifyzone` | ACTS, x64-zt | A corrupted zone is rejected before install | M5 ✔ ACTS --test per zone (0 failed on the sample) + x64-zt structural check |
| Install into the Replay custom-map folder with rollback | IW8 | Same layout the proxy loads (`mods\mw120r\maps`), hashes, backup | mw120rproxy | Deploy, then Undo deploy restores the previous package | M5 ✔ Compile > Deploy / Undo Last Deploy (staged, SHA-256, backup); the owner deployed the 2026-11-11 test maps with it; Undo Last Deploy tested on a scratch folder only |
| Launch to the map (F6 local play or console) | iw3xo+ (devmap after fastfile) | Proxy console injection (`Cbuf_AddText` RVA 0x15C1C20) + `ui_mapname` | mw120rproxy | One click: the game starts on the map | done ✔ offline (Compile > Launch Replay, Load Map in Game Ctrl+F8, F5 build, deploy and load); in game: not checked |

## M. Live link

| Feature | Ref | IW8 implementation path | Dependencies | Mapper test | Status |
|---|---|---|---|---|---|
| Editor ↔ game connection | iw3xo+ (TCP 3700) | Live-link server inside mw120rproxy (already injected into Replay 1.20) | proxy build | Status shows "connected to Replay 1.20.4.7623265" | done ✔ (Live Link Direct, no proxy needed); used by the owner with the running game (2026-11-11) |
| Camera sync (editor → game, game → editor) | iw3xo+ | Proxy calls the set-origin/angles routines *after* their semantics are verified (name conflict in [research/05](research/05_runtime_evidence_1_20.md) §12) | verification | Fly in the editor: the game camera follows | partial: game-to-editor camera and editor-to-game player origin used by the owner (2026-11-11); view angles are not written to the game |
| Entity hot update without rebuild | iw3xo+ (brush sync) | Script-side entity reposition for script entities; full rebuild for world geometry | M2 | Move a `script_model`: it moves in game within 1 s | done ✔ offline (Live Link hot update; hot channels for effects and sounds); hot channels seen in game by the owner (2026-11-11); script_model hot update in game: not checked |
| Send console commands / dvars | iw3xo+ | `Cbuf_AddText` | proxy | Send `r_fullbright 1` | done ✔ offline (Live Link Direct sends any console command); in game: not checked |

## N. Files, safety and robustness

| Feature | Ref | IW8 implementation path | Dependencies | Mapper test | Status |
|---|---|---|---|---|---|
| Byte-exact round trip of untouched content | IW8 requirement | Partitioned model + load self-check | none | Open and save 2,182 Radiant files plus stock IW8 strings: identical | M1 ✔ |
| Atomic save with `.bak` | stock (`.bak`) | Temp file + `MoveFileEx`, previous file to `.bak` | none | Kill the editor mid-save: original intact | M1 ✔ |
| Save a copy without switching documents | stock | `Document::saveCopy` | none | Save a Copy, keep editing the original | M1 ✔ |
| Parse errors with line numbers, no silent repair | iw3xo~ | `ParseError` (1-based line) | none | Remove a `}`: error names the line | M1 ✔ |
| Autosave / crash recovery | stock (snapshots) | Periodic snapshot of the text to `snapshots/` | none | Kill the process: reopen offers recovery | M3 ✔ 5-minute autosave to app data, File > Recover Autosave |
| Scripted CLI for batch edits and CI | IW8 | `iw8r info/roundtrip/diff/edit/iw8-ents` | none | `iw8r diff` reports exactly the edited items | M1 ✔ |
