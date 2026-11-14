# Test checklist (owner)

The dev showcase map `samples/dev/iw8r_dev_showcase.map` (regenerate: `py -3 -I tools/make_dev_showcase.py`) has one
labelled row per asset variation (each row starts at a `row_<name>` script_origin on the west side). Build it with F7
(or headless: `bin\release\IW8Radiant.exe --compile samples\dev\iw8r_dev_showcase.map --out out\sc-compile`), deploy
with Ctrl+F7, then in Replay: Local Play, F6, pick the map (title "IW8 Radiant dev showcase").

Mark each line OK / wrong (with a screenshot) and send it back.

## In the editor

- [ ] New on 2026-11-14 (editor only, nothing here needs the game; all are covered by tests, these are for your own look):
  - Brush > Paint Mesh on Faces: Ctrl+Shift+click a floor face, run the command, keep 32. A mesh lies on the face, the
    face under it is caulk now (not drawn while the mesh covers it), and Vertex Paint Mode starts. Ctrl+Z takes the
    whole step back.
  - Open Hideout, View > Game Lighting: the picture is warm. View > Game Lighting Options > White Balance off: it
    turns cold and grey. On again.
  - Open a large stock map and move the camera at once: textures arrive while you move, without the view stalling.
  - Fly away from Hideout: small props (straps, bottles, debris) go first; walls, pipes and towers stay.
- [x] The map opens; the camera shows textured rows (stock textures may fill in over a few seconds on first open).
- [x] View > Camera Mode: Wireframe / Flat / Fullbright / Normals all draw; Ctrl+L lit preview shows sun shadows and the yellow sun arrow.
- [ ] Optional, only if you want to check it: a map with worldspawn `iw8r_bake_lightmaps 1` in game next to View > Game
      Lighting (Ctrl+Alt+L) in the editor from the same spot: brightness, shadows, light pools and colours should match.
- [ ] Models browser: selecting a model shows the 3D preview; Prefabs dock lists prefabs (if any); Effects dock places
      an fx_origin (double-click). (wrong 2026-11-11: the model browser works; the Prefabs dock thumbnails are cut off with no names, and prefabs cannot be placed)
- [x] P (play preview): walk, sprint, slide, mantle the 24/40/56/72 ledges (row ladder_and_mantle), climb the ladder tower.
- [ ] View > Show Compiled Collision after F7: cyan boxes around the placed models, green ladder. (wrong 2026-11-11: the outlines work, but the ladder was not seen in the dev map)
- [x] Live Link > Direct: Read camera, Follow game while Replay runs (custom exe), Move player.
- [x] Textured view with normal maps (View > Normal Maps and Gloss): stone, wood and metal show bump and a gloss highlight;
      turning it off makes them flat. Ctrl+L on a textured view: the light entities light the floor per pixel (round pools).
- [x] View > Camera Mode > Contents: structural grey, detail green, non-colliding blue, weapon clip yellow, brush entities purple.
- [ ] Materials / Models browser right-click: Material Info / Model Info show zone, technique, images, texture slots, LOD0 surfaces.
- [ ] Drag a model, effect or sound from its browser into the camera: it lands on the surface under the cursor. Drag a
      material onto a brush: the brush takes it. (wrong 2026-11-11: models, effects and sounds drop correctly; a material does not apply to misc_models and other objects, and the owner wants drag-over with a live preview)
- [x] Browsers: All / Favourites / Recent (right-click > Add to Favourites).
- [ ] Script dock: New Script, Insert Behaviour > Sliding door, Check; F7 builds with it (the log shows "preflight: 0 error(s)"). (wrong 2026-11-11: a spin script on tm/building_horse_stall_wood_beam_128_01 gave a compile error; the owner commented it out)
- [ ] Compile > Time of Day > Dusk / Night: sun, sky, fog change in one undo step (Shift keeps the sky). (wrong 2026-11-11: it applies, but the sun marker, the sun in the sky and the shadows do not line up; the set sun direction wins)
- [x] Ctrl+Shift+P command palette: type "tod night" and Enter.
- [x] Outliner dock (next to Entity): entities by class, filter, clicking rows selects them.
- [x] Ctrl+Shift+1 saves a camera bookmark, Ctrl+1 returns to it. View > Camera Path: add keys (Ctrl+Alt+K), play (Ctrl+Alt+P).
- [x] Brush > Arrange: Array / Radial Duplicate, Mirror Copy, Align, Distribute. Brush > Spline: select 3 script_origins, Wall Through Selected Points.
- [x] Primitives > Triggers: hurt / use / radius / callout zone on the selection bounds.
- [ ] Tools > Check Map: link, light budget, spawn sight-line and callout findings appear (with a broken target to see one).
- [ ] File > Compare With Last Saved Backup (.bak), or File > Compare With Map with any other .map: lists what
      changed; Save Patch / Apply Map Patch.
- [ ] File > Open IW8 Stock Map: a list of the 37 openable stock maps. (wrong 2026-11-11: mp_backlot2 opened with a lot of geometry missing)
- [ ] A misc_prefab with "iw8r_mat:<material>" "<other material>" shows the swapped material in the views and the build.

