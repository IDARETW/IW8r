# Changes

Newest first. "Not seen in game" means built and checked offline (converter read-back, the game's own loaders through
ACTS, tests) but not yet played; [docs/TEST_CHECKLIST.md](docs/TEST_CHECKLIST.md) lists the in-game checks.

## 2026-11-13

- **Brush faces nobody can see are not drawn and not built.** A face that lies inside another brush, or flat against
  one, is left out when that brush is opaque: in the viewport and in the built map. A brush with a cut-out or
  see-through material (glass, fences, foliage cards) hides nothing, and neither does a material the editor does not
  know yet. Cinder Rail (Toon) builds with 109,224 world triangles instead of 147,754, and the toon shader's lines no
  longer show where such a face met a wall. Checked: the built map passes the game's loaders; the viewport picture
  with and without the rule (bench switch `IW8R_NO_HIDE_FACES`) differs in at most 0.16 % of the pixels, all thin
  lines at such joints. Not seen in game.

- **Fixed: an installed stock edit stopped the game ("Disc read error [3.1]: '<map>.fp'").** The game reads a patch
  file of one of its signed zones only when the patch carries the game's own signature, which nobody else can make;
  every stock edit was installed as such a patch. File > Install Edited Stock Map Into Game now installs the edited
  zones as whole files, which the game loads unsigned: the stock zone file (and its stock patch) is moved to
  `iw8r_stock` in the game folder and File > Restore Stock Map In Game moves it back. An old patch file of this
  editor in the zone folder is removed by the install. With no rebuild in the session the install can take the folder
  of an earlier rebuild. While an edit is installed, the editor and the rebuild read the stock map from the kept file.
  Found in the game's code, checked offline; not seen in game (research 116).

- **Custom shaders on stock maps.** A shader file can now hold a second function, `iw8rFilter`, for surfaces that keep
  the game's own lighting. Set a shader for an opened stock map (Tools > Shader Editor > Use for This Map) and the
  viewport shows its world and props through the filter; File > Rebuild Stock Zone builds the filter into the map's
  own lit shaders and writes the patch files as usual. The toon shader has such a filter: vivid colour and the light
  in steps. Limits: no ink lines on stock maps (their geometry has no edge data), and characters, weapons and effects
  keep their look (their shaders are shared by every map). Checked offline: the edited Shipment zone loads in the
  game's own loaders and Direct3D accepts all 217 changed shaders. Not seen in game (research 115).

- **No more z-fighting where brushes overlap.** Two brush faces lying in the same plane on top of each other used to
  be drawn both, flickering against each other in the viewport and in game (Cinder Rail had 2,636 such pairs). Now
  only one is drawn where they overlap: the smaller face, so a panel sits on its wall. The build says how many faces
  it left out because they were wholly covered.
- **No lines at the slices of a wall.** Toon lines never follow the joins between brushes that make one surface, and
  hidden faces (inside or against another brush) carry no edge. The seams between touching brushes are also tight
  now: no dark pixels can show through.
- **Game Lighting hides the editor's brush outlines.** That view shows what the game shows; the outlines made every
  join between brushes look like a line. Selected brushes keep theirs. View > Game Lighting Options > Brush Outlines
  brings them back.
- **View > Umbra Visibility.** For a stock map: Cull by Tome draws only what the game itself would draw from where
  the camera is (its occlusion data hides the rest), and Show Cells and Portals draws the camera's visibility cell,
  its portals and the visible cells around it. The status bar shows how many surfaces, models, lights and probes are
  visible. Both choices are remembered.

- **Custom shaders for your maps.** A map (worldspawn `iw8r_surface_shader`) or a single material (`"shader"` in its
  definition) can light its surfaces with your own shader function: `<map folder>/shaders/<name>.iw8shader.hlsl` or
  the editor's `data/shaders`. The same code runs in the viewport (Game Lighting) and in the built map. Example: a
  toon (anime cel) shader, `toon`, with flat light tones, cool shade and sharp borders (research 114).
