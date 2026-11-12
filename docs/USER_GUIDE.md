# IW8 Radiant Suite: mapper's guide

A Radiant-style map editor for Call of Duty: Modern Warfare (2019), Replay 1.20.4.7623265. You edit `iwmap 4` `.map`
files, preview them with textures, lighting and a walkable player, and build them into a Replay map package with
x64-zt, then deploy it into the game.

Press **F1** in the editor for the full, searchable list of keys. This guide covers the workflows.
docs/KEYS_REFERENCE.md lists every `iw8r_` entity key with its value format and an example.

---

## 1. First start

1. **Help > First-Run Setup** opens by itself when the Replay exe is not found. Press **Detect**. It finds the game
   folder (`...\IW8\Builds\<build>`) and the exes in it, including renamed or recompiled ones such as
   `game_dx12_ship_replay_local_dw.exe`, plus ACTS, Oodle and x64-zt (`zonetool.exe`).
2. Choose the **Replay exe** the tools read (the stock `game_dx12_ship_replay.exe` is safest for ACTS) and the
   **Launch exe** you play with. Save.
3. The editor needs no proxy DLL. Custom maps are loaded by the recompiled Replay client
   (`game_dx12_ship_replay_local_dw.exe` or `game_dx12_ship_replay_dw.exe`); the stock exe cannot load them.
   Leave **Launch exe** empty and F5 picks the recompiled client in the game folder by itself.