**The web page (2026-11-13; no game needed).** Tools > Web Server on, then open the address the status bar shows.
Each point was run by me in a browser against a running editor; this list is for your own look.

- [ ] The page has the editor's menu bar. View > Game Lighting from the page switches the editor's camera view.
- [ ] "Find a command": type "shadow"; Sun Shadow Detail is listed greyed ("opens a dialog on the editor's screen").
- [ ] Compile > Sun & Sky opens a form with the map's sun; change the pitch, Apply, and the editor's sun moves;
      Undo in the page puts it back.
- [ ] Tools > Game Modes: pick "Warzone: Battle Royale Trios", "Carry in this map", OK; worldspawn gets
      `iw8r_mode_dvars`. Tools > Check Map lists the same findings as the editor's dialog.
- [ ] View box > "Editor view (live)": the editor's own picture; drag to look, W A S D to fly, click selects.
      A model clicked in Assets lands on the surface at the centre of the picture.
- [ ] A big map (Cinder Rail) opens in the page within a few seconds.
- [ ] Draw brush, drag a rectangle in the Top view: a brush appears in the editor too. Drag a handle of its box:
      it grows on that side only. Select a spawn: the Inspector shows what the class is and help for its keys.

## In game: the short list (2026-11-14), what only the game can settle, most important first

Everything else in this file was proven without the game. If you look at nothing else, look at these, in this order.
Each one names the longer section below that has the steps.

- [x] SHORT 1 (seen 2026-11-14: Shipment loads and shows the toon filter; no ink lines on stock geometry and no
      toon on the weapon yet, both asked for). An installed stock edit loads: install the Shipment toon zone (folder `out\agent\toon_ship\edit`,
      File > Install Edited Stock Map Into Game) and start Shipment. Every stock-map and Warzone edit goes into the
      game this way, and it has never been run. Section "Shipment with the toon shader's filter".
- [ ] SHORT 2. Your own map looks like View > Game Lighting, once with Compile > Bake lightmaps off and once on.
      Section "an own map with and without baked lightmaps".
- [ ] SHORT 3. A stock door carried into your own map opens. Section "real stock doors and objects".
- [ ] SHORT 4. A stock map rebuilt with removed world geometry: the object is gone and you can walk through where it
      was. Section "an edited stock map".
- [ ] SHORT 5. Copied loot, doors and extraction sites on Quarry work like the originals. Section "game modes and a
      small Warzone map".
- [ ] SHORT 6. Only if you want it: a map built with Compile > Occlusion bake shows nothing missing. Section "the
      occlusion bake".

## In game (optional, 2026-11-14): props at a distance
- [ ] On Hideout, stand at one end of the map and look along it in the game and in the editor (View > Game Lighting,
  the same place and view). Small props far away (conduit straps, bottles, small debris) should be missing in both,
  and the walls, pipes and manhole covers should be there in both. If the editor shows less than the game, say which
  objects.

## In game (optional, 2026-11-14): your own HUD picture
- [ ] Put a PNG into `<map name>.hud` (Compile > HUD Images Folder), add a map script (worldspawn `iw8r_script`) with
  `user_main() { icon = newhudelem(); icon.x = 20; icon.y = 120; icon setshader("hud_<file name>", 64, 64); }`, build
  and play: the picture is on the screen at the left. If a grey or checkered square shows, the material loaded but
  the image did not; if nothing shows, say so.