- **Shader Editor.** Tools > Shader Editor makes, edits, checks and applies your map's shaders. Templates: Stock
  lighting (the game's own surface lighting, editable) and Toon. Errors show with the shader's own line numbers;
  saving updates Game Lighting at once; Use for This Map applies it to the map (models included) and to the build.
- **Edge inputs for custom shaders.** A shader can now ask how far a pixel is from the nearest edge of its brush
  face, in screen pixels and in units, and which way the face across that edge points. Nothing about a look is built
  in: the inputs are there for any shader (outlines, wear, highlights). Seams between flush brushes are no edge. Maps
  whose shaders do not read them are unchanged.
- **The toon shader is a real shader now.** `toon.iw8shader.hlsl` holds the whole look, editable in the Shader Editor:
  more vivid, painted colour; the sun in flat tones with crisp shadow borders; shade as a cool tone instead of
  blackness; and ink lines 3 pixels wide at any distance, with clean corners, drawn by the shader itself. The ink strip
  brushes and Tools > Toon Ink Outlines are gone.
- **Example: Cinder Rail (Toon).** A copy of Cinder Rail with the toon shader and its sun matched to its sky. It
  builds and passes validation. Not seen in game since these changes.
- **The sky's sun lines up with the map's sun.** A map without `skyrotation` now has its sky turned so the painted
  sun is where the light comes from, in the build and the viewport. The build warns when the sky's sun sits higher
  or lower than the map's sun. Compile > Sun From Sky now keeps your sun's yaw and takes only the sky sun's height.
- **Toon surfaces no longer wash out in game.** With a custom shader the build no longer adds its own sun and probe
  shine on top (the viewport never did).
- **Pink containers fixed.** Material constants stored as raw bits were read byte-reversed (research 113).
- **Docking library rebuilt.** A failed build had left `qtadvanceddocking.dll` corrupt; it is rebuilt and every DLL in
  the release folder checked.

- **The sky is turned as in game.** The stock sky images were read transposed and mirrored, and the map's own sky
  rotation (worldspawn `skyrotation`, the same angle as its sun) was ignored; both now follow the game's sky shaders,
  so the clouds sit where you see them in game (owner-confirmed on Speedball). Sun From Sky also uses the rotation.
- **Scripted objects are drawn.** A stock map's scriptables (Speedball's signal lights, its caged lamps, the
  firing-range targets, boxes and bins: 106 there) show in the viewport as read-only props, until File > Edit Placed
  Objects makes them editable entities.
- **Lighter shade inside trees.** The leaf cards of tree canopies (and every `*_noshadow` material) no longer cast sun
  shadows, as in game: the trees' separate shadow meshes cast instead.
- **Props use the map's own materials.** Each placed model takes the materials the map assigns to it (its material
  skin) instead of the model's defaults.
- **Smooth camera movement.** The fly camera now eases up to speed and back down to a stop (also turning with the
  keys). View > Smooth Camera Movement turns it off.
- **Volumetric fog (option, off).** The game's volumetric haze is in View > Game Lighting Options, off by default: with
  the constants decoded so far it darkens Speedball's sky more than your in-game shot shows.
- **The game's own fog in Game Lighting.** The fog is now the game's formula, decoded from its shader and executable
  (research 111), instead of a fit made on Speedball. On Rust it was more than twice too bright and greyed the scene;
  it now keeps the colours and the far refinery shows through as in game. Speedball's sky band now matches your
  in-game shot within 2 %. Still missing: the game's volumetric haze on top (research 109).
- **View > Game Mode.** Pick a mode (Domination, Search and Destroy, Hardpoint, Headquarters, Capture the Flag, ...)
  and the viewports show only what the game keeps in that mode: its objectives, triggers, script models, brushmodels
  and spawns; the other modes' objects are hidden. All Modes shows everything, as before. Remembered between sessions.
