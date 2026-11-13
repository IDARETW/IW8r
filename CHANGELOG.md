# Changes

Newest first. "Not seen in game" means built and checked offline (converter read-back, the game's own loaders through
ACTS, tests) but not yet played; [docs/TEST_CHECKLIST.md](docs/TEST_CHECKLIST.md) lists the in-game checks.

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