Tool paths can be changed later in **Tools > Tool Paths**. Everything the editor exports from the game (textures,
models, sky, stock world geometry) is cached under `%LOCALAPPDATA%\IW8 Radiant Suite\IW8 Radiant Suite\`.

## 2. The window

- **Camera** (3D, D3D11) and **Grid** views (XY / XZ / YZ: Ctrl+Tab cycles). Docks can be dragged, tabbed and floated;
  View > Reset Panel Layout restores them. Every dock can be shown or hidden from the **View** menu.
- **Entity** inspector (keys with help text from the entity definitions; lights have a colour/radius/cone group),
  **Surface** inspector (fit, natural, justify, shift/scale/rotate), **Layers**, **Materials** and **Models** browsers,
  **Compile**, **Replay Log**, **Live Link** and **Console** (every status line, compile line and warning, with
  filters). Warnings and errors also pop up as short notifications (View > Toggle Toasts).

Camera: hold right mouse to look, W A S D to fly, mouse wheel for speed. **View > Camera Mode** switches Textured, Flat,
Wireframe, Fullbright and Normals. **Ctrl+L** turns on the lit preview (sun, light entities and sun shadows; the yellow
arrow points at the sun).

## 3. Building geometry

- **Brushes**: drag in a grid view to create a box with the current material (toolbar field). Drag inside a selection to move it,
  drag outside it to resize the nearest faces. Clipper (toolbar) for 2- and 3-point clips (Enter / Shift+Enter keep one side, Ctrl+Enter
  both), CSG Hollow, Subtract (Shift+U) and Merge. **E** in a grid view toggles edge drag; vertex mode for vertices.
- **Primitives** menu: stairs, arch, cylinder, cone, sphere, ramp, and **Game Modes** (Domination flags, Hardpoint
  zones, Search & Destroy sites, CTF flags, spawn sets; trigger sizes are placeholders to adjust).
- **Patches**: Shift+P creates a patch; the Patch menu inserts/deletes rows and columns, caps, welds, inverts and
  generates terrain (Create Terrain, Noise, Smooth). **Terrain Brush** (Patch menu) sculpts in the XY view: Raise
  (Shift lowers), Smooth, Flatten, Noise; Radius Larger/Smaller. Patches are drawn from both sides. To turn that off,
  set worldspawn `iw8r_patch_one_sided` to 1.
- **Brush flags**: Ctrl+M makes a brush detail (does not seal), Brush > Make Structural, Toggle Non-Colliding.
- **Tool materials**: Brush > Tool Material applies caulk, nodraw, clip, player/weapon/AI-sight clip, **ladder**
  (climbable), water (non-solid; Replay has no swim volumes), trigger, hint, skip or sky.
- **Region** (View > Region): limit the views to the selection or the visible XY area. File > Save Selected / Save
  Region write those parts to a new map.
- **Layers** and find/replace materials (Ctrl+F) and select connected (Ctrl+E) work as in Radiant.

### Scripted objects

- **Primitives > Scripted Objects > Make Ladder** (climbable brush with a ladder model): makes a thin climbable slab
  through the rungs and a ladder model with no player collision (`iw8r_no_collision`, a live `script_model`). The
  model is unscaled and set to the nearest stock height: 72, 144 or 160 units. Bullets still hit the model. Not seen in
  game yet.
- **Primitives > Scripted Objects > Make Glass Pane**: select the thin brush or brushes first. It turns them into a
  `script_brushmodel` named `glass_N` with the built-in blended `iw8r_glass` material. Any custom material with
  `"blend": true` also works. The build replaces it with Replay's breakable glass.
- **Primitives > Scripted Objects > Make Scripted** makes a live object for the map script.

### Prefabs

A prefab is a saved group of objects that a map places as one object. There are two forms.

- A **prefab bundle** is one folder named `<name>.iw8prefab`. It holds everything that belongs to the object: its
  brushes and entities with all their keys (so behaviour such as a door, a jump pad, an effect or a sound travels with
  it), your own materials, models, sounds and effects that it uses, an optional script, a title, a description and
  search tags. Stock assets are only named, never copied. `docs/PREFAB_BUNDLES.md` describes the folder.
- A **plain prefab** is the old form: one `.map` file with brushes and entities only. It still works as before.

**Two bundles ship with the editor** (not seen in game yet):

- **Jump pad with effect and sound**: a manhole cover model, a trigger that throws the player upward, a pulse effect and
  a hum. One parameter: the launch speed.
- **Stock door, painted wood, single**: the wooden door of Talsik Backlot. It is built as
  the game's real door, copied from the stock map (not seen in game yet). With Edit > Preferences > Real stock
  scripted objects turned off it has the badge FALLBACK and is built from the stock door's own model and sounds plus
  a script. The build log says which one was built.

**The Prefabs dock** (View > Prefabs) lists the bundles first, then every plain prefab. Each has a picture: the
bundle's own `thumb.png` when it has one, else a picture drawn from its contents (triggers and clips are drawn as dashed
outlines). A bundle has a small BUNDLE tag. Hold the mouse over a bundle to read its description, tags and parameters.
The filter box finds a bundle by its title, a tag, its category or its id. Badges on a picture:

- **FALLBACK**: the build makes the bundle's second set of contents, because a feature the bundle needs is missing.
- **LIMITED**: a needed build feature is missing and the bundle has no second set of contents.
- **MISSING**: a stock asset the bundle names is not in the editor's catalogue.
- **CANNOT PLACE**: the bundle cannot be read (the tooltip says why).

**Place a prefab**: drag it from the dock into the Camera or a Grid view, or double-click it to place it on the surface
at the middle of the Camera view. While you drag over the Camera, the prefab itself follows the cursor; leaving the view
takes it away again. A placed prefab is one `misc_prefab` entity: one click selects it, the gizmo moves and turns it.
Every placement of a bundle gets its own short id (key `iw8r_pid`, for example `p3`). The build uses it to keep the
names inside two placements apart, so two doors never open each other. Clone, Copy and Paste, and the Arrange
duplicates give each copy a new id. Do not edit the id by hand.

In the Camera, the brushes of a placed prefab are drawn in flat colours and each model or other point entity inside it
is drawn as a small box. The picture in the dock shows the real models.

**Parameters**: select a placement of a bundle and open the Entity dock. Above the key list it shows the bundle's title
and description, its badge with the reason, and one field per parameter with its range and default (the jump pad has
"Launch speed"). A value you set is written as one key on this placement only, and Undo takes it back. The button
**Default** removes the key again. The buttons **Edit Prefab**, **Open Bundle Folder**, **Explode** and **Make Unique**
are there too. File > Prefab Parameters still lists every key of every entity inside any prefab.

**Save Selection As Prefab** (File menu): select the brushes and entities, then fill in the title, description,
category and tags. The id is made from the title; it names the folder. The dialog lists what will travel with the
prefab: the brushes and entities, your own material, model, sound and effect files that are copied into the bundle,
the stock assets it needs by name, the names that are kept apart per placement, and linked objects you did not select
(tick one to add it). The bundle is written into the first prefab folder of Edit > Preferences, or into `prefabs`
beside the map when no prefab folder is set. The selection is then replaced by one placement of the new prefab (untick
the box to keep the loose objects). Saving again under the same id asks before it replaces the bundle; parameters,
script and fallback settings written into `prefab.json` by hand are kept. A picture (`thumb.png`) is saved when every
model in the prefab could be drawn; otherwise the dock draws the picture itself. Parameters, a script and fallback
contents are written into the bundle folder by hand for now (`docs/PREFAB_BUNDLES.md`).

**Other commands** (File and Edit menus):

- **Explode Selected Prefab** replaces a placement by its loose contents. For a bundle the names get the placement id
  in front (`p3_door`), as in the build, so the links keep working. A bundle's script does not run for loose objects.
- **Enter Prefab** (Ctrl+Shift+E) opens the prefab's map to edit it; for a bundle that is its `prefab.map`.
  **Leave Prefab** (Ctrl+Shift+Q) returns, and every placement shows the change.
- **Open Bundle Folder of Selected Prefab** shows the folder in Explorer.
- **Make Selected Prefab Unique** copies the prefab into `prefabs` beside the map under a new id and points this one
  placement at the copy. The map must be saved first.
- **Save Selection As Plain .map Prefab** writes the old single-file form. **Insert Prefab** places a `.map` file or a
  bundle (pick the `prefab.json` in its folder).

**Building**: the Compile panel passes the bundles' own material, model, sound and effect folders to the build. The
log has one line per bundle, for example `prefab bundle 'jump_pad' (jump_pad.iw8prefab): 2 placement(s), main contents`,
and a warning when fallback contents were built or a named stock asset is not in the catalogue. While you edit, the
materials and models of every bundle the map places are available like your own.

Section 12 has the playtest notes on prefab pictures and dragging.

## 4. Materials and textures

- **Stock materials**: type or browse any stock name (`tm2co/me_curb_stone_01`). The editor exports its images with
  ACTS on first use, so a texture can take a few seconds to appear. Builds use the stock colour image
  (**Compile > Stock textures** on the Compile panel). Cut-out materials (chainlink, foliage) are recognized and built
  alpha-tested. A material whose image is not cached yet builds grey once; the next build has it.
- **Custom textures**: File > Import Texture writes a `<map dir>\materials\*.iw8mat.json` definition (colour, normal,
  specular, alpha test, surface type for footsteps).
- **Surface type**: the surface type sets the footstep and bullet impact sounds. Stock materials use the type the game
  gives them. Custom materials take their type from File > New / Edit Custom Material.

### Materials on models

1. Open the **Materials** dock (View > Materials) and drag a material over a model in the Camera view.
2. While you drag, the model under the cursor shows the material as a live preview. Move off it and the model goes
   back to its own material. Drop the material to keep it (one undo step).
3. The drop sets the key `iw8r_material` on the model. The build makes a copy of the model with that material on
   every surface. This works for stock models and for your own models (shape models from Primitives, imported files).
4. You can also select one or more models (and brushes) and click a material in the Materials dock: the material is
   applied to everything selected, in one undo step.

### Editing materials

Use these commands to change a material, not only to place one. Every command works on the current material.

- **File > New Custom Material** makes a new definition in the map's `materials` folder. **File > Edit Custom Material**
  opens an existing one in the Material Editor. A definition sets the colour image, the normal and specular images, the
  surface type (footstep and bullet sounds), cut-out (alpha test) and two-sided.
- **Shader presets** in the Material Editor set a common look in one step: opaque, cut-out, blended, additive glow,
  multiply, decal, unlit sign and emissive. Pick one, then adjust the controls below.
- **Blend mode** (alpha, additive or multiply): alpha is for glass and see-through decals, additive adds light for glows
  and light shafts, and multiply darkens for stains.
- **Unlit** takes no light from the map. Use it for signs and screens.
- **Emission** makes the surface glow by a factor from 0 to 64.
- **Tint** multiplies the colour image, one value each for red, green and blue (0 to 4).
- **Decal sort order** sets which decal layer draws on top of another. Higher draws on top.
- **Depth offset** (0 to 2) pulls a decal off the surface under it.

Commands in the Tools menu:

- **Tools > Make Editable Copy of the Current Material** copies a stock material's colour, normal and specular images
  and a definition into the map's `materials` folder, with the tint applied to the copy. The copy opens in the Material
  Editor so you can change its shader state.
- **Tools > Adjust Image** changes a colour image: brightness, contrast, saturation, hue, gamma, tint, invert, channel
  swap and resize. It can also make a normal map from a height image. A preview shows the result before you save a new
  PNG.
- **Tools > Techset Info** explains the current material's technique set (the shaders the game uses for it). The first
  look exports the technique set once through ACTS and lists its techniques, shader names and arguments. The state
  words are not decoded yet.
- **Tools > Replace a Material in the Whole Map** gives every face and patch that uses one material the current
  material. Model keys follow. It is one undo step.
- **Tools > Asset Usage** lists what the map uses of each kind (materials, models, sounds, effects) with counts. It also
  lists the map's own files that nothing uses.

## 5. Models

- Place a `script_model` or `misc_model` and set its `model` key (Models browser). Stock models are exported with ACTS
  for the camera view and built into the map as textured copies (`iw8r_stock_*`).
- **Custom models**: File > Import Model (OBJ) copies the model next to the map.
- **Collision**: every placed model without a `targetname` gets an invisible solid box (players and bullets) fitted to
  its size and angles. A `script_model` with a `targetname` is treated as scripted and gets none. Check them with
  **View > Show Compiled Collision** after a compile (solid cyan, player clip orange, ladders green).
- **Stock map references**: when you open a stock map, its props fill in over a few minutes. Each missing prop model is
  fetched in batches and the props are drawn as they arrive. mp_backlot2 took about seven minutes on first open.
- Models can move without scripting. See section 11.

### Replacing one material

- **Tools > Replace One Material of the Selected Model** sets the key `iw8r_mat:<material>` on the selected model. The
  material after the colon is the slot to replace, and the value is the current material. Use it when a model has
  several materials and only one should change. Model Info lists the slots, each with a Replace button. Slot keys win
  over `iw8r_material` (section 4), which changes every slot.
- The camera shows the change, and the build makes a copy of the stock model with that slot replaced. Not seen in game
  yet.

## 6. Lighting and atmosphere

- **Lights**: `light` entities (omni and spot) become primary lights; `reflection_probe` entities become probes.
  A spot with a cone of 150 degrees or more is built as an omni light. The build log notes it.
- **Compile > Sun & Sky**: sun pitch/yaw/colour and a stock sky (`sky_mp_*`; exported and converted at build time).
- **Compile > Time of Day**: a preset sets the sky. With **Compile > Aim the Sun When the Sky Changes** on, the sun is
  also aimed at the sun painted in that sky, in the same undo step.
- **Compass**: the compass north is the top of the XY view (+Y). Builds write worldspawn `northyaw 90` unless the map
  sets its own, so the game's north matches the editor's.
- **Compile > Atmosphere**: exponential fog (start, halfway distance, colour; previewed in the camera, View > Fog
  Preview) and a film grade. Exposure is in **Map Info** on the Compile panel, with title, description, compass and
  minimap settings.
- **Compile > Vision Editor** lists all 218 vision fields of the game by name, with type and default. Filter by text or
  group. The values you set are the `iw8r_vision` overrides of the map, or of the selected vision volume trigger. Fog,
  film grade, exposure and the grading LUT keep their own dialogs. Only 87 fields have readable names; the rest are
  listed by token. Not seen in game yet.

### Decals

A decal is a picture projected onto the surfaces inside a box: a stain, a painted marking, a scorch mark.

1. Place the point entity `iw8r_decal` where the picture should be.
2. Set `angles` so that the entity's forward direction points into the surface. For a floor use `90 0 0`.
3. Set `size` to the box: depth (along the projection), then width and height. The default is `8 64 64`.
4. Set `material` to a material of your custom library (the alpha of its picture is the shape of the decal) or to
   the name of a stock decal material.
5. Optional keys: `opacity` (0 to 1), `feather` (soft fade at the front and back, 0 to 1), `draw_order` (-4 to 3,
   higher on top), `uv_rotate`, `uv_scale`, `uv_shift`.

The build log says how many decals were written. A material of your library is built a second time in the form
decals need (the build names it `<material>_dcl`); you do not have to prepare anything. Decals need a converter
with the decal producer (the one this project uses has it since 2026-11-11). Not seen in game.

### Editing a stock map

1. File > Open IW8 Stock Map opens a stock map: its entities are the document, its world and props are shown as a
   reference.
2. File > Edit Compiled Spawns turns the spawn points into entities you can move, add and delete.
3. Add things: a `script_model` with any stock model works even when the model belongs to another stock map. The
   rebuild copies that model with its materials and textures from the map that has it, and the Console lists what
   is copied.
4. File > Rebuild Stock Zone writes the changed map in a few seconds and checks it with the game's own loaders
   (ACTS). The Console shows the result and the folder. The game folder is only read.
5. File > Install Edited Stock Map Into Game puts the edit into the game. It copies small patch files (`<map>.fp`)
   beside the stock zones after showing you the list; the stock zones themselves are not changed, and a stock
   patch of the same name is saved first. Close the game before installing. File > Restore Stock Map In Game
   takes the edit out again.

6. To hide stock props: draw a brush around them, select it and use File > Hide Stock Props In Selection. The props
   leave the view and the rebuilt map draws them at size zero; their collision is taken out too (on the small maps;
   a Warzone map keeps prop collision in its collision tiles, which stay as they are). File > Show All Stock Props
   brings them back. The helper brush is not built into the stock map (the Console says so); delete it when you
   are done.

7. Loot points (Warzone maps): File > Edit Loot Points turns the map's loot points into green `iw8r_loot_point`
   entities (Quarry has 2,924, Verdansk 23,446). Move, delete or copy them; a new point needs only an origin. The
   rebuild writes them back and the Console says how many. A point you move out of its area finds its new cluster and
   zone by position; `loot_cluster` `cache` makes a supply box point. If you undo back to no loot point entity, the
   map keeps its own loot; worldspawn `iw8r_no_loot` `1` is the way to remove all of it.
   New loot areas: draw a brush (or several) where the area should be, make it an `iw8r_loot_cluster` (a group of
   points) or an `iw8r_loot_zone` entity (an area the loot tables know by its `name`; reuse one of the map's own zone
   names such as `map_quarry`). Loot points inside it then belong to it. The map's own areas win where they overlap.

8. Vehicles, buy stations and other placed things of a Warzone map: vehicle spawns are ordinary entities
   (`script_struct_mp_atv`, `_tacrover`, `_jeep`, `_cargotruck`, `_littlebird`; Quarry has 183). Copy one, move the
   copy, or delete some; the rebuild writes them like any other entity.
9. Big Warzone maps (Quarry, Verdansk) keep most of their world in streamed tiles: the reference first shows only
   what the map zone holds. Fly to the area you work on and use File > Load Streamed World Around Camera: the tiles
   near the camera are read (a few seconds each, kept for next time) and drawn.
10. Buy stations: File > Edit Placed Objects turns the buy stations of the map into `scriptable` entities (Quarry and
   Verdansk have 120 each). Move one, or copy one and move the copy for an extra station; the rebuild writes it and the
   Console counts what was moved and added. A station cannot be removed (move it out of the way), and doors and other
   objects with moving parts cannot be edited this way yet.

11. New brushes: draw brushes and patches as usual (in the worldspawn or grouped in func_groups). File > Rebuild
    Stock Zone builds the visible ones as one model per group (with collision) and places it where you drew it; the
    first part of the rebuild is a normal build of a small helper map, so it takes a minute or two. Brushes made only
    of tool materials (caulk, clip, nodraw), such as the helper brush for hiding props, are not built. Not seen in
    game.

### Stock scripted objects (scriptables)

A scriptable is one of the game's own objects with built-in behaviour. Place the point entity `scriptable` and
set its `scriptable` key to the name of the stock definition. Optional keys: `angles`, `targetname`, `target`,
`script_noteworthy`, `script_linkname`, `script_linkto`.

There are two kinds:

- **Always loaded**: `hardpoint_chevron`, the marker of a Hardpoint zone, and `scriptable_auto_ascender`. They are
  placed by name and always build.
- **Warzone objects**: the buy station `br_plunder_box` (also `brloot_rock`, `brloot_ammo_762`, `brloot_ammo_919`,
  `brloot_assassination_tablet`). They live in the game's Warzone file, which a custom map does not load, so the build
  copies them into the map (Edit > Preferences > Real stock scripted objects, on by default). It accepts them only on
  a map made for the Warzone game type (Tools > Game Modes).
- **From a stock map**: doors, traffic cones, cars, tyres, radios, signs, fences and other world objects (502
  definitions). Their
  definitions live inside stock map files. The build copies the definition with its models, collision and
  materials from the stock map into yours (it reads the game folder, a few seconds per stock map). The switch for
  this is **Edit > Preferences > Real stock scripted objects**, on by default; with it off such an entity is left
  out and the log says so.

Nothing built this way has been seen in game yet. The built data is checked against the stock map's own (the
definition read back from your map equals the stock one). If a map with such an object does not load, turn the
switch off and build again: that gives the previous behaviour back.

`data/asset_catalog/iw8_scriptables.tsv` lists all 837 stock definitions, where each lives and whether it can be
built (column `verified`). Check Map lists every scriptable that would be left out, with the reason.

### Effects

Place an `fx_origin` entity and set `fx` to a stock Replay effect name (`vfx/...`), optional `angles`, and optional
`delay` (seconds between plays; leave it out for one persistent effect, e.g. a looping fire). The build writes them
into the map script, which x64-zt compiles when the **gsc_tool** tool path is set (Tools > Tool Paths). Effects
load from Replay's always-loaded common zones, so an effect that lives only in another map's zone will not play.

**Your own effects** are made in **Tools > Effect Editor**. Build the effect from elements: an image (or a soft
default particle), blended or additive, looping or one-shot, size and colour from start to end, and so on. Save it
with the map; the file goes in the map's `fx` folder. Then press **Place in Map**. It puts an `fx_origin` with that
effect at the middle of the view. Compile to build it as a Replay effect. The build makes each element a burst. A
looping effect is played again by the map script about once a second. The effect editor is not verified in game.

**Sound and damage on an effect** are keys on the `fx_origin`:

- `soundalias` plays a sound at the effect. It can be one of the map's own sounds or a stock alias. It loops unless
  `sound_loop` is 0.
- `iw8r_damage` makes the effect hurt: `<amount> [radius] [interval seconds]`. A player inside the radius (48 by
  default) takes the damage every interval (1 second by default). No trigger is needed.

### Sound settings

Each of the map's own sounds can have its own settings. **Tools > Sound Settings** edits them. The settings live in a
file next to the sound, with the same name and the extension `.iw8snd.json` (for example `fire_loop.iw8snd.json` beside
`fire_loop.wav`). The build reads the file. Every field is optional:

- `volume`: 0 to 1.
- `dist_min` and `dist_max`: the distance range of the sound, in map units. `dist_max` cannot be below `dist_min`.
- `loop`: `true` or `false`.
- `two_d`: `true` makes it a 2D sound, with no position in the world.

docs/KEYS_REFERENCE.md has the table for this file.

## 7. Walking the map (play preview)

Press **P** in the camera. Movement follows Replay 1.20's values: W A S D, **Space** jump (at a ledge: mantle; in the
air: grab a high ledge; on a ladder: push off), **Shift** sprint, **C** crouch (while sprinting: slide), **Z** prone,
**N** noclip, **Esc** leaves. Ladders are climbed by walking into them; look down to climb down. Collision uses the
same rules as the build (clips block, triggers and weapon clips do not).

## 8. Compile, deploy, play

1. **F7** builds: extraction (the editor's compiler), x64-zt `buildextraction` (zones), ACTS validation. Shift+F7 runs
   the extraction only. The Compile dock shows each step; warnings are listed, not fatal.
2. **Ctrl+F7** builds and deploys into `mods\mw120r\maps\<id>` of the game folder. Deploying works while Replay is
   running, except for the map that is currently loaded. The previous package is kept in `.proxy\backups`
   (Compile > Undo Last Deploy).
3. **F5** builds, deploys and puts you in the map (see "F5 from a closed game" below). You can also load the map by
   hand in Replay, from a private match lobby you host.
4. **Check Map** (Tools) lists problems before a build (missing spawns, entity keys, leaks: Tools > Leak Check draws
   the leak path).

### Game mode presets and Warzone

Tools > Game Modes lists mode presets. A mode in this game is a game type script plus settings (dvars): Warzone Trios,
Quads, Stimulus Solos and Blood Money are all the script `br` with different settings. A private match applies no
playlist settings by itself, so a preset brings them:

- **Use for F5**: F5 then starts that game type, and the editor sends the preset's settings before the match starts.
- **Carry in this map**: the map itself sets them at every start (worldspawn keys `iw8r_mode_dvars` and `iw8r_br`).
  Use this for a map that is meant for one mode.
- **Save as my preset** keeps your own mode: change the id, title, game type and the `name=value` lines first.
- **Rules** change how the game type plays, for every player at each spawn: `health=50` (max health), `speed=1.2`
  (move speed), `weapon=iw8_pi_golf21_mp` (only this weapon instead of the loadout), `ammo=infinite`. Separate them
  with `;`. Rules work only when the map carries them (Carry in this map, worldspawn `iw8r_mode_rules`): the map's
  own script applies them after the game type has started.

For a small map of your own in the Warzone game type: pick "Warzone on a small map", press Carry in this map, then
Insert Warzone start entities (eight start spawns and a prematch area at the 2D view centre; move the spawns onto the
floor). The build then lists the map for `br`. Such a map has no circle, no gulag and no plane. For loot, place
`iw8r_loot_point` entities where items should lie (`loot_cluster` `cache` for a supply box point); the build writes
them into the map's loot table, in one loot zone named by worldspawn `iw8r_loot_zone` (default `map_quarry`: the loot
tables choose the items by the zone name). For a buy station, place a `scriptable` entity with `scriptable` `br_plunder_box`.
For vehicles, place `script_struct_mp_atv`, `_tacrover`, `_jeep`, `_cargotruck` or `_littlebird` entities (their keys
are filled in); the build copies the stock vehicle and its model into your map. Not seen in game.

### Big maps: collision in tiles

A Warzone-size map keeps its collision the way the stock big maps do: cut into the game's grid of 32 x 32 tiles of
8192 units. A client loads the tiles near the player; a host loads the combined package. The editor makes the tiles
by itself from the same brushes and models.

- Edit > Preferences > Build: collision. Automatic (the default) uses tiles for a map wider than 20000 units on X or
  Y and one piece for anything smaller. You can also pick always one piece or always tiles.
- A map can decide for itself: worldspawn key `iw8r_collision` with `auto`, `one` or `tiles`.
- The build log says which layout was used. A tiled map's package has 1,024 more small files and one combined
  package; they are copied with the map.
- The private client currently loads only the combined package of a custom map; that is enough to play. Not seen in
  game.

### Game modes of your map

The game offers a custom map in the modes that its `map.json` lists. The editor writes that list after every
build. A mode is listed when the map has the spawns and objects for it:

| Mode | The map needs |
|---|---|
| Team Deathmatch (`war`) | 2 `mp_tdm_spawn` and one start for each team |
| Free-for-all (`dm`) | 2 `mp_dm_spawn` |
| Domination (`dom`) | Domination spawns, both starts and the three flags |
| Search and Destroy (`sd`) | attacker and defender spawns, two bomb sites, the bomb and its pickup trigger |
| Capture the Flag (`ctf`) | CTF spawns, both starts, and flag, trigger and zone for both teams |

Primitives > Game Modes inserts each of these sets. The build log shows the result in a line that starts with
`game types:`. To list other modes yourself, open Map Info... in the Compile dock and fill in **Game modes**, for
example `war conf`. The editor cannot check modes it does not know.

This needs a game client that reads the list. A client without it offers Team Deathmatch only. Not seen in game yet.

### F5 from a closed game

Press F5 with Replay closed. The editor builds the map and deploys it. Then it does the rest by itself:

1. It picks the game exe. Tools > Tool Paths "launch" is used when it is set. When it is empty, the editor looks in
   the game folder and takes `game_dx12_ship_replay_local_dw.exe`, then another recompiled client, then the stock exe
   (with a warning: the stock exe cannot load custom maps).
2. For the local client it checks that the local online-services host answers at `http://127.0.0.1:8085/health`.
   If not, it starts the host and waits up to 60 seconds for it.