- **Painted bands and stencils on props no longer flicker.** Decal materials inside models (Rust's yellow pillar band)
  are drawn on top of the surface beneath, as in game, instead of fighting it.
- **No more "Shadow" walls.** Surfaces that only cast shadows in game (their image reads "Shadow", around Rust's yard)
  are no longer drawn; their shadows stay.
- **Verdansk opens and runs without running the machine out of memory, at about 20 ms a frame.** It used to climb past
  14 GB and close (out of memory) a few minutes after opening; it now peaks at about 12 GB. Its frame went from 286 ms
  (since the stock lamps' shadows) to about 20 ms once the lamp shadows are drawn (more while other programs fill the
  graphics card's memory). What changed:
  - Props are kept as indexed meshes, a fifth of the memory (video memory for props 788 -> 161 MB), and the light of a
    streamed tile that arrives no longer rebuilds every prop: only their light is updated.
  - Lamp shadow maps and the near sun shadow draw only the props in their view (they drew all 1.2 million placements).
  - Projecting decals onto the props kept 8 GB for a moment; it now copies only the triangles that touch a decal.
  - The texture budget also follows the machine's memory (2 GB on a 16 GB machine), emissive images included, and
    materials that share one stock image share its texture. Windows counts video memory against the machine's memory.
  - A step that runs out of memory (a background job or the window itself) now stops with a message and is tried
    again, instead of closing the editor. Windows had about 14 GB committed by other programs during these tests.
  - Streamed tiles reach 14,000 units (48 tiles) on machines under 24 GB, 20,000 (96) above; stock/streamRadius and
    stock/streamLimit still override.
  - Very many editor lines (Verdansk's 1.2 million entity outlines) are drawn without antialiasing: 7 ms -> 0.2 ms.
  - Prop levels of detail now arrive on Verdansk: one name that points into another zone used to fail whole batches.
  - The 2D views no longer stall on Warzone maps: models arriving no longer re-read all 47,545 entities each time.
- **The game's bloom and vignette in Game Lighting.** Stock maps now get their vision's soft glow (the "veil": a wide
  bloom of the whole frame, 8 % on Speedball) and its darkened screen corners, built the way the game builds them
  (research 109). Against your in-game Speedball shot the frame's edges and corners now match far better (left edge 181
  -> 140, the game 137). View > Game Lighting Options > Bloom and Vignette turns them off.
- **Verdansk needs far less memory to open.** Reading the map's 422 MB export used to take about 1.6 GB per step (world,
  props, displacement, clutter, streamed tile list); each now takes 5 to 280 MB, with exactly the same result. The
  reflection probes' 160 MB of images stay on disk and only the ones around the camera are read. Two Verdansk tiles
  whose export had come out empty are exported again.
- **Verdansk and Quarry reflections.** Their reflection probes now load (research 108): the editor reads all 5,620 of
  Verdansk's, takes the shared ones from your install's packages once (about 160 MB in the world cache) and each streamed
  tile's own with the tile, and gives the view the probes around the camera. Wet roads, glass and metal reflect the
  town around them instead of nothing.
- **Your maps' props and the gun take their light from where they stand.** The build's light grid (what lights
  models, players and the viewmodel in game) was one flat colour at 70 % of the sun everywhere, so the gun was as
  bright in a closed room as outside. It is now baked from the map itself: open sky where it is visible, light bounced
  off the walls and floors around, dark indoors (plan 1.11, ROADMAP 5.20). Worldspawn `iw8r_flat_light_grid 1` keeps
  the old flat colour. Not seen in game.
- **View > Game Lighting Options.** Ambient occlusion, screen-space reflections, the stock lamps, their shadows and
  the prop levels of detail can each be turned off, with an AO strength, a fixed exposure for comparing screenshots,
  Re-bake Lighting Now and debug views; all of it is remembered between sessions.
- **Shadows from the stock maps' own lamps.** Lamps that cast shadows in game (70 on Hideout) now cast them in Game
  Lighting too, with the game's soft exponential shadow edge (research 106): shelves, beams and doorways block their
  light. The nearest 64 shadowed lights get a map; they are drawn once (a few per frame) and again only when something
  changes.
- **Stock props use their level-of-detail models, as in game.** Far props switch to the game's lower-detail models at
  the game's own distances, fetched in the background the first time a map is opened. Aniyah Tac went from 23.5 to
  10.3 ms a frame (30 to 6.8 million triangles), Speedball from 8 to 6 ms.
- **Lamps, signs and screens glow at their real brightness.** Stock emissive surfaces now use their material's
  brightness constant (research 107): a wall lantern or a care-package light is as bright as in game instead of a dim
  texture.
- **Grass, leaves, tarps and flags let light through.** Back-lit foliage and cloth glow with the sun behind them, and
  pick up the baked light from their far side (research 107 transmission). Wrap-lit materials (rocks, snow, some
  ground) soften their shading past the shadow line as in game.
- **Stock material constants read correctly.** Some materials (mostly foliage and sandbags) had their gloss and
  specular settings read from the wrong buffer or row; the material cache now reads the lit technique's own constants.
  Materials cached before are read again once, in the background.
- **The editor no longer closes when Verdansk runs out of memory while redrawing props**: it keeps the props as they
  are and says so in the status bar.
- **Stock maps' own lamps light the scene in Game Lighting.** Every spot and point light of a stock map (124 on Hideout,
  267 on Aniyah Tac) now lights the world and the props as the game draws them at runtime (research 106): their
  falloff, cone, bulb size and tube shape, their IES beam profile (read once from your install) and their highlights
  on glossy surfaces. Rooms under lamps are lit as in game instead of only by the baked light; the game's lightmap does
  not contain this light. The nearest 256 in view are drawn, as the game does. Shadows from these lights come next.
- **Verdansk props lit per tile.** Props in Verdansk's streamed tiles take their light from their own tile's light grid
  (read with the tile's lightmap, in the background), so props in the town get the same treatment as on the 6v6 maps.
- **The game's specular occlusion table.** Reflections in corners, creases and under props now fade exactly as the
  game's own table (gtso_lut, read once from your install like the BRDF table) says, from the ambient occlusion, the
  material's cavity, its gloss and the reflection angle. The change is small and mostly on glossy surfaces.
- **Placed props lit like the world around them.** In Game Lighting each stock prop now takes the light the game
  baked for it (its own light grid box, research 105) instead of one map-wide value, so props indoors and in shade are
  as dark as the walls next to them, and props in the open keep the sky's colour. The world cache reads the light
  grid once per map (a short background job the first time a map is opened after this update). On Hideout 12,131 of
  12,367 prop placements use it; the rest (lightmapped props such as rugs) keep the old light for now.
- **Screen-space reflections.** The stock materials that trace them in game (puddles, wet layers, some glossy props
  and glass; research 104) reflect the scene, traced in the previous frame as the game does, over the probe
  reflection. The effect is subtle on most maps (only those glossy materials trace).
- **Game Lighting renders in HDR.** The lit scene is drawn in linear light and the game's post chain (exposure, tone
  curve, colour grade) runs once over it; decals, glass and multiply layers now blend in linear light as in game, and
  auto exposure meters the scene itself instead of the finished frame (editor lines and markers no longer count).
  This is the base for screen-space reflections and bloom next.
- **Verdansk and Quarry stream their town around the camera.** The streamed world tiles within 20,000 units are
  read and drawn in the background as you move (buildings, lots, roads), and dropped behind you; past them the
  ground shows instead of holes. Read tiles stay on disk up to 3 GB (oldest dropped first). Settings
  stock/autoStream, stock/streamRadius, stock/streamLimit, stock/streamCacheGB. File > Load Streamed World Around
  Camera now just asks the loader to look again.
- **Verdansk's buildings and roads (owner hint: transient tiles).** Most of Verdansk's world (buildings, lots, roads)
  is in 749 streamed tile zones; once they are read, the reference shows the town instead of flat grey sheets.
  Groundwork landed: tiles can be built one at a time from the map's surface table, stock textures stay
  block-compressed on the GPU, the texture budget follows the video card (40 %, 1-4 GB), and world decals no
  longer cast sun shadows (Verdansk's thousands of puddle and dirt decals blotched the ground). Loading the tiles
  automatically around the camera is next (docs/PLAN_REMAINING.md 1.1).
- **Ambient occlusion in Game Lighting.** Corners, wall bases and the ground under props darken the way the game's
  screen-space AO does: indirect light gets a multi-bounce AO, reflections get specular occlusion. It needs a depth
  pre-pass; props small on screen are left out of it to keep the cost down (Hideout 12 ms, Aniyah Tac 18 ms).
- **Stock material shading.** World surfaces and placed props use each material's own gloss, cavity, metalness and
  normal strength, and props now show their normal maps.
- **Remaining work** is planned in docs/PLAN_REMAINING.md: lighting (streamed Verdansk tiles, HDR, reflections,
  lights, the compiled map's lighting), then the Umbra system, then the web viewer, then the rest.
- **Verdansk ground blended out to the game's distance** (17,368 units) by default, at your request. Far ground patches
  are no longer dropped at 4,000 units (that left sky-coloured holes), and the patch also covers the small cut-outs the
  coarse ground covered. About 21 ms a frame from a raised camera. `IW8R_TERRAIN_LAYER_RADIUS` still overrides it.
- **Softer grass and cut-out edges.** Placed props' cut-out edges (grass blades, fences, grates) are anti-aliased instead
  of hard-cut, which removes most of the black speckle in Speedball's grass. Stock foliage also stops drawing past its
  last LOD distance, as in game. `IW8R_HARD_CUTOUT=1` / `IW8R_NO_FOLIAGE_LOD_OUT=1` turn these off.
- **Verdansk ground up close.** Within about 6,000 units of the camera the editor now blends the terrain's real ground
  materials (dirt, grass, sand, rock) with the game's own per-layer weight images and reveal ramp (research 103),
  instead of the blurry baked colour. The patch follows the camera (rebuilt in the background when you move 2,000
  units) and replaces the coarse terrain there, so nothing pokes through. Verdansk's GPU grass clutter is drawn too.
  The game itself blends out to 17,368 units; `IW8R_TERRAIN_LAYER_RADIUS=17368` does the same in the editor, at about
  +2.6 ms and 900 MB more video memory for a picture that changes by about 4 levels in 255 on average. The patch's
  caches now drop surfaces that leave the radius, so a long walk across Verdansk stays bounded.
- **Prop LODs** are not used: screen culling (props under 4 pixels or out of view are skipped) already keeps every
  stock map between 4 and 20 ms a frame with Game Lighting on.
- **Test runner:** `tools/run_all_tests.ps1` builds the core and test targets in two xmake calls (it reported a
  build failure every run).
- **Verdansk and Quarry ground.** Their super terrain (the open ground, hills and mountains) was missing: ACTS could not export it. The ACTS fork now does (8b5ee11d, deployed), and the editor builds it from its height maps with each surface's baked colour, about a million triangles.
- **Stand-in boxes** for models past the prop budget no longer cast shadows, and are left out of the lit and Game Lighting views (Verdansk was covered in tan slabs).
- **Displaced surfaces.** Stone walls, cobbles and other displaced stock surfaces get their relief: their meshes are subdivided and moved by the game's own height maps (a few inches, as in game). Village, Aniyah Tac and Backlot use it most.
- **Missing stock images** draw plain grey on the stock reference instead of the magenta checker.
- **Converted maps' sky** proven offline to sit at infinity in game (the converter's six sky faces, drawn camera-relative).
- **Decals on props.** Stock decal volumes that find no world surface (stencils on crates, paint on the concrete
  barriers) are projected on the props, drawn in view within 4,000 units.
- **Viewport speed on big stock maps.** Hideout went from 56 ms to 17 ms a frame (Speedball 4.4 ms): layer and decal
  constants bound by offset instead of copied per draw, props culled on screen when outside the view or under a
  pixel, a lookup grid for the reflection probes, and layer triangles with no weight left out.
- **Grass.** Back faces of two-sided foliage are lit facing the viewer (the dark speckles are gone) and stock
  cut-outs use the game's 128/255 threshold. Speedball's grass is placed props; GPU ground clutter exists only on
  Verdansk's terrain (research 100).

## 2026-11-12

Game Lighting against the game (evening):

- **Stock maps look like the game.** Compared with an in-game screenshot of Speedball (same spot, FOV 120):
  - The vision's own colour grade is applied, with the base exposure recalibrated (+1.0 EV); the ground's brightness
    percentiles now match.
  - Fog and haze come from the vision (atmospheric scattering, height fog, the sky's haze band).
  - The stock sky is drawn at the brightness its material gives it, and it stays put as the camera moves.
  - Layered ground (up to 8 texture layers), puddles and edge wear, ground decals in their sort order, normal maps,
    painted-material tints (Speedball's building, blue containers), and shadows from trees, poles and props with a
    sharper shadow map near the camera.
  - Stock maps auto-expose from the frame as the game does. Lit Preview on a stock map shows the game lighting.
- **Other maps.** King's interior used its baked light 64x too bright (its bakedLightScale is read now); Shipment's
  sky was missing (its skyline surfaces were taken for the sky box); Hideout's sky haze overshot into cyan. Rust,
  Backlot and others checked. Per-map layered materials no longer take another map's same-named blend (Speedball's
  ground showed a metal base).
- **Material cache.** ACTS runs moved to a short temp folder: long packed image names passed Windows' 260-character
  path limit and stayed the missing-material checker.

Warzone and stock maps (later the same day):

- **Viewport speed on stock maps.** With a stock map open (mp_rust: about a thousand models) a camera step took 182 ms
  (about 5 fps) because the grid views redrew every model triangle on each move. Models are now boxes in 2D unless
  selected, and a camera move repaints only around the camera marker: the window adds about 6 ms per step. Measured
  with the new benchmark mode `--viewport-bench stock:<zone .ff>`.

- **Remove stock world geometry.** An `iw8r_remove_world` box on a stock map removes the world inside it (walls,
  crates baked into the world) from drawing and from collision when the zone is rebuilt, and bakes the sun shadow
  again without it. Baked lighting and occlusion keep the old geometry. Proven offline on Shipment
  (tools/stock_world_remove_test.py). Not seen in game.
- **Game Lighting on Warzone maps.** Verdansk, Quarry and the tutorial map draw with their own lightmaps: the
  editor reads the lightmaps of the streamed tiles it loads around the camera (one tile at a time, the scratch deleted
  at once) and packs them into one atlas. Verdansk uses Quarry's sun and exposure, as the game does.
- **Stock reflections and specular in Game Lighting.** The stock world shows its own reflection probes (read from the
  zone once, cached), the game's environment BRDF table (read from the install) and the GGX sun highlight, as the
  stock world shader does. About 2.2 ms per frame on Backlot2 at 1920 x 1080.

- **Turn a stock map's sun.** File > Relight Stock Map Sun takes a direction (pitch yaw). Rebuild Stock Zone turns
  the sun, re-bakes its shadows from the stock world and props, and turns the sun-tinted fog with it. The baked
  bounce light of the lightmaps keeps the old sun. Proven offline on Shipment (tools/stock_sun_direction_test.py).
  Not seen in game.
- **Bake lightmaps checkbox** in the Compile panel (worldspawn `iw8r_bake_lightmaps`).

- **Copy and delete placed objects with physics.** File > Edit Placed Objects now lets you copy and delete doors,
  signs, fences, props and extraction sites too, not only buy stations; Rebuild Stock Zone writes them with their
  dynamic entities (renumbered, spatial chains and counts rebuilt). Copies keep the group names scripts look for.
  Proven offline on Quarry and Shipment (tools/dynent_copy_remove_test.py). Verdansk removals are refused (its
  streamed zones point at later objects). Not seen in game.

Lighting:

- **Baked lightmaps for own maps.** Worldspawn `iw8r_bake_lightmaps 1` bakes the world's lightmap when the map is
  built: charts packed into one atlas, the sky light and one bounce of the sun and lights, and where the sun is
  blocked. The game draws the sun and the lights live on top, as it does for converted maps, so they are not baked
  twice. `iw8r_bake_sky`, `iw8r_lightmap_texel` and `iw8r_bake_rays` tune it. The converter turns it into the
  game's lightmap and every world surface uses it (tools/lightmap_bake_build_test.py: build, ACTS, the built
  world's lightmap table). Not seen in game.
- **View > Game Lighting (Ctrl+Alt+L).** The camera shows the map as the built map looks in game: the editor bakes the
  lightmap in the background with the build's baker (again after edits) and shades with the converter's world shader
  formula (lightmap, sun and its shadows, light entities in the game's units) and the game's exposure and ACES tone
  curve (research 95). Cost about 0.8 ms per frame over the textured view on a 2-million-triangle scene at 1920 x 1080
  (8 ms moving); the small sample room draws in 0.35 ms. Not compared with the game yet.
- **Game Lighting on stock maps.** With a stock map shown (View > Stock Map Reference or a stock edit), Game Lighting
  draws the stock world with the map's own baked lightmap (read from the zone once, cached as
  `world_cache/<zone>/lightmap.iw8rlm`), the map's own sun, and its vision's auto exposure, measured from the frame and
  adapting over time as in game. Backlot2's 363k-triangle world draws in about 0.6 ms per frame. Warzone maps
  (streamed lightmaps) are not supported yet and say so.

Warzone and stock maps:

- **Loot points.** File > Edit Loot Points shows the loot points of a Warzone stock map as entities (Quarry 2,924,
  Verdansk 23,446); moved, added and deleted points are written back with the stock rules for clusters, zones and
  counts. Own maps take `iw8r_loot_point` entities, in one loot zone named by worldspawn `iw8r_loot_zone` (default
  `map_quarry`). Not seen in game.
- **New loot areas on stock maps.** `iw8r_loot_cluster` and `iw8r_loot_zone` brush entities add loot clusters and
  named loot zones after the map's own (converter editzone `loot_boxes`); loot points inside them join them. Proven
  offline on mp_br_tut and mp_br_quarry. The same entities work on your own maps (several clusters and named
  zones instead of one zone over every point). Not seen in game.
- **Move other placed objects** of a stock map (signs, fences, props with physics): their physics entry follows the
  move (pose, spatial partition and chain; checked on Quarry, tools/dynent_move_test.py). Doors move inside their area
  (the box of the stock doors that share their spatial index). Not seen in game.
- **Remove buy stations** from a stock Warzone map: delete the entity after Edit Placed Objects; the rebuild drops
  the instance and renumbers the dynamic entities after it (converter aacfad65). Not seen in game.
- **Buy stations.** File > Edit Placed Objects moves and copies the buy stations of a stock map; own Warzone maps
  carry `br_plunder_box` into the map (a custom map never loads the Warzone zone: research 93). The scriptable physics limits rule (two definition flag pairs, exact on 21 stock
  maps) makes 509 stock scripted objects buildable (was 317). Not seen in game.
- **Vehicles.** Vehicle spawn entities (ATV, tactical rover, SUV, cargo truck, helicopter) on an own map carry the
  stock vehicle and its model from mp_br_quarry. Not seen in game.
- **Streamed world.** File > Load Streamed World Around Camera draws the streamed world tiles of Quarry and Verdansk
  near the camera in the stock reference.
- **Added brushes on stock maps.** Brushes drawn on a stock map are built as models (with collision) and carried in
  on File > Rebuild Stock Zone. Not seen in game.
- **Hidden props lose their collision** on the small stock maps. Not seen in game.
- **Stock patch files.** The game ships patch files with real map data; every stock edit starts from the stock map
  with its own patch, also while an earlier edit is installed.
- **Stock terrain textured on every map.** The terrain blend materials of a stock map (`tw4/`, `twc4/`, named per
  map) are read from the map's own zone; they were checkered on maps outside the material catalogue (mp_br_tut: 346
  materials).
- **New game types.** Tools > Game Modes > New game type script: a game type of your own under a new name, compiled
  into the map zone (converter c5e182f3). Every build checks its far calls against the stock script index (only files
  the game stores by number link: research 92). Built and ACTS-checked offline; not seen in game.
- **Mode rules.** Tools > Game Modes > Rules: max health, move speed, one weapon instead of the loadout and
  infinite ammo, applied to every player at each spawn by the map's own script, which runs after the game type's start
  (read from the game exe: docs/research/91). Built through the real pipeline offline; not seen in game.
- **Loot items on own maps.** A map with loot points carries the loot item definitions of its loot table from the
  Warzone zone, which a custom map never loads (research 93). Built and ACTS-checked offline; not seen in game.
- **Fix: "no circle" no longer breaks a Warzone map.** The generated map script called
  `br_gametypes::disablefeature`, a far call that cannot link (research 92) and would drop the match or exit the game
  at load. The map now sets only `level.br_circle_disabled`.
- **Build preflight fix**: methods called on `self` (`self takeallweapons()`) are no longer flagged as unknown
  functions.
- **Retexture stock maps.** Worldspawn `iw8r_stock_retexture` swaps world materials on Rebuild Stock Zone
  (tools/stock_retexture_test.py: 66 surfaces of mp_shipment, ACTS 0 failed). Not seen in game.
- **Stock map sky.** File > Relight Stock Map Sky: another stock map's sky, carried in and put on this map's sky
  surfaces (converter replace_materials). Not seen in game.
- **Stock sun colour and intensity.** File > Relight Stock Map Sun; Rebuild Stock Zone writes them into the stock
  sun light (converter 5ecee331). The direction stays (baked sun shadows). Not seen in game.
- **Game mode presets** with the private server's playlist settings, carried in a map or sent at F5.

Editing:

- **Traversals for bots** (Primitives menu): a begin / end node pair with a stock animation, written into the
  navigation mesh by the build.
- **Bridge Two Faces** (Brush menu): a new brush that spans two selected faces.
- **Prefab sharing**: export and import prefab bundles as zips (deflated zips too), convert a plain `.map` prefab to a
  bundle, wall placement and Rotate Selected Prefab by the bundle's yaw step.
- **Blend By Slope** (Patch > Height Map): the terrain's slope written into its vertex alpha for layered materials.

Building:

- **Collision tiles.** Maps can keep their collision in one piece or as the game's streamed tiles (1,024 tile zones
  and a combined package, generated automatically): Edit > Preferences > Build: collision, or worldspawn
  `iw8r_collision`. The private client currently loads only the combined package of a custom map. Not seen in game.
- **Converter fixes**: a fresh rewrite of a stock zone kept every asset (Quarry lost a killstreak definition before);
  zone headers keep their transient file type.

Documentation:

- **Feature list pictures redone** (docs/features): taken on the real window system by `IW8Radiant.exe
  --feature-shots --out <dir>` (45 pictures, readable text, every view settled before the picture).

Tools for this work (read only on the game folder): `zonetool havokinfo`, `zonetool streaminfo`,
`tools/dw_disasm.py` (reads the recompiled game exe with its linker map), and one offline proof script per feature
under `tools/` (`*_test.py`).
