# IW8 Radiant User Guide

IW8 Radiant is a map editor for Call of Duty: Modern Warfare (2019), game build Replay 1.20.4.7623265. This guide
describes the main workflows. Press **F1** in the editor for the complete list of keyboard shortcuts, and see the
[entity keys reference](KEYS_REFERENCE.md) for the keys IW8 Radiant adds to entities.

## Contents

1. [Getting started](#1-getting-started)
2. [Navigating and selecting](#2-navigating-and-selecting)
3. [Building geometry](#3-building-geometry)
4. [Materials and textures](#4-materials-and-textures)
5. [Models](#5-models)
6. [Prefabs](#6-prefabs)
7. [Entities and gameplay](#7-entities-and-gameplay)
8. [Lighting and atmosphere](#8-lighting-and-atmosphere)
9. [Effects and sound](#9-effects-and-sound)
10. [Previewing your map](#10-previewing-your-map)
11. [Building and playing](#11-building-and-playing)
12. [Editing stock maps](#12-editing-stock-maps)
13. [Warzone](#13-warzone)
14. [Live Link](#14-live-link)
15. [Using the editor from a browser](#15-using-the-editor-from-a-browser)
16. [Troubleshooting](#16-troubleshooting)

## 1. Getting started

### First-run setup

The first time you start IW8 Radiant, **Help > First-Run Setup** opens. Press **Detect** to find the game folder, or
browse to it yourself, then choose:

- **Replay exe**: the game executable IW8 Radiant reads the game's data from. The stock
  `game_dx12_ship_replay.exe` is recommended.
- **Launch exe**: the game executable used to play your maps. Custom maps need a game client that can load them. If
  you leave this empty, IW8 Radiant picks a suitable client in the game folder.

IW8 Radiant includes its own build tools, so the game folder is the only path you need to set. You can change the paths
later in **Tools > Tool Paths**.

### The main window

- **Camera**: the 3D view.
- **Grid**: the 2D views. **Ctrl+Tab** switches between top, front and side.
- **Entity** inspector, **Surface** inspector and **Layers** on the right.
- **Side bar** on the left: the asset browsers (Materials, Models, Prefabs, Effects, Sounds, Images, Spawns and Undo
  History). Click an icon to open a panel and click it again to close it.
- **Bottom panel**: the Console, Compile log, game log, Build Budget, Script editor and Live Link. **Ctrl+J** shows or
  hides it.

Panels can be dragged, docked and floated. **View > Reset Panel Layout** restores the default layout, and
**View > Workspaces** saves layouts you use often.

## 2. Navigating and selecting

| Action | Input |
|---|---|
| Fly the camera | Hold the right mouse button and use W, A, S, D, Q and E |
| Change the camera speed | Mouse wheel while flying |
| Pan and zoom a 2D view | Right or middle mouse drag, mouse wheel |
| Select in the camera or a 2D view | Left click; Shift+click to add or remove |
| Select a face | Ctrl+Shift+click |
| Move the selection | Drag inside the selection |
| Frame the selection | End |
| Clone, delete, deselect | Space, Backspace, Esc |
| Grid size | `~` and 1 to 9, or `[` and `]` |
| Nudge | Alt+arrow keys |
| Hide and show | H, Shift+H |
| Camera bookmarks | Ctrl+1 to 9 to go, Ctrl+Shift+1 to 9 to save |

Hotkey presets for the stock CoD Radiant and iw3xo-radiant layouts are in **Edit > Hotkey Presets**. Individual keys
can be changed in **Edit > Keyboard Shortcuts**.

## 3. Building geometry

### Brushes

- Drag in a 2D view to create a box brush with the current material.
- Drag outside the selection to resize the nearest faces. Press **V** for vertex mode and **E** for edge mode.
- The toolbar has the **Clipper**, **CSG Hollow** and **CSG Subtract**. The **Brush** menu has extrude, chamfer,
  inset, bridge, arbitrary rotation and scale, mirror, array and radial duplicate, align and distribute.
- **Brush > Auto Caulk** applies caulk to faces that touch other brushes.
- **Ctrl+M** makes a brush detail, so it does not seal the map.

### Tool materials

**Brush > Tool Material** applies a tool material:

| Material | Effect in the game |
|---|---|
| caulk, nodraw | Invisible, still solid |
| clip | Blocks players and bullets |
| player clip | Blocks players only |
| weapon clip | Blocks bullets only |
| ladder | Invisible and climbable |
| water | Water volume |
| hint, skip | Invisible and not solid |
| sky | Draws the sky |

### Primitives and generators

The **Primitives** menu creates stairs, arches, cylinders, cones, spheres, ramps and prisms. **Generate Stairs** and
**Generate Building** fill the selected area with a staircase or a one- or two-storey building.

### Patches and terrain

- **Shift+P** creates a patch. The **Patch** menu edits rows and columns, caps, welds, thickens and inverts patches.
- **Patch > Create Terrain** creates a terrain mesh. The terrain brush raises, lowers, smooths and flattens it, and
  **Patch > Height Map** imports or exports a height map and applies erosion.
- **Patch > Road Through Selected Points** lays a road over the terrain.
- **Patch > Vertex Paint Mode** paints colour and blend values on patches.

## 4. Materials and textures

### Stock materials

Open the **Materials** panel to browse every stock material, or type a material name in the toolbar. Click a material
to apply it to the selection, or drag it onto a surface in the camera. Right-click a material for **Material Info**
and **Replace Everywhere With**.

The first time a stock material is used, its images are read from the game, so it can take a few seconds to appear.

### Custom textures and materials

- **File > Import Texture** creates a material from a PNG, TGA, JPG or BMP image.
- **File > New Custom Material** opens the Material Editor, where you set the colour, normal and specular images,
  the surface type, transparency, glow and tint. The preview updates as you work.
- Custom materials are stored in the `materials` folder next to your map.

### Live techsets and shader graphs

The **Techsets** icon, immediately after Materials in the upper sidebar, opens a large workspace docked in the
main window. **Back to map** restores the previous camera, grid and panel layout. The authoring and preview
columns have a draggable divider, and its position is remembered. **Document** contains New, Open and Recover draft.
You can also right-click materials, models, prefabs, effects and images and choose **Open in Techset Editor**.

The preview uses **Game Lighting**. It supports a sphere, plane, cube, models and prefabs, and effect sprites.
**Preview target > Selected map geometry** copies selected map geometry (including patches, terrain and prefab contents), or a picked stock
surface/model, into the dedicated preview without writing a temporary asset. Select a material slot below the preview to limit it. **Apply in map: Selected targets** temporarily applies
the same revision to the current map selection; **All uses of material** applies it to every use of the named
material. **Clear** restores the original rendering. None of these assignments changes the map file.

Use **Browse models** above the preview to open the model selector. Its search bar and category, kit, source,
favourites, recent and in-map filters narrow a large thumbnail grid. Select a tile and choose **Use model**, or
double-click it. Project OBJ/GLB/glTF models are included; choosing a preview never places it in the map. Cancel
leaves the current target unchanged. The LOD and material-slot controls remain beside the live Game Lighting view.

**Ctrl+S** saves the techset while this workspace has focus; **Ctrl+Enter** compiles it.

- **HLSL:** edit `iw8rPreviewVertex(inout IW8RPreviewVertex v)` and
  `float4 iw8rPreviewPixel(IW8RPreviewSurface s)`. Vertex inputs use world coordinates. `s.litColor` is the
  renderer's Game Lighting result; the default pixel function preserves it. Ctrl+Space completes interface names.
- **Nodes:** click an output dot and then an input dot to connect nodes. Tab opens the searchable node list;
  middle drag pans and the wheel zooms. Select nodes to copy, delete, undo, or extract a reusable subgraph.
  Double-click a subgraph to edit it. The inspector provides fields for common values; **Advanced properties**
  exposes the complete JSON. **Frame graph** fits the nodes into the canvas.
- **Material graphs:** the Surface output supplies colour, normal, coverage, cutoff, roughness, metallic and
  emission. Its material inputs run before Game Lighting; its Unlit property bypasses lighting for that output.
  Vertex graphs produce displacement. **Preview node** inspects an intermediate result; **Final output** restores
  the complete graph. Generated HLSL is read-only until copied explicitly into HLSL mode.
- **Technique:** add or duplicate supported slots, inspect the complete imported layout, and edit `previewState`
  for blend, culling, depth testing/writing, depth comparison, wireframe and depth bias. Native fields remain in
  the document even when the D3D11 preview cannot implement their game-specific behavior.
- **Resources / parameters:** declare named float4 parameters in slots 0–63 and texture/sampler bindings in slots
  0–7. The HLSL names are `iw8rParameters[slot]`, `iw8rTexture<slot>` and `iw8rSampler<slot>`. Parameter-table edits
  update the preview without recompiling. Textures support linear/point filtering, wrap/clamp/mirror addressing,
  and sRGB decoding. Cube inputs use a horizontal or vertical six-face strip in +X, -X, +Y, -Y, +Z, -Z order.

Auto Compile waits 350 ms after an edit and compiles on a background worker. **Diagnostics** lists errors and
warnings; activating an error navigates to its source line or graph node. A failed or superseded compile keeps
the last successful preview. Time nodes animate without recompilation.

Save editor authoring data as `.iw8techset.json`, normally under `techsets` beside the map. Standalone files are
also supported. Documents contain source, graphs, parameters and exact imported integers, including 64-bit flags
and hashes. **Recover draft** restores the latest unsaved recovery document from the application's data folder.
Texture paths can be relative to the document or absolute.

Stock imports search the owning zone and shared dependencies. Where the original HLSL is unavailable, the editor
shows the original stage reference and available bytecode disassembly. **Replace stage** creates an editable
preview template. Importing bytecode does not automatically execute it with guessed game bindings.

Live execution supports vertex and pixel stages and ordinary colour, depth and shadow adapters. Hull/domain,
tessellation, VRS, velocity and other unsupported variants are preserved and identified as unavailable for live
preview. This workspace is editor-only: it does not export new techsets into game zones or modify a running game.
The older map shader editor keeps its existing build behavior.

### Texture alignment

The **Surface** inspector fits, scales, shifts and rotates textures on the selected faces. **Brush > UV Editor**
shows the faces of the selected brushes unfolded, for precise placement.

## 5. Models

- Open the **Models** panel, then double-click a model or drag it into the camera to place it.
- **File > Import Model** adds your own OBJ, glTF or GLB model, with levels of detail and a collision mesh.
- Placed models are solid for players and bullets. Models used by scripts (with a `targetname`) are not.
- Useful keys: `lod_scale` changes the level-of-detail distance, `no_shadow` stops shadow casting, and
  `iw8r_material` replaces the model's material.
- **Tools > Scatter Models** places many models at once, with rules for slope, spacing and rotation.
- Right-click a model for **Model Info**: its size, materials, levels of detail and where it is used.

## 6. Prefabs

A prefab bundle is a folder that holds brushes, entities, an optional script and the prefab's own assets. A map
places it as a single object.

- Drag a prefab from the **Prefabs** panel into the camera, or double-click it.
- Select a placed prefab to edit its parameters in the Entity inspector.
- **Edit > Enter Prefab** (Ctrl+Shift+E) opens the prefab for editing. **Edit > Leave Prefab** (Ctrl+Shift+Q)
  returns to the map, and every placement shows the change.
- **File > Save Selection As Prefab** creates a new bundle from the selection.
- **File > Export Prefab Bundle As Zip** and **Import Prefab Bundles From Zip** share prefabs.

## 7. Entities and gameplay

### Entities

Select an entity to edit its keys in the **Entity** inspector. Each key shows its description and expected value,
and missing required keys can be added with one click. The **Links** panel shows `target` and `targetname`
connections. **Help > Entity Class Reference** describes every entity class.

### Spawns and game modes

- Place spawns from the **Spawns** panel, or use **Tools > Generate Spawns** to create a set over the walkable floors.
- **Primitives > Game Modes** inserts the objects for Domination, Hardpoint, Search and Destroy and Capture the Flag.
- After each build, IW8 Radiant lists the game modes your map supports:

| Mode | Required |
|---|---|
| Team Deathmatch | Two team spawns and a start spawn for each team |
| Free-for-all | Two free-for-all spawns |
| Domination | Domination spawns, both start spawns and three flags |
| Search and Destroy | Attacker and defender spawns, two bomb sites and the bomb |
| Capture the Flag | Spawns, both start spawns, and a flag, trigger and zone for each team |

### Behaviours without scripting

These keys add behaviour without writing a script:

| Key | Entity | Behaviour |
|---|---|---|
| `iw8r_spin`, `iw8r_bob` | Model or brush model | Spins or bobs continuously |
| `iw8r_teleport` | Trigger | Teleports players to a `script_origin` |
| `iw8r_jump` | Trigger | Launches players, for example `0 0 700` |
| `iw8r_damage` | Trigger | Damages players inside, for example `10 1` |
| `iw8r_lift` | Brush model | Moves up, waits and returns |
| `iw8r_door` | Brush model | Opens as a sliding or rotating door |

**Primitives > Scripted Objects** also creates hinged doors, breakable glass, ladders and destructible objects.
**View > Behaviour Preview** shows moving objects in the editor.

### Scripting

**File > New Script** creates a script for your map. The **Script** panel provides syntax highlighting, completion and
checking, and **Insert Behaviour** adds templates such as doors, lifts, teleporters and HUD messages. Errors in the
build log open the script at the matching line.

## 8. Lighting and atmosphere

### Lights

Place `light`, `light_spot` or `light_omni` entities and set their colour, intensity, radius, cone and shadows in the
Entity inspector. **Tools > IES Profile Browser** applies light profiles to spot lights.

### Sun, sky and atmosphere

- **Compile > Sun & Sky** sets the sun direction, colour and the sky. The sun can be aimed at the sun in the sky
  automatically.
- **Compile > Time of Day** applies presets such as Clear Day, Dusk and Night.
- **Compile > Atmosphere** sets fog and colour grading. **Compile > Vision Editor** gives access to every vision
  setting.
- **Compile > Sky From Panorama** creates a custom sky from a panoramic image.

### Previewing light

- **Ctrl+L** turns on the lit preview with the sun, lights and shadows.
- **View > Game Lighting** (Ctrl+Alt+L) shades the view the way the game will, including baked lighting when
  **Compile > Bake lightmaps** is enabled.
- **Compile > Capture Reflection Probes** captures the reflections for `reflection_probe` volumes.

## 9. Effects and sound

- Drag an effect from the **Effects** panel into the camera to place an `fx_origin`. Set `delay` to repeat it, or
  leave it empty for a continuous effect. **View > Effect Preview in Camera** plays effects in the editor.
- **Tools > Effect Editor** creates your own particle effects.
- Drag a sound from the **Sounds** panel to place a `sound_emitter`. Your own WAV and MP3 files in the `sounds`
  folder next to the map become sounds of the map, and **Tools > Sound Settings** sets their volume and range.
- Trigger brushes with `iw8r_audio_zone` change the reverb while players are inside.

## 10. Previewing your map

Press **P** in the camera to walk through the map:

| Action | Key |
|---|---|
| Move | W, A, S, D |
| Jump and mantle | Space |
| Sprint | Shift |
| Crouch, slide | C |
| Prone | Z |
| Use | F |
| Noclip | N |
| Leave | Esc |

Collision follows the same rules as the game, and doors, lifts and other behaviours work as they will in the game.

## 11. Building and playing

| Command | Key | Result |
|---|---|---|
| Compile Map | F7 | Builds the map and checks every zone |
| Compile and Deploy | Ctrl+F7 | Builds the map and installs it into the game's custom map folder |
| Play | F5 | Builds, installs, starts the game and loads the map |

- The **Compile** panel shows every step. Warnings do not stop the build.
- **Tools > Check Map** lists problems before you build, and **Tools > Leak Check** shows where the map is open to
  the void.
- The **Build Budget** panel compares your map with the game's limits.
- **Compile > Undo Last Deploy** restores the previously installed version of the map.
- **Map Info** in the Compile panel sets the map's title, description, minimap and supported game modes.
- **Compile > Set Loadscreen From Camera** uses the current view as the loading screen.
- Large maps are built with the game's streamed collision layout automatically.

## 12. Editing stock maps

1. **File > Open IW8 Stock Map** opens a stock map. Its entities become editable, and its world and props are shown
   as a reference.
2. Edit entities and spawns (**File > Stock Map > Edit Compiled Spawns**), add models and brushes, or hide props
   (**File > Stock Map > Hide Stock Props In Selection**).
3. The **Stock Map** panel changes the sun, sky and textures; the view updates immediately.
4. **File > Stock Map > Rebuild Stock Zone** writes the changed map in a few seconds.
5. **File > Stock Map > Install Edited Stock Map Into Game** installs it. The original files are kept, and
   **Restore Stock Map In Game** puts them back. Close the game before installing or restoring.

Always install edited stock maps with this command rather than copying files by hand.

## 13. Warzone

- **Tools > Game Modes** lists game mode presets, including Warzone Trios, Quads, Stimulus Solos and Blood Money.
  **Use for F5** starts that mode with F5, and **Carry in this map** stores the mode in the map.
- On your own map, `iw8r_loot_point` entities mark where loot appears, a `scriptable` entity with `scriptable` set to
  `br_plunder_box` adds a buy station, and vehicle spawn entities add vehicles.
- On stock Warzone maps, **File > Stock Map > Edit Loot Points** and **Edit Placed Objects** make loot points and buy
  stations editable; rebuild and install the map as described above.

## 14. Live Link

The **Live Link** panel connects to a running game:

- **Read camera** moves the editor camera to the player's view, and **Move player** moves the player to the editor
  camera.
- **Follow game** makes the editor camera follow the player.
- A console line sends commands to the game.
- Moved models, effects, sounds and sun changes are sent to the game without rebuilding.
- **Quick match** starts a private match on your map with bots.
- **Debug overlay** shows entity labels, trigger boxes and your position in the game.

## 15. Using the editor from a browser

**Tools > Web Server** lets you use IW8 Radiant from a web browser, including on a phone or tablet. The browser shows
the editor's live camera view, the 2D views, the entity list and inspector, and the main dialogs, and it can build and
play the map. The server is available only on your computer unless you enable network access in the web server
settings, which requires a password.

## 16. Troubleshooting

- **The editor crashed**: a crash report is saved in `%LOCALAPPDATA%\IW8 Radiant\IW8 Radiant\crashes`. Include it
  when you report the problem.
- **Lost work**: unsaved changes are saved every five minutes. Use **File > Autosave History / Recover**. Every save also keeps the previous version as a `.bak` file.
- **A texture or model is missing from a build**: the editor was still reading it from the game. Build again.
- **Deploy failed**: the game has the map loaded. Load another map in the game, then deploy again.
- **Script errors**: double-click the error in the Compile log to open the script at that line.
- **Settings problems**: hold Shift while starting the editor, or start it with `--safe-mode`, to use the default
  settings. **Help > Reset All Settings** resets them permanently.
- **Logs**: **View > Console** shows the editor's messages, and **Compile > Show Replay Log** shows the game's logs.