3. It starts the game, selects Multiplayer, hosts a private match, sets your map and Team Deathmatch, and starts
   the match with no countdown.

While this runs, a dark card with the map name covers the game window. It closes when the map loading screen
begins, so the loading screen is the first thing you see of the game. The card never takes the keyboard or the
mouse. Every step is listed in the Compile dock with the time since F5.

Options: Edit > Preferences has the F5 rows (game mode, bots, cover card, host check, host command, client start).
They are stored in `IW8Radiant.ini`, section `[devmap]`:

| Key | Default | Meaning |
|---|---|---|
| `gametype` | `war` | The game mode F5 starts |
| `cover` | `true` | Show the cover card |
| `hostCheck` | `true` | Check the local host before the game starts |
| `hostCommand` | the Host-Local-Server script with `-NoLaunch` | What starts the host. `-` means never start it |
| `hostHealth` | `http://127.0.0.1:8085/health` | Where the host answers |
| `startRequest` | `true` | Ask a client that supports it to start the map by itself |
| `bots` | `0` | Bots for a client that supports the start request |

This has not been seen in game yet.

## 9. Live Link

The Live Link dock talks to a running game:

- **Game link** works with any exe in the game folder, including recompiled ones (it reads their address map):
  **Read camera -> editor**, **Move player to editor camera**, **Follow game** (the editor camera copies your in-game
  view live) and a console line for any command.