## In game (optional, 2026-11-14): a hint on a use trigger
- [ ] Give a `trigger_use` brush the key `hint` = `Press ^3[{+activate}]^7 to test`. Build and play, walk into the
  trigger: the text appears a little below the middle of the screen and goes when you step out. If nothing shows, or
  the trigger stops reacting to Use, say so (the hint is drawn by the map's script).

## In game (optional, 2026-11-14): your own text
- [ ] Compile > Map Text File, keep the example entry, and in the map script (worldspawn `iw8r_script`) put
  `user_main() { t = newhudelem(); t.x = 20; t.y = 100; t settext( &"MP_YOURMAP/HINT_EXAMPLE" ); }` with your map id
  in capitals. Build and play: the text is on the screen. If the name itself shows (MP_YOURMAP/HINT_EXAMPLE), the
  game did not find the entry: say so.

## In game: your map's model light (2026-11-13), not seen in game

- [ ] Build the dev showcase (F7) and walk from outside into a closed room: the gun and nearby props get darker inside
      and take the sky's colour outside (before, they were equally bright everywhere). The build log says "light grid:
      ... colour(s) baked from the map".

## In game: Game Lighting against the game (2026-11-13), the editor next to your in-game shots

Open a stock map (File > Open IW8 Stock Map), turn on View > Game Lighting (Ctrl+Alt+L), and compare with the same spot
in game. Only if you want to check it; each line says what should match.

- [ ] Hideout: rooms under lamps are lit by the lamps (pools under ceiling lights, soft beam edges), with shadows from
      shelves and beams; props inside are as dark as the walls next to them.
- [ ] Aniyah Tac: the wall lanterns glow; far props switch to their low-detail models without visible popping.
- [ ] Speedball: grass and leaves glow when the sun is behind them (look toward the sun across the grass).
- [ ] Hideout: the doorway cloths and tarps have the colours you see in game (the editor now reads their tint from the
      material's lit constants; one cloth turned purple-grey, another red).
- [ ] Verdansk: props in the town are lit like the buildings around them (not uniformly bright).
- [ ] Hideout or Verdansk: the overall colour is as warm as in the game (2026-11-14: the map's white balance). If the
      editor is warmer or colder than the game, turn View > Game Lighting Options > White Balance off and say which
      of the two is closer.

## In game (after deploy)

- [x] **stock_materials**: 20 pillars, each its own stock texture; the last (chainlink) is see-through.
- [x] **tool_materials**: caulk/nodraw invisible; clip blocks you and bullets; player clip blocks you only; weapon clip
      blocks bullets only; ladder box is invisible but climbable; water is a blue rippled box and walk-through; hint/skip invisible and walk-through.
- [ ] **brush_flags**: detail and weaponClip boxes solid; nonColliding box walk-through.
- [x] **shapes_and_slopes**: the 4 ramps (14/27/37/45 degrees) walkable except possibly the steepest; both stairs walkable.
- [ ] **patches**: flat patch, arch curve and grassy terrain visible and solid. (wrong 2026-11-11: patches draw from one side only)
- [x] **stock_models_misc_model**: 15 stock models (static misc_model), textured, standing upright, blocking you and bullets.
- [ ] **live_script_models_and_misc_model** (regenerated showcase): scripted_0 spins, scripted_1 bobs, scripted_2 blinks,
      all three block you; the pallet at the east end breaks (sparks, gone) after a few shots. (wrong 2026-11-11: visible and solid, but static: no spin, bob or blink)
- [x] **live_script_models_and_misc_model**: the 3 live script_models (each targeted by a script_origin) visible and
      solid; the 3 misc_models visible.
- [ ] **lights_and_probes**: white, red, blue omni lights and a spot light visibly light the floor; the first probe has an
      authored volume (teal box in the editor): reflections inside it come from that probe. (wrong 2026-11-11: the white, red, blue and spot lights work; going into the probe volume changes nothing visible)
- [ ] **effects**: explosion every 6 s, a lingering fire, light smoke, sparks every 2 s. (wrong 2026-11-11: the effects look good, but they have no sound and do no damage)
- [x] **ambient sound** (east end of the effects row, regenerated showcase only): a smokestack loop, a fluorescent
      hum loop, and a debris one-shot every 4 s, louder as you walk up to each green diamond.
- [x] **navigation**: (bots, if you test them) path around the rows; jump down from the 40-unit block.
- [x] **ladder_and_mantle**: ladder climbable to the tower top; ledges mantle.
- [x] **spawns**: you spawn on the spawns row (TDM/DM/DOM).
- [ ] **game_modes**: Domination A/B/C flags capture; Hardpoint zone and S&D bomb site present in those modes. (wrong 2026-11-11: only TDM (war) can be picked; other modes say the map does not support this game mode)
- [x] **custom_assets**: orange/black checker brush and crate (custom texture + custom model); the grate panel is see-through.
- [ ] **triggers_and_brushmodels**: the hurt trigger hurts; the scripted door slab is present. (wrong 2026-11-11: the hurt trigger does not hurt; triggers do nothing in game)
- [x] **world**: shipment sky, light fog, slight film grade; the sun (sundirection -40 235) is south-west, where the
      shipment sky paints it, so shadows fall away from the bright spot in the sky.
- [ ] Nothing in the map is plain white (white = a material that was not built); the cinderblock pallet is textured. (wrong 2026-11-11: nothing is white, but the placed traffic cones look as if they have no colour material)
- [x] In the editor camera: the dryer is a solid white appliance; the alder bush shows cut-out leaves, not cards.
- [ ] Probes: a reflection_probe with priority / feather keys blends its reflections over that distance. (wrong 2026-11-11: no visible change near the probes)
- [x] Normal and gloss maps on stock textures (built from the cached `_n&_g` images): bricks, wood and metal look bumpy
      and metal shines, not flat. Report anything that looks inverted (bumps lit from the wrong side) or too shiny.
- [ ] A map with the Script dock's behaviours (door, lift, teleporter, jump pad, spinner, timed fx, trigger message)
      runs them: the build compiles them with gsc-tool, but none has been seen in game yet.
- [ ] Time of Day presets build and the sky / fog / sun match the editor's preview.
- [x] Live hot channels (needs a new build, Live Link hot update ticked): moving an fx_origin replays its effect at the
      new spot; moving a sound_emitter plays its alias there (a loop follows it); editing the worldspawn sundirection or
      suncolor changes the sun in game. Report the sun direction if it comes out mirrored (the sign is a guess).
- [ ] Editor: View > Camera Mode > Texel Density colours faces (green = about 2 px per unit); Tools > Start / Stop Macro
      Recording turns a few moves and key edits into a script that replays on another selection; Brush > UV Editor drags,
      scales and rotates a face's texture; Effect Info shows the moving particle preview; Model Info lists LODs and tags.

- [ ] Sun From Sky: set a stock sky (e.g. sky_mp_shipment), build, load: is the sun disc in the sky where the shadows
      point from? If the sky looks turned about 90 degrees from the shadows, report it. (wrong 2026-11-11: in game the shadows are turned about 90 degrees counter-clockwise from the editor)
- [x] Lit preview: compare the camera (View > Lit Preview) with the game at the same spot; adjust View > Lit Preview
      Brightness until they match and tell me the values (they can become the defaults).
- [x] A patch painted with Patch > Vertex Paint Mode: does the colour or alpha show in game?
- [x] misc_model `lod_scale 4` keeps the detailed model further away; `no_shadow 1` casts no shadow from light entities.

- [x] (2026-11-10 fixes) The gun / viewmodel is lit normally (no longer black); models are lit, darker in sun shadow.
- [x] Sun shadows reach further from the player than before.
- [ ] A misc_model with no_shadow 1 casts no shadow at all; dynamic props and script models block the player.
- [ ] A dynamic prop moves when shot or pushed (it rests until disturbed). (wrong 2026-11-11: the dynamic objects (traffic cones) stay static)
- [ ] A light entity with a large radius (e.g. 600) and iw8r_intensity_units set lights the floor near it. (wrong 2026-11-11: a light whose position and values were changed does not show in game)

- [ ] Door: a script_brushmodel with iw8r_door "rotate 90 1 3" and a trigger_use targeting it swings open when you use it.

## In game: the new client exe (installed 2026-11-11 19:22), not seen in game

The local client was rebuilt with three additions (cold start, game-mode list, debug overlay). Do these first.

- [ ] Start `game_dx12_ship_replay_local_dw.exe` by hand, with the local host running, with no arguments. It boots
      and reaches the menus as before. If it does not start or crashes: run the rollback line below and report it.
- [ ] In the game console type `iw8r_mapDebug`. It is a known dvar with value 0.
- [ ] Close the game. In the editor press F5 on a map: the F5 lines say "the client was asked to boot straight into
      the map", then lines that start with `client:` (request, signed in, multiplayer active, front end ready,
      hosting, start requested, loading), and you see a dark screen and then the map loading screen.
- [ ] Rollback, if needed (game closed): `py -3 -B
      E:\IW8-Radiant\tools\dw_candidate_install.py --rollback E:\IW8-Radiant\out\dw_backup\20261111-191715-nov8-candidate`

## In game: F5 cold start to the loading screen (2026-11-11), not seen in game

Start with Replay closed. The local online-services host may be closed too. Map: `out/agent/mp_iw8r_fix1/mp_iw8r_fix1.map`.

- [ ] Press F5. The Compile dock shows lines that start with `F5 +<seconds> s:`. One of them says which exe is
      started: it must be `game_dx12_ship_replay_local_dw.exe`.
- [ ] If the host was closed, a line says it is being started, then that it answers. The game starts after that.
- [ ] A dark card "Loading mp_iw8r_fix1" covers the game window from the start. You do not see the splash screens,
      the mode select, the Multiplayer menu or the lobby.
- [ ] The card closes when the map loading screen begins. There is no lobby countdown. You spawn in Team Deathmatch.
- [ ] The last line is `F5 +<seconds> s: In game on mp_iw8r_fix1.` Write down that time.
- [ ] If a menu stays on screen, or the card stays for more than 3 minutes: copy every `F5 +` line and send it.
- [ ] If the card does not hide the game (for example the game runs in exclusive full screen), say so.
- [ ] Say whether menu music or a video sound is heard while the card is up (sound is not covered).
- [ ] The card can be switched off: `IW8Radiant.ini`, section `[devmap]`, `cover=false`.

## In game: real stock doors and objects (2026-11-12), not seen in game: the one look that is needed

Everything else in this list can wait. This one cannot be settled without the game, and one load answers it.

- [ ] Edit > Preferences: "Real stock scripted objects" is ticked (it is on by default since 2026-11-12, the
      owner's choice). In the Prefabs dock the stock door has no FALLBACK badge.
- [ ] In a small map place the prefab "Stock door, painted wood, single" and build (F7). The log says "main
      contents" for the door and "carry their definition from the stock zone mp_backlot2".
- [ ] Load the map. Say which of these happens: (a) the map loads and the door opens with the "Open Door" prompt,
      (b) the map loads but the door is missing, does not open or has no collision, (c) the map does not load
      (what does the game show).
- [ ] If (c): untick the switch and build again to get the previous behaviour back.

## In game: game modes and a small Warzone map (2026-11-12), not seen in game

- [ ] Tools > Game Modes. Pick "Warzone on a small map", Carry in this map, Insert Warzone start entities. Move the
      eight spawns onto the floor. Build (F7): the log has "game types detected: ... br" and two lines about the
      mode dvars and the Warzone switches.
- [ ] In the game, start the map with the game type br. Say what happens: (a) the match starts and you spawn,
      (b) it loads and stops with a script error (which one), (c) the map is not offered for br.
- [ ] Tools > Game Modes > "Warzone: Battle Royale Trios" > Use for F5 is meant for the stock Warzone maps: the
      editor sends its settings before it starts the match.
- [ ] Loot on your own map: place a few `iw8r_loot_point` entities on the floor (one with `loot_cluster` `cache`),
      build, start with br. Say whether items or a supply box appear there.
- [ ] Vehicles on your own Warzone map: place a `script_struct_mp_atv` and a `script_struct_mp_jeep` on open ground.
      Build: the log says "2 vehicle spawn(s); 4 stock asset(s) carried". In a br match, say whether the vehicles
      are there and drive.
- [ ] Buy station on your own Warzone map: a `scriptable` entity with `scriptable` `br_plunder_box`. Build: the log
      says "1 scriptable instance(s) name a definition of the Warzone zone". In a br match, say whether the station
      stands there and opens. If the map fails to load with it, that is the answer to "does the game load the
      Warzone file for an own map": remove the entity and tell me.

## In game: a map with collision in tiles (2026-11-12), not seen in game

- [ ] Open any small map, set worldspawn `iw8r_collision` to `tiles`, build (F7). The log has "collision is built as
      streamed tiles"; the package folder has `<map>_cg_ls_tr.ff` and 1,024 `<map>_cg_NNNNN_tr.ff`.
- [ ] Play it (F5): walk on the floor, jump on boxes, shoot walls. Say whether you fall through anything. If the map
      does not load, set the key to `one` and build again, and tell me.

## In game: an edited stock map (2026-11-12), not seen in game

- [ ] File > Open IW8 Stock Map > mp_shipment. Map > Edit Compiled Spawns. Move one spawn a little, add a
      `script_model` with a model of another map (for example `door_wooden_panel_03_painted_left_mp_nophysics`).
- [ ] File > Rebuild Stock Zone: a few seconds, the status line ends with "checked (ACTS, 0 failed)".
- [ ] Close the game. File > Install Edited Stock Map Into Game: the dialog lists `mp_shipment.ff` (and
      `techsets_mp_shipment.ff`) and the folder `iw8r_stock` the stock files move to. Confirm.
- [ ] Draw a box brush with a stock material somewhere open on Shipment before the rebuild. The Console says "added
      brushes (entity 0): 12 triangles ... built as model iw8r_ovl_...".
- [ ] Start Shipment in the game: say whether it loads and whether the added model, the box (can you stand on it?)
      and the moved spawn are there.
- [ ] File > Restore Stock Map In Game puts the stock map back (do this before playing online or with others).
- [ ] Loot of a Warzone map: File > Open IW8 Stock Map > mp_br_quarry (or mp_br_tut, which is small). File > Edit
      Loot Points: green points appear (Quarry 2,924). Delete the points of one building and add a few in another
      place. Rebuild, install, start the map in Warzone: say whether loot is gone there and appears at the new points.

- [ ] Buy stations of a Warzone map: on mp_br_quarry, File > Edit Placed Objects (120 orange points). Move one a
      few metres and copy another to a new place. Rebuild (the Console says "1 placed object(s) moved, 1 added"),
      install, start the map: say whether the station stands at the new place and whether the extra one works.

## In game: camera view speed and prefab models (2026-11-11 late), editor only, not seen

- [ ] Restart the editor and open a large map. Selecting and moving things in the camera view is immediate.
- [ ] Place the bundle "Jump pad with effect and sound" from the Prefabs dock: its manhole cover is drawn as the
      real model, not as a small box.
- [ ] A physics prop (dynamic_prop) is drawn as its model.

## In game: decals and hardpoint markers (2026-11-11 late), not seen in game

- [ ] Showcase 2 (`samples/dev/mp_iw8r_show2.map`), the decal station: the picture is projected on the floor.
- [ ] Optional, only if you want to try scriptables: place the entity `scriptable` with `scriptable` set to
      `hardpoint_chevron`, build and load the map. Pass: the map loads. If it does not load, say what the game
      shows; removing the entity gives the previous build back.

## In game: stock maps show their geometry (2026-11-11 night), editor only, not seen

- [ ] Restart the editor. File > Open IW8 Stock Map > mp_rust. The world appears at once; props appear over the
      next half minute or so (longer the first time). Props still loading are amber cubes; failed ones are red.
- [ ] The status bar has a permanent "Stock props: N of M models ready" label while this runs, and a final count.
- [ ] The same with mp_backlot2.

## In game: showcase 2 (2026-11-11 night), not seen in game

- [ ] Open `samples/dev/mp_iw8r_show2.map` and press F5. You spawn at the south end of one arena with 93 numbered
      stations. Each has a sign board with its number, and a message with the number and name when you step on it.
- [ ] Work through `docs/DEV_SHOWCASE.md`: one row per station with what to look for. Say which numbers are wrong.

## In game: fixes after the third playtest (2026-11-11 night), not seen in game

- [ ] Restart the editor first. In the camera view the grass cubes are the right way up.
- [ ] Build and play: you can stand on the grass cubes and walk up the staircase (they stopped bullets only).
- [ ] The door: still the editor-made one in this build. A real stock door as a prefab is being worked on.

## In game: materials on your own models (2026-11-11 evening), editor steps first, not seen in game

- [ ] Restart the editor (the fix is in the new build). Select the staircase cubes in `mp_iw8r_fix1`, then click
      `mc_grass_custom` in the Materials dock: all selected cubes show the grass. One Ctrl+Z takes it off again.
- [ ] Build (F7): the log says `N model(s) built with their iw8r_material on every surface` and has no line that
      ends in "it keeps its own" for the cubes.
- [ ] In game: the staircase cubes show the grass texture.

## In game: map debug overlay (2026-11-11), with the new client exe, not seen in game

Host a private match on your map. The full list with pass conditions is in `docs/research/78_map_debug_overlay.md`,
section 6. The short form, from the editor:

- [ ] Live Link dock > Debug overlay > "Where am I": a panel in the game shows your position. The feet height
      matches the floor height in the editor.
- [ ] "Entities and triggers": labels sit on your script_models and triggers; triggers have an orange wire box of
      the size of the trigger brush.
- [ ] Select an entity that has a targetname in the editor, press Selected: only that entity is labelled in game.
- [ ] "Entities and crosshair": the surface row changes with what you look at and matches the surface type you set
      in the editor. If the game freezes or crashes when this is switched on, say so and do not use it again.
- [ ] Compare with game: a window lists the game's entity count and any named map entity that is missing or at
      another place. "Move editor camera to the player" puts the editor camera where you stand.

## In game: game modes of a custom map (2026-11-11), with the new client exe, not seen in game

- [ ] In the editor first: build any map. The Compile dock log has a line `game types: ...`, and the package's
      `map.json` has a `"gameTypes"` line.
- [ ] A map with only TDM spawns still offers Team
      Deathmatch only.
- [ ] Add Primitives > Game Modes > Spawn Set: Domination and Domination Flags (3), build and deploy. The log says
      `game types detected: war dom`. The lobby offers Domination for the map, and the three flags can be captured.
- [ ] Compile dock > Map Info... > Game modes = `war conf`: the lobby offers Kill Confirmed. Say whether it plays.

## In game: fixes after the second playtest (2026-11-11 evening), not seen in game

Map: `out/agent/mp_iw8r_fix1/mp_iw8r_fix1.map` (already built; Ctrl+F7, then Local Play, F6, mp_iw8r_fix1).

- [ ] The damage zone (box at x -72..-40), the hurt trigger (x -200..-168, y 120..152) and the campfire hurt you.
- [ ] The door at y 180 is visible. It is now a hinged door: press Use near it and it swings about 100 degrees away
      from you on a hinge at its west end (x 96); press Use again and it swings shut. Say whether you can walk
      through the open doorway (whether its collision moves with it is not known).
- [ ] The ladder on the west wall (x -240, y 0) can be climbed from the floor, and the hands are on the rungs.
- [ ] The hum is heard in the south-east corner (230 -230), outside the centre box. Say whether it is also heard at
      the campfire inside the box.
- [ ] The glow panel on the east side (x 200, y -100..-36) is a soft orange glow you can see through (additive,
      unlit), from both sides.
- [ ] Bullet impacts now differ by surface: the cardboard box (paper), the juggernaut models (cloth), the traffic
      cones (plastic) and the concrete floor should not all look like stone. (A converter change; a guess until seen.)
- [ ] The hum at the campfire and in the south-east corner is clearly audible now (it was playing at 18 %).
- [ ] Reverb: inside the 400 x 400 box at the centre (now from the floor to z 160) gunshots ring like a tunnel;
      outside it they sound like open space. (In the last test the box was above head height.)
- [ ] Reflection probes now carry a picture of their surroundings (captured before every build): glossy surfaces
      near a probe should reflect the room, not flat grey. Since 2026-11-14 they are stored in colour at the
      game's own level (research 123): a weapon's metal and a glossy floor should reflect about as strongly as on
      a stock map (rebuild the map first).
- [x] Toon lines (Cinder Rail Toon, rebuilt 2026-11-14): seen in game 2026-11-14 16:15, the lines come from the scene
      depth and join cleanly (owner's screenshots).
- [x] Toon lines on Shipment (stock rebuild with the screen filter): seen in game 2026-11-14 ("the toon effect now
      works").
- [ ] Shipment with the toon zone: do objects still come and go on a second load? (First load 2026-11-14: yes; the
      game was building the zone's shader cache then.)
- [x] Haze and sky (Cinder Rail Toon: Desert haze 0.7, sky brightness 3, exposure +1): seen in game 2026-11-14, warm
      haze toward the horizon, bright sky.
- [ ] Haze details: light shafts where the sun passes gaps; glass and other see-through surfaces fogged like the
      walls behind them.
- [ ] Cinder Rail Toon without the occlusion bake (redeployed 2026-11-14 16:25): no surface or model goes missing.
      (With Occlusion bake on, surfaces came and went and models were not drawn: the bake is experimental.)
- [ ] Toon look on the first-person weapon and hands: the zone is built (out/agent/toon_vm/out); after it is installed,
      weapons and hands show the toon colours and ink lines on every map, and nothing else changes.
- [ ] Occlusion bake after the fix of 2026-11-14 (research 129): a map built with Occlusion bake on keeps all its
      surfaces and models from every camera position.
- [ ] Killstreak aircraft that fly high or start far out (gunship, cruise missile, advanced UAV, airstrike planes)
      move normally: the map's physics world is now as large as a stock map's (research 124; rebuild first).
- [ ] Killstreaks, radar: with a UAV (not the Personal Radar) enemies without the Ghost perk ping on the minimap
      and the sweep line crosses it; with the Personal Radar the drone is visible about 1,500 units above you
      (research 122 says what each should do).
- [ ] Killstreaks: the UAV aircraft is visible in the sky, and enemies are visible from the Chopper Gunner view
      (fixed 2026-11-14: the map's occlusion data now reaches as far as a stock map's, research 121; rebuild the
      map first).
- [ ] Editor: File > Edit Custom Material shows a live preview of the material on a sphere that follows the
      shader controls; Primitives > Scripted Objects > Make Hinged Door makes a door from the selected brush.
- [ ] Editor: prefabs drag from the Prefabs dock into the camera, and their pictures show the models; Tools > Make
      Editable Copy of the Current Material, Tools > Adjust Image, Tools > Techset Info, Tools > Asset Usage and Tools >
      Sound Settings open and do what their names say; File > Edit Custom Material shows the shader presets.

## In game: fixes after the owner playtest of 2026-11-11

The owner tested `out/agent/mp_iw8r_fix1` on 2026-11-11 (second playtest). Results:

- [x] Jump pad and teleporter work.
- [ ] Damage zone (acid) and hurt trigger: the trigger fires but the player takes no damage. (wrong 2026-11-11: no damage)
- [ ] The door at y 180 that slides up when you hold Use in front of it. (wrong 2026-11-11: the door is invisible in game)
- [x] The crate at 160 -96 spins and the crate at 160 -160 bobs (scripted objects behave correctly).
- [x] The glass pane is see-through.
- [ ] The campfire effect keeps burning and buzzes (fx_origin soundalias test_hum). (wrong 2026-11-11: the hum is not heard)
- [x] A wind-like bed plays everywhere.
- [x] Shadows point the same way in the editor and in game.
- [x] Patches show from both sides.
- [ ] Bullet impacts differ by surface. (wrong 2026-11-11: every impact looks like a stone impact)
- [x] The dynamic traffic cone moves when shot or pushed.
- [ ] Primitives > Scripted Objects > Make Ladder places a ladder you can see and climb. (wrong 2026-11-11: it can be
      placed, but the bottom is not climbable and the hands sit a little off the ladder)
- [ ] The Prefabs dock: prefabs can be dragged into the camera, and their pictures show what they are. (wrong
      2026-11-11: prefabs cannot be dragged, and prefabs made of models all look like boxes)
- [ ] Not reported yet: the cones' colour, the campfire's damage, reverb inside the centre box, View > Behaviour
      Preview, the material drag preview, Time of Day lining the sun up with the sky, mp_backlot2 props.

The room_trigger brush being invisible in game is expected: triggers are never drawn.

## In game: new on 2026-11-11, one test map for all of it

`out/agent/mp_iw8r_nov11/mp_iw8r_nov11.map` (already built; open it in the editor, Ctrl+F7 to deploy, then Local Play,
F6, mp_iw8r_nov11). It is the sample room with every new feature added; the positions below are map units from the room
centre (the room is about 512 x 512).

- [ ] Jump pad (trigger iw8r_jump "0 0 700", the 32 x 32 box at the centre): stepping in launches you up. (wrong 2026-11-11: does nothing)
- [ ] Teleporter (trigger iw8r_teleport "tp_out", box at x 40..72): stepping in moves you to the script_origin
      tp_out (0 0 64) and turns you its way. (wrong 2026-11-11: does nothing)
- [ ] Damage zone (trigger iw8r_damage "5 1", box at x -72..-40): 5 damage per second while you stand in it. (wrong 2026-11-11: does nothing)
- [x] Ground lighting (misc_model at 32 32 0 with ground_lighting "0.8 0.2 0.2"): the box is lit reddish, not by
      the room light. Report if it comes out blue (red and blue swapped).
- [ ] Campfire effect (fx_origin iw8r/test_campfire at 64 64 0, made in Tools > Effect Editor): flames and smoke rise
      (the smoke follows a 4-key curve), a firelight glows around it, small ember models fly up and fall. Report size,
      fade, light radius and model scale if they look wrong. (wrong 2026-11-11: no effect visible)
- [ ] Sun sprite (worldspawn iw8r_sun_sprite "16 0.5 0.45 0.25"): a sun disc in the sky where the shadows point
      from, a flare when you look at it. (wrong 2026-11-11: shadows in game are turned about 90 degrees counter-clockwise from the editor)
- [ ] Glass (pane glass_1 at y 100..104, made with Primitives > Scripted Objects > Make Glass Pane): see-through, and
      it shatters when shot. (wrong 2026-11-11: breaks, but it is opaque light blue, not see-through)
- [ ] Reverb: gunshots outside sound like open space (worldspawn iw8r_reverb iw8_ext_open_space_med_1); inside the
      400 x 400 box at the centre (zone "hall", iw8_lg_room_warehouse, interior occlusion) they echo like a warehouse
      and sounds from outside are muffled. (wrong 2026-11-11: sounds like open space everywhere)
- [ ] The map's own sounds (sounds folder): a low 110 Hz bed everywhere, a 220 Hz hum from the centre (sound_emitter
      test_hum), short chirps every 3 to 7 seconds. (wrong 2026-11-11: the chirps play; the 110 Hz bed and the 220 Hz hum were not noticed)
- [ ] Editor: View > LOD Preview switches stock models to lower LODs as the camera backs away (a stock map reference
      shows it best); Tools > Effect Editor, Tools > Cache Manager and Tools > Asset Availability Audit open; the
      Compile log ends with a "build time:" line, and building again without changes says the zones are reused.

Separate maps (also built):
- [ ] out/agent/mp_iw8r_behtest: the same jump pad, teleporter and damage zone, plus nothing else (if the combined
      map has a problem, this isolates it).
- [ ] out/agent/mp_iw8r_stockchunk: a Stock Reference to Model copy of mp_shipment shows its stock textures.
- [ ] out/agent/mp_iw8r_stockbrush: a Stock Reference to Brushes copy (2,040 brushes) of mp_shipment floor and walls
      shows its stock textures lined up as in mp_shipment, and you can walk on it.

## In game: earlier items still open

- [ ] Vision volume: a trigger_multiple with iw8r_vision "tonemapExposure=2.5" and a targetname brightens the view inside.
- [ ] Animated model: a live script_model with iw8r_anim plays the stock animation (try desktop_fan_fn_01_loop).
- [ ] Live Link > Quick match picks the map and mode with bots in the lobby; Live Link > Screenshot saves the game view.

## In game (optional, 2026-11-12 later): all proven offline, none seen in game

- [ ] Compile panel > Bake lightmaps, build, then stand in game and in View > Game Lighting (Ctrl+Alt+L) at the same
      spot: brightness, shadows, light pools and colours should match.
- [ ] A stock map rebuilt with an `iw8r_remove_world` box: the object is gone, you walk through where it was, nothing
      behind it pops out of view.
- [ ] A stock map with a new sun direction (File > Relight Stock Map Sun): shadows follow the new sun.
- [ ] Quarry with copied loot rocks, doors and an extraction site (File > Edit Placed Objects): they work like the
      originals; a deleted door is gone.

## In game (optional, 2026-11-13): Cinder Rail (Toon), rebuilt after your shots

Map project out/agent/mp_cinderrail_toon (built as mp_cinderrail_t).

- [ ] Sunlit walls keep their colour (no white wash-out), shade is a flat darker, slightly cool tone with a sharp
      border, and the ground is not grey.
- [ ] Dark ink lines run along building corners, roof edges and window frames. They are about 3 pixels wide whether
      you stand next to a wall or far from it, and meet cleanly at corners (they are drawn by the shader, not geometry).
- [ ] Colours are stronger than on the normal Cinder Rail, and shade is a cool darker tone, not black.
- [ ] No lines run across a wall where it is made of several brushes, and nothing flickers (z-fights) on the square
      structures or anywhere two brushes overlap.
- [ ] The bright sun in the sky is where the shadows come from (the sun is now 20 degrees up, like the sky's).

## In game (optional, 2026-11-13): Shipment with the toon shader's filter

The first try (a patch file `techsets_mp_shipment.fp` copied into `zone`) stopped the game with "Disc read error
[3.1]": the game takes no unsigned patch of a stock zone (research 116). Delete that `.fp` from the `zone` folder if
it is still there (the install below also removes it).

Install, game closed: start the editor again (it was rebuilt), File > Install Edited Stock Map Into Game, answer Yes
to "choose a folder", pick `E:\IW8-Radiant\out\agent\toon_ship\edit`. The dialog lists
`techsets_mp_shipment.ff`; confirm. The stock file moves to `iw8r_stock` in the game folder. To take it out: open
Shipment in the editor (File > Open IW8 Stock Map), then File > Restore Stock Map In Game.
(Or rebuild it yourself first: open Shipment, Tools > Shader Editor, toon, Use for This Map, File > Rebuild Stock
Zone, then install.)

- [ ] Shipment loads and plays (this is the first stock map whose lit shaders were replaced: if it does not load, or
      the world is black or invisible, restore and tell me).
- [ ] The world and the containers have stronger colours and the light falls in steps instead of smooth gradients.
- [ ] Characters, weapons and effects look as always.
- [ ] The first load may take longer (the game builds the changed shaders' pipelines).

## In game (optional, 2026-11-13): an own map with and without baked lightmaps

- [ ] A map built with Compile > Bake lightmaps **off**: in the game the shadowed sides of buildings are lit evenly,
      with no darker corners, as View > Game Lighting now shows it.
- [ ] The same map built with Bake lightmaps **on**: corners and covered places are darker, as View > Game Lighting
      shows it with the key on.
- [ ] A floor with a custom material that has a specular image (2026-11-14): looking toward the sun across it, the
      highlight and the overall lightening of the floor are as View > Game Lighting shows them.

## In game (optional, 2026-11-14): the occlusion bake

Only if you want the extra speed on a map with rooms. Compile > Occlusion bake on, build, play.

- [ ] The map loads and nothing is missing from any spot (walk through doors, look along streets, look over walls
      from high places, spectate).
- [ ] If anything disappears, turn Occlusion bake off and build again: the map is then as before. Tell me where it
      happened.

## What is not expected to work yet

Decal volumes, volumetrics, custom music states, custom fonts and LUI (research 36) (docs/ROADMAP.md lists every
item).