- **Quick match...** starts a private match on the deployed map in a mode you pick, with bots. From a lobby you host
  it starts at once with no countdown. From the Multiplayer menu it hosts a private match first and starts when the
  lobby is up. **Load map in game** starts the deployed map from a lobby you host.
- The sections for the old mw120r proxy (status, select the deployed map, `set` dvars) appear only when its DLL
  (`XInput9_1_0.dll`) is in the game folder. Without it they are hidden and nothing is written for it.

### Debug overlay in the game

The Live Link dock has a **Debug overlay** row. It tells the game to draw what your map contains while you play it:

| Preset | What the game shows |
|---|---|
| Where am I | A panel with your position in map units (the same numbers as in the editor), the map, entity counts and the audio zone |
| Entities and triggers | A label and an axis marker on each entity near you (class, name, model, distance) and a wire box on each trigger |
| Entities and crosshair | The same, and what the crosshair is on: surface type, flags and the entity hit |
| Everything | A box on every entity that has bounds, players and AI included |

- The filter box limits the labels to entities whose class, name, target or model contains one of the words. A word
  that starts with `-` hides matches. **Selected** filters to the entity selected in the editor.
- **Compare with game** asks the game for its entity list and shows which named entities of your map are missing in
  the running match, at another place, or there in a different number. Its window can move the editor camera to
  where you stand in the game.
- In the game console the same things are the dvars `iw8r_mapDebug` (a number: 1 labels, 2 trigger boxes, 4 panel,
  8 crosshair, 16 all boxes, 32 players; add them up), `iw8r_mapDebugDist`, `iw8r_mapDebugMax`,
  `iw8r_mapDebugFilter`, and the command `iw8r_mapDebugDump`.

You must host the match, and the game client must be one built with the overlay. Not seen in game yet.

## 10. When something goes wrong

- **Crash**: the editor writes a report (stack with file:line) and a dump to
  `%LOCALAPPDATA%\IW8 Radiant Suite\IW8 Radiant Suite\crashes\`; the next start names it in the status bar. Send the
  `.txt` with your report.
- **Autosave**: unsaved changes are snapshotted every 5 minutes to
  `%LOCALAPPDATA%\IW8 Radiant Suite\IW8 Radiant Suite\autosave\` (`<name>.autosave.map`); open the snapshot to recover.
- **Compare with the last save**: **File > Compare With Last Saved Backup (.bak)** compares the open map with the
  backup next to it, with no file dialog. Use it to see what changed since the last save.
- **Script errors**: double-click a line in the Compile log or the Replay Log that names a `.gsc` line. The **Script**
  dock (View > Script) opens on that line. Lines of the generated map script are mapped back to your own script. A
  message that points into the generated part says so. Build checks name the line in your own file first.
- **Grey textures or missing models in a build**: the export cache was still filling. Build again.
- **Deploy refused**: the message names the file that is in use (usually the map Replay has loaded: load another map
  first). An Explorer window open on the map folder no longer blocks a deploy.
- **Logs**: View > Console in the editor; Compile > Show Replay Log for the game's own logs.

## 11. Behaviours and triggers without scripting

You can make models move and make triggers act by setting keys in the Entity inspector. The build writes the map script
for you. Each key is listed with its format and an example in docs/KEYS_REFERENCE.md.

- **Spin and bob** (`iw8r_spin`, `iw8r_bob`) on a `misc_model`, `script_model` or `script_brushmodel`. A `misc_model`
  with one of them becomes a live model, and a missing `targetname` is filled in. Example: `iw8r_spin` = `4` turns
  the model once every 4 seconds. `iw8r_bob` = `16 2` moves it up 16 units and back every 2 seconds.
- **Lift and door** on a `script_brushmodel` that has a `targetname`. `iw8r_lift` = `128 2 2` moves it up 128 units
  over 2 seconds, waits 2 seconds, and comes back down. For a door, `iw8r_door` = `rotate 90 1 3` (or `slide ...`)
  opens it. The door needs a `trigger_use` or `trigger_multiple` with its own `targetname`, whose `target` is the door.
  A moving brush model is drawn by a generated model that the build links to it, because the brush's own surfaces did
  not show in game. Key `iw8r_brush_visual` = `0` on the brush model turns that off. Not seen in game yet.
- **Teleport, jump and damage** on a `trigger_multiple` that has a `targetname`. `iw8r_teleport` takes the
  `targetname` of a `script_origin` as its destination. `iw8r_jump` = `0 0 700` launches a player who touches it.
  `iw8r_damage` = `10 1` hurts a player inside it every second. Hurt triggers and damage zones now deal their damage
  through the game's radius damage call (changed after the second playtest). Not seen in game yet.
- **Damage on an effect** is set on the `fx_origin` (section 6). It needs no trigger. Not seen in game yet.

### View > Behaviour Preview

Turn on **View > Behaviour Preview**. The camera then moves the models that the build moves in game: models with
`iw8r_spin` or `iw8r_bob`, and brush models with `iw8r_lift`, `iw8r_bob`, `iw8r_door` or a yaw `iw8r_spin`. Doors open,
hold and close in a loop. Your own map script is not previewed. Use the preview to check the motion before a build.

### Triggers for the map script

The map script tests players against the trigger volumes itself. A script gets a trigger with
`iw8r_trigger("name")`. That call returns the object that the build notifies with `"trigger"` and the player's entity,
so your script can wait for it:

```
trig = iw8r_trigger("gate_trig");
trig waittill("trigger", player);
```

`iw8r_in_trigger(player, "name")` tells you whether a player is inside the named trigger. Write your own code in the
**Script** dock (View > Script). The build appends it after the generated map script. Use `iw8r_trigger` instead of
`getent` for triggers.

### Hinged doors

Select the door brush and choose Primitives > Scripted Objects > Make Hinged Door. The door hangs on a hinge at one end of its longer side. In game, press Use near it: it swings away from you and stays open until someone uses it again. It needs no trigger. The key `iw8r_hinge` moves the hinge: `far` takes the other end, and `x y z` names the point. The key `iw8r_door` holds the swing: `hinge <angle> [time]` stays open, `rotate <angle> [time] [hold]` closes by itself, `slide <x> <y> <z> [time] [hold]` slides. View > Behaviour Preview shows the swing in the camera. Not seen in game yet.

### Live material preview

File > New Custom Material and File > Edit Custom Material show the material on a sphere, a plane or a cube. The picture follows every control as you change it: blend mode, unlit, emission, tint, two-sided and alpha test. Drag the picture to turn the object, move the sun with the sliders, or tick Spin. It uses the editor's own renderer, so it is close to the game but not identical: it has no shadows, no reflections, no fog and no colour grade.

## 12. Known limits

Not verified in game for every feature (see docs/MILESTONES.md). Stock xmodels are rebuilt as copies, not referenced
by name. Lightmap baking is not available yet.

The owner's playtest of 2026-11-11 left these open. docs/ROADMAP.md (fix queue F.1 to F.18) has the details:

- Hurt triggers and damage zones fired but did no damage in the second playtest. They now use the radius damage call.
  Not seen in game yet.
- A looping sound of the map's own, played from an effect, was not heard in game, though the ambient bed was. Unresolved.
- Reverb is not confirmed in game.
- Reflection probes make no visible change in game.
- Only Team Deathmatch can be picked in the game's lobby. Other modes say the map does not support them. Live Link >
  Quick match starts other modes directly.
- Bullet impacts look alike on every surface in game. The build tags each surface correctly; the cause is not known yet.
- Killstreak visuals: the UAV shows no sweep or pings, and players are invisible in the chopper gunner view.
- Prefabs made of models showed as boxes, and dragging a prefab did not work. Both were changed after the second
  playtest. Not seen in game yet.
- A ladder's bottom was not climbable in game, and the hands sat a little off the ladder. The ladder was rebuilt after
  the second playtest (section 3). Not seen in game yet.
- Additive and multiply blending are not shown in the editor camera. The camera does show a custom material's tint.
  Gloss scale is not done yet. Not seen in game yet.
- Make Editable Copy, Adjust Image, Techset Info and Asset Usage are tested offline only. The Techset export has not
  been run on a real cached material by a test.
